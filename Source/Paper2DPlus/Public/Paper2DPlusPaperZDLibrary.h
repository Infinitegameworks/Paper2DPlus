// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Paper2DPlusPaperZDLibrary.generated.h"

class UPaper2DPlusCharacterProfileAsset;
class UPaperFlipbook;
class AActor;

/**
 * Blueprint library with PaperZD sequence accessors.
 *
 * Returns UObject* — cast to UPaperZDAnimSequence in Blueprint when PaperZD
 * is installed. When PaperZD is absent, these functions still exist but
 * return nullptr.
 */
UCLASS()
class PAPER2DPLUS_API UPaper2DPlusPaperZDLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|PaperZD")
	static UObject* FindPaperZDSequenceForFlipbook(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		UPaperFlipbook* Flipbook);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|PaperZD")
	static UObject* GetCachedPaperZDSequenceForFlipbook(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		UPaperFlipbook* Flipbook);

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|PaperZD")
	static UObject* GetActorCurrentPaperZDSequence(AActor* Actor);
};
