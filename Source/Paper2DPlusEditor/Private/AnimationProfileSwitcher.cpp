// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AnimationProfileSwitcher.h"

#include "EditorCanvasUtils.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "PaperFlipbook.h"
#include "SlateShortcutUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "AnimationProfileSwitcher"

namespace AnimationProfileSwitcherPrivate
{
	TSharedRef<SWidget> BuildEmptyPreview()
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("—")))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
	}
}

TSharedPtr<SWidget> BuildAnimationProfileItemPreview(const FProfilePickerItem& Item)
{
	UPaperFlipbook* Flipbook = Item.Identity.ObjectPath.IsNull()
		? nullptr
		: Cast<UPaperFlipbook>(Item.Identity.ObjectPath.ResolveObject());
	return SNew(SBox)
		.WidthOverride(40.0f)
		.HeightOverride(40.0f)
		[
			SNew(SFlipbookThumbnail)
			.Flipbook(Flipbook)
		];
}

void SAnimationProfileSwitcher::Construct(const FArguments& InArgs)
{
	Source = InArgs._Source;
	EmptySelectionText = InArgs._EmptySelectionText;
	if (!EmptySelectionText.IsSet())
	{
		EmptySelectionText = LOCTEXT("DefaultEmptySelection", "Select an animation");
	}
	// Animation wording stays the default so Character and Layer are untouched; Combat overrides both
	// so an attack is never captioned "Current animation".
	CaptionText = InArgs._CaptionText;
	if (!CaptionText.IsSet())
	{
		CaptionText = LOCTEXT("CurrentAnimation", "Current animation");
	}
	ItemNounText = InArgs._ItemNounText;
	if (!ItemNounText.IsSet())
	{
		ItemNounText = LOCTEXT("DefaultItemNoun", "animation");
	}
	if (Source.IsValid())
	{
		SourceChangedHandle = Source->OnSourceChanged().AddSP(
			this, &SAnimationProfileSwitcher::RefreshFromSource);
	}

#if WITH_ACCESSIBILITY
	SetAccessibleBehavior(
		EAccessibleBehavior::Custom,
		TAttribute<FText>::CreateSP(this, &SAnimationProfileSwitcher::GetAccessibleSummary));
#endif

	ChildSlot
	[
		SAssignNew(KeyboardFocusBorder, SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(this, &SAnimationProfileSwitcher::GetFocusBorderColor)
		.Padding(2.0f)
		.ToolTipText(LOCTEXT(
			"SwitcherToolTip",
			"Click the current animation control to focus it. Up and Down select adjacent animations; navigation stops at the ends."))
		.OnMouseButtonDown(this, &SAnimationProfileSwitcher::HandleSwitcherMouseButtonDown)
		[
			BuildCurrentRow()
		]
	];

	RefreshFromSource();
}

SAnimationProfileSwitcher::~SAnimationProfileSwitcher()
{
	if (Source.IsValid())
	{
		Source->OnSourceChanged().Remove(SourceChangedHandle);
	}
}

void SAnimationProfileSwitcher::OnFocusChanging(
	const FWeakWidgetPath& PreviousFocusPath,
	const FWidgetPath& NewWidgetPath,
	const FFocusEvent& InFocusEvent)
{
	SCompoundWidget::OnFocusChanging(PreviousFocusPath, NewWidgetPath, InFocusEvent);
#if WITH_DEV_AUTOMATION_TESTS
	++FocusChangeRevision;
#endif
	if (KeyboardFocusBorder.IsValid())
	{
		KeyboardFocusBorder->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FReply SAnimationProfileSwitcher::OnKeyDown(
	const FGeometry& MyGeometry,
	const FKeyEvent& InKeyEvent)
{
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
	}
	if (!InKeyEvent.IsControlDown() && !InKeyEvent.IsAltDown() && !InKeyEvent.IsCommandDown())
	{
		if (InKeyEvent.GetKey() == EKeys::Up)
		{
			NavigateAdjacent(-1);
			return FReply::Handled();
		}
		if (InKeyEvent.GetKey() == EKeys::Down)
		{
			NavigateAdjacent(1);
			return FReply::Handled();
		}
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SAnimationProfileSwitcher::RefreshFromSource()
{
	OrderedItems.Reset();
	CurrentItemIndex = INDEX_NONE;
	if (Source.IsValid())
	{
		Source->GetItems(OrderedItems);
		OrderedItems.StableSort([](const FProfilePickerItem& A, const FProfilePickerItem& B)
		{
			const int32 AOrder = A.CanonicalOrder == INDEX_NONE ? MAX_int32 : A.CanonicalOrder;
			const int32 BOrder = B.CanonicalOrder == INDEX_NONE ? MAX_int32 : B.CanonicalOrder;
			return AOrder < BOrder;
		});

		const FProfileItemIdentity SelectedIdentity = Source->GetSelectedIdentity();
		CurrentItemIndex = OrderedItems.IndexOfByPredicate(
			[&SelectedIdentity](const FProfilePickerItem& Item)
			{
				return Item.Identity.Matches(SelectedIdentity);
			});
	}
	RefreshCurrentPreview();
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

bool SAnimationProfileSwitcher::NavigateAdjacent(int32 Delta)
{
	if (!Source.IsValid() || (Delta != -1 && Delta != 1))
	{
		return false;
	}
	const FProfilePickerItem* Target = GetRelativeItem(Delta);
	return Target && Source->SelectItem(Target->Identity);
}

const FProfilePickerItem* SAnimationProfileSwitcher::GetRelativeItem(int32 Delta) const
{
	if (CurrentItemIndex == INDEX_NONE)
	{
		return nullptr;
	}
	const int32 TargetIndex = CurrentItemIndex + Delta;
	return OrderedItems.IsValidIndex(TargetIndex) ? &OrderedItems[TargetIndex] : nullptr;
}

TSharedRef<SWidget> SAnimationProfileSwitcher::BuildCurrentRow()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.Padding(FMargin(4.0f, 3.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(CurrentPreviewHost, SBox)
				.WidthOverride(44.0f)
				.HeightOverride(44.0f)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(CaptionText)
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SProfileItemPicker)
					.Source(Source)
					.EmptySelectionText(EmptySelectionText)
					.OnGenerateItemPreview(FOnGenerateProfileNavigatorItemPreview::CreateStatic(
						&BuildAnimationProfileItemPreview))
				]
			]
		];
}

void SAnimationProfileSwitcher::RefreshCurrentPreview()
{
	if (!CurrentPreviewHost.IsValid())
	{
		return;
	}
	const FProfilePickerItem* CurrentItem = OrderedItems.IsValidIndex(CurrentItemIndex)
		? &OrderedItems[CurrentItemIndex]
		: nullptr;
	CurrentPreviewHost->SetContent(CurrentItem
		? BuildAnimationProfileItemPreview(*CurrentItem).ToSharedRef()
		: AnimationProfileSwitcherPrivate::BuildEmptyPreview());
}

FText SAnimationProfileSwitcher::GetRelativeLabel(int32 Delta) const
{
	if (const FProfilePickerItem* Item = GetRelativeItem(Delta))
	{
		return Item->Label;
	}
	return Delta < 0
		? FText::Format(LOCTEXT("NoPreviousAnimation", "Start of {0} list"), ItemNounText.Get())
		: FText::Format(LOCTEXT("NoNextAnimation", "End of {0} list"), ItemNounText.Get());
}

FText SAnimationProfileSwitcher::GetAccessibleSummary() const
{
	const FProfilePickerItem* Current = OrderedItems.IsValidIndex(CurrentItemIndex)
		? &OrderedItems[CurrentItemIndex]
		: nullptr;
	const FText CurrentText = Current ? Current->Label : EmptySelectionText.Get();
	return FText::Format(
		LOCTEXT(
			"AccessibleSummaryFormat",
			"{0} navigation. Current: {1}. Up: {2}. Down: {3}. Use Up and Down when focused."),
		FText::AsCultureInvariant(ItemNounText.Get().ToString()),
		CurrentText,
		GetRelativeLabel(-1),
		GetRelativeLabel(1));
}

FSlateColor SAnimationProfileSwitcher::GetFocusBorderColor() const
{
	const bool bFocused = HasKeyboardFocus()
		|| (FSlateApplication::IsInitialized()
			&& FSlateApplication::Get().HasFocusedDescendants(SharedThis(this)));
	return bFocused
		? FSlateColor(FLinearColor(0.08f, 0.32f, 0.52f, 1.0f))
		: FSlateColor(FLinearColor(0.03f, 0.03f, 0.03f, 0.75f));
}

FReply SAnimationProfileSwitcher::HandleSwitcherMouseButtonDown(
	const FGeometry& /*Geometry*/,
	const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}
	return FReply::Unhandled();
}

#if WITH_DEV_AUTOMATION_TESTS
FProfileItemIdentity SAnimationProfileSwitcher::GetPreviousIdentityForTests() const
{
	const FProfilePickerItem* Item = GetRelativeItem(-1);
	return Item ? Item->Identity : FProfileItemIdentity();
}

FProfileItemIdentity SAnimationProfileSwitcher::GetCurrentIdentityForTests() const
{
	return OrderedItems.IsValidIndex(CurrentItemIndex)
		? OrderedItems[CurrentItemIndex].Identity
		: FProfileItemIdentity();
}

FProfileItemIdentity SAnimationProfileSwitcher::GetNextIdentityForTests() const
{
	const FProfilePickerItem* Item = GetRelativeItem(1);
	return Item ? Item->Identity : FProfileItemIdentity();
}

FText SAnimationProfileSwitcher::GetAccessibleSummaryForTests() const
{
	return GetAccessibleSummary();
}

bool SAnimationProfileSwitcher::IsKeyboardFocusIndicatorVisibleForTests() const
{
	return HasKeyboardFocus()
		|| (FSlateApplication::IsInitialized()
			&& FSlateApplication::Get().HasFocusedDescendants(SharedThis(this)));
}

int32 SAnimationProfileSwitcher::GetPersistentRowCountForTests() const
{
	return CurrentPreviewHost.IsValid() ? 1 : 0;
}
#endif

#undef LOCTEXT_NAMESPACE
