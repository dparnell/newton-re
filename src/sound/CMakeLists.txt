# Sound: the codecs (IMACodec.h; SampleConvert.h's mu-law and
# offset-binary 8-bit; the TSoundCodec protocol with its mu-law, IMA, GSM
# and DTMF implementations, SoundCodec.h, DTMFCodec.cpp), the sample-rate
# converters (Resample.h), the sound server (SoundServer.h over the
# PSoundDriver of SoundDriver.h), its clients (SoundChannel.h,
# FrameSoundChannel.h) and the NewtonScript sound functions
# (SoundSettings.h).  The DDK has no header for these; reconstructed from
# the ROM (docs/sound/README.md).
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
    DTMFCodec.cpp
    DTMFTables.cpp
    GSM.cpp
    GSMCodec.cpp
    GSMTables.cpp
    SoundChannel.cpp
    FrameSoundChannel.cpp
    SoundSettings.cpp
    SoundServer.cpp
    SoundDriver.cpp
)
target_link_libraries(sound PUBLIC newton_config qd protocols frames utility os600_user)
target_include_directories(sound PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
