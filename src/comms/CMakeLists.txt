# Communications: the option arrays the framework is configured through
# (docs/comms/README.md for the plan: the comm tools, the comm manager,
# the endpoints and the NewtonScript endpoint over them, and the host's
# network behind a host comm tool).
add_library(comms STATIC
    Options.cpp
)
target_link_libraries(comms PUBLIC newton_config os600_user memory)
target_include_directories(comms PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
