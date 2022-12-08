#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 1) uniform sampler2D shadowMap;

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
layout (location = 4) in vec3 worldSpaceFragPos;
layout (location = 5) in vec4 lightSpaceFragPos;

layout (location = 0) out vec4 outFragColor;

float ShadowCalculation(vec4 lightspaceFragPos)
{
    // perform perspective divide
    vec3 shadowCoords = lightSpaceFragPos.xyz / lightSpaceFragPos.w;

    // transform to [0, 1] range
    shadowCoords = shadowCoords * 0.5 + 0.5;

    // get closest depth of current frag in shadowMap
    float closestDepth = texture(shadowMap, shadowCoords.xy).r;

    // get depth of current fragment from light`s perspective
    float currentDepth = shadowCoords.z;

    // check wether current fragment is in shadodw
    float shadow = currentDepth > closestDepth ? 1.0 : 0.0;

    return shadow;
}

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
    vec3 inLightVec = normalize(inLightPos - worldSpaceFragPos.xyz);

    vec3 normal;
    if (material.has_normal_map > 0)
    {
        normal = applyNormalMap(inNormal, texture(normal_sampler, inUV).rgb);
    }
    else
    {
        normal = inNormal;
    }

    vec3 IDiffuse;
    if (material.has_albedo_map > 0)
    {
        IDiffuse = texture(albedo_sampler, inUV).rgb * max(dot(normal, inLightVec), 0.0);
    }
    else
    {
        IDiffuse = vec3(0.5) * max(dot(normal, inLightVec), 0.0);
    }

    vec3 IAmbient = vec3(0.2);

	vec3 Eye = normalize(inCameraPos - worldSpaceFragPos.xyz);
	vec3 Reflected = normalize(reflect(-inLightVec, normal)); 

	float shininess = 0.75;
	vec3 ISpecular = vec3(0.5) * pow(max(dot(Reflected, Eye), 0.0), 2.0) * shininess;

    float shadow = ShadowCalculation(lightSpaceFragPos);
	outFragColor = vec4((IAmbient + (1.0 - shadow) * (IDiffuse + ISpecular)), 1.0);
}