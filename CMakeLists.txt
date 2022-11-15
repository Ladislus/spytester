cmake_minimum_required(VERSION 3.23)
project(SpyTester)
enable_language(C ASM)
set(CMAKE_CXX_STANDARD 17)

add_library(TestLib SHARED test/BasicTest/TestLib.cpp test/BasicTest/TestLib.h)
add_library(ThreadIdLib SHARED test/ThreadIdentification/TestedLib.cpp test/ThreadIdentification/TestedLib.h)

add_library(SpyTester SHARED source/SpiedProgram.cpp source/utils.S include/SpiedProgram.h source/SpiedThread.cpp include/SpiedThread.h source/Tracer.cpp include/Tracer.h source/Breakpoint.cpp include/Breakpoint.h include/WrappedFunction.h  source/WatchPoint.cpp include/WatchPoint.h source/DynamicLinker.cpp include/DynamicLinker.h)
target_compile_options(SpyTester PRIVATE -O2)
target_link_libraries(SpyTester dl pthread)
set_target_properties(SpyTester PROPERTIES BUILD_RPATH_USE_ORIGIN TRUE)
set_target_properties(SpyTester PROPERTIES BUILD_RPATH .)

# Tester executables
add_executable(BasicTest test/BasicTest/TestExe.cpp include/SpiedProgram.h)
target_compile_options(BasicTest PRIVATE -fPIC )
target_link_libraries(BasicTest SpyTester TestLib)

add_executable(ThreadIdExe test/ThreadIdentification/Tester.cpp include/SpiedProgram.h)
target_compile_options(ThreadIdExe PRIVATE -fPIC )
target_link_libraries(ThreadIdExe SpyTester ThreadIdLib)

# Tested executables
add_executable(TestProgram test/BasicTest/TestProgram.cpp)
target_compile_options(TestProgram PRIVATE -fPIC )
target_link_libraries(TestProgram pthread TestLib)

add_executable(ThreadIdProgram test/ThreadIdentification/TestedExe.cpp)
target_compile_options(ThreadIdProgram PRIVATE -fPIC )
target_link_libraries(ThreadIdProgram pthread ThreadIdLib)