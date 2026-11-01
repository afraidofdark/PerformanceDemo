#version 300 es
precision highp sampler2D;
precision highp sampler2DArray;
precision highp samplerCube;
precision highp sampler3D;
precision highp int;
precision highp float;
precision highp int;
// ==== vulkanCompatInc.shader ====

  
#ifndef TK_VULKAN_COMPAT
#define TK_VULKAN_COMPAT

// Vulkan/GL single-source compatibility macros.
//
// GL ES 3.00 doesn't support layout(binding = N) on UBO blocks or samplers; in the GL path
// bindings are wired up at runtime via glUniformBlockBinding / glUniform1i. Vulkan/SPIR-V
// requires explicit binding qualifiers in the source.
//
// To keep one .shader file per program and zero runtime regex on Vulkan's side, we wrap
// every UBO block declaration and every sampler declaration with these macros. The macro
// expansion is conditioned on the VULKAN macro that VulkanShader::CompileGlslToSpirv sets
// via shaderc options.AddMacroDefinition("VULKAN", "1").
//
//   GL   path: VULKAN undefined  -> macro emits the ES 3.00-friendly form (no binding= qualifier).
//   VK   path: VULKAN defined    -> macro emits the Vulkan-required layout(binding=N) form.
//
// Vulkan binding numbers are produced by shaderc's SetBindingBase(uniform_kind_buffer, 8)
// shifting every UBO binding up by kUboBindingBase. So writing TK_UBO_BINDING(3) here lands
// at SPIR-V binding 11 for Vulkan and stays a plain layout(std140) uniform on GL where the
// number 3 is bound from the C++ side via glUniformBlockBinding. Sampler bindings are not
// shifted (kTextureBindingBase = 0); s_textureN's N is its binding directly.
//
// See VulkanBindings.h for the full Vulkan binding map.

#ifdef VULKAN
  #define TK_UBO_BINDING(n)     layout(std140, binding = n)
  #define TK_SAMPLER_BINDING(n) layout(binding = n)
  #define TK_LOC(n)             layout(location = n)
  #define NDC_NEAR				0.0
  #define TEX_COORD_Y(tc)		1.0 - tc.y;         					
#else
  #define TK_UBO_BINDING(n)     layout(std140)
  #define TK_SAMPLER_BINDING(n)
  #define TK_LOC(n)
  #define NDC_NEAR				-1.0
  #define TEX_COORD_Y(tc)		tc.y;
#endif

#endif // TK_VULKAN_COMPAT
  
  

// ==== pbrCommon.shader ====

	
#ifndef PBR_COMMON_CODE
#define PBR_COMMON_CODE

const float PI = 3.14159265359;

// ---------------------------------------------------------------------------
// Helper functions (Google Filament)
// ---------------------------------------------------------------------------

float pow5(float x)
{
	float x2 = x * x;
	return x2 * x2 * x;
}

float sq(float x)
{
	return x * x;
}

// ---------------------------------------------------------------------------
// Normal Distribution Function
// Walter et al. 2007, "Microfacet Models for Refraction through Rough Surfaces"
// Google Filament implementation with fp16 overflow protection
// ---------------------------------------------------------------------------

float D_GGX(float roughness, float NoH, const vec3 h)
{
	float oneMinusNoHSquared = 1.0 - NoH * NoH;
	float a = NoH * roughness;
	float k = roughness / (oneMinusNoHSquared + a * a);
	float d = k * k * (1.0 / PI);
	return d;
}

// ---------------------------------------------------------------------------
// Visibility (masking-shadowing) functions
// Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
// ---------------------------------------------------------------------------

float V_SmithGGXCorrelated(float roughness, float NoV, float NoL)
{
	float a2 = roughness * roughness;
	float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
	float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
	return 0.5 / max(lambdaV + lambdaL, 0.0000077);
}

// Hammon 2017, "PBR Diffuse Lighting for GGX+Smith Microsurfaces"
float V_SmithGGXCorrelated_Fast(float roughness, float NoV, float NoL)
{
	return 0.5 / mix(2.0 * NoL * NoV, NoL + NoV, roughness);
}

// ---------------------------------------------------------------------------
// Fresnel
// Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"
// ---------------------------------------------------------------------------

vec3 F_Schlick(vec3 f0, float f90, float VoH)
{
	return f0 + (f90 - f0) * pow5(1.0 - VoH);
}

vec3 F_Schlick(float cosTheta, vec3 F0)
{
	return F0 + (vec3(1.0) - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 F_SchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

// ---------------------------------------------------------------------------
// Diffuse BRDFs
// ---------------------------------------------------------------------------

float Fd_Lambert()
{
	return 1.0 / PI;
}

// Burley 2012, "Physically-Based Shading at Disney"
float Fd_Burley(float roughness, float NoV, float NoL, float LoH)
{
	float f90 = 0.5 + 2.0 * roughness * LoH * LoH;
	float lightScatter = 1.0 + (f90 - 1.0) * pow5(1.0 - NoL);
	float viewScatter  = 1.0 + (f90 - 1.0) * pow5(1.0 - NoV);
	return lightScatter * viewScatter * (1.0 / PI);
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

vec3 BaseReflectivityPBR(vec3 F0, vec3 albedo, float metallic)
{
	return mix(F0, albedo, metallic);
}

// Google Filament multiscattering energy compensation
// https://google.github.io/filament/Filament.html#toc4.7.2
vec3 EnergyCompensation(vec2 dfg, vec3 f0)
{
	return 1.0 + f0 * (1.0 / max(f0 * dfg.x + dfg.y, 1e-3) - 1.0);
}

// ---------------------------------------------------------------------------
// Roughness remapping (Google Filament)
// ---------------------------------------------------------------------------

float perceptualRoughnessToRoughness(float perceptualRoughness)
{
	return perceptualRoughness * perceptualRoughness;
}

float roughnessToPerceptualRoughness(float roughness)
{
	return sqrt(roughness);
}

// ---------------------------------------------------------------------------
// Geometric Specular Anti-Aliasing
// Kaplanyan 2016, "Stable Specular Highlights"
// Tokuyoshi 2017, "Error Reduction and Simplification for Shading Anti-Aliasing"
// Tokuyoshi and Kaplanyan 2019, "Improved Geometric Specular Antialiasing"
// Google Filament implementation
// ---------------------------------------------------------------------------

const float SPECULAR_AA_VARIANCE  = 0.15;
const float SPECULAR_AA_THRESHOLD = 0.25;

float specularAntiAliasing(float perceptualRoughness, const vec3 worldNormal)
{
	vec3 du = dFdx(worldNormal);
	vec3 dv = dFdy(worldNormal);

	float variance = SPECULAR_AA_VARIANCE * (dot(du, du) + dot(dv, dv));
	float roughness = perceptualRoughnessToRoughness(perceptualRoughness);
	float kernelRoughness = min(2.0 * variance, SPECULAR_AA_THRESHOLD);
	float squareRoughness = clamp(roughness * roughness + kernelRoughness, 0.0, 1.0);

	return roughnessToPerceptualRoughness(sqrt(squareRoughness));
}

// ---------------------------------------------------------------------------
// BRDF dispatch functions (Filament style)
// ---------------------------------------------------------------------------

float distribution(float roughness, float NoH, const vec3 h)
{
	return D_GGX(roughness, NoH, h);
}

float visibility(float roughness, float NoV, float NoL)
{
	return V_SmithGGXCorrelated(roughness, NoV, NoL);
}

vec3 fresnel(vec3 f0, float LoH)
{
	float f90 = clamp(dot(f0, vec3(50.0 * 0.33)), 0.0, 1.0);
	return F_Schlick(f0, f90, LoH);
}

float diffuse(float roughness, float NoV, float NoL, float LoH)
{
	return Fd_Lambert();
}

#endif
	
	

// ==== VSMCommon.shader ====

	
  #ifndef VSM_COMMON_SHADER
  #define VSM_COMMON_SHADER

  // ---------------------------------------------------------------------------
  // EVSM Configuration (Filament style)
  // https://google.github.io/filament/Filament.html
  // ---------------------------------------------------------------------------

  const float VsmExponent = 5.54;
  const float VsmMaxMoment = 65000.0;

  // ---------------------------------------------------------------------------
  // Depth warping (Filament: evaluateEVSM)
  // ---------------------------------------------------------------------------

  float WarpDepth(float depth)
  {
      depth = 2.0 * depth - 1.0;
      return exp(VsmExponent * depth);
  }

  #endif
	
	

// ==== VSM.shader ====

	
  #ifndef VSM_SHADER
  #define VSM_SHADER

  // ---------------------------------------------------------------------------
  // Utility
  // ---------------------------------------------------------------------------

  float saturate(float value)
  {
    return clamp(value, 0.0, 1.0);
  }

  // ---------------------------------------------------------------------------
  // Chebyshev Upper Bound (Filament style)
  // https://google.github.io/filament/Filament.html
  // ---------------------------------------------------------------------------

  float ChebyshevUpperBound(vec2 moments, float mean, float minVariance, float lightBleedingReduction)
  {
      // Fast path: receiver is fully in front of the caster
      if (mean <= moments.x)
      {
          return 1.0;
      }

      // Variance with clamped minimum to reduce acne
      float variance = moments.y - (moments.x * moments.x);
      variance = max(variance, minVariance);

      // Standard Chebyshev inequality
      float d = mean - moments.x;
      float pMax = variance / (variance + d * d);

      // Light Bleeding Reduction (Filament style)
      // Remaps [lbr, 1] to [0, 1]
      pMax = saturate((pMax - lightBleedingReduction) / (1.0 - lightBleedingReduction));

      return pMax;
  }

  #endif
	
	

// ==== textureUtil.shader ====

		

	#ifndef TEXTURE_UTIL_SHADER
	#define TEXTURE_UTIL_SHADER

// Returns uv coordinates and layer: vec3(u, v, layer)
// Branch-free cubemap face lookup for shadow atlas.
// Layer order must match ShadowPass.cpp cube map rotations:
//   0: +X   1: -X   2: +Y   3: -Y   4: +Z   5: -Z
vec3 UVWToUVLayer(vec3 v)
{
	vec3 a = abs(v);

	// Determine dominant axis: 0=X, 1=Y, 2=Z.
	// step(a.yx, a.xy) = (a.x>=a.y, a.y>=a.x)
	float isX = step(a.y, a.x) * step(a.z, a.x); // 1 if X dominant
	float isY = (1.0 - isX) * step(a.z, a.y);     // 1 if Y dominant (and not X)
	float isZ = 1.0 - isX - isY;                  // 1 if Z dominant

	// Dominant axis magnitude and sign.
	float major = isX * a.x + isY * a.y + isZ * a.z;
	float signVal = isX * v.x + isY * v.y + isZ * v.z;
	float s = step(0.0, signVal) * 2.0 - 1.0; // +1 or -1

	// Face index: base + (negative ? 1 : 0).
	float layer = isX * 0.0 + isY * 2.0 + isZ * 4.0 + (1.0 - step(0.0, signVal));

	// Per-face UV mapping (matches the original convention exactly):
	//   +X: (-z/x, -y/x)   -X: ( z/|x|, -y/|x|)
	//   +Y: ( x/y,  z/y)   -Y: ( x/|y|, -z/|y|)
	//   +Z: ( x/z, -y/z)   -Z: (-x/|z|, -y/|z|)
	float inv = 1.0 / major;

	vec2 uvX = vec2(-s * v.z, -v.y) * inv;
	vec2 uvY = vec2(v.x, s * v.z) * inv;
	vec2 uvZ = vec2(s * v.x, -v.y) * inv;

	vec2 coord = isX * uvX + isY * uvY + isZ * uvZ;

	coord = coord * 0.5 + 0.5;
	return vec3(coord, layer);
}

#endif

	
	

// ==== perDrawDataInc.shader ====

	
#ifndef PER_DRAW_DATA
#define PER_DRAW_DATA

// PerDrawData UBO - slot 2 (Vulkan binding 34 after UboBindingFor remap).
//
// Mirror of `PerDrawUboLayout` in Renderer.h. Add or reorder fields ONLY in lockstep - shader
// and C++ are byte-identical std140 layouts.
//
// Migrated shaders read these fields directly: `perDraw._model * localPos`,
// `perDraw._inverseTransposeModel`, etc. No accessor macros - those would clash with bare
// `uniform <type> <name>` declarations in not-yet-migrated includes (e.g. skinning.shader's
// `uniform vec4 skinParams;` is hit if we `#define skinParams perDraw._skinParams`).
// Migrate the include first, then call sites here can be tightened.
//
// Texture samplers stay outside the UBO - they cannot live in a uniform block.
//////////////////////////////////////////

struct DrawCommandLayout
{
	vec4 global0;     // x: iblInUse, y: aoInUse, z: skyIntensity, w: pad
	vec4 global1;     // x: pointLightCount, y: spotLightCount, z: directionalLightCount, w: pad

	vec4 vol0Params;  // x: intensity, y: fadeDistance, z: interior, w: pccEnabled
	vec4 vol0Min;
	vec4 vol0Max;
	vec4 vol0InvT0;
	vec4 vol0InvT1;
	vec4 vol0InvT2;
	vec4 vol0InvT3;
	vec4 vol0WldT0;
	vec4 vol0WldT1;
	vec4 vol0WldT2;
	vec4 vol0WldT3;

	vec4 vol1Params;
	vec4 vol1Min;
	vec4 vol1Max;
	vec4 vol1InvT0;
	vec4 vol1InvT1;
	vec4 vol1InvT2;
	vec4 vol1InvT3;
	vec4 vol1WldT0;
	vec4 vol1WldT1;
	vec4 vol1WldT2;
	vec4 vol1WldT3;
};

struct MaterialDataLayout
{
	vec4 colorAlpha;        // rgb=color, a=alpha
	vec4 emissiveThreshold; // rgb=emissive, a=alphaMaskThreshold
	vec4 metallicRoughness; // x=metallic, y=roughness, z=useAlphaMask, w=diffuseInUse
	vec4 textureFlags;      // x=emissiveInUse, y=normalInUse, z=metallicRoughInUse, w=pad
};

TK_UBO_BINDING(2) uniform PerDrawData
{
	mat4 _model;
	mat4 _modelWithoutTranslate;
	mat4 _inverseModel;
	mat4 _inverseTransposeModel;
	mat4 _iblRotation;
	mat4 _iblSecondaryRotation;

	vec4 _viewportSizeAndPad;          // .xy = viewportSize

	DrawCommandLayout _drawCommand;
	MaterialDataLayout _materialData;

	ivec4 _activePointLightIndices[6]; // 24 ints packed
	ivec4 _activeSpotLightIndices[6];
	ivec4 _lightCounts;                // .x = pointCount, .y = spotCount

	vec4 _keyFrameData;
	vec4 _blendFrameData;
	vec4 _skinParams;
	vec4 _animBlendFactorAndPad;       // .x = animationBlendFactor

	// .x = first instance index in the instance texture, .y = 1 when the draw is instanced.
	// Instanced vertex shaders read the model matrix of their own instance from there, so a
	// batch of N objects becomes one draw call.
	ivec4 _instanceParams;
} perDraw;

#endif // PER_DRAW_DATA
	
	

// ==== drawDataInc.shader ====

	

	#ifndef DRAW_DATA
	#define DRAW_DATA

	// DrawCommand accessors - back-end is now `perDraw._drawCommand` (PerDrawData UBO, slot 2).
	// Function signatures kept identical so every call site (lighting/ibl/AO) stays untouched.
	// Volume index branches with a ternary; the GLSL compiler folds it when `vol` is constant.
	//////////////////////////////////////////

	// --- Global accessors ---

	bool IsIBLInUse()
	{
		return perDraw._drawCommand.global0.x > 0.5;
	}

	bool IsAmbientOcculusionInUse()
	{
		return perDraw._drawCommand.global0.y > 0.5;
	}

	float GetSkyIntensity()
	{
		return perDraw._drawCommand.global0.z;
	}

	int GetActivePointLightCount()
	{
		return int(perDraw._drawCommand.global1.x);
	}

	int GetActiveSpotLightCount()
	{
		return int(perDraw._drawCommand.global1.y);
	}

	int GetActiveDirectionalLightCount()
	{
		return int(perDraw._drawCommand.global1.z);
	}

	// --- Per-volume accessors ---

	float GetVolumeIntensity(int vol)
	{
		return vol == 0 ? perDraw._drawCommand.vol0Params.x : perDraw._drawCommand.vol1Params.x;
	}

	float GetVolumeFadeDistance(int vol)
	{
		return vol == 0 ? perDraw._drawCommand.vol0Params.y : perDraw._drawCommand.vol1Params.y;
	}

	bool IsVolumePccEnabled(int vol)
	{
		float v = vol == 0 ? perDraw._drawCommand.vol0Params.w : perDraw._drawCommand.vol1Params.w;
		return v > 0.5;
	}

	bool IsVolumeInterior(int vol)
	{
		float v = vol == 0 ? perDraw._drawCommand.vol0Params.z : perDraw._drawCommand.vol1Params.z;
		return v > 0.5;
	}

	vec3 GetVolumeMin(int vol)
	{
		return (vol == 0 ? perDraw._drawCommand.vol0Min : perDraw._drawCommand.vol1Min).xyz;
	}

	vec3 GetVolumeMax(int vol)
	{
		return (vol == 0 ? perDraw._drawCommand.vol0Max : perDraw._drawCommand.vol1Max).xyz;
	}

	mat4 GetVolumeInverseTransform(int vol)
	{
		if (vol == 0)
		{
			return mat4(perDraw._drawCommand.vol0InvT0,
			            perDraw._drawCommand.vol0InvT1,
			            perDraw._drawCommand.vol0InvT2,
			            perDraw._drawCommand.vol0InvT3);
		}
		return mat4(perDraw._drawCommand.vol1InvT0,
		            perDraw._drawCommand.vol1InvT1,
		            perDraw._drawCommand.vol1InvT2,
		            perDraw._drawCommand.vol1InvT3);
	}

	mat4 GetVolumeWorldTransform(int vol)
	{
		if (vol == 0)
		{
			return mat4(perDraw._drawCommand.vol0WldT0,
			            perDraw._drawCommand.vol0WldT1,
			            perDraw._drawCommand.vol0WldT2,
			            perDraw._drawCommand.vol0WldT3);
		}
		return mat4(perDraw._drawCommand.vol1WldT0,
		            perDraw._drawCommand.vol1WldT1,
		            perDraw._drawCommand.vol1WldT2,
		            perDraw._drawCommand.vol1WldT3);
	}

	// Compute per-pixel blend factor for a local volume.
	// Returns 1.0 inside, fades to 0.0 at the edge, 0.0 outside.
	//
	// The fade distance is clamped per axis to that axis' own half extent: a volume can never
	// fade over more than its size, so every volume keeps a full weight core (blend == 1) no
	// matter how small, thin or how large its Fade is. Without the clamp a volume whose fade
	// reaches past its center stays permanently mixed with the sky.
	float ComputeVolumeBlendFactor(int vol, vec3 worldPos)
	{
		float intensity = GetVolumeIntensity(vol);
		if (intensity <= 0.0)
		{
			return 0.0;
		}

		vec3 localPos = (GetVolumeInverseTransform(vol) * vec4(worldPos, 1.0)).xyz;

		vec3 vMin = GetVolumeMin(vol);
		vec3 vMax = GetVolumeMax(vol);

		vec3 halfExtent  = max((vMax - vMin) * 0.5, vec3(0.000001));
		vec3 fadePerAxis = min(vec3(GetVolumeFadeDistance(vol)), halfExtent);

		vec3 distToMin = localPos - vMin;
		vec3 distToMax = vMax - localPos;
		vec3 minDist = min(distToMin, distToMax);
		vec3 ratio   = minDist / fadePerAxis;

		return clamp(min(ratio.x, min(ratio.y, ratio.z)), 0.0, 1.0);
	}

	// Defines
	//////////////////////////////////////////

	#define MAX_CASCADE_COUNT 4

	#define DIRECTIONAL_LIGHT_CACHE_ITEM_COUNT 12
	#define MAX_DIRECTIONAL_LIGHT_PER_OBJECT 8
	#define POINT_LIGHT_CACHE_ITEM_COUNT 32
	#define MAX_POINT_LIGHT_PER_OBJECT 24
	#define SPOT_LIGHT_CACHE_ITEM_COUNT 32
	#define MAX_SPOT_LIGHT_PER_OBJECT 24

	// Graphic Constants Data
	//////////////////////////////////////////

	struct GraphicConstatsDataLayout
	{
		float shadowDistance;
		float shadowAtlasSize;
		int iblMaxReflectionLod;
		int cascadeCount;
		vec4 cascadeDistances;
	};

	TK_UBO_BINDING(1) uniform GraphicConstatsData
	{
		GraphicConstatsDataLayout graphicConstants;
	};

	// Directional Light Data
	//////////////////////////////////////////

	#define COMMON_LIGHT_DATA												\
	vec3 color;																\
	float intensity;														\
	vec3 position;															\
	int castShadow;															\
	float shadowBias;														\
	float bleedingReduction;												\
	float padx;																\
	float pady;																\
	vec2 shadowAtlasCoord;													\
	float shadowAtlasResRatio;												\
	int shadowAtlasLayer;

	struct DirectionalLightData
	{
		COMMON_LIGHT_DATA

		// Directional light specific fields
		vec3 direction;
		float pad0;
	};

	TK_UBO_BINDING(3) uniform DirectionalLightBuffer
	{
		DirectionalLightData directionalLightArray[DIRECTIONAL_LIGHT_CACHE_ITEM_COUNT];
	};

	struct DirectionalLightPVMData
	{
		mat4 projectionViewMatrices[MAX_CASCADE_COUNT];
	};

	TK_UBO_BINDING(6) uniform DirectionalLightPVMBuffer
	{
		DirectionalLightPVMData directionalLightPVMArray[DIRECTIONAL_LIGHT_CACHE_ITEM_COUNT];
	};

	struct PointLightData
	{
		COMMON_LIGHT_DATA

		// Point light specific fields
		float radius;
	};

	TK_UBO_BINDING(4) uniform PointLightCache
	{
		PointLightData pointLightArray[POINT_LIGHT_CACHE_ITEM_COUNT];
	};

	struct SpotLightData
	{
		COMMON_LIGHT_DATA

		// Spot light specific fields
		vec3 direction;
		float radius;

		float outerAngle;
		float innerAngle;

		mat4 projectionViewMatrix;
	};

	TK_UBO_BINDING(5) uniform SpotLightCache
	{
		SpotLightData spotLightArray[SPOT_LIGHT_CACHE_ITEM_COUNT];
	};

	#endif // DRAW_DATA

	
	

// ==== shadow.shader ====

	
#ifndef SHADOW_SHADER
#define SHADOW_SHADER

#define TWO_PI 6.283185

// ---------------------------------------------------------------------------
// Interleaved Gradient Noise (Jorge Jimenez, 2014)
// Deterministic screen-space noise to break shadow banding.
// ---------------------------------------------------------------------------

float InterleavedGradientNoise(vec2 screenPos)
{
	vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
	return fract(magic.z * fract(dot(screenPos, magic.xy)));
}

vec2 ShadowDitherJitter(float texelSize)
{
	float noise = InterleavedGradientNoise(gl_FragCoord.xy);
	float angle = noise * TWO_PI;
	return vec2(cos(angle), sin(angle)) * texelSize * noise;
}

// ---------------------------------------------------------------------------
// Shadow Atlas Lookup
// ---------------------------------------------------------------------------

void ShadowAtlasLut(in float size, in vec2 startCoord, in int queriedMap, out int layer, out vec2 targetCoord)
{
	float mapsPerRow = floor(graphicConstants.shadowAtlasSize / size);
	float mapsPerLayer = mapsPerRow * mapsPerRow;

	float mapIndex = float( queriedMap );

	layer = int( floor( mapIndex / mapsPerLayer ) );
	float indexInLayer = mapIndex - float(layer) * mapsPerLayer;

	float row = floor(indexInLayer / mapsPerRow);
	float col = indexInLayer - row * mapsPerRow;

	targetCoord = startCoord + vec2(col, row) * size;
}

// ---------------------------------------------------------------------------
// Filament-style EVSM evaluation
// ---------------------------------------------------------------------------

float EvaluateEVSM
(
	vec2 positiveMoments,
	float positiveWarpedDepth,
	float shadowBias,
	float lightBleedReduction
)
{
	// Filament-style EVSM evaluation (2-component, positive warp only)
	float pw = positiveWarpedDepth;
	float posMinVariance = shadowBias * (pw * pw);
	float posContrib = ChebyshevUpperBound(positiveMoments, pw, posMinVariance, lightBleedReduction);

	return posContrib;
}

// ---------------------------------------------------------------------------
// Bilinear PCF tap helpers
// Each texture() with linear filtering already interpolates 2x2 texels.
// By placing samples at sub-texel offsets we cover larger effective kernels.
//
//  4 samples -> ~3x3 kernel  (4 bilinear taps at +-0.5 texel)
//  9 samples -> ~5x5 kernel  (9 bilinear taps, center + 4 edges + 4 corners)
// 16 samples -> ~7x7 kernel  (16 bilinear taps in a 4x4 grid)
//
// All loops are manually unrolled for GPU performance.
// ---------------------------------------------------------------------------

#if ShadowPCF >= 4
vec2 ShadowPCFFilter(sampler2DArray atlas, vec3 uvLayer, vec2 coordStart, vec2 coordEnd, float texelSize)
{
	vec2 result = vec2(0.0);

#define SHADOW_TAP(ox, oy) texture(atlas, vec3(clamp(uvLayer.xy + vec2(ox, oy) * texelSize, coordStart, coordEnd), uvLayer.z)).xy

#if ShadowPCF == 4
	// 4 bilinear taps at +-0.5 texel offsets -> effective ~3x3 kernel
	result += SHADOW_TAP(-0.5, -0.5);
	result += SHADOW_TAP( 0.5, -0.5);
	result += SHADOW_TAP(-0.5,  0.5);
	result += SHADOW_TAP( 0.5,  0.5);
	result *= 0.25;

#elif ShadowPCF == 9
	// 9 bilinear taps -> effective ~5x5 Gaussian approximation
	// 4 corners at +-1.2 (weighted center of 2x2 blocks)
	// 4 edges at +-1.2 on one axis, 0 on the other
	// 1 center tap
	// Weights: corners 25/256 each, edges 30/256 each, center 36/256
	result += SHADOW_TAP(-1.2, -1.2) * 25.0;
	result += SHADOW_TAP( 1.2, -1.2) * 25.0;
	result += SHADOW_TAP(-1.2,  1.2) * 25.0;
	result += SHADOW_TAP( 1.2,  1.2) * 25.0;
	result += SHADOW_TAP( 0.0, -1.2) * 30.0;
	result += SHADOW_TAP( 0.0,  1.2) * 30.0;
	result += SHADOW_TAP(-1.2,  0.0) * 30.0;
	result += SHADOW_TAP( 1.2,  0.0) * 30.0;
	result += SHADOW_TAP( 0.0,  0.0) * 36.0;
	result *= (1.0 / 256.0);

#elif ShadowPCF == 16
	// 16 bilinear taps in a 4x4 grid -> effective ~7x7 kernel
	// Sub-texel offsets at +-0.5 and +-1.5 texels
	result += SHADOW_TAP(-1.5, -1.5);
	result += SHADOW_TAP(-0.5, -1.5);
	result += SHADOW_TAP( 0.5, -1.5);
	result += SHADOW_TAP( 1.5, -1.5);
	result += SHADOW_TAP(-1.5, -0.5);
	result += SHADOW_TAP(-0.5, -0.5);
	result += SHADOW_TAP( 0.5, -0.5);
	result += SHADOW_TAP( 1.5, -0.5);
	result += SHADOW_TAP(-1.5,  0.5);
	result += SHADOW_TAP(-0.5,  0.5);
	result += SHADOW_TAP( 0.5,  0.5);
	result += SHADOW_TAP( 1.5,  0.5);
	result += SHADOW_TAP(-1.5,  1.5);
	result += SHADOW_TAP(-0.5,  1.5);
	result += SHADOW_TAP( 0.5,  1.5);
	result += SHADOW_TAP( 1.5,  1.5);
	result *= (1.0 / 16.0);
#endif

#undef SHADOW_TAP

	return result;
}
#endif

// ---------------------------------------------------------------------------
// 2D Shadow Sampling (Directional + Spot)
// ---------------------------------------------------------------------------

float SampleShadow2D
(
	sampler2DArray shadowAtlas,
	vec3 uvLayer,
	vec2 coordStart,
	vec2 coordEnd,
	float texelSize,
	float currDepth,
	float LBR,
	float shadowBias
)
{
	float halfPixel = (1.0 / graphicConstants.shadowAtlasSize) * 0.5;
	vec2 clampMin = coordStart + halfPixel;
	vec2 clampMax = coordEnd - halfPixel;

	vec2 jitter = ShadowDitherJitter(texelSize);
	vec3 jitteredUV = vec3(clamp(uvLayer.xy + jitter, clampMin, clampMax), uvLayer.z);

#if ShadowPCF >= 4
	vec2 moments = ShadowPCFFilter(shadowAtlas, jitteredUV, clampMin, clampMax, texelSize);
#else
	vec2 moments = texture(shadowAtlas, jitteredUV).xy;
#endif

	float warpedDepth = WarpDepth(currDepth);

	return EvaluateEVSM
	(
		moments.xy,
		warpedDepth,
		shadowBias,
		LBR
	);
}

// Paraboloid Shadow Sampling (Point lights)
//
// A point light renders one map per hemisphere instead of six cube faces: the scene is projected onto
// a paraboloid, radii divided by 1.15 so the hemisphere sits inside about 87 percent of the map and the
// stretched rim is never read. Two maps per light is a third of the atlas a cube needed.
//
// The render side puts the camera at the light looking along -Z and turns it half a turn about Y for the
// other half, so the covered hemisphere always has a negative z in its view space. This mirrors that:
// the half with a negative world z uses the direction as is, the other half negates x and z.
// ---------------------------------------------------------------------------

// Radii are divided by this before projecting, so the hemisphere occupies about 87 percent of the map
// and the rim, where the projection stretches without bound, stays outside the part that is sampled.
const float ParaboloidRimInv = 0.98039216;

float SampleShadowOmni
(
	sampler2DArray shadowAtlas,
	vec2 startCoord,
	float shadowAtlasResRatio,
	int shadowAtlasLayer,
	vec3 dir,
	float texelSize,
	float currDepth,
	float LBR,
	float shadowBias
)
{
	float halfPixel = (1.0 / graphicConstants.shadowAtlasSize) * 0.5;
	float shadowMapSize = shadowAtlasResRatio * graphicConstants.shadowAtlasSize;

	vec3 d = normalize(dir);

	// Which map holds this half, and what the direction looks like in that map's own frame.
	//
	// The engine camera looks along +Z when it has no rotation, and its view flips z, so slot 0 - drawn
	// with no rotation - holds the half where the world z of the direction is positive, with z flipped.
	// Slot 1 is drawn with the camera turned half a turn about Y, which flips x instead. Getting this
	// backwards leaves x and y looking right while the shadow follows the light along z, because the
	// wrong half only mirrors z.
	int side = d.z >= 0.0 ? 0 : 1;
	vec3 v = side == 0 ? vec3(d.x, d.y, -d.z) : vec3(-d.x, d.y, d.z);

	// Same projection as the shadow vertex shader, rim scale included.
	//
	// The rows are read back mirrored: the shadow pass writes the map through the light's camera view,
	// whose vertical axis is the reverse of the one this sampler builds its direction in (the same
	// reversal the cube path had to compensate for). Without it the shadow follows the light when the
	// light is moved up or down instead of sliding the other way along the wall.
	float denom = max(1.0 - v.z, 1e-4);
	vec2 proj = (v.xy / denom) * ParaboloidRimInv;
	vec2 uvInMap = proj * 0.5 + 0.5;
	uvInMap.y = 1.0 - uvInMap.y;

	// Face 0's slot index from its pixel coordinate, then the absolute atlas coordinate of
	// (baseIndex + side): the two hemispheres are consecutive slots.
	float mapsPerRow = floor(graphicConstants.shadowAtlasSize / shadowMapSize);
	int baseIndex = int(startCoord.y / shadowMapSize) * int(mapsPerRow) + int(startCoord.x / shadowMapSize);

	int layer = 0;
	vec2 coord = vec2(0.0);
	ShadowAtlasLut(shadowMapSize, vec2(0.0), baseIndex + side, layer, coord);
	coord /= graphicConstants.shadowAtlasSize;

	layer += shadowAtlasLayer;

	vec2 beginCoord = coord;
	vec2 endCoord = beginCoord + shadowAtlasResRatio;

	vec2 texCoord = beginCoord + (shadowAtlasResRatio * uvInMap);
	texCoord = clamp(texCoord, beginCoord + halfPixel, endCoord - halfPixel);

	vec2 jitter = ShadowDitherJitter(texelSize);
	vec2 jitteredXY = clamp(texCoord + jitter, beginCoord + halfPixel, endCoord - halfPixel);

	vec3 sampleCoord = vec3(jitteredXY, float(layer));

#if ShadowPCF >= 4
	vec2 moments = ShadowPCFFilter(shadowAtlas, sampleCoord, beginCoord + halfPixel, endCoord - halfPixel, texelSize);
#else
	vec2 moments = texture(shadowAtlas, sampleCoord).xy;
#endif

	float warpedDepth = WarpDepth(currDepth);

	return EvaluateEVSM
	(
		moments.xy,
		warpedDepth,
		shadowBias,
		LBR
	);
}

#endif
	
	

// ==== lighting.shader ====

	
#ifndef LIGHTING_SHADER
#define LIGHTING_SHADER

TK_SAMPLER_BINDING(8)  uniform sampler2DArray s_shadowAtlas; // Shadow atlas

/// Deferred rendering uniforms
TK_SAMPLER_BINDING(13) uniform sampler2D s_lightData; // Light data

const float shadowFadeOutDistanceNorm = 0.9;

// ---------------------------------------------------------------------------
// Linear-style attenuation
// ---------------------------------------------------------------------------

// Smooth falloff: (1 - d2/r2)2.
float DistanceAttenuation(float distanceSq, float falloff)
{
	// falloff = 1.0 / (radius * radius)
	float factor = clamp(distanceSq * falloff, 0.0, 1.0);
	float f = 1.0 - factor;
	return f * f;
}

// Filament-style spot light angular attenuation
// cosInner and cosOuter are pre-computed cosine values
float SpotAngleAttenuation(float cosAngle, float cosInner, float cosOuter)
{
	float scale = 1.0 / max(cosInner - cosOuter, 1e-4);
	float offset = -cosOuter * scale;
	float attenuation = clamp(cosAngle * scale + offset, 0.0, 1.0);
	return attenuation * attenuation;
}

// Fades shadow towards 1.0 (no shadow) as distance approaches maxDist.
// Fade starts at 90% of maxDist. Returns a value to blend with shadow.
float ShadowDistanceFade(float distance, float maxDist)
{
	float fadeStart = maxDist * shadowFadeOutDistanceNorm;
	float fade = (distance - fadeStart) / (maxDist - fadeStart);
	fade = clamp(fade, 0.0, 1.0);
	return fade * fade;
}

float CalculateDirectionalShadow
(
	vec3 pos,
	vec3 viewCamPos,
	mat4 lightProjView,
	vec2 shadowAtlasCoord,
	float shadowAtlasResRatio,
	int shadowAtlasLayer,
	float lightBleedReduction,
	float shadowBias
)
{
	vec4 fragPosForLight = lightProjView * vec4(pos, 1.0);
	vec3 projCoord = fragPosForLight.xyz * 0.5 + 0.5;
#ifdef VULKAN
	// Vulkan textures: UV.y=0 is top (opposite of OpenGL's bottom-origin convention).
	// Negate to match where the shadow atlas was rendered with the negative-viewport trick.
	projCoord.y = 1.0 - projCoord.y;
	// GLM_FORCE_DEPTH_ZERO_TO_ONE: ortho clip Z is already [0,1]; no * 0.5 + 0.5 needed.
	projCoord.z = fragPosForLight.z;
#endif

	vec2 startCoord = shadowAtlasCoord;
	vec2 endCoord = shadowAtlasCoord + shadowAtlasResRatio;

	vec2 uvInAtlas = startCoord + shadowAtlasResRatio * projCoord.xy;
	vec3 sampleCoord = vec3(uvInAtlas, shadowAtlasLayer);

	float texelSize = 1.0 / graphicConstants.shadowAtlasSize;

	float shadow = SampleShadow2D
	(
		s_shadowAtlas,
		sampleCoord,
		startCoord,
		endCoord,
		texelSize,
		projCoord.z,
		lightBleedReduction,
		shadowBias
	);

	float camDist = length(pos - viewCamPos);
	float fade = ShadowDistanceFade(camDist, graphicConstants.shadowDistance);
	return clamp(shadow + fade, 0.0, 1.0);
}

float CalculateSpotShadow
(
	vec3 pos,
	vec3 lightPos,
	mat4 lightProjView,
	float shadowCameraFar,
	vec2 shadowAtlasCoord,
	float shadowAtlasResRatio,
	int shadowAtlasLayer,
	float lightBleedReduction,
	float shadowBias,
	float lightDistance
)
{
	vec4 fragPosForLight = lightProjView * vec4(pos, 1.0);
	vec3 projCoord = fragPosForLight.xyz / fragPosForLight.w;
	projCoord = projCoord * 0.5 + 0.5;
#ifdef VULKAN
	// Same UV.y origin correction as directional shadow.
	projCoord.y = 1.0 - projCoord.y;
#endif

	float currFragDepth = lightDistance / shadowCameraFar;

	vec2 startCoord = shadowAtlasCoord;
	vec3 coord = vec3(startCoord + shadowAtlasResRatio * projCoord.xy, shadowAtlasLayer);

	float texelSize = 1.0 / graphicConstants.shadowAtlasSize;

	return SampleShadow2D
	(
		s_shadowAtlas,
		coord,
		startCoord,
		startCoord + shadowAtlasResRatio,
		texelSize,
		currFragDepth,
		lightBleedReduction,
		shadowBias
	);
}

float CalculatePointShadow
(
	vec3 pos,
	vec3 lightPos,
	float shadowCameraFar,
	vec2 shadowAtlasCoord,
	float shadowAtlasResRatio,
	int shadowAtlasLayer,
	float lightBleedReduction,
	float shadowBias,
	float precomputedDist
)
{
	vec3 lightToFrag = pos - lightPos;
	float currFragDepth = precomputedDist / shadowCameraFar;

	float texelSize = 1.0 / graphicConstants.shadowAtlasSize;

	return SampleShadowOmni
	(
		s_shadowAtlas,
		shadowAtlasCoord,
		shadowAtlasResRatio,
		shadowAtlasLayer,
		lightToFrag,
		texelSize,
		currFragDepth,
		lightBleedReduction,
		shadowBias
	);
}

// ---------------------------------------------------------------------------
// Filament-style surface shading for a single light
// Combines BRDF evaluation with light color, intensity, attenuation, NoL and shadow
// ---------------------------------------------------------------------------

vec3 SurfaceShading
(
	vec3 normal,
	vec3 fragToEye,
	vec3 albedo,
	float metallic,
	float roughness,
	vec3 lightDir,
	vec3 lightColor,
	float lightIntensity,
	float attenuation,
	float shadow,
	vec3 energyCompensation
)
{
	vec3 F0 = BaseReflectivityPBR(vec3(0.04), albedo, metallic);
	vec3 diffuseColor = albedo * (1.0 - metallic);

	vec3 halfway = normalize(lightDir + fragToEye);

	float NoV = clamp(dot(normal, fragToEye), 1e-4, 1.0);
	float NoL = clamp(dot(normal, lightDir), 0.0, 1.0);
	float NoH = clamp(dot(normal, halfway), 0.0, 1.0);
	float LoH = clamp(dot(lightDir, halfway), 0.0, 1.0);

	// Specular BRDF
	vec3 h = halfway;
	float D = distribution(roughness, NoH, h);
	float V = visibility(roughness, NoV, NoL);
	vec3 F = fresnel(F0, LoH);
	vec3 Fr = (D * V) * F;

	// Diffuse BRDF
	vec3 Fd = diffuseColor * diffuse(roughness, NoV, NoL, LoH);

	// Thin surfaces produce flicker when the view angle is grazing. Fade out the contribution to hide the flickering.
	float edgeFade = smoothstep(0.0, 1.0, NoV); 

	// Combine: Fd + Fr * energyCompensation (Filament style)
	vec3 color = Fd + Fr * energyCompensation * edgeFade;

	// Apply light contribution: color * lightColor * (intensity * attenuation * NoL * shadow)
	return color * lightColor * (lightIntensity * attenuation * NoL * shadow);
}

vec3 PBRLighting
(
	vec3 fragPos,
	float viewPosDepth,
	vec3 normal,
	vec3 fragToEye,
	vec3 viewCamPos,
	vec3 albedo,
	float metallic,
	float roughness,
	vec3 energyCompensation
)
{
	vec3 irradiance = vec3(0.0);

	// ----- Directional Lights -----
	for (int i = 0; i < GetActiveDirectionalLightCount(); i++)
	{
		DirectionalLightData light = directionalLightArray[i];

		float resRatio = light.shadowAtlasResRatio;

		vec3 lightDir = normalize(-light.direction);

		float shadow = 1.0;
		vec3 cascadeMultiplier = vec3(1.0);

		if (light.castShadow == 1)
		{
			int numCascade = graphicConstants.cascadeCount;
			int cascadeOfThisPixel = numCascade - 1;

			float depth = abs(viewPosDepth);
			for (int ci = 0; ci < numCascade; ci++)
			{
				if (depth < graphicConstants.cascadeDistances[ci])
				{
					cascadeOfThisPixel = ci;
					break;
				}
			}

#if highlightCascades
			if (cascadeOfThisPixel == 0)
			{
				cascadeMultiplier = vec3(4.0, 1.0, 1.0);
			}
			else if (cascadeOfThisPixel == 1)
			{
				cascadeMultiplier = vec3(1.0, 4.0, 1.0);
			}
			else if (cascadeOfThisPixel == 2)
			{
				cascadeMultiplier = vec3(1.0, 1.0, 4.0);
			}
			else if (cascadeOfThisPixel == 3)
			{
				cascadeMultiplier = vec3(4.0, 4.0, 1.0);
			}
#endif

			float shadowMapSize = resRatio * graphicConstants.shadowAtlasSize;
			int layer = 0;
			vec2 coord = vec2(0.0);
			ShadowAtlasLut(shadowMapSize, light.shadowAtlasCoord, cascadeOfThisPixel, layer, coord);

			layer += light.shadowAtlasLayer;

			shadow = CalculateDirectionalShadow
			(
				fragPos,
				viewCamPos,
				directionalLightPVMArray[i].projectionViewMatrices[cascadeOfThisPixel],
				coord / graphicConstants.shadowAtlasSize,
				resRatio,
				layer,
				light.bleedingReduction,
				light.shadowBias
			);

#if CascadeBlend
			// Fade into the next cascade across the far end of this one, so the split does not show
			// as a line across the ground. The band is a fraction of this cascade's own depth span
			// and not a world distance, so it scales with the cascade.
			//
			// The weight is a plain smoothstep and deliberately not dithered: a dithered weight
			// would put a different blend on neighbouring pixels every frame, which is the temporal
			// shimmer the stable cascade fit exists to remove.
			if (cascadeOfThisPixel != numCascade - 1)
			{
				const float CascadeBlendBand = 0.15;
				float splitNear              = cascadeOfThisPixel == 0
				                                   ? 0.0
				                                   : graphicConstants.cascadeDistances[cascadeOfThisPixel - 1];
				float splitFar               = graphicConstants.cascadeDistances[cascadeOfThisPixel];
				float span                   = max(splitFar - splitNear, 1e-4);
				float fadeFactor             = (splitFar - depth) / span;

				if (fadeFactor < CascadeBlendBand)
				{
					int nextLayer    = 0;
					vec2 nextCoord   = vec2(0.0);
					ShadowAtlasLut(shadowMapSize,
					               light.shadowAtlasCoord,
					               cascadeOfThisPixel + 1,
					               nextLayer,
					               nextCoord);

					nextLayer += light.shadowAtlasLayer;

					float nextShadow = CalculateDirectionalShadow
					(
						fragPos,
						viewCamPos,
						directionalLightPVMArray[i].projectionViewMatrices[cascadeOfThisPixel + 1],
						nextCoord / graphicConstants.shadowAtlasSize,
						resRatio,
						nextLayer,
						light.bleedingReduction,
						light.shadowBias
					);

					shadow = mix(nextShadow, shadow, smoothstep(0.0, CascadeBlendBand, fadeFactor));
				}
			}
#endif
		}

		// Directional lights have no distance attenuation
		vec3 Lo = SurfaceShading(normal, fragToEye, albedo, metallic, roughness,
			lightDir, light.color, light.intensity, 1.0, shadow, energyCompensation);

		irradiance += Lo * cascadeMultiplier;
	}

	// ----- Point Lights -----
	for (int i = 0; i < GetActivePointLightCount(); i++)
	{
		int ii = perDraw._activePointLightIndices[i >> 2][i & 3];
		PointLightData light = pointLightArray[ii];

		vec3 fragToLight = light.position - fragPos;
		float lightDistanceSq = dot(fragToLight, fragToLight);
		float lightDistance = sqrt(lightDistanceSq);

		if (lightDistance >= light.radius)
		{
			continue;
		}

		float resRatio = light.shadowAtlasResRatio;
		float falloff = 1.0 / (light.radius * light.radius);
		float attenuation = DistanceAttenuation(lightDistanceSq, falloff);

		vec3 lightDir = fragToLight / lightDistance;

		float shadow = 1.0;
		if (light.castShadow == 1)
		{
			shadow = CalculatePointShadow
			(
				fragPos,
				light.position,
				light.radius,
				light.shadowAtlasCoord,
				resRatio,
				light.shadowAtlasLayer,
				light.bleedingReduction,
				light.shadowBias,
				lightDistance
			);

			float camDist = length(fragPos - viewCamPos);
			float fade = ShadowDistanceFade(camDist, graphicConstants.shadowDistance);
			shadow = clamp(shadow + fade, 0.0, 1.0);
		}

		vec3 Lo = SurfaceShading(normal, fragToEye, albedo, metallic, roughness,
			lightDir, light.color, light.intensity, attenuation, shadow, energyCompensation);

		irradiance += Lo;
	}

	// ----- Spot Lights -----
	for (int i = 0; i < GetActiveSpotLightCount(); i++)
	{
		int ii = perDraw._activeSpotLightIndices[i >> 2][i & 3];
		SpotLightData light = spotLightArray[ii];

		vec3 fragToLight = light.position - fragPos;
		float lightDistanceSq = dot(fragToLight, fragToLight);
		float lightDistance = sqrt(lightDistanceSq);

		if (lightDistance >= light.radius)
		{
			continue;
		}

		float resRatio = light.shadowAtlasResRatio;
		float falloff = 1.0 / (light.radius * light.radius);
		float attenuation = DistanceAttenuation(lightDistanceSq, falloff);

		// Filament-style spot cone attenuation
		vec3 lightDirNorm = fragToLight / lightDistance;
		float cosAngle = dot(-lightDirNorm, light.direction);
		// light.innerAngle and light.outerAngle are pre-computed cosine values from CPU
		attenuation *= SpotAngleAttenuation(cosAngle, light.innerAngle, light.outerAngle);

		float shadow = 1.0;
		if (light.castShadow == 1)
		{
			shadow = CalculateSpotShadow
			(
				fragPos,
				light.position,
				light.projectionViewMatrix,
				light.radius,
				light.shadowAtlasCoord / graphicConstants.shadowAtlasSize,
				resRatio,
				light.shadowAtlasLayer,
				light.bleedingReduction,
				light.shadowBias,
				lightDistance
			);

			float camDist = length(fragPos - viewCamPos);
			float fade = ShadowDistanceFade(camDist, graphicConstants.shadowDistance);
			shadow = clamp(shadow + fade, 0.0, 1.0);
		}

		vec3 Lo = SurfaceShading(normal, fragToEye, albedo, metallic, roughness,
			lightDirNorm, light.color, light.intensity, attenuation, shadow, energyCompensation);

		irradiance += Lo;
	}

	return irradiance;
}

#endif
	
	

// ==== ibl.shader ====

	

#ifndef IBL_SHADER
#define IBL_SHADER

// Local volume 0
TK_SAMPLER_BINDING(7)  uniform samplerCube s_irradianceMap; 	// Diffuse Map
TK_SAMPLER_BINDING(15) uniform samplerCube s_iblSpecular; 	// Pre-Filtered Specular Map
TK_SAMPLER_BINDING(10) uniform sampler2D s_brdfLut;		// IBL BRDF Lut

// Local volume 1
TK_SAMPLER_BINDING(11) uniform samplerCube s_secondaryIrradiance;	// Diffuse Map
TK_SAMPLER_BINDING(12) uniform samplerCube s_secondarySpecular;	// Pre-Filtered Specular Map

// Sky (global fallback)
TK_SAMPLER_BINDING(16) uniform samplerCube s_skyIrradiance;	// Sky Diffuse Map
TK_SAMPLER_BINDING(17) uniform samplerCube s_skySpecular;	// Sky Pre-Filtered Specular Map

// Sky rotation backed by perDraw._iblRotation (PerDrawData UBO, slot 6). A local volume
// carries its own orientation in the volume transform, so it never touches this matrix.
// `perDraw._iblSecondaryRotation` stays reserved for per-volume rotations and identity-only;
// nothing reads it today.

// ---------------------------------------------------------------------------
// Filament-style IBL helpers
// ---------------------------------------------------------------------------

vec3 GetParallaxCorrectedReflection(vec3 R, vec3 worldPos, mat4 inverseVolTransform, vec3 volMin, vec3 volMax)
{
	vec3 localPos = (inverseVolTransform * vec4(worldPos, 1.0)).xyz;
	vec3 localDir = (inverseVolTransform * vec4(R, 0.0)).xyz;

	vec3 invLocalDir = 1.0 / (localDir + 0.000001);
	vec3 t0 = (volMin - localPos) * invLocalDir;
	vec3 t1 = (volMax - localPos) * invLocalDir;
	vec3 tMaxPlane = max(t0, t1);
	float dist = min(min(tMaxPlane.x, tMaxPlane.y), tMaxPlane.z);

	vec3 intersectLocal = localPos + localDir * dist;

	// Return local-space direction. Cubemap is rendered aligned to the entity's local axes.
	return normalize(intersectLocal);
}

// Level of the prefiltered chain a roughness reads from. Linear in roughness, because the chain is
// baked the same way (each level holds `mip / (mipMaps - 1)`, see Renderer::GenerateSpecularEnvMap):
// read and write have to use one rule or the reflection shows a roughness the material does not have.
// The perceptual `r * (2 - r)` curve this used to apply spends up to twice the mips on the low end
// (`r * (2 - r)` is `r` plus `r * (1 - r)`, positive for every roughness in between), which is exactly
// where a wet floor lives, so it stretched those reflections well before their roughness asked for it.
float RoughnessToLod(float roughness, float maxLod)
{
	return maxLod * roughness;
}

vec3 GetSpecularDominantDirection(vec3 n, vec3 r, float roughness)
{
	return mix(r, n, roughness * roughness);
}

vec3 SpecularDFG(vec2 dfg, vec3 f0)
{
	return f0 * dfg.x + dfg.y;
}

// ---------------------------------------------------------------------------
// Sky IBL evaluation (returns raw sky color, no weighting)
// ---------------------------------------------------------------------------

vec3 EvalSky(vec3 normal, vec3 fragToEye, vec3 albedo, float metallic, float perceptualRoughness, vec3 E, vec3 energyComp)
{
	vec3 color = vec3(0.0);

	float skyIntensity = GetSkyIntensity();
	if (skyIntensity <= 0.0)
	{
		return color;
	}

	// Diffuse
	vec3 diffuseColor = albedo * (1.0 - metallic);
	vec3 iblDiffuseVec = (perDraw._iblRotation * vec4(normal, 0.0)).xyz;
	vec3 irradiance = texture(s_skyIrradiance, iblDiffuseVec).rgb;
	color += diffuseColor * irradiance * (1.0 - E);

	// Specular
	vec3 R = reflect(-fragToEye, normal);
	R = GetSpecularDominantDirection(normal, R, perceptualRoughness);
	vec3 iblSpecVec = (perDraw._iblRotation * vec4(R, 0.0)).xyz;
	float lod = RoughnessToLod(perceptualRoughness, float(graphicConstants.iblMaxReflectionLod));
	vec3 preFilteredColor = textureLod(s_skySpecular, iblSpecVec, lod).rgb;
	color += E * preFilteredColor * energyComp;

	return color * skyIntensity;
}

// ---------------------------------------------------------------------------
// Per-volume IBL evaluation (local volumes only)
// ---------------------------------------------------------------------------

vec3 EvalVolumeDiffuse(int vol, vec3 normal, vec3 albedo, float metallic, vec3 E)
{
	vec3 diffuseColor = albedo * (1.0 - metallic);
	// Cubemap is rendered aligned to entity local axes, rotate world normal to local space.
	vec3 iblSamplerVec = (GetVolumeInverseTransform(vol) * vec4(normal, 0.0)).xyz;
	vec3 irradiance;
	if (vol == 0)
		irradiance = texture(s_irradianceMap, iblSamplerVec).rgb;
	else
		irradiance = texture(s_secondaryIrradiance, iblSamplerVec).rgb;
	return diffuseColor * irradiance * (1.0 - E);
}

vec3 EvalVolumeSpecular(int vol, vec3 normal, vec3 fragToEye, float perceptualRoughness, vec3 E, vec3 energyComp, vec3 worldPos)
{
	vec3 R = reflect(-fragToEye, normal);
	R = GetSpecularDominantDirection(normal, R, perceptualRoughness);

	vec3 iblSamplerVec;
	if (IsVolumePccEnabled(vol))
	{
		// PCC returns a local-space direction.
		iblSamplerVec = GetParallaxCorrectedReflection(R, worldPos,
			GetVolumeInverseTransform(vol), GetVolumeMin(vol), GetVolumeMax(vol));
	}
	else
	{
		// Cubemap is rendered aligned to entity local axes, rotate world R to local space.
		iblSamplerVec = (GetVolumeInverseTransform(vol) * vec4(R, 0.0)).xyz;
	}

	float lod = RoughnessToLod(perceptualRoughness, float(graphicConstants.iblMaxReflectionLod));
	vec3 preFilteredColor;
	if (vol == 0)
		preFilteredColor = textureLod(s_iblSpecular, iblSamplerVec, lod).rgb;
	else
		preFilteredColor = textureLod(s_secondarySpecular, iblSamplerVec, lod).rgb;

	vec3 specular = E * preFilteredColor;
	specular *= energyComp;
	return specular;
}

// ---------------------------------------------------------------------------
// Premultiplied-alpha volume accumulation with the sky as a weighted participant.
//
// Each volume contributes weight * vec4(volumeColor, 1), where `blend` acts as
// both its weight in the volume-to-volume average and the amount of sky it lets
// through. The sky then takes only the weight the strongest volume did not cover
// (skyWeight = 1 - max(blend)). Consequences:
// - A pixel fully covered by any volume (blend == 1) gets no sky at all, so the
//   old partial sky leak cannot happen.
// - A pixel inside a fade band (0 < blend < 1) blends smoothly towards the sky
//   instead of stepping to it, because the volume weight cancels out of the
//   average only when nothing is left uncovered.
// - Exterior and interior volumes therefore weigh the same in the average; the
//   slot order of the two volumes does not change the result.
//
// `Interior` volumes never reveal sky: their presence at a pixel pins skyWeight
// to 0, so an interior probe stays fully opaque up to its own box face and only
// blends against the other volume. Such a box is expected to sit inside geometry.
// ---------------------------------------------------------------------------

struct VolumeAccum
{
	vec3 color;     // sum(volumeColor * blend)
	float weight;   // sum(blend)
	float maxBlend; // max(blend)
	bool interior;  // true when any contributing volume is flagged interior
};

void AccumulateVolume(int vol, vec3 normal, vec3 fragToEye, vec3 albedo, float metallic,
	float perceptualRoughness, vec3 E, vec3 energyComp, vec3 worldPos,
	inout VolumeAccum accum)
{
	float blend = ComputeVolumeBlendFactor(vol, worldPos);
	if (blend <= 0.0)
	{
		return;
	}

	vec3 Fd = EvalVolumeDiffuse(vol, normal, albedo, metallic, E);
	vec3 Fr = EvalVolumeSpecular(vol, normal, fragToEye, perceptualRoughness, E, energyComp, worldPos);
	vec3 volumeColor = (Fd + Fr) * GetVolumeIntensity(vol);

	accum.color += volumeColor * blend;
	accum.weight += blend;
	accum.maxBlend = max(accum.maxBlend, blend);
	accum.interior = accum.interior || IsVolumeInterior(vol);
}

// ---------------------------------------------------------------------------
// Combined IBL: weighted average of the local volumes plus the sky that fills
// whatever weight no volume covered.
// ---------------------------------------------------------------------------

vec3 IBLPBR(vec3 normal, vec3 fragToEye, vec3 albedo, float metallic, float perceptualRoughness, vec2 dfg, vec3 energyComp, vec3 worldPos)
{
	if (!IsIBLInUse())
	{
		return vec3(0.0);
	}

	vec3 f0 = BaseReflectivityPBR(vec3(0.04), albedo, metallic);
	vec3 E = SpecularDFG(dfg, f0);

	// Evaluate sky once: it is both the outside fallback and the boundary blend target.
	vec3 skyColor = EvalSky(normal, fragToEye, albedo, metallic, perceptualRoughness, E, energyComp);

	VolumeAccum accum;
	accum.color    = vec3(0.0);
	accum.weight   = 0.0;
	accum.maxBlend = 0.0;
	accum.interior = false;

	AccumulateVolume(0, normal, fragToEye, albedo, metallic, perceptualRoughness, E, energyComp, worldPos, accum);
	AccumulateVolume(1, normal, fragToEye, albedo, metallic, perceptualRoughness, E, energyComp, worldPos, accum);

	// Outside every volume: pure sky.
	if (accum.weight <= 0.0)
	{
		return skyColor;
	}

	// Sky fills only the weight that no volume covered. Interior volumes keep the
	// sky out of the pixel entirely, exterior ones fade into it over their fade
	// distance.
	float skyWeight = accum.interior ? 0.0 : (1.0 - accum.maxBlend);

	return (accum.color + skyColor * skyWeight) / (accum.weight + skyWeight);
}

#endif

	
	

// ==== AO.shader ====

	
#ifndef AO_SHADER
#define AO_SHADER

TK_SAMPLER_BINDING(5) uniform sampler2D s_ambientOcclusion; // ambient occlusion.

float AmbientOcclusion()
{
	if (IsAmbientOcculusionInUse())
	{
		vec2 coords = gl_FragCoord.xy / perDraw._viewportSizeAndPad.xy;
		return texture(s_ambientOcclusion, coords).r;
	}

	return 1.0;
}

#endif
	
	

// ==== cameraDataInc.shader ====

	
#ifndef CAMERA_DATA
#define CAMERA_DATA

// Camera Data
//////////////////////////////////////////

struct Camera
{
	vec3 position;
	float farPlane;

	vec3 direction;
	float pad0;

	mat4 projection;
	mat4 view;
	mat4 projectionView;
	mat4 projectionViewNoTranslate;
};

TK_UBO_BINDING(0) uniform CameraData
{
	Camera camera;
};

#endif // CAMERA_DATA
	
	

// ==== materialCacheInc.shader ====

	
	#ifndef MATERIAL_CACHE
	#define MATERIAL_CACHE

	// Material accessors — backing storage is now `perDraw._materialData` (PerDrawData UBO,
	// slot 6). `MaterialDataLayout` in perDrawDataInc.shader matches MaterialCacheItem::Data
	// in Material.h byte-for-byte.
	//////////////////////////////////////////

	struct Material
	{
		vec3 color;
		float alpha;

		vec3 emissiveColor;
		float alphaMaskThreshold;

		float metallic;
		float roughness;
		int useAlphaMask;
		int diffuseTextureInUse;

		int emissiveTextureInUse;
		int normalMapInUse;
		int metallicRoughnessTextureInUse;
	};

	// Material Utility
	//////////////////////////////////////////

	Material GetMaterial()
	{
		Material material;

		vec4 colorAlpha         = perDraw._materialData.colorAlpha;
		vec4 emissiveThreshold  = perDraw._materialData.emissiveThreshold;
		vec4 metallicRoughness  = perDraw._materialData.metallicRoughness;
		vec4 textureFlags       = perDraw._materialData.textureFlags;

		material.color = colorAlpha.rgb;
		material.alpha = colorAlpha.a;

		material.emissiveColor = emissiveThreshold.rgb;
		material.alphaMaskThreshold = emissiveThreshold.a;

		material.metallic = metallicRoughness.x;
		material.roughness = metallicRoughness.y;
		material.useAlphaMask = int(metallicRoughness.z);
		material.diffuseTextureInUse = int(metallicRoughness.w);

		material.emissiveTextureInUse = int(textureFlags.x);
		material.normalMapInUse = int(textureFlags.y);
		material.metallicRoughnessTextureInUse = int(textureFlags.z);

		return material;
	}

	bool IsNormalMapInUse()
	{
		return perDraw._materialData.textureFlags.y > 0.5;
	}

	#endif // MATERIAL_CACHE

	
	

// ==== defaultFragment.shader ====

	
	
	precision highp float;
	precision highp sampler2D;
	precision highp sampler2DArray;
	precision highp samplerCube;
	precision highp int;

	TK_SAMPLER_BINDING(0) uniform sampler2D s_diffuseColor; // color
	TK_SAMPLER_BINDING(1) uniform sampler2D s_emissiveColor; // emissive
	TK_SAMPLER_BINDING(4) uniform sampler2D s_metallicRoughness; // metallic-roughness
	TK_SAMPLER_BINDING(9) uniform sampler2D s_normalMap; // normal

	#define SHADE_LIGHTING_ONLY 1
	#define SHADE_ALBEDO_ONLY 2
	#define SHADE_NORMAL_ONLY 3
	#define SHADE_METALLIC_ONLY 4
	#define SHADE_ROUGHNESS_ONLY 5

	TK_LOC(0) in vec3 v_worldPos;
	TK_LOC(1) in vec3 v_worldNormal;
	TK_LOC(2) in vec2 v_texture;
	TK_LOC(3) in float v_viewDepth;
	TK_LOC(4) in mat3 TBN;

	layout (location = 0) out vec4 fragColor;

	void main()
	{
		Material material = GetMaterial();

		vec4 color;
		if(material.diffuseTextureInUse > 0)
		{
			color = texture(s_diffuseColor, v_texture);
		}
		else
		{
			color = vec4(material.color, material.alpha);
		}

		vec3 emissive;
		if(material.emissiveTextureInUse > 0)
		{
			emissive = texture(s_emissiveColor, v_texture).xyz;
		}
		else
		{
			emissive = material.emissiveColor;
		}

		float metallic, roughness;
		if (material.metallicRoughnessTextureInUse > 0)
		{
			// Texture Holds Occulussion Roughness Metallic values in rgb.
			// Occulusion is not supported at the moment. ( No Value )
			vec3 orm = texture(s_metallicRoughness, v_texture).rgb;
			metallic = orm.b;
			roughness = orm.g;
		}
		else
		{
			metallic = material.metallic;
			roughness = material.roughness;
		}
		metallic = clamp(metallic, 0.0, 1.0);
		float perceptualRoughness = clamp(roughness, 0.045, 1.0);

	#if DrawAlphaMasked
		if(color.a <= material.alphaMaskThreshold)
		{
			discard;
		}
	#endif

	#if ShadingMode == SHADE_ALBEDO_ONLY
		fragColor = color;
		return;
	#endif

	#if ShadingMode == SHADE_METALLIC_ONLY
		fragColor = vec4(metallic, metallic, metallic, 1.0);
		return;
	#endif

	#if ShadingMode == SHADE_ROUGHNESS_ONLY
		fragColor = vec4(perceptualRoughness, perceptualRoughness, perceptualRoughness, 1.0);
		return;
	#endif

	#if ShadingMode == SHADE_LIGHTING_ONLY
		color.xyz = vec3(1.0);
	#endif

		vec3 n;
		if (material.normalMapInUse > 0)
		{
			n = texture(s_normalMap, v_texture).xyz;
			n = n * 2.0 - 1.0;
			n = TBN * n;
			n = normalize(n);
		}
		else
		{
			n = normalize(v_worldNormal);
		}

	#if ShadingMode == SHADE_NORMAL_ONLY
		fragColor = vec4(n * 0.5 + 0.5, 1.0);
		return;
	#endif

		perceptualRoughness = specularAntiAliasing(perceptualRoughness, n);
		roughness = perceptualRoughnessToRoughness(perceptualRoughness);

		vec3 e = normalize(camera.position - v_worldPos);

		// Compute energy compensation for multiscattering
		vec3 f0 = BaseReflectivityPBR(vec3(0.04), color.xyz, metallic);
		float NdotV = max(dot(n, e), 0.0);
		vec2 dfg = texture(s_brdfLut, vec2(NdotV, perceptualRoughness)).rg;
		vec3 energyComp = EnergyCompensation(dfg, f0);

		vec3 irradiance = PBRLighting(v_worldPos, v_viewDepth, n, e, camera.position, color.xyz, metallic, roughness, energyComp);

		float ambientOcclusion = AmbientOcclusion();
		irradiance += IBLPBR(n, e, color.xyz, metallic, perceptualRoughness, dfg, energyComp, v_worldPos) * ambientOcclusion;

		fragColor = vec4(irradiance, color.a) + vec4(emissive, 0.0);
	}
	
	
