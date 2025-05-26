#ifndef NETTVERKPROSJEKT_EVENTPROCESSOR_H
#define NETTVERKPROSJEKT_EVENTPROCESSOR_H

#include "../models/packet.cpp"
#include <vector>
#include <mutex>
#include <boost/asio.hpp>
#include <iostream>

class EventProcessor {
public:
    EventProcessor(const std::function<void(const Packet &packet)> &processor_fn): processor_fn(processor_fn), io_context(), work_guard(boost::asio::make_work_guard(io_context)){}

    ~EventProcessor() {
        stop();
    }

    // queues a new packet for processing
    void queue_packet(const Packet &packet){
        auto lock = acquire_packet_queue();
        packet_queue.push_back(packet);
    }

    // set the tick rate
    void set_tick_rate(float tick_rate){
        if(tick_rate <= 0){
            throw std::invalid_argument("tick rate cannot be negative or 0");
        }

        ideal_tick_rate = tick_rate;
    }

    // gets the measured tick rate
    float get_real_tickrate() const {
        return real_tick_rate;
    }

    // starts the eventProcessor on a separate worker thread
    void start() {
        thread = std::thread([this]() {
            boost::asio::co_spawn(io_context, this->start_internal(), boost::asio::detached);
            io_context.run();
        });
    }

    // Stop the event processor thread and io_context.
    void stop() {
        work_guard.reset();
        io_context.stop();
        if (thread.joinable()) {
            thread.join();
        }
    }

private:
    boost::asio::awaitable<void> start_internal(){
        // internal start event. Meant to be started on a separate thread.
        auto executor = co_await boost::asio::this_coro::executor;
        boost::asio::steady_timer timer(executor);

        auto tick_duration = std::chrono::milliseconds((int)(1000 / ideal_tick_rate));
        std::vector<Packet> packet_queue_copy;

        for (;;) {
            auto tick_start = std::chrono::steady_clock::now();

            // Copy packets to a new vector
            // Potentially bad for performance, but it allows events to be added to the new queue, even while events
            // are being processed.
            {
                auto lock = acquire_packet_queue();
                packet_queue_copy = packet_queue;
                packet_queue.clear();
            }

            // process packets. This can potentially be done on separate worker threads
            for (const auto &packet: packet_queue_copy) {
                processor_fn(packet);
            }

            //  calculate sleep duration
            auto elapsed = std::chrono::steady_clock::now() - tick_start;
            auto sleep_duration = tick_duration - std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);

            update_real_tick_rate(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());

            if (sleep_duration > std::chrono::milliseconds::zero()) {
                // we have processed faster than the tickrate, sleep
                timer.expires_after(sleep_duration);
                co_await timer.async_wait(boost::asio::use_awaitable);
            } else {
                // We are behind schedule
                co_await boost::asio::post(executor, boost::asio::use_awaitable);
            }
        }
    }

    // This could be replaced with the space_optimized circular buffer.
    // Would make the server drop packets if overloaded, instead of continuing to accept more events
    std::vector<Packet> packet_queue;
    std::mutex packet_queue_lock;
    std::function<void(const Packet &packet)> processor_fn;

    // Thread internals
    boost::asio::io_context io_context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard;
    std::thread thread;

    float real_tick_rate = 0;
    float ideal_tick_rate = 5;

    void update_real_tick_rate(float elapsed_time){
        if(elapsed_time > 0){
            real_tick_rate = 1000 / elapsed_time;

            // cap tick rate to ideal tick rate
            if(real_tick_rate > ideal_tick_rate){
                real_tick_rate = ideal_tick_rate;
            }
            return;
        }
        real_tick_rate = ideal_tick_rate;
    }

    std::lock_guard<std::mutex> acquire_packet_queue(){
        return std::lock_guard<std::mutex>(packet_queue_lock);
    }
};


#endif //NETTVERKPROSJEKT_EVENTPROCESSOR_H
