#ifndef NETTVERKPROSJEKT_CONNECTIONMANAGER_H
#define NETTVERKPROSJEKT_CONNECTIONMANAGER_H

#include <unordered_map>
#include <boost/asio.hpp>

class ConnectionManager {
public:
    ConnectionManager(unsigned int connection_timeout): connection_timeout(std::chrono::seconds(connection_timeout)){};

    // a server connection
    struct connection {
        std::chrono::time_point<std::chrono::high_resolution_clock> last_ping;
        boost::asio::ip::udp::endpoint endpoint;
    };

    // add a new connection
    unsigned int add_connection(const boost::asio::ip::udp::endpoint &endpoint) {
        unsigned int id = generate_id();
        connections.insert({id, {std::chrono::high_resolution_clock::now(), endpoint}});
        return id;
    };

    // updates the last known client ping
    void update_ping(unsigned int id){
        connections.find(id)->second.last_ping = std::chrono::high_resolution_clock::now();
    }

    // removes all connections that have expired
    void cleanup_expired_connections(){
        auto now = std::chrono::high_resolution_clock::now();

        for (auto it = connections.begin(); it != connections.end(); ) {
            if ((now - it->second.last_ping) > connection_timeout) {
                it = connections.erase(it);
            } else {
                ++it;
            }
        }
    }

    // gets all connections
    const std::unordered_map<unsigned int, connection> get_connections(){
        return connections;
    }

private:
    std::unordered_map<unsigned int, connection> connections;
    unsigned int next_id = 1;
    std::chrono::seconds connection_timeout;

    unsigned int generate_id(){
        return next_id++;
    }
};


#endif //NETTVERKPROSJEKT_CONNECTIONMANAGER_H
