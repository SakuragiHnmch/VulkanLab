#version 450
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

layout(location = 0) out vec3 Normal;
layout(location = 1) out vec3 FragPos;
layout(location = 2) out vec2 TexCoords;

layout (binding = 0) uniform UBO
{
    mat4 projection;
	mat4 model;
	mat4 view;
} ubo;

void main()
{
    gl_Position = ubo.projection * ubo.view * ubo.model * vec4(aPos, 1.0);
    FragPos = vec3(ubo.model * vec4(aPos, 1.0));
    Normal = aNormal;
    TexCoords = aTexCoords;
}