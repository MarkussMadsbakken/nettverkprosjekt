#ifndef NETTVERKPROSJEKT_PACKET_H
#define NETTVERKPROSJEKT_PACKET_H

#include <nlohmann/json.hpp>
#include <any>
#include "error.cpp"

using json = nlohmann::json;

const std::string EVENT_SEPARATOR = ";";
const std::string ID_SEPARATOR = ":";

class Packet {
public:
    json content;
    std::string event;
    int packet_id = 0;

    Packet(const std::string &data) {

        // separate the event from the internal data
        std::size_t separator_pos = data.find(EVENT_SEPARATOR);
        if (separator_pos == std::string::npos) {
            throw BadEventFormatException();
        }

        // get the packet headers
        std::string event_id_part = data.substr(0, separator_pos);
        std::size_t id_separator_pos = event_id_part.find(ID_SEPARATOR);
        if (id_separator_pos == std::string::npos) {
            throw BadEventFormatException();
        }

        // extract packet headers
        event = event_id_part.substr(0, id_separator_pos);
        std::string id_str = event_id_part.substr(id_separator_pos + 1);
        try {
            packet_id = std::stoul(id_str);
        } catch (...) {
            throw BadEventFormatException();
        }

        // parse the JSON content from the remaining string
        std::string json_data = data.substr(separator_pos + 1);
        content = json::parse(json_data);
    }

    Packet(const std::string &event, json data, int packet_id): event(event), content(data), packet_id(packet_id) {}
    Packet(const std::string &event, json data): event(event), content(data) {}

    std::string package_to_request() const {
        return event + ID_SEPARATOR + std::to_string(packet_id) + EVENT_SEPARATOR + content.dump();
    }
};

#endif //NETTVERKPROSJEKT_PACKET_H
