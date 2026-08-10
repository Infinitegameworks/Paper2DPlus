// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UPaperFlipbook;
class UPaperSprite;

/** Active drawing tool in the Flipbook Draw editor. */
enum class EFlipbookDrawTool : uint8
{
	Pencil,
	Eraser,
	Eyedropper,
	Fill,
	Line,
	Rectangle
};

/** Mirror-drawing axes (a brush dab is also stamped at the mirrored position within the frame). */
enum class EFlipbookMirror : uint8
{
	None       = 0,
	Horizontal = 1 << 0,
	Vertical   = 1 << 1
};
ENUM_CLASS_FLAGS(EFlipbookMirror);

/**
 * FFlipbookDrawModel — lightweight shared state for the Flipbook Draw editor (toolkit + canvas + tools panel).
 *
 * A plain C++ class (NOT a UObject), mirroring FCharacterProfileEditorModel's role but with none of its
 * profile/layer coupling. It owns the edited flipbook, the current key-frame index, and the tool/brush
 * state, and broadcasts multicast delegates so the canvas and panels stay in sync. v1 targets a raw
 * UPaperFlipbook: "logical frame N" == key-frame index N == exactly one UPaperSprite.
 */
class PAPER2DPLUSEDITOR_API FFlipbookDrawModel
{
public:
	// ---- Flipbook / frame ----
	void SetFlipbook(UPaperFlipbook* InFlipbook);
	UPaperFlipbook* GetFlipbook() const { return Flipbook.Get(); }

	/** Number of key frames (== drawable frames). */
	int32 GetNumFrames() const;

	/** The sprite for key-frame Index, or null if out of range / unset. */
	UPaperSprite* GetSpriteAt(int32 Index) const;
	UPaperSprite* GetCurrentSprite() const { return GetSpriteAt(CurrentFrame); }

	int32 GetCurrentFrame() const { return CurrentFrame; }
	/** Clamp + set the current key-frame index; broadcasts OnFrameChanged when it actually changes. */
	void SetCurrentFrame(int32 Index);
	/** Step the current frame by Delta with wrap-around (no-op if < 2 frames). */
	void StepFrame(int32 Delta);

	// ---- Tool / brush state (used from Phase 2/3 on) ----
	EFlipbookDrawTool GetActiveTool() const { return ActiveTool; }
	void SetActiveTool(EFlipbookDrawTool InTool);

	int32 GetBrushSize() const { return BrushSize; }
	void SetBrushSize(int32 InSize);

	FLinearColor GetPrimaryColor() const { return PrimaryColor; }
	void SetPrimaryColor(const FLinearColor& InColor);

	EFlipbookMirror GetMirror() const { return Mirror; }
	void SetMirror(EFlipbookMirror InMirror);

	bool IsOnionSkinEnabled() const { return bOnionSkin; }
	void SetOnionSkinEnabled(bool bEnabled);

	bool IsPixelGridEnabled() const { return bPixelGrid; }
	void SetPixelGridEnabled(bool bEnabled);

	TArray<FLinearColor>& GetSwatches() { return Swatches; }

	// ---- Playback ----
	bool IsPlaying() const { return bIsPlaying; }
	void SetPlaying(bool bPlay);
	void TogglePlaying() { SetPlaying(!bIsPlaying); }

	// ---- Delegates ----
	/** Fired when the current key-frame index changes. */
	FSimpleMulticastDelegate OnFrameChanged;
	/** Fired when any tool/brush/color/toggle state changes. */
	FSimpleMulticastDelegate OnToolStateChanged;
	/** Fired when playback starts/stops. */
	FSimpleMulticastDelegate OnPlaybackStateChanged;

private:
	TWeakObjectPtr<UPaperFlipbook> Flipbook;
	int32 CurrentFrame = 0;

	EFlipbookDrawTool ActiveTool = EFlipbookDrawTool::Pencil;
	int32 BrushSize = 1;
	FLinearColor PrimaryColor = FLinearColor::Red;
	EFlipbookMirror Mirror = EFlipbookMirror::None;
	bool bOnionSkin = false;
	bool bPixelGrid = false;
	bool bIsPlaying = false;
	TArray<FLinearColor> Swatches;
};
