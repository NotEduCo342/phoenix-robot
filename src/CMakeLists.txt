idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_robo_eyes bmi160 audio_manager haptics wifi_manager alarm_clock driver esp_timer freertos)
