#ifndef NETTVERKPROSJEKT_EVENTPOOL_H
#define NETTVERKPROSJEKT_EVENTPOOL_H

#include <chrono>
#include "../models/packet.h"
#include <iostream>
#include <thread>

class EventPool {
public:
    EventPool(){}

    struct pooled_event{
        std::chrono::time_point<std::chrono::high_resolution_clock> insertion_time;
        std::chrono::time_point<std::chrono::high_resolution_clock> last_insertion_time;
        Packet packet;
        bool is_scheduled;
    };

    // adds an element to the pool.
    void pool(const Packet &packet){
        auto now = std::chrono::high_resolution_clock::now();

        auto lock = acquire_event_pool();
        auto event = get_pooled_element(packet);

        event->last_insertion_time = now;

        // event pool is triggered, update information
        if(now - event->insertion_time > event_pool_trigger && !event->is_scheduled){
            // event pool is not triggered, update insertion time
            event->insertion_time = now;

            // trigger listeners and return;
            trigger_pool_listeners(packet);
            return;
        }

        // pool is triggered
        event->packet = packet;

        if(event->is_scheduled){
            // pool is already scheduled, do nothing
            return;
        }

        event->is_scheduled = true;

        // schedule timeout for event trigger
        std::thread([this, event](){
            std::this_thread::sleep_for(event_pool_timeout);

            // lock the event pool while the event is accessed
            auto lock = this->acquire_event_pool();
            event->is_scheduled = false;
            trigger_pool_listeners(event->packet);
        }).detach();

    }

    // gets a pooled event if it exists, creates it if doesnt.
    std::shared_ptr<pooled_event> get_pooled_element(const Packet &packet){
        if(event_pool.find(packet.event) != event_pool.end()){
            return event_pool.at(packet.event);
        }
        auto value = event_pool.emplace(packet.event, std::make_shared<pooled_event>(
            pooled_event{
                    std::chrono::high_resolution_clock::now(),
                    std::chrono::high_resolution_clock::now(),
                    packet
            }
        ));

        return value.first->second;
    }
    
    std::lock_guard<std::mutex> acquire_event_pool(){
        return std::lock_guard<std::mutex>(event_pool_lock);
    }

    void add_pool_listener(const std::function<void(Packet)> &listener){
        pool_trigger_listeners.push_back(listener);
    }

    void set_event_pool_timeout(const std::chrono::milliseconds &timeout){
        event_pool_timeout = timeout;
        event_pool_trigger = timeout / 2; // maybe a good constant?
    }

private:
    // pool timing
    std::chrono::milliseconds event_pool_trigger = std::chrono::milliseconds(100);
    std::chrono::milliseconds event_pool_timeout = std::chrono::milliseconds(200);

    // mutex pool
    std::unordered_map<std::string, std::shared_ptr<pooled_event>> event_pool;
    std::mutex event_pool_lock;

    // event trigger listeners
    std::vector<std::function<void(Packet)>> pool_trigger_listeners;

    void trigger_pool_listeners(const Packet &packet){
        for(auto &listener: pool_trigger_listeners){
            listener(packet);
        }
    }
};


#endif //NETTVERKPROSJEKT_EVENTPOOL_H
