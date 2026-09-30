# The system alerts (AlertManager.h): boxes drawn straight onto the screen's
# bits over everything else (the card reinsert alert and the rest), and the
# 'alrt world that puts them up and takes them down.
add_library(alert STATIC
    AlertDialog.cpp
    AlertManager.cpp
)
target_link_libraries(alert PUBLIC newton_config qd frames recognition utility os600_user)
target_include_directories(alert PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
