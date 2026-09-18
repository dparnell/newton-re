add_executable(test_DoubleQ test_DoubleQ.cpp)
target_link_libraries(test_DoubleQ PRIVATE os600_kernel)
add_test(NAME os600.kernel.DoubleQ COMMAND test_DoubleQ)

add_executable(test_ObjectTable test_ObjectTable.cpp)
target_link_libraries(test_ObjectTable PRIVATE os600_kernel)
add_test(NAME os600.kernel.ObjectTable COMMAND test_ObjectTable)

add_executable(test_Scheduler test_Scheduler.cpp)
target_link_libraries(test_Scheduler PRIVATE os600_kernel)
add_test(NAME os600.kernel.Scheduler COMMAND test_Scheduler)

add_executable(test_Semaphore test_Semaphore.cpp)
target_link_libraries(test_Semaphore PRIVATE os600_kernel)
add_test(NAME os600.kernel.Semaphore COMMAND test_Semaphore)

add_executable(test_RealTimeClock test_RealTimeClock.cpp)
target_link_libraries(test_RealTimeClock PRIVATE os600_kernel)
add_test(NAME os600.kernel.RealTimeClock COMMAND test_RealTimeClock)

add_executable(test_TimerEngine test_TimerEngine.cpp)
target_link_libraries(test_TimerEngine PRIVATE os600_kernel)
add_test(NAME os600.kernel.TimerEngine COMMAND test_TimerEngine)

add_executable(test_Port test_Port.cpp)
target_link_libraries(test_Port PRIVATE os600_kernel)
add_test(NAME os600.kernel.Port COMMAND test_Port)

add_executable(test_Monitor test_Monitor.cpp)
target_link_libraries(test_Monitor PRIVATE os600_kernel)
add_test(NAME os600.kernel.Monitor COMMAND test_Monitor)

add_executable(test_Environment test_Environment.cpp)
target_link_libraries(test_Environment PRIVATE os600_kernel)
add_test(NAME os600.kernel.Environment COMMAND test_Environment)

add_executable(test_ObjectManager test_ObjectManager.cpp)
target_link_libraries(test_ObjectManager PRIVATE os600_kernel)
add_test(NAME os600.kernel.ObjectManager COMMAND test_ObjectManager)

add_executable(test_TaskSwitch test_TaskSwitch.cpp)
target_link_libraries(test_TaskSwitch PRIVATE os600_kernel)
add_test(NAME os600.kernel.TaskSwitch COMMAND test_TaskSwitch)

add_executable(test_MemObjManager test_MemObjManager.cpp)
target_link_libraries(test_MemObjManager PRIVATE os600_kernel)
add_test(NAME os600.kernel.MemObjManager COMMAND test_MemObjManager)
