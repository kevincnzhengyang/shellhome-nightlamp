set(srcs "app_main.c"
         "board_sensor.c"
         "board_leds.c")
set(include_dirs ".")

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${include_dirs}"
                       REQUIRES nvs_flash button driver esp_timer esp_adc led_strip)
