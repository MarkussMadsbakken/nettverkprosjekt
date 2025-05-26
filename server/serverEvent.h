#ifndef NETTVERKPROSJEKT_SERVEREVENT_H
#define NETTVERKPROSJEKT_SERVEREVENT_H

#include "../models/packet.h"
#include <functional>
#include <iostream>
#include <SFML/System/Vector2.hpp>
#include <nlohmann/json.hpp>

template <typename T>
struct server_response_actions{
    std::function<void(const T &response)> accept;
    std::function<void(const T &response)> reject;
};

class IServerEvent{
public:
    IServerEvent() = default;
    virtual ~IServerEvent() = default;
    virtual void receive_event(const Packet &packet) = 0;

    void set_broadcast_fn(const std::function<void(const Packet &packet)> &fn){
        broadcast_fn = fn;
    }

protected:
    std::function<void(const Packet &packet)> broadcast_fn;
};

template <typename T>
class ServerEvent: public IServerEvent{
public:
    ServerEvent() = default;
    explicit ServerEvent(const std::function<void(const T &data, const server_response_actions<T> &actions)> &event_callback): on_receive_listener(event_callback){};
    virtual ~ServerEvent() = default;

    virtual json serialize(const T &data) = 0;
    virtual T deserialize(const Packet &packet) = 0;

    // recieves an event
    virtual void receive_event(const Packet &packet) override{
        T value = deserialize(packet);

        if(!on_receive_listener){
            return;
        }

        // create actions
        // TODO: should be instantiated in the constructor
        server_response_actions<T> actions{
                [this, &packet](const T &content){
                    // accept by sending the same packet id
                    this->broadcast_fn(Packet(packet.event, this->serialize(content), packet.packet_id));
                },
                [this, &packet](const T &content){
                    // reject by sending a packet_id of -1
                    this->broadcast_fn(Packet(packet.event, this->serialize(content), -1));
                },
        };

        on_receive_listener(value, actions);
    }

    // set a new event received callback
    void on_event_received(const std::function<void(const T &data, const server_response_actions<T> &actions)> &callback){
        on_receive_listener = callback;
    }

protected:
    std::function<void(const T &data, const server_response_actions<T> &actions)> on_receive_listener;
};

namespace ServerEvents {
    class Json: public ServerEvent<json>{
    public:
        Json(const std::function<void(const json &data, const server_response_actions<json> &actions)> &callback): ServerEvent<json>(callback){}

        json serialize(const json &data) override{
            return data;
        }

        json deserialize(const Packet &packet) override{
            return packet.content;
        }
    };

    class Vector2f : public ServerEvent<sf::Vector2f> {
    public:
        Vector2f(const std::function<void(const sf::Vector2f &data, const server_response_actions<sf::Vector2f> &actions)> &callback): ServerEvent<sf::Vector2f>(callback){}

        json serialize(const sf::Vector2f &vec) override {
            nlohmann::json data{
                    {"x", vec.x},
                    {"y", vec.y}
            };

            return data;
        }

        sf::Vector2f deserialize(const Packet &packet) override {
            return {
                    packet.content["x"],
                    packet.content["y"]
            };
        }
    };
}

#endif //NETTVERKPROSJEKT_SERVEREVENT_H