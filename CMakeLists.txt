cmake_minimum_required(VERSION 3.23)
project(SpyTester)
enable_language(C ASM)
set(CMAKE_CXX_STANDARD 17)

add_library(TestLib SHARED test/BasicTest/TestLib.cpp test/BasicTest/TestLib.h)
add_library(ThreadIdLib SHARED test/ThreadIdentification/TestedLib.cpp test/ThreadIdentification/TestedLib.h)
add_library(WrapperTestedLib SHARED test/WrapperTest/WrapperTestedLib.cpp test/WrapperTest/WrapperTestedLib.h)
add_library(TLSTestedLib SHARED test/TLSTest/TLSTestedLib.cpp test/TLSTest/TLSTestedLib.h)
target_compile_options(TLSTestedLib PRIVATE -ftls-model=initial-exec)

add_library(SpyLoader SHARED source/Loader.cpp source/SpyLoader.cpp include/SpyLoader.h source/DynamicNamespace.cpp include/DynamicNamespace.h source/DynamicModule.cpp source/Relinkage.cpp include/Relinkage.h source/ElfFile.cpp include/ElfFile.h)
target_link_libraries(SpyLoader dl pthread -g)
target_link_options(SpyLoader PRIVATE -Wl,-z nodelete -z interpose)
set_target_properties(SpyLoader PROPERTIES BUILD_RPATH_USE_ORIGIN TRUE)
set_target_properties(SpyLoader PROPERTIES BUILD_RPATH .)

add_library(SpyTester SHARED source/SpiedProgram.cpp include/SpiedProgram.h source/SpiedThread.cpp include/SpiedThread.h source/Tracer.cpp include/Tracer.h source/Breakpoint.cpp include/Breakpoint.h include/WrappedFunction.h  source/WatchPoint.cpp include/WatchPoint.h include/Meta.h source/CallbackHandler.cpp include/CallbackHandler.h)
target_compile_options(SpyTester PRIVATE -O2 -g)
target_link_libraries(SpyTester dl pthread)
set_target_properties(SpyTester PROPERTIES BUILD_RPATH_USE_ORIGIN TRUE)
set_target_properties(SpyTester PROPERTIES BUILD_RPATH .)
add_dependencies(SpyTester SpyLoader)

# Tester executables
add_executable(BasicTest test/BasicTest/BasicTest.cpp include/SpiedProgram.h test/WrapperTest/WrapperTest.cpp test/WrapperTest/WrapperTestedLib.cpp test/TLSTest/TLSTest.cpp source/DynamicNamespace.cpp include/DynamicNamespace.h source/Relinkage.cpp include/Relinkage.h)
target_compile_options(BasicTest PRIVATE -fPIC -g )
target_link_libraries(BasicTest SpyTester TestLib)

add_executable(ThreadIdExe test/ThreadIdentification/Tester.cpp include/SpiedProgram.h)
target_compile_options(ThreadIdExe PRIVATE -fPIC -g )
target_link_libraries(ThreadIdExe SpyTester ThreadIdLib)

add_executable(TLSTest test/TLSTest/TLSTest.cpp)
target_compile_options(TLSTest PRIVATE -fPIC -g )
target_link_libraries(TLSTest SpyTester)

add_executable(WrapperTest test/WrapperTest/WrapperTest.cpp test/WrapperTest/WrapperTestedLib.h)
target_compile_options(ThreadIdExe PRIVATE -fPIC -g )
target_link_libraries(ThreadIdExe SpyTester WrapperTestedLib)

# Tested executables
add_executable(TestProgram test/BasicTest/TestProgram.cpp)
target_compile_options(TestProgram PRIVATE -fPIC -g)
target_link_options(TestProgram PRIVATE -rdynamic)
target_link_libraries(TestProgram pthread TestLib)

add_executable(ThreadIdProgram test/ThreadIdentification/TestedExe.cpp)
target_compile_options(ThreadIdProgram PRIVATE -fPIC -g )
target_link_libraries(ThreadIdProgram pthread ThreadIdLib)

add_executable(TLSProgram test/TLSTest/TLSProgram.cpp)
target_compile_options(TLSProgram PRIVATE -fPIC -g -I../../ )
#target_link_options(TLSProgram PRIVATE -rdynamic)
target_link_libraries(TLSProgram pthread TLSTestedLib dl)

add_executable(WrapperTestedProgram test/WrapperTest/WrapperTestedProgram.cpp)
target_compile_options(WrapperTestedProgram PRIVATE -fPIC -g )
target_link_libraries(WrapperTestedProgram pthread WrapperTestedLib )

# Loader Test
add_executable(LoaderTest test/LoaderTest/LoaderTest.cpp)
target_compile_options(LoaderTest PRIVATE -fPIC -g )
target_link_libraries(LoaderTest SpyLoader)
set_target_properties(LoaderTest PROPERTIES BUILD_RPATH_USE_ORIGIN TRUE)
set_target_properties(LoaderTest PROPERTIES BUILD_RPATH .)
