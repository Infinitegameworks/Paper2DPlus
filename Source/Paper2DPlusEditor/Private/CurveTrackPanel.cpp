// Copyright 2026 Infinite Gameworks. All Rights Reserved.

/**
 * Curve tracks under Frame Cues — the non-row half of the feature (2026-06-11 brainstorm D1-D11):
 *
 * - Paper2DPlusCurveTracks: the PURE coerce funnel + orphan detector + shared name->color hash
 *   (worldless-tested in Paper2DPlusCurveTracksTest.cpp).
 * - FPaper2DPlusCurvePickerUtils: the ONE add-curve picker (KnownCurves registry + free-form name).
 *   Originally shared with the retired Curves tab (SCurvesPanel, removed in curves PR C); the stack
 *   is now the sole curve-authoring surface.
 * - FPaper2DPlusCurveTrackOwner: the shared FCurveOwnerInterface adapter the engine SCurveEditor
 *   edits through (live resolve, coerce-on-change, cross-version GetCurves guards).
 * - SCurveTrackStack: the per-move shared graph + identity rows + gesture-liveness gate + the
 *   Paper2DPlus.CurveTracksProbe console command. The unified timeline owns "+ Add Curve".
 *
 * SCurveTrackRow lives in CurveTrackRow.cpp (one widget class per .cpp).
 */

#include "CurveTrackPanel.h"
#include "Paper2DPlusSettings.h"
#include "Paper2DPlusMoveTransition.h"
#include "PaperFlipbook.h"
#include "SFrameCueTimeline.h"
#include "SCurveEditor.h"
#include "ScopedTransaction.h"
#include "EditorCanvasUtils.h" // MakePaintGeometry (cross-version ToPaintGeometry shim)
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectGlobals.h" // FCoreUObjectDelegates::OnObjectModified (F6 out-of-band reattach)

#define LOCTEXT_NAMESPACE "CurveTrackPanel"

// =====================================================================================================
// Paper2DPlusCurveTracks — PURE coerce funnel / orphan detector / color hash
// =====================================================================================================

namespace Paper2DPlusCurveTracks
{
	FCurveCoerceSummary CoerceCurveKeysToFrames(FRichCurve& Curve, int32 FrameCount, EPaper2DPlusCurveInterp Mode)
	{
		// UE_KINDA_SMALL_NUMBER spelled out: the UE_-prefixed math constants arrived in 5.1, and this
		// file builds on the 5.0 cross-version harness leg.
		constexpr float CoerceTolerance = 1.e-4f;

		FCurveCoerceSummary Summary;

		// Pass 1: classify every key — original time, rounded frame, clamped target, and the
		// distance-from-integer that drives the collision heuristic.
		struct FCoerceEntry
		{
			FKeyHandle Handle;
			float OriginalTime = 0.f;
			int32 TargetFrame = 0;
			float FracDistance = 0.f;
			bool bRounded = false;
			bool bClamped = false;
		};
		TArray<FCoerceEntry> Entries;
		for (auto It = Curve.GetKeyHandleIterator(); It; ++It)
		{
			FCoerceEntry Entry;
			Entry.Handle = *It;
			Entry.OriginalTime = Curve.GetKeyTime(*It);
			const int32 Rounded = FMath::RoundToInt(Entry.OriginalTime);
			Entry.bRounded = !FMath::IsNearlyEqual(Entry.OriginalTime, static_cast<float>(Rounded), CoerceTolerance);
			int32 Target = Rounded;
			if (FrameCount > 0 && Entry.bRounded)
			{
				// Clamp into the valid frame band — but ONLY keys whose pre-coerce time was FRACTIONAL,
				// i.e. keys actively moved this gesture (F4/CT-5). Exactly-integral out-of-range keys
				// are LEGACY ORPHANS (flipbook shrink/reimport — including a snapped drag landing
				// exactly on an out-of-range integer): leave them for the orphan detector / prune path,
				// otherwise any unrelated edit on a curve carrying legacy orphans cascades clamp+dedupe
				// and silently destroys the authored last-frame key. FrameCount <= 0 means there is no
				// valid band to clamp into at all (no flipbook resolved) — leave the rounded frame; the
				// orphan detector's caller surfaces the frames-unavailable state instead (F3).
				Target = FMath::Clamp(Rounded, 0, FrameCount - 1);
			}
			Entry.bClamped = (Target != Rounded);
			Entry.TargetFrame = Target;
			Entry.FracDistance = FMath::Abs(Entry.OriginalTime - static_cast<float>(Rounded));
			Entries.Add(Entry);
		}

		// Pass 2: dedupe target-frame collisions. KEEP the key whose pre-coerce time was FURTHER from
		// the integer — the deterministic stand-in for "the key that was just dragged" (a mid-gesture /
		// MMB-free-dragged key carries a fractional time; a stationary key sits exactly on its frame).
		// Tie-break (both exactly integral — the snapped-drag-onto-an-occupied-frame case): keep the
		// EARLIER key in iteration order. Verified against 5.7 RichCurve.cpp: AddKey inserts an
		// equal-time key BEFORE existing ones and SetKeyTime is DeleteKey+AddKey, so the most
		// recently MOVED key is always the earlier equal-time array entry — earlier-wins keeps the
		// dragged key. Deleted keys count as NumDeduped only (their rounding/clamping never applied).
		TMap<int32, int32> WinnerByFrame; // TargetFrame -> index into Entries
		TArray<FKeyHandle> KeysToDelete;
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			if (int32* ExistingIndex = WinnerByFrame.Find(Entries[Index].TargetFrame))
			{
				const FCoerceEntry& Incumbent = Entries[*ExistingIndex];
				const FCoerceEntry& Challenger = Entries[Index];
				// Strictly-further-beyond-tolerance wins; a tie therefore keeps the incumbent (the
				// EARLIER array entry = the most recently moved key, per the insert-before-equal note).
				const bool bChallengerWins =
					Challenger.FracDistance > Incumbent.FracDistance + CoerceTolerance;
				if (bChallengerWins)
				{
					KeysToDelete.Add(Incumbent.Handle);
					*ExistingIndex = Index;
				}
				else
				{
					KeysToDelete.Add(Challenger.Handle);
				}
				++Summary.NumDeduped;
			}
			else
			{
				WinnerByFrame.Add(Entries[Index].TargetFrame, Index);
			}
		}
		for (const FKeyHandle& Handle : KeysToDelete)
		{
			Curve.DeleteKey(Handle); // handles of surviving keys stay valid across deletes
		}

		// Pass 3: snap survivors onto their integer frames. Final targets are unique per survivor, so
		// no transient SetKeyTime ever creates a duplicate-time end state.
		for (const TPair<int32, int32>& Pair : WinnerByFrame)
		{
			const FCoerceEntry& Entry = Entries[Pair.Value];
			if (Entry.bRounded || Entry.bClamped)
			{
				Curve.SetKeyTime(Entry.Handle, static_cast<float>(Entry.TargetFrame));
				if (Entry.bRounded)
				{
					++Summary.NumRounded;
				}
				if (Entry.bClamped)
				{
					++Summary.NumClamped;
				}
			}
		}

		// Pass 4: re-stamp Mode over every remaining key (SetMode semantics) — the engine's per-key RMB
		// interp menu sets per-key modes the data model does not have; the per-curve Mode invariant wins.
		const ERichCurveInterpMode RichMode = FPaper2DPlusFrameCurve::ToRichCurveInterpMode(Mode);
		for (auto It = Curve.GetKeyHandleIterator(); It; ++It)
		{
			if (Curve.GetKeyInterpMode(*It) != RichMode)
			{
				Curve.SetKeyInterpMode(*It, RichMode);
				Summary.bChanged = true;
			}
		}

		Summary.bChanged = Summary.bChanged || Summary.NumRounded > 0 || Summary.NumDeduped > 0 || Summary.NumClamped > 0;
		return Summary;
	}

	static bool IsOrphanKeyTime(const float KeyTime, const int32 FrameCount)
	{
		if (FrameCount <= 0)
		{
			return false;
		}
		const int32 Frame = FMath::RoundToInt(KeyTime);
		return Frame < 0 || Frame >= FrameCount;
	}

	TArray<int32> FindOrphanKeyFrames(const FRichCurve& Curve, int32 FrameCount)
	{
		TArray<int32> Orphans;
		if (FrameCount <= 0)
		{
			// No flipbook resolved -> orphan state is UNKNOWN, not "everything is an orphan" (F3/CT-4).
			// Contextual Details surfaces a distinct frames-unavailable state; prune disables.
			return Orphans;
		}
		for (const FRichCurveKey& Key : Curve.GetConstRefOfKeys())
		{
			if (IsOrphanKeyTime(Key.Time, FrameCount))
			{
				Orphans.Add(FMath::RoundToInt(Key.Time));
			}
		}
		Orphans.Sort();
		return Orphans;
	}

	FLinearColor NameToColor(FName CurveName)
	{
		// Deterministic hue from the name's string hash so a curve keeps its color across
		// refreshes/sessions (originally in CurvesPanel.cpp; THE single implementation since the
		// Curves tab — and its forwarding Paper2DPlusCurveGraph namespace — retired in curves PR C).
		const uint32 Hash = GetTypeHash(CurveName.ToString().ToLower());
		const float Hue = static_cast<float>(Hash % 360u);
		// Golden-ratio-ish jitter on saturation/value keeps adjacent hashes visually distinct.
		const float Sat = 0.65f + static_cast<float>((Hash / 360u) % 20u) * 0.01f; // 0.65..0.84
		const float Val = 0.85f;
		return FLinearColor::MakeFromHSV8(
			static_cast<uint8>(Hue / 360.0f * 255.0f),
			static_cast<uint8>(Sat * 255.0f),
			static_cast<uint8>(Val * 255.0f));
	}

	void FCurvePresentationState::Reconcile(const TArray<FName>& AuthoredCurveNames)
	{
		TSet<FName> ReconciledNames;
		for (const FName& CurveName : AuthoredCurveNames)
		{
			if (CurveName.IsNone())
			{
				continue;
			}
			ReconciledNames.Add(CurveName);
			if (!KnownCurves.Contains(CurveName))
			{
				// New authored curves are visible by default. This is intentionally independent from
				// solo, which remains a temporary override of the base visibility set.
				BaseVisibleCurves.Add(CurveName);
			}
		}

		for (auto It = BaseVisibleCurves.CreateIterator(); It; ++It)
		{
			if (!ReconciledNames.Contains(*It))
			{
				It.RemoveCurrent();
			}
		}
		KnownCurves = MoveTemp(ReconciledNames);

		if (SoloCurve.IsSet() && !KnownCurves.Contains(SoloCurve.GetValue()))
		{
			SoloCurve.Reset();
		}
		if (!SelectedCurve.IsNone() && !KnownCurves.Contains(SelectedCurve))
		{
			SelectedCurve = NAME_None;
		}
	}

	void FCurvePresentationState::ToggleVisibility(FName CurveName)
	{
		if (!KnownCurves.Contains(CurveName))
		{
			return;
		}
		if (BaseVisibleCurves.Contains(CurveName))
		{
			BaseVisibleCurves.Remove(CurveName);
		}
		else
		{
			BaseVisibleCurves.Add(CurveName);
		}
	}

	void FCurvePresentationState::ToggleSolo(FName CurveName)
	{
		if (!KnownCurves.Contains(CurveName))
		{
			return;
		}
		if (SoloCurve.IsSet() && SoloCurve.GetValue() == CurveName)
		{
			SoloCurve.Reset();
		}
		else
		{
			SoloCurve = CurveName;
		}
	}

	void FCurvePresentationState::Select(FName CurveName)
	{
		SelectedCurve = CurveName.IsNone() || KnownCurves.Contains(CurveName) ? CurveName : NAME_None;
	}

	void FCurvePresentationState::Rename(FName OldName, FName NewName)
	{
		if (OldName.IsNone() || NewName.IsNone() || OldName == NewName || !KnownCurves.Contains(OldName))
		{
			return;
		}

		const bool bWasBaseVisible = BaseVisibleCurves.Contains(OldName);
		KnownCurves.Remove(OldName);
		KnownCurves.Add(NewName);
		BaseVisibleCurves.Remove(OldName);
		if (bWasBaseVisible)
		{
			BaseVisibleCurves.Add(NewName);
		}
		if (SoloCurve.IsSet() && SoloCurve.GetValue() == OldName)
		{
			SoloCurve = NewName;
		}
		if (SelectedCurve == OldName)
		{
			SelectedCurve = NewName;
		}
	}

	void FCurvePresentationState::Add(FName CurveName)
	{
		if (!CurveName.IsNone())
		{
			KnownCurves.Add(CurveName);
			BaseVisibleCurves.Add(CurveName);
		}
	}

	void FCurvePresentationState::Remove(FName CurveName)
	{
		KnownCurves.Remove(CurveName);
		BaseVisibleCurves.Remove(CurveName);
		if (SoloCurve.IsSet() && SoloCurve.GetValue() == CurveName)
		{
			SoloCurve.Reset();
		}
		if (SelectedCurve == CurveName)
		{
			SelectedCurve = NAME_None;
		}
	}

	void FCurvePresentationState::ExitSoloForRetarget()
	{
		SoloCurve.Reset();
	}

	bool FCurvePresentationState::IsVisible(FName CurveName) const
	{
		if (!KnownCurves.Contains(CurveName))
		{
			return false;
		}
		return SoloCurve.IsSet()
			? SoloCurve.GetValue() == CurveName
			: BaseVisibleCurves.Contains(CurveName);
	}

	TArray<FName> FCurvePresentationState::GetVisibleCurves(const TArray<FName>& AuthoredCurveNames) const
	{
		TArray<FName> Visible;
		for (const FName& CurveName : AuthoredCurveNames)
		{
			if (IsVisible(CurveName))
			{
				Visible.Add(CurveName);
			}
		}
		return Visible;
	}
}

// =====================================================================================================
// FPaper2DPlusCurvePickerUtils — the shared add-curve picker (U2)
// =====================================================================================================

FPaper2DPlusFrameCurve FPaper2DPlusCurvePickerUtils::MakeSeededCurve(FName CurveName)
{
	FPaper2DPlusFrameCurve NewCurve;
	NewCurve.Mode = ResolveCurveMetadata(CurveName).DefaultMode;
	return NewCurve;
}

namespace
{
	bool CurveTrackPanel_IsLegacyGenericMetadata(const FPaper2DPlusKnownCurve& Known)
	{
		return Known.Semantic == EPaper2DPlusKnownCurveSemantic::Continuous
			&& Known.DefaultMode == EPaper2DPlusCurveInterp::Linear
			&& !Known.bUseFixedValueRange
			&& !Known.bWarnWhenOutsideValueRange
			&& !Known.bSnapOutputValues
			&& Known.Units.IsEmpty();
	}

	void CurveTrackPanel_ApplyStepWindowMetadata(FPaper2DPlusKnownCurve& Known)
	{
		Known.Semantic = EPaper2DPlusKnownCurveSemantic::StepWindow;
		Known.DefaultMode = EPaper2DPlusCurveInterp::Constant;
		Known.bUseFixedValueRange = true;
		Known.ValueMin = 0.f;
		Known.ValueMax = 1.f;
		Known.bWarnWhenOutsideValueRange = true;
		Known.bSnapOutputValues = true;
		Known.OutputSnap = 1.f;
	}

	void CurveTrackPanel_ApplyHitStopMetadata(FPaper2DPlusKnownCurve& Known)
	{
		Known.Semantic = EPaper2DPlusKnownCurveSemantic::FrameCount;
		Known.DefaultMode = EPaper2DPlusCurveInterp::Constant;
		Known.ValueMin = 0.f;
		Known.ValueMax = 60.f;
		Known.bWarnWhenOutsideValueRange = true;
		Known.bSnapOutputValues = true;
		Known.OutputSnap = 1.f;
		Known.Units = TEXT("frames");
	}

	void CurveTrackPanel_ApplyArmorMetadata(FPaper2DPlusKnownCurve& Known)
	{
		Known.Semantic = EPaper2DPlusKnownCurveSemantic::BooleanStep;
		Known.DefaultMode = EPaper2DPlusCurveInterp::Constant;
		Known.bUseFixedValueRange = true;
		Known.ValueMin = 0.f;
		Known.ValueMax = 1.f;
		Known.bWarnWhenOutsideValueRange = true;
		Known.bSnapOutputValues = true;
		Known.OutputSnap = 1.f;
	}
}

FPaper2DPlusKnownCurve FPaper2DPlusCurvePickerUtils::ResolveCurveMetadata(FName CurveName)
{
	FPaper2DPlusKnownCurve Resolved(CurveName, FString(), 0.f);
	bool bFoundKnownCurve = false;

	if (const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get())
	{
		for (const FPaper2DPlusKnownCurve& Known : Settings->KnownCurves)
		{
			if (!Known.Name.IsNone()
				&& Known.Name.ToString().Equals(CurveName.ToString(), ESearchCase::IgnoreCase))
			{
				Resolved = Known;
				Resolved.Name = CurveName;
				bFoundKnownCurve = true;
				break;
			}
		}
	}

	const bool bNeedsBuiltInUpgrade = !bFoundKnownCurve || CurveTrackPanel_IsLegacyGenericMetadata(Resolved);
	if (FFlipbookTransitionData::IsCancelCurveName(CurveName))
	{
		// Cancel_* is a reserved authoring convention, not a current Paper2DPlus runtime reader.
		CurveTrackPanel_ApplyStepWindowMetadata(Resolved);
	}
	else if (bNeedsBuiltInUpgrade && CurveName.ToString().Equals(TEXT("HitStop"), ESearchCase::IgnoreCase))
	{
		CurveTrackPanel_ApplyHitStopMetadata(Resolved);
	}
	else if (bNeedsBuiltInUpgrade && CurveName.ToString().Equals(TEXT("Armor"), ESearchCase::IgnoreCase))
	{
		CurveTrackPanel_ApplyArmorMetadata(Resolved);
	}

	return Resolved;
}

bool FPaper2DPlusCurvePickerUtils::ResolveSharedOutputSnap(const TArray<FName>& CurveNames, float& OutOutputSnap)
{
	bool bHaveSnap = false;
	float SharedSnap = 0.f;

	for (const FName& CurveName : CurveNames)
	{
		const FPaper2DPlusKnownCurve Metadata = ResolveCurveMetadata(CurveName);
		if (!Metadata.bSnapOutputValues || Metadata.OutputSnap <= 0.f)
		{
			return false;
		}
		if (!bHaveSnap)
		{
			bHaveSnap = true;
			SharedSnap = Metadata.OutputSnap;
		}
		else if (!FMath::IsNearlyEqual(SharedSnap, Metadata.OutputSnap))
		{
			return false;
		}
	}

	if (!bHaveSnap)
	{
		return false;
	}
	OutOutputSnap = SharedSnap;
	return true;
}

TSharedRef<SWidget> FPaper2DPlusCurvePickerUtils::BuildKnownCurvesMenu(
	TFunction<const FFlipbookCurveData*()> GetExistingCurves,
	TFunction<void(FName)> OnAddCurve)
{
	FMenuBuilder MenuBuilder(/*bCloseAfterSelection*/ true, nullptr);

	const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();
	const FFlipbookCurveData* Existing = GetExistingCurves ? GetExistingCurves() : nullptr;

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("KnownCurvesSection", "Known Curves"));
	bool bAnyKnown = false;
	if (Settings)
	{
		for (const FPaper2DPlusKnownCurve& Known : Settings->KnownCurves)
		{
			if (Known.Name.IsNone())
			{
				continue;
			}
			const bool bAlreadyAuthored = Existing && Existing->Curves.Contains(Known.Name);
			const FName Name = Known.Name;
			const FPaper2DPlusKnownCurve Metadata = ResolveCurveMetadata(Name);
			FText Tooltip = Known.Description.IsEmpty()
				? FText::Format(LOCTEXT("KnownCurveDefaultTip", "Default {0}"), FText::AsNumber(Known.DefaultValue))
				: FText::FromString(Known.Description);
			if (!Metadata.Units.IsEmpty())
			{
				Tooltip = FText::Format(LOCTEXT("KnownCurveUnitsTipFmt", "{0}\nUnits: {1}"),
					Tooltip, FText::FromString(Metadata.Units));
			}
			MenuBuilder.AddMenuEntry(
				FText::FromName(Name),
				Tooltip,
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([OnAddCurve, Name]() { if (OnAddCurve) { OnAddCurve(Name); } }),
					FCanExecuteAction::CreateLambda([bAlreadyAuthored]() { return !bAlreadyAuthored; })));
			bAnyKnown = true;
		}
	}
	if (!bAnyKnown)
	{
		MenuBuilder.AddWidget(
			SNew(STextBlock)
			.Text(LOCTEXT("NoKnownCurves", "No Known Curves registered.\nProject Settings > Plugins > Paper2DPlus."))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f))),
			FText::GetEmpty());
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> FPaper2DPlusCurvePickerUtils::BuildAddCurveControls(
	TFunction<const FFlipbookCurveData*()> GetExistingCurves,
	TFunction<void(FName)> OnAddCurve)
{
	// The control row owns its own free-form-name state: a shared FText captured by the lambdas, alive
	// exactly as long as the widgets that read it.
	TSharedRef<FText> NameText = MakeShared<FText>();

	auto CommitName = [OnAddCurve, NameText]()
	{
		const FString Trimmed = NameText->ToString().TrimStartAndEnd();
		if (!Trimmed.IsEmpty() && OnAddCurve)
		{
			OnAddCurve(FName(*Trimmed));
		}
		*NameText = FText::GetEmpty();
	};

	return SNew(SHorizontalBox)

		// Registry picker
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 4, 0)
		[
			SNew(SComboButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ToolTipText(LOCTEXT("AddKnownCurveTip", "Add a curve from the project's Known Curves registry"))
			.OnGetMenuContent_Lambda([GetExistingCurves, OnAddCurve]()
			{
				return BuildKnownCurvesMenu(GetExistingCurves, OnAddCurve);
			})
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AddCurveBtn", "+ Add Curve"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		]

		// Free-form name entry
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.MaxWidth(220.0f)
		.VAlign(VAlign_Center)
		.Padding(0, 0, 4, 0)
		[
			SNew(SEditableTextBox)
			.HintText(LOCTEXT("NewCurveHint", "New curve name…"))
			.Text_Lambda([NameText]() { return *NameText; })
			.OnTextChanged_Lambda([NameText](const FText& InText) { *NameText = InText; })
			.OnTextCommitted_Lambda([CommitName](const FText& InText, ETextCommit::Type CommitType)
			{
				if (CommitType == ETextCommit::OnEnter && !InText.IsEmptyOrWhitespace())
				{
					CommitName();
				}
			})
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.IsEnabled_Lambda([NameText]() { return !NameText->IsEmptyOrWhitespace(); })
			.OnClicked_Lambda([CommitName]()
			{
				CommitName();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AddBtn", "Add"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		];
}

// =====================================================================================================
// FPaper2DPlusCurveTrackOwner — the per-row adapter (U3)
// =====================================================================================================

FPaper2DPlusCurveTrackOwner::FPaper2DPlusCurveTrackOwner(
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> InAsset,
	int32 InFlipbookIndex,
	TWeakObjectPtr<UPaperFlipbook> InFlipbookSnapshot,
	int32 InFallbackFrameCount,
	TArray<FName> InCurveNames,
	FCallbacks InCallbacks)
	: Asset(InAsset)
	, FlipbookIndex(InFlipbookIndex)
	, FlipbookSnapshot(InFlipbookSnapshot)
	, FallbackFrameCount(FMath::Max(0, InFallbackFrameCount))
	, CurveNames(MoveTemp(InCurveNames))
	, Callbacks(MoveTemp(InCallbacks))
{
}

FPaper2DPlusFrameCurve* FPaper2DPlusCurveTrackOwner::ResolveFrameCurve(FName InCurveName) const
{
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Asset.Get();
	if (!AssetPtr)
	{
		return nullptr;
	}
	// FlipbookIndex is the row-build SNAPSHOT (F2/CT-3) — a mid-gesture selection change can never
	// retarget this resolve onto the wrong move's same-named curve (rows rebuild per move; the
	// structural fingerprint includes the index).
	if (!AssetPtr->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return nullptr;
	}
	return AssetPtr->Flipbooks[FlipbookIndex].CurveData.Curves.Find(InCurveName);
}

int32 FPaper2DPlusCurveTrackOwner::GetSnapshotFrameCount() const
{
	// Pointer cached (snapshot, never LoadSynchronous — perf residual CT-R3); COUNT read live so
	// frame-count edits reflect.
	const UPaperFlipbook* FB = FlipbookSnapshot.Get();
	if (FB)
	{
		return FB->GetNumKeyFrames();
	}
	const UPaper2DPlusCharacterProfileAsset* AssetPtr = Asset.Get();
	return AssetPtr && AssetPtr->Flipbooks.IsValidIndex(FlipbookIndex)
		? AssetPtr->Flipbooks[FlipbookIndex].CombatData.Frames.Num()
		: FallbackFrameCount;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
TArray<FRichCurveEditInfoConst> FPaper2DPlusCurveTrackOwner::GetCurves() const
{
	TArray<FRichCurveEditInfoConst> Curves;
	for (const FName& CurveName : CurveNames)
	{
		if (const FPaper2DPlusFrameCurve* FrameCurve = ResolveFrameCurve(CurveName))
		{
			Curves.Add(FRichCurveEditInfoConst(&FrameCurve->Curve, CurveName));
		}
	}
	return Curves;
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
void FPaper2DPlusCurveTrackOwner::GetCurves(TAdderReserverRef<FRichCurveEditInfoConst> Curves) const
{
	// 5.7's replacement signature — built directly (never via the deprecated overload).
	for (const FName& CurveName : CurveNames)
	{
		if (const FPaper2DPlusFrameCurve* FrameCurve = ResolveFrameCurve(CurveName))
		{
			Curves.Add(FRichCurveEditInfoConst(&FrameCurve->Curve, CurveName));
		}
	}
}
#endif

TArray<FRichCurveEditInfo> FPaper2DPlusCurveTrackOwner::GetCurves()
{
	TArray<FRichCurveEditInfo> Curves;
	for (const FName& CurveName : CurveNames)
	{
		if (FPaper2DPlusFrameCurve* FrameCurve = ResolveFrameCurve(CurveName))
		{
			Curves.Add(FRichCurveEditInfo(&FrameCurve->Curve, CurveName));
		}
	}
	return Curves;
}

void FPaper2DPlusCurveTrackOwner::ModifyOwner()
{
	// Called by SCurveEditor INSIDE its own engine transaction (the documented exception). Routed
	// through the stack so dirty/undo enrollment stays at one site behind the write gate.
	if (Callbacks.ModifyOwner)
	{
		Callbacks.ModifyOwner();
	}
}

TArray<const UObject*> FPaper2DPlusCurveTrackOwner::GetOwners() const
{
	TArray<const UObject*> Owners;
	if (const UPaper2DPlusCharacterProfileAsset* AssetPtr = Asset.Get())
	{
		Owners.Add(AssetPtr);
	}
	return Owners;
}

void FPaper2DPlusCurveTrackOwner::MakeTransactional()
{
	if (UPaper2DPlusCharacterProfileAsset* AssetPtr = Asset.Get())
	{
		AssetPtr->SetFlags(RF_Transactional);
	}
}

void FPaper2DPlusCurveTrackOwner::OnCurveChanged(const TArray<FRichCurveEditInfo>& ChangedCurveEditInfos)
{
	// The engine widget just poked the FRichCurve directly. The adapter does NOT run the coerce
	// funnel itself (F1/CT-1): SCurveEditor::ProcessDrag -> MoveSelectedKeys -> OnCurveChanged fires
	// on EVERY mouse-move during a drag, and the funnel deletes keys (dedupe) — a mid-drag pass would
	// destroy bystander keys as the dragged key passes over their frames. The STACK owns the
	// inline-vs-pend decision (HandleCurveMutated): click gestures coerce inline (riding the engine's
	// still-open transaction); live drags pend until the gesture-end poll.
	if (!Callbacks.OnCurveMutated)
	{
		return;
	}

	TSet<FName> ChangedNames;
	for (const FRichCurveEditInfo& Info : ChangedCurveEditInfos)
	{
		if (OwnsCurve(Info.CurveName))
		{
			ChangedNames.Add(Info.CurveName);
		}
	}
	if (ChangedNames.Num() == 0)
	{
		// Defensive fallback: older or unusual engine paths should still get coerced.
		for (const FName& CurveName : CurveNames)
		{
			ChangedNames.Add(CurveName);
		}
	}
	for (const FName& CurveName : ChangedNames)
	{
		Callbacks.OnCurveMutated(CurveName);
	}
}

bool FPaper2DPlusCurveTrackOwner::IsValidCurve(FRichCurveEditInfo CurveInfo)
{
	// Pointer-compare against the LIVE resolve — a stale FRichCurve* (map reallocated, curve removed,
	// flipbook switched) is invalid by definition, which is exactly what the engine's
	// ValidateSelection needs to drop dead key handles.
	const FPaper2DPlusFrameCurve* FrameCurve = ResolveFrameCurve(CurveInfo.CurveName);
	return FrameCurve && CurveInfo.CurveToEdit == &FrameCurve->Curve && OwnsCurve(CurveInfo.CurveName);
}

FLinearColor FPaper2DPlusCurveTrackOwner::GetCurveColor(FRichCurveEditInfo CurveInfo) const
{
	// The deterministic shared hash — the engine widget tints the curve line the same color as the
	// legend swatch and the legacy graph.
	return Paper2DPlusCurveTracks::NameToColor(CurveInfo.CurveName);
}

// =====================================================================================================
// SCurveTrackStack
// =====================================================================================================

namespace
{
	/** EVERY live stack, not last-constructed-wins (F10/ADV-7): the Character Layer editor hosts a
	 *  second SFrameEventEditor — and therefore a second stack — over the SAME asset, and closing one
	 *  of two editors must not orphan the probe seam for the survivor. Pushed in Construct; invalid
	 *  entries compacted opportunistically (Construct, destructor, probe). */
	TArray<TWeakPtr<SCurveTrackStack>> GCurveTrackStackRegistry;

	void CurveTrackStack_CompactRegistry()
	{
		GCurveTrackStackRegistry.RemoveAll(
			[](const TWeakPtr<SCurveTrackStack>& Entry) { return !Entry.IsValid(); });
	}

	FAutoConsoleCommand GCurveTracksProbeCommand(
		TEXT("Paper2DPlus.CurveTracksProbe"),
		TEXT("Log every open Frame Cues curve-track stack: one block per live stack (prefixed by asset name) — row count + gesture-live state, then one line per curve (name, mode, key (frame,value) pairs, orphan frames)"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			CurveTrackStack_CompactRegistry();
			if (GCurveTrackStackRegistry.Num() == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("Paper2DPlus.CurveTracksProbe: no open curve track stack"));
				return;
			}
			for (const TWeakPtr<SCurveTrackStack>& Entry : GCurveTrackStackRegistry)
			{
				if (TSharedPtr<SCurveTrackStack> Stack = Entry.Pin())
				{
					Stack->LogProbe();
				}
			}
		}));

	const TCHAR* CurveTrackStack_ModeToString(EPaper2DPlusCurveInterp Mode)
	{
		switch (Mode)
		{
		case EPaper2DPlusCurveInterp::Constant:	return TEXT("Constant");
		case EPaper2DPlusCurveInterp::Cubic:	return TEXT("Cubic");
		case EPaper2DPlusCurveInterp::Linear:
		default:								return TEXT("Linear");
		}
	}

	constexpr float CurveTrackStack_ColumnWidth =
		Paper2DPlusFrameCueTimeline::FTimingGeometry::PixelsPerKeyFrame;
	constexpr float CurveTrackStack_SharedBodyHeight = 140.0f;

	/** Focusable shell for one identity row. A click selects/focuses the curve and Up/Down navigates. */
	class SCurveTrackLegendFocusTarget : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SCurveTrackLegendFocusTarget) {}
			SLATE_ARGUMENT(TFunction<void()>, OnSelect)
			SLATE_ARGUMENT(TFunction<void(int32)>, OnNavigate)
			SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			OnSelect = InArgs._OnSelect;
			OnNavigate = InArgs._OnNavigate;
			ChildSlot
			[
				InArgs._Content.Widget
			];
		}

		virtual bool SupportsKeyboardFocus() const override
		{
			return true;
		}

		virtual FReply OnFocusReceived(
			const FGeometry& /*MyGeometry*/, const FFocusEvent& /*InFocusEvent*/) override
		{
			if (OnSelect)
			{
				OnSelect();
			}
			return FReply::Handled();
		}

		virtual FReply OnMouseButtonDown(
			const FGeometry& /*MyGeometry*/, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return FReply::Unhandled();
			}
			if (OnSelect)
			{
				OnSelect();
			}
			return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
		}

		virtual FReply OnKeyDown(
			const FGeometry& /*MyGeometry*/, const FKeyEvent& InKeyEvent) override
		{
			// Never reinterpret text-editor keystrokes as row navigation/rename commands.
			if (FSlateApplication::IsInitialized())
			{
				const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
				if (Focused.IsValid() && Focused->GetType() == TEXT("SEditableText"))
				{
					return FReply::Unhandled();
				}
			}

			if (OnSelect)
			{
				OnSelect();
			}
			if (InKeyEvent.GetKey() == EKeys::Up || InKeyEvent.GetKey() == EKeys::Down)
			{
				if (OnNavigate)
				{
					OnNavigate(InKeyEvent.GetKey() == EKeys::Up ? -1 : 1);
				}
				return FReply::Handled();
			}
			return FReply::Unhandled();
		}

	private:
		TFunction<void()> OnSelect;
		TFunction<void(int32)> OnNavigate;
	};

	class SCurveTrackStackPlayhead : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SCurveTrackStackPlayhead) {}
			SLATE_ATTRIBUTE(int32, SelectedFrameIndex)
			SLATE_ATTRIBUTE(int32, FrameCount)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			SelectedFrameIndex = InArgs._SelectedFrameIndex;
			FrameCount = InArgs._FrameCount;
			SetVisibility(EVisibility::HitTestInvisible);
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D::ZeroVector;
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
			const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
			int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const int32 Frames = FrameCount.Get(0);
			if (Frames <= 0)
			{
				return LayerId;
			}
			const int32 SelFrame = FMath::Clamp(SelectedFrameIndex.Get(0), 0, Frames - 1);
			const float PlayheadX = SelFrame * CurveTrackStack_ColumnWidth + CurveTrackStack_ColumnWidth * 0.5f;
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				MakePaintGeometry(AllottedGeometry,
					FVector2D(2.0f, AllottedGeometry.GetLocalSize().Y),
					FSlateLayoutTransform(FVector2D(PlayheadX - 1.0f, 0.0f))),
				FAppStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor(0.9f, 0.9f, 0.9f, 0.6f));
			return LayerId + 1;
		}

	private:
		TAttribute<int32> SelectedFrameIndex;
		TAttribute<int32> FrameCount;
	};
}

void SCurveTrackStack::Construct(const FArguments& InArgs)
{
	AssetAttr = InArgs._Asset;
	SelectedFlipbookIndex = InArgs._SelectedFlipbookIndex;
	SelectedFrameIndex = InArgs._SelectedFrameIndex;
	CanEditCurvesAttr = InArgs._CanEditCurves;
	OnCurveListChanged = InArgs._OnCurveListChanged;
	OnGestureSettled = InArgs._OnGestureSettled;
	OnCurveSelectionChanged = InArgs._OnCurveSelectionChanged;

	if (GEditor)
	{
		// Registered BEFORE any row builds, so this stack's PostUndo (which re-attaches fresh curve
		// pointers) runs before the rows' own SCurveEditor undo clients validate their selections.
		GEditor->RegisterForUndo(this);
	}

	// F6 (BLOCKER ADV-1): out-of-band writers (a second editor hosting a stack over the SAME asset
	// via the layer-editor reuse seam; Content-Browser JSON import replacing the whole Flipbooks
	// array; ECABridge set_asset_property) mutate/destroy curve storage with only a NEXT-TICK
	// deferred model broadcast — the engine widgets' cached FRichCurve* view models would paint
	// freed memory THIS tick. Modify() precedes the mutation in all those trigger paths, so a
	// synchronous reattach here lands before paint.
	ObjectModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddSP(
		this, &SCurveTrackStack::HandleObjectModified);

	ChildSlot
	[
		SAssignNew(StackBox, SVerticalBox)

		// The unified Frame Cue timeline owns the permanent Add Curve action. Keep an explicit body
		// empty state so the curve region remains visible before the first curve is authored.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 2, 0, 2)
		[
			SNew(SBox)
			.WidthOverride_Lambda([this]() { return FOptionalSize(GetCurveBodyWidth()); })
			[
				SNew(SBorder)
				.Visibility_Lambda([this]()
				{
					return RowCurveNames.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
				})
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.Padding(FMargin(8.0f, 12.0f))
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return CanEditCurvesAttr.Get(true)
							? LOCTEXT("NoCurvesTimelineState", "No curves yet. Use + Add Curve above to create one.")
							: LOCTEXT(
								"NoInheritedCurvesTimelineState",
								"Curves belong to the Character Profile, and this animation does not author any.");
					})
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
		]

		// One taller shared SCurveEditor for every authored curve. No left padding: the body must stay
		// flush with the 52px frame-strip columns in the shared horizontal scroller.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.Padding(0, 0, 0, 2)
		[
			SNew(SBox)
			.Visibility_Lambda([this]()
			{
				return RowCurveNames.Num() > 0 && GetFrameCount() > 0
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			.WidthOverride_Lambda([this]() { return FOptionalSize(GetCurveBodyWidth()); })
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.Padding(0)
				.Clipping(EWidgetClipping::ClipToBounds)
				[
					SNew(SOverlay)

					+ SOverlay::Slot()
					[
						SAssignNew(SharedCurveEditorWidget, SCurveEditor)
						.ViewMinInput_Lambda([]() { return -0.5f; })
						.ViewMaxInput_Lambda([this]() { return static_cast<float>(GetFrameCount()) - 0.5f; })
						.ViewMinOutput_Lambda([this]() { return SharedViewMinOutput; })
						.ViewMaxOutput_Lambda([this]() { return SharedViewMaxOutput; })
						.TimelineLength_Lambda([this]() { return static_cast<float>(GetFrameCount()); })
						.OnSetInputViewRange(FOnSetInputViewRange::CreateLambda([](float, float) { /* X locked to the frame strip */ }))
						.OnSetOutputViewRange(FOnSetOutputViewRange::CreateSP(this, &SCurveTrackStack::HandleSetSharedOutputViewRange))
						.DesiredSize_Lambda([this]()
						{
							return FVector2D(GetCurveBodyWidth(), CurveTrackStack_SharedBodyHeight);
						})
						.DrawCurve(true)
						.HideUI(true)
						.ShowZoomButtons(false)
						.ShowInputGridNumbers(true)
						.ShowOutputGridNumbers(true)
						.ShowCurveSelector(false)
						.InputSnap(1.0f)
						.InputSnappingEnabled(true)
						.OutputSnap_Lambda([this]() { return SharedOutputSnap; })
						.OutputSnappingEnabled_Lambda([this]() { return bSharedOutputSnappingEnabled; })
						.ShowTimeInFrames(true)
						.ZoomToFitVertical(false)
						.ZoomToFitHorizontal(false)
					]

					+ SOverlay::Slot()
					[
						SAssignNew(SharedPlayheadWidget, SCurveTrackStackPlayhead)
						.SelectedFrameIndex(SelectedFrameIndex)
						.FrameCount_Lambda([this]() { return GetFrameCount(); })
					]
				]
			]
		]

		// A zero-frame animation has no valid key domain. Keep the timeline structure and the shared
		// 312px non-timing body, but do not mount an editable graph or synthesize a fake one-frame band.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.Padding(0, 0, 0, 2)
		[
			SNew(SBox)
			.Visibility_Lambda([this]()
			{
				return RowCurveNames.Num() > 0 && GetFrameCount() <= 0
					? EVisibility::HitTestInvisible
					: EVisibility::Collapsed;
			})
			.WidthOverride_Lambda([this]() { return FOptionalSize(GetCurveBodyWidth()); })
			.HeightOverride(CurveTrackStack_SharedBodyHeight)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.Padding(FMargin(12.0f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return HasResolvedAnimationForTiming()
							? LOCTEXT("ZeroFrameCurveState", "This animation has no timed frames. Curves are preserved, but the graph is unavailable.")
							: LOCTEXT("NoAnimationCurveState", "Select a resolved animation to view curve timing.");
					})
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Justification(ETextJustify::Center)
				]
			]
		]

		// One compact identity row per authored curve, sorted name order.
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(RowsBox, SVerticalBox)
		]
	];

	if (SharedCurveEditorWidget.IsValid())
	{
		// Wheel-zoom only when focused — the stack lives inside the Frame Cues tab's horizontal
		// scroller; an unfocused wheel should scroll the tab, not rescale the shared Y axis.
		SharedCurveEditorWidget->SetRequireFocusToZoom(true);
	}

	CurveTrackStack_CompactRegistry();
	GCurveTrackStackRegistry.Add(SharedThis(this));

	RefreshTracks(/*bForceRebuild*/ true);
}

SCurveTrackStack::~SCurveTrackStack()
{
	if (TSharedPtr<FActiveTimerHandle> Timer = RowsRebuildTimerHandle.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	RowsRebuildTimerHandle.Reset();
	if (TSharedPtr<FActiveTimerHandle> Timer = GestureEndPollHandle.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	GestureEndPollHandle.Reset();
	bRowsRebuildPending = false;
	bGestureEndPollActive = false;
	FCoreUObjectDelegates::OnObjectModified.Remove(ObjectModifiedHandle);
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
	// Never touch host-bound state from teardown-in-destruction — the host panel may already be
	// mid-destruction (the ChildSlot ref can be the last one released): unbind the host delegates AND
	// DROP (not flush) any pending coercions, since FlushPendingCoerce's transaction open resolves the
	// host-bound Asset attribute. Worst case a sub-0.1s-old drag leaves fractional key times behind;
	// the funnel re-coerces them on the next engine edit of that curve.
	OnCurveListChanged = FSimpleDelegate();
	OnGestureSettled = FSimpleDelegate();
	OnCurveSelectionChanged = FOnCurveTrackSelectionChanged();
	PendingCoerceCurves.Empty();
	// Risk-5 teardown ordering: the shared editor (and any live capture) detaches before its adapter dies.
	TeardownRows();
	CurveTrackStack_CompactRegistry();
}

TSharedRef<SWidget> SCurveTrackStack::BuildAddCurveMenu()
{
	TSharedRef<SWidget> Controls = FPaper2DPlusCurvePickerUtils::BuildAddCurveControls(
		[this]() -> const FFlipbookCurveData*
		{
			const FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
			return Data ? &Data->CurveData : nullptr;
		},
		[this](const FName CurveName)
		{
			AddCurve(CurveName);
		});

	return SNew(SBox)
		.IsEnabled_Lambda([this]() { return CanEditCurvesAttr.Get(true); })
		.ToolTipText_Lambda([this]()
		{
			return CanEditCurvesAttr.Get(true)
				? FText::GetEmpty()
				: LOCTEXT("ReadOnlyAddCurveTip", "Curves are inherited from the Character Profile and are read-only here.");
		})
		[
			Controls
		];
}

TSharedRef<SWidget> SCurveTrackStack::DetachLegendContentForExternalLayout()
{
	if (!bLegendDetached && StackBox.IsValid() && RowsBox.IsValid())
	{
		StackBox->RemoveSlot(RowsBox.ToSharedRef());
		bLegendDetached = true;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
	return RowsBox.IsValid() ? StaticCastSharedRef<SWidget>(RowsBox.ToSharedRef()) : SNullWidget::NullWidget;
}

void SCurveTrackStack::ToggleCurveVisibility(FName CurveName)
{
	Paper2DPlusCurveTracks::FCurvePresentationState* State =
		ReconcileCurrentPresentation(GetSortedCurveNames());
	if (!State || !State->IsKnown(CurveName))
	{
		return;
	}
	State->ToggleVisibility(CurveName);
	RequestRowsRebuild();
}

void SCurveTrackStack::ToggleCurveSolo(FName CurveName)
{
	Paper2DPlusCurveTracks::FCurvePresentationState* State =
		ReconcileCurrentPresentation(GetSortedCurveNames());
	if (!State || !State->IsKnown(CurveName))
	{
		return;
	}
	State->ToggleSolo(CurveName);
	RequestRowsRebuild();
}

void SCurveTrackStack::SelectCurve(FName CurveName)
{
	if (SetCurveSelectionState(CurveName))
	{
		OnCurveSelectionChanged.ExecuteIfBound(CurveName);
	}
}

void SCurveTrackStack::SynchronizeSelection(FName CurveName)
{
	SetCurveSelectionState(CurveName);
}

bool SCurveTrackStack::SetCurveSelectionState(FName CurveName)
{
	Paper2DPlusCurveTracks::FCurvePresentationState* State =
		ReconcileCurrentPresentation(GetSortedCurveNames());
	if (!State || (!CurveName.IsNone() && !State->IsKnown(CurveName)))
	{
		return false;
	}
	if (State->GetSelectedCurve() == CurveName)
	{
		return false;
	}
	State->Select(CurveName);
	for (const TSharedPtr<SCurveTrackRow>& Row : Rows)
	{
		if (Row.IsValid())
		{
			Row->Invalidate(EInvalidateWidgetReason::Paint);
		}
	}
	if (RowsBox.IsValid())
	{
		RowsBox->Invalidate(EInvalidateWidgetReason::Paint);
	}
	return true;
}

FName SCurveTrackStack::GetSelectedCurve() const
{
	const Paper2DPlusCurveTracks::FCurvePresentationState* State = FindCurrentPresentation();
	return State ? State->GetSelectedCurve() : NAME_None;
}

bool SCurveTrackStack::IsCurveVisible(FName CurveName) const
{
	const Paper2DPlusCurveTracks::FCurvePresentationState* State = FindCurrentPresentation();
	return State ? State->IsVisible(CurveName) : GetSortedCurveNames().Contains(CurveName);
}

bool SCurveTrackStack::IsCurveBaseVisible(FName CurveName) const
{
	const Paper2DPlusCurveTracks::FCurvePresentationState* State = FindCurrentPresentation();
	return State ? State->IsBaseVisible(CurveName) : GetSortedCurveNames().Contains(CurveName);
}

bool SCurveTrackStack::IsCurveSoloed(FName CurveName) const
{
	const Paper2DPlusCurveTracks::FCurvePresentationState* State = FindCurrentPresentation();
	return State && State->IsSoloed(CurveName);
}

EPaper2DPlusCurveInterp SCurveTrackStack::GetCurveMode(FName CurveName) const
{
	const FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	const FPaper2DPlusFrameCurve* Curve =
		Data ? Data->CurveData.Curves.Find(CurveName) : nullptr;
	return Curve ? Curve->Mode : EPaper2DPlusCurveInterp::Linear;
}

TArray<int32> SCurveTrackStack::GetCurveOrphanFrames(FName CurveName) const
{
	const FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	const FPaper2DPlusFrameCurve* Curve =
		Data ? Data->CurveData.Curves.Find(CurveName) : nullptr;
	return Curve
		? Paper2DPlusCurveTracks::FindOrphanKeyFrames(Curve->Curve, GetFrameCount())
		: TArray<int32>();
}

bool SCurveTrackStack::HasCurveOrphans(FName CurveName) const
{
	const FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	const FPaper2DPlusFrameCurve* Curve =
		Data ? Data->CurveData.Curves.Find(CurveName) : nullptr;
	const int32 FrameCount = GetFrameCount();
	if (!Curve || FrameCount <= 0)
	{
		return false;
	}
	for (const FRichCurveKey& Key : Curve->Curve.GetConstRefOfKeys())
	{
		if (Paper2DPlusCurveTracks::IsOrphanKeyTime(Key.Time, FrameCount))
		{
			return true;
		}
	}
	return false;
}

bool SCurveTrackStack::HasResolvedCurveTiming() const
{
	return HasResolvedAnimationForTiming() && GetFrameCount() > 0;
}

bool SCurveTrackStack::CanMutateCurve(FName CurveName) const
{
	if (!CanEditCurvesAttr.Get(true))
	{
		return false;
	}
	const FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	return ResolveAsset() && Data && Data->CurveData.Curves.Contains(CurveName);
}

bool SCurveTrackStack::HasCurveRowKeyboardTargetForTests(FName CurveName) const
{
	const TWeakPtr<SWidget>* Target = CurveRowFocusTargets.Find(CurveName);
	return Target && Target->IsValid();
}

bool SCurveTrackStack::MoveCurveSelection(int32 Direction, bool bSetKeyboardFocus)
{
	if (Direction == 0 || RowCurveNames.IsEmpty())
	{
		return false;
	}

	const int32 Step = Direction < 0 ? -1 : 1;
	const int32 CurrentIndex = RowCurveNames.IndexOfByKey(GetSelectedCurve());
	const int32 TargetIndex = CurrentIndex == INDEX_NONE
		? (Step < 0 ? RowCurveNames.Num() - 1 : 0)
		: FMath::Clamp(CurrentIndex + Step, 0, RowCurveNames.Num() - 1);
	const FName TargetCurve = RowCurveNames[TargetIndex];
	const bool bSelectionChanged = TargetCurve != GetSelectedCurve();
	SelectCurve(TargetCurve);

	if (bSetKeyboardFocus && FSlateApplication::IsInitialized())
	{
		if (const TWeakPtr<SWidget>* Target = CurveRowFocusTargets.Find(TargetCurve))
		{
			if (const TSharedPtr<SWidget> Widget = Target->Pin())
			{
				FSlateApplication::Get().SetKeyboardFocus(Widget, EFocusCause::Navigation);
			}
		}
	}
	return bSelectionChanged;
}

bool SCurveTrackStack::ValidateCurveRename(
	FName OldName, const FString& RequestedName, FName& OutNewName, FText& OutError) const
{
	OutNewName = NAME_None;
	OutError = FText::GetEmpty();
	if (!CanEditCurvesAttr.Get(true))
	{
		OutError = LOCTEXT("RenameCurveReadOnly", "Curves are read-only in this workspace.");
		return false;
	}

	const FString TrimmedName = RequestedName.TrimStartAndEnd();
	if (TrimmedName.IsEmpty())
	{
		OutError = LOCTEXT("RenameCurveEmpty", "Curve names cannot be empty.");
		return false;
	}

	const FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!ResolveAsset() || !Data || !Data->CurveData.Curves.Contains(OldName))
	{
		OutError = LOCTEXT("RenameCurveMissing", "The curve no longer exists in the selected animation.");
		return false;
	}

	const FString OldString = OldName.ToString();
	if (TrimmedName.Equals(OldString, ESearchCase::CaseSensitive))
	{
		OutError = LOCTEXT("RenameCurveUnchanged", "Enter a different curve name.");
		return false;
	}
	for (const TPair<FName, FPaper2DPlusFrameCurve>& Pair : Data->CurveData.Curves)
	{
		if (Pair.Key.ToString().Equals(TrimmedName, ESearchCase::IgnoreCase))
		{
			OutError = Pair.Key == OldName
				? LOCTEXT("RenameCurveCaseOnly", "Case-only curve renames are not supported.")
				: FText::Format(
					LOCTEXT("RenameCurveCollisionFmt", "A curve named '{0}' already exists."),
					FText::FromName(Pair.Key));
			return false;
		}
	}

	OutNewName = FName(*TrimmedName);
	return true;
}

void SCurveTrackStack::HandleHostDeactivated()
{
	// Stop deferred work before settling capture. A hidden contextual panel must not wake later and
	// rebuild/commit against a newly selected animation.
	if (TSharedPtr<FActiveTimerHandle> Timer = RowsRebuildTimerHandle.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	RowsRebuildTimerHandle.Reset();
	bRowsRebuildPending = false;
	if (TSharedPtr<FActiveTimerHandle> Timer = GestureEndPollHandle.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	GestureEndPollHandle.Reset();
	bGestureEndPollActive = false;

	if (SharedCurveEditorHasMouseCapture() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().ReleaseAllPointerCapture();
	}
	// Capture loss closes the engine gesture. Resolve pending key coercion while the snapshot adapter
	// still points at its original asset/move, then close any stack-owned structural transaction.
	FlushPendingCoerce();
	EndTransaction();
}

// -----------------------------------------------------------------------------------------------------
// Undo / redo
// -----------------------------------------------------------------------------------------------------

void SCurveTrackStack::PostUndo(bool bSuccess)
{
	if (!bSuccess)
	{
		return;
	}

	// Undo restores the asset's Curves TMap by EMPTY+REFILL — every FPaper2DPlusFrameCurve value
	// REALLOCATES, so every live SCurveEditor's cached FRichCurve* dangles (the engine documents this
	// exact hazard class at SCurveEditor.cpp:3881-3885). Re-attach the shared editor in place FIRST:
	// SetCurveOwner with the SAME adapter re-resolves fresh pointers without clearing selection or
	// rebuilding widgets (removed curves resolve to zero curves — a safe empty paint).
	ReattachSharedCurveOwner();
	InvalidateSharedCurveEditor();

	// Structural diff: rebuild rows only if the curve SET changed. The rebuild DEFERS one active-timer
	// beat so widget teardown never mutates the undo-client list inside this PostUndo broadcast.
	RefreshTracks();
}

void SCurveTrackStack::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

// -----------------------------------------------------------------------------------------------------
// Out-of-band writers (F6/ADV-1)
// -----------------------------------------------------------------------------------------------------

void SCurveTrackStack::HandleObjectModified(UObject* Object)
{
	// Only the resolved asset (or anything outered to it) concerns this stack.
	UPaper2DPlusCharacterProfileAsset* AssetPtr = ResolveAsset();
	if (!Object || !AssetPtr || (Object != AssetPtr && !Object->IsIn(AssetPtr)))
	{
		return;
	}
	// Self-originated writes (Rule A flag up, our own panel transaction, or a live engine gesture on
	// one of our rows) already manage row/view-model lifetimes precisely — only OUT-OF-BAND writers
	// need the emergency reattach.
	if (bCurveWriteInProgress || ActiveTransaction.IsValid() || IsCurveGestureLive())
	{
		return;
	}
	// Cheap guard: a multi-property out-of-band mutation can Modify several times in one tick —
	// reattach once per frame is enough (the engine itself dedupes OnObjectModified per object per
	// frame, but sub-objects outered to the asset broadcast separately).
	if (LastExternalReattachFrame == GFrameCounter)
	{
		return;
	}
	LastExternalReattachFrame = GFrameCounter;

	// Immediate reattach: SetCurveOwner with the SAME adapter re-resolves fresh FRichCurve pointers
	// (a removed curve resolves empty and paints nothing) — this lands BEFORE this tick's paint, the
	// whole point. The structural legend diff rides the existing pre-paint active-timer via RefreshTracks.
	ReattachSharedCurveOwner();
	InvalidateSharedCurveEditor();
	RefreshTracks();
}

// -----------------------------------------------------------------------------------------------------
// Transactions (the panel template — mirrors the tool-tab panels)
// -----------------------------------------------------------------------------------------------------

void SCurveTrackStack::BeginTransaction(
	const FText& Description,
	UPaper2DPlusCharacterProfileAsset* TransactionAsset)
{
	UPaper2DPlusCharacterProfileAsset* AssetPtr = TransactionAsset ? TransactionAsset : ResolveAsset();
	if (!AssetPtr)
	{
		return;
	}
	EndTransaction();
	ActiveTransaction = MakeUnique<FScopedTransaction>(Description);
	LastTransactionAsset = AssetPtr;
	AssetPtr->Modify();
}

void SCurveTrackStack::EndTransaction()
{
	ActiveTransaction.Reset();
}

// -----------------------------------------------------------------------------------------------------
// Gesture liveness (research risk 3 — the gate for SCurveEditor's self-owned transactions)
// -----------------------------------------------------------------------------------------------------

bool SCurveTrackStack::IsCurveGestureLive() const
{
	// Rule A self-echo: a stack-side mutate+notify window is in flight right now.
	if (bCurveWriteInProgress)
	{
		return true;
	}
	// Engine-owned gesture: SCurveEditor opens its own transaction on mouse-down and holds capture for
	// the drag — both must be true for "a curve gesture is live" (capture without a transaction is
	// some other widget's drag; a transaction without our capture is some other panel's edit).
	if (GEditor && GEditor->IsTransactionActive())
	{
		return SharedCurveEditorHasMouseCapture();
	}
	return false;
}

// -----------------------------------------------------------------------------------------------------
// Refresh / rebuild
// -----------------------------------------------------------------------------------------------------

void SCurveTrackStack::RefreshTracks(bool bForceRebuild)
{
	// Full structural fingerprint: names AND asset AND flipbook index (see the header note — two
	// moves routinely author the same curve names, so a names-only diff would keep rows pointed at
	// the previous move's curves).
	const TArray<FName> CurrentNames = GetSortedCurveNames();
	const bool bSameTarget =
		RowAsset == TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>(ResolveAsset()) &&
		RowFlipbookIndex == SelectedFlipbookIndex.Get(INDEX_NONE) &&
		bRowCanEditCurves == CanEditCurvesAttr.Get(true);
	if (!bForceRebuild && bSameTarget && CurrentNames == RowCurveNames)
	{
		// Value-only change: widgets paint live off the asset — repaint, no rebuild.
		ReattachSharedCurveOwner();
		InvalidateSharedCurveEditor();
		for (const TSharedPtr<SCurveTrackRow>& Row : Rows)
		{
			if (Row.IsValid())
			{
				Row->Invalidate(EInvalidateWidgetReason::Paint);
			}
		}
		return;
	}
	RequestRowsRebuild();
}

void SCurveTrackStack::RebuildRowsImmediatelyForTests()
{
	if (TSharedPtr<FActiveTimerHandle> Existing = RowsRebuildTimerHandle.Pin())
	{
		UnRegisterActiveTimer(Existing.ToSharedRef());
	}
	RowsRebuildTimerHandle.Reset();
	bRowsRebuildPending = true;
	RebuildRowsNow();
}

void SCurveTrackStack::RequestRowsRebuild()
{
	if (bRowsRebuildPending && RowsRebuildTimerHandle.IsValid())
	{
		return;
	}
	if (TSharedPtr<FActiveTimerHandle> Existing = RowsRebuildTimerHandle.Pin())
	{
		UnRegisterActiveTimer(Existing.ToSharedRef());
	}
	RowsRebuildTimerHandle.Reset();
	bRowsRebuildPending = true;
	// Deferred one beat: callers include PostUndo / model-broadcast handlers, where destroying the
	// rows' SCurveEditors (FEditorUndoClients) inline would mutate engine delegate lists
	// mid-iteration. Active timers execute before this widget paints, so no stale frame is shown;
	// a hidden tab's pending rebuild applies on its first paint after foregrounding (for free).
	RowsRebuildTimerHandle = RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SCurveTrackStack::OnRowsRebuildTimer));
}

EActiveTimerReturnType SCurveTrackStack::OnRowsRebuildTimer(double /*InCurrentTime*/, float /*InDeltaTime*/)
{
	RowsRebuildTimerHandle.Reset();
	if (bRowsRebuildPending)
	{
		RebuildRowsNow();
	}
	return EActiveTimerReturnType::Stop;
}

void SCurveTrackStack::RebuildRowsNow()
{
	bRowsRebuildPending = false;
	TeardownRows();
	BuildRows();
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility); // row count changed = desired size changed
}

void SCurveTrackStack::TeardownRows()
{
	// Risk-5 lifetime contract, in order:
	// 1. Release any live pointer capture FIRST — a mid-gesture teardown (flipbook switch, curve
	//    remove, Base-Profile swap) must not leave a destroyed SCurveEditor as the mouse captor.
	//    Releasing capture fires the widget's OnMouseCaptureLost, which closes its own gesture
	//    (the capture-lost-closes-gestures house rule).
	if (SharedCurveEditorHasMouseCapture() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().ReleaseAllPointerCapture();
	}

	// 1b. Pend-never-drop: the capture release above closed any live gesture, so coercions pended by
	//     F1 can flush NOW, while the adapters they resolve through are still alive — otherwise a
	//     teardown (flipbook switch, curve add/remove, Base-Profile swap) would strand mid-drag
	//     fractional key times on the old move. (The destructor DROPS pending coercions before this
	//     runs — flushing there would resolve host-bound state mid-host-destruction.)
	FlushPendingCoerce();

	// 2. Detach the shared engine widget from its curve owner — SetCurveOwner(nullptr) empties the
	//    widget's cached raw FRichCurve* view models, so even a transiently-lingering widget cannot
	//    dereference freed curve storage.
	DetachSharedCurveOwner();

	// 3. Drop the legend row widgets, THEN the adapter the shared editor pointed at.
	if (RowsBox.IsValid())
	{
		RowsBox->ClearChildren();
	}
	Rows.Empty();
	CurveRowFocusTargets.Empty();
	SharedAdapter.Reset();
	RowCurveNames.Empty();
	VisibleCurveNames.Empty();
	RowAsset = nullptr;
	RowFlipbookIndex = INDEX_NONE;
	bRowCanEditCurves = true;
}

void SCurveTrackStack::BuildRows()
{
	if (!RowsBox.IsValid())
	{
		return;
	}

	RowCurveNames = GetSortedCurveNames();
	RowAsset = ResolveAsset();
	RowFlipbookIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
	bRowCanEditCurves = CanEditCurvesAttr.Get(true);
	if (Paper2DPlusCurveTracks::FCurvePresentationState* Presentation =
		ReconcileCurrentPresentation(RowCurveNames))
	{
		VisibleCurveNames = Presentation->GetVisibleCurves(RowCurveNames);
	}
	else
	{
		VisibleCurveNames = RowCurveNames;
	}

	// F2 (CT-3 + CT-R3): snapshot the asset, flipbook index, AND the resolved UPaperFlipbook* BY
	// VALUE at row-build time. Rows are rebuilt per move (the structural fingerprint includes the
	// index), so live resolves buy nothing here and cost two ways: a mid-gesture selection change
	// could coerce the WRONG move's same-named curve, and every per-paint attribute read
	// (ViewMaxInput/TimelineLength/DesiredSize/playhead) would TSoftObjectPtr::LoadSynchronous. The
	// flipbook POINTER is the snapshot; its key-frame COUNT is still read live off it each call so
	// frame-count edits reflect.
	const TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> AssetSnapshot = RowAsset;
	const int32 FlipbookIndexSnapshot = RowFlipbookIndex;
	TWeakObjectPtr<UPaperFlipbook> FlipbookSnapshot;
	int32 FallbackFrameCountSnapshot = 0;
	if (const FFlipbookProfileEntry* Data = GetSelectedFlipbookData())
	{
		FallbackFrameCountSnapshot = Data->CombatData.Frames.Num();
		if (!Data->Identity.Flipbook.IsNull())
		{
			FlipbookSnapshot = Data->Identity.Flipbook.LoadSynchronous();
		}
	}
	const auto SnapshotFrameCount = [
		FlipbookSnapshot,
		AssetSnapshot,
		FlipbookIndexSnapshot,
		FallbackFrameCountSnapshot]() -> int32
	{
		const UPaperFlipbook* FB = FlipbookSnapshot.Get();
		if (FB)
		{
			return FB->GetNumKeyFrames();
		}
		const UPaper2DPlusCharacterProfileAsset* AssetPtr = AssetSnapshot.Get();
		return AssetPtr && AssetPtr->Flipbooks.IsValidIndex(FlipbookIndexSnapshot)
			? AssetPtr->Flipbooks[FlipbookIndexSnapshot].CombatData.Frames.Num()
			: FallbackFrameCountSnapshot;
	};
	FPaper2DPlusCurveTrackOwner::FCallbacks Callbacks;
	Callbacks.ModifyOwner = [this, AssetSnapshot]()
	{
		if (CanEditCurvesAttr.Get(true))
		{
			if (UPaper2DPlusCharacterProfileAsset* AssetPtr = AssetSnapshot.Get())
			{
				AssetPtr->Modify();
			}
		}
	};
	Callbacks.OnCurveMutated = [this](FName MutatedCurve)
	{
		HandleCurveMutated(MutatedCurve);
	};

	SharedAdapter = MakeUnique<FPaper2DPlusCurveTrackOwner>(
		AssetSnapshot,
		FlipbookIndexSnapshot,
		FlipbookSnapshot,
		FallbackFrameCountSnapshot,
		VisibleCurveNames,
		MoveTemp(Callbacks));

	UpdateSharedOutputViewRange();
	if (SharedCurveEditorWidget.IsValid() && RowCurveNames.Num() > 0 && SnapshotFrameCount() > 0)
	{
		// Layer workspaces mount the exact same curves with bCanEdit=false; the engine widget still
		// renders the real FRichCurves while declining every key mutation/transaction path.
		bSharedCurveEditorAcceptsEdits = bRowCanEditCurves;
		SharedCurveEditorWidget->SetCurveOwner(SharedAdapter.Get(), bSharedCurveEditorAcceptsEdits);
	}
	else
	{
		DetachSharedCurveOwner();
	}

	for (const FName& CurveName : RowCurveNames)
	{
		TSharedRef<SCurveTrackRow> Row = SNew(SCurveTrackRow)
			.CurveName(CurveName);

		TSharedRef<SCurveTrackLegendFocusTarget> FocusTarget =
			SNew(SCurveTrackLegendFocusTarget)
			.OnSelect([this, CurveName]() { SelectCurve(CurveName); })
			.OnNavigate([this](int32 Direction) { MoveCurveSelection(Direction, true); })
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor_Lambda([this, CurveName]()
				{
					return GetSelectedCurve() == CurveName
						? FLinearColor(0.15f, 0.55f, 1.0f, 0.9f)
						: FLinearColor::Transparent;
				})
				.Padding(1.0f)
				[
					Row
				]
			];

		RowsBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 2)
		[
			FocusTarget
		];
		CurveRowFocusTargets.Add(CurveName, FocusTarget);

		Rows.Add(Row);
	}
}

void SCurveTrackStack::InvalidateScrub()
{
	InvalidateSharedPlayhead();
}

// -----------------------------------------------------------------------------------------------------
// Structural mutations (panel transaction template + Rule A self-echo window)
// -----------------------------------------------------------------------------------------------------

void SCurveTrackStack::AddCurve(FName CurveName)
{
	if (!CanEditCurvesAttr.Get(true) || CurveName.IsNone())
	{
		return;
	}
	const TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> MutationAsset = ResolveAsset();
	const int32 MutationIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data)
	{
		return;
	}
	if (Data->CurveData.Curves.Contains(CurveName))
	{
		// Idempotent re-add: the row already exists (or the next refresh picks it up) — no transaction.
		RefreshTracks();
		return;
	}

	// TMap::Add can REHASH and move every existing FPaper2DPlusFrameCurve value — any live
	// SCurveEditor still holding &Curve would dangle. Tear the rows down BEFORE the map mutation
	// (risk 5); BuildRows below re-resolves everything fresh. Safe inline here: menu-click context,
	// never inside an undo/model broadcast.
	TeardownRows();
	UPaper2DPlusCharacterProfileAsset* MutationAssetPtr = MutationAsset.Get();
	if (!MutationAssetPtr || !MutationAssetPtr->Flipbooks.IsValidIndex(MutationIndex))
	{
		BuildRows();
		return;
	}
	Paper2DPlusCurveTracks::FCurvePresentationState* Presentation =
		ReconcileCurrentPresentation(GetSortedCurveNames());
	Data = &MutationAssetPtr->Flipbooks[MutationIndex];
	{
		TGuardValue<bool> WriteGuard(bCurveWriteInProgress, true); // Rule A: spans mutate+notify
		LastSelfCurveWriteFrame = GFrameCounter; // F8
		BeginTransaction(LOCTEXT("AddCurveTrackTxn", "Add Curve"), MutationAssetPtr);
		Data->CurveData.Curves.Add(CurveName, FPaper2DPlusCurvePickerUtils::MakeSeededCurve(CurveName));
		EndTransaction();
		if (Presentation)
		{
			Presentation->Add(CurveName);
		}
		if (ResolveAsset() == MutationAssetPtr && SelectedFlipbookIndex.Get(INDEX_NONE) == MutationIndex)
		{
			OnCurveListChanged.ExecuteIfBound(); // host -> Model->NotifyAssetDataChanged() (gated self-echo)
		}
	}
	BuildRows();
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

void SCurveTrackStack::RemoveCurve(FName CurveName)
{
	if (!CanEditCurvesAttr.Get(true))
	{
		return;
	}
	const TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> MutationAsset = ResolveAsset();
	const int32 MutationIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data || !Data->CurveData.Curves.Contains(CurveName))
	{
		return;
	}

	// TMap::Remove destroys the value a live SCurveEditor may still be pointing at — rows down first
	// (risk 5), exactly like AddCurve.
	TeardownRows();
	UPaper2DPlusCharacterProfileAsset* MutationAssetPtr = MutationAsset.Get();
	if (!MutationAssetPtr || !MutationAssetPtr->Flipbooks.IsValidIndex(MutationIndex))
	{
		BuildRows();
		return;
	}
	Paper2DPlusCurveTracks::FCurvePresentationState* Presentation =
		ReconcileCurrentPresentation(GetSortedCurveNames());
	const bool bClearedSelection = Presentation && Presentation->GetSelectedCurve() == CurveName;
	Data = &MutationAssetPtr->Flipbooks[MutationIndex];
	{
		TGuardValue<bool> WriteGuard(bCurveWriteInProgress, true);
		LastSelfCurveWriteFrame = GFrameCounter; // F8
		BeginTransaction(LOCTEXT("RemoveCurveTrackTxn", "Remove Curve"), MutationAssetPtr);
		Data->CurveData.Curves.Remove(CurveName);
		EndTransaction();
		if (Presentation)
		{
			Presentation->Remove(CurveName);
		}
		if (ResolveAsset() == MutationAssetPtr && SelectedFlipbookIndex.Get(INDEX_NONE) == MutationIndex)
		{
			OnCurveListChanged.ExecuteIfBound();
		}
	}
	if (bClearedSelection)
	{
		OnCurveSelectionChanged.ExecuteIfBound(NAME_None);
	}
	BuildRows();
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

bool SCurveTrackStack::RenameCurve(FName OldName, const FString& RequestedName, FText* OutError)
{
	auto SetError = [OutError](const FText& Error)
	{
		if (OutError)
		{
			*OutError = Error;
		}
	};
	FName NewName;
	FText ValidationError;
	if (!ValidateCurveRename(OldName, RequestedName, NewName, ValidationError))
	{
		SetError(ValidationError);
		return false;
	}
	SetError(FText::GetEmpty());
	UPaper2DPlusCharacterProfileAsset* MutationAsset = ResolveAsset();
	const int32 MutationIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	Paper2DPlusCurveTracks::FCurvePresentationState* Presentation =
		ReconcileCurrentPresentation(GetSortedCurveNames());
	const bool bSelectedCurveRenamed = Presentation && Presentation->GetSelectedCurve() == OldName;
	const TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> MutationAssetSnapshot = MutationAsset;

	// Settle any live gesture and detach SCurveEditor's raw curve pointers before the TMap remove/add.
	// Reacquire the exact captured target afterward; a retarget can never redirect this transaction.
	TeardownRows();
	MutationAsset = MutationAssetSnapshot.Get();
	if (!MutationAsset || !MutationAsset->Flipbooks.IsValidIndex(MutationIndex))
	{
		BuildRows();
		SetError(LOCTEXT("RenameCurveTargetChanged", "The animation changed before the curve could be renamed."));
		return false;
	}
	Data = &MutationAsset->Flipbooks[MutationIndex];
	const FPaper2DPlusFrameCurve* ExistingCurve = Data->CurveData.Curves.Find(OldName);
	if (!ExistingCurve || Data->CurveData.Curves.Contains(NewName))
	{
		BuildRows();
		SetError(LOCTEXT("RenameCurveRevalidateFailed", "The curve list changed before the rename could be applied."));
		return false;
	}
	const FPaper2DPlusFrameCurve RenamedValue = *ExistingCurve;

	{
		TGuardValue<bool> WriteGuard(bCurveWriteInProgress, true);
		LastSelfCurveWriteFrame = GFrameCounter;
		BeginTransaction(LOCTEXT("RenameCurveTrackTxn", "Rename Curve"), MutationAsset);
		Data->CurveData.Curves.Remove(OldName);
		Data->CurveData.Curves.Add(NewName, RenamedValue);
		EndTransaction();
		if (Presentation)
		{
			Presentation->Rename(OldName, NewName);
		}
		if (ResolveAsset() == MutationAsset && SelectedFlipbookIndex.Get(INDEX_NONE) == MutationIndex)
		{
			OnCurveListChanged.ExecuteIfBound();
		}
	}

	BuildRows();
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	if (bSelectedCurveRenamed)
	{
		OnCurveSelectionChanged.ExecuteIfBound(NewName);
	}
	return true;
}

void SCurveTrackStack::SetCurveMode(FName CurveName, EPaper2DPlusCurveInterp NewMode)
{
	if (!CanEditCurvesAttr.Get(true))
	{
		return;
	}
	UPaper2DPlusCharacterProfileAsset* MutationAsset = ResolveAsset();
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	FPaper2DPlusFrameCurve* Curve = Data ? Data->CurveData.Curves.Find(CurveName) : nullptr;
	if (!Curve || Curve->Mode == NewMode)
	{
		return;
	}

	// Value-shape change only (no map-shape mutation): rows stay alive, repaint shows the new interp.
	{
		TGuardValue<bool> WriteGuard(bCurveWriteInProgress, true);
		LastSelfCurveWriteFrame = GFrameCounter; // F8
		BeginTransaction(LOCTEXT("SetCurveTrackModeTxn", "Set Curve Interpolation"), MutationAsset);
		Curve->SetMode(NewMode); // re-stamps every existing key's RichCurve interp mode
		EndTransaction();
		OnCurveListChanged.ExecuteIfBound();
	}
	for (const TSharedPtr<SCurveTrackRow>& Row : Rows)
	{
		if (Row.IsValid() && Row->GetCurveName() == CurveName)
		{
			Row->Invalidate(EInvalidateWidgetReason::Paint);
		}
	}
	InvalidateSharedCurveEditor();
}

void SCurveTrackStack::PruneOrphanKeys(FName CurveName)
{
	if (!CanEditCurvesAttr.Get(true))
	{
		return;
	}
	UPaper2DPlusCharacterProfileAsset* MutationAsset = ResolveAsset();
	FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	FPaper2DPlusFrameCurve* Curve = Data ? Data->CurveData.Curves.Find(CurveName) : nullptr;
	if (!Curve)
	{
		return;
	}
	const int32 FrameCount = GetFrameCount();
	if (FrameCount <= 0)
	{
		// F3 (CT-4/ADV-3): no flipbook resolved -> orphan state is UNKNOWN — pruning here would
		// delete EVERY key. The detector already returns empty for this case; this guard makes the
		// contract explicit at the destructive call site too.
		return;
	}
	const TArray<int32> Orphans = Paper2DPlusCurveTracks::FindOrphanKeyFrames(Curve->Curve, FrameCount);
	if (Orphans.Num() == 0)
	{
		return; // nothing to prune — never open an empty transaction
	}

	{
		TGuardValue<bool> WriteGuard(bCurveWriteInProgress, true);
		LastSelfCurveWriteFrame = GFrameCounter; // F8
		BeginTransaction(LOCTEXT("PruneOrphanKeysTxn", "Prune Orphan Curve Keys"), MutationAsset);
		for (int32 OrphanFrame : Orphans)
		{
			Curve->DeleteKeyAtFrame(OrphanFrame); // the U1 runtime funnel — no FKeyHandle scanning here
		}
		EndTransaction();
		OnCurveListChanged.ExecuteIfBound();
	}
	// Key deletion under a live widget: re-attach so its view models re-validate, then repaint.
	ReattachSharedCurveOwner();
	InvalidateRowFor(CurveName);
}

void SCurveTrackStack::HandleCurveMutated(FName CurveName)
{
	if (!CanEditCurvesAttr.Get(true))
	{
		return;
	}
	// F8 (ADV-5): every self-originated curve write stamps the frame — the host's broadcast handlers
	// treat the engine Modify's NEXT-TICK deferred broadcast as curve-only when it lands within 1-2
	// frames of this stamp (the deferred-tick analogue of the Rule A self-echo flag).
	LastSelfCurveWriteFrame = GFrameCounter;

	if (IsCurveGestureLive())
	{
		// F1 (BLOCKER CT-1): NO MUTATIONS MID-GESTURE. SCurveEditor::ProcessDrag -> MoveSelectedKeys ->
		// OnCurveChanged fires on EVERY mouse-move during a drag, and the funnel deletes keys (dedupe) —
		// with LMB snap every pass-over time is exactly integral, so a mid-drag funnel pass would kill a
		// bystander (or the dragged key itself) as it crosses occupied frames. Pend the curve and let
		// the gesture-end poll run the FULL funnel once, after the engine closes its transaction.
		PendingCoerceCurves.Add(CurveName);
		InvalidateRowFor(CurveName);
		ArmGestureEndPoll();
		return;
	}

	// NOT gesture-live: a click gesture (Shift+Click add, RMB delete, numeric commit) — the engine
	// opens+closes its transaction within the input event, so the inline funnel rides that still-open
	// transaction as one undo entry. Self-echo flag spans mutate+notify (Rule A).
	Paper2DPlusCurveTracks::FCurveCoerceSummary Summary;
	{
		TGuardValue<bool> WriteGuard(bCurveWriteInProgress, true);
		Summary = RunCoerceFunnel(CurveName);
		if (Summary.bChanged && IsRowTargetCurrent())
		{
			// F7: sibling panels (Frame Data table, frame-event preview/readouts) must converge on the
			// funnel's FINAL values — the old drag-end staleness bug D14, closed properly.
			OnCurveListChanged.ExecuteIfBound();
		}
	}
	if (Summary.bChanged)
	{
		// The notify above ran INSIDE the self-echo window, so the host's gated handler pended its
		// refresh (bNeedsRefresh) rather than acting — arm the poll so OnGestureSettled flushes it
		// (pend, never drop: F7 applies to the inline path too).
		ArmGestureEndPoll();
	}
	InvalidateRowFor(CurveName);
}

void SCurveTrackStack::ArmGestureEndPoll()
{
	if (bGestureEndPollActive && GestureEndPollHandle.IsValid())
	{
		return;
	}
	if (TSharedPtr<FActiveTimerHandle> Existing = GestureEndPollHandle.Pin())
	{
		UnRegisterActiveTimer(Existing.ToSharedRef());
	}
	GestureEndPollHandle.Reset();
	bGestureEndPollActive = true;
	GestureEndPollHandle = RegisterActiveTimer(
		0.1f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SCurveTrackStack::OnGestureEndPoll));
}

EActiveTimerReturnType SCurveTrackStack::OnGestureEndPoll(double /*InCurrentTime*/, float /*InDeltaTime*/)
{
	if (IsCurveGestureLive())
	{
		return EActiveTimerReturnType::Continue; // keep polling until the gesture closes
	}
	GestureEndPollHandle.Reset();
	bGestureEndPollActive = false;
	FlushPendingCoerce();
	// F7 flush seam: broadcasts that arrived under the widened HasActiveTransaction gate during the
	// gesture were PENDED (bNeedsRefresh on the host), never dropped — let the host flush them now
	// that the gesture and the funnel are both done.
	OnGestureSettled.ExecuteIfBound();
	return EActiveTimerReturnType::Stop;
}

void SCurveTrackStack::FlushPendingCoerce()
{
	if (PendingCoerceCurves.Num() == 0)
	{
		return;
	}
	const TSet<FName> Pending = MoveTemp(PendingCoerceCurves);
	PendingCoerceCurves.Reset();
	if (!CanEditCurvesAttr.Get(true))
	{
		return;
	}
	UPaper2DPlusCharacterProfileAsset* PendingAsset = RowAsset.Get();
	const bool bPendingTargetStillCurrent = IsRowTargetCurrent();
	if (!PendingAsset)
	{
		return;
	}

	// Dry-run on COPIES first: only open the snap transaction when at least one pending curve
	// actually needs coercion (never an empty transaction / spurious dirty for a snapped drag that
	// landed clean).
	bool bAnyNeedsCoerce = false;
	for (const FName& CurveName : Pending)
	{
		const FPaper2DPlusCurveTrackOwner* Adapter = FindAdapter(CurveName);
		const FPaper2DPlusFrameCurve* FrameCurve = Adapter ? Adapter->ResolveFrameCurve(CurveName) : nullptr;
		if (!FrameCurve)
		{
			continue;
		}
		FRichCurve Scratch = FrameCurve->Curve;
		if (Paper2DPlusCurveTracks::CoerceCurveKeysToFrames(
				Scratch, Adapter->GetSnapshotFrameCount(), FrameCurve->Mode).bChanged)
		{
			bAnyNeedsCoerce = true;
			break;
		}
	}

	if (bAnyNeedsCoerce)
	{
		// ONE panel-style transaction over ALL pending-curve funnel writes. This is a DELIBERATE
		// SECOND undo entry after the engine's own drag entry: deletions must never happen
		// mid-gesture (F1), and the undo order funnel-then-drag is coherent (first Ctrl+Z restores
		// the un-coerced drag result, the second undoes the drag itself).
		TGuardValue<bool> WriteGuard(bCurveWriteInProgress, true); // Rule A: spans mutate+notify
		LastSelfCurveWriteFrame = GFrameCounter; // F8
		BeginTransaction(LOCTEXT("SnapCurveKeysTxn", "Snap curve keys to frames"), PendingAsset);
		for (const FName& CurveName : Pending)
		{
			RunCoerceFunnel(CurveName);
		}
		EndTransaction();
		// F7: converge sibling panels on the funnel's final values (D14).
		if (bPendingTargetStillCurrent)
		{
			OnCurveListChanged.ExecuteIfBound();
		}
	}

	// The funnel deletes keys (dedupe) under the live shared widget — re-attach so its view models
	// re-validate, then repaint (the PruneOrphanKeys recipe).
	ReattachSharedCurveOwner();
	for (const FName& CurveName : Pending)
	{
		InvalidateRowFor(CurveName);
	}
}

Paper2DPlusCurveTracks::FCurveCoerceSummary SCurveTrackStack::RunCoerceFunnel(FName CurveName)
{
	// Resolve through the shared adapter (F2): snapshot index + snapshot flipbook count — a selection
	// change since the gesture began can never point this at the wrong move's same-named curve.
	FPaper2DPlusCurveTrackOwner* Adapter = FindAdapter(CurveName);
	FPaper2DPlusFrameCurve* FrameCurve = Adapter ? Adapter->ResolveFrameCurve(CurveName) : nullptr;
	if (!FrameCurve)
	{
		return Paper2DPlusCurveTracks::FCurveCoerceSummary();
	}
	return Paper2DPlusCurveTracks::CoerceCurveKeysToFrames(
		FrameCurve->Curve, Adapter->GetSnapshotFrameCount(), FrameCurve->Mode);
}

// -----------------------------------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------------------------------

UPaper2DPlusCharacterProfileAsset* SCurveTrackStack::ResolveAsset() const
{
	return AssetAttr.Get(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>()).Get();
}

FFlipbookProfileEntry* SCurveTrackStack::GetSelectedFlipbookData() const
{
	UPaper2DPlusCharacterProfileAsset* AssetPtr = ResolveAsset();
	const int32 FBIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
	if (!AssetPtr || !AssetPtr->Flipbooks.IsValidIndex(FBIndex))
	{
		return nullptr;
	}
	return &AssetPtr->Flipbooks[FBIndex];
}

int32 SCurveTrackStack::GetFrameCount() const
{
	const FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	if (!Data)
	{
		return 0;
	}
	if (const UPaperFlipbook* FB = Data->Identity.Flipbook.Get())
	{
		return FB->GetNumKeyFrames();
	}
	// This helper feeds Slate attributes (desired widths, view ranges, playheads). Never sync-load from
	// an attribute/paint path; the profile's cook-synced frame rows are the non-blocking fallback used
	// by the Frame Cue provider too. BuildRows may resolve the soft object once outside paint.
	return Data->CombatData.Frames.Num();
}

bool SCurveTrackStack::HasResolvedAnimationForTiming() const
{
	return GetSelectedFlipbookData() != nullptr;
}

float SCurveTrackStack::GetCurveBodyWidth() const
{
	return Paper2DPlusFrameCueTimeline::FTimingGeometry::GetBodyWidth(
		HasResolvedAnimationForTiming(), GetFrameCount());
}

float SCurveTrackStack::GetCurveBodyWidthForTests() const
{
	return GetCurveBodyWidth();
}

TArray<FName> SCurveTrackStack::GetSortedCurveNames() const
{
	TArray<FName> Names;
	if (const FFlipbookProfileEntry* Data = GetSelectedFlipbookData())
	{
		Data->CurveData.Curves.GetKeys(Names);
	}
	Names.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	return Names;
}

FString SCurveTrackStack::MakePresentationScopeKey() const
{
	const UPaper2DPlusCharacterProfileAsset* AssetPtr = ResolveAsset();
	const int32 FlipbookIndex = SelectedFlipbookIndex.Get(INDEX_NONE);
	if (!AssetPtr || !AssetPtr->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return FString();
	}
	const FFlipbookProfileEntry& Data = AssetPtr->Flipbooks[FlipbookIndex];
	FString AnimationIdentity = Data.Identity.FlipbookName;
	const FString FlipbookPath = Data.Identity.Flipbook.ToSoftObjectPath().ToString();
	if (AnimationIdentity.IsEmpty() && FlipbookPath.IsEmpty())
	{
		AnimationIdentity = FString::Printf(TEXT("Index:%d"), FlipbookIndex);
	}
	return FString::Printf(
		TEXT("%s|%s|%s"),
		*AssetPtr->GetPathName(),
		*AnimationIdentity,
		*FlipbookPath);
}

Paper2DPlusCurveTracks::FCurvePresentationState* SCurveTrackStack::ReconcileCurrentPresentation(
	const TArray<FName>& AuthoredCurveNames)
{
	const FString NewScope = MakePresentationScopeKey();
	if (CurrentPresentationScope != NewScope)
	{
		if (Paper2DPlusCurveTracks::FCurvePresentationState* OldState =
			PresentationByScope.Find(CurrentPresentationScope))
		{
			// Solo is intentionally a temporary focus mode: leaving an animation always exits it, even
			// though base visibility remains isolated per scope. The unified timeline reasserts the one
			// primary selection after every retarget, so a cached legend selection never revives alone.
			OldState->ExitSoloForRetarget();
		}
		CurrentPresentationScope = NewScope;
	}
	if (CurrentPresentationScope.IsEmpty())
	{
		return nullptr;
	}
	Paper2DPlusCurveTracks::FCurvePresentationState& State =
		PresentationByScope.FindOrAdd(CurrentPresentationScope);
	State.Reconcile(AuthoredCurveNames);
	return &State;
}

const Paper2DPlusCurveTracks::FCurvePresentationState* SCurveTrackStack::FindCurrentPresentation() const
{
	const FString Scope = MakePresentationScopeKey();
	return Scope.IsEmpty() ? nullptr : PresentationByScope.Find(Scope);
}

bool SCurveTrackStack::IsRowTargetCurrent() const
{
	return RowAsset == TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>(ResolveAsset())
		&& RowFlipbookIndex == SelectedFlipbookIndex.Get(INDEX_NONE);
}

FPaper2DPlusCurveTrackOwner* SCurveTrackStack::FindAdapter(FName CurveName) const
{
	return SharedAdapter.IsValid() && SharedAdapter->OwnsCurve(CurveName) ? SharedAdapter.Get() : nullptr;
}

bool SCurveTrackStack::SharedCurveEditorHasMouseCapture() const
{
	return SharedCurveEditorWidget.IsValid() && SharedCurveEditorWidget->HasMouseCapture();
}

void SCurveTrackStack::DetachSharedCurveOwner()
{
	bSharedCurveEditorAcceptsEdits = false;
	if (SharedCurveEditorWidget.IsValid())
	{
		// Clears the engine widget's cached raw FRichCurve* view models (risk 5) — after this, the
		// widget paints empty and cannot dereference freed curve storage.
		SharedCurveEditorWidget->SetCurveOwner(nullptr);
	}
}

void SCurveTrackStack::ReattachSharedCurveOwner()
{
	if (SharedCurveEditorWidget.IsValid()
		&& SharedAdapter.IsValid()
		&& RowCurveNames.Num() > 0
		&& SharedAdapter->GetSnapshotFrameCount() > 0)
	{
		// Same adapter -> SCurveEditor skips EmptyAllSelection and just rebuilds its view models from
		// a fresh GetCurves() resolve — the cure for undo's empty+refill TMap reallocation.
		bSharedCurveEditorAcceptsEdits = CanEditCurvesAttr.Get(true);
		SharedCurveEditorWidget->SetCurveOwner(SharedAdapter.Get(), bSharedCurveEditorAcceptsEdits);
	}
	else
	{
		DetachSharedCurveOwner();
	}
}

void SCurveTrackStack::InvalidateSharedCurveEditor()
{
	if (SharedCurveEditorWidget.IsValid())
	{
		SharedCurveEditorWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SCurveTrackStack::InvalidateSharedPlayhead()
{
	if (SharedPlayheadWidget.IsValid())
	{
		SharedPlayheadWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SCurveTrackStack::HandleSetSharedOutputViewRange(float NewMin, float NewMax)
{
	if (NewMax > NewMin)
	{
		SharedViewMinOutput = NewMin;
		SharedViewMaxOutput = NewMax;
	}
}

void SCurveTrackStack::UpdateSharedOutputViewRange()
{
	bool bHaveRange = false;
	bool bOnlyFixedSemanticRanges = VisibleCurveNames.Num() > 0;
	float MinValue = 0.0f;
	float MaxValue = 1.0f;

	auto IncludeValue = [&bHaveRange, &MinValue, &MaxValue](float Value)
	{
		if (!bHaveRange)
		{
			MinValue = Value;
			MaxValue = Value;
			bHaveRange = true;
		}
		else
		{
			MinValue = FMath::Min(MinValue, Value);
			MaxValue = FMath::Max(MaxValue, Value);
		}
	};

	const FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	for (const FName& CurveName : VisibleCurveNames)
	{
		const FPaper2DPlusKnownCurve Metadata = FPaper2DPlusCurvePickerUtils::ResolveCurveMetadata(CurveName);
		const bool bHasFixedMetadataRange = Metadata.bUseFixedValueRange && Metadata.ValueMax > Metadata.ValueMin;
		if (bHasFixedMetadataRange)
		{
			IncludeValue(Metadata.ValueMin);
			IncludeValue(Metadata.ValueMax);
		}
		else
		{
			bOnlyFixedSemanticRanges = false;
			if (Metadata.Semantic == EPaper2DPlusKnownCurveSemantic::FrameCount)
			{
				IncludeValue(FMath::Max(0.0f, Metadata.ValueMin));
			}
		}

		const FPaper2DPlusFrameCurve* Curve = Data ? Data->CurveData.Curves.Find(CurveName) : nullptr;
		if (!Curve)
		{
			continue;
		}
		for (const FRichCurveKey& Key : Curve->Curve.GetConstRefOfKeys())
		{
			IncludeValue(Key.Value);
			if (!bHasFixedMetadataRange || Key.Value < Metadata.ValueMin || Key.Value > Metadata.ValueMax)
			{
				bOnlyFixedSemanticRanges = false;
			}
		}
	}

	if (!bHaveRange)
	{
		MinValue = 0.0f;
		MaxValue = 1.0f;
	}
	else if (FMath::IsNearlyEqual(MinValue, MaxValue))
	{
		const float Pad = FMath::Max(1.0f, FMath::Abs(MinValue) * 0.25f);
		MinValue -= Pad;
		MaxValue += Pad;
	}
	else if (!bOnlyFixedSemanticRanges)
	{
		const float Pad = FMath::Max(0.05f, (MaxValue - MinValue) * 0.10f);
		MinValue -= Pad;
		MaxValue += Pad;
	}

	SharedViewMinOutput = MinValue;
	SharedViewMaxOutput = MaxValue;
	bSharedOutputSnappingEnabled = FPaper2DPlusCurvePickerUtils::ResolveSharedOutputSnap(VisibleCurveNames, SharedOutputSnap);
	if (!bSharedOutputSnappingEnabled)
	{
		SharedOutputSnap = 1.0f;
	}
}

void SCurveTrackStack::InvalidateRowFor(FName CurveName)
{
	// Targeted repaint only — contextual Details diagnostics and the curve shape are live reads,
	// so a paint invalidate is the whole refresh.
	for (const TSharedPtr<SCurveTrackRow>& Row : Rows)
	{
		if (Row.IsValid() && Row->GetCurveName() == CurveName)
		{
			Row->Invalidate(EInvalidateWidgetReason::Paint);
		}
	}
	InvalidateSharedCurveEditor();
}

// -----------------------------------------------------------------------------------------------------
// Probe (Paper2DPlus.CurveTracksProbe — greppable headless/ECABridge verification, D11)
// -----------------------------------------------------------------------------------------------------

void SCurveTrackStack::LogProbe() const
{
	const FFlipbookProfileEntry* Data = GetSelectedFlipbookData();
	const FString MoveName = Data ? Data->Identity.FlipbookName : FString(TEXT("<none>"));
	const int32 FrameCount = GetFrameCount();
	// Asset-name prefix (F10): with the registry logging one block per live stack, the asset name is
	// what tells two editors' blocks apart.
	const UPaper2DPlusCharacterProfileAsset* AssetPtr = ResolveAsset();
	const FString AssetName = AssetPtr ? AssetPtr->GetName() : FString(TEXT("<none>"));

	UE_LOG(LogTemp, Display, TEXT("[CurveTracks] Probe: asset=%s move=%s rows=%d frames=%d gestureLive=%d"),
		*AssetName, *MoveName, Rows.Num(), FrameCount, IsCurveGestureLive() ? 1 : 0);

	if (!Data)
	{
		return;
	}
	for (const FName& CurveName : GetSortedCurveNames())
	{
		const FPaper2DPlusFrameCurve* Curve = Data->CurveData.Curves.Find(CurveName);
		if (!Curve)
		{
			continue;
		}
		FString Keys;
		for (const FRichCurveKey& Key : Curve->Curve.GetConstRefOfKeys())
		{
			Keys += FString::Printf(TEXT("%s(%d,%.3f)"),
				Keys.IsEmpty() ? TEXT("") : TEXT(","), FMath::RoundToInt(Key.Time), Key.Value);
		}
		FString Orphans;
		for (int32 OrphanFrame : Paper2DPlusCurveTracks::FindOrphanKeyFrames(Curve->Curve, FrameCount))
		{
			Orphans += FString::Printf(TEXT("%s%d"), Orphans.IsEmpty() ? TEXT("") : TEXT(","), OrphanFrame);
		}
		UE_LOG(LogTemp, Display, TEXT("[CurveTracks]   curve %s mode=%s keys=[%s] orphans=[%s]"),
			*CurveName.ToString(), CurveTrackStack_ModeToString(Curve->Mode), *Keys, *Orphans);
	}
}

#undef LOCTEXT_NAMESPACE
