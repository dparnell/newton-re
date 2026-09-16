# International utilities: the current locale and its attribute cache
# (Locale.h), dates and times (Dates.h: TDate and the NewtonScript date
# functions), the repeating meetings (Meetings.h, over the stores' soups);
# number formatting and the lexical dictionaries to come.
add_library(intl STATIC
    Locale.cpp
    Dates.cpp
    Meetings.cpp
)
target_link_libraries(intl PUBLIC newton_config frames stores utility os600_user)
target_include_directories(intl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
