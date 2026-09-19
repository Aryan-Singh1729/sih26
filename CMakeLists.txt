cmake_minimum_required(VERSION 3.16)
project(raksh_depth_service VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RAKSH_BUILD_REALSENSE_SERVICE "Build the live SR300 executable" ON)
option(RAKSH_BUILD_TESTS "Build deterministic unit tests" ON)
set(REALSENSE_SOURCE_DIR "/mnt/sdcard/librealsense" CACHE PATH
    "Path to the pinned librealsense 2.50.0 source tree")
set(REALSENSE_BUILD_DIR "/mnt/sdcard/librealsense/build" CACHE PATH
    "Path to the pinned librealsense 2.50.0 build tree")

find_package(Threads REQUIRED)

add_library(raksh_depth_core
    src/depth_sampler.cpp
    src/depth_state.cpp
    src/perception.cpp
)

add_library(raksh_camera_core src/emeet_camera.cpp)
target_include_directories(raksh_camera_core PUBLIC include)
target_link_libraries(raksh_camera_core PUBLIC Threads::Threads)
target_compile_options(raksh_camera_core PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(raksh_depth_core PUBLIC include)
target_link_libraries(raksh_depth_core PUBLIC Threads::Threads)
target_compile_options(raksh_depth_core PRIVATE -Wall -Wextra -Wpedantic)

if(RAKSH_BUILD_REALSENSE_SERVICE)
    set(REALSENSE_HEADER "${REALSENSE_SOURCE_DIR}/include/librealsense2/rs.hpp")
    set(REALSENSE_LIBRARY "${REALSENSE_BUILD_DIR}/librealsense2.so.2.50.0")
    if(NOT EXISTS "${REALSENSE_HEADER}")
        message(FATAL_ERROR "Missing librealsense 2.50.0 header: ${REALSENSE_HEADER}")
    endif()
    if(NOT EXISTS "${REALSENSE_LIBRARY}")
        message(FATAL_ERROR "Missing librealsense 2.50.0 library: ${REALSENSE_LIBRARY}")
    endif()

    add_library(realsense2::realsense2 SHARED IMPORTED)
    set_target_properties(realsense2::realsense2 PROPERTIES
        IMPORTED_LOCATION "${REALSENSE_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${REALSENSE_SOURCE_DIR}/include"
    )
    add_executable(raksh_depth_service src/depth_service.cpp)
    target_link_libraries(raksh_depth_service PRIVATE
        raksh_depth_core
        realsense2::realsense2
    )
    target_compile_options(raksh_depth_service PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(raksh_perception_service src/perception_service.cpp)
    target_link_libraries(raksh_perception_service PRIVATE
        raksh_depth_core
        realsense2::realsense2
    )
    target_compile_options(raksh_perception_service PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(raksh_dashboard_server src/dashboard_server.cpp)
    target_link_libraries(raksh_dashboard_server PRIVATE
        raksh_depth_core
        raksh_camera_core
        realsense2::realsense2
        Threads::Threads
    )
    target_compile_options(raksh_dashboard_server PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(RAKSH_BUILD_TESTS)
    include(CTest)
    add_executable(depth_sampler_test tests/depth_sampler_test.cpp)
    target_link_libraries(depth_sampler_test PRIVATE raksh_depth_core)
    add_test(NAME depth_sampler_test COMMAND depth_sampler_test)

    add_executable(depth_state_test tests/depth_state_test.cpp)
    target_link_libraries(depth_state_test PRIVATE raksh_depth_core)
    add_test(NAME depth_state_test COMMAND depth_state_test)

    add_executable(perception_test tests/perception_test.cpp)
    target_link_libraries(perception_test PRIVATE raksh_depth_core)
    add_test(NAME perception_test COMMAND perception_test)
endif()
