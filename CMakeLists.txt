cmake_minimum_required(VERSION 3.16)

project(QtNatsProject VERSION 0.1.0)

include(GenerateExportHeader)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_AUTOMOC ON)

find_package(cnats CONFIG QUIET)
if(NOT cnats_FOUND)
    message(STATUS "cnats not found via find_package, fetching via FetchContent...")
    include(FetchContent)
    FetchContent_Declare(cnats
        URL https://github.com/nats-io/nats.c/archive/refs/tags/v3.12.0.tar.gz
        URL_HASH SHA256=06b64d7045fd618c98e5608001b384bdbfa6a17718dba64e732ba72a6f00649b
    )
    set(NATS_BUILD_WITH_TLS ON CACHE BOOL "" FORCE)
    set(NATS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(NATS_BUILD_STREAMING OFF CACHE BOOL "" FORCE)
    set(NATS_BUILD_LIB_SHARED OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING_SAVED "${BUILD_TESTING}")
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Populate(cnats)
    add_subdirectory(${cnats_SOURCE_DIR} ${cnats_BINARY_DIR} EXCLUDE_FROM_ALL)
    set(BUILD_TESTING "${BUILD_TESTING_SAVED}" CACHE BOOL "" FORCE)

    # cnats exposes <nats.h> at build time, but vcpkg installs it as <nats/nats.h>.
    # Create a wrapper so #include <nats/nats.h> works in both cases.
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cnats_include/nats")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cnats_include/nats/nats.h"
        "#include <nats.h>\n")
    target_include_directories(nats_static PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/cnats_include>)

endif()

if(cnats_FOUND)
    set(CNATS_TARGET cnats::nats_static)
else()
    set(CNATS_TARGET nats_static)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)

# ------------- qtnats library ----------------------
add_library(qtnats
    STATIC
        src/qtnats/qtnats_p.cpp
        src/qtnats/jetstream.cpp
        src/qtnats/qtnats.cpp
        src/qtnats/qtnats.h
        src/qtnats/qtnats_p.h
        ${CMAKE_CURRENT_BINARY_DIR}/qtnats/qtnats_export.h
)

target_include_directories(qtnats
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(qtnats PUBLIC ${CNATS_TARGET} Qt6::Core)

set_target_properties(qtnats PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0.1
    EXPORT_NAME qtnats
)

GENERATE_EXPORT_HEADER(qtnats
    EXPORT_FILE_NAME qtnats/qtnats_export.h
)

# ------------- install rules ----------------------
# Install/export rules require cnats from find_package (e.g. vcpkg).
# When cnats is fetched via FetchContent, skip these to avoid conflicts
# with cnats's own export sets. FetchContent consumers use add_subdirectory
# and don't need install targets.
if(cnats_FOUND)
    install(TARGETS qtnats
        EXPORT qtnats-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    install(FILES
        src/qtnats/qtnats.h
        src/qtnats/qtnats_p.h
        ${CMAKE_CURRENT_BINARY_DIR}/qtnats/qtnats_export.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qtnats
    )

    install(EXPORT qtnats-targets
        NAMESPACE qtnats::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qtnats
    )

    include(CMakePackageConfigHelpers)

    configure_package_config_file(
        cmake/qtnats-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/qtnats-config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qtnats
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/qtnats-config-version.cmake
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/qtnats-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/qtnats-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qtnats
    )
endif()

# integration tests (these spin up nats server and nats client)
set(BUILD_QT_NATS_INTEGRATION_TESTING OFF CACHE BOOL "Build qtnats integration tests")
if (BUILD_QT_NATS_INTEGRATION_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(test_core test/test_core.cpp)
    add_test(NAME test_core COMMAND test_core)
    target_link_libraries(test_core PRIVATE qtnats Qt::Test)

    add_executable(test_jetstream test/test_jetstream.cpp)
    add_test(NAME test_jetstream COMMAND test_jetstream)
    target_link_libraries(test_jetstream PRIVATE qtnats Qt::Test)
endif ()
