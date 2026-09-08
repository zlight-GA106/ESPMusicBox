if(NOT DEFINED ENV{ADF_PATH})
    message(FATAL_ERROR "ADF_PATH is required by the audio_stream component")
endif()

file(TO_CMAKE_PATH "$ENV{ADF_PATH}" ADF_PATH_CMAKE)
idf_component_register(
    SRCS
        "${ADF_PATH_CMAKE}/components/audio_stream/i2s_stream_idf5.c"
        "${ADF_PATH_CMAKE}/components/audio_stream/raw_stream.c"
    INCLUDE_DIRS
        "${ADF_PATH_CMAKE}/components/audio_stream/include"
    REQUIRES
        audio_board
        audio_pipeline
        audio_sal
        esp-adf-libs
        esp_driver_i2s)
