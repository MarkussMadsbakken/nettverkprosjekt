#ifndef NETTVERKPROSJEKT_EVENT_H
#define NETTVERKPROSJEKT_EVENT_H

#include "../models/packet.h"
#include <functional>
#include <iostream>
#include <boost/circular_buffer.hpp>
#include <SFML/System/Vector2.hpp>
#include <nlohmann/json.hpp>
#include <queue>
#include "interpolation.h"


class IEvent{
public:
    std::string event_id;

    IEvent() = default;
    virtual ~IEvent() = default;

    virtual Packet serialize_any(const std::any &data) = 0;
    virtual std::any deserialize_any(const Packet &packet) = 0;
    virtual void receive_event(const Packet &packet) = 0;

    void set_event_id(const std::string &id){
        event_id = id;
    }

    void on_send(const std::function<void(const Packet &packet)> &callback){
        send_listener = callback;
    }

protected:
    std::function<void(const Packet &packet)> send_listener;

    void notify_send_listener(const Packet &packet){
        send_listener(packet);
    }
};

template <typename T>
class Event: public IEvent{
public:

    Event() = default;
    explicit Event(const std::function<void(T)> &event_callback):  on_receive_listener(event_callback){};
    virtual ~Event() = default;

    virtual Packet serialize(const T &data) = 0;
    virtual T deserialize(const Packet &packet) = 0;

    Packet serialize_any(const std::any &data) override {
        return serialize(std::any_cast<T>(data));
    }

    std::any deserialize_any(const Packet &packet) override {
        return deserialize(packet);
    }

    void send(const T &data){
        Packet packet = serialize(data);

        before_send(packet);
        notify_send_listener(serialize(data));
    }

    virtual void receive_event(const Packet &packet) override{
        T value = deserialize(packet);
        latest_value = value;

        if(!on_receive_listener){
            return;
        }

        on_receive_listener(value);
    }

    void on_event_received(const std::function<void(T)> &callback){
        on_receive_listener = callback;
    }

    virtual std::optional<T> get_latest_value() {
        return latest_value;
    }

protected:
    T latest_value;
    std::function<void(T)> on_receive_listener;


    // function that triggers right before send
    virtual void before_send(const Packet &packet){}
};

namespace Events{
class Vector2f: public Event<sf::Vector2f>{
public:
    Packet serialize(const sf::Vector2f &vec) override {
        nlohmann::json data{
            {"x", vec.x},
            {"y", vec.y}
        };

        return {this->event_id, data};
    }

    sf::Vector2f deserialize(const Packet &packet) override {
        return {
            packet.content["x"],
            packet.content["y"]
        };
    }
};

class Json: public Event<nlohmann::json>{
public:
    Json(): Event<nlohmann::json>(){};
    Json(const std::function<void(nlohmann::json)> &event_callback): Event<nlohmann::json>(event_callback) {}

    Packet serialize(const nlohmann::json &data) override {
        return {this->event_id, data};
    }

    nlohmann::json deserialize(const Packet &packet) override {
        return packet.content;
    }
};
}

// Interpolated events are meant for continuous actions
namespace Events::Interpolated {
    class ClientSidePredictToken {
    public:
        constexpr ClientSidePredictToken(bool use_predict) : use_predict_(use_predict) {}

        constexpr bool use_predict() const { return use_predict_; }

    private:
        bool use_predict_;
    };

    inline constexpr ClientSidePredictToken AssumeAccepted{true};
    inline constexpr ClientSidePredictToken Interpolate{false};

    template<typename T>
    class InterpolatedEventBase : public Event<T> {
    public:
        struct event_value {
            std::chrono::time_point<std::chrono::high_resolution_clock> time;
            T raw_value;
        };

        InterpolatedEventBase(const T &initial_value): clientSidePredictToken(true), interpolator(initial_value) {
            interpolator.set_stiffness(interpolator.get_tick_rate_stiffness(5));
        };
        InterpolatedEventBase(Events::Interpolated::ClientSidePredictToken token, const T &initial_value): clientSidePredictToken(token), interpolator(initial_value) {
            interpolator.set_stiffness(interpolator.get_tick_rate_stiffness(5));
        }

        void receive_event(const Packet &packet) override {
            T value = this->deserialize(packet);

            this->latest_value = value;
            this->interpolator.update_target(value);

            if (!this->accept_event(packet)) {
                // event is not accepted.
                // stop all interpolation/prediction
                current_value = value;
            }

            this->last_event_received = std::chrono::high_resolution_clock::now();

            if (!this->on_receive_listener) {
                return;
            }

            this->on_receive_listener(value);
        }

        // serialize is not to be overridden, because it needs to generate event ids.
        // use serialize_impl instead for subclasses.
        Packet serialize(const T &data) override final {
            Packet packet = serialize_impl(data);
            packet.packet_id = generate_event_id();
            return packet;
        }

        virtual Packet serialize_impl(const T &data) = 0;

        virtual T get_current_value(){
            if(!clientSidePredictToken.use_predict()){
                current_value = interpolator.update();
                return current_value;
            }
            return current_value;
        }

    protected:
        std::deque<int> expected_packets;
        T current_value;
        std::chrono::milliseconds interpolation_duration = std::chrono::milliseconds(10000);
        std::chrono::time_point<std::chrono::high_resolution_clock> last_event_received;
        int last_event_id = 0;
        Events::Interpolated::ClientSidePredictToken clientSidePredictToken;
        Interpolator<T> interpolator;

        void before_send(const Packet &packet) override {
            push_expected_packet(packet);
            if(clientSidePredictToken.use_predict()){
                current_value = this->deserialize(packet);
            }
        }

    private:
        // checks if a given packet should be considered accepted/contains values we have expected to be true
        bool accept_event(const Packet &packet){
            // packet was rejected
            if (packet.packet_id < 0) {
                return false;
            }

            // an unexpected value has returned.
            if (packet.packet_id > last_event_id) {
                expected_packets.clear();
            }

            // clear earlier non-acknowledged packets
            while (!expected_packets.empty() && expected_packets.front() < packet.packet_id) {
                expected_packets.pop_front();
            }

            return true;
        }

        int generate_event_id() {
            // prevent event_id from overflowing
            if (last_event_id >= std::numeric_limits<int>::max()) {
                last_event_id = 0;
            }

            return ++last_event_id;
        }

        void push_expected_packet(const Packet &packet) {
            expected_packets.push_back(packet.packet_id);
        }
    };

    class Vector2f : public InterpolatedEventBase<sf::Vector2f> {
    public:
        Vector2f(): InterpolatedEventBase<sf::Vector2f>(sf::Vector2f(0, 0)) {}
        Vector2f(Events::Interpolated::ClientSidePredictToken token): InterpolatedEventBase<sf::Vector2f>(token, sf::Vector2f(0, 0)) {}


        Packet serialize_impl(const sf::Vector2f &vec) override {
            nlohmann::json data{
                    {"x", vec.x},
                    {"y", vec.y}
            };
            return {this->event_id, data};
        }

        sf::Vector2f deserialize(const Packet &packet) override {
            return {
                    packet.content["x"],
                    packet.content["y"]
            };
        }
    };
}

#endif //NETTVERKPROSJEKT_EVENT_H
