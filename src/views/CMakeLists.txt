# The view system: TView (View.h), the C++ object behind every NewtonScript
# view - its context, slot cache, bounds justified against the parent,
# children, scripts, drawing through QuickDraw - the root view (RootView.h)
# with the update regions, the making of views from templates
# (BuildView.cpp), the NewtonScript view functions (ViewNatives.cpp) and
# the text, picture and paragraph views (TextView.h, PictureView.h,
# ParagraphView.h over DataView.h, the style runs in StyleRuns.h); the
# gauge (GaugeView.h), the NewtonScript shapes and DrawShape
# (DrawShape.h), the command frames (Commands.h) and the application
# that dispatches them and keeps the undo stacks and delayed actions
# (Application.h), the picker of popup menus (PickView.h); the other subclasses (lists, pickers, ...) to come.
add_library(views STATIC
    View.cpp
    ViewDraw.cpp
    BuildView.cpp
    RootView.cpp
    ViewNatives.cpp
    TextView.cpp
    PictureView.cpp
    DataView.cpp
    StyleRuns.cpp
    ParagraphView.cpp
    GaugeView.cpp
    DrawShape.cpp
    Commands.cpp
    Keyboard.cpp
    Application.cpp
    PickView.cpp
)
target_link_libraries(views PUBLIC newton_config qd frames intl utility)
target_include_directories(views PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
