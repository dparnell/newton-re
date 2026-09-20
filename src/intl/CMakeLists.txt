# International utilities: the current locale and its attribute cache
# (Locale.h), dates and times (Dates.h: TDate and the NewtonScript date
# functions), numbers as text (NumberFormat.h), the world map's
# coordinates (Coordinates.h); the lexical dictionaries to come.  The frames library formats numbers through this one, so the
# two static libraries depend on each other.
add_library(intl STATIC
    Locale.cpp
    Dates.cpp
    NumberFormat.cpp
    Coordinates.cpp
)
target_link_libraries(intl PUBLIC newton_config frames utility os600_user memory)
target_include_directories(intl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The repeating meetings (Meetings.h): the Dates application's repeat
# templates stepped and collected over the stores' soups.
add_library(intl_meetings STATIC
    Meetings.cpp
)
target_link_libraries(intl_meetings PUBLIC intl stores)

add_subdirectory(tests)
