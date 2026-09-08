if(NOT DEFINED ENV{ADF_PATH})
    message(FATAL_ERROR "ADF_PATH is required by the audio_board component")
endif()

file(TO_CMAKE_PATH "$ENV{ADF_PATH}" ADF_PATH_CMAKE)
idf_component_register(
    SRCS "${CMAKE_SOURCE_DIR}/main/audio/adf_board_pins.c"
    INCLUDE_DIRS "${ADF_PATH_CMAKE}/components/audio_board/include"
    REQUIRES driver)
