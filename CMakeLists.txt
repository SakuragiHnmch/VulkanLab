cmake_minimum_required(VERSION 3.17)
set(NAME VulkanLab)
project(${NAME})

set(CMAKE_CXX_STANDARD 11)
include_directories(3rd)
include_directories(3rd/glm)
include_directories(3rd/imgui)
include_directories(3rd/tinygltf)
include_directories(3rd/stb_image)
include_directories(3rd/glfw-3.3.6/include)
include_directories(3rd/tinyobj)
include_directories(base)

find_package(Vulkan)

if (NOT Vulkan_FOUND)
    message(FATAL_ERROR "Could not find Vulkan library!")
else()
    message(STATUS "Found Vulkan lib: " ${Vulkan_LIBRARY})
    message(STATUS ${Vulkan_INCLUDE_DIR})
endif()


set(CMAKE_CXX_FLAGS "-DVK_USE_PLATFORM_MACOS_MVK")

include_directories(${Vulkan_INCLUDE_DIR})
link_libraries(${Vulkan_LIBRARIES})

add_subdirectory(3rd)
add_subdirectory(base)
add_subdirectory(examples)