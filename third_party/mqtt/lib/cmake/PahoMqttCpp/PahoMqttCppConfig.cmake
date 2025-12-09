# save build-time options
set(PAHO_BUILD_STATIC OFF)
set(PAHO_BUILD_SHARED ON)
set(PAHO_WITH_SSL ON)
set(PAHO_WITH_MQTT_C ON)

include(CMakeFindDependencyMacro)

find_dependency(Threads REQUIRED)

if (NOT PAHO_WITH_MQTT_C)
  find_dependency(eclipse-paho-mqtt-c REQUIRED)
endif()

if (PAHO_WITH_SSL)
  find_dependency(OpenSSL REQUIRED)
endif()

if(NOT TARGET PahoMqttCpp::paho-mqttpp3-shared AND NOT TARGET PahoMqttCpp::paho-mqttpp3-static)
    include("${CMAKE_CURRENT_LIST_DIR}/PahoMqttCppTargets.cmake")

    if(TARGET PahoMqttCpp::paho-mqttpp3-shared)
        add_library(PahoMqttCpp::paho-mqttpp3 ALIAS PahoMqttCpp::paho-mqttpp3-shared)
    else()
        add_library(PahoMqttCpp::paho-mqttpp3 ALIAS PahoMqttCpp::paho-mqttpp3-static)
    endif()
endif()
