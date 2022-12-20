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
	mat4 depthMVP;
	vec4 lightPos;
    vec4 cameraPos;
} ubo;

layout (location = 0) out vec2 outUV;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec3 outLightPos;
layout (location = 3) out vec3 outCameraPos;
layout (location = 4) out vec4 worldSpaceFragPos;
layout (location = 5) out vec4 lightSpaceFragPos;

void main() 
{
	gl_Position = ubo.projection * ubo.view * ubo.model * vec4(inPos, 1.0);

	outUV = inTexCoord.st;
	outNormal = normalize(mat3(ubo.normal) * inNormal);
	outLightPos = ubo.lightPos.xyz;
	outCameraPos = ubo.cameraPos.xyz;
	worldSpaceFragPos = ubo.model * vec4(inPos, 1.0);
    lightSpaceFragPos = ubo.depthMVP * vec4(inPos, 1.0);
}