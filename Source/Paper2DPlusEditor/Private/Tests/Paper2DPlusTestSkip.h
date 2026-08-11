// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"

/**
 * THE canonical skip marker for Paper2DPlus automation.
 *
 * Unreal has no "skipped" result: a test that early-returns true because its preconditions are
 * absent reports Success, and is then textually indistinguishable from a test that actually ran.
 * That is not a cosmetic problem. The 7.0.0 audit shipped a "612/612 green" suite in which six rows
 * executed nothing under -nullrhi, and one of those six was hiding a crash that killed the editor
 * the moment it ran for real on a render-capable host. The headline number asserted coverage the
 * run did not have.
 *
 * Every deliberate skip therefore emits this exact token, so skips are COUNTABLE from the exported
 * report rather than merely present in prose somebody has to read:
 *
 *     python -c "import json;d=json.load(open('index.json',encoding='utf-8-sig'));
 *       print(sum(1 for t in d['tests'] for e in t.get('entries',[])
 *                 if '[P2DP-SKIPPED]' in str(e.get('event',{}).get('message',''))))"
 *
 * Report the count alongside the total. "612/612, 6 skipped" is an honest sentence; "612/612" on its
 * own is not.
 */
#define PAPER2DPLUS_TEST_SKIP_MARKER TEXT("[P2DP-SKIPPED]")

namespace Paper2DPlusTestSkip
{
	/** Emit a canonical, countable skip notice. Prefer the helpers below over calling this directly. */
	inline void Mark(FAutomationTestBase& Test, const FString& What, const FString& Why)
	{
		Test.AddInfo(FString::Printf(
			TEXT("%s %s — %s"),
			PAPER2DPLUS_TEST_SKIP_MARKER,
			*What,
			*Why));
	}

	/**
	 * True when there is no renderer, after recording a countable skip.
	 *
	 * Use as: if (Paper2DPlusTestSkip::WithoutRenderer(*this, TEXT("..."))) { return true; }
	 *
	 * Slate initialization is checked as well as CanEverRender because a headless host can have one
	 * without the other, and widget construction needs both.
	 */
	inline bool WithoutRenderer(FAutomationTestBase& Test, const FString& What)
	{
		if (FSlateApplication::IsInitialized() && FApp::CanEverRender())
		{
			return false;
		}
		Mark(
			Test,
			What,
			TEXT("no renderer in this host; run the host-runtime matrix render-capable to exercise it"));
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
