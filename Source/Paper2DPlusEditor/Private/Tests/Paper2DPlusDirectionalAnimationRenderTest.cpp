// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CharacterProfileAssetEditor.h"
#include "CharacterProfileEditorModel.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Framework/Commands/InputChord.h"
#include "InputCoreTypes.h"
#include "Misc/App.h"
#include "Misc/ScopeExit.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "SDirectionalAnimationCommandRouter.h"
#include "SDirectionalAnimationWheel.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"

namespace Paper2DPlusDirectionalAnimationRenderTestPrivate
{
	static bool IsInputProcessorRegistered(
		const FSlateApplication& SlateApp,
		const TSharedPtr<IInputProcessor>& Processor)
	{
		if (!Processor.IsValid())
		{
			return false;
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
		return SlateApp.FindInputPreProcessor(
			Processor, EInputPreProcessorType::Game) != INDEX_NONE;
#else
		return SlateApp.FindInputPreProcessor(Processor) != INDEX_NONE;
#endif
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationRoutedSlateTest,
	"Paper2DPlusRender.DirectionalAnimation.Editor.Wheel.RoutedFocusLayoutAndCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationRoutedSlateTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("The render lane must run without NullRHI"), FApp::CanEverRender())
		|| !TestTrue(TEXT("Slate is initialized"), FSlateApplication::IsInitialized()))
	{
		return false;
	}
	FSlateApplication& SlateApp = FSlateApplication::Get();
	if (!TestNotNull(TEXT("A real Slate renderer is available"), SlateApp.GetRenderer()))
	{
		return false;
	}

	const FInputChord HeldShortcut(EKeys::K, EModifierKey::Alt);
	const FInputChord NoSecondaryShortcut;
	const TSet<FKey> NoPressedButtons;
	const FModifierKeysState HeldShortcutModifiers(
		false, false,
		false, false,
		true, false,
		false, false,
		false);
	FKey CapturedTriggerKey;
	int32 ShortcutOpenCount = 0;
	int32 CommittedSlot = INDEX_NONE;
	int32 MixedIntentCommittedSlot = INDEX_NONE;
	int32 FocusLossCancelCount = 0;
	bool bFocusLossRequestedRestore = true;
	TArray<FPaper2DPlusDirectionalWheelSegment> ValidSegments;
	ValidSegments.SetNum(8);
	for (int32 SlotIndex = 0; SlotIndex < ValidSegments.Num(); ++SlotIndex)
	{
		ValidSegments[SlotIndex].SlotIndex = SlotIndex;
	}
	TArray<FPaper2DPlusDirectionalWheelSegment> MixedIntentSegments = ValidSegments;
	MixedIntentSegments[2].bValid = false;
	TSharedRef<SDirectionalAnimationWheel> Wheel =
		SNew(SDirectionalAnimationWheel)
		.DirectionCount(8)
		.AngleOffsetDegrees(0.0f)
		.SegmentStates(ValidSegments)
		.DirectCommitEnabled(false)
		.OnSlotCommitted_Lambda([&CommittedSlot](int32 SlotIndex)
		{
			CommittedSlot = SlotIndex;
		});
	TSharedRef<SDirectionalAnimationWheel> FocusLossWheel =
		SNew(SDirectionalAnimationWheel)
		.DirectionCount(8)
		.AngleOffsetDegrees(0.0f)
		.SegmentStates(ValidSegments)
		.DirectCommitEnabled(false)
		.OnCancelled_Lambda([&FocusLossCancelCount, &bFocusLossRequestedRestore](
			bool bRestoreFocus)
		{
			++FocusLossCancelCount;
			bFocusLossRequestedRestore = bRestoreFocus;
		});
	TSharedRef<SDirectionalAnimationWheel> MixedIntentWheel =
		SNew(SDirectionalAnimationWheel)
		.DirectionCount(8)
		.AngleOffsetDegrees(0.0f)
		.SegmentStates(MixedIntentSegments)
		.DirectCommitEnabled(false)
		.OnSlotCommitted_Lambda([&MixedIntentCommittedSlot](int32 SlotIndex)
		{
			MixedIntentCommittedSlot = SlotIndex;
		});
	TSharedPtr<SButton> ExternalFocusTarget;
	TSharedPtr<SWidgetSwitcher> RoutedContent;
	TSharedRef<SDirectionalAnimationCommandRouter> CommandRouter =
		SNew(SDirectionalAnimationCommandRouter)
		.OnDirectionalAnimationPreviewKeyDown_Lambda([
			&RoutedContent,
			&HeldShortcut,
			&NoSecondaryShortcut,
			&CapturedTriggerKey,
			&ShortcutOpenCount](const FKeyEvent& Event)
		{
			const FKey TriggerKey =
				FCharacterProfileAssetEditorToolkit::ResolveDirectionalWheelTriggerKey(
					HeldShortcut, NoSecondaryShortcut, Event);
			if (!TriggerKey.IsValid() || !RoutedContent.IsValid())
			{
				return false;
			}

			CapturedTriggerKey = TriggerKey;
			++ShortcutOpenCount;
			RoutedContent->SetActiveWidgetIndex(1);
			return true;
		})
		[
			SAssignNew(RoutedContent, SWidgetSwitcher)
			.WidgetIndex(0)
			+ SWidgetSwitcher::Slot()
			[
				SAssignNew(ExternalFocusTarget, SButton)
				.Text(FText::FromString(TEXT("Open direction wheel")))
			]
			+ SWidgetSwitcher::Slot()
			[
				Wheel
			]
		];
	TSharedRef<SWindow> Window =
		SNew(SWindow)
		.Title(FText::FromString(TEXT("Paper2DPlus Directional Render Proof")))
		.ClientSize(FVector2D(300.0f, 600.0f))
		.SizingRule(ESizingRule::FixedSize)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				CommandRouter
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				FocusLossWheel
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				MixedIntentWheel
			]
		];
	SlateApp.AddWindow(Window, /*bShowImmediately=*/true);
	ON_SCOPE_EXIT
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().RequestDestroyWindow(Window);
			FSlateApplication::Get().Tick();
		}
	};

	SlateApp.Tick();
	SlateApp.SetKeyboardFocus(FocusLossWheel, EFocusCause::SetDirectly);
	TestTrue(TEXT("The focus-loss fixture first owns keyboard focus"),
		FocusLossWheel->HasKeyboardFocus());
	SlateApp.SetKeyboardFocus(ExternalFocusTarget, EFocusCause::SetDirectly);
	TestEqual(TEXT("Moving focus outside the wheel cancels exactly once"),
		FocusLossCancelCount, 1);
	TestTrue(TEXT("Focus-transition cancellation requests deferred opener restoration"),
		bFocusLossRequestedRestore);
	SlateApp.ReleaseAllPointerCapture();
	TestFalse(TEXT("The resolved focus-loss fixture releases its held pointer capture"),
		FocusLossWheel->HasMouseCapture());
	SlateApp.Tick();
	TestEqual(TEXT("Resolved focus loss cannot schedule a second cancellation on capture release"),
		FocusLossCancelCount, 1);

	SlateApp.SetKeyboardFocus(ExternalFocusTarget, EFocusCause::SetDirectly);
	TestTrue(TEXT("The shortcut opener receives keyboard focus"),
		ExternalFocusTarget->HasKeyboardFocus());
	TestTrue(TEXT("A non-default remapped shortcut opens through the real tunnel route"),
		SlateApp.ProcessKeyDownEvent(FKeyEvent(
			EKeys::K,
			HeldShortcutModifiers,
			0,
			false,
			0,
			0)));
	TestEqual(TEXT("The tunnel command opens the wheel exactly once"), ShortcutOpenCount, 1);
	TestEqual(TEXT("The held interaction captures only the opening chord's primary key"),
		CapturedTriggerKey, EKeys::K);
	SlateApp.Tick();
	Wheel->SlatePrepass();
	const FVector2D WheelSize = Wheel->GetCachedGeometry().GetLocalSize();
	TestTrue(TEXT("The real window lays out the opened radial wheel at a paintable size"),
		WheelSize.X > 0.0f && WheelSize.Y > 0.0f);
	SlateApp.SetKeyboardFocus(Wheel, EFocusCause::SetDirectly);
	TestTrue(TEXT("Opening the wheel grants it keyboard focus"),
		Wheel->HasKeyboardFocus());

	const bool bRouted = SlateApp.ProcessKeyDownEvent(FKeyEvent(
		EKeys::Right,
		HeldShortcutModifiers,
		0,
		false,
		0,
		0));
	TestTrue(TEXT("Slate routes Right while the opening modifier remains held"), bRouted);
	TestTrue(TEXT("Keyboard navigation focuses a real segment control"),
		Wheel->HasFocusedDescendants());
	TestTrue(TEXT("The held wheel commits its keyboard-focused segment"),
		Wheel->CommitHeldSelection());
	TestEqual(TEXT("Clockwise keyboard navigation commits the next slot"),
		CommittedSlot, 1);

	SlateApp.SetKeyboardFocus(MixedIntentWheel, EFocusCause::SetDirectly);
	TestTrue(TEXT("The mixed-input fixture receives keyboard focus"),
		MixedIntentWheel->HasKeyboardFocus());
	TestTrue(TEXT("Keyboard navigation focuses a valid segment before pointer motion"),
		SlateApp.ProcessKeyDownEvent(FKeyEvent(
			EKeys::Right,
			HeldShortcutModifiers,
			0,
			false,
			0,
			0)));
	TestTrue(TEXT("The mixed-input fixture has a keyboard-focused segment"),
		MixedIntentWheel->HasFocusedDescendants());

	const FGeometry MixedGeometry = MixedIntentWheel->GetCachedGeometry();
	const FVector2D MixedLocalSize = MixedGeometry.GetLocalSize();
	const double MixedRingRadius = FMath::Min(MixedLocalSize.X, MixedLocalSize.Y) * 0.40;
	const FVector2D InvalidRightPoint = MixedGeometry.LocalToAbsolute(
		MixedLocalSize * 0.5 + FVector2D(MixedRingRadius, 0.0));
	const FPointerEvent PointAtInvalidRight(
		0,
		FSlateApplication::CursorPointerIndex,
		InvalidRightPoint,
		InvalidRightPoint,
		NoPressedButtons,
		FKey(),
		0.0f,
		HeldShortcutModifiers);
	MixedIntentWheel->OnMouseMove(MixedGeometry, PointAtInvalidRight);
	TestEqual(TEXT("Pointer motion selects the invalid right segment after keyboard focus"),
		MixedIntentWheel->GetHoveredSlot(), 2);

	TestFalse(TEXT("An invalid pointer hover overrides stale valid keyboard focus"),
		MixedIntentWheel->CommitHeldSelection());
	TestEqual(TEXT("The mixed-input release commits no stale keyboard direction"),
		MixedIntentCommittedSlot, INDEX_NONE);
	SlateApp.ReleaseAllPointerCapture();

	TStrongObjectPtr<UPaper2DPlusCharacterProfileAsset> ProcessorProfile(
		NewObject<UPaper2DPlusCharacterProfileAsset>());
	UPaperFlipbook* ProcessorBase =
		NewObject<UPaperFlipbook>(ProcessorProfile.Get(), TEXT("ProcessorBase"));
	UPaperFlipbook* IncompatibleVariant =
		NewObject<UPaperFlipbook>(ProcessorProfile.Get(), TEXT("ProcessorIncompatible"));
	{
		FScopedFlipbookMutator BaseMutator(ProcessorBase);
		BaseMutator.FramesPerSecond = 30.0f;
		FScopedFlipbookMutator VariantMutator(IncompatibleVariant);
		VariantMutator.FramesPerSecond = 12.0f;
	}
	FFlipbookProfileEntry& ProcessorEntry =
		ProcessorProfile->Flipbooks.AddDefaulted_GetRef();
	ProcessorEntry.Identity.FlipbookName = TEXT("ProcessorDirectional");
	ProcessorEntry.Identity.Flipbook = ProcessorBase;
	ProcessorEntry.DirectionalAnimationData.bHasDirectionalSet = true;
	ProcessorEntry.DirectionalAnimationData.bOverrideProfileSettings = true;
	ProcessorEntry.DirectionalAnimationData.DirectionCount = 3;
	FPaper2DPlusDirectionalAnimationSlot& InvalidSlot =
		ProcessorEntry.DirectionalAnimationData.Slots.AddDefaulted_GetRef();
	InvalidSlot.SlotIndex = 2;
	InvalidSlot.Flipbook = IncompatibleVariant;

	TSharedPtr<FCharacterProfileAssetEditorToolkit> Toolkit =
		MakeShared<FCharacterProfileAssetEditorToolkit>();
	Toolkit->InitEditor(EToolkitMode::Standalone, nullptr, ProcessorProfile.Get());
	ON_SCOPE_EXIT
	{
		if (Toolkit.IsValid())
		{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
			Toolkit->CloseWindow(EAssetEditorCloseReason::AssetUnloadingOrInvalid);
#else
			Toolkit->CloseWindow();
#endif
			Toolkit.Reset();
		}
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().Tick();
		}
	};
	SlateApp.Tick();
	int32 OutsideDismissClickCount = 0;
	TSharedPtr<SButton> OutsideDismissTarget;
	const FSlateRect PreferredWorkArea = SlateApp.GetPreferredWorkArea();
	const FVector2D OutsideDismissWindowSize(220.0f, 80.0f);
	const FVector2D OutsideDismissWindowPosition(
		FMath::Max<double>(
			PreferredWorkArea.Left,
			PreferredWorkArea.Right - OutsideDismissWindowSize.X - 24.0),
		FMath::Max<double>(
			PreferredWorkArea.Top,
			PreferredWorkArea.Bottom - OutsideDismissWindowSize.Y - 24.0));
	TSharedRef<SWindow> OutsideDismissWindow =
		SNew(SWindow)
		.Title(FText::FromString(TEXT("Paper2DPlus Outside Direction Wheel Target")))
		.ClientSize(OutsideDismissWindowSize)
		.ScreenPosition(OutsideDismissWindowPosition)
		.AutoCenter(EAutoCenter::None)
		.SizingRule(ESizingRule::FixedSize)
		.IsTopmostWindow(true)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SAssignNew(OutsideDismissTarget, SButton)
			.Text(FText::FromString(TEXT("Outside focus target")))
			.OnClicked_Lambda([&OutsideDismissClickCount]()
			{
				++OutsideDismissClickCount;
				return FReply::Handled();
			})
		];
	SlateApp.AddWindow(OutsideDismissWindow, /*bShowImmediately=*/true);
	ON_SCOPE_EXIT
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().RequestDestroyWindow(OutsideDismissWindow);
			FSlateApplication::Get().Tick();
		}
	};
	SlateApp.Tick();

	const TWeakPtr<SButton>* ToolkitDirectionButtonEntry = Toolkit->DirectionButtons.Find(
		FCharacterProfileAssetEditorToolkit::AnimationsTabId);
	const TSharedPtr<SButton> ToolkitDirectionButton =
		ToolkitDirectionButtonEntry ? ToolkitDirectionButtonEntry->Pin() : nullptr;
	const TSharedPtr<SWindow> ToolkitWindow = ToolkitDirectionButton.IsValid()
		? SlateApp.FindWidgetWindow(ToolkitDirectionButton.ToSharedRef())
		: nullptr;
	if (!TestTrue(TEXT("The production Direction opener belongs to a live toolkit window"),
		ToolkitWindow.IsValid() && ToolkitWindow != OutsideDismissWindow))
	{
		return false;
	}
	TestTrue(TEXT("The live toolkit first proves the separate button-opened path"),
		Toolkit->ActivateDirectionalHeaderForTests(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId));
	const TSharedPtr<SDirectionalAnimationWheel> ButtonWheel =
		Toolkit->ActiveDirectionWheel.Pin();
	TestTrue(TEXT("The button-opened wheel is live"), ButtonWheel.IsValid());
	if (ButtonWheel.IsValid())
	{
		TestFalse(TEXT("Button opening does not take held-shortcut pointer capture"),
			ButtonWheel->HasMouseCapture());
	}
	Toolkit->CancelDirectionalWheel(/*bDismissMenu=*/true, /*bRestoreFocus=*/false);
	SlateApp.SetKeyboardFocus(ToolkitDirectionButton, EFocusCause::SetDirectly);
	Toolkit->PendingDirectionWheelTriggerKeys = { EKeys::K };
	Toolkit->OpenDirectionalWheel(
		FCharacterProfileAssetEditorToolkit::AnimationsTabId,
		/*bShortcutHeld=*/true);
	const TSharedPtr<SDirectionalAnimationWheel> CrossWindowHeldWheel =
		Toolkit->ActiveDirectionWheel.Pin();
	const TSharedPtr<IInputProcessor> CrossWindowHeldProcessor =
		Toolkit->DirectionInputProcessor;
	TestTrue(TEXT("The production held wheel is live for cross-window dismissal"),
		CrossWindowHeldWheel.IsValid());
	TestTrue(TEXT("The production held wheel owns capture before window activation"),
		CrossWindowHeldWheel.IsValid() && CrossWindowHeldWheel->HasMouseCapture());
	TestTrue(TEXT("The held-wheel processor is registered before window activation"),
		Paper2DPlusDirectionalAnimationRenderTestPrivate::IsInputProcessorRegistered(
			SlateApp, CrossWindowHeldProcessor));
	if (!TestTrue(TEXT("The outside-click target is focusable"),
		OutsideDismissTarget.IsValid() && OutsideDismissTarget->SupportsKeyboardFocus()))
	{
		return false;
	}
	const FGeometry ExternalTargetGeometry = OutsideDismissTarget->GetCachedGeometry();
	const FVector2D ExternalTargetSize = ExternalTargetGeometry.GetLocalSize();
	if (!TestTrue(TEXT("The outside-click target has routed Slate geometry"),
		ExternalTargetSize.X > 0.0f && ExternalTargetSize.Y > 0.0f))
	{
		return false;
	}
	const FVector2D ExternalTargetPoint = ExternalTargetGeometry.LocalToAbsolute(
		ExternalTargetSize * 0.5f);
	const FVector2D OriginalCursorPosition = SlateApp.GetCursorPos();
	SlateApp.SetCursorPos(ExternalTargetPoint);
	const FWidgetPath ExternalTargetPath = SlateApp.LocateWindowUnderMouse(
		ExternalTargetPoint,
		SlateApp.GetInteractiveTopLevelWindows(),
		/*bIgnoreEnabledStatus=*/false,
		/*UserIndex=*/0);
	if (!TestTrue(TEXT("Slate resolves the intended external control before popup dismissal"),
		ExternalTargetPath.IsValid()
		&& ExternalTargetPath.ContainsWidget(OutsideDismissTarget.Get())))
	{
		SlateApp.SetCursorPos(OriginalCursorPosition);
		return false;
	}
	const FPointerEvent ExternalMove(
		0,
		FSlateApplication::CursorPointerIndex,
		ExternalTargetPoint,
		ExternalTargetPoint + FVector2D(1.0f, 0.0f),
		NoPressedButtons,
		FKey(),
		0.0f,
		FModifierKeysState());
	SlateApp.ProcessMouseMoveEvent(ExternalMove);
	TestTrue(TEXT("Pointer hover alone leaves the production popup open"),
		Toolkit->IsDirectionalWheelOpenForTests());
	(void)SlateApp.ProcessWindowActivatedEvent(FWindowActivateEvent(
		FWindowActivateEvent::EA_ActivateByMouse,
		OutsideDismissWindow));
	TestFalse(TEXT("Activating the clicked Slate window dismisses the captured held wheel"),
		Toolkit->IsDirectionalWheelOpenForTests());
	TestFalse(TEXT("Cross-window dismissal releases the exact held-wheel capture"),
		CrossWindowHeldWheel.IsValid() && CrossWindowHeldWheel->HasMouseCapture());
	TestFalse(TEXT("Cross-window dismissal unregisters the exact held-wheel processor"),
		Paper2DPlusDirectionalAnimationRenderTestPrivate::IsInputProcessorRegistered(
			SlateApp, CrossWindowHeldProcessor));
	TestFalse(TEXT("Cross-window dismissal clears the toolkit processor owner"),
		Toolkit->DirectionInputProcessor.IsValid());
	SlateApp.SetKeyboardFocus(ToolkitDirectionButton, EFocusCause::SetDirectly);
	Toolkit->OpenDirectionalWheel(
		FCharacterProfileAssetEditorToolkit::AnimationsTabId,
		/*bShortcutHeld=*/false);
	const TSharedPtr<SDirectionalAnimationWheel> ReplacementWheel =
		Toolkit->ActiveDirectionWheel.Pin();
	TestTrue(TEXT("A replacement wheel can open before the dismissed wheel's deferred callback"),
		Toolkit->IsDirectionalWheelOpenForTests() && ReplacementWheel.IsValid());
	TestTrue(TEXT("The replacement wheel is distinct from the externally dismissed held wheel"),
		ReplacementWheel.IsValid() && ReplacementWheel != CrossWindowHeldWheel);
	SlateApp.Tick();
	TestTrue(TEXT("Host teardown disarms the stale capture-loss timer before it can cancel a replacement"),
		Toolkit->IsDirectionalWheelOpenForTests()
		&& Toolkit->ActiveDirectionWheel.Pin() == ReplacementWheel);
	Toolkit->CancelDirectionalWheel(/*bDismissMenu=*/true, /*bRestoreFocus=*/false);
	TestFalse(TEXT("The replacement wheel closes through explicit host cleanup"),
		Toolkit->IsDirectionalWheelOpenForTests());
	(void)SlateApp.ProcessWindowActivatedEvent(FWindowActivateEvent(
		FWindowActivateEvent::EA_ActivateByMouse,
		OutsideDismissWindow));
	const FWidgetPath ActivatedExternalTargetPath = SlateApp.LocateWindowUnderMouse(
		ExternalTargetPoint,
		SlateApp.GetInteractiveTopLevelWindows(),
		/*bIgnoreEnabledStatus=*/false,
		/*UserIndex=*/0);
	if (!TestTrue(TEXT("Slate still resolves the intended control after cross-window activation"),
		ActivatedExternalTargetPath.IsValid()
		&& ActivatedExternalTargetPath.ContainsWidget(OutsideDismissTarget.Get())))
	{
		SlateApp.SetCursorPos(OriginalCursorPosition);
		return false;
	}
	SlateApp.ProcessMouseMoveEvent(ExternalMove);
	TestTrue(TEXT("The external target receives hover after held capture is released"),
		OutsideDismissTarget->IsHovered());
	if (!TestTrue(TEXT("The external Slate window has a native platform window"),
		OutsideDismissWindow->GetNativeWindow().IsValid()))
	{
		SlateApp.SetCursorPos(OriginalCursorPosition);
		return false;
	}
	TSet<FKey> ExternalClickPressedButtons;
	ExternalClickPressedButtons.Add(EKeys::LeftMouseButton);
	const FPointerEvent ExternalClickDown(
		0,
		FSlateApplication::CursorPointerIndex,
		ExternalTargetPoint,
		ExternalTargetPoint,
		ExternalClickPressedButtons,
		EKeys::LeftMouseButton,
		0.0f,
		FModifierKeysState());
	(void)SlateApp.ProcessMouseButtonDownEvent(
		OutsideDismissWindow->GetNativeWindow(),
		ExternalClickDown);
	TestTrue(TEXT("The routed external press gives its target mouse capture"),
		OutsideDismissTarget->HasMouseCapture());
	TestTrue(TEXT("The routed external press focuses its target before release"),
		OutsideDismissTarget->HasKeyboardFocus());
	const FPointerEvent ExternalClickUp(
		0,
		FSlateApplication::CursorPointerIndex,
		ExternalTargetPoint,
		ExternalTargetPoint,
		NoPressedButtons,
		EKeys::LeftMouseButton,
		0.0f,
		FModifierKeysState());
	TestTrue(TEXT("Slate routes the matching outside release onto the focusable control"),
		SlateApp.ProcessMouseButtonUpEvent(ExternalClickUp));
	SlateApp.SetCursorPos(OriginalCursorPosition);
	TestEqual(TEXT("The real outside click activates its intended control exactly once"),
		OutsideDismissClickCount, 1);
	TestFalse(TEXT("The real outside click leaves the held wheel dismissed"),
		Toolkit->IsDirectionalWheelOpenForTests());
	TestTrue(TEXT("The clicked control receives focus during the routed interaction"),
		OutsideDismissTarget->HasKeyboardFocus());
	for (int32 FocusAttempt = 0; FocusAttempt < 3; ++FocusAttempt)
	{
		SlateApp.Tick();
	}
	TestTrue(TEXT("External dismissal never steals focus back from the clicked control"),
		OutsideDismissTarget->HasKeyboardFocus());
	int32 SameWindowDismissClickCount = 0;
	TSharedPtr<SButton> SameWindowDismissTarget;
	ToolkitWindow->AddOverlaySlot(1000)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(24.0f))
		[
			SAssignNew(SameWindowDismissTarget, SButton)
			.Text(FText::FromString(TEXT("Same-window focus target")))
			.OnClicked_Lambda([&SameWindowDismissClickCount]()
			{
				++SameWindowDismissClickCount;
				return FReply::Handled();
			})
		];
	ON_SCOPE_EXIT
	{
		if (ToolkitWindow.IsValid() && SameWindowDismissTarget.IsValid())
		{
			ToolkitWindow->RemoveOverlaySlot(SameWindowDismissTarget.ToSharedRef());
		}
	};
	SlateApp.Tick();
	ToolkitWindow->SlatePrepass();
	(void)SlateApp.ProcessWindowActivatedEvent(FWindowActivateEvent(
		FWindowActivateEvent::EA_ActivateByMouse,
		ToolkitWindow.ToSharedRef()));
	TestTrue(TEXT("Returning to the editor reactivates the production toolkit window"),
		SlateApp.GetActiveTopLevelWindow() == ToolkitWindow);
	TestFalse(TEXT("The externally dismissed wheel stays closed before the next editor click"),
		Toolkit->IsDirectionalWheelOpenForTests());
	const FGeometry SameWindowTargetGeometry = SameWindowDismissTarget->GetCachedGeometry();
	const FVector2D SameWindowTargetSize = SameWindowTargetGeometry.GetLocalSize();
	if (!TestTrue(TEXT("The same-window target has routed Slate geometry"),
		SameWindowTargetSize.X > 0.0f && SameWindowTargetSize.Y > 0.0f))
	{
		return false;
	}
	const FVector2D SameWindowTargetPoint = SameWindowTargetGeometry.LocalToAbsolute(
		SameWindowTargetSize * 0.5f);
	const FVector2D CursorBeforeSameWindowClick = SlateApp.GetCursorPos();
	SlateApp.SetCursorPos(SameWindowTargetPoint);
	const FWidgetPath SameWindowTargetPath = SlateApp.LocateWindowUnderMouse(
		SameWindowTargetPoint,
		SlateApp.GetInteractiveTopLevelWindows(),
		/*bIgnoreEnabledStatus=*/false,
		/*UserIndex=*/0);
	if (!TestTrue(TEXT("Slate resolves the same-window follow-up target"),
		SameWindowTargetPath.IsValid()
		&& SameWindowTargetPath.ContainsWidget(SameWindowDismissTarget.Get())))
	{
		SlateApp.SetCursorPos(CursorBeforeSameWindowClick);
		return false;
	}
	const FPointerEvent SameWindowMove(
		0,
		FSlateApplication::CursorPointerIndex,
		SameWindowTargetPoint,
		SameWindowTargetPoint + FVector2D(1.0f, 0.0f),
		NoPressedButtons,
		FKey(),
		0.0f,
		FModifierKeysState());
	SlateApp.ProcessMouseMoveEvent(SameWindowMove);
	TestTrue(TEXT("Slate hover reaches the same-window follow-up target"),
		SameWindowDismissTarget->IsHovered());
	const FPointerEvent SameWindowClickDown(
		0,
		FSlateApplication::CursorPointerIndex,
		SameWindowTargetPoint,
		SameWindowTargetPoint,
		ExternalClickPressedButtons,
		EKeys::LeftMouseButton,
		0.0f,
		FModifierKeysState());
	(void)SlateApp.ProcessMouseButtonDownEvent(
		ToolkitWindow->GetNativeWindow(),
		SameWindowClickDown);
	TestTrue(TEXT("The same-window outside press gives its target mouse capture"),
		SameWindowDismissTarget->HasMouseCapture());
	TestTrue(TEXT("The same-window outside press focuses its target"),
		SameWindowDismissTarget->HasKeyboardFocus());
	const FPointerEvent SameWindowClickUp(
		0,
		FSlateApplication::CursorPointerIndex,
		SameWindowTargetPoint,
		SameWindowTargetPoint,
		NoPressedButtons,
		EKeys::LeftMouseButton,
		0.0f,
		FModifierKeysState());
	TestTrue(TEXT("Slate routes the same-window outside release"),
		SlateApp.ProcessMouseButtonUpEvent(SameWindowClickUp));
	SlateApp.SetCursorPos(CursorBeforeSameWindowClick);
	TestEqual(TEXT("The same-window outside click activates its target exactly once"),
		SameWindowDismissClickCount, 1);
	for (int32 FocusAttempt = 0; FocusAttempt < 3; ++FocusAttempt)
	{
		SlateApp.Tick();
	}
	TestTrue(TEXT("External dismissal leaves no pending opener focus over the next editor click"),
		SameWindowDismissTarget->HasKeyboardFocus());

	TestTrue(TEXT("The live toolkit opens a button wheel for external menu-policy proof"),
		Toolkit->ActivateDirectionalHeaderForTests(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId));
	const TSharedPtr<IMenu> ExternallyDismissedPolicyMenu = Toolkit->ActiveDirectionMenu;
	TestTrue(TEXT("The external menu-policy fixture owns a live menu"),
		ExternallyDismissedPolicyMenu.IsValid());
	if (ExternallyDismissedPolicyMenu.IsValid())
	{
		ExternallyDismissedPolicyMenu->Dismiss();
	}
	TestFalse(TEXT("External menu dismissal closes its button wheel"),
		Toolkit->IsDirectionalWheelOpenForTests());
	SlateApp.SetKeyboardFocus(SameWindowDismissTarget, EFocusCause::SetDirectly);
	TestTrue(TEXT("A post-dismissal control establishes the next focus target"),
		SameWindowDismissTarget->HasKeyboardFocus());
	for (int32 FocusAttempt = 0; FocusAttempt < 3; ++FocusAttempt)
	{
		SlateApp.Tick();
	}
	TestTrue(TEXT("External menu dismissal never schedules opener focus over the next target"),
		SameWindowDismissTarget->HasKeyboardFocus());

	SlateApp.SetKeyboardFocus(ToolkitDirectionButton, EFocusCause::SetDirectly);
	Toolkit->PendingDirectionWheelTriggerKeys = { EKeys::K };
	Toolkit->OpenDirectionalWheel(
		FCharacterProfileAssetEditorToolkit::AnimationsTabId,
		/*bShortcutHeld=*/true);
	const TSharedPtr<SDirectionalAnimationWheel> PendingHostTeardownWheel =
		Toolkit->ActiveDirectionWheel.Pin();
	const TSharedPtr<IInputProcessor> PendingHostTeardownProcessor =
		Toolkit->DirectionInputProcessor;
	if (!TestTrue(TEXT("The pending-host fixture starts with a captured held wheel"),
		Toolkit->IsDirectionalWheelOpenForTests()
		&& PendingHostTeardownWheel.IsValid()
		&& PendingHostTeardownWheel->HasMouseCapture()))
	{
		return false;
	}
	SlateApp.ReleaseAllPointerCapture();
	TestTrue(TEXT("Capture loss remains pending until the next Slate turn"),
		Toolkit->IsDirectionalWheelOpenForTests());
	TestFalse(TEXT("The pending-host fixture has released its exact capture"),
		PendingHostTeardownWheel.IsValid() && PendingHostTeardownWheel->HasMouseCapture());
	Toolkit->CancelDirectionalWheel(/*bDismissMenu=*/true, /*bRestoreFocus=*/false);
	TestFalse(TEXT("Host teardown resolves the pending-capture wheel"),
		Toolkit->IsDirectionalWheelOpenForTests());
	TestFalse(TEXT("Host teardown unregisters the pending-capture processor"),
		Paper2DPlusDirectionalAnimationRenderTestPrivate::IsInputProcessorRegistered(
			SlateApp, PendingHostTeardownProcessor));
	TestTrue(TEXT("The resolved pending-capture wheel retains its one-shot timer until Slate ticks"),
		PendingHostTeardownWheel->HasActiveTimers());
	{
		// Keep the detached wheel in a separate live window for one paint. This models the real
		// dismissal turn without letting its timer execute from the replacement popup's widget tree.
		Window->AddOverlaySlot(1000)
		[
			PendingHostTeardownWheel.ToSharedRef()
		];
		ON_SCOPE_EXIT
		{
			Window->RemoveOverlaySlot(PendingHostTeardownWheel.ToSharedRef());
		};
		Window->SlatePrepass();
		SlateApp.SetKeyboardFocus(ToolkitDirectionButton, EFocusCause::SetDirectly);
		Toolkit->OpenDirectionalWheel(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId,
			/*bShortcutHeld=*/false);
		const TSharedPtr<SDirectionalAnimationWheel> PendingHostReplacementWheel =
			Toolkit->ActiveDirectionWheel.Pin();
		TestTrue(TEXT("A replacement opens before the pending capture-loss timer can run"),
			Toolkit->IsDirectionalWheelOpenForTests()
			&& PendingHostReplacementWheel.IsValid()
			&& PendingHostReplacementWheel != PendingHostTeardownWheel);
		SlateApp.Tick();
		TestFalse(TEXT("The resolved pending-capture timer stops after its live Slate turn"),
			PendingHostTeardownWheel->HasActiveTimers());
		TestTrue(TEXT("Host teardown disarms pending capture loss before it can cancel a replacement"),
			Toolkit->IsDirectionalWheelOpenForTests()
			&& Toolkit->ActiveDirectionWheel.Pin() == PendingHostReplacementWheel);
		Toolkit->CancelDirectionalWheel(/*bDismissMenu=*/true, /*bRestoreFocus=*/false);
	}

	SlateApp.SetKeyboardFocus(ToolkitDirectionButton, EFocusCause::SetDirectly);
	Toolkit->PendingDirectionWheelTriggerKeys = { EKeys::K };
	Toolkit->OpenDirectionalWheel(
		FCharacterProfileAssetEditorToolkit::AnimationsTabId,
		/*bShortcutHeld=*/true);
	const TSharedPtr<SDirectionalAnimationWheel> HostCancelledHeldWheel =
		Toolkit->ActiveDirectionWheel.Pin();
	const TSharedPtr<IInputProcessor> HostCancelledHeldProcessor =
		Toolkit->DirectionInputProcessor;
	TestTrue(TEXT("Host-owned cancellation starts with an unresolved held wheel"),
		Toolkit->IsDirectionalWheelOpenForTests()
		&& HostCancelledHeldWheel.IsValid()
		&& HostCancelledHeldWheel->HasMouseCapture());
	TestTrue(TEXT("Host-owned cancellation starts with its exact processor registered"),
		Paper2DPlusDirectionalAnimationRenderTestPrivate::IsInputProcessorRegistered(
			SlateApp, HostCancelledHeldProcessor));
	Toolkit->CancelDirectionalWheel(/*bDismissMenu=*/true, /*bRestoreFocus=*/true);
	TestFalse(TEXT("Host-owned cancellation closes the unresolved held wheel"),
		Toolkit->IsDirectionalWheelOpenForTests());
	TestFalse(TEXT("Host-owned cancellation releases the unresolved wheel's capture"),
		HostCancelledHeldWheel.IsValid() && HostCancelledHeldWheel->HasMouseCapture());
	TestFalse(TEXT("Host-owned cancellation unregisters the unresolved wheel's processor"),
		Paper2DPlusDirectionalAnimationRenderTestPrivate::IsInputProcessorRegistered(
			SlateApp, HostCancelledHeldProcessor));
	TestFalse(TEXT("Host-owned cancellation clears the unresolved wheel's processor owner"),
		Toolkit->DirectionInputProcessor.IsValid());
	SlateApp.SetKeyboardFocus(SameWindowDismissTarget, EFocusCause::SetDirectly);
	TestTrue(TEXT("A competing control owns focus before host restoration timers run"),
		SameWindowDismissTarget->HasKeyboardFocus());
	SlateApp.Tick();
	SlateApp.SetKeyboardFocus(SameWindowDismissTarget, EFocusCause::SetDirectly);
	TestTrue(TEXT("A competing control can steal focus during the bounded restore window"),
		SameWindowDismissTarget->HasKeyboardFocus());
	SlateApp.Tick();
	SlateApp.Tick();
	TestTrue(TEXT("Host-owned held cancellation preserves its deferred opener restoration"),
		Toolkit->IsDirectionalHeaderFocusedForTests(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId));

	TestTrue(TEXT("The live toolkit reopens the button path for explicit cancellation"),
		Toolkit->ActivateDirectionalHeaderForTests(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId));
	Toolkit->DismissDirectionalWheelForTests();
	for (int32 FocusAttempt = 0;
		FocusAttempt < 3 && !Toolkit->IsDirectionalHeaderFocusedForTests(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId);
		++FocusAttempt)
	{
		SlateApp.Tick();
	}
	TestTrue(TEXT("Button dismissal restores the real Direction opener before remapped input"),
		Toolkit->IsDirectionalHeaderFocusedForTests(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId));

	const FModifierKeysState RemappedModifiers(
		false, false,
		false, false,
		true, false,
		false, false,
		false);
	const FKeyEvent RemappedKeyRepeat(
		EKeys::K, RemappedModifiers, 0, true, 0, 0);
	const FKeyEvent RemappedKeyUp(
		EKeys::K, FModifierKeysState(), 0, false, 0, 0);
	auto OpenProductionHeldWheel = [&Toolkit]()
	{
		// Feed the key resolved by the tunnel route directly into the live toolkit opener. This
		// exercises the production input processor without mutating the editor's global keybinding
		// preferences, and K deliberately proves that the processor does not assume the default key.
		Toolkit->PendingDirectionWheelTriggerKeys = { EKeys::K };
		Toolkit->OpenDirectionalWheel(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId,
			/*bShortcutHeld=*/true);
		return Toolkit->IsDirectionalWheelOpenForTests();
	};

	TestTrue(TEXT("The live toolkit opens a 3-way wheel from a captured remapped key"),
		OpenProductionHeldWheel());
	TestTrue(TEXT("The 3-way held wheel is open"), Toolkit->IsDirectionalWheelOpenForTests());
	TSharedPtr<SDirectionalAnimationWheel> ProductionWheel =
		Toolkit->ActiveDirectionWheel.Pin();
	TSharedPtr<IInputProcessor> RegisteredProcessor = Toolkit->DirectionInputProcessor;
	TestTrue(TEXT("The production held wheel owns pointer capture"),
		ProductionWheel.IsValid() && ProductionWheel->HasMouseCapture());
	TestTrue(TEXT("The toolkit-scoped production processor is registered with Slate"),
		Paper2DPlusDirectionalAnimationRenderTestPrivate::IsInputProcessorRegistered(
			SlateApp, RegisteredProcessor));
	TestTrue(TEXT("The production processor consumes repeats of the captured remapped key"),
		SlateApp.ProcessKeyDownEvent(RemappedKeyRepeat));

	TestFalse(TEXT("Releasing Alt is not claimed as a directional commit"),
		SlateApp.ProcessKeyUpEvent(FKeyEvent(
			EKeys::LeftAlt, FModifierKeysState(), 0, false, 0, 0)));
	TestTrue(TEXT("Modifier release leaves the held wheel and processor armed"),
		Toolkit->IsDirectionalWheelOpenForTests()
		&& Paper2DPlusDirectionalAnimationRenderTestPrivate::IsInputProcessorRegistered(
			SlateApp, RegisteredProcessor));
	TestFalse(TEXT("The retired default D key is not the captured remapped release"),
		SlateApp.ProcessKeyUpEvent(FKeyEvent(
			EKeys::D, FModifierKeysState(), 0, false, 0, 0)));
	TestTrue(TEXT("A non-captured primary key leaves the held wheel open"),
		Toolkit->IsDirectionalWheelOpenForTests());
	TestTrue(TEXT("Right traverses the narrow 3-way production wheel"),
		SlateApp.ProcessKeyDownEvent(FKeyEvent(
			EKeys::Right, FModifierKeysState(), 0, false, 0, 0)));
	TestTrue(TEXT("Captured remapped key-up commits through the production processor"),
		SlateApp.ProcessKeyUpEvent(RemappedKeyUp));
	TestEqual(TEXT("The 3-way production path commits clockwise slot 1"),
		Toolkit->EditorModel->GetDirectionalPreview().SlotIndex, 1);
	TestFalse(TEXT("Successful commit closes the held wheel"),
		Toolkit->IsDirectionalWheelOpenForTests());
	TestFalse(TEXT("Successful commit clears the toolkit processor owner"),
		Toolkit->DirectionInputProcessor.IsValid());
	TestFalse(TEXT("Successful commit unregisters the exact production processor"),
		Paper2DPlusDirectionalAnimationRenderTestPrivate::IsInputProcessorRegistered(
			SlateApp, RegisteredProcessor));
	TestFalse(TEXT("Successful commit releases the held wheel pointer capture"),
		ProductionWheel.IsValid() && ProductionWheel->HasMouseCapture());
	TestFalse(TEXT("A second captured-key release is unhandled after unregister"),
		SlateApp.ProcessKeyUpEvent(RemappedKeyUp));

	Toolkit->EditorModel->SetCommittedDirectionalBearing(0.0);
	for (int32 FocusAttempt = 0;
		FocusAttempt < 3 && !Toolkit->IsDirectionalHeaderFocusedForTests(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId);
		++FocusAttempt)
	{
		SlateApp.Tick();
	}
	TestTrue(TEXT("The captured remapped key reopens for mixed pointer and keyboard intent"),
		OpenProductionHeldWheel());
	ProductionWheel = Toolkit->ActiveDirectionWheel.Pin();
	RegisteredProcessor = Toolkit->DirectionInputProcessor;
	if (!TestTrue(TEXT("The mixed-input production wheel is live"), ProductionWheel.IsValid()))
	{
		return false;
	}
	for (int32 ReadyAttempt = 0; ReadyAttempt < 3; ++ReadyAttempt)
	{
		SlateApp.Tick();
		ProductionWheel->SlatePrepass();
		const FVector2D ReadySize = ProductionWheel->GetCachedGeometry().GetLocalSize();
		if (ProductionWheel->HasMouseCapture()
			&& ReadySize.X > 0.0f
			&& ReadySize.Y > 0.0f)
		{
			break;
		}
	}
	TestTrue(TEXT("The reopened mixed-input wheel owns routed pointer capture"),
		ProductionWheel->HasMouseCapture());
	const FVector2D MixedProductionSize =
		ProductionWheel->GetCachedGeometry().GetLocalSize();
	TestTrue(TEXT("The reopened mixed-input wheel has stable paintable geometry"),
		MixedProductionSize.X > 0.0f && MixedProductionSize.Y > 0.0f);
	TestTrue(TEXT("Keyboard navigation first focuses valid 3-way slot 1"),
		SlateApp.ProcessKeyDownEvent(FKeyEvent(
			EKeys::Right, FModifierKeysState(), 0, false, 0, 0)));
	if (ProductionWheel->HasMouseCapture()
		&& MixedProductionSize.X > 0.0f
		&& MixedProductionSize.Y > 0.0f)
	{
		const FGeometry ProductionGeometry = ProductionWheel->GetCachedGeometry();
		const double ProductionRadius =
			FMath::Min(MixedProductionSize.X, MixedProductionSize.Y) * 0.40;
		// Slot 2 of a zero-offset 3-way topology is centered 240 degrees clockwise from +Y.
		const FVector2D InvalidSlotPoint = ProductionGeometry.LocalToAbsolute(
			MixedProductionSize * 0.5
			+ FVector2D(-0.8660254, 0.5) * ProductionRadius);
		const FPointerEvent PointAtInvalidSlot(
			0,
			FSlateApplication::CursorPointerIndex,
			InvalidSlotPoint,
			InvalidSlotPoint + FVector2D(1.0, 0.0),
			NoPressedButtons,
			FKey(),
			0.0f,
			FModifierKeysState());
		const TSharedPtr<FSlateUser> PointerUser = SlateApp.GetUser(PointAtInvalidSlot);
		TestTrue(TEXT("The captured route has a live Slate user"), PointerUser.IsValid());
		if (PointerUser.IsValid())
		{
			const uint32 PointerIndex = PointAtInvalidSlot.GetPointerIndex();
			TestTrue(TEXT("The live wheel is the actual Slate pointer captor"),
				PointerUser->GetPointerCaptor(PointerIndex).Get() == ProductionWheel.Get());
			const FWidgetPath CaptorPath = PointerUser->GetCaptorPath(
				PointerIndex,
				FWeakWidgetPath::EInterruptedPathHandling::ReturnInvalid,
				&PointAtInvalidSlot);
			TestTrue(TEXT("Slate resolves the live wheel's captured widget path"),
				CaptorPath.IsValid());
			if (CaptorPath.IsValid())
			{
				TestTrue(TEXT("The captured path terminates at the live wheel"),
					&CaptorPath.GetLastWidget().Get() == ProductionWheel.Get());
			}
			// Keep the public platform-input seam under test. The cursor pointer index above is what
			// makes ProcessMouseMoveEvent resolve the held wheel's real Slate capture path.
			SlateApp.ProcessMouseMoveEvent(PointAtInvalidSlot);
		}
		TestEqual(TEXT("Captured pointer motion reaches invalid 3-way slot 2"),
			ProductionWheel->GetHoveredSlot(), 2);
	}
	TestTrue(TEXT("The captured key-up is consumed even when invalid hover cancels"),
		SlateApp.ProcessKeyUpEvent(RemappedKeyUp));
	TestEqual(TEXT("Invalid pointer intent overrides stale valid keyboard focus"),
		Toolkit->EditorModel->GetDirectionalPreview().SlotIndex, 0);
	TestFalse(TEXT("Invalid release closes and unregisters the production interaction"),
		Toolkit->IsDirectionalWheelOpenForTests()
		|| Paper2DPlusDirectionalAnimationRenderTestPrivate::IsInputProcessorRegistered(
			SlateApp, RegisteredProcessor));

	ProcessorEntry.DirectionalAnimationData.DirectionCount = 16;
	ProcessorEntry.DirectionalAnimationData.Slots.Reset();
	ProcessorProfile->InvalidateFlipbookLookupCache();
	Toolkit->EditorModel->NotifyAssetDataChanged();
	for (int32 FocusAttempt = 0;
		FocusAttempt < 3 && !Toolkit->IsDirectionalHeaderFocusedForTests(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId);
		++FocusAttempt)
	{
		SlateApp.Tick();
	}
	TestTrue(TEXT("The captured remapped key opens the narrow 16-way topology"),
		OpenProductionHeldWheel());
	ProductionWheel = Toolkit->ActiveDirectionWheel.Pin();
	RegisteredProcessor = Toolkit->DirectionInputProcessor;
	if (ProductionWheel.IsValid())
	{
		SlateApp.Tick();
		ProductionWheel->SlatePrepass();
		const FVector2D ProductionSize = ProductionWheel->GetCachedGeometry().GetLocalSize();
		TestTrue(TEXT("The live 16-way wheel remains paintable"),
			ProductionSize.X > 0.0f && ProductionSize.Y > 0.0f);
		TestTrue(TEXT("The live 16-way wheel exposes all segments accessibly"),
			ProductionWheel->GetAccessibleSummaryText().ToString().Contains(TEXT("16 segments")));
	}
	const int32 SlotBeforeOutsideRingClick =
		Toolkit->EditorModel->GetDirectionalPreview().SlotIndex;
	bool bOutsideRingClickHandled = false;
	if (ProductionWheel.IsValid())
	{
		const FGeometry ProductionGeometry = ProductionWheel->GetCachedGeometry();
		const FVector2D CenterDeadZonePoint = ProductionGeometry.LocalToAbsolute(
			ProductionGeometry.GetLocalSize() * 0.5);
		TSet<FKey> PressedButtons;
		PressedButtons.Add(EKeys::LeftMouseButton);
		const FPointerEvent ClickOutsideRing(
			0,
			FSlateApplication::CursorPointerIndex,
			CenterDeadZonePoint,
			CenterDeadZonePoint,
			PressedButtons,
			EKeys::LeftMouseButton,
			0.0f,
			FModifierKeysState());
		bOutsideRingClickHandled = SlateApp.ProcessMouseButtonDownEvent(
			nullptr, ClickOutsideRing);
	}
	TestTrue(TEXT("A captured click in the wheel's center dead zone is handled"),
		bOutsideRingClickHandled);
	TestFalse(TEXT("A captured outside-ring click cancels the live held wheel"),
		Toolkit->IsDirectionalWheelOpenForTests());
	TestEqual(TEXT("Outside-ring cancellation preserves the committed preview slot"),
		Toolkit->EditorModel->GetDirectionalPreview().SlotIndex, SlotBeforeOutsideRingClick);
	TestFalse(TEXT("Outside-ring cancellation unregisters the production processor"),
		Paper2DPlusDirectionalAnimationRenderTestPrivate::IsInputProcessorRegistered(
			SlateApp, RegisteredProcessor));
	TestFalse(TEXT("Outside-ring cancellation clears the toolkit processor owner"),
		Toolkit->DirectionInputProcessor.IsValid());
	TestFalse(TEXT("Outside-ring cancellation releases the exact wheel capture"),
		ProductionWheel.IsValid() && ProductionWheel->HasMouseCapture());

	for (int32 FocusAttempt = 0;
		FocusAttempt < 3 && !Toolkit->IsDirectionalHeaderFocusedForTests(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId);
		++FocusAttempt)
	{
		SlateApp.Tick();
	}
	TestTrue(TEXT("Outside-ring cancellation restores the Direction opener"),
		Toolkit->IsDirectionalHeaderFocusedForTests(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId));
	TestTrue(TEXT("The captured remapped key reopens for capture-loss teardown"),
		OpenProductionHeldWheel());
	ProductionWheel = Toolkit->ActiveDirectionWheel.Pin();
	RegisteredProcessor = Toolkit->DirectionInputProcessor;
	TestTrue(TEXT("The reopened held wheel owns pointer capture before forced loss"),
		ProductionWheel.IsValid() && ProductionWheel->HasMouseCapture());
	TestTrue(TEXT("The reopened production processor is registered before forced loss"),
		Paper2DPlusDirectionalAnimationRenderTestPrivate::IsInputProcessorRegistered(
			SlateApp, RegisteredProcessor));
	const int32 SlotBeforeCaptureLoss =
		Toolkit->EditorModel->GetDirectionalPreview().SlotIndex;
	SlateApp.ReleaseAllPointerCapture();
	TestTrue(TEXT("Standalone capture loss defers cancellation for the current Slate turn"),
		Toolkit->IsDirectionalWheelOpenForTests());
	SlateApp.Tick();
	TestFalse(TEXT("Mouse-capture loss cancels the live held wheel"),
		Toolkit->IsDirectionalWheelOpenForTests());
	TestEqual(TEXT("Mouse-capture loss preserves the committed preview slot"),
		Toolkit->EditorModel->GetDirectionalPreview().SlotIndex, SlotBeforeCaptureLoss);
	TestFalse(TEXT("Mouse-capture loss unregisters the production processor"),
		Paper2DPlusDirectionalAnimationRenderTestPrivate::IsInputProcessorRegistered(
			SlateApp, RegisteredProcessor));
	TestFalse(TEXT("Mouse-capture loss clears the toolkit processor owner"),
		Toolkit->DirectionInputProcessor.IsValid());
	TestFalse(TEXT("Mouse-capture loss releases the exact wheel capture"),
		ProductionWheel.IsValid() && ProductionWheel->HasMouseCapture());

	for (int32 FocusAttempt = 0;
		FocusAttempt < 3 && !Toolkit->IsDirectionalHeaderFocusedForTests(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId);
		++FocusAttempt)
	{
		SlateApp.Tick();
	}
	TestTrue(TEXT("Capture-loss cancellation restores the Direction opener"),
		Toolkit->IsDirectionalHeaderFocusedForTests(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId));
	TestTrue(TEXT("The captured remapped key reopens to reject release while capture loss is pending"),
		OpenProductionHeldWheel());
	ProductionWheel = Toolkit->ActiveDirectionWheel.Pin();
	RegisteredProcessor = Toolkit->DirectionInputProcessor;
	TestTrue(TEXT("End focuses a committable candidate before capture is lost"),
		SlateApp.ProcessKeyDownEvent(FKeyEvent(
			EKeys::End, FModifierKeysState(), 0, false, 0, 0)));
	TestTrue(TEXT("The pending-release fixture has a focused segment after End"),
		ProductionWheel.IsValid() && ProductionWheel->HasFocusedDescendants());
	const int32 SlotBeforePendingRelease =
		Toolkit->EditorModel->GetDirectionalPreview().SlotIndex;
	TestTrue(TEXT("The pending-release fixture has not already committed its slot-15 candidate"),
		SlotBeforePendingRelease != 15);
	SlateApp.ReleaseAllPointerCapture();
	TestTrue(TEXT("Capture loss leaves only a pending cancellation before the next Slate turn"),
		Toolkit->IsDirectionalWheelOpenForTests());
	TestTrue(TEXT("The pending interaction still consumes its captured primary-key release"),
		SlateApp.ProcessKeyUpEvent(RemappedKeyUp));
	TestEqual(TEXT("A primary-key release cannot commit after capture is lost"),
		Toolkit->EditorModel->GetDirectionalPreview().SlotIndex,
		SlotBeforePendingRelease);
	TestFalse(TEXT("The rejected release resolves the pending interaction as cancellation"),
		Toolkit->IsDirectionalWheelOpenForTests());
	TestFalse(TEXT("The rejected release unregisters the pending processor"),
		Paper2DPlusDirectionalAnimationRenderTestPrivate::IsInputProcessorRegistered(
			SlateApp, RegisteredProcessor));
	TestFalse(TEXT("The rejected release clears the pending processor owner"),
		Toolkit->DirectionInputProcessor.IsValid());
	for (int32 FocusAttempt = 0;
		FocusAttempt < 3 && !Toolkit->IsDirectionalHeaderFocusedForTests(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId);
		++FocusAttempt)
	{
		SlateApp.Tick();
	}
	TestTrue(TEXT("Pending-release cancellation restores the Direction opener"),
		Toolkit->IsDirectionalHeaderFocusedForTests(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId));
	TestTrue(TEXT("The captured remapped key reopens after pending-release teardown"),
		OpenProductionHeldWheel());
	RegisteredProcessor = Toolkit->DirectionInputProcessor;
	TestTrue(TEXT("End focuses the final slot in the production 16-way wheel"),
		SlateApp.ProcessKeyDownEvent(FKeyEvent(
			EKeys::End, FModifierKeysState(), 0, false, 0, 0)));
	TestTrue(TEXT("Captured key-up commits the final 16-way slot"),
		SlateApp.ProcessKeyUpEvent(RemappedKeyUp));
	TestEqual(TEXT("The production 16-way path commits slot 15"),
		Toolkit->EditorModel->GetDirectionalPreview().SlotIndex, 15);
	TestFalse(TEXT("The final commit unregisters the reopened processor"),
		Paper2DPlusDirectionalAnimationRenderTestPrivate::IsInputProcessorRegistered(
			SlateApp, RegisteredProcessor));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationToolkitHeadersRenderTest,
	"Paper2DPlusRender.DirectionalAnimation.Editor.Toolkit.SixLiveHeadersExposeInvalidWheelState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationToolkitHeadersRenderTest::RunTest(
	const FString& Parameters)
{
	if (!TestTrue(TEXT("The render lane must run without NullRHI"), FApp::CanEverRender())
		|| !TestTrue(TEXT("Slate is initialized"), FSlateApplication::IsInitialized()))
	{
		return false;
	}
	FSlateApplication& SlateApp = FSlateApplication::Get();
	if (!TestNotNull(TEXT("A real Slate renderer is available"), SlateApp.GetRenderer()))
	{
		return false;
	}

	TStrongObjectPtr<UPaper2DPlusCharacterProfileAsset> Profile(
		NewObject<UPaper2DPlusCharacterProfileAsset>());
	UPaperFlipbook* Base = NewObject<UPaperFlipbook>(Profile.Get(), TEXT("ToolkitBase"));
	UPaperFlipbook* FirstVariant =
		NewObject<UPaperFlipbook>(Profile.Get(), TEXT("ToolkitVariantA"));
	UPaperFlipbook* DuplicateVariant =
		NewObject<UPaperFlipbook>(Profile.Get(), TEXT("ToolkitVariantB"));
	FFlipbookProfileEntry& Entry = Profile->Flipbooks.AddDefaulted_GetRef();
	Entry.Identity.FlipbookName = TEXT("MalformedDirectional");
	Entry.Identity.Flipbook = Base;
	Entry.DirectionalAnimationData.bHasDirectionalSet = true;
	Entry.DirectionalAnimationData.bOverrideProfileSettings = true;
	Entry.DirectionalAnimationData.DirectionCount = 5;
	Entry.DirectionalAnimationData.AngleOffsetDegrees = -20.0f;
	FPaper2DPlusDirectionalAnimationSlot& FirstSlot =
		Entry.DirectionalAnimationData.Slots.AddDefaulted_GetRef();
	FirstSlot.SlotIndex = 1;
	FirstSlot.Flipbook = FirstVariant;
	FPaper2DPlusDirectionalAnimationSlot& DuplicateSlot =
		Entry.DirectionalAnimationData.Slots.AddDefaulted_GetRef();
	DuplicateSlot.SlotIndex = 1;
	DuplicateSlot.Flipbook = DuplicateVariant;

	TSharedPtr<FCharacterProfileAssetEditorToolkit> Toolkit =
		MakeShared<FCharacterProfileAssetEditorToolkit>();
	Toolkit->InitEditor(EToolkitMode::Standalone, nullptr, Profile.Get());
	ON_SCOPE_EXIT
	{
		if (Toolkit.IsValid())
		{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
			Toolkit->CloseWindow(EAssetEditorCloseReason::AssetUnloadingOrInvalid);
#else
			Toolkit->CloseWindow();
#endif
			Toolkit.Reset();
		}
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().Tick();
		}
	};
	SlateApp.Tick();

	const TArray<FName> MainToolIds = {
		FCharacterProfileAssetEditorToolkit::AnimationsTabId,
		FCharacterProfileAssetEditorToolkit::HitboxEditorTabId,
		FCharacterProfileAssetEditorToolkit::SpriteEditorTabId,
		FCharacterProfileAssetEditorToolkit::FrameTimingTabId,
		FCharacterProfileAssetEditorToolkit::FrameEventsTabId,
		FCharacterProfileAssetEditorToolkit::RootMotionTabId
	};
	for (const FName ToolId : MainToolIds)
	{
		TestTrue(
			*FString::Printf(TEXT("%s activates its live shared header button"), *ToolId.ToString()),
			Toolkit->ActivateDirectionalHeaderForTests(ToolId));
		const FString Summary =
			Toolkit->GetActiveDirectionalWheelSummaryForTests().ToString();
		TestTrue(
			*FString::Printf(TEXT("%s keeps the base animation visible"), *ToolId.ToString()),
			Summary.Contains(TEXT("MalformedDirectional")));
		TestTrue(
			*FString::Printf(TEXT("%s exposes structural validation as invalid wedges"), *ToolId.ToString()),
			Summary.Contains(TEXT("5 segments")) && Summary.Contains(TEXT("5 invalid")));
		Toolkit->DismissDirectionalWheelForTests();
		// Focus restoration intentionally runs after the menu's outer Slate transition. Give the
		// bounded active-timer retry enough real Slate frames to outlive any panel activation timer.
		for (int32 FocusAttempt = 0;
			FocusAttempt < 3 && !Toolkit->IsDirectionalHeaderFocusedForTests(ToolId);
			++FocusAttempt)
		{
			SlateApp.Tick();
		}
		TestFalse(
			*FString::Printf(TEXT("%s dismisses the transient wheel cleanly"), *ToolId.ToString()),
			Toolkit->IsDirectionalWheelOpenForTests());
		TestTrue(
			*FString::Printf(TEXT("%s restores focus to its Direction opener"), *ToolId.ToString()),
			Toolkit->IsDirectionalHeaderFocusedForTests(ToolId));
	}

	Entry.DirectionalAnimationData.Slots.SetNum(1);
	Entry.DirectionalAnimationData.Slots[0].SlotIndex = 0;
	Entry.DirectionalAnimationData.Slots[0].Flipbook = FirstVariant;
	{
		FScopedFlipbookMutator BaseMutator(Base);
		BaseMutator.FramesPerSecond = 30.0f;
		FScopedFlipbookMutator VariantMutator(FirstVariant);
		VariantMutator.FramesPerSecond = 12.0f;
	}
	Profile->InvalidateFlipbookLookupCache();
	TestTrue(TEXT("Animations opens a wheel for structurally valid incompatible art"),
		Toolkit->ActivateDirectionalHeaderForTests(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId));
	TestTrue(TEXT("Timeline-incompatible resident art is exposed as one invalid wedge"),
		Toolkit->GetActiveDirectionalWheelSummaryForTests().ToString().Contains(
			TEXT("1 occupied and 1 invalid")));
	Toolkit->DismissDirectionalWheelForTests();

	{
		FScopedFlipbookMutator VariantMutator(FirstVariant);
		VariantMutator.FramesPerSecond = 30.0f;
	}
	FFlipbookProfileEntry& ConflictingOwner = Profile->Flipbooks.AddDefaulted_GetRef();
	ConflictingOwner.Identity.FlipbookName = TEXT("ConflictingOwner");
	ConflictingOwner.Identity.Flipbook = FirstVariant;
	Profile->InvalidateFlipbookLookupCache();
	TestTrue(TEXT("Animations opens a wheel for a cross-owner resident collision"),
		Toolkit->ActivateDirectionalHeaderForTests(
			FCharacterProfileAssetEditorToolkit::AnimationsTabId));
	TestTrue(TEXT("Cross-owner ambiguity is exposed as one invalid wedge"),
		Toolkit->GetActiveDirectionalWheelSummaryForTests().ToString().Contains(
			TEXT("1 occupied and 1 invalid")));
	Toolkit->DismissDirectionalWheelForTests();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
