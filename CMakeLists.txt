cmake_minimum_required(VERSION 3.22)
project(storage_server)

set(CXX_STANDARD_REQUIRED true)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_BUILD_TYPE Debug)

find_package(PkgConfig REQUIRED)
find_package(jsoncpp REQUIRED)
pkg_check_modules(LIBEVENT libevent)


# Add these lines to your CMakeLists.txt
if(LIBEVENT_FOUND)
  include_directories(${LIBEVENT_INCLUDE_DIRS})
else()
  message(FATAL_ERROR "Could not find libevent using pkg-config")
endif()

include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/async_logger/logger/include)
aux_source_directory(${PROJECT_SOURCE_DIR}/src SRC_DIR)
aux_source_directory(${PROJECT_SOURCE_DIR}/async_logger/logger/src ASYNC_LOGGER_SRC)
aux_source_directory(${PROJECT_SOURCE_DIR}/test TEST_DIR)

# Find all test cpp files
file(GLOB TEST_FILES "${PROJECT_SOURCE_DIR}/test/*.cpp")

foreach(test_file ${TEST_FILES})
    # Get the filename without path and extension
    get_filename_component(test_name ${test_file} NAME_WE)
    add_executable(${test_name} ${SRC_DIR} ${ASYNC_LOGGER_SRC} ${test_file})
    target_link_libraries(${test_name} ${LIBEVENT_LIBRARIES} jsoncpp_lib 
                         ${PROJECT_SOURCE_DIR}/lib/libbundle.so 
                         ${PROJECT_SOURCE_DIR}/lib/libbase64.so)
endforeach()