# The view system: TView (View.h), the C++ object behind every NewtonScript
# view - its context, slot cache, bounds justified against the parent,
# children, scripts, drawing through QuickDraw - the root view (RootView.h)
# with the update regions, the making of views from templates
# (BuildView.cpp) and the NewtonScript view functions (ViewNatives.cpp).
# The subclasses (text, pictures, lists, ...) to come.
add_library(views STATIC
    View.cpp
    ViewDraw.cpp
    BuildView.cpp
    RootView.cpp
    ViewNatives.cpp
)
target_link_libraries(views PUBLIC newton_config qd frames intl utility)
target_include_directories(views PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
