// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusLayerGameplayCompose.h"

#include "FrameCues/Paper2DPlusFrameCue.h"
#include "Paper2DPlusCharacterLayerAsset.h"

namespace Paper2DPlusLayerGameplayComposePrivate
{
	bool OperationAuthorsHitboxType(
		const FPaper2DPlusLayerGameplayOperation& Operation,
		EHitboxType Type)
	{
		for (const FFrameHitboxData& Frame : Operation.Frames)
		{
			if (Frame.HasHitboxOfType(Type))
			{
				return true;
			}
		}
		return false;
	}

	bool OperationAuthorsSockets(const FPaper2DPlusLayerGameplayOperation& Operation)
	{
		for (const FFrameHitboxData& Frame : Operation.Frames)
		{
			if (!Frame.Sockets.IsEmpty())
			{
				return true;
			}
		}
		return false;
	}

	void StripHitboxType(TArray<FFrameHitboxData>& Frames, EHitboxType Type)
	{
		for (FFrameHitboxData& Frame : Frames)
		{
			Frame.Hitboxes.RemoveAll([Type](const FHitboxData& Box)
			{
				return Box.Type == Type;
			});
		}
	}

	void AppendHitboxType(
		TArray<FFrameHitboxData>& Frames,
		const FPaper2DPlusLayerGameplayOperation& Operation,
		EHitboxType Type)
	{
		const int32 Count = FMath::Min(Frames.Num(), Operation.Frames.Num());
		for (int32 FrameIndex = 0; FrameIndex < Count; ++FrameIndex)
		{
			for (const FHitboxData& Box : Operation.Frames[FrameIndex].Hitboxes)
			{
				if (Box.Type == Type)
				{
					Frames[FrameIndex].Hitboxes.Add(Box);
				}
			}
		}
	}

	void ApplyHitboxChannel(
		TArray<FFrameHitboxData>& Frames,
		const FPaper2DPlusLayerGameplayOperation& Operation,
		EHitboxType Type,
		EPaper2DPlusLayerSourceMerge Merge)
	{
		if (Merge == EPaper2DPlusLayerSourceMerge::ReplaceLower)
		{
			if (!OperationAuthorsHitboxType(Operation, Type))
			{
				return;
			}
			StripHitboxType(Frames, Type);
		}
		AppendHitboxType(Frames, Operation, Type);
	}

	void ApplySocketChannel(
		TArray<FFrameHitboxData>& Frames,
		const FPaper2DPlusLayerGameplayOperation& Operation)
	{
		if (Operation.SocketMerge == EPaper2DPlusLayerSourceMerge::ReplaceLower)
		{
			if (!OperationAuthorsSockets(Operation))
			{
				return;
			}
			for (FFrameHitboxData& Frame : Frames)
			{
				Frame.Sockets.Reset();
			}
		}

		const int32 Count = FMath::Min(Frames.Num(), Operation.Frames.Num());
		for (int32 FrameIndex = 0; FrameIndex < Count; ++FrameIndex)
		{
			Frames[FrameIndex].Sockets.Append(Operation.Frames[FrameIndex].Sockets);
		}
	}

}

bool Paper2DPlusLayerGameplayCompose::ComposeFrames(
	const TArray<FFrameHitboxData>& Baseline,
	const TArray<FPaper2DPlusLayerGameplayOperation>& Operations,
	TArray<FFrameHitboxData>& OutFrames)
{
	if (Operations.IsEmpty())
	{
		return false;
	}

	int32 FrameCount = Baseline.Num();
	for (const FPaper2DPlusLayerGameplayOperation& Operation : Operations)
	{
		FrameCount = FMath::Max(FrameCount, Operation.Frames.Num());
	}

	OutFrames = Baseline;
	// Preserve every real baseline row, then default-initialize any rows supplied only by a layer. This
	// keeps character-wide fields base-owned without making a sparse Profile combat table a truncation gate.
	OutFrames.SetNum(FrameCount);
	for (const FPaper2DPlusLayerGameplayOperation& Operation : Operations)
	{
		Paper2DPlusLayerGameplayComposePrivate::ApplyHitboxChannel(
			OutFrames, Operation, EHitboxType::Attack, Operation.AttackMerge);
		Paper2DPlusLayerGameplayComposePrivate::ApplyHitboxChannel(
			OutFrames, Operation, EHitboxType::Hurtbox, Operation.HurtMerge);
		Paper2DPlusLayerGameplayComposePrivate::ApplySocketChannel(OutFrames, Operation);
	}
	return true;
}

bool Paper2DPlusLayerGameplayCompose::ComposeFrameCues(
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Baseline,
	const TArray<FPaper2DPlusLayerGameplayOperation>& Operations,
	TArray<TObjectPtr<UPaper2DPlusCueBase>>& OutCues)
{
	OutCues.Reset();
	OutCues.Reserve(Baseline.Num());
	for (const TObjectPtr<UPaper2DPlusCueBase>& Cue : Baseline)
	{
		if (Cue)
		{
			OutCues.Add(Cue);
		}
	}

	for (const FPaper2DPlusLayerGameplayOperation& Operation : Operations)
	{
		for (const TObjectPtr<UPaper2DPlusCueBase>& Cue : Operation.FrameCues)
		{
			if (Cue)
			{
				OutCues.Add(Cue);
			}
		}
	}
	return !OutCues.IsEmpty();
}
