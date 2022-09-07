file(GLOB BASE_SRC "*.cpp" "*.hpp" "*.h" "../3rd/imgui/*.cpp")
file(GLOB BASE_HEADERS "*.hpp" "*.h")

add_library(base STATIC ${BASE_SRC})
target_link_libraries(base ${Vulkan_LIBRARY} glfw)