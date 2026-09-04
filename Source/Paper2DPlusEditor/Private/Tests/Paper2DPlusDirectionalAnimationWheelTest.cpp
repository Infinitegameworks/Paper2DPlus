// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "SDirectionalAnimationWheel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalWheelClockwiseScreenMathTest,
	"Paper2DPlus.DirectionalAnimation.Editor.WheelMath.ClockwiseScreenMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalWheelClockwiseScreenMathTest::RunTest(const FString& Parameters)
{
	const FVector2D Size(200.0, 200.0);
	TestEqual(TEXT("Up is slot zero"),
		SDirectionalAnimationWheel::HitTestSegment(FVector2D(100.0, 10.0), Size, 4, 0.0f), 0);
	TestEqual(TEXT("Right is the next clockwise slot despite Slate +Y pointing down"),
		SDirectionalAnimationWheel::HitTestSegment(FVector2D(190.0, 100.0), Size, 4, 0.0f), 1);
	TestEqual(TEXT("Down is the opposite slot"),
		SDirectionalAnimationWheel::HitTestSegment(FVector2D(100.0, 190.0), Size, 4, 0.0f), 2);
	TestEqual(TEXT("Left is the final clockwise slot"),
		SDirectionalAnimationWheel::HitTestSegment(FVector2D(10.0, 100.0), Size, 4, 0.0f), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalWheelSupportedCountsTest,
	"Paper2DPlus.DirectionalAnimation.Editor.WheelMath.ThreeAndSixteenSegments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalWheelSupportedCountsTest::RunTest(const FString& Parameters)
{
	const FVector2D Size(200.0, 200.0);
	TestEqual(TEXT("Three-way up is slot zero"),
		SDirectionalAnimationWheel::HitTestSegment(FVector2D(100.0, 20.0), Size, 3, 0.0f), 0);
	TestEqual(TEXT("Three-way lower right is slot one"),
		SDirectionalAnimationWheel::HitTestSegment(FVector2D(169.3, 140.0), Size, 3, 0.0f), 1);
	TestEqual(TEXT("Three-way lower left is slot two"),
		SDirectionalAnimationWheel::HitTestSegment(FVector2D(30.7, 140.0), Size, 3, 0.0f), 2);
	TestEqual(TEXT("Sixteen-way right is slot four"),
		SDirectionalAnimationWheel::HitTestSegment(FVector2D(180.0, 100.0), Size, 16, 0.0f), 4);
	TestEqual(TEXT("Sixteen-way down is slot eight"),
		SDirectionalAnimationWheel::HitTestSegment(FVector2D(100.0, 180.0), Size, 16, 0.0f), 8);
	TestEqual(TEXT("Sixteen-way left is slot twelve"),
		SDirectionalAnimationWheel::HitTestSegment(FVector2D(20.0, 100.0), Size, 16, 0.0f), 12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalWheelOffsetWrapTest,
	"Paper2DPlus.DirectionalAnimation.Editor.WheelMath.OffsetAndWrap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalWheelOffsetWrapTest::RunTest(const FString& Parameters)
{
	const FVector2D Size(200.0, 200.0);
	TestEqual(TEXT("Positive offset advances the upward bearing"),
		SDirectionalAnimationWheel::HitTestSegment(FVector2D(100.0, 20.0), Size, 8, 45.0f), 1);
	TestEqual(TEXT("Negative offset wraps the upward bearing"),
		SDirectionalAnimationWheel::HitTestSegment(FVector2D(100.0, 20.0), Size, 8, -45.0f), 7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalWheelBoundaryTest,
	"Paper2DPlus.DirectionalAnimation.Editor.WheelMath.HalfSectorBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalWheelBoundaryTest::RunTest(const FString& Parameters)
{
	const FVector2D Size(200.0, 200.0);
	TestEqual(TEXT("A bearing just before the first clockwise boundary stays in slot zero"),
		SDirectionalAnimationWheel::HitTestSegment(
			FVector2D(130.485630, 26.036317), Size, 8, 0.0f),
		0);
	TestEqual(TEXT("A bearing just after the first clockwise boundary advances to slot one"),
		SDirectionalAnimationWheel::HitTestSegment(
			FVector2D(130.743626, 26.143183), Size, 8, 0.0f),
		1);
	TestEqual(TEXT("The negative-side bearing before the wrap remains in the final slot"),
		SDirectionalAnimationWheel::HitTestSegment(
			FVector2D(69.256374, 26.143183), Size, 8, 0.0f),
		7);
	TestEqual(TEXT("The negative-side bearing after the wrap returns to slot zero"),
		SDirectionalAnimationWheel::HitTestSegment(
			FVector2D(69.514370, 26.036317), Size, 8, 0.0f),
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalWheelHitBoundsTest,
	"Paper2DPlus.DirectionalAnimation.Editor.WheelMath.DeadZoneAndBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalWheelHitBoundsTest::RunTest(const FString& Parameters)
{
	const FVector2D Size(200.0, 200.0);
	TestEqual(TEXT("The context center is not a direction segment"),
		SDirectionalAnimationWheel::HitTestSegment(FVector2D(100.0, 100.0), Size, 8, 0.0f),
		INDEX_NONE);
	TestEqual(TEXT("Outside the wheel is not a direction segment"),
		SDirectionalAnimationWheel::HitTestSegment(FVector2D(205.0, 100.0), Size, 8, 0.0f),
		INDEX_NONE);
	TestEqual(TEXT("Counts below the authored range fail closed"),
		SDirectionalAnimationWheel::HitTestSegment(FVector2D(100.0, 20.0), Size, 2, 0.0f),
		INDEX_NONE);
	TestEqual(TEXT("Counts above the authored range fail closed"),
		SDirectionalAnimationWheel::HitTestSegment(FVector2D(100.0, 20.0), Size, 17, 0.0f),
		INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalWheelKeyboardTraversalTest,
	"Paper2DPlus.DirectionalAnimation.Editor.WheelMath.KeyboardIndexTraversal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalWheelKeyboardTraversalTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Previous wraps from the first slot"),
		SDirectionalAnimationWheel::TraverseSlotIndex(0, -1, 3), 2);
	TestEqual(TEXT("Next wraps from the final slot"),
		SDirectionalAnimationWheel::TraverseSlotIndex(2, 1, 3), 0);
	TestEqual(TEXT("Large positive traversal wraps by authored count"),
		SDirectionalAnimationWheel::TraverseSlotIndex(14, 3, 16), 1);
	TestEqual(TEXT("Invalid current selection starts at slot zero"),
		SDirectionalAnimationWheel::TraverseSlotIndex(INDEX_NONE, 0, 8), 0);
	TestEqual(TEXT("Unsupported counts fail closed"),
		SDirectionalAnimationWheel::TraverseSlotIndex(0, 1, 17), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalWheelCommitCancelTest,
	"Paper2DPlus.DirectionalAnimation.Editor.WheelMath.CommitAndCancelAreTerminal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalWheelCommitCancelTest::RunTest(const FString& Parameters)
{
	int32 CommittedSlot = INDEX_NONE;
	int32 CommitCount = 0;
	int32 CancelAfterCommitCount = 0;
	TSharedRef<SDirectionalAnimationWheel> CommitWheel =
		SNew(SDirectionalAnimationWheel)
		.OnSlotCommitted_Lambda([&CommittedSlot, &CommitCount](int32 SlotIndex)
		{
			CommittedSlot = SlotIndex;
			++CommitCount;
		})
		.OnCancelled_Lambda([&CancelAfterCommitCount](bool bRestoreFocus)
		{
			(void)bRestoreFocus;
			++CancelAfterCommitCount;
		});
	TestTrue(TEXT("Enter is handled by the wheel"),
		CommitWheel->OnKeyDown(
			FGeometry(),
			FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0)).IsEventHandled());
	CommitWheel->OnKeyDown(
		FGeometry(),
		FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
	CommitWheel->OnKeyDown(
		FGeometry(),
		FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
	TestEqual(TEXT("Keyboard commit selects the first slot when no slot was preselected"),
		CommittedSlot, 0);
	TestEqual(TEXT("Commit is published only once"), CommitCount, 1);
	TestEqual(TEXT("Cancellation cannot follow a completed commit"), CancelAfterCommitCount, 0);

	int32 CancelCount = 0;
	int32 CommitAfterCancelCount = 0;
	bool bCancellationRequestedFocusRestore = false;
	TSharedRef<SDirectionalAnimationWheel> CancelWheel =
		SNew(SDirectionalAnimationWheel)
		.OnSlotCommitted_Lambda([&CommitAfterCancelCount](int32 SlotIndex)
		{
			(void)SlotIndex;
			++CommitAfterCancelCount;
		})
		.OnCancelled_Lambda([&CancelCount, &bCancellationRequestedFocusRestore](bool bRestoreFocus)
		{
			++CancelCount;
			bCancellationRequestedFocusRestore = bRestoreFocus;
		});
	const FModifierKeysState HeldAltModifier(
		false, false,
		false, false,
		true, false,
		false, false,
		false);
	CancelWheel->OnKeyDown(
		FGeometry(),
		FKeyEvent(EKeys::Escape, HeldAltModifier, 0, false, 0, 0));
	CancelWheel->OnKeyDown(
		FGeometry(),
		FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
	CancelWheel->OnKeyDown(
		FGeometry(),
		FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
	TestEqual(TEXT("Cancellation is published only once"), CancelCount, 1);
	TestTrue(TEXT("Escape cancels and restores focus while the opening modifier remains held"),
		bCancellationRequestedFocusRestore);
	TestEqual(TEXT("Commit cannot follow cancellation"), CommitAfterCancelCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalWheelInvalidDirectActionTest,
	"Paper2DPlus.DirectionalAnimation.Editor.WheelMath.InvalidDirectActionRefusesInPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalWheelInvalidDirectActionTest::RunTest(const FString& Parameters)
{
	const FGeometry Geometry = FGeometry::MakeRoot(
		FVector2D(200.0, 200.0), FSlateLayoutTransform());
	const auto MakeLeftPress = [](const FVector2D& ScreenPosition)
	{
		TSet<FKey> PressedButtons;
		PressedButtons.Add(EKeys::LeftMouseButton);
		return FPointerEvent(
			0,
			ScreenPosition,
			ScreenPosition,
			PressedButtons,
			EKeys::LeftMouseButton,
			0.0f,
			FModifierKeysState());
	};

	TArray<FPaper2DPlusDirectionalWheelSegment> SegmentStates;
	SegmentStates.SetNum(8);
	for (int32 SlotIndex = 0; SlotIndex < SegmentStates.Num(); ++SlotIndex)
	{
		SegmentStates[SlotIndex].SlotIndex = SlotIndex;
	}
	SegmentStates[0].bValid = false;

	int32 InvalidCommitCount = 0;
	int32 InvalidCancelCount = 0;
	bool bInvalidCancelRequestedFocusRestore = false;
	TSharedRef<SDirectionalAnimationWheel> InvalidSegmentWheel =
		SNew(SDirectionalAnimationWheel)
		.SegmentStates(SegmentStates)
		.OnSlotCommitted_Lambda([&InvalidCommitCount](int32 SlotIndex)
		{
			(void)SlotIndex;
			++InvalidCommitCount;
		})
		.OnCancelled_Lambda([
			&InvalidCancelCount,
			&bInvalidCancelRequestedFocusRestore](bool bRestoreFocus)
		{
			++InvalidCancelCount;
			bInvalidCancelRequestedFocusRestore = bRestoreFocus;
		});
	// Deliberate contract change (TASK-190 review): a click on a real but invalid wedge REFUSES IN
	// PLACE — the wheel stays open with its warning visible instead of silently eating the click.
	// Only outside-ring presses dismiss.
	const FReply InvalidReply = InvalidSegmentWheel->OnMouseButtonDown(
		Geometry, MakeLeftPress(FVector2D(100.0, 10.0)));
	TestTrue(TEXT("A direct press on an invalid segment is consumed"),
		InvalidReply.IsEventHandled());
	TestEqual(TEXT("An invalid direct press does not cancel the wheel"),
		InvalidCancelCount, 0);
	TestEqual(TEXT("An invalid segment never commits"), InvalidCommitCount, 0);
	InvalidSegmentWheel->OnMouseButtonDown(
		Geometry, MakeLeftPress(FVector2D(190.0, 100.0)));
	TestEqual(TEXT("The wheel stays live after an invalid refusal: a valid press still commits"),
		InvalidCommitCount, 1);
	TestEqual(TEXT("The valid commit resolves without a cancellation"),
		InvalidCancelCount, 0);
	InvalidSegmentWheel->OnKeyDown(
		Geometry,
		FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
	TestEqual(TEXT("Escape after the terminal commit cannot cancel"),
		InvalidCancelCount, 0);
	TestTrue(TEXT("A refusal keeps focus on the live wheel"),
		InvalidReply.ShouldSetUserFocus());

	// A refused wheel must remain fully live: Escape after the refusal still cancels normally.
	int32 RefusalCancelCount = 0;
	bool bRefusalCancelRequestedRestore = false;
	TSharedRef<SDirectionalAnimationWheel> RefusalWheel =
		SNew(SDirectionalAnimationWheel)
		.SegmentStates(SegmentStates)
		.OnCancelled_Lambda([
			&RefusalCancelCount,
			&bRefusalCancelRequestedRestore](bool bRestoreFocus)
		{
			++RefusalCancelCount;
			bRefusalCancelRequestedRestore = bRestoreFocus;
		});
	RefusalWheel->OnMouseButtonDown(Geometry, MakeLeftPress(FVector2D(100.0, 10.0)));
	TestEqual(TEXT("The refusal itself never cancels"), RefusalCancelCount, 0);
	RefusalWheel->OnKeyDown(
		Geometry,
		FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
	TestEqual(TEXT("Escape after a refusal still cancels exactly once"),
		RefusalCancelCount, 1);
	TestTrue(TEXT("Escape after a refusal restores opener focus"),
		bRefusalCancelRequestedRestore);

	int32 OutsideCancelCount = 0;
	TSharedRef<SDirectionalAnimationWheel> OutsideWheel =
		SNew(SDirectionalAnimationWheel)
		.OnCancelled_Lambda([&OutsideCancelCount](bool bRestoreFocus)
		{
			(void)bRestoreFocus;
			++OutsideCancelCount;
		});
	const FReply OutsideReply = OutsideWheel->OnMouseButtonDown(
		Geometry, MakeLeftPress(FVector2D(100.0, 100.0)));
	TestEqual(TEXT("A direct press in the center dead zone closes through cancellation"),
		OutsideCancelCount, 1);
	TestFalse(TEXT("Dead-zone cancellation never returns focus into the closing popup"),
		OutsideReply.ShouldSetUserFocus());
	OutsideWheel->OnMouseButtonDown(
		Geometry, MakeLeftPress(FVector2D(210.0, 100.0)));
	TestEqual(TEXT("A later out-of-bounds press cannot cancel a second time"),
		OutsideCancelCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalWheelHeldShortcutCommitTest,
	"Paper2DPlus.DirectionalAnimation.Editor.WheelMath.HeldShortcutCommitsOnlyOnReleaseSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalWheelHeldShortcutCommitTest::RunTest(const FString& Parameters)
{
	int32 CommittedSlot = INDEX_NONE;
	int32 CommitCount = 0;
	int32 CancelCount = 0;
	TArray<FPaper2DPlusDirectionalWheelSegment> SegmentStates;
	SegmentStates.SetNum(8);
	for (int32 SlotIndex = 0; SlotIndex < SegmentStates.Num(); ++SlotIndex)
	{
		SegmentStates[SlotIndex].SlotIndex = SlotIndex;
	}
	SegmentStates[2].bValid = false;
	TSharedRef<SDirectionalAnimationWheel> Wheel =
		SNew(SDirectionalAnimationWheel)
		.DirectCommitEnabled(false)
		.CurrentSelection(5)
		.SegmentStates(SegmentStates)
		.OnSlotCommitted_Lambda([&CommittedSlot, &CommitCount](int32 SlotIndex)
		{
			CommittedSlot = SlotIndex;
			++CommitCount;
		})
		.OnCancelled_Lambda([&CancelCount](bool bRestoreFocus)
		{
			(void)bRestoreFocus;
			++CancelCount;
		});
	TestFalse(TEXT("Release with no pointer or keyboard intent does not commit the current slot"),
		Wheel->CommitHeldSelection());
	TestEqual(TEXT("The no-intent release publishes nothing"), CommitCount, 0);

	const FGeometry Geometry = FGeometry::MakeRoot(
		FVector2D(200.0, 200.0), FSlateLayoutTransform());
	TSet<FKey> PressedButtons;
	PressedButtons.Add(EKeys::LeftMouseButton);
	const FPointerEvent PointAtRight(
		0,
		FVector2D(190.0, 100.0),
		FVector2D(190.0, 100.0),
		PressedButtons,
		EKeys::LeftMouseButton,
		0.0f,
		FModifierKeysState());

	Wheel->OnMouseMove(Geometry, PointAtRight);
	TestEqual(TEXT("Invalid pointer hover remains live in held-shortcut mode"),
		Wheel->GetHoveredSlot(), 2);
	const FReply HeldInvalidClickReply = Wheel->OnMouseButtonDown(Geometry, PointAtRight);
	TestTrue(TEXT("An invalid direct click is consumed in held-shortcut mode"),
		HeldInvalidClickReply.IsEventHandled());
	TestTrue(TEXT("The held click keeps the wheel focusable until the release seam resolves it"),
		HeldInvalidClickReply.ShouldSetUserFocus());
	Wheel->OnKeyDown(
		Geometry,
		FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
	Wheel->OnKeyDown(
		Geometry,
		FKeyEvent(EKeys::SpaceBar, FModifierKeysState(), 0, false, 0, 0));
	TestEqual(TEXT("Click and keyboard activation do not directly commit"), CommitCount, 0);
	TestEqual(TEXT("An invalid direct click does not race release-driven cancellation"),
		CancelCount, 0);

	const FPointerEvent PointAtDown(
		0,
		FVector2D(100.0, 190.0),
		FVector2D(100.0, 190.0),
		PressedButtons,
		EKeys::LeftMouseButton,
		0.0f,
		FModifierKeysState());
	Wheel->OnMouseMove(Geometry, PointAtDown);

	TestTrue(TEXT("The host key-up seam commits the hovered slot"), Wheel->CommitHeldSelection());
	TestEqual(TEXT("The release seam publishes the hovered physical bearing"), CommittedSlot, 4);
	TestEqual(TEXT("The release seam publishes exactly once"), CommitCount, 1);
	TestEqual(TEXT("A successful release commit never also cancels"), CancelCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalWheelMissingHostStateTest,
	"Paper2DPlus.DirectionalAnimation.Editor.WheelMath.SuppliedEmptyHostStateFailsClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalWheelMissingHostStateTest::RunTest(const FString& Parameters)
{
	int32 CommitCount = 0;
	const TArray<FPaper2DPlusDirectionalWheelSegment> MissingHostState;
	TSharedRef<SDirectionalAnimationWheel> Wheel =
		SNew(SDirectionalAnimationWheel)
		.SegmentStates(MissingHostState)
		.OnSlotCommitted_Lambda([&CommitCount](int32 SlotIndex)
		{
			(void)SlotIndex;
			++CommitCount;
		});
	Wheel->OnKeyDown(
		FGeometry(),
		FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
	TestEqual(TEXT("A host that cannot resolve owner segment state cannot commit a default wedge"),
		CommitCount, 0);
	TestTrue(TEXT("The accessible summary reports invalid supplied segments"),
		Wheel->GetAccessibleSummaryText().ToString().Contains(TEXT("8 invalid")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalWheelFocusPolicyTest,
	"Paper2DPlus.DirectionalAnimation.Editor.WheelMath.DirectCommitDoesNotRefocusDetachedPopup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalWheelFocusPolicyTest::RunTest(const FString& Parameters)
{
	int32 CommittedSlot = INDEX_NONE;
	int32 CommitCount = 0;
	int32 CancelAfterCommitCount = 0;
	TSharedRef<SDirectionalAnimationWheel> CommitWheel =
		SNew(SDirectionalAnimationWheel)
		.OnSlotCommitted_Lambda([&CommittedSlot, &CommitCount](int32 SlotIndex)
		{
			CommittedSlot = SlotIndex;
			++CommitCount;
		})
		.OnCancelled_Lambda([&CancelAfterCommitCount](bool bRestoreFocus)
		{
			(void)bRestoreFocus;
			++CancelAfterCommitCount;
		});
	const FGeometry Geometry = FGeometry::MakeRoot(
		FVector2D(200.0, 200.0), FSlateLayoutTransform());
	TSet<FKey> PressedButtons;
	PressedButtons.Add(EKeys::LeftMouseButton);
	const FPointerEvent ClickRight(
		0,
		FVector2D(190.0, 100.0),
		FVector2D(190.0, 100.0),
		PressedButtons,
		EKeys::LeftMouseButton,
		0.0f,
		FModifierKeysState());
	const FReply CommitReply = CommitWheel->OnMouseButtonDown(Geometry, ClickRight);
	TestEqual(TEXT("Direct pointer commit publishes the exact segment"), CommittedSlot, 2);
	TestEqual(TEXT("Direct pointer commit publishes exactly once"), CommitCount, 1);
	TestEqual(TEXT("A valid direct action never also cancels"), CancelAfterCommitCount, 0);
	TestFalse(TEXT("A synchronous commit never returns focus into the dismissing popup"),
		CommitReply.ShouldSetUserFocus());

	const FPointerEvent ClickCenter(
		0,
		FVector2D(100.0, 100.0),
		FVector2D(100.0, 100.0),
		PressedButtons,
		EKeys::LeftMouseButton,
		0.0f,
		FModifierKeysState());
	CommitWheel->OnMouseButtonDown(Geometry, ClickCenter);
	TestEqual(TEXT("A later invalid action cannot cancel after a valid commit"),
		CancelAfterCommitCount, 0);
	TestEqual(TEXT("A later action cannot commit a second time"), CommitCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalWheelSlotLabelTest,
	"Paper2DPlus.DirectionalAnimation.Editor.WheelMath.CompassSlotLabels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalWheelSlotLabelTest::RunTest(const FString& Parameters)
{
	// Compass-friendly topologies (zero offset, 4/8/16 slots) name every surface's slots the same
	// way: slot 0 up, clockwise. Everything else falls back to the bare index.
	TestEqual(TEXT("8-way slot 0 is N"),
		SDirectionalAnimationWheel::GetSlotDirectionLabel(0, 8, 0.0f), FString(TEXT("N")));
	TestEqual(TEXT("8-way slot 1 is NE"),
		SDirectionalAnimationWheel::GetSlotDirectionLabel(1, 8, 0.0f), FString(TEXT("NE")));
	TestEqual(TEXT("8-way slot 4 is S"),
		SDirectionalAnimationWheel::GetSlotDirectionLabel(4, 8, 0.0f), FString(TEXT("S")));
	TestEqual(TEXT("8-way slot 7 is NW"),
		SDirectionalAnimationWheel::GetSlotDirectionLabel(7, 8, 0.0f), FString(TEXT("NW")));
	TestEqual(TEXT("4-way slot 1 is E"),
		SDirectionalAnimationWheel::GetSlotDirectionLabel(1, 4, 0.0f), FString(TEXT("E")));
	TestEqual(TEXT("4-way slot 3 is W"),
		SDirectionalAnimationWheel::GetSlotDirectionLabel(3, 4, 0.0f), FString(TEXT("W")));
	TestEqual(TEXT("16-way slot 1 is NNE"),
		SDirectionalAnimationWheel::GetSlotDirectionLabel(1, 16, 0.0f), FString(TEXT("NNE")));
	TestEqual(TEXT("16-way slot 15 is NNW"),
		SDirectionalAnimationWheel::GetSlotDirectionLabel(15, 16, 0.0f), FString(TEXT("NNW")));
	TestEqual(TEXT("A nonzero offset falls back to the bare index"),
		SDirectionalAnimationWheel::GetSlotDirectionLabel(3, 8, 22.5f), FString(TEXT("3")));
	TestEqual(TEXT("A non-compass count falls back to the bare index"),
		SDirectionalAnimationWheel::GetSlotDirectionLabel(4, 5, 0.0f), FString(TEXT("4")));
	TestEqual(TEXT("An out-of-range slot falls back to the bare index"),
		SDirectionalAnimationWheel::GetSlotDirectionLabel(9, 8, 0.0f), FString(TEXT("9")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
