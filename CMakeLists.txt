cmake_minimum_required(VERSION 3.14)
project(XiSort LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable OpenMP if available
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
endif()

option(XISORT_BUILD_TESTS "Build test harness" ON)

add_library(xisort_core src/xisort.cpp)

add_executable(xisort src/xisort_cli.cpp)
target_link_libraries(xisort PRIVATE xisort_core)

if(XISORT_BUILD_TESTS)
    add_executable(xisort_tests src/xisort_test.cpp)
    target_link_libraries(xisort_tests PRIVATE xisort_core)
endif()

# install targets
install(TARGETS xisort DESTINATION bin)
