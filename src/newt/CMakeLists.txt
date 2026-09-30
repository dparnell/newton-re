# The NewtonScript world: TNewtWorld, the application world the 'main' task
# runs (NewtWorld.h: its boot, its event loop and the 'newt event handler),
# and TNotebook, the Newton application (Notebook.h: the root view, the
# toolbox, the idle passes, the notifiers).  Over views and recognition,
# and the utility task frameworks; the host's tablet stands in for the
# inker.
add_library(newt STATIC
    Notebook.cpp
    NewtWorld.cpp
    StorageCards.cpp
    ForkGlobals.cpp
    ModalDialogNatives.cpp
    DebugNatives.cpp
    SCPEvents.cpp
)
target_link_libraries(newt PUBLIC newton_config comms_script comms_ntk views books armcpu_frames assist recognition compression packages pcmcia utility system sound os600_user hal_host_tablet)
target_include_directories(newt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
