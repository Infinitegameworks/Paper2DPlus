// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Misc/EngineVersionComparison.h" // ENGINE_*_VERSION for the GetTagLeafName gate - explicit, not PCH-order-dependent
#include "Widgets/SWidget.h"

/**
 * Shared animation-tag CHIP visuals (TASK-108 U6, R6) — ONE implementation consumed by BOTH the
 * flipbook browser's grid cards (FlipbookBrowserPanel.cpp, where an SComboButton wrapper adds the
 * SGameplayTagPicker edit affordance) and the Animation Map's move nodes (SAnimationMapMoveNode.cpp,
 * read-only, denormalized from the node's projected data). Colors resolve through
 * Paper2DPlusAnimationMap::GetAnimationTagChipColor (registry exact → ancestor → convention fallback
 * palettes — R11's single seam).
 *
 * Provenance styling contract:
 *  - Own            = SOLID fill (the tag is authored on this animation),
 *  - ChainInherited = GHOSTED (reduced-opacity fill — inherited from a reaching chain root),
 *  - GroupImplied   = OUTLINED (colored 1px ring, dark fill — implied by TagMappings membership).
 * Strongest provenance wins when one tag arrives from several sources (own > chain > group).
 * Overflow: chips beyond MaxVisible collapse into a "+N" chip whose tooltip lists the rest
 * (full tag paths + provenance).
 */
namespace Paper2DPlusAnimationTagChips
{
	/** The tag's LEAF segment (e.g. "Airborne" for Paper2DPlus.Animation.Context.Airborne) — THE one
	 *  shared cross-version leaf helper (review consolidation; this body was duplicated 4× across the
	 *  chip utils, AnimationMapCore, the Map panel, and the browser cards). FGameplayTag::GetTagLeafName()
	 *  is 5.6+ — pre-5.6 parses the last dot segment (the whole name when dotless). Pure header-inline;
	 *  safe to include from Slate-free .cpps (the projection core) — only this header's OTHER helpers
	 *  touch Slate. */
	inline FString GetTagLeafString(const FGameplayTag& InTag)
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
		return InTag.GetTagLeafName().ToString();
#else
		FString Full = InTag.GetTagName().ToString();
		int32 DotIndex = INDEX_NONE;
		if (Full.FindLastChar(TEXT('.'), DotIndex))
		{
			return Full.RightChop(DotIndex + 1);
		}
		return Full;
#endif
	}

	enum class EAnimationTagChipProvenance : uint8
	{
		Own,
		ChainInherited,
		GroupImplied,
	};

	struct FAnimationTagChipItem
	{
		FGameplayTag Tag;
		EAnimationTagChipProvenance Provenance = EAnimationTagChipProvenance::Own;
	};

	/** Flatten the three provenance containers into ordered chip items: own first, then
	 *  chain-inherited, then group-implied; each tag appears ONCE with its STRONGEST provenance
	 *  (own > chain > group). Pure. */
	TArray<FAnimationTagChipItem> BuildChipItems(const FGameplayTagContainer& OwnTags,
		const FGameplayTagContainer& ChainInheritedTags, const FGameplayTagContainer& GroupImpliedTags);

	/** One compact leaf-name chip for a single item (rounded pill, provenance-styled, full-path +
	 *  provenance tooltip). Building block for MakeChipsRow; exposed for bespoke layouts. */
	TSharedRef<SWidget> MakeTagChip(const FAnimationTagChipItem& Item);

	/** The chips ROW: up to MaxVisible provenance-styled chips + a "+N" overflow chip whose tooltip
	 *  lists the collapsed tags (full paths + provenance). Empty items -> a collapsed null widget.
	 *  Read-only — callers wanting edit (the browser card) wrap the row in their own combo/picker. */
	TSharedRef<SWidget> MakeChipsRow(const TArray<FAnimationTagChipItem>& Items, int32 MaxVisible = 3);
}

#include "Widgets/SCompoundWidget.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h" // EKeys

/**
 * Crash guard for SGameplayTagPicker instances hosted inside Slate MENUS (legacy-cleanup 2026-07).
 *
 * The engine picker's tag-tree rows open a MANAGEMENT context menu on right-click (Add Sub-Tag /
 * Rename / Delete Tag). When the picker lives inside an auto-dismissing menu host (an SComboButton
 * dropdown, a node context-menu submenu), pushing that context menu can create its window against a
 * menu-stack parent that is being torn down — CreateWindowEx fails with Windows error 1400 (invalid
 * window handle) and the editor fatal-asserts ("Window Creation Failed"). Reproduced from the card
 * tag picker (crash UECC-...DEE7, 2026-07-09).
 *
 * The guard swallows right-clicks in the PREVIEW (tunnel) phase, before the tree row can see them,
 * making menu-hosted pickers SELECTION-ONLY. Tag management belongs in the Gameplay Tag Manager
 * window / Project Settings, where the context menu is safe. Wrap the picker:
 *     SNew(SMenuHostedTagPickerGuard)[ SNew(SGameplayTagPicker)... ]
 */
class SMenuHostedTagPickerGuard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenuHostedTagPickerGuard) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ChildSlot[ InArgs._Content.Widget ];
	}

	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			return FReply::Handled();
		}
		return SCompoundWidget::OnPreviewMouseButtonDown(MyGeometry, MouseEvent);
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		// Also eat the RMB up: STableRow opens the context menu from mouse-UP, and the bubbling-up
		// event would still reach the row even with the down swallowed.
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			return FReply::Handled();
		}
		return SCompoundWidget::OnMouseButtonUp(MyGeometry, MouseEvent);
	}
};
