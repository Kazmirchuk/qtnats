cmake_minimum_required(VERSION 3.14)

project(QtNats)

include(CMakePrintHelpers)
include(FetchContent)

set(default_build_type "Release")
option(QTNATS_BUILD_LIB_STATIC "Build static library" ON) #unused yet

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 COMPONENTS Core Test REQUIRED)

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

# also in cnats
set(BUILD_TESTING OFF)

FetchContent_MakeAvailable(cnats)

include_directories(${cnats_SOURCE_DIR}/src src)

# ------------- qtnats library ----------------------
file(GLOB SOURCES "src/*.cpp")
file(GLOB HEADERS "src/*.h")
add_library(qtnats STATIC ${SOURCES} ${HEADERS})
target_link_libraries(qtnats nats_static Qt::Core)
set_target_properties(qtnats PROPERTIES VERSION 0.1.0 SOVERSION 0.1)



# ------------- unit tests ----------------------
add_executable(test_core test/test_core.cpp)
add_test(NAME test_core COMMAND test_core)
target_link_libraries(test_core PRIVATE qtnats Qt5::Test)

add_executable(test_jetstream test/test_jetstream.cpp)
add_test(NAME test_jetstream COMMAND test_jetstream)
target_link_libraries(test_jetstream PRIVATE qtnats Qt5::Test)
