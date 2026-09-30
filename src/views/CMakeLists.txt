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
# (Application.h), the picker of popup menus (PickView.h) and the
# clipboard (ClipboardView.h), the outline list (ListView.h), a meeting
# in the day view (MeetingView.h); the other subclasses to come.
add_library(views STATIC
    View.cpp
    Hilites.cpp
    Animate.cpp
    DragDrop.cpp
    ViewDraw.cpp
    BuildView.cpp
    RootView.cpp
    ModalDialogs.cpp
    ViewNatives.cpp
    FontNatives.cpp
    ViewExtraNatives.cpp
    SplashScreen.cpp
    KeyHelpSlip.cpp
    Reflow.cpp
    TextView.cpp
    PictureView.cpp
    DataView.cpp
    ContainerView.cpp
    EditView.cpp
    ListView.cpp
    MeetingView.cpp
    SliderView.cpp
    RemoteView.cpp
    StyleRuns.cpp
    ParagraphView.cpp
    Rerecognize.cpp
    GaugeView.cpp
    PolygonView.cpp
    PolygonTraced.cpp
    PolygonViewTables.cpp
    DrawShape.cpp
    ShapeVerbs.cpp
    PictureShapes.cpp
    Commands.cpp
    Keyboard.cpp
    KeyboardView.cpp
    MonthView.cpp
    Bits.cpp
    Application.cpp
    PickView.cpp
    ClipboardView.cpp
    PrintView.cpp
)
# (the root view tracks clicks on the caret through the recognition units;
# the recogniser posts to the views: the two static libraries depend on
# each other)
# (the view system draws ink, and the ink area reaches the strokes through
# the recogniser, which draws through the views: the three are mutually
# dependent, as frames and intl are)
# (BuildView makes the text engine's TXView, which is itself a view: views
# and text are mutually dependent)
# (BuildView makes the book reader's outline, TOutline, which is a view
# over the book reader's library: views and books are mutually dependent)
# (BuildView makes the print view, TPrintView, which drives a printer: views
# links print)
target_link_libraries(views PUBLIC ink newton_config qd frames intl intl_meetings utility recognition stores sound text books print)
target_include_directories(views PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
