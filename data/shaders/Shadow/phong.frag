#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(std140, set = 1, binding = 0) uniform MaterialUbo
{
    int has_albedo_map;
    int has_normal_map;
} material;

layout(set = 1, binding = 1) uniform sampler2D albedo_sampler;
layout(set = 1, binding = 2) uniform sampler2D normal_sampler;

layout (location = 0) in vec2 inUV;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inLightPos;
layout (location = 3) in vec3 inCameraPos;
layout (location = 4) in vec3 inFragPos;
layout (location = 5) in vec3 inColor;

layout (location = 0) out vec4 outFragColor;

float specpart(vec3 L, vec3 N, vec3 H)
{
	if (dot(N, L) > 0.0)
	{
		return pow(clamp(dot(H, N), 0.0, 1.0), 64.0);
	}
	return 0.0;
}

vec3 applyNormalMap(vec3 geomnor, vec3 normap)
{
    normap = normap * 2.0 - 1.0;
    vec3 up = normalize(vec3(0.001, 1, 0.001));
    vec3 surftan = normalize(cross(geomnor, up));
    vec3 surfbinor = cross(geomnor, surftan);
    return normalize(normap.y * surftan + normap.x * surfbinor + normap.z * geomnor);
}

void main() 
{	
    vec3 inLightVec = normalize(inLightPos - inFragPos);

    vec3 normal;
    if (material.has_normal_map > 0)
    {
        normal = applyNormalMap(inNormal, texture(normal_sampler, inUV).rgb);
    }
    else
    {
        normal = inNormal;
    }

    vec4 IDiffuse;
    if (material.has_albedo_map > 0)
    {
        IDiffuse = vec4(texture(albedo_sampler, inUV).rgb * max(dot(normal, inLightVec), 0.0), 1.0);
    }
    else
    {
        IDiffuse = vec4(0.5, 0.5, 0.5, 0.5) * max(dot(normal, inLightVec), 0.0);
    }

    vec4 IAmbient = vec4(0.2, 0.2, 0.2, 1.0);

	vec3 Eye = normalize(inCameraPos - inFragPos);
	vec3 Reflected = normalize(reflect(-inLightVec, normal)); 

	vec3 halfVec = normalize(inLightVec + Eye);
	float diff = clamp(dot(inLightVec, normal), 0.0, 1.0);
	float spec = specpart(inLightVec, normal, halfVec);
	float intensity = 0.1 + diff + spec;

	float shininess = 0.75;
	vec4 ISpecular = vec4(0.5, 0.5, 0.5, 1.0) * pow(max(dot(Reflected, Eye), 0.0), 2.0) * shininess; 

	outFragColor = vec4((IAmbient + IDiffuse) * vec4(inColor, 1.0) + ISpecular);
 
	// Some manual saturation
	if (intensity > 0.95)
		outFragColor *= 2.25;
	if (intensity < 0.15)
		outFragColor = vec4(0.1);
}