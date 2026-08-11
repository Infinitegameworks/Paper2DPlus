// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusSpriteMaterialContract.h"

#include "Misc/EngineVersionComparison.h"
#include "MaterialShared.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2
// UE 5.0–5.1 define EMaterialDomain in MaterialShared.h. UE 5.2 split the definition into
// MaterialDomain.h and left only a forward declaration in MaterialShared.h.
#include "MaterialDomain.h"
#endif
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

bool Paper2DPlusSpriteMaterialContract::IsKnownPaper2DMaterial(
	const UMaterialInterface* Material)
{
	if (!Material)
	{
		return false;
	}

	static const TSet<FName> SupportedNames = {
		TEXT("DefaultSpriteMaterial"),
		TEXT("TranslucentUnlitSpriteMaterial"),
		TEXT("MaskedUnlitSpriteMaterial"),
		TEXT("OpaqueUnlitSpriteMaterial")
	};
	return Material->GetPathName().StartsWith(TEXT("/Paper2D/"))
		&& SupportedNames.Contains(Material->GetFName());
}

bool Paper2DPlusSpriteMaterialContract::ResolveOpacityMode(
	const UMaterialInterface* Material,
	bool bRequireKnownPaper2DMaterial,
	EPaper2DPlusSpriteOpacityMode& OutMode,
	float& OutOpacityMaskClipValue,
	FString& OutError)
{
	OutMode = EPaper2DPlusSpriteOpacityMode::Translucent;
	OutOpacityMaskClipValue = 0.3333f;
	OutError.Reset();

	const UMaterial* BaseMaterial = Material ? Material->GetMaterial() : nullptr;
	if (!BaseMaterial || BaseMaterial->MaterialDomain != MD_Surface
		|| !Material->GetShadingModels().HasOnlyShadingModel(MSM_Unlit))
	{
		OutError = TEXT("Only resident Surface materials using exactly the Unlit shading model can be composited exactly.");
		return false;
	}
	if (bRequireKnownPaper2DMaterial && !IsKnownPaper2DMaterial(Material))
	{
		OutError = FString::Printf(
			TEXT("Material '%s' has custom opacity/color behavior. Use a stock Paper2D unlit sprite material or keep this art live."),
			*Material->GetPathName());
		return false;
	}

	switch (Material->GetBlendMode())
	{
	case BLEND_Opaque:
		OutMode = EPaper2DPlusSpriteOpacityMode::Opaque;
		break;
	case BLEND_Masked:
		OutMode = EPaper2DPlusSpriteOpacityMode::Masked;
		break;
	case BLEND_Translucent:
		OutMode = EPaper2DPlusSpriteOpacityMode::Translucent;
		break;
	default:
		OutError = TEXT("Only Opaque, Masked, and straight-alpha Translucent blend modes are supported.");
		return false;
	}

	OutOpacityMaskClipValue = Material->GetOpacityMaskClipValue();
	if (!FMath::IsFinite(OutOpacityMaskClipValue))
	{
		OutError = TEXT("The material has a non-finite opacity-mask clip value.");
		return false;
	}
	return true;
}

bool Paper2DPlusSpriteMaterialContract::CanRepresentFlattenedOpacity(
	EPaper2DPlusSpriteOpacityMode Output,
	EPaper2DPlusSpriteOpacityMode Source)
{
	switch (Output)
	{
	case EPaper2DPlusSpriteOpacityMode::Translucent:
		return true;
	case EPaper2DPlusSpriteOpacityMode::Masked:
		return Source != EPaper2DPlusSpriteOpacityMode::Translucent;
	case EPaper2DPlusSpriteOpacityMode::Opaque:
		return Source == EPaper2DPlusSpriteOpacityMode::Opaque;
	default:
		return false;
	}
}

const TCHAR* Paper2DPlusSpriteMaterialContract::LexToString(
	EPaper2DPlusSpriteOpacityMode Mode)
{
	switch (Mode)
	{
	case EPaper2DPlusSpriteOpacityMode::Opaque:
		return TEXT("Opaque");
	case EPaper2DPlusSpriteOpacityMode::Masked:
		return TEXT("Masked");
	case EPaper2DPlusSpriteOpacityMode::Translucent:
		return TEXT("Translucent");
	default:
		return TEXT("Unknown");
	}
}
