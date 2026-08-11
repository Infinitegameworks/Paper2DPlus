// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "Paper2DPlusFrameCuePreviewAdapter.generated.h"

class UPaper2DPlusFrameCuePreviewContext;

/**
 * Editor-only, creator-extensible simulation for one family of cue payloads. Adapters can play audio,
 * create trajectory/proxy visuals, and show overlays only through the host-owned preview context.
 *
 * The plugin ships NO adapter subclasses of its own. Every adapter the preview host runs comes from a
 * project, registered through UPaper2DPlusFrameCueEditorSettings::PreviewAdapters. Subclass this in
 * C++ or Blueprint, point Supported Cue Class at your Cue Type, and add it to that project setting.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew)
class PAPER2DPLUSEDITOR_API UPaper2DPlusFrameCuePreviewAdapter : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Frame Cue Preview")
	TSubclassOf<UPaper2DPlusCueBase> SupportedCueClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Frame Cue Preview")
	bool bIncludeDerivedCueClasses = true;

	bool CanPreview(const UPaper2DPlusCueBase& Cue) const;
	void DispatchPreview(
		UPaper2DPlusCueBase* Cue,
		const FPaper2DPlusFrameCueContext& Context,
		UPaper2DPlusFrameCuePreviewContext* PreviewContext);
	void DispatchReset(UPaper2DPlusFrameCuePreviewContext* PreviewContext);

protected:
	virtual void HandlePreview(
		UPaper2DPlusCueBase* Cue,
		const FPaper2DPlusFrameCueContext& Context,
		UPaper2DPlusFrameCuePreviewContext* PreviewContext) {}
	virtual void HandleReset(UPaper2DPlusFrameCuePreviewContext* PreviewContext) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Paper2DPlus|Frame Cue Preview", meta = (DisplayName = "Preview Frame Cue"))
	void ReceivePreview(
		UPaper2DPlusCueBase* Cue,
		const FPaper2DPlusFrameCueContext& Context,
		UPaper2DPlusFrameCuePreviewContext* PreviewContext);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paper2DPlus|Frame Cue Preview", meta = (DisplayName = "Reset Frame Cue Preview"))
	void ReceiveReset(UPaper2DPlusFrameCuePreviewContext* PreviewContext);
};
