#version 430

#include "common.h"

layout(binding = 0) uniform sampler2D diffuseTex;

in vec3 v_worldpos;
in vec2 v_diffusetexcoord;
in vec4 v_color;
in vec4 v_projpos;

layout(location = 0) out vec4 out_Diffuse;

void main()
{
#if defined(TRIAPI_HUD_SPACE_ENABLED)
	vec4 baseColor = texture(diffuseTex, v_diffusetexcoord.xy);

	#if defined(ALPHA_TEST_ENABLED)
        float alpha = baseColor.a;
        if (alpha < 0.001)
            discard;
	#endif

	vec4 finalColor = baseColor * v_color;

	out_Diffuse = finalColor;
#else
	ClipPlaneTest(v_worldpos.xyz, -GetCameraVForward( GetCameraViewIndex() ));

	vec4 baseColor = texture(diffuseTex, v_diffusetexcoord.xy);

	#if defined(ALPHA_TEST_ENABLED)
        float alpha = baseColor.a;
        if (alpha < 0.001)
            discard;
	#endif

	vec4 lightmapColor = v_color;

	lightmapColor.r = clamp(lightmapColor.r, 0.0, 1.0);
	lightmapColor.g = clamp(lightmapColor.g, 0.0, 1.0);
	lightmapColor.b = clamp(lightmapColor.b, 0.0, 1.0);
	lightmapColor.a = clamp(lightmapColor.a, 0.0, 1.0);
	
	// Native texture modulation and fog precede framebuffer encoding.
	vec4 modulatedColor = baseColor * lightmapColor;
	float flDistanceToFragment = abs((GetCameraWorldMatrix(0) * vec4(v_worldpos, 1.0)).z);
	float fogFactor = 1.0;
	#if defined(LINEAR_FOG_ENABLED)
		fogFactor = (SceneUBO.fogEnd - flDistanceToFragment) / (SceneUBO.fogEnd - SceneUBO.fogStart);
	#elif defined(EXP_FOG_ENABLED)
		fogFactor = exp(-SceneUBO.fogDensity * flDistanceToFragment);
	#elif defined(EXP2_FOG_ENABLED)
		fogFactor = exp(-pow(SceneUBO.fogDensity * flDistanceToFragment, 2.0));
	#endif
	modulatedColor.rgb = mix(SceneUBO.fogColor.rgb, modulatedColor.rgb, clamp(fogFactor, 0.0, 1.0));
	// Linear scene targets still need decoding, without the Renderer's
	// optional blend/fog shifts or texture-only gamma remapping.
	vec4 finalColor = ProcessOtherGammaColor(modulatedColor);

	#if defined(OIT_BLEND_ENABLED)

		GatherFragment(finalColor);

	#endif

	out_Diffuse = clamp(finalColor, 0.0, 64.0);
#endif
	
}
