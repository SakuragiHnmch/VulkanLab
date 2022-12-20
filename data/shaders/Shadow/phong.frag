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
    vec3 projCoords = lightSpaceFragPos.xyz / lightSpaceFragPos.w;
    vec3 shadowCoords = projCoords * 0.5 + 0.5;

    vec4 depth = texture(shadowMap, shadowCoords.xy);
    vec4 bitShift = vec4(1.0, 1.0/256.0, 1.0/(256.0*256.0), 1.0/(256.0*256.0*256.0));
    float depthUnpack = dot(depth, bitShift);

    if (depthUnpack < shadowCoords.z - 0.005)
        return 0.0;

    return 1.0;
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

    vec3 mColor;
    if (material.has_albedo_map > 0)
    {
        mColor = texture(albedo_sampler, inUV).rgb;
    }
    else
    {
        mColor = vec3(0.5);
    }

    vec3 ambient = 0.05 * mColor;

    float diff = max(dot(normal, inLightVec), 0.0);
    vec3 diffuse = diff * mColor;

	vec3 Eye = normalize(inCameraPos - worldSpaceFragPos.xyz);
	vec3 Reflected = normalize(reflect(-inLightVec, normal)); 

	float spec = pow(max(dot(Reflected, Eye), 0.0), 32.0);
    vec3 specular = spec * vec3(1.0);

    float visibility = 1.0;
    //visibility = ShadowCalculation(lightSpaceFragPos);

    vec3 radiance = (ambient + diffuse + specular);
    outFragColor = vec4(radiance * visibility, 1.0);
}