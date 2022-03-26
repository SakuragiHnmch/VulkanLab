cmake_minimum_required(VERSION 3.17)
project(learn_vulkan)

set(CMAKE_CXX_STANDARD 17)


add_executable(learn_vulkan src/main.cpp src/main.cpp)

#target_add_custom_command(
#        TARGET learn_vulkan POST_BUILD
#        COMMAND ${CMAKE_COMMAND} -E
#        copy ${CMAKE_CURRENT_SOURCE_DIR}/resource ${CMAKE_CURRENT_BINARY_DIR}
#)


target_include_directories(learn_vulkan PRIVATE
        3rd/VulkanSdk/Include
        3rd/glm
        3rd/glfw-3.3.6.bin.WIN64/include
        )
target_link_directories(learn_vulkan PRIVATE
        3rd/VulkanSdk/Lib
        3rd/glfw-3.3.6.bin.WIN64/lib-vc2019
        )

target_link_libraries(learn_vulkan vulkan-1 glfw3)
