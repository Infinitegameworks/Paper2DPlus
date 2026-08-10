// Copyright 2026 Infinite Gameworks. All Rights Reserved.
//
// Paper2DPlusVisualTour.cpp
//
// `Paper2DPlus.VisualTour [outDir] [NoMap]` is the render-capable designer-workflow tour used where
// ECABridge is unavailable. It synthesizes one fully related TRANSIENT Character/Profile/Layer/Effect/
// Combat/Catalog fixture plus four empty first-run siblings, opens the ACTUAL asset editors, and captures
// their primary designer surfaces. Character, Layer, Effect, Combat, and Catalog all run at normal, narrow,
// and wide window sizes so responsive regressions cannot hide behind one desktop layout. No project Content
// package is created, registered, dirtied, or saved.
//
// The Animations Map is enabled by default, but the tour deliberately never reveals a previously-hidden Map
// widget programmatically. The fixture owns two transient copies of the same Character data: the primary copy
// is seeded to List and the second to Map before either editor opens. Switching assets therefore gives Map its
// normal Construct/first-paint path without the known headless hidden->visible focus re-entry or a timing-
// fragile close/reopen of one asset. `NoMap` remains an emergency diagnostic switch, not the release gate.
// Run it per engine via `scripts/run-paper2dplus-visual-tour.ps1` (each engine launches the host editor with
// -RenderOffScreen and a GUID-isolated output directory), then eyeball the PNGs side by side.
//
// Design notes:
//  - Headless-safe: no-ops under !FApp::CanEverRender() (screenshots need a real RHI). The automation
//    suite runs with -nullrhi, so the command simply logs-and-returns there; it never crashes a sweep.
//  - The tour steps from an FTSTicker (switch tab -> let Slate paint a few frames -> capture -> next), never
//    a blocking loop. Capture (ForceRedrawWindow + TakeScreenshot) runs there because the core ticker fires
//    in the engine main loop, OUTSIDE Slate's widget tick (capturing from inside an active timer would be a
//    reentrant render). A do-nothing keep-awake ACTIVE TIMER on the editor window prevents the unattended
//    editor from parking its main loop once idle — without it the FTSTicker stalls and the tour freezes
//    mid-capture. A short warmup before opening the editor avoids a first-frame deadlock when -ExecCmds fires
//    during early init.
//  - An unattended-only worker-thread watchdog observes core-ticker heartbeats. If the game thread makes no
//    progress for 45 wall-clock seconds, it writes a FAIL manifest and force-exits with a non-zero status. A
//    synchronous Slate/graph loop can stall both the main ticker and every widget active timer, so a second
//    game-thread timer would not be a watchdog.
//  - Under -unattended (the harness) it RequestExit()s when done so the launcher returns; an interactive
//    run leaves the editor open. Either path closes every tour editor, clears its transient INI selections,
//    and unroots every fixture object. `00-visual-tour-manifest.txt` is PASS only when every requested PNG
//    was captured AND its live workspace passed semantic structure checks, so automation cannot confuse a
//    generic or structurally regressed screenshot with visual coverage.
//  - Screenshot path mirrors ECABridge's proven cross-version recipe: FSlateApplication::TakeScreenshot ->
//    FImageUtils::PNGCompressImageArray -> FFileHelper::SaveArrayToFile (avoids the 5.8-only FImageView APIs).

#include "CoreMinimal.h"
#include "Async/Async.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Crc.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EngineVersion.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Children.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "ImageUtils.h"

#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/IToolkitHost.h"

#include "PaperFlipbook.h"
#include "PaperSprite.h"

#include "AsepriteImporter.h"
#include "CharacterProfileAssetEditor.h"   // FCharacterProfileAssetEditorToolkit tab-id constants
#include "CharacterProfileEditorModel.h"   // FCharacterProfileEditorModel::GActiveEditorModel
#include "AnimationsPanel.h"               // SAnimationsPanel + EAnimationsViewMode
#include "AnimationMapPanel.h"
#include "FrameEventEditor.h"
#include "SFrameEventTimelineTrack.h"
#include "FrameTimingEditor.h"
#include "HitboxEditorPanel.h"
#include "RootMotionEditor.h"
#include "SpriteEditorPanel.h"
#include "CharacterLayerAssetEditorToolkit.h"
#include "EffectProfileAssetEditorToolkit.h"
#include "EffectProfileEditor/EffectProfileDetailsPanel.h"
#include "EffectProfileEditor/EffectProfileLibraryPanel.h"
#include "EffectProfileEditor/EffectProfilePreviewPanel.h"
#include "CombatProfileAssetEditorToolkit.h"
#include "CombatProfileEditor/CombatLabPanel.h"
#include "CombatProfileEditor/CombatProfileSetupPanel.h"
#include "CombatProfileEditor/CombatScorePlaygroundPanel.h"
#include "CharacterCatalogAssetEditorToolkit.h"
#include "CharacterCatalogDetailsPanel.h"
#include "CharacterCatalogRosterPanel.h"

#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusEffectTags.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusAnimationTags.h"
#include "Paper2DPlusCueTags.h"
#include "Paper2DPlusTypes.h"
#include "Paper2DPlusFrameCurve.h"
#include "Paper2DPlusMoveTransition.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewBehavior.h"
#include "Paper2DPlusVisualTourFixtureCue.h"
#include "GameplayTagContainer.h"

namespace
{
	const TCHAR* Tour_AnimationsConfigSection = TEXT("Paper2DPlus.AnimationsTab");

	FString Tour_AnimationsViewModeKey(const UObject* Asset)
	{
		FString AssetKey = Asset ? Asset->GetPathName() : TEXT("NoAsset");
		AssetKey.ReplaceInline(TEXT("/"), TEXT("_"));
		AssetKey.ReplaceInline(TEXT("."), TEXT("_"));
		AssetKey.ReplaceInline(TEXT(":"), TEXT("_"));
		AssetKey.ReplaceInline(TEXT(" "), TEXT("_"));
		return FString::Printf(TEXT("ViewMode_%s"), *AssetKey);
	}

	void Tour_SetAnimationsViewMode(const UObject* Asset, int32 ViewMode)
	{
		if (GConfig && Asset && (ViewMode == 0 || ViewMode == 1))
		{
			GConfig->SetString(
				Tour_AnimationsConfigSection,
				*Tour_AnimationsViewModeKey(Asset),
				ViewMode == 1 ? TEXT("Map") : TEXT("List"),
				GEditorPerProjectIni);
		}
	}

	// ---------------------------------------------------------------------------------------------
	// Demo-asset construction — a transient profile rich enough that every tab renders something.
	// ---------------------------------------------------------------------------------------------

	/** Build a flipbook whose sprites have REAL BGRA8 source pixels + UpdateResource() (a bare
	 *  UTexture2D::CreateTransient leaves mip0 uninitialized and draws blank). Reuses the editor
	 *  importer pair the Flipbook Draw tests use; materialises into a throwaway /Temp package. */
	UPaperFlipbook* Tour_MakeDrawableFlipbook(
		UObject* Outer,
		int32 NumFrames,
		FColor BaseColor = FColor(40, 120, 220, 255),
		int32 W = 48,
		int32 H = 48)
	{
		const FString Dir = FString::Printf(TEXT("/Temp/P2DPTour_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		TArray<TArray<FColor>> Frames;
		Frames.Reserve(NumFrames);
		for (int32 f = 0; f < NumFrames; ++f)
		{
			TArray<FColor> Px;
			Px.SetNum(W * H);
			const FColor Body(
				(uint8)FMath::Clamp((int32)BaseColor.R + f * 18, 0, 255),
				(uint8)FMath::Clamp((int32)BaseColor.G + f * 8, 0, 255),
				(uint8)FMath::Clamp((int32)BaseColor.B - f * 12, 0, 255),
				BaseColor.A);
			for (int32 y = 0; y < H; ++y)
			{
				for (int32 x = 0; x < W; ++x)
				{
					const bool bInside = (x > W / 4 && x < 3 * W / 4 && y > H / 6 && y < 5 * H / 6);
					Px[y * W + x] = bInside ? Body : FColor(0, 0, 0, 0);
				}
			}
			Frames.Add(MoveTemp(Px));
		}

		UTexture2D* Tex = FAsepriteImporter::CreatePerLayerSpriteSheetTexture(Frames, W, H, Dir, TEXT("TourSheet"));
		if (!Tex)
		{
			return nullptr;
		}
		TArray<UPaperSprite*> Sprites = FAsepriteImporter::CreateSpritesFromSheet(Tex, W, H, NumFrames, Dir, TEXT("TourSprite"));

		UPaperFlipbook* FB = NewObject<UPaperFlipbook>(Outer); // outered to the asset: soft-ref resolves + GC-safe together
		FScopedFlipbookMutator Mut(FB);
		Mut.FramesPerSecond = 12.f;
		Mut.KeyFrames.Empty();
		for (int32 f = 0; f < NumFrames; ++f)
		{
			FPaperFlipbookKeyFrame KF;
			KF.Sprite = Sprites.IsValidIndex(f) ? Sprites[f] : nullptr;
			KF.FrameRun = 1 + (f % 3); // non-uniform holds so the Frame Timing tab is interesting
			Mut.KeyFrames.Add(KF);
		}
		return FB;
	}

	/** Add one fully-populated move: drawable flipbook + per-frame hitboxes/sockets, sprite offsets,
	 *  a root-motion path, fixture Cue/Cue State, and HitStop (linear) + Cancel_Normal (step) curves. */
	int32 Tour_AddMove(UPaper2DPlusCharacterProfileAsset* Asset, const FString& Name, int32 N, FName Group = NAME_None)
	{
		FFlipbookProfileEntry E;
		E.Identity.FlipbookName = Name;
		E.Identity.Flipbook = Tour_MakeDrawableFlipbook(Asset, N); // TSoftObjectPtr accepts the raw ptr
		E.FlipbookGroup = Group;
		E.CombatData.Frames.SetNum(N);
		E.CombatData.FrameExtractionInfo.SetNum(N);
		E.MotionData.RootMotion.SetNum(N);
		for (int32 f = 0; f < N; ++f)
		{
			E.CombatData.Frames[f].FrameName = FString::Printf(TEXT("%s_%02d"), *Name, f);
			FHitboxData HB;
			HB.Type = (f % 2 == 0) ? EHitboxType::Attack : EHitboxType::Hurtbox;
			HB.X = 12 + f;
			HB.Y = 8;
			HB.Width = 20;
			HB.Height = 24;
			E.CombatData.Frames[f].Hitboxes.Add(HB);

			FSocketData Sock;
			Sock.Name = TEXT("Hand");
			Sock.X = 30;
			Sock.Y = 16;
			E.CombatData.Frames[f].Sockets.Add(Sock);

			E.CombatData.FrameExtractionInfo[f].SpriteOffset = FIntPoint(f * 2, 0);
			E.MotionData.RootMotion[f].Position = FVector2D(f * 8.0, FMath::Sin((double)f) * 4.0);
		}

		// Screenshot fixture only -- see Paper2DPlusVisualTourFixtureCue.h. The Frame Cues tab's readiness
		// gate requires a placed Cue, and the plugin ships no concrete Cue Type for the tour to place.
		UPaper2DPlusVisualTourFixtureCue* Cue =
			NewObject<UPaper2DPlusVisualTourFixtureCue>(Asset, NAME_None, RF_Transactional);
		Cue->TriggerFrame = FMath::Min(2, N - 1);
		Cue->DebugName = TEXT("Impact Cue");
		E.FrameEventData.FrameCues.Add(Cue);
		UPaper2DPlusVisualTourFixtureCueState* CueState =
			NewObject<UPaper2DPlusVisualTourFixtureCueState>(
				Asset, NAME_None, RF_Transactional);
		CueState->StartFrame = FMath::Min(3, N - 1);
		CueState->FrameCount = FMath::Min(2, N - CueState->StartFrame);
		CueState->DebugName = TEXT("Guard Cue State");
		E.FrameEventData.FrameCues.Add(CueState);

		FPaper2DPlusFrameCurve& HitStop = E.CurveData.Curves.Add(TEXT("HitStop"));
		HitStop.Mode = EPaper2DPlusCurveInterp::Linear;
		HitStop.SetKeyValue(0, 0.f);
		HitStop.SetKeyValue(N - 1, 6.f);

		FPaper2DPlusFrameCurve& Cancel = E.CurveData.Curves.Add(TEXT("Cancel_Normal"));
		Cancel.Mode = EPaper2DPlusCurveInterp::Constant;
		Cancel.SetKeyValue(0, 0.f);
		Cancel.SetKeyValue(FMath::Max(1, N / 2), 1.f);

		return Asset->Flipbooks.Add(E);
	}

	enum class ETourAsset : uint8
	{
		Character,
		CharacterMap,
		Layer,
		Effect,
		Combat,
		Catalog,
		EmptyLayer,
		EmptyEffect,
		EmptyCombat,
		EmptyCatalog
	};

	/** Assemble the rich profile used by Character, Layer, Combat, Effect-Cue, and Catalog screens. */
	UPaper2DPlusCharacterProfileAsset* Tour_BuildProfile(
		const FString& Token,
		const FString& ObjectStem,
		const FString& DisplayName,
		bool bWithAnimations)
	{
		UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>(
			GetTransientPackage(),
			FName(*(ObjectStem + TEXT("_") + Token)),
			RF_Transient | RF_Transactional);
		if (!Asset)
		{
			return nullptr;
		}
		Asset->DisplayName = DisplayName;
		if (!bWithAnimations)
		{
			return Asset;
		}

		FFlipbookGroupInfo Attacks;
		Attacks.GroupName = TEXT("Attacks");
		Attacks.Color = FLinearColor(0.8f, 0.3f, 0.3f, 1.f);
		Asset->FlipbookGroups.Add(Attacks);
		FFlipbookGroupInfo Movement;
		Movement.GroupName = TEXT("Movement");
		Movement.Color = FLinearColor(0.25f, 0.55f, 0.9f, 1.f);
		Asset->FlipbookGroups.Add(Movement);

		Tour_AddMove(Asset, TEXT("Jab"), 5, TEXT("Attacks"));
		Tour_AddMove(Asset, TEXT("Jab2"), 6, TEXT("Attacks"));
		Tour_AddMove(Asset, TEXT("Dash"), 5, TEXT("Movement"));
		Tour_AddMove(Asset, TEXT("Idle"), 4);

		// Pure From->To arrows and deterministic positions give the Map a real combo plus a movement branch.
		Asset->Flipbooks[0].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Jab2")));
		Asset->Flipbooks[1].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Dash")));
		Asset->AnimationMapNodePositions.Add(TEXT("jab"), FVector2D(0, 0));
		Asset->AnimationMapNodePositions.Add(TEXT("jab2"), FVector2D(300, 0));
		Asset->AnimationMapNodePositions.Add(TEXT("dash"), FVector2D(600, 0));
		Asset->AnimationMapNodePositions.Add(TEXT("idle"), FVector2D(0, 220));

		// Tag mapping -> List chips and a real Map group board (when the native tag is registered).
		const FGameplayTag AnimTag = Paper2DPlusAnimationTags::Combat_Combo;
		if (AnimTag.IsValid())
		{
			FFlipbookTagMapping Mapping;
			for (const TCHAR* Move : { TEXT("Jab"), TEXT("Jab2") })
			{
				FFlipbookTagMappingEntry& Entry = Mapping.Entries.AddDefaulted_GetRef();
				Entry.FlipbookName = Move;
			}
			Asset->TagMappings.Add(AnimTag, Mapping);
		}

		const FGameplayTag PhaseTag = FGameplayTag::RequestGameplayTag(
			FName("Paper2DPlus.Phase.Startup"), /*ErrorIfNotFound*/ false);
		if (PhaseTag.IsValid())
		{
			Asset->Flipbooks[0].EditorMeta.PhaseTag = PhaseTag;
		}
		return Asset;
	}

	UPaper2DPlusEffectProfileAsset* Tour_BuildEffectProfile(
		const FString& Token,
		TArray<UPaperFlipbook*>& OutFlipbooks)
	{
		UPaper2DPlusEffectProfileAsset* Asset = NewObject<UPaper2DPlusEffectProfileAsset>(
			GetTransientPackage(),
			FName(*(TEXT("VisualTourEffects_") + Token)),
			RF_Transient | RF_Transactional);
		if (!Asset)
		{
			return nullptr;
		}
		Asset->DisplayName = TEXT("Guide Hero Effects");
		Asset->EffectLibrarySchemaVersion = UPaper2DPlusEffectProfileAsset::CurrentEffectLibrarySchemaVersion;

		struct FEffectSeed
		{
			const TCHAR* Label;
			FColor Color;
			FGameplayTag Type;
			FGameplayTag Descriptor;
		};
		const FEffectSeed Seeds[] = {
			{ TEXT("Electric Impact"), FColor(80, 170, 255), Paper2DPlusEffectTags::Type_Impact, Paper2DPlusEffectTags::Descriptor_Electric },
			{ TEXT("Fire Trail"), FColor(255, 95, 35), Paper2DPlusEffectTags::Type_Trail, Paper2DPlusEffectTags::Descriptor_Fire },
			{ TEXT("Healing Aura"), FColor(70, 235, 125), Paper2DPlusEffectTags::Type_Aura, Paper2DPlusEffectTags::Descriptor_Healing }
		};
		for (const FEffectSeed& Seed : Seeds)
		{
			UPaperFlipbook* Flipbook = Tour_MakeDrawableFlipbook(Asset, 5, Seed.Color, 56, 56);
			OutFlipbooks.Add(Flipbook);
			FPaper2DPlusEffectProfileEntry& Entry = Asset->Effects.AddDefaulted_GetRef();
			Entry.EffectFlipbook = Flipbook;
			Entry.DisplayLabel = FText::FromString(Seed.Label);
			Entry.TypeTag = Seed.Type;
			Entry.DescriptorTags.AddTag(Seed.Descriptor);
		}
		return Asset;
	}

	void Tour_AddLayerMapping(
		FCharacterLayer& Layer,
		UPaperFlipbook* CanonicalFlipbook,
		UPaperFlipbook* ArtSource,
		bool bWithGameplay,
		UPaper2DPlusCharacterLayerAsset* Owner)
	{
		FCharacterLayerAnimationMapping& Mapping = Layer.AnimationSprites.AddDefaulted_GetRef();
		Mapping.AnimationName = TEXT("Jab");
#if WITH_EDITORONLY_DATA
		Mapping.Flipbook = CanonicalFlipbook;
#endif
		if (ArtSource)
		{
			for (int32 Index = 0; Index < ArtSource->GetNumKeyFrames(); ++Index)
			{
				UPaperSprite* Sprite = ArtSource->GetKeyFrameChecked(Index).Sprite;
				Mapping.Sprites.Add(Sprite);
				if (Index == 0 && Sprite)
				{
					Layer.SourceTexture = Sprite->GetSourceTexture();
				}
			}
		}
#if WITH_EDITORONLY_DATA
		if (bWithGameplay)
		{
			FCharacterLayerAuthoredAnimationData& Authored = Layer.AuthoredAnimations.AddDefaulted_GetRef();
			Authored.Flipbook = CanonicalFlipbook;
			Authored.LegacyAnimationName = TEXT("Jab");
			Authored.Frames.SetNum(CanonicalFlipbook ? CanonicalFlipbook->GetNumKeyFrames() : 0);
			for (int32 Frame = 0; Frame < Authored.Frames.Num(); ++Frame)
			{
				FHitboxData Box;
				Box.Type = EHitboxType::Attack;
				Box.X = 22 + Frame * 2;
				Box.Y = 12;
				Box.Width = 22;
				Box.Height = 12;
				Authored.Frames[Frame].AttackBoxes.Add(Box);
			}
			UPaper2DPlusVisualTourFixtureCue* Cue = NewObject<UPaper2DPlusVisualTourFixtureCue>(
				Owner, NAME_None, RF_Transient | RF_Transactional);
			Cue->TriggerFrame = 2;
			Authored.FrameCues.Add(Cue);
		}
#endif
	}

	UPaper2DPlusCharacterLayerAsset* Tour_BuildLayerProfile(
		const FString& Token,
		UPaper2DPlusCharacterProfileAsset* Character,
		const TArray<UPaperFlipbook*>& ArtSources)
	{
		UPaper2DPlusCharacterLayerAsset* Asset = NewObject<UPaper2DPlusCharacterLayerAsset>(
			GetTransientPackage(),
			FName(*(TEXT("VisualTourLayers_") + Token)),
			RF_Transient | RF_Transactional);
		if (!Asset || !Character || Character->Flipbooks.IsEmpty())
		{
			return Asset;
		}
		Asset->BaseProfile = Character;
		Asset->LayerSchemaVersion = UPaper2DPlusCharacterLayerAsset::CurrentLayerSchemaVersion;
		Asset->UsageMode = ECharacterLayerUsageMode::RuntimeCustomizable;

		FCharacterLayerGroupInfo& BodyGroup = Asset->LayerGroups.AddDefaulted_GetRef();
		BodyGroup.GroupId = FGuid::NewGuid();
		BodyGroup.DisplayName = FText::FromString(TEXT("Body & Hair"));
		FCharacterLayerGroupInfo& GearGroup = Asset->LayerGroups.AddDefaulted_GetRef();
		GearGroup.GroupId = FGuid::NewGuid();
		GearGroup.DisplayName = FText::FromString(TEXT("Equipment & FX"));
		FCharacterLayerExclusiveGroup& HairStyles = Asset->ExclusiveGroups.AddDefaulted_GetRef();
		HairStyles.GroupId = FGuid::NewGuid();
		HairStyles.DisplayName = TEXT("Hair Style");

		UPaperFlipbook* Canonical = Character->Flipbooks[0].Identity.Flipbook.Get();
		struct FLayerSeed
		{
			const TCHAR* Name;
			FGuid Group;
			int32 ArtIndex;
			bool bGameplay;
		};
		const FLayerSeed Seeds[] = {
			{ TEXT("Body Base"), BodyGroup.GroupId, 2, false },
			{ TEXT("Hair Short"), BodyGroup.GroupId, 1, false },
			{ TEXT("Hair Long"), BodyGroup.GroupId, 0, false },
			{ TEXT("Sword"), GearGroup.GroupId, 0, true },
			{ TEXT("Electric Trail"), GearGroup.GroupId, 1, false }
		};
		for (const FLayerSeed& Seed : Seeds)
		{
			FCharacterLayer& Layer = Asset->Layers.AddDefaulted_GetRef();
#if WITH_EDITORONLY_DATA
			Layer.LayerId = FGuid::NewGuid();
			Layer.GroupId = Seed.Group;
#endif
			Layer.LayerName = Seed.Name;
			if (Layer.LayerName.StartsWith(TEXT("Hair ")))
			{
				Layer.ExclusiveGroupId = HairStyles.GroupId;
			}
			Layer.RuntimeRenderChannel = FCString::Strcmp(Seed.Name, TEXT("Sword")) == 0
				? ECharacterLayerRuntimeRenderChannel::IndependentLive
				: ECharacterLayerRuntimeRenderChannel::BaseComposite;
			Tour_AddLayerMapping(
				Layer,
				Canonical,
				ArtSources.IsValidIndex(Seed.ArtIndex) ? ArtSources[Seed.ArtIndex] : nullptr,
				Seed.bGameplay,
				Asset);
		}

		FCharacterLayerAppearancePreset& DefaultPreset = Asset->AppearancePresets.AddDefaulted_GetRef();
		DefaultPreset.PresetId = FGuid::NewGuid();
		DefaultPreset.DisplayName = TEXT("Default");
		for (const FCharacterLayer& Layer : Asset->Layers)
		{
			if (!Layer.LayerName.Equals(TEXT("Hair Long"), ESearchCase::CaseSensitive))
			{
				DefaultPreset.ActiveLayerIds.Add(Layer.LayerId);
			}
		}
		Asset->DefaultAppearancePresetId = DefaultPreset.PresetId;
		return Asset;
	}

	UPaper2DPlusCombatProfileAsset* Tour_BuildCombatProfile(
		const FString& Token,
		UPaper2DPlusCharacterProfileAsset* Character)
	{
		UPaper2DPlusCombatProfileAsset* Asset = NewObject<UPaper2DPlusCombatProfileAsset>(
			GetTransientPackage(),
			FName(*(TEXT("VisualTourCombat_") + Token)),
			RF_Transient | RF_Transactional);
		if (!Asset)
		{
			return nullptr;
		}
		Asset->CharacterProfile = Character;
		for (int32 Index = 0; Character && Index < FMath::Min(3, Character->Flipbooks.Num()); ++Index)
		{
			FPaper2DPlusCombatAttackOption& Option = Asset->AttackOptions.AddDefaulted_GetRef();
			Option.MoveName = FName(*Character->Flipbooks[Index].Identity.FlipbookName);
			Option.MoveFlipbook = Character->Flipbooks[Index].Identity.Flipbook;
			Option.bIncludeWhenNotTagged = true;
			Option.BaseWeight = 1.4f - Index * 0.25f;
			Option.CooldownSeconds = Index * 0.15f;
			Option.bOverridePreferredRange = true;
			Option.PreferredRangeLocal = FVector2D(20.0f + Index * 35.0f, 80.0f + Index * 55.0f);
		}
		FPaper2DPlusCombatScoringProfile& Scoring = Asset->ScoringProfiles.AddDefaulted_GetRef();
		Scoring.ProfileName = TEXT("Default");
		Scoring.MinimumViableScore = 0.05f;
		FPaper2DPlusCombatScenarioPreset& Close = Asset->ScenarioPresets.AddDefaulted_GetRef();
		Close.PresetName = TEXT("Close Pressure");
		Close.Context.DistanceToTarget = 45.0f;
		Close.AttackerMove = TEXT("Jab");
		Close.DefenderMove = TEXT("Jab2");
		FPaper2DPlusCombatScenarioPreset& Far = Asset->ScenarioPresets.AddDefaulted_GetRef();
		Far.PresetName = TEXT("Far Approach");
		Far.Context.DistanceToTarget = 160.0f;
		Far.AttackerMove = TEXT("Dash");
		Far.DefenderMove = TEXT("Idle");
		return Asset;
	}

	/**
	 * The tour deliberately authors non-zero extraction offsets so the Sprite workspace has meaningful
	 * content. Programmatic asset switching must not open the real designer close-confirmation dialog,
	 * though: unattended mode answers "No", leaves the old editor alive, and two profile editors then
	 * contend for the process-scoped active panel/model seams. Suppress only while CloseAllEditorsForAsset
	 * runs, then restore the transient fixture byte-for-byte for later screenshots.
	 */
	class FScopedTourSpriteOffsetCloseSuppression
	{
	public:
		explicit FScopedTourSpriteOffsetCloseSuppression(
			UPaper2DPlusCharacterProfileAsset* InProfile)
			: Profile(InProfile)
		{
			if (!Profile)
			{
				return;
			}
			SavedOffsets.SetNum(Profile->Flipbooks.Num());
			for (int32 AnimationIndex = 0; AnimationIndex < Profile->Flipbooks.Num(); ++AnimationIndex)
			{
				TArray<FSpriteExtractionInfo>& Infos =
					Profile->Flipbooks[AnimationIndex].CombatData.FrameExtractionInfo;
				TArray<FIntPoint>& AnimationOffsets = SavedOffsets[AnimationIndex];
				AnimationOffsets.Reserve(Infos.Num());
				for (FSpriteExtractionInfo& Info : Infos)
				{
					AnimationOffsets.Add(Info.SpriteOffset);
					Info.SpriteOffset = FIntPoint::ZeroValue;
				}
			}
		}

		~FScopedTourSpriteOffsetCloseSuppression()
		{
			if (!Profile)
			{
				return;
			}
			for (int32 AnimationIndex = 0;
				AnimationIndex < Profile->Flipbooks.Num() && SavedOffsets.IsValidIndex(AnimationIndex);
				++AnimationIndex)
			{
				TArray<FSpriteExtractionInfo>& Infos =
					Profile->Flipbooks[AnimationIndex].CombatData.FrameExtractionInfo;
				const TArray<FIntPoint>& AnimationOffsets = SavedOffsets[AnimationIndex];
				for (int32 FrameIndex = 0;
					FrameIndex < Infos.Num() && AnimationOffsets.IsValidIndex(FrameIndex);
					++FrameIndex)
				{
					Infos[FrameIndex].SpriteOffset = AnimationOffsets[FrameIndex];
				}
			}
		}

	private:
		UPaper2DPlusCharacterProfileAsset* Profile = nullptr;
		TArray<TArray<FIntPoint>> SavedOffsets;
	};

	struct FVisualTourFixture
	{
		struct FConfigValueSnapshot
		{
			FString Section;
			FString Key;
			FString Value;
			bool bWasPresent = false;
		};

		UPaper2DPlusCharacterProfileAsset* Character = nullptr;
		UPaper2DPlusCharacterProfileAsset* MapCharacter = nullptr;
		UPaper2DPlusCharacterLayerAsset* Layer = nullptr;
		UPaper2DPlusEffectProfileAsset* Effect = nullptr;
		UPaper2DPlusCombatProfileAsset* Combat = nullptr;
		UPaper2DPlusCharacterCatalogAsset* Catalog = nullptr;
		UPaper2DPlusCharacterLayerAsset* EmptyLayer = nullptr;
		UPaper2DPlusEffectProfileAsset* EmptyEffect = nullptr;
		UPaper2DPlusCombatProfileAsset* EmptyCombat = nullptr;
		UPaper2DPlusCharacterCatalogAsset* EmptyCatalog = nullptr;
		TArray<UPaper2DPlusCharacterProfileAsset*> CatalogCharacters;
		TArray<UObject*> RootedObjects;
		FString EffectSelectionConfigSection;
		TArray<FConfigValueSnapshot> PreferenceSnapshots;

		void OverridePreference(const TCHAR* Section, const TCHAR* Key, const TCHAR* CanonicalValue)
		{
			if (!GConfig)
			{
				return;
			}
			FConfigValueSnapshot& Snapshot = PreferenceSnapshots.AddDefaulted_GetRef();
			Snapshot.Section = Section;
			Snapshot.Key = Key;
			Snapshot.bWasPresent = GConfig->GetString(
				Section, Key, Snapshot.Value, GEditorPerProjectIni);
			GConfig->SetString(Section, Key, CanonicalValue, GEditorPerProjectIni);
		}

		bool RestorePreferences()
		{
			if (!GConfig)
			{
				const bool bNothingToRestore = PreferenceSnapshots.IsEmpty();
				PreferenceSnapshots.Reset();
				return bNothingToRestore;
			}
			bool bRestoredExactly = true;
			for (int32 Index = PreferenceSnapshots.Num() - 1; Index >= 0; --Index)
			{
				const FConfigValueSnapshot& Snapshot = PreferenceSnapshots[Index];
				if (Snapshot.bWasPresent)
				{
					GConfig->SetString(
						*Snapshot.Section, *Snapshot.Key, *Snapshot.Value, GEditorPerProjectIni);
				}
				else
				{
					GConfig->RemoveKey(*Snapshot.Section, *Snapshot.Key, GEditorPerProjectIni);
				}
				FString RestoredValue;
				const bool bIsPresent = GConfig->GetString(
					*Snapshot.Section, *Snapshot.Key, RestoredValue, GEditorPerProjectIni);
				bRestoredExactly &= bIsPresent == Snapshot.bWasPresent
					&& (!Snapshot.bWasPresent || RestoredValue == Snapshot.Value);
			}
			PreferenceSnapshots.Reset();
			return bRestoredExactly;
		}

		void Root(UObject* Object)
		{
			if (Object && !Object->IsRooted())
			{
				Object->AddToRoot();
				RootedObjects.Add(Object);
			}
		}

		UObject* GetAsset(ETourAsset Kind) const
		{
			switch (Kind)
			{
			case ETourAsset::Character: return Character;
			case ETourAsset::CharacterMap: return MapCharacter;
			case ETourAsset::Layer: return Layer;
			case ETourAsset::Effect: return Effect;
			case ETourAsset::Combat: return Combat;
			case ETourAsset::Catalog: return Catalog;
			case ETourAsset::EmptyLayer: return EmptyLayer;
			case ETourAsset::EmptyEffect: return EmptyEffect;
			case ETourAsset::EmptyCombat: return EmptyCombat;
			case ETourAsset::EmptyCatalog: return EmptyCatalog;
			default: return nullptr;
			}
		}

		bool Release(bool bReleaseRoots = true)
		{
			if (Character && Character->Flipbooks.IsValidIndex(0)
				&& Character->Flipbooks[0].FrameEventData.FrameCues.IsValidIndex(0))
			{
				if (const UPaper2DPlusCueBase* ImpactCue =
					Character->Flipbooks[0].FrameEventData.FrameCues[0])
				{
					Paper2DPlusFrameCuePreviewBehavior::
						ClearVisualTourPlacementErrorBadge(*ImpactCue);
				}
			}
			if (GConfig)
			{
				for (const UPaper2DPlusCharacterProfileAsset* Profile : { Character, MapCharacter })
				{
					if (Profile)
					{
						GConfig->RemoveKey(
							Tour_AnimationsConfigSection,
							*Tour_AnimationsViewModeKey(Profile),
							GEditorPerProjectIni);
					}
				}
			}
			if (GConfig && !EffectSelectionConfigSection.IsEmpty())
			{
				GConfig->EmptySection(*EffectSelectionConfigSection, GEditorPerProjectIni);
			}
			const bool bPreferencesRestored = RestorePreferences();
			if (bReleaseRoots)
			{
				for (UObject* Object : RootedObjects)
				{
					if (Object && Object->IsRooted()) Object->RemoveFromRoot();
				}
				RootedObjects.Reset();
			}
			return bPreferencesRestored;
		}
	};

	TSharedPtr<FVisualTourFixture> Tour_BuildFixture()
	{
		const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		TSharedRef<FVisualTourFixture> Fixture = MakeShared<FVisualTourFixture>();
		// These production preferences are intentionally global. Snapshot exact presence/value before any
		// tour editor opens, force deterministic capture settings, and restore only after editor destructors
		// have had their normal chance to save. The tour must never inherit or overwrite designer state.
		Fixture->OverridePreference(TEXT("CharacterProfileEditor"), TEXT("AnimationMapFilterTag"), TEXT(""));
		Fixture->OverridePreference(TEXT("CharacterProfileEditor"), TEXT("AnimationMapZoom"), TEXT("1.0"));
		Fixture->OverridePreference(TEXT("CharacterProfileEditor"), TEXT("AnimationMapViewX"), TEXT("0.0"));
		Fixture->OverridePreference(TEXT("CharacterProfileEditor"), TEXT("AnimationMapViewY"), TEXT("0.0"));
		Fixture->OverridePreference(
			TEXT("Paper2DPlus.CharacterLayerWorkspace"), TEXT("AnimationDrawerOpen"), TEXT("False"));
		Fixture->OverridePreference(
			TEXT("Paper2DPlus.CharacterLayerWorkspace"), TEXT("AnimationDrawerRatio"), TEXT("0.22"));
		auto RootProfileAnimations = [&Fixture](UPaper2DPlusCharacterProfileAsset* Profile)
		{
			if (!Profile)
			{
				return;
			}
			for (const FFlipbookProfileEntry& Entry : Profile->Flipbooks)
			{
				if (UPaperFlipbook* Flipbook = Entry.Identity.Flipbook.Get())
				{
					// A transient object's Outer is ownership identity, not a GC reference. Root the
					// tour flipbooks explicitly so their soft pointers still resolve in the later
					// Catalog screenshots after the Character/Layer/Combat steps have triggered GC.
					Fixture->Root(Flipbook);
				}
			}
		};
		Fixture->Character = Tour_BuildProfile(Token, TEXT("VisualTourCharacter"), TEXT("Guide Hero"), true);
		Fixture->Root(Fixture->Character);
		RootProfileAnimations(Fixture->Character);
		if (Fixture->Character)
		{
			Fixture->MapCharacter = DuplicateObject<UPaper2DPlusCharacterProfileAsset>(
				Fixture->Character,
				GetTransientPackage(),
				FName(*(TEXT("VisualTourMapCharacter_") + Token)));
			if (Fixture->MapCharacter)
			{
				Fixture->MapCharacter->SetFlags(RF_Transient | RF_Transactional);
				Fixture->Root(Fixture->MapCharacter);
				RootProfileAnimations(Fixture->MapCharacter);
			}
		}
		Tour_SetAnimationsViewMode(Fixture->Character, 0);
		Tour_SetAnimationsViewMode(Fixture->MapCharacter, 1);

		TArray<UPaperFlipbook*> EffectFlipbooks;
		Fixture->Effect = Tour_BuildEffectProfile(Token, EffectFlipbooks);
		Fixture->Root(Fixture->Effect);
		for (UPaperFlipbook* EffectFlipbook : EffectFlipbooks)
		{
			// The responsive matrix closes and later reopens the Effect editor. Its library identity is soft,
			// so retain the transient sources explicitly across intervening editor-close GC opportunities.
			Fixture->Root(EffectFlipbook);
		}
		Fixture->Layer = Tour_BuildLayerProfile(Token, Fixture->Character, EffectFlipbooks);
		Fixture->Root(Fixture->Layer);
		Fixture->Combat = Tour_BuildCombatProfile(Token, Fixture->Character);
		Fixture->Root(Fixture->Combat);

		// Four separate first-run assets keep the rich fixture stable while proving the real empty guidance.
		// They remain transient/unregistered, so opening their editors cannot create or dirty project Content.
		Fixture->EmptyLayer = NewObject<UPaper2DPlusCharacterLayerAsset>(
			GetTransientPackage(),
			FName(*(TEXT("VisualTourEmptyLayers_") + Token)),
			RF_Transient | RF_Transactional);
		if (Fixture->EmptyLayer)
		{
			Fixture->EmptyLayer->DisplayName = TEXT("New Character Layers");
			Fixture->EmptyLayer->LayerSchemaVersion =
				UPaper2DPlusCharacterLayerAsset::CurrentLayerSchemaVersion;
			Fixture->Root(Fixture->EmptyLayer);
		}
		Fixture->EmptyEffect = NewObject<UPaper2DPlusEffectProfileAsset>(
			GetTransientPackage(),
			FName(*(TEXT("VisualTourEmptyEffects_") + Token)),
			RF_Transient | RF_Transactional);
		if (Fixture->EmptyEffect)
		{
			Fixture->EmptyEffect->DisplayName = TEXT("New Effect Library");
			Fixture->EmptyEffect->EffectLibrarySchemaVersion =
				UPaper2DPlusEffectProfileAsset::CurrentEffectLibrarySchemaVersion;
			Fixture->Root(Fixture->EmptyEffect);
		}
		Fixture->EmptyCombat = NewObject<UPaper2DPlusCombatProfileAsset>(
			GetTransientPackage(),
			FName(*(TEXT("VisualTourEmptyCombat_") + Token)),
			RF_Transient | RF_Transactional);
		Fixture->Root(Fixture->EmptyCombat);
		Fixture->EmptyCatalog = NewObject<UPaper2DPlusCharacterCatalogAsset>(
			GetTransientPackage(),
			FName(*(TEXT("VisualTourEmptyCatalog_") + Token)),
			RF_Transient | RF_Transactional);
		Fixture->Root(Fixture->EmptyCatalog);

		// Enrich only the active move photographed by 06-character-frame-cues-normal. Keep Impact at
		// index 0 so its existing Effect assignment remains stable, Guard State at index 1, and append
		// one untouched-fresh-equivalent Cue to prove fallback identity and Default-tag color.
		if (Fixture->Character && !Fixture->Character->Flipbooks.IsEmpty()
			&& Fixture->Character->Flipbooks[0].FrameEventData.FrameCues.Num() == 2
			&& !EffectFlipbooks.IsEmpty())
		{
			TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues =
				Fixture->Character->Flipbooks[0].FrameEventData.FrameCues;
			UPaper2DPlusVisualTourFixtureCue* ImpactCue =
				Cast<UPaper2DPlusVisualTourFixtureCue>(Cues[0]);
			UPaper2DPlusVisualTourFixtureCueState* GuardCueState =
				Cast<UPaper2DPlusVisualTourFixtureCueState>(Cues[1]);
			if (ImpactCue && GuardCueState)
			{
				ImpactCue->DemoEffectFlipbook = EffectFlipbooks[0];
				ImpactCue->Offset = FVector2D(18.0, -5.0);
				// These two exist to show DISTINCT authored bar colours on the Frame Cues screens, so
				// they must opt into the override — a colour alone no longer implies authorship.
				ImpactCue->bOverrideColor = true;
				ImpactCue->Color = FLinearColor(0.92f, 0.20f, 0.08f, 1.0f);
				GuardCueState->bOverrideColor = true;
				GuardCueState->Color = FLinearColor(0.10f, 0.62f, 0.88f, 1.0f);
				UPaper2DPlusVisualTourFixtureCue* FreshCue =
					NewObject<UPaper2DPlusVisualTourFixtureCue>(
						Fixture->Character, NAME_None, RF_Transactional);
				FreshCue->TriggerFrame = 0;
				FreshCue->Color = FLinearColor::White;
				FreshCue->DebugName = NAME_None;
				FreshCue->CueTag = Paper2DPlusCueTags::Default.GetTag();
				Cues.Add(FreshCue);
				Paper2DPlusFrameCuePreviewBehavior::SeedVisualTourPlacementErrorBadge(
					*ImpactCue,
					NSLOCTEXT(
						"Paper2DPlusVisualTour",
						"ImpactCueBehaviorError",
						"Visual-tour behavior fixture failure"));
			}
		}

		Fixture->Catalog = NewObject<UPaper2DPlusCharacterCatalogAsset>(
			GetTransientPackage(),
			FName(*(TEXT("VisualTourCatalog_") + Token)),
			RF_Transient | RF_Transactional);
		Fixture->Root(Fixture->Catalog);
		for (const TPair<FString, FString>& Seed : {
			TPair<FString, FString>(TEXT("VisualTourRival"), TEXT("Rival Captain")),
			TPair<FString, FString>(TEXT("VisualTourMage"), TEXT("Archive Mage")) })
		{
			UPaper2DPlusCharacterProfileAsset* Extra = Tour_BuildProfile(Token, Seed.Key, Seed.Value, true);
			Fixture->CatalogCharacters.Add(Extra);
			Fixture->Root(Extra);
			RootProfileAnimations(Extra);
		}

		if (Fixture->Catalog)
		{
			FPaper2DPlusCharacterCatalogEntry& Hero = Fixture->Catalog->Entries.AddDefaulted_GetRef();
			Hero.CharacterProfile = Fixture->Character;
			Hero.LayerProfile = Fixture->Layer;
			Hero.EffectProfile = Fixture->Effect;
			Hero.CombatProfile = Fixture->Combat;
			Hero.Requirements.bRequireLayer = true;
			Hero.Requirements.bRequireEffect = true;
			Hero.Requirements.bRequireCombat = true;
			for (UPaper2DPlusCharacterProfileAsset* Extra : Fixture->CatalogCharacters)
			{
				FPaper2DPlusCharacterCatalogEntry& Row = Fixture->Catalog->Entries.AddDefaulted_GetRef();
				Row.CharacterProfile = Extra;
			}
			FPaper2DPlusCharacterCatalogGroup& Playable = Fixture->Catalog->Groups.AddDefaulted_GetRef();
			Playable.GroupName = TEXT("Playable");
			Playable.DisplayName = FText::FromString(TEXT("Playable Heroes"));
			Playable.Members.Add(Fixture->Character);
			FPaper2DPlusCharacterCatalogGroup& Rivals = Fixture->Catalog->Groups.AddDefaulted_GetRef();
			Rivals.GroupName = TEXT("NemesisPool");
			Rivals.DisplayName = FText::FromString(TEXT("Nemesis Candidates"));
			for (UPaper2DPlusCharacterProfileAsset* Extra : Fixture->CatalogCharacters)
			{
				Rivals.Members.Add(Extra);
			}
		}

		// Effect Preview/Details intentionally open with a real selection. The section is unique to the
		// transient asset and is removed in Release(), so the tour leaves no per-project editor preference.
		if (GConfig && Fixture->Effect && !EffectFlipbooks.IsEmpty())
		{
			Fixture->EffectSelectionConfigSection = FString::Printf(
				TEXT("Paper2DPlus.EffectProfileEditor.Selection.%08X"),
				FCrc::StrCrc32(*Fixture->Effect->GetPathName()));
			GConfig->SetString(
				*Fixture->EffectSelectionConfigSection,
				TEXT("Scope"),
				*Fixture->Effect->GetPathName(),
				GEditorPerProjectIni);
			GConfig->SetString(
				*Fixture->EffectSelectionConfigSection,
				TEXT("SelectedEffect"),
				*FSoftObjectPath(EffectFlipbooks[0]).ToString(),
				GEditorPerProjectIni);
		}

		if (!Fixture->Character || !Fixture->MapCharacter || !Fixture->Layer
			|| !Fixture->Effect || !Fixture->Combat || !Fixture->Catalog
			|| !Fixture->EmptyLayer || !Fixture->EmptyEffect
			|| !Fixture->EmptyCombat || !Fixture->EmptyCatalog)
		{
			Fixture->Release();
			return nullptr;
		}
		return Fixture;
	}

	// ---------------------------------------------------------------------------------------------
	// The tour runner — an FTSTicker state machine: per step, switch tab -> settle -> capture -> next.
	// ---------------------------------------------------------------------------------------------

	/**
	 * FTourStep::ViewMode value for the Catalog rail screens. The retired Groups TAB had its own tab id;
	 * the rail lives inside the Roster, so the rail captures are Roster steps distinguished by this mode.
	 */
	constexpr int32 Tour_CatalogGroupsRailViewMode = 4;
	/** The authored fixture group the rail screens scope the grid to. */
	const FName Tour_CatalogRailGroup(TEXT("Playable"));

	struct FTourStep
	{
		ETourAsset AssetKind;
		FName TabId;
		// -1 = no change, 0 = Animations List, 1 = Animations Map, 2 = Layer Appearance, 3 = Layer Art,
		// 4 = Catalog Roster with its Groups rail scoped to Tour_CatalogRailGroup
		int32 ViewMode;
		FVector2D WindowSize;
		FString Label;
		int32 SettleTicks;
	};

	struct FVisualTourWatchdogState
	{
		FVisualTourWatchdogState()
			: bCompleted(false)
			, Heartbeat(0)
			, StepIndex(0)
			, SavedCount(0)
			, SemanticPassedCount(0)
		{
		}

		TAtomic<bool> bCompleted;
		TAtomic<uint64> Heartbeat;
		TAtomic<int32> StepIndex;
		TAtomic<int32> SavedCount;
		TAtomic<int32> SemanticPassedCount;
	};

	class FVisualTourRunner : public TSharedFromThis<FVisualTourRunner>
	{
	public:
		static TSharedPtr<FVisualTourRunner> GActive;

		TSharedPtr<FVisualTourFixture> Fixture;
		FString OutDir;
		bool bExitWhenDone = false;
		bool bIncludeMap = true;
		bool bCleanupComplete = false;
		bool bPreferencesRestored = true;
		TArray<FTourStep> Steps;
		int32 StepIndex = 0;
		int32 Remaining = 0;
		int32 ApplyRetriesRemaining = 0;
		int32 SavedCount = 0;
		int32 SemanticPassedCount = 0;
		uint64 WatchdogHeartbeat = 0;
		bool bCurrentStepReady = false;
		bool bFinished = false; // set in Finish() so the keep-awake active timer returns Stop
		TWeakObjectPtr<UObject> CurrentAsset;
		TArray<TWeakPtr<SWindow>> KeepAwakeRegisteredWindows;
		TMap<TWeakObjectPtr<UObject>, FVector2D> OriginalWindowSizes;
		TArray<FString> FailedLabels;
		TArray<FString> CaptureRows;
		TSharedPtr<FVisualTourWatchdogState, ESPMode::ThreadSafe> WatchdogState;
		TFuture<void> WatchdogFuture;

		void Start()
		{
			IFileManager::Get().MakeDirectory(*OutDir, /*Tree*/ true);
			BuildSteps();
			for (const FTourStep& Step : Steps)
			{
				IFileManager::Get().Delete(*(OutDir / (Step.Label + TEXT(".png"))), false, true, true);
			}
			IFileManager::Get().Delete(*(OutDir / TEXT("00-visual-tour-manifest.txt")), false, true, true);
			StepIndex = 0;
			SemanticPassedCount = 0;
			ArmWatchdog();
			BeginCurrentStep();

			// Stepping + capture run from the core FTSTicker (safe context for screenshot readback).
			FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateSP(this, &FVisualTourRunner::Tick), 0.03f);

			UE_LOG(LogTemp, Display, TEXT("[VisualTour] started: %d steps -> %s%s"),
				Steps.Num(), *OutDir, bExitWhenDone ? TEXT(" (will exit when done)") : TEXT(""));
		}

	private:
		void BuildSteps()
		{
			const FVector2D Normal(1440.0f, 900.0f);
			const FVector2D CharacterNarrow(860.0f, 760.0f);
			// 640 px pressures the persistent Layer Structure/right rails and the fixed three-column Effect
			// grammar, plus Combat/Catalog controls, without relying on the retired embedded Structure collapse.
			const FVector2D ResponsiveNarrow(640.0f, 760.0f);
			const FVector2D Wide(1920.0f, 1080.0f);
			Steps = {
				{ ETourAsset::Character, FCharacterProfileAssetEditorToolkit::AnimationsTabId, 0, Normal, TEXT("01-character-animations-list-normal"), 18 },
			};
			if (bIncludeMap)
			{
				// A dedicated transient clone starts in Map from Construct/first paint. This exercises the real
				// editor surface without revealing a previously hidden graph widget in unattended Slate.
				Steps.Add({ ETourAsset::CharacterMap, FCharacterProfileAssetEditorToolkit::AnimationsTabId, 1, Normal, TEXT("02-character-animations-map-normal"), 30 });
			}
			Steps.Append({
				{ ETourAsset::Character, FCharacterProfileAssetEditorToolkit::HitboxEditorTabId, -1, Normal, TEXT("03-character-hitboxes-normal"), 14 },
				{ ETourAsset::Character, FCharacterProfileAssetEditorToolkit::SpriteEditorTabId, -1, Normal, TEXT("04-character-sprite-normal"), 14 },
				{ ETourAsset::Character, FCharacterProfileAssetEditorToolkit::FrameTimingTabId, -1, Normal, TEXT("05-character-frame-timing-normal"), 14 },
				{ ETourAsset::Character, FCharacterProfileAssetEditorToolkit::FrameEventsTabId, -1, Normal, TEXT("06-character-frame-cues-normal"), 16 },
				{ ETourAsset::Character, FCharacterProfileAssetEditorToolkit::RootMotionTabId, -1, Normal, TEXT("07-character-root-motion-normal"), 14 },
				{ ETourAsset::Character, FCharacterProfileAssetEditorToolkit::AnimationsTabId, 0, CharacterNarrow, TEXT("08-character-animations-list-narrow"), 20 },
				{ ETourAsset::Character, FCharacterProfileAssetEditorToolkit::AnimationsTabId, 0, Wide, TEXT("09-character-animations-list-wide"), 20 },
				{ ETourAsset::Layer, FCharacterLayerAssetEditorToolkit::ArtTabId, -1, Normal, TEXT("10-layer-authoring-workspace"), 24 },
				{ ETourAsset::Layer, FCharacterLayerAssetEditorToolkit::AppearanceTabId, -1, Normal, TEXT("11-layer-appearance-workspace"), 24 },
				{ ETourAsset::Effect, FEffectProfileAssetEditorToolkit::LibraryTabId, -1, Normal, TEXT("12-effect-library-workspace"), 24 },
				{ ETourAsset::Combat, FCombatProfileAssetEditorToolkit::ScorePreviewTabId, -1, Normal, TEXT("13-combat-score-playground"), 24 },
				{ ETourAsset::Combat, FCombatProfileAssetEditorToolkit::CombatLabTabId, -1, Normal, TEXT("14-combat-lab"), 24 },
				{ ETourAsset::Catalog, FCharacterCatalogAssetEditorToolkit::RosterTabId, -1, Normal, TEXT("15-catalog-roster"), 24 },
				{ ETourAsset::Catalog, FCharacterCatalogAssetEditorToolkit::RosterTabId, Tour_CatalogGroupsRailViewMode, Normal, TEXT("16-catalog-groups-rail"), 18 },
				{ ETourAsset::Layer, FCharacterLayerAssetEditorToolkit::ArtTabId, -1, ResponsiveNarrow, TEXT("17-layer-authoring-narrow"), 28 },
				{ ETourAsset::Layer, FCharacterLayerAssetEditorToolkit::ArtTabId, -1, Wide, TEXT("18-layer-authoring-wide"), 24 },
				{ ETourAsset::EmptyLayer, FCharacterLayerAssetEditorToolkit::StructureTabId, -1, Normal, TEXT("19-layer-empty-first-run"), 24 },
				{ ETourAsset::Effect, FEffectProfileAssetEditorToolkit::LibraryTabId, -1, ResponsiveNarrow, TEXT("20-effect-library-narrow"), 28 },
				{ ETourAsset::Effect, FEffectProfileAssetEditorToolkit::LibraryTabId, -1, Wide, TEXT("21-effect-library-wide"), 24 },
				{ ETourAsset::EmptyEffect, FEffectProfileAssetEditorToolkit::LibraryTabId, -1, Normal, TEXT("22-effect-empty-first-run"), 24 },
				{ ETourAsset::Combat, FCombatProfileAssetEditorToolkit::ScorePreviewTabId, -1, ResponsiveNarrow, TEXT("23-combat-score-playground-narrow"), 28 },
				{ ETourAsset::Combat, FCombatProfileAssetEditorToolkit::ScorePreviewTabId, -1, Wide, TEXT("24-combat-score-playground-wide"), 24 },
				{ ETourAsset::Combat, FCombatProfileAssetEditorToolkit::CombatLabTabId, -1, ResponsiveNarrow, TEXT("25-combat-lab-narrow"), 28 },
				{ ETourAsset::Combat, FCombatProfileAssetEditorToolkit::CombatLabTabId, -1, Wide, TEXT("26-combat-lab-wide"), 24 },
				{ ETourAsset::EmptyCombat, FCombatProfileAssetEditorToolkit::SetupTabId, -1, Normal, TEXT("27-combat-empty-first-run"), 24 },
				{ ETourAsset::Catalog, FCharacterCatalogAssetEditorToolkit::RosterTabId, -1, ResponsiveNarrow, TEXT("28-catalog-roster-narrow"), 28 },
				{ ETourAsset::Catalog, FCharacterCatalogAssetEditorToolkit::RosterTabId, -1, Wide, TEXT("29-catalog-roster-wide"), 24 },
				{ ETourAsset::Catalog, FCharacterCatalogAssetEditorToolkit::RosterTabId, Tour_CatalogGroupsRailViewMode, ResponsiveNarrow, TEXT("30-catalog-groups-rail-narrow"), 24 },
				{ ETourAsset::Catalog, FCharacterCatalogAssetEditorToolkit::RosterTabId, Tour_CatalogGroupsRailViewMode, Wide, TEXT("31-catalog-groups-rail-wide"), 20 },
				{ ETourAsset::EmptyCatalog, FCharacterCatalogAssetEditorToolkit::RosterTabId, -1, Normal, TEXT("32-catalog-empty-first-run"), 24 },
			});
		}

		// Keep-awake active timer: does NO work — it returns Continue purely so the Slate app never idle-sleeps
		// and the engine main loop keeps running (which keeps the stepping FTSTicker below firing). Stops when
		// the tour finishes. Active timers are the only thing that reliably prevents an unattended editor from
		// parking its main loop once it has nothing else to do.
		EActiveTimerReturnType OnKeepAwakeTimer(double /*InCurrentTime*/, float /*InDeltaTime*/)
		{
			return bFinished ? EActiveTimerReturnType::Stop : EActiveTimerReturnType::Continue;
		}

		// Stepping + capture run from the core FTSTicker (the engine main loop, OUTSIDE Slate's widget tick —
		// the safe context for ForceRedrawWindow + TakeScreenshot; capturing from inside an active timer would
		// be a reentrant render). The keep-awake timer above guarantees this keeps firing.
		bool Tick(float /*Dt*/)
		{
			PulseWatchdog();
			if (!Steps.IsValidIndex(StepIndex))
			{
				return false;
			}
			if (!bCurrentStepReady)
			{
				if (ApplyRetriesRemaining-- <= 0)
				{
					RecordFailure(Steps[StepIndex], TEXT("editor/tab did not become ready"));
					AdvanceStep();
					return !bFinished;
				}
				bCurrentStepReady = ApplyStep(Steps[StepIndex]);
				if (bCurrentStepReady)
				{
					Remaining = Steps[StepIndex].SettleTicks;
				}
				return true;
			}
			if (Remaining > 0)
			{
				--Remaining;
				return true; // let the just-switched tab paint
			}

			Capture(Steps[StepIndex]);
			AdvanceStep();
			return !bFinished;
		}

		void BeginCurrentStep()
		{
			bCurrentStepReady = false;
			ApplyRetriesRemaining = 60;
			Remaining = 0;
			if (Steps.IsValidIndex(StepIndex))
			{
				if (WatchdogState.IsValid())
				{
					WatchdogState->StepIndex.Store(StepIndex);
				}
				bCurrentStepReady = ApplyStep(Steps[StepIndex]);
				if (bCurrentStepReady) Remaining = Steps[StepIndex].SettleTicks;
			}
		}

		void AdvanceStep()
		{
			++StepIndex;
			if (!Steps.IsValidIndex(StepIndex))
			{
				Finish();
				return;
			}
			BeginCurrentStep();
		}

		FAssetEditorToolkit* ResolveToolkit(UObject* Asset) const
		{
			UAssetEditorSubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
			if (Sub && Asset)
			{
				if (IAssetEditorInstance* Inst = Sub->FindEditorForAsset(Asset, /*bFocusIfOpen*/ false))
				{
					return static_cast<FAssetEditorToolkit*>(Inst);
				}
			}
			return nullptr;
		}

		static SWidget* FindWidgetWhoseTypeContains(SWidget& Root, const TCHAR* TypeFragment)
		{
			if (Root.GetTypeAsString().Contains(TypeFragment))
			{
				return &Root;
			}
			if (FChildren* Children = Root.GetChildren())
			{
				for (int32 Index = 0; Index < Children->Num(); ++Index)
				{
					if (SWidget* Found = FindWidgetWhoseTypeContains(
						Children->GetChildAt(Index).Get(), TypeFragment))
					{
						return Found;
					}
				}
			}
			return nullptr;
		}

		static int32 CountWidgetsWhoseTypeContains(SWidget& Root, const TCHAR* TypeFragment)
		{
			int32 Count = Root.GetTypeAsString().Contains(TypeFragment) ? 1 : 0;
			if (FChildren* Children = Root.GetChildren())
			{
				for (int32 Index = 0; Index < Children->Num(); ++Index)
				{
					Count += CountWidgetsWhoseTypeContains(
						Children->GetChildAt(Index).Get(), TypeFragment);
				}
			}
			return Count;
		}

		static int32 CountEffectivelyVisibleWidgetsWhoseTypeContains(
			SWidget& Root,
			const TCHAR* TypeFragment,
			bool bAncestorsVisible = true)
		{
			const bool bEffectivelyVisible = bAncestorsVisible && Root.GetVisibility().IsVisible();
			if (!bEffectivelyVisible)
			{
				return 0;
			}
			int32 Count = Root.GetTypeAsString().Contains(TypeFragment) ? 1 : 0;
			if (FChildren* Children = Root.GetChildren())
			{
				for (int32 Index = 0; Index < Children->Num(); ++Index)
				{
					Count += CountEffectivelyVisibleWidgetsWhoseTypeContains(
						Children->GetChildAt(Index).Get(), TypeFragment, bEffectivelyVisible);
				}
			}
			return Count;
		}

		static bool ContainsEffectivelyVisibleText(
			SWidget& Root,
			const TCHAR* TextFragment,
			bool bExact = false,
			bool bAncestorsVisible = true)
		{
			const bool bEffectivelyVisible = bAncestorsVisible && Root.GetVisibility().IsVisible();
			if (!bEffectivelyVisible)
			{
				return false;
			}
			if (Root.GetType() == FName(TEXT("STextBlock")))
			{
				const FString Text = static_cast<STextBlock&>(Root).GetText().ToString();
				if ((bExact && Text.Equals(TextFragment, ESearchCase::IgnoreCase))
					|| (!bExact && Text.Contains(TextFragment, ESearchCase::IgnoreCase)))
				{
					return true;
				}
			}
			if (FChildren* Children = Root.GetChildren())
			{
				for (int32 Index = 0; Index < Children->Num(); ++Index)
				{
					if (ContainsEffectivelyVisibleText(
						Children->GetChildAt(Index).Get(), TextFragment, bExact, bEffectivelyVisible))
					{
						return true;
					}
				}
			}
			return false;
		}

		/** Same walk as ContainsEffectivelyVisibleText, but blind to one subtree.
		 *
		 *  Used to interrogate a host panel's OWN chrome without seeing text that legitimately belongs to
		 *  the view it hosts — e.g. asserting that the Animations panel never rebuilds the retired
		 *  [Grid | List | Map] row, while its Map view is still free to label its overflow menu "Map". */
		static bool ContainsEffectivelyVisibleTextOutside(
			SWidget& Root,
			const SWidget* ExcludedSubtree,
			const TCHAR* TextFragment,
			bool bExact = false,
			bool bAncestorsVisible = true)
		{
			if (&Root == ExcludedSubtree)
			{
				return false;
			}
			const bool bEffectivelyVisible = bAncestorsVisible && Root.GetVisibility().IsVisible();
			if (!bEffectivelyVisible)
			{
				return false;
			}
			if (Root.GetType() == FName(TEXT("STextBlock")))
			{
				const FString Text = static_cast<STextBlock&>(Root).GetText().ToString();
				if ((bExact && Text.Equals(TextFragment, ESearchCase::IgnoreCase))
					|| (!bExact && Text.Contains(TextFragment, ESearchCase::IgnoreCase)))
				{
					return true;
				}
			}
			if (FChildren* Children = Root.GetChildren())
			{
				for (int32 Index = 0; Index < Children->Num(); ++Index)
				{
					if (ContainsEffectivelyVisibleTextOutside(
						Children->GetChildAt(Index).Get(),
						ExcludedSubtree,
						TextFragment,
						bExact,
						bEffectivelyVisible))
					{
						return true;
					}
				}
			}
			return false;
		}

		static bool IsEmptyAssetKind(ETourAsset Kind)
		{
			return Kind == ETourAsset::EmptyLayer
				|| Kind == ETourAsset::EmptyEffect
				|| Kind == ETourAsset::EmptyCombat
				|| Kind == ETourAsset::EmptyCatalog;
		}

		static bool ValidateRequestedWindowSize(
			const TSharedRef<SWindow>& Window,
			const FTourStep& Step,
			FString& OutReason)
		{
			const FVector2D ActualSize = Window->GetSizeInScreen();
			constexpr float SizeTolerance = 64.0f;
			if (FMath::Abs(ActualSize.X - Step.WindowSize.X) > SizeTolerance
				|| FMath::Abs(ActualSize.Y - Step.WindowSize.Y) > SizeTolerance)
			{
				OutReason = FString::Printf(
					TEXT("window settled at %.0fx%.0f; requested %.0fx%.0f"),
					ActualSize.X,
					ActualSize.Y,
					Step.WindowSize.X,
					Step.WindowSize.Y);
				return false;
			}
			return true;
		}

		bool ValidateSemanticStructure(const FTourStep& Step, FString& OutReason) const
		{
			OutReason.Reset();
			UObject* Target = Fixture.IsValid() ? Fixture->GetAsset(Step.AssetKind) : nullptr;
			FAssetEditorToolkit* Toolkit = ResolveToolkit(Target);
			if (!Target || !Toolkit || !Toolkit->GetTabManager().IsValid())
			{
				OutReason = TEXT("asset editor toolkit was not live");
				return false;
			}
			const TSharedPtr<FTabManager> Manager = Toolkit->GetTabManager();
			const TSharedPtr<SDockTab> ActiveTab = Manager->FindExistingLiveTab(Step.TabId);
			if (!ActiveTab.IsValid() || !ActiveTab->IsForeground())
			{
				OutReason = FString::Printf(
					TEXT("requested tab %s was not the live foreground tab"),
					*Step.TabId.ToString());
				return false;
			}
			const TSharedPtr<SWindow> HostWindow = ResolveWindow(Target);
			if (!HostWindow.IsValid()
				|| !ValidateRequestedWindowSize(HostWindow.ToSharedRef(), Step, OutReason))
			{
				if (OutReason.IsEmpty()) OutReason = TEXT("asset editor had no live host window");
				return false;
			}
			// SDockTab's own cached geometry describes its tab-well label (typically about 160x25),
			// not the docked workspace body. Validate the live content geometry so this release gate
			// measures the designer surface that was actually captured.
			const TSharedRef<SWidget> ActiveContent = ActiveTab->GetContent();
			const FVector2D ActiveSize = ActiveContent->GetCachedGeometry().GetLocalSize();
			if (ActiveSize.X < 100.0f || ActiveSize.Y < 120.0f)
			{
				OutReason = FString::Printf(
					TEXT("foreground workspace was not usable at %.0fx%.0f"),
					ActiveSize.X,
					ActiveSize.Y);
				return false;
			}
			const bool bCatalogWorkspace = Step.AssetKind == ETourAsset::Catalog
				|| Step.AssetKind == ETourAsset::EmptyCatalog;
			if (!bCatalogWorkspace
				&& CountWidgetsWhoseTypeContains(*HostWindow, TEXT("SProfileValidationPanel")) != 0)
			{
				OutReason = TEXT("a retired permanent Validation surface was present in the asset workspace");
				return false;
			}
			// Character Profile's current layout *name* retains "CompactValidation" for save compatibility,
			// even though its permanent Validation tab is gone. The four profile workspaces under this
			// responsive matrix use fresh names, so their persisted trees can be checked without that false hit.
			if (Step.AssetKind != ETourAsset::Character
				&& Step.AssetKind != ETourAsset::CharacterMap
				&& Manager->PersistLayout()->ToString().Contains(
					TEXT("Validation"), ESearchCase::IgnoreCase))
			{
				OutReason = TEXT("the live persisted workspace still contained a Validation tab");
				return false;
			}

			if (Step.AssetKind == ETourAsset::Character || Step.AssetKind == ETourAsset::CharacterMap)
			{
				UPaper2DPlusCharacterProfileAsset* CharacterAsset =
					Cast<UPaper2DPlusCharacterProfileAsset>(Target);
				const TSharedPtr<FCharacterProfileEditorModel> CharacterModel =
					FCharacterProfileEditorModel::GActiveEditorModel.Pin();
				if (!CharacterAsset || CharacterAsset->Flipbooks.Num() != 4
					|| !CharacterModel.IsValid() || CharacterModel->GetAsset() != CharacterAsset
					|| CharacterModel->GetSelectedFlipbookIndex() != 0)
				{
					OutReason = TEXT("Character workspace did not resolve the populated four-animation fixture");
					return false;
				}

				// TASK-151 (R3/AE7): whole-profile validation is an Asset-menu command (or, in a world-centric
				// host, the compact Profile Actions header menu) — no Character Profile main tool carries a
				// persistent Validate action. WrapMainToolContent builds ONE shared header for all six tools, so
				// a reintroduced button lands on every one of them. Assert it here, OUTSIDE the per-tool
				// branches, so every captured Character tool screen (Animations List/Map plus Hitbox, Sprite,
				// Frame Timing, Frame Cues, and Root Motion) proves the absence — an Animations-only assertion
				// would pass a regression that happened to skip that one header.
				if (ContainsEffectivelyVisibleText(*ActiveContent, TEXT("Validate")))
				{
					OutReason = TEXT("the retired persistent Validate action was still in a Character Profile main tool; whole-profile validation belongs in the Asset menu");
					return false;
				}

				if (Step.TabId == FCharacterProfileAssetEditorToolkit::AnimationsTabId)
				{
					SWidget* AnimationsWidget = FindWidgetWhoseTypeContains(
						*ActiveContent, TEXT("SAnimationsPanel"));
					SAnimationsPanel* AnimationsPanel = AnimationsWidget
						? static_cast<SAnimationsPanel*>(AnimationsWidget) : nullptr;
					const bool bExpectMap = Step.AssetKind == ETourAsset::CharacterMap;
					const EAnimationsViewMode ExpectedMode = bExpectMap
						? EAnimationsViewMode::Map : EAnimationsViewMode::List;
					SWidget* BrowserWidget = AnimationsWidget
						? FindWidgetWhoseTypeContains(*AnimationsWidget, TEXT("SFlipbookBrowserPanel")) : nullptr;
					SWidget* MapWidget = AnimationsWidget
						? FindWidgetWhoseTypeContains(*AnimationsWidget, TEXT("SAnimationMapPanel")) : nullptr;
					SAnimationMapPanel* MapPanel = MapWidget
						? static_cast<SAnimationMapPanel*>(MapWidget) : nullptr;
					const bool bBrowserLaidOut = BrowserWidget
						&& BrowserWidget->GetCachedGeometry().GetLocalSize().X > 100.0f
						&& BrowserWidget->GetCachedGeometry().GetLocalSize().Y > 100.0f;
					const bool bMapLaidOut = MapWidget
						&& MapWidget->GetCachedGeometry().GetLocalSize().X > 100.0f
						&& MapWidget->GetCachedGeometry().GetLocalSize().Y > 100.0f;
					if (!AnimationsPanel || AnimationsPanel->GetViewMode() != ExpectedMode
						|| (bExpectMap && (!bMapLaidOut
							|| !MapPanel
							|| !MapPanel->HasLiveScopedGraphForAutomation()
							|| MapPanel->GetProjectedMoveNodeCountForAutomation() != 2
							|| MapPanel->GetProjectedTransitionNodeCountForAutomation() != 1))
						|| (!bExpectMap && (!bBrowserLaidOut
							|| !ContainsEffectivelyVisibleText(*BrowserWidget, TEXT("Jab")))))
					{
						OutReason = bExpectMap
							? TEXT("Character Animations Map did not expose its live graph surface")
							: TEXT("Character Animations List did not expose the populated browser surface");
						return false;
					}

					// TASK-151: the workspace must present the CLEANED-UP Animations hierarchy, not the
					// retired dense chrome. One shared `View: <current>` control identifies and switches the
					// view from the Current Animation header (R1/R2); the dedicated [Grid | List | Map] row is
					// gone (R1) — the persistent-Validate check (R3) is hoisted above the per-tool branches so it
					// covers every Character tool; and the active view exposes its own compact control surface —
					// one browse bar or one canvas rail — instead of a toolbar (R4-R14). These assertions live
					// beside the existing surface checks so a regression fails readiness BEFORE the release
					// screenshot is taken.
					const FString ExpectedViewControl = FString::Printf(
						TEXT("View: %s"),
						*SAnimationsPanel::GetViewModeLabel(ExpectedMode).ToString());
					if (!ContainsEffectivelyVisibleText(
						*ActiveContent, *ExpectedViewControl, /*bExact=*/true))
					{
						OutReason = FString::Printf(
							TEXT("the shared Animations header did not expose the '%s' view control"),
							*ExpectedViewControl);
						return false;
					}

					// The retired row spelled each view as its own segment label. The header control now spells
					// the active view as "View: <name>", so an EXACT "Grid"/"List"/"Map" text block anywhere in
					// this tab's chrome OUTSIDE the view switcher means the row came back. The walk starts at
					// ActiveContent, not the Animations panel: the switcher now IS that panel's whole ChildSlot,
					// so a panel-rooted walk inspects almost nothing, while the natural home for a reintroduced
					// segmented row is the SHARED header WrapMainToolContent builds above it.
					SWidget* ViewSwitcherWidget = AnimationsWidget
						? FindWidgetWhoseTypeContains(*AnimationsWidget, TEXT("SWidgetSwitcher")) : nullptr;
					if (!ViewSwitcherWidget)
					{
						OutReason = TEXT("Animations workspace did not expose its Grid/List/Map view switcher");
						return false;
					}
					for (const TCHAR* RetiredSegment : { TEXT("Grid"), TEXT("List"), TEXT("Map") })
					{
						if (ContainsEffectivelyVisibleTextOutside(
							*ActiveContent, ViewSwitcherWidget, RetiredSegment, /*bExact=*/true))
						{
							OutReason = FString::Printf(
								TEXT("the retired dedicated [Grid | List | Map] view row was present in the Animations tab chrome (segment '%s')"),
								RetiredSegment);
							return false;
						}
					}

					if (bExpectMap)
					{
						// R9-R14: a compact canvas rail over the graph — context-appropriate scope navigation,
						// Zoom to Fit, the compact inactive Filter control, and the labeled "Map" overflow that
						// owns advanced map actions such as phase derivation.
						if (!ContainsEffectivelyVisibleText(*MapWidget, TEXT("Back to Groups"), true)
							|| !ContainsEffectivelyVisibleText(*MapWidget, TEXT("Zoom to Fit"), true)
							|| !ContainsEffectivelyVisibleText(*MapWidget, TEXT("Filter"), true)
							|| !ContainsEffectivelyVisibleText(*MapWidget, TEXT("Map"), true))
						{
							OutReason = TEXT("Character Animations Map did not expose its compact canvas rail (scope navigation, Zoom to Fit, the inactive Filter control, and the Map overflow)");
							return false;
						}
						for (const TCHAR* RetiredMapCommand : {
							TEXT("Derive Phases") })
						{
							if (ContainsEffectivelyVisibleText(*MapWidget, RetiredMapCommand))
							{
								OutReason = FString::Printf(
									TEXT("the retired persistent Animation Map command toolbar was present ('%s' should live in the Map overflow)"),
									RetiredMapCommand);
								return false;
							}
						}
					}
					else
					{
						// R4-R8: one compact browse bar — Add is the sole persistent collection mutation,
						// organization is one labeled menu, and group maintenance is in Browse overflow.
						// The Organize control has TWO legitimate presentations and the tour must accept
						// both. At comfortable width it reads "Organize: <value>"; once the browse bar
						// goes compact it drops the prefix and shows the value alone, because R16
						// requires the active organization mode to stay readable without opening the
						// menu. Demanding the "Organize:" prefix therefore failed every narrow capture
						// even though the control was present and correct.
						const bool bOrganizeSegmentMyGroups =
							ContainsEffectivelyVisibleText(*BrowserWidget, TEXT("My Groups"), true);
						const bool bOrganizeSegmentByTagGroup =
							ContainsEffectivelyVisibleText(*BrowserWidget, TEXT("By Tag Group"), true);
						const bool bOrganizePresent =
							ContainsEffectivelyVisibleText(*BrowserWidget, TEXT("Organize:"))
							|| bOrganizeSegmentMyGroups
							|| bOrganizeSegmentByTagGroup;
						// The Add button compacts the same way Organize does: "+ Add Flipbooks…" at
						// comfortable width, "+ Add…" once the browse bar goes compact (the
						// FlipbookBrowserPanel AddFlipbooksCompact label). Accept both presentations.
						const bool bAddPresent =
							ContainsEffectivelyVisibleText(*BrowserWidget, TEXT("Add Flipbooks"))
							|| ContainsEffectivelyVisibleText(*BrowserWidget, TEXT("+ Add"));
						if (!bAddPresent
							|| !bOrganizePresent
							|| !ContainsEffectivelyVisibleText(*BrowserWidget, TEXT("Browse"), true))
						{
							OutReason = TEXT("Character Animations List did not expose its compact browse bar (Add Flipbooks, the Organize control, and the Browse overflow)");
							return false;
						}
						for (const TCHAR* RetiredBrowseCommand : { TEXT("New Group"), TEXT("Auto-group") })
						{
							if (ContainsEffectivelyVisibleText(*BrowserWidget, RetiredBrowseCommand))
							{
								OutReason = FString::Printf(
									TEXT("the retired persistent group-maintenance buttons were present ('%s' should live in the Browse overflow)"),
									RetiredBrowseCommand);
								return false;
							}
						}
						// What was retired is a TWO-SEGMENT toggle: both choices on screen at once, one
						// of them selected. The compact Organize button legitimately shows exactly ONE
						// of those strings — whichever mode is active — so treating either string on
						// its own as proof of the old toggle rejected the new control it replaced.
						// Both visible simultaneously is the thing that cannot happen in the new UI,
						// and that is what this now tests.
						if (bOrganizeSegmentMyGroups && bOrganizeSegmentByTagGroup)
						{
							OutReason = TEXT("the retired My Groups | By Tag Group segmented toggle was present (both segments were visible at once; the Organize control must show only the active mode, with the alternative inside its menu)");
							return false;
						}
					}
				}
				else if (Step.TabId == FCharacterProfileAssetEditorToolkit::HitboxEditorTabId)
				{
					SWidget* PanelWidget = FindWidgetWhoseTypeContains(
						*ActiveContent, TEXT("SHitboxEditorPanel"));
					SHitboxEditorPanel* Panel = PanelWidget
						? static_cast<SHitboxEditorPanel*>(PanelWidget) : nullptr;
					if (!Panel || Panel->GetCurrentFrameCountForTests() != 5
						|| CountEffectivelyVisibleWidgetsWhoseTypeContains(
							*PanelWidget, TEXT("SCharacterProfileEditorCanvas")) != 1)
					{
						OutReason = TEXT("Character Hitbox tool did not resolve its five-frame canvas/data surface");
						return false;
					}
				}
				else if (Step.TabId == FCharacterProfileAssetEditorToolkit::SpriteEditorTabId)
				{
					SWidget* PanelWidget = FindWidgetWhoseTypeContains(
						*ActiveContent, TEXT("SSpriteEditorPanel"));
					if (!PanelWidget
						|| CountEffectivelyVisibleWidgetsWhoseTypeContains(
							*PanelWidget, TEXT("SSpriteEditorCanvas")) != 1
						|| !ContainsEffectivelyVisibleText(
							*PanelWidget, TEXT("Playback:")))
					{
						OutReason = TEXT("Character Sprite tool did not expose its live preview/alignment surface");
						return false;
					}
				}
				else if (Step.TabId == FCharacterProfileAssetEditorToolkit::FrameTimingTabId)
				{
					SWidget* PanelWidget = FindWidgetWhoseTypeContains(
						*ActiveContent, TEXT("SFrameTimingEditor"));
					SFrameTimingEditor* Panel = PanelWidget
						? static_cast<SFrameTimingEditor*>(PanelWidget) : nullptr;
					if (!Panel || Panel->GetSelectedFlipbookIndexForTests() != 0
						|| Panel->GetFPSForTests() <= 0.0f
						|| Panel->GetTotalDurationSecondsForTests() <= 0.0f
						|| CountEffectivelyVisibleWidgetsWhoseTypeContains(
							*PanelWidget, TEXT("SAnimationTimeline")) != 1)
					{
						OutReason = TEXT("Character Frame Timing tool did not resolve timeline/FPS/duration data");
						return false;
					}
				}
				else if (Step.TabId == FCharacterProfileAssetEditorToolkit::FrameEventsTabId)
				{
					SWidget* PanelWidget = FindWidgetWhoseTypeContains(
						*ActiveContent, TEXT("SFrameEventEditor"));
					SFrameEventEditor* Panel = PanelWidget
						? static_cast<SFrameEventEditor*>(PanelWidget) : nullptr;
					SWidget* TimelineWidget = PanelWidget
						? FindWidgetWhoseTypeContains(
							*PanelWidget, TEXT("SFrameEventTimelineTrack"))
						: nullptr;
					SFrameEventTimelineTrack* Timeline = TimelineWidget
						? static_cast<SFrameEventTimelineTrack*>(TimelineWidget)
						: nullptr;
					const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues =
						CharacterAsset->Flipbooks[0].FrameEventData.FrameCues;
					if (!Panel || !Panel->GetDataProviderForTests().IsValid()
						|| !Timeline
						|| Panel->GetCueCountForTests() != 3
						|| Cues.Num() != 3
						|| CountEffectivelyVisibleWidgetsWhoseTypeContains(
							*PanelWidget, TEXT("SFrameEventPreviewCanvas")) != 1
						|| CountEffectivelyVisibleWidgetsWhoseTypeContains(
							*PanelWidget, TEXT("SFrameEventTimelineTrack")) != 1)
					{
						OutReason = TEXT("Character Frame Cues tool did not resolve Cue/preview/timeline data");
						return false;
					}
					const UPaper2DPlusVisualTourFixtureCue* ImpactCue =
						Cast<UPaper2DPlusVisualTourFixtureCue>(Cues[0]);
					const UPaper2DPlusVisualTourFixtureCueState* GuardCueState =
						Cast<UPaper2DPlusVisualTourFixtureCueState>(Cues[1]);
					const UPaper2DPlusVisualTourFixtureCue* FreshCue =
						Cast<UPaper2DPlusVisualTourFixtureCue>(Cues[2]);
					if (!ImpactCue || !GuardCueState || !FreshCue
						|| ImpactCue->DebugName != FName(TEXT("Impact Cue"))
						|| GuardCueState->DebugName != FName(TEXT("Guard Cue State"))
						|| ImpactCue->TriggerFrame != 2
						|| GuardCueState->StartFrame != 3
						|| GuardCueState->FrameCount != 2
						|| FreshCue->TriggerFrame != 0
						|| ImpactCue->IsRangeCue()
						|| !GuardCueState->IsRangeCue()
						|| FreshCue->IsRangeCue()
						|| ImpactCue->DemoEffectFlipbook.IsNull()
						|| !ImpactCue->bOverrideColor
						|| !GuardCueState->bOverrideColor
						|| ImpactCue->Color.Equals(FLinearColor::White)
						|| GuardCueState->Color.Equals(FLinearColor::White)
						|| ImpactCue->Color.Equals(GuardCueState->Color)
						|| FreshCue->bOverrideColor
						|| !FreshCue->Color.Equals(FLinearColor::White)
						|| !FreshCue->DebugName.IsNone()
						|| !FreshCue->CueTag.MatchesTagExact(
							Paper2DPlusCueTags::Default.GetTag()))
					{
						OutReason = TEXT("Character Frame Cues fixture did not preserve the authored Impact Cue, distinct Guard Cue State, and untouched fresh-equivalent Cue identities");
						return false;
					}
					const Paper2DPlusFrameCuePreviewBehavior::FPlacementBadge ImpactBadge =
						Paper2DPlusFrameCuePreviewBehavior::GetPlacementBadge(ImpactCue);
					const FString ImpactSummary =
						Timeline->GetAccessibleSummaryTextForCue(0).ToString();
					const FString GuardCueStateSummary =
						Timeline->GetAccessibleSummaryTextForCue(1).ToString();
					const FString FreshCueSummary =
						Timeline->GetAccessibleSummaryTextForCue(2).ToString();
					const FString FreshTypeIdentity =
						FreshCue->GetClass()->GetDisplayNameText().ToString();
					if (!ImpactBadge.bHasError
						|| !ImpactSummary.Contains(
							TEXT("Selected Cue Impact Cue, frame 2"))
						|| !ImpactSummary.Contains(TEXT("Behavior error:"))
						|| !ImpactSummary.Contains(
							TEXT("Visual-tour behavior fixture failure"))
						|| !GuardCueStateSummary.Contains(
							TEXT("Selected Cue State Guard Cue State, start frame 3, duration 2 frames"))
						|| !FreshCueSummary.Contains(FString::Printf(
							TEXT("Selected Cue %s, frame 0"),
							*FreshTypeIdentity))
						|| ImpactSummary.Contains(TEXT("Moment Cue"))
						|| ImpactSummary.Contains(TEXT("Range Cue"))
						|| GuardCueStateSummary.Contains(TEXT("Moment Cue"))
						|| GuardCueStateSummary.Contains(TEXT("Range Cue"))
						|| FreshCueSummary.Contains(TEXT("Moment Cue"))
						|| FreshCueSummary.Contains(TEXT("Range Cue"))
						|| FreshCueSummary.Contains(TEXT("Cue State")))
					{
						OutReason = TEXT("Character Frame Cues tool did not expose all three exact Cue identities, timing forms, and the Impact behavior-error badge");
						return false;
					}

					// U7: the tab's default surface is pick a frame, add a Cue, edit, preview. Named Cue
					// tracks are an optional editor-only organizational sidecar, so the persistent
					// "+ Add Track" button retired into one labeled "Manage tracks..." overflow that owns
					// create/rename/reorder/remove and the Add Cue target. Both halves are asserted here so
					// a regression fails readiness BEFORE the release screenshot is taken.
					if (!ContainsEffectivelyVisibleText(*PanelWidget, TEXT("Manage tracks")))
					{
						OutReason = TEXT("Character Frame Cues tool did not expose the Manage tracks overflow");
						return false;
					}
					if (ContainsEffectivelyVisibleText(*PanelWidget, TEXT("Add Track")))
					{
						OutReason = TEXT("the retired persistent + Add Track button was present in the Frame Cues chrome (track commands belong in the Manage tracks overflow)");
						return false;
					}
					// The fixture authors no named tracks, so the implicit Default lane is the whole story
					// and the timeline must not be spending a line on an Add target that only ever reads
					// "Default".
					if (ContainsEffectivelyVisibleText(*PanelWidget, TEXT("Add target:")))
					{
						OutReason = TEXT("the Frame Cues timeline showed the named-track Add target line with no named tracks authored");
						return false;
					}
				}
				else if (Step.TabId == FCharacterProfileAssetEditorToolkit::RootMotionTabId)
				{
					SWidget* PanelWidget = FindWidgetWhoseTypeContains(
						*ActiveContent, TEXT("SRootMotionEditor"));
					SRootMotionEditor* Panel = PanelWidget
						? static_cast<SRootMotionEditor*>(PanelWidget) : nullptr;
					const TArray<FRootMotionFrameData>& Motion =
						CharacterAsset->Flipbooks[0].MotionData.RootMotion;
					if (!Panel || Panel->GetSelectedFlipbookIndexForTests() != 0
						|| Motion.Num() != 5 || Motion.Last().Position.IsNearlyZero()
						|| CountEffectivelyVisibleWidgetsWhoseTypeContains(
							*PanelWidget, TEXT("SRootMotionCanvas")) != 1)
					{
						OutReason = TEXT("Character Root Motion tool did not resolve its authored path/canvas surface");
						return false;
					}
				}
				else
				{
					OutReason = TEXT("Character visual-tour step had no surface-specific semantic validator");
					return false;
				}
			}
			else if (Step.AssetKind == ETourAsset::Layer || Step.AssetKind == ETourAsset::EmptyLayer)
			{
				SWidget* BakeControl = FindWidgetWhoseTypeContains(
					*HostWindow, TEXT("SCharacterLayerBakeStatusWidget"));
				const int32 VisibleBakeControls = CountEffectivelyVisibleWidgetsWhoseTypeContains(
					*HostWindow, TEXT("SCharacterLayerBakeStatusWidget"));
				const TSharedPtr<SDockTab> StructureTab = Manager->FindExistingLiveTab(
					FCharacterLayerAssetEditorToolkit::StructureTabId);
				const TSharedPtr<SDockTab> ArtTab = Manager->FindExistingLiveTab(
					FCharacterLayerAssetEditorToolkit::ArtTabId);
				const TSharedPtr<SDockTab> HitboxesTab = Manager->FindExistingLiveTab(
					FCharacterLayerAssetEditorToolkit::HitboxesTabId);
				const TSharedPtr<SDockTab> FrameCuesTab = Manager->FindExistingLiveTab(
					FCharacterLayerAssetEditorToolkit::FrameCuesTabId);
				const TSharedPtr<SDockTab> AppearanceTab = Manager->FindExistingLiveTab(
					FCharacterLayerAssetEditorToolkit::AppearanceTabId);
				const TSharedPtr<SDockTab> ToolPanelsTab = Manager->FindExistingLiveTab(
					FCharacterLayerAssetEditorToolkit::ContextHostTabId);
				const TSharedPtr<SDockTab> CompletionTab = Manager->FindExistingLiveTab(
					FCharacterLayerAssetEditorToolkit::CompletionTabId);
				if (!StructureTab.IsValid() || !ArtTab.IsValid() || !HitboxesTab.IsValid()
					|| !FrameCuesTab.IsValid() || !AppearanceTab.IsValid()
					|| !ToolPanelsTab.IsValid() || !CompletionTab.IsValid())
				{
					OutReason = TEXT("Layer editor lost a docked tool, Tool Panels, or Completion tab");
					return false;
				}
				if (CountEffectivelyVisibleWidgetsWhoseTypeContains(
						*HostWindow, TEXT("SLayerStructureTree")) != 1
					|| StructureTab->GetCachedGeometry().GetLocalSize().X < 100.0f)
				{
					OutReason = TEXT("Layer Structure was not persistently visible in a usable side dock");
					return false;
				}
				if (FindWidgetWhoseTypeContains(*HostWindow, TEXT("SLayerAuthoringWorkspace")))
				{
					OutReason = TEXT("Layer editor still mounted the retired embedded mode-switch workspace");
					return false;
				}
				if (CountEffectivelyVisibleWidgetsWhoseTypeContains(
						*HostWindow, TEXT("SAnimationProfileSwitcher")) != 1
					|| !FindWidgetWhoseTypeContains(*ToolPanelsTab->GetContent(), TEXT("SProfileToolPanelHost")))
				{
					OutReason = TEXT("Layer editor did not expose one shared animation control and the contextual Tool Panels host");
					return false;
				}
				if (!BakeControl
					|| VisibleBakeControls != 1
					|| !ContainsEffectivelyVisibleText(*BakeControl, TEXT("Bake"), true)
					|| ContainsEffectivelyVisibleText(*HostWindow, TEXT("Never Baked"), true)
					|| ContainsEffectivelyVisibleText(*HostWindow, TEXT("Ready for one-time adoption"), true))
				{
					OutReason = TEXT("Layer completion stack did not expose one compact Bake control without the retired status banner");
					return false;
				}

				const FGeometry& ActiveGeometry = HostWindow->GetCachedGeometry();
				const FGeometry& BakeGeometry = BakeControl->GetCachedGeometry();
				const FVector2D ActivePosition = ActiveGeometry.LocalToAbsolute(FVector2D::ZeroVector);
				const FVector2D ActiveBoundsSize = ActiveGeometry.LocalToAbsolute(ActiveGeometry.GetLocalSize())
					- ActivePosition;
				const FVector2D BakePosition = BakeGeometry.LocalToAbsolute(FVector2D::ZeroVector);
				const FVector2D BakeSize = BakeGeometry.LocalToAbsolute(BakeGeometry.GetLocalSize())
					- BakePosition;
				const FVector2D BakeCenter = BakePosition + (BakeSize * 0.5f);
				if (BakeCenter.X < ActivePosition.X + (ActiveBoundsSize.X * 0.75f)
					|| BakeCenter.Y < ActivePosition.Y + (ActiveBoundsSize.Y * 0.75f))
				{
					OutReason = TEXT("Layer Bake control was not anchored in the bottom-right completion region");
					return false;
				}

				const FVector2D ToolPanelsPosition = ToolPanelsTab->GetCachedGeometry().LocalToAbsolute(
					FVector2D::ZeroVector);
				const FVector2D CompletionPosition = CompletionTab->GetCachedGeometry().LocalToAbsolute(
					FVector2D::ZeroVector);
				if (ToolPanelsPosition.Y >= CompletionPosition.Y)
				{
					OutReason = TEXT("Layer Completion stack was not positioned below Tool Panels");
					return false;
				}

				UPaper2DPlusCharacterLayerAsset* LayerAsset =
					Cast<UPaper2DPlusCharacterLayerAsset>(Target);
				if (!LayerAsset)
				{
					OutReason = TEXT("Layer visual-tour fixture was unavailable");
					return false;
				}

				if (Step.TabId == FCharacterLayerAssetEditorToolkit::ArtTabId)
				{
					SWidget* OverviewWidget = FindWidgetWhoseTypeContains(
						*ActiveContent, TEXT("SLayerOverviewPanel"));
					const float StructureLeft = StructureTab->GetCachedGeometry().LocalToAbsolute(
						FVector2D::ZeroVector).X;
					const float ArtLeft = ActiveContent->GetCachedGeometry().LocalToAbsolute(
						FVector2D::ZeroVector).X;
					if (!OverviewWidget
						|| OverviewWidget->GetCachedGeometry().GetLocalSize().X < 100.0f
						|| StructureLeft >= ArtLeft)
					{
						OutReason = TEXT("Layer Art canvas was not usable beside the persistent Structure dock");
						return false;
					}
				}
				else if (Step.TabId == FCharacterLayerAssetEditorToolkit::AppearanceTabId)
				{
					if (!FindWidgetWhoseTypeContains(*ActiveContent, TEXT("SLayerAppearancePanel"))
						|| !ContainsEffectivelyVisibleText(*ActiveContent, TEXT("Default Appearance"))
						|| !ContainsEffectivelyVisibleText(*ActiveContent, TEXT("Exclusive Group"))
						|| ContainsEffectivelyVisibleText(*ActiveContent, TEXT("Wardrobe"), true))
					{
						OutReason = TEXT("Layer Appearance tab did not expose generic presets, default, and exclusivity guidance");
						return false;
					}
				}
				else if (Step.TabId == FCharacterLayerAssetEditorToolkit::StructureTabId)
				{
					if (!FindWidgetWhoseTypeContains(*ActiveContent, TEXT("SLayerStructureTree")))
					{
						OutReason = TEXT("Layer Structure tab was absent");
						return false;
					}
					// TASK-160: the first-run guidance is split across two deliberate homes. The Structure
					// dock owns the "No layers yet" call to action and is asserted SUBTREE-scoped, but the
					// "Select an animation" prompt lives in the shared current-animation header that only
					// the central tools carry — SpawnTab_Structure deliberately skips WrapMainToolContent
					// ("avoids a duplicate animation header"), so demanding that prompt inside the Structure
					// tab's own content rejected the shipped v13 layout on every run. Assert it WINDOW-scoped
					// instead: on the empty fixture no animation can be selected, so the one visible shared
					// switcher (already counted above) must read its empty-state prompt somewhere in the
					// first-run window. A regression that loses the header entirely still fails the
					// one-visible-SAnimationProfileSwitcher count; one that loses the prompt fails here.
					if (Step.AssetKind == ETourAsset::EmptyLayer
						&& (!LayerAsset->BaseProfile.IsNull() || !LayerAsset->Layers.IsEmpty()
							|| !ContainsEffectivelyVisibleText(
								*ActiveContent, TEXT("No layers yet. Import layered art or add a layer to begin."))
							|| !ContainsEffectivelyVisibleText(
								*HostWindow, TEXT("Select an animation"))))
					{
						OutReason = TEXT("Layer Structure first-run tab did not show actionable empty guidance");
						return false;
					}
				}

				if (Step.AssetKind == ETourAsset::Layer
					&& (LayerAsset->BaseProfile.IsNull() || LayerAsset->Layers.IsEmpty()))
				{
					OutReason = TEXT("Layer populated fixture was not available to the docked tools");
					return false;
				}
			}
			else if (Step.AssetKind == ETourAsset::Effect || Step.AssetKind == ETourAsset::EmptyEffect)
			{
				FEffectProfileAssetEditorToolkit* EffectToolkit =
					static_cast<FEffectProfileAssetEditorToolkit*>(Toolkit);
				const TSharedPtr<SDockTab> LibraryTab = Manager->FindExistingLiveTab(
					FEffectProfileAssetEditorToolkit::LibraryTabId);
				const TSharedPtr<SDockTab> PreviewTab = Manager->FindExistingLiveTab(
					FEffectProfileAssetEditorToolkit::PreviewTabId);
				const TSharedPtr<SDockTab> DetailsTab = Manager->FindExistingLiveTab(
					FEffectProfileAssetEditorToolkit::DetailsTabId);
				if (!LibraryTab.IsValid() || !PreviewTab.IsValid() || !DetailsTab.IsValid()
					|| !(LibraryTab->GetCachedGeometry().GetAbsolutePosition().X
						< PreviewTab->GetCachedGeometry().GetAbsolutePosition().X)
					|| !(PreviewTab->GetCachedGeometry().GetAbsolutePosition().X
						< DetailsTab->GetCachedGeometry().GetAbsolutePosition().X))
				{
					OutReason = TEXT("Effect workspace was not ordered Library / Preview / right Details");
					return false;
				}
				if (FEffectProfileAssetEditorToolkit::BuildDefaultLayout()->ToString().Contains(
					TEXT("Validation"), ESearchCase::IgnoreCase))
				{
					OutReason = TEXT("Effect default layout contained a permanent Validation tab");
					return false;
				}

				SWidget* LibraryWidget = FindWidgetWhoseTypeContains(
					*HostWindow, TEXT("SEffectProfileLibraryPanel"));
				SWidget* PreviewWidget = FindWidgetWhoseTypeContains(
					*HostWindow, TEXT("SEffectProfilePreviewPanel"));
				SWidget* DetailsWidget = FindWidgetWhoseTypeContains(
					*HostWindow, TEXT("SEffectProfileDetailsPanel"));
				if (!LibraryWidget || !PreviewWidget || !DetailsWidget)
				{
					OutReason = TEXT("Effect Library, Preview, or right Details panel was absent");
					return false;
				}
				SEffectProfileLibraryPanel* LibraryPanel =
					static_cast<SEffectProfileLibraryPanel*>(LibraryWidget);
				SEffectProfilePreviewPanel* PreviewPanel =
					static_cast<SEffectProfilePreviewPanel*>(PreviewWidget);
				SEffectProfileDetailsPanel* DetailsPanel =
					static_cast<SEffectProfileDetailsPanel*>(DetailsWidget);
				if (PreviewPanel->CountDescendantWidgetsForTests(TEXT("SSlider")) != 0
					|| PreviewPanel->CountDescendantWidgetsForTests(TEXT("SButton")) != 1
					|| ContainsEffectivelyVisibleText(*PreviewWidget, TEXT("Start"), true)
					|| ContainsEffectivelyVisibleText(*PreviewWidget, TEXT("Stop"), true)
					|| ContainsEffectivelyVisibleText(*PreviewWidget, TEXT("Autoplay"), true))
				{
					OutReason = TEXT("Effect Preview exposed retired slider/transport/autoplay chrome");
					return false;
				}

				if (Step.AssetKind == ETourAsset::EmptyEffect)
				{
					if (!LibraryPanel->IsShowingEmptyStateForTests()
						|| PreviewPanel->HasSelectionForTests()
						|| PreviewPanel->GetFrameStripFrameCountForTests() != 0
						|| DetailsPanel->HasSelectionForTests()
						|| !ContainsEffectivelyVisibleText(
							*LibraryWidget, TEXT("This visual library is empty"))
						|| !ContainsEffectivelyVisibleText(
							*DetailsWidget, TEXT("No effect selected")))
					{
						OutReason = TEXT("Effect first-run asset did not show actionable Library/Details guidance");
						return false;
					}
				}
				else
				{
					if (!EffectToolkit->ValidateDesignerWorkspaceForTests(5, OutReason)
						|| LibraryPanel->IsShowingEmptyStateForTests()
						|| LibraryPanel->GetResultCountForTests() <= 0
						|| LibraryPanel->GenerateRowsForViewportForTests(
							FVector2D(600.0f, 520.0f)) <= 0
						|| !DetailsPanel->HasSelectionForTests())
					{
						if (OutReason.IsEmpty())
						{
							OutReason = TEXT("Effect populated workspace was not usable at this width");
						}
						return false;
					}
				}
			}
			else if (Step.AssetKind == ETourAsset::Combat || Step.AssetKind == ETourAsset::EmptyCombat)
			{
				// v9 Character-workspace grammar: Overview/Score Playground/Combat Lab are sibling central
				// tools that each carry the shared current-attack header, and the contextual attack Details
				// panel sits to their RIGHT so a selection stays inspectable from every tool.
				const TSharedPtr<SDockTab> SetupTab = Manager->FindExistingLiveTab(
					FCombatProfileAssetEditorToolkit::SetupTabId);
				const TSharedPtr<SDockTab> AttackDetailsTab = Manager->FindExistingLiveTab(
					FCombatProfileAssetEditorToolkit::AttackDetailsTabId);
				if (!SetupTab.IsValid() || !AttackDetailsTab.IsValid())
				{
					OutReason = TEXT("Combat Overview tool or contextual attack Details panel was absent");
					return false;
				}
				const TSharedRef<SWidget> SetupContent = SetupTab->GetContent();
				const TSharedRef<SWidget> AttackDetailsContent = AttackDetailsTab->GetContent();
				// Every central tool shares the current-attack header; Details is a right-hand sibling.
				if (CountEffectivelyVisibleWidgetsWhoseTypeContains(
						*ActiveContent, TEXT("SAnimationProfileSwitcher")) != 1
					|| !(ActiveTab->GetCachedGeometry().GetAbsolutePosition().X
						< AttackDetailsTab->GetCachedGeometry().GetAbsolutePosition().X))
				{
					OutReason = TEXT("Combat tool lost its shared current-attack header or right-hand Details panel");
					return false;
				}
				if (Step.AssetKind == ETourAsset::EmptyCombat)
				{
					// First run: the foregrounded Overview tool carries the status line and the empty attack
					// browser (the ONE tile view), while the Details panel explains what selecting does.
					// Retired Attack Catalog remnants (pinned navigator, multi-column facts table) are gone.
					SWidget* SetupWidget = FindWidgetWhoseTypeContains(
						*SetupContent, TEXT("SCombatProfileSetupPanel"));
					SCombatProfileSetupPanel* SetupPanel = SetupWidget
						? static_cast<SCombatProfileSetupPanel*>(SetupWidget) : nullptr;
					UPaper2DPlusCombatProfileAsset* CombatAsset =
						Cast<UPaper2DPlusCombatProfileAsset>(Target);
					if (Step.TabId != FCombatProfileAssetEditorToolkit::SetupTabId
						|| !SetupTab->IsForeground()
						|| !SetupPanel || !CombatAsset || CombatAsset->CharacterProfile
						|| !CombatAsset->AttackOptions.IsEmpty()
						|| !SetupPanel->GetStatusText().ToString().Contains(
							TEXT("Link a Character Profile"), ESearchCase::IgnoreCase)
						|| !ContainsEffectivelyVisibleText(
							*SetupContent, TEXT("No attacks yet"))
						|| CountEffectivelyVisibleWidgetsWhoseTypeContains(
							*SetupContent, TEXT("STileView")) != 1
						|| !ContainsEffectivelyVisibleText(
							*AttackDetailsContent, TEXT("Select an attack to see how it scores"))
						|| CountEffectivelyVisibleWidgetsWhoseTypeContains(
							*SetupContent, TEXT("SProfileNavigatorPanel")) != 0
						|| CountEffectivelyVisibleWidgetsWhoseTypeContains(
							*SetupContent, TEXT("SHeaderRow")) != 0)
					{
						OutReason = TEXT("Combat first-run capture did not show the Overview guidance beside contextual Details");
						return false;
					}
				}
				else
				{
					if (Step.TabId == FCombatProfileAssetEditorToolkit::ScorePreviewTabId)
					{
						SWidget* ScoreWidget = FindWidgetWhoseTypeContains(
							*ActiveContent, TEXT("SCombatScorePlaygroundPanel"));
						SCombatScorePlaygroundPanel* ScorePanel = ScoreWidget
							? static_cast<SCombatScorePlaygroundPanel*>(ScoreWidget) : nullptr;
						if (!ScorePanel || ScorePanel->GetRankedCountForTests() <= 0
							|| !ContainsEffectivelyVisibleText(*ActiveContent, TEXT("Ranked attacks")))
						{
							OutReason = TEXT("Combat Score Playground lost its ranked designer surface");
							return false;
						}
					}
					else if (Step.TabId == FCombatProfileAssetEditorToolkit::CombatLabTabId)
					{
						SWidget* LabWidget = FindWidgetWhoseTypeContains(
							*ActiveContent, TEXT("SCombatLabPanel"));
						SCombatLabPanel* LabPanel = LabWidget
							? static_cast<SCombatLabPanel*>(LabWidget) : nullptr;
						if (!LabPanel || !LabPanel->GetModelForTests().IsValid()
							|| LabPanel->GetStatusText().IsEmpty()
							|| CountWidgetsWhoseTypeContains(*ActiveContent, TEXT("SCombatLabCanvas")) != 1)
						{
							OutReason = TEXT("Combat Lab lost its worldless canvas and advisory status");
							return false;
						}
					}
				}
			}
			else if (Step.AssetKind == ETourAsset::Catalog || Step.AssetKind == ETourAsset::EmptyCatalog)
			{
				FCharacterCatalogAssetEditorToolkit* CatalogToolkit =
					static_cast<FCharacterCatalogAssetEditorToolkit*>(Toolkit);
				const TSharedPtr<SDockTab> DetailsTab = Manager->FindExistingLiveTab(
					FCharacterCatalogAssetEditorToolkit::DetailsTabId);
				const TSharedPtr<SDockTab> WarningsTab = Manager->FindExistingLiveTab(
					FCharacterCatalogAssetEditorToolkit::WarningsTabId);
				SWidget* DetailsWidget = FindWidgetWhoseTypeContains(
					*HostWindow, TEXT("SCharacterCatalogDetailsPanel"));
				SCharacterCatalogDetailsPanel* DetailsPanel = DetailsWidget
					? static_cast<SCharacterCatalogDetailsPanel*>(DetailsWidget) : nullptr;
				if (!DetailsTab.IsValid() || !WarningsTab.IsValid()
					|| !DetailsPanel || !DetailsPanel->HasDetailsSurfaceForTests()
					|| CountWidgetsWhoseTypeContains(*HostWindow, TEXT("SProfileValidationPanel")) != 1
					|| Manager->FindExistingLiveTab(FCharacterCatalogAssetEditorToolkit::AdvancedTabId).IsValid())
				{
					OutReason = TEXT("Catalog docked Details/Warnings or default Advanced tab state was incorrect");
					return false;
				}
				if (Step.AssetKind == ETourAsset::EmptyCatalog)
				{
					SWidget* RosterWidget = FindWidgetWhoseTypeContains(
						*ActiveContent, TEXT("SCharacterCatalogRosterPanel"));
					SCharacterCatalogRosterPanel* RosterPanel = RosterWidget
						? static_cast<SCharacterCatalogRosterPanel*>(RosterWidget) : nullptr;
					if (!RosterPanel || !RosterPanel->HasCardSurfaceForTests()
						|| RosterPanel->GetVisibleCharacterCountForTests() != 0
						|| CountWidgetsWhoseTypeContains(*RosterWidget, TEXT("STileView")) != 1
						|| !ContainsEffectivelyVisibleText(
							*RosterWidget, TEXT("No characters match this view"))
						|| !ContainsEffectivelyVisibleText(
							*DetailsWidget, TEXT("choose Add Characters")))
					{
						OutReason = TEXT("Catalog first-run Roster and docked Details did not show intake guidance");
						return false;
					}
				}
				else if (Step.ViewMode == Tour_CatalogGroupsRailViewMode)
				{
					SWidget* RosterWidget = FindWidgetWhoseTypeContains(
						*ActiveContent, TEXT("SCharacterCatalogRosterPanel"));
					if (!CatalogToolkit->ValidateGroupsRailForTests(Tour_CatalogRailGroup, OutReason)
						|| !RosterWidget
						|| !ContainsEffectivelyVisibleText(*RosterWidget, TEXT("Groups"), /*bExact=*/true))
					{
						if (OutReason.IsEmpty())
						{
							OutReason = TEXT("Catalog Groups rail was not usable at this width");
						}
						return false;
					}
				}
				else if (Step.TabId == FCharacterCatalogAssetEditorToolkit::RosterTabId)
				{
					TArray<FSoftObjectPath> ExpectedAnimatedCharacters;
					ExpectedAnimatedCharacters.Reserve(Fixture->Catalog->Entries.Num());
					for (const FPaper2DPlusCharacterCatalogEntry& Entry : Fixture->Catalog->Entries)
					{
						ExpectedAnimatedCharacters.Add(Entry.CharacterProfile.ToSoftObjectPath());
					}
					if (!CatalogToolkit->ValidateRosterWorkspaceForTests(
						ExpectedAnimatedCharacters, OutReason))
					{
						return false;
					}
					SWidget* RosterWidget = FindWidgetWhoseTypeContains(
						*ActiveContent, TEXT("SCharacterCatalogRosterPanel"));
					SCharacterCatalogRosterPanel* RosterPanel = RosterWidget
						? static_cast<SCharacterCatalogRosterPanel*>(RosterWidget) : nullptr;
					SWidget* TileWidget = RosterWidget
						? FindWidgetWhoseTypeContains(*RosterWidget, TEXT("STileView")) : nullptr;
					const TSharedPtr<SWidget> DetailsFocus = DetailsPanel
						? DetailsPanel->GetStatusFocusTargetForTests() : nullptr;
					if (!RosterPanel || !TileWidget || !DetailsFocus.IsValid()
						|| CountWidgetsWhoseTypeContains(*RosterWidget, TEXT("STileView")) != 1
						|| RosterPanel->GenerateTilesForViewportForTests(
							FVector2D(700.0f, 520.0f)) <= 0
						|| !(TileWidget->GetCachedGeometry().GetAbsolutePosition().X
							< DetailsFocus->GetCachedGeometry().GetAbsolutePosition().X))
					{
						OutReason = TEXT("Catalog virtualized cards or docked Details were not usable");
						return false;
					}
				}
			}
			else if (IsEmptyAssetKind(Step.AssetKind))
			{
				OutReason = TEXT("empty-state asset kind had no semantic validator");
				return false;
			}
			return true;
		}

		TSharedPtr<SWindow> ResolveWindow(UObject* Asset) const
		{
			if (FAssetEditorToolkit* Toolkit = ResolveToolkit(Asset))
			{
				if (TSharedPtr<IToolkitHost> Host = Toolkit->GetToolkitHost())
				{
					if (TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(Host->GetParentWidget()))
					{
						return Window;
					}
				}
				return FSlateApplication::Get().GetActiveTopLevelWindow();
			}
			return nullptr;
		}

		void RestoreWindowSize(UObject* Asset)
		{
			const TWeakObjectPtr<UObject> Key(Asset);
			if (const FVector2D* OriginalSize = OriginalWindowSizes.Find(Key))
			{
				if (TSharedPtr<SWindow> Window = ResolveWindow(Asset)) Window->Resize(*OriginalSize);
			}
		}

		bool ApplyStep(const FTourStep& Step)
		{
			UObject* Target = Fixture.IsValid() ? Fixture->GetAsset(Step.AssetKind) : nullptr;
			UAssetEditorSubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
			if (!Target || !Sub)
			{
				return false;
			}

			if (CurrentAsset.IsValid() && CurrentAsset.Get() != Target)
			{
				RestoreWindowSize(CurrentAsset.Get());
				if (UPaper2DPlusCharacterProfileAsset* CurrentProfile =
					Cast<UPaper2DPlusCharacterProfileAsset>(CurrentAsset.Get()))
				{
					FScopedTourSpriteOffsetCloseSuppression OffsetSuppression(CurrentProfile);
					Sub->CloseAllEditorsForAsset(CurrentProfile);
				}
				else
				{
					Sub->CloseAllEditorsForAsset(CurrentAsset.Get());
				}
				CurrentAsset.Reset();
			}
			if (!ResolveToolkit(Target))
			{
				if (!Sub->OpenEditorForAsset(Target))
				{
					return false;
				}
			}
			FAssetEditorToolkit* Toolkit = ResolveToolkit(Target);
			if (!Toolkit || !Toolkit->GetTabManager().IsValid())
			{
				return false;
			}
			CurrentAsset = Target;
			if (!Toolkit->GetTabManager()->TryInvokeTab(Step.TabId).IsValid())
			{
				return false;
			}

			if (Step.AssetKind == ETourAsset::CharacterMap)
			{
				// The dedicated clone was seeded before its editor opened. Fail readiness instead of falling
				// back to the unsafe hidden->visible live switch if Construct did not restore Map.
				TSharedPtr<SAnimationsPanel> Panel = SAnimationsPanel::GActiveAnimationsPanel.Pin();
				if (!Panel.IsValid() || Panel->GetViewMode() != EAnimationsViewMode::Map)
				{
					return false;
				}
				const TSharedPtr<SDockTab> AnimationsTab =
					Toolkit->GetTabManager()->FindExistingLiveTab(
						FCharacterProfileAssetEditorToolkit::AnimationsTabId);
				SWidget* MapWidget = AnimationsTab.IsValid()
					? FindWidgetWhoseTypeContains(
						*AnimationsTab->GetContent(), TEXT("SAnimationMapPanel"))
					: nullptr;
				if (!MapWidget)
				{
					return false;
				}
				// Exercise the panel's public automation/console seam after its normal visible Construct path.
				// The landing board is useful navigation, but the release screenshot must prove the graph itself.
				SAnimationMapPanel* MapPanel = static_cast<SAnimationMapPanel*>(MapWidget);
				MapPanel->OpenAnimationMapView(TEXT("Combo"));
				if (!MapPanel->SynchronizeAndFrameScopedGraphForAutomation())
				{
					return false;
				}
			}
			else if (Step.AssetKind == ETourAsset::Character)
			{
				if (TSharedPtr<FCharacterProfileEditorModel> Model = FCharacterProfileEditorModel::GActiveEditorModel.Pin())
				{
					Model->SetSelectedFlipbook(0);
				}
				if (Step.ViewMode >= 0)
				{
					TSharedPtr<SAnimationsPanel> Panel = SAnimationsPanel::GActiveAnimationsPanel.Pin();
					if (!Panel.IsValid())
					{
						return false;
					}
					Panel->SetViewMode(Step.ViewMode == 1 ? EAnimationsViewMode::Map : EAnimationsViewMode::List);
				}
			}
			else if (Step.AssetKind == ETourAsset::Catalog)
			{
				// The Groups rail lives inside the Roster, so a rail screen is a Roster step that scopes the
				// grid through the panel's production selection path; the plain Roster screens reset to All
				// Characters. Fail readiness rather than capture a grid scoped to the wrong thing.
				const TSharedPtr<SDockTab> RosterTab = Toolkit->GetTabManager()->FindExistingLiveTab(
					FCharacterCatalogAssetEditorToolkit::RosterTabId);
				SWidget* RosterWidget = RosterTab.IsValid()
					? FindWidgetWhoseTypeContains(
						*RosterTab->GetContent(), TEXT("SCharacterCatalogRosterPanel"))
					: nullptr;
				if (!RosterWidget)
				{
					return false;
				}
				SCharacterCatalogRosterPanel* RosterPanel =
					static_cast<SCharacterCatalogRosterPanel*>(RosterWidget);
				FName ScopeGroup = NAME_None;
				if (Step.ViewMode == Tour_CatalogGroupsRailViewMode)
				{
					ScopeGroup = Tour_CatalogRailGroup;
				}
				if (!RosterPanel->SelectGroupRail(ScopeGroup))
				{
					return false;
				}
			}
			TSharedPtr<SWindow> Window = ResolveWindow(Target);
			if (!Window.IsValid())
			{
				return false;
			}
			const TWeakObjectPtr<UObject> WeakTarget(Target);
			if (!OriginalWindowSizes.Contains(WeakTarget))
			{
				OriginalWindowSizes.Add(WeakTarget, Window->GetSizeInScreen());
			}
			Window->Resize(Step.WindowSize);
			KeepAwakeRegisteredWindows.RemoveAll([](const TWeakPtr<SWindow>& WeakWindow)
			{
				return !WeakWindow.IsValid();
			});
			const bool bWindowAlreadyKeptAwake = KeepAwakeRegisteredWindows.ContainsByPredicate(
				[&Window](const TWeakPtr<SWindow>& WeakWindow)
				{
					return WeakWindow.Pin() == Window;
				});
			if (!bWindowAlreadyKeptAwake)
			{
				Window->RegisterActiveTimer(0.0f,
					FWidgetActiveTimerDelegate::CreateSP(this, &FVisualTourRunner::OnKeepAwakeTimer));
				KeepAwakeRegisteredWindows.Add(Window);
			}
			return true;
		}

		void Capture(const FTourStep& Step)
		{
			FString SemanticReason;
			const bool bSemanticPassed = ValidateSemanticStructure(Step, SemanticReason);
			if (bSemanticPassed)
			{
				++SemanticPassedCount;
				if (WatchdogState.IsValid())
				{
					WatchdogState->SemanticPassedCount.Store(SemanticPassedCount);
				}
			}
			else
			{
				FailedLabels.AddUnique(Step.Label);
				UE_LOG(LogTemp, Warning, TEXT("[VisualTour] %s semantic check FAILED: %s"),
					*Step.Label, *SemanticReason);
			}

			UObject* Target = Fixture.IsValid() ? Fixture->GetAsset(Step.AssetKind) : nullptr;
			TSharedPtr<SWindow> Win = ResolveWindow(Target);
			if (!Win.IsValid())
			{
				RecordFailure(Step, TEXT("no asset-editor window"));
				return;
			}

			FSlateApplication::Get().ForceRedrawWindow(Win.ToSharedRef());

			TArray<FColor> Bitmap;
			FIntVector Size;
			const bool bOk = FSlateApplication::Get().TakeScreenshot(Win.ToSharedRef(), Bitmap, Size);
			if (!bOk || Bitmap.Num() == 0 || Size.X <= 0 || Size.Y <= 0)
			{
				RecordFailure(Step, TEXT("Slate screenshot readback failed"));
				return;
			}

			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Size.X, Size.Y,
				TArrayView64<const FColor>(Bitmap.GetData(), Bitmap.Num()), Png);

			const FString File = OutDir / (Step.Label + TEXT(".png"));
			if (FFileHelper::SaveArrayToFile(TArrayView<const uint8>(Png.GetData(), Png.Num()), *File))
			{
				++SavedCount;
				if (WatchdogState.IsValid())
				{
					WatchdogState->SavedCount.Store(SavedCount);
				}
				CaptureRows.Add(FString::Printf(
					TEXT("CAPTURED\t%s\t%s\t%dx%d%s%s"),
					bSemanticPassed ? TEXT("PASS") : TEXT("FAIL"),
					*Step.Label,
					Size.X,
					Size.Y,
					bSemanticPassed ? TEXT("") : TEXT("\t"),
					bSemanticPassed ? TEXT("") : *SemanticReason));
				UE_LOG(LogTemp, Display, TEXT("[VisualTour] saved %s (%dx%d)"), *File, Size.X, Size.Y);
			}
			else
			{
				RecordFailure(Step, TEXT("PNG save failed"));
			}
		}

		void RecordFailure(const FTourStep& Step, const FString& Reason)
		{
			if (!FailedLabels.Contains(Step.Label))
			{
				FailedLabels.Add(Step.Label);
				CaptureRows.Add(FString::Printf(TEXT("FAILED\tFAIL\t%s\t%s"), *Step.Label, *Reason));
			}
			UE_LOG(LogTemp, Warning, TEXT("[VisualTour] %s FAILED: %s"), *Step.Label, *Reason);
		}

		void WriteManifest() const
		{
			const bool bPassed = FailedLabels.IsEmpty()
				&& SavedCount == Steps.Num()
				&& SemanticPassedCount == Steps.Num()
				&& bCleanupComplete
				&& bPreferencesRestored;
			FString Manifest = FString::Printf(
				TEXT("Paper2DPlus Designer Visual Tour\nresult=%s\nengine=%s\nexpected=%d\nsaved=%d\ncaptured=%d\nsemantic_passed=%d\nmap=%s\ncleanup=%s\npreferences_restored=%s\noutput=%s\n\n"),
				bPassed ? TEXT("PASS") : TEXT("FAIL"),
				*FEngineVersion::Current().ToString(),
				Steps.Num(),
				SavedCount,
				SavedCount,
				SemanticPassedCount,
				bIncludeMap ? TEXT("included") : TEXT("diagnostically skipped"),
				bCleanupComplete ? TEXT("complete") : TEXT("incomplete"),
				bPreferencesRestored ? TEXT("true") : TEXT("false"),
				*OutDir);
			Manifest += TEXT("capture\tsemantic\tlabel\tsize-or-reason\n");
			for (const FString& Row : CaptureRows)
			{
				Manifest += Row + LINE_TERMINATOR;
			}
			const FString ManifestPath = OutDir / TEXT("00-visual-tour-manifest.txt");
			if (!FFileHelper::SaveStringToFile(Manifest, *ManifestPath))
			{
				UE_LOG(LogTemp, Warning, TEXT("[VisualTour] manifest save FAILED: %s"), *ManifestPath);
			}
		}

		void Finish()
		{
			bFinished = true; // stops the keep-awake active timer on its next fire

			UAssetEditorSubsystem* AssetEditorSubsystem = nullptr;
			if (GEditor && Fixture.IsValid())
			{
				AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
				if (AssetEditorSubsystem)
				{
					for (ETourAsset Kind : { ETourAsset::Character, ETourAsset::CharacterMap, ETourAsset::Layer,
						ETourAsset::Effect, ETourAsset::Combat, ETourAsset::Catalog, ETourAsset::EmptyLayer,
						ETourAsset::EmptyEffect, ETourAsset::EmptyCombat, ETourAsset::EmptyCatalog })
					{
						if (UObject* Asset = Fixture->GetAsset(Kind))
						{
							RestoreWindowSize(Asset);
							if (UPaper2DPlusCharacterProfileAsset* Profile =
								Cast<UPaper2DPlusCharacterProfileAsset>(Asset))
							{
								FScopedTourSpriteOffsetCloseSuppression OffsetSuppression(Profile);
								AssetEditorSubsystem->CloseAllEditorsForAsset(Profile);
							}
							else
							{
								AssetEditorSubsystem->CloseAllEditorsForAsset(Asset);
							}
						}
					}
				}
			}
			bCleanupComplete = AssetEditorSubsystem && Fixture.IsValid();
			if (bCleanupComplete)
			{
				for (ETourAsset Kind : { ETourAsset::Character, ETourAsset::CharacterMap, ETourAsset::Layer,
					ETourAsset::Effect, ETourAsset::Combat, ETourAsset::Catalog, ETourAsset::EmptyLayer,
					ETourAsset::EmptyEffect, ETourAsset::EmptyCombat, ETourAsset::EmptyCatalog })
				{
					if (UObject* Asset = Fixture->GetAsset(Kind))
					{
						if (AssetEditorSubsystem->FindEditorForAsset(Asset, /*bFocusIfOpen*/ false))
						{
							bCleanupComplete = false;
							break;
						}
					}
				}
			}
			if (!bCleanupComplete)
			{
				FailedLabels.Add(TEXT("editor-cleanup"));
				CaptureRows.Add(TEXT("FAILED\tFAIL\teditor-cleanup\tone or more tour asset editors remained live"));
				UE_LOG(LogTemp, Error, TEXT("[VisualTour] editor cleanup verification FAILED"));
			}
			if (Fixture.IsValid())
			{
				bPreferencesRestored = Fixture->Release(/*bReleaseRoots*/ bCleanupComplete);
				if (!bPreferencesRestored)
				{
					FailedLabels.Add(TEXT("preference-restore"));
					CaptureRows.Add(TEXT("FAILED\tFAIL\tpreference-restore\tglobal editor preferences were not restored exactly"));
					UE_LOG(LogTemp, Error, TEXT("[VisualTour] global editor preference restoration FAILED"));
				}
				Fixture.Reset();
			}
			CurrentAsset.Reset();
			UE_LOG(LogTemp, Display,
				TEXT("[VisualTour] complete: %d/%d screenshots, %d/%d semantic passes, cleanup=%s, %d failure(s), in %s"),
				SavedCount, Steps.Num(), SemanticPassedCount, Steps.Num(),
				bCleanupComplete ? TEXT("complete") : TEXT("incomplete"), FailedLabels.Num(), *OutDir);
			// PASS is a post-cleanup completion marker. The watchdog remains armed during editor teardown
			// and can publish only FAIL/cleanup=incomplete if cleanup stalls or crashes first.
			WriteManifest();
			if (WatchdogState.IsValid())
			{
				// Disarm only after manifest + editor cleanup. A cleanup-time synchronous stall remains a
				// failed unattended tour and must still trip the independent wall-clock watchdog.
				WatchdogState->bCompleted.Store(true);
			}

			// Drop the last strong ref NEXT frame — we're inside Tick() on this object's stack right now.
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
			{
				FVisualTourRunner::GActive.Reset();
				return false;
			}), 0.f);

			if (bExitWhenDone)
			{
				UE_LOG(LogTemp, Display, TEXT("[VisualTour] requesting editor exit (unattended)."));
				FPlatformMisc::RequestExit(/*Force*/ false);
			}
		}

		void PulseWatchdog()
		{
			if (WatchdogState.IsValid())
			{
				WatchdogState->Heartbeat.Store(++WatchdogHeartbeat);
			}
		}

		void ArmWatchdog()
		{
			if (!bExitWhenDone)
			{
				return; // never force-close a designer's interactive editor session
			}

			constexpr double NoProgressTimeoutSeconds = 45.0;
			constexpr float PollSeconds = 0.25f;
			WatchdogState = MakeShared<FVisualTourWatchdogState, ESPMode::ThreadSafe>();
			const TSharedPtr<FVisualTourWatchdogState, ESPMode::ThreadSafe> State = WatchdogState;
			const FString ManifestPath = OutDir / TEXT("00-visual-tour-manifest.txt");
			const FString EngineVersion = FEngineVersion::Current().ToString();
			const int32 ExpectedCount = Steps.Num();
			const bool bMapWasIncluded = bIncludeMap;
			TArray<FString> StepLabels;
			StepLabels.Reserve(Steps.Num());
			for (const FTourStep& Step : Steps)
			{
				StepLabels.Add(Step.Label);
			}

			WatchdogFuture = Async(EAsyncExecution::Thread,
				[State, ManifestPath, EngineVersion, ExpectedCount, bMapWasIncluded,
					NoProgressTimeoutSeconds, PollSeconds, StepLabels = MoveTemp(StepLabels)]()
				{
					uint64 LastHeartbeat = State->Heartbeat.Load();
					double LastProgressSeconds = FPlatformTime::Seconds();
					while (!State->bCompleted.Load())
					{
						FPlatformProcess::Sleep(PollSeconds);
						if (State->bCompleted.Load())
						{
							return;
						}

						const uint64 CurrentHeartbeat = State->Heartbeat.Load();
						const double NowSeconds = FPlatformTime::Seconds();
						if (CurrentHeartbeat != LastHeartbeat)
						{
							LastHeartbeat = CurrentHeartbeat;
							LastProgressSeconds = NowSeconds;
							continue;
						}
						if (NowSeconds - LastProgressSeconds < NoProgressTimeoutSeconds)
						{
							continue;
						}

						const int32 FrozenStepIndex = State->StepIndex.Load();
						const FString FrozenLabel = StepLabels.IsValidIndex(FrozenStepIndex)
							? StepLabels[FrozenStepIndex]
							: TEXT("unknown-step");
						const FString Reason = FString::Printf(
							TEXT("no core-ticker heartbeat for %.0f wall-clock seconds at step %d/%d"),
							NoProgressTimeoutSeconds,
							FrozenStepIndex + 1,
							ExpectedCount);
						FString Manifest = FString::Printf(
							TEXT("Paper2DPlus Designer Visual Tour\nresult=FAIL\nengine=%s\nexpected=%d\nsaved=%d\ncaptured=%d\nsemantic_passed=%d\nmap=%s\ncleanup=incomplete\npreferences_restored=false\nwatchdog=%s\n\ncapture\tsemantic\tlabel\tsize-or-reason\nFAILED\tFAIL\t%s\t%s\n"),
							*EngineVersion,
							ExpectedCount,
							State->SavedCount.Load(),
							State->SavedCount.Load(),
							State->SemanticPassedCount.Load(),
							bMapWasIncluded ? TEXT("included") : TEXT("diagnostically skipped"),
							*Reason,
							*FrozenLabel,
							*Reason);
						FFileHelper::SaveStringToFile(Manifest, *ManifestPath);
						FPlatformMisc::RequestExitWithStatus(/*Force*/ true, /*ReturnCode*/ 2);
						return;
					}
				});
		}
	};

	TSharedPtr<FVisualTourRunner> FVisualTourRunner::GActive = nullptr;

	// Warmup before opening the asset-editor window. -ExecCmds fires the command during EARLY editor init;
	// opening a NEW top-level window at that moment can deadlock the first frame under -RenderOffScreen
	// (observed: game thread frozen at frame 1, ticker never fires). Deferring the heavy work by a few
	// seconds lets the editor come fully up + render steadily first, which makes the open reliable.
	constexpr float Tour_WarmupSeconds = 4.0f;

	// Set between schedule and runner-creation (GActive is null during the warmup window, so it alone can't
	// guard against a double-schedule).
	bool GTourScheduled = false;

	void Tour_StartNow(const FString& InOutDir, bool bIncludeMap)
	{
		GTourScheduled = false;
		if (!GEditor || FVisualTourRunner::GActive.IsValid())
		{
			return;
		}

		TSharedPtr<FVisualTourFixture> Fixture = Tour_BuildFixture();
		if (!Fixture.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[VisualTour] failed to build the related transient fixture."));
			if (FApp::IsUnattended()) FPlatformMisc::RequestExit(/*Force*/ false);
			return;
		}

		TSharedRef<FVisualTourRunner> Runner = MakeShared<FVisualTourRunner>();
		Runner->Fixture = Fixture;
		Runner->OutDir = FPaths::ConvertRelativePathToFull(
			InOutDir.IsEmpty() ? (FPaths::ProjectSavedDir() / TEXT("VisualTour")) : InOutDir);
		Runner->bExitWhenDone = FApp::IsUnattended();
		Runner->bIncludeMap = bIncludeMap;
		FVisualTourRunner::GActive = Runner;
		Runner->Start();
	}

	void Tour_Schedule(const FString& InOutDir, bool bIncludeMap)
	{
		if (!FApp::CanEverRender())
		{
			UE_LOG(LogTemp, Warning, TEXT("[VisualTour] needs a rendering RHI — skipped (running under -nullrhi?). Launch with -RenderOffScreen."));
			if (FApp::IsUnattended()) FPlatformMisc::RequestExit(/*Force*/ false);
			return;
		}
		if (GTourScheduled || FVisualTourRunner::GActive.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[VisualTour] already scheduled/running."));
			return;
		}
		GTourScheduled = true;
		UE_LOG(LogTemp, Display, TEXT("[VisualTour] scheduled — warming up %.1fs before opening the editor."), Tour_WarmupSeconds);

		const FString OutDir = InOutDir;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OutDir, bIncludeMap](float)
		{
			Tour_StartNow(OutDir, bIncludeMap);
			return false; // one-shot
		}), Tour_WarmupSeconds);
	}

	FAutoConsoleCommand GVisualTourCommand(
		TEXT("Paper2DPlus.VisualTour"),
		TEXT("Build populated plus empty Character/Layer/Effect/Combat/Catalog fixtures and capture the actual "
		     "responsive designer workspaces to [outDir] (default <ProjectSaved>/VisualTour). Needs "
		     "-RenderOffScreen, not -nullrhi. Animations Map is included by default; optional NoMap is diagnostic-only."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			FString OutDir;
			bool bIncludeMap = true;
			for (const FString& Arg : Args)
			{
				if (Arg.Equals(TEXT("NoMap"), ESearchCase::IgnoreCase))
				{
					bIncludeMap = false;
				}
				else if (OutDir.IsEmpty())
				{
					OutDir = Arg;
				}
			}
			if (OutDir.IsEmpty())
			{
				// The unattended wrapper passes its per-run path separately so spaces in the project
				// path cannot be split by console-command argument parsing.
				FParse::Value(
					FCommandLine::Get(),
					TEXT("Paper2DPlusVisualTourOut="),
					OutDir);
			}
			Tour_Schedule(OutDir, bIncludeMap);
		}));
}
