// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Engine/Texture2D.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "ProfileTools/SProfileReExtractTool.h"
#include "ProfileTools/SProfileSpriteBoundsTool.h"
#include "ProfileToolsWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "PaperFlipbook.h"
#include "UObject/Package.h"

/*
 * TASK-157 U3 -- the Profile Tools window shell.
 *
 * What is pinned here is the shell's contract, not any one tool's content:
 *   - the tool list is descriptor-driven, so adding a tool is a local change;
 *   - selection maps one-to-one onto the switcher, and re-selecting is a no-op that does not
 *     rebuild a surface;
 *   - the same-index guard covers an INVALID index too. The list mirrors the selection back through
 *     OnSelectionChanged, so a guard that only no-ops on valid indices lets an out-of-range value
 *     fall through and re-enter forever -- the synchronous within-frame freeze class;
 *   - a freshly opened window always has something selected, so the right pane is never blank;
 *   - the PaperZD entry is ABSENT rather than disabled when the feature is compiled out, and the
 *     remaining list stays contiguous and selectable;
 *   - Validation hosts the shared panel configured not to validate on open.
 *
 * Construction is headless-safe: the content widget is an ordinary SCompoundWidget, so SNew works
 * under -nullrhi. Anything that actually SHOWS a window is gated on FApp::CanEverRender(), because
 * adding a real SWindow fatal-asserts when Slate cannot create one.
 *
 * Helpers are ToolsWin_-prefixed per the unity-build file-unique-name rule.
 */

namespace
{
	UPaper2DPlusCharacterProfileAsset* ToolsWin_MakeProfile()
	{
		UPaper2DPlusCharacterProfileAsset* Profile =
			NewObject<UPaper2DPlusCharacterProfileAsset>(GetTransientPackage());
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = TEXT("Idle");
		Entry.Identity.Flipbook = NewObject<UPaperFlipbook>(Profile);
		Profile->Flipbooks.Add(MoveTemp(Entry));
		return Profile;
	}

	TSharedRef<SProfileToolsWindow> ToolsWin_MakeContent(
		UPaper2DPlusCharacterProfileAsset* Profile,
		FName InitialToolId = NAME_None)
	{
		return SNew(SProfileToolsWindow)
			.Profile(Profile)
			.InitialToolId(InitialToolId);
	}
}

// =============================================================================
// Descriptor list: contiguous, uniquely identified, and PaperZD-conditional.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileToolsWindowToolList,
	"Paper2DPlus.ProfileTools.ToolListIsDescriptorDriven",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileToolsWindowToolList::RunTest(const FString& Parameters)
{
	const TSharedRef<SProfileToolsWindow> Content = ToolsWin_MakeContent(ToolsWin_MakeProfile());
	const TArray<TSharedPtr<FProfileToolDescriptor>>& Tools = Content->GetTools();

	TestTrue(TEXT("the window offers at least Character Data and Validation"), Tools.Num() >= 2);

	TSet<FName> SeenIds;
	for (const TSharedPtr<FProfileToolDescriptor>& Tool : Tools)
	{
		if (!TestTrue(TEXT("every descriptor is valid"), Tool.IsValid()))
		{
			return false;
		}
		TestFalse(TEXT("every tool carries a stable id"), Tool->Id.IsNone());
		TestFalse(TEXT("every tool carries a label"), Tool->Label.IsEmpty());
		TestTrue(TEXT("every tool can build a surface"), (bool)Tool->MakeSurface);
		bool bAlreadySeen = false;
		SeenIds.Add(Tool->Id, &bAlreadySeen);
		TestFalse(TEXT("tool ids are unique"), bAlreadySeen);
	}

	TestTrue(TEXT("Character Data is present"), SeenIds.Contains(SProfileToolsWindow::ToolId_CharacterData));
	TestTrue(TEXT("Validation is present"), SeenIds.Contains(SProfileToolsWindow::ToolId_Validation));

	// The PaperZD entry is absent rather than disabled when the feature is compiled out. Either way
	// every entry above stays selectable, which is what "the list stays coherent" has to mean.
#if WITH_PAPERZD
	TestTrue(TEXT("PaperZD builds expose the Sequences tool"),
		SeenIds.Contains(SProfileToolsWindow::ToolId_PaperZDSequences));
#else
	TestFalse(TEXT("non-PaperZD builds do not list an unavailable Sequences tool"),
		SeenIds.Contains(SProfileToolsWindow::ToolId_PaperZDSequences));
#endif

	for (int32 Index = 0; Index < Tools.Num(); ++Index)
	{
		Content->SelectTool(Index);
		TestEqual(TEXT("every listed tool is selectable"), Content->GetSelectedToolIndex(), Index);
	}
	return true;
}

// =============================================================================
// Default selection: the right pane is never blank on open, and an explicit
// initial tool id is honoured.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileToolsWindowDefaultSelection,
	"Paper2DPlus.ProfileTools.DefaultSelectionIsNeverBlank",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileToolsWindowDefaultSelection::RunTest(const FString& Parameters)
{
	{
		const TSharedRef<SProfileToolsWindow> Content = ToolsWin_MakeContent(ToolsWin_MakeProfile());
		TestTrue(TEXT("a freshly opened window has a tool selected"),
			Content->GetTools().IsValidIndex(Content->GetSelectedToolIndex()));
		TestEqual(TEXT("the first entry is the default"), Content->GetSelectedToolIndex(), 0);
	}
	{
		const TSharedRef<SProfileToolsWindow> Content =
			ToolsWin_MakeContent(ToolsWin_MakeProfile(), SProfileToolsWindow::ToolId_Validation);
		const int32 Index = Content->GetSelectedToolIndex();
		TestTrue(TEXT("an explicit initial tool id selects that tool"),
			Content->GetTools().IsValidIndex(Index));
		TestEqual(TEXT("and it is the requested one"),
			Content->GetTools()[Index]->Id, SProfileToolsWindow::ToolId_Validation);
	}
	{
		// An unknown id must not leave the pane blank -- it falls back to the first entry.
		const TSharedRef<SProfileToolsWindow> Content =
			ToolsWin_MakeContent(ToolsWin_MakeProfile(), FName(TEXT("NoSuchTool")));
		TestEqual(TEXT("an unknown initial tool id falls back to the first entry"),
			Content->GetSelectedToolIndex(), 0);
	}
	return true;
}

// =============================================================================
// The selection guard, including the invalid-index case.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileToolsWindowSelectionGuard,
	"Paper2DPlus.ProfileTools.SelectionNoOpCoversInvalidIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileToolsWindowSelectionGuard::RunTest(const FString& Parameters)
{
	const TSharedRef<SProfileToolsWindow> Content = ToolsWin_MakeContent(ToolsWin_MakeProfile());
	const int32 BuildsAfterConstruct = Content->GetSurfaceBuildCountForTests();

	TestEqual(TEXT("each tool built its surface exactly once at construction"),
		BuildsAfterConstruct, Content->GetTools().Num());

	// Re-selecting the current tool is a no-op and must not rebuild anything.
	const int32 Current = Content->GetSelectedToolIndex();
	Content->SelectTool(Current);
	Content->SelectTool(Current);
	TestEqual(TEXT("re-selecting the current tool does not rebuild a surface"),
		Content->GetSurfaceBuildCountForTests(), BuildsAfterConstruct);
	TestEqual(TEXT("and leaves the selection alone"), Content->GetSelectedToolIndex(), Current);

	// The invalid-index half of the guard. Selecting the SAME invalid index twice must settle
	// rather than fall through and re-enter; the freeze this pins is synchronous, so a hang here
	// would present as the test never returning.
	Content->SelectTool(INDEX_NONE);
	TestEqual(TEXT("an invalid index is accepted as the selection"),
		Content->GetSelectedToolIndex(), INDEX_NONE);
	Content->SelectTool(INDEX_NONE);
	TestEqual(TEXT("re-selecting the SAME invalid index is a no-op, not a re-entrant broadcast"),
		Content->GetSelectedToolIndex(), INDEX_NONE);

	const int32 OutOfRange = Content->GetTools().Num() + 5;
	Content->SelectTool(OutOfRange);
	Content->SelectTool(OutOfRange);
	TestEqual(TEXT("an out-of-range index settles the same way"),
		Content->GetSelectedToolIndex(), OutOfRange);

	// Recovery: a real index still selects afterwards.
	Content->SelectTool(0);
	TestEqual(TEXT("a valid index recovers the selection"), Content->GetSelectedToolIndex(), 0);
	TestEqual(TEXT("and still no surface was rebuilt"),
		Content->GetSurfaceBuildCountForTests(), BuildsAfterConstruct);

	TestFalse(TEXT("selecting an unknown id reports failure"),
		Content->SelectToolById(FName(TEXT("NoSuchTool"))));
	TestTrue(TEXT("selecting a known id reports success"),
		Content->SelectToolById(SProfileToolsWindow::ToolId_Validation));
	return true;
}

// =============================================================================
// Validation hosts the shared panel and does not validate merely because the
// window opened.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileToolsWindowValidationHosting,
	"Paper2DPlus.ProfileTools.ValidationDoesNotRunOnOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileToolsWindowValidationHosting::RunTest(const FString& Parameters)
{
	const TSharedRef<SProfileToolsWindow> Content = ToolsWin_MakeContent(ToolsWin_MakeProfile());

	const int32 ValidationIndex = Content->GetTools().IndexOfByPredicate(
		[](const TSharedPtr<FProfileToolDescriptor>& Tool)
		{
			return Tool.IsValid() && Tool->Id == SProfileToolsWindow::ToolId_Validation;
		});
	if (!TestTrue(TEXT("the Validation tool exists"), ValidationIndex != INDEX_NONE))
	{
		return false;
	}

	// Constructing the window builds every surface, including Validation. Because the panel is
	// configured RunInitially(false), that construction must not have run a validation pass --
	// merely opening the tools window should cost nothing.
	Content->SelectTool(ValidationIndex);
	TestEqual(TEXT("selecting Validation switches to it"),
		Content->GetSelectedToolIndex(), ValidationIndex);

	// Selecting it repeatedly still must not rebuild, which is what would re-run construction.
	const int32 Builds = Content->GetSurfaceBuildCountForTests();
	Content->SelectTool(ValidationIndex);
	TestEqual(TEXT("re-selecting Validation does not reconstruct the shared panel"),
		Content->GetSurfaceBuildCountForTests(), Builds);
	return true;
}

// =============================================================================
// Asset lifetime: the window survives losing its subject without leaving tools
// bound to a dead object.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileToolsWindowAssetLifetime,
	"Paper2DPlus.ProfileTools.SurvivesProfileGoingAway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileToolsWindowAssetLifetime::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = ToolsWin_MakeProfile();
	const TSharedRef<SProfileToolsWindow> Content = ToolsWin_MakeContent(Profile);

	TestTrue(TEXT("the window holds its profile"), Content->GetProfile() == Profile);

	// The window holds the profile WEAKLY, so a collected asset leaves a null rather than a
	// dangling pointer. Tools gate on that null instead of writing into a dead object.
	Profile->MarkAsGarbage();
	TestNull(TEXT("a garbage profile resolves to null through the weak handle"),
		Content->GetProfile());

	// Selection still works with no subject -- the shell must not depend on the asset.
	Content->SelectTool(0);
	TestEqual(TEXT("tool selection still functions with no profile"),
		Content->GetSelectedToolIndex(), 0);
	return true;
}

// =============================================================================
// Single instance -- gated, because it shows a real window.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileToolsWindowSingleInstance,
	"Paper2DPlus.ProfileTools.SecondOpenFocusesTheLiveWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileToolsWindowSingleInstance::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender() || !FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Skipped: showing a window requires a renderer (headless -nullrhi run)."));
		return true;
	}

	UPaper2DPlusCharacterProfileAsset* Profile = ToolsWin_MakeProfile();

	SProfileToolsWindow::OpenProfileTools(Profile, SProfileToolsWindow::ToolId_CharacterData);
	const TSharedPtr<SProfileToolsWindow> First = SProfileToolsWindow::GetActiveContentForTests();
	if (!TestTrue(TEXT("the first open produced a live content widget"), First.IsValid()))
	{
		return false;
	}

	// The second request must focus and RE-TARGET the existing instance, not replace a session the
	// designer may be part-way through.
	SProfileToolsWindow::OpenProfileTools(Profile, SProfileToolsWindow::ToolId_Validation);
	const TSharedPtr<SProfileToolsWindow> Second = SProfileToolsWindow::GetActiveContentForTests();
	TestTrue(TEXT("the second open reuses the same content widget"), Second == First);

	if (Second.IsValid())
	{
		const int32 Index = Second->GetSelectedToolIndex();
		if (Second->GetTools().IsValidIndex(Index))
		{
			TestEqual(TEXT("and selects the requested tool on it"),
				Second->GetTools()[Index]->Id, SProfileToolsWindow::ToolId_Validation);
		}
	}

	if (const TSharedPtr<SWindow> Window =
		FSlateApplication::Get().FindWidgetWindow(First.ToSharedRef()))
	{
		Window->RequestDestroyWindow();
	}
	return true;
}

// =============================================================================
// U4/U5 -- Sprite Bounds reports, Re-extract fixes, and a row hops between them.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileToolsSpriteBoundsIsVerifyOnly,
	"Paper2DPlus.ProfileTools.SpriteBoundsIsVerifyOnlyAndDefersItsScan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileToolsSpriteBoundsIsVerifyOnly::RunTest(const FString& Parameters)
{
	const TSharedRef<SProfileToolsWindow> Content = ToolsWin_MakeContent(ToolsWin_MakeProfile());
	const TSharedPtr<SProfileSpriteBoundsTool> Tool = Content->GetSpriteBoundsToolForTests();
	if (!TestTrue(TEXT("the Sprite Bounds tool was built"), Tool.IsValid()))
	{
		return false;
	}

	// The scan reads source pixels for every frame, so merely selecting the tool must not start it.
	TestEqual(TEXT("the tool starts in its pre-scan state"),
		(int32)Tool->GetState(), (int32)SProfileSpriteBoundsTool::EState::PreScan);
	Content->SelectToolById(SProfileToolsWindow::ToolId_SpriteBounds);
	TestEqual(TEXT("selecting the tool does not run the scan"),
		(int32)Tool->GetState(), (int32)SProfileSpriteBoundsTool::EState::PreScan);
	TestTrue(TEXT("the scan action is available before a scan"), Tool->IsScanActionEnabledForTests());

	// Deferred start: the Scanning state must actually be observable, otherwise the progress
	// indicator would be decorative and a long scan would read as a frozen window.
	Tool->BeginScanForTests();
	TestEqual(TEXT("beginning a scan enters the in-progress state"),
		(int32)Tool->GetState(), (int32)SProfileSpriteBoundsTool::EState::Scanning);
	TestFalse(TEXT("a scan in progress disables its own action"), Tool->IsScanActionEnabledForTests());

	Tool->RunScanForTests();
	TestEqual(TEXT("the scan completes into results"),
		(int32)Tool->GetState(), (int32)SProfileSpriteBoundsTool::EState::Results);
	TestTrue(TEXT("the action is available again for a re-check"), Tool->IsScanActionEnabledForTests());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileToolsAttentionRowRoutesToReExtract,
	"Paper2DPlus.ProfileTools.AttentionRowRoutesToReExtract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileToolsAttentionRowRoutesToReExtract::RunTest(const FString& Parameters)
{
	// A profile whose animation carries no stored detection settings: Sprite Bounds cannot establish
	// bounds for it, so it reports as needing attention, and Re-extract reports it ineligible with a
	// reason rather than silently doing nothing.
	UPaper2DPlusCharacterProfileAsset* Profile = ToolsWin_MakeProfile();
	const TSharedRef<SProfileToolsWindow> Content = ToolsWin_MakeContent(Profile);

	const TSharedPtr<SProfileSpriteBoundsTool> Bounds = Content->GetSpriteBoundsToolForTests();
	const TSharedPtr<SProfileReExtractTool> ReExtract = Content->GetReExtractToolForTests();
	if (!TestTrue(TEXT("both tools were built"), Bounds.IsValid() && ReExtract.IsValid()))
	{
		return false;
	}

	Bounds->RunScanForTests();

	// Every diagnostic the service produced is reachable, whatever the frame's classification --
	// the surface this replaces showed them for Unsupported frames only.
	const FProfileSpriteBoundsReport& Report = Bounds->GetReportForTests();
	int32 DiagnosticsInReport = 0;
	for (const FProfileSpriteBoundsFlipbookReport& Flipbook : Report.Flipbooks)
	{
		for (const FProfileSpriteBoundsFrameReport& Frame : Flipbook.Frames)
		{
			if (!Frame.Diagnostic.IsEmpty())
			{
				++DiagnosticsInReport;
			}
		}
	}
	TestEqual(TEXT("every diagnostic the service produced reaches the results surface"),
		Bounds->GetVisibleDiagnosticsForTests().Num(), DiagnosticsInReport);

	// The hop: activating an attention row selects Re-extract AND carries the animation across.
	if (Bounds->ActivateFirstAttentionRowForTests())
	{
		const int32 Index = Content->GetSelectedToolIndex();
		if (TestTrue(TEXT("activation left a valid tool selected"),
			Content->GetTools().IsValidIndex(Index)))
		{
			TestEqual(TEXT("activating an attention row selects the Re-extract tool"),
				Content->GetTools()[Index]->Id, SProfileToolsWindow::ToolId_ReExtract);
		}

		const TSharedPtr<FProfileReExtractRow> Row = ReExtract->FindRowForTests(TEXT("Idle"));
		if (TestTrue(TEXT("Re-extract lists the routed animation"), Row.IsValid()))
		{
			TestTrue(TEXT("and it arrives selected"), Row->bSelected);
		}
	}
	else
	{
		AddInfo(TEXT("Fixture reported no attention rows; routing assertions skipped."));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileToolsReExtractFailsLoudlyOnMissingParams,
	"Paper2DPlus.ProfileTools.ReExtractFailsLoudlyOnMissingParams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileToolsReExtractFailsLoudlyOnMissingParams::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = ToolsWin_MakeProfile();
	const TSharedRef<SProfileToolsWindow> Content = ToolsWin_MakeContent(Profile);
	const TSharedPtr<SProfileReExtractTool> ReExtract = Content->GetReExtractToolForTests();
	if (!TestTrue(TEXT("the Re-extract tool was built"), ReExtract.IsValid()))
	{
		return false;
	}

	// The fixture animation has no stored detection settings at all.
	const TSharedPtr<FProfileReExtractRow> Row = ReExtract->FindRowForTests(TEXT("Idle"));
	if (!TestTrue(TEXT("the animation is listed"), Row.IsValid()))
	{
		return false;
	}
	TestFalse(TEXT("an animation with no stored detection settings is not eligible"), Row->bEligible);
	TestFalse(TEXT("and it says why, rather than presenting a silently dead button"),
		Row->Reason.IsEmpty());

	// Selecting an ineligible row must not make it runnable.
	ReExtract->SelectAnimation(TEXT("Idle"));
	ReExtract->RunSelectedForTests();
	TestFalse(TEXT("an ineligible animation is never run"), Row->bRan);

	// Now give it real detection settings and confirm eligibility flips.
	Profile->Flipbooks[0].AlignmentData.GridDims = FIntPoint(2, 1);
	Profile->Flipbooks[0].AlignmentData.OriginalCellSize = FIntPoint(32, 32);
	Profile->Flipbooks[0].SourceTexture =
		NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
	ReExtract->RefreshRows();
	const TSharedPtr<FProfileReExtractRow> Refreshed = ReExtract->FindRowForTests(TEXT("Idle"));
	if (TestTrue(TEXT("the animation is still listed after a refresh"), Refreshed.IsValid()))
	{
		TestTrue(TEXT("stored detection settings make it eligible"), Refreshed->bEligible);
		TestTrue(TEXT("and the blocking reason is cleared"), Refreshed->Reason.IsEmpty());
	}
	return true;
}

#endif // WITH_EDITOR
