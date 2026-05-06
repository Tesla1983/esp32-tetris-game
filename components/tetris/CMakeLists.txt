idf_component_register(
    SRCS "tetris_game.c" "drivers/st7735.c"
    INCLUDE_DIRS "." "drivers"
    PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_timer
)
