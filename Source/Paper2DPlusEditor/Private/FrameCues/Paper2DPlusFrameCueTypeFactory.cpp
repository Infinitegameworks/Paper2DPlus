// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCueTypeFactory.h"

#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueTypeBirthReady.h"
#include "Paper2DPlusEditorModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FeedbackContext.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusFrameCueTypeFactory"

namespace Paper2DPlusFrameCueTypeFactoryInternal
{
	class SFrameCueTypeKindDialog final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SFrameCueTypeKindDialog) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			ChildSlot
			[
				SNew(SBorder)
				.Padding(FMargin(18.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT(
							"ChooseCueKindPrompt",
							"Choose the lifecycle for this reusable behavior-capable Cue Type."))
						.AutoWrapText(true)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						MakeKindButton(
							EPaper2DPlusFrameCueTypeKind::Moment,
							LOCTEXT("MomentCueTitle", "Cue"),
							LOCTEXT(
								"MomentCueDescription",
								"Triggers once on one definitive animation frame."))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 14.0f)
					[
						MakeKindButton(
							EPaper2DPlusFrameCueTypeKind::Range,
							LOCTEXT("RangeCueTitle", "Cue State"),
							LOCTEXT(
								"RangeCueDescription",
								"Broadcasts Begin, optional Update, and End across a frame range."))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Right)
					[
						SNew(SButton)
						.Text(LOCTEXT("Cancel", "Cancel"))
						.OnClicked(this, &SFrameCueTypeKindDialog::HandleCancel)
					]
				]
			];
		}

		void SetOwningWindow(const TSharedRef<SWindow>& InWindow)
		{
			OwningWindow = InWindow;
		}

		bool WasAccepted() const { return bAccepted; }
		EPaper2DPlusFrameCueTypeKind GetSelectedKind() const { return SelectedKind; }

	private:
		TSharedRef<SWidget> MakeKindButton(
			EPaper2DPlusFrameCueTypeKind Kind,
			const FText& Title,
			const FText& Description)
		{
			return SNew(SButton)
				.HAlign(HAlign_Fill)
				.ContentPadding(FMargin(12.0f, 9.0f))
				.OnClicked(this, &SFrameCueTypeKindDialog::HandleChooseKind, Kind)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(Title)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 3.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(Description)
						.AutoWrapText(true)
					]
				];
		}

		FReply HandleChooseKind(EPaper2DPlusFrameCueTypeKind Kind)
		{
			SelectedKind = Kind;
			bAccepted = true;
			CloseWindow();
			return FReply::Handled();
		}

		FReply HandleCancel()
		{
			SelectedKind = EPaper2DPlusFrameCueTypeKind::Invalid;
			bAccepted = false;
			CloseWindow();
			return FReply::Handled();
		}

		void CloseWindow()
		{
			if (const TSharedPtr<SWindow> Window = OwningWindow.Pin())
			{
				Window->RequestDestroyWindow();
			}
		}

		TWeakPtr<SWindow> OwningWindow;
		EPaper2DPlusFrameCueTypeKind SelectedKind =
			EPaper2DPlusFrameCueTypeKind::Invalid;
		bool bAccepted = false;
	};

	bool PromptForCueTypeKind(EPaper2DPlusFrameCueTypeKind& OutKind)
	{
		OutKind = EPaper2DPlusFrameCueTypeKind::Invalid;
		if (!FSlateApplication::IsInitialized())
		{
			return false;
		}

		const TSharedRef<SFrameCueTypeKindDialog> Dialog =
			SNew(SFrameCueTypeKindDialog);
		const TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(LOCTEXT("ChooseCueKindTitle", "Create Paper2D+ Frame Cue Type"))
			.ClientSize(FVector2D(520.0f, 270.0f))
			.SizingRule(ESizingRule::FixedSize)
			.AutoCenter(EAutoCenter::PreferredWorkArea)
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			[
				Dialog
			];
		Dialog->SetOwningWindow(Window);

		FSlateApplication::Get().AddModalWindow(
			Window,
			FSlateApplication::Get().GetActiveTopLevelWindow(),
			/*bSlowTaskWindow*/ false);

		if (!Dialog->WasAccepted())
		{
			return false;
		}

		OutKind = Dialog->GetSelectedKind();
		return OutKind == EPaper2DPlusFrameCueTypeKind::Moment
			|| OutKind == EPaper2DPlusFrameCueTypeKind::Range;
	}
}

UPaper2DPlusFrameCueTypeFactory::UPaper2DPlusFrameCueTypeFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UPaper2DPlusFrameCueBlueprint::StaticClass();
}

void UPaper2DPlusFrameCueTypeFactory::ConfigureForKind(
	EPaper2DPlusFrameCueTypeKind Kind)
{
	ConfiguredKind = Kind;
	bUsePreconfiguredKind = true;
}

#if WITH_DEV_AUTOMATION_TESTS
void UPaper2DPlusFrameCueTypeFactory::SetKindDialogResponseForTests(
	bool bAccept,
	EPaper2DPlusFrameCueTypeKind Kind)
{
	bHasKindDialogResponseForTests = true;
	bAcceptKindDialogForTests = bAccept;
	KindDialogResponseForTests = Kind;
	bUsePreconfiguredKind = false;
}
#endif

bool UPaper2DPlusFrameCueTypeFactory::ConfigureProperties()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (bHasKindDialogResponseForTests)
	{
		bHasKindDialogResponseForTests = false;
		ConfiguredKind = bAcceptKindDialogForTests
			? KindDialogResponseForTests
			: EPaper2DPlusFrameCueTypeKind::Invalid;
		return bAcceptKindDialogForTests && IsSupportedKind(ConfiguredKind);
	}
#endif

	if (bUsePreconfiguredKind)
	{
		bUsePreconfiguredKind = false;
		return IsSupportedKind(ConfiguredKind);
	}

	ConfiguredKind = EPaper2DPlusFrameCueTypeKind::Invalid;
	return Paper2DPlusFrameCueTypeFactoryInternal::PromptForCueTypeKind(ConfiguredKind);
}

UObject* UPaper2DPlusFrameCueTypeFactory::FactoryCreateNew(
	UClass* Class,
	UObject* InParent,
	FName Name,
	EObjectFlags Flags,
	UObject* Context,
	FFeedbackContext* Warn,
	FName CallingContext)
{
	return CreateConfiguredCueType(Class, InParent, Name, Flags, Warn);
}

UObject* UPaper2DPlusFrameCueTypeFactory::FactoryCreateNew(
	UClass* Class,
	UObject* InParent,
	FName Name,
	EObjectFlags Flags,
	UObject* Context,
	FFeedbackContext* Warn)
{
	return CreateConfiguredCueType(Class, InParent, Name, Flags, Warn);
}

FText UPaper2DPlusFrameCueTypeFactory::GetDisplayName() const
{
	return LOCTEXT("FactoryDisplayName", "Paper2D+ Frame Cue Type");
}

uint32 UPaper2DPlusFrameCueTypeFactory::GetMenuCategories() const
{
	return FPaper2DPlusEditorModule::GetAssetCategory();
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
TArray<FAssetCategoryPath> UPaper2DPlusFrameCueTypeFactory::GetAssetMenuPathsForCategory(FName InCategory) const
{
	return { FAssetCategoryPath(
		FText::FromName(InCategory),
		NSLOCTEXT("Paper2DPlusAssetMenu", "EffectsAndCuesSection", "Effects & Cues"),
		ECategoryMenuType::Section) };
}
#endif

FString UPaper2DPlusFrameCueTypeFactory::GetDefaultNewAssetName() const
{
	return TEXT("NewFrameCueType");
}

bool UPaper2DPlusFrameCueTypeFactory::IsSupportedKind(
	EPaper2DPlusFrameCueTypeKind Kind)
{
	return Kind == EPaper2DPlusFrameCueTypeKind::Moment
		|| Kind == EPaper2DPlusFrameCueTypeKind::Range;
}

UObject* UPaper2DPlusFrameCueTypeFactory::CreateConfiguredCueType(
	UClass* Class,
	UObject* InParent,
	FName Name,
	EObjectFlags Flags,
	FFeedbackContext* Warn)
{
	LastCreateStatus = EPaper2DPlusFrameCueTypeCreateStatus::CreationFailed;
	LastCreateError = FText::GetEmpty();
	bUsePreconfiguredKind = false;

	if (Class != UPaper2DPlusFrameCueBlueprint::StaticClass())
	{
		LastCreateError = LOCTEXT(
			"UnsupportedFactoryClass",
			"The Frame Cue Type factory creates only Paper2D+ Frame Cue Type assets.");
	}
	else if (!IsSupportedKind(ConfiguredKind))
	{
		LastCreateStatus = EPaper2DPlusFrameCueTypeCreateStatus::InvalidKind;
		LastCreateError = LOCTEXT(
			"MissingCueKind",
			"Choose Cue or Cue State before creating the Frame Cue Type.");
	}
	else if (UPackage* Package = Cast<UPackage>(InParent))
	{
		FPaper2DPlusFrameCueTypeCreateRequest Request;
		Request.Package = Package;
		Request.AssetName = Name;
		Request.Kind = ConfiguredKind;
		Request.ObjectFlags = Flags;

		const FPaper2DPlusFrameCueTypeCreateResult Result =
			FPaper2DPlusFrameCueTypeAuthoring::CreateCueType(Request);
		LastCreateStatus = Result.Status;
		LastCreateError = Result.Error;
		ConfiguredKind = EPaper2DPlusFrameCueTypeKind::Invalid;
		if (!Result.IsSuccess() && Warn && !LastCreateError.IsEmpty())
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("%s"), *LastCreateError.ToString());
		}
		if (Result.IsSuccess())
		{
			// Finish creation into a placement-ready asset. The Content Browser only calls this
			// factory once the inline rename commits, so `Name` is already the designer's final name
			// and the deferred save leaves no redirector behind. The deferral is what makes the save
			// stick: IAssetTools dirties this package again the instant this function returns, and
			// the asset editor opens in the same frame.
			FPaper2DPlusFrameCueTypeBirthReady::CompleteOnNextTick(*Result.CueType);
		}
		return Result.IsSuccess() ? Result.CueType : nullptr;
	}
	else
	{
		LastCreateStatus = EPaper2DPlusFrameCueTypeCreateStatus::InvalidPackage;
		LastCreateError = LOCTEXT(
			"InvalidFactoryPackage",
			"The Frame Cue Type requires a valid asset package.");
	}

	ConfiguredKind = EPaper2DPlusFrameCueTypeKind::Invalid;
	if (Warn && !LastCreateError.IsEmpty())
	{
		Warn->Logf(ELogVerbosity::Error, TEXT("%s"), *LastCreateError.ToString());
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
