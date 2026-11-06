idf_component_register(SRCS "src/st7735.c" "src/ssd1306.c" "src/esp_robo_eyes.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_timer esp_hw_support)
