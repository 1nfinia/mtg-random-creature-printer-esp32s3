idf_component_register(
    SRCS
        "main.c"
        "ssd1306.c"
        "encoder.c"
        "card_db.c"
        "sd_card_reader.c"
        "thermal_printer.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        fatfs
        sdmmc
        esp_driver_gpio
        esp_timer
        esp_driver_i2c
        esp_driver_spi
        esp_driver_sdspi
        esp_driver_uart
)

target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wno-error=missing-braces
    -Wno-missing-braces
)
