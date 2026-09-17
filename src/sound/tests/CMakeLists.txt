add_executable(test_IMACodec test_IMACodec.cpp)
target_link_libraries(test_IMACodec PRIVATE sound)
add_test(NAME sound.IMACodec COMMAND test_IMACodec)
set_tests_properties(sound.IMACodec PROPERTIES TIMEOUT 60)

add_executable(test_SampleConvert test_SampleConvert.cpp)
target_link_libraries(test_SampleConvert PRIVATE sound)
add_test(NAME sound.SampleConvert COMMAND test_SampleConvert)
set_tests_properties(sound.SampleConvert PROPERTIES TIMEOUT 60)

add_executable(test_Resample test_Resample.cpp)
target_link_libraries(test_Resample PRIVATE sound)
add_test(NAME sound.Resample COMMAND test_Resample)
set_tests_properties(sound.Resample PROPERTIES TIMEOUT 60)

add_executable(test_ResampleFiltered test_ResampleFiltered.cpp)
target_link_libraries(test_ResampleFiltered PRIVATE sound)
add_test(NAME sound.ResampleFiltered COMMAND test_ResampleFiltered)
set_tests_properties(sound.ResampleFiltered PROPERTIES TIMEOUT 60)
