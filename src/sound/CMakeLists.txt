# Sound: the codecs the Newton uses for recorded sound - IMA/DVI ADPCM
# (IMACodec.h).  The DDK has no header for these; reconstructed from the
# ROM.  MuLaw and the TSoundCodec/TSoundServer streaming are NOT YET.
add_library(sound STATIC
    IMACodec.cpp
)
target_link_libraries(sound PUBLIC newton_config)
target_include_directories(sound PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
