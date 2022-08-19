cmake_minimum_required(VERSION 3.16)

project(QtNatsProject)

include(CMakePrintHelpers)
include(FetchContent)
include(GenerateExportHeader)

option(BUILD_QMLNATS "Build the QML NATS plugin (Qt6-only)" OFF)

set(default_build_type "Release")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)  # to find qtnats_export.h

find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
# according to https://gitlab.kitware.com/cmake/cmake/-/issues/20843 I need 2 find_package's to detect Qt5/Qt6
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Test Qml Quick REQUIRED)
message("Using Qt version ${QT_VERSION}")

# show that we're git-cloning cnats
set(FETCHCONTENT_QUIET FALSE)
FetchContent_Declare(
  cnats
  GIT_REPOSITORY https://github.com/nats-io/nats.c.git
  GIT_TAG        v3.3.0
  GIT_SHALLOW    ON
)

set(NATS_BUILD_WITH_TLS OFF)
set(NATS_BUILD_EXAMPLES OFF)
set(NATS_BUILD_STREAMING OFF)
set(NATS_BUILD_LIB_SHARED OFF)

set(BUILD_TESTING OFF)  # do not build cnats tests

FetchContent_MakeAvailable(cnats)

include_directories(${cnats_SOURCE_DIR}/src src)

# ------------- qtnats library ----------------------
file(GLOB SOURCES "src/*.cpp")
file(GLOB HEADERS "src/*.h")
add_library(qtnats SHARED ${SOURCES} ${HEADERS})
target_link_libraries(qtnats nats_static Qt::Core)

# this has no effect on Windows due to https://gitlab.kitware.com/cmake/cmake/-/issues/19618
set_target_properties(qtnats PROPERTIES VERSION 0.1.0 SOVERSION 0.1)

GENERATE_EXPORT_HEADER(qtnats)

# ------------- unit tests ----------------------
add_executable(test_core test/test_core.cpp)
add_test(NAME test_core COMMAND test_core)
target_link_libraries(test_core PRIVATE qtnats Qt::Test)

add_executable(test_jetstream test/test_jetstream.cpp)
add_test(NAME test_jetstream COMMAND test_jetstream)
target_link_libraries(test_jetstream PRIVATE qtnats Qt::Test)

if(BUILD_QMLNATS)
    if(${QT_VERSION_MAJOR} EQUAL 6)
        add_subdirectory(qml)
    else()
        message(FATAL_ERROR "NATS QML is supported only with Qt6")
    endif()
endif()
