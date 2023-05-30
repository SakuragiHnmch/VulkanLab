#version 450

layout(binding = 1) uniform sampler2D shadowMap;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec3 inViewVec;
layout (location = 3) in vec3 inLightVec;
layout (location = 4) in vec4 inShadowCoord;
layout (location = 5) in vec2 uv;

layout (location = 0) out vec4 outFragColor;

layout (set = 1, binding = 0) uniform sampler2D albedoMap;

layout(push_constant) uniform PushConsts {
	int enablePCF;
	int enablePCSS;
} pushConsts;

#define ambient 0.1

float textureProj(vec4 shadowCoord, vec2 off)
{
    float bias = 0.001;
	float visibility = 1.0;
	if ( shadowCoord.z > 0.0 && shadowCoord.z < 1.0 )
	{
		float dist = texture( shadowMap, shadowCoord.st + off ).r;
		if ( shadowCoord.w > 0.0 && dist + bias < shadowCoord.z )
		{
			visibility = ambient;
		}
	}
	return visibility;
}

float filterPCF(vec4 shadowCoord)
{
	ivec2 texDim = textureSize(shadowMap, 0);
	float scale = 1.5;
	float dx = scale * 1.0 / float(texDim.x);
	float dy = scale * 1.0 / float(texDim.y);

	float shadowFactor = 0.0;
	int count = 0;
	int range = 1;

	for (int x = -range; x <= range; x++)
	{
		for (int y = -range; y <= range; y++)
		{
			shadowFactor += textureProj(shadowCoord, vec2(dx*x, dy*y));
			count++;
		}

	}
	return shadowFactor / count;
}

void main() 
{
	float visibility = 1;
	if (pushConsts.enablePCF == 0) {
		visibility = textureProj(inShadowCoord / inShadowCoord.w, vec2(0.0));
	} else {
		visibility = filterPCF(inShadowCoord / inShadowCoord.w);
	}

	vec3 abledoColor = texture(albedoMap, uv).xyz;

	vec3 N = normalize(inNormal);
	vec3 L = normalize(inLightVec);
	vec3 V = normalize(inViewVec);
	vec3 R = normalize(-reflect(L, N));
	vec3 diffuse = max(dot(N, L), ambient) * abledoColor * 0.7;
    vec3 specular = pow(max(dot(R, V), 0.0), 16.0) * vec3(0.5);

	outFragColor = vec4((diffuse + specular) * visibility, 1.0);
	//outFragColor = vec4(1.0);
}