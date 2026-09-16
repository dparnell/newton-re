# Protocols: Newton's interface/implementation mechanism (TProtocol,
# TClassInfo, the by-name instance makers) and the protocol registry monitor
# that every subsystem registers its implementations with.  Protocols.h here
# replaces the DDK's (see its comment).
add_library(protocols STATIC
    Protocols.cpp
    ClassInfoRegistry.cpp
)
target_link_libraries(protocols PUBLIC newton_config os600_user utility memory)
target_include_directories(protocols PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
