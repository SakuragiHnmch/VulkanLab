function(buildExample EXAMPLE_NAME)
    SET(EXAMPLE_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/${EXAMPLE_NAME})
    message(STATUS "Generating project file for example in ${EXAMPLE_FOLDER}")

    file(GLOB SOURCE ${EXAMPLE_FOLDER}/*.h ${EXAMPLE_FOLDER}/*.cpp)

	IF(${EXAMPLE_NAME} STREQUAL "imgui")
        file(GLOB ADD_SOURCE "../external/imgui/*.cpp")
        SET(SOURCE ${SOURCE} ${ADD_SOURCE})
    ENDIF()
    # Add shaders
	set(SHADER_DIR_GLSL "../data/shaders/${EXAMPLE_NAME}")
	file(GLOB SHADERS_GLSL "${SHADER_DIR_GLSL}/*.vert" "${SHADER_DIR_GLSL}/*.frag" "${SHADER_DIR_GLSL}/*.comp" "${SHADER_DIR_GLSL}/*.geom" "${SHADER_DIR_GLSL}/*.tesc" "${SHADER_DIR_GLSL}/*.tese" "${SHADER_DIR_GLSL}/*.mesh" "${SHADER_DIR_GLSL}/*.task" "${SHADER_DIR_GLSL}/*.rgen" "${SHADER_DIR_GLSL}/*.rchit" "${SHADER_DIR_GLSL}/*.rmiss" "${SHADER_DIR_GLSL}/*.rcall")
	source_group("Shaders\\GLSL" FILES ${SHADERS_GLSL})

    add_executable(${EXAMPLE_NAME} ${SOURCE})
    target_link_libraries(${EXAMPLE_NAME} base ${Vulkan_LIBRARY} glfw assimp)
endfunction(buildExample)

function(buildExamples)
	foreach(EXAMPLE ${EXAMPLES})
		buildExample(${EXAMPLE})
	endforeach(EXAMPLE)
endfunction(buildExamples)

set(EXAMPLES
    TexturedCube
    Shadow
)

buildExamples()
