// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class SVerticalBox;
class SWidget;
class FCharacterProfileEditorModel;
class UPaperSprite;
class UPaper2DPlusCharacterProfileAsset;

class FFlipbookListBuilder
{
public:
	static void Build(
		TSharedPtr<SVerticalBox> ListBox,
		TSharedPtr<FCharacterProfileEditorModel> Model,
		TFunction<TSharedRef<SWidget>(int32)> ItemBuilder,
		TFunction<void()> OnCollapseChanged,
		TFunction<bool(int32)> Filter = nullptr);
};

struct FSpriteContextMenuParams
{
	UPaperSprite* Sprite = nullptr;
	FVector2D ScreenSpacePosition = FVector2D::ZeroVector;
	int32 ReferenceFlipbookIndex = INDEX_NONE;
	int32 ReferenceFrameIndex = INDEX_NONE;
	int32 ContextFrameIndex = INDEX_NONE;
	int32 ExcludedFrameIndex = INDEX_NONE;

	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Asset;
	int32 SelectedFlipbookIndex = INDEX_NONE;

	TSharedPtr<SWidget> AnchorWidget;

	TFunction<void(int32, int32)> OnSetReferenceSprite;
	TFunction<void(UPaperSprite*)> OnOpenSpriteEditor;
	TFunction<void(UPaperSprite*)> OnBrowseInContentBrowser;
	TFunction<void(int32, int32, bool)> OnDeleteFrame;
	TFunction<void(int32, int32, bool)> OnDeleteExcludedFrame;
};

class FEditorContextMenuUtils
{
public:
	static void ShowSpriteContextMenu(const FSpriteContextMenuParams& Params);
};
