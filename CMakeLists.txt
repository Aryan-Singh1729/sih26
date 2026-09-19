cmake_minimum_required(VERSION 3.16)
project(raksh_depth_service VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RAKSH_BUILD_REALSENSE_SERVICE "Build the live SR300 executable" ON)
option(RAKSH_BUILD_TESTS "Build deterministic unit tests" ON)

find_package(Threads REQUIRED)

add_library(raksh_depth_core
    src/depth_sampler.cpp
    src/depth_state.cpp
)
target_include_directories(raksh_depth_core PUBLIC include)
target_link_libraries(raksh_depth_core PUBLIC Threads::Threads)
target_compile_options(raksh_depth_core PRIVATE -Wall -Wextra -Wpedantic)

if(RAKSH_BUILD_REALSENSE_SERVICE)
    find_package(realsense2 2.50 REQUIRED CONFIG)
    if(NOT realsense2_VERSION VERSION_EQUAL "2.50.0")
        message(FATAL_ERROR
            "Raksh Depth requires librealsense 2.50.0; found ${realsense2_VERSION}")
    endif()
    add_executable(raksh_depth_service src/depth_service.cpp)
    target_link_libraries(raksh_depth_service PRIVATE
        raksh_depth_core
        realsense2::realsense2
    )
    target_compile_options(raksh_depth_service PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(RAKSH_BUILD_TESTS)
    include(CTest)
    add_executable(depth_sampler_test tests/depth_sampler_test.cpp)
    target_link_libraries(depth_sampler_test PRIVATE raksh_depth_core)
    add_test(NAME depth_sampler_test COMMAND depth_sampler_test)

    add_executable(depth_state_test tests/depth_state_test.cpp)
    target_link_libraries(depth_state_test PRIVATE raksh_depth_core)
    add_test(NAME depth_state_test COMMAND depth_state_test)
endif()
