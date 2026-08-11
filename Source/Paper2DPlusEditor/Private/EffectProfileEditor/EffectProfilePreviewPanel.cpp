// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "EffectProfileEditor/EffectProfilePreviewPanel.h"

#include "Editor.h"
#include "EditorCanvasUtils.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "PaperSpriteAtlas.h"
#include "SlateShortcutUtils.h"
#include "Subsystems/ImportSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#if WITH_ACCESSIBILITY
#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#endif
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "EffectProfilePreviewPanel"

namespace
{
	int32 CountDescendantWidgetsByType(SWidget& Widget, FName WidgetType)
	{
		int32 Count = Widget.GetType() == WidgetType ? 1 : 0;
		if (FChildren* Children = Widget.GetChildren())
		{
			for (int32 Index = 0; Index < Children->Num(); ++Index)
			{
				Count += CountDescendantWidgetsByType(Children->GetChildAt(Index).Get(), WidgetType);
			}
		}
		return Count;
	}
}

class SEffectProfileFramePreview final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SEffectProfileFramePreview) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&)
	{
		SetClipping(EWidgetClipping::ClipToBounds);
		StatusText = LOCTEXT("NoSelectionCanvas", "Select an effect to preview");
	}

	void SetFrame(UPaperFlipbook* InFlipbook, int32 InKeyFrame, bool bForceRefresh = false)
	{
		if (!bForceRefresh && Flipbook.Get() == InFlipbook && KeyFrame == InKeyFrame)
		{
			return;
		}
		Flipbook.Reset(InFlipbook);
		KeyFrame = InKeyFrame;
		UpdateBrush();
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	UObject* GetBrushResourceForTests() const { return Brush.GetResourceObject(); }

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(320.0f, 240.0f);
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override
	{
		if (!FSlateRect::DoRectanglesIntersect(
			AllottedGeometry.GetLayoutBoundingRect(), MyCullingRect))
		{
			return LayerId;
		}
		FEditorCanvasUtils::DrawCheckerboard(
			OutDrawElements, LayerId, AllottedGeometry, 16.0f);
		if (bHasTexture)
		{
			const FVector2D Available = AllottedGeometry.GetLocalSize();
			const float FitScale = SpriteSize.X > 0.0f && SpriteSize.Y > 0.0f
				? FMath::Min(Available.X / SpriteSize.X, Available.Y / SpriteSize.Y)
				: 1.0f;
			const FVector2D DrawSize = SpriteSize * FMath::Max(FitScale, 0.0f);
			const FVector2D DrawOffset = (Available - DrawSize) * 0.5f;
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 1,
				MakePaintGeometry(
					AllottedGeometry,
					DrawSize,
					FSlateLayoutTransform(DrawOffset)),
				&Brush,
				ESlateDrawEffect::None,
				FLinearColor::White);
		}
		else
		{
			FSlateDrawElement::MakeText(
				OutDrawElements,
				LayerId + 1,
				AllottedGeometry.ToPaintGeometry(),
				StatusText,
				FCoreStyle::GetDefaultFontStyle("Regular", 10),
				ESlateDrawEffect::None,
				FLinearColor(0.8f, 0.8f, 0.82f));
		}
		return LayerId + 1;
	}

private:
	void UpdateBrush()
	{
		bHasTexture = false;
		SpriteSize = FVector2D::ZeroVector;
		Brush = FSlateBrush();
		UPaperFlipbook* Current = Flipbook.Get();
		if (!Current)
		{
			StatusText = LOCTEXT("NoSelectionCanvas", "Select an effect to preview");
			return;
		}
		if (Current->GetNumKeyFrames() <= 0)
		{
			StatusText = LOCTEXT("NoFramesCanvas", "Selected flipbook has no frames");
			return;
		}
		const int32 SafeFrame = FMath::Clamp(KeyFrame, 0, Current->GetNumKeyFrames() - 1);
		UPaperSprite* Sprite = Current->GetKeyFrameChecked(SafeFrame).Sprite;
		if (!Sprite)
		{
			StatusText = LOCTEXT("NoSpriteCanvas", "Current frame has no sprite");
			return;
		}
		if (!FSpriteSlateAtlasUtils::ConfigureBrush(Sprite, Brush, &SpriteSize))
		{
			StatusText = LOCTEXT("NoTextureCanvas", "Current sprite has no texture");
			return;
		}
		bHasTexture = true;
		StatusText = FText::GetEmpty();
	}

	TStrongObjectPtr<UPaperFlipbook> Flipbook;
	FSlateBrush Brush;
	FText StatusText;
	FVector2D SpriteSize = FVector2D::ZeroVector;
	int32 KeyFrame = 0;
	bool bHasTexture = false;
};

void SEffectProfilePreviewPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	if (Model.IsValid())
	{
		SelectionChangedHandle = Model->OnSelectedEffectChanged().AddSP(
			this, &SEffectProfilePreviewPanel::HandleSelectedEffectChanged);
		SourceChangedHandle = Model->OnSourceChanged().AddSP(
			this, &SEffectProfilePreviewPanel::HandleModelSourceChanged);
	}
	ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(
		this, &SEffectProfilePreviewPanel::HandleSourceObjectChanged);
	if (GEditor)
	{
		if (UImportSubsystem* ImportSubsystem =
			GEditor->GetEditorSubsystem<UImportSubsystem>())
		{
			AssetReimportHandle = ImportSubsystem->OnAssetReimport.AddSP(
				this, &SEffectProfilePreviewPanel::HandleAssetReimported);
		}
	}
	Playback.SetFlipbook(Model.IsValid() ? Model->GetSelectedFlipbook() : nullptr);
	RefreshSourceDependencies();
#if WITH_ACCESSIBILITY
	SetAccessibleBehavior(
		EAccessibleBehavior::Custom,
		TAttribute<FText>::CreateLambda([this]() { return GetPreviewAccessibleText(); }));
#endif

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(5.0f)
			.ToolTipText(LOCTEXT(
				"PreviewShortcutTip",
				"Click the preview to focus it. Left/Right choose adjacent frames; Home/End choose first/last; Space plays or pauses."))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SEffectProfilePreviewPanel::GetSelectionTitle)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(this, &SEffectProfilePreviewPanel::GetPlaybackStatusText)
						.ToolTipText(LOCTEXT("PlaybackStatusTip", "Space plays or pauses. Left/Right and Home/End select frames."))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "NoBorder")
						.ContentPadding(FMargin(3.0f, 1.0f))
						.Text(LOCTEXT("OpenSource", "Open Flipbook…"))
						.ToolTipText(LOCTEXT("OpenSourceTip", "Open the selected source Paper Flipbook."))
						.AccessibleText(LOCTEXT("OpenSourceAccessible", "Open selected source Paper Flipbook"))
						.IsEnabled_Lambda([this]() { return Playback.GetFlipbook() != nullptr; })
						.OnClicked(this, &SEffectProfilePreviewPanel::HandleOpenSourceClicked)
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SBox)
					.MinDesiredWidth(240.0f)
					.MinDesiredHeight(180.0f)
					[
						SAssignNew(PreviewCanvas, SEffectProfileFramePreview)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					BuildFrameStrip()
				]
			]
		]
		+ SOverlay::Slot()
		[
			SAssignNew(KeyboardFocusBorder, SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("FocusRectangle"))
			.BorderBackgroundColor(this, &SEffectProfilePreviewPanel::GetKeyboardFocusIndicatorColor)
			.Padding(0.0f)
			.Visibility(EVisibility::HitTestInvisible)
		]
	];
	RefreshPreviewFrame();
}

SEffectProfilePreviewPanel::~SEffectProfilePreviewPanel()
{
	StopPlaybackTimer();
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
	if (GEditor)
	{
		if (UImportSubsystem* ImportSubsystem =
			GEditor->GetEditorSubsystem<UImportSubsystem>())
		{
			ImportSubsystem->OnAssetReimport.Remove(AssetReimportHandle);
		}
	}
	if (Model.IsValid())
	{
		Model->OnSelectedEffectChanged().Remove(SelectionChangedHandle);
		Model->OnSourceChanged().Remove(SourceChangedHandle);
	}
}

void SEffectProfilePreviewPanel::HandleSelectedEffectChanged(UPaperFlipbook* Flipbook)
{
	StopPlaybackTimer();
	Playback.SetFlipbook(Flipbook);
	ForceRefreshSource();
}

void SEffectProfilePreviewPanel::HandleModelSourceChanged()
{
	// Display labels, flipbook frames, sprite atlas assignments, and reimported textures can all change
	// while the selected object path and frame index remain identical.
	ForceRefreshSource();
}

void SEffectProfilePreviewPanel::HandleSourceObjectChanged(
	UObject* Object,
	FPropertyChangedEvent& Event)
{
	(void)Event;
	if (IsSourceDependency(Object))
	{
		ForceRefreshSource();
	}
}

void SEffectProfilePreviewPanel::HandleAssetReimported(UObject* Object)
{
	if (IsSourceDependency(Object))
	{
		ForceRefreshSource();
	}
}

FReply SEffectProfilePreviewPanel::HandleOpenSourceClicked()
{
	if (Model.IsValid())
	{
		Model->OpenSelectedSource();
	}
	return FReply::Handled();
}

FReply SEffectProfilePreviewPanel::HandleFrameClicked(
	int32 FrameIndex,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	SeekToFrame(FrameIndex);
	return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

bool SEffectProfilePreviewPanel::HandleFrameNavigationKey(const FKey& Key)
{
	// Panel-level navigation commits selection immediately, so keyboard users do not need a second
	// Enter/Space activation step. Space remains unambiguously reserved for preview play/pause.
	const int32 FrameCount = Playback.GetNumKeyFrames();
	if (FrameCount <= 0)
	{
		return false;
	}

	int32 TargetFrame = Playback.GetCurrentKeyFrame();
	if (Key == EKeys::Left)
	{
		--TargetFrame;
	}
	else if (Key == EKeys::Right)
	{
		++TargetFrame;
	}
	else if (Key == EKeys::Home)
	{
		TargetFrame = 0;
	}
	else if (Key == EKeys::End)
	{
		TargetFrame = FrameCount - 1;
	}
	else
	{
		return false;
	}

	return SeekToFrame(FMath::Clamp(TargetFrame, 0, FrameCount - 1));
}

bool SEffectProfilePreviewPanel::SeekToFrame(int32 FrameIndex)
{
	const int32 FrameCount = Playback.GetNumKeyFrames();
	if (FrameCount <= 0)
	{
		return false;
	}

	Playback.Pause();
	Playback.SeekKeyFrame(FMath::Clamp(FrameIndex, 0, FrameCount - 1));
	StopPlaybackTimer();
	RefreshPreviewFrame();
	if (FrameStripBox.IsValid())
	{
		FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	}
	ScrollSelectedFrameIntoView();
	Invalidate(EInvalidateWidgetReason::Paint);
	NotifyAccessibleStateChanged();
	return true;
}

TSharedRef<SWidget> SEffectProfilePreviewPanel::BuildFrameStrip()
{
	SAssignNew(FrameStripBox, SHorizontalBox);
	RefreshFrameStrip();
	return SAssignNew(FrameStripScrollBox, SScrollBox)
		.Orientation(Orient_Horizontal)
		+ SScrollBox::Slot()
		[
			FrameStripBox.ToSharedRef()
		];
}

void SEffectProfilePreviewPanel::RefreshFrameStrip()
{
	if (!FrameStripBox.IsValid()) return;
	FrameStripBox->ClearChildren();
	FrameCellWidgets.Reset();
	++FrameStripRefreshRevision;
	UPaperFlipbook* Flipbook = Playback.GetFlipbook();
	const int32 FrameCount = Flipbook ? Flipbook->GetNumKeyFrames() : 0;
	for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
	{
		FFrameStripCellArgs CellArgs;
		CellArgs.FrameIndex = FrameIndex;
		CellArgs.Sprite = Flipbook->GetKeyFrameChecked(FrameIndex).Sprite;
		CellArgs.IsSelected = [this, FrameIndex]()
		{
			return Playback.GetCurrentKeyFrame() == FrameIndex;
		};
		CellArgs.OnMouseButtonDown = [this, FrameIndex](const FPointerEvent& MouseEvent)
		{
			return HandleFrameClicked(FrameIndex, MouseEvent);
		};
		const TSharedRef<SWidget> FrameCell = FFrameStripCellUtils::Build(CellArgs);
		FrameCellWidgets.Add(FrameCell);
		FrameStripBox->AddSlot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			FrameCell
		];
	}
}

void SEffectProfilePreviewPanel::ScrollSelectedFrameIntoView()
{
	const int32 SelectedFrame = Playback.GetCurrentKeyFrame();
	if (FrameStripScrollBox.IsValid() && FrameCellWidgets.IsValidIndex(SelectedFrame))
	{
		FrameStripScrollBox->ScrollDescendantIntoView(
			FrameCellWidgets[SelectedFrame].ToSharedRef(),
			true);
	}
}

void SEffectProfilePreviewPanel::RefreshPreviewFrame(bool bForceRefresh)
{
	if (PreviewCanvas.IsValid())
	{
		PreviewCanvas->SetFrame(
			Playback.GetFlipbook(),
			Playback.GetCurrentKeyFrame(),
			bForceRefresh);
		++PreviewRefreshRevision;
	}
}

UObject* SEffectProfilePreviewPanel::GetPreviewBrushResourceForTests() const
{
	return PreviewCanvas.IsValid() ? PreviewCanvas->GetBrushResourceForTests() : nullptr;
}

int32 SEffectProfilePreviewPanel::GetFrameStripFrameCountForTests() const
{
	return FrameStripBox.IsValid() ? FrameStripBox->GetChildren()->Num() : 0;
}

int32 SEffectProfilePreviewPanel::CountDescendantWidgetsForTests(FName WidgetType) const
{
	return WidgetType.IsNone()
		? 0
		: CountDescendantWidgetsByType(const_cast<SEffectProfilePreviewPanel&>(*this), WidgetType);
}

bool SEffectProfilePreviewPanel::HasKeyboardFocusIndicatorForTests() const
{
	return KeyboardFocusBorder.IsValid();
}

bool SEffectProfilePreviewPanel::IsKeyboardFocusIndicatorVisibleForTests() const
{
	return KeyboardFocusBorder.IsValid()
		&& GetKeyboardFocusIndicatorColor().GetSpecifiedColor().A > 0.0f;
}

float SEffectProfilePreviewPanel::GetFrameStripScrollOffsetForTests() const
{
	return FrameStripScrollBox.IsValid() ? FrameStripScrollBox->GetScrollOffset() : 0.0f;
}

FText SEffectProfilePreviewPanel::GetAccessibleSummaryForTests() const
{
	return GetPreviewAccessibleText();
}

void SEffectProfilePreviewPanel::ForceRefreshSource()
{
	Playback.SeekKeyFrame(Playback.GetCurrentKeyFrame());
	// A single-frame source has nothing to advance. Reimport can shrink a playing source in place,
	// so retire its active timer here just as TogglePlayback does for an initially static source.
	if (Playback.GetNumKeyFrames() <= 1)
	{
		Playback.Pause();
		StopPlaybackTimer();
	}
	RefreshSourceDependencies();
	RefreshPreviewFrame(true);
	RefreshFrameStrip();
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SEffectProfilePreviewPanel::RefreshSourceDependencies()
{
	SourceDependencies.Reset();
	auto AddDependency = [this](UObject* Dependency)
	{
		if (Dependency)
		{
			SourceDependencies.Add(TWeakObjectPtr<UObject>(Dependency));
		}
	};

	UPaperFlipbook* Flipbook = Playback.GetFlipbook();
	AddDependency(Flipbook);
	if (!Flipbook)
	{
		return;
	}

	for (int32 FrameIndex = 0; FrameIndex < Flipbook->GetNumKeyFrames(); ++FrameIndex)
	{
		UPaperSprite* Sprite = Flipbook->GetKeyFrameChecked(FrameIndex).Sprite;
		AddDependency(Sprite);
		if (!Sprite)
		{
			continue;
		}

		// Atlas-backed sprites draw from Baked/AtlasTexture, but editor reimport broadcasts the original
		// source sheet. Track both identities so the same selected sprite refreshes after either changes.
		AddDependency(Sprite->GetSourceTexture());
		AddDependency(Sprite->GetBakedTexture());
		const FSlateAtlasData AtlasData = Sprite->GetSlateAtlasData();
		AddDependency(AtlasData.AtlasTexture);
		AddDependency(const_cast<UPaperSpriteAtlas*>(Sprite->GetAtlasGroup()));
	}
}

bool SEffectProfilePreviewPanel::IsSourceDependency(const UObject* Object) const
{
	if (!Object)
	{
		return false;
	}
	for (const TWeakObjectPtr<UObject>& Dependency : SourceDependencies)
	{
		if (Dependency.Get() == Object)
		{
			return true;
		}
	}
	return false;
}

FReply SEffectProfilePreviewPanel::OnKeyDown(
	const FGeometry& MyGeometry,
	const FKeyEvent& InKeyEvent)
{
	(void)MyGeometry;
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return FReply::Unhandled();
	}
	if (!InKeyEvent.IsControlDown()
		&& !InKeyEvent.IsAltDown()
		&& HandleFrameNavigationKey(InKeyEvent.GetKey()))
	{
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::SpaceBar && !InKeyEvent.IsControlDown())
	{
		TogglePlayback();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SEffectProfilePreviewPanel::OnFocusChanging(
	const FWeakWidgetPath& PreviousFocusPath,
	const FWidgetPath& NewWidgetPath,
	const FFocusEvent& InFocusEvent)
{
	SCompoundWidget::OnFocusChanging(PreviousFocusPath, NewWidgetPath, InFocusEvent);
	++FocusChangeRevision;
	LastFocusCause = InFocusEvent.GetCause();
	if (KeyboardFocusBorder.IsValid())
	{
		KeyboardFocusBorder->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FReply SEffectProfilePreviewPanel::OnMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	(void)MyGeometry;
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}
	return FReply::Unhandled();
}

void SEffectProfilePreviewPanel::TogglePlayback()
{
	if (Playback.GetNumKeyFrames() <= 1)
	{
		Playback.Pause();
		StopPlaybackTimer();
		Invalidate(EInvalidateWidgetReason::Paint);
		NotifyAccessibleStateChanged();
		return;
	}
	Playback.TogglePlay();
	if (Playback.IsPlaying())
	{
		EnsurePlaybackTimer();
	}
	else
	{
		StopPlaybackTimer();
	}
	Invalidate(EInvalidateWidgetReason::Paint);
	NotifyAccessibleStateChanged();
}

void SEffectProfilePreviewPanel::NotifyAccessibleStateChanged()
{
#if WITH_ACCESSIBILITY
	if (!FSlateApplication::IsInitialized() || !HasAnyUserFocusOrFocusedDescendants())
	{
		return;
	}

	const FString Announcement = GetPreviewAccessibleText().ToString();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	FSlateApplication::Get().GetAccessibleMessageHandler()->OnWidgetEventRaised(
		SharedThis(this),
		EAccessibleEvent::Notification,
		FString(),
		Announcement);
#else
	FSlateApplication::Get().GetAccessibleMessageHandler()->OnWidgetEventRaised(
		FSlateAccessibleMessageHandler::FSlateWidgetAccessibleEventArgs(
			SharedThis(this),
			EAccessibleEvent::Notification,
			FString(),
			Announcement));
#endif
	++AccessibleAnnouncementRevision;
#endif
}

void SEffectProfilePreviewPanel::EnsurePlaybackTimer()
{
	if (!Playback.IsPlaying() || PlaybackTimerHandle.IsValid())
	{
		return;
	}
	PlaybackTimerHandle.Reset();
	PlaybackTimerHandle = RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateSP(
			this, &SEffectProfilePreviewPanel::HandlePlaybackTimer));
}

void SEffectProfilePreviewPanel::StopPlaybackTimer()
{
	if (const TSharedPtr<FActiveTimerHandle> Timer = PlaybackTimerHandle.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	PlaybackTimerHandle.Reset();
}

EActiveTimerReturnType SEffectProfilePreviewPanel::HandlePlaybackTimer(
	double CurrentTime,
	float DeltaSeconds)
{
	(void)CurrentTime;
	const int32 PreviousFrame = Playback.GetCurrentKeyFrame();
	const bool bWasPlaying = Playback.IsPlaying();
	Playback.Tick(DeltaSeconds);
	if (Playback.GetCurrentKeyFrame() != PreviousFrame)
	{
		RefreshPreviewFrame();
		if (FrameStripBox.IsValid())
		{
			FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
		}
	}
	if (bWasPlaying != Playback.IsPlaying()
		|| Playback.GetCurrentKeyFrame() != PreviousFrame)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	if (!Playback.IsPlaying() || !Playback.GetFlipbook())
	{
		PlaybackTimerHandle.Reset();
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

void SEffectProfilePreviewPanel::AdvancePlaybackForTests(float DeltaSeconds)
{
	const int32 PreviousFrame = Playback.GetCurrentKeyFrame();
	Playback.Tick(DeltaSeconds);
	if (Playback.GetCurrentKeyFrame() != PreviousFrame)
	{
		RefreshPreviewFrame();
		if (FrameStripBox.IsValid()) FrameStripBox->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FText SEffectProfilePreviewPanel::GetSelectionTitle() const
{
	if (!Model.IsValid() || !Model->GetSelectedEntry())
	{
		return LOCTEXT("NoPreviewSelection", "No effect selected");
	}
	const FPaper2DPlusEffectProfileEntry* Entry = Model->GetSelectedEntry();
	UPaperFlipbook* SelectedFlipbook = Model->GetSelectedFlipbook();
	return FText::Format(
		LOCTEXT("PreviewTitle", "{0}"),
		Entry->DisplayLabel.IsEmpty()
			? (SelectedFlipbook
				? FText::FromString(SelectedFlipbook->GetName())
				: LOCTEXT("MissingPreviewFlipbook", "Missing source flipbook"))
			: Entry->DisplayLabel);
}

FText SEffectProfilePreviewPanel::GetPlaybackStatusText() const
{
	if (Playback.GetNumKeyFrames() <= 1)
	{
		return Playback.GetFrameStatusText();
	}
	return FText::Format(
		Playback.IsPlaying()
			? LOCTEXT("PlayingStatus", "{0}  |  Playing (Space)")
			: LOCTEXT("PausedStatus", "{0}  |  Paused (Space)"),
		Playback.GetFrameStatusText());
}

FText SEffectProfilePreviewPanel::GetPreviewAccessibleText() const
{
	const int32 FrameCount = Playback.GetNumKeyFrames();
	if (FrameCount <= 0)
	{
		return FText::Format(LOCTEXT(
			"PreviewAccessibleNoFrames",
			"Effect preview for {0}. No frame is available. Space plays or pauses when the selected flipbook has animation frames."),
			GetSelectionTitle());
	}

	return FText::Format(
		LOCTEXT(
			"PreviewAccessibleWithFrame",
			"Effect preview for {0}. Frame {1} of {2} selected. {3}. Left and Right select adjacent frames. Home and End select the first and last frame. Space plays or pauses."),
		GetSelectionTitle(),
		FText::AsNumber(Playback.GetCurrentKeyFrame() + 1),
		FText::AsNumber(FrameCount),
		Playback.IsPlaying() ? LOCTEXT("AccessiblePlaying", "Playing") : LOCTEXT("AccessiblePaused", "Paused"));
}

FSlateColor SEffectProfilePreviewPanel::GetKeyboardFocusIndicatorColor() const
{
	// Mouse focus draws nothing: clicking the preview is how you scrub, and a permanent rectangle
	// around the whole surface afterwards reads as a stuck highlight rather than a focus cue.
	// Keyboard and programmatic focus still show the indicator.
	return HasAnyUserFocusOrFocusedDescendants() && LastFocusCause != EFocusCause::Mouse
		? FSlateColor(FLinearColor::White)
		: FSlateColor(FLinearColor::Transparent);
}

#undef LOCTEXT_NAMESPACE
