cmake_minimum_required(VERSION 3.17)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
set(NAME learn_vulkan)
project(${NAME})

set(CMAKE_CXX_STANDARD 11)
include_directories(3rd)
include_directories(3rd/glm)
include_directories(3rd/imgui)
include_directories(3rd/tinygltf)
include_directories(3rd/ktx/include)
include_directories(3rd/ktx/other_include)
include_directories(base)

if (NOT CMAKE_VERSION VERSION_LESS 3.7.0)
    message(STATUS "Using module to find Vulkan")
    find_package(Vulkan)
endif()

if (NOT Vulkan_FOUND)
    message(FATAL_ERROR "Could not find Vulkan library!")
else()
    message(STATUS ${Vulkan_LIBRARY})
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")

set(CMAKE_CXX_FLAGS "-DVK_USE_PLATFORM_MACOS_MVK")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNOMINMAX -D_USE_MATH_DEFINES")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fobjc-arc -xobjective-c++")

# Clang specific stuff
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-switch-enum")
endif()

link_libraries(${Vulkan_LIBRARIES} "-framework AppKit" "-framework QuartCore")

add_subdirectory(base)
add_subdirectory(examples)