// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FCharacterProfileEditorModel;
class UPaper2DPlusCharacterLayerAsset;
class UPaperSprite;

enum class ELayerArtOffsetScope : uint8
{
	CurrentAnimation,
	AllAnimations
};

/** Focused source-art inspector for the selected real LayerId. The shared composite canvas remains the
 *  only direct-manipulation surface; this inspector edits its sprite mapping and placement numerically. */
class PAPER2DPLUSEDITOR_API SLayerArtInspector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLayerArtInspector) {}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		SLATE_ARGUMENT(UPaper2DPlusCharacterLayerAsset*, LayerAsset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Worldless seams used by the U12 regression tests and both numeric fields. */
	static FVector2D GetPlacement(
		const UPaper2DPlusCharacterLayerAsset& LayerAsset,
		const FCharacterProfileEditorModel& Model,
		ELayerArtOffsetScope Scope);
	static bool SetPlacement(
		UPaper2DPlusCharacterLayerAsset& LayerAsset,
		FCharacterProfileEditorModel& Model,
		ELayerArtOffsetScope Scope,
		const FVector2D& NewValue);
	static bool SetFrameSprite(
		UPaper2DPlusCharacterLayerAsset& LayerAsset,
		FCharacterProfileEditorModel& Model,
		UPaperSprite* Sprite);

private:
	FText GetLayerTitle() const;
	FText GetMappingSummary() const;
	FString GetSpriteObjectPath() const;
	void HandleSpriteChanged(const struct FAssetData& AssetData);
	TOptional<float> GetOffsetComponent(bool bX) const;
	void CommitOffsetComponent(float Value, ETextCommit::Type CommitType, bool bX);
	FReply ResetPlacement();
	ECheckBoxState GetScopeState(ELayerArtOffsetScope Scope) const;
	void SetScope(ECheckBoxState State, ELayerArtOffsetScope Scope);

	TSharedPtr<FCharacterProfileEditorModel> Model;
	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerAsset;
	ELayerArtOffsetScope OffsetScope = ELayerArtOffsetScope::CurrentAnimation;
};

