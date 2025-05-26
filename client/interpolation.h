#ifndef NETTVERKPROSJEKT_INTERPOLATION_H
#define NETTVERKPROSJEKT_INTERPOLATION_H


#include <chrono>
#include <SFML/System/Vector2.hpp>
#include <concepts>

template<typename T>
concept Interpolateable = requires(T t, float f) {
    { t.length() } -> std::same_as<float>;
    { t.normalized()} -> std::same_as<T>;
    { t + t } -> std::same_as<T>;
    { t - t } -> std::same_as<T>;
    { t * f } -> std::same_as<T>;
};

template<Interpolateable T>
class Interpolator {
public:
    Interpolator(const T &initial_value): current_value(initial_value), target_value(initial_value), velocity(initial_value * 0.0f), last_update(std::chrono::steady_clock::now()), stiffness(1.0f) {
        damping = 2.0f * std::sqrt(stiffness);
    }

    void update_target(const T &new_target) {
        target_value = new_target;
    }

    T update() {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last_update).count();
        last_update = now;

        T delta = target_value - current_value;
        T acceleration = delta * stiffness - velocity * damping;
        velocity = velocity + acceleration * dt;
        current_value = current_value + velocity * dt;

        if ((target_value - current_value).length() < 0.01f) {
            current_value = target_value;
            velocity = current_value * 0.0f;
        }
        return current_value;
    }

    void set_stiffness(float new_stiffness){
        stiffness = new_stiffness;
        damping = 2.0f * std::sqrt(new_stiffness);
    }

    void set_velocity(const T &new_velocity) {
        velocity = new_velocity;
    }

    T current() const {
        return current_value;
    }

    static float get_tick_rate_stiffness(float tick_rate){
        return get_tick_rate_stiffness(tick_rate, 2);
    }

    static float get_tick_rate_stiffness(float tick_rate, float settle_in_ticks){
        float tau = settle_in_ticks / tick_rate;
        return 1.5f / (tau * tau);
    }

private:
    T current_value;
    T target_value;
    T velocity;
    std::chrono::time_point<std::chrono::steady_clock> last_update;

    float stiffness;
    float damping;
};

#endif //NETTVERKPROSJEKT_INTERPOLATION_H
