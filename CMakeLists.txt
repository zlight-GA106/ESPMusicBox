cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{ADF_PATH})
    message(FATAL_ERROR "ADF_PATH is not set. Point it to ESP-ADF v2.7.")
endif()

file(TO_CMAKE_PATH "$ENV{ADF_PATH}" ADF_PATH_CMAKE)
list(APPEND EXTRA_COMPONENT_DIRS
    "${ADF_PATH_CMAKE}/components/audio_sal"
    "${ADF_PATH_CMAKE}/components/audio_pipeline"
    "${ADF_PATH_CMAKE}/components/esp-adf-libs")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_music_box)
