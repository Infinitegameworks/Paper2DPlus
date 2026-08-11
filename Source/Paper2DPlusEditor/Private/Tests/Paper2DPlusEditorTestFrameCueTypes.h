// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
// The soft art field below hands back a soft Paper Flipbook reference. CoreMinimal does not reach
// TSoftObjectPtr on 5.0-5.5, so request it explicitly rather than relying on PCH order.
#include "UObject/SoftObjectPtr.h"

#if WITH_EDITOR

#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "FrameCues/Paper2DPlusFrameCueTypeEditor.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#endif // WITH_EDITOR

#include "Paper2DPlusEditorTestFrameCueTypes.generated.h"

class UPaperFlipbook;

/**
 * The editor module's shared concrete Cue Type pair.
 *
 * Both Cue bases are abstract, so after the built-in Cue Types were deleted the runtime module ships
 * zero concrete native Cue classes. These editor-only fixtures are not designer-placeable Cue Types;
 * tests that need a real placement object instantiate them directly.
 *
 * Declared in a header on purpose: UnrealBuildTool unity-groups the editor test .cpp files into one
 * translation unit, so same-named helpers declared in per-file anonymous namespaces collide. This
 * rule is written into AGENTS.md and has bitten this project twice.
 *
 * These types are NOT a substitute for a designer Cue Type in placement-commit tests. Every commit
 * seam routes through FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType, whose IsEditorOnlyClass
 * check rejects any class whose outermost package carries PKG_EditorOnly - and the script package
 * for an editor module (/Script/Paper2DPlusEditor) always does. Placement-commit coverage therefore
 * needs a durably saved Cue Type Blueprint, exactly as production placement does. The lower-level
 * Paper2DPlusFrameCueEditorAuthoring::CreatePlacement/DuplicatePlacement helpers apply no such
 * readiness gate and accept these fixtures directly.
 */
UCLASS()
class UPaper2DPlusEditorTestMomentCue : public UPaper2DPlusCue
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	FVector2D Offset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	FVector2D Scale = FVector2D(1.0, 1.0);

	/** Stands in for art that stays cold until the structural soft-flipbook warm pass finds it. */
	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	TSoftObjectPtr<UPaperFlipbook> SoftArt;

	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	TObjectPtr<UPaperFlipbook> EffectArt = nullptr;
};

/** Range counterpart of UPaper2DPlusEditorTestMomentCue, with the same payload surface. */
UCLASS()
class UPaper2DPlusEditorTestRangeCue : public UPaper2DPlusCueState
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	FVector2D Offset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	FVector2D Scale = FVector2D(1.0, 1.0);

	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	TSoftObjectPtr<UPaperFlipbook> SoftArt;

	UPROPERTY(EditAnywhere, Category = "Paper2DPlus Tests")
	TObjectPtr<UPaperFlipbook> EffectArt = nullptr;
};

#if WITH_EDITOR

/**
 * A genuinely placement-ready Cue Type, saved to disk for the lifetime of one test.
 *
 * Editor tests cannot use the native fixtures above for anything that commits a placement.
 * FPaper2DPlusFrameCuePlacementAuthoring::IsCueClassCompatible - the single readiness authority
 * behind FPaper2DPlusFrameCuePendingPlacement::Commit, CreatePlacementFromClassDefaults, and
 * SFrameEventEditor::AddFrameCue - routes through DescribeCueType, and DescribeCueType rejects any
 * class whose outermost package carries PKG_EditorOnly. The script package of an editor module
 * always does, so a native cue class declared anywhere in Paper2DPlusEditor can never be Ready.
 *
 * A designer Cue Type reaches Ready only by being compiled and durably saved, so that is exactly
 * what this fixture does: create through the production CreateCueType path, stage the durable
 * schema, save the package, finalize. IsReady() is asserted by callers rather than assumed, and the
 * destructor unloads the package and removes its files.
 */
struct FPaper2DPlusEditorTestPlaceableCueType
{
	FPaper2DPlusEditorTestPlaceableCueType(
		EPaper2DPlusFrameCueTypeKind InKind,
		const TCHAR* Stem,
		TFunction<void(UPaper2DPlusCueBase&)> ConfigureDefaults = nullptr)
	{
		PackageName = FString::Printf(
			TEXT("/Game/__AutomationTemp__/%s_%s"),
			Stem,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		FilePath = FPackageName::LongPackageNameToFilename(
			PackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);

		Package = CreatePackage(*PackageName);
		if (!Package)
		{
			Error = TEXT("Placeable Cue Type fixture could not create its package.");
			return;
		}
		Package->AddToRoot();

		FPaper2DPlusFrameCueTypeCreateRequest Request;
		Request.Package = Package;
		Request.AssetName = FName(*FString::Printf(TEXT("BP_%s"), Stem));
		Request.Kind = InKind;
		const FPaper2DPlusFrameCueTypeCreateResult Result =
			FPaper2DPlusFrameCueTypeAuthoring::CreateCueType(Request);
		if (!Result.IsSuccess())
		{
			Error = FString::Printf(
				TEXT("Placeable Cue Type fixture creation failed: %s"), *Result.Error.ToString());
			return;
		}
		CueType = Result.CueType;
		if (ConfigureDefaults)
		{
			UPaper2DPlusCueBase* Defaults = CueType->GeneratedClass
				? Cast<UPaper2DPlusCueBase>(CueType->GeneratedClass->GetDefaultObject())
				: nullptr;
			if (!Defaults)
			{
				Error = TEXT("Placeable Cue Type fixture could not resolve its generated defaults.");
				return;
			}
			ConfigureDefaults(*Defaults);
		}
		CueType->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
		CueType->bIsNewlyCreated = false;
		Package->MarkPackageDirty();

		FPaper2DPlusFrameCueDurableSaveAttempt SaveAttempt;
		FText StageError;
		if (!FPaper2DPlusFrameCueTypeEditor::PrepareDurableSchemaSaveAttempt(
			*CueType, SaveAttempt, &StageError))
		{
			Error = FString::Printf(
				TEXT("Placeable Cue Type fixture could not stage its durable schema: %s"),
				*StageError.ToString());
			return;
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Package, CueType, *FilePath, SaveArgs);
		FText FinalizeError;
		const bool bFinalized = FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(
			*CueType, SaveAttempt, &FinalizeError);
		if (!bSaved || !bFinalized)
		{
			Error = FString::Printf(
				TEXT("Placeable Cue Type fixture save failed (saved=%d): %s"),
				bSaved ? 1 : 0,
				*FinalizeError.ToString());
			return;
		}

		UClass* GeneratedClass = CueType->GeneratedClass;
		bReady = GeneratedClass
			&& FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(GeneratedClass).Availability
				== EPaper2DPlusFrameCueTypeAvailability::Ready;
		if (!bReady)
		{
			Error = TEXT("Placeable Cue Type fixture saved but is still not placement-ready.");
		}
	}

	~FPaper2DPlusEditorTestPlaceableCueType()
	{
		CueType = nullptr;
		if (Package)
		{
			Package->SetDirtyFlag(false);
			Package->RemoveFromRoot();
			FText UnloadError;
			TArray<UPackage*> Packages = { Package };
			UPackageTools::UnloadPackages(Packages, UnloadError, true);
			Package = nullptr;
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
		IFileManager& FileManager = IFileManager::Get();
		FileManager.Delete(*FilePath, false, true, true);
		FileManager.Delete(*FPaths::ChangeExtension(FilePath, TEXT("uexp")), false, true, true);
		FileManager.Delete(*FPaths::ChangeExtension(FilePath, TEXT("ubulk")), false, true, true);
		FileManager.Delete(*FPaths::ChangeExtension(FilePath, TEXT("uptnl")), false, true, true);
	}

	FPaper2DPlusEditorTestPlaceableCueType(const FPaper2DPlusEditorTestPlaceableCueType&) = delete;
	FPaper2DPlusEditorTestPlaceableCueType& operator=(
		const FPaper2DPlusEditorTestPlaceableCueType&) = delete;

	bool IsReady() const { return bReady; }
	UClass* GetCueClass() const { return CueType ? CueType->GeneratedClass.Get() : nullptr; }
	const FString& GetError() const { return Error; }

	FString PackageName;
	FString FilePath;
	FString Error;
	UPackage* Package = nullptr;
	UPaper2DPlusFrameCueBlueprint* CueType = nullptr;
	bool bReady = false;
};

#endif // WITH_EDITOR
