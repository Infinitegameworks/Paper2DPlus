// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Paper2DPlusFrameCueEditorSettings.generated.h"

class UPaper2DPlusFrameCuePreviewAdapter;

/** Project-wide registry for creator-authored editor-only Frame Cue preview adapters. */
UCLASS(Config = Editor, DefaultConfig, meta = (DisplayName = "Frame Cue Preview"))
class PAPER2DPLUSEDITOR_API UPaper2DPlusFrameCueEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Adapters", meta = (MetaClass = "/Script/Paper2DPlusEditor.Paper2DPlusFrameCuePreviewAdapter"))
	TArray<TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>> PreviewAdapters;

	/** Process-wide change seam used by every open preview host. */
	static FSimpleMulticastDelegate& OnPreviewAdaptersChanged();

	/** Broadcast after programmatic registry mutation that does not pass through PostEditChangeProperty. */
	void NotifyPreviewAdaptersChanged();

	/** Add one creator adapter, optionally persist it, and refresh every open preview host. */
	bool RegisterPreviewAdapter(
		const TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>& AdapterClass,
		bool bSaveConfig = true);

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Paper2DPlusFrameCues"); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
