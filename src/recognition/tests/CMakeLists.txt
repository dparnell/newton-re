add_executable(test_RecObject test_RecObject.cpp)
target_link_libraries(test_RecObject PRIVATE recognition)
add_test(NAME recognition.RecObject COMMAND test_RecObject)
add_executable(test_Stroke test_Stroke.cpp)
target_link_libraries(test_Stroke PRIVATE recognition)
add_test(NAME recognition.Stroke COMMAND test_Stroke)
add_executable(test_Unit test_Unit.cpp)
target_link_libraries(test_Unit PRIVATE recognition)
add_test(NAME recognition.Unit COMMAND test_Unit)

add_executable(test_Areas test_Areas.cpp)
target_link_libraries(test_Areas PRIVATE recognition)
add_test(NAME recognition.Areas COMMAND test_Areas)

add_executable(test_Words test_Words.cpp)
target_link_libraries(test_Words PRIVATE recognition)
add_test(NAME recognition.Words COMMAND test_Words)

add_executable(test_Airus test_Airus.cpp)
target_link_libraries(test_Airus PRIVATE recognition)
add_test(NAME recognition.Airus COMMAND test_Airus)

add_executable(test_RecConfig test_RecConfig.cpp)
target_link_libraries(test_RecConfig PRIVATE recognition)
target_compile_definitions(test_RecConfig PRIVATE "NEWTON_ROM_BIN=\"${NEWTON_ROM_BIN}\"")
add_test(NAME recognition.RecConfig COMMAND test_RecConfig)

add_executable(test_Controller test_Controller.cpp)
target_link_libraries(test_Controller PRIVATE recognition)
add_test(NAME recognition.Controller COMMAND test_Controller)

add_executable(test_EdgeList test_EdgeList.cpp)
target_link_libraries(test_EdgeList PRIVATE recognition)
add_test(NAME recognition.EdgeList COMMAND test_EdgeList)

add_executable(test_WRecDomain test_WRecDomain.cpp)
target_link_libraries(test_WRecDomain PRIVATE recognition)
add_test(NAME recognition.WRecDomain COMMAND test_WRecDomain)

add_executable(test_WordList test_WordList.cpp)
target_link_libraries(test_WordList PRIVATE recognition)
add_test(NAME recognition.WordList COMMAND test_WordList)

add_executable(test_Dictionaries test_Dictionaries.cpp)
target_link_libraries(test_Dictionaries PRIVATE recognition)
target_compile_definitions(test_Dictionaries PRIVATE "NEWTON_ROM_BIN=\"${NEWTON_ROM_BIN}\"")
add_test(NAME recognition.Dictionaries COMMAND test_Dictionaries)

add_executable(test_Spelling test_Spelling.cpp)
target_link_libraries(test_Spelling PRIVATE recognition)
target_compile_definitions(test_Spelling PRIVATE "NEWTON_ROM_BIN=\"${NEWTON_ROM_BIN}\"")
add_test(NAME recognition.Spelling COMMAND test_Spelling)

add_executable(test_RosRecognizer test_RosRecognizer.cpp)
target_link_libraries(test_RosRecognizer PRIVATE recognition)
add_test(NAME recognition.RosRecognizer COMMAND test_RosRecognizer)

add_executable(test_RosStrokes test_RosStrokes.cpp)
target_link_libraries(test_RosStrokes PRIVATE recognition memory)
add_test(NAME recognition.RosStrokes COMMAND test_RosStrokes)

add_executable(test_WordRecog test_WordRecog.cpp)
target_link_libraries(test_WordRecog PRIVATE recognition memory)
add_test(NAME recognition.WordRecog COMMAND test_WordRecog)

add_executable(test_Rosetta test_Rosetta.cpp)
target_link_libraries(test_Rosetta PRIVATE recognition memory)
add_test(NAME recognition.Rosetta COMMAND test_Rosetta)

add_executable(test_Search test_Search.cpp)
target_link_libraries(test_Search PRIVATE recognition memory)
target_compile_definitions(test_Search PRIVATE "NEWTON_ROM_BIN=\"${NEWTON_ROM_BIN}\"")
add_test(NAME recognition.Search COMMAND test_Search)

add_executable(test_RosList test_RosList.cpp)
target_link_libraries(test_RosList PRIVATE recognition memory)
add_test(NAME recognition.RosList COMMAND test_RosList)

add_executable(test_Fragment test_Fragment.cpp)
target_link_libraries(test_Fragment PRIVATE recognition memory)
add_test(NAME recognition.Fragment COMMAND test_Fragment)

add_executable(test_Reading test_Reading.cpp)
target_link_libraries(test_Reading PRIVATE recognition memory)
target_compile_definitions(test_Reading PRIVATE "NEWTON_ROM_BIN=\"${NEWTON_ROM_BIN}\"")

add_test(NAME recognition.Reading COMMAND test_Reading)

add_executable(test_GeoContext test_GeoContext.cpp)
target_link_libraries(test_GeoContext PRIVATE recognition memory)
add_test(NAME recognition.GeoContext COMMAND test_GeoContext)

add_executable(test_WordTails test_WordTails.cpp)
target_link_libraries(test_WordTails PRIVATE recognition memory)
add_test(NAME recognition.WordTails COMMAND test_WordTails)

add_executable(test_Segment test_Segment.cpp)
target_link_libraries(test_Segment PRIVATE recognition memory)
add_test(NAME recognition.Segment COMMAND test_Segment)

add_executable(test_CharBox test_CharBox.cpp)
target_link_libraries(test_CharBox PRIVATE recognition memory)
add_test(NAME recognition.CharBox COMMAND test_CharBox)

add_executable(test_NetPattern test_NetPattern.cpp)
target_link_libraries(test_NetPattern PRIVATE recognition memory)
add_test(NAME recognition.NetPattern COMMAND test_NetPattern)

add_executable(test_ShapeDomain test_ShapeDomain.cpp)
target_link_libraries(test_ShapeDomain PRIVATE recognition memory)
add_test(NAME recognition.ShapeDomain COMMAND test_ShapeDomain)

add_executable(test_WordDescriptors test_WordDescriptors.cpp)
target_link_libraries(test_WordDescriptors PRIVATE recognition)
add_test(NAME recognition.WordDescriptors COMMAND test_WordDescriptors)
add_executable(test_LowLevel test_LowLevel.cpp)
target_link_libraries(test_LowLevel PRIVATE recognition)
add_test(NAME recognition.LowLevel COMMAND test_LowLevel)
