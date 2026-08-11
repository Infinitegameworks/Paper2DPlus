// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FlipbookDrawModel.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"

void FFlipbookDrawModel::SetFlipbook(UPaperFlipbook* InFlipbook)
{
	Flipbook = InFlipbook;
	CurrentFrame = 0;
	OnFrameChanged.Broadcast();
}

int32 FFlipbookDrawModel::GetNumFrames() const
{
	const UPaperFlipbook* FB = Flipbook.Get();
	return FB ? FB->GetNumKeyFrames() : 0;
}

UPaperSprite* FFlipbookDrawModel::GetSpriteAt(int32 Index) const
{
	const UPaperFlipbook* FB = Flipbook.Get();
	if (!FB || Index < 0 || Index >= FB->GetNumKeyFrames())
	{
		return nullptr;
	}
	return FB->GetKeyFrameChecked(Index).Sprite;
}

void FFlipbookDrawModel::SetCurrentFrame(int32 Index)
{
	const int32 NumFrames = GetNumFrames();
	if (NumFrames <= 0)
	{
		return;
	}
	const int32 Clamped = FMath::Clamp(Index, 0, NumFrames - 1);
	if (Clamped != CurrentFrame)
	{
		CurrentFrame = Clamped;
		OnFrameChanged.Broadcast();
	}
}

void FFlipbookDrawModel::StepFrame(int32 Delta)
{
	const int32 NumFrames = GetNumFrames();
	if (NumFrames < 2 || Delta == 0)
	{
		return;
	}
	// Wrap-around so ←/→ never dead-ends at the strip edges.
	const int32 Next = ((CurrentFrame + Delta) % NumFrames + NumFrames) % NumFrames;
	SetCurrentFrame(Next);
}

void FFlipbookDrawModel::SetActiveTool(EFlipbookDrawTool InTool)
{
	if (ActiveTool != InTool)
	{
		ActiveTool = InTool;
		OnToolStateChanged.Broadcast();
	}
}

void FFlipbookDrawModel::SetBrushSize(int32 InSize)
{
	const int32 Clamped = FMath::Clamp(InSize, 1, 64);
	if (Clamped != BrushSize)
	{
		BrushSize = Clamped;
		OnToolStateChanged.Broadcast();
	}
}

void FFlipbookDrawModel::SetPrimaryColor(const FLinearColor& InColor)
{
	if (!PrimaryColor.Equals(InColor))
	{
		PrimaryColor = InColor;
		OnToolStateChanged.Broadcast();
	}
}

void FFlipbookDrawModel::SetMirror(EFlipbookMirror InMirror)
{
	if (Mirror != InMirror)
	{
		Mirror = InMirror;
		OnToolStateChanged.Broadcast();
	}
}

void FFlipbookDrawModel::SetOnionSkinEnabled(bool bEnabled)
{
	if (bOnionSkin != bEnabled)
	{
		bOnionSkin = bEnabled;
		OnToolStateChanged.Broadcast();
	}
}

void FFlipbookDrawModel::SetPixelGridEnabled(bool bEnabled)
{
	if (bPixelGrid != bEnabled)
	{
		bPixelGrid = bEnabled;
		OnToolStateChanged.Broadcast();
	}
}

void FFlipbookDrawModel::SetPlaying(bool bPlay)
{
	// Need at least 2 frames to play.
	if (bPlay && GetNumFrames() < 2)
	{
		bPlay = false;
	}
	if (bIsPlaying != bPlay)
	{
		bIsPlaying = bPlay;
		OnPlaybackStateChanged.Broadcast();
	}
}
