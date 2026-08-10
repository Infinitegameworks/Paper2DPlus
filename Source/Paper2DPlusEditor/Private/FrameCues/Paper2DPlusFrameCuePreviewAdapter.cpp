// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCuePreviewAdapter.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewContext.h"

bool UPaper2DPlusFrameCuePreviewAdapter::CanPreview(const UPaper2DPlusCueBase& Cue) const
{
	const UClass* Supported = SupportedCueClass.Get();
	return Supported && (bIncludeDerivedCueClasses ? Cue.IsA(Supported) : Cue.GetClass() == Supported);
}

void UPaper2DPlusFrameCuePreviewAdapter::DispatchPreview(
	UPaper2DPlusCueBase* Cue,
	const FPaper2DPlusFrameCueContext& Context,
	UPaper2DPlusFrameCuePreviewContext* PreviewContext)
{
	if (!Cue || !PreviewContext || !CanPreview(*Cue))
	{
		return;
	}
	HandlePreview(Cue, Context, PreviewContext);
	ReceivePreview(Cue, Context, PreviewContext);
}

void UPaper2DPlusFrameCuePreviewAdapter::DispatchReset(UPaper2DPlusFrameCuePreviewContext* PreviewContext)
{
	HandleReset(PreviewContext);
	ReceiveReset(PreviewContext);
}
