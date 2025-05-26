#ifndef NETTVERKPROSJEKT_ERROR_H
#define NETTVERKPROSJEKT_ERROR_H

#include <exception>

class BadEventFormatException: public std::exception {
public:
    [[nodiscard]] const char* what() const noexcept override {
        return "Could not parse packet: Bad format";
    }
};

#endif //NETTVERKPROSJEKT_ERROR_H
