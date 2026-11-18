// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"

/**
 * The game's materials are built in code with HLSL custom nodes that sample textures with
 * Texture2DSample. Ray-tracing hit shaders (hardware Lumen on High and Ultra, 2.1.1) have no
 * screen derivatives, so that call does not compile there ("Opcode Sample not valid in shader
 * model lib_6_6(closesthit)") and the material drops out of the ray-traced scene. Before a
 * built material compiles, every custom node's Texture2DSample becomes SC_TEX2D: the same call
 * in raster passes, the base level in ray-tracing hit shaders.
 */
namespace OsdsShaderSafety
{
	inline void PatchForRayTracing(UMaterial* Material)
	{
		if (!Material) return;
		static const TCHAR* Guard = TEXT(
			"#ifndef SC_TEX2D\n"
			"#if defined(RAYHITGROUPSHADER) && RAYHITGROUPSHADER\n"
			"#define SC_TEX2D(T, S, UV) Texture2DSampleLevel(T, S, UV, 0)\n"
			"#else\n"
			"#define SC_TEX2D(T, S, UV) Texture2DSample(T, S, UV)\n"
			"#endif\n"
			"#endif\n");
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression);
			if (!Custom || !Custom->Code.Contains(TEXT("Texture2DSample(")) || Custom->Code.Contains(TEXT("SC_TEX2D"))) continue;
			Custom->Code = FString(Guard) + Custom->Code.Replace(TEXT("Texture2DSample("), TEXT("SC_TEX2D("));
		}
	}
}
#endif
