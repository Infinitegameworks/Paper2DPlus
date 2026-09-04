// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfileDetailsPanel.h"
#include "CharacterProfileEditorModel.h"
#include "EditorCanvasUtils.h"
#include "ProfilePropertyRow.h"
#include "SDirectionalAnimationWheel.h"
#include "ProfileSpriteBoundsService.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusMoveTransition.h"
#include "Paper2DPlusAnimationTagQuery.h"
#include "AnimationMapCore.h"
#include "AnimationTagChipUtils.h"
#include "PaperZDSequenceAuthoring.h"
#include "ProfileToolPanelProvider.h"
#include "DestructiveActionUtils.h"
// SGameplayTagCombo was added in UE 5.3
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
#include "SGameplayTagCombo.h"
#include "SGameplayTagPicker.h"
#endif
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SRotatorInputBox.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Framework/Application/SlateApplication.h"
#include "GameplayTagsEditorModule.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/UnrealType.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "ProfileDetailsPanelDirectional"

namespace DestructiveActions = Paper2DPlusEditor::DestructiveActionUtils;

namespace
{
	class SDirectionalImpactButton final : public SButton
	{
	public:
		FReply ExecuteAssignedClickForTests()
		{
			return ExecuteOnClick();
		}
	};

	double NormalizeDirectionalBearing(const double BearingDegrees)
	{
		double Normalized = FMath::Fmod(BearingDegrees, 360.0);
		if (Normalized < 0.0)
		{
			Normalized += 360.0;
		}
		return FMath::IsNearlyEqual(Normalized, 360.0) ? 0.0 : Normalized;
	}

	double GetDirectionalSlotCenterBearing(
		const int32 SlotIndex,
		const int32 DirectionCount,
		const float AngleOffsetDegrees)
	{
		return NormalizeDirectionalBearing(
			static_cast<double>(SlotIndex) * (360.0 / static_cast<double>(DirectionCount))
			- static_cast<double>(AngleOffsetDegrees));
	}

	int32 GetDirectionalCountRepairCounterpart(
		const int32 CurrentValue,
		const int32 PreferredFallback = 8)
	{
		if (CurrentValue >= UPaper2DPlusCharacterProfileAsset::MinimumDirectionalCount
			&& CurrentValue <= UPaper2DPlusCharacterProfileAsset::MaximumDirectionalCount)
		{
			return CurrentValue;
		}
		return PreferredFallback >= UPaper2DPlusCharacterProfileAsset::MinimumDirectionalCount
			&& PreferredFallback <= UPaper2DPlusCharacterProfileAsset::MaximumDirectionalCount
			? PreferredFallback
			: 8;
	}

	float GetDirectionalOffsetRepairCounterpart(
		const float CurrentValue,
		const float PreferredFallback = 0.0f)
	{
		auto IsValidOffset = [](const float Value)
		{
			return FMath::IsFinite(Value)
				&& Value >= UPaper2DPlusCharacterProfileAsset::MinimumDirectionalAngleOffset
				&& Value <= UPaper2DPlusCharacterProfileAsset::MaximumDirectionalAngleOffset;
		};
		if (IsValidOffset(CurrentValue))
		{
			return CurrentValue;
		}
		return IsValidOffset(PreferredFallback) ? PreferredFallback : 0.0f;
	}

	void SetInvalidDirectionalImpact(
		FProfileDirectionalTopologyImpact& Impact,
		const FString& IssueText)
	{
		Impact.bRequestValid = false;
		Impact.Items.Reset();
		Impact.IssueText = IssueText;
	}

	bool AddDirectionalTopologyImpacts(
		const UPaper2DPlusCharacterProfileAsset* Profile,
		const int32 AnimationIndex,
		const int32 ProposedDirectionCount,
		const float ProposedAngleOffsetDegrees,
		const bool bProposedOverrideProfileSettings,
		FProfileDirectionalTopologyImpact& Impact)
	{
		if (!Profile || !Profile->Flipbooks.IsValidIndex(AnimationIndex))
		{
			SetInvalidDirectionalImpact(
				Impact,
				TEXT("Directional topology preflight failed because an affected animation no longer exists."));
			return false;
		}

		// Validate the post-change structure. This keeps unrelated malformed storage fail-closed while
		// allowing these numeric controls to repair the exact invalid count/offset they own.
		UPaper2DPlusCharacterProfileAsset* ProspectiveProfile =
			NewObject<UPaper2DPlusCharacterProfileAsset>(GetTransientPackage());
		ProspectiveProfile->DefaultDirectionalCount = Profile->DefaultDirectionalCount;
		ProspectiveProfile->DefaultDirectionalAngleOffset =
			Profile->DefaultDirectionalAngleOffset;
		ProspectiveProfile->Flipbooks.Add(Profile->Flipbooks[AnimationIndex]);
		if (Impact.bProfileDefaultsChange)
		{
			ProspectiveProfile->DefaultDirectionalCount = ProposedDirectionCount;
			ProspectiveProfile->DefaultDirectionalAngleOffset = ProposedAngleOffsetDegrees;
		}
		else
		{
			FPaper2DPlusDirectionalAnimationData& ProspectiveData =
				ProspectiveProfile->Flipbooks[0].DirectionalAnimationData;
			ProspectiveData.bOverrideProfileSettings = bProposedOverrideProfileSettings;
			if (bProposedOverrideProfileSettings)
			{
				ProspectiveData.DirectionCount = ProposedDirectionCount;
				ProspectiveData.AngleOffsetDegrees = ProposedAngleOffsetDegrees;
			}
		}
		FPaper2DPlusDirectionalStructureResult StructureResult;
		if (!ProspectiveProfile->CheckDirectionalAnimationStructure(
			0, StructureResult)
			&& StructureResult.Fault
				!= EPaper2DPlusDirectionalStructureFault::OccupiedInactiveSlot)
		{
			const FString AnimationName =
				Profile->Flipbooks[AnimationIndex].Identity.FlipbookName;
			SetInvalidDirectionalImpact(
				Impact,
				FString::Printf(
					TEXT("Directional topology preflight failed for '%s': %s"),
					*AnimationName,
					*StructureResult.Message));
			return false;
		}

		const FFlipbookProfileEntry& Entry = Profile->Flipbooks[AnimationIndex];
		const FPaper2DPlusDirectionalAnimationData& CurrentData =
			Entry.DirectionalAnimationData;
		const int32 CurrentDirectionCount = CurrentData.bHasDirectionalSet
			&& CurrentData.bOverrideProfileSettings
			? CurrentData.DirectionCount
			: Profile->DefaultDirectionalCount;
		const float CurrentAngleOffsetDegrees = CurrentData.bHasDirectionalSet
			&& CurrentData.bOverrideProfileSettings
			? CurrentData.AngleOffsetDegrees
			: Profile->DefaultDirectionalAngleOffset;
		const bool bCurrentSettingsValid =
			UPaper2DPlusCharacterProfileAsset::AreDirectionalSettingsValid(
				CurrentDirectionCount, CurrentAngleOffsetDegrees);
		TArray<const FPaper2DPlusDirectionalAnimationSlot*> OccupiedSlots;
		for (const FPaper2DPlusDirectionalAnimationSlot& Slot :
			Entry.DirectionalAnimationData.Slots)
		{
			if (!Slot.Flipbook.IsNull())
			{
				OccupiedSlots.Add(&Slot);
			}
		}
		OccupiedSlots.Sort(
			[](const FPaper2DPlusDirectionalAnimationSlot& A,
				const FPaper2DPlusDirectionalAnimationSlot& B)
			{
				return A.SlotIndex < B.SlotIndex;
			});

		for (const FPaper2DPlusDirectionalAnimationSlot* Slot : OccupiedSlots)
		{
			if (!Slot)
			{
				continue;
			}
			FProfileDirectionalTopologyImpactItem Item;
			Item.AnimationIdentity =
				Paper2DPlusProfileToolProvider::MakeAnimationIdentity(Profile, AnimationIndex);
			Item.AnimationIndexAtPreflight = AnimationIndex;
			Item.AnimationName = Entry.Identity.FlipbookName;
			Item.SlotIndex = Slot->SlotIndex;
			Item.bOldBearingValid = bCurrentSettingsValid
				&& Slot->SlotIndex >= 0
				&& Slot->SlotIndex < CurrentDirectionCount;
			if (Item.bOldBearingValid)
			{
				Item.OldBearingDegrees = GetDirectionalSlotCenterBearing(
					Slot->SlotIndex, CurrentDirectionCount, CurrentAngleOffsetDegrees);
			}

			if (Slot->SlotIndex < 0 || Slot->SlotIndex >= ProposedDirectionCount)
			{
				Item.Disposition = EProfileDirectionalTopologyImpactDisposition::Stranded;
				Impact.Items.Add(MoveTemp(Item));
				continue;
			}

			Item.NewBearingDegrees = GetDirectionalSlotCenterBearing(
				Slot->SlotIndex, ProposedDirectionCount, ProposedAngleOffsetDegrees);
			if (!Item.bOldBearingValid || !FMath::IsNearlyEqual(
				Item.OldBearingDegrees, Item.NewBearingDegrees, KINDA_SMALL_NUMBER))
			{
				Item.Disposition =
					EProfileDirectionalTopologyImpactDisposition::Reinterpreted;
				Impact.Items.Add(MoveTemp(Item));
			}
		}
		return true;
	}

	FString BuildStrandedDirectionalIssue(
		const FProfileDirectionalTopologyImpact& Impact)
	{
		const int32 StrandedCount = Impact.CountStrandedAssignments();
		FString Result = FString::Printf(
			TEXT("Directional topology change blocked: %d occupied slot(s) would become inactive."),
			StrandedCount);
		for (const FProfileDirectionalTopologyImpactItem& Item : Impact.Items)
		{
			if (Item.IsStranded())
			{
				Result += LINE_TERMINATOR;
				Result += Item.ToDeterministicString();
			}
		}
		return Result;
	}

	bool AreDirectionalImpactFloatsEquivalent(const float A, const float B)
	{
		constexpr uint32 ExponentMask = 0x7f800000u;
		constexpr uint32 MantissaMask = 0x007fffffu;
		constexpr uint32 MagnitudeMask = 0x7fffffffu;
		const uint32 ABits = FPlatformMath::AsUInt(A);
		const uint32 BBits = FPlatformMath::AsUInt(B);
		const bool bAIsNaN = (ABits & ExponentMask) == ExponentMask
			&& (ABits & MantissaMask) != 0u;
		const bool bBIsNaN = (BBits & ExponentMask) == ExponentMask
			&& (BBits & MantissaMask) != 0u;
		if (bAIsNaN || bBIsNaN)
		{
			return bAIsNaN && bBIsNaN;
		}
		return ABits == BBits
			|| ((ABits & MagnitudeMask) == 0u
				&& (BBits & MagnitudeMask) == 0u);
	}

	bool AreDirectionalFiniteFloatsEquivalent(const float A, const float B)
	{
		constexpr uint32 ExponentMask = 0x7f800000u;
		constexpr uint32 MagnitudeMask = 0x7fffffffu;
		const uint32 ABits = FPlatformMath::AsUInt(A);
		const uint32 BBits = FPlatformMath::AsUInt(B);
		if ((ABits & ExponentMask) == ExponentMask
			|| (BBits & ExponentMask) == ExponentMask)
		{
			return false;
		}
		return ABits == BBits
			|| ((ABits & MagnitudeMask) == 0u
				&& (BBits & MagnitudeMask) == 0u);
	}

	bool AreDirectionalImpactDoublesEquivalent(const double A, const double B)
	{
		constexpr uint64 ExponentMask = 0x7ff0000000000000ull;
		constexpr uint64 MantissaMask = 0x000fffffffffffffull;
		constexpr uint64 MagnitudeMask = 0x7fffffffffffffffull;
		const uint64 ABits = FPlatformMath::AsUInt(A);
		const uint64 BBits = FPlatformMath::AsUInt(B);
		const bool bAIsNaN = (ABits & ExponentMask) == ExponentMask
			&& (ABits & MantissaMask) != 0ull;
		const bool bBIsNaN = (BBits & ExponentMask) == ExponentMask
			&& (BBits & MantissaMask) != 0ull;
		if (bAIsNaN || bBIsNaN)
		{
			return bAIsNaN && bBIsNaN;
		}
		return ABits == BBits
			|| ((ABits & MagnitudeMask) == 0ull
				&& (BBits & MagnitudeMask) == 0ull);
	}

	bool AreDirectionalTopologyImpactsEquivalent(
		const FProfileDirectionalTopologyImpact& A,
		const FProfileDirectionalTopologyImpact& B)
	{
		if (A.bProfileDefaultsChange != B.bProfileDefaultsChange
			|| A.CurrentDirectionCount != B.CurrentDirectionCount
			|| !AreDirectionalImpactFloatsEquivalent(
				A.CurrentAngleOffsetDegrees, B.CurrentAngleOffsetDegrees)
			|| A.ProposedDirectionCount != B.ProposedDirectionCount
			|| !AreDirectionalImpactFloatsEquivalent(
				A.ProposedAngleOffsetDegrees, B.ProposedAngleOffsetDegrees)
			|| A.bRequestValid != B.bRequestValid
			|| A.Items.Num() != B.Items.Num())
		{
			return false;
		}

		for (int32 ItemIndex = 0; ItemIndex < A.Items.Num(); ++ItemIndex)
		{
			const FProfileDirectionalTopologyImpactItem& Left = A.Items[ItemIndex];
			const FProfileDirectionalTopologyImpactItem& Right = B.Items[ItemIndex];
			if (Left.AnimationIdentity != Right.AnimationIdentity
				|| Left.AnimationIndexAtPreflight != Right.AnimationIndexAtPreflight
				|| Left.AnimationName != Right.AnimationName
				|| Left.SlotIndex != Right.SlotIndex
				|| Left.bOldBearingValid != Right.bOldBearingValid
				|| !AreDirectionalImpactDoublesEquivalent(
					Left.OldBearingDegrees, Right.OldBearingDegrees)
				|| !AreDirectionalImpactDoublesEquivalent(
					Left.NewBearingDegrees, Right.NewBearingDegrees)
				|| Left.Disposition != Right.Disposition)
			{
				return false;
			}
		}
		return true;
	}
}

FString FProfileDirectionalTopologyImpactItem::ToDeterministicString() const
{
	if (IsStranded())
	{
		if (!bOldBearingValid)
		{
			return FString::Printf(
				TEXT("%s | slot %d | invalid bearing -> inactive"),
				*AnimationName,
				SlotIndex);
		}
		return FString::Printf(
			TEXT("%s | slot %d | %.3f deg -> inactive"),
			*AnimationName,
			SlotIndex,
			OldBearingDegrees);
	}
	if (!bOldBearingValid)
	{
		return FString::Printf(
			TEXT("%s | slot %d | invalid bearing -> %.3f deg"),
			*AnimationName,
			SlotIndex,
			NewBearingDegrees);
	}
	return FString::Printf(
		TEXT("%s | slot %d | %.3f deg -> %.3f deg"),
		*AnimationName,
		SlotIndex,
		OldBearingDegrees,
		NewBearingDegrees);
}

bool FProfileDirectionalTopologyImpact::HasStrandedAssignments() const
{
	return CountStrandedAssignments() > 0;
}

bool FProfileDirectionalTopologyImpact::HasReinterpretedAssignments() const
{
	return CountReinterpretedAssignments() > 0;
}

int32 FProfileDirectionalTopologyImpact::CountStrandedAssignments() const
{
	int32 Count = 0;
	for (const FProfileDirectionalTopologyImpactItem& Item : Items)
	{
		if (Item.IsStranded())
		{
			++Count;
		}
	}
	return Count;
}

int32 FProfileDirectionalTopologyImpact::CountReinterpretedAssignments() const
{
	int32 Count = 0;
	for (const FProfileDirectionalTopologyImpactItem& Item : Items)
	{
		if (!Item.IsStranded())
		{
			++Count;
		}
	}
	return Count;
}

UPaper2DPlusCharacterProfileAsset* SProfileDetailsPanel::GetLiveDirectionalProfile() const
{
	return Model.IsValid() ? Model->GetAsset() : Asset.Get();
}

void SProfileDetailsPanel::CancelDirectionalNumericEdit()
{
	ActiveDirectionalNumericEdit = EDirectionalNumericEdit::None;
	bDirectionalNumericSliderMovement = false;
	DirectionalNumericDraftValue = 0.0;
	DirectionalNumericOwner = FProfileAnimationIdentity();
	DirectionalNumericProfileOwner.Reset();
	DirectionalNumericModelGeneration = 0;
}

bool SProfileDetailsPanel::StartDirectionalNumericEdit(
	const EDirectionalNumericEdit EditKind,
	const double InitialValue,
	const bool bSliderMovement)
{
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	if (!Profile)
	{
		CancelDirectionalNumericEdit();
		return false;
	}

	const bool bProfileEdit = EditKind == EDirectionalNumericEdit::ProfileCount
		|| EditKind == EDirectionalNumericEdit::ProfileOffset;
	const FProfileAnimationIdentity OwnerIdentity = bProfileEdit
		? FProfileAnimationIdentity()
		: GetSelectedDirectionalAnimationIdentity();
	if (!bProfileEdit
		&& Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
			Profile, OwnerIdentity) == INDEX_NONE)
	{
		CancelDirectionalNumericEdit();
		return false;
	}

	ActiveDirectionalNumericEdit = EditKind;
	bDirectionalNumericSliderMovement = bSliderMovement;
	DirectionalNumericDraftValue = InitialValue;
	DirectionalNumericOwner = OwnerIdentity;
	DirectionalNumericProfileOwner = Profile;
	DirectionalNumericModelGeneration = DirectionalModelGeneration;
	return true;
}

bool SProfileDetailsPanel::ConsumeDirectionalNumericEdit(
	const EDirectionalNumericEdit EditKind,
	FProfileAnimationIdentity& OutOwnerIdentity)
{
	OutOwnerIdentity = FProfileAnimationIdentity();
	if (ActiveDirectionalNumericEdit != EditKind
		|| EditKind == EDirectionalNumericEdit::ProfileCount
		|| EditKind == EDirectionalNumericEdit::ProfileOffset)
	{
		return false;
	}
	UPaper2DPlusCharacterProfileAsset* ProfileOwner =
		DirectionalNumericProfileOwner.Get();
	const bool bOwnerCurrent = ProfileOwner
		&& ProfileOwner == GetLiveDirectionalProfile()
		&& DirectionalNumericModelGeneration == DirectionalModelGeneration;
	OutOwnerIdentity = DirectionalNumericOwner;
	CancelDirectionalNumericEdit();
	if (!bOwnerCurrent || !OutOwnerIdentity.IsValid())
	{
		OutOwnerIdentity = FProfileAnimationIdentity();
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Directional local numeric edit expired because its Profile owner or editor model generation changed."));
		PublishDirectionalIssue(Impact);
		return false;
	}
	return true;
}

bool SProfileDetailsPanel::ConsumeProfileDirectionalNumericEdit(
	const EDirectionalNumericEdit EditKind,
	UPaper2DPlusCharacterProfileAsset*& OutProfileOwner,
	uint64& OutModelGeneration)
{
	OutProfileOwner = nullptr;
	OutModelGeneration = 0;
	if (ActiveDirectionalNumericEdit != EditKind
		|| (EditKind != EDirectionalNumericEdit::ProfileCount
			&& EditKind != EDirectionalNumericEdit::ProfileOffset))
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Directional Profile numeric edit expired because its Profile owner or editor model generation changed."));
		PublishDirectionalIssue(Impact);
		return false;
	}

	OutProfileOwner = DirectionalNumericProfileOwner.Get();
	OutModelGeneration = DirectionalNumericModelGeneration;
	const bool bOwnerCurrent = OutProfileOwner
		&& OutProfileOwner == GetLiveDirectionalProfile()
		&& OutModelGeneration == DirectionalModelGeneration;
	CancelDirectionalNumericEdit();
	if (!bOwnerCurrent)
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Directional Profile numeric edit expired because its Profile owner or editor model generation changed."));
		PublishDirectionalIssue(Impact);
	}
	return bOwnerCurrent;
}

void SProfileDetailsPanel::ValidateDirectionalNumericEditOwner()
{
	if (ActiveDirectionalNumericEdit == EDirectionalNumericEdit::None)
	{
		return;
	}
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	const bool bProfileEdit =
		ActiveDirectionalNumericEdit == EDirectionalNumericEdit::ProfileCount
		|| ActiveDirectionalNumericEdit == EDirectionalNumericEdit::ProfileOffset;
	const bool bOwnerValid = bProfileEdit
		? Profile
			&& DirectionalNumericProfileOwner.Get() == Profile
			&& DirectionalNumericModelGeneration == DirectionalModelGeneration
		: Profile
			&& Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
				Profile, DirectionalNumericOwner) != INDEX_NONE;
	if (!bOwnerValid)
	{
		CancelDirectionalNumericEdit();
	}
}

void SProfileDetailsPanel::ReconcileDirectionalTransientState()
{
	ValidateDirectionalNumericEditOwner();
	if (LastDirectionalImpact.Items.IsEmpty())
	{
		LastDirectionalIssueText.Reset();
		return;
	}

	const UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	const bool bAnyImpactOwnerStillResolves =
		LastDirectionalImpact.Items.ContainsByPredicate(
			[Profile](const FProfileDirectionalTopologyImpactItem& Item)
			{
				return Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
					Profile, Item.AnimationIdentity) != INDEX_NONE;
			});
	if (!bAnyImpactOwnerStillResolves)
	{
		LastDirectionalImpact = FProfileDirectionalTopologyImpact();
		LastDirectionalIssueText.Reset();
		RefreshDirectionalImpactRows();
	}
}

FProfileAnimationIdentity SProfileDetailsPanel::GetSelectedDirectionalAnimationIdentity() const
{
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	return Paper2DPlusProfileToolProvider::MakeAnimationIdentity(
		Profile,
		Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE);
}

void SProfileDetailsPanel::ResetDirectionalIssue()
{
	CancelDirectionalNumericEdit();
	LastDirectionalImpact = FProfileDirectionalTopologyImpact();
	LastDirectionalIssueText.Reset();
	RefreshDirectionalImpactRows();
}

void SProfileDetailsPanel::PublishDirectionalIssue(
	const FProfileDirectionalTopologyImpact& Impact)
{
	LastDirectionalImpact = Impact;
	LastDirectionalIssueText = Impact.IssueText;
	RefreshDirectionalImpactRows();
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SProfileDetailsPanel::RefreshDirectionalImpactRows()
{
	if (!DirectionalImpactRowsBox.IsValid())
	{
		return;
	}

	DirectionalImpactRowsBox->ClearChildren();
	DirectionalImpactRowButtons.Reset();
	for (int32 ImpactIndex = 0; ImpactIndex < LastDirectionalImpact.Items.Num(); ++ImpactIndex)
	{
		const FProfileDirectionalTopologyImpactItem& Item =
			LastDirectionalImpact.Items[ImpactIndex];
		TSharedPtr<SDirectionalImpactButton> ImpactButton;
		const TSharedRef<SHorizontalBox> ImpactRow = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(ImpactButton, SDirectionalImpactButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(6.0f, 4.0f))
				.ToolTipText(FText::Format(
					LOCTEXT(
						"DirectionalImpactNavigateTip",
						"Select {0} and focus slot {1} at its pre-change physical bearing."),
					FText::FromString(Item.AnimationName),
					FText::AsNumber(Item.SlotIndex)))
				.OnClicked_Lambda([this, ImpactIndex]()
				{
					NavigateDirectionalImpactItem(ImpactIndex);
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Item.ToDeterministicString()))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.AutoWrapText(true)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DirectionalImpactOpen", "Focus"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
					]
				]
			];
		if (Item.IsStranded())
		{
			// A blocked count reduction is only actionable if each stranded slot can be cleared
			// from its own row; Focus-and-hunt through the header was the whole workflow before.
			const FProfileAnimationIdentity StrandedIdentity = Item.AnimationIdentity;
			const int32 StrandedSlotIndex = Item.SlotIndex;
			ImpactRow->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("DirectionalImpactClear", "Clear slot"))
				.ToolTipText(LOCTEXT(
					"DirectionalImpactClearTip",
					"Clear this stranded assignment so the topology change can be retried."))
				.OnClicked_Lambda([this, StrandedIdentity, StrandedSlotIndex]()
				{
					// Deferred: ClearDirectionalSlot rebuilds this row container, which must not
					// happen from inside one of its own button callbacks.
					RegisterActiveTimer(
						0.0f,
						FWidgetActiveTimerDelegate::CreateSP(
							this,
							&SProfileDetailsPanel::HandleDeferredStrandedSlotClear,
							StrandedIdentity,
							StrandedSlotIndex));
					return FReply::Handled();
				})
			];
		}
		DirectionalImpactRowsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			ImpactRow
		];
		DirectionalImpactRowButtons.Add(ImpactButton);
	}
}

EActiveTimerReturnType SProfileDetailsPanel::HandleDeferredStrandedSlotClear(
	double InCurrentTime,
	float InDeltaTime,
	FProfileAnimationIdentity AnimationIdentity,
	int32 SlotIndex)
{
	(void)InCurrentTime;
	(void)InDeltaTime;
	// A successful clear resets the whole directional issue, but a multi-slot stranded list must
	// SURVIVE clearing one member — otherwise a 16->8 reduction with four stranded slots forces a
	// re-attempted count change per slot. Snapshot the survivors and re-publish them.
	FProfileDirectionalTopologyImpact Remaining = LastDirectionalImpact;
	Remaining.Items.RemoveAll(
		[&AnimationIdentity, SlotIndex](const FProfileDirectionalTopologyImpactItem& Item)
		{
			return Item.SlotIndex == SlotIndex
				&& Item.AnimationIdentity == AnimationIdentity;
		});
	if (ClearDirectionalSlot(AnimationIdentity, SlotIndex)
		&& !Remaining.Items.IsEmpty())
	{
		if (Remaining.HasStrandedAssignments())
		{
			Remaining.IssueText = BuildStrandedDirectionalIssue(Remaining);
		}
		PublishDirectionalIssue(Remaining);
	}
	return EActiveTimerReturnType::Stop;
}

bool SProfileDetailsPanel::NavigateDirectionalImpactItem(const int32 ImpactIndex)
{
	if (!LastDirectionalImpact.Items.IsValidIndex(ImpactIndex) || !Model.IsValid())
	{
		return false;
	}

	// Copy before SetSelectedFlipbook broadcasts; the stable identity, never the observed index, owns
	// this delayed button action even if the Profile was reordered after preflight.
	const FProfileDirectionalTopologyImpactItem Item =
		LastDirectionalImpact.Items[ImpactIndex];
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	const int32 LiveAnimationIndex =
		Paper2DPlusProfileToolProvider::ResolveAnimationIndex(Profile, Item.AnimationIdentity);
	if (!Profile || !Profile->Flipbooks.IsValidIndex(LiveAnimationIndex))
	{
		// Do not rebuild/clear the row container from inside one of its own button callbacks.
		LastDirectionalImpact.bRequestValid = false;
		LastDirectionalImpact.IssueText =
			TEXT("Directional impact navigation expired because its animation or Profile owner is no longer available.");
		LastDirectionalIssueText = LastDirectionalImpact.IssueText;
		Invalidate(EInvalidateWidgetReason::Layout);
		return false;
	}

	Model->SetSelectedFlipbook(LiveAnimationIndex);
	if (!Model->CommitDirectionalPreviewSlot(Item.SlotIndex))
	{
		// A topology may have changed since the report, or this may be a shared base-only Layer host.
		// Preserve the report's physical meaning without opting that host into directional rendering.
		Model->SetCommittedDirectionalBearing(
			Item.bOldBearingValid ? Item.OldBearingDegrees : Item.NewBearingDegrees);
	}
	return Model->GetSelectedFlipbookIndex() == LiveAnimationIndex;
}

bool SProfileDetailsPanel::ActivateDirectionalImpactRowForTests(const int32 ImpactIndex)
{
	if (!DirectionalImpactRowButtons.IsValidIndex(ImpactIndex))
	{
		return false;
	}
	const TSharedPtr<SButton> Button = DirectionalImpactRowButtons[ImpactIndex].Pin();
	return Button.IsValid()
		&& StaticCastSharedPtr<SDirectionalImpactButton>(Button)
			->ExecuteAssignedClickForTests().IsEventHandled();
}

FProfileDirectionalTopologyImpact
SProfileDetailsPanel::BuildDirectionalDefaultsImpactForTests(
	const UPaper2DPlusCharacterProfileAsset* Profile,
	const int32 DirectionCount,
	const float AngleOffsetDegrees)
{
	FProfileDirectionalTopologyImpact Impact;
	Impact.bProfileDefaultsChange = true;
	Impact.ProposedDirectionCount = DirectionCount;
	Impact.ProposedAngleOffsetDegrees = AngleOffsetDegrees;
	if (!Profile)
	{
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Directional topology preflight failed because the Profile is unavailable."));
		return Impact;
	}
	Impact.CurrentDirectionCount = Profile->DefaultDirectionalCount;
	Impact.CurrentAngleOffsetDegrees = Profile->DefaultDirectionalAngleOffset;
	if (!UPaper2DPlusCharacterProfileAsset::AreDirectionalSettingsValid(
		DirectionCount, AngleOffsetDegrees))
	{
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Directional settings are invalid: count must be 3-16 and offset must be -45.0 to 45.0 degrees."));
		return Impact;
	}

	Impact.bRequestValid = true;
	for (int32 AnimationIndex = 0; AnimationIndex < Profile->Flipbooks.Num(); ++AnimationIndex)
	{
		const FPaper2DPlusDirectionalAnimationData& DirectionalData =
			Profile->Flipbooks[AnimationIndex].DirectionalAnimationData;
		if (!DirectionalData.bHasDirectionalSet || DirectionalData.bOverrideProfileSettings)
		{
			continue;
		}
		if (!AddDirectionalTopologyImpacts(
			Profile,
			AnimationIndex,
			DirectionCount,
			AngleOffsetDegrees,
			/*bProposedOverrideProfileSettings=*/false,
			Impact))
		{
			return Impact;
		}
	}
	TArray<int32> RuntimeStrandedAnimations;
	const bool bRuntimeCanApply = Profile->CanSetDirectionalDefaults(
		DirectionCount, RuntimeStrandedAnimations);
	if (bRuntimeCanApply == Impact.HasStrandedAssignments())
	{
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Directional topology preflight disagreed with the Profile mutator; the change was refused."));
		return Impact;
	}
	if (Impact.HasStrandedAssignments())
	{
		Impact.IssueText = BuildStrandedDirectionalIssue(Impact);
	}
	return Impact;
}

FProfileDirectionalTopologyImpact
SProfileDetailsPanel::BuildDirectionalOverrideImpactForTests(
	const UPaper2DPlusCharacterProfileAsset* Profile,
	const FProfileAnimationIdentity& AnimationIdentity,
	const bool bOverrideProfileSettings,
	const int32 DirectionCount,
	const float AngleOffsetDegrees)
{
	FProfileDirectionalTopologyImpact Impact;
	Impact.ProposedDirectionCount = DirectionCount;
	Impact.ProposedAngleOffsetDegrees = AngleOffsetDegrees;
	const int32 AnimationIndex =
		Paper2DPlusProfileToolProvider::ResolveAnimationIndex(Profile, AnimationIdentity);
	if (!Profile || !Profile->Flipbooks.IsValidIndex(AnimationIndex))
	{
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Directional change expired because its animation or Profile owner is no longer available."));
		return Impact;
	}
	Profile->GetEffectiveDirectionalSettings(
		AnimationIndex,
		Impact.CurrentDirectionCount,
		Impact.CurrentAngleOffsetDegrees);
	if (!Profile->HasDirectionalSet(AnimationIndex))
	{
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Directional override preflight requires an explicitly enabled Directional Set."));
		return Impact;
	}

	const int32 ProposedDirectionCount = bOverrideProfileSettings
		? DirectionCount
		: Profile->DefaultDirectionalCount;
	const float ProposedAngleOffsetDegrees = bOverrideProfileSettings
		? AngleOffsetDegrees
		: Profile->DefaultDirectionalAngleOffset;
	Impact.ProposedDirectionCount = ProposedDirectionCount;
	Impact.ProposedAngleOffsetDegrees = ProposedAngleOffsetDegrees;
	if (!UPaper2DPlusCharacterProfileAsset::AreDirectionalSettingsValid(
		ProposedDirectionCount, ProposedAngleOffsetDegrees))
	{
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Directional settings are invalid: count must be 3-16 and offset must be -45.0 to 45.0 degrees."));
		return Impact;
	}

	Impact.bRequestValid = true;
	if (!AddDirectionalTopologyImpacts(
		Profile,
		AnimationIndex,
		ProposedDirectionCount,
		ProposedAngleOffsetDegrees,
		bOverrideProfileSettings,
		Impact))
	{
		return Impact;
	}
	TArray<int32> RuntimeStrandedSlots;
	const bool bRuntimeCanApply = Profile->CanSetDirectionalOverride(
		AnimationIndex,
		bOverrideProfileSettings,
		DirectionCount,
		RuntimeStrandedSlots);
	if (bRuntimeCanApply == Impact.HasStrandedAssignments())
	{
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Directional topology preflight disagreed with the animation mutator; the change was refused."));
		return Impact;
	}
	if (Impact.HasStrandedAssignments())
	{
		Impact.IssueText = BuildStrandedDirectionalIssue(Impact);
	}
	return Impact;
}

bool SProfileDetailsPanel::ConfirmDirectionalReinterpretation(
	const FProfileDirectionalTopologyImpact& Impact,
	const FText& Title,
	const FText& Body) const
{
	if (!Impact.HasReinterpretedAssignments())
	{
		return true;
	}

	DestructiveActions::FDestructiveActionPrompt Prompt;
	Prompt.Title = Title;
	Prompt.Body = Body;
	Prompt.Consequence = LOCTEXT(
		"DirectionalReinterpretConsequence",
		"Each listed slot stays occupied, but its physical center bearing changes. Directional art changes only; gameplay data remains owned by the base animation.");
	Prompt.MaxAffectedItems = 0;
	for (const FProfileDirectionalTopologyImpactItem& Item : Impact.Items)
	{
		if (!Item.IsStranded())
		{
			Prompt.AffectedItems.Add(Item.ToDeterministicString());
		}
	}
	return DestructiveActions::Confirm(Prompt);
}

bool SProfileDetailsPanel::ExecuteDirectionalMutation(
	UPaper2DPlusCharacterProfileAsset* Profile,
	const FText& TransactionDescription,
	TFunctionRef<bool()> Mutate)
{
	if (!Profile)
	{
		return false;
	}

	// All callers finish their stable-owner and structural/topology preflight before reaching this
	// one mutation funnel. BeginTransaction calls Modify() before the runtime mutator is invoked.
	Asset = Profile;
	TGuardValue<bool> PanelWriteGuard(bPanelWriteInProgress, true);
	BeginTransaction(TransactionDescription);
	if (!Mutate())
	{
		if (ActiveTransaction.IsValid())
		{
			ActiveTransaction->Cancel();
			ActiveTransaction.Reset();
		}
		return false;
	}

	EndTransaction();
	ResetDirectionalIssue();
	if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
	Invalidate(EInvalidateWidgetReason::Layout);
	return true;
}

bool SProfileDetailsPanel::EnableDirectionalSet(
	const FProfileAnimationIdentity& AnimationIdentity)
{
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	const int32 AnimationIndex =
		Paper2DPlusProfileToolProvider::ResolveAnimationIndex(Profile, AnimationIdentity);
	if (!Profile || !Profile->Flipbooks.IsValidIndex(AnimationIndex))
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Enable Directional Set expired because its animation or Profile owner is no longer available."));
		PublishDirectionalIssue(Impact);
		return false;
	}
	if (Profile->HasDirectionalSet(AnimationIndex))
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Enable Directional Set requires an animation that is not already configured."));
		PublishDirectionalIssue(Impact);
		return false;
	}
	if (Profile->Flipbooks[AnimationIndex].Identity.Flipbook.IsNull())
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Enable Directional Set requires a canonical base flipbook on the animation."));
		PublishDirectionalIssue(Impact);
		return false;
	}
	if (!UPaper2DPlusCharacterProfileAsset::AreDirectionalSettingsValid(
		Profile->DefaultDirectionalCount,
		Profile->DefaultDirectionalAngleOffset))
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Enable Directional Set requires valid Profile directional defaults."));
		PublishDirectionalIssue(Impact);
		return false;
	}

	return ExecuteDirectionalMutation(
		Profile,
		LOCTEXT("EnableDirectionalSetTransaction", "Enable Directional Animation Set"),
		[Profile, AnimationIndex]()
		{
			return Profile->EnableDirectionalSet(AnimationIndex);
		});
}

bool SProfileDetailsPanel::RemoveDirectionalSet(
	const FProfileAnimationIdentity& AnimationIdentity)
{
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	const int32 AnimationIndex =
		Paper2DPlusProfileToolProvider::ResolveAnimationIndex(Profile, AnimationIdentity);
	if (!Profile || !Profile->Flipbooks.IsValidIndex(AnimationIndex)
		|| !Profile->HasDirectionalSet(AnimationIndex))
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Remove Directional Set expired or the selected animation has no configured set."));
		PublishDirectionalIssue(Impact);
		return false;
	}

	const bool bHasOccupiedSlot = Profile->Flipbooks[AnimationIndex]
		.DirectionalAnimationData.Slots.ContainsByPredicate(
			[](const FPaper2DPlusDirectionalAnimationSlot& Slot)
			{
				return !Slot.Flipbook.IsNull();
			});
	if (bHasOccupiedSlot)
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Remove Directional Set is blocked until every occupied slot is cleared."));
		PublishDirectionalIssue(Impact);
		return false;
	}
	return ExecuteDirectionalMutation(
		Profile,
		LOCTEXT("RemoveDirectionalSetTransaction", "Remove Directional Animation Set"),
		[Profile, AnimationIndex]()
		{
			return Profile->RemoveDirectionalSet(AnimationIndex);
		});
}

bool SProfileDetailsPanel::CommitDirectionalSlot(
	const FProfileAnimationIdentity& AnimationIdentity,
	const int32 SlotIndex,
	const TSoftObjectPtr<UPaperFlipbook>& Flipbook)
{
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	const int32 AnimationIndex =
		Paper2DPlusProfileToolProvider::ResolveAnimationIndex(Profile, AnimationIdentity);
	int32 EffectiveDirectionCount = 0;
	float EffectiveAngleOffsetDegrees = 0.0f;
	if (!Profile || !Profile->Flipbooks.IsValidIndex(AnimationIndex)
		|| Flipbook.IsNull()
		|| !Profile->GetEffectiveDirectionalSettings(
			AnimationIndex, EffectiveDirectionCount, EffectiveAngleOffsetDegrees)
		|| SlotIndex < 0 || SlotIndex >= EffectiveDirectionCount)
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Directional slot assignment expired or targets an invalid active slot."));
		PublishDirectionalIssue(Impact);
		return false;
	}
	const bool bFirstAssignment = !Profile->HasDirectionalSet(AnimationIndex);
	if (bFirstAssignment && Profile->Flipbooks[AnimationIndex].Identity.Flipbook.IsNull())
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("The first directional slot assignment requires a canonical base flipbook on the animation."));
		PublishDirectionalIssue(Impact);
		return false;
	}

	int32 MatchingSlotCount = 0;
	bool bMatchingPath = false;
	for (const FPaper2DPlusDirectionalAnimationSlot& ExistingSlot :
		Profile->Flipbooks[AnimationIndex].DirectionalAnimationData.Slots)
	{
		if (ExistingSlot.SlotIndex == SlotIndex)
		{
			++MatchingSlotCount;
			bMatchingPath |= ExistingSlot.Flipbook.ToSoftObjectPath()
				== Flipbook.ToSoftObjectPath();
		}
	}
	if (!bFirstAssignment && MatchingSlotCount == 1 && bMatchingPath)
	{
		ResetDirectionalIssue();
		Invalidate(EInvalidateWidgetReason::Layout);
		return false;
	}

	return ExecuteDirectionalMutation(
		Profile,
		LOCTEXT("AssignDirectionalSlotTransaction", "Assign Directional Animation Slot"),
		[Profile, AnimationIndex, SlotIndex, Flipbook]()
		{
			return Profile->SetDirectionalSlot(AnimationIndex, SlotIndex, Flipbook);
		});
}

bool SProfileDetailsPanel::ClearDirectionalSlot(
	const FProfileAnimationIdentity& AnimationIdentity,
	const int32 SlotIndex)
{
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	const int32 AnimationIndex =
		Paper2DPlusProfileToolProvider::ResolveAnimationIndex(Profile, AnimationIdentity);
	const bool bHasSlotRecord = Profile && Profile->Flipbooks.IsValidIndex(AnimationIndex)
		&& Profile->Flipbooks[AnimationIndex].DirectionalAnimationData.Slots.ContainsByPredicate(
			[SlotIndex](const FPaper2DPlusDirectionalAnimationSlot& ExistingSlot)
			{
				return ExistingSlot.SlotIndex == SlotIndex;
			});
	if (!Profile || !Profile->Flipbooks.IsValidIndex(AnimationIndex)
		|| SlotIndex < 0
		|| SlotIndex >= UPaper2DPlusCharacterProfileAsset::MaximumDirectionalCount
		|| !bHasSlotRecord)
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Clear Directional Slot expired or the selected slot has no authored record."));
		PublishDirectionalIssue(Impact);
		return false;
	}
	return ExecuteDirectionalMutation(
		Profile,
		LOCTEXT("ClearDirectionalSlotTransaction", "Clear Directional Animation Slot"),
		[Profile, AnimationIndex, SlotIndex]()
		{
			return Profile->ClearDirectionalSlot(AnimationIndex, SlotIndex);
		});
}

bool SProfileDetailsPanel::CommitDirectionalSlotMirror(
	const FProfileAnimationIdentity& AnimationIdentity,
	const int32 SlotIndex,
	const bool bMirrorHorizontally)
{
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	const int32 AnimationIndex =
		Paper2DPlusProfileToolProvider::ResolveAnimationIndex(Profile, AnimationIdentity);
	TSoftObjectPtr<UPaperFlipbook> Assigned;
	bool bExistingMirror = false;
	if (!Profile || !Profile->Flipbooks.IsValidIndex(AnimationIndex)
		|| !Profile->GetDirectionalSlot(
			AnimationIndex, SlotIndex, Assigned, bExistingMirror))
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Mirror Directional Slot expired or the selected slot holds no art to mirror."));
		PublishDirectionalIssue(Impact);
		return false;
	}
	if (bExistingMirror == bMirrorHorizontally)
	{
		ResetDirectionalIssue();
		Invalidate(EInvalidateWidgetReason::Layout);
		return false;
	}

	return ExecuteDirectionalMutation(
		Profile,
		LOCTEXT("MirrorDirectionalSlotTransaction", "Mirror Directional Animation Slot"),
		[Profile, AnimationIndex, SlotIndex, bMirrorHorizontally]()
		{
			return Profile->SetDirectionalSlotMirror(
				AnimationIndex, SlotIndex, bMirrorHorizontally);
		});
}

TMap<int32, int32> SProfileDetailsPanel::MatchDirectionalSlotsByNameSuffix(
	const FString& BaseAssetName,
	const TArray<FString>& CandidateAssetNames,
	int32 DirectionCount,
	float AngleOffsetDegrees)
{
	TMap<int32, int32> Matches;
	if (BaseAssetName.IsEmpty()
		|| !UPaper2DPlusCharacterProfileAsset::AreDirectionalSettingsValid(
			DirectionCount, AngleOffsetDegrees))
	{
		return Matches;
	}

	static const TCHAR* Separators[] = { TEXT("_"), TEXT("-"), TEXT(" ") };
	for (int32 SlotIndex = 0; SlotIndex < DirectionCount; ++SlotIndex)
	{
		TArray<FString, TInlineAllocator<2>> AcceptedSuffixes;
		AcceptedSuffixes.Add(SDirectionalAnimationWheel::GetSlotDirectionLabel(
			SlotIndex, DirectionCount, AngleOffsetDegrees));
		const FString IndexSuffix = FString::FromInt(SlotIndex);
		if (!AcceptedSuffixes.Contains(IndexSuffix))
		{
			AcceptedSuffixes.Add(IndexSuffix);
		}

		int32 MatchedCandidate = INDEX_NONE;
		bool bAmbiguous = false;
		for (int32 CandidateIndex = 0;
			CandidateIndex < CandidateAssetNames.Num() && !bAmbiguous;
			++CandidateIndex)
		{
			for (const TCHAR* Separator : Separators)
			{
				for (const FString& Suffix : AcceptedSuffixes)
				{
					if (!CandidateAssetNames[CandidateIndex].Equals(
						BaseAssetName + Separator + Suffix, ESearchCase::IgnoreCase))
					{
						continue;
					}
					if (MatchedCandidate != INDEX_NONE
						&& MatchedCandidate != CandidateIndex)
					{
						// Two distinct assets both claim this direction. Skip the slot
						// rather than guess between them.
						bAmbiguous = true;
						break;
					}
					MatchedCandidate = CandidateIndex;
				}
				if (bAmbiguous)
				{
					break;
				}
			}
		}
		if (!bAmbiguous && MatchedCandidate != INDEX_NONE)
		{
			Matches.Add(SlotIndex, MatchedCandidate);
		}
	}
	return Matches;
}

bool SProfileDetailsPanel::RunDirectionalNameSuffixAutoFill()
{
	return RunDirectionalNameSuffixAutoFillInternal(
		[](const FString& BaseAssetName,
			const int32 DirectionCount,
			const float AngleOffsetDegrees,
			const TArray<TPair<int32, FSoftObjectPath>>& Proposals)
		{
			DestructiveActions::FDestructiveActionPrompt Prompt;
			Prompt.Title = LOCTEXT("AutoFillDirectionalTitle", "Auto-fill Directional Slots");
			Prompt.Body = FText::Format(
				LOCTEXT(
					"AutoFillDirectionalBody",
					"Assign {0} empty direction slot(s) from name-suffix matches beside '{1}'?"),
				FText::AsNumber(Proposals.Num()),
				FText::FromString(BaseAssetName));
			Prompt.Consequence = LOCTEXT(
				"AutoFillDirectionalConsequence",
				"Only empty slots are filled; existing assignments are never overwritten. One undo reverts the whole fill.");
			Prompt.MaxAffectedItems = 0;
			for (const TPair<int32, FSoftObjectPath>& Proposal : Proposals)
			{
				Prompt.AffectedItems.Add(FString::Printf(
					TEXT("%s (Slot %d) <- %s"),
					*SDirectionalAnimationWheel::GetSlotDirectionLabel(
						Proposal.Key, DirectionCount, AngleOffsetDegrees),
					Proposal.Key,
					*Proposal.Value.GetAssetName()));
			}
			return DestructiveActions::Confirm(Prompt);
		});
}

bool SProfileDetailsPanel::RunDirectionalNameSuffixAutoFillForTests(
	const bool bConfirmProposals)
{
	return RunDirectionalNameSuffixAutoFillInternal(
		[bConfirmProposals](const FString&, int32, float,
			const TArray<TPair<int32, FSoftObjectPath>>&)
		{
			return bConfirmProposals;
		});
}

bool SProfileDetailsPanel::RunDirectionalNameSuffixAutoFillInternal(
	TFunctionRef<bool(
		const FString& BaseAssetName,
		int32 DirectionCount,
		float AngleOffsetDegrees,
		const TArray<TPair<int32, FSoftObjectPath>>& Proposals)> ConfirmProposals)
{
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	const FProfileAnimationIdentity OwnerIdentity =
		GetSelectedDirectionalAnimationIdentity();
	const int32 OwnerIndex = Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
		Profile, OwnerIdentity);
	int32 DirectionCount = 0;
	float AngleOffsetDegrees = 0.0f;
	if (!Profile || !Profile->Flipbooks.IsValidIndex(OwnerIndex)
		|| !Profile->HasDirectionalSet(OwnerIndex)
		|| !Profile->GetEffectiveDirectionalSettings(
			OwnerIndex, DirectionCount, AngleOffsetDegrees))
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Auto-fill from names requires a selected animation with an enabled Directional Set."));
		PublishDirectionalIssue(Impact);
		return false;
	}
	const FSoftObjectPath BasePath =
		Profile->Flipbooks[OwnerIndex].Identity.Flipbook.ToSoftObjectPath();
	if (!BasePath.IsValid())
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Auto-fill from names requires a canonical base flipbook on the animation."));
		PublishDirectionalIssue(Impact);
		return false;
	}

	// Directional art universally ships beside its base as <Base>_<N/NE/...> (or <Base>_<index>).
	// One same-folder registry scan turns that convention into slot assignments in one gesture.
	const FString BaseAssetName = BasePath.GetAssetName();
	const FString BaseFolder = FPackageName::GetLongPackagePath(
		BasePath.GetLongPackageName());
	const IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
			TEXT("AssetRegistry")).Get();
	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*BaseFolder));
	Filter.bRecursivePaths = false;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	Filter.ClassNames.Add(UPaperFlipbook::StaticClass()->GetFName());
#else
	Filter.ClassPaths.Add(UPaperFlipbook::StaticClass()->GetClassPathName());
#endif
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> CandidateAssets;
	AssetRegistry.GetAssets(Filter, CandidateAssets);

	TArray<FString> CandidateNames;
	TArray<FSoftObjectPath> CandidatePaths;
	CandidateNames.Reserve(CandidateAssets.Num());
	CandidatePaths.Reserve(CandidateAssets.Num());
	for (const FAssetData& Candidate : CandidateAssets)
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		const FSoftObjectPath CandidatePath = Candidate.ToSoftObjectPath();
#else
		const FSoftObjectPath CandidatePath = Candidate.GetSoftObjectPath();
#endif
		if (CandidatePath == BasePath)
		{
			continue;
		}
		CandidateNames.Add(Candidate.AssetName.ToString());
		CandidatePaths.Add(CandidatePath);
	}

	const TMap<int32, int32> Matches = MatchDirectionalSlotsByNameSuffix(
		BaseAssetName, CandidateNames, DirectionCount, AngleOffsetDegrees);

	// Fill only EMPTY slots: auto-fill must never silently overwrite an authored assignment.
	TArray<TPair<int32, FSoftObjectPath>> Proposals;
	int32 SkippedOccupied = 0;
	for (const TPair<int32, int32>& Match : Matches)
	{
		TSoftObjectPtr<UPaperFlipbook> Existing;
		if (Profile->GetDirectionalSlot(OwnerIndex, Match.Key, Existing))
		{
			++SkippedOccupied;
			continue;
		}
		Proposals.Emplace(Match.Key, CandidatePaths[Match.Value]);
	}
	Proposals.Sort([](const TPair<int32, FSoftObjectPath>& A,
		const TPair<int32, FSoftObjectPath>& B)
	{
		return A.Key < B.Key;
	});
	if (Proposals.IsEmpty())
	{
		// Name the form that actually applies: compass suffixes exist only for zero-offset
		// 4/8/16-way topologies; everything else matches the bare slot index.
		const bool bCompassFriendly = FMath::IsNearlyZero(AngleOffsetDegrees)
			&& (DirectionCount == 4 || DirectionCount == 8 || DirectionCount == 16);
		FString NoMatchText;
		if (SkippedOccupied > 0)
		{
			NoMatchText = FString::Printf(
				TEXT("Auto-fill found %d name match(es), but every matched slot is already assigned."),
				SkippedOccupied);
		}
		else if (bCompassFriendly)
		{
			NoMatchText = FString::Printf(
				TEXT("Auto-fill found no flipbooks named '%s_<N/NE/E/...>' or '%s_<slot index>' beside the base animation."),
				*BaseAssetName,
				*BaseAssetName);
		}
		else
		{
			NoMatchText = FString::Printf(
				TEXT("Auto-fill found no flipbooks named '%s_0' .. '%s_%d' beside the base animation (this topology is not zero-offset 4/8/16-way, so compass names are not matched)."),
				*BaseAssetName,
				*BaseAssetName,
				DirectionCount - 1);
		}
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(Impact, NoMatchText);
		PublishDirectionalIssue(Impact);
		return false;
	}

	if (!ConfirmProposals(BaseAssetName, DirectionCount, AngleOffsetDegrees, Proposals))
	{
		return false;
	}

	// The modal boundary is an async ownership boundary; the mutation lambda re-resolves the
	// owner by stable identity and re-proves every target slot is still empty, so a change made
	// while the dialog was open cancels the whole transaction instead of overwriting.
	const bool bApplied = ExecuteDirectionalMutation(
		Profile,
		LOCTEXT("AutoFillDirectionalTransaction", "Auto-fill Directional Animation Slots"),
		[Profile, OwnerIdentity, Proposals]()
		{
			const int32 LiveOwnerIndex =
				Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
					Profile, OwnerIdentity);
			if (!Profile->Flipbooks.IsValidIndex(LiveOwnerIndex))
			{
				return false;
			}
			for (const TPair<int32, FSoftObjectPath>& Proposal : Proposals)
			{
				TSoftObjectPtr<UPaperFlipbook> Existing;
				if (Profile->GetDirectionalSlot(LiveOwnerIndex, Proposal.Key, Existing)
					|| !Profile->SetDirectionalSlot(
						LiveOwnerIndex,
						Proposal.Key,
						TSoftObjectPtr<UPaperFlipbook>(Proposal.Value)))
				{
					return false;
				}
			}
			return true;
		});
	if (!bApplied)
	{
		// The one branch that means "something changed while the dialog was open" must say so —
		// a confirmed Yes followed by silence is indistinguishable from a hang.
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Auto-fill was cancelled because the animation, topology, or slot assignments changed while confirmation was open; retry."));
		PublishDirectionalIssue(Impact);
	}
	return bApplied;
}

bool SProfileDetailsPanel::CommitDirectionalDefaultsInternal(
	UPaper2DPlusCharacterProfileAsset* ProfileOwner,
	const uint64 ModelGeneration,
	const int32 DirectionCount,
	const float AngleOffsetDegrees,
	TFunctionRef<bool(const FProfileDirectionalTopologyImpact&)> ConfirmReinterpretation)
{
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	if (!Profile || Profile != ProfileOwner
		|| ModelGeneration != DirectionalModelGeneration)
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Directional change expired because its Profile owner or editor model generation is no longer available."));
		PublishDirectionalIssue(Impact);
		return false;
	}
	// UE 5.0-5.7 Editor targets compile with fast floating-point semantics. Keep non-finite
	// repair requests out of the no-op path without relying on FP equality under those flags.
	if (Profile->DefaultDirectionalCount == DirectionCount
		&& AreDirectionalFiniteFloatsEquivalent(
			Profile->DefaultDirectionalAngleOffset, AngleOffsetDegrees))
	{
		ResetDirectionalIssue();
		Invalidate(EInvalidateWidgetReason::Layout);
		return false;
	}

	FProfileDirectionalTopologyImpact Impact =
		BuildDirectionalDefaultsImpactForTests(Profile, DirectionCount, AngleOffsetDegrees);
	if (!Impact.bRequestValid || Impact.HasStrandedAssignments())
	{
		PublishDirectionalIssue(Impact);
		return false;
	}
	if (Impact.HasReinterpretedAssignments())
	{
		LastDirectionalImpact = Impact;
		LastDirectionalIssueText = FString::Printf(
			TEXT("Review %d affected directional assignment(s) before confirming."),
			Impact.Items.Num());
		RefreshDirectionalImpactRows();
		Invalidate(EInvalidateWidgetReason::Layout);
		if (!ConfirmReinterpretation(Impact))
		{
			LastDirectionalIssueText =
				TEXT("Directional topology change was cancelled; the Profile was not modified.");
			Invalidate(EInvalidateWidgetReason::Layout);
			return false;
		}

		// The modal boundary is an async ownership boundary: undo, reimport, or model reuse can occur
		// before the designer answers. Reacquire the exact Profile + model generation and require the
		// complete deterministic impact to still match. Profile defaults never depend on selection.
		Profile = GetLiveDirectionalProfile();
		FProfileDirectionalTopologyImpact ConfirmedImpact =
			BuildDirectionalDefaultsImpactForTests(
				Profile, DirectionCount, AngleOffsetDegrees);
		if (!Profile || Profile != ProfileOwner
			|| ModelGeneration != DirectionalModelGeneration
			|| !ConfirmedImpact.bRequestValid
			|| !AreDirectionalTopologyImpactsEquivalent(Impact, ConfirmedImpact))
		{
			ConfirmedImpact.bRequestValid = false;
			ConfirmedImpact.IssueText =
				TEXT("Directional topology changed while confirmation was open; review the refreshed impact and retry.");
			PublishDirectionalIssue(ConfirmedImpact);
			return false;
		}
		Impact = MoveTemp(ConfirmedImpact);
	}

	return ExecuteDirectionalMutation(
		Profile,
		LOCTEXT("SetDirectionalDefaultsTransaction", "Set Directional Animation Defaults"),
		[Profile, DirectionCount, AngleOffsetDegrees]()
		{
			return Profile->SetDirectionalDefaults(DirectionCount, AngleOffsetDegrees);
		});
}

bool SProfileDetailsPanel::CommitDirectionalOverrideInternal(
	const FProfileAnimationIdentity& AnimationIdentity,
	const bool bOverrideProfileSettings,
	const int32 DirectionCount,
	const float AngleOffsetDegrees,
	TFunctionRef<bool(const FProfileDirectionalTopologyImpact&)> ConfirmReinterpretation)
{
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	int32 AnimationIndex =
		Paper2DPlusProfileToolProvider::ResolveAnimationIndex(Profile, AnimationIdentity);
	if (!Profile || !Profile->Flipbooks.IsValidIndex(AnimationIndex))
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Directional change expired because its animation or Profile owner is no longer available."));
		PublishDirectionalIssue(Impact);
		return false;
	}

	const FPaper2DPlusDirectionalAnimationData& Current =
		Profile->Flipbooks[AnimationIndex].DirectionalAnimationData;
	const bool bSame = Current.bOverrideProfileSettings == bOverrideProfileSettings
		&& (!bOverrideProfileSettings
			|| (Current.DirectionCount == DirectionCount
				&& AreDirectionalFiniteFloatsEquivalent(
					Current.AngleOffsetDegrees, AngleOffsetDegrees)));
	if (bSame)
	{
		ResetDirectionalIssue();
		Invalidate(EInvalidateWidgetReason::Layout);
		return false;
	}

	FProfileDirectionalTopologyImpact Impact = BuildDirectionalOverrideImpactForTests(
		Profile,
		AnimationIdentity,
		bOverrideProfileSettings,
		DirectionCount,
		AngleOffsetDegrees);
	if (!Impact.bRequestValid || Impact.HasStrandedAssignments())
	{
		PublishDirectionalIssue(Impact);
		return false;
	}
	if (Impact.HasReinterpretedAssignments())
	{
		LastDirectionalImpact = Impact;
		LastDirectionalIssueText = FString::Printf(
			TEXT("Review %d affected directional assignment(s) before confirming."),
			Impact.Items.Num());
		RefreshDirectionalImpactRows();
		Invalidate(EInvalidateWidgetReason::Layout);
		if (!ConfirmReinterpretation(Impact))
		{
			LastDirectionalIssueText =
				TEXT("Directional topology change was cancelled; the animation was not modified.");
			Invalidate(EInvalidateWidgetReason::Layout);
			return false;
		}

		Profile = GetLiveDirectionalProfile();
		const int32 ConfirmedAnimationIndex =
			Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
				Profile, AnimationIdentity);
		FProfileDirectionalTopologyImpact ConfirmedImpact =
			BuildDirectionalOverrideImpactForTests(
				Profile,
				AnimationIdentity,
				bOverrideProfileSettings,
				DirectionCount,
				AngleOffsetDegrees);
		if (!Profile || !Profile->Flipbooks.IsValidIndex(ConfirmedAnimationIndex)
			|| !ConfirmedImpact.bRequestValid
			|| !AreDirectionalTopologyImpactsEquivalent(Impact, ConfirmedImpact))
		{
			ConfirmedImpact.bRequestValid = false;
			ConfirmedImpact.IssueText =
				TEXT("Directional topology changed while confirmation was open; review the refreshed impact and retry.");
			PublishDirectionalIssue(ConfirmedImpact);
			return false;
		}
		Impact = MoveTemp(ConfirmedImpact);
		AnimationIndex = ConfirmedAnimationIndex;
	}

	return ExecuteDirectionalMutation(
		Profile,
		LOCTEXT("SetDirectionalOverrideTransaction", "Set Directional Animation Override"),
		[Profile,
			AnimationIndex,
			bOverrideProfileSettings,
			DirectionCount,
			AngleOffsetDegrees]()
		{
			return Profile->SetDirectionalOverride(
				AnimationIndex,
				bOverrideProfileSettings,
				DirectionCount,
				AngleOffsetDegrees);
		});
}

bool SProfileDetailsPanel::CommitDirectionalDefaultsForTests(
	const int32 DirectionCount,
	const float AngleOffsetDegrees,
	const bool bConfirmReinterpretation)
{
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	return CommitDirectionalDefaultsInternal(
		Profile,
		DirectionalModelGeneration,
		DirectionCount,
		AngleOffsetDegrees,
		[bConfirmReinterpretation](const FProfileDirectionalTopologyImpact&)
		{
			return bConfirmReinterpretation;
		});
}

bool SProfileDetailsPanel::CommitDirectionalOverrideForTests(
	const FProfileAnimationIdentity& AnimationIdentity,
	const bool bOverrideProfileSettings,
	const int32 DirectionCount,
	const float AngleOffsetDegrees,
	const bool bConfirmReinterpretation)
{
	return CommitDirectionalOverrideInternal(
		AnimationIdentity,
		bOverrideProfileSettings,
		DirectionCount,
		AngleOffsetDegrees,
		[bConfirmReinterpretation](const FProfileDirectionalTopologyImpact&)
		{
			return bConfirmReinterpretation;
		});
}

bool SProfileDetailsPanel::CommitDirectionalDefaultsWithConfirmationForTests(
	const int32 DirectionCount,
	const float AngleOffsetDegrees,
	TFunctionRef<bool(const FProfileDirectionalTopologyImpact&)> ConfirmReinterpretation)
{
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	return CommitDirectionalDefaultsInternal(
		Profile,
		DirectionalModelGeneration,
		DirectionCount,
		AngleOffsetDegrees,
		ConfirmReinterpretation);
}

bool SProfileDetailsPanel::CommitDirectionalOverrideWithConfirmationForTests(
	const FProfileAnimationIdentity& AnimationIdentity,
	const bool bOverrideProfileSettings,
	const int32 DirectionCount,
	const float AngleOffsetDegrees,
	TFunctionRef<bool(const FProfileDirectionalTopologyImpact&)> ConfirmReinterpretation)
{
	return CommitDirectionalOverrideInternal(
		AnimationIdentity,
		bOverrideProfileSettings,
		DirectionCount,
		AngleOffsetDegrees,
		ConfirmReinterpretation);
}

bool SProfileDetailsPanel::BeginProfileDirectionalCountEditForTests()
{
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	if (!Profile)
	{
		return false;
	}
	return StartDirectionalNumericEdit(
		EDirectionalNumericEdit::ProfileCount,
		Profile->DefaultDirectionalCount,
		false);
}

bool SProfileDetailsPanel::CommitCapturedProfileDirectionalCount(
	const int32 DirectionCount,
	TFunctionRef<bool(const FProfileDirectionalTopologyImpact&)> ConfirmReinterpretation)
{
	UPaper2DPlusCharacterProfileAsset* ProfileOwner = nullptr;
	uint64 ModelGeneration = 0;
	if (!ConsumeProfileDirectionalNumericEdit(
		EDirectionalNumericEdit::ProfileCount,
		ProfileOwner,
		ModelGeneration))
	{
		return false;
	}
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	if (!Profile || Profile != ProfileOwner
		|| ModelGeneration != DirectionalModelGeneration)
	{
		return false;
	}
	return CommitDirectionalDefaultsInternal(
		ProfileOwner,
		ModelGeneration,
		DirectionCount,
		GetDirectionalOffsetRepairCounterpart(
			Profile->DefaultDirectionalAngleOffset),
		ConfirmReinterpretation);
}

bool SProfileDetailsPanel::CommitProfileDirectionalCountEditForTests(
	const int32 DirectionCount,
	const bool bConfirmReinterpretation)
{
	return CommitCapturedProfileDirectionalCount(
		DirectionCount,
		[bConfirmReinterpretation](const FProfileDirectionalTopologyImpact&)
		{
			return bConfirmReinterpretation;
		});
}

bool SProfileDetailsPanel::IsDirectionalProfileDefaultsSurfaceVisibleForTests() const
{
	return DirectionalProfileDefaultsSurface.IsValid()
		&& DirectionalProfileDefaultsSurface->GetVisibility().IsVisible();
}

bool SProfileDetailsPanel::BeginLocalDirectionalCountEditForTests()
{
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	const FProfileAnimationIdentity OwnerIdentity =
		GetSelectedDirectionalAnimationIdentity();
	const int32 OwnerIndex = Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
		Profile, OwnerIdentity);
	if (!Profile || !Profile->Flipbooks.IsValidIndex(OwnerIndex))
	{
		return false;
	}
	return StartDirectionalNumericEdit(
		EDirectionalNumericEdit::LocalCount,
		Profile->Flipbooks[OwnerIndex].DirectionalAnimationData.DirectionCount,
		false);
}

bool SProfileDetailsPanel::CommitCapturedLocalDirectionalCount(
	const int32 DirectionCount,
	TFunctionRef<bool(const FProfileDirectionalTopologyImpact&)> ConfirmReinterpretation)
{
	FProfileAnimationIdentity OwnerIdentity;
	if (!ConsumeDirectionalNumericEdit(
		EDirectionalNumericEdit::LocalCount, OwnerIdentity))
	{
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	const int32 OwnerIndex = Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
		Profile, OwnerIdentity);
	if (!Profile || !Profile->Flipbooks.IsValidIndex(OwnerIndex))
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Directional numeric edit expired because its captured animation or Profile owner is no longer available."));
		PublishDirectionalIssue(Impact);
		return false;
	}

	return CommitDirectionalOverrideInternal(
		OwnerIdentity,
		true,
		DirectionCount,
		GetDirectionalOffsetRepairCounterpart(
			Profile->Flipbooks[OwnerIndex]
				.DirectionalAnimationData.AngleOffsetDegrees,
			Profile->DefaultDirectionalAngleOffset),
		ConfirmReinterpretation);
}

bool SProfileDetailsPanel::CommitCapturedLocalDirectionalOffset(
	const float AngleOffsetDegrees,
	TFunctionRef<bool(const FProfileDirectionalTopologyImpact&)> ConfirmReinterpretation)
{
	FProfileAnimationIdentity OwnerIdentity;
	if (!ConsumeDirectionalNumericEdit(
		EDirectionalNumericEdit::LocalOffset, OwnerIdentity))
	{
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	const int32 OwnerIndex = Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
		Profile, OwnerIdentity);
	if (!Profile || !Profile->Flipbooks.IsValidIndex(OwnerIndex))
	{
		FProfileDirectionalTopologyImpact Impact;
		SetInvalidDirectionalImpact(
			Impact,
			TEXT("Directional numeric edit expired because its captured animation or Profile owner is no longer available."));
		PublishDirectionalIssue(Impact);
		return false;
	}

	return CommitDirectionalOverrideInternal(
		OwnerIdentity,
		true,
		GetDirectionalCountRepairCounterpart(
			Profile->Flipbooks[OwnerIndex]
				.DirectionalAnimationData.DirectionCount,
			Profile->DefaultDirectionalCount),
		AngleOffsetDegrees,
		ConfirmReinterpretation);
}

bool SProfileDetailsPanel::CommitSelectedDirectionalOverrideEnabled(
	const bool bOverrideProfileSettings,
	TFunctionRef<bool(const FProfileDirectionalTopologyImpact&)> ConfirmReinterpretation)
{
	UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
	const FProfileAnimationIdentity OwnerIdentity =
		GetSelectedDirectionalAnimationIdentity();
	const int32 OwnerIndex = Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
		Profile, OwnerIdentity);
	if (!Profile || !Profile->Flipbooks.IsValidIndex(OwnerIndex))
	{
		return false;
	}

	int32 DirectionCount = Profile->DefaultDirectionalCount;
	float AngleOffsetDegrees = Profile->DefaultDirectionalAngleOffset;
	if (bOverrideProfileSettings
		&& !Profile->GetEffectiveDirectionalSettings(
			OwnerIndex, DirectionCount, AngleOffsetDegrees))
	{
		return false;
	}
	return CommitDirectionalOverrideInternal(
		OwnerIdentity,
		bOverrideProfileSettings,
		DirectionCount,
		AngleOffsetDegrees,
		ConfirmReinterpretation);
}

bool SProfileDetailsPanel::CommitSelectedDirectionalOverrideEnabledForTests(
	const bool bOverrideProfileSettings,
	const bool bConfirmReinterpretation)
{
	return CommitSelectedDirectionalOverrideEnabled(
		bOverrideProfileSettings,
		[bConfirmReinterpretation](const FProfileDirectionalTopologyImpact&)
		{
			return bConfirmReinterpretation;
		});
}

bool SProfileDetailsPanel::CommitLocalDirectionalCountEditForTests(
	const int32 DirectionCount,
	const bool bConfirmReinterpretation)
{
	return CommitCapturedLocalDirectionalCount(
		DirectionCount,
		[bConfirmReinterpretation](const FProfileDirectionalTopologyImpact&)
		{
			return bConfirmReinterpretation;
		});
}

TSharedRef<SWidget> SProfileDetailsPanel::BuildDirectionalAnimationSection()
{
	auto HasLiveSelection = [this]()
	{
		UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
		const int32 SelectedIndex =
			Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
		return Profile && Profile->Flipbooks.IsValidIndex(SelectedIndex);
	};
	auto GetSelectedIndex = [this]()
	{
		UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
		const int32 SelectedIndex =
			Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
		return Profile && Profile->Flipbooks.IsValidIndex(SelectedIndex)
			? SelectedIndex
			: INDEX_NONE;
	};
	auto ConfirmDefaults = [this](const FProfileDirectionalTopologyImpact& Impact)
	{
		return ConfirmDirectionalReinterpretation(
			Impact,
			LOCTEXT("ConfirmDirectionalDefaultsTitle", "Change Directional Profile Defaults"),
			LOCTEXT(
				"ConfirmDirectionalDefaultsBody",
				"Change the inherited direction topology for every affected animation?"));
	};
	auto ConfirmOverride = [this](const FProfileDirectionalTopologyImpact& Impact)
	{
		return ConfirmDirectionalReinterpretation(
			Impact,
			LOCTEXT("ConfirmDirectionalOverrideTitle", "Change Directional Animation Topology"),
			LOCTEXT(
				"ConfirmDirectionalOverrideBody",
				"Change this animation's direction topology?"));
	};
	// UE 5.0-5.8 SSpinBox dispatches typed and arrow-key commits as
	// OnValueCommitted followed by OnValueChanged. The commit consumes the stable edit owner, so
	// ignore exactly that trailing change instead of letting it capture the completed owner again.
	// Slider release ends with OnEndSliderMovement instead; that callback also clears the marker in
	// case the slider's captured owner disappeared before mouse-up.
	const TSharedRef<EDirectionalNumericEdit> PendingPostCommitValueChanged =
		MakeShared<EDirectionalNumericEdit>(EDirectionalNumericEdit::None);
	auto ExpectPostCommitValueChanged =
		[PendingPostCommitValueChanged](const EDirectionalNumericEdit EditKind)
		{
			*PendingPostCommitValueChanged = EditKind;
		};
	auto ConsumePostCommitValueChanged =
		[PendingPostCommitValueChanged](const EDirectionalNumericEdit EditKind)
		{
			if (*PendingPostCommitValueChanged != EditKind)
			{
				return false;
			}
			*PendingPostCommitValueChanged = EDirectionalNumericEdit::None;
			return true;
		};

	TSharedRef<SSpinBox<int32>> ProfileCount =
		SNew(SSpinBox<int32>)
		.MinValue(UPaper2DPlusCharacterProfileAsset::MinimumDirectionalCount)
		.MaxValue(UPaper2DPlusCharacterProfileAsset::MaximumDirectionalCount)
		.MinSliderValue(UPaper2DPlusCharacterProfileAsset::MinimumDirectionalCount)
		.MaxSliderValue(UPaper2DPlusCharacterProfileAsset::MaximumDirectionalCount)
		.Delta(1)
		.Value_Lambda([this]()
		{
			if (ActiveDirectionalNumericEdit == EDirectionalNumericEdit::ProfileCount)
			{
				return static_cast<int32>(FMath::RoundToInt(DirectionalNumericDraftValue));
			}
			const UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
			return Profile ? Profile->DefaultDirectionalCount : 8;
		})
		.OnBeginSliderMovement_Lambda([this]()
		{
			const UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
			StartDirectionalNumericEdit(
				EDirectionalNumericEdit::ProfileCount,
				Profile ? Profile->DefaultDirectionalCount : 8,
				true);
		})
		.OnValueChanged_Lambda([this, ConsumePostCommitValueChanged](int32 NewValue)
		{
			if (ConsumePostCommitValueChanged(EDirectionalNumericEdit::ProfileCount))
			{
				return;
			}
			if (ActiveDirectionalNumericEdit != EDirectionalNumericEdit::ProfileCount)
			{
				const UPaper2DPlusCharacterProfileAsset* Profile =
					GetLiveDirectionalProfile();
				StartDirectionalNumericEdit(
					EDirectionalNumericEdit::ProfileCount,
					Profile ? Profile->DefaultDirectionalCount : NewValue,
					false);
			}
			if (ActiveDirectionalNumericEdit == EDirectionalNumericEdit::ProfileCount)
			{
				DirectionalNumericDraftValue = static_cast<double>(NewValue);
			}
		})
		.OnValueCommitted_Lambda(
			[this, ConfirmDefaults, ExpectPostCommitValueChanged](
				int32 NewValue, ETextCommit::Type)
		{
			if (ActiveDirectionalNumericEdit == EDirectionalNumericEdit::ProfileCount
				&& bDirectionalNumericSliderMovement)
			{
				return;
			}
			const UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
			if (ActiveDirectionalNumericEdit != EDirectionalNumericEdit::ProfileCount)
			{
				StartDirectionalNumericEdit(
					EDirectionalNumericEdit::ProfileCount,
					Profile ? Profile->DefaultDirectionalCount : NewValue,
					false);
			}
			if (ActiveDirectionalNumericEdit == EDirectionalNumericEdit::ProfileCount)
			{
				ExpectPostCommitValueChanged(EDirectionalNumericEdit::ProfileCount);
			}
			CommitCapturedProfileDirectionalCount(NewValue, ConfirmDefaults);
		})
		.OnEndSliderMovement_Lambda(
			[this, ConfirmDefaults, ConsumePostCommitValueChanged](int32 NewValue)
		{
			ConsumePostCommitValueChanged(EDirectionalNumericEdit::ProfileCount);
			if (ActiveDirectionalNumericEdit != EDirectionalNumericEdit::ProfileCount)
			{
				return;
			}
			CommitCapturedProfileDirectionalCount(NewValue, ConfirmDefaults);
		});

	TSharedRef<SSpinBox<float>> ProfileOffset =
		SNew(SSpinBox<float>)
		.MinValue(UPaper2DPlusCharacterProfileAsset::MinimumDirectionalAngleOffset)
		.MaxValue(UPaper2DPlusCharacterProfileAsset::MaximumDirectionalAngleOffset)
		.MinSliderValue(UPaper2DPlusCharacterProfileAsset::MinimumDirectionalAngleOffset)
		.MaxSliderValue(UPaper2DPlusCharacterProfileAsset::MaximumDirectionalAngleOffset)
		.Delta(1.0f)
		.Value_Lambda([this]()
		{
			if (ActiveDirectionalNumericEdit == EDirectionalNumericEdit::ProfileOffset)
			{
				return static_cast<float>(DirectionalNumericDraftValue);
			}
			const UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
			return Profile ? Profile->DefaultDirectionalAngleOffset : 0.0f;
		})
		.OnBeginSliderMovement_Lambda([this]()
		{
			const UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
			StartDirectionalNumericEdit(
				EDirectionalNumericEdit::ProfileOffset,
				Profile ? Profile->DefaultDirectionalAngleOffset : 0.0,
				true);
		})
		.OnValueChanged_Lambda([this, ConsumePostCommitValueChanged](float NewValue)
		{
			if (ConsumePostCommitValueChanged(EDirectionalNumericEdit::ProfileOffset))
			{
				return;
			}
			if (ActiveDirectionalNumericEdit != EDirectionalNumericEdit::ProfileOffset)
			{
				const UPaper2DPlusCharacterProfileAsset* Profile =
					GetLiveDirectionalProfile();
				StartDirectionalNumericEdit(
					EDirectionalNumericEdit::ProfileOffset,
					Profile ? Profile->DefaultDirectionalAngleOffset : NewValue,
					false);
			}
			if (ActiveDirectionalNumericEdit == EDirectionalNumericEdit::ProfileOffset)
			{
				DirectionalNumericDraftValue = NewValue;
			}
		})
		.OnValueCommitted_Lambda(
			[this, ConfirmDefaults, ExpectPostCommitValueChanged](
				float NewValue, ETextCommit::Type)
		{
			if (ActiveDirectionalNumericEdit == EDirectionalNumericEdit::ProfileOffset
				&& bDirectionalNumericSliderMovement)
			{
				return;
			}
			const UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
			if (ActiveDirectionalNumericEdit != EDirectionalNumericEdit::ProfileOffset)
			{
				StartDirectionalNumericEdit(
					EDirectionalNumericEdit::ProfileOffset,
					Profile ? Profile->DefaultDirectionalAngleOffset : NewValue,
					false);
			}
			if (ActiveDirectionalNumericEdit == EDirectionalNumericEdit::ProfileOffset)
			{
				ExpectPostCommitValueChanged(EDirectionalNumericEdit::ProfileOffset);
			}
			UPaper2DPlusCharacterProfileAsset* ProfileOwner = nullptr;
			uint64 ModelGeneration = 0;
			if (!ConsumeProfileDirectionalNumericEdit(
				EDirectionalNumericEdit::ProfileOffset,
				ProfileOwner,
				ModelGeneration))
			{
				return;
			}
			Profile = GetLiveDirectionalProfile();
			if (!Profile || Profile != ProfileOwner
				|| ModelGeneration != DirectionalModelGeneration)
			{
				return;
			}
			CommitDirectionalDefaultsInternal(
				ProfileOwner,
				ModelGeneration,
				GetDirectionalCountRepairCounterpart(
					Profile->DefaultDirectionalCount),
				NewValue,
				ConfirmDefaults);
		})
		.OnEndSliderMovement_Lambda(
			[this, ConfirmDefaults, ConsumePostCommitValueChanged](float NewValue)
		{
			ConsumePostCommitValueChanged(EDirectionalNumericEdit::ProfileOffset);
			if (ActiveDirectionalNumericEdit != EDirectionalNumericEdit::ProfileOffset)
			{
				return;
			}
			UPaper2DPlusCharacterProfileAsset* ProfileOwner = nullptr;
			uint64 ModelGeneration = 0;
			if (!ConsumeProfileDirectionalNumericEdit(
				EDirectionalNumericEdit::ProfileOffset,
				ProfileOwner,
				ModelGeneration))
			{
				return;
			}
			const UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
			if (!Profile || Profile != ProfileOwner
				|| ModelGeneration != DirectionalModelGeneration)
			{
				return;
			}
			CommitDirectionalDefaultsInternal(
				ProfileOwner,
				ModelGeneration,
				GetDirectionalCountRepairCounterpart(
					Profile->DefaultDirectionalCount),
				NewValue,
				ConfirmDefaults);
		});

	TSharedRef<SSpinBox<int32>> LocalCount =
		SNew(SSpinBox<int32>)
		.MinValue(UPaper2DPlusCharacterProfileAsset::MinimumDirectionalCount)
		.MaxValue(UPaper2DPlusCharacterProfileAsset::MaximumDirectionalCount)
		.MinSliderValue(UPaper2DPlusCharacterProfileAsset::MinimumDirectionalCount)
		.MaxSliderValue(UPaper2DPlusCharacterProfileAsset::MaximumDirectionalCount)
		.Delta(1)
		.Value_Lambda([this, GetSelectedIndex]()
		{
			if (ActiveDirectionalNumericEdit == EDirectionalNumericEdit::LocalCount)
			{
				return static_cast<int32>(FMath::RoundToInt(DirectionalNumericDraftValue));
			}
			const UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
			const int32 SelectedIndex = GetSelectedIndex();
			return Profile && Profile->Flipbooks.IsValidIndex(SelectedIndex)
				? Profile->Flipbooks[SelectedIndex].DirectionalAnimationData.DirectionCount
				: 8;
		})
		.OnBeginSliderMovement_Lambda([this]()
		{
			const UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
			const FProfileAnimationIdentity OwnerIdentity =
				GetSelectedDirectionalAnimationIdentity();
			const int32 OwnerIndex = Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
				Profile, OwnerIdentity);
			StartDirectionalNumericEdit(
				EDirectionalNumericEdit::LocalCount,
				Profile && Profile->Flipbooks.IsValidIndex(OwnerIndex)
					? Profile->Flipbooks[OwnerIndex].DirectionalAnimationData.DirectionCount
					: 8,
				true);
		})
		.OnValueChanged_Lambda([this, ConsumePostCommitValueChanged](int32 NewValue)
		{
			if (ConsumePostCommitValueChanged(EDirectionalNumericEdit::LocalCount))
			{
				return;
			}
			if (ActiveDirectionalNumericEdit != EDirectionalNumericEdit::LocalCount)
			{
				BeginLocalDirectionalCountEditForTests();
			}
			if (ActiveDirectionalNumericEdit == EDirectionalNumericEdit::LocalCount)
			{
				DirectionalNumericDraftValue = static_cast<double>(NewValue);
			}
		})
		.OnValueCommitted_Lambda(
			[this, ConfirmOverride, ExpectPostCommitValueChanged](
				int32 NewValue, ETextCommit::Type)
		{
			if (ActiveDirectionalNumericEdit == EDirectionalNumericEdit::LocalCount
				&& bDirectionalNumericSliderMovement)
			{
				return;
			}
			if (ActiveDirectionalNumericEdit != EDirectionalNumericEdit::LocalCount
				&& !BeginLocalDirectionalCountEditForTests())
			{
				return;
			}
			ExpectPostCommitValueChanged(EDirectionalNumericEdit::LocalCount);
			CommitCapturedLocalDirectionalCount(NewValue, ConfirmOverride);
		})
		.OnEndSliderMovement_Lambda(
			[this, ConfirmOverride, ConsumePostCommitValueChanged](int32 NewValue)
		{
			ConsumePostCommitValueChanged(EDirectionalNumericEdit::LocalCount);
			if (ActiveDirectionalNumericEdit != EDirectionalNumericEdit::LocalCount)
			{
				return;
			}
			CommitCapturedLocalDirectionalCount(NewValue, ConfirmOverride);
		});

	TSharedRef<SSpinBox<float>> LocalOffset =
		SNew(SSpinBox<float>)
		.MinValue(UPaper2DPlusCharacterProfileAsset::MinimumDirectionalAngleOffset)
		.MaxValue(UPaper2DPlusCharacterProfileAsset::MaximumDirectionalAngleOffset)
		.MinSliderValue(UPaper2DPlusCharacterProfileAsset::MinimumDirectionalAngleOffset)
		.MaxSliderValue(UPaper2DPlusCharacterProfileAsset::MaximumDirectionalAngleOffset)
		.Delta(1.0f)
		.Value_Lambda([this, GetSelectedIndex]()
		{
			if (ActiveDirectionalNumericEdit == EDirectionalNumericEdit::LocalOffset)
			{
				return static_cast<float>(DirectionalNumericDraftValue);
			}
			const UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
			const int32 SelectedIndex = GetSelectedIndex();
			return Profile && Profile->Flipbooks.IsValidIndex(SelectedIndex)
				? Profile->Flipbooks[SelectedIndex].DirectionalAnimationData.AngleOffsetDegrees
				: 0.0f;
		})
		.OnBeginSliderMovement_Lambda([this]()
		{
			const UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
			const FProfileAnimationIdentity OwnerIdentity =
				GetSelectedDirectionalAnimationIdentity();
			const int32 OwnerIndex = Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
				Profile, OwnerIdentity);
			StartDirectionalNumericEdit(
				EDirectionalNumericEdit::LocalOffset,
				Profile && Profile->Flipbooks.IsValidIndex(OwnerIndex)
					? Profile->Flipbooks[OwnerIndex].DirectionalAnimationData.AngleOffsetDegrees
					: 0.0,
				true);
		})
		.OnValueChanged_Lambda([this, ConsumePostCommitValueChanged](float NewValue)
		{
			if (ConsumePostCommitValueChanged(EDirectionalNumericEdit::LocalOffset))
			{
				return;
			}
			if (ActiveDirectionalNumericEdit != EDirectionalNumericEdit::LocalOffset)
			{
				const UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
				const FProfileAnimationIdentity OwnerIdentity =
					GetSelectedDirectionalAnimationIdentity();
				const int32 OwnerIndex =
					Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
						Profile, OwnerIdentity);
				StartDirectionalNumericEdit(
					EDirectionalNumericEdit::LocalOffset,
					Profile && Profile->Flipbooks.IsValidIndex(OwnerIndex)
						? Profile->Flipbooks[OwnerIndex]
							.DirectionalAnimationData.AngleOffsetDegrees
						: NewValue,
					false);
			}
			if (ActiveDirectionalNumericEdit == EDirectionalNumericEdit::LocalOffset)
			{
				DirectionalNumericDraftValue = NewValue;
			}
		})
		.OnValueCommitted_Lambda(
			[this, ConfirmOverride, ExpectPostCommitValueChanged](
				float NewValue, ETextCommit::Type)
		{
			if (ActiveDirectionalNumericEdit == EDirectionalNumericEdit::LocalOffset
				&& bDirectionalNumericSliderMovement)
			{
				return;
			}
			if (ActiveDirectionalNumericEdit != EDirectionalNumericEdit::LocalOffset)
			{
				const UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
				const int32 OwnerIndex = Paper2DPlusProfileToolProvider::ResolveAnimationIndex(
					Profile, GetSelectedDirectionalAnimationIdentity());
				if (!Profile || !Profile->Flipbooks.IsValidIndex(OwnerIndex)
					|| !StartDirectionalNumericEdit(
						EDirectionalNumericEdit::LocalOffset,
						Profile->Flipbooks[OwnerIndex]
							.DirectionalAnimationData.AngleOffsetDegrees,
						false))
				{
					return;
				}
			}
			ExpectPostCommitValueChanged(EDirectionalNumericEdit::LocalOffset);
			CommitCapturedLocalDirectionalOffset(NewValue, ConfirmOverride);
		})
		.OnEndSliderMovement_Lambda(
			[this, ConfirmOverride, ConsumePostCommitValueChanged](float NewValue)
		{
			ConsumePostCommitValueChanged(EDirectionalNumericEdit::LocalOffset);
			if (ActiveDirectionalNumericEdit != EDirectionalNumericEdit::LocalOffset)
			{
				return;
			}
			CommitCapturedLocalDirectionalOffset(NewValue, ConfirmOverride);
		});

	LocalCount->SetTag(FName(TEXT("Paper2DPlus.Directional.LocalCount")));
	LocalOffset->SetTag(FName(TEXT("Paper2DPlus.Directional.LocalOffset")));

	// The per-slot inventory: one details-style row per active slot, so a Directional Set is
	// visible and editable as a list (with drag-drop via the standard soft-asset picker) instead
	// of hiding every assignment behind the transient wheel. Rows exist for the maximum count and
	// gate on the live effective topology, so count changes need no rebuild.
	auto SlotRowVisible = [this, GetSelectedIndex](const int32 SlotIndex)
	{
		const UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
		const int32 SelectedIndex = GetSelectedIndex();
		int32 DirectionCount = 0;
		float AngleOffsetDegrees = 0.0f;
		return Profile && Profile->Flipbooks.IsValidIndex(SelectedIndex)
			&& Profile->HasDirectionalSet(SelectedIndex)
			&& Profile->GetEffectiveDirectionalSettings(
				SelectedIndex, DirectionCount, AngleOffsetDegrees)
			&& SlotIndex < DirectionCount
			? EVisibility::Visible
			: EVisibility::Collapsed;
	};
	auto SlotDirectionText = [this, GetSelectedIndex](const int32 SlotIndex)
	{
		const UPaper2DPlusCharacterProfileAsset* Profile = GetLiveDirectionalProfile();
		const int32 SelectedIndex = GetSelectedIndex();
		int32 DirectionCount = 0;
		float AngleOffsetDegrees = 0.0f;
		if (!Profile || !Profile->Flipbooks.IsValidIndex(SelectedIndex)
			|| !Profile->GetEffectiveDirectionalSettings(
				SelectedIndex, DirectionCount, AngleOffsetDegrees)
			|| SlotIndex >= DirectionCount)
		{
			return FText::GetEmpty();
		}
		return FText::FromString(FString::Printf(
			TEXT("%s · %.0f°"),
			*SDirectionalAnimationWheel::GetSlotDirectionLabel(
				SlotIndex, DirectionCount, AngleOffsetDegrees),
			GetDirectionalSlotCenterBearing(
				SlotIndex, DirectionCount, AngleOffsetDegrees)));
	};
	const TSharedRef<SVerticalBox> SlotRowsBox = SNew(SVerticalBox);
	for (int32 SlotIndex = 0;
		SlotIndex < UPaper2DPlusCharacterProfileAsset::MaximumDirectionalCount;
		++SlotIndex)
	{
		SlotRowsBox->AddSlot()
		.AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([SlotRowVisible, SlotIndex]()
			{
				return SlotRowVisible(SlotIndex);
			})
			[
				FProfilePropertyRowUtils::MakeRow(
					FText::Format(
						LOCTEXT("DirectionalSlotRowLabelFmt", "Slot {0}"),
						FText::AsNumber(SlotIndex)),
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(SBox)
						.MinDesiredWidth(52.0f)
						[
							SNew(STextBlock)
							.Text_Lambda([SlotDirectionText, SlotIndex]()
							{
								return SlotDirectionText(SlotIndex);
							})
							.Font(FProfilePropertyRowUtils::GetPropertyFont())
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(UPaperFlipbook::StaticClass())
						.AllowClear(true)
						.DisplayThumbnail(false)
						.ObjectPath_Lambda([this, GetSelectedIndex, SlotIndex]() -> FString
						{
							const UPaper2DPlusCharacterProfileAsset* Profile =
								GetLiveDirectionalProfile();
							const int32 SelectedIndex = GetSelectedIndex();
							TSoftObjectPtr<UPaperFlipbook> Assigned;
							return Profile
								&& Profile->Flipbooks.IsValidIndex(SelectedIndex)
								&& Profile->GetDirectionalSlot(
									SelectedIndex, SlotIndex, Assigned)
								? Assigned.ToSoftObjectPath().ToString()
								: FString();
						})
						.OnObjectChanged_Lambda([this, GetSelectedIndex, SlotIndex](
							const FAssetData& AssetData)
						{
							const FProfileAnimationIdentity OwnerIdentity =
								GetSelectedDirectionalAnimationIdentity();
							if (AssetData.IsValid())
							{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
								const FSoftObjectPath PickedPath =
									AssetData.ToSoftObjectPath();
#else
								const FSoftObjectPath PickedPath =
									AssetData.GetSoftObjectPath();
#endif
								CommitDirectionalSlot(
									OwnerIdentity,
									SlotIndex,
									TSoftObjectPtr<UPaperFlipbook>(PickedPath));
								return;
							}
							// Clear only an authored record so clearing an already-empty
							// picker never publishes an expiry issue.
							const UPaper2DPlusCharacterProfileAsset* Profile =
								GetLiveDirectionalProfile();
							const int32 SelectedIndex = GetSelectedIndex();
							TSoftObjectPtr<UPaperFlipbook> Assigned;
							if (Profile && Profile->Flipbooks.IsValidIndex(SelectedIndex)
								&& Profile->GetDirectionalSlot(
									SelectedIndex, SlotIndex, Assigned))
							{
								ClearDirectionalSlot(OwnerIdentity, SlotIndex);
							}
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SCheckBox)
						.Visibility_Lambda([this, GetSelectedIndex, SlotIndex]()
						{
							const UPaper2DPlusCharacterProfileAsset* Profile =
								GetLiveDirectionalProfile();
							const int32 SelectedIndex = GetSelectedIndex();
							TSoftObjectPtr<UPaperFlipbook> Assigned;
							return Profile
								&& Profile->Flipbooks.IsValidIndex(SelectedIndex)
								&& Profile->GetDirectionalSlot(
									SelectedIndex, SlotIndex, Assigned)
								? EVisibility::Visible
								: EVisibility::Collapsed;
						})
						.IsChecked_Lambda([this, GetSelectedIndex, SlotIndex]()
						{
							const UPaper2DPlusCharacterProfileAsset* Profile =
								GetLiveDirectionalProfile();
							const int32 SelectedIndex = GetSelectedIndex();
							TSoftObjectPtr<UPaperFlipbook> Assigned;
							bool bMirror = false;
							return Profile
								&& Profile->Flipbooks.IsValidIndex(SelectedIndex)
								&& Profile->GetDirectionalSlot(
									SelectedIndex, SlotIndex, Assigned, bMirror)
								&& bMirror
								? ECheckBoxState::Checked
								: ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([this, SlotIndex](
							const ECheckBoxState NewState)
						{
							CommitDirectionalSlotMirror(
								GetSelectedDirectionalAnimationIdentity(),
								SlotIndex,
								NewState == ECheckBoxState::Checked);
						})
						.ToolTipText(LOCTEXT(
							"DirectionalSlotMirrorTip",
							"Present this slot's art horizontally mirrored. The resolver returns the flag beside the flipbook; your game applies the flip (typically actor scale), so a standard 8-way set can ship five authored facings plus three mirrored reuses."))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("DirectionalSlotMirrorLabel", "Mirror"))
							.Font(FProfilePropertyRowUtils::GetPropertyFont())
						]
					],
					LOCTEXT(
						"DirectionalSlotRowTip",
						"The flipbook shown at this direction. Empty slots fail resolution exactly — there is no nearest-slot or base fallback."),
					90.0f)
			]
		];
	}

	return SAssignNew(DirectionalProfileDefaultsSurface, SBox)
		.Visibility_Lambda([this]()
		{
			return GetLiveDirectionalProfile()
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				FProfilePropertyRowUtils::MakeSectionTitle(
					LOCTEXT("DirectionalAnimationSectionTitle", "Directional Animation"))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				FProfilePropertyRowUtils::MakeSectionHint(
					LOCTEXT(
						"DirectionalAnimationSectionHint",
						"One logical animation can own 3-16 evenly spaced visual directions. Hitboxes, root motion, Frame Cues, transitions, and timing remain shared on the base animation."))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				FProfilePropertyRowUtils::MakeSectionHint(
					LOCTEXT(
						"DirectionalProfileScopeHint",
						"Profile defaults — inherited by every animation without Local Settings."))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				FProfilePropertyRowUtils::MakeRow(
					LOCTEXT("DirectionalProfileCountLabel", "Direction Count"),
					ProfileCount,
					LOCTEXT("DirectionalProfileCountTip", "Profile-wide default direction count inherited by animations without a local override."),
					90.0f,
					140.0f)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				FProfilePropertyRowUtils::MakeRow(
					LOCTEXT("DirectionalProfileOffsetLabel", "Angle Offset"),
					ProfileOffset,
					LOCTEXT("DirectionalProfileOffsetTip", "Profile-wide inherited angle offset in degrees. A positive value treats the incoming facing as that much more clockwise before it buckets, so the sector layout rotates counter-clockwise on screen. Sign-compatible with PaperZD's Directional Angle Offset."),
					90.0f,
					140.0f)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([HasLiveSelection]()
				{
					return HasLiveSelection()
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				[
					FProfilePropertyRowUtils::MakeRow(
						LOCTEXT("DirectionalSetPresenceLabel", "Directional Set"),
						SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([this, GetSelectedIndex]()
						{
							const UPaper2DPlusCharacterProfileAsset* Profile =
								GetLiveDirectionalProfile();
							const int32 SelectedIndex = GetSelectedIndex();
							if (!Profile || !Profile->Flipbooks.IsValidIndex(SelectedIndex)
								|| !Profile->HasDirectionalSet(SelectedIndex))
							{
								return LOCTEXT("DirectionalSetAbsent", "Not configured (base only)");
							}
							int32 Occupied = 0;
							for (const FPaper2DPlusDirectionalAnimationSlot& Slot :
								Profile->Flipbooks[SelectedIndex].DirectionalAnimationData.Slots)
							{
								Occupied += !Slot.Flipbook.IsNull() ? 1 : 0;
							}
							int32 DirectionCount = 0;
							float AngleOffsetDegrees = 0.0f;
							if (Occupied == 0)
							{
								return LOCTEXT("DirectionalSetConfiguredEmpty", "Configured empty");
							}
							return Profile->GetEffectiveDirectionalSettings(
								SelectedIndex, DirectionCount, AngleOffsetDegrees)
								? FText::Format(
									LOCTEXT(
										"DirectionalSetCoverageFmt",
										"{0} of {1} directions assigned"),
									FText::AsNumber(Occupied),
									FText::AsNumber(DirectionCount))
								: FText::Format(
									LOCTEXT("DirectionalSetOccupiedFmt", "{0} occupied slot(s)"),
									FText::AsNumber(Occupied));
						})
						.Font(FProfilePropertyRowUtils::GetPropertyFont())
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.Text(LOCTEXT("EnableDirectionalSetButton", "Enable"))
						.ToolTipText(LOCTEXT("EnableDirectionalSetTip", "Create an explicit configured-empty Directional Set for this animation."))
						.Visibility_Lambda([this, GetSelectedIndex]()
						{
							const UPaper2DPlusCharacterProfileAsset* Profile =
								GetLiveDirectionalProfile();
							const int32 SelectedIndex = GetSelectedIndex();
							return Profile && Profile->Flipbooks.IsValidIndex(SelectedIndex)
								&& !Profile->HasDirectionalSet(SelectedIndex)
								? EVisibility::Visible
								: EVisibility::Collapsed;
						})
						.OnClicked_Lambda([this]()
						{
							EnableDirectionalSet(GetSelectedDirectionalAnimationIdentity());
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.Text(LOCTEXT("RemoveDirectionalSetButton", "Remove"))
						.ToolTipText(LOCTEXT("RemoveDirectionalSetTip", "Remove explicit Directional Set presence. Clear every occupied slot first."))
						.Visibility_Lambda([this, GetSelectedIndex]()
						{
							const UPaper2DPlusCharacterProfileAsset* Profile =
								GetLiveDirectionalProfile();
							const int32 SelectedIndex = GetSelectedIndex();
							return Profile && Profile->Flipbooks.IsValidIndex(SelectedIndex)
								&& Profile->HasDirectionalSet(SelectedIndex)
								? EVisibility::Visible
								: EVisibility::Collapsed;
						})
						.IsEnabled_Lambda([this, GetSelectedIndex]()
						{
							const UPaper2DPlusCharacterProfileAsset* Profile =
								GetLiveDirectionalProfile();
							const int32 SelectedIndex = GetSelectedIndex();
							if (!Profile || !Profile->Flipbooks.IsValidIndex(SelectedIndex))
							{
								return false;
							}
							return !Profile->Flipbooks[SelectedIndex]
								.DirectionalAnimationData.Slots.ContainsByPredicate(
									[](const FPaper2DPlusDirectionalAnimationSlot& Slot)
									{
										return !Slot.Flipbook.IsNull();
									});
						})
						.OnClicked_Lambda([this]()
						{
							RemoveDirectionalSet(GetSelectedDirectionalAnimationIdentity());
							return FReply::Handled();
						})
						]
					)
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([this, GetSelectedIndex]()
				{
					const UPaper2DPlusCharacterProfileAsset* Profile =
						GetLiveDirectionalProfile();
					const int32 SelectedIndex = GetSelectedIndex();
					return Profile && Profile->HasDirectionalSet(SelectedIndex)
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				[
					FProfilePropertyRowUtils::MakeRow(
						LOCTEXT("DirectionalOverrideLabel", "Local Settings"),
						SNew(SCheckBox)
						.IsChecked_Lambda([this, GetSelectedIndex]()
						{
							const UPaper2DPlusCharacterProfileAsset* Profile =
								GetLiveDirectionalProfile();
							const int32 SelectedIndex = GetSelectedIndex();
							return Profile && Profile->Flipbooks.IsValidIndex(SelectedIndex)
								&& Profile->Flipbooks[SelectedIndex]
									.DirectionalAnimationData.bOverrideProfileSettings
								? ECheckBoxState::Checked
								: ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([this, ConfirmOverride](ECheckBoxState NewState)
						{
							CommitSelectedDirectionalOverrideEnabled(
								NewState == ECheckBoxState::Checked,
								ConfirmOverride);
						})
						[
							SNew(STextBlock)
							.Text(LOCTEXT("DirectionalOverrideCheckText", "Override Profile count and offset"))
							.Font(FProfilePropertyRowUtils::GetPropertyFont())
						],
						LOCTEXT("DirectionalOverrideTip", "Unchecked inherits both Profile values. Checked authors both values locally."))
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([this, GetSelectedIndex]()
				{
					const UPaper2DPlusCharacterProfileAsset* Profile =
						GetLiveDirectionalProfile();
					const int32 SelectedIndex = GetSelectedIndex();
					return Profile && Profile->Flipbooks.IsValidIndex(SelectedIndex)
						&& Profile->HasDirectionalSet(SelectedIndex)
						&& Profile->Flipbooks[SelectedIndex]
							.DirectionalAnimationData.bOverrideProfileSettings
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						FProfilePropertyRowUtils::MakeRow(
							LOCTEXT("DirectionalLocalCountLabel", "Local Count"),
							LocalCount,
							LOCTEXT("DirectionalLocalCountTip", "Direction count used only by this animation."),
							90.0f,
							140.0f)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						FProfilePropertyRowUtils::MakeRow(
							LOCTEXT("DirectionalLocalOffsetLabel", "Local Offset"),
							LocalOffset,
							LOCTEXT("DirectionalLocalOffsetTip", "Angle offset in degrees used only by this animation. Positive rotates the sector layout counter-clockwise on screen (the incoming facing buckets as more clockwise). Sign-compatible with PaperZD's Directional Angle Offset."),
							90.0f,
							140.0f)
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([this, GetSelectedIndex]()
				{
					const UPaper2DPlusCharacterProfileAsset* Profile =
						GetLiveDirectionalProfile();
					const int32 SelectedIndex = GetSelectedIndex();
					return Profile && Profile->Flipbooks.IsValidIndex(SelectedIndex)
						&& Profile->HasDirectionalSet(SelectedIndex)
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						FProfilePropertyRowUtils::MakeRow(
							LOCTEXT("DirectionalAutoFillLabel", "Fill"),
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
							.Text(LOCTEXT("DirectionalAutoFillButton", "Auto-fill from names…"))
							.ToolTipText(LOCTEXT(
								"DirectionalAutoFillTip",
								"Fill every empty active slot from flipbooks named <Base>_<N/NE/E/...> (zero-offset 4/8/16-way topologies) or <Base>_<slot index> (any topology) in the base flipbook's folder. Existing assignments are never overwritten."))
							.OnClicked_Lambda([this]()
							{
								RunDirectionalNameSuffixAutoFill();
								return FReply::Handled();
							}),
							LOCTEXT(
								"DirectionalAutoFillRowTip",
								"One-gesture assignment from the universal name-suffix convention."),
							90.0f)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SlotRowsBox
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 4, 4, 2)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					return FText::FromString(LastDirectionalIssueText);
				})
				.Visibility_Lambda([this]()
				{
					return LastDirectionalIssueText.IsEmpty()
						? EVisibility::Collapsed
						: EVisibility::Visible;
				})
				.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.45f, 0.20f)))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 0, 4, 4)
			[
				SAssignNew(DirectionalImpactRowsBox, SVerticalBox)
				.Visibility_Lambda([this]()
				{
					return LastDirectionalImpact.Items.IsEmpty()
						? EVisibility::Collapsed
						: EVisibility::Visible;
				})
			]
		];
}

#undef LOCTEXT_NAMESPACE
