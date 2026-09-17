# Sound: the codecs the Newton uses for recorded sound - IMA/DVI ADPCM
# (IMACodec.h), the mu-law companding of 8-bit sound (MuLaw.h) and the
# plain sample-rate converter (Resample.h).  The DDK
# has no header for these; reconstructed from the ROM.  The
# TSoundCodec/TSoundServer streaming is NOT YET.
#
# The mu-law decoder dithers with QuickDraw's Random (qd/Ports.h), as the
# ROM's does, so the library depends on qd.
add_library(sound STATIC
    IMACodec.cpp
    MuLaw.cpp
    Resample.cpp
)
target_link_libraries(sound PUBLIC newton_config qd)
target_include_directories(sound PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
