# The recognition system: the recogniser's object classes (RecObject.h:
# TRecObject, TArray, TDArray), the strokes (Stroke.h: TStroke and its
# public face TStrokePublic), the units the recognisers make (Unit.h:
# TUnit, TSIUnit, TStrokeUnit, TClickUnit, TClickEventUnit) and the face
# the views see (UnitPublic.h), the areas (Areas.h) and the
# configuration they are built from (RecConfig.h), the domains' base
# (Domain.h) and the recognisers' front (Recognizer.h: TRecognizer, the
# click recognisers, TRecognitionManager).  The recognisers themselves
# The tablet buffer (TabletBuffer.h) and the stroke queue (StrokeQueue.h)
# bring the pen's points in as strokes; the stroke world (StrokeCentral.h)
# makes clicks of them and the unit handler (HandleUnit.cpp) posts the
# commands to the views.  The controller (Controller.h) groups, classifies
# and arbitrates them through the domains; the arbiter's deciding and the
# domains above the stroke domain are NOT YET.  Over frames (the units and strokes are handed to NewtonScript
# as address refs) and qd.
add_library(recognition STATIC
    RecObject.cpp
    Stroke.cpp
    StrokeBundle.cpp
    WordUnit.cpp
    WordList.cpp
    WordInfo.cpp
    CorrectInfo.cpp
    InkRecognizer.cpp
    WRecDomain.cpp
    Unit.cpp
    UnitPublic.cpp
    Areas.cpp
    RecConfig.cpp
    Domain.cpp
    EdgeList.cpp
    ShapeDomain.cpp
    ShapeGeometry.cpp
    ShapeKeyPoints.cpp
    ShapeEllipses.cpp
    ShapeSolver.cpp
    ShapeTrends.cpp
    ShapeEquations.cpp
    ShapeSnapping.cpp
    ShapeTables.cpp
    Controller.cpp
    Arbiter.cpp
    Recognizer.cpp
    RosRecognizer.cpp
    RosStrokes.cpp
    WordTails.cpp
    Search.cpp
    GeoContext.cpp
    GeoTables.cpp
    LELang.cpp
    RosList.cpp
    WordSegment.cpp
    WordSegmentTables.cpp
    InkGroups.cpp
    WordDescriptors.cpp
    LowLevel.cpp
    LowFilter.cpp
    LowExtr.cpp
    LowBaseline.cpp
    LowGeometry.cpp
    LowPunct.cpp
    LowLine.cpp
    LowBorders.cpp
    LowClassify.cpp
    LowTables.cpp
    CursiveReader.cpp
    StrXrDomain.cpp
    Fragment.cpp
    FragmentTables.cpp
    LELangTables.cpp
    SearchEasterEgg.cpp
    RosEngine.cpp
    Segment.cpp
    WordRecog.cpp
    ROMGrammar.cpp
    ArProbTables.cpp
    CharHeightTable.cpp
    BPNet.cpp
    BPNetTables.cpp
    NetPattern.cpp
    CharBox.cpp
    Render.cpp
    RenderTables.cpp
    RosCITables.cpp
    Rosetta.cpp
    TabletBuffer.cpp
    StrokeQueue.cpp
    StrokeCentral.cpp
    HandleUnit.cpp
    UnitNatives.cpp
    Airus.cpp
    AirusIterator.cpp
    Dictionaries.cpp
    RandomWords.cpp
    RandomWordTables.cpp
    ROMDictionaryData.cpp
    ROMDictionaryTable.cpp
    Learning.cpp
    Spelling.cpp
    SpellMaps.cpp
    AirusTables.cpp
    Words.cpp
    ParaGraph.cpp
    ParaGraphTables.cpp
    XrDomains.cpp
    WordRecognizer.cpp
    LetterShapes.cpp
)
target_include_directories(recognition PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# (the strokes tell the root view what to redraw; the views track strokes:
# the two static libraries depend on each other)
target_link_libraries(recognition PUBLIC frames qd views testing)
add_subdirectory(tests)
