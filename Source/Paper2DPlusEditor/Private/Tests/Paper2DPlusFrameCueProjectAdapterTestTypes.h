// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusEditorTestFrameCueTypes.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewAdapter.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewContext.h"
#include "FrameCues/Paper2DPlusFrameCueEditorSettings.h"
#include "Paper2DPlusFrameCueProjectAdapterTestTypes.generated.h"

/**
 * Test-only project adapters used to pin registry ordering and host-owned cleanup. HideDropdown keeps
 * these native fixtures out of every designer-facing class picker.
 */
UCLASS(HideDropdown)
class UPaper2DPlusFrameCueProjectAdapterATest : public UPaper2DPlusFrameCuePreviewAdapter
{
	GENERATED_BODY()

public:
	UPaper2DPlusFrameCueProjectAdapterATest()
	{
		SupportedCueClass = UPaper2DPlusEditorTestRangeCue::StaticClass();
		bIncludeDerivedCueClasses = false;
	}

protected:
	virtual void HandlePreview(
		UPaper2DPlusCueBase* Cue,
		const FPaper2DPlusFrameCueContext& Context,
		UPaper2DPlusFrameCuePreviewContext* PreviewContext) override
	{
		if (Cue && PreviewContext && Context.Phase == EPaper2DPlusFrameCuePhase::Trigger)
		{
			PreviewContext->ShowShape(
				FVector2D(11.0f, 13.0f),
				FVector2D(17.0f, 19.0f),
				FLinearColor::Green,
				10.0f);
		}
	}
};

UCLASS(HideDropdown)
class UPaper2DPlusFrameCueProjectAdapterBTest : public UPaper2DPlusFrameCuePreviewAdapter
{
	GENERATED_BODY()

public:
	UPaper2DPlusFrameCueProjectAdapterBTest()
	{
		SupportedCueClass = UPaper2DPlusEditorTestRangeCue::StaticClass();
		bIncludeDerivedCueClasses = false;
	}

protected:
	virtual void HandlePreview(
		UPaper2DPlusCueBase* Cue,
		const FPaper2DPlusFrameCueContext& Context,
		UPaper2DPlusFrameCuePreviewContext* PreviewContext) override
	{
		if (Cue && PreviewContext && Context.Phase == EPaper2DPlusFrameCuePhase::Trigger)
		{
			PreviewContext->CreateTimer(10.0f);
		}
	}
};

/**
 * Resource-ledger fixture.
 *
 * The plugin ships no native preview adapters any more, so the only way a host-owned Effect/Overlay/
 * Camera resource can exist is through a registered project adapter. This fixture allocates exactly
 * one of each so the ledger and its expiry stay covered on the same seam production uses.
 */
UCLASS(HideDropdown)
class UPaper2DPlusFrameCueLedgerAdapterTest : public UPaper2DPlusFrameCuePreviewAdapter
{
	GENERATED_BODY()

public:
	UPaper2DPlusFrameCueLedgerAdapterTest()
	{
		SupportedCueClass = UPaper2DPlusEditorTestMomentCue::StaticClass();
		bIncludeDerivedCueClasses = false;
	}

protected:
	virtual void HandlePreview(
		UPaper2DPlusCueBase* Cue,
		const FPaper2DPlusFrameCueContext& Context,
		UPaper2DPlusFrameCuePreviewContext* PreviewContext) override
	{
		UPaper2DPlusEditorTestMomentCue* TestCue = Cast<UPaper2DPlusEditorTestMomentCue>(Cue);
		if (!TestCue || !PreviewContext || Context.Phase != EPaper2DPlusFrameCuePhase::Trigger)
		{
			return;
		}
		FPaper2DPlusEffectSpawnSettings Settings;
		Settings.EffectFlipbook = TestCue->EffectArt;
		Settings.Offset = TestCue->Offset;
		Settings.Scale = TestCue->Scale;
		// Every lifetime stays short so one 0.3s host tick proves natural expiry, not just release.
		PreviewContext->SpawnEffectProxy(Settings, 0.2f, TestCue);
		PreviewContext->ShowOverlay(FLinearColor::White, 0.05f);
		PreviewContext->ShowCameraShakeApproximation(2.0f, 0.1f);
	}
};

/** Re-entry fixture: registry edits raised inside creator callbacks must be deferred by the host. */
UCLASS(HideDropdown)
class UPaper2DPlusFrameCueRegistryReentryAdapterTest : public UPaper2DPlusFrameCuePreviewAdapter
{
	GENERATED_BODY()

public:
	UPaper2DPlusFrameCueRegistryReentryAdapterTest()
	{
		SupportedCueClass = UPaper2DPlusEditorTestRangeCue::StaticClass();
		bIncludeDerivedCueClasses = false;
	}

	static bool bBroadcastOnPreview;
	static bool bBroadcastOnReset;
	static bool bInsideCallback;
	static TWeakObjectPtr<UPaper2DPlusFrameCuePreviewContext> TrackedContext;
	static int32 PreviewCallCount;
	static int32 ResetCallCount;

	static void ResetFixture()
	{
		bBroadcastOnPreview = false;
		bBroadcastOnReset = false;
		bInsideCallback = false;
		TrackedContext.Reset();
		PreviewCallCount = 0;
		ResetCallCount = 0;
	}

protected:
	virtual void HandlePreview(
		UPaper2DPlusCueBase* Cue,
		const FPaper2DPlusFrameCueContext& Context,
		UPaper2DPlusFrameCuePreviewContext* PreviewContext) override
	{
		if (PreviewContext != TrackedContext.Get())
		{
			return;
		}
		++PreviewCallCount;
		if (PreviewContext && Context.Phase == EPaper2DPlusFrameCuePhase::Trigger)
		{
			PreviewContext->CreateTimer(10.0f);
		}
		if (bBroadcastOnPreview && !bInsideCallback)
		{
			TGuardValue<bool> ReentryGuard(bInsideCallback, true);
			GetMutableDefault<UPaper2DPlusFrameCueEditorSettings>()->NotifyPreviewAdaptersChanged();
		}
	}

	virtual void HandleReset(UPaper2DPlusFrameCuePreviewContext* PreviewContext) override
	{
		if (PreviewContext != TrackedContext.Get())
		{
			return;
		}
		++ResetCallCount;
		if (bBroadcastOnReset && !bInsideCallback)
		{
			TGuardValue<bool> ReentryGuard(bInsideCallback, true);
			GetMutableDefault<UPaper2DPlusFrameCueEditorSettings>()->NotifyPreviewAdaptersChanged();
		}
	}
};
