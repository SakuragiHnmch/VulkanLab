cmake_minimum_required(VERSION 3.18)
project(learn_vulkan)

set(CMAKE_CXX_STANDARD 17)

# 3rd library
add_subdirectory(3rd)

find_package(Vulkan)
# Alright, no errors? Tell me what you found!
#message("Vulkan found? " ${VULKAN_FOUND})
#message("Alright, where is it? " ${Vulkan_LIBRARIES})
#message("And I canq include it? " ${Vulkan_INCLUDE_DIRS})

IF (NOT Vulkan_FOUND)
    find_library(Vulkan_LIBRARY NAMES vulkan HINTS "$ENV{VULKAN_SDK}/lib" "${CMAKE_SOURCE_DIR}/libs/vulkan" REQUIRED)
    IF (Vulkan_LIBRARY)
        set(Vulkan_FOUND ON)
        MESSAGE("Using bundled Vulkan library version")
    ENDIF()
ENDIF()

add_executable(learn_vulkan src/main.cpp)

target_include_directories(learn_vulkan PRIVATE
        ${Vulkan_INCLUDE_DIRS}
        3rd/glm
        3rd/glfw-3.3.6/include
        3rd/stb_image
        3rd/tinyobjloader
        )
target_link_directories(learn_vulkan PRIVATE
#        ${Vulkan_LIBRARY}
        )

target_link_libraries(learn_vulkan ${Vulkan_LIBRARIES} glfw)
