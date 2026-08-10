// Copyright 2026 Infinite Gameworks. All Rights Reserved.

/**
 * SAnimationMapPanel — the "Animation Map" tab (combo-graph plan U4 surface + U5 gestures).
 *
 * A fully TRANSIENT UEdGraph projection over the asset's flat FFlipbookTransitionData, hosted in an
 * SGraphEditor. The flat data is the single source of truth; every external change reconciles the
 * projection through the engine described in AnimationMapPanel.h (full-diff early-out -> identity-
 * preserving move/edge reconciliation). EDITABLE since U5 — the gesture funnel lives here: wire create
 * (HandleWireCreateRequested, inside the engine's GraphEd_CreateConnection transaction), the Delete
 * command (DeleteSelectedNodes — THE single row-deletion path), drag-drop placement (the panel's own
 * OnDragOver/OnDrop as the thin outer drop target), and node-move persistence (HandleNodeMoveCommitted
 * via the graph's OnNodeMoveCommitted delegate). The suicide hook stays cleanup-only.
 *
 * Also home to the file-scope `Paper2DPlus.AnimationMapGraphProbe` console command (ECABridge/headless
 * verification) and the GEditorPerProjectIni zoom/view persistence.
 */

#include "AnimationMapPanel.h"

#include "AnimationTagChipUtils.h" // GetTagLeafString — the ONE shared cross-version tag-leaf helper

#include "CharacterProfileAssetEditor.h"
#include "DestructiveActionUtils.h"
#include "SSpriteEditorDragDropWidgets.h"
#include "CharacterProfileEditorModel.h"
#include "Paper2DPlusSettings.h" // Tag Colors registry — group tile accent + in-editor color swatch
#include "ISettingsModule.h"      // Paper2DPlus.OpenSettings console seam
#include "Widgets/Colors/SColorBlock.h"  // group-tile color swatch
#include "Widgets/Colors/SColorPicker.h" // in-editor tag color picker
#include "AnimationMap/Paper2DPlusAnimationMap.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_ChainEnd.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_ChainStart.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_Move.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_Transition.h"
#include "AnimationMap/SAnimationMapMoveNode.h"
#include "EdGraphNode_Comment.h"
#include "UObject/UObjectGlobals.h" // FCoreUObjectDelegates
#include "UObject/UnrealType.h" // FPropertyChangedEvent; explicit for per-version PCH drift
#include "AnimationMap/Paper2DPlusAnimationMapSchema.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h" // live tag-tree enumeration for the migration dialog's group options
// Also the Map filter's cross-version picker seam: IGameplayTagsEditorModule::MakeGameplayTagWidget is
// available unchanged on UE 5.0-5.8, unlike the 5.3+ SGameplayTagCombo / SGameplayTagPicker widgets.
#include "GameplayTagsEditorModule.h" // Map filter's cross-version picker seam
#include "Misc/App.h" // FApp::IsUnattended — automatic modals are forbidden in unattended editor runs
// SGameplayTagPicker was added in UE 5.3 (mirrors ProfileDetailsPanel's guard). On older engines the
// multi-selection cohort's tag menus degrade to a read-only note — drag-drop into a group stays the
// assign path, and the Map filter uses the module's cross-version widget instead.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
#include "SGameplayTagPicker.h"
#endif
#include "Brushes/SlateRoundedBoxBrush.h" // TASK-151 U3: the active-filter chip pill
#include "Framework/MultiBox/MultiBoxBuilder.h" // TASK-151 U3: the Map overflow menu
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "SGraphPanel.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h" // ETextOverflowPolicy — the rail elides instead of wrapping
#include "UObject/Package.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#include <initializer_list>

#define LOCTEXT_NAMESPACE "AnimationMapPanel"

namespace DestructiveActions = Paper2DPlusEditor::DestructiveActionUtils;

// =====================================================================================================
// File-scope panel registry + probe console command (ECABridge/headless verification seam)
// =====================================================================================================

namespace
{
	/** Last-constructed panel wins — mirrors FCharacterProfileEditorModel::GActiveEditorModel semantics.
	 *  TWeakPtr auto-invalidates on panel destruction, so no destructor bookkeeping is needed. */
	TWeakPtr<SAnimationMapPanel> GActiveAnimationMapPanel;

	/** First case-insensitive entry match — the FIRST-MATCH contract shared with the projection core
	 *  (AnimationMapCore.cpp). Deliberately NOT FindFlipbookDataPtr: the asset's name-lookup cache is
	 *  last-entry-wins on duplicate names (RebuildNameLookupCache uses TMap::Add in a forward loop),
	 *  which would diverge from ProjectGraph's first-entry rows. File-unique name per the unity rule. */
	const FFlipbookProfileEntry* AnimationMapPanel_FindEntry(const UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName)
	{
		if (!Asset || MoveName.IsEmpty())
		{
			return nullptr;
		}
		for (const FFlipbookProfileEntry& Anim : Asset->Flipbooks)
		{
			if (Anim.Identity.FlipbookName.Equals(MoveName, ESearchCase::IgnoreCase))
			{
				return &Anim;
			}
		}
		return nullptr;
	}

	/** Mutable first-match lookup for the strip's write paths (same contract as the const variant). */
	FFlipbookProfileEntry* AnimationMapPanel_FindEntryMutable(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName)
	{
		return const_cast<FFlipbookProfileEntry*>(AnimationMapPanel_FindEntry(Asset, MoveName));
	}

	/** The flipbook OBJECT a move node currently resolves to (no load forced — reconcile's DIFF must
	 *  stay cheap). Null for stubs/unloaded/missing. */
	UPaperFlipbook* AnimationMapPanel_ResolveFlipbook(const UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName)
	{
		const FFlipbookProfileEntry* Entry = AnimationMapPanel_FindEntry(Asset, MoveName);
		return Entry ? Entry->Identity.Flipbook.Get() : nullptr;
	}

	/** U6 DISPLAY resolve (the ResolvedFlipbook stamping source): like the cheap variant, but pays a
	 *  one-time LoadSynchronous when the soft ptr names an unloaded asset — the transitions-row idiom
	 *  (ProfileDetailsPanel.cpp:2493-2506). After the first load, Get() returns the same object, so
	 *  the diff bookkeeping and this stamp converge on one pointer. Null soft ptrs stay cheap. */
	UPaperFlipbook* AnimationMapPanel_ResolveFlipbookForDisplay(const UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName)
	{
		const FFlipbookProfileEntry* Entry = AnimationMapPanel_FindEntry(Asset, MoveName);
		if (!Entry)
		{
			return nullptr;
		}
		if (UPaperFlipbook* Loaded = Entry->Identity.Flipbook.Get())
		{
			return Loaded;
		}
		return Entry->Identity.Flipbook.LoadSynchronous();
	}

	// --- Chain Start marker helpers (chain-start rework) ---

	/** The synthetic marker-position key. The "__chainstart__:" prefix guarantees it can never collide
	 *  with a real lowered move name (moves come from FlipbookName strings; the projection's placed-key
	 *  path never sees this map — it is PANEL-owned, see ChainStartMarkerPositions in the header). */
	FString AnimationMapPanel_ChainStartPositionKey(const FString& TargetMoveLower)
	{
		return FString::Printf(TEXT("__chainstart__:%s"), *TargetMoveLower);
	}

	/** The Chain End marker's synthetic position key (the mirror of the helper above — same
	 *  can-never-collide-with-a-move-name rationale; the map is PANEL-owned, see
	 *  ChainEndMarkerPositions in the header). */
	FString AnimationMapPanel_ChainEndPositionKey(const FString& TargetMoveLower)
	{
		return FString::Printf(TEXT("__chainend__:%s"), *TargetMoveLower);
	}

	/** Index of MoveName's one case-insensitive entry inside the GroupTag mapping's Entries, or
	 *  INDEX_NONE when absent OR duplicated — the address SetTagMappingEntryChainStart /
	 *  SetTagMappingEntryChainEnd take (one resolver, both marker kinds). A duplicate is invalid
	 *  authoring data, so a marker gesture must never silently designate its first row. */
	int32 AnimationMapPanel_FindGroupEntryIndex(const UPaper2DPlusCharacterProfileAsset* Asset,
		const FGameplayTag& GroupTag, const FString& MoveName)
	{
		if (!Asset || !GroupTag.IsValid() || MoveName.IsEmpty())
		{
			return INDEX_NONE;
		}
		const FFlipbookTagMapping* Mapping = Asset->TagMappings.Find(GroupTag);
		if (!Mapping)
		{
			return INDEX_NONE;
		}
		int32 MatchIndex = INDEX_NONE;
		for (int32 EntryIndex = 0; EntryIndex < Mapping->Entries.Num(); ++EntryIndex)
		{
			if (Mapping->Entries[EntryIndex].FlipbookName.Equals(MoveName, ESearchCase::IgnoreCase))
			{
				if (MatchIndex != INDEX_NONE)
				{
					return INDEX_NONE;
				}
				MatchIndex = EntryIndex;
			}
		}
		return MatchIndex;
	}

	/** The refusal-toast pattern (the duplicate-wire refusal's shape) — one-liner for the chain-start
	 *  AND chain-end marker gesture refusals (one helper, both marker kinds). */
	void AnimationMapPanel_ChainStartRefusalToast(const FText& Message)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	// Group-tag candidates for the migration dialog: the DIRECT children of `Paper2DPlus.Animation`
	// in the LIVE tag tree, so designer-added tags surface without a code change (legacy-cleanup
	// 2026-07 — replaces the old hardcoded 7-tag seed list, which also forced empty default groups
	// onto the map board; the board/lens group set now derives purely from non-empty TagMappings).
	TArray<FGameplayTag> AnimationMapPanel_GetAnimationGroupTagOptions()
	{
		TArray<FGameplayTag> Tags;
		const FGameplayTag Root = FGameplayTag::RequestGameplayTag(
			FName(TEXT("Paper2DPlus.Animation")), /*ErrorIfNotFound=*/false);
		if (!Root.IsValid())
		{
			return Tags;
		}

		if (const TSharedPtr<FGameplayTagNode> RootNode = UGameplayTagsManager::Get().FindTagNode(Root))
		{
			for (const TSharedPtr<FGameplayTagNode>& Child : RootNode->GetChildTagNodes())
			{
				if (Child.IsValid() && Child->GetCompleteTag().IsValid())
				{
					Tags.Add(Child->GetCompleteTag());
				}
			}
		}
		Tags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.ToString() < B.ToString();
		});
		return Tags;
	}

	struct FAnimationMapMigrationRow
	{
		FName SourceGroup;
		int32 FlipbookCount = 0;
		TSharedPtr<FString> SelectedTagOption;
	};

	static const FString& AnimationMapPanel_MigrationSkipOption()
	{
		static const FString SkipOption = TEXT("(skip)");
		return SkipOption;
	}

	bool AnimationMapPanel_IsSkipMigrationOption(const FString& Option)
	{
		return Option.Equals(AnimationMapPanel_MigrationSkipOption(), ESearchCase::IgnoreCase);
	}

	TSharedPtr<FString> AnimationMapPanel_FindStringOption(
		const TArray<TSharedPtr<FString>>& Options, const FString& Value)
	{
		for (const TSharedPtr<FString>& Option : Options)
		{
			if (Option.IsValid() && Option->Equals(Value, ESearchCase::IgnoreCase))
			{
				return Option;
			}
		}
		return nullptr;
	}

	void AnimationMapPanel_AddTagOption(TArray<TSharedPtr<FString>>& Options, FGameplayTag Tag)
	{
		if (!Tag.IsValid())
		{
			return;
		}

		const FString TagString = Tag.ToString();
		if (!AnimationMapPanel_FindStringOption(Options, TagString).IsValid())
		{
			Options.Add(MakeShared<FString>(TagString));
		}
	}

	TArray<TSharedPtr<FString>> AnimationMapPanel_BuildMigrationTagOptions(const UPaper2DPlusCharacterProfileAsset* Asset)
	{
		TArray<TSharedPtr<FString>> Options;
		Options.Add(MakeShared<FString>(AnimationMapPanel_MigrationSkipOption()));

		for (const FGameplayTag& Tag : AnimationMapPanel_GetAnimationGroupTagOptions())
		{
			AnimationMapPanel_AddTagOption(Options, Tag);
		}

		if (Asset)
		{
			TArray<FGameplayTag> AssetTags;
			Asset->TagMappings.GetKeys(AssetTags);
			AssetTags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
			{
				return A.ToString() < B.ToString();
			});
			for (const FGameplayTag& Tag : AssetTags)
			{
				AnimationMapPanel_AddTagOption(Options, Tag);
			}

			for (const FFlipbookGroupInfo& Group : Asset->FlipbookGroups)
			{
				FGameplayTag GroupTag;
				if (Asset->IsTagBackedFlipbookGroup(Group.GroupName, GroupTag))
				{
					AnimationMapPanel_AddTagOption(Options, GroupTag);
				}
			}
		}

		return Options;
	}

	FString AnimationMapPanel_InferMigrationTagString(const UPaper2DPlusCharacterProfileAsset* Asset, FName SourceGroup)
	{
		FGameplayTag ExistingTag;
		if (Asset && Asset->IsTagBackedFlipbookGroup(SourceGroup, ExistingTag))
		{
			return ExistingTag.ToString();
		}

		const FString LowerName = SourceGroup.ToString().ToLower();
		auto HasAny = [&LowerName](std::initializer_list<const TCHAR*> Terms)
		{
			for (const TCHAR* Term : Terms)
			{
				if (LowerName.Contains(Term))
				{
					return true;
				}
			}
			return false;
		};

		if (HasAny({ TEXT("attack"), TEXT("combat"), TEXT("slash"), TEXT("punch"), TEXT("kick"), TEXT("shoot"), TEXT("weapon") }))
		{
			return TEXT("Paper2DPlus.Animation.Combat");
		}
		if (HasAny({ TEXT("idle"), TEXT("walk"), TEXT("run"), TEXT("move"), TEXT("jump"), TEXT("fall"), TEXT("land"), TEXT("dash"), TEXT("crouch") }))
		{
			return TEXT("Paper2DPlus.Animation.Locomotion");
		}
		if (HasAny({ TEXT("hit"), TEXT("hurt"), TEXT("damage"), TEXT("block"), TEXT("parry"), TEXT("stun"), TEXT("knock") }))
		{
			return TEXT("Paper2DPlus.Animation.Reaction");
		}
		if (HasAny({ TEXT("death"), TEXT("dead"), TEXT("die"), TEXT("ko"), TEXT("spawn"), TEXT("revive") }))
		{
			return TEXT("Paper2DPlus.Animation.Lifecycle");
		}
		if (HasAny({ TEXT("potion"), TEXT("drink"), TEXT("interact"), TEXT("use"), TEXT("open"), TEXT("talk"), TEXT("pickup") }))
		{
			return TEXT("Paper2DPlus.Animation.Interaction");
		}
		if (HasAny({ TEXT("ability"), TEXT("spell"), TEXT("cast"), TEXT("magic"), TEXT("skill"), TEXT("special") }))
		{
			return TEXT("Paper2DPlus.Animation.Ability");
		}
		if (HasAny({ TEXT("taunt"), TEXT("emote"), TEXT("celebrate"), TEXT("flavor"), TEXT("gesture") }))
		{
			return TEXT("Paper2DPlus.Animation.Flavor");
		}

		return AnimationMapPanel_MigrationSkipOption();
	}

	void AnimationMapPanel_BuildMigrationRows(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const TArray<TSharedPtr<FString>>& TagOptions,
		TArray<TSharedPtr<FAnimationMapMigrationRow>>& OutRows)
	{
		OutRows.Reset();
		if (!Asset)
		{
			return;
		}

		TArray<const FFlipbookGroupInfo*> Groups;
		for (const FFlipbookGroupInfo& Group : Asset->FlipbookGroups)
		{
			Groups.Add(&Group);
		}
		Groups.Sort([](const FFlipbookGroupInfo& A, const FFlipbookGroupInfo& B)
		{
			return A.GroupName.LexicalLess(B.GroupName);
		});

		for (const FFlipbookGroupInfo* Group : Groups)
		{
			const FString InferredTag = AnimationMapPanel_InferMigrationTagString(Asset, Group->GroupName);
			TSharedPtr<FString> Selected = AnimationMapPanel_FindStringOption(TagOptions, InferredTag);
			if (!Selected.IsValid())
			{
				Selected = TagOptions.Num() > 0 ? TagOptions[0] : nullptr;
			}

			TSharedPtr<FAnimationMapMigrationRow> Row = MakeShared<FAnimationMapMigrationRow>();
			Row->SourceGroup = Group->GroupName;
			Row->FlipbookCount = Asset->GetFlipbookIndicesForFlipbookGroup(Group->GroupName).Num();
			Row->SelectedTagOption = Selected;
			OutRows.Add(Row);
		}
	}

	bool AnimationMapPanel_HasNonTagOverviewGroups(const UPaper2DPlusCharacterProfileAsset* Asset)
	{
		if (!Asset)
		{
			return false;
		}

		for (const FFlipbookGroupInfo& Group : Asset->FlipbookGroups)
		{
			FGameplayTag GroupTag;
			if (!Asset->IsTagBackedFlipbookGroup(Group.GroupName, GroupTag))
			{
				return true;
			}
		}
		return false;
	}

	FString AnimationMapPanel_ShortAnimationTagName(const FGameplayTag& Tag)
	{
		FString Text = Tag.IsValid() ? Tag.ToString() : TEXT("(no tag)");
		static const FString Prefix = TEXT("Paper2DPlus.Animation.");
		if (Text.StartsWith(Prefix))
		{
			Text = Text.RightChop(Prefix.Len());
		}
		return Text;
	}

	FString AnimationMapPanel_TagParentPath(const FString& TagText)
	{
		int32 DotIndex = INDEX_NONE;
		if (TagText.FindLastChar(TEXT('.'), DotIndex) && DotIndex > 0)
		{
			return TagText.Left(DotIndex);
		}
		return TagText;
	}

	FString AnimationMapPanel_TileSubtitle(const FGameplayTag& Tag, const FString& ShortTag)
	{
		if (ShortTag.Contains(TEXT(".")))
		{
			return AnimationMapPanel_TagParentPath(ShortTag);
		}
		static const FString AnimationPrefix = TEXT("Paper2DPlus.Animation.");
		if (Tag.IsValid() && Tag.ToString().StartsWith(AnimationPrefix))
		{
			return TEXT("Animation");
		}
		return TEXT("Tag group");
	}

	FText AnimationMapPanel_CountText(int32 Count)
	{
		return Count == 1
			? LOCTEXT("AnimationMapOneFlipbook", "1 flipbook")
			: FText::Format(LOCTEXT("AnimationMapManyFlipbooksFmt", "{0} flipbooks"), FText::AsNumber(Count));
	}

	FText AnimationMapPanel_GroupCountText(int32 FlipbookCount, int32 RootCount)
	{
		// Chain-start rework: the counted identity is now the per-entry bIsChainStart flag.
		const FText RootText = RootCount == 1
			? LOCTEXT("AnimationMapOneChainStart", "1 chain start")
			: FText::Format(LOCTEXT("AnimationMapManyChainStartsFmt", "{0} chain starts"), FText::AsNumber(RootCount));
		return FText::Format(
			LOCTEXT("AnimationMapGroupCountsFmt", "{0}  •  {1}"),
			AnimationMapPanel_CountText(FlipbookCount),
			RootText);
	}

	FLinearColor AnimationMapPanel_TileAccent(const FString& StableKey, bool bUnassigned)
	{
		if (bUnassigned)
		{
			return FLinearColor(0.78f, 0.50f, 0.16f, 1.0f);
		}

		static const FLinearColor Palette[] = {
			FLinearColor(0.18f, 0.48f, 0.82f, 1.0f),
			FLinearColor(0.18f, 0.62f, 0.50f, 1.0f),
			FLinearColor(0.54f, 0.46f, 0.82f, 1.0f),
			FLinearColor(0.72f, 0.38f, 0.42f, 1.0f),
			FLinearColor(0.42f, 0.62f, 0.26f, 1.0f),
			FLinearColor(0.74f, 0.55f, 0.20f, 1.0f),
		};
		const uint32 PaletteIndex = GetTypeHash(StableKey) % UE_ARRAY_COUNT(Palette);
		return Palette[PaletteIndex];
	}

	/** Exact visible-content signature for the group board. Asset signals are intentionally coarse;
	 *  this keeps their explicit + deferred echo from clearing/recreating identical Slate tiles. */
	FString AnimationMapPanel_BoardSignature(
		const Paper2DPlusAnimationMap::FAnimationMapProjection& Projection,
		const FGameplayTag& FilterTag)
	{
		FString Signature = FString::Printf(TEXT("filter=%s|unassigned=%d"),
			*FilterTag.ToString(), Projection.Unmapped.Num());
		for (const Paper2DPlusAnimationMap::FAnimationMapGroup& Group : Projection.Groups)
		{
			int32 ChainStartCount = 0;
			for (const Paper2DPlusAnimationMap::FAnimationMapEntry& Entry : Group.Entries)
			{
				ChainStartCount += Entry.bIsChainStart ? 1 : 0;
			}
			bool bTagColorFound = false;
			const FLinearColor TagColor = UPaper2DPlusSettings::ResolveTagColor(Group.Tag, bTagColorFound);
			const FString ColorSignature = bTagColorFound ? TagColor.ToString() : TEXT("fallback");
			Signature += FString::Printf(TEXT("|%s:%d:%d:%s"),
				*Group.Tag.ToString(), Group.Entries.Num(), ChainStartCount, *ColorSignature);
		}
		return Signature;
	}

	// --- U7 cross-version shims (the 5.0-5.7 BuildPlugin contract; one guard per drifted API) ---

	/** 5.6 FVector2f sweep: SGraphEditor gained FVector2f Get/SetViewLocation overloads in 5.6
	 *  (GraphEditor.h:257-:295) where the FVector2D forms are deprecated virtuals; 5.0-5.5 have ONLY the
	 *  FVector2D forms (:190/:204 in 5.0). Panel code keeps FVector2f locals; these shims convert. */
	void AnimationMapPanel_GetViewLocation(const TSharedPtr<SGraphEditor>& GraphEditor, FVector2f& OutLocation, float& OutZoom)
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
		GraphEditor->GetViewLocation(OutLocation, OutZoom);
#else
		FVector2D Location = FVector2D::ZeroVector;
		GraphEditor->GetViewLocation(Location, OutZoom);
		OutLocation = FVector2f(Location);
#endif
	}

	void AnimationMapPanel_SetViewLocation(const TSharedPtr<SGraphEditor>& GraphEditor, const FVector2f& Location, float Zoom)
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
		GraphEditor->SetViewLocation(Location, Zoom);
#else
		GraphEditor->SetViewLocation(FVector2D(Location), Zoom);
#endif
	}

	// INI persistence (the HitboxEditorPanel/CharacterProfileEditorPersistence recipe: section
	// "CharacterProfileEditor" in GEditorPerProjectIni; no explicit Flush). Not keyed per-asset — matches
	// HitboxEditorPanel's panel-local keys.
	const TCHAR* AnimationMapPanel_ConfigSection = TEXT("CharacterProfileEditor");
	const TCHAR* AnimationMapPanel_ConfigKeyZoom = TEXT("AnimationMapZoom");
	const TCHAR* AnimationMapPanel_ConfigKeyViewX = TEXT("AnimationMapViewX");
	const TCHAR* AnimationMapPanel_ConfigKeyViewY = TEXT("AnimationMapViewY");
	const TCHAR* AnimationMapPanel_ConfigKeyFilterTag = TEXT("AnimationMapFilterTag"); // U6 (R8)

	// --- TASK-151 U3: the canvas rail's vertical geometry, declared in ONE place ---
	// The switcher's top inset used to be a hand-tuned literal whose only link to the rail's real height
	// was a comment, so a later font or padding change could silently push the rail over the graph. The
	// three primitives below are now the single source of truth and the inset is DERIVED from them:
	//   * the rail's overlay slot reads AnimationMapPanel_RailTopMargin as its top padding,
	//   * the rail's SBorder reads AnimationMapPanel_RailBorderPaddingV as its vertical padding,
	//   * Construct wraps the rail in an SBox that HeightOverrides it to exactly
	//     (AnimationMapPanel_CanvasRailInset - AnimationMapPanel_RailTopMargin) — i.e. the space the inset
	//     reserves — so the height is ENFORCED at the same site that applies the inset, never measured or
	//     re-guessed.
	// The coupling is therefore structural, not documentary: a bigger font clips inside the rail's own
	// ClipToBounds (obvious and self-correcting) instead of quietly overhanging the canvas.

	/** Gap between the panel's top edge and the rail border (the rail overlay slot's top padding). */
	constexpr float AnimationMapPanel_RailTopMargin = 2.0f;
	/** Vertical padding INSIDE the rail border (the rail SBorder's .Padding Y). */
	constexpr float AnimationMapPanel_RailBorderPaddingV = 2.0f;
	/** Enforced height of the rail's single content row — an explicit HeightOverride, never a guess. */
	constexpr float AnimationMapPanel_RailContentHeight = 22.0f;
	/** Top inset applied to the board/graph switcher: exactly the rail's total occupied height, so the
	 *  overlaid rail can never cover an actionable node or group tile. DERIVED — never hand-tuned. */
	constexpr float AnimationMapPanel_CanvasRailInset =
		AnimationMapPanel_RailTopMargin
		+ 2.0f * AnimationMapPanel_RailBorderPaddingV
		+ AnimationMapPanel_RailContentHeight;
	// Compile-time guard: catches a future hand-edit of the inset (or a dropped term in the sum above)
	// that would let the rail cover the board/graph switcher again.
	//
	// The threshold must be the rail's TOTAL occupied height — top margin included. It previously
	// omitted AnimationMapPanel_RailTopMargin, which made it guard a strictly smaller quantity than
	// its own message claimed: against the live derivation it reduced to "RailTopMargin >= 0" and so
	// could never fire, and had anyone replaced the derivation with a hand-typed literal, any value in
	// [content + 2*padding, content + 2*padding + topMargin) would have passed while the rail still
	// overhung the switcher by exactly the top margin. Comparing against the same terms the inset is
	// built from is deliberate: this is a "the derivation was not quietly replaced" guard, and the
	// only threshold that means anything here is the full occupied height.
	static_assert(
		AnimationMapPanel_CanvasRailInset
			>= AnimationMapPanel_RailTopMargin
				+ 2.0f * AnimationMapPanel_RailBorderPaddingV
				+ AnimationMapPanel_RailContentHeight,
		"AnimationMapPanel_CanvasRailInset must cover the rail's TOTAL occupied height — top margin, "
		"both border paddings, and the enforced content height — otherwise the overlaid canvas rail "
		"overhangs the board/graph switcher.");
	/** Longest scope/status text the rail shows before eliding (full text stays in the tooltip). */
	constexpr float AnimationMapPanel_RailTextMaxWidth = 150.0f;
	/** Horizontal padding of the rail's overlay slot (both sides) — subtracted from the panel's allotted
	 *  width when clamping the rail so an HAlign_Left overlay child can never ask for more than it has. */
	constexpr float AnimationMapPanel_RailSlotPaddingH = 6.0f;

	FAutoConsoleCommand GAnimationMapGraphProbeCommand(
		TEXT("Paper2DPlus.AnimationMapGraphProbe"),
		TEXT("Log the open Animation Map tab's node/edge/stub counts + active filter (header 'nodes=N edges=N stubs=N filter=<TagName|none>'), per-node positions + effective tags (name@x,y[,stub][,start][,end][,combo=N/M][,dim] tags=<leaf1>;<leaf2> — ,start marks the group's authored Chain Start, ,end the authored Chain End, combo=N/M is the derived main-line index/length, tags= always present, empty when none), per-marker rows (chainstart -> <TargetMove|floating> @x,y, then chainend -> <TargetMove|floating> @x,y), and per-edge rows (edge From->Target row=N phase=<leaf|none>)"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			TSharedPtr<SAnimationMapPanel> Panel = GActiveAnimationMapPanel.Pin();
			if (!Panel.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.AnimationMapGraphProbe: no open Animation Map panel"));
				return;
			}
			Panel->LogProbe();
		}));

	FAutoConsoleCommand GAnimationMapProbeCommand(
		TEXT("Paper2DPlus.AnimationMapProbe"),
		TEXT("Log the open Animation Map tab's Animation Map group and unmapped counts."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			TSharedPtr<SAnimationMapPanel> Panel = GActiveAnimationMapPanel.Pin();
			if (!Panel.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.AnimationMapProbe: no open Animation Map panel"));
				return;
			}
			Panel->LogAnimationMapProbe();
		}));

	FAutoConsoleCommand GAnimationMapViewCommand(
		TEXT("Paper2DPlus.AnimationMapView"),
		TEXT("Drive the open Animation Map tab: no arg / 'groups' / 'close' shows the group board; 'unassigned' opens the unassigned graph; any other value opens the group whose tag leaf name matches."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			TSharedPtr<SAnimationMapPanel> Panel = GActiveAnimationMapPanel.Pin();
			if (!Panel.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.AnimationMapView: no open Animation Map panel"));
				return;
			}
			Panel->OpenAnimationMapView(Args.Num() > 0 ? Args[0] : TEXT("groups"));
		}));

	FAutoConsoleCommand GAddCommentCommand(
		TEXT("Paper2DPlus.AddComment"),
		TEXT("Add a comment box to the open Animation Map group graph (opens the unassigned bucket first if on the group board). Test/automation + screenshot seam."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			TSharedPtr<SAnimationMapPanel> Panel = GActiveAnimationMapPanel.Pin();
			if (!Panel.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.AddComment: no open Animation Map panel"));
				return;
			}
			Panel->ConsoleAddComment();
		}));

	FAutoConsoleCommand GSetTagColorCommand(
		TEXT("Paper2DPlus.SetTagColor"),
		TEXT("Set a project-wide Tag Color: Paper2DPlus.SetTagColor <Tag> <R> <G> <B> (0..1). Persists to config + repaints open editors. Test/automation seam."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 4)
			{
				UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.SetTagColor: usage <Tag> <R> <G> <B>"));
				return;
			}
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Args[0]), /*ErrorIfNotFound*/ false);
			if (!Tag.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.SetTagColor: '%s' is not a registered tag"), *Args[0]);
				return;
			}
			const FLinearColor Color(FCString::Atof(*Args[1]), FCString::Atof(*Args[2]), FCString::Atof(*Args[3]), 1.0f);
			UPaper2DPlusSettings::SetTagColor(Tag, Color);
			UE_LOG(LogTemp, Display, TEXT("Paper2DPlus.SetTagColor: %s -> (%.2f, %.2f, %.2f)"),
				*Tag.GetTagName().ToString(), Color.R, Color.G, Color.B);
		}));

	FAutoConsoleCommand GOpenSettingsCommand(
		TEXT("Paper2DPlus.OpenSettings"),
		TEXT("Open Project Settings to the Paper2DPlus section (Tag Colors etc.). Automation/screenshot seam."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
			{
				SettingsModule->ShowViewer("Project", "Plugins", "Paper2DPlus");
			}
		}));

	FAutoConsoleCommand GClearTagColorCommand(
		TEXT("Paper2DPlus.ClearTagColor"),
		TEXT("Clear a project-wide Tag Color override: Paper2DPlus.ClearTagColor <Tag>."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.ClearTagColor: usage <Tag>"));
				return;
			}
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Args[0]), /*ErrorIfNotFound*/ false);
			UPaper2DPlusSettings::ClearTagColor(Tag);
			UE_LOG(LogTemp, Display, TEXT("Paper2DPlus.ClearTagColor: %s"), *Args[0]);
		}));

	FAutoConsoleCommand GDerivePhasesCommand(
		TEXT("Paper2DPlus.DerivePhasesFromMap"),
		TEXT("Set each move's phase tag (Startup/Active/Recovery) from its position under an exact-group Chain Start. Only fills moves with NO phase tag yet; never overwrites a manual one. Logs the count set."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			TSharedPtr<SAnimationMapPanel> Panel = GActiveAnimationMapPanel.Pin();
			if (!Panel.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.DerivePhasesFromMap: no open Animation Map panel"));
				return;
			}
			Panel->DerivePhasesFromMapAction();
		}));
}

// =====================================================================================================
// CONSTRUCT / DESTROY
// =====================================================================================================

void SAnimationMapPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;

	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}

	// --- The transient projection graph (KTD: storage and cook/JSON safety) ---
	// RF_Transient into the transient package: NEVER serialized; rebuilt from the flat data on every
	// reconcile. Rooted by the panel so GC can't collect it under the live SGraphEditor.
	GraphObj = NewObject<UPaper2DPlusAnimationMap>(GetTransientPackage(), NAME_None, RF_Transient);
	GraphObj->ClearFlags(RF_Transactional); // explicit: the asset must be the ONLY transacted object
	GraphObj->Schema = UPaper2DPlusAnimationMapSchema::StaticClass();
	GraphObj->AddToRoot();

	// --- U3/U5 delegate seam ---
	GraphObj->OnWireCreateRequested.BindSP(this, &SAnimationMapPanel::HandleWireCreateRequested);
	GraphObj->OnTransitionTargetRewireRequested.BindSP(
		this, &SAnimationMapPanel::HandleTransitionTargetRewireRequested);
	GraphObj->OnEdgeRemovalRequested.BindSP(this, &SAnimationMapPanel::HandleEdgeRemovalRequested);
	GraphObj->OnNodeMoveCommitted.BindSP(this, &SAnimationMapPanel::HandleNodeMoveCommitted);
	GraphObj->OnCommentChanged.BindSP(this, &SAnimationMapPanel::HandleCommentChanged);
	GraphObj->OnChangeGroupRequested.BindSP(this, &SAnimationMapPanel::HandleChangeGroupRequested);
	GraphObj->OnSetPhaseRequested.BindSP(this, &SAnimationMapPanel::HandleSetPhaseRequested);
	GraphObj->OnSetAnimationTagsRequested.BindSP(this, &SAnimationMapPanel::HandleSetAnimationTagsRequested);
	// Chain-start rework: the marker gestures (aim / break / empty-canvas add / move persist).
	GraphObj->OnChainStartLinkRequested.BindSP(this, &SAnimationMapPanel::HandleChainStartLinkRequested);
	GraphObj->OnChainStartUnlinkRequested.BindSP(this, &SAnimationMapPanel::HandleChainStartUnlinkRequested);
	GraphObj->OnAddChainStartRequested.BindSP(this, &SAnimationMapPanel::HandleAddChainStartRequested);
	GraphObj->OnChainStartMarkerMoveCommitted.BindSP(this, &SAnimationMapPanel::HandleChainStartMarkerMoveCommitted);
	// Chain-end rework: the mirrored end-marker gesture set.
	GraphObj->OnChainEndLinkRequested.BindSP(this, &SAnimationMapPanel::HandleChainEndLinkRequested);
	GraphObj->OnChainEndUnlinkRequested.BindSP(this, &SAnimationMapPanel::HandleChainEndUnlinkRequested);
	GraphObj->OnAddChainEndRequested.BindSP(this, &SAnimationMapPanel::HandleAddChainEndRequested);
	GraphObj->OnChainEndMarkerMoveCommitted.BindSP(this, &SAnimationMapPanel::HandleChainEndMarkerMoveCommitted);

	// A plain UEdGraphNode_Comment cannot override PostEditChangeProperty for plugin write-through on
	// UE 5.0-5.4, so Details-panel changes use the global broadcast. The handler filters to comments in
	// this panel's graph and ignores interactive color-picker updates.
	CommentPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(
		this, &SAnimationMapPanel::HandleCommentObjectPropertyChanged);

	// --- U5 graph commands (the FAIGraphEditor::CreateCommandList shape, AIGraphEditor.cpp:41-189):
	// Delete is the ONLY caller of the panel's single delete funnel this PR; SelectAll is free.
	// Copy/Cut/Paste/Duplicate deliberately unbound (CanDuplicateNode()=false on both node classes).
	GraphEditorCommands = MakeShared<FUICommandList>();
	GraphEditorCommands->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SAnimationMapPanel::DeleteSelectedNodes),
		FCanExecuteAction::CreateSP(this, &SAnimationMapPanel::CanDeleteSelectedNodes));
	GraphEditorCommands->MapAction(FGenericCommands::Get().SelectAll,
		FExecuteAction::CreateSP(this, &SAnimationMapPanel::SelectAllNodes),
		FCanExecuteAction::CreateSP(this, &SAnimationMapPanel::CanSelectAllNodes));
	GraphEditorCommands->MapAction(FGenericCommands::Get().Rename,
		FExecuteAction::CreateSP(this, &SAnimationMapPanel::RenameSelectedComment),
		FCanExecuteAction::CreateSP(this, &SAnimationMapPanel::CanRenameSelectedComment));
	// Comment boxes: the idiomatic Blueprint-graph C-key (with selection = wrap the selected nodes).
	GraphEditorCommands->MapAction(FGraphEditorCommands::Get().CreateComment,
		FExecuteAction::CreateSP(this, &SAnimationMapPanel::AddCommentForCurrentGroup),
		FCanExecuteAction::CreateSP(this, &SAnimationMapPanel::CanAddComment));

	// --- Model triggers (the reconcile contract, R9). Handlers are cheap + re-entrancy-safe: the
	// model's BroadcastOrDefer can call them INLINE mid-broadcast, so they only set flags/arm a timer.
	if (Model.IsValid())
	{
		ModelAssetDataChangedHandle = Model->OnAssetDataChanged.AddSP(this, &SAnimationMapPanel::HandleAssetChangedSignal);
		ModelAssetExternallyModifiedHandle = Model->OnAssetExternallyModified.AddSP(this, &SAnimationMapPanel::HandleAssetChangedSignal);
		// TASK-96 P3: follow the shared selection — focus the matching move node (paint-deferred, guarded).
		ModelFlipbookSelectionHandle = Model->OnFlipbookSelectionChanged.AddSP(this, &SAnimationMapPanel::HandleModelFlipbookSelectionChanged);
	}
	// Tag Colors: repaint every tag-colored surface this panel owns whenever a tag's registered color
	// changes (in-editor swatch or the Project Settings table) — the group board's tiles AND the canvas
	// rail's active-filter chip.
	TagColorsChangedHandle = UPaper2DPlusSettings::OnTagColorsChanged().AddSP(this, &SAnimationMapPanel::HandleTagColorsChanged);

	// U6: the strip listens on the graph's selection (SGraphPanel wires this straight into its
	// SelectionManager — SGraphPanel.cpp:99 — so programmatic SetNodeSelection/ClearSelectionSet fire
	// it too, which is what re-shows the strip after wire-create's auto-select and the re-match).
	SGraphEditor::FGraphEditorEvents GraphEvents;
	GraphEvents.OnSelectionChanged =
		SGraphEditor::FOnSelectionChanged::CreateSP(this, &SAnimationMapPanel::HandleGraphSelectionChanged);

	// TASK-151 U3 (R9/R17): the Map's persistent horizontal command toolbar is RETIRED. Graph-scope
	// navigation, Zoom to Fit, filter state, and the advanced-tool overflow now live in ONE compact
	// NON-WRAPPING canvas rail
	// overlaid top-left on the board/graph switcher. Placement changed; every command still calls the
	// SAME callbacks and predicates it did as a toolbar button. The base content carries a matching top
	// inset (AnimationMapPanel_CanvasRailInset) so the rail can never cover an actionable node or tile.
	//
	// The overlay is CLIPPED to the panel's own bounds (the house canvas-clipping convention, and the
	// specific fix for the rail): SOverlay::AlignChild hands an HAlign_Left child its full DESIRED width,
	// unclamped by the allotted geometry, so at narrow widths (the narrow Character visual-tour capture,
	// or any splitter drag squeezing the Map) the rail's right-hand controls would paint past the panel's
	// right edge over the neighbouring shared SProfileDetailsPanel. Clipping here plus the explicit width
	// clamp on the rail slot below makes that overhang impossible.
	ChildSlot
	[
		SNew(SOverlay)
		.Clipping(EWidgetClipping::ClipToBounds)

		+ SOverlay::Slot()
		.Padding(0.0f, AnimationMapPanel_CanvasRailInset, 0.0f, 0.0f)
		[
			SAssignNew(MainViewSwitcher, SWidgetSwitcher)
			.WidgetIndex_Lambda([this]()
			{
				return GetMainViewSwitcherIndex();
			})

			+ SWidgetSwitcher::Slot()
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					// Clipping wrapper per the house canvas convention (belt-and-braces — SGraphPanel clips its
					// own draws, but every pan/zoom canvas host in this plugin wraps with ClipToBounds).
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
					.Padding(0)
					.Clipping(EWidgetClipping::ClipToBounds)
					[
						SNew(SOverlay)

						+ SOverlay::Slot()
						[
							SAssignNew(GraphEditorWidget, SGraphEditor)
							.GraphToEdit(GraphObj)
							// EDITABLE since U5: every gesture funnels its data write through this panel.
							.IsEditable(true)
							.AdditionalCommands(GraphEditorCommands)
							.GraphEvents(GraphEvents)
							.Appearance(TAttribute<FGraphAppearanceInfo>::Create(
								TAttribute<FGraphAppearanceInfo>::FGetter::CreateSP(this, &SAnimationMapPanel::GetGraphAppearance)))
						]

						+ SOverlay::Slot()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Top)
						.Padding(12.0f)
						[
							BuildSelectionCohortCard()
						]
					]
				]
				// TASK-96 P3: the in-graph details strip is RETIRED — node selection now drives the merged
				// tab's shared SProfileDetailsPanel (flipbook details / phase-tag chip / the target-only
				// Move Transitions rows; transitions are pure From→To since TASK-108).
			]

			+ SWidgetSwitcher::Slot()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.Padding(8.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(AnimationMapGroupsBox, SVerticalBox)
					]
				]
			]
		]

		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(
			AnimationMapPanel_RailSlotPaddingH,
			AnimationMapPanel_RailTopMargin,
			AnimationMapPanel_RailSlotPaddingH,
			0.0f)
		[
			// One box owns BOTH halves of the rail's geometry contract:
			//  * WIDTH — belt-and-braces with the overlay clip above: clamp the rail's DESIRED width to
			//    the panel's own allotted width (minus this slot's horizontal padding). An HAlign_Left
			//    overlay child is arranged at its desired width, so without this the rail's desired size
			//    — not the available space — decided how far right it reached.
			//  * HEIGHT — HeightOverride to exactly the space the switcher's inset above reserves
			//    (inset minus this slot's top margin). The inset and the rail's real height are therefore
			//    the SAME derived number rather than two hand-tuned values kept in sync by a comment.
			SNew(SBox)
			.MaxDesiredWidth_Lambda([this]() -> FOptionalSize
			{
				// static_cast: FGeometry::GetLocalSize() is double-based on UE 5.0-5.1 and float-based
				// from 5.2 (FDeprecateVector2DResult), so FMath::Max would fail to deduce one T.
				const float PanelWidth = static_cast<float>(GetTickSpaceGeometry().GetLocalSize().X);
				if (PanelWidth <= 0.0f)
				{
					// Not laid out yet (first prepass, or a never-painted/headless panel): report NO
					// clamp rather than a zero width, so the rail still reports its natural desired size.
					// The overlay's ClipToBounds is the guard that does not depend on geometry.
					return FOptionalSize();
				}
				return FOptionalSize(
					FMath::Max(PanelWidth - 2.0f * AnimationMapPanel_RailSlotPaddingH, 0.0f));
			})
			.HeightOverride(AnimationMapPanel_CanvasRailInset - AnimationMapPanel_RailTopMargin)
			[
				BuildMapCanvasRail()
			]
		]
	];

	GActiveAnimationMapPanel = SharedThis(this);

	RestoreViewFromConfig();
	// U6 (R8) / U3 (R10): populate the rail's filter control AFTER the restore so a persisted tag shows
	// as its removable chip rather than the inactive compact Filter button.
	RebuildMapFilterPicker();

	// Initial reconcile (applies on the tab's first paint via the timer).
	RequestReconcile();
	QueueAnimationMapRefresh();
	// One-shot "migrate legacy non-tag Overview groups" prompt. KEPT as-is under the merged Animations tab
	// (TASK-96 P5): this panel lives in a hidden SWidgetSwitcher slot until the user picks "Map", and the
	// queueing active timer only fires from Paint — so the prompt naturally defers to the first Map view and
	// never nags a user who stays in List. Still one-shot per asset (GEditorPerProjectIni key).
	QueueAnimationMapMigrationPrompt();
}

SAnimationMapPanel::~SAnimationMapPanel()
{
	SaveViewToConfig();

	UPaper2DPlusSettings::OnTagColorsChanged().Remove(TagColorsChangedHandle);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(CommentPropertyChangedHandle);

	if (Model.IsValid())
	{
		Model->OnAssetDataChanged.Remove(ModelAssetDataChangedHandle);
		Model->OnAssetExternallyModified.Remove(ModelAssetExternallyModifiedHandle);
		Model->OnFlipbookSelectionChanged.Remove(ModelFlipbookSelectionHandle);
		// Graph destroyed — the model's edge key has no owner surface anymore (TASK-108 U3). The pane
		// (if it outlives this panel) falls back to flipbook mode via the clear broadcast.
		Model->ClearSelectedTransition();
	}

	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}

	// Unroot behind the engine's own guard — cloned from SReferenceViewer::~SReferenceViewer
	// (SReferenceViewer.cpp:127-138): during exit purge the rooted object may already be torn down.
	if (!GExitPurge)
	{
		if (ensure(GraphObj))
		{
			GraphObj->OnWireCreateRequested.Unbind();
			GraphObj->OnTransitionTargetRewireRequested.Unbind();
			GraphObj->OnEdgeRemovalRequested.Unbind();
			GraphObj->OnNodeMoveCommitted.Unbind();
			GraphObj->OnCommentChanged.Unbind();
			GraphObj->OnChangeGroupRequested.Unbind();
			GraphObj->OnSetPhaseRequested.Unbind();
			GraphObj->OnSetAnimationTagsRequested.Unbind();
			GraphObj->OnChainStartLinkRequested.Unbind();
			GraphObj->OnChainStartUnlinkRequested.Unbind();
			GraphObj->OnAddChainStartRequested.Unbind();
			GraphObj->OnChainStartMarkerMoveCommitted.Unbind();
			GraphObj->OnChainEndLinkRequested.Unbind();
			GraphObj->OnChainEndUnlinkRequested.Unbind();
			GraphObj->OnAddChainEndRequested.Unbind();
			GraphObj->OnChainEndMarkerMoveCommitted.Unbind();
			GraphObj->RemoveFromRoot();
		}
	}
}

// =====================================================================================================
// TASK-151 U3 — THE MAP CANVAS RAIL (R9-R14, R16-R19)
//
// One compact NON-WRAPPING rail overlaid on the board/graph switcher, replacing the retired persistent
// horizontal toolbar. Placement is the ONLY thing that changed: Full Map / Back to Groups, Zoom to Fit,
// tag filtering, and phase derivation all still call the exact callbacks and predicates the toolbar
// buttons called.
//
// Narrow-width priority order (KTD11): the rail never wraps. Long scope text ELIDES with the full text
// in its tooltip, and the stable navigation / filter / overflow controls always survive.
// =====================================================================================================

TSharedRef<SWidget> SAnimationMapPanel::BuildMapCanvasRail()
{
	const FSlateFontInfo RailLabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(FMargin(4.0f, AnimationMapPanel_RailBorderPaddingV))
		.Clipping(EWidgetClipping::ClipToBounds)
		[
			// Deliberately an SHorizontalBox and NOT an SWrapBox: the rail must stay ONE line at every
			// width (R17). Nothing here is allowed to reflow onto a second row.
			// The rail's HEIGHT is not decided here: Construct wraps this widget in the SBox that also
			// applies AnimationMapPanel_CanvasRailInset, and that box HeightOverrides the rail to exactly
			// the height the inset reserves — so the two cannot drift apart.
			SNew(SHorizontalBox)

			// --- Scope navigation. The two entries are mutually exclusive, so the pair costs the width
			// of one button on either surface.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ContentPadding(FMargin(6.0f, 1.0f))
				.Text(LOCTEXT("AnimationMapOpenFullGraph", "Full Map"))
				.ToolTipText(LOCTEXT(
					"AnimationMapOpenFullGraphTip",
					"Open the complete Animation Map."))
				.Visibility_Lambda([this]()
				{
					return bAnimationMapGroupGraphOpen
						? EVisibility::Collapsed
						: EVisibility::Visible;
				})
				.OnClicked_Lambda([this]()
				{
					OpenAnimationMapFullGraph();
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ContentPadding(FMargin(6.0f, 1.0f))
				.Text(LOCTEXT("AnimationMapGraphBackButton", "Back to Groups"))
				.ToolTipText(LOCTEXT(
					"AnimationMapGraphBackButtonTip",
					"Leave this graph and return to the Animation Group board."))
				.Visibility_Lambda([this]()
				{
					return bAnimationMapGroupGraphOpen
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				.OnClicked_Lambda([this]()
				{
					CloseAnimationMapGroup();
					return FReply::Handled();
				})
			]

			// --- Viewport: manual re-center (audit F14). Applicable only on a graph surface.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ContentPadding(FMargin(6.0f, 1.0f))
				.Text(LOCTEXT("AnimationMapZoomToFit", "Zoom to Fit"))
				.ToolTipText(LOCTEXT("AnimationMapZoomToFitTip", "Re-center the graph on its nodes (Zoom to Fit)."))
				.Visibility_Lambda([this]()
				{
					return bAnimationMapGroupGraphOpen ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.OnClicked_Lambda([this]()
				{
					if (GraphEditorWidget.IsValid())
					{
						GraphEditorWidget->ZoomToFit(/*bOnlySelection=*/false);
					}
					return FReply::Handled();
				})
			]

			// --- Current graph scope, elided under width pressure with the full name in the tooltip.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SBox)
				.MaxDesiredWidth(AnimationMapPanel_RailTextMaxWidth)
				.Visibility_Lambda([this]()
				{
					return bAnimationMapGroupGraphOpen
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return GetAnimationMapScopeLabel();
					})
					.ToolTipText_Lambda([this]()
					{
						return GetAnimationMapScopeLabel();
					})
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
				]
			]

			// --- Filter: compact "Filter" control while inactive, a directly removable tag chip while
			// active (R10/R16). Rebuilt statically by RebuildMapFilterPicker — never a bound .Tag_Lambda.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SAssignNew(MapFilterPickerBox, SBox)
			]

			// --- Advanced tools (R11/R13). The overflow is also the STABLE focus target a self-removing
			// contextual action returns to (KTD12).
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(MapOverflowButton, SComboButton)
				.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
				.ContentPadding(FMargin(6.0f, 1.0f))
				.ToolTipText(LOCTEXT(
					"AnimationMapOverflowTip",
					"Advanced Animation Map tools, including whole-profile phase derivation."))
				.OnGetMenuContent(FOnGetContent::CreateSP(this, &SAnimationMapPanel::BuildMapOverflowMenu))
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AnimationMapOverflowLabel", "Map"))
					.Font(RailLabelFont)
				]
			]
		];
}

FText SAnimationMapPanel::GetAnimationMapScopeLabel() const
{
	if (bAnimationMapGroupGraphIsUnassigned)
	{
		return LOCTEXT("AnimationMapGraphUnassignedLabel", "Unassigned graph");
	}
	return ActiveAnimationMapGroupTag.IsValid()
		? FText::Format(
			LOCTEXT("AnimationMapGraphGroupLabelFmt", "{0} graph"),
			FText::FromString(
				AnimationMapPanel_ShortAnimationTagName(
					ActiveAnimationMapGroupTag)))
		: LOCTEXT(
			"AnimationMapGraphFullLabel",
			"Full composition graph");
}

TSharedRef<SWidget> SAnimationMapPanel::BuildMapOverflowMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, /*InCommandList=*/nullptr);

	// Every entry below hands the real work to the SAME method the retired toolbar button called, and
	// defers it one tick behind an explicit menu dismissal: these actions open confirmation modals and
	// rebuild the board/graph under the menu that invoked them.
	const TWeakPtr<SAnimationMapPanel> WeakPanel = SharedThis(this);
	auto RunDeferred = [](TWeakPtr<SAnimationMapPanel> InWeakThis, TFunction<void(SAnimationMapPanel&)> InAction)
	{
		FSlateApplication::Get().DismissAllMenus();
		auto Fire = [InWeakThis, InAction]()
		{
			if (const TSharedPtr<SAnimationMapPanel> Panel = InWeakThis.Pin())
			{
				InAction(*Panel);
			}
		};
		if (GEditor)
		{
			GEditor->GetTimerManager()->SetTimerForNextTick(Fire);
		}
		else
		{
			Fire();
		}
	};


	MenuBuilder.BeginSection(TEXT("AnimationMapPhaseTools"),
		LOCTEXT("MapOverflowPhasesSection", "Phases"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("MapOverflowDeriveMissingPhases", "Derive Missing Phases…"),
			LOCTEXT(
				"MapOverflowDeriveMissingPhasesTip",
				"Whole-profile and FILL-ONLY: sets Startup / Active / Recovery on every animation that has NO phase tag yet, from its position under a Chain Start in the same Animation Group. Manually authored phase tags are never replaced."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([WeakPanel, RunDeferred]()
				{
					RunDeferred(WeakPanel, [](SAnimationMapPanel& Panel)
					{
						Panel.DerivePhasesFromMapAction();
					});
				})));
	}
	MenuBuilder.EndSection();

	const UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	const FGameplayTag InvalidGroupTag;
	const FFlipbookTagMapping* InvalidMapping =
		Asset ? Asset->TagMappings.Find(InvalidGroupTag) : nullptr;
	if (InvalidMapping)
	{
		MenuBuilder.BeginSection(
			TEXT("AnimationMapRecoveryTools"),
			LOCTEXT("MapOverflowRecoverySection", "Recovery"));
		if (InvalidMapping->Entries.IsEmpty())
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("MapOverflowRemoveInvalidEmptyGroup", "Remove Invalid Empty Group"),
				LOCTEXT(
					"MapOverflowRemoveInvalidEmptyGroupTip",
					"Remove the empty TagMappings entry whose Gameplay Tag is missing or invalid. This is undoable."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([WeakPanel, RunDeferred]()
					{
						RunDeferred(WeakPanel, [](SAnimationMapPanel& Panel)
						{
							Panel.RepairInvalidTagMapping(FGameplayTag());
						});
					})));
		}
		else
		{
			MenuBuilder.AddWidget(
				BuildInvalidTagMappingRepairWidget(),
				FText::GetEmpty(),
				/*bNoIndent=*/true);
		}
		MenuBuilder.EndSection();
	}

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SAnimationMapPanel::BuildInvalidTagMappingRepairWidget()
{
	InvalidGroupRepairPickerValue = MakeShared<FGameplayTag>();

	const TWeakPtr<SAnimationMapPanel> WeakPanel = SharedThis(this);
	FOnSetGameplayTag OnSetTag = FOnSetGameplayTag::CreateLambda(
		[WeakPanel](const FGameplayTag& InNewTag)
		{
			if (!InNewTag.IsValid())
			{
				return;
			}
			if (const TSharedPtr<SAnimationMapPanel> Panel = WeakPanel.Pin())
			{
				Panel->CommitInvalidTagMappingRepairFromPicker(InNewTag);
			}
		});

	return SNew(SBox)
		.MinDesiredWidth(320.0f)
		.MaxDesiredHeight(460.0f)
		.Padding(FMargin(8.0f, 4.0f))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 5.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT(
					"MapOverflowRepairInvalidGroupHelp",
					"Repair the invalid Animation Group by moving all of its entries to one unused registered tag."))
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				// The group key can live outside Paper2DPlus.Animation, so deliberately expose the
				// complete tag tree. Management remains disabled inside this menu-hosted picker.
				SNew(SMenuHostedTagPickerGuard)
				[
					IGameplayTagsEditorModule::Get().MakeGameplayTagWidget(
						OnSetTag,
						InvalidGroupRepairPickerValue)
				]
			]
		];
}

bool SAnimationMapPanel::RepairInvalidTagMapping(const FGameplayTag& ReplacementTag)
{
	if (bGraphWriteInProgress || HasActiveTransaction())
	{
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	const FGameplayTag InvalidGroupTag;
	const FFlipbookTagMapping* InvalidMapping =
		Asset ? Asset->TagMappings.Find(InvalidGroupTag) : nullptr;
	if (!Asset || !InvalidMapping)
	{
		return false;
	}

	if (ReplacementTag.IsValid())
	{
		if (Asset->TagMappings.Contains(ReplacementTag))
		{
			FNotificationInfo Info(LOCTEXT(
				"MapRepairInvalidGroupTagAlreadyUsed",
				"That tag already owns an Animation Group. Choose an unused registered tag."));
			Info.ExpireDuration = 4.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
			return false;
		}
	}
	else if (!InvalidMapping->Entries.IsEmpty())
	{
		// Never turn a repair gesture into silent data deletion. Non-empty invalid groups must be
		// moved to a valid tag; only an empty shell exposes the remove action.
		return false;
	}

	BeginTransaction(ReplacementTag.IsValid()
		? LOCTEXT("MapRepairInvalidGroupTransaction", "Repair Invalid Animation Group")
		: LOCTEXT("MapRemoveInvalidEmptyGroupTransaction", "Remove Invalid Empty Animation Group"));
	const bool bChanged = ReplacementTag.IsValid()
		? Asset->RenameTagMapping(InvalidGroupTag, ReplacementTag)
		: Asset->RemoveTagMapping(InvalidGroupTag);
	EndTransaction();
	if (!bChanged)
	{
		return false;
	}

	if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
	return true;
}

void SAnimationMapPanel::CommitInvalidTagMappingRepairFromPicker(const FGameplayTag InNewTag)
{
	if (!InNewTag.IsValid())
	{
		return;
	}

	// The commit rebuilds the Map board and destroys this picker. Unwind its callback first.
	FSlateApplication::Get().DismissAllMenus();
	const TWeakPtr<SAnimationMapPanel> WeakPanel = SharedThis(this);
	auto ApplyPick = [WeakPanel, InNewTag]()
	{
		if (const TSharedPtr<SAnimationMapPanel> Panel = WeakPanel.Pin())
		{
			Panel->RepairInvalidTagMapping(InNewTag);
			if (Panel->MapOverflowButton.IsValid() && FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().SetKeyboardFocus(
					Panel->MapOverflowButton,
					EFocusCause::SetDirectly);
			}
		}
	};
	if (GEditor)
	{
		GEditor->GetTimerManager()->SetTimerForNextTick(ApplyPick);
	}
	else
	{
		ApplyPick();
	}
}

void SAnimationMapPanel::OpenAnimationMapView(const FString& Spec)
{
	const FString Lowered = Spec.TrimStartAndEnd().ToLower();
	if (Lowered.IsEmpty() || Lowered == TEXT("groups") || Lowered == TEXT("close") || Lowered == TEXT("map"))
	{
		CloseAnimationMapGroup();
		return;
	}
	if (Lowered == TEXT("unassigned"))
	{
		OpenAnimationMapUnassigned();
		return;
	}
	if (Lowered == TEXT("full"))
	{
		OpenAnimationMapFullGraph();
		return;
	}
	// Resolve the spec against the ACTUAL projection groups — the seed groups PLUS the asset's real
	// tag-mapping groups — and open with the group's EXACT projection tag, so RefreshAnimationMap's
	// strict `Group.Tag == ActiveAnimationMapGroupTag` guard matches and the view stays on the scoped
	// graph. Before, the loop searched only the 7 Paper2DPlus.Animation.* seed tags, so a custom-tagged
	// group (e.g. GroundAttack/AirAttack under PlayerStates.Attacking.*) was either unreachable or — if a
	// seed leaf happened to match — opened under the wrong (seed) tag and the strict guard bounced it back
	// to the board (TASK-97, the strict-vs-lenient match asymmetry). Using ONE projection here keeps the
	// match source identical to the guard's.
	if (const UPaper2DPlusCharacterProfileAsset* Asset = GetAsset())
	{
		const Paper2DPlusAnimationMap::FAnimationMapProjection Projection =
			Paper2DPlusAnimationMap::ProjectAnimationMap(Asset);
		for (const Paper2DPlusAnimationMap::FAnimationMapGroup& Group : Projection.Groups)
		{
			const FString Short = AnimationMapPanel_ShortAnimationTagName(Group.Tag);
			if (Short.ToLower() == Lowered
				|| (Group.Tag.IsValid() && Paper2DPlusAnimationTagChips::GetTagLeafString(Group.Tag).ToLower() == Lowered))
			{
				OpenAnimationMapGroup(Group.Tag);
				return;
			}
		}
	}
	// No match — fall back to the group board so the seam never strands on a bad arg.
	CloseAnimationMapGroup();
}

bool SAnimationMapPanel::HasLiveScopedGraphForAutomation() const
{
	const FVector2D GraphSize = GraphEditorWidget.IsValid()
		? GraphEditorWidget->GetCachedGeometry().GetLocalSize()
		: FVector2D::ZeroVector;
	return bAnimationMapGroupGraphOpen
		&& GraphObj
		&& GraphEditorWidget.IsValid()
		&& MainViewSwitcher.IsValid()
		&& MainViewSwitcher->GetActiveWidgetIndex() == 0
		&& GraphSize.X > 100.0f
		&& GraphSize.Y > 100.0f;
}

bool SAnimationMapPanel::SynchronizeAndFrameScopedGraphForAutomation()
{
	if (!HasLiveScopedGraphForAutomation() || IsGestureLive())
	{
		return false;
	}
	ReconcileNow();
	GraphEditorWidget->ZoomToFit(/*bOnlySelection=*/false);
	return HasLiveScopedGraphForAutomation();
}

int32 SAnimationMapPanel::GetProjectedMoveNodeCountForAutomation() const
{
	int32 Count = 0;
	if (GraphObj)
	{
		for (const UEdGraphNode* Node : GraphObj->Nodes)
		{
			if (Node && Node->IsA<UPaper2DPlusAnimationMapNode_Move>())
			{
				++Count;
			}
		}
	}
	return Count;
}

int32 SAnimationMapPanel::GetProjectedTransitionNodeCountForAutomation() const
{
	int32 Count = 0;
	if (GraphObj)
	{
		for (const UEdGraphNode* Node : GraphObj->Nodes)
		{
			if (Node && Node->IsA<UPaper2DPlusAnimationMapNode_Transition>())
			{
				++Count;
			}
		}
	}
	return Count;
}

UPaper2DPlusAnimationMapNode_Move* SAnimationMapPanel::FindProjectedMoveNodeForTests(
	const FString& MoveName) const
{
	if (!GraphObj)
	{
		return nullptr;
	}
	for (UEdGraphNode* Node : GraphObj->Nodes)
	{
		UPaper2DPlusAnimationMapNode_Move* MoveNode =
			Cast<UPaper2DPlusAnimationMapNode_Move>(Node);
		if (MoveNode && MoveNode->MoveName.Equals(MoveName, ESearchCase::IgnoreCase))
		{
			return MoveNode;
		}
	}
	return nullptr;
}

int32 SAnimationMapPanel::GetMainViewSwitcherIndex() const
{
	// The group board is the home view. A group, the unassigned bucket, or the explicit full
	// composition view swaps to the graph surface.
	return bAnimationMapGroupGraphOpen ? 0 : 1;
}

void SAnimationMapPanel::RefreshMainViewSwitcher()
{
	if (MainViewSwitcher.IsValid())
	{
		MainViewSwitcher->SetActiveWidgetIndex(GetMainViewSwitcherIndex());
	}
}

bool SAnimationMapPanel::BuildAnimationMapGraphFilter(const UPaper2DPlusCharacterProfileAsset* Asset,
	TSet<FString>& OutMoveNamesLower) const
{
	OutMoveNamesLower.Reset();
	if (!bAnimationMapGroupGraphOpen || !Asset)
	{
		return false;
	}
	if (!bAnimationMapGroupGraphIsUnassigned
		&& !ActiveAnimationMapGroupTag.IsValid())
	{
		// Full composition view: no move filter and no exact group scope.
		return false;
	}

	const Paper2DPlusAnimationMap::FAnimationMapProjection Projection =
		Paper2DPlusAnimationMap::ProjectAnimationMap(Asset);

	const TArray<Paper2DPlusAnimationMap::FAnimationMapEntry>* Entries = nullptr;
	if (bAnimationMapGroupGraphIsUnassigned)
	{
		Entries = &Projection.Unmapped;
	}
	else
	{
		for (const Paper2DPlusAnimationMap::FAnimationMapGroup& Group : Projection.Groups)
		{
			if (Group.Tag == ActiveAnimationMapGroupTag)
			{
				Entries = &Group.Entries;
				break;
			}
		}
	}

	if (Entries)
	{
		for (const Paper2DPlusAnimationMap::FAnimationMapEntry& Entry : *Entries)
		{
			if (!Entry.FlipbookName.TrimStartAndEnd().IsEmpty())
			{
				OutMoveNamesLower.Add(Entry.FlipbookName.ToLower());
			}
		}
	}

	return true;
}

Paper2DPlusAnimationMap::FGraphProjection SAnimationMapPanel::ProjectGraphForCurrentView(
	const UPaper2DPlusCharacterProfileAsset* Asset) const
{
	TSet<FString> MoveFilterLower;
	const bool bUseFilter = BuildAnimationMapGraphFilter(Asset, MoveFilterLower);
	const FGameplayTag* ExactGroupScope = bUseFilter
		&& !bAnimationMapGroupGraphIsUnassigned
		&& ActiveAnimationMapGroupTag.IsValid()
		? &ActiveAnimationMapGroupTag
		: nullptr;
	return Paper2DPlusAnimationMap::ProjectGraph(
		Asset,
		bUseFilter ? &MoveFilterLower : nullptr,
		ExactGroupScope);
}

// =====================================================================================================
// UNDO / REDO (the AIGraphEditor.cpp:107-133 pattern — NEVER mutates the asset or the positions map)
// =====================================================================================================

void SAnimationMapPanel::PostUndo(bool bSuccess)
{
	if (!bSuccess)
	{
		return;
	}
	// Dismiss menus, then reconcile the projection from restored data. Granular RemoveNode actions
	// drop only dead selections; surviving move/edge/comment objects keep their native selection.
	// No asset/position-map writes occur from undo handling.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().DismissAllMenus();
	}
	RequestReconcile();
}

void SAnimationMapPanel::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

// =====================================================================================================
// TRANSACTION HELPERS (cloned from SCurvesPanel — every U5 gesture's data write runs through these)
// =====================================================================================================

void SAnimationMapPanel::BeginTransaction(const FText& Description)
{
	ActiveTransaction = MakeUnique<FScopedTransaction>(Description);
	if (UPaper2DPlusCharacterProfileAsset* Asset = GetAsset())
	{
		Asset->Modify();
	}
}

void SAnimationMapPanel::EndTransaction()
{
	ActiveTransaction.Reset();
	// Flush a reconcile that PENDED while the transaction was open (the handler guard parks it here —
	// pend, never drop). U5's bGraphWriteInProgress scopes must do the same on exit.
	if (bReconcilePending && !bGraphWriteInProgress)
	{
		RequestReconcile();
	}
}

// =====================================================================================================
// COMMENT BOXES (Blueprint-graph-style annotations, scoped to the open group's graph)
// =====================================================================================================

FString SAnimationMapPanel::GetCommentScopeKey() const
{
	if (!bAnimationMapGroupGraphOpen)
	{
		return FString(); // group board open — no graph comment surface
	}
	if (bAnimationMapGroupGraphIsUnassigned)
	{
		return TEXT("__unassigned__");
	}
	// Full composition deliberately has no comment store. Group-scoped comments would otherwise
	// collide or appear to belong to a single group while several groups are visible.
	return ActiveAnimationMapGroupTag.IsValid() ? ActiveAnimationMapGroupTag.ToString() : FString();
}

bool SAnimationMapPanel::CanAddComment() const
{
	return bAnimationMapGroupGraphOpen && !GetCommentScopeKey().IsEmpty();
}

void SAnimationMapPanel::CollectLiveComments(TArray<UEdGraphNode_Comment*>& OutComments) const
{
	OutComments.Reset();
	if (!GraphObj)
	{
		return;
	}
	// The group board owns no graph projection. Keep the last graph dormant behind the switcher and
	// project fresh truth when a group, the unassigned bucket, or Full Map opens.
	if (!bAnimationMapGroupGraphOpen)
	{
		return;
	}
	for (UEdGraphNode* Node : GraphObj->Nodes)
	{
		if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
		{
			OutComments.Add(Comment);
		}
	}
}

bool SAnimationMapPanel::ScreenToGraphPosition(const FVector2D& ScreenPos, FVector2D& OutGraphPos) const
{
	if (!GraphEditorWidget.IsValid())
	{
		return false;
	}
	SGraphPanel* GraphPanel = GraphEditorWidget->GetGraphPanel();
	if (!GraphPanel)
	{
		return false;
	}
	// Same absolute -> panel-local -> graph-space recipe the drop handler uses (SGraphPanel::OnDrop).
	const FVector2D PanelLocal = GraphPanel->GetTickSpaceGeometry().AbsoluteToLocal(ScreenPos);
	OutGraphPos = GraphPanel->PanelCoordToGraphCoord(PanelLocal);
	return true;
}

FReply SAnimationMapPanel::OnPreviewMouseButtonDown(const FGeometry& /*MyGeometry*/, const FPointerEvent& MouseEvent)
{
	// Record where a left-drag STARTS over the canvas so the C-key can wrap an empty-space rubber-band (the
	// stock C-key only wraps selected nodes). We never consume the event: the graph editor still runs its own
	// marquee/selection off the same press.
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && GraphEditorWidget.IsValid())
	{
		if (SGraphPanel* GraphPanel = GraphEditorWidget->GetGraphPanel())
		{
			const FVector2D ScreenPos = MouseEvent.GetScreenSpacePosition();
			if (GraphPanel->GetTickSpaceGeometry().IsUnderLocation(ScreenPos)
				&& ScreenToGraphPosition(ScreenPos, CommentDragStartGraph))
			{
				bCommentDragStartValid = true;
			}
		}
	}
	return FReply::Unhandled();
}

void SAnimationMapPanel::AddCommentForCurrentGroup()
{
	if (bGraphWriteInProgress || !GraphObj || !GraphEditorWidget.IsValid() || !CanAddComment())
	{
		return;
	}
	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset)
	{
		return;
	}
	const FString Scope = GetCommentScopeKey();
	if (Scope.IsEmpty())
	{
		return;
	}

	// Default placement priority:
	//   1. wrap the SELECTED nodes (the stock "C with selection" behavior), else
	//   2. if the user just rubber-banded EMPTY canvas, size to that drag rectangle (start = the last left-drag
	//      anchor from OnPreviewMouseButtonDown, end = the cursor at C-press), else
	//   3. a modest box at the view's paste location.
	const FVector2D CursorScreen = FSlateApplication::Get().GetCursorPos();
	FVector2D DragEndGraph = FVector2D::ZeroVector;
	FVector2D Pos(0.0, 0.0);
	FVector2D Size(400.0, 120.0);
	FSlateRect Bounds;
	if (GraphEditorWidget->GetBoundsForSelectedNodes(Bounds, /*Padding=*/50.0f))
	{
		Pos = FVector2D(Bounds.Left, Bounds.Top);
		Size = FVector2D(FMath::Max(120.0, Bounds.Right - Bounds.Left), FMath::Max(80.0, Bounds.Bottom - Bounds.Top));
	}
	else if (bCommentDragStartValid
		&& ScreenToGraphPosition(CursorScreen, DragEndGraph)
		&& FMath::Abs(DragEndGraph.X - CommentDragStartGraph.X) >= 40.0
		&& FMath::Abs(DragEndGraph.Y - CommentDragStartGraph.Y) >= 40.0)
	{
		// Rubber-band over blank canvas: wrap the dragged rectangle, normalized so any drag direction works.
		const FVector2D Min(FMath::Min(CommentDragStartGraph.X, DragEndGraph.X), FMath::Min(CommentDragStartGraph.Y, DragEndGraph.Y));
		const FVector2D Max(FMath::Max(CommentDragStartGraph.X, DragEndGraph.X), FMath::Max(CommentDragStartGraph.Y, DragEndGraph.Y));
		Pos = Min;
		Size = FVector2D(FMath::Max(120.0, Max.X - Min.X), FMath::Max(80.0, Max.Y - Min.Y));
	}
	else
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
		const FVector2f Paste = GraphEditorWidget->GetPasteLocation2f();
		Pos = FVector2D(Paste.X, Paste.Y);
#else
		Pos = GraphEditorWidget->GetPasteLocation();
#endif
	}
	bCommentDragStartValid = false; // one-shot: consume the drag anchor regardless of which branch ran

	// Match the engine's usual comment default (UEdGraphNode_Comment::CommentColor == White, which renders as
	// the familiar translucent-grey box) so a new comment reads grey from birth — not a custom blue tint.
	const FLinearColor DefaultColor = FLinearColor::White;
	const FString DefaultText(TEXT("Comment"));

	UEdGraphNode_Comment* Node = nullptr;
	{
		TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);
		BeginTransaction(LOCTEXT("AnimationMapAddComment", "Add Animation Map Comment"));

		// Live node via the shared funnel (RF_Transactional cleared; comments own no pins). Its
		// engine-assigned NodeGuid becomes the persistence handle.
		Node = UPaper2DPlusAnimationMap::SpawnNodeUntransactional<UEdGraphNode_Comment>(*GraphObj,
			[&](UEdGraphNode_Comment& N)
			{
				N.NodeComment = DefaultText;
				N.NodePosX = FMath::RoundToInt32(Pos.X);
				N.NodePosY = FMath::RoundToInt32(Pos.Y);
				N.NodeWidth = FMath::RoundToInt32(Size.X);
				N.NodeHeight = FMath::RoundToInt32(Size.Y);
				N.CommentColor = DefaultColor;
			});

		// Persist (the asset is the only transacted object — undo removes the entry, the next reconcile's
		// MaterializeComments drops the orphan node).
		FPaper2DPlusAnimationMapCommentList& List = Asset->AnimationMapComments.FindOrAdd(Scope);
		FPaper2DPlusAnimationMapComment& Entry = List.Comments.AddDefaulted_GetRef();
		Entry.CommentId = Node->NodeGuid;
		Entry.Text = Node->NodeComment;
		Entry.NodePos = FVector2D(Node->NodePosX, Node->NodePosY);
		Entry.NodeSize = FVector2D(Node->NodeWidth, Node->NodeHeight);
		Entry.Color = Node->CommentColor;

		EndTransaction();
	}

	if (Node)
	{
		GraphEditorWidget->ClearSelectionSet();
		GraphEditorWidget->SetNodeSelection(Node, true);
	}
}

void SAnimationMapPanel::ConsoleAddComment()
{
	if (!CanAddComment())
	{
		// Ensure a scoped graph is open so the comment has a home (the group board has no comment surface).
		OpenAnimationMapView(TEXT("unassigned"));
	}

	// Opening/switching a scoped group only SCHEDULES a reconcile (OpenAnimationMap* -> RequestReconcile, the
	// poll-until-applied active timer), so GraphObj->Nodes can still hold the PREVIOUS group's / board's
	// projection. Flush it synchronously BEFORE the wrap-selection below: the comment is saved under THIS
	// scope (GetCommentScopeKey), so it must be sized around THIS group's nodes — otherwise an
	// "AnimationMapView <group>" then "AddComment" sequence (or AddComment from the group board) wraps stale
	// nodes and the box reopens in the wrong place/size. No-op when the projection is already current. Codex P2.
	if (GraphObj)
	{
		ReconcileNow();
	}

	// Select every real (move/transition) node in the open group BEFORE adding so the comment WRAPS the
	// group's content (the Blueprint "C with a selection" behavior) instead of dropping a stray box at the
	// paste location. Skip existing comment nodes so a re-run doesn't grow the box around prior comments.
	// This is the test/automation + screenshot seam; an interactive "C" press still wraps the user's own
	// selection (or pastes an empty box) via the GraphEditor command unchanged.
	if (GraphEditorWidget.IsValid() && GraphObj)
	{
		GraphEditorWidget->ClearSelectionSet();
		for (UEdGraphNode* GraphNode : GraphObj->Nodes)
		{
			if (GraphNode && !GraphNode->IsA<UEdGraphNode_Comment>())
			{
				GraphEditorWidget->SetNodeSelection(GraphNode, true);
			}
		}
	}

	AddCommentForCurrentGroup();

	// AddCommentForCurrentGroup re-selects just the new comment node; frame the whole group so the wrapped
	// content is visible (zoomed in when the group is compact).
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->ZoomToFit(/*bOnlySelection=*/false);
	}
}

void SAnimationMapPanel::HandleCommentChanged(UEdGraphNode_Comment* CommentNode)
{
	if (!CommentNode || !GraphObj || CommentNode->GetGraph() != GraphObj
		|| !GraphObj->Nodes.Contains(CommentNode)
		|| GraphObj->bRebuildInProgress || bGraphWriteInProgress)
	{
		return;
	}
	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset)
	{
		return;
	}
	const FString Scope = GetCommentScopeKey();
	if (Scope.IsEmpty())
	{
		return;
	}

	// The widget's title hook fires whenever edit mode ends, including Escape-cancel, and the property
	// broadcast can echo unrelated node fields. Avoid empty transactions when the persisted record already
	// matches the transient node.
	if (const FPaper2DPlusAnimationMapCommentList* ExistingList = Asset->AnimationMapComments.Find(Scope))
	{
		const FPaper2DPlusAnimationMapComment* Existing = ExistingList->Comments.FindByPredicate(
			[CommentNode](const FPaper2DPlusAnimationMapComment& C) { return C.CommentId == CommentNode->NodeGuid; });
		if (Existing
			&& Existing->Text == CommentNode->NodeComment
			&& Existing->NodePos.Equals(FVector2D(CommentNode->NodePosX, CommentNode->NodePosY))
			&& Existing->NodeSize.Equals(FVector2D(CommentNode->NodeWidth, CommentNode->NodeHeight))
			&& Existing->Color.Equals(CommentNode->CommentColor))
		{
			return;
		}
	}

	TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);

	// MoveTo arrives inside an engine-opened transaction. Resize mouse-up and title-edit falling edge
	// arrive after/outside their engine paths, so open a local transaction when necessary. Nested
	// FScopedTransactions are reference-counted, so this is safe either way; the explicit check just avoids
	// an extra empty undo step on the move path.
	const bool bEngineTransactionOpen = GEditor && GEditor->IsTransactionActive();
	TOptional<FScopedTransaction> LocalTransaction;
	if (!bEngineTransactionOpen)
	{
		LocalTransaction.Emplace(LOCTEXT("AnimationMapEditComment", "Edit Animation Map Comment"));
	}
	Asset->Modify();

	FPaper2DPlusAnimationMapCommentList& List = Asset->AnimationMapComments.FindOrAdd(Scope);
	FPaper2DPlusAnimationMapComment* Entry = List.Comments.FindByPredicate(
		[CommentNode](const FPaper2DPlusAnimationMapComment& C) { return C.CommentId == CommentNode->NodeGuid; });
	if (!Entry)
	{
		Entry = &List.Comments.AddDefaulted_GetRef();
		Entry->CommentId = CommentNode->NodeGuid;
	}
	Entry->Text = CommentNode->NodeComment;
	Entry->NodePos = FVector2D(CommentNode->NodePosX, CommentNode->NodePosY);
	Entry->NodeSize = FVector2D(CommentNode->NodeWidth, CommentNode->NodeHeight);
	Entry->Color = CommentNode->CommentColor;
}

void SAnimationMapPanel::HandleCommentObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	// Skip intermediate color-picker drag values. The final ValueSet reaches this handler and the
	// no-change guard absorbs any echo.
	if (Event.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}
	UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Object);
	if (CommentNode && GraphObj && CommentNode->GetGraph() == GraphObj
		&& GraphObj->Nodes.Contains(CommentNode))
	{
		HandleCommentChanged(CommentNode);
	}
}

void SAnimationMapPanel::MaterializeCommentsForCurrentGroup(UPaper2DPlusCharacterProfileAsset* Asset)
{
	if (!GraphObj || !Asset)
	{
		return;
	}
	const FString Scope = GetCommentScopeKey();
	const FPaper2DPlusAnimationMapCommentList* List = Scope.IsEmpty() ? nullptr : Asset->AnimationMapComments.Find(Scope);

	TArray<UEdGraphNode_Comment*> LiveComments;
	CollectLiveComments(LiveComments);

	// Suppress write-through while we touch the graph (same discipline as the reconcile's bRebuildInProgress
	// scope): setting NodePos/Size/text directly must not re-enter HandleCommentChanged.
	TGuardValue<bool> RebuildGuard(GraphObj->bRebuildInProgress, true);

	// 1. Remove any live comment not in the current scope's store (group switch / undo / delete).
	TSet<FGuid> WantedIds;
	if (List)
	{
		for (const FPaper2DPlusAnimationMapComment& C : List->Comments)
		{
			WantedIds.Add(C.CommentId);
		}
	}
	TMap<FGuid, UEdGraphNode_Comment*> LiveById;
	for (UEdGraphNode_Comment* Live : LiveComments)
	{
		if (WantedIds.Contains(Live->NodeGuid) && !LiveById.Contains(Live->NodeGuid))
		{
			LiveById.Add(Live->NodeGuid, Live);
		}
		else
		{
			GraphObj->RemoveNode(Live);
		}
	}

	// 2. Spawn missing + refresh drifted (undo can change the stored fields under a surviving node).
	auto SpawnStoredComment = [this](const FPaper2DPlusAnimationMapComment& Stored,
		int32 PX, int32 PY, int32 W, int32 H)
	{
		UEdGraphNode_Comment* Node =
			UPaper2DPlusAnimationMap::SpawnNodeUntransactional<UEdGraphNode_Comment>(*GraphObj,
			[&](UEdGraphNode_Comment& N)
			{
				N.NodeGuid = Stored.CommentId; // restored after Finalize by the shared spawn funnel
				N.NodeComment = Stored.Text;
				N.NodePosX = PX;
				N.NodePosY = PY;
				N.NodeWidth = W;
				N.NodeHeight = H;
				N.CommentColor = Stored.Color;
			});
		// UEdGraphNode_Comment::PostPlacedNewNode runs inside Finalize and resets NodeComment plus
		// CommentColor from editor defaults. Reapply every persisted field after Finalize; the graph's
		// granular AddNode action holds the pointer and its deferred widget creation sees these values.
		Node->NodeComment = Stored.Text;
		Node->NodePosX = PX;
		Node->NodePosY = PY;
		Node->NodeWidth = W;
		Node->NodeHeight = H;
		Node->CommentColor = Stored.Color;
		return Node;
	};
	if (List)
	{
		for (const FPaper2DPlusAnimationMapComment& Stored : List->Comments)
		{
			const int32 PX = FMath::RoundToInt32(Stored.NodePos.X);
			const int32 PY = FMath::RoundToInt32(Stored.NodePos.Y);
			const int32 W = FMath::Max(1, FMath::RoundToInt32(Stored.NodeSize.X));
			const int32 H = FMath::Max(1, FMath::RoundToInt32(Stored.NodeSize.Y));
			UEdGraphNode_Comment* Node = LiveById.FindRef(Stored.CommentId);
			if (!Node)
			{
				Node = SpawnStoredComment(Stored, PX, PY, W, H);
				LiveById.Add(Stored.CommentId, Node);
			}
			else if (Node->NodePosX != PX || Node->NodePosY != PY || Node->NodeWidth != W || Node->NodeHeight != H
				|| Node->NodeComment != Stored.Text || !Node->CommentColor.Equals(Stored.Color))
			{
				// Replacing only this comment keeps SGraphNodeComment's cached size/title in sync while
				// preserving every move/transition widget. The remove/add actions are granular; never send
				// a default NotifyGraphChanged, which purges the entire SGraphPanel node cache.
				const bool bWasSelected = GraphEditorWidget.IsValid()
					&& GraphEditorWidget->GetSelectedNodes().Contains(Node);
				GraphObj->RemoveNode(Node);
				Node = SpawnStoredComment(Stored, PX, PY, W, H);
				LiveById.Add(Stored.CommentId, Node);
				if (bWasSelected && GraphEditorWidget.IsValid())
				{
					GraphEditorWidget->SetNodeSelection(Node, true);
				}
			}
		}
	}
}

// =====================================================================================================
// RECONCILE TRIGGERS
// =====================================================================================================

void SAnimationMapPanel::HandleAssetChangedSignal()
{
	QueueAnimationMapRefresh();
	if (!bAnimationMapGroupGraphOpen)
	{
		// The board signature absorbs coarse explicit/deferred signal echoes. A scoped graph will
		// project the latest asset truth when the designer opens it.
		return;
	}

	// Guard order per the plan's three-guard stack, instance (1): a trigger landing mid-gesture PENDS —
	// a bare return would silently drop a genuinely-external edit arriving during our own transaction.
	//
	// PhaseTag writes also run through here. DiffProjection reports the move field and every existing
	// target pill separately from edge identity, so ReconcileNow updates lambda-bound pill state in
	// place without rebuilding edges or move cards.
	if (HasActiveTransaction() || bGraphWriteInProgress)
	{
		bReconcilePending = true;
		return;
	}
	RequestReconcile();
}

void SAnimationMapPanel::RequestReconcile()
{
	bReconcilePending = true;
	// Coalesced: one live timer regardless of how many triggers landed. The timer is the ONLY caller
	// of ReconcileNow(), and Slate executes widget active timers from SWidget::Paint
	// (SWidget.cpp:1432-1436) — so a backgrounded tab's pending reconcile waits, then applies on the
	// first paint after foregrounding (the hidden-tab contract for free).
	if (!ReconcileTimerHandle.IsValid())
	{
		ReconcileTimerHandle = RegisterActiveTimer(0.0f,
			FWidgetActiveTimerDelegate::CreateSP(this, &SAnimationMapPanel::OnReconcileTimer));
	}
}

bool SAnimationMapPanel::IsGestureLive() const
{
	// Gesture-liveness poll (three-guard stack, instance (3)): a rebuild under a live FDragConnection
	// is a crash; an escape-cancelled drag produces NO drop event, hence poll-until-applied.
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().IsDragDropping())
	{
		return true; // any Slate drag-drop op, incl. a live wire drag (FDragConnection)
	}
	if (GraphEditorWidget.IsValid())
	{
		// Node drags / marquee hold mouse capture on the graph panel (SNodePanel mouse handling).
		if (SGraphPanel* GraphPanel = GraphEditorWidget->GetGraphPanel())
		{
			if (GraphPanel->HasMouseCapture())
			{
				return true;
			}
		}
	}
	if (GEditor && GEditor->IsTransactionActive())
	{
		return true; // an engine-opened transaction is mid-gesture (wire drop, node move finalize, ...)
	}
	if (GIsTransacting)
	{
		return true; // an undo/redo restore is in flight — nothing may touch the graph or the asset
	}
	return HasActiveTransaction() || bGraphWriteInProgress;
}

EActiveTimerReturnType SAnimationMapPanel::OnReconcileTimer(double /*InCurrentTime*/, float /*InDeltaTime*/)
{
	if (!bReconcilePending)
	{
		return EActiveTimerReturnType::Stop;
	}
	if (IsGestureLive())
	{
		return EActiveTimerReturnType::Continue; // poll until quiet
	}
	ReconcileNow();
	// ReconcileNow clears the flag at START; a re-pend (defensive GIsTransacting race) keeps polling.
	return bReconcilePending ? EActiveTimerReturnType::Continue : EActiveTimerReturnType::Stop;
}

// =====================================================================================================
// THE RECONCILE (R9 contract: full-diff early-out -> identity-preserving move/edge surgery)
// =====================================================================================================

void SAnimationMapPanel::CollectLiveNodes(TMap<FString, UPaper2DPlusAnimationMapNode_Move*>& OutMovesByLower,
	TArray<UPaper2DPlusAnimationMapNode_Transition*>& OutEdges) const
{
	if (!GraphObj)
	{
		return;
	}
	for (UEdGraphNode* Node : GraphObj->Nodes)
	{
		if (UPaper2DPlusAnimationMapNode_Move* MoveNode = Cast<UPaper2DPlusAnimationMapNode_Move>(Node))
		{
			OutMovesByLower.Add(MoveNode->MoveName.ToLower(), MoveNode);
		}
		else if (UPaper2DPlusAnimationMapNode_Transition* EdgeNode = Cast<UPaper2DPlusAnimationMapNode_Transition>(Node))
		{
			OutEdges.Add(EdgeNode);
		}
	}
}

void SAnimationMapPanel::ReconcileNow()
{
	// Defensive re-check (the timer already gates on this): never mutate the projection while a
	// transaction restore is in flight. Re-pend so the live timer retries.
	if (GIsTransacting)
	{
		bReconcilePending = true;
		return;
	}

	// Cleared at reconcile START (coalescing contract): a trigger landing mid-reconcile re-pends and
	// the timer runs us again.
	bReconcilePending = false;

	if (!GraphObj)
	{
		return;
	}
	// The group board owns no graph projection. A timer queued just before Back to Groups can still
	// arrive after the switcher changes; leave the last scoped graph dormant instead of interpreting
	// the missing group filter as a request to materialize the entire unscoped map behind the board.
	if (!bAnimationMapGroupGraphOpen)
	{
		return;
	}

	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset(); // strictly per-use (Base-Profile-swap rule)

	using namespace Paper2DPlusAnimationMap;
	FGraphProjection Current = ProjectGraphForCurrentView(Asset); // null asset -> empty projection (graph empties)

	TMap<FString, UPaper2DPlusAnimationMapNode_Move*> LiveMovesByLower;
	TArray<UPaper2DPlusAnimationMapNode_Transition*> LiveEdges;
	CollectLiveNodes(LiveMovesByLower, LiveEdges);

	// --- Caller-supplied diff dimensions (the pure core stays UEdGraph-free) ---
	// Position drift: any in-place node whose NodePosX/Y differs from map-else-session-cache.
	bool bAnyPositionDrift = false;
	for (const TPair<FString, UPaper2DPlusAnimationMapNode_Move*>& Pair : LiveMovesByLower)
	{
		const FVector2D* Expected =
			Asset ? Asset->AnimationMapNodePositions.Find(Pair.Key) : nullptr;
		if (!Expected)
		{
			Expected = SessionPositionCache.Find(Pair.Key);
		}
		if (Expected
			&& (Pair.Value->NodePosX != FMath::RoundToInt32(Expected->X)
				|| Pair.Value->NodePosY != FMath::RoundToInt32(Expected->Y)))
		{
			bAnyPositionDrift = true;
			break;
		}
	}

	// Flipbook-pointer changes: the resolved UPaperFlipbook* now vs what the node last displayed
	// (panel-side map). The refresh path replaces only each changed card's thumbnail child.
	bool bAnyFlipbookPtrChanged = false;
	TArray<UPaper2DPlusAnimationMapNode_Move*> FlipbookChangedNodes;
	for (const TPair<FString, UPaper2DPlusAnimationMapNode_Move*>& Pair : LiveMovesByLower)
	{
		if (const TWeakObjectPtr<UPaperFlipbook>* Last = LastResolvedFlipbookByName.Find(Pair.Key))
		{
			UPaperFlipbook* Now = AnimationMapPanel_ResolveFlipbook(Asset, Pair.Value->MoveName);
			if (Last->Get() != Now)
			{
				bAnyFlipbookPtrChanged = true;
				FlipbookChangedNodes.Add(Pair.Value);
			}
		}
	}

	const FProjectionDiff Diff = DiffProjection(LastProjection, Current, bAnyPositionDrift, bAnyFlipbookPtrChanged);

	// The edge-removal hook flags live-graph divergence the DATA diff cannot see (an external pin
	// break orphans an edge node while the rows are unchanged) — consume it here.
	const bool bForceTopology = bForceTopologyRepair;
	bForceTopologyRepair = false;

	// Lowered names spawned THIS reconcile (a rebuild / brand-new move) — ReapplyPositions restores/places
	// these from the stored position, and leaves every other (in-place) node's user-moved position alone.
	const TSet<FString> NewlyAddedLower(Diff.MovesToAdd);

	// --- FULL-diff early-out: the own-echo no-op path. Comments/markers carry graph-only identity
	// that is intentionally outside FGraphProjection, so their granular no-op synchronizers still run. ---
	if (Diff.IsEmpty() && !bForceTopology)
	{
		MaterializeCommentsForCurrentGroup(Asset);
		// Chain-start/-end rework: markers sync on BOTH reconcile paths (the comments shape) — the
		// early-out means no flag/topology change, so this is normally a pure no-op walk; it exists
		// for graph-only marker divergence (e.g. a group reopen whose flags echo empty-diff).
		SynchronizeChainStartMarkers(Asset, Current);
		SynchronizeChainEndMarkers(Asset, Current);
		LastProjection = MoveTemp(Current);
		if (bPendingFitToView && GraphEditorWidget.IsValid())
		{
			bPendingFitToView = false;
			GraphEditorWidget->ZoomToFit(/*bOnlySelection=*/false);
		}
		return;
	}

	const bool bMoveNodeSetChanged = Diff.MovesToAdd.Num() > 0
		|| Diff.MovesToRemove.Num() > 0
		|| Diff.StubFlagFlips.Num() > 0;
	const bool bEdgeIdentitySetChanged = Diff.EdgesToAdd.Num() > 0 || Diff.EdgesToRemove.Num() > 0;
	const bool bNeedsEdgeReconcile = bForceTopology
		|| bMoveNodeSetChanged
		|| !Diff.bEdgesEqual;
	const bool bGraphObjectsChanged = bMoveNodeSetChanged || bEdgeIdentitySetChanged;

	// Only a stub<->real flip recreates a surviving move object (its pin set changes). Capture those
	// selections by lowered identity; all transition identities now survive in place and keep native
	// selection, including row reorder and endpoint-adjacent move recreation.
	TArray<FString> SelectedMoveLowers;
	if (Diff.StubFlagFlips.Num() > 0 && GraphEditorWidget.IsValid())
	{
		for (UObject* SelectedObject : GraphEditorWidget->GetSelectedNodes())
		{
			if (const UPaper2DPlusAnimationMapNode_Move* MoveNode = Cast<UPaper2DPlusAnimationMapNode_Move>(SelectedObject))
			{
				SelectedMoveLowers.Add(MoveNode->MoveName.ToLower());
			}
		}
	}

	// View snapshot around rebuilds (the U7 shim picks the FVector2f form on 5.6+, FVector2D before).
	FVector2f ViewLocation = FVector2f::ZeroVector;
	float ZoomAmount = 1.0f;
	if (bGraphObjectsChanged && GraphEditorWidget.IsValid())
	{
		AnimationMapPanel_GetViewLocation(GraphEditorWidget, ViewLocation, ZoomAmount);
	}

	TArray<UPaper2DPlusAnimationMapNode_Move*> ThumbnailRefreshNodes;
	TArray<UPaper2DPlusAnimationMapNode_Move*> TagChipRefreshNodes;
	TArray<UPaper2DPlusAnimationMapNode_Move*> MoveTitleRefreshNodes;
	TArray<UPaper2DPlusAnimationMapNode_Transition*> EdgeLayoutRefreshNodes;
	TArray<UPaper2DPlusAnimationMapNode_Transition*> EdgesNeedingMidpointSeed;

	{
		// bRebuildInProgress for the WHOLE reconcile scope (three-guard stack, instance (2)): the
		// suicide hook and the schema's conversion hook early-out while the projection is half-built.
		TGuardValue<bool> RebuildGuard(GraphObj->bRebuildInProgress, true);

		// Current-projection lookup (lowered name -> projected node) for spawn/update payloads.
		TMap<FString, const FProjectedNode*> CurrentByLower;
		CurrentByLower.Reserve(Current.Nodes.Num());
		for (const FProjectedNode& Node : Current.Nodes)
		{
			CurrentByLower.Add(Node.MoveNameLower, &Node);
		}

		// Stamp every display field BEFORE a newly-created graph node is finalized. AddNode emits a
		// granular graph action immediately, so its first widget must already have the final thumbnail,
		// tags, badges, and filter state; no follow-up whole-card rebuild is necessary.
		auto StampNewMove = [this, Asset](UPaper2DPlusAnimationMapNode_Move& Node, const FProjectedNode& Projected)
		{
			Node.MoveName = Projected.DisplayName;
			Node.bIsStub = Projected.bIsStub;
			Node.bIsChainStart = Projected.bIsChainStart;
			Node.bIsChainEnd = Projected.bIsChainEnd;
			Node.PhaseTag = Projected.PhaseTag;
			Node.ComboSpineIndex = Projected.ComboSpineIndex;
			Node.ComboSpineLength = Projected.ComboSpineLength;
			Node.OwnAnimationTags = Projected.OwnAnimationTags;
			Node.ChainInheritedAnimationTags = Projected.ChainInheritedAnimationTags;
			Node.GroupImpliedAnimationTags = Projected.GroupImpliedAnimationTags;
			Node.bDimmedByFilter = MapFilterTag.IsValid()
				&& !Paper2DPlusAnimationMap::AnimationTagFilterMatches(Projected.EffectiveAnimationTags(), MapFilterTag);
			Node.ResolvedFlipbook = AnimationMapPanel_ResolveFlipbookForDisplay(Asset, Projected.DisplayName);
		};

		// ---- Move nodes: diffed IN PLACE by lowered name ----
		for (const FString& NameLower : Diff.MovesToRemove)
		{
			if (UPaper2DPlusAnimationMapNode_Move* Node = LiveMovesByLower.FindRef(NameLower))
			{
				GraphObj->RemoveNode(Node);
				LiveMovesByLower.Remove(NameLower);
			}
		}

		// Stub-flag flips change the pin set, so recreate only that move while preserving position.
		for (const FString& NameLower : Diff.StubFlagFlips)
		{
			UPaper2DPlusAnimationMapNode_Move* OldNode = LiveMovesByLower.FindRef(NameLower);
			const FProjectedNode* Projected = CurrentByLower.FindRef(NameLower);
			if (!OldNode || !Projected)
			{
				continue;
			}
			const int32 KeepPosX = OldNode->NodePosX;
			const int32 KeepPosY = OldNode->NodePosY;
			GraphObj->RemoveNode(OldNode);
			UPaper2DPlusAnimationMapNode_Move* NewNode =
				UPaper2DPlusAnimationMap::SpawnNodeUntransactional<UPaper2DPlusAnimationMapNode_Move>(*GraphObj,
					[Projected, &StampNewMove](UPaper2DPlusAnimationMapNode_Move& Node)
					{
						StampNewMove(Node, *Projected);
					});
			NewNode->NodePosX = KeepPosX;
			NewNode->NodePosY = KeepPosY;
			LiveMovesByLower.Add(NameLower, NewNode);
		}

		// Adds land in the position reapply pass below.
		for (const FString& NameLower : Diff.MovesToAdd)
		{
			const FProjectedNode* Projected = CurrentByLower.FindRef(NameLower);
			if (!Projected)
			{
				continue;
			}
			UPaper2DPlusAnimationMapNode_Move* NewNode =
				UPaper2DPlusAnimationMap::SpawnNodeUntransactional<UPaper2DPlusAnimationMapNode_Move>(*GraphObj,
					[Projected, &StampNewMove](UPaper2DPlusAnimationMapNode_Move& Node)
					{
						StampNewMove(Node, *Projected);
					});
			LiveMovesByLower.Add(NameLower, NewNode);
		}

		// The title is an attribute bound through SNodeTitle; layout/paint invalidation is sufficient.
		for (const FString& NameLower : Diff.DisplayNameChanges)
		{
			UPaper2DPlusAnimationMapNode_Move* Node = LiveMovesByLower.FindRef(NameLower);
			const FProjectedNode* Projected = CurrentByLower.FindRef(NameLower);
			if (Node && Projected)
			{
				Node->MoveName = Projected->DisplayName;
				MoveTitleRefreshNodes.AddUnique(Node);
			}
		}

		// ---- Edge nodes: preserve every surviving (From, To) object; restamp row handles in place. ----
		if (bNeedsEdgeReconcile || Diff.DisplayNameChanges.Num() > 0)
		{
			const TArray<UPaper2DPlusAnimationMapNode_Transition*> ExistingLiveEdges = LiveEdges;
			LiveEdges.Reset();
			TSet<UPaper2DPlusAnimationMapNode_Transition*> MatchedEdges;

			for (const FProjectedEdge& Edge : Current.Edges)
			{
				UPaper2DPlusAnimationMapNode_Move* FromNode = LiveMovesByLower.FindRef(Edge.FromMoveLower);
				UPaper2DPlusAnimationMapNode_Move* ToNode = LiveMovesByLower.FindRef(Edge.TargetMoveLower);
				if (!FromNode || !ToNode)
				{
					continue; // projection guarantees both ends; defensive against a half-failed spawn
				}

				// The LIVE row (display-case target for the denormalized snapshot), resolved by the
				// (From, To) pair through the same first-match contract the projection used — the
				// projection no longer carries a row index (edge identity = the pair, U2); the node's
				// RowIndex handle is re-stamped from this resolve.
				const FFlipbookProfileEntry* FromEntry = AnimationMapPanel_FindEntry(Asset, FromNode->MoveName);
				const int32 ResolvedRowIndex = Paper2DPlusAnimationMap::FindTransitionRowIndex(
					Asset, FromNode->MoveName, ToNode->MoveName);
				if (!FromEntry || !FromEntry->TransitionData.Transitions.IsValidIndex(ResolvedRowIndex))
				{
					continue; // data shifted mid-reconcile would re-signal; never stamp a bad handle
				}
				const FPaper2DPlusMoveTransition& Row = FromEntry->TransitionData.Transitions[ResolvedRowIndex];

				UPaper2DPlusAnimationMapNode_Transition* EdgeNode = nullptr;
				for (UPaper2DPlusAnimationMapNode_Transition* Candidate : ExistingLiveEdges)
				{
					if (Candidate && !MatchedEdges.Contains(Candidate)
						&& Candidate->FromMove.Equals(Edge.FromMoveLower, ESearchCase::IgnoreCase)
						&& Candidate->TargetMove.Equals(Edge.TargetMoveLower, ESearchCase::IgnoreCase))
					{
						EdgeNode = Candidate;
						break;
					}
				}

				if (EdgeNode)
				{
					const bool bPhaseChanged = EdgeNode->TransitionPhaseTag != Edge.TransitionPhaseTag;
					const bool bRowIndexChanged = EdgeNode->RowIndex != ResolvedRowIndex;
					EdgeNode->SetFromRow(FromNode->MoveName, ResolvedRowIndex, Row, Edge.TransitionPhaseTag);
					if (bPhaseChanged || bRowIndexChanged)
					{
						EdgeLayoutRefreshNodes.AddUnique(EdgeNode);
					}
					if (EdgeNode->GetPreviousMoveNode() != FromNode || EdgeNode->GetNextMoveNode() != ToNode)
					{
						// CreateConnections' empty-list contract is safe for fresh nodes. A surviving node
						// must first remove reciprocal links from the old endpoints.
						if (UEdGraphPin* InputPin = EdgeNode->GetInputPin())
						{
							InputPin->BreakAllPinLinks(/*bNotifyNodes=*/false);
						}
						if (UEdGraphPin* OutputPin = EdgeNode->GetOutputPin())
						{
							OutputPin->BreakAllPinLinks(/*bNotifyNodes=*/false);
						}
						EdgeNode->CreateConnections(FromNode, ToNode);
						EdgesNeedingMidpointSeed.AddUnique(EdgeNode);
					}
				}
				else
				{
					const FString FromDisplayName = FromNode->MoveName;
					const int32 RowIndex = ResolvedRowIndex;
					const FGameplayTag TransitionPhaseTag = Edge.TransitionPhaseTag;
					EdgeNode = UPaper2DPlusAnimationMap::SpawnNodeUntransactional<UPaper2DPlusAnimationMapNode_Transition>(
						*GraphObj,
						[&FromDisplayName, RowIndex, &Row, &TransitionPhaseTag](
							UPaper2DPlusAnimationMapNode_Transition& Node)
						{
							Node.SetFromRow(FromDisplayName, RowIndex, Row, TransitionPhaseTag);
						});
					EdgeNode->CreateConnections(FromNode, ToNode);
					EdgesNeedingMidpointSeed.Add(EdgeNode);
				}
				MatchedEdges.Add(EdgeNode);
				LiveEdges.Add(EdgeNode);
			}

			for (UPaper2DPlusAnimationMapNode_Transition* ExistingEdge : ExistingLiveEdges)
			{
				if (ExistingEdge && !MatchedEdges.Contains(ExistingEdge))
				{
					GraphObj->RemoveNode(ExistingEdge);
				}
			}
		}
		else
		{
			// Phase-only display work does not enter the row-handle/topology path.
			for (const FProjectedEdge& ChangedEdge : Diff.EdgePhaseChanges)
			{
				for (UPaper2DPlusAnimationMapNode_Transition* EdgeNode : LiveEdges)
				{
					if (EdgeNode
						&& EdgeNode->FromMove.Equals(ChangedEdge.FromMoveLower, ESearchCase::IgnoreCase)
						&& EdgeNode->TargetMove.Equals(ChangedEdge.TargetMoveLower, ESearchCase::IgnoreCase))
					{
						EdgeNode->TransitionPhaseTag = ChangedEdge.TransitionPhaseTag;
						EdgeLayoutRefreshNodes.AddUnique(EdgeNode);
						break;
					}
				}
			}
		}

		// These child-only refresh lists deliberately run for BOTH paths. A topology edit can arrive in
		// the same model signal as a tag or reimport change; surviving move widgets still need their
		// chips/thumbnail children refreshed even though other nodes were added or removed. Freshly
		// spawned/recreated nodes already received their final fields before Finalize and have no live
		// widget yet, so only pointers that still match the post-diff live map are queued here.
		for (UPaper2DPlusAnimationMapNode_Move* Node : FlipbookChangedNodes)
		{
			if (Node && LiveMovesByLower.FindRef(Node->MoveName.ToLower()) == Node)
			{
				ThumbnailRefreshNodes.AddUnique(Node);
			}
		}
		for (const FString& NameLower : Diff.AnimationTagChanges)
		{
			if (UPaper2DPlusAnimationMapNode_Move* Node = LiveMovesByLower.FindRef(NameLower))
			{
				TagChipRefreshNodes.AddUnique(Node);
			}
		}
		// ---- Positions: ordinary nodes use map-else-session-cache; position-less arrivals use
		// transient FirstLayout only.
		ReapplyPositions(LiveMovesByLower, Asset, NewlyAddedLower);

		// ---- Seed only fresh or rewired edge pills at the endpoint midpoint.
		// A fresh pill starts at NodePosX/Y = (0,0) and otherwise has no initial position — a pill has
		// RequiresSecondPassLayout()==true + a no-op MoveTo, so its ONLY position
		// source is PerformSecondPassLayout. But SNodePanel runs the second pass ONLY over non-culled
		// VisibleChildren (SNodePanel.cpp:341-365), and IsNodeCulled tests the node's own NodePosX/Y
		// against the viewport guard band (:1690-1704). With the view restored to the user's nodes (away
		// from the graph origin, see AnimationMapPanel_SetViewLocation below), a pill left at (0,0) is
		// culled -> its second pass never runs -> it stays at (0,0) -> stays culled: a deadlock where the
		// pill never paints (bubble disappears) or arranges for hit-testing (unselectable). Seeding the
		// pill between its two endpoints (both on-screen) keeps it un-culled so the second pass runs and
		// refines it onto the wire. Mirrors the engine's own anim-SM seeding (a transition spawns at the
		// two states' midpoint before CreateConnections — AnimationStateMachineSchema.cpp:253-254).
		for (UPaper2DPlusAnimationMapNode_Transition* EdgeNode : EdgesNeedingMidpointSeed)
		{
			const UPaper2DPlusAnimationMapNode_Move* PrevNode = EdgeNode ? EdgeNode->GetPreviousMoveNode() : nullptr;
			const UPaper2DPlusAnimationMapNode_Move* NextNode = EdgeNode ? EdgeNode->GetNextMoveNode() : nullptr;
			if (PrevNode && NextNode)
			{
				EdgeNode->NodePosX = (PrevNode->NodePosX + NextNode->NodePosX) / 2;
				EdgeNode->NodePosY = (PrevNode->NodePosY + NextNode->NodePosY) / 2;
			}
		}

		MaterializeCommentsForCurrentGroup(Asset);

		// Chain-start/-end rework: marker syncs are projection-driven, so they need no
		// stamp-loop ordering). Run AFTER ReapplyPositions (a fresh targeted marker's default spot
		// derives from its move node's final position). Granular AddNode/RemoveNode actions build or
		// remove only marker widgets; link-only repairs invalidate the connection layer's paint.
		SynchronizeChainStartMarkers(Asset, Current);
		SynchronizeChainEndMarkers(Asset, Current);
	} // RebuildGuard released

	// ---- U6: stamp the DISPLAY flipbook on every live move before the targeted child refreshes.
	// Topology-path widgets are created by granular AddNode actions and read the same final stamp.
	// One-time LoadSynchronous per unloaded move — display state only, weak-held, nothing rooted.
	// TASK-108 U6: the projected tag containers + filter dim flag are stamped in the same loop (the
	// widgets' chips row + dim lambdas read them; provenance comes from the projection's one
	// BuildAnimationTagMap batch, never from the asset at paint time).
	TMap<FString, const Paper2DPlusAnimationMap::FProjectedNode*> StampByLower;
	StampByLower.Reserve(Current.Nodes.Num());
	for (const Paper2DPlusAnimationMap::FProjectedNode& Node : Current.Nodes)
	{
		StampByLower.Add(Node.MoveNameLower, &Node);
	}
	for (const TPair<FString, UPaper2DPlusAnimationMapNode_Move*>& Pair : LiveMovesByLower)
	{
		Pair.Value->ResolvedFlipbook = AnimationMapPanel_ResolveFlipbookForDisplay(Asset, Pair.Value->MoveName);
		// U6 (R6/R8): denormalize the tag provenance + refresh the filter dim.
		if (const Paper2DPlusAnimationMap::FProjectedNode* const* Projected = StampByLower.Find(Pair.Key))
		{
			Pair.Value->bIsChainStart = (*Projected)->bIsChainStart;
			Pair.Value->bIsChainEnd = (*Projected)->bIsChainEnd;
			Pair.Value->PhaseTag = (*Projected)->PhaseTag;
			Pair.Value->ComboSpineIndex = (*Projected)->ComboSpineIndex;
			Pair.Value->ComboSpineLength = (*Projected)->ComboSpineLength;
			Pair.Value->OwnAnimationTags = (*Projected)->OwnAnimationTags;
			Pair.Value->ChainInheritedAnimationTags = (*Projected)->ChainInheritedAnimationTags;
			Pair.Value->GroupImpliedAnimationTags = (*Projected)->GroupImpliedAnimationTags;
			Pair.Value->bDimmedByFilter = MapFilterTag.IsValid()
				&& !Paper2DPlusAnimationMap::AnimationTagFilterMatches((*Projected)->EffectiveAnimationTags(), MapFilterTag);
		}
		else
		{
			Pair.Value->bIsChainStart = false;
			Pair.Value->bIsChainEnd = false;
			Pair.Value->PhaseTag = FGameplayTag();
			Pair.Value->ComboSpineIndex = INDEX_NONE;
			Pair.Value->ComboSpineLength = 0;
			Pair.Value->OwnAnimationTags.Reset();
			Pair.Value->ChainInheritedAnimationTags.Reset();
			Pair.Value->GroupImpliedAnimationTags.Reset();
			Pair.Value->bDimmedByFilter = MapFilterTag.IsValid(); // no projection entry = no tags = no match
		}
	}
	if ((Diff.ChainStartChanges.Num() > 0
		|| Diff.ComboSpineChanges.Num() > 0)
		&& GraphEditorWidget.IsValid())
	{
		if (SGraphPanel* GraphPanel = GraphEditorWidget->GetGraphPanel())
		{
			GraphPanel->Invalidate(EInvalidateWidgetReason::Paint);
		}
	}

	// ---- Field-only Slate refreshes AFTER the stamps complete ----
	// These reach the existing node widgets directly. Tag edits replace only the chips child;
	// thumbnail pointer edits replace only the thumbnail child; phase edits merely invalidate the
	// already lambda-bound pill. This path is identical on UE 5.0-5.8 and never falls back to the
	// default graph notification that purges every widget.
	if (GraphEditorWidget.IsValid())
	{
		if (SGraphPanel* GraphPanel = GraphEditorWidget->GetGraphPanel())
		{
			if (EdgesNeedingMidpointSeed.Num() > 0 || EdgeLayoutRefreshNodes.Num() > 0)
			{
				GraphPanel->Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
			}
			for (UPaper2DPlusAnimationMapNode_Move* Node : ThumbnailRefreshNodes)
			{
				if (Node)
				{
					if (TSharedPtr<SGraphNode> Widget = GraphPanel->GetNodeWidgetFromGuid(Node->NodeGuid))
					{
						StaticCastSharedPtr<SAnimationMapMoveNode>(Widget)->RefreshThumbnail();
					}
				}
			}
			for (UPaper2DPlusAnimationMapNode_Move* Node : TagChipRefreshNodes)
			{
				if (Node)
				{
					if (TSharedPtr<SGraphNode> Widget = GraphPanel->GetNodeWidgetFromGuid(Node->NodeGuid))
					{
						StaticCastSharedPtr<SAnimationMapMoveNode>(Widget)->RefreshTagChips();
					}
				}
			}
			for (UPaper2DPlusAnimationMapNode_Move* Node : MoveTitleRefreshNodes)
			{
				if (Node)
				{
					if (TSharedPtr<SGraphNode> Widget = GraphPanel->GetNodeWidgetFromGuid(Node->NodeGuid))
					{
						Widget->Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
					}
				}
			}
			for (UPaper2DPlusAnimationMapNode_Transition* EdgeNode : EdgeLayoutRefreshNodes)
			{
				if (EdgeNode)
				{
					if (TSharedPtr<SGraphNode> Widget = GraphPanel->GetNodeWidgetFromGuid(EdgeNode->NodeGuid))
					{
						Widget->Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
					}
				}
			}
		}
	}
	if (SelectedMoveLowers.Num() > 0 && GraphEditorWidget.IsValid())
	{
		// Granular RemoveNode drops only dead selections. Re-select only stub-flipped moves whose pin
		// shape required object recreation; surviving moves, edges, and comments never churn.
		for (const FString& MoveLower : SelectedMoveLowers)
		{
			if (UPaper2DPlusAnimationMapNode_Move* MoveNode = LiveMovesByLower.FindRef(MoveLower))
			{
				if (!GraphEditorWidget->GetSelectedNodes().Contains(MoveNode))
				{
					GraphEditorWidget->SetNodeSelection(MoveNode, true);
				}
			}
		}
	}

	// Only object add/remove work can perturb the panel's view bookkeeping.
	if (bGraphObjectsChanged && GraphEditorWidget.IsValid())
	{
		AnimationMapPanel_SetViewLocation(GraphEditorWidget, ViewLocation, ZoomAmount);
	}

	// Audit F14: on an explicit group/unassigned OPEN, frame the scoped nodes once — the view-restore above
	// would otherwise leave the viewport at a stale/default origin, so the graph appeared blank with no easy
	// way to re-center. ZoomToFit defers internally to the next tick, so it works even though topology nodes
	// are created on the deferred purge. FocusMoveNodeByFlipbookIndex clears the flag (it does its own jump).
	if (bPendingFitToView && GraphEditorWidget.IsValid())
	{
		bPendingFitToView = false;
		GraphEditorWidget->ZoomToFit(/*bOnlySelection=*/false);
	}

	LastProjection = MoveTemp(Current);
	if (RefreshSelectionCohortSummary() && SelectionCohortCard.IsValid())
	{
		SelectionCohortCard->Invalidate(EInvalidateWidgetReason::Layout);
	}

	// Refresh the flipbook-object bookkeeping for the final node set.
	LastResolvedFlipbookByName.Reset();
	for (const TPair<FString, UPaper2DPlusAnimationMapNode_Move*>& Pair : LiveMovesByLower)
	{
		LastResolvedFlipbookByName.Add(Pair.Key, AnimationMapPanel_ResolveFlipbook(Asset, Pair.Value->MoveName));
	}
}

void SAnimationMapPanel::ReapplyPositions(const TMap<FString, UPaper2DPlusAnimationMapNode_Move*>& LiveMovesByLower,
	UPaper2DPlusCharacterProfileAsset* Asset, const TSet<FString>& NewlyAddedLower)
{
	// Occupied = ordinary stored map UNION session cache.
	TMap<FString, FVector2D> Occupied;
	if (Asset)
	{
		Occupied = Asset->AnimationMapNodePositions;
	}
	for (const TPair<FString, FVector2D>& Pair : SessionPositionCache)
	{
		if (!Occupied.Contains(Pair.Key))
		{
			Occupied.Add(Pair.Key, Pair.Value);
		}
	}

	// Position-less nodes get FirstLayout results — written to the SESSION-TRANSIENT cache ONLY
	// (never the asset; opening the tab never dirties, R4/R10).
	bool bAnyMissing = false;
	for (const TPair<FString, UPaper2DPlusAnimationMapNode_Move*>& Pair : LiveMovesByLower)
	{
		if (!Occupied.Contains(Pair.Key))
		{
			bAnyMissing = true;
			break;
		}
	}
	if (bAnyMissing)
	{
		TSet<FString> MoveFilterLower;
		const bool bUseFilter = BuildAnimationMapGraphFilter(Asset, MoveFilterLower);
		const TMap<FString, FVector2D> Fresh = Paper2DPlusAnimationMap::FirstLayout(
			Asset, Occupied, bUseFilter ? &MoveFilterLower : nullptr);
		for (const TPair<FString, FVector2D>& Pair : Fresh)
		{
			SessionPositionCache.Add(Pair.Key, Pair.Value);
			if (!Occupied.Contains(Pair.Key))
			{
				Occupied.Add(Pair.Key, Pair.Value);
			}
		}
	}

	// Apply with USER-MOVE STICKINESS (the fix for "nodes shuffle themselves"). A node is RE-ASSERTED to
	// its stored position ONLY when that is correct:
	//   (a) it was freshly spawned this reconcile (NewlyAddedLower, or never applied before) — a rebuild /
	//       first placement must restore/place it; or
	//   (b) the stored value legitimately CHANGED since we last applied it (undo / external edit / a
	//       committed drag) — re-assert it (this is what keeps undo-of-first-move restoring the auto spot).
	// Otherwise, if the node's live position drifted while the stored value is UNCHANGED, the USER moved it
	// (a drag that never reached the durable AnimationMapNodePositions map) — KEEP the live position and adopt
	// it (session-only nodes into the session cache) so it never snaps back to the FirstLayout grid.
	// DIRECT NodePosX/Y assignment — NEVER SGraphNode::MoveTo, which re-enters U5's write-through.
	bool bAnyApplied = false;
	for (const TPair<FString, UPaper2DPlusAnimationMapNode_Move*>& Pair : LiveMovesByLower)
	{
		const FString& Key = Pair.Key;
		UPaper2DPlusAnimationMapNode_Move* Node = Pair.Value;

		const bool bDurable =
			Asset && Asset->AnimationMapNodePositions.Contains(Key);
		const FVector2D* Stored = Occupied.Find(Key); // asset value if durable, else session
		const FVector2D* LastApplied = LastAppliedPositions.Find(Key);

		const bool bFreshlySpawned = NewlyAddedLower.Contains(Key) || LastApplied == nullptr;
		const bool bStoredChanged = Stored && LastApplied
			&& (FMath::RoundToInt32(Stored->X) != FMath::RoundToInt32(LastApplied->X)
				|| FMath::RoundToInt32(Stored->Y) != FMath::RoundToInt32(LastApplied->Y));

		if (Stored && (bFreshlySpawned || bStoredChanged))
		{
			const int32 NewPosX = FMath::RoundToInt32(Stored->X);
			const int32 NewPosY = FMath::RoundToInt32(Stored->Y);
			if (Node->NodePosX != NewPosX || Node->NodePosY != NewPosY)
			{
				Node->NodePosX = NewPosX;
				Node->NodePosY = NewPosY;
				bAnyApplied = true;
			}
			LastAppliedPositions.Add(Key, FVector2D(Node->NodePosX, Node->NodePosY));
		}
		else
		{
			// Stored unchanged + not freshly spawned: a drifted live position is a USER MOVE — keep it.
			// A session-only node's cache follows the user (so the drift check sees no drift next reconcile);
			// a durable node's committed value already equals live, so this is a no-op for it.
			const FVector2D Live(Node->NodePosX, Node->NodePosY);
			if (!bDurable)
			{
				SessionPositionCache.Add(Key, Live);
			}
			LastAppliedPositions.Add(Key, Live);
		}
	}

	// SNodePanel arranges from NodePosX/Y each paint — invalidate so invalidation-panel setups repaint
	// without waiting for an interaction (targeted refresh; no widget rebuild).
	if (bAnyApplied && GraphEditorWidget.IsValid())
	{
		if (SGraphPanel* GraphPanel = GraphEditorWidget->GetGraphPanel())
		{
			GraphPanel->Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
		}
	}
}

// =====================================================================================================
// U3/U5 GRAPH-DELEGATE HANDLERS
// =====================================================================================================

bool SAnimationMapPanel::HandleWireCreateRequested(const FString& FromMoveName, const FString& ToMoveName)
{
	// WIRE CREATE (R5, F1/AE1, R14 self-loops). The schema's conversion hook calls this INSIDE the
	// engine's open GraphEd_CreateConnection transaction (FDragConnection::DroppedOnPin opens it before
	// Schema->TryCreateConnection — DragConnection.cpp:316). Do NOT ReconcileNow here: the drag
	// machinery holds live pin pointers through TryCreateConnection, and a wholesale rebuild under
	// them is the mid-gesture crash class the plan forbids.

	// Re-entrancy: every write entry point no-ops while a write is already in flight (KTD).
	if (bGraphWriteInProgress || !GraphObj)
	{
		return false;
	}
	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset)
	{
		return false;
	}

	// Resolve the live endpoint nodes before data preflight. Self-loops resolve both ends to the same
	// node.
	UPaper2DPlusAnimationMapNode_Move* FromNode = nullptr;
	UPaper2DPlusAnimationMapNode_Move* ToNode = nullptr;
	const FString FromLower = FromMoveName.ToLower();
	const FString ToLower = ToMoveName.ToLower();
	for (UEdGraphNode* Node : GraphObj->Nodes)
	{
		if (UPaper2DPlusAnimationMapNode_Move* MoveNode =
			Cast<UPaper2DPlusAnimationMapNode_Move>(Node))
		{
			const FString NodeLower = MoveNode->MoveName.ToLower();
			if (!FromNode && NodeLower == FromLower)
			{
				FromNode = MoveNode;
			}
			if (!ToNode && NodeLower == ToLower)
			{
				ToNode = MoveNode;
			}
		}
	}
	if (!FromNode || !ToNode)
	{
		return false;
	}
	// Pre-validate BEFORE opening the nested transaction (never open+Modify for a refusal): these are
	// exactly AppendTransitionRow's failure conditions, checked through the same first-match contract.
	if (ToMoveName.IsEmpty() || !AnimationMapPanel_FindEntry(Asset, FromMoveName))
	{
		return false;
	}

	// U2 write-time invariant (KTD): ONE row per (From, To) pair, true by construction mid-session —
	// a wire the pair already owns refuses with a toast (no transaction, no Modify, no data change).
	// Self-loops stay legal (one arc per pair); the refusal covers case-variant duplicates too.
	if (Paper2DPlusAnimationMap::FindTransitionRowIndex(
		Asset, FromMoveName, ToMoveName) != INDEX_NONE)
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT(
				"AnimationMapDuplicateTransitionRefused",
				"A transition {0} → {1} already exists — each move pair carries one arrow."),
			FText::FromString(FromMoveName),
			FText::FromString(ToMoveName)));
		Info.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return false;
	}

	UPaper2DPlusAnimationMapNode_Transition* EdgeNode = nullptr;
	{
		TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);

		// 1) DATA write-through: the panel transaction NESTS inside the engine's open
		//    GraphEd_CreateConnection -> ONE undo step, asset-only recorded (every graph node is
		//    non-transactional by the spawn-funnel invariant).
		BeginTransaction(LOCTEXT("AnimationMapCreateTransition", "Create Transition"));
		const bool bAppended = Paper2DPlusAnimationMap::AppendTransitionRow(Asset, FromMoveName, ToMoveName);
		EndTransaction(); // pend-flush suppressed by the guard; the broadcast below flushes instead
		if (!bAppended)
		{
			return false; // pre-validation makes this unreachable; an empty nested transaction is dropped
		}

		// 2) Edge-node spawn + link, still inside the ENGINE's transaction. The appended row is by
		//    contract at Transitions.Num()-1 (append-only, R13 — pinned by test); the funnel clears
		//    RF_Transactional BEFORE CreateConnections' link surgery (the U3 zombie invariant).
		const FFlipbookProfileEntry* FromEntry = AnimationMapPanel_FindEntry(Asset, FromMoveName);
		const int32 NewRowIndex = FromEntry->TransitionData.Transitions.Num() - 1;
		const FPaper2DPlusMoveTransition& NewRow = FromEntry->TransitionData.Transitions[NewRowIndex];
		const FString FromDisplayName = FromNode->MoveName;
		// A fresh row has no override, so its effective phase inherits the target animation.
		const FFlipbookProfileEntry* TargetEntry = AnimationMapPanel_FindEntry(Asset, ToNode->MoveName);
		const FGameplayTag TransitionPhaseTag = NewRow.GetEffectivePhaseTag(
			TargetEntry ? TargetEntry->EditorMeta.PhaseTag : FGameplayTag());
		EdgeNode = UPaper2DPlusAnimationMap::SpawnNodeUntransactional<UPaper2DPlusAnimationMapNode_Transition>(*GraphObj,
			[&FromDisplayName, NewRowIndex, &NewRow, &TransitionPhaseTag](UPaper2DPlusAnimationMapNode_Transition& Node)
			{
				Node.SetFromRow(FromDisplayName, NewRowIndex, NewRow, TransitionPhaseTag);
			});
		EdgeNode->CreateConnections(FromNode, ToNode);

		// Seed the pill between its endpoints (both on-screen — the drag started/ended on them). A pill
		// left at the default (0,0) is culled by SNodePanel whenever the view is scrolled away from the
		// graph origin, and a culled node never runs the second-pass layout that is its ONLY position
		// source — so the freshly-created bubble would silently never appear. Same seed + rationale as the
		// fresh-edge path in ReconcileNow; mirrors the engine's anim-SM (AnimationStateMachineSchema.cpp:253).
		EdgeNode->NodePosX = (FromNode->NodePosX + ToNode->NodePosX) / 2;
		EdgeNode->NodePosY = (FromNode->NodePosY + ToNode->NodePosY) / 2;

		// 3) THE lockstep invariant (LastProjection comment in the header): graph and data were mutated
		//    in LOCKSTEP, so refresh the diff baseline BEFORE releasing the guard — without this the
		//    gesture's own Modify echo performs redundant reconcile work. EXCEPT when an EXTERNAL edit pended
		//    mid-gesture (bReconcilePending — the handler pends while the guard/capture is up): the
		//    fresh projection would already CONTAIN that unabsorbed change, the armed post-gesture
		//    reconcile would diff EMPTY against it, and the external edit's graph effects would be
		//    silently swallowed (zombie nodes). Keep the stale baseline in that rare case — the
		//    reconcile then applies the affected granular changes in safe timer context. (No broadcast
		//    can land between this check and the guard release —
		//    single-threaded scope, no Slate pumping.)
		if (!bReconcilePending)
		{
			LastProjection = ProjectGraphForCurrentView(Asset);
			RestampComboDisplayFromBaseline();
		}
	}

	// 4) Select the new edge programmatically (selection is graph-local; the echo's empty diff keeps it
	//    alive). The selection drives the shared details pane to the edge's FromMove (TASK-96 P3) — the
	//    designer types the label there, in the Move Transitions row, now that the in-graph strip is gone.
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->ClearSelectionSet();
		GraphEditorWidget->SetNodeSelection(EdgeNode, true);
	}

	// 5) Broadcast choice (documented): EXPLICIT Model->NotifyAssetDataChanged() AFTER the guard
	//    released — the U1 contract's symmetric direction (graph edit -> list refresh, AE8), and it
	//    doubles as the pend-flush for any signal that pended while the guard was up (the handler
	//    pends without arming the timer; EndTransaction's flush was suppressed by the guard). Our own
	//    handler re-enters here unguarded -> RequestReconcile -> timer -> diff EMPTY against the
	//    just-updated LastProjection -> zero graph mutation. The Modify echo (the model's deferred
	//    OnAssetExternallyModified) additionally lands next tick and early-outs the same way. NET:
	//    list refreshes, the graph never rebuilds its just-spawned widget.
	if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}

	return true;
}

bool SAnimationMapPanel::HandleTransitionTargetRewireRequested(
	UPaper2DPlusAnimationMapNode_Transition* EdgeNode, const FString& NewTargetMoveName)
{
	// The visible pill grip starts a normal pin drag, but unlike Ctrl-drag it never disconnects the
	// old target first. Every refusal below therefore leaves both the authored row and original link
	// untouched; successful validation replaces them together in one transaction.
	if (bGraphWriteInProgress || !GraphObj || GraphObj->bRebuildInProgress || !EdgeNode
		|| EdgeNode->GetGraph() != GraphObj || NewTargetMoveName.IsEmpty())
	{
		return false;
	}
	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	const FFlipbookProfileEntry* NewTargetEntry = AnimationMapPanel_FindEntry(Asset, NewTargetMoveName);
	if (!Asset || !NewTargetEntry
		|| EdgeNode->TargetMove.Equals(NewTargetEntry->Identity.FlipbookName, ESearchCase::IgnoreCase))
	{
		return false;
	}

	TMap<FString, UPaper2DPlusAnimationMapNode_Move*> LiveMovesByLower;
	TArray<UPaper2DPlusAnimationMapNode_Transition*> LiveEdges;
	CollectLiveNodes(LiveMovesByLower, LiveEdges);
	UPaper2DPlusAnimationMapNode_Move* FromNode = LiveMovesByLower.FindRef(EdgeNode->FromMove.ToLower());
	UPaper2DPlusAnimationMapNode_Move* NewTargetNode =
		LiveMovesByLower.FindRef(NewTargetEntry->Identity.FlipbookName.ToLower());
	if (!FromNode || !NewTargetNode || NewTargetNode->bIsStub)
	{
		return false; // the replacement endpoint must belong to the currently open scoped graph
	}

	const int32 LiveRowIndex = Paper2DPlusAnimationMap::FindTransitionRowIndex(
		Asset, EdgeNode->FromMove, EdgeNode->TargetMove);
	FFlipbookProfileEntry* FromEntry = AnimationMapPanel_FindEntryMutable(Asset, EdgeNode->FromMove);
	if (!FromEntry || LiveRowIndex == INDEX_NONE || EdgeNode->RowIndex != LiveRowIndex
		|| !FromEntry->TransitionData.Transitions.IsValidIndex(LiveRowIndex)
		|| !Paper2DPlusAnimationMap::TransitionRowsEquivalent(
			FromEntry->TransitionData.Transitions[LiveRowIndex], EdgeNode->MakeRowSnapshot()))
	{
		RequestReconcile(); // stale handle: preserve data/link, refresh the projection safely later
		return false;
	}
	if (Paper2DPlusAnimationMap::FindTransitionRowIndex(
		Asset, EdgeNode->FromMove, NewTargetEntry->Identity.FlipbookName) != INDEX_NONE)
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("AnimationMapDuplicateRewireRefused",
				"Cannot retarget {0}: that move pair already has a transition."),
			FText::FromString(EdgeNode->FromMove)));
		Info.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return false;
	}

	{
		TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);
		TGuardValue<bool> RebuildGuard(GraphObj->bRebuildInProgress, true);
		BeginTransaction(LOCTEXT("AnimationMapRewireTransition", "Rewire Transition Target"));
		FromEntry->TransitionData.Transitions[LiveRowIndex].TargetMove =
			NewTargetEntry->Identity.FlipbookName;
		EndTransaction();

		const FPaper2DPlusMoveTransition& UpdatedRow = FromEntry->TransitionData.Transitions[LiveRowIndex];
		EdgeNode->SetFromRow(FromNode->MoveName, LiveRowIndex, UpdatedRow,
			UpdatedRow.GetEffectivePhaseTag(NewTargetEntry->EditorMeta.PhaseTag));
		if (UEdGraphPin* InputPin = EdgeNode->GetInputPin()) { InputPin->BreakAllPinLinks(); }
		if (UEdGraphPin* OutputPin = EdgeNode->GetOutputPin()) { OutputPin->BreakAllPinLinks(); }
		EdgeNode->CreateConnections(FromNode, NewTargetNode);
		EdgeNode->NodePosX = (FromNode->NodePosX + NewTargetNode->NodePosX) / 2;
		EdgeNode->NodePosY = (FromNode->NodePosY + NewTargetNode->NodePosY) / 2;

		if (!bReconcilePending)
		{
			LastProjection = ProjectGraphForCurrentView(Asset);
			RestampComboDisplayFromBaseline();
		}
	}

	if (GraphEditorWidget.IsValid())
	{
		if (SGraphPanel* GraphPanel = GraphEditorWidget->GetGraphPanel())
		{
			if (TSharedPtr<SGraphNode> Widget = GraphPanel->GetNodeWidgetFromGuid(EdgeNode->NodeGuid))
			{
				Widget->Invalidate(EInvalidateWidgetReason::Layout);
			}
			GraphPanel->Invalidate(EInvalidateWidgetReason::Paint);
		}
	}
	if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
	return true;
}

void SAnimationMapPanel::HandleEdgeRemovalRequested(UPaper2DPlusAnimationMapNode_Transition* /*EdgeNode*/)
{
	// CLEANUP-ONLY by KTD: the suicide hook NEVER mutates rows (re-entrant destroys double-delete
	// identical duplicate rows, R14). The rows are unchanged here, so the data diff alone would
	// early-out and strand the orphan; force the targeted topology-repair branch.
	bForceTopologyRepair = true;
	RequestReconcile();
}

void SAnimationMapPanel::HandleNodeMoveCommitted(UPaper2DPlusAnimationMapNode_Move* MoveNode)
{
	// NODE-MOVE persistence (R11). Reached from SAnimationMapMoveNode::MoveTo on bMarkDirty=true —
	// always inside an ENGINE-opened transaction, of which 5.7 has TWO producers: FinalizeNodeMovements'
	// NodeMoveTransaction (exactly one true call per node per drag, SNodePanel.cpp:1906-1961) and
	// SGraphPanel::UpdateSelectedNodesPositions' "Nudge Node" transaction (arrow-key nudge, one per
	// press, SGraphPanel.cpp:797-830). Either way: one undo step, one map write per gesture.
	//
	// Skip ONLY when the graph/asset is gone (or the name is degenerate). Deliberately NO stub check:
	// stubs are movable and their position persists under the dangling key by design (R3,
	// resurrection-friendly) — the map write for a stub key is legal.
	if (!MoveNode || MoveNode->MoveName.IsEmpty() || !GraphObj || GraphObj->bRebuildInProgress
		|| bGraphWriteInProgress)
	{
		return;
	}
	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset)
	{
		return;
	}

	{
		TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);

		const FVector2D NewPosition(
			MoveNode->NodePosX,
			MoveNode->NodePosY);
		// THE ONE sanctioned direct Asset->Modify() outside the panel's BeginTransaction (plan
		// KTD "node-move write-through", per-gesture transaction map): an ENGINE transaction
		// (drag finalize or arrow-nudge) is already open around this call.
		Asset->Modify();
		Asset->AnimationMapNodePositions.Add(
			MoveNode->MoveName.ToLower(),
			NewPosition);

		// Lockstep invariant: a first-ever placement key for an already-projecting node does not change
		// the projected node SET (AddNode dedupes by lowered name), but honor the invariant literally so
		// the Modify echo provably diffs empty. The drift dimension is clean too: the map now equals the
		// node's live NodePosX/Y (map wins over the session cache in ReapplyPositions). Same
		// mid-gesture-pend exception as wire-create: with an external edit pended, keep the stale
		// baseline so the armed reconcile cannot swallow it.
		if (!bReconcilePending)
		{
			LastProjection = ProjectGraphForCurrentView(Asset);
			RestampComboDisplayFromBaseline();
		}
	}

	// No NotifyAssetDataChanged (documented choice): positions are graph-local editor state no list
	// surface renders; the Modify echo (deferred OnAssetExternallyModified) covers every subscriber and
	// early-outs here. Pend-flush insurance: a signal landing inside the guard pends without arming
	// the timer.
	if (bReconcilePending)
	{
		RequestReconcile();
	}
}

// =====================================================================================================
// TASK-108 U6 (R16): "Change Group…" — move an animation's TagMappings membership from the Map
// =====================================================================================================

TArray<FString> SAnimationMapPanel::ResolveRealMoveTargets(const FString& RequestedMoveName) const
{
	TArray<FString> Result;
	const UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	const FFlipbookProfileEntry* RequestedEntry = AnimationMapPanel_FindEntry(Asset, RequestedMoveName);
	if (!RequestedEntry)
	{
		return Result;
	}

	const FString RequestedLower = RequestedEntry->Identity.FlipbookName.ToLower();
	TSet<FString> SelectedRealLowers;
	if (GraphEditorWidget.IsValid())
	{
		for (UObject* SelectedObject : GraphEditorWidget->GetSelectedNodes())
		{
			const UPaper2DPlusAnimationMapNode_Move* MoveNode =
				Cast<UPaper2DPlusAnimationMapNode_Move>(SelectedObject);
			if (MoveNode && !MoveNode->bIsStub && AnimationMapPanel_FindEntry(Asset, MoveNode->MoveName))
			{
				SelectedRealLowers.Add(MoveNode->MoveName.ToLower());
			}
		}
	}

	// Context-clicking outside the selection is intentionally single-target. Context-clicking one
	// member of a genuine selection applies to the selected real-move cohort in authored order.
	const TArray<FString> ProjectedNames = Paper2DPlusAnimationMap::ResolveMultiEditTargetNames(
		RequestedLower, SelectedRealLowers, LastProjection);
	for (const FString& ProjectedName : ProjectedNames)
	{
		if (const FFlipbookProfileEntry* Entry = AnimationMapPanel_FindEntry(Asset, ProjectedName))
		{
			Result.Add(Entry->Identity.FlipbookName);
		}
	}
	// A live context-click can only name a projected node, but preserve the historic single-target
	// behavior if a stale baseline is briefly visible between a granular graph action and reconcile.
	if (Result.Num() == 0 && !SelectedRealLowers.Contains(RequestedLower))
	{
		Result.Add(RequestedEntry->Identity.FlipbookName);
	}
	return Result;
}

TArray<FString> SAnimationMapPanel::GetSelectedRealMoveNamesInProjectionOrder() const
{
	TArray<FString> Result;
	const UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset || !GraphEditorWidget.IsValid())
	{
		return Result;
	}
	TSet<FString> SelectedLower;
	for (UObject* SelectedObject : GraphEditorWidget->GetSelectedNodes())
	{
		const UPaper2DPlusAnimationMapNode_Move* MoveNode =
			Cast<UPaper2DPlusAnimationMapNode_Move>(SelectedObject);
		if (MoveNode && !MoveNode->bIsStub && AnimationMapPanel_FindEntry(Asset, MoveNode->MoveName))
		{
			SelectedLower.Add(MoveNode->MoveName.ToLower());
		}
	}
	for (const Paper2DPlusAnimationMap::FProjectedNode& Projected : LastProjection.Nodes)
	{
		if (!Projected.bIsStub && SelectedLower.Contains(Projected.MoveNameLower))
		{
			if (const FFlipbookProfileEntry* Entry = AnimationMapPanel_FindEntry(Asset, Projected.DisplayName))
			{
				Result.Add(Entry->Identity.FlipbookName);
			}
		}
	}
	return Result;
}

bool SAnimationMapPanel::RefreshSelectionCohortSummary()
{
	const int32 PreviousCount = SelectionCohortCount;
	const FText PreviousGroup = SelectionCohortGroupText;
	const FText PreviousPhase = SelectionCohortPhaseText;
	const FText PreviousTags = SelectionCohortAnimationTagsText;
	const FText PreviousStart = SelectionCohortChainStartText;
	const FText PreviousEnd = SelectionCohortChainEndText;
	auto DidChange = [this, PreviousCount, PreviousGroup, PreviousPhase, PreviousTags, PreviousStart, PreviousEnd]()
	{
		return PreviousCount != SelectionCohortCount
			|| !PreviousGroup.EqualTo(SelectionCohortGroupText)
			|| !PreviousPhase.EqualTo(SelectionCohortPhaseText)
			|| !PreviousTags.EqualTo(SelectionCohortAnimationTagsText)
			|| !PreviousStart.EqualTo(SelectionCohortChainStartText)
			|| !PreviousEnd.EqualTo(SelectionCohortChainEndText);
	};

	const UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	const TArray<FString> Names = GetSelectedRealMoveNamesInProjectionOrder();
	SelectionCohortCount = Names.Num();
	SelectionCohortGroupText = LOCTEXT("SelectionCohortGroupEmpty", "Group: None");
	SelectionCohortPhaseText = LOCTEXT("SelectionCohortPhaseEmpty", "Phase: None");
	SelectionCohortAnimationTagsText = LOCTEXT("SelectionCohortTagsEmpty", "Animation Tags: None");
	SelectionCohortChainStartText = LOCTEXT("SelectionCohortChainStartEmpty", "Chain Start: No");
	SelectionCohortChainEndText = LOCTEXT("SelectionCohortChainEndEmpty", "Chain End: No");
	if (!Asset || Names.Num() == 0)
	{
		return DidChange();
	}

	FGameplayTag CommonGroup = Paper2DPlusAnimationMap::FindHomeAnimationGroupTag(Asset, Names[0]);
	FGameplayTag CommonPhase;
	FGameplayTagContainer CommonTags;
	if (const FFlipbookProfileEntry* First = AnimationMapPanel_FindEntry(Asset, Names[0]))
	{
		CommonPhase = First->EditorMeta.PhaseTag;
		CommonTags = First->EditorMeta.AnimationTags;
	}
	bool bMixedGroup = false;
	bool bMixedPhase = false;
	bool bMixedTags = false;
	int32 StartCount = 0;
	int32 EndCount = 0;
	for (const FString& Name : Names)
	{
		bMixedGroup |= Paper2DPlusAnimationMap::FindHomeAnimationGroupTag(Asset, Name) != CommonGroup;
		if (const FFlipbookProfileEntry* Entry = AnimationMapPanel_FindEntry(Asset, Name))
		{
			bMixedPhase |= Entry->EditorMeta.PhaseTag != CommonPhase;
			bMixedTags |= !(Entry->EditorMeta.AnimationTags == CommonTags);
		}
		const FString Lower = Name.ToLower();
		if (const Paper2DPlusAnimationMap::FProjectedNode* Projected = LastProjection.Nodes.FindByPredicate(
			[&Lower](const Paper2DPlusAnimationMap::FProjectedNode& Node) { return Node.MoveNameLower == Lower; }))
		{
			StartCount += Projected->bIsChainStart ? 1 : 0;
			EndCount += Projected->bIsChainEnd ? 1 : 0;
		}
	}

	SelectionCohortGroupText = bMixedGroup
		? LOCTEXT("SelectionCohortGroupMixed", "Group: Mixed")
		: FText::Format(LOCTEXT("SelectionCohortGroupFmt", "Group: {0}"),
			CommonGroup.IsValid() ? FText::FromString(AnimationMapPanel_ShortAnimationTagName(CommonGroup))
				: LOCTEXT("SelectionCohortUnassigned", "Unassigned"));
	SelectionCohortPhaseText = bMixedPhase
		? LOCTEXT("SelectionCohortPhaseMixed", "Phase: Mixed")
		: FText::Format(LOCTEXT("SelectionCohortPhaseFmt", "Phase: {0}"),
			CommonPhase.IsValid() ? FText::FromString(AnimationMapPanel_ShortAnimationTagName(CommonPhase))
				: LOCTEXT("SelectionCohortPhaseNone", "None"));
	SelectionCohortAnimationTagsText = bMixedTags
		? LOCTEXT("SelectionCohortTagsMixed", "Animation Tags: Mixed")
		: FText::Format(LOCTEXT("SelectionCohortTagsFmt", "Animation Tags: {0}"),
			CommonTags.IsEmpty() ? LOCTEXT("SelectionCohortTagsNone", "None")
				: FText::FromString(CommonTags.ToStringSimple()));
	auto FlagText = [&Names](bool bStart, int32 TrueCount)
	{
		const FText Label = bStart ? LOCTEXT("SelectionCohortChainStart", "Chain Start")
			: LOCTEXT("SelectionCohortChainEnd", "Chain End");
		const FText Value = TrueCount == 0 ? LOCTEXT("SelectionCohortFlagNo", "No")
			: (TrueCount == Names.Num() ? LOCTEXT("SelectionCohortFlagYes", "Yes")
				: LOCTEXT("SelectionCohortFlagMixed", "Mixed"));
		return FText::Format(LOCTEXT("SelectionCohortFlagFmt", "{0}: {1}"), Label, Value);
	};
	SelectionCohortChainStartText = FlagText(true, StartCount);
	SelectionCohortChainEndText = FlagText(false, EndCount);
	return DidChange();
}

FText SAnimationMapPanel::GetSelectionCohortCountText() const
{
	return FText::Format(LOCTEXT("SelectionCohortCount", "{0} animations selected"),
		FText::AsNumber(SelectionCohortCount));
}

FText SAnimationMapPanel::GetSelectionCohortGroupText() const { return SelectionCohortGroupText; }
FText SAnimationMapPanel::GetSelectionCohortPhaseText() const { return SelectionCohortPhaseText; }
FText SAnimationMapPanel::GetSelectionCohortAnimationTagsText() const { return SelectionCohortAnimationTagsText; }
FText SAnimationMapPanel::GetSelectionCohortChainFlagText(bool bChainStart) const
{
	return bChainStart ? SelectionCohortChainStartText : SelectionCohortChainEndText;
}

TSharedRef<SWidget> SAnimationMapPanel::BuildSelectionCohortCard()
{
	return SNew(SBox)
		.MaxDesiredWidth(420.0f)
	[
		SAssignNew(SelectionCohortCard, SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor(0.035f, 0.04f, 0.05f, 0.96f))
		.Padding(8.0f)
		.Visibility_Lambda([this]()
		{
			return SelectionCohortCount > 1
				? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(this, &SAnimationMapPanel::GetSelectionCohortCountText)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(this, &SAnimationMapPanel::GetSelectionCohortGroupText)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(this, &SAnimationMapPanel::GetSelectionCohortPhaseText)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(this, &SAnimationMapPanel::GetSelectionCohortAnimationTagsText)
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text_Lambda([this]() { return GetSelectionCohortChainFlagText(true); })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text_Lambda([this]() { return GetSelectionCohortChainFlagText(false); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SComboButton)
					.ButtonContent()[ SNew(STextBlock).Text(LOCTEXT("CohortChangeGroup", "Change Group")) ]
					.OnGetMenuContent(this, &SAnimationMapPanel::BuildCohortGroupMenu)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SComboButton)
					.ButtonContent()[ SNew(STextBlock).Text(LOCTEXT("CohortSetPhase", "Set Phase")) ]
					.OnGetMenuContent(this, &SAnimationMapPanel::BuildCohortPhaseMenu)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SComboButton)
					.ButtonContent()[ SNew(STextBlock).Text(LOCTEXT("CohortSetTags", "Set Animation Tags")) ]
					.OnGetMenuContent(this, &SAnimationMapPanel::BuildCohortAnimationTagsMenu)
				]
			]
		]
	];
}

TSharedRef<SWidget> SAnimationMapPanel::BuildCohortGroupMenu()
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	const TArray<FString> Names = GetSelectedRealMoveNamesInProjectionOrder();
	if (Names.Num() < 2) { return SNullWidget::NullWidget; }
	const FString Anchor = Names[0];
	TWeakPtr<SAnimationMapPanel> WeakPanel = SharedThis(this);
	return SNew(SBox).MinDesiredWidth(300.f).Padding(2.f)
	[
		SNew(SMenuHostedTagPickerGuard)
		[
			SNew(SGameplayTagPicker)
			.Filter(TEXT("Paper2DPlus.Animation"))
			.MultiSelect(false)
			.GameplayTagPickerMode(EGameplayTagPickerMode::HybridMode)
			.TagContainers(TArray<FGameplayTagContainer>{ FGameplayTagContainer() })
			.OnTagChanged_Lambda([WeakPanel, Anchor](const TArray<FGameplayTagContainer>& Containers)
			{
				FGameplayTag Picked;
				for (const FGameplayTagContainer& Container : Containers)
				{
					for (auto It = Container.CreateConstIterator(); It; ++It) { Picked = *It; break; }
					if (Picked.IsValid()) { break; }
				}
				if (!Picked.IsValid()) { return; }
				FSlateApplication::Get().DismissAllMenus();
				auto Fire = [WeakPanel, Anchor, Picked]()
				{
					if (TSharedPtr<SAnimationMapPanel> Panel = WeakPanel.Pin())
					{
						Panel->HandleChangeGroupRequested(Anchor, Picked);
					}
				};
				if (GEditor) { GEditor->GetTimerManager()->SetTimerForNextTick(Fire); } else { Fire(); }
			})
		]
	];
#else
	return SNew(STextBlock).Text(LOCTEXT("CohortTagsUnavailable", "Tag editing requires Unreal 5.3+."));
#endif
}

TSharedRef<SWidget> SAnimationMapPanel::BuildCohortPhaseMenu()
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	const TArray<FString> Names = GetSelectedRealMoveNamesInProjectionOrder();
	if (Names.Num() < 2) { return SNullWidget::NullWidget; }
	const FString Anchor = Names[0];
	FGameplayTagContainer Initial;
	const UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (const FFlipbookProfileEntry* Entry = AnimationMapPanel_FindEntry(Asset, Anchor))
	{
		bool bCommon = true;
		for (const FString& Name : Names)
		{
			const FFlipbookProfileEntry* Other = AnimationMapPanel_FindEntry(Asset, Name);
			bCommon &= Other && Other->EditorMeta.PhaseTag == Entry->EditorMeta.PhaseTag;
		}
		if (bCommon && Entry->EditorMeta.PhaseTag.IsValid()) { Initial.AddTag(Entry->EditorMeta.PhaseTag); }
	}
	const TSharedRef<FGameplayTagContainer> Pending = MakeShared<FGameplayTagContainer>(Initial);
	TWeakPtr<SAnimationMapPanel> WeakPanel = SharedThis(this);
	return SNew(SVerticalBox)
	+ SVerticalBox::Slot().AutoHeight()
	[
		SNew(SBox).MinDesiredWidth(300.f).Padding(2.f)
		[
			SNew(SMenuHostedTagPickerGuard)
			[
				SNew(SGameplayTagPicker).Filter(TEXT("Paper2DPlus.Phase")).MultiSelect(false)
				.GameplayTagPickerMode(EGameplayTagPickerMode::SelectionMode)
				.TagContainers(TArray<FGameplayTagContainer>{ *Pending })
				.OnTagChanged_Lambda([Pending](const TArray<FGameplayTagContainer>& Containers)
				{ *Pending = Containers.Num() > 0 ? Containers[0] : FGameplayTagContainer(); })
			]
		]
	]
	+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(2.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SButton).Text(LOCTEXT("CohortClearPhase", "Clear"))
			.OnClicked_Lambda([WeakPanel, Anchor]()
			{
				FSlateApplication::Get().DismissAllMenus();
				auto Fire = [WeakPanel, Anchor]()
				{
					if (TSharedPtr<SAnimationMapPanel> Panel = WeakPanel.Pin())
					{ Panel->HandleSetPhaseRequested(Anchor, FGameplayTag()); }
				};
				if (GEditor) { GEditor->GetTimerManager()->SetTimerForNextTick(Fire); } else { Fire(); }
				return FReply::Handled();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).Text(LOCTEXT("CohortApplyPhase", "Apply"))
			.IsEnabled_Lambda([Pending]() { return !Pending->IsEmpty(); })
			.OnClicked_Lambda([WeakPanel, Anchor, Pending]()
			{
				FGameplayTag Picked;
				for (auto It = Pending->CreateConstIterator(); It; ++It) { Picked = *It; break; }
				FSlateApplication::Get().DismissAllMenus();
				auto Fire = [WeakPanel, Anchor, Picked]()
				{
					if (TSharedPtr<SAnimationMapPanel> Panel = WeakPanel.Pin())
					{ Panel->HandleSetPhaseRequested(Anchor, Picked); }
				};
				if (GEditor) { GEditor->GetTimerManager()->SetTimerForNextTick(Fire); } else { Fire(); }
				return FReply::Handled();
			})
		]
	];
#else
	return SNew(STextBlock).Text(LOCTEXT("CohortPhaseUnavailable", "Phase editing requires Unreal 5.3+."));
#endif
}

TSharedRef<SWidget> SAnimationMapPanel::BuildCohortAnimationTagsMenu()
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	const TArray<FString> Names = GetSelectedRealMoveNamesInProjectionOrder();
	if (Names.Num() < 2) { return SNullWidget::NullWidget; }
	const FString Anchor = Names[0];
	FGameplayTagContainer Initial;
	const UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (const FFlipbookProfileEntry* Entry = AnimationMapPanel_FindEntry(Asset, Anchor))
	{
		bool bCommon = true;
		for (const FString& Name : Names)
		{
			const FFlipbookProfileEntry* Other = AnimationMapPanel_FindEntry(Asset, Name);
			bCommon &= Other && Other->EditorMeta.AnimationTags == Entry->EditorMeta.AnimationTags;
		}
		if (bCommon) { Initial = Entry->EditorMeta.AnimationTags; }
	}
	const TSharedRef<FGameplayTagContainer> Pending = MakeShared<FGameplayTagContainer>(Initial);
	TWeakPtr<SAnimationMapPanel> WeakPanel = SharedThis(this);
	return SNew(SVerticalBox)
	+ SVerticalBox::Slot().AutoHeight()
	[
		SNew(SBox).MinDesiredWidth(320.f).Padding(2.f)
		[
			SNew(SMenuHostedTagPickerGuard)
			[
				SNew(SGameplayTagPicker).Filter(TEXT("Paper2DPlus.Animation")).MultiSelect(true)
				.GameplayTagPickerMode(EGameplayTagPickerMode::SelectionMode)
				.TagContainers(TArray<FGameplayTagContainer>{ *Pending })
				.OnTagChanged_Lambda([Pending](const TArray<FGameplayTagContainer>& Containers)
				{
					Pending->Reset();
					for (const FGameplayTagContainer& Container : Containers) { Pending->AppendTags(Container); }
				})
			]
		]
	]
	+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(2.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SButton).Text(LOCTEXT("CohortClearTags", "Clear"))
			.OnClicked_Lambda([WeakPanel, Anchor]()
			{
				FSlateApplication::Get().DismissAllMenus();
				auto Fire = [WeakPanel, Anchor]()
				{
					if (TSharedPtr<SAnimationMapPanel> Panel = WeakPanel.Pin())
					{ Panel->HandleSetAnimationTagsRequested(Anchor, FGameplayTagContainer()); }
				};
				if (GEditor) { GEditor->GetTimerManager()->SetTimerForNextTick(Fire); } else { Fire(); }
				return FReply::Handled();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).Text(LOCTEXT("CohortApplyTags", "Apply"))
			.IsEnabled_Lambda([Pending]() { return !Pending->IsEmpty(); })
			.OnClicked_Lambda([WeakPanel, Anchor, Pending]()
			{
				const FGameplayTagContainer Tags = *Pending;
				FSlateApplication::Get().DismissAllMenus();
				auto Fire = [WeakPanel, Anchor, Tags]()
				{
					if (TSharedPtr<SAnimationMapPanel> Panel = WeakPanel.Pin())
					{ Panel->HandleSetAnimationTagsRequested(Anchor, Tags); }
				};
				if (GEditor) { GEditor->GetTimerManager()->SetTimerForNextTick(Fire); } else { Fire(); }
				return FReply::Handled();
			})
		]
	];
#else
	return SNew(STextBlock).Text(LOCTEXT("CohortAnimationTagsUnavailable", "Tag editing requires Unreal 5.3+."));
#endif
}

void SAnimationMapPanel::HandleChangeGroupRequested(const FString& MoveName, const FGameplayTag& InTargetTag)
{
	if (!InTargetTag.IsValid() || MoveName.IsEmpty())
	{
		return;
	}
	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset)
	{
		return;
	}

	TArray<FString> Targets = ResolveRealMoveTargets(MoveName);
	Targets.RemoveAll([Asset, InTargetTag](const FString& TargetName)
	{
		return Paper2DPlusAnimationMap::FindHomeAnimationGroupTag(Asset, TargetName) == InTargetTag;
	});
	if (Targets.Num() == 0)
	{
		return; // all no-ops: no transaction, no dirty
	}

	// ONE transaction: AssignFlipbookToTagMapping FindOrAdd-creates the mapping key when the tag is
	// new, APPENDS the entry at the END of the target's list (first-match runtime resolution of the
	// existing entries undisturbed), and strips the move from every other mapping via
	// RemoveDuplicateFlipbookTagMappings (RemoveAt — the source group's remaining order preserved).
	// Same funnel the browser card's Tag chip commits through (CommitCardAnimationTag).
	BeginTransaction(Targets.Num() > 1
		? LOCTEXT("ChangeAnimationGroupsTransaction", "Change Animation Groups")
		: LOCTEXT("ChangeAnimationGroupTransaction", "Change Animation Group"));
	TArray<FString> ResetPositionKeys;
	ResetPositionKeys.Reserve(Targets.Num());
	for (const FString& TargetName : Targets)
	{
		const FFlipbookProfileEntry* Entry = AnimationMapPanel_FindEntry(Asset, TargetName);
		Asset->AssignFlipbookToTagMapping(InTargetTag, TargetName,
			Entry ? Entry->Identity.PaperZDSequence.Get() : nullptr);
		const FString TargetLower = TargetName.ToLower();
		Asset->AnimationMapNodePositions.Remove(TargetLower);
		ResetPositionKeys.Add(TargetLower);
	}
	EndTransaction();
	for (const FString& TargetLower : ResetPositionKeys)
	{
		SessionPositionCache.Remove(TargetLower);
		LastAppliedPositions.Remove(TargetLower);
	}
	if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged(); // board tiles / graph / group-implied tags via normal reconcile
	}
}

void SAnimationMapPanel::HandleSetPhaseRequested(const FString& MoveName, const FGameplayTag& InPhaseTag)
{
	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset)
	{
		return;
	}
	TArray<FString> Targets = ResolveRealMoveTargets(MoveName);
	Targets.RemoveAll([Asset, InPhaseTag](const FString& TargetName)
	{
		const FFlipbookProfileEntry* Entry = AnimationMapPanel_FindEntry(Asset, TargetName);
		return !Entry || Entry->EditorMeta.PhaseTag == InPhaseTag;
	});
	if (Targets.Num() == 0)
	{
		return;
	}

	BeginTransaction(Targets.Num() > 1
		? LOCTEXT("SetAnimationPhasesTransaction", "Set Animation Phases")
		: LOCTEXT("SetAnimationPhaseTransaction", "Set Animation Phase"));
	for (const FString& TargetName : Targets)
	{
		if (FFlipbookProfileEntry* Entry = AnimationMapPanel_FindEntryMutable(Asset, TargetName))
		{
			Entry->EditorMeta.PhaseTag = InPhaseTag;
		}
	}
	EndTransaction();
	if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
}

void SAnimationMapPanel::HandleSetAnimationTagsRequested(
	const FString& MoveName, const FGameplayTagContainer& InTags)
{
	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset)
	{
		return;
	}
	TArray<FString> Targets = ResolveRealMoveTargets(MoveName);
	Targets.RemoveAll([Asset, &InTags](const FString& TargetName)
	{
		const FFlipbookProfileEntry* Entry = AnimationMapPanel_FindEntry(Asset, TargetName);
		return !Entry || Entry->EditorMeta.AnimationTags == InTags;
	});
	if (Targets.Num() == 0)
	{
		return;
	}

	BeginTransaction(Targets.Num() > 1
		? LOCTEXT("SetAnimationTagContainersTransaction", "Set Animation Tag Containers")
		: LOCTEXT("SetAnimationTagContainerTransaction", "Set Animation Tag Container"));
	for (const FString& TargetName : Targets)
	{
		if (FFlipbookProfileEntry* Entry = AnimationMapPanel_FindEntryMutable(Asset, TargetName))
		{
			Entry->EditorMeta.AnimationTags = InTags;
		}
	}
	EndTransaction();
	if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
}

// =====================================================================================================
// CHAIN START MARKERS (chain-start rework) — the wired marker node IS the authoring surface for the
// exact-group mapping entry's bIsChainStart flag. Flat data stays the single source of truth: the
// gestures below write the FLAG through the panel transaction template and mutate the graph in
// LOCKSTEP (the HandleWireCreateRequested discipline); the reconcile-tail sync owns everything else.
// =====================================================================================================

void SAnimationMapPanel::CollectLiveChainStartMarkers(TArray<UPaper2DPlusAnimationMapNode_ChainStart*>& OutMarkers) const
{
	OutMarkers.Reset();
	if (!GraphObj)
	{
		return;
	}
	for (UEdGraphNode* Node : GraphObj->Nodes)
	{
		if (UPaper2DPlusAnimationMapNode_ChainStart* Marker = Cast<UPaper2DPlusAnimationMapNode_ChainStart>(Node))
		{
			OutMarkers.Add(Marker);
		}
	}
}

void SAnimationMapPanel::SynchronizeChainStartMarkers(UPaper2DPlusCharacterProfileAsset* Asset,
	const Paper2DPlusAnimationMap::FGraphProjection& Current)
{
	if (!GraphObj)
	{
		return;
	}

	// The flagged set comes from the PROJECTION (never node stamps — sync must not depend on stamp
	// ordering, and the projection already fail-closes invalid groups via the shared runtime resolver).
	TSet<FString> FlaggedLower;
	for (const Paper2DPlusAnimationMap::FProjectedNode& Node : Current.Nodes)
	{
		if (Node.bIsChainStart)
		{
			FlaggedLower.Add(Node.MoveNameLower);
		}
	}

	TMap<FString, UPaper2DPlusAnimationMapNode_Move*> LiveMovesByLower;
	TArray<UPaper2DPlusAnimationMapNode_Transition*> LiveEdges;
	CollectLiveNodes(LiveMovesByLower, LiveEdges);

	TArray<UPaper2DPlusAnimationMapNode_ChainStart*> Markers;
	CollectLiveChainStartMarkers(Markers);

	// Spawn gate: markers materialize only on the EXACT-GROUP surface. The board's hidden reconcile
	// projects UNFILTERED (every group's openers flag), and the unassigned surface projects no flags
	// at all — neither is a chain-start authoring surface, so neither mints markers. Removal below
	// still runs everywhere (a stale targeted marker must not linger on any surface).
	const bool bSpawnScope = bAnimationMapGroupGraphOpen && !bAnimationMapGroupGraphIsUnassigned
		&& ActiveAnimationMapGroupTag.IsValid();

	// Same discipline as MaterializeCommentsForCurrentGroup: direct graph surgery must not re-enter
	// the write-through hooks (the marker MoveTo/unlink seams early-out under this).
	TGuardValue<bool> RebuildGuard(GraphObj->bRebuildInProgress, true);
	bool bChanged = false;

	// (b) TARGETED markers: remove when the target move is gone or no longer flagged; dedupe to ONE
	// marker per move (first survives). (c) FLOATING markers are NEVER removed — session-transient
	// authoring intent; only the user deletes them.
	TSet<FString> WiredTargets;
	for (UPaper2DPlusAnimationMapNode_ChainStart* Marker : Markers)
	{
		if (Marker->TargetMoveLower.IsEmpty())
		{
			continue; // floating — (c)
		}
		UPaper2DPlusAnimationMapNode_Move* Target = LiveMovesByLower.FindRef(Marker->TargetMoveLower);
		const bool bDuplicate = WiredTargets.Contains(Marker->TargetMoveLower);
		if (!Target || !FlaggedLower.Contains(Marker->TargetMoveLower) || bDuplicate)
		{
			// Position stays in ChainStartMarkerPositions (kept-stale, resurrect-friendly).
			GraphObj->RemoveNode(Marker);
			bChanged = true;
			continue;
		}
		WiredTargets.Add(Marker->TargetMoveLower);

		// (a) exactly one OUTGOING wire onto the move's input/whole-body pin. The marker was spawned
		// through the untransactional funnel, so RF_Transactional is already cleared — the
		// ClearFlags-before-MakeLinkTo invariant holds for this surgery.
		UEdGraphPin* OutputPin = Marker->GetOutputPin();
		UEdGraphPin* InputPin = Target->GetInputPin();
		if (OutputPin && InputPin && (!OutputPin->LinkedTo.Contains(InputPin) || OutputPin->LinkedTo.Num() != 1))
		{
			OutputPin->BreakAllPinLinks();
			OutputPin->MakeLinkTo(InputPin);
			bChanged = true;
		}
	}

	// (a) every flagged move without a marker gets one, wired, at its stored spot (the synthetic
	// "__chainstart__:<move>" key) else ~180 left / 40 up of the move node (whose position was already
	// reapplied this reconcile).
	if (bSpawnScope)
	{
		for (const FString& FlaggedName : FlaggedLower)
		{
			if (WiredTargets.Contains(FlaggedName))
			{
				continue;
			}
			UPaper2DPlusAnimationMapNode_Move* Target = LiveMovesByLower.FindRef(FlaggedName);
			if (!Target)
			{
				continue; // projection guarantees the node; defensive against a half-failed spawn
			}
			const FVector2D* StoredPos =
				ChainStartMarkerPositions.Find(AnimationMapPanel_ChainStartPositionKey(FlaggedName));
			const FVector2D MarkerPos = StoredPos
				? *StoredPos
				: FVector2D(Target->NodePosX - 180.0, Target->NodePosY - 40.0);

			UPaper2DPlusAnimationMapNode_ChainStart* Marker =
				UPaper2DPlusAnimationMap::SpawnNodeUntransactional<UPaper2DPlusAnimationMapNode_ChainStart>(*GraphObj,
					[&FlaggedName](UPaper2DPlusAnimationMapNode_ChainStart& Node)
					{
						Node.TargetMoveLower = FlaggedName;
					});
			Marker->NodePosX = FMath::RoundToInt32(MarkerPos.X);
			Marker->NodePosY = FMath::RoundToInt32(MarkerPos.Y);
			// Link surgery AFTER the funnel cleared RF_Transactional (the U3 ordering invariant).
			UEdGraphPin* OutputPin = Marker->GetOutputPin();
			UEdGraphPin* InputPin = Target->GetInputPin();
			if (OutputPin && InputPin)
			{
				OutputPin->MakeLinkTo(InputPin);
			}
			bChanged = true;
		}
	}

	if (bChanged)
	{
		// AddNode/RemoveNode already emitted granular graph actions. Link-only repairs merely need the
		// connection layer repainted; a default graph notification would purge every move card.
		if (GraphEditorWidget.IsValid())
		{
			if (SGraphPanel* GraphPanel = GraphEditorWidget->GetGraphPanel())
			{
				GraphPanel->Invalidate(EInvalidateWidgetReason::Paint);
			}
		}
	}
}

void SAnimationMapPanel::RestampComboDisplayFromBaseline()
{
	// See the header contract: the write funnels adopt the fresh projection as the diff baseline, so
	// the deferred echo reconcile early-outs — the DERIVED combo dimensions (main-line renumbering on
	// nodes whose own rows never changed) must therefore be stamped HERE, in the funnel's lockstep.
	TMap<FString, UPaper2DPlusAnimationMapNode_Move*> LiveMovesByLower;
	TArray<UPaper2DPlusAnimationMapNode_Transition*> LiveEdges;
	CollectLiveNodes(LiveMovesByLower, LiveEdges);

	TMap<FString, const Paper2DPlusAnimationMap::FProjectedNode*> ByLower;
	ByLower.Reserve(LastProjection.Nodes.Num());
	for (const Paper2DPlusAnimationMap::FProjectedNode& Node : LastProjection.Nodes)
	{
		ByLower.Add(Node.MoveNameLower, &Node);
	}
	for (const TPair<FString, UPaper2DPlusAnimationMapNode_Move*>& Pair : LiveMovesByLower)
	{
		if (const Paper2DPlusAnimationMap::FProjectedNode* const* Projected = ByLower.Find(Pair.Key))
		{
			Pair.Value->bIsChainStart = (*Projected)->bIsChainStart;
			Pair.Value->bIsChainEnd = (*Projected)->bIsChainEnd;
			Pair.Value->ComboSpineIndex = (*Projected)->ComboSpineIndex;
			Pair.Value->ComboSpineLength = (*Projected)->ComboSpineLength;
		}
		else
		{
			Pair.Value->bIsChainStart = false;
			Pair.Value->bIsChainEnd = false;
			Pair.Value->ComboSpineIndex = INDEX_NONE;
			Pair.Value->ComboSpineLength = 0;
		}
	}

	if (GraphEditorWidget.IsValid())
	{
		if (SGraphPanel* GraphPanel = GraphEditorWidget->GetGraphPanel())
		{
			GraphPanel->Invalidate(EInvalidateWidgetReason::Paint);
		}
	}
}

bool SAnimationMapPanel::HandleChainStartLinkRequested(UPaper2DPlusAnimationMapNode_ChainStart* MarkerNode,
	const FString& TargetMoveName)
{
	// THE AIM GESTURE (the wire funnel's chain-start sibling). Called by the schema INSIDE the
	// engine's open GraphEd_CreateConnection transaction — do NOT ReconcileNow here (live pin
	// pointers, the mid-gesture crash class); mutate data + graph in LOCKSTEP and refresh the baseline.
	if (bGraphWriteInProgress || !GraphObj || !MarkerNode || TargetMoveName.IsEmpty())
	{
		return false;
	}
	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset)
	{
		return false;
	}

	// Chain starts are EXACT-GROUP identity — no group scope (unassigned/board), no gesture.
	if (!bAnimationMapGroupGraphOpen || bAnimationMapGroupGraphIsUnassigned || !ActiveAnimationMapGroupTag.IsValid())
	{
		AnimationMapPanel_ChainStartRefusalToast(LOCTEXT("ChainStartNoGroupScope",
			"Chain starts belong to a tag group — open a group graph to set one."));
		return false;
	}

	// Pre-validate BEFORE opening the nested transaction (never open+Modify for a refusal).
	const int32 EntryIndex = AnimationMapPanel_FindGroupEntryIndex(Asset, ActiveAnimationMapGroupTag, TargetMoveName);
	if (EntryIndex == INDEX_NONE)
	{
		AnimationMapPanel_ChainStartRefusalToast(FText::Format(LOCTEXT("ChainStartTargetNotInGroup",
			"'{0}' is not one unambiguous member of this group — repair duplicate or missing membership first."),
			FText::FromString(TargetMoveName)));
		return false;
	}
	const FFlipbookTagMapping* Mapping = Asset->TagMappings.Find(ActiveAnimationMapGroupTag);
	if (Mapping && Mapping->Entries[EntryIndex].bIsChainStart)
	{
		// Already flagged (by this marker or another) — a no-op gesture opens NO transaction.
		AnimationMapPanel_ChainStartRefusalToast(FText::Format(LOCTEXT("ChainStartAlreadyFlagged",
			"'{0}' is already a chain start."), FText::FromString(TargetMoveName)));
		return false;
	}

	const FString TargetLower = TargetMoveName.ToLower();
	const FString PreviousLower = MarkerNode->TargetMoveLower;
	const int32 PreviousIndex = PreviousLower.IsEmpty()
		? INDEX_NONE
		: AnimationMapPanel_FindGroupEntryIndex(
			Asset,
			ActiveAnimationMapGroupTag,
			PreviousLower);
	if (!PreviousLower.IsEmpty() && PreviousIndex == INDEX_NONE)
	{
		AnimationMapPanel_ChainStartRefusalToast(FText::Format(
			LOCTEXT(
				"ChainStartPreviousTargetAmbiguous",
				"Cannot re-aim this Chain Start: its previous target '{0}' is missing or duplicated in the group."),
			FText::FromString(PreviousLower)));
		return false;
	}
	{
		TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);

		// 1) DATA write-through: clear-old + set-new in ONE panel transaction, NESTED inside the
		//    engine's connection transaction (one undo step; asset-only recorded — every graph node is
		//    non-transactional by the spawn-funnel invariant). Re-aiming = the single-transition rule:
		//    a marker carries at most ONE designation, so its previous target unflags first.
		BeginTransaction(LOCTEXT("AnimationMapSetChainStart", "Set Chain Start"));
		if (PreviousIndex != INDEX_NONE)
		{
			Asset->SetTagMappingEntryChainStart(ActiveAnimationMapGroupTag, PreviousIndex, false);
		}
		Asset->SetTagMappingEntryChainStart(ActiveAnimationMapGroupTag, EntryIndex, true);
		EndTransaction(); // pend-flush suppressed by the guard; the broadcast below flushes instead

		// 2) Graph LOCKSTEP, still inside the ENGINE's transaction: break the marker's old wire (at
		//    most one exists), wire the new target, retarget the marker, and mirror the flags onto the
		//    move nodes' display state (the ▶ glyphs read them per paint). RF_Transactional was
		//    cleared at spawn — the ClearFlags-before-MakeLinkTo invariant holds.
		TMap<FString, UPaper2DPlusAnimationMapNode_Move*> LiveMovesByLower;
		TArray<UPaper2DPlusAnimationMapNode_Transition*> LiveEdges;
		CollectLiveNodes(LiveMovesByLower, LiveEdges);
		if (UEdGraphPin* OutputPin = MarkerNode->GetOutputPin())
		{
			OutputPin->BreakAllPinLinks();
			UPaper2DPlusAnimationMapNode_Move* TargetNode = LiveMovesByLower.FindRef(TargetLower);
			if (TargetNode)
			{
				if (UEdGraphPin* InputPin = TargetNode->GetInputPin())
				{
					OutputPin->MakeLinkTo(InputPin);
				}
				TargetNode->bIsChainStart = true;
			}
		}
		if (!PreviousLower.IsEmpty())
		{
			if (UPaper2DPlusAnimationMapNode_Move* PreviousNode = LiveMovesByLower.FindRef(PreviousLower))
			{
				PreviousNode->bIsChainStart = false;
			}
		}
		MarkerNode->TargetMoveLower = TargetLower;
		// Adopt the marker's spot under its NEW synthetic key so a later rebuild re-places it where
		// the user aimed from (a floating marker's position becomes the targeted marker's position).
		ChainStartMarkerPositions.Add(AnimationMapPanel_ChainStartPositionKey(TargetLower),
			FVector2D(MarkerNode->NodePosX, MarkerNode->NodePosY));

		// 3) THE lockstep invariant (LastProjection contract) with the wire funnel's mid-gesture-pend
		//    exception: an external edit that pended while the guard was up must not be swallowed by a
		//    refreshed baseline.
		if (!bReconcilePending)
		{
			LastProjection = ProjectGraphForCurrentView(Asset);
			RestampComboDisplayFromBaseline();
		}
	}

	// 4) EXPLICIT broadcast after the guard released (the AE8 graph->list direction — the flag is
	//    list-visible via chain/phase derivations, and this doubles as the pend-flush). Our own
	//    handler re-enters -> RequestReconcile -> diff EMPTY against the refreshed baseline ->
	//    zero graph mutation (the reconcile-tail sync then verifies the marker as already-wired).
	if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
	return true;
}

void SAnimationMapPanel::HandleChainStartUnlinkRequested(UPaper2DPlusAnimationMapNode_ChainStart* MarkerNode)
{
	// THE BREAK GESTURE (schema Break* overrides route here, bRebuildInProgress-gated at the schema).
	// Clears the target's flag in a transaction and leaves the marker FLOATING at its position.
	if (bGraphWriteInProgress || !GraphObj || GraphObj->bRebuildInProgress || !MarkerNode)
	{
		return;
	}
	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset)
	{
		return;
	}

	const FString PreviousLower = MarkerNode->TargetMoveLower;
	if (PreviousLower.IsEmpty())
	{
		// Already floating — a stray break gesture just tidies any orphan links (no data, no dirty).
		if (UEdGraphPin* OutputPin = MarkerNode->GetOutputPin())
		{
			OutputPin->BreakAllPinLinks();
		}
		return;
	}
	{
		TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);

		// Flag clear in ONE transaction (nests inside the engine's break transaction when the gesture
		// came from a Break-Link menu/command; stands alone otherwise — either way one undo step).
		// The scope-tag resolve mirrors the aim gesture; a marker can only exist on its group surface.
		const int32 EntryIndex = (bAnimationMapGroupGraphOpen && !bAnimationMapGroupGraphIsUnassigned
				&& ActiveAnimationMapGroupTag.IsValid())
			? AnimationMapPanel_FindGroupEntryIndex(Asset, ActiveAnimationMapGroupTag, PreviousLower)
			: INDEX_NONE;
		const FFlipbookTagMapping* Mapping = Asset->TagMappings.Find(ActiveAnimationMapGroupTag);
		if (EntryIndex != INDEX_NONE && Mapping && Mapping->Entries[EntryIndex].bIsChainStart)
		{
			BeginTransaction(LOCTEXT("AnimationMapBreakChainStart", "Break Chain Start"));
			Asset->SetTagMappingEntryChainStart(ActiveAnimationMapGroupTag, EntryIndex, false);
			EndTransaction(); // pend-flush suppressed by the guard; the broadcast below flushes instead
		}

		// Graph lockstep: unlink, float the marker in place, fade the ex-target's ▶ mirror.
		if (UEdGraphPin* OutputPin = MarkerNode->GetOutputPin())
		{
			OutputPin->BreakAllPinLinks();
		}
		TMap<FString, UPaper2DPlusAnimationMapNode_Move*> LiveMovesByLower;
		TArray<UPaper2DPlusAnimationMapNode_Transition*> LiveEdges;
		CollectLiveNodes(LiveMovesByLower, LiveEdges);
		if (UPaper2DPlusAnimationMapNode_Move* PreviousNode = LiveMovesByLower.FindRef(PreviousLower))
		{
			PreviousNode->bIsChainStart = false;
		}
		MarkerNode->TargetMoveLower.Reset();

		if (!bReconcilePending)
		{
			LastProjection = ProjectGraphForCurrentView(Asset);
			RestampComboDisplayFromBaseline();
		}
	}

	if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
}

UEdGraphNode* SAnimationMapPanel::HandleAddChainStartRequested(const FVector2D& GraphPosition)
{
	// Empty-canvas "Add Chain Start": a FLOATING marker is pure transient graph furniture — no data
	// change, no transaction, no dirty. The schema already omits the action off the exact-group
	// surface; this guard is belt-and-braces for future callers.
	if (bGraphWriteInProgress || !GraphObj)
	{
		return nullptr;
	}
	if (!bAnimationMapGroupGraphOpen || bAnimationMapGroupGraphIsUnassigned || !ActiveAnimationMapGroupTag.IsValid())
	{
		AnimationMapPanel_ChainStartRefusalToast(LOCTEXT("ChainStartAddNoGroupScope",
			"Chain starts belong to a tag group — open a group graph to add one."));
		return nullptr;
	}

	UPaper2DPlusAnimationMapNode_ChainStart* Marker = nullptr;
	{
		TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);
		// The funnel broadcasts a granular AddNode action; no default graph notification is needed.
		Marker = UPaper2DPlusAnimationMap::SpawnNodeUntransactional<UPaper2DPlusAnimationMapNode_ChainStart>(*GraphObj,
			[](UPaper2DPlusAnimationMapNode_ChainStart& Node)
			{
				Node.TargetMoveLower.Reset(); // explicit: floating
			});
		Marker->NodePosX = FMath::RoundToInt32(GraphPosition.X);
		Marker->NodePosY = FMath::RoundToInt32(GraphPosition.Y);
	}
	// Markers live outside the projection, so LastProjection needs no refresh and no reconcile fires.
	return Marker;
}

void SAnimationMapPanel::HandleChainStartMarkerMoveCommitted(UPaper2DPlusAnimationMapNode_ChainStart* MarkerNode)
{
	// MARKER-MOVE persistence (the HandleNodeMoveCommitted sibling, minus the asset write): fired by
	// the widget's bMarkDirty=true MoveTo inside the ENGINE's drag/nudge transaction. The position
	// lands in the PANEL-owned session map only (see ChainStartMarkerPositions in the header) — no
	// Modify, no dirty; the engine transaction records nothing for this gesture, which is correct for
	// session-transient furniture.
	if (!MarkerNode || !GraphObj || GraphObj->bRebuildInProgress || bGraphWriteInProgress)
	{
		return;
	}
	if (MarkerNode->TargetMoveLower.IsEmpty())
	{
		return; // floating: the live node IS the position; nothing keys the map
	}
	ChainStartMarkerPositions.Add(AnimationMapPanel_ChainStartPositionKey(MarkerNode->TargetMoveLower),
		FVector2D(MarkerNode->NodePosX, MarkerNode->NodePosY));
}

// =====================================================================================================
// CHAIN END MARKERS (chain-end rework) — the Chain Start cluster's MIRROR: the wired marker node IS
// the authoring surface for the exact-group mapping entry's bIsChainEnd flag (the combo main line's
// countable END — the runtime spine derivation prefers paths terminating at a flagged end and never
// walks past one). Wire direction is REVERSED vs the start marker: a move's output wires INTO the
// marker's single input. Flat data stays the single source of truth: the gestures below write the
// FLAG through the panel transaction template and mutate the graph in LOCKSTEP (the
// HandleWireCreateRequested discipline); the reconcile-tail sync owns everything else.
// =====================================================================================================

void SAnimationMapPanel::CollectLiveChainEndMarkers(TArray<UPaper2DPlusAnimationMapNode_ChainEnd*>& OutMarkers) const
{
	OutMarkers.Reset();
	if (!GraphObj)
	{
		return;
	}
	for (UEdGraphNode* Node : GraphObj->Nodes)
	{
		if (UPaper2DPlusAnimationMapNode_ChainEnd* Marker = Cast<UPaper2DPlusAnimationMapNode_ChainEnd>(Node))
		{
			OutMarkers.Add(Marker);
		}
	}
}

void SAnimationMapPanel::SynchronizeChainEndMarkers(UPaper2DPlusCharacterProfileAsset* Asset,
	const Paper2DPlusAnimationMap::FGraphProjection& Current)
{
	if (!GraphObj)
	{
		return;
	}

	// The flagged set comes from the PROJECTION (never node stamps — sync must not depend on stamp
	// ordering, and the projection already fail-closes invalid groups via the shared runtime resolver).
	TSet<FString> FlaggedLower;
	for (const Paper2DPlusAnimationMap::FProjectedNode& Node : Current.Nodes)
	{
		if (Node.bIsChainEnd)
		{
			FlaggedLower.Add(Node.MoveNameLower);
		}
	}

	TMap<FString, UPaper2DPlusAnimationMapNode_Move*> LiveMovesByLower;
	TArray<UPaper2DPlusAnimationMapNode_Transition*> LiveEdges;
	CollectLiveNodes(LiveMovesByLower, LiveEdges);

	TArray<UPaper2DPlusAnimationMapNode_ChainEnd*> Markers;
	CollectLiveChainEndMarkers(Markers);

	// Spawn gate: markers materialize only on the EXACT-GROUP surface (the start sync's rule — the
	// board's hidden reconcile projects UNFILTERED, the unassigned surface projects no flags at all;
	// neither is an authoring surface). Removal below still runs everywhere.
	const bool bSpawnScope = bAnimationMapGroupGraphOpen && !bAnimationMapGroupGraphIsUnassigned
		&& ActiveAnimationMapGroupTag.IsValid();

	// Same discipline as MaterializeCommentsForCurrentGroup: direct graph surgery must not re-enter
	// the write-through hooks (the marker MoveTo/unlink seams early-out under this).
	TGuardValue<bool> RebuildGuard(GraphObj->bRebuildInProgress, true);
	bool bChanged = false;

	// (b) TARGETED markers: remove when the target move is gone, no longer flagged, degraded to a
	// stub, or duplicated (first survives). DIVERGENCE from the start sync: a stub target has NO
	// output pin to wire from (stubs allocate none), so a flag surviving on a now-dangling entry
	// cannot render a wired end marker — remove rather than strand an unwireable marker. (c)
	// FLOATING markers are NEVER removed — session-transient authoring intent; only the user
	// deletes them.
	TSet<FString> WiredTargets;
	for (UPaper2DPlusAnimationMapNode_ChainEnd* Marker : Markers)
	{
		if (Marker->TargetMoveLower.IsEmpty())
		{
			continue; // floating — (c)
		}
		UPaper2DPlusAnimationMapNode_Move* Target = LiveMovesByLower.FindRef(Marker->TargetMoveLower);
		const bool bDuplicate = WiredTargets.Contains(Marker->TargetMoveLower);
		if (!Target || !FlaggedLower.Contains(Marker->TargetMoveLower) || !Target->GetOutputPin() || bDuplicate)
		{
			// Position stays in ChainEndMarkerPositions (kept-stale, resurrect-friendly).
			GraphObj->RemoveNode(Marker);
			bChanged = true;
			continue;
		}
		WiredTargets.Add(Marker->TargetMoveLower);

		// (a) exactly one INCOMING wire from the move's output/whole-body pin. The marker was spawned
		// through the untransactional funnel, so RF_Transactional is already cleared — the
		// ClearFlags-before-MakeLinkTo invariant holds for this surgery.
		UEdGraphPin* InputPin = Marker->GetInputPin();
		UEdGraphPin* OutputPin = Target->GetOutputPin();
		if (InputPin && OutputPin && (!InputPin->LinkedTo.Contains(OutputPin) || InputPin->LinkedTo.Num() != 1))
		{
			InputPin->BreakAllPinLinks();
			OutputPin->MakeLinkTo(InputPin);
			bChanged = true;
		}
	}

	// (a) every flagged move without a marker gets one, wired, at its stored spot (the synthetic
	// "__chainend__:<move>" key) else ~180 right / 40 down of the move node (the OPPOSITE corner
	// from the start marker, so a move flagged both start and end shows both markers apart; the
	// move's position was already reapplied this reconcile).
	if (bSpawnScope)
	{
		for (const FString& FlaggedName : FlaggedLower)
		{
			if (WiredTargets.Contains(FlaggedName))
			{
				continue;
			}
			UPaper2DPlusAnimationMapNode_Move* Target = LiveMovesByLower.FindRef(FlaggedName);
			if (!Target || !Target->GetOutputPin())
			{
				continue; // projection guarantees the node; stubs own no output pin to wire from
			}
			const FVector2D* StoredPos =
				ChainEndMarkerPositions.Find(AnimationMapPanel_ChainEndPositionKey(FlaggedName));
			const FVector2D MarkerPos = StoredPos
				? *StoredPos
				: FVector2D(Target->NodePosX + 180.0, Target->NodePosY + 40.0);

			UPaper2DPlusAnimationMapNode_ChainEnd* Marker =
				UPaper2DPlusAnimationMap::SpawnNodeUntransactional<UPaper2DPlusAnimationMapNode_ChainEnd>(*GraphObj,
					[&FlaggedName](UPaper2DPlusAnimationMapNode_ChainEnd& Node)
					{
						Node.TargetMoveLower = FlaggedName;
					});
			Marker->NodePosX = FMath::RoundToInt32(MarkerPos.X);
			Marker->NodePosY = FMath::RoundToInt32(MarkerPos.Y);
			// Link surgery AFTER the funnel cleared RF_Transactional (the U3 ordering invariant).
			UEdGraphPin* InputPin = Marker->GetInputPin();
			UEdGraphPin* OutputPin = Target->GetOutputPin();
			if (InputPin && OutputPin)
			{
				OutputPin->MakeLinkTo(InputPin);
			}
			bChanged = true;
		}
	}

	if (bChanged)
	{
		if (GraphEditorWidget.IsValid())
		{
			if (SGraphPanel* GraphPanel = GraphEditorWidget->GetGraphPanel())
			{
				GraphPanel->Invalidate(EInvalidateWidgetReason::Paint);
			}
		}
	}
}

bool SAnimationMapPanel::HandleChainEndLinkRequested(UPaper2DPlusAnimationMapNode_ChainEnd* MarkerNode,
	const FString& TargetMoveName)
{
	// THE AIM GESTURE (HandleChainStartLinkRequested MIRRORED — wire direction reversed: the move's
	// output wired INTO this marker's input). Called by the schema INSIDE the engine's open
	// GraphEd_CreateConnection transaction — do NOT ReconcileNow here (live pin pointers, the
	// mid-gesture crash class); mutate data + graph in LOCKSTEP and refresh the baseline.
	if (bGraphWriteInProgress || !GraphObj || !MarkerNode || TargetMoveName.IsEmpty())
	{
		return false;
	}
	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset)
	{
		return false;
	}

	// Chain ends are EXACT-GROUP identity — no group scope (unassigned/board), no gesture.
	if (!bAnimationMapGroupGraphOpen || bAnimationMapGroupGraphIsUnassigned || !ActiveAnimationMapGroupTag.IsValid())
	{
		AnimationMapPanel_ChainStartRefusalToast(LOCTEXT("ChainEndNoGroupScope",
			"Chain ends belong to a tag group — open a group graph to set one."));
		return false;
	}

	// Pre-validate BEFORE opening the nested transaction (never open+Modify for a refusal).
	const int32 EntryIndex = AnimationMapPanel_FindGroupEntryIndex(Asset, ActiveAnimationMapGroupTag, TargetMoveName);
	if (EntryIndex == INDEX_NONE)
	{
		AnimationMapPanel_ChainStartRefusalToast(FText::Format(LOCTEXT("ChainEndTargetNotInGroup",
			"'{0}' is not one unambiguous member of this group — repair duplicate or missing membership first."),
			FText::FromString(TargetMoveName)));
		return false;
	}
	const FFlipbookTagMapping* Mapping = Asset->TagMappings.Find(ActiveAnimationMapGroupTag);
	if (Mapping && Mapping->Entries[EntryIndex].bIsChainEnd)
	{
		// Already flagged (by this marker or another) — a no-op gesture opens NO transaction.
		AnimationMapPanel_ChainStartRefusalToast(FText::Format(LOCTEXT("ChainEndAlreadyFlagged",
			"'{0}' is already a chain end."), FText::FromString(TargetMoveName)));
		return false;
	}

	const FString TargetLower = TargetMoveName.ToLower();
	const FString PreviousLower = MarkerNode->TargetMoveLower;
	const int32 PreviousIndex = PreviousLower.IsEmpty()
		? INDEX_NONE
		: AnimationMapPanel_FindGroupEntryIndex(
			Asset,
			ActiveAnimationMapGroupTag,
			PreviousLower);
	if (!PreviousLower.IsEmpty() && PreviousIndex == INDEX_NONE)
	{
		AnimationMapPanel_ChainStartRefusalToast(FText::Format(
			LOCTEXT(
				"ChainEndPreviousTargetAmbiguous",
				"Cannot re-aim this Chain End: its previous target '{0}' is missing or duplicated in the group."),
			FText::FromString(PreviousLower)));
		return false;
	}
	{
		TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);

		// 1) DATA write-through: clear-old + set-new in ONE panel transaction, NESTED inside the
		//    engine's connection transaction (one undo step; asset-only recorded — every graph node is
		//    non-transactional by the spawn-funnel invariant). Re-aiming = the one-incoming-wire rule:
		//    a marker carries at most ONE designation, so its previous target unflags first.
		BeginTransaction(LOCTEXT("AnimationMapSetChainEnd", "Set Chain End"));
		if (PreviousIndex != INDEX_NONE)
		{
			Asset->SetTagMappingEntryChainEnd(ActiveAnimationMapGroupTag, PreviousIndex, false);
		}
		Asset->SetTagMappingEntryChainEnd(ActiveAnimationMapGroupTag, EntryIndex, true);
		EndTransaction(); // pend-flush suppressed by the guard; the broadcast below flushes instead

		// 2) Graph LOCKSTEP, still inside the ENGINE's transaction: break the marker's old wire (at
		//    most one exists), wire the new source move in, retarget the marker, and mirror the flags
		//    onto the move nodes' display state (the ⏹ glyphs read them per paint). RF_Transactional
		//    was cleared at spawn — the ClearFlags-before-MakeLinkTo invariant holds.
		TMap<FString, UPaper2DPlusAnimationMapNode_Move*> LiveMovesByLower;
		TArray<UPaper2DPlusAnimationMapNode_Transition*> LiveEdges;
		CollectLiveNodes(LiveMovesByLower, LiveEdges);
		if (UEdGraphPin* InputPin = MarkerNode->GetInputPin())
		{
			InputPin->BreakAllPinLinks();
			UPaper2DPlusAnimationMapNode_Move* TargetNode = LiveMovesByLower.FindRef(TargetLower);
			if (TargetNode)
			{
				if (UEdGraphPin* OutputPin = TargetNode->GetOutputPin())
				{
					OutputPin->MakeLinkTo(InputPin);
				}
				TargetNode->bIsChainEnd = true;
			}
		}
		if (!PreviousLower.IsEmpty())
		{
			if (UPaper2DPlusAnimationMapNode_Move* PreviousNode = LiveMovesByLower.FindRef(PreviousLower))
			{
				PreviousNode->bIsChainEnd = false;
			}
		}
		MarkerNode->TargetMoveLower = TargetLower;
		// Adopt the marker's spot under its NEW synthetic key so a later rebuild re-places it where
		// the user aimed at (a floating marker's position becomes the targeted marker's position).
		ChainEndMarkerPositions.Add(AnimationMapPanel_ChainEndPositionKey(TargetLower),
			FVector2D(MarkerNode->NodePosX, MarkerNode->NodePosY));

		// 3) THE lockstep invariant (LastProjection contract) with the wire funnel's mid-gesture-pend
		//    exception: an external edit that pended while the guard was up must not be swallowed by a
		//    refreshed baseline.
		if (!bReconcilePending)
		{
			LastProjection = ProjectGraphForCurrentView(Asset);
			RestampComboDisplayFromBaseline();
		}
	}

	// 4) EXPLICIT broadcast after the guard released (the AE8 graph->list direction — the flag is
	//    list-visible via chain/phase derivations, and this doubles as the pend-flush). Our own
	//    handler re-enters -> RequestReconcile -> diff EMPTY against the refreshed baseline ->
	//    zero graph mutation (the reconcile-tail sync then verifies the marker as already-wired).
	if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
	return true;
}

void SAnimationMapPanel::HandleChainEndUnlinkRequested(UPaper2DPlusAnimationMapNode_ChainEnd* MarkerNode)
{
	// THE BREAK GESTURE (schema Break* overrides route here, bRebuildInProgress-gated at the schema).
	// Clears the target's flag in a transaction and leaves the marker FLOATING at its position.
	if (bGraphWriteInProgress || !GraphObj || GraphObj->bRebuildInProgress || !MarkerNode)
	{
		return;
	}
	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset)
	{
		return;
	}

	const FString PreviousLower = MarkerNode->TargetMoveLower;
	if (PreviousLower.IsEmpty())
	{
		// Already floating — a stray break gesture just tidies any orphan links (no data, no dirty).
		if (UEdGraphPin* InputPin = MarkerNode->GetInputPin())
		{
			InputPin->BreakAllPinLinks();
		}
		return;
	}
	{
		TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);

		// Flag clear in ONE transaction (nests inside the engine's break transaction when the gesture
		// came from a Break-Link menu/command; stands alone otherwise — either way one undo step).
		// The scope-tag resolve mirrors the aim gesture; a marker can only exist on its group surface.
		const int32 EntryIndex = (bAnimationMapGroupGraphOpen && !bAnimationMapGroupGraphIsUnassigned
				&& ActiveAnimationMapGroupTag.IsValid())
			? AnimationMapPanel_FindGroupEntryIndex(Asset, ActiveAnimationMapGroupTag, PreviousLower)
			: INDEX_NONE;
		const FFlipbookTagMapping* Mapping = Asset->TagMappings.Find(ActiveAnimationMapGroupTag);
		if (EntryIndex != INDEX_NONE && Mapping && Mapping->Entries[EntryIndex].bIsChainEnd)
		{
			BeginTransaction(LOCTEXT("AnimationMapBreakChainEnd", "Break Chain End"));
			Asset->SetTagMappingEntryChainEnd(ActiveAnimationMapGroupTag, EntryIndex, false);
			EndTransaction(); // pend-flush suppressed by the guard; the broadcast below flushes instead
		}

		// Graph lockstep: unlink, float the marker in place, fade the ex-target's ⏹ mirror.
		if (UEdGraphPin* InputPin = MarkerNode->GetInputPin())
		{
			InputPin->BreakAllPinLinks();
		}
		TMap<FString, UPaper2DPlusAnimationMapNode_Move*> LiveMovesByLower;
		TArray<UPaper2DPlusAnimationMapNode_Transition*> LiveEdges;
		CollectLiveNodes(LiveMovesByLower, LiveEdges);
		if (UPaper2DPlusAnimationMapNode_Move* PreviousNode = LiveMovesByLower.FindRef(PreviousLower))
		{
			PreviousNode->bIsChainEnd = false;
		}
		MarkerNode->TargetMoveLower.Reset();

		if (!bReconcilePending)
		{
			LastProjection = ProjectGraphForCurrentView(Asset);
			RestampComboDisplayFromBaseline();
		}
	}

	if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
}

UEdGraphNode* SAnimationMapPanel::HandleAddChainEndRequested(const FVector2D& GraphPosition)
{
	// Empty-canvas "Add Chain End": a FLOATING marker is pure transient graph furniture — no data
	// change, no transaction, no dirty. The schema already omits the action off the exact-group
	// surface; this guard is belt-and-braces for future callers.
	if (bGraphWriteInProgress || !GraphObj)
	{
		return nullptr;
	}
	if (!bAnimationMapGroupGraphOpen || bAnimationMapGroupGraphIsUnassigned || !ActiveAnimationMapGroupTag.IsValid())
	{
		AnimationMapPanel_ChainStartRefusalToast(LOCTEXT("ChainEndAddNoGroupScope",
			"Chain ends belong to a tag group — open a group graph to add one."));
		return nullptr;
	}

	UPaper2DPlusAnimationMapNode_ChainEnd* Marker = nullptr;
	{
		TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);
		// The funnel broadcasts a granular AddNode action; no default graph notification is needed.
		Marker = UPaper2DPlusAnimationMap::SpawnNodeUntransactional<UPaper2DPlusAnimationMapNode_ChainEnd>(*GraphObj,
			[](UPaper2DPlusAnimationMapNode_ChainEnd& Node)
			{
				Node.TargetMoveLower.Reset(); // explicit: floating
			});
		Marker->NodePosX = FMath::RoundToInt32(GraphPosition.X);
		Marker->NodePosY = FMath::RoundToInt32(GraphPosition.Y);
	}
	// Markers live outside the projection, so LastProjection needs no refresh and no reconcile fires.
	return Marker;
}

void SAnimationMapPanel::HandleChainEndMarkerMoveCommitted(UPaper2DPlusAnimationMapNode_ChainEnd* MarkerNode)
{
	// MARKER-MOVE persistence (the HandleChainStartMarkerMoveCommitted sibling): fired by the
	// widget's bMarkDirty=true MoveTo inside the ENGINE's drag/nudge transaction. The position lands
	// in the PANEL-owned session map only (see ChainEndMarkerPositions in the header) — no Modify,
	// no dirty; the engine transaction records nothing for this gesture, which is correct for
	// session-transient furniture.
	if (!MarkerNode || !GraphObj || GraphObj->bRebuildInProgress || bGraphWriteInProgress)
	{
		return;
	}
	if (MarkerNode->TargetMoveLower.IsEmpty())
	{
		return; // floating: the live node IS the position; nothing keys the map
	}
	ChainEndMarkerPositions.Add(AnimationMapPanel_ChainEndPositionKey(MarkerNode->TargetMoveLower),
		FVector2D(MarkerNode->NodePosX, MarkerNode->NodePosY));
}

// =====================================================================================================
// TASK-108 U6 (R8): the Map tag filter — dim (never hide) non-matching nodes and board tiles.
// TASK-151 U3 (R10/R16) reworked its PRESENTATION only: a compact "Filter" control while inactive, a
// directly removable chip while active, over a cross-version menu-hosted picker. The dim semantics,
// the persisted GEditorPerProjectIni key, and SetMapFilterTag's write path are unchanged.
// =====================================================================================================

void SAnimationMapPanel::SetMapFilterTag(const FGameplayTag& InFilterTag)
{
	if (MapFilterTag == InFilterTag)
	{
		return;
	}
	MapFilterTag = InFilterTag;

	// Persist per the panel's INI view-state precedent (in-memory set like the zoom/view keys).
	GConfig->SetString(AnimationMapPanel_ConfigSection, AnimationMapPanel_ConfigKeyFilterTag,
		MapFilterTag.IsValid() ? *MapFilterTag.GetTagName().ToString() : TEXT(""), GEditorPerProjectIni);
	// No explicit Flush (legacy-cleanup 2026-07): GConfig holds the value; the engine flushes
	// GEditorPerProjectIni itself. A manual Flush rewrites the whole multi-hundred-KB ini via
	// tmp+MoveFile and stalled ~8s per call under file-lock contention - the slow-close bug.

	RebuildMapFilterPicker();       // swaps the rail between the compact Filter control and the tag chip
	ApplyMapFilterToLiveNodes();    // re-stamp the scoped graph's dim flags (widgets read per paint)
	QueueAnimationMapRefresh();     // rebuild the board so tile dimming reflects the filter
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAnimationMapPanel::RebuildMapFilterPicker()
{
	// TASK-151 U3 (R10/R16): filtering is COMPACT while inactive and unmistakably STATEFUL while
	// active. Inactive -> one small "Filter" control; active -> a removable chip naming the tag with a
	// direct clear affordance, so a collapsed control can never hide a live filter.
	// STATIC content rebuilt on commit — the details-pane picker idiom (a bound .Tag_Lambda in an
	// always-visible surface invalidates Layout every frame).
	if (!MapFilterPickerBox.IsValid())
	{
		return;
	}

	const FText FilterHelp = LOCTEXT(
		"AnimationMapFilterTip",
		"Dim animations whose effective tags (own + chain-inherited + group-implied) don't match this tag (hierarchical — a Combat filter matches Combat.Heavy). Group tiles dim when their group key doesn't match. Nothing is hidden.");

	if (!MapFilterTag.IsValid())
	{
		TSharedRef<SComboButton> InactiveControl =
			SNew(SComboButton)
			.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
			.ContentPadding(FMargin(6.0f, 1.0f))
			.ToolTipText(FilterHelp)
			.OnGetMenuContent(FOnGetContent::CreateSP(this, &SAnimationMapPanel::BuildMapFilterPickerMenu))
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AnimationMapFilterInactive", "Filter"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.75f)))
			];
		MapFilterInvoker = InactiveControl;
		MapFilterPickerBox->SetContent(InactiveControl);
		return;
	}

	// Active: a tag chip in the shared registry color, plus its own clear button.
	//
	// COLORS ARE _Lambda-BOUND, NEVER CAPTURED (CLAUDE.md refresh rule 7). The Tag Colors registry is
	// authored live — `Paper2DPlus.SetTagColor …` and the Project Settings table both broadcast
	// OnTagColorsChanged, which repaints the group tiles, phase badges and node chips. A chip that baked
	// GetAnimationTagChipColor(MapFilterTag) into a local at build time kept its stale color until the
	// filter TAG itself changed, because nothing rebuilds this widget on a color change. Resolving inside
	// the binding (plus the panel's OnTagColorsChanged -> Invalidate(Paint), see HandleTagColorsChanged)
	// keeps the active filter chip in step with every other tag-colored surface.
	auto ResolveChipColor = [this]()
	{
		return Paper2DPlusAnimationMap::GetAnimationTagChipColor(MapFilterTag);
	};
	static const FSlateRoundedBoxBrush ChipBrush(FLinearColor::White, 3.0f);
	const FText ChipTooltip = FText::Format(
		LOCTEXT("AnimationMapFilterActiveTipFmt", "Filtering by {0}.\n\n{1}"),
		FText::FromName(MapFilterTag.GetTagName()),
		FilterHelp);

	TSharedRef<SComboButton> ChipInvoker =
		SNew(SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
		.HasDownArrow(false)
		.ContentPadding(FMargin(2.0f, 0.0f))
		.ToolTipText(ChipTooltip)
		.OnGetMenuContent(FOnGetContent::CreateSP(this, &SAnimationMapPanel::BuildMapFilterPickerMenu))
		.ButtonContent()
		[
			SNew(SBox)
			.MaxDesiredWidth(AnimationMapPanel_RailTextMaxWidth)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Paper2DPlusAnimationTagChips::GetTagLeafString(MapFilterTag)))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity_Lambda([ResolveChipColor]()
				{
					return FSlateColor(ResolveChipColor().TextColor);
				})
			]
		];
	MapFilterInvoker = ChipInvoker;

	MapFilterPickerBox->SetContent(
		SNew(SBorder)
		.BorderImage(&ChipBrush)
		.BorderBackgroundColor_Lambda([ResolveChipColor]()
		{
			return FSlateColor(ResolveChipColor().Color);
		})
		.Padding(FMargin(4.0f, 0.0f, 1.0f, 0.0f))
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				ChipInvoker
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(2.0f, 0.0f))
				.ToolTipText(LOCTEXT("AnimationMapFilterClearTip", "Clear the tag filter"))
				.OnClicked_Lambda([this]()
				{
					// Clearing rebuilds this very chip, so the button must not be replaced from inside its
					// own click handler — defer one tick (the house deferred-rebuild rule).
					const TWeakPtr<SAnimationMapPanel> WeakPanel = SharedThis(this);
					auto ClearFilter = [WeakPanel]()
					{
						if (const TSharedPtr<SAnimationMapPanel> Panel = WeakPanel.Pin())
						{
							Panel->SetMapFilterTag(FGameplayTag());
							Panel->FocusMapFilterControl();
						}
					};
					if (GEditor)
					{
						GEditor->GetTimerManager()->SetTimerForNextTick(ClearFilter);
					}
					else
					{
						ClearFilter();
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AnimationMapFilterClear", "✕"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
					.ColorAndOpacity_Lambda([ResolveChipColor]()
					{
						return FSlateColor(ResolveChipColor().TextColor);
					})
				]
			]
		]);
}

TSharedRef<SWidget> SAnimationMapPanel::BuildMapFilterPickerMenu()
{
	// IGameplayTagsEditorModule::MakeGameplayTagWidget is the ONE tag-picker entry point available
	// unchanged on UE 5.0-5.8 (SGameplayTagCombo is 5.3+), so every supported engine gets an
	// interactive tree here instead of the old pre-5.3 read-only degradation.
	// NO filter string: effective tags include TagMappings group keys, which live OUTSIDE
	// Paper2DPlus.Animation on real projects.
	MapFilterPickerValue = MakeShared<FGameplayTag>(MapFilterTag);

	const TWeakPtr<SAnimationMapPanel> WeakPanel = SharedThis(this);
	FOnSetGameplayTag OnSetTag = FOnSetGameplayTag::CreateLambda(
		[WeakPanel](const FGameplayTag& InNewTag)
		{
			if (const TSharedPtr<SAnimationMapPanel> Panel = WeakPanel.Pin())
			{
				Panel->CommitMapFilterTagFromPicker(InNewTag);
			}
		});

	return SNew(SBox)
		.MinDesiredWidth(300.0f)
		.MaxDesiredHeight(420.0f)
		.Padding(2.0f)
		[
			// Menu-hosted picker: the engine tag rows' right-click management menu fatal-asserts against
			// an auto-dismissing menu host ("Window Creation Failed (1400)"). Selection-only here.
			SNew(SMenuHostedTagPickerGuard)
			[
				IGameplayTagsEditorModule::Get().MakeGameplayTagWidget(OnSetTag, MapFilterPickerValue)
			]
		];
}

void SAnimationMapPanel::CommitMapFilterTagFromPicker(const FGameplayTag InNewTag)
{
	// Committing rebuilds MapFilterPickerBox, i.e. DESTROYS the combo button that owns this open menu.
	// Dismiss the menu first, then defer the commit one tick so the picker callback has fully unwound
	// before its invoker widget is replaced.
	FSlateApplication::Get().DismissAllMenus();

	const TWeakPtr<SAnimationMapPanel> WeakPanel = SharedThis(this);
	auto ApplyPick = [WeakPanel, InNewTag]()
	{
		if (const TSharedPtr<SAnimationMapPanel> Panel = WeakPanel.Pin())
		{
			Panel->SetMapFilterTag(InNewTag);
			Panel->FocusMapFilterControl();
		}
	};
	if (GEditor)
	{
		GEditor->GetTimerManager()->SetTimerForNextTick(ApplyPick);
	}
	else
	{
		ApplyPick();
	}
}

void SAnimationMapPanel::FocusMapFilterControl()
{
	// Focus returns to the INVOKER the popup was opened from (KTD12) — resolved after the rebuild, so
	// it lands on the replacement chip/button rather than the destroyed one.
	if (MapFilterInvoker.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(MapFilterInvoker, EFocusCause::SetDirectly);
	}
}

void SAnimationMapPanel::ApplyMapFilterToLiveNodes()
{
	TMap<FString, UPaper2DPlusAnimationMapNode_Move*> LiveMovesByLower;
	TArray<UPaper2DPlusAnimationMapNode_Transition*> LiveEdges;
	CollectLiveNodes(LiveMovesByLower, LiveEdges);
	for (const TPair<FString, UPaper2DPlusAnimationMapNode_Move*>& Pair : LiveMovesByLower)
	{
		// Match on the node's DENORMALIZED effective tags (own ∪ chain ∪ group — stamped by the
		// reconcile from the projection batch); the widget's dim lambdas pick the flag up per paint.
		Pair.Value->bDimmedByFilter = MapFilterTag.IsValid()
			&& !Paper2DPlusAnimationMap::AnimationTagFilterMatches(Pair.Value->EffectiveAnimationTags(), MapFilterTag);
	}
}

// =====================================================================================================
// U5 DELETE COMMAND (R7, F3, AE2/AE6/AE7) — THE single row-deletion funnel; the Delete key is its only
// caller this PR (the suicide hook stays cleanup-only).
// =====================================================================================================

bool SAnimationMapPanel::CanDeleteSelectedNodes() const
{
	// The FAIGraphEditor::CanDeleteNodes shape (AIGraphEditor.cpp:175-189): deletable if ANY selected
	// node says CanUserDeleteNode.
	if (!GraphEditorWidget.IsValid())
	{
		return false;
	}
	for (UObject* SelectedObject : GraphEditorWidget->GetSelectedNodes())
	{
		const UEdGraphNode* Node = Cast<UEdGraphNode>(SelectedObject);
		if (Node && Node->CanUserDeleteNode())
		{
			return true;
		}
	}
	return false;
}

bool SAnimationMapPanel::CanRenameSelectedComment() const
{
	if (!GraphEditorWidget.IsValid() || !GraphObj)
	{
		return false;
	}
	const FGraphPanelSelectionSet SelectedNodes = GraphEditorWidget->GetSelectedNodes();
	if (SelectedNodes.Num() != 1)
	{
		return false;
	}
	UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(*SelectedNodes.CreateConstIterator());
	return CommentNode && CommentNode->GetGraph() == GraphObj
		&& GraphObj->Nodes.Contains(CommentNode) && CommentNode->GetCanRenameNode();
}

void SAnimationMapPanel::RenameSelectedComment()
{
	if (!CanRenameSelectedComment())
	{
		return;
	}
	const FGraphPanelSelectionSet SelectedNodes = GraphEditorWidget->GetSelectedNodes();
	UEdGraphNode_Comment* CommentNode = CastChecked<UEdGraphNode_Comment>(*SelectedNodes.CreateConstIterator());
	GraphEditorWidget->JumpToNode(CommentNode, /*bRequestRename=*/true, /*bSelectNode=*/true);
}

void SAnimationMapPanel::SelectAllNodes()
{
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->SelectAllNodes();
	}
}

void SAnimationMapPanel::DeleteSelectedNodes()
{
	// Re-entrancy: the helper no-ops when called re-entrantly (KTD — with IDENTICAL duplicate rows a
	// re-entrant delete would defeat the snapshot guard and remove both).
	if (bGraphWriteInProgress || !GraphObj || !GraphEditorWidget.IsValid())
	{
		return;
	}
	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset)
	{
		return;
	}

	// Comments delete independently of the row/placement machinery (no confirm). Handle them first in their
	// own transaction; a pure-comment selection then early-returns at the edge/move partition below.
	{
		TArray<UEdGraphNode_Comment*> CommentNodes;
		for (UObject* SelectedObject : GraphEditorWidget->GetSelectedNodes())
		{
			if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(SelectedObject))
			{
				if (CommentNode->CanUserDeleteNode())
				{
					CommentNodes.Add(CommentNode);
				}
			}
		}
		if (CommentNodes.Num() > 0)
		{
			TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);
			FScopedTransaction Transaction(LOCTEXT("AnimationMapDeleteComments", "Delete Animation Map Comment(s)"));
			Asset->Modify();
			const FString Scope = GetCommentScopeKey();
			FPaper2DPlusAnimationMapCommentList* List = Scope.IsEmpty() ? nullptr : Asset->AnimationMapComments.Find(Scope);
			for (UEdGraphNode_Comment* CommentNode : CommentNodes)
			{
				if (List)
				{
					List->Comments.RemoveAll([CommentNode](const FPaper2DPlusAnimationMapComment& C) { return C.CommentId == CommentNode->NodeGuid; });
				}
				GraphObj->RemoveNode(CommentNode);
			}
			if (SGraphPanel* GraphPanel = GraphEditorWidget->GetGraphPanel())
			{
				GraphPanel->Invalidate(EInvalidateWidgetReason::Paint);
			}
		}
	}

	// Chain Start AND Chain End markers (the mirrored marker pair) delete before the row machinery,
	// comment-style (no confirm — the blast radius is one flag each). A TARGETED marker clears its
	// entry's flag in ONE shared transaction with the node removal; a FLOATING marker is a pure
	// graph-node removal (no data, no transaction). Deleting a flagged MOVE node deliberately does
	// NOT clear the flags (they live on the mapping entry, untouched by move-node deletion) — its
	// markers then leave via the syncs.
	{
		TArray<UPaper2DPlusAnimationMapNode_ChainStart*> MarkerNodes;
		TArray<UPaper2DPlusAnimationMapNode_ChainEnd*> EndMarkerNodes;
		for (UObject* SelectedObject : GraphEditorWidget->GetSelectedNodes())
		{
			if (UPaper2DPlusAnimationMapNode_ChainStart* MarkerNode = Cast<UPaper2DPlusAnimationMapNode_ChainStart>(SelectedObject))
			{
				if (MarkerNode->CanUserDeleteNode())
				{
					MarkerNodes.Add(MarkerNode);
				}
			}
			else if (UPaper2DPlusAnimationMapNode_ChainEnd* EndMarkerNode = Cast<UPaper2DPlusAnimationMapNode_ChainEnd>(SelectedObject))
			{
				if (EndMarkerNode->CanUserDeleteNode())
				{
					EndMarkerNodes.Add(EndMarkerNode);
				}
			}
		}
		bool bMarkerDataChanged = false;
		if (MarkerNodes.Num() > 0 || EndMarkerNodes.Num() > 0)
		{
			TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);

			// Resolve the flag writes FIRST so a floating-only batch opens no transaction (never
			// dirties). The scope tag is the panel's open group — markers only exist on that surface.
			// Separate index lists per flag: the two mutators address the same entries but different
			// booleans, and a move flagged start AND end may legitimately clear both.
			TArray<int32> EntryIndicesToClear;
			TArray<int32> EndEntryIndicesToClear;
			const bool bHasScope = bAnimationMapGroupGraphOpen && !bAnimationMapGroupGraphIsUnassigned
				&& ActiveAnimationMapGroupTag.IsValid();
			for (const UPaper2DPlusAnimationMapNode_ChainStart* MarkerNode : MarkerNodes)
			{
				if (bHasScope && !MarkerNode->TargetMoveLower.IsEmpty())
				{
					const int32 EntryIndex = AnimationMapPanel_FindGroupEntryIndex(
						Asset, ActiveAnimationMapGroupTag, MarkerNode->TargetMoveLower);
					if (EntryIndex != INDEX_NONE)
					{
						EntryIndicesToClear.AddUnique(EntryIndex);
					}
				}
			}
			for (const UPaper2DPlusAnimationMapNode_ChainEnd* EndMarkerNode : EndMarkerNodes)
			{
				if (bHasScope && !EndMarkerNode->TargetMoveLower.IsEmpty())
				{
					const int32 EntryIndex = AnimationMapPanel_FindGroupEntryIndex(
						Asset, ActiveAnimationMapGroupTag, EndMarkerNode->TargetMoveLower);
					if (EntryIndex != INDEX_NONE)
					{
						EndEntryIndicesToClear.AddUnique(EntryIndex);
					}
				}
			}

			bMarkerDataChanged = EntryIndicesToClear.Num() > 0 || EndEntryIndicesToClear.Num() > 0;
			if (bMarkerDataChanged)
			{
				BeginTransaction(LOCTEXT("AnimationMapDeleteChainMarkers", "Delete Chain Marker(s)"));
				for (const int32 EntryIndex : EntryIndicesToClear)
				{
					Asset->SetTagMappingEntryChainStart(ActiveAnimationMapGroupTag, EntryIndex, false);
				}
				for (const int32 EntryIndex : EndEntryIndicesToClear)
				{
					Asset->SetTagMappingEntryChainEnd(ActiveAnimationMapGroupTag, EntryIndex, false);
				}
				EndTransaction(); // pend-flush suppressed by the guard; the DEFERRED reconcile absorbs pends
			}

			// Node removal under the rebuild lock so the markers' own link breaks can't re-enter the
			// schema's unlink hooks (RemoveNode -> BreakAllNodeLinks bypasses the schema, but the lock
			// is belt-and-braces against future engine routing).
			{
				TGuardValue<bool> RebuildGuard(GraphObj->bRebuildInProgress, true);
				for (UPaper2DPlusAnimationMapNode_ChainStart* MarkerNode : MarkerNodes)
				{
					// Stamp the ex-target's mirror so the ▶ glyph fades without waiting for the stamp loop.
					if (!MarkerNode->TargetMoveLower.IsEmpty())
					{
						for (UEdGraphNode* Node : GraphObj->Nodes)
						{
							UPaper2DPlusAnimationMapNode_Move* MoveNode = Cast<UPaper2DPlusAnimationMapNode_Move>(Node);
							if (MoveNode && MoveNode->MoveName.ToLower() == MarkerNode->TargetMoveLower)
							{
								MoveNode->bIsChainStart = false;
								break;
							}
						}
					}
					GraphObj->RemoveNode(MarkerNode);
				}
				for (UPaper2DPlusAnimationMapNode_ChainEnd* EndMarkerNode : EndMarkerNodes)
				{
					// Stamp the ex-target's mirror so the ⏹ glyph fades without waiting for the stamp loop.
					if (!EndMarkerNode->TargetMoveLower.IsEmpty())
					{
						for (UEdGraphNode* Node : GraphObj->Nodes)
						{
							UPaper2DPlusAnimationMapNode_Move* MoveNode = Cast<UPaper2DPlusAnimationMapNode_Move>(Node);
							if (MoveNode && MoveNode->MoveName.ToLower() == EndMarkerNode->TargetMoveLower)
							{
								MoveNode->bIsChainEnd = false;
								break;
							}
						}
					}
					GraphObj->RemoveNode(EndMarkerNode);
				}
				if (SGraphPanel* GraphPanel = GraphEditorWidget->GetGraphPanel())
				{
					GraphPanel->Invalidate(EInvalidateWidgetReason::Paint);
				}
			}

			// DELIBERATELY no ReconcileNow here (unlike the edge/move batch below): a reconcile's
			// granular removal actions need to finish before the partition below processes any other
			// selected edges/moves. Deferring is safe against the lockstep invariant — the
			// gesture created no widgets a rebuild could destroy, and the stale-baseline echo takes the
			// harmless field-only ChainStartChanges path (stamps + a no-op marker sync).
		}
		// The flag write is list-visible (the AE8 graph->list direction) — broadcast AFTER the guard
		// released; the re-entrant handler arms the deferred reconcile that trues everything up.
		if (bMarkerDataChanged && Model.IsValid())
		{
			Model->NotifyAssetDataChanged();
		}
	}

	// Partition the selection. Edge rows delete snapshot-validated; move nodes prune via
	// RemoveAllRowsForMove after a per-node confirm.
	TArray<UPaper2DPlusAnimationMapNode_Transition*> EdgeNodes;
	TArray<UPaper2DPlusAnimationMapNode_Move*> MoveNodes;
	for (UObject* SelectedObject : GraphEditorWidget->GetSelectedNodes())
	{
		if (UPaper2DPlusAnimationMapNode_Transition* EdgeNode = Cast<UPaper2DPlusAnimationMapNode_Transition>(SelectedObject))
		{
			if (EdgeNode->CanUserDeleteNode())
			{
				EdgeNodes.Add(EdgeNode);
			}
		}
		else if (UPaper2DPlusAnimationMapNode_Move* MoveNode = Cast<UPaper2DPlusAnimationMapNode_Move>(SelectedObject))
		{
			if (MoveNode->CanUserDeleteNode())
			{
				MoveNodes.Add(MoveNode);
			}
		}
	}
	if (EdgeNodes.Num() == 0 && MoveNodes.Num() == 0)
	{
		return;
	}
	TArray<UPaper2DPlusAnimationMapNode_Transition*> LocalEdgeNodes = EdgeNodes;
	bool bAnyMutation = false;
	{
		// The helper's WHOLE scope runs under bGraphWriteInProgress (KTD) — including the modal confirm,
		// which pumps Slate: signals landing mid-dialog pend (never drop) and the post-mutation
		// ReconcileNow absorbs them by re-projecting current data.
		TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);

		// ---- Phase 1: per-move counts from the FLAT DATA + modal confirms, BEFORE any transaction
		// opens (the dialog must never sit inside an open transaction). Copy says "rows" deliberately:
		// the outgoing count includes empty-target authoring rows invisible as wires, so "transitions"
		// would show counts exceeding what the user sees (AE2/AE7). Neither copy ever claims the move
		// itself is deleted — it never is (AE2).
		TArray<FString> ConfirmedMoveNames;
		for (const UPaper2DPlusAnimationMapNode_Move* MoveNode : MoveNodes)
		{
			const Paper2DPlusAnimationMap::FMoveRowCounts Counts =
				Paper2DPlusAnimationMap::CountRowsForMove(Asset, MoveNode->MoveName);
			if (Counts.TotalRows() == 0)
			{
				// Zero transitions: NO dialog. Placed -> just remove the placement entry (the
				// RemoveAllRowsForMove call below is exactly that removal). Not placed -> nothing to
				// remove at all (such a node cannot project; defensive skip).
				if (Counts.bPlacementExists)
				{
					ConfirmedMoveNames.Add(MoveNode->MoveName);
				}
				continue;
			}

			FText Message;
			if (MoveNode->bIsStub)
			{
				// Stub-distinct copy (AE7): incoming rows are ALL a stub can own (no entry -> no
				// outgoing), and there is no move to preserve or delete.
				Message = FText::Format(
					LOCTEXT("AnimationMapDeleteStubConfirm",
						"Remove {0} incoming transition row(s) into '{1}'? The move does not exist in this profile."),
					FText::AsNumber(Counts.IncomingRows), FText::FromString(MoveNode->MoveName));
			}
			else if (Counts.bPlacementExists)
			{
				Message = FText::Format(
					LOCTEXT("AnimationMapDeleteMoveConfirm",
						"Remove {0} transition row(s) for '{1}' ({2} outgoing, {3} incoming) and its graph placement? The move itself is not removed from the profile."),
					FText::AsNumber(Counts.TotalRows()), FText::FromString(MoveNode->MoveName),
					FText::AsNumber(Counts.OutgoingRows), FText::AsNumber(Counts.IncomingRows));
			}
			else
			{
				// Same copy minus the placement clause (the node only projects via its rows).
				Message = FText::Format(
					LOCTEXT("AnimationMapDeleteMoveConfirmUnplaced",
						"Remove {0} transition row(s) for '{1}' ({2} outgoing, {3} incoming)? The move itself is not removed from the profile."),
					FText::AsNumber(Counts.TotalRows()), FText::FromString(MoveNode->MoveName),
					FText::AsNumber(Counts.OutgoingRows), FText::AsNumber(Counts.IncomingRows));
			}
			DestructiveActions::FDestructiveActionPrompt Prompt;
			Prompt.Title = LOCTEXT("AnimationMapDeleteNodeTitle", "Delete Animation Map Node");
			Prompt.Body = Message;
			Prompt.Consequence = LOCTEXT("AnimationMapDeleteNodeConsequence", "Only graph placement and transition rows are removed. The move asset/profile entry is not deleted.");
			Prompt.AffectedItems.Add(FString::Printf(TEXT("Move: %s"), *MoveNode->MoveName));
			if (Counts.OutgoingRows > 0)
			{
				Prompt.AffectedItems.Add(FString::Printf(TEXT("Outgoing rows: %d"), Counts.OutgoingRows));
			}
			if (Counts.IncomingRows > 0)
			{
				Prompt.AffectedItems.Add(FString::Printf(TEXT("Incoming rows: %d"), Counts.IncomingRows));
			}
			if (Counts.bPlacementExists)
			{
				Prompt.AffectedItems.Add(FString(TEXT("Graph placement")));
			}
			if (MoveNode->bIsStub)
			{
				Prompt.AffectedItems.Add(FString(TEXT("Stub node")));
			}
			if (DestructiveActions::Confirm(Prompt))
			{
				ConfirmedMoveNames.Add(MoveNode->MoveName);
			}
		}

		if (LocalEdgeNodes.Num() > 0
			|| ConfirmedMoveNames.Num() > 0)
		{
			// ---- Phase 2: ONE panel transaction for the whole batch (AE1: one undo step). ----
			BeginTransaction(LOCTEXT("AnimationMapDeleteSelection", "Delete Animation Map Selection"));
			bool bBatchMutated = false;

			// Local edges first, ordered DESCENDING by RowIndex per from-move (index stability: removals
			// never shift the indices still to visit). Each removal is snapshot-validated through the
			// (index, snapshot) handle, so deleting one of two IDENTICAL duplicate rows removes exactly
			// the addressed one (AE6); a stale handle (external edit in the deferral window) skips just
			// that edge and the reconcile below trues the projection up. NOTE: in a mixed selection the
			// edge removals run before the move pruning, so a confirmed move's dialog count can exceed
			// its remaining rows by the already-removed selected edges — and with TWO confirmed moves,
			// the first move's outgoing wipe can remove rows the second move's dialog counted as
			// incoming. Same end state either way; the counts are pre-removal by design (all dialogs
			// front-run the transaction).
			LocalEdgeNodes.Sort([](const UPaper2DPlusAnimationMapNode_Transition& A, const UPaper2DPlusAnimationMapNode_Transition& B)
			{
				const int32 FromCompare = A.FromMove.Compare(B.FromMove, ESearchCase::IgnoreCase);
				if (FromCompare != 0)
				{
					return FromCompare < 0;
				}
				return A.RowIndex > B.RowIndex; // DESCENDING per from-move
			});
			for (const UPaper2DPlusAnimationMapNode_Transition* EdgeNode :
				LocalEdgeNodes)
			{
				bBatchMutated |=
					Paper2DPlusAnimationMap::RemoveTransitionRowChecked(
						Asset,
						EdgeNode->FromMove,
						EdgeNode->RowIndex,
						EdgeNode->MakeRowSnapshot());
			}

			// Confirmed moves: outgoing + incoming rows + placement entry (the move STAYS, AE2).
			for (const FString& MoveName : ConfirmedMoveNames)
			{
				const Paper2DPlusAnimationMap::FRemoveMoveResult Result =
					Paper2DPlusAnimationMap::RemoveAllRowsForMove(
						Asset,
						MoveName);
				bBatchMutated |= Result.OutgoingRemoved > 0
					|| Result.IncomingRemoved > 0
					|| Result.bPositionEntryRemoved;
			}

			EndTransaction(); // pend-flush suppressed by the guard; ReconcileNow below absorbs pends

			if (bBatchMutated)
			{
				// Safe to rebuild HERE (unlike the wire-create hook): command context — no engine pin
				// machinery in flight. ReconcileNow rebuilds the graph AND updates LastProjection, so
				// the deferred Modify echo early-outs.
				ReconcileNow();
				bAnyMutation = true;
			}
			else
			{
				RequestReconcile();
			}
		}
	}

	// Pend-flush for the all-declined path (the dialog pumped Slate under the guard; a pended signal
	// would otherwise strand with no armed timer).
	if (bReconcilePending)
	{
		RequestReconcile();
	}

	// Row mutations are list-visible: the explicit broadcast is the U1 contract's graph->list direction
	// (AE8). Our own handler re-enters unguarded -> RequestReconcile -> diff EMPTY (ReconcileNow just
	// refreshed LastProjection) -> no second rebuild.
	if (bAnyMutation && Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
}

// =====================================================================================================
// U5 DRAG-DROP PLACEMENT (R2, F1) — the panel IS the thin outer drop target (see the header's SWidget
// block: 5.7 SGraphPanel returns Unhandled for unknown ops when IsEditable=true, so the event bubbles).
// =====================================================================================================

FReply SAnimationMapPanel::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// Handled for either supported flipbook-card operation; source-asset
	// identity is judged at drop time. The Overview browser mints FFlipbookGroupDragDropOp; the
	// always-visible FlipbookList sidebar (the natural source while THIS tab is foreground — the
	// browser lives inside the hidden Overview tab) mints FQueueDragDropOp. Queue-REORDER ops
	// (IsFromQueue — never SourceAsset-stamped) are DECLINED here, not at drop time: claiming them
	// would show a droppable cursor for a drop that fails closed; bubbling is safe (no queue accept
	// site sits above the graph panel).
	if (DragDropEvent.GetOperationAs<FFlipbookGroupDragDropOp>().IsValid())
	{
		return FReply::Handled();
	}
	if (TSharedPtr<FQueueDragDropOp> QueueOp = DragDropEvent.GetOperationAs<FQueueDragDropOp>())
	{
		if (!QueueOp->IsFromQueue())
		{
			return FReply::Handled();
		}
	}
	return SCompoundWidget::OnDragOver(MyGeometry, DragDropEvent);
}

FReply SAnimationMapPanel::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// Extract (indices, source asset) from whichever flipbook-card op this is — browser cards
	// (FFlipbookGroupDragDropOp) or FlipbookList/SpriteEditor sidebar rows (FQueueDragDropOp).
	TArray<int32> DroppedIndices;
	UPaper2DPlusCharacterProfileAsset* SourceAsset = nullptr;
	if (TSharedPtr<FFlipbookGroupDragDropOp> GroupOp = DragDropEvent.GetOperationAs<FFlipbookGroupDragDropOp>())
	{
		DroppedIndices = GroupOp->FlipbookIndices;
		SourceAsset = GroupOp->SourceAsset.Get();
	}
	else if (TSharedPtr<FQueueDragDropOp> QueueOp = DragDropEvent.GetOperationAs<FQueueDragDropOp>())
	{
		if (QueueOp->IsFromQueue())
		{
			// Queue-REORDER op: declined in OnDragOver; bubble the drop too (mirrors the no-claim).
			return SCompoundWidget::OnDrop(MyGeometry, DragDropEvent);
		}
		DroppedIndices = QueueOp->FlipbookIndices;
		SourceAsset = QueueOp->SourceAsset.Get();
	}
	else
	{
		return SCompoundWidget::OnDrop(MyGeometry, DragDropEvent);
	}

	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset || !GraphObj || !GraphEditorWidget.IsValid() || bGraphWriteInProgress)
	{
		return FReply::Handled(); // consumed: a half-valid drop must not bubble to an ancestor accept site
	}

	// FOREIGN-ASSET rejection (asset identity, not model identity — keeps the deferred layer-editor
	// hosting compatible, plan A4): the op's bare indices index a DIFFERENT Flipbooks array. CHOICE
	// (documented): return HANDLED with NO action — letting it bubble as Unhandled could land the stale
	// indices on an unrelated ancestor accept site, the exact cross-editor hazard the SourceAsset stamp
	// exists to stop. A null SourceAsset (untracked creation site, dead asset, or a queue-reorder op,
	// which is never stamped) fails CLOSED here too.
	if (SourceAsset != Asset)
	{
		return FReply::Handled();
	}

	SGraphPanel* GraphPanel = GraphEditorWidget->GetGraphPanel();
	if (!GraphPanel)
	{
		return FReply::Handled();
	}

	// Drop position in graph space — the SGraphPanel::OnDrop recipe (SGraphPanel.cpp:1450): the
	// PANEL's geometry (not ours — the border could pad) -> PanelCoordToGraphCoord.
	const FVector2D PanelLocal = GraphPanel->GetTickSpaceGeometry().AbsoluteToLocal(DragDropEvent.GetScreenSpacePosition());
	const FVector2D DropGraphPosition = GraphPanel->PanelCoordToGraphCoord(PanelLocal);

	TMap<FString, UPaper2DPlusAnimationMapNode_Move*> LiveMovesByLower;
	TArray<UPaper2DPlusAnimationMapNode_Transition*> LiveEdges;
	CollectLiveNodes(LiveMovesByLower, LiveEdges);

	UEdGraphNode* NodeToFocus = nullptr;
	TArray<FString> LowerKeysToPlace;
	TArray<int32> FlipbookIndicesToPlace;
	bool bAnyAnimationMapAssigned = false;
	{
		TGuardValue<bool> WriteGuard(bGraphWriteInProgress, true);

		// ALL indices, IsValidIndex-guarded (multi-select): create-or-focus per move.
		TSet<FString> QueuedLower;
		for (const int32 FlipbookIndex : DroppedIndices)
		{
			if (!Asset->Flipbooks.IsValidIndex(FlipbookIndex))
			{
				continue;
			}
			const FString& MoveName = Asset->Flipbooks[FlipbookIndex].Identity.FlipbookName;
			if (MoveName.TrimStartAndEnd().IsEmpty())
			{
				continue; // degenerate names never project — never mint a junk placement key
			}
			const FString MoveLower = MoveName.ToLower();
			if (UPaper2DPlusAnimationMapNode_Move* ExistingNode = LiveMovesByLower.FindRef(MoveLower))
			{
				// Already on the graph: focus, NO transaction, NO duplicate (create-or-focus, R2).
				if (!NodeToFocus)
				{
					NodeToFocus = ExistingNode;
				}
				continue;
			}
			bool bAlreadyQueued = false;
			QueuedLower.Add(MoveLower, &bAlreadyQueued);
			if (!bAlreadyQueued)
			{
				LowerKeysToPlace.Add(MoveLower);
				FlipbookIndicesToPlace.Add(FlipbookIndex);
			}
		}

		if (LowerKeysToPlace.Num() > 0)
		{
			const bool bAssignToAnimationMapGroup =
				bAnimationMapGroupGraphOpen
				&& !bAnimationMapGroupGraphIsUnassigned
				&& ActiveAnimationMapGroupTag.IsValid();

			// ONE panel transaction per drop batch; entries fanned (DropFanOffset) so a multi-drop
			// never stacks exactly. The placement set IS the position store — the nodes materialize
			// via projection, not by spawning here.
			BeginTransaction(bAssignToAnimationMapGroup
				? LOCTEXT("AnimationMapGraphPlaceMoves", "Place Animation Map Move(s)")
				: LOCTEXT("AnimationMapPlaceMoves", "Place Animation Map Move(s)"));
			if (bAssignToAnimationMapGroup)
			{
				for (const int32 FlipbookIndex : FlipbookIndicesToPlace)
				{
					const FString& FlipbookName = Asset->Flipbooks[FlipbookIndex].Identity.FlipbookName;
					UObject* PaperZDSequence = Asset->Flipbooks[FlipbookIndex].Identity.PaperZDSequence.Get();
					bAnyAnimationMapAssigned |= Asset->AssignFlipbookToTagMapping(
						ActiveAnimationMapGroupTag, FlipbookName, PaperZDSequence);
				}
			}
			for (int32 PlaceIndex = 0; PlaceIndex < LowerKeysToPlace.Num(); ++PlaceIndex)
			{
				Asset->AnimationMapNodePositions.Add(LowerKeysToPlace[PlaceIndex],
					DropGraphPosition + Paper2DPlusAnimationMap::DropFanOffset(PlaceIndex));
			}
			EndTransaction(); // pend-flush suppressed by the guard; ReconcileNow below absorbs pends

			// Safe to rebuild here — drop context, the drag already concluded, no engine pin machinery
			// in flight. Updates LastProjection so the deferred Modify echo early-outs. No
			// NotifyAssetDataChanged (documented): placements are graph-local editor state no list
			// surface renders.
			ReconcileNow();
		}
	}

	if (bReconcilePending)
	{
		RequestReconcile(); // pend-flush insurance (mirrors the other gesture helpers)
	}

	// Focus only when nothing new was placed — otherwise the jump would yank the viewport away from
	// the just-dropped nodes.
	if (NodeToFocus && LowerKeysToPlace.Num() == 0)
	{
		GraphEditorWidget->JumpToNode(NodeToFocus, /*bRequestRename=*/false, /*bSelectNode=*/true);
	}

	if (bAnyAnimationMapAssigned)
	{
		QueueAnimationMapRefresh();
		if (Model.IsValid())
		{
			Model->NotifyAssetDataChanged();
		}
	}

	return FReply::Handled();
}

// =====================================================================================================
// ANIMATION MAP MODE — tag-backed grouping surface hosted inside the Animation Map tab
// =====================================================================================================

void SAnimationMapPanel::QueueAnimationMapRefresh()
{
	if (bAnimationMapRefreshQueued)
	{
		return;
	}
	bAnimationMapRefreshQueued = true;
	RegisterActiveTimer(0.0f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SAnimationMapPanel::OnAnimationMapRefreshTimer));
}

EActiveTimerReturnType SAnimationMapPanel::OnAnimationMapRefreshTimer(double /*InCurrentTime*/, float /*InDeltaTime*/)
{
	bAnimationMapRefreshQueued = false;
	RefreshAnimationMap();
	return EActiveTimerReturnType::Stop;
}

void SAnimationMapPanel::RefreshAnimationMap()
{
	if (!AnimationMapGroupsBox.IsValid())
	{
		return;
	}

	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset)
	{
		bAnimationMapGroupGraphOpen = false;
		bAnimationMapGroupGraphIsUnassigned = false;
		ActiveAnimationMapGroupTag = FGameplayTag();
		if (GraphObj)
		{
			GraphObj->bHasExactGroupScope = false;
		}
		if (Model.IsValid())
		{
			Model->ClearSelectedTransition();
		}
		if (AnimationMapGroupsBox->NumSlots() > 0)
		{
			AnimationMapGroupsBox->ClearChildren();
		}
		LastAnimationMapBoardSignature.Reset();
		RefreshMainViewSwitcher();
		return;
	}

	const Paper2DPlusAnimationMap::FAnimationMapProjection Projection =
		Paper2DPlusAnimationMap::ProjectAnimationMap(Asset);

	if (bAnimationMapGroupGraphOpen
		&& !bAnimationMapGroupGraphIsUnassigned
		&& ActiveAnimationMapGroupTag.IsValid())
	{
		bool bFoundActiveGroup = false;
		for (const Paper2DPlusAnimationMap::FAnimationMapGroup& Group : Projection.Groups)
		{
			if (Group.Tag == ActiveAnimationMapGroupTag)
			{
				bFoundActiveGroup = true;
				break;
			}
		}
		if (!bFoundActiveGroup)
		{
			bAnimationMapGroupGraphOpen = false;
			bAnimationMapGroupGraphIsUnassigned = false;
			ActiveAnimationMapGroupTag = FGameplayTag();
			if (GraphObj)
			{
				GraphObj->bHasExactGroupScope = false;
			}
			if (Model.IsValid())
			{
				Model->ClearSelectedTransition();
			}
			RefreshMainViewSwitcher();
		}
	}
	if (bAnimationMapGroupGraphOpen)
	{
		// The group board is hidden behind the switcher. Do not clear/recreate any off-screen tiles;
		// CloseAnimationMapGroup will compare and refresh the latest visible projection.
		return;
	}

	const FString BoardSignature = AnimationMapPanel_BoardSignature(Projection, MapFilterTag);
	if (AnimationMapGroupsBox->NumSlots() > 0 && BoardSignature == LastAnimationMapBoardSignature)
	{
		return;
	}
	AnimationMapGroupsBox->ClearChildren();
	LastAnimationMapBoardSignature = BoardSignature;

	int32 MappedCount = 0;
	for (const Paper2DPlusAnimationMap::FAnimationMapGroup& Group : Projection.Groups)
	{
		MappedCount += Group.Entries.Num();
	}

	AnimationMapGroupsBox->AddSlot()
	.AutoHeight()
	.Padding(0, 0, 0, 8)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AnimationMapTitle", "Animation Map"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(8, 0, 0, 0)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("AnimationMapCountFmt", "{0} mapped, {1} unassigned"),
				FText::AsNumber(MappedCount), FText::AsNumber(Projection.Unmapped.Num())))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNullWidget::NullWidget
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("AnimationMapMigrateOverviewButton", "Migrate Overview Groups"))
			.ToolTipText(LOCTEXT("AnimationMapMigrateOverviewTip", "Review existing Overview groups and move their flipbooks into Animation Map tag groups."))
			.OnClicked(this, &SAnimationMapPanel::OpenAnimationMapMigrationDialog, false)
		]
	];

	TSharedRef<SWrapBox> GroupTiles = SNew(SWrapBox)
		.UseAllottedSize(true);
	for (const Paper2DPlusAnimationMap::FAnimationMapGroup& Group : Projection.Groups)
	{
		GroupTiles->AddSlot()
		.Padding(0, 0, 8, 8)
		[
			BuildAnimationMapGroupTile(Group)
		];
	}

	GroupTiles->AddSlot()
	.Padding(0, 0, 8, 8)
	[
		BuildAnimationMapUnassignedTile(Projection.Unmapped.Num())
	];

	AnimationMapGroupsBox->AddSlot()
	.AutoHeight()
	.Padding(0)
	[
		GroupTiles
	];
}

void SAnimationMapPanel::OpenAnimationMapFullGraph()
{
	bAnimationMapGroupGraphOpen = true;
	bAnimationMapGroupGraphIsUnassigned = false;
	ActiveAnimationMapGroupTag = FGameplayTag();
	if (GraphObj)
	{
		GraphObj->bHasExactGroupScope = false;
	}
	bPendingFitToView = true;
	RefreshMainViewSwitcher();
	QueueAnimationMapRefresh();
	RequestReconcile();
}

void SAnimationMapPanel::OpenAnimationMapGroup(FGameplayTag GroupTag)
{
	if (!GroupTag.IsValid())
	{
		return;
	}
	bAnimationMapGroupGraphOpen = true;
	bAnimationMapGroupGraphIsUnassigned = false;
	ActiveAnimationMapGroupTag = GroupTag;
	if (GraphObj)
	{
		// Chain-start rework: the schema's "Add Chain Start" empty-canvas action keys off this (the
		// schema runs on the CDO and cannot see the panel's scope state).
		GraphObj->bHasExactGroupScope = true;
	}
	bPendingFitToView = true; // audit F14: frame the scoped nodes once after the next reconcile populates them
	RefreshMainViewSwitcher();
	QueueAnimationMapRefresh();
	RequestReconcile();
}

void SAnimationMapPanel::OpenAnimationMapUnassigned()
{
	bAnimationMapGroupGraphOpen = true;
	bAnimationMapGroupGraphIsUnassigned = true;
	ActiveAnimationMapGroupTag = FGameplayTag();
	if (GraphObj)
	{
		GraphObj->bHasExactGroupScope = false; // unassigned bucket: no exact group, no chain-start authoring
	}
	bPendingFitToView = true; // audit F14: frame the scoped nodes once after the next reconcile populates them
	RefreshMainViewSwitcher();
	QueueAnimationMapRefresh();
	RequestReconcile();
}

void SAnimationMapPanel::CloseAnimationMapGroup()
{
	bAnimationMapGroupGraphOpen = false;
	bAnimationMapGroupGraphIsUnassigned = false;
	ActiveAnimationMapGroupTag = FGameplayTag();
	if (GraphObj)
	{
		GraphObj->bHasExactGroupScope = false; // board: no scoped graph at all
	}
	// Back to the group board = the scoped graph (and any selected arrow) is gone — drop the model's
	// edge key so the shared pane leaves edge mode (TASK-108 U3; no-op when already clear).
	if (Model.IsValid())
	{
		Model->ClearSelectedTransition();
	}
	RefreshMainViewSwitcher();
	QueueAnimationMapRefresh();
}

void SAnimationMapPanel::HandleTagColorsChanged()
{
	// ONE subscription for every Tag-Colors-driven surface this panel owns:
	//  * the group board rebuilds its tiles (BuildAnimationMapGroupTile → ResolveTagColor), and
	//  * the canvas rail's active-filter chip repaints IN PLACE — its border/text colors are _Lambda
	//    bound (see RebuildMapFilterPicker), so a paint invalidate is the whole update. Without this the
	//    chip alone kept a stale color while tiles, phase badges and node chips all repainted.
	RefreshAnimationMap();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAnimationMapPanel::OpenAnimationMapGroupColorPicker(FGameplayTag InTag, FLinearColor InCurrentColor)
{
	if (!InTag.IsValid())
	{
		return;
	}

	FColorPickerArgs PickerArgs;
	PickerArgs.bIsModal = true;
	PickerArgs.ParentWidget = SharedThis(this);
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2)
	PickerArgs.InitialColor = InCurrentColor;
#endif
	// Commit straight to the project-wide registry: SetTagColor persists to DefaultGame.ini and
	// broadcasts OnTagColorsChanged → this panel's RefreshAnimationMap repaints the board.
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda([InTag](FLinearColor NewColor)
	{
		UPaper2DPlusSettings::SetTagColor(InTag, NewColor);
	});
	OpenColorPicker(PickerArgs);
}

TSharedRef<SWidget> SAnimationMapPanel::BuildAnimationMapGroupTile(const Paper2DPlusAnimationMap::FAnimationMapGroup& Group)
{
	const FGameplayTag CapturedTag = Group.Tag;
	const FString ShortTag = AnimationMapPanel_ShortAnimationTagName(Group.Tag);
	// Shared leaf helper; an invalid tag keeps the ShortTag "(no tag)" placeholder (pre-consolidation behavior).
	const FString TileTitle = Group.Tag.IsValid() ? Paper2DPlusAnimationTagChips::GetTagLeafString(Group.Tag) : ShortTag;
	const FString TileSubtitle = AnimationMapPanel_TileSubtitle(Group.Tag, ShortTag);
	const int32 FlipbookCount = Group.Entries.Num();
	int32 RootCount = 0;
	for (const Paper2DPlusAnimationMap::FAnimationMapEntry& Entry : Group.Entries)
	{
		RootCount += Entry.bIsChainStart ? 1 : 0; // chain-start rework: the flag replaced RootNumber
	}
	// Tag Colors override: a project-wide registered color for this group's tag (or an ancestor) wins
	// over the legacy hash palette, so designers control the board colors. Unset → the stable hash hue.
	bool bTagColorFound = false;
	const FLinearColor RegistryAccent = UPaper2DPlusSettings::ResolveTagColor(Group.Tag, bTagColorFound);
	const FLinearColor Accent = bTagColorFound
		? RegistryAccent
		: AnimationMapPanel_TileAccent(Group.Tag.ToString(), /*bUnassigned=*/false);
	const FLinearColor EffectiveAccent = FlipbookCount > 0
		? Accent
		: FLinearColor(Accent.R * 0.56f, Accent.G * 0.56f, Accent.B * 0.56f, 1.0f);
	const FLinearColor BadgeTint = FlipbookCount > 0
		? FLinearColor(Accent.R * 0.28f, Accent.G * 0.28f, Accent.B * 0.28f, 0.95f)
		: FLinearColor(0.12f, 0.12f, 0.12f, 0.95f);

	// U6 (R8): the board honors the Map tag filter — a tile DIMS (stays clickable, never hidden) when
	// its group KEY doesn't match the filter hierarchically (key == filter or a descendant of it).
	// Static value: the board rebuilds through RefreshAnimationMap on every filter change.
	const bool bTileDimmedByFilter = MapFilterTag.IsValid() && !Group.Tag.MatchesTag(MapFilterTag);

	return SNew(SBox)
		.WidthOverride(236)
		.HeightOverride(108)
		[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
		.Padding(0)
		.ColorAndOpacity(bTileDimmedByFilter ? FLinearColor(1.0f, 1.0f, 1.0f, 0.30f) : FLinearColor::White)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(0)
			.ToolTipText(FText::FromString(Group.Tag.ToString()))
			.OnClicked_Lambda([this, CapturedTag]()
			{
				OpenAnimationMapGroup(CapturedTag);
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.Padding(1)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.070f, 0.070f, 0.070f, 1.0f))
					.Padding(0)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
							.BorderBackgroundColor(EffectiveAccent)
							.Padding(0)
							[
								SNew(SBox)
								.WidthOverride(4)
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(10, 9, 10, 9)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SHorizontalBox)

								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(FText::FromString(TileTitle))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
									.ColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.88f, 0.88f)))
								]

								// Tag color swatch — click to set this tag's project-wide color. The inner
								// SButton consumes the click so the tile's open-group button doesn't also fire.
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(6, 0, 0, 0)
								[
									SNew(SButton)
									.ButtonStyle(FAppStyle::Get(), "SimpleButton")
									.ContentPadding(0)
									.ToolTipText(LOCTEXT("AnimationMapTileSetColorTip", "Set this tag's color (project-wide Tag Colors)"))
									.OnClicked_Lambda([this, CapturedTag, Accent]()
									{
										OpenAnimationMapGroupColorPicker(CapturedTag, Accent);
										return FReply::Handled();
									})
									[
										SNew(SBorder)
										.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
										.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f))
										.Padding(1)
										[
											SNew(SBox)
											.WidthOverride(12)
											.HeightOverride(12)
											[
												SNew(SColorBlock)
												.Color(Accent)
											]
										]
									]
								]

								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(6, 0, 0, 0)
								[
									SNew(SBorder)
									.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
									.BorderBackgroundColor(BadgeTint)
									.Padding(FMargin(6, 2))
									[
										SNew(STextBlock)
										.Text(FText::AsNumber(FlipbookCount))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
										.ColorAndOpacity(FSlateColor(FlipbookCount > 0
											? FLinearColor(0.90f, 0.90f, 0.90f)
											: FLinearColor(0.55f, 0.55f, 0.55f)))
									]
								]
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0, 3, 0, 0)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TileSubtitle))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.50f, 0.50f, 0.50f)))
							]

							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							[
								SNullWidget::NullWidget
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SHorizontalBox)

								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(FlipbookCount > 0
										? AnimationMapPanel_GroupCountText(FlipbookCount, RootCount)
										: LOCTEXT("AnimationMapTileEmptyGroup", "Empty group"))
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
									.ColorAndOpacity(FSlateColor(FLinearColor(0.60f, 0.60f, 0.60f)))
								]

								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("AnimationMapOpenGroupGraphTile", "Open graph"))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
									.ColorAndOpacity(FSlateColor(EffectiveAccent))
								]
							]
						]
					]
				]
				]
			] // U6 (R8): closes the filter-dim SBorder wrapper
		];
}

TSharedRef<SWidget> SAnimationMapPanel::BuildAnimationMapUnassignedTile(int32 UnassignedCount)
{
	const FLinearColor Accent = AnimationMapPanel_TileAccent(TEXT("Unassigned"), /*bUnassigned=*/true);
	const FLinearColor BadgeTint = UnassignedCount > 0
		? FLinearColor(Accent.R * 0.28f, Accent.G * 0.28f, Accent.B * 0.28f, 0.95f)
		: FLinearColor(0.12f, 0.12f, 0.12f, 0.95f);

	// U6 (R8): the unassigned bucket has no group KEY, so any active filter dims it (tiles match by
	// key; its members may still match individually inside the graph). Stays clickable, never hidden.
	const bool bTileDimmedByFilter = MapFilterTag.IsValid();

	return SNew(SBox)
		.WidthOverride(236)
		.HeightOverride(108)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
			.Padding(0)
			.ColorAndOpacity(bTileDimmedByFilter ? FLinearColor(1.0f, 1.0f, 1.0f, 0.30f) : FLinearColor::White)
			[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(0)
			.OnClicked_Lambda([this]()
			{
				OpenAnimationMapUnassigned();
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(1)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.075f, 0.066f, 0.050f, 1.0f))
					.Padding(0)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
							.BorderBackgroundColor(UnassignedCount > 0 ? Accent : FLinearColor(0.32f, 0.26f, 0.18f, 1.0f))
							.Padding(0)
							[
								SNew(SBox)
								.WidthOverride(4)
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(10, 9, 10, 9)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SHorizontalBox)

								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("AnimationMapUnassigned", "Unassigned"))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
									.ColorAndOpacity(FSlateColor(FLinearColor(0.90f, 0.86f, 0.78f)))
								]

								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(6, 0, 0, 0)
								[
									SNew(SBorder)
									.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
									.BorderBackgroundColor(BadgeTint)
									.Padding(FMargin(6, 2))
									[
										SNew(STextBlock)
										.Text(FText::AsNumber(UnassignedCount))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
										.ColorAndOpacity(FSlateColor(UnassignedCount > 0
											? FLinearColor(0.94f, 0.88f, 0.72f)
											: FLinearColor(0.55f, 0.55f, 0.55f)))
									]
								]
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0, 3, 0, 0)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("AnimationMapUnassignedSubtitle", "Flipbooks without an animation tag"))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.56f, 0.52f, 0.44f)))
							]

							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							[
								SNullWidget::NullWidget
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SHorizontalBox)

								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(UnassignedCount > 0
										? AnimationMapPanel_CountText(UnassignedCount)
										: LOCTEXT("AnimationMapUnassignedEmpty", "Nothing pending"))
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
									.ColorAndOpacity(FSlateColor(FLinearColor(0.63f, 0.59f, 0.50f)))
								]

								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("AnimationMapOpenUnassignedGraphTile", "Open graph"))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
									.ColorAndOpacity(FSlateColor(Accent))
								]
							]
						]
					]
				]
			]
			] // U6 (R8): closes the filter-dim SBorder wrapper
		];
}

void SAnimationMapPanel::QueueAnimationMapMigrationPrompt()
{
	// Automatic modal prompts must never be armed for unattended editor sessions. AddModalWindow() runs a
	// nested modal loop, so an automation/tour ticker cannot dismiss it and its watchdog sees a frozen game
	// thread. The explicit "Migrate Overview Groups" button remains available in interactive sessions.
	if (FApp::IsUnattended())
	{
		return;
	}

	if (bAnimationMapMigrationPromptQueued)
	{
		return;
	}

	bAnimationMapMigrationPromptQueued = true;
	RegisterActiveTimer(0.1f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SAnimationMapPanel::OnAnimationMapMigrationPromptTimer));
}

EActiveTimerReturnType SAnimationMapPanel::OnAnimationMapMigrationPromptTimer(double /*InCurrentTime*/, float /*InDeltaTime*/)
{
	bAnimationMapMigrationPromptQueued = false;
	// Re-check at the side-effect boundary as well as the queue site. This keeps direct/test invocation
	// and any future queue path from entering Slate's blocking modal loop in an unattended process.
	if (!FApp::IsUnattended() && ShouldShowAnimationMapMigrationPrompt())
	{
		OpenAnimationMapMigrationDialog(/*bMarkPromptSeenOnApply=*/true);
	}
	return EActiveTimerReturnType::Stop;
}

bool SAnimationMapPanel::ShouldShowAnimationMapMigrationPrompt() const
{
	const UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset || !AnimationMapPanel_HasNonTagOverviewGroups(Asset))
	{
		return false;
	}

	bool bSeen = false;
	if (GConfig)
	{
		GConfig->GetBool(TEXT("Paper2DPlus.AnimationMapMigration"),
			*GetAnimationMapMigrationPromptConfigKey(), bSeen, GEditorPerProjectIni);
	}
	return !bSeen;
}

void SAnimationMapPanel::MarkAnimationMapMigrationPromptSeen() const
{
	if (!GConfig)
	{
		return;
	}

	GConfig->SetBool(TEXT("Paper2DPlus.AnimationMapMigration"),
		*GetAnimationMapMigrationPromptConfigKey(), true, GEditorPerProjectIni);
	// No explicit Flush (legacy-cleanup 2026-07): GConfig holds the value; the engine flushes
	// GEditorPerProjectIni itself. A manual Flush rewrites the whole multi-hundred-KB ini via
	// tmp+MoveFile and stalled ~8s per call under file-lock contention - the slow-close bug.
}

FString SAnimationMapPanel::GetAnimationMapMigrationPromptConfigKey() const
{
	const UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	FString AssetKey = Asset ? Asset->GetPathName() : TEXT("NoAsset");
	AssetKey.ReplaceInline(TEXT("/"), TEXT("_"));
	AssetKey.ReplaceInline(TEXT("."), TEXT("_"));
	AssetKey.ReplaceInline(TEXT(":"), TEXT("_"));
	AssetKey.ReplaceInline(TEXT(" "), TEXT("_"));
	return FString::Printf(TEXT("OverviewToAnimationMap_v1_%s"), *AssetKey);
}

FReply SAnimationMapPanel::OpenAnimationMapMigrationDialog(bool bMarkPromptSeenOnApply)
{
	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset)
	{
		return FReply::Handled();
	}

	TSharedPtr<TArray<TSharedPtr<FString>>> TagOptions = MakeShared<TArray<TSharedPtr<FString>>>(
		AnimationMapPanel_BuildMigrationTagOptions(Asset));
	TSharedPtr<TArray<TSharedPtr<FAnimationMapMigrationRow>>> Rows = MakeShared<TArray<TSharedPtr<FAnimationMapMigrationRow>>>();
	AnimationMapPanel_BuildMigrationRows(Asset, *TagOptions, *Rows);

	if (Rows->Num() == 0)
	{
		if (bMarkPromptSeenOnApply)
		{
			MarkAnimationMapMigrationPromptSeen();
		}
		FNotificationInfo Info(LOCTEXT("AnimationMapNoOverviewGroupsToMigrate", "No Overview groups need migration."));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return FReply::Handled();
	}

	TSharedRef<SVerticalBox> RowsBox = SNew(SVerticalBox);
	for (const TSharedPtr<FAnimationMapMigrationRow>& Row : *Rows)
	{
		if (!Row.IsValid())
		{
			continue;
		}

		RowsBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 6)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(8)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromName(Row->SourceGroup))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 2, 0, 0)
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("AnimationMapMigrationGroupCountFmt", "{0} direct flipbooks"),
							FText::AsNumber(Row->FlipbookCount)))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(230)
					[
						SNew(SComboBox<TSharedPtr<FString>>)
						.OptionsSource(TagOptions.Get())
						.InitiallySelectedItem(Row->SelectedTagOption)
						.OnSelectionChanged_Lambda([Row](TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
						{
							if (SelectInfo != ESelectInfo::OnNavigation && NewValue.IsValid())
							{
								Row->SelectedTagOption = NewValue;
							}
						})
						.OnGenerateWidget_Lambda([TagOptions](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
						{
							FText Label = LOCTEXT("AnimationMapMigrationSkipOption", "Skip");
							if (Item.IsValid() && !AnimationMapPanel_IsSkipMigrationOption(*Item))
							{
								const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(**Item), /*ErrorIfNotFound=*/false);
								Label = FText::FromString(AnimationMapPanel_ShortAnimationTagName(Tag));
							}
							return SNew(STextBlock)
								.Text(Label)
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9));
						})
						.Content()
						[
							SNew(STextBlock)
							.Text_Lambda([Row]()
							{
								if (!Row.IsValid() || !Row->SelectedTagOption.IsValid()
									|| AnimationMapPanel_IsSkipMigrationOption(*Row->SelectedTagOption))
								{
									return LOCTEXT("AnimationMapMigrationSkipContent", "Skip");
								}
								const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
									FName(**Row->SelectedTagOption), /*ErrorIfNotFound=*/false);
								return FText::FromString(AnimationMapPanel_ShortAnimationTagName(Tag));
							})
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						]
					]
				]
			]
		];
	}

	TSharedRef<SWindow> Dialog = SNew(SWindow)
		.Title(LOCTEXT("AnimationMapMigrationTitle", "Migrate Overview Groups"))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(620.0f, 520.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);
	TWeakPtr<SWindow> WeakDialog = Dialog;

	Dialog->SetContent(
		SNew(SBorder)
		.Padding(12)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AnimationMapMigrationIntro",
					"Review existing Overview groups and choose the Animation Map tag group each should move into. Skipped groups are left unchanged."))
				.AutoWrapText(true)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					RowsBox
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 10, 0, 0)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNullWidget::NullWidget
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 6, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("AnimationMapMigrationApply", "Apply"))
					.OnClicked_Lambda([this, Rows, bMarkPromptSeenOnApply, WeakDialog]()
					{
						UPaper2DPlusCharacterProfileAsset* LiveAsset = GetAsset();
						if (!LiveAsset)
						{
							return FReply::Handled();
						}

						bool bHasActionableRow = false;
						for (const TSharedPtr<FAnimationMapMigrationRow>& Row : *Rows)
						{
							if (Row.IsValid() && Row->SelectedTagOption.IsValid()
								&& !AnimationMapPanel_IsSkipMigrationOption(*Row->SelectedTagOption))
							{
								const FGameplayTag TargetTag = FGameplayTag::RequestGameplayTag(
									FName(**Row->SelectedTagOption), /*ErrorIfNotFound=*/false);
								if (TargetTag.IsValid()
									&& LiveAsset->GetFlipbookIndicesForFlipbookGroup(Row->SourceGroup).Num() > 0)
								{
									bHasActionableRow = true;
									break;
								}
							}
						}

						bool bAnyApplied = false;
						if (bHasActionableRow)
						{
							BeginTransaction(LOCTEXT("AnimationMapMigrationTransaction", "Migrate Overview Groups to Animation Map"));
							for (const TSharedPtr<FAnimationMapMigrationRow>& Row : *Rows)
							{
								if (!Row.IsValid() || !Row->SelectedTagOption.IsValid()
									|| AnimationMapPanel_IsSkipMigrationOption(*Row->SelectedTagOption))
								{
									continue;
								}

								const FGameplayTag TargetTag = FGameplayTag::RequestGameplayTag(
									FName(**Row->SelectedTagOption), /*ErrorIfNotFound=*/false);
								if (!TargetTag.IsValid())
								{
									continue;
								}

								const FName TargetGroupName = TargetTag.GetTagName();
								const FName SourceGroupName = Row->SourceGroup;
								const bool bRemoveSourceGroupWhenEmpty = SourceGroupName != TargetGroupName;
								const TArray<int32> FlipbookIndices = LiveAsset->GetFlipbookIndicesForFlipbookGroup(SourceGroupName);
								for (const int32 FlipbookIndex : FlipbookIndices)
								{
									if (LiveAsset->Flipbooks.IsValidIndex(FlipbookIndex))
									{
										LiveAsset->MoveFlipbookToFlipbookGroup(FlipbookIndex, TargetGroupName);
										bAnyApplied = true;
									}
								}

								if (bRemoveSourceGroupWhenEmpty
									&& Row->FlipbookCount > 0
									&& LiveAsset->HasFlipbookGroup(SourceGroupName)
									&& LiveAsset->GetFlipbookIndicesForFlipbookGroup(SourceGroupName).Num() == 0)
								{
									LiveAsset->RemoveFlipbookGroup(SourceGroupName);
								}
							}
							EndTransaction();
						}

						if (bMarkPromptSeenOnApply)
						{
							MarkAnimationMapMigrationPromptSeen();
						}
						if (bAnyApplied && Model.IsValid())
						{
							Model->NotifyAssetDataChanged();
						}
						QueueAnimationMapRefresh();
						if (TSharedPtr<SWindow> Window = WeakDialog.Pin())
						{
							Window->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 6, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("AnimationMapMigrationSkip", "Skip"))
					.Visibility(bMarkPromptSeenOnApply ? EVisibility::Visible : EVisibility::Collapsed)
					.OnClicked_Lambda([this, WeakDialog]()
					{
						MarkAnimationMapMigrationPromptSeen();
						if (TSharedPtr<SWindow> Window = WeakDialog.Pin())
						{
							Window->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("AnimationMapMigrationCancel", "Cancel"))
					.OnClicked_Lambda([WeakDialog]()
					{
						if (TSharedPtr<SWindow> Window = WeakDialog.Pin())
						{
							Window->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]
			]
		]);

	FSlateApplication::Get().AddModalWindow(Dialog, SharedThis(this));
	return FReply::Handled();
}

// =====================================================================================================
// TASK-96 P3: CROSS-VIEW SELECTION SYNC (replaces the retired U6 in-graph details strip). A single node
// selection drives the shared SProfileDetailsPanel through the model; the Map follows the model's
// OnFlipbookSelectionChanged by focusing the matching move node (gesture-guarded, paint-applied). The
// shared pane covers everything a selection needs: the target-only Move Transitions rows (transitions
// are pure From→To since TASK-108 — the strip-era Label/Condition/CancelCategory fields are gone from
// the data model itself) and the Phase tag by the flipbook details view (relocated in P3a).
// =====================================================================================================

void SAnimationMapPanel::HandleGraphSelectionChanged(const FGraphPanelSelectionSet& NewSelection)
{
	if (RefreshSelectionCohortSummary() && SelectionCohortCard.IsValid())
	{
		SelectionCohortCard->Invalidate(EInvalidateWidgetReason::Layout);
	}
	// A single selected node pushes its flipbook into the shared model so List/Card/details follow: a
	// move node selects its own flipbook; an edge selects its FromMove's flipbook (so the pane shows that
	// move's Move Transitions row for the edge) AND the model-level transition key (TASK-108 U3 — the
	// pane's edge mode). EMPTY selection leaves the model selection alone — granular reconciliation
	// may remove a genuinely deleted node, but must never deselect the flipbook OR drop a surviving edge
	// key (the pane holds the key by VALUE and re-resolves). A genuine
	// MULTI selection clears the transition key (a single-edge channel can't represent it; the clear
	// no-ops when already clear, so churn re-selecting a prior multi-selection stays quiet).
	if (bSuppressSelectionSync)
	{
		return;
	}
	if (NewSelection.Num() != 1)
	{
		if (NewSelection.Num() > 1 && Model.IsValid())
		{
			Model->ClearSelectedTransition();
		}
		return;
	}

	FString MoveName;
	FString EdgeFromMove;
	FString EdgeTargetMove;
	bool bEdgeSelected = false;
	for (UObject* SelectedObject : NewSelection)
	{
		if (UPaper2DPlusAnimationMapNode_Move* MoveNode = Cast<UPaper2DPlusAnimationMapNode_Move>(SelectedObject))
		{
			MoveName = MoveNode->MoveName;
		}
		else if (UPaper2DPlusAnimationMapNode_Transition* EdgeNode = Cast<UPaper2DPlusAnimationMapNode_Transition>(SelectedObject))
		{
			MoveName = EdgeNode->FromMove;
			EdgeFromMove = EdgeNode->FromMove;
			EdgeTargetMove = EdgeNode->TargetMove;
			bEdgeSelected = true;
		}
	}

	if (MoveName.IsEmpty() || !Model.IsValid())
	{
		return; // a stub node (no backing flipbook) resolves to no index below — same no-op outcome
	}

	{
		// Suppress the model->Map echo of our own push: the node is already selected here, so the
		// OnFlipbookSelectionChanged follow would only re-focus it redundantly.
		TGuardValue<bool> Guard(bSuppressSelectionSync, true);

		// Flipbook FIRST, then the transition key: SetSelectedFlipbook clears the transition
		// selection up front (BEFORE its same-index no-op guard — Codex P2, PR #224), so this order
		// can never wipe the key we are about to set (it only clears an OLD key; re-clicking the
		// SAME edge clears then re-sets it). A stub MOVE node resolves no index — flipbook untouched.
		const int32 FlipbookIndex = FindFlipbookIndexByName(MoveName);
		if (FlipbookIndex != INDEX_NONE)
		{
			Model->SetSelectedFlipbook(FlipbookIndex);
		}
		if (bEdgeSelected)
		{
			Model->SetSelectedTransition(EdgeFromMove, EdgeTargetMove);
		}
		else
		{
			// A single MOVE node (real or stub) is a flipbook-mode selection — drop any edge key.
			// Still needed despite SetSelectedFlipbook's own hoisted clear: a STUB move node resolves
			// no flipbook index, so SetSelectedFlipbook was never called above.
			Model->ClearSelectedTransition();
		}
	}
}

void SAnimationMapPanel::HandleModelFlipbookSelectionChanged(int32 NewIndex)
{
	// The shared model selected a flipbook elsewhere (List/Card row, the Flipbooks sidebar, a tool tab).
	// Follow it by focusing the matching move node. bSuppressSelectionSync is up only when the selection
	// ORIGINATED from our own graph click, so we skip the redundant re-focus in that case.
	if (bSuppressSelectionSync)
	{
		return;
	}
	PendingFocusFlipbookIndex = NewIndex;
	QueueFocusMove();
}

void SAnimationMapPanel::QueueFocusMove()
{
	// Defer to a paint-time active timer (like the reconcile timer): a hidden switcher slot does not
	// paint, so the focus naturally waits and applies when the Map view is next shown — AFTER any pending
	// reconcile, never mid-gesture. Latest-wins via PendingFocusFlipbookIndex.
	if (bFocusTimerQueued)
	{
		return;
	}
	bFocusTimerQueued = true;
	RegisterActiveTimer(0.0f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SAnimationMapPanel::OnFocusMoveTimer));
}

EActiveTimerReturnType SAnimationMapPanel::OnFocusMoveTimer(double /*InCurrentTime*/, float /*InDeltaTime*/)
{
	// Gesture-aware poll-until-applied: a graph rebuild / JumpToNode under a live gesture is unsafe, so
	// keep waiting (Continue) until quiet, then apply the latest requested focus exactly once.
	if (IsGestureLive())
	{
		return EActiveTimerReturnType::Continue;
	}
	bFocusTimerQueued = false;
	const int32 Idx = PendingFocusFlipbookIndex;
	PendingFocusFlipbookIndex = INDEX_NONE;
	if (Idx != INDEX_NONE)
	{
		FocusMoveNodeByFlipbookIndex(Idx);
	}
	return EActiveTimerReturnType::Stop;
}

void SAnimationMapPanel::FocusMoveNodeByFlipbookIndex(int32 FlipbookIndex)
{
	// Resolve the flipbook's home group, open that scoped graph (else the unassigned bucket), reconcile,
	// and JumpToNode the matching move without changing the model selection (the model is the source here).
	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset || !Asset->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return;
	}

	const FString MoveName = Asset->Flipbooks[FlipbookIndex].Identity.FlipbookName;

	// Suppress the model push for the WHOLE focus: opening a group + ReconcileNow can re-match a
	// previously-selected edge (firing HandleGraphSelectionChanged), and we don't want that re-match to
	// shove a stray selection back into the model — the model already holds the focus target. The final
	// JumpToNode (also under this guard) selects the intended node; model and graph stay consistent.
	TGuardValue<bool> SyncGuard(bSuppressSelectionSync, true);

	// Unified Animation Map: a move's graph lives inside its group. Resolve which group this flipbook
	// projects into and open that scoped graph (else the unassigned bucket), then focus the node.
	const Paper2DPlusAnimationMap::FAnimationMapProjection Projection =
		Paper2DPlusAnimationMap::ProjectAnimationMap(Asset);
	FGameplayTag ResolvedGroupTag;
	for (const Paper2DPlusAnimationMap::FAnimationMapGroup& Group : Projection.Groups)
	{
		for (const Paper2DPlusAnimationMap::FAnimationMapEntry& Entry : Group.Entries)
		{
			if (Entry.FlipbookIndex == FlipbookIndex)
			{
				ResolvedGroupTag = Group.Tag;
				break;
			}
		}
		if (ResolvedGroupTag.IsValid())
		{
			break;
		}
	}

	if (ResolvedGroupTag.IsValid())
	{
		OpenAnimationMapGroup(ResolvedGroupTag);
	}
	else
	{
		OpenAnimationMapUnassigned();
	}

	// The Open* calls above requested a fit-all (audit F14), but this path focuses a SPECIFIC node — clear
	// the request so the JumpToNode below wins instead of being overridden by a fit-to-all on reconcile.
	bPendingFitToView = false;

	if (GraphObj && GraphEditorWidget.IsValid() && !HasActiveTransaction() && !bGraphWriteInProgress && !IsGestureLive())
	{
		ReconcileNow();

		TMap<FString, UPaper2DPlusAnimationMapNode_Move*> LiveMovesByLower;
		TArray<UPaper2DPlusAnimationMapNode_Transition*> LiveEdges;
		CollectLiveNodes(LiveMovesByLower, LiveEdges);
		if (UPaper2DPlusAnimationMapNode_Move* NodeToFocus = LiveMovesByLower.FindRef(MoveName.ToLower()))
		{
			// Selects the node (re-entering HandleGraphSelectionChanged) — covered by SyncGuard above.
			GraphEditorWidget->JumpToNode(NodeToFocus, /*bRequestRename=*/false, /*bSelectNode=*/true);
		}
	}
	else
	{
		RequestReconcile();
	}
}

int32 SAnimationMapPanel::FindFlipbookIndexByName(const FString& MoveName) const
{
	const UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset || MoveName.IsEmpty())
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Asset->Flipbooks.Num(); ++Index)
	{
		if (Asset->Flipbooks[Index].Identity.FlipbookName.Equals(MoveName, ESearchCase::IgnoreCase))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

// =====================================================================================================
// HELPERS
// =====================================================================================================

UPaper2DPlusCharacterProfileAsset* SAnimationMapPanel::GetAsset() const
{
	// STRICTLY per-use (never cached): the Character Layer editor's Base-Profile swap re-points the
	// model, and every reconcile/read must follow it.
	return Model.IsValid() ? Model->GetAsset() : nullptr;
}

FGraphAppearanceInfo SAnimationMapPanel::GetGraphAppearance() const
{
	FGraphAppearanceInfo Appearance;
	// Empty-state contract (R4): when the node set is empty, the instruction text IS the empty state —
	// never pre-seed nodes from all profile moves.
	if (GraphObj && GraphObj->Nodes.Num() == 0)
	{
		Appearance.InstructionText =
			bAnimationMapGroupGraphOpen
				&& !bAnimationMapGroupGraphIsUnassigned
				&& ActiveAnimationMapGroupTag.IsValid()
			? LOCTEXT("AnimationMapGraphEmptyInstruction", "Drag flipbooks here to add them to this animation group")
			: LOCTEXT("AnimationMapEmptyInstruction", "Drag a flipbook here to place its move");
	}
	return Appearance;
}

// =====================================================================================================
// ZOOM / VIEW-LOCATION PERSISTENCE (GEditorPerProjectIni — the HitboxEditorPanel recipe; no explicit Flush)
// =====================================================================================================

void SAnimationMapPanel::RestoreViewFromConfig()
{
	if (!GConfig || !GraphEditorWidget.IsValid())
	{
		return;
	}
	float Zoom = 1.0f;
	float ViewX = 0.0f;
	float ViewY = 0.0f;
	const bool bHasZoom = GConfig->GetFloat(AnimationMapPanel_ConfigSection, AnimationMapPanel_ConfigKeyZoom, Zoom, GEditorPerProjectIni);
	const bool bHasX = GConfig->GetFloat(AnimationMapPanel_ConfigSection, AnimationMapPanel_ConfigKeyViewX, ViewX, GEditorPerProjectIni);
	const bool bHasY = GConfig->GetFloat(AnimationMapPanel_ConfigSection, AnimationMapPanel_ConfigKeyViewY, ViewY, GEditorPerProjectIni);
	if (bHasZoom && bHasX && bHasY)
	{
		AnimationMapPanel_SetViewLocation(GraphEditorWidget, FVector2f(ViewX, ViewY), Zoom);
	}

	// U6 (R8): the persisted tag filter. Request lenient — a tag deleted from the project since last
	// session restores as filter-off. Assigned directly (never SetMapFilterTag: nothing to re-stamp
	// yet, and the picker is populated right after this returns).
	FString FilterTagString;
	if (GConfig->GetString(AnimationMapPanel_ConfigSection, AnimationMapPanel_ConfigKeyFilterTag, FilterTagString, GEditorPerProjectIni)
		&& !FilterTagString.TrimStartAndEnd().IsEmpty())
	{
		MapFilterTag = FGameplayTag::RequestGameplayTag(FName(*FilterTagString), /*ErrorIfNotFound*/ false);
	}
}

void SAnimationMapPanel::SaveViewToConfig() const
{
	if (!GConfig || !GraphEditorWidget.IsValid())
	{
		return;
	}
	FVector2f ViewLocation = FVector2f::ZeroVector;
	float ZoomAmount = 1.0f;
	AnimationMapPanel_GetViewLocation(GraphEditorWidget, ViewLocation, ZoomAmount);
	GConfig->SetFloat(AnimationMapPanel_ConfigSection, AnimationMapPanel_ConfigKeyZoom, ZoomAmount, GEditorPerProjectIni);
	GConfig->SetFloat(AnimationMapPanel_ConfigSection, AnimationMapPanel_ConfigKeyViewX, ViewLocation.X, GEditorPerProjectIni);
	GConfig->SetFloat(AnimationMapPanel_ConfigSection, AnimationMapPanel_ConfigKeyViewY, ViewLocation.Y, GEditorPerProjectIni);
	// No explicit Flush (legacy-cleanup 2026-07): GConfig holds the value; the engine flushes
	// GEditorPerProjectIni itself. A manual Flush rewrites the whole multi-hundred-KB ini via
	// tmp+MoveFile and stalled ~8s per call under file-lock contention - the slow-close bug.
}

// =====================================================================================================
// PROBE (Paper2DPlus.AnimationMapGraphProbe — greppable headless/ECABridge verification)
// =====================================================================================================

void SAnimationMapPanel::LogProbe() const
{
	if (!GraphObj)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AnimationMap] Probe: no graph object"));
		return;
	}

	TArray<UPaper2DPlusAnimationMapNode_Move*> MoveNodes;
	TArray<UPaper2DPlusAnimationMapNode_Transition*> EdgeNodes;
	TArray<UPaper2DPlusAnimationMapNode_ChainStart*> MarkerNodes;
	TArray<UPaper2DPlusAnimationMapNode_ChainEnd*> EndMarkerNodes;
	int32 StubCount = 0;
	for (UEdGraphNode* Node : GraphObj->Nodes)
	{
		if (UPaper2DPlusAnimationMapNode_Move* MoveNode = Cast<UPaper2DPlusAnimationMapNode_Move>(Node))
		{
			MoveNodes.Add(MoveNode);
			if (MoveNode->bIsStub)
			{
				++StubCount;
			}
		}
		else if (UPaper2DPlusAnimationMapNode_Transition* EdgeNode = Cast<UPaper2DPlusAnimationMapNode_Transition>(Node))
		{
			EdgeNodes.Add(EdgeNode);
		}
		else if (UPaper2DPlusAnimationMapNode_ChainStart* MarkerNode = Cast<UPaper2DPlusAnimationMapNode_ChainStart>(Node))
		{
			MarkerNodes.Add(MarkerNode); // chain-start rework: markers get their own record below
		}
		else if (UPaper2DPlusAnimationMapNode_ChainEnd* EndMarkerNode = Cast<UPaper2DPlusAnimationMapNode_ChainEnd>(Node))
		{
			EndMarkerNodes.Add(EndMarkerNode); // chain-end rework: mirrored marker record below
		}
	}

	// Header self-description (review, agent-native): the active tag filter is APPENDED at the END of
	// the pre-existing header format so established greps keep matching ("none" when off).
	UE_LOG(LogTemp, Display, TEXT("[AnimationMap] Probe: nodes=%d edges=%d stubs=%d filter=%s"),
		MoveNodes.Num(), EdgeNodes.Num(), StubCount,
		MapFilterTag.IsValid() ? *MapFilterTag.ToString() : TEXT("none"));
	for (const UPaper2DPlusAnimationMapNode_Move* MoveNode : MoveNodes)
	{
		// Effective tags (own ∪ chain ∪ group) as semicolon-joined LEAF names via the shared helper,
		// APPENDED LAST after the position markers — "tags=" is ALWAYS emitted (empty when none) so
		// parsers can key on it unconditionally. Existing fields keep their exact positions/format.
		FString TagsJoined;
		const FGameplayTagContainer EffectiveTags = MoveNode->EffectiveAnimationTags();
		for (auto It = EffectiveTags.CreateConstIterator(); It; ++It)
		{
			if (!TagsJoined.IsEmpty())
			{
				TagsJoined += TEXT(";");
			}
			TagsJoined += Paper2DPlusAnimationTagChips::GetTagLeafString(*It);
		}
		// Chain-start rework: the ,root=R# marker became the bare ,start flag (the number carried no
		// meaning beyond identity, which the flag now provides); the chain-end rework inserted the
		// mirrored ,end flag directly after it. Marker order is
		// [,stub][,start][,end][,combo=N/M][,dim].
		// Combo main-line marker: index 0 (the start itself) prints too, though the #N chip hides it —
		// the probe is the numbering's headless verification seam, so it must show the whole line.
		const FString ComboMarker = MoveNode->ComboSpineIndex >= 0
			? FString::Printf(TEXT(",combo=%d/%d"), MoveNode->ComboSpineIndex, MoveNode->ComboSpineLength)
			: FString();
		UE_LOG(LogTemp, Display, TEXT("[AnimationMap]   node %s@%d,%d%s%s%s%s%s tags=%s"),
			*MoveNode->MoveName, MoveNode->NodePosX, MoveNode->NodePosY,
			MoveNode->bIsStub ? TEXT(",stub") : TEXT(""),
			MoveNode->bIsChainStart ? TEXT(",start") : TEXT(""),  // chain-start flag (replaced ,root=R#)
			MoveNode->bIsChainEnd ? TEXT(",end") : TEXT(""),      // chain-end flag (the ,start mirror)
			*ComboMarker,                                          // combo marker (appended after ,end)
			MoveNode->bDimmedByFilter ? TEXT(",dim") : TEXT(""),  // TASK-108 U6 filter marker
			*TagsJoined);
	}
	// Per-marker lines (chain-start rework): one line per Chain Start marker node so headless
	// verification can see the authoring surface — "floating" for un-aimed markers. Sorted by target
	// (floating markers, empty target, sort first) for deterministic output.
	TArray<UPaper2DPlusAnimationMapNode_ChainStart*> OrderedMarkers = MarkerNodes;
	OrderedMarkers.Sort([](const UPaper2DPlusAnimationMapNode_ChainStart& A, const UPaper2DPlusAnimationMapNode_ChainStart& B)
	{
		return A.TargetMoveLower.Compare(B.TargetMoveLower, ESearchCase::IgnoreCase) < 0;
	});
	for (const UPaper2DPlusAnimationMapNode_ChainStart* MarkerNode : OrderedMarkers)
	{
		UE_LOG(LogTemp, Display, TEXT("[AnimationMap]   chainstart -> %s @%d,%d"),
			MarkerNode->TargetMoveLower.IsEmpty() ? TEXT("floating") : *MarkerNode->TargetMoveLower,
			MarkerNode->NodePosX, MarkerNode->NodePosY);
	}
	// Per-end-marker lines (chain-end rework — the chainstart record's mirror, emitted AFTER all
	// chainstart lines so established greps keep their ordering): one line per Chain End marker node,
	// "floating" for un-aimed markers, sorted by target for deterministic output.
	TArray<UPaper2DPlusAnimationMapNode_ChainEnd*> OrderedEndMarkers = EndMarkerNodes;
	OrderedEndMarkers.Sort([](const UPaper2DPlusAnimationMapNode_ChainEnd& A, const UPaper2DPlusAnimationMapNode_ChainEnd& B)
	{
		return A.TargetMoveLower.Compare(B.TargetMoveLower, ESearchCase::IgnoreCase) < 0;
	});
	for (const UPaper2DPlusAnimationMapNode_ChainEnd* EndMarkerNode : OrderedEndMarkers)
	{
		UE_LOG(LogTemp, Display, TEXT("[AnimationMap]   chainend -> %s @%d,%d"),
			EndMarkerNode->TargetMoveLower.IsEmpty() ? TEXT("floating") : *EndMarkerNode->TargetMoveLower,
			EndMarkerNode->NodePosX, EndMarkerNode->NodePosY);
	}
	// Per-edge lines (U5 — the greppable gate): the edge node's denormalized snapshot/handle, exactly
	// as the write-side guard sees it, plus the target's entering-phase leaf (the pill's phase badge).
	// TASK-108 U1 STRIPPED the label=/cond=/cancel=/badge= fields — they died with the runtime fields
	// (pure From->To arrows); the "edge From->Target" prefix and row=/phase= keep their positions.
	TArray<UPaper2DPlusAnimationMapNode_Transition*> OrderedEdges = EdgeNodes;
	OrderedEdges.Sort([](const UPaper2DPlusAnimationMapNode_Transition& A, const UPaper2DPlusAnimationMapNode_Transition& B)
	{
		const int32 FromCompare = A.FromMove.Compare(B.FromMove, ESearchCase::IgnoreCase);
		return FromCompare != 0 ? FromCompare < 0 : A.RowIndex < B.RowIndex;
	});
	for (const UPaper2DPlusAnimationMapNode_Transition* EdgeNode : OrderedEdges)
	{
		// "none" when neither the transition row nor its target animation supplies a phase.
		const Paper2DPlusAnimationMap::FPhaseTagBadge PhaseBadge =
			Paper2DPlusAnimationMap::GetPhaseTagBadge(EdgeNode->TransitionPhaseTag);
		const FString PhaseString = PhaseBadge.IsValid() ? PhaseBadge.Label : FString(TEXT("none"));
		UE_LOG(LogTemp, Display, TEXT("[AnimationMap]   edge %s->%s row=%d phase=%s"),
			*EdgeNode->FromMove, *EdgeNode->TargetMove, EdgeNode->RowIndex, *PhaseString);
	}
}

int32 SAnimationMapPanel::DerivePhasesFromMapAction()
{
	UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset)
	{
		return 0;
	}

	const TMap<FString, EAnimationPhase> Derived = Paper2DPlusAnimationMap::DerivePhasesFromMap(Asset);

	// PRE-SCAN so an all-already-tagged / nothing-to-derive run opens NO transaction (never dirties).
	// First-write-wins is already baked into Derived; here we ONLY fill empty tags (never overwrite a
	// manual one) and only when the derived phase maps to a registered tag.
	TArray<TPair<int32, FGameplayTag>> Writes;
	for (int32 Index = 0; Index < Asset->Flipbooks.Num(); ++Index)
	{
		FFlipbookProfileEntry& Entry = Asset->Flipbooks[Index];
		if (Entry.EditorMeta.PhaseTag.IsValid())
		{
			continue; // respect a manually-set phase tag
		}
		const EAnimationPhase* Phase = Derived.Find(Entry.Identity.FlipbookName.ToLower());
		if (!Phase)
		{
			continue;
		}
		const FGameplayTag PhaseTag = Paper2DPlusAnimationMap::PhaseToGameplayTag(*Phase);
		if (PhaseTag.IsValid())
		{
			Writes.Emplace(Index, PhaseTag);
		}
	}

	if (Writes.Num() == 0)
	{
		FNotificationInfo Info(LOCTEXT("AnimationMapNoPhasesDerived",
			"No phases to derive — every move under a chain start already has a phase tag (or no multi-move chains are authored)."));
		Info.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return 0;
	}

	BeginTransaction(LOCTEXT("AnimationMapDerivePhasesTransaction", "Derive Phases From Map"));
	for (const TPair<int32, FGameplayTag>& Write : Writes)
	{
		Asset->Flipbooks[Write.Key].EditorMeta.PhaseTag = Write.Value;
	}
	EndTransaction();

	// The asset Modify() echo reconciles the pills (edge phase badges) and the browser rows (phase tints)
	// via OnAssetExternallyModified — no explicit refresh needed here.
	FNotificationInfo Info(FText::Format(
		LOCTEXT("AnimationMapPhasesDerived", "Derived phase tags for {0} move(s) from chain starts."),
		FText::AsNumber(Writes.Num())));
	Info.ExpireDuration = 4.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
	UE_LOG(LogTemp, Display, TEXT("[AnimationMap] Derive Phases: set %d phase tag(s)."), Writes.Num());
	return Writes.Num();
}

void SAnimationMapPanel::LogAnimationMapProbe() const
{
	const UPaper2DPlusCharacterProfileAsset* Asset = GetAsset();
	if (!Asset)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AnimationMap] Probe: no asset"));
		return;
	}

	const Paper2DPlusAnimationMap::FAnimationMapProjection Projection =
		Paper2DPlusAnimationMap::ProjectAnimationMap(Asset);

	int32 MappedCount = 0;
	for (const Paper2DPlusAnimationMap::FAnimationMapGroup& Group : Projection.Groups)
	{
		MappedCount += Group.Entries.Num();
	}

	UE_LOG(LogTemp, Display, TEXT("[AnimationMap] Probe: groups=%d mapped=%d unmapped=%d"),
		Projection.Groups.Num(), MappedCount, Projection.Unmapped.Num());
	for (const Paper2DPlusAnimationMap::FAnimationMapGroup& Group : Projection.Groups)
	{
		UE_LOG(LogTemp, Display, TEXT("[AnimationMap]   group %s entries=%d"),
			*AnimationMapPanel_ShortAnimationTagName(Group.Tag), Group.Entries.Num());
		for (const Paper2DPlusAnimationMap::FAnimationMapEntry& Entry : Group.Entries)
		{
			const FString Phase = Entry.PhaseTag.IsValid() ? Entry.PhaseTag.ToString() : FString(TEXT("none"));
			UE_LOG(LogTemp, Display, TEXT("[AnimationMap]     entry %s phase=%s"),
				*Entry.FlipbookName, *Phase);
		}
	}
	for (const Paper2DPlusAnimationMap::FAnimationMapEntry& Entry : Projection.Unmapped)
	{
		const FString Phase = Entry.PhaseTag.IsValid() ? Entry.PhaseTag.ToString() : FString(TEXT("none"));
		UE_LOG(LogTemp, Display, TEXT("[AnimationMap]   unmapped %s phase=%s"),
			*Entry.FlipbookName, *Phase);
	}
}

#undef LOCTEXT_NAMESPACE
