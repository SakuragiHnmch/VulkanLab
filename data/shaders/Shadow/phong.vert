#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inTexCoord;
layout (location = 3) in vec3 inNormal;

layout (std140, set = 0, binding = 0) uniform UBO 
{
	mat4 projection;
	mat4 model;
	mat4 normal;
	mat4 view;
	vec3 lightPos;
    vec3 cameraPos;
} ubo;

layout (location = 0) out vec2 outUV;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec3 outLightPos;
layout (location = 3) out vec3 outCameraPos;
layout (location = 4) out vec3 outFragPos;
layout (location = 5) out vec3 outColor;

void main() 
{
	gl_Position = ubo.projection * ubo.view * ubo.model * vec4(inPos, 1.0);

	outUV = inTexCoord.st;
	outNormal = normalize(mat3(ubo.normal) * inNormal);
	outLightPos = ubo.lightPos;    
	outCameraPos = ubo.cameraPos;
	outFragPos = vec3(ubo.model * vec4(inPos, 1.0));
    outColor = inColor;
}