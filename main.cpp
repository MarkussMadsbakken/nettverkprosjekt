#include <iostream>
#include <nlohmann/json.hpp>
#include "server/netServer.cpp"
#include "client/netClient.cpp"
#include <SFML/Graphics.hpp>
#include <boost/asio.hpp>

using json = nlohmann::json;

class ExamplePlayer{
public:
    ExamplePlayer(NetClient &client, bool is_player_1, const bool &game_is_paused): is_player_1(is_player_1), game_is_paused(game_is_paused){
        std::string event_name = is_player_1 ? "bluemove" : "redmove";
        std::string other_event_name = is_player_1 ? "redmove" : "bluemove";
        this_client_move = client.add_event(event_name, Events::Interpolated::Vector2f());
        other_client_move = client.add_event(other_event_name, Events::Interpolated::Vector2f(Events::Interpolated::Interpolate));
    }

    std::pair<sf::Vector2f, sf::Vector2f> get_event_values(){
        return {this_client_move->get_current_value(), other_client_move->get_current_value()};
    }

    void push_move(const sf::Vector2f value){
        this_client_move->send(value);
    }

    void handle_player_1_move(float x, float y){
        // do no valiation

        auto value = this_client_move->get_current_value();
        value.x += x;
        value.y += y;
        push_move(value);
    }

    void handle_player_2_move(float x, float y){
        if(game_is_paused){
            return;
        }

        auto value = this_client_move->get_current_value();
        value.x += x;
        value.y += y;

        // validate the move
        if(value.x > 300){
            value.x = 300;
        }

        push_move(value);
    }

    void handle_keypress(){
        if(is_player_1) {
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left)) {
                handle_player_1_move(-5, 0);
            }

            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right)) {
                handle_player_1_move(5, 0);
            }

            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up)) {
                handle_player_1_move(0, -5);
            }

            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down)) {
                handle_player_1_move(0, 5);
            }
            return;
        }

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A)) {
            handle_player_2_move(-5, 0);
        }

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D)) {
            handle_player_2_move(5, 0);
        }

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W)) {
            handle_player_2_move(0, -5);
        }

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S)) {
            handle_player_2_move(0, 5);
        }

    }

    std::shared_ptr<Events::Interpolated::Vector2f> this_client_move;
    std::shared_ptr<Events::Interpolated::Vector2f> other_client_move;
    bool is_player_1;
    const bool &game_is_paused;
};


int main() {
    boost::asio::io_context event_loop(1);
    NetServer server(event_loop, 3000);

    bool game_is_paused = false;
    sf::Vector2f red_pos_server(0, 0);
    sf::Vector2f blue_pos_server(0, 0);

    server.add_event("redmove", [&server, &game_is_paused, &red_pos_server](const json &message, const NetServer::server_response_actions &actions){
        auto [accept, reject] = actions;

        if(game_is_paused){
            json new_message = {
                    {"x", red_pos_server.x},
                    {"y", red_pos_server.y}
            };
            red_pos_server = sf::Vector2f(new_message["x"].template get<float>(), new_message["y"].template get<float>());
            reject(new_message);
            return;
        }

        if(message["x"].template get<float>() > 300){
            json new_message = message;
            new_message["x"] = 300;
            reject(new_message);
            red_pos_server = sf::Vector2f(new_message["x"].template get<float>(), new_message["y"].template get<float>());
            return;
        }


        red_pos_server = sf::Vector2f(message["x"].template get<float>(), message["y"].template get<float>());
        accept(message);
    });

    server.add_event("bluemove", [&server, &game_is_paused, &blue_pos_server](const json &message, const NetServer::server_response_actions &actions){
        auto [accept, reject] = actions;
        if(game_is_paused){
            json new_message = {
                {"x", blue_pos_server.x},
                {"y", blue_pos_server.y}
            };
            blue_pos_server = sf::Vector2f(new_message["x"].template get<float>(), new_message["y"].template get<float>());
            reject(new_message);
            return;
        }

        if(message["x"].template get<float>() > 300){
            json new_message = message;
            new_message["x"] = 300;
            reject(new_message);
            blue_pos_server = sf::Vector2f(new_message["x"].template get<float>(), new_message["y"].template get<float>());
            return;
        }

        blue_pos_server = sf::Vector2f(message["x"].template get<float>(), message["y"].template get<float>());
        accept(message);
    });

    boost::asio::co_spawn(event_loop, server.start(), boost::asio::detached);

    NetClient client(event_loop, "localhost", 3000);
    boost::asio::co_spawn(event_loop, client.start(), boost::asio::detached);

    NetClient client2(event_loop, "localhost", 3000);
    boost::asio::co_spawn(event_loop, client2.start(), boost::asio::detached);

    ExamplePlayer blue_player(client, true, game_is_paused);
    ExamplePlayer red_player(client2, false, game_is_paused);

    sf::RenderWindow window(sf::VideoMode({800, 600}), "My window");
    window.setFramerateLimit(60);

    sf::Font font("inter.ttf");
    sf::Text ping_text(font);
    sf::Text ping_text_2(font);
    sf::Text tick_rate_text(font);
    sf::Text pause_text(font);

    ping_text.setCharacterSize(24);
    ping_text.setFillColor(sf::Color::Black);
    ping_text.setPosition({0, 0});

    ping_text_2.setCharacterSize(24);
    ping_text_2.setFillColor(sf::Color::Black);
    ping_text_2.setPosition({0, 25});

    tick_rate_text.setCharacterSize(24);
    tick_rate_text.setFillColor(sf::Color::Black);
    tick_rate_text.setPosition({0, 50});

    pause_text.setCharacterSize(24);
    pause_text.setString("Game is paused!");
    pause_text.setFillColor(sf::Color::Red);
    pause_text.setPosition({0, 75});

    sf::RectangleShape red_rect({50, 50});
    red_rect.setFillColor(sf::Color::Red);
    sf::RectangleShape blue_rect({50, 50});
    blue_rect.setFillColor(sf::Color::Blue);
    sf::RectangleShape red_est_rect({50, 50});
    red_est_rect.setFillColor(sf::Color::Yellow);
    sf::RectangleShape blue_est_rect({50, 50});
    blue_est_rect.setFillColor(sf::Color::Yellow);

    sf::RectangleShape right_wall({25, 600});
    right_wall.setFillColor(sf::Color::Black);
    right_wall.setPosition({350, 0});

    std::thread eventLoopThread([&event_loop]() {
        event_loop.run();
    });

    int pause_cooldown = 0;


    while (window.isOpen()) {
        while (const std::optional event = window.pollEvent()){
            if (event->is<sf::Event::Closed>()) window.close();
        }

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Escape)){
            window.close();
        }

        if(pause_cooldown > 0) {
            pause_cooldown--;
        }

        if(sf::Keyboard::isKeyPressed(sf::Keyboard::Key::P)){
            if(pause_cooldown == 0){
                game_is_paused = !game_is_paused;

                // manual cooldown
                pause_cooldown = 30;
            }
        }

        blue_player.handle_keypress();
        auto blue_player_values = blue_player.get_event_values();
        blue_rect.setPosition(blue_player_values.first);
        red_est_rect.setPosition(blue_player_values.second);

        red_player.handle_keypress();
        auto red_player_values = red_player.get_event_values();
        red_rect.setPosition(red_player_values.first);
        blue_est_rect.setPosition(red_player_values.second);

        window.clear(sf::Color::White);
        ping_text.setString("client 1 ping: " + std::to_string(client.get_ping()) + "ms");
        ping_text_2.setString("client 2 ping: " + std::to_string(client2.get_ping()) + "ms");
        tick_rate_text.setString("server tick rate: " + std::to_string(client.get_tick_rate()));

        window.draw(ping_text);
        window.draw(ping_text_2);
        window.draw(tick_rate_text);
        window.draw(red_rect);
        window.draw(blue_rect);
        window.draw(red_est_rect);
        window.draw(blue_est_rect);
        window.draw(right_wall);

        if(game_is_paused){
            window.draw(pause_text);
        }

        window.display();
    }

    event_loop.stop();
    if (eventLoopThread.joinable()) {
        eventLoopThread.join();
    }
}
