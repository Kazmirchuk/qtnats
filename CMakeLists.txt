cmake_minimum_required(VERSION 3.14)

project(QtNats)

include(CMakePrintHelpers)
include(FetchContent)

set(default_build_type "Release")
option(QTNATS_BUILD_LIB_STATIC "Build static library" ON) #unused yet

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 COMPONENTS Core Test REQUIRED)

# show that we're git-cloning cnats
set(FETCHCONTENT_QUIET FALSE)
FetchContent_Declare(
  cnats
  GIT_REPOSITORY https://github.com/nats-io/nats.c.git
  GIT_TAG        v3.1.1
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
file(GLOB SOURCES "test/*.cpp")
add_executable(qtnats_test ${SOURCES})
target_link_libraries(qtnats_test qtnats Qt::Test)





