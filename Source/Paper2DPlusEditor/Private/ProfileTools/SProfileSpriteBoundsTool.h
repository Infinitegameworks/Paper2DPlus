// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProfileSpriteBoundsService.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UPaper2DPlusCharacterProfileAsset;

DECLARE_DELEGATE_OneParam(FOnProfileSpriteBoundsRouteToReExtract, FString /*AnimationName*/);

/**
 * The Sprite Bounds tool -- VERIFICATION ONLY.
 *
 * It detects and explains; the fix is re-extraction. There is deliberately no repair affordance
 * here: the in-place repair path cannot grow a sprite's source region, which is the exact condition
 * the check most often reports, so offering a Repair button would promise something it cannot do.
 * An attention row instead routes to the Re-extract tool with its animation carried across.
 *
 * Three states, not one. The scan walks every frame of every flipbook reading source pixels, so a
 * large profile takes real time: without a distinct in-progress state a designer sees a frozen
 * window and clicks again. The scan therefore runs behind an explicit action, and the action is
 * disabled while it runs.
 */
class SProfileSpriteBoundsTool : public SCompoundWidget
{
public:
	enum class EState : uint8
	{
		/** Nothing scanned yet. The scan reads source pixels, so it never runs on tool selection. */
		PreScan,
		Scanning,
		Results,
	};

	SLATE_BEGIN_ARGS(SProfileSpriteBoundsTool) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Profile)
		SLATE_EVENT(FOnProfileSpriteBoundsRouteToReExtract, OnRouteToReExtract)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	EState GetState() const { return State; }

#if WITH_DEV_AUTOMATION_TESTS
	/** Runs the scan synchronously, skipping the deferred paint that produces the Scanning state. */
	void RunScanForTests();
	void BeginScanForTests() { BeginScan(); }
	const FProfileSpriteBoundsReport& GetReportForTests() const { return Report; }
	/** Every diagnostic string the results surface would show, in row order. */
	TArray<FString> GetVisibleDiagnosticsForTests() const;
	bool ActivateFirstAttentionRowForTests();
	bool IsScanActionEnabledForTests() const { return CanScan(); }
#endif

private:
	void BeginScan();
	void FinishScan();
	bool CanScan() const;
	FReply HandleScanClicked();
	void RebuildResults();
	FText GetSummaryText() const;

	/** Rows the designer must act on: repairable or unsupported. Never a repair action. */
	void RouteToReExtract(const FString& AnimationName);

	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Profile;
	FOnProfileSpriteBoundsRouteToReExtract OnRouteToReExtract;

	EState State = EState::PreScan;
	FProfileSpriteBoundsReport Report;
	TSharedPtr<SVerticalBox> ResultsBox;
};
