cmake_minimum_required(VERSION 3.17)
project(learn_vulkan)

set(CMAKE_CXX_STANDARD 17)

# 3rd library
add_subdirectory(3rd)

find_package(Vulkan REQUIRED)

add_executable(learn_vulkan src/main.cpp)

#target_add_custom_command(
#        TARGET learn_vulkan POST_BUILD
#        COMMAND ${CMAKE_COMMAND} -E
#        copy ${CMAKE_CURRENT_SOURCE_DIR}/resource ${CMAKE_CURRENT_BINARY_DIR}
#)


target_include_directories(learn_vulkan PRIVATE
        ${Vulkan_INCLUDE_DIRS}
        3rd/glm
        3rd/glfw-3.3.6/include
        3rd/stb_image
        )
target_link_directories(learn_vulkan PRIVATE
#        ${Vulkan_LIBRARY}
        )

target_link_libraries(learn_vulkan ${Vulkan_LIBRARIES} glfw)
