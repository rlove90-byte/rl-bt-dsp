idf_component_register(
    SRCS "rl_battery.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_adc freertos log
)
