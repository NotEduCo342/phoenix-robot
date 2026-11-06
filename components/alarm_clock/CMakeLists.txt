idf_component_register(SRCS "src/alarm_clock.c" "src/web_assets.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server esp_robo_eyes audio_manager haptics wifi_manager esp_timer freertos json)




