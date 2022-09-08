#version 450
layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec3 Normal;
layout(location = 1) in vec3 FragPos;
layout(location = 2) in vec2 TexCoords;

layout(binding = 3) uniform UboFS {
    vec3 position;
    vec3 viewPos;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
} uboFS;

layout (binding = 1) uniform sampler2D diffuse;
layout (binding = 2) uniform sampler2D specular;

void main() {
    vec3 ambient = uboFS.ambient * vec3(texture(diffuse, TexCoords));

    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(uboFS.position - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = uboFS.diffuse * diff * vec3(texture(diffuse, TexCoords));

    vec3 viewDir = normalize(uboFS.viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(reflectDir, viewDir), 0.0), 4.0);
    vec3 specular = uboFS.specular * spec * vec3(texture(specular, TexCoords));

    vec3 result = ambient + diffuse + specular;
    FragColor = vec4(result, 1.0);
}