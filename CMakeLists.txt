cmake_minimum_required(VERSION 3.23)
project(SpyTester)
enable_language(C ASM)
set(CMAKE_CXX_STANDARD 11)

add_library(SpyTester SHARED SpiedProgram.cpp utils.S SpiedProgram.h SpiedThread.cpp SpiedThread.h Tracer.cpp Tracer.h Breakpoint.cpp Breakpoint.h TracingCommand.h)
target_link_libraries(SpyTester dl)
set_target_properties(SpyTester PROPERTIES BUILD_RPATH_USE_ORIGIN TRUE)
set_target_properties(SpyTester PROPERTIES BUILD_RPATH .)

add_executable(TestExe cmake-build-debug/Testing/TestExe.cpp SpiedProgram.h)
target_link_libraries(TestExe SpyTester)

add_executable(TestProgram cmake-build-debug/Testing/TestProgram.cpp)
target_link_libraries(TestProgram pthread)
target_link_options(TestProgram PRIVATE -rdynamic)
