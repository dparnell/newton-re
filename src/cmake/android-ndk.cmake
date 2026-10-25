# Cross-compile for an Android tablet (the Viwoods AiPaper Mini, docs/host-android.md)
# with the Android NDK (r27 or later; Homebrew's `android-ndk` cask):
#   cmake -G Ninja -S src -B build/android -DCMAKE_TOOLCHAIN_FILE=src/cmake/android-ndk.cmake
#         -DNEWTON_HOST_NEWTONSCRIPT=<a newtonscript built for the build machine>
# ANDROID_NDK_HOME (or /opt/homebrew/share/android-ndk) says where the NDK is,
# ANDROID_ABI (default arm64-v8a) and ANDROID_PLATFORM (default android-26) the
# target.  char is unsigned on Android's arm64 and signed on every host the
# reconstruction was written on: keep it signed (docs/host-remarkable.md, "char").
if(NOT DEFINED ANDROID_NDK AND DEFINED ENV{ANDROID_NDK_HOME})
    set(ANDROID_NDK $ENV{ANDROID_NDK_HOME})
endif()
if(NOT DEFINED ANDROID_NDK)
    set(ANDROID_NDK /opt/homebrew/share/android-ndk)
endif()
if(NOT DEFINED ANDROID_ABI)
    set(ANDROID_ABI arm64-v8a)
endif()
if(NOT DEFINED ANDROID_PLATFORM)
    set(ANDROID_PLATFORM android-26)
endif()
set(ANDROID_STL c++_static)
include(${ANDROID_NDK}/build/cmake/android.toolchain.cmake)
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -fsigned-char")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -fsigned-char")
