set(src_dirs
            NETWORK
            MQTT
            SENSOR_APP)

set(include_dirs
            CONFIG
            COMMON
            NETWORK
            MQTT
            SENSOR_APP)

set(requires
            BSP
            esp_event
            esp_netif
            esp_wifi
            lwip
            mqtt
            nvs_flash)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
