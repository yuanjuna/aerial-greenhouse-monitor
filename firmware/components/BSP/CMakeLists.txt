set(src_dirs
			DHT11
            LED
            MYIIC
            XL9555
            MYSPI
            SPILCD
            ADC
            CO2_SENSOR
            O2_SENSOR
            SOIL_SENSOR)

set(include_dirs
			DHT11
            LED
            MYIIC
            XL9555
            MYSPI
            SPILCD
            ADC
            CO2_SENSOR
            O2_SENSOR
            SOIL_SENSOR)

set(requires
            driver
            esp_lcd
            esp_adc)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)
