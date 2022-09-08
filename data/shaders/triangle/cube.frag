#version 450
layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec3 Normal;
layout(location = 1) in vec3 FragPos;
layout(location = 2) in vec2 TexCoords;

layout (binding = 1) uniform sampler2D diffuse;
layout (binding = 2) uniform sampler2D specular;

void main() {
    FragColor = texture(diffuse, TexCoords) + texture(specular, TexCoords);
}