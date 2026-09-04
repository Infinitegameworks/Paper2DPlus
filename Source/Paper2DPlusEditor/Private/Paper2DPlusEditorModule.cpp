// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusEditorModule.h"
#include "Paper2DPlusEditorStyle.h"
#include "Paper2DPlusEditorIcons.h"
#include "Paper2DPlusDirectionalAnimationCommands.h"
#include "Paper2DPlusProfileEditorToolbar.h"
#include "AssetToolsModule.h"

DEFINE_LOG_CATEGORY(LogPaper2DPlusEditor);
#include "IAssetTools.h"
#include "CharacterProfileAssetActions.h"
#include "CharacterCatalogAssetActions.h"
#include "CharacterCatalog/Paper2DPlusCatalogGraphPinFactory.h"
#include "CharacterLayerAssetActions.h"
#include "ClashGraphAssetActions.h"
#include "CombatProfileAssetActions.h"
#include "EffectProfileAssetActions.h"
#include "FlipbookDrawEditorToolkit.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserMenuContexts.h"
#include "ToolMenus.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
#include "Paper2DPlusCharacterProfileAssetValidator.h"
#include "EditorValidatorSubsystem.h"
#endif
#include "SpriteExtractorWindow.h"
#include "AsepriteImporter.h"
#include "AsepriteContentBrowserDrop.h"
#include "AnimationMap/SPaper2DPlusAnimationMapCommentNode.h"
#include "EdGraphUtilities.h"
#include "TextureWatcherService.h"
#include "CharacterProfileAssetThumbnailRenderer.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "Editor.h"
#include "PaperFlipbook.h"
#include "ScopedTransaction.h"
// UE 5.0 compat: FAppStyle/AppStyle.h doesn't exist, use FEditorStyle
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "Customizations/Paper2DPlusCombatConsiderationCustomization.h"
#include "Paper2DPlusGameplayTagColorCustomization.h"
#include "Paper2DPlusSettings.h"
#include "Paper2DPlusEffectLibraryIndex.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewBehavior.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAssetActions.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "FrameCues/Paper2DPlusFrameCueTypeBirthReady.h"
#include "FrameCues/Paper2DPlusFrameCueTypeEditor.h"
#include "KismetCompiler.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Misc/CoreDelegates.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SButton.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"

/** FPaper2DPlusEditorModule — Editor plugin startup: asset type registration, menu extensions, validator setup. */

#define LOCTEXT_NAMESPACE "FPaper2DPlusEditorModule"

// Initialize static member
EAssetTypeCategories::Type FPaper2DPlusEditorModule::Paper2DPlusAssetCategory = EAssetTypeCategories::Misc;

namespace Paper2DPlusFrameCueCompileValidation
{
	void Validate(
		UPaper2DPlusFrameCueBlueprint& Blueprint,
		FKismetCompilerContext* CompilerContext)
	{
		if (!CompilerContext)
		{
			return;
		}

		FText ContractError;
		if (!UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(
			Blueprint,
			&ContractError))
		{
			CompilerContext->MessageLog.Error(*FString::Printf(
				TEXT("Frame Cue Type compilation refused: %s"),
				*ContractError.ToString()));
			return;
		}
		if (!FPaper2DPlusFrameCueTypeAuthoring::ValidateSynchronousBehavior(
			Blueprint,
			&ContractError))
		{
			CompilerContext->MessageLog.Error(*FString::Printf(
				TEXT("Frame Cue Type compilation refused: %s"),
				*ContractError.ToString()));
		}
	}
}

namespace Paper2DPlusFrameCueAutomaticSave
{
	FDelegateHandle PackageSavedHandle;

	void SetError(FText* OutError, const FText& Error)
	{
		if (OutError)
		{
			*OutError = Error;
		}
	}

	bool Prepare(
		UPaper2DPlusFrameCueBlueprint& Blueprint,
		FText* OutError)
	{
		FText ContractError;
		if (!UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(
			Blueprint,
			&ContractError))
		{
			SetError(OutError, ContractError);
			return false;
		}
		if (!FPaper2DPlusFrameCueTypeAuthoring::ValidateSynchronousBehavior(
			Blueprint,
			&ContractError))
		{
			SetError(OutError, ContractError);
			return false;
		}

		const FPaper2DPlusFrameCueSchemaPreflight Preflight =
			FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(Blueprint);
		// The restricted editor's schema-change dialogs record the exact authored-schema
		// fingerprint whose consequences the author reviewed on screen — the destructive change
		// list, or the baseline-recovery consent when the stored baseline is unusable. While the
		// authored schema still equals that confirmation, an ordinary save (Save All, Content
		// Browser, close prompts) commits the reviewed change; anything unconfirmed fails closed.
		const bool bChangeExplicitlyConfirmed =
			Preflight.CurrentSchema.bValid
			&& !Blueprint.ConfirmedDestructiveSchemaFingerprint.IsEmpty()
			&& Blueprint.ConfirmedDestructiveSchemaFingerprint
				== Preflight.CurrentSchema.Fingerprint;
		if (!Preflight.bCanCompile)
		{
			// An unusable durable baseline also lands in bCanCompile=false. Staging never reads
			// the broken baseline, so a confirmed baseline-recovery compile may re-establish it
			// through this save; an invalid AUTHORED schema can never proceed.
			const bool bConfirmedBaselineRecovery = bChangeExplicitlyConfirmed
				&& Preflight.CurrentSchemaError.IsEmpty()
				&& !Preflight.DurableBaselineError.IsEmpty();
			if (!bConfirmedBaselineRecovery)
			{
				SetError(
					OutError,
					!Preflight.CurrentSchemaError.IsEmpty()
						? Preflight.CurrentSchemaError
						: (!Preflight.DurableBaselineError.IsEmpty()
							? Preflight.DurableBaselineError
							: LOCTEXT(
								"AutomaticCueSaveInvalidSchema",
								"This Cue Type's authored schema is invalid and cannot be prepared for Save All.")));
				return false;
			}
		}
		else if (Preflight.bRequiresDestructiveConfirmation && !bChangeExplicitlyConfirmed)
		{
			SetError(
				OutError,
				LOCTEXT(
					"AutomaticCueSaveNeedsConfirmation",
					"This Cue Type contains a schema change that can reset existing placement values. "
					"Open the Cue Type, compile it, and confirm the schema-change prompt (or use the Cue "
					"Type editor's own Save); compatible payload and behavior edits save normally "
					"through Save All."));
			return false;
		}
		if (Blueprint.bHasPendingDefaultProposal
			&& Blueprint.bPendingDefaultProposalNeedsCompile)
		{
			SetError(
				OutError,
				LOCTEXT(
					"AutomaticCueSavePendingDefaultProposal",
					"This Cue Type has a recovered default-value proposal that needs the protected Cue "
					"Type compile. Open the Cue Type and use Save once; ordinary saves need no separate "
					"staging step afterward."));
			return false;
		}

		FText CompiledError;
		const bool bCompiledSchemaIsCurrent =
			(Blueprint.Status == BS_UpToDate
				|| Blueprint.Status == BS_UpToDateWithWarnings)
			&& UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
				Blueprint,
				&CompiledError)
			&& FPaper2DPlusFrameCueTypeAuthoring::ValidateCompiledSchemaParity(
				Blueprint,
				&CompiledError);
		if (!bCompiledSchemaIsCurrent)
		{
			FKismetEditorUtilities::CompileBlueprint(&Blueprint);
		}

		if ((Blueprint.Status != BS_UpToDate
				&& Blueprint.Status != BS_UpToDateWithWarnings)
			|| !UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
				Blueprint,
				&CompiledError)
			|| !FPaper2DPlusFrameCueTypeAuthoring::ValidateCompiledSchemaParity(
				Blueprint,
				&CompiledError))
		{
			SetError(
				OutError,
				CompiledError.IsEmpty()
					? LOCTEXT(
						"AutomaticCueSaveCompileFailed",
						"This Cue Type could not compile its current payload and behavior before saving.")
					: CompiledError);
			return false;
		}

		const FPaper2DPlusFrameCueDurableSchemaCandidate Candidate =
			FPaper2DPlusFrameCueTypeAuthoring::PrepareDurableSchemaSaveCandidate(Blueprint);
		if (!Candidate.bSuccess)
		{
			SetError(OutError, Candidate.Error);
			return false;
		}
		SetError(OutError, FText::GetEmpty());
		return true;
	}

	void HandlePackageSaved(
		const FString&,
		UPackage* Package,
		FObjectPostSaveContext SaveContext)
	{
		if (!Package)
		{
			return;
		}
		if ((SaveContext.GetSaveFlags() & SAVE_FromAutosave) != 0)
		{
			// Autosave writes a recovery copy rather than the canonical asset. Let the queued
			// fallback restore the canonical durable baseline while retaining the authored edit.
			return;
		}

		// CoreUObject broadcasts this event only after the exact package write succeeds. Commit every
		// specialized Cue Type in that package; ordinary asset packages normally contain one, while
		// legacy multi-asset packages remain correct without guessing an object name.
		ForEachObjectWithPackage(
			Package,
			[](UObject* Object)
			{
				if (UPaper2DPlusFrameCueBlueprint* Blueprint =
					Cast<UPaper2DPlusFrameCueBlueprint>(Object))
				{
					Blueprint->CommitAutomaticDurableSaveAfterPackageSaved();
				}
				return true;
			});
	}
}

namespace Paper2DPlusFrameCuePersistRefusalToast
{
	// The engine's failed-save prompt re-runs the refused write on Retry, and a bulk Save All can
	// refuse the same asset several times in one gesture, so identical reasons inside the window
	// earn exactly one toast.
	FString LastReasonShown;
	double LastShownSeconds = 0.0;

	void Show(const UPaper2DPlusFrameCueBlueprint& Blueprint, const FText& Reason)
	{
		if (!IsInGameThread() || !FSlateApplication::IsInitialized())
		{
			return;
		}
		const FString ReasonString = Reason.ToString();
		const double NowSeconds = FPlatformTime::Seconds();
		if (ReasonString == LastReasonShown && NowSeconds - LastShownSeconds < 10.0)
		{
			return;
		}
		LastReasonShown = ReasonString;
		LastShownSeconds = NowSeconds;

		FNotificationInfo Notification(FText::Format(
			LOCTEXT("CueTypeSaveRefusedTitle", "Cue Type '{0}' was not saved"),
			FText::FromString(Blueprint.GetName())));
		Notification.SubText = Reason;
		Notification.bFireAndForget = true;
		Notification.ExpireDuration = 12.0f;
		Notification.bUseLargeFont = false;
		Notification.bUseSuccessFailIcons = true;
		if (const TSharedPtr<SNotificationItem> Item =
			FSlateNotificationManager::Get().AddNotification(Notification))
		{
			Item->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}
}

namespace Paper2DPlusFrameCueTrackReinstance
{
	void RemapLoadedLayouts(
		const FCoreUObjectDelegates::FReplacementObjectMap& ObjectReplacements)
	{
		FPaper2DPlusFrameCueReplacementMap CueReplacements;
		for (const TPair<UObject*, UObject*>& Pair : ObjectReplacements)
		{
			if (UPaper2DPlusCueBase* Previous = Cast<UPaper2DPlusCueBase>(Pair.Key))
			{
				CueReplacements.Add(Previous, Cast<UPaper2DPlusCueBase>(Pair.Value));
			}
		}
		if (CueReplacements.IsEmpty())
		{
			return;
		}

		for (TObjectIterator<UPaper2DPlusCharacterProfileAsset> It; It; ++It)
		{
			UPaper2DPlusCharacterProfileAsset* Profile = *It;
			if (!Profile || Profile->IsTemplate()) continue;
			bool bChanged = false;
			for (FFlipbookProfileEntry& Entry : Profile->Flipbooks)
			{
				bChanged |= Entry.FrameEventData.CueTrackLayout.RemapCueReferences(
					CueReplacements);
			}
			for (FPaper2DPlusCharacterBaselineAnimation& Baseline : Profile->CharacterBaseline)
			{
				bChanged |= Baseline.CueTrackLayout.RemapCueReferences(CueReplacements);
			}
			if (bChanged)
			{
				Profile->MarkPackageDirty();
			}
		}

		for (TObjectIterator<UPaper2DPlusCharacterLayerAsset> It; It; ++It)
		{
			UPaper2DPlusCharacterLayerAsset* LayerAsset = *It;
			if (!LayerAsset || LayerAsset->IsTemplate()) continue;
			bool bChanged = false;
			for (FCharacterLayer& Layer : LayerAsset->Layers)
			{
				for (FCharacterLayerAuthoredAnimationData& Animation : Layer.AuthoredAnimations)
				{
					bChanged |= Animation.CueTrackLayout.RemapCueReferences(
						CueReplacements);
				}
			}
			if (bChanged)
			{
				LayerAsset->MarkPackageDirty();
			}
		}
	}
}

void FPaper2DPlusEditorModule::StartupModule()
{
	UPaper2DPlusFrameCueBlueprint::SetCompileValidationDelegate(
		FPaper2DPlusFrameCueCompileValidation::CreateStatic(
			&Paper2DPlusFrameCueCompileValidation::Validate));
	UPaper2DPlusFrameCueBlueprint::SetCompiledSchemaValidationDelegate(
		FPaper2DPlusFrameCueCompiledSchemaValidation::CreateStatic(
			&FPaper2DPlusFrameCueTypeAuthoring::ValidateCompiledSchemaParity));
	UPaper2DPlusFrameCueBlueprint::SetDurableSaveValidationDelegate(
		FPaper2DPlusFrameCueDurableSaveValidation::CreateStatic(
			&FPaper2DPlusFrameCueTypeAuthoring::ValidateDurableSaveMetadataForPersistence));
	UPaper2DPlusFrameCueBlueprint::SetAutomaticSavePreparationDelegate(
		FPaper2DPlusFrameCueAutomaticSavePreparation::CreateStatic(
			&Paper2DPlusFrameCueAutomaticSave::Prepare));
	UPaper2DPlusFrameCueBlueprint::SetPersistRefusalNotificationDelegate(
		FPaper2DPlusFrameCuePersistRefusalNotification::CreateStatic(
			&Paper2DPlusFrameCuePersistRefusalToast::Show));
	Paper2DPlusFrameCueAutomaticSave::PackageSavedHandle =
		UPackage::PackageSavedWithContextEvent.AddStatic(
			&Paper2DPlusFrameCueAutomaticSave::HandlePackageSaved);
	CueObjectReplacementHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddStatic(
		&Paper2DPlusFrameCueTrackReinstance::RemapLoadedLayouts);
	// The runtime module declares the context-aware sound seam and leaves it unbound so it never
	// depends on editor code. Binding it here gives the Frame Cues editor deterministic ownership of
	// helper-routed audio while ordinary sound nodes use the host's isolated preview world/device.
	Paper2DPlusFrameCuePreviewBehavior::RegisterPreviewSoundHandler();

	// Register the plugin's editor style set (custom bundled icons, e.g. the Frame Timing clock) before
	// anything spawns tabs/menus that reference its brushes (audit).
	FPaper2DPlusEditorStyle::Register();
	FPaper2DPlusDirectionalAnimationCommands::Register();

	// Register custom asset category
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	Paper2DPlusAssetCategory = AssetTools.RegisterAdvancedAssetCategory(
		FName(TEXT("Paper2DPlus")),
		LOCTEXT("Paper2DPlusAssetCategory", "Paper2D+")
	);

	RegisterAssetTools();
	RegisterMenuExtensions();

	// Register the Combat Profile property-type customizations (guided consideration editor).
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomPropertyTypeLayout(
			TEXT("Paper2DPlusCombatConsideration"),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPaper2DPlusCombatConsiderationCustomization::MakeInstance));

		PropertyModule.NotifyCustomizationModuleChanged();
	}

	// Tag Colors: optionally override the engine's FGameplayTag property widget editor-wide with the
	// colored-chip customization. Gated on the project setting so it can be turned off.
	// IMPORTANT: GameplayTagsEditor registers its FGameplayTag customization in OnPostEngineInit (NOT in
	// StartupModule), so a StartupModule registration here gets CLOBBERED. We therefore (a) register now
	// (wins when this module is loaded AFTER PostEngineInit, e.g. hot reload) AND (b) re-register on our
	// own OnPostEngineInit hook, added after the engine's, so ours runs last and wins in the normal path.
	// 5.3+ only: the customization is built on the public SGameplayTagPicker, which does not exist before
	// 5.3 — older engines keep the stock engine widget (the registry + all other consumers still work).
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	if (const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get())
	{
		UE_LOG(LogPaper2DPlusEditor, Display, TEXT("TagColors: bColorizeGameplayTagPickers=%d"), Settings->bColorizeGameplayTagPickers ? 1 : 0);
		if (Settings->bColorizeGameplayTagPickers)
		{
			RegisterGameplayTagColorCustomization();
			// GameplayTagsEditor registers its FGameplayTag customization in OnPostEngineInit, which would
			// clobber a StartupModule registration. Defer ONE tick past PostEngineInit so ours is last.
			// (GetOnPostEngineInit() is 5.8+ — the bare OnPostEngineInit member is the pre-5.8 spelling and
			// is UE_DEPRECATED from 5.8, so both spellings must be version-gated for the 5.0–5.8 contract.)
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
			FSimpleMulticastDelegate& PostEngineInitDelegate = FCoreDelegates::GetOnPostEngineInit();
#else
			FSimpleMulticastDelegate& PostEngineInitDelegate = FCoreDelegates::OnPostEngineInit;
#endif
			PostEngineInitHandle = PostEngineInitDelegate.AddLambda([this]()
			{
				if (GEditor)
				{
					GEditor->GetTimerManager()->SetTimerForNextTick([this]()
					{
						RegisterGameplayTagColorCustomization();
					});
				}
				else
				{
					RegisterGameplayTagColorCustomization();
				}
			});
		}
	}
#endif // >= 5.3
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	RegisterDataValidators();
#endif
	FSpriteExtractorActions::RegisterMenus();
	FAsepriteImporter::RegisterMenus();
	// A Content Browser drop of .ase files must reach the Bulk Sprite Extractor as ONE list; the
	// factory path can only ever deliver the first file (see AsepriteContentBrowserDrop.h).
	Paper2DPlusEditor::AsepriteContentBrowserDrop::Register();

	// Plain engine comment nodes inside the Animation Map receive the cross-version-safe write-through
	// Slate widget. The factory fast-rejects all other graph/node types.
	AnimationMapCommentNodeFactory = MakeShared<FPaper2DPlusAnimationMapCommentNodeFactory>();
	FEdGraphUtilities::RegisterVisualNodeFactory(AnimationMapCommentNodeFactory);

	CharacterCatalogPinFactory = MakeShared<FPaper2DPlusCatalogGraphPinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(CharacterCatalogPinFactory);

	// Register CharacterProfile thumbnail renderer so the Content Browser shows a
	// preview of the first flipbook's first-frame sprite instead of the generic icon.
	UThumbnailManager::Get().RegisterCustomRenderer(
		UPaper2DPlusCharacterProfileAsset::StaticClass(),
		UPaper2DPlusCharacterProfileThumbnailRenderer::StaticClass());

	// Initialize the texture/.ase watcher service. GEditor is STILL NULL while plugin editor modules
	// load, so the old `if (GEditor)` guard here silently disabled the watcher in EVERY session — live
	// .ase auto-reimport was dead code from the day it shipped (BloodJunkies.log 2026-08-20: zero
	// TextureWatcherService lines across whole sessions). OnFEngineLoopInitComplete fires once the
	// editor fully exists; the GEditor re-check inside keeps commandlet/headless runs watcher-free.
	// The direct branch covers a module loaded late (engine loop already complete).
	if (GEditor)
	{
		FTextureWatcherService::Get().Initialize();
	}
	else
	{
		WatcherInitHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([]()
		{
			if (GEditor)
			{
				FTextureWatcherService::Get().Initialize();
			}
		});
	}
}

void FPaper2DPlusEditorModule::RegisterGameplayTagColorCustomization()
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout(
		TEXT("GameplayTag"),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPaper2DPlusGameplayTagColorCustomization::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();
	bRegisteredGameplayTagColorCustomization = true;
	UE_LOG(LogPaper2DPlusEditor, Display, TEXT("TagColors: registered colored 'GameplayTag' property customization (override)"));
#endif
}

void FPaper2DPlusEditorModule::ShutdownModule()
{
	Paper2DPlusEditor::AsepriteContentBrowserDrop::Unregister();
	FPaper2DPlusDirectionalAnimationCommands::Unregister();
	UToolMenus::UnregisterOwner(
		Paper2DPlusProfileEditorToolbar::OwnerName);
	UToolMenus::UnregisterOwner(
		FPaper2DPlusFrameCueTypeEditor::ToolbarOwnerName);
	FPaper2DPlusEffectLibraryIndex::Shutdown();
	UPaper2DPlusFrameCueBlueprint::ClearCompileValidationDelegate();
	UPaper2DPlusFrameCueBlueprint::ClearCompiledSchemaValidationDelegate();
	UPaper2DPlusFrameCueBlueprint::ClearDurableSaveValidationDelegate();
	UPaper2DPlusFrameCueBlueprint::ClearAutomaticSavePreparationDelegate();
	UPaper2DPlusFrameCueBlueprint::ClearPersistRefusalNotificationDelegate();
	if (Paper2DPlusFrameCueAutomaticSave::PackageSavedHandle.IsValid())
	{
		UPackage::PackageSavedWithContextEvent.Remove(
			Paper2DPlusFrameCueAutomaticSave::PackageSavedHandle);
		Paper2DPlusFrameCueAutomaticSave::PackageSavedHandle.Reset();
	}
	UPaper2DPlusFrameCueBlueprint::
		ResolveAllAutomaticDurableSaveTransactionsForShutdown();
	Paper2DPlusFrameCuePreviewBehavior::UnregisterPreviewSoundHandler();
	// A queued first-save is one frame long, but the ticker it registers must never outlive this
	// module's code.
	FPaper2DPlusFrameCueTypeBirthReady::Shutdown();
	if (CueObjectReplacementHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectsReplaced.Remove(CueObjectReplacementHandle);
		CueObjectReplacementHandle.Reset();
	}

	if (PostEngineInitHandle.IsValid())
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
#else
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
#endif
		PostEngineInitHandle.Reset();
	}

	// Shutdown texture watcher service first
	if (WatcherInitHandle.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(WatcherInitHandle);
		WatcherInitHandle.Reset();
	}
	FTextureWatcherService::Get().Shutdown();

	if (AnimationMapCommentNodeFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualNodeFactory(AnimationMapCommentNodeFactory);
		AnimationMapCommentNodeFactory.Reset();
	}
	if (CharacterCatalogPinFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualPinFactory(CharacterCatalogPinFactory);
		CharacterCatalogPinFactory.Reset();
	}

	FPaper2DPlusEditorStyle::Unregister();

	// Unregister thumbnail renderer (safe if UThumbnailManager was never used)
	if (UObjectInitialized())
	{
		UThumbnailManager::Get().UnregisterCustomRenderer(UPaper2DPlusCharacterProfileAsset::StaticClass());
	}

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomPropertyTypeLayout(TEXT("Paper2DPlusCombatConsideration"));
		if (bRegisteredGameplayTagColorCustomization)
		{
			PropertyModule.UnregisterCustomPropertyTypeLayout(TEXT("GameplayTag"));
			bRegisteredGameplayTagColorCustomization = false;
		}
	}

	UnregisterAssetTools();
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	UnregisterDataValidators();
#endif
	FSpriteExtractorActions::UnregisterMenus();
	FAsepriteImporter::UnregisterMenus();
}

void FPaper2DPlusEditorModule::RegisterAssetTools()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// Register asset type actions for Paper2DPlusCharacterProfileAsset
	TSharedPtr<IAssetTypeActions> CharacterProfileActions = MakeShareable(new FCharacterProfileAssetActions());
	AssetTools.RegisterAssetTypeActions(CharacterProfileActions.ToSharedRef());
	RegisteredAssetTypeActions.Add(CharacterProfileActions);

	TSharedPtr<IAssetTypeActions> CharacterCatalogActions = MakeShareable(new FCharacterCatalogAssetActions());
	AssetTools.RegisterAssetTypeActions(CharacterCatalogActions.ToSharedRef());
	RegisteredAssetTypeActions.Add(CharacterCatalogActions);

	TSharedPtr<IAssetTypeActions> CharacterLayerActions = MakeShareable(new FCharacterLayerAssetActions());
	AssetTools.RegisterAssetTypeActions(CharacterLayerActions.ToSharedRef());
	RegisteredAssetTypeActions.Add(CharacterLayerActions);

	TSharedPtr<IAssetTypeActions> CombatProfileActions = MakeShareable(new FCombatProfileAssetActions());
	AssetTools.RegisterAssetTypeActions(CombatProfileActions.ToSharedRef());
	RegisteredAssetTypeActions.Add(CombatProfileActions);

	TSharedPtr<IAssetTypeActions> EffectProfileActions = MakeShareable(new FEffectProfileAssetActions());
	AssetTools.RegisterAssetTypeActions(EffectProfileActions.ToSharedRef());
	RegisteredAssetTypeActions.Add(EffectProfileActions);

	TSharedPtr<IAssetTypeActions> ClashGraphActions = MakeShareable(new FClashGraphAssetActions());
	AssetTools.RegisterAssetTypeActions(ClashGraphActions.ToSharedRef());
	RegisteredAssetTypeActions.Add(ClashGraphActions);

	TSharedPtr<IAssetTypeActions> FrameCueTypeActions =
		MakeShareable(new FPaper2DPlusFrameCueTypeAssetActions());
	AssetTools.RegisterAssetTypeActions(FrameCueTypeActions.ToSharedRef());
	RegisteredAssetTypeActions.Add(FrameCueTypeActions);
}

void FPaper2DPlusEditorModule::UnregisterAssetTools()
{
	FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");
	if (AssetToolsModule)
	{
		IAssetTools& AssetTools = AssetToolsModule->Get();
		for (TSharedPtr<IAssetTypeActions>& Actions : RegisteredAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(Actions.ToSharedRef());
		}
	}
	RegisteredAssetTypeActions.Empty();
}

void FPaper2DPlusEditorModule::RegisterMenuExtensions()
{
// UE 5.0 UContentBrowserAssetContextMenuContext lacks SelectedAssets/LoadSelectedObjects — skip context menu extension
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.PaperFlipbook");
		if (!Menu) return;

		FToolMenuSection& Section = Menu->FindOrAddSection("CommonAssetActions");
		Section.AddSubMenu(
			"Paper2DPlusActions",
			LOCTEXT("Paper2DPlusActionsLabel", "Paper2D+ Actions"),
			LOCTEXT("Paper2DPlusActionsTooltip", "Paper2D+ Flipbook authoring tools"),
			FNewToolMenuDelegate::CreateLambda([](UToolMenu* SubMenu)
		{
			UContentBrowserAssetContextMenuContext* Context = SubMenu->FindContext<UContentBrowserAssetContextMenuContext>();
			if (!Context) return;

			FToolMenuSection& SubSection = SubMenu->FindOrAddSection("Default");

			SubSection.AddMenuEntry(
				"AddToCharacterProfileAsset",
				LOCTEXT("AddToCharacterProfile", "Add to Character Profile Asset..."),
				LOCTEXT("AddToCharacterProfileTooltip", "Add the selected flipbook(s) as new animations on a Character Profile Asset"),
				FSlateIcon(Paper2DPlusEditorIcons::StyleSet, Paper2DPlusEditorIcons::MenuAddToCharacterProfile),
				FUIAction(FExecuteAction::CreateLambda([Context]()
				{
					// Load selected flipbooks
					TArray<UPaperFlipbook*> Flipbooks;
					Flipbooks = Context->LoadSelectedObjects<UPaperFlipbook>();
					if (Flipbooks.IsEmpty()) return;

					// Create asset picker window
					TSharedRef<SWindow> PickerWindow = SNew(SWindow)
						.Title(LOCTEXT("PickCharacterProfileTitle", "Select Character Profile Asset"))
						.ClientSize(FVector2D(400, 120))
						.SupportsMinimize(false)
						.SupportsMaximize(false);

					TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> SelectedAsset;

					PickerWindow->SetContent(
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.Padding(8)
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::Format(LOCTEXT("PickerPrompt", "Add {0} flipbook(s) to:"), FText::AsNumber(Flipbooks.Num())))
						]
						+ SVerticalBox::Slot()
						.Padding(8, 0, 8, 8)
						.AutoHeight()
						[
							SNew(SObjectPropertyEntryBox)
							.AllowedClass(UPaper2DPlusCharacterProfileAsset::StaticClass())
							.OnObjectChanged_Lambda([&SelectedAsset](const FAssetData& AssetData)
							{
								SelectedAsset = Cast<UPaper2DPlusCharacterProfileAsset>(AssetData.GetAsset());
							})
						]
						+ SVerticalBox::Slot()
						.Padding(8, 0, 8, 8)
						.AutoHeight()
						.HAlign(HAlign_Right)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
							.Text(LOCTEXT("AddBtn", "Add"))
							.IsEnabled_Lambda([&SelectedAsset]() { return SelectedAsset.IsValid(); })
							.OnClicked_Lambda([&SelectedAsset, &Flipbooks, &PickerWindow]() -> FReply
							{
								if (!SelectedAsset.IsValid()) return FReply::Handled();

								UPaper2DPlusCharacterProfileAsset* Asset = SelectedAsset.Get();
								FScopedTransaction Transaction(LOCTEXT("AddFlipbooksToCDA", "Add Flipbooks to Character Profile Asset"));
								Asset->Modify();

								int32 AddedCount = 0;
								for (UPaperFlipbook* FB : Flipbooks)
								{
									if (!FB) continue;

									// Check for duplicate
									TSoftObjectPtr<UPaperFlipbook> SoftRef(FB);
									bool bAlreadyExists = false;
									for (const FFlipbookProfileEntry& Existing : Asset->Flipbooks)
									{
										if (Existing.Identity.Flipbook == SoftRef)
										{
											bAlreadyExists = true;
											UE_LOG(LogPaper2DPlusEditor, Log, TEXT("Skipping duplicate flipbook: %s"), *FB->GetName());
											break;
										}
									}
									if (bAlreadyExists) continue;

									// Generate unique name
									FString BaseName = FB->GetName();
									FString AnimName = BaseName;
									int32 Suffix = 2;
									while (Asset->FindFlipbookDataPtr(AnimName) != nullptr)
									{
										AnimName = FString::Printf(TEXT("%s (%d)"), *BaseName, Suffix++);
									}

									FFlipbookProfileEntry NewAnim;
									NewAnim.Identity.FlipbookName = AnimName;
									NewAnim.Identity.Flipbook = SoftRef;

									int32 NewIndex = Asset->Flipbooks.Add(NewAnim);
									Asset->SyncFramesToFlipbook(NewIndex);
									AddedCount++;
								}

								Asset->MarkPackageDirty();
								PickerWindow->RequestDestroyWindow();

								if (AddedCount > 0)
								{
									FNotificationInfo Info(FText::Format(
										LOCTEXT("AddedFlipbooks", "Added {0} animation(s) to {1}"),
										FText::AsNumber(AddedCount),
										FText::FromString(Asset->GetName())
									));
									Info.ExpireDuration = 3.0f;
									FSlateNotificationManager::Get().AddNotification(Info);
								}

								return FReply::Handled();
							})
						]
					);

					FSlateApplication::Get().AddModalWindow(PickerWindow, FSlateApplication::Get().GetActiveTopLevelWindow());
				}))
			);

			// Open the selected flipbook(s) in the Paper2D+ pixel draw editor (paint on frames).
			SubSection.AddMenuEntry(
				"EditFramesPaper2DDraw",
				LOCTEXT("EditFramesDraw", "Edit Frames (Paper2D+ Draw)..."),
				LOCTEXT("EditFramesDrawTooltip", "Open the selected flipbook in the Paper2D+ pixel draw editor to paint on its frames"),
				FSlateIcon(Paper2DPlusEditorIcons::StyleSet, Paper2DPlusEditorIcons::MenuFlipbookDraw),
				FUIAction(FExecuteAction::CreateLambda([Context]()
				{
					TArray<UPaperFlipbook*> Flipbooks = Context->LoadSelectedObjects<UPaperFlipbook>();
					for (UPaperFlipbook* FB : Flipbooks)
					{
						if (!FB) continue;
						TSharedRef<FFlipbookDrawEditorToolkit> Toolkit = MakeShared<FFlipbookDrawEditorToolkit>();
						Toolkit->InitEditor(EToolkitMode::Standalone, nullptr, FB);
					}
				}))
			);
		}),
			false,
			FSlateIcon(Paper2DPlusEditorIcons::StyleSet, Paper2DPlusEditorIcons::MenuPaper2DPlusActions));
	}));
#endif // ENGINE_MINOR_VERSION >= 1
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
void FPaper2DPlusEditorModule::RegisterDataValidators()
{
	if (!GEditor)
	{
		return;
	}

	UEditorValidatorSubsystem* ValidatorSubsystem = GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>();
	if (!ValidatorSubsystem)
	{
		return;
	}

	if (!RegisteredCharacterProfileValidator)
	{
		RegisteredCharacterProfileValidator = NewObject<UPaper2DPlusCharacterProfileAssetValidator>(GetTransientPackage());
	}

	ValidatorSubsystem->AddValidator(RegisteredCharacterProfileValidator);
}

void FPaper2DPlusEditorModule::UnregisterDataValidators()
{
	if (!RegisteredCharacterProfileValidator || !GEditor)
	{
		RegisteredCharacterProfileValidator = nullptr;
		return;
	}

	if (UEditorValidatorSubsystem* ValidatorSubsystem = GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>())
	{
		// Note: the entire register/unregister pair is already gated by the outer
		// UE 5.4+ #if, so RemoveValidator is always available here. (PR #98 review #4
		// flagged this as a UE 5.0 leak but the outer guard already excludes 5.0 —
		// inner 5.1+ guard was redundant and has been removed for clarity.)
		ValidatorSubsystem->RemoveValidator(RegisteredCharacterProfileValidator);
	}

	RegisteredCharacterProfileValidator = nullptr;
}
#endif // UE 5.4+

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPaper2DPlusEditorModule, Paper2DPlusEditor)
