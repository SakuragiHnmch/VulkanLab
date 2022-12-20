#version 450

layout (binding = 1) uniform sampler2D samplerColor;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;

layout (binding = 0) uniform UBO 
{
	mat4 projection;
	mat4 model;
	mat4 normal;
	mat4 view;
	mat4 depthMVP;
	vec4 lightPos;
    vec4 cameraPos;
} ubo;

float LinearizeDepth(float depth)
{

  float n = 1.0;
  float f = 96.0;
  float z = depth;
  return (2.0 * n) / (f + n - z * (f - n));	
}

void main() 
{
	float depth = texture(samplerColor, inUV).r;
	//outFragColor = vec4(vec3(1.0-LinearizeDepth(depth)), 1.0);
	outFragColor = vec4(depth);
}
