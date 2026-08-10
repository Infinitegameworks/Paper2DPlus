// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UPaper2DPlusCharacterLayerAsset;

/** Generic Layer appearance authoring: complete presets, one default, and optional Exclusive Groups. */
class PAPER2DPLUSEDITOR_API SLayerAppearancePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLayerAppearancePanel) {}
		SLATE_ARGUMENT(UPaper2DPlusCharacterLayerAsset*, LayerAsset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }

	bool AddPreset(const FString& DisplayName, FGuid& OutPresetId);
	bool RemovePreset(FGuid PresetId);
	bool SelectPreset(FGuid PresetId);
	bool SetDefaultPreset(FGuid PresetId);
	bool SetLayerActiveInPreset(FGuid PresetId, FGuid LayerId, bool bActive);
	bool AddExclusiveGroup(const FString& DisplayName, FGuid& OutGroupId);
	bool AssignLayerToExclusiveGroup(FGuid LayerId, FGuid GroupId);

	FGuid GetSelectedPresetIdForTests() const { return SelectedPresetId; }
	int32 GetPresetCountForTests() const;
	int32 GetExclusiveGroupCountForTests() const;

private:
	void Rebuild();
	void ScheduleRebuild();
	FText GetSummaryText() const;
	FReply AddPresetFromUi();
	FReply AddExclusiveGroupFromUi();
	FReply SetSelectedAsDefault();
	FReply CycleLayerExclusiveGroup(FGuid LayerId);

	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerAsset;
	FGuid SelectedPresetId;
	TSharedPtr<SVerticalBox> Content;
	bool bRebuildPending = false;
};
