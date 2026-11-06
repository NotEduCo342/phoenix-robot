idf_component_register(SRCS "src/audio_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2s driver freertos esp_timer)

