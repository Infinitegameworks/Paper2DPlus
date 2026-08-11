// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Paper2DPlusTestSkip.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTestSkipConventionTest,
	"Paper2DPlus.TestInfrastructure.SkipsAreCountable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTestSkipConventionTest::RunTest(const FString& Parameters)
{
	// The marker is a WIRE FORMAT, not a message. Run evidence counts occurrences of this exact
	// token in the exported report, and the count is quoted next to the pass total so nobody reads
	// "612/612" as "612 tests ran". Changing the literal silently zeroes every skip count instead of
	// failing anything, so it is pinned here.
	TestEqual(
		TEXT("skip marker literal is unchanged"),
		FString(PAPER2DPLUS_TEST_SKIP_MARKER),
		FString(TEXT("[P2DP-SKIPPED]")));

	// This test deliberately does NOT call Mark() or WithoutRenderer().
	//
	// Both emit the marker, and this test is not a skip — so exercising them here would add a
	// phantom entry to the very count the marker exists to produce. The first run of this
	// convention proved the point immediately: the report read "6 skipped" when only five tests had
	// actually skipped anything. A metric its own self-test corrupts is worse than no metric.
	//
	// Emission is already proven by the real skips: every render-gated test in the suite runs these
	// helpers for real on a headless host, and their tokens are what the count is made of. What
	// cannot be proven that way is the literal itself — a rename would zero every count silently
	// rather than fail anything — so pinning it above is exactly the coverage this test owes.
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
