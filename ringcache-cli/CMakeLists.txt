cmake_minimum_required(VERSION 3.20)
project(simple_ringcache LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# MurmurHash3 from aappleby/smhasher. If the smhasher folder is already next to
# this file (it ships with the project), use it and stay offline. Otherwise
# download it, pinned to one commit.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/smhasher/src/MurmurHash3.cpp)
  set(smhasher_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/smhasher)
else()
  include(FetchContent)
  FetchContent_Declare(smhasher
    GIT_REPOSITORY https://github.com/aappleby/smhasher.git
    GIT_TAG        07bb4de10a63e8cc2e1724865454eba635742383
    SOURCE_SUBDIR  do-not-configure)
  FetchContent_MakeAvailable(smhasher)
endif()

add_library(murmurhash3 STATIC ${smhasher_SOURCE_DIR}/src/MurmurHash3.cpp)
target_include_directories(murmurhash3 SYSTEM PUBLIC ${smhasher_SOURCE_DIR}/src)

add_executable(demo main.cpp)
target_link_libraries(demo PRIVATE murmurhash3)
target_compile_options(demo PRIVATE -Wall -Wextra -Wpedantic)
