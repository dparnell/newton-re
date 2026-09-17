# Sound: the codecs the Newton uses for recorded sound - IMA/DVI ADPCM
# (IMACodec.h), the 8-bit sample codings (SampleConvert.h: mu-law and
# offset-binary 8-bit) and the sample-rate converters (Resample.h).  The DDK
# has no header for these; reconstructed from the ROM.  The
# TSoundCodec/TSoundServer streaming is NOT YET.
#
# The 8-bit decoders dither with QuickDraw's Random (qd/Ports.h), as the
# ROM's do, so the library depends on qd; the filtered resampler needs the
# toolbox's fixed-point arithmetic.
add_library(sound STATIC
    IMACodec.cpp
    SampleConvert.cpp
    Resample.cpp
    ResampleTables.cpp
)
target_link_libraries(sound PUBLIC newton_config qd)
target_include_directories(sound PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
