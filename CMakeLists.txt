cmake_minimum_required(VERSION 3.26)
project(nettverkprosjekt)

set(CMAKE_CXX_STANDARD 23)

add_executable(nettverkprosjekt main.cpp
        server/netServer.cpp
        client/netClient.cpp
        models/error.h
        models/packet.h
        server/connectionManager.h
        client/eventPool.h
        client/event.h
        client/interpolation.h
        server/eventProcessor.h
        server/serverEvent.h
)

# SFML
if(APPLE)
    find_package(SFML COMPONENTS System Window Graphics REQUIRED)
else()
    set(SFML_STATIC_LIBRARIES TRUE)
    set(SFML_DIR ${CMAKE_CURRENT_SOURCE_DIR}/SFML/lib/cmake/SFML)
    find_package(SFML COMPONENTS System Window Graphics REQUIRED)

    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/SFML/include)
endif()

target_link_libraries(nettverkprosjekt SFML::System SFML::Window SFML::Graphics)

# boost
find_package(Boost 1.54.0 COMPONENTS system REQUIRED)
include_directories(${Boost_INCLUDE_DIR})
target_link_libraries(nettverkprosjekt ${Boost_LIBRARIES})

# JSON serialization
find_package(nlohmann_json 3.12.0 REQUIRED)
target_link_libraries(nettverkprosjekt nlohmann_json::nlohmann_json)