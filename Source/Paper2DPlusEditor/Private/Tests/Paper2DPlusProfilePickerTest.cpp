// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AnimationProfileSwitcher.h"
#include "AnimationProfilePickerSource.h"
#include "CharacterProfileEditorModel.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Misc/App.h"
#include "Paper2DPlusAnimationTags.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "ProfileItemPicker.h"
#include "ProfileNavigatorPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWindow.h"

namespace
{
	class FPickerTestSource final : public IProfileItemPickerSource
	{
	public:
		FName Type = TEXT("Test");
		FString Scope;
		TArray<FProfilePickerItem> Items;
		FProfileItemIdentity Selected;
		int32 SelectCallCount = 0;

		virtual FName GetSourceType() const override { return Type; }
		virtual FString GetLogicalCatalogScope() const override { return Scope; }
		virtual void GetItems(TArray<FProfilePickerItem>& OutItems) const override { OutItems = Items; }
		virtual FProfileItemIdentity GetSelectedIdentity() const override { return Selected; }
		virtual bool SelectItem(const FProfileItemIdentity& Identity) override
		{
			const FProfilePickerItem* Match = Items.FindByPredicate([&Identity](const FProfilePickerItem& Item)
			{
				return Item.Identity.Matches(Identity);
			});
			if (!Match) return false;
			++SelectCallCount;
			Selected = Match->Identity;
			Changed.Broadcast();
			return true;
		}
		virtual FOnProfileItemSourceChanged& OnSourceChanged() override { return Changed; }
		void NotifyChanged() { Changed.Broadcast(); }

	private:
		FOnProfileItemSourceChanged Changed;
	};

	FProfilePickerItem PickerTest_Item(const FString& Name, int32 Order, const FString& Group = FString())
	{
		FProfilePickerItem Item;
		Item.Identity.SourceType = TEXT("Test");
		Item.Identity.ObjectPath = FSoftObjectPath(FString::Printf(TEXT("/Game/Picker/%s.%s"), *Name, *Name));
		Item.Identity.FallbackKey = Name;
		Item.Label = FText::FromString(Name);
		Item.Group = Group;
		Item.CanonicalOrder = Order;
		return Item;
	}

	TArray<FString> PickerTest_Headers(const FProfilePickerResultModel& Model)
	{
		TArray<FString> Headers;
		for (const TSharedPtr<FProfilePickerDisplayRow>& Row : Model.GetRows())
		{
			if (Row.IsValid() && Row->Kind == FProfilePickerDisplayRow::EKind::Header)
			{
				Headers.Add(Row->Header.ToString());
			}
		}
		return Headers;
	}

	FKeyEvent PickerTest_KeyEvent(const FKey& Key)
	{
		return FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0);
	}

	class FPickerScopedKeyboardFocus
	{
	public:
		FPickerScopedKeyboardFocus()
		{
			if (FSlateApplication::IsInitialized())
			{
				PreviousFocus = FSlateApplication::Get().GetKeyboardFocusedWidget();
			}
		}

		~FPickerScopedKeyboardFocus()
		{
			if (!FSlateApplication::IsInitialized())
			{
				return;
			}
			if (const TSharedPtr<SWidget> FocusToRestore = PreviousFocus.Pin())
			{
				FSlateApplication::Get().SetKeyboardFocus(FocusToRestore, EFocusCause::SetDirectly);
			}
			else
			{
				FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
			}
		}

		void ClearForDirectDispatch() const
		{
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
			}
		}

	private:
		TWeakPtr<SWidget> PreviousFocus;
	};

	class FPickerScopedSlateWindow
	{
	public:
		explicit FPickerScopedSlateWindow(const TSharedRef<SWidget>& Content)
		{
			if (!FSlateApplication::IsInitialized() || !FApp::CanEverRender())
			{
				return;
			}
			Window = SNew(SWindow)
				.Title(FText::FromString(TEXT("Paper2DPlus Animation Switcher Input Test")))
				.ClientSize(FVector2D(420.0f, 260.0f))
				.FocusWhenFirstShown(false)
				.SupportsMaximize(false)
				.SupportsMinimize(false)
				[
					Content
				];
			FSlateApplication::Get().AddWindow(Window.ToSharedRef(), true);
			TickWidgets();
		}

		~FPickerScopedSlateWindow()
		{
			if (Window.IsValid() && FSlateApplication::IsInitialized())
			{
				Window->RequestDestroyWindow();
				TickWidgets();
			}
		}

		bool IsValid() const { return Window.IsValid(); }

		void TickWidgets(int32 TickCount = 1) const
		{
			if (!FSlateApplication::IsInitialized())
			{
				return;
			}
			for (int32 TickIndex = 0; TickIndex < TickCount; ++TickIndex)
			{
				FSlateApplication::Get().Tick(ESlateTickType::TimeAndWidgets);
			}
		}

	private:
		TSharedPtr<SWindow> Window;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfilePickerSearchProjectionTest,
	"Paper2DPlus.Editor.ProfilePicker.SearchAndDeterministicGroups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfilePickerSearchProjectionTest::RunTest(const FString& Parameters)
{
	TSharedRef<FPickerTestSource> Source = MakeShared<FPickerTestSource>();
	Source->Scope = TEXT("Automation.SearchProjection");
	FProfilePickerCatalogStore::Get().ResetScopeForTests(Source->Scope);

	FProfilePickerItem Heavy = PickerTest_Item(TEXT("HeavyAttack"), 0, TEXT("Combat"));
	Heavy.Aliases = { TEXT("Power Strike") };
	Heavy.SecondaryText = FText::FromString(TEXT("12 frames startup"));
	Heavy.SearchTags.AddTag(Paper2DPlusAnimationTags::Combat_Heavy);
	FProfilePickerItem Idle = PickerTest_Item(TEXT("Idle"), 1, TEXT("Locomotion"));
	Source->Items = { Heavy, Idle };

	FProfilePickerResultModel Results(Source);
	TestTrue(TEXT("Canonical group order follows first Profile occurrence"),
		PickerTest_Headers(Results) == TArray<FString>({ TEXT("Combat"), TEXT("Locomotion") }));
	for (const TCHAR* Query : { TEXT("heavyatt"), TEXT("power strike"), TEXT("12 frames"), TEXT("combat"), TEXT("Combat.Heavy") })
	{
		Results.SetQuery(Query);
		TestEqual(FString::Printf(TEXT("Query '%s' matches label/alias/secondary/group/tag projection"), Query),
			Results.GetMatchingItems().Num(), 1);
		if (Results.GetMatchingItems().Num() == 1)
		{
			TestEqual(TEXT("Matched HeavyAttack"), Results.GetMatchingItems()[0]->Label.ToString(), FString(TEXT("HeavyAttack")));
		}
	}

	// The contract is domain-agnostic: a non-animation source uses the exact same filter/group model.
	Source->Type = TEXT("Effect");
	for (FProfilePickerItem& Item : Source->Items) Item.Identity.SourceType = Source->Type;
	Results.Refresh();
	Results.SetQuery(TEXT("power"));
	TestEqual(TEXT("Non-animation source shares result filtering"), Results.GetMatchingItems().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfilePickerPopupPinnedBehaviorTest,
	"Paper2DPlus.Editor.ProfilePicker.PopupAndPinnedSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfilePickerPopupPinnedBehaviorTest::RunTest(const FString& Parameters)
{
	TSharedRef<FPickerTestSource> PopupSource = MakeShared<FPickerTestSource>();
	PopupSource->Scope = TEXT("Automation.Popup");
	PopupSource->Items = { PickerTest_Item(TEXT("Idle"), 0), PickerTest_Item(TEXT("Run"), 1) };
	FProfilePickerCatalogStore::Get().ResetScopeForTests(PopupSource->Scope);
	int32 PopupDismissals = 0;
	TSharedRef<SProfileNavigatorPanel> Popup = SNew(SProfileNavigatorPanel)
		.Source(PopupSource)
		.Mode(EProfileNavigatorMode::Popup)
		.OnDismissed(FOnProfileNavigatorDismissed::CreateLambda([&PopupDismissals]() { ++PopupDismissals; }));
	TestTrue(TEXT("Popup commits a valid identity"), Popup->SelectIdentityForTests(PopupSource->Items[1].Identity));
	TestEqual(TEXT("Popup calls provider exactly once"), PopupSource->SelectCallCount, 1);
	TestEqual(TEXT("Popup dismisses exactly once after selection"), PopupDismissals, 1);

	TSharedRef<FPickerTestSource> PinnedSource = MakeShared<FPickerTestSource>();
	PinnedSource->Scope = TEXT("Automation.Pinned");
	PinnedSource->Items = PopupSource->Items;
	FProfilePickerCatalogStore::Get().ResetScopeForTests(PinnedSource->Scope);
	int32 PinnedDismissals = 0;
	int32 AuxiliaryActivations = 0;
	TSharedRef<SProfileNavigatorPanel> Pinned = SNew(SProfileNavigatorPanel)
		.Source(PinnedSource)
		.Mode(EProfileNavigatorMode::Pinned)
		.OnDismissed(FOnProfileNavigatorDismissed::CreateLambda([&PinnedDismissals]() { ++PinnedDismissals; }))
		.OnItemDoubleClicked(FOnProfileNavigatorItemDoubleClicked::CreateLambda(
			[&AuxiliaryActivations](const FProfileItemIdentity&) { ++AuxiliaryActivations; }));
	TestTrue(TEXT("Pinned navigator commits a valid identity"), Pinned->SelectIdentityForTests(PinnedSource->Items[0].Identity));
	TestEqual(TEXT("Pinned calls provider exactly once"), PinnedSource->SelectCallCount, 1);
	TestTrue(TEXT("Pinned row double-click keeps auxiliary action available"),
		Pinned->DoubleClickIdentityForTests(PinnedSource->Items[0].Identity));
	TestEqual(TEXT("Double-click on an already selected row does not call provider twice"), PinnedSource->SelectCallCount, 1);
	TestEqual(TEXT("Pinned double-click invokes its auxiliary command once"), AuxiliaryActivations, 1);
	TestEqual(TEXT("Pinned navigator remains open"), PinnedDismissals, 0);

	// Keyboard arrows only move list focus; Enter is the one provider command. Printable input stays
	// unhandled for the search field, Escape dismisses popup without changing the model.
	TSharedRef<FPickerTestSource> KeyboardSource = MakeShared<FPickerTestSource>();
	KeyboardSource->Scope = TEXT("Automation.Keyboard");
	KeyboardSource->Items = PopupSource->Items;
	KeyboardSource->Selected = KeyboardSource->Items[0].Identity;
	FProfilePickerCatalogStore::Get().ResetScopeForTests(KeyboardSource->Scope);
	int32 KeyboardDismissals = 0;
	TSharedRef<SProfileNavigatorPanel> KeyboardPopup = SNew(SProfileNavigatorPanel)
		.Source(KeyboardSource)
		.Mode(EProfileNavigatorMode::Popup)
		.OnDismissed(FOnProfileNavigatorDismissed::CreateLambda([&KeyboardDismissals]() { ++KeyboardDismissals; }));
	TestTrue(TEXT("Down is contained as list navigation"),
		KeyboardPopup->OnKeyDown(FGeometry(), PickerTest_KeyEvent(EKeys::Down)).IsEventHandled());
	TestEqual(TEXT("Arrow navigation does not call provider"), KeyboardSource->SelectCallCount, 0);
	TestTrue(TEXT("Enter commits focused result"),
		KeyboardPopup->OnKeyDown(FGeometry(), PickerTest_KeyEvent(EKeys::Enter)).IsEventHandled());
	TestEqual(TEXT("Enter calls provider once"), KeyboardSource->SelectCallCount, 1);
	TestEqual(TEXT("Enter dismisses popup once"), KeyboardDismissals, 1);
	TestFalse(TEXT("Printable text is not intercepted by navigator shortcuts"),
		KeyboardPopup->OnKeyDown(FGeometry(), PickerTest_KeyEvent(EKeys::A)).IsEventHandled());

	TSharedRef<FPickerTestSource> EscapeSource = MakeShared<FPickerTestSource>();
	EscapeSource->Scope = TEXT("Automation.KeyboardEscape");
	EscapeSource->Items = PopupSource->Items;
	int32 EscapeDismissals = 0;
	TSharedRef<SProfileNavigatorPanel> EscapePopup = SNew(SProfileNavigatorPanel)
		.Source(EscapeSource)
		.Mode(EProfileNavigatorMode::Popup)
		.OnDismissed(FOnProfileNavigatorDismissed::CreateLambda([&EscapeDismissals]() { ++EscapeDismissals; }));
	TestTrue(TEXT("Escape is contained by popup"),
		EscapePopup->OnKeyDown(FGeometry(), PickerTest_KeyEvent(EKeys::Escape)).IsEventHandled());
	TestEqual(TEXT("Escape dismisses once"), EscapeDismissals, 1);
	TestEqual(TEXT("Escape never calls provider"), EscapeSource->SelectCallCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfilePickerCatalogPersistenceTest,
	"Paper2DPlus.Editor.ProfilePicker.LogicalScopePinsRecentsAndPruning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfilePickerCatalogPersistenceTest::RunTest(const FString& Parameters)
{
	FProfilePickerCatalogStore& Store = FProfilePickerCatalogStore::Get();
	const FString SharedScope = TEXT("Automation.SharedProfile");
	const FString OtherScope = TEXT("Automation.OtherProfile");
	Store.ResetScopeForTests(SharedScope);
	Store.ResetScopeForTests(OtherScope);

	const FProfilePickerItem Idle = PickerTest_Item(TEXT("Idle"), 0);
	const FProfilePickerItem Run = PickerTest_Item(TEXT("Run"), 1);
	Store.TogglePin(SharedScope, Idle.Identity);
	Store.AddRecent(SharedScope, Run.Identity);
	TestTrue(TEXT("Second toolkit/source sees shared logical-profile pin"), Store.IsPinned(SharedScope, Idle.Identity));
	TestFalse(TEXT("Different profile scope is independent"), Store.IsPinned(OtherScope, Idle.Identity));
	TestEqual(TEXT("Shared recent is visible process-locally"), Store.GetRecents(SharedScope).Num(), 1);
	TestEqual(TEXT("Pinned row exposes a named Unpin action"), Store.GetPinActionLabel(SharedScope, Idle.Identity).ToString(), FString(TEXT("Unpin")));
	TestEqual(TEXT("Unpinned row exposes a named Pin action"), Store.GetPinActionLabel(OtherScope, Idle.Identity).ToString(), FString(TEXT("Pin")));

	TSharedRef<FPickerTestSource> CharacterSource = MakeShared<FPickerTestSource>();
	CharacterSource->Scope = SharedScope;
	CharacterSource->Items = { Idle, Run };
	TSharedRef<FPickerTestSource> LayerSource = MakeShared<FPickerTestSource>();
	LayerSource->Scope = SharedScope;
	LayerSource->Items = { Idle, Run };
	FProfilePickerResultModel CharacterResults(CharacterSource);
	FProfilePickerResultModel LayerResults(LayerSource);
	TestTrue(TEXT("Character source projects shared Pinned and Recent sections"),
		PickerTest_Headers(CharacterResults).Contains(TEXT("Pinned")) && PickerTest_Headers(CharacterResults).Contains(TEXT("Recent")));
	TestTrue(TEXT("Layer source for same profile projects the same shared sections"),
		PickerTest_Headers(LayerResults).Contains(TEXT("Pinned")) && PickerTest_Headers(LayerResults).Contains(TEXT("Recent")));

	// Duplicate writes dedupe, missing/deleted/renamed identities prune, and no asset/source is mutated.
	Store.AddRecent(SharedScope, Run.Identity);
	Store.TogglePin(SharedScope, Run.Identity);
	FProfileItemIdentity Missing = PickerTest_Item(TEXT("Deleted"), 2).Identity;
	Store.TogglePin(SharedScope, Missing);
	Store.Prune(SharedScope, TArray<FProfilePickerItem>({ Idle, Run }));
	TestEqual(TEXT("Deleted identity pruned from pins"), Store.GetPins(SharedScope).Num(), 2);
	TestEqual(TEXT("Duplicate recent deduped"), Store.GetRecents(SharedScope).Num(), 1);
	const int32 SourceCountBeforePrune = CharacterSource->Items.Num();
	FProfilePickerItem OldRename = PickerTest_Item(TEXT("BeforeRename"), 3);
	Store.TogglePin(SharedScope, OldRename.Identity);
	FProfilePickerItem NewRename = PickerTest_Item(TEXT("AfterRename"), 3);
	Store.Prune(SharedScope, TArray<FProfilePickerItem>({ Idle, Run, NewRename }));
	TestFalse(TEXT("Renamed/stale object identity is pruned"), Store.IsPinned(SharedScope, OldRename.Identity));
	TestEqual(TEXT("Config pruning never mutates source rows or assets"), CharacterSource->Items.Num(), SourceCountBeforePrune);

	for (int32 Index = 0; Index < FProfilePickerCatalogStore::MaxRecents + 7; ++Index)
	{
		Store.AddRecent(OtherScope, PickerTest_Item(FString::Printf(TEXT("Item%d"), Index), Index).Identity);
	}
	TestEqual(TEXT("Recent list is bounded"), Store.GetRecents(OtherScope).Num(), FProfilePickerCatalogStore::MaxRecents);
	const FString PinBoundScope = TEXT("Automation.PinBound");
	Store.ResetScopeForTests(PinBoundScope);
	for (int32 Index = 0; Index < FProfilePickerCatalogStore::MaxPins + 7; ++Index)
	{
		Store.TogglePin(PinBoundScope, PickerTest_Item(FString::Printf(TEXT("Pin%d"), Index), Index).Identity);
	}
	TestEqual(TEXT("Pin list is bounded and deduped"), Store.GetPins(PinBoundScope).Num(), FProfilePickerCatalogStore::MaxPins);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationPickerStableIdentityTest,
	"Paper2DPlus.Editor.ProfilePicker.AnimationStableIdentityAndNullState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationPickerStableIdentityTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	FFlipbookProfileEntry Idle;
	Idle.Identity.FlipbookName = TEXT("Idle");
	Idle.Identity.Flipbook = NewObject<UPaperFlipbook>(Profile, TEXT("IdleAsset"));
	FFlipbookProfileEntry Run;
	Run.Identity.FlipbookName = TEXT("Run");
	Run.Identity.Flipbook = NewObject<UPaperFlipbook>(Profile, TEXT("RunAsset"));
	Profile->Flipbooks = { Idle, Run };

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	Model->SetSelectedFlipbook(1);
	TSharedRef<FAnimationProfilePickerSource> Source = MakeShared<FAnimationProfilePickerSource>(Model);
	const FProfileItemIdentity RunIdentity = Source->GetSelectedIdentity();
	int32 SelectionBroadcasts = 0;
	Model->OnFlipbookSelectionChanged.AddLambda([&SelectionBroadcasts](int32) { ++SelectionBroadcasts; });

	Swap(Profile->Flipbooks[0], Profile->Flipbooks[1]);
	Model->NotifyAssetDataChanged();
	TestEqual(TEXT("Object-path identity follows reorder"), Model->GetSelectedFlipbookIndex(), 0);
	TestTrue(TEXT("Source identity remains the same object path"), Source->GetSelectedIdentity().Matches(RunIdentity));
	TestEqual(TEXT("Reorder broadcasts one selection change"), SelectionBroadcasts, 1);

	Profile->Flipbooks.RemoveAt(0);
	Model->NotifyAssetDataChanged();
	TestEqual(TEXT("Deleted identity becomes no selection, never a neighbor"), Model->GetSelectedFlipbookIndex(), INDEX_NONE);
	TestFalse(TEXT("Stale popup identity cannot select another row"), Source->SelectItem(RunIdentity));
	TestEqual(TEXT("Failed stale selection retains no selection"), Model->GetSelectedFlipbookIndex(), INDEX_NONE);

	Model->InitializeFromAsset(nullptr);
	TestEqual(TEXT("Null profile clears index"), Model->GetSelectedFlipbookIndex(), INDEX_NONE);
	TestFalse(TEXT("Null profile clears stable identity"), Source->GetSelectedIdentity().IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAnimationProfileSwitcherTest,
	"Paper2DPlus.Editor.ProfilePicker.AnimationSwitcherStableAdjacencyAndKeyboard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAnimationProfileSwitcherTest::RunTest(const FString& Parameters)
{
	TSharedRef<FPickerTestSource> Source = MakeShared<FPickerTestSource>();
	Source->Type = TEXT("Animation");
	Source->Scope = TEXT("Automation.AnimationSwitcher");
	FProfilePickerItem Third = PickerTest_Item(TEXT("Third"), 2);
	FProfilePickerItem First = PickerTest_Item(TEXT("First"), 0);
	FProfilePickerItem Second = PickerTest_Item(TEXT("Second"), 1);
	for (FProfilePickerItem* Item : { &Third, &First, &Second })
	{
		Item->Identity.SourceType = Source->Type;
	}
	Source->Items = { Third, First, Second };
	Source->Selected = Second.Identity;

	TSharedRef<SAnimationProfileSwitcher> Switcher = SNew(SAnimationProfileSwitcher)
		.Source(Source)
		.EmptySelectionText(FText::FromString(TEXT("Choose an animation")));
	FPickerScopedKeyboardFocus FocusGuard;
	FPickerScopedSlateWindow SlateWindow(Switcher);
	const bool bUsesRealSlateRouting = SlateWindow.IsValid();
	if (bUsesRealSlateRouting)
	{
		const int32 FocusRevisionBefore = Switcher->GetFocusChangeRevisionForTests();
		TestTrue(TEXT("Animation control accepts real Slate keyboard focus"),
			FSlateApplication::Get().SetKeyboardFocus(Switcher, EFocusCause::SetDirectly));
		SlateWindow.TickWidgets();
		TestTrue(TEXT("Animation control owns Slate keyboard focus"), Switcher->HasKeyboardFocus());
		TestTrue(TEXT("Focus transition invalidates the animation control indicator"),
			Switcher->GetFocusChangeRevisionForTests() > FocusRevisionBefore);
		TestTrue(TEXT("Keyboard focus indicator is visible for the focused animation control"),
			Switcher->IsKeyboardFocusIndicatorVisibleForTests());
	}
	else
	{
		FocusGuard.ClearForDirectDispatch();
		AddInfo(TEXT("Null-RHI/headless run uses direct animation-control key semantics; render-capable automation exercises real Slate focus and routed input."));
	}
	auto DispatchSwitcherKey = [&Switcher, bUsesRealSlateRouting](const FKey& Key)
	{
		return bUsesRealSlateRouting
			? FSlateApplication::Get().ProcessKeyDownEvent(PickerTest_KeyEvent(Key))
			: Switcher->OnKeyDown(FGeometry(), PickerTest_KeyEvent(Key)).IsEventHandled();
	};
	TestTrue(TEXT("Animation control accepts keyboard focus"), Switcher->SupportsKeyboardFocus());
	TestTrue(TEXT("Animation control constructs a visible-focus border"),
		Switcher->HasKeyboardFocusIndicatorForTests());
	TestEqual(TEXT("Compact animation search has no persistent previous or next rows"),
		Switcher->GetPersistentRowCountForTests(), 1);
	TestTrue(TEXT("Canonical predecessor is resolved by stable identity"),
		Switcher->GetPreviousIdentityForTests().Matches(First.Identity));
	TestTrue(TEXT("Current animation is resolved by stable identity"),
		Switcher->GetCurrentIdentityForTests().Matches(Second.Identity));
	TestTrue(TEXT("Canonical successor is resolved by stable identity"),
		Switcher->GetNextIdentityForTests().Matches(Third.Identity));
	TestTrue(TEXT("Accessible animation-control state names the current animation"),
		Switcher->GetAccessibleSummaryForTests().ToString().Contains(TEXT("Second")));

	TestTrue(TEXT("Focused Up selects the previous animation"),
		DispatchSwitcherKey(EKeys::Up));
	TestTrue(TEXT("Up routes one selection through the source"), Source->Selected.Matches(First.Identity));
	TestEqual(TEXT("Up emits one selection request"), Source->SelectCallCount, 1);
	TestFalse(TEXT("The first animation has no wrapped predecessor"),
		Switcher->GetPreviousIdentityForTests().IsValid());
	TestTrue(TEXT("Boundary Up remains handled by the focused animation control"),
		DispatchSwitcherKey(EKeys::Up));
	TestEqual(TEXT("Boundary Up does not emit another selection request"), Source->SelectCallCount, 1);

	TestTrue(TEXT("Focused Down selects the next animation"),
		DispatchSwitcherKey(EKeys::Down));
	TestTrue(TEXT("Down restores the stable middle identity"), Source->Selected.Matches(Second.Identity));
	TestEqual(TEXT("Down emits exactly one additional selection request"), Source->SelectCallCount, 2);

	First.CanonicalOrder = 2;
	Second.CanonicalOrder = 0;
	Third.CanonicalOrder = 1;
	Source->Items = { Third, First, Second };
	Source->NotifyChanged();
	TestFalse(TEXT("Reorder keeps the selected identity and moves it to the first boundary"),
		Switcher->GetPreviousIdentityForTests().IsValid());
	TestTrue(TEXT("Reorder derives the new successor without selecting it"),
		Switcher->GetNextIdentityForTests().Matches(Third.Identity));
	TestEqual(TEXT("Source refresh never rebroadcasts selection"), Source->SelectCallCount, 2);

	Source->Items.RemoveAll([&Second](const FProfilePickerItem& Item)
	{
		return Item.Identity.Matches(Second.Identity);
	});
	Source->NotifyChanged();
	TestFalse(TEXT("Removed current identity becomes an explicit empty state"),
		Switcher->GetCurrentIdentityForTests().IsValid());
	TestFalse(TEXT("A missing current identity never guesses a neighbor"),
		Switcher->GetNextIdentityForTests().IsValid());
	TestEqual(TEXT("Removal refresh never rebroadcasts selection"), Source->SelectCallCount, 2);

	FProfilePickerItem Unresolved = PickerTest_Item(TEXT("Unresolved"), 0);
	Unresolved.Identity.SourceType = TEXT("Animation");
	TestTrue(TEXT("Unresolved flipbooks still produce an explicit preview placeholder"),
		BuildAnimationProfileItemPreview(Unresolved).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfilePickerLargeVirtualizedFixtureTest,
	"Paper2DPlus.Editor.ProfilePicker.LargeVirtualizedFixtureAndEmptyState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfilePickerLargeVirtualizedFixtureTest::RunTest(const FString& Parameters)
{
	TSharedRef<FPickerTestSource> Source = MakeShared<FPickerTestSource>();
	Source->Scope = TEXT("Automation.LargeFixture");
	FProfilePickerCatalogStore::Get().ResetScopeForTests(Source->Scope);
	for (int32 Index = 0; Index < 200; ++Index)
	{
		FProfilePickerItem Item = PickerTest_Item(FString::Printf(TEXT("Animation_%03d"), Index), Index,
			FString::Printf(TEXT("Group_%d"), Index / 25));
		Item.Aliases.Add(FString::Printf(TEXT("alias_%03d"), Index));
		Source->Items.Add(MoveTemp(Item));
	}
	Source->Selected = Source->Items[42].Identity;
	int32 PreviewFactoryCalls = 0;

	TSharedRef<SProfileNavigatorPanel> Panel = SNew(SProfileNavigatorPanel)
		.Source(Source)
		.Mode(EProfileNavigatorMode::Pinned)
		.OnGenerateItemPreview(FOnGenerateProfileNavigatorItemPreview::CreateLambda(
			[&PreviewFactoryCalls](const FProfilePickerItem&)
			{
				++PreviewFactoryCalls;
				return TSharedPtr<SWidget>(SNew(SBox).WidthOverride(40.0f).HeightOverride(40.0f));
			}));
	TestEqual(TEXT("All 200 source rows are searchable"), Panel->GetResultCountForTests(), 200);
	TestEqual(TEXT("SListView does not eagerly construct all rows during widget construction"),
		Panel->GetConstructedRowCountForTests(), 0);
	TestEqual(TEXT("Preview widgets are not eagerly constructed with source results"), PreviewFactoryCalls, 0);
	const int32 VisibleRows = Panel->GenerateRowsForViewportForTests(FVector2D(400.0f, 240.0f));
	TestTrue(TEXT("A constrained viewport constructs visible rows"), VisibleRows > 0);
	TestTrue(TEXT("A constrained viewport does not construct all 200 source rows"), VisibleRows < 200);
	TestTrue(TEXT("Only generated item rows ask for previews"), PreviewFactoryCalls > 0 && PreviewFactoryCalls <= VisibleRows);
	TestEqual(TEXT("Navigator counts only previews returned for constructed rows"),
		Panel->GetConstructedPreviewCountForTests(), PreviewFactoryCalls);
	Panel->SetQueryForTests(TEXT("alias_199"));
	TestEqual(TEXT("Large fixture alias search narrows deterministically"), Panel->GetResultCountForTests(), 1);
	TestEqual(TEXT("A query refresh clears stale constructed-preview accounting"),
		Panel->GetConstructedPreviewCountForTests(), 0);
	Panel->SetQueryForTests(TEXT("definitely-no-match"));
	TestEqual(TEXT("No-match state has zero results"), Panel->GetResultCountForTests(), 0);
	TestTrue(TEXT("No-match search retains current selection"), Source->Selected.Matches(Source->Items[42].Identity));

	TSharedRef<SProfileNavigatorPanel> TextOnlyPanel = SNew(SProfileNavigatorPanel)
		.Source(Source)
		.Mode(EProfileNavigatorMode::Pinned);
	TextOnlyPanel->SetQueryForTests(TEXT("alias_042"));
	TextOnlyPanel->GenerateRowsForViewportForTests(FVector2D(400.0f, 240.0f));
	TestEqual(TEXT("A source without a preview renderer remains text-only"),
		TextOnlyPanel->GetConstructedPreviewCountForTests(), 0);
	return true;
}

#endif // WITH_EDITOR
