cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(WIFI_STA_AP_tutorial VERSION 1.0.0)

# Create SPIFFS image from WEBUI directory
spiffs_create_partition_image(storage WEBUI FLASH_IN_PROJECT)
