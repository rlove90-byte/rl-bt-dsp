idf_component_register(
    SRCS "rl_leds.c"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos log
)
target_link_libraries(${COMPONENT_LIB} PRIVATE m)
