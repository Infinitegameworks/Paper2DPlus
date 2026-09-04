// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"
#include "Modules/ModuleManager.h"
#include "AssetTypeCategories.h"

PAPER2DPLUSEDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogPaper2DPlusEditor, Log, All);

class PAPER2DPLUSEDITOR_API FPaper2DPlusEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Get the custom asset category for Paper2D+ assets */
	static EAssetTypeCategories::Type GetAssetCategory() { return Paper2DPlusAssetCategory; }

private:
	void RegisterAssetTools();
	void UnregisterAssetTools();
	void RegisterMenuExtensions();
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	void RegisterDataValidators();
	void UnregisterDataValidators();
#endif

	TArray<TSharedPtr<class IAssetTypeActions>> RegisteredAssetTypeActions;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	class UEditorValidatorBase* RegisteredCharacterProfileValidator = nullptr;
#endif

	/** Custom asset category for Paper2D+ assets */
	static EAssetTypeCategories::Type Paper2DPlusAssetCategory;

	/** True if we overrode the engine's "GameplayTag" property customization with the colored chip
	 *  (gated on UPaper2DPlusSettings::bColorizeGameplayTagPickers) — so Shutdown only unregisters
	 *  what we registered. */
	bool bRegisteredGameplayTagColorCustomization = false;

	/** Registers the colored-chip "GameplayTag" customization (overriding the engine default). */
	void RegisterGameplayTagColorCustomization();

	/** OnPostEngineInit hook — GameplayTagsEditor registers its FGameplayTag customization at
	 *  PostEngineInit, so we MUST re-register after it (StartupModule is too early — it gets clobbered). */
	FDelegateHandle PostEngineInitHandle;

	/** OnFEngineLoopInitComplete hook that initializes FTextureWatcherService once GEditor exists —
	 *  GEditor is null during StartupModule, so initializing there silently no-ops (bit: live .ase
	 *  auto-reimport was dead in every session until 2026-08-20). */
	FDelegateHandle WatcherInitHandle;

	/** Rehashes/remaps Cue-object track membership from Unreal's exact Blueprint replacement map. */
	FDelegateHandle CueObjectReplacementHandle;

	/** Animation Map comment-box widget factory. The node is the plain engine UEdGraphNode_Comment
	 *  because subclassing it is unlinkable on UE 5.0-5.4; the factory attaches plugin behavior without
	 *  introducing that link dependency. */
	TSharedPtr<struct FGraphPanelNodeFactory> AnimationMapCommentNodeFactory;

	/** Catalog-aware Character Profile pin widget used by Select Character from Catalog. */
	TSharedPtr<struct FGraphPanelPinFactory> CharacterCatalogPinFactory;
};
