# Sound: the codecs the Newton uses for recorded sound - IMA/DVI ADPCM
# (IMACodec.h), the 8-bit sample codings (SampleConvert.h: mu-law and
# offset-binary 8-bit), the sample-rate converters (Resample.h) and the
# TSoundCodec protocol with its mu-law implementation (SoundCodec.h).  The DDK
# has no header for these; reconstructed from the ROM.  The
# TSoundCodec/TSoundServer streaming is NOT YET; SoundChannel.h has only
# as much of TUSoundChannel as the volume needs, and SoundSettings.h the
# NewtonScript volume functions over it.
#
# The 8-bit decoders dither with QuickDraw's Random (qd/Ports.h), as the
# ROM's do, so the library depends on qd; the filtered resampler needs the
# toolbox's fixed-point arithmetic.
add_library(sound STATIC
    IMACodec.cpp
    SampleConvert.cpp
    Resample.cpp
    ResampleTables.cpp
    SoundCodec.cpp
    SoundChannel.cpp
    FrameSoundChannel.cpp
    SoundSettings.cpp
    SoundServer.cpp
    SoundDriver.cpp
)
target_link_libraries(sound PUBLIC newton_config qd protocols frames utility os600_user)
target_include_directories(sound PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
