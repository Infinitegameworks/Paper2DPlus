// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

class UBlueprint;
class UPaper2DPlusCueBase;

/** Validated, non-interactive input for creating a project Frame Cue preview-adapter Blueprint. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCuePreviewAdapterCreateRequest
{
	FString PackagePath = TEXT("/Game/Blueprints/FrameCuePreviews");
	FString AssetName;
	TSubclassOf<UPaper2DPlusCueBase> SupportedCueClass;
	bool bIncludeDerivedCueClasses = true;
};

/** Safe creation seam used by the future Create Preview Adapter wizard/menu action. */
class PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCuePreviewAuthoring
{
public:
	/** Validates every input and collision without loading or creating the target package. */
	static bool ValidateCreateRequest(
		const FPaper2DPlusFrameCuePreviewAdapterCreateRequest& Request,
		FString& OutLongPackageName,
		FText& OutError);

	/** Creates and compiles the adapter only after ValidateCreateRequest succeeds. */
	static UBlueprint* CreatePreviewAdapterBlueprint(
		const FPaper2DPlusFrameCuePreviewAdapterCreateRequest& Request,
		FText& OutError);
};

