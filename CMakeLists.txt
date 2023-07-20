CMAKE_MINIMUM_REQUIRED(VERSION 3.23)

PROJECT(
    "SpyTester"
    DESCRIPTION "S28 - Outil de test intrusif"
    VERSION 0.0.1
    LANGUAGES C CXX ASM
)

    ##############################
    # C++ Standard Configuration #
    ##############################

SET(CMAKE_CXX_STANDARD          17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_EXTENSIONS        ON)

    #####################
    # Project Variables #
    #####################

    SET(ST_LIBRARY_NAME         SpyTester)
    SET(ST_LOADER_NAME          SpyLoader)
    SET(ST_TEST_EXECUTABLE_NAME spytester_tests)
    
    SET(ST_BASE_DIR    ${CMAKE_CURRENT_SOURCE_DIR})
    SET(ST_SOURCE_DIR  ${ST_BASE_DIR}/source)
    SET(ST_INCLUDE_DIR ${ST_BASE_DIR}/include)
    SET(ST_TEST_DIR    ${ST_BASE_DIR}/test)
    SET(ST_BIN_DIR     ${CMAKE_CURRENT_BINARY_DIR})

    ###################
    # Project Options #
    ###################

OPTION(ST_VERBOSE "Enable verbose output"                        OFF)
OPTION(ST_DEBUG   "Enable debug build (Runtime assertion)"       OFF)
OPTION(ST_TEST    "Enable test build and run (Using GoogleTest)" OFF)

    #######################
    # CMake Configuration #
    #######################

# If no build type is specified (and we're not using a multi-config generator), default to Debug
IF (NOT CMAKE_BUILD_TYPE AND NOT GENERATOR_IS_MULTI_CONFIG)
    MESSAGE(STATUS "No build type selected, default to Debug")
    SET(CMAKE_BUILD_TYPE "Debug")
ENDIF()

IF (CMAKE_BUILD_TYPE MATCHES Release)
    SET(ST_COMPILE_FLAGS "-O3;-w")
ELSE()
    # SET(ST_COMPILE_FLAGS "$<BUILD_LOCAL_INTERFACE:-g3;-Og;-save-temps;-Wall;-Wextra;-pedantic;-Werror;-Wconversion;-Wsign-conversion;-Wundef>")
    SET(ST_COMPILE_FLAGS "$<BUILD_LOCAL_INTERFACE:-g3;-Og;-save-temps;-Wall;-Wextra;-pedantic;-Wconversion;-Wsign-conversion;-Wundef>")
ENDIF()

    ##########################
    # Platform Configuration #
    ##########################

IF (NOT UNIX OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    MESSAGE(
        FATAL_ERROR
        "You're using ${CMAKE_SYSTEM} ${CMAKE_SYSTEM_PROCESSOR}, which is not supported.
        This software uses UNIX specific functions (dlopen, pthreads, ...) and x86_64 inline assembly, which is not cross-platform"
    )
ENDIF()

IF (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    MESSAGE(
        WARNING
        "You're using ${CMAKE_CXX_COMPILER_ID} v${CMAKE_CXX_COMPILER_VERSION}, which is not supported.
        This software uses GNU extensions (extended ASM), which is mostly supported by Clang, but might produce unexpected results"
    )
ELSEIF (NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    MESSAGE(
        FATAL_ERROR
        "You're using ${CMAKE_CXX_COMPILER_ID} v${CMAKE_CXX_COMPILER_VERSION}, which is not supported.
        This software uses GNU extensions (extended ASM), which is not supported on this compilers"
    )
ENDIF()

    ###################
    # Project Targets #
    ###################

    ########### Loader ###########

# Create target and add sources
ADD_LIBRARY(${ST_LOADER_NAME} SHARED)
TARGET_SOURCES(
    ${ST_LOADER_NAME} PRIVATE
        ${ST_SOURCE_DIR}/Loader.cpp
        ${ST_SOURCE_DIR}/SpyLoader.cpp 
        ${ST_SOURCE_DIR}/DynamicNamespace.cpp 
        ${ST_SOURCE_DIR}/DynamicModule.cpp 
        ${ST_SOURCE_DIR}/Relinkage.cpp
        ${ST_SOURCE_DIR}/ElfFile.cpp
        ${ST_SOURCE_DIR}/Logger.cpp
)
# Add include directories to the include path
TARGET_INCLUDE_DIRECTORIES(${ST_LOADER_NAME} PRIVATE ${ST_INCLUDE_DIR})
# Conditionally add definition based on CMake options
TARGET_COMPILE_DEFINITIONS(${ST_LOADER_NAME} PRIVATE "$<$<BOOL:${ST_VERBOSE}>:ST_VERBOSE>")
TARGET_COMPILE_DEFINITIONS(${ST_LOADER_NAME} PRIVATE "$<$<BOOL:${ST_DEBUG}>:ST_DEBUG>")
# Add compiler flags based on the configuration
TARGET_COMPILE_OPTIONS(${ST_LOADER_NAME} PRIVATE ${ST_COMPILE_FLAGS})
# Link options and libraries
TARGET_LINK_LIBRARIES(${ST_LOADER_NAME} dl pthread "$<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:-lstdc++fs>")
TARGET_LINK_OPTIONS(${ST_LOADER_NAME} PRIVATE "LINKER:-z,nodelete,-z,interpose")
# Change target properties
SET_TARGET_PROPERTIES(${ST_LOADER_NAME} PROPERTIES BUILD_RPATH_USE_ORIGIN TRUE)
SET_TARGET_PROPERTIES(${ST_LOADER_NAME} PROPERTIES BUILD_RPATH .)

    ########### Library ###########

ADD_LIBRARY(${ST_LIBRARY_NAME} SHARED)
TARGET_SOURCES(
    ${ST_LIBRARY_NAME} PRIVATE
        ${ST_SOURCE_DIR}/SpiedProgram.cpp 
        ${ST_SOURCE_DIR}/SpiedThread.cpp 
        ${ST_SOURCE_DIR}/Tracer.cpp 
        ${ST_SOURCE_DIR}/Breakpoint.cpp 
        ${ST_SOURCE_DIR}/WatchPoint.cpp 
        ${ST_SOURCE_DIR}/CallbackHandler.cpp
        ${ST_SOURCE_DIR}/Logger.cpp
)
# Add include directories to the include path
TARGET_INCLUDE_DIRECTORIES(${ST_LIBRARY_NAME} PRIVATE ${ST_INCLUDE_DIR})
# Conditionally add definition based on CMake options
TARGET_COMPILE_DEFINITIONS(${ST_LIBRARY_NAME} PRIVATE "$<$<BOOL:${ST_VERBOSE}>:ST_VERBOSE>")
TARGET_COMPILE_DEFINITIONS(${ST_LIBRARY_NAME} PRIVATE "$<$<BOOL:${ST_DEBUG}>:ST_DEBUG>")
# Add compiler flags based on the configuration
TARGET_COMPILE_OPTIONS(${ST_LIBRARY_NAME} PRIVATE ${ST_COMPILE_FLAGS})
# Link options and libraries
TARGET_LINK_LIBRARIES(${ST_LIBRARY_NAME} dl pthread ${ST_LOADER_NAME})
# Change target properties
SET_TARGET_PROPERTIES(${ST_LIBRARY_NAME} PROPERTIES BUILD_RPATH_USE_ORIGIN TRUE)
SET_TARGET_PROPERTIES(${ST_LIBRARY_NAME} PROPERTIES BUILD_RPATH .)

ADD_DEPENDENCIES(${ST_LIBRARY_NAME} ${ST_LOADER_NAME})

    ########################
    # Project Test Targets #
    ########################

# Tested lib
ADD_LIBRARY(TestLib SHARED)
TARGET_SOURCES(
    TestLib PRIVATE
        ${ST_TEST_DIR}/BasicTest/TestLib.cpp
)
TARGET_INCLUDE_DIRECTORIES(TestLib PRIVATE ${ST_INCLUDE_DIR})
TARGET_COMPILE_OPTIONS(TestLib PRIVATE ${ST_COMPILE_FLAGS})

ADD_LIBRARY(ThreadIdLib SHARED)
TARGET_SOURCES(
    ThreadIdLib PRIVATE
        ${ST_TEST_DIR}/ThreadIdentification/TestedLib.cpp
)
TARGET_INCLUDE_DIRECTORIES(ThreadIdLib PRIVATE ${ST_INCLUDE_DIR})
TARGET_COMPILE_OPTIONS(ThreadIdLib PRIVATE ${ST_COMPILE_FLAGS})


ADD_LIBRARY(WrapperTestedLib SHARED)
TARGET_SOURCES(
    WrapperTestedLib PRIVATE
        ${ST_TEST_DIR}/WrapperTest/WrapperTestedLib.cpp
)
TARGET_INCLUDE_DIRECTORIES(WrapperTestedLib PRIVATE ${ST_INCLUDE_DIR})
TARGET_COMPILE_OPTIONS(WrapperTestedLib PRIVATE ${ST_COMPILE_FLAGS})


ADD_LIBRARY(TLSTestedLib SHARED)
TARGET_SOURCES(
    TLSTestedLib PRIVATE
        ${ST_TEST_DIR}/TLSTest/TLSTestedLib.cpp
)
TARGET_INCLUDE_DIRECTORIES(TLSTestedLib PRIVATE ${ST_INCLUDE_DIR})
TARGET_COMPILE_OPTIONS(TLSTestedLib PRIVATE ${ST_COMPILE_FLAGS} -ftls-model=initial-exec)


# Tester executables
ADD_EXECUTABLE(BasicTest)
TARGET_SOURCES(
    BasicTest PRIVATE
        ${ST_TEST_DIR}/BasicTest/BasicTest.cpp 
        ${ST_TEST_DIR}/WrapperTest/WrapperTest.cpp 
        ${ST_TEST_DIR}/WrapperTest/WrapperTestedLib.cpp
)
TARGET_INCLUDE_DIRECTORIES(BasicTest PRIVATE ${ST_INCLUDE_DIR})
TARGET_COMPILE_OPTIONS(BasicTest PRIVATE ${ST_COMPILE_FLAGS})
TARGET_LINK_LIBRARIES(BasicTest ${ST_LIBRARY_NAME} TestLib "$<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:-lstdc++fs>")


ADD_EXECUTABLE(ThreadIdExe)
TARGET_SOURCES(
    ThreadIdExe PRIVATE
        ${ST_TEST_DIR}/ThreadIdentification/Tester.cpp
)
TARGET_INCLUDE_DIRECTORIES(ThreadIdExe PRIVATE ${ST_INCLUDE_DIR})
TARGET_COMPILE_OPTIONS(ThreadIdExe PRIVATE ${ST_COMPILE_FLAGS})
TARGET_LINK_LIBRARIES(ThreadIdExe ${ST_LIBRARY_NAME} ThreadIdLib)


ADD_EXECUTABLE(TLSTest)
TARGET_SOURCES(
    TLSTest PRIVATE
        ${ST_TEST_DIR}/TLSTest/TLSTest.cpp
)
TARGET_INCLUDE_DIRECTORIES(TLSTest PRIVATE ${ST_INCLUDE_DIR})
TARGET_COMPILE_OPTIONS(TLSTest PRIVATE ${ST_COMPILE_FLAGS})
TARGET_LINK_LIBRARIES(TLSTest ${ST_LIBRARY_NAME})


# ADD_EXECUTABLE(WrapperTest)
# TARGET_SOURCES(
#     WrapperTest PRIVATE
#         ${ST_TEST_DIR}/WrapperTest/WrapperTest.cpp
# )
# TARGET_INCLUDE_DIRECTORIES(WrapperTest PRIVATE ${ST_INCLUDE_DIR})
# TARGET_COMPILE_OPTIONS(ThreadIdExe PRIVATE ${ST_COMPILE_FLAGS})
# TARGET_LINK_LIBRARIES(ThreadIdExe ${ST_LIBRARY_NAME} WrapperTestedLib)


# Tested executables
ADD_EXECUTABLE(TestProgram)
TARGET_SOURCES(
    TestProgram PRIVATE
        ${ST_TEST_DIR}/BasicTest/TestProgram.cpp
)
TARGET_INCLUDE_DIRECTORIES(TestProgram PRIVATE ${ST_INCLUDE_DIR})
TARGET_COMPILE_OPTIONS(TestProgram PRIVATE ${ST_COMPILE_FLAGS})
TARGET_LINK_LIBRARIES(TestProgram pthread TestLib)
TARGET_LINK_OPTIONS(TestProgram PRIVATE -rdynamic)


ADD_EXECUTABLE(ThreadIdProgram)
TARGET_SOURCES(
    ThreadIdProgram PRIVATE
        ${ST_TEST_DIR}/ThreadIdentification/TestedExe.cpp
)
TARGET_INCLUDE_DIRECTORIES(ThreadIdProgram PRIVATE ${ST_INCLUDE_DIR})
TARGET_COMPILE_OPTIONS(ThreadIdProgram PRIVATE ${ST_COMPILE_FLAGS})
TARGET_LINK_LIBRARIES(ThreadIdProgram pthread ThreadIdLib)


ADD_EXECUTABLE(TLSProgram)
TARGET_SOURCES(
    TLSProgram PRIVATE
        ${ST_TEST_DIR}/TLSTest/TLSProgram.cpp
)
TARGET_INCLUDE_DIRECTORIES(TLSProgram PRIVATE ${ST_INCLUDE_DIR})
TARGET_COMPILE_OPTIONS(TLSProgram PRIVATE ${ST_COMPILE_FLAGS})
TARGET_LINK_LIBRARIES(TLSProgram pthread TLSTestedLib dl)


# ADD_EXECUTABLE(WrapperTestedProgram)
# TARGET_SOURCES(
#     WrapperTestedProgram PRIVATE
#         ${ST_TEST_DIR}/WrapperTest/WrapperTestedProgram.cpp
# )
# TARGET_INCLUDE_DIRECTORIES(WrapperTestedProgram PRIVATE ${ST_INCLUDE_DIR})
# TARGET_COMPILE_OPTIONS(WrapperTestedProgram PRIVATE ${ST_COMPILE_FLAGS})
# TARGET_LINK_LIBRARIES(WrapperTestedProgram pthread WrapperTestedLib)


# Loader Test
ADD_EXECUTABLE(LoaderTest)
TARGET_SOURCES(
    LoaderTest PRIVATE
        test/LoaderTest/LoaderTest.cpp
)
TARGET_INCLUDE_DIRECTORIES(LoaderTest PRIVATE ${ST_INCLUDE_DIR})
TARGET_COMPILE_OPTIONS(LoaderTest PRIVATE ${ST_COMPILE_FLAGS})
TARGET_LINK_LIBRARIES(LoaderTest ${ST_LOADER_NAME})
SET_TARGET_PROPERTIES(LoaderTest PROPERTIES BUILD_RPATH_USE_ORIGIN TRUE)
SET_TARGET_PROPERTIES(LoaderTest PROPERTIES BUILD_RPATH .)

    #################################
    # Test Generation And Execution #
    #################################

# IF (ST_TEST)
#     MESSAGE(STATUS "Building of tests enabled")

#     INCLUDE(FetchContent)
#     FETCHCONTENT_DECLARE(
#         googletest
#         GIT_REPOSITORY https://github.com/google/googletest.git
#         GIT_TAG        b796f7d44681514f58a683a3a71ff17c94edb0c1 # v1.13.0
#         GIT_SHALLOW    ON
#     )
#     FETCHCONTENT_MAKEAVAILABLE(googletest)

#     # Enable generation of tests (using GoogleTest, without using CTest directly)
#     ENABLE_TESTING()
#     # Include GoogleTest CMake macros
#     INCLUDE(GoogleTest)

#     # Test sources
#     FILE(GLOB_RECURSE ST_TEST_SOURCES CONFIGURE_DEPENDS "${ST_TEST_DIR}/*.cpp")
#     # Test executable
#     ADD_EXECUTABLE(${ST_TEST_EXECUTABLE_NAME} ${ST_TEST_SOURCES})

#     # Link test executable to GoogleTest libraries
#     TARGET_LINK_LIBRARIES(
#         ${ST_TEST_EXECUTABLE_NAME}
#         gtest gmock gtest_main
#     )

#     # No matter what, tests should always be verbose and enable debug mode.
#     # The 'ST_DEBUG' macro enable runtime assertions, which triggers crashes in some cases,
#     # and some tests check for these crashes. In production mode, these assertions are disabled
#     # to improve performances, and thus production code should never trigger this crashes.
#     TARGET_COMPILE_DEFINITIONS(${ST_TEST_EXECUTABLE_NAME} PRIVATE ST_VERBOSE ST_DEBUG)

#     # Macro added by GoogleTest to automatically add the tests inside the given executable to the test list
#     GTEST_DISCOVER_TESTS(${ST_TEST_EXECUTABLE_NAME})
# ENDIF()
