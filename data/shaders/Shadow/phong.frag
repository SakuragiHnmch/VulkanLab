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
	int sampleNum;
	float PCFRadius;
	float lightWidth;
} pushConsts;

#define ambient 0.1

// Shadow map related variables
#define NUM_SAMPLES 100
#define BLOCKER_SEARCH_NUM_SAMPLES 20
#define NUM_RINGS 10

#define EPS 1e-3
#define PI 3.141592653589793
#define PI2 6.283185307179586

float rand_1to1(float x) { 
	// [-1, 1]
	return fract(sin(x) * 10000.0);
}

float rand_2to1(vec2 uv) { 
	// [0, 1]
	const float a = 12.9898, b = 78.233, c = 43758.5453;
	float dt = dot(uv.xy, vec2(a, b)), sn = mod(dt, PI);
	return fract(sin(sn) * c);
}

vec2 poissonDisk[NUM_SAMPLES];

void poissonDiskSamples(vec2 randomSeed) {
	float ANGLE_STEP = PI2 * float(NUM_RINGS) / float(NUM_SAMPLES);
	float INV_NUM_SAMPLES = 1.0 / float(NUM_SAMPLES);

	float angle = rand_2to1(randomSeed) * PI2;
	float radius = INV_NUM_SAMPLES;
	float radiusStep = radius;

	for( int i = 0; i < NUM_SAMPLES; i ++ ) {
		poissonDisk[i] = vec2(cos(angle), sin(angle)) * pow(radius, 0.75);
		radius += radiusStep;
		angle += ANGLE_STEP;
	}
}

float Bias() {
	vec3 L = normalize(inLightVec);
	vec3 N = normalize(inNormal);
	return max(0.001 * (1 - dot(N, L)), 0.001);
}

float textureProj(vec4 shadowCoord, vec2 off)
{
    float bias = Bias();
	float visibility = 1.0;
	if (shadowCoord.z > 0.0 && shadowCoord.z < 1.0)
	{
		float dist = texture(shadowMap, shadowCoord.st + off).r;
		if (shadowCoord.w > 0.0 && dist + bias < shadowCoord.z)
		{
			visibility = ambient;
		}
	}
	return visibility;
}

float filterPCF(vec4 shadowCoord, float radius)
{
	ivec2 texDim = textureSize(shadowMap, 0);
	float filterSize = 1.0 / float(texDim.x) * radius;

	// generate poissonDisk
	poissonDiskSamples(shadowCoord.xy);

	float shadowFactor = 0.0;
	for (int i = 0; i < pushConsts.sampleNum; i++)
	{
		vec2 offset = poissonDisk[i] * filterSize;
		shadowFactor += textureProj(shadowCoord, offset);
	}
	return shadowFactor / float(pushConsts.sampleNum);
}

float getBlockerDepth(vec4 shadowCoord) {
	ivec2 texDim = textureSize(shadowMap, 0);
	float radius = 2.0;
	float filterSize = 1.0 / float(texDim.x) * radius;

	vec2 uv = shadowCoord.xy;
	float zReceiver = shadowCoord.z;

	// generate poissonDisk
	poissonDiskSamples(uv);

	float depth = 0.0;
	int cnt = 0;
	for (int i = 0; i < BLOCKER_SEARCH_NUM_SAMPLES; i++) {
		vec2 offset = poissonDisk[i] * filterSize;
		float zBlocker = textureProj(shadowCoord, offset);
		if (zBlocker < zReceiver) {
			depth += zBlocker;
			cnt++;
		}
	}

	return depth / float(cnt);
}

float PCSS(vec4 shadowCoord) {
	float avgBlockerDepth = getBlockerDepth(shadowCoord);

	if (avgBlockerDepth == 0.0) {
		// this fragment is totally not in shadow
		// set the visibility to 1
		return 1.0;
	}

	float zReceiver = shadowCoord.z;
	float penumbraSize = max((zReceiver - avgBlockerDepth), 0.0) / avgBlockerDepth * pushConsts.lightWidth;

	return filterPCF(shadowCoord, penumbraSize);
}

void main() 
{
	float visibility = 1;
	if (pushConsts.enablePCSS == 1) {
		visibility = PCSS(inShadowCoord / inShadowCoord.w);
	} else if (pushConsts.enablePCF == 1) {
		visibility = filterPCF(inShadowCoord / inShadowCoord.w, pushConsts.PCFRadius);
	} else {
		visibility = textureProj(inShadowCoord / inShadowCoord.w, vec2(0.0));
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