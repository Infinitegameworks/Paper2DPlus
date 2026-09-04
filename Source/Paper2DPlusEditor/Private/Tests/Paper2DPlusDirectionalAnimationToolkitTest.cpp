// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CharacterProfileAssetEditor.h"
#include "CharacterProfileEditorModel.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/InputChord.h"
#include "InputCoreTypes.h"
#include "Misc/ScopeExit.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "ProfileToolPanelProvider.h"
#include "SDirectionalAnimationCommandRouter.h"
#include "SlateShortcutUtils.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	class SDirectionalAnimationConsumingChild final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SDirectionalAnimationConsumingChild) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			(void)InArgs;
			ChildSlot
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Consumes local keys")))
			];
		}

		virtual bool SupportsKeyboardFocus() const override { return true; }

		virtual FReply OnKeyDown(
			const FGeometry& MyGeometry,
			const FKeyEvent& InKeyEvent) override
		{
			(void)MyGeometry;
			(void)InKeyEvent;
			++KeyDownCount;
			return FReply::Handled();
		}

		int32 GetKeyDownCount() const { return KeyDownCount; }

	private:
		int32 KeyDownCount = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalToolkitSharedHeaderTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Toolkit.SharedHeaderAcrossMainTools",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalToolkitSharedHeaderTest::RunTest(const FString& Parameters)
{
	const TArray<FName> MainToolIds = {
		FCharacterProfileAssetEditorToolkit::AnimationsTabId,
		FCharacterProfileAssetEditorToolkit::HitboxEditorTabId,
		FCharacterProfileAssetEditorToolkit::SpriteEditorTabId,
		FCharacterProfileAssetEditorToolkit::FrameTimingTabId,
		FCharacterProfileAssetEditorToolkit::FrameEventsTabId,
		FCharacterProfileAssetEditorToolkit::RootMotionTabId
	};
	const FString ExpectedDisclosure =
		TEXT("Gameplay edits apply to all directions (base-owned)");
	for (const FName ToolId : MainToolIds)
	{
		TestTrue(
			*FString::Printf(TEXT("%s receives the shared Direction header"), *ToolId.ToString()),
			FCharacterProfileAssetEditorToolkit::SupportsDirectionalHeader(ToolId));
		TestEqual(
			*FString::Printf(TEXT("%s keeps the base-owned gameplay notice visible"), *ToolId.ToString()),
			FCharacterProfileAssetEditorToolkit::GetDirectionalHeaderGameplayDisclosure(ToolId).ToString(),
			ExpectedDisclosure);
	}

	TestFalse(
		TEXT("The legacy Flipbook List sidebar is not a main animation tool host"),
		FCharacterProfileAssetEditorToolkit::SupportsDirectionalHeader(
			FCharacterProfileAssetEditorToolkit::FlipbookListTabId));
	TestTrue(
		TEXT("A non-main host has no directional gameplay disclosure"),
		FCharacterProfileAssetEditorToolkit::GetDirectionalHeaderGameplayDisclosure(
			FCharacterProfileAssetEditorToolkit::ContextHostTabId).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalToolkitProfileOptInTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Toolkit.ProfileHostExplicitlyOptsIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalToolkitProfileOptInTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	TSharedRef<FCharacterProfileEditorModel> ProfileHostModel =
		MakeShared<FCharacterProfileEditorModel>();
	TSharedRef<FCharacterProfileEditorModel> SharedLayerHostModel =
		MakeShared<FCharacterProfileEditorModel>();
	ProfileHostModel->InitializeFromAsset(Profile);
	SharedLayerHostModel->InitializeFromAsset(Profile);

	TestFalse(
		TEXT("The shared model remains base-only until a host opts in"),
		ProfileHostModel->IsDirectionalPreviewEnabled());
	FCharacterProfileAssetEditorToolkit::EnableDirectionalPreviewForCharacterProfileHost(
		ProfileHostModel);
	TestTrue(
		TEXT("The Character Profile host enables directional visual projection"),
		ProfileHostModel->IsDirectionalPreviewEnabled());
	TestFalse(
		TEXT("A separate Layer-style host retains the shared model's base-only default"),
		SharedLayerHostModel->IsDirectionalPreviewEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalToolkitVisualStateTableTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Toolkit.HeaderVisualStateTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalToolkitVisualStateTableTest::RunTest(const FString& Parameters)
{
	UPaperFlipbook* Resident = NewObject<UPaperFlipbook>();
	FCharacterProfileDirectionalPreview Preview;
	Preview.ResidentFlipbook = Resident;

	struct FStateExpectation
	{
		ECharacterProfileDirectionalPreviewState State;
		bool bShowsResident;
	};
	const FStateExpectation Expectations[] = {
		{ ECharacterProfileDirectionalPreviewState::Base, true },
		{ ECharacterProfileDirectionalPreviewState::OccupiedVariant, true },
		{ ECharacterProfileDirectionalPreviewState::Empty, false },
		{ ECharacterProfileDirectionalPreviewState::Resolving, false },
		{ ECharacterProfileDirectionalPreviewState::Unavailable, false }
	};

	for (const FStateExpectation& Expectation : Expectations)
	{
		Preview.State = Expectation.State;
		UPaperFlipbook* Resolved =
			FCharacterProfileAssetEditorToolkit::ResolveDirectionalHeaderPreviewFlipbook(Preview);
		TestEqual(
			TEXT("The shared header applies the state table to its visual source"),
			Resolved,
			Expectation.bShowsResident ? Resident : nullptr);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalToolkitRemappableReleaseKeyTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Toolkit.ReleaseMatchesActualOpeningChord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalToolkitRemappableReleaseKeyTest::RunTest(
	const FString& Parameters)
{
	auto MakeKeyEvent = [](const FKey& Key, bool bControl, bool bAlt, bool bCommand)
	{
		const FModifierKeysState Modifiers(
			false, false,
			bControl, false,
			bAlt, false,
			bCommand, false,
			false);
		return FKeyEvent(Key, Modifiers, 0, false, 0, 0);
	};
	const FInputChord RemappedPrimary(EKeys::K, EModifierKey::Alt);
	const FInputChord RemappedSecondary(EKeys::J, EModifierKey::Control);
	const FKey PrimaryTrigger =
		FCharacterProfileAssetEditorToolkit::ResolveDirectionalWheelTriggerKey(
			RemappedPrimary,
			RemappedSecondary,
			MakeKeyEvent(EKeys::K, false, true, false));
	const FKey SecondaryTrigger =
		FCharacterProfileAssetEditorToolkit::ResolveDirectionalWheelTriggerKey(
			RemappedPrimary,
			RemappedSecondary,
			MakeKeyEvent(EKeys::J, true, false, false));
	TestEqual(TEXT("Opening through the primary chord snapshots only its main key"),
		PrimaryTrigger, EKeys::K);
	TestEqual(TEXT("Opening through the secondary chord snapshots only its main key"),
		SecondaryTrigger, EKeys::J);
	const TArray<FKey> AltFallback =
		FCharacterProfileAssetEditorToolkit::ResolveDirectionalWheelFallbackTriggerKeys(
			RemappedPrimary,
			RemappedSecondary,
			MakeKeyEvent(EKeys::K, false, true, false).GetModifierKeys());
	TestTrue(TEXT("An unwrapped toolkit command correlates Alt to one remapped chord"),
		AltFallback.Num() == 1 && AltFallback[0] == EKeys::K);
	const TArray<FKey> SharedModifierFallback =
		FCharacterProfileAssetEditorToolkit::ResolveDirectionalWheelFallbackTriggerKeys(
			FInputChord(EKeys::K, EModifierKey::Alt),
			FInputChord(EKeys::J, EModifierKey::Alt),
			MakeKeyEvent(EKeys::K, false, true, false).GetModifierKeys());
	TestTrue(TEXT("Same-modifier remaps refuse an ambiguous held-release fallback"),
		SharedModifierFallback.IsEmpty());
	TestTrue(
		TEXT("Releasing the key that actually opened the primary interaction resolves it"),
		FCharacterProfileAssetEditorToolkit::IsDirectionalWheelTriggerReleaseKey(
			PrimaryTrigger, EKeys::K));
	TestTrue(
		TEXT("Releasing the key that actually opened the secondary interaction resolves it"),
		FCharacterProfileAssetEditorToolkit::IsDirectionalWheelTriggerReleaseKey(
			SecondaryTrigger, EKeys::J));
	TestFalse(TEXT("The other configured chord cannot terminate a primary-opened interaction"),
		FCharacterProfileAssetEditorToolkit::IsDirectionalWheelTriggerReleaseKey(
			PrimaryTrigger, EKeys::J));
	TestFalse(TEXT("Mouse buttons are excluded because the toolkit host has no pointer command route"),
		FCharacterProfileAssetEditorToolkit::ResolveDirectionalWheelTriggerKey(
			FInputChord(EKeys::ThumbMouseButton),
			FInputChord(),
			MakeKeyEvent(EKeys::ThumbMouseButton, false, false, false)).IsValid());
	TestFalse(
		TEXT("The old default D key cannot resolve a K-remapped interaction"),
		FCharacterProfileAssetEditorToolkit::IsDirectionalWheelTriggerReleaseKey(
			PrimaryTrigger, EKeys::D));
	TestFalse(
		TEXT("Releasing the shortcut modifier never resolves the held interaction"),
		FCharacterProfileAssetEditorToolkit::IsDirectionalWheelTriggerReleaseKey(
			PrimaryTrigger, EKeys::LeftAlt));
	TestTrue(TEXT("Alt-modified local gestures yield to the toolkit command route"),
		Paper2DPlusEditor::SlateShortcutUtils::HasEditorCommandModifier(false, true, false));
	TestTrue(TEXT("Command-modified local gestures yield to the toolkit command route"),
		Paper2DPlusEditor::SlateShortcutUtils::HasEditorCommandModifier(false, false, true));
	TestFalse(TEXT("Shift remains available for coarse authoring gestures"),
		Paper2DPlusEditor::SlateShortcutUtils::HasEditorCommandModifier(false, false, false));
	TestTrue(TEXT("The tunnel route recognizes the active Alt-modified chord exactly"),
		FCharacterProfileAssetEditorToolkit::MatchesDirectionalWheelChord(
			RemappedPrimary, MakeKeyEvent(EKeys::K, false, true, false)));
	TestTrue(TEXT("The tunnel route also recognizes a modifierless remap before descendants"),
		FCharacterProfileAssetEditorToolkit::MatchesDirectionalWheelChord(
			FInputChord(EKeys::D), MakeKeyEvent(EKeys::D, false, false, false)));
	TestFalse(TEXT("An extra modifier does not activate a modifierless remap"),
		FCharacterProfileAssetEditorToolkit::MatchesDirectionalWheelChord(
			FInputChord(EKeys::D), MakeKeyEvent(EKeys::D, false, true, false)));
	TestFalse(TEXT("A pointer chord is not advertised as a routed toolkit hotkey"),
		FCharacterProfileAssetEditorToolkit::MatchesDirectionalWheelChord(
			FInputChord(EKeys::ThumbMouseButton),
			MakeKeyEvent(EKeys::ThumbMouseButton, false, false, false)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalToolkitPreviewRouterTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Toolkit.PreviewRouterPreemptsChildrenButYieldsToText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalToolkitPreviewRouterTest::RunTest(
	const FString& Parameters)
{
	if (!TestTrue(TEXT("The required editor Slate application is initialized"),
		FSlateApplication::IsInitialized()))
	{
		return false;
	}

	int32 ClaimedShortcutCount = 0;
	int32 IgnoredTextShortcutCount = 0;
	TSharedPtr<SDirectionalAnimationConsumingChild> ConsumingChild;
	TSharedPtr<SEditableText> EditableText;
	const TSharedRef<SDirectionalAnimationCommandRouter> Router =
		SNew(SDirectionalAnimationCommandRouter)
		.OnDirectionalAnimationPreviewKeyDown_Lambda([
			&ClaimedShortcutCount,
			&IgnoredTextShortcutCount](const FKeyEvent& Event)
		{
			if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
			{
				++IgnoredTextShortcutCount;
				return false;
			}
			if (Event.GetKey() == EKeys::D && !Event.IsControlDown()
				&& !Event.IsAltDown() && !Event.IsCommandDown() && !Event.IsShiftDown())
			{
				++ClaimedShortcutCount;
				return true;
			}
			return false;
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(ConsumingChild, SDirectionalAnimationConsumingChild)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(EditableText, SEditableText)
			]
		];
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.ClientSize(FVector2D(320.0f, 120.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			Router
		];
	FSlateApplication& SlateApp = FSlateApplication::Get();
	SlateApp.AddWindow(Window, /*bShowImmediately=*/false);
	ON_SCOPE_EXIT
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().RequestDestroyWindow(Window);
			FSlateApplication::Get().Tick();
		}
	};

	SlateApp.Tick();
	SlateApp.SetKeyboardFocus(ConsumingChild, EFocusCause::SetDirectly);
	TestTrue(TEXT("The consuming authoring child receives keyboard focus"),
		ConsumingChild->HasKeyboardFocus());
	const FKeyEvent ModifierlessD(
		EKeys::D, FModifierKeysState(), 0, false, 0, 0);
	TestTrue(TEXT("The routed modifierless command is handled through the real Slate path"),
		SlateApp.ProcessKeyDownEvent(ModifierlessD));
	TestEqual(TEXT("The preview router claims the command exactly once"),
		ClaimedShortcutCount, 1);
	TestEqual(TEXT("The focused authoring child cannot swallow the remapped command"),
		ConsumingChild->GetKeyDownCount(), 0);

	SlateApp.SetKeyboardFocus(EditableText, EFocusCause::SetDirectly);
	TestTrue(TEXT("The text editor owns keyboard focus"), EditableText->HasKeyboardFocus());
	const FReply TextEntryReply = Router->OnPreviewKeyDown(
		Router->GetCachedGeometry(), ModifierlessD);
	TestFalse(TEXT("The preview router leaves a text-entry chord unhandled"),
		TextEntryReply.IsEventHandled());
	TestEqual(TEXT("Text focus reaches the explicit ignore branch"),
		IgnoredTextShortcutCount, 1);
	TestEqual(TEXT("Yielding to text does not execute the command"),
		ClaimedShortcutCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalToolkitAssignmentTargetExpiryTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Toolkit.AssignmentPickerTargetExpiresWithContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalToolkitAssignmentTargetExpiryTest::RunTest(
	const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	FFlipbookProfileEntry& Idle = Profile->Flipbooks.AddDefaulted_GetRef();
	Idle.Identity.FlipbookName = TEXT("Idle");
	Idle.Identity.Flipbook = NewObject<UPaperFlipbook>(Profile, TEXT("PickerIdleBase"));
	FFlipbookProfileEntry& Run = Profile->Flipbooks.AddDefaulted_GetRef();
	Run.Identity.FlipbookName = TEXT("Run");
	Run.Identity.Flipbook = NewObject<UPaperFlipbook>(Profile, TEXT("PickerRunBase"));

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	Model->SetDirectionalPreviewEnabled(true);
	Model->SetSelectedFlipbook(0);
	const FProfileAnimationIdentity CapturedOwner =
		Model->GetDirectionalPreview().BaseAnimation;
	const int32 CapturedOwnerIndex = Model->GetSelectedFlipbookIndex();
	const int32 CapturedSlot = Model->GetDirectionalPreview().SlotIndex;
	TestTrue(TEXT("A freshly captured assignment target matches its open-picker context"),
		FCharacterProfileAssetEditorToolkit::IsDirectionalAssignmentTargetCurrent(
			Model, CapturedOwner, CapturedOwnerIndex, CapturedSlot));

	Model->SetCommittedDirectionalBearing(60.0);
	TestFalse(TEXT("Changing the inspected bearing expires the open picker's slot target"),
		FCharacterProfileAssetEditorToolkit::IsDirectionalAssignmentTargetCurrent(
			Model, CapturedOwner, CapturedOwnerIndex, CapturedSlot));
	Model->SetCommittedDirectionalBearing(0.0);
	Model->SetSelectedFlipbook(1);
	TestFalse(TEXT("Changing the selected animation expires the open picker's owner target"),
		FCharacterProfileAssetEditorToolkit::IsDirectionalAssignmentTargetCurrent(
			Model, CapturedOwner, CapturedOwnerIndex, CapturedSlot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalToolkitDuplicateBaseOwnerTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Toolkit.DuplicateBaseUsesExactSelectedOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalToolkitDuplicateBaseOwnerTest::RunTest(
	const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* SharedBase = NewObject<UPaperFlipbook>(Profile);
	FFlipbookProfileEntry& First = Profile->Flipbooks.AddDefaulted_GetRef();
	First.Identity.FlipbookName = TEXT("FirstOwner");
	First.Identity.Flipbook = SharedBase;
	FFlipbookProfileEntry& Second = Profile->Flipbooks.AddDefaulted_GetRef();
	Second.Identity.FlipbookName = TEXT("SelectedOwner");
	Second.Identity.Flipbook = SharedBase;

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	Model->SetSelectedFlipbook(1);
	Model->NotifyAssetDataChanged();
	TestEqual(TEXT("Asset reconciliation preserves the later duplicate-base row"),
		Model->GetSelectedFlipbookIndex(), 1);
	const FProfileAnimationIdentity CapturedOwner = Model->GetDirectionalPreview().BaseAnimation;
	TestEqual(TEXT("The composite duplicate owner resolves to the selected authored row"),
		Paper2DPlusProfileToolProvider::ResolveAnimationIndex(Profile, CapturedOwner), 1);
	TestEqual(TEXT("A duplicate base path resolves to the exact selected row"),
		FCharacterProfileAssetEditorToolkit::ResolveDirectionalSelectedOwnerIndex(Model), 1);

	Profile->Flipbooks.Swap(0, 1);
	TestEqual(TEXT("The composite owner follows its authored row across the silent reorder"),
		Paper2DPlusProfileToolProvider::ResolveAnimationIndex(Profile, CapturedOwner), 0);
	TestEqual(TEXT("A stale selected-row identity fails closed after an unannounced reorder"),
		FCharacterProfileAssetEditorToolkit::ResolveDirectionalSelectedOwnerIndex(Model),
		INDEX_NONE);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
