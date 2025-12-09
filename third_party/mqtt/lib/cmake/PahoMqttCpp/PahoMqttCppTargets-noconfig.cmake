#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "PahoMqttCpp::paho-mqtt3a" for configuration ""
set_property(TARGET PahoMqttCpp::paho-mqtt3a APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(PahoMqttCpp::paho-mqtt3a PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libpaho-mqtt3a.so.1.3.14"
  IMPORTED_SONAME_NOCONFIG "libpaho-mqtt3a.so.1"
  )

list(APPEND _IMPORT_CHECK_TARGETS PahoMqttCpp::paho-mqtt3a )
list(APPEND _IMPORT_CHECK_FILES_FOR_PahoMqttCpp::paho-mqtt3a "${_IMPORT_PREFIX}/lib/libpaho-mqtt3a.so.1.3.14" )

# Import target "PahoMqttCpp::paho-mqtt3as" for configuration ""
set_property(TARGET PahoMqttCpp::paho-mqtt3as APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(PahoMqttCpp::paho-mqtt3as PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libpaho-mqtt3as.so.1.3.14"
  IMPORTED_SONAME_NOCONFIG "libpaho-mqtt3as.so.1"
  )

list(APPEND _IMPORT_CHECK_TARGETS PahoMqttCpp::paho-mqtt3as )
list(APPEND _IMPORT_CHECK_FILES_FOR_PahoMqttCpp::paho-mqtt3as "${_IMPORT_PREFIX}/lib/libpaho-mqtt3as.so.1.3.14" )

# Import target "PahoMqttCpp::paho-mqttpp3-shared" for configuration ""
set_property(TARGET PahoMqttCpp::paho-mqttpp3-shared APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(PahoMqttCpp::paho-mqttpp3-shared PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libpaho-mqttpp3.so.1.5.1"
  IMPORTED_SONAME_NOCONFIG "libpaho-mqttpp3.so.1"
  )

list(APPEND _IMPORT_CHECK_TARGETS PahoMqttCpp::paho-mqttpp3-shared )
list(APPEND _IMPORT_CHECK_FILES_FOR_PahoMqttCpp::paho-mqttpp3-shared "${_IMPORT_PREFIX}/lib/libpaho-mqttpp3.so.1.5.1" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
