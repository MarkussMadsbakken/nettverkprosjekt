#include <boost/asio.hpp>
#include <iostream>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "../models/packet.h"
#include "eventPool.h"
#include "event.h"

using namespace boost::asio::ip;
using json = nlohmann::json;

class NetClient {
public:
    struct ping_update{
        float tick_rate;
        int ping;
    };

    NetClient(boost::asio::io_context &io_context, const std::string &server_address, int server_port)
            : socket(io_context),
              ping_timer(io_context) {

        // setup endpoint
        boost::asio::ip::udp::resolver resolver(io_context);
        auto endpoints = resolver.resolve(server_address, std::to_string(server_port));
        server_endpoint = *endpoints.begin();

        // Add internal events
        add_internal_event("connect", [this](const json &message){
            unsigned int id = message["connection_id"].template get<unsigned int>();
            this->connection_id = id;
        });

        add_internal_event("ping", [this](const json &message){
            auto timestamp = std::stoll(message["client_timestamp"].template get<std::string>());
            auto time = std::chrono::system_clock::time_point(std::chrono::milliseconds(timestamp));

             ping = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - time).count();
             server_tick_rate = message["server_tick_rate"].template get<float>();

             // adjust event pool timing
             eventPool.set_event_pool_timeout(std::chrono::milliseconds((int)(2000 / server_tick_rate)));

             // push ping update
            push_ping_update({server_tick_rate, ping});
        });

        // setup event pool
        // TODO: this should really not be sent synchronously, but to fix it i would need to do a big refactoring
        eventPool.add_pool_listener([this](const Packet &packet){
            std::string message = packet.package_to_request();
            this->socket.send_to(boost::asio::buffer(message, message.length()), server_endpoint);
        });
    }

    // adds a new event to the client, in the form of a json callback
    void add_event(const std::string &command, const std::function<void(const json &message)> &function) {
        events.insert({command, std::make_shared<Events::Json>(Events::Json(function))});
    }

    // adds a new event to the client, and returns a pointer to it
    template <typename T, typename = std::enable_if_t<std::is_base_of_v<IEvent, std::decay_t<T>>>>
    std::shared_ptr<T> add_event(const std::string &command, T&& event) {
        auto event_pointer = std::make_shared<std::decay_t<T>>(std::forward<T>(event));

        // set id and send callback
        event_pointer->set_event_id(command);
        event_pointer->on_send([this](const Packet &packet){
            this->send(packet);
        });

        if(events.find(command) != events.end()){
            throw std::invalid_argument("The event " + command + " has already been added");
        }

        events.insert({command, event_pointer});
        return event_pointer;
    }

    // connects to the server
    boost::asio::awaitable<void> connect(){
        co_await send_async("!connect", json());
    }

    // Async events are not pooled!
    boost::asio::awaitable<void> send_async(const std::string &command, const json &content) {
        Packet packet(command, content);
        std::string message = packet.package_to_request();

        co_await socket.async_send_to(boost::asio::buffer(message, message.length()), server_endpoint, boost::asio::use_awaitable);
    }

    // sends a packet to the server
    void send(const Packet &packet){
        eventPool.pool(packet);
    }

    // sends event + content to the server
    void send(const std::string &command, const json &content){
        eventPool.pool({command, content});
    }

    // starts the client
    boost::asio::awaitable<void> start() {
        socket.open(udp::v6());
        co_await connect();
        schedule_ping();

        std::cout << "Client started" << std::endl;

        // main execution loop
        for (;;) {
            char buffer[max_udp_message_size];
            udp::endpoint sender_endpoint;
            auto bytes_transferred = co_await socket.async_receive_from(boost::asio::buffer(buffer, max_udp_message_size), sender_endpoint, boost::asio::use_awaitable);

            std::string message(buffer, bytes_transferred);

            // std::cout << "Client: recieved message " + message << std::endl;

            Packet packet(message);

            if (packet.event.starts_with('!')) {
                trigger_internal_event(packet);
                continue;
            }

            trigger_event(Packet(message));
        }
    }

    void on_ping_update(const std::function<void(ping_update)> &callback){
        ping_update_listeners.push_back(callback);
    }

    void push_ping_update(const ping_update &update){
        for(auto &listener: ping_update_listeners){
            listener(update);
        }
    }

    int get_ping(){
        return ping;
    }

    float get_tick_rate(){
        return server_tick_rate;
    }

private:
    udp::socket socket;
    udp::endpoint server_endpoint;
    boost::asio::steady_timer ping_timer;
    std::unordered_map<std::string, std::shared_ptr<IEvent>> events;
    std::unordered_map<std::string, std::function<void(const json &)>> internal_events;

    int ping;
    float server_tick_rate;
    std::vector<std::function<void(ping_update)>> ping_update_listeners;

    EventPool eventPool;

    std::optional<unsigned int> connection_id;
    static constexpr size_t max_udp_message_size = 0xffff - 20 - 8; // 16 bit UDP length field - 20 byte IP header - 8 byte UDP header

    void trigger_event(const Packet &packet){
        auto it = events.find(packet.event);
        if (it != events.end()) {
            it->second->receive_event(packet);
        } else {
            std::cerr << "Client: No event found for command " << packet.event << std::endl;
        }
    }

    void trigger_internal_event(const Packet &packet){
        auto it = internal_events.find(packet.event);

        if (it != internal_events.end()) {
            it->second(packet.content);
        } else {
            std::cerr << "No internal event found for command: " << packet.event << std::endl;
        }
    }

    void add_internal_event(const std::string &command, const std::function<void(const json &message)> &function){
        internal_events.insert({"!" + command, function});
    }

    void schedule_ping() {
        ping_timer.expires_after(std::chrono::seconds(1));
        ping_timer.async_wait([this](const boost::system::error_code &ec) {
            if (!ec) {
                send_ping();
                schedule_ping();
            }
        });
    }

    void send_ping() {
        if (connection_id.has_value()) {
            json ping_request = {
                    {"connection_id", connection_id},
                    {"client_timestamp", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count())}
            };
            co_spawn(socket.get_executor(), send_async("!ping", ping_request), boost::asio::detached);
        } else {
            std::cerr << "Ping cancelled: no connection" << std::endl;
        }
    }
};