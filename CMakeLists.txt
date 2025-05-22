cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ZEPHYR_MQTT_NRF91)

target_sources(app
    PRIVATE
    src/main.c
)

# Add the component LTE
target_sources(app PRIVATE
    components/lte/lte.c)
target_include_directories(app
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/components/lte
)

# Add the component MQTT
target_sources(app PRIVATE
    components/mqtt/mqtt.c)
target_include_directories(app
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/components/mqtt
)

# Add the component CERTIFICATES
target_sources(app PRIVATE
    components/certs/certs.c)
target_include_directories(app
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/components/certs
)
