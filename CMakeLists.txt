cmake_minimum_required(VERSION 3.23)
project(SpyTester)
enable_language(C ASM)
set(CMAKE_CXX_STANDARD 17)

add_library(TestLib SHARED cmake-build-debug/Testing/TestLib.cpp cmake-build-debug/Testing/TestLib.h)
add_library(SpyTester SHARED SpiedProgram.cpp utils.S SpiedProgram.h SpiedThread.cpp SpiedThread.h Tracer.cpp Tracer.h Breakpoint.cpp Breakpoint.h TracingCommand.h WrappedFunction.h cmake-build-debug/Testing/TestLib.cpp cmake-build-debug/Testing/TestLib.h WatchPoint.cpp WatchPoint.h)
target_link_libraries(SpyTester dl pthread)
set_target_properties(SpyTester PROPERTIES BUILD_RPATH_USE_ORIGIN TRUE)
set_target_properties(SpyTester PROPERTIES BUILD_RPATH .)

add_executable(TestExe cmake-build-debug/Testing/TestExe.cpp SpiedProgram.h)
target_link_libraries(TestExe SpyTester TestLib)

add_executable(TestProgram cmake-build-debug/Testing/TestProgram.cpp)
target_compile_options(TestProgram PRIVATE -fPIC )
target_link_libraries(TestProgram pthread TestLib)
target_link_options(TestProgram PRIVATE -rdynamic)
