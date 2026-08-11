// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "Paper2DPlusFrameCueTypeFactory.generated.h"

/**
 * First-class Content Browser factory for reusable, behavior-capable Frame Cue Types.
 *
 * This intentionally derives from UFactory rather than UBlueprintFactory: there is no arbitrary
 * ParentClass property or class-viewer path. The only configuration is Cue versus Cue State, and
 * the shared authoring service remains the sole authority that creates the specialized envelope.
 */
UCLASS()
class PAPER2DPLUSEDITOR_API UPaper2DPlusFrameCueTypeFactory : public UFactory
{
	GENERATED_BODY()

public:
	UPaper2DPlusFrameCueTypeFactory();

	/** Preconfigures one valid kind so inline workflows can reuse the factory without a second modal. */
	void ConfigureForKind(EPaper2DPlusFrameCueTypeKind Kind);
	EPaper2DPlusFrameCueTypeKind GetConfiguredKind() const { return ConfiguredKind; }

	EPaper2DPlusFrameCueTypeCreateStatus GetLastCreateStatus() const
	{
		return LastCreateStatus;
	}
	const FText& GetLastCreateError() const { return LastCreateError; }

#if WITH_DEV_AUTOMATION_TESTS
	/** Headless seam for proving the explicit Cancel branch without spawning a modal window. */
	void SetKindDialogResponseForTests(
		bool bAccept,
		EPaper2DPlusFrameCueTypeKind Kind);
#endif

	// UFactory
	virtual bool ConfigureProperties() override;
	virtual UObject* FactoryCreateNew(
		UClass* Class,
		UObject* InParent,
		FName Name,
		EObjectFlags Flags,
		UObject* Context,
		FFeedbackContext* Warn,
		FName CallingContext) override;
	virtual UObject* FactoryCreateNew(
		UClass* Class,
		UObject* InParent,
		FName Name,
		EObjectFlags Flags,
		UObject* Context,
		FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual FString GetDefaultNewAssetName() const override;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
	virtual TArray<FAssetCategoryPath> GetAssetMenuPathsForCategory(FName InCategory) const override;
#endif

private:
	static bool IsSupportedKind(EPaper2DPlusFrameCueTypeKind Kind);
	UObject* CreateConfiguredCueType(
		UClass* Class,
		UObject* InParent,
		FName Name,
		EObjectFlags Flags,
		FFeedbackContext* Warn);

	EPaper2DPlusFrameCueTypeKind ConfiguredKind =
		EPaper2DPlusFrameCueTypeKind::Invalid;
	bool bUsePreconfiguredKind = false;
	EPaper2DPlusFrameCueTypeCreateStatus LastCreateStatus =
		EPaper2DPlusFrameCueTypeCreateStatus::CreationFailed;
	FText LastCreateError;

#if WITH_DEV_AUTOMATION_TESTS
	bool bHasKindDialogResponseForTests = false;
	bool bAcceptKindDialogForTests = false;
	EPaper2DPlusFrameCueTypeKind KindDialogResponseForTests =
		EPaper2DPlusFrameCueTypeKind::Invalid;
#endif
};
