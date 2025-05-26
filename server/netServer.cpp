#include <unordered_map>
#include <boost/asio.hpp>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../models/packet.h"
#include "connectionManager.h"
#include "eventProcessor.h"
#include "serverEvent.h"

using json = nlohmann::json;

class NetServer{
public:
    NetServer(boost::asio::io_context &io_context, int port): socket(io_context, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v6(), port)), connectionManager(10), cleanup_timer(socket.get_executor(), std::chrono::seconds(20)){

        // create the event processor
        eventProcessor = std::make_unique<EventProcessor>([this](const Packet &packet){
            this->trigger_event(packet);
        });

        setup_internal_events();

        schedule_cleanup();
    }

    template <typename T, typename = std::enable_if_t<std::is_base_of_v<IServerEvent, std::decay_t<T>>>>
    std::shared_ptr<T> add_event(const std::string &command, T&& event) {
        auto event_pointer = std::make_shared<std::decay_t<T>>(std::forward<T>(event));
        event_pointer->set_broadcast_fn([this](const Packet &packet){
           this->broadcast(packet);
        });

        events.insert({command, event_pointer});
        return event_pointer;
    }

    boost::asio::awaitable<void> handle_request(const boost::asio::ip::udp::endpoint &endpoint, const std::string &message) {
        // std::cout << "Server: received: " << message << " from " << endpoint.address() << ":" << endpoint.port() << std::endl;

        Packet packet(message);

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        if(packet.event.starts_with('!')){
            trigger_internal_event(endpoint, packet);
            co_return void();
        }

        // Non-internals get queued for execution
        eventProcessor->queue_packet(packet);
        co_return void();
    }

    // broadcasts a packet to all available clients
    void broadcast(const Packet &packet){
        std::string data = packet.package_to_request();

        for(auto &conn: connectionManager.get_connections()){
            socket.send_to(boost::asio::buffer(data, data.length()), conn.second.endpoint);
        }
    }

    // starts the server
    boost::asio::awaitable<void> start() {
        eventProcessor->start();

        std::cout << "Server started on port" << socket.local_endpoint() << std::endl;

        for (;;) {
            char buffer[max_udp_message_size];
            boost::asio::ip::udp::endpoint endpoint;
            auto bytes_transferred = co_await socket.async_receive_from(boost::asio::buffer(buffer, max_udp_message_size), endpoint, boost::asio::use_awaitable);

            std::string message(buffer, bytes_transferred);
            co_spawn(socket.get_executor(), handle_request(endpoint, message), boost::asio::detached);
        }
    }

private:
    ConnectionManager connectionManager;
    std::unique_ptr<EventProcessor> eventProcessor;
    boost::asio::ip::udp::socket socket;
    std::unordered_map<std::string, std::shared_ptr<IServerEvent>> events;
    std::unordered_map<std::string, std::function<void(boost::asio::ip::udp::endpoint, json)>> internal_events;
    boost::asio::steady_timer cleanup_timer;

    static constexpr size_t max_udp_message_size = 0xffff - 20 - 8; // 16 bit UDP length field - 20 byte IP header - 8 byte UDP header

    void trigger_event(const Packet &packet) {
        auto it = events.find(packet.event);
        if (it != events.end()) {
            it->second->receive_event(packet);
        } else {
            std::cerr << "No event found for command: " << packet.event << std::endl;
        }
    }

    void trigger_internal_event(const boost::asio::ip::udp::endpoint &endpoint, const Packet &packet){
        auto it = internal_events.find(packet.event);

        if (it != internal_events.end()) {
            it->second(endpoint, packet.content);
        } else {
            std::cerr << "No internal event found for command: " << packet.event << std::endl;
        }
    }

    void add_internal_event(const std::string &command, const std::function<void(boost::asio::ip::udp::endpoint, const json &message)> &function){
        internal_events.insert({"!" + command, function});
    }

    void setup_internal_events(){
        add_internal_event("ping", [this](const boost::asio::ip::udp::endpoint &endpoint, const json &message){
            int id = message["connection_id"].template get<int>();
            connectionManager.update_ping(id);

            json responseContent = {
                    {"client_timestamp", message["client_timestamp"].template get<std::string>()},
                    {"server_tick_rate", eventProcessor->get_real_tickrate()}
            };

            std::string res = Packet("!ping", responseContent).package_to_request();

            socket.send_to(boost::asio::buffer(res, res.length()), endpoint);
        });

        add_internal_event("connect", [this](const boost::asio::ip::udp::endpoint &endpoint, const json &message){
            auto id = connectionManager.add_connection(endpoint);
            json responseContent = {{
                                            "connection_id", id
                                    }};

            std::string res = Packet("!connect", responseContent).package_to_request();
            socket.send_to(boost::asio::buffer(res, res.length()), endpoint);
        });
    }

    void schedule_cleanup() {
        cleanup_timer.async_wait([this](const boost::system::error_code &ec) {
            if (!ec) {
                connectionManager.cleanup_expired_connections();
                cleanup_timer.expires_after(std::chrono::seconds(20));
                schedule_cleanup();
            }
        });
    }
};