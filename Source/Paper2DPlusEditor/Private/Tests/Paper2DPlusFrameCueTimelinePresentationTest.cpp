// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueTimelinePresentation.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "PackageTools.h"
#include "Paper2DPlusCueTags.h"
#include "Paper2DPlusEditorTestFrameCueTypes.h"
#include "Paper2DPlusSettings.h"
#include "SFrameEventTimelineTrack.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"

namespace Paper2DPlusFrameCueTimelinePresentationTest
{
	struct FScopedTagColorRegistry
	{
		FScopedTagColorRegistry()
			: Settings(GetMutableDefault<UPaper2DPlusSettings>())
			, Saved(Settings ? Settings->TagColors : TArray<FPaper2DPlusTagColor>())
		{
		}

		~FScopedTagColorRegistry()
		{
			if (Settings)
			{
				// Restore directly. Tests must never persist config or broadcast editor-facing changes.
				Settings->TagColors = Saved;
			}
		}

		UPaper2DPlusSettings* Settings = nullptr;
		TArray<FPaper2DPlusTagColor> Saved;
	};

	bool EqualTagColorRegistries(
		const TArray<FPaper2DPlusTagColor>& A,
		const TArray<FPaper2DPlusTagColor>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (A[Index].Tag != B[Index].Tag
				|| !A[Index].Color.Equals(B[Index].Color))
			{
				return false;
			}
		}
		return true;
	}

	struct FCueDefaultsSnapshot
	{
		FLinearColor Color;
		FName DebugName;
		FGameplayTag CueTag;
		EPaper2DPlusFrameCueNetPolicy NetPolicy =
			EPaper2DPlusFrameCueNetPolicy::LocalAlways;

		static FCueDefaultsSnapshot Capture(const UPaper2DPlusCueBase& Defaults)
		{
			FCueDefaultsSnapshot Result;
			Result.Color = Defaults.Color;
			Result.DebugName = Defaults.DebugName;
			Result.CueTag = Defaults.CueTag;
			Result.NetPolicy = Defaults.NetPolicy;
			return Result;
		}

		bool Matches(const UPaper2DPlusCueBase& Defaults) const
		{
			return Color.Equals(Defaults.Color)
				&& DebugName == Defaults.DebugName
				&& CueTag == Defaults.CueTag
				&& NetPolicy == Defaults.NetPolicy;
		}
	};

	struct FScopedCreationPackage
	{
		explicit FScopedCreationPackage(const TCHAR* Stem)
		{
			PackageName = FString::Printf(
				TEXT("/Game/__AutomationTemp__/%s_%s"),
				Stem,
				*FGuid::NewGuid().ToString(EGuidFormats::Digits));
			Package = CreatePackage(*PackageName);
			if (Package)
			{
				Package->AddToRoot();
				Package->SetDirtyFlag(false);
			}
		}

		~FScopedCreationPackage()
		{
			if (!Package)
			{
				return;
			}
			Package->SetDirtyFlag(false);
			Package->RemoveFromRoot();
			FText UnloadError;
			TArray<UPackage*> Packages = { Package };
			UPackageTools::UnloadPackages(Packages, UnloadError, true);
			Package = nullptr;
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}

		FString PackageName;
		UPackage* Package = nullptr;
	};

	UPaper2DPlusCueBase* GetGeneratedDefaults(
		const FPaper2DPlusFrameCueTypeCreateResult& Created)
	{
		return Created.CueType && Created.CueType->GeneratedClass
			? Cast<UPaper2DPlusCueBase>(
				Created.CueType->GeneratedClass->GetDefaultObject(false))
			: nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPlacementBarUsesAuthoredCueColor,
	"Paper2DPlus.FrameCues.Timeline.PlacementBarUsesAuthoredCueColor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPlacementBarUsesAuthoredCueColor::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTimelinePresentation;
	using namespace Paper2DPlusFrameCueTimelinePresentationTest;
	FScopedTagColorRegistry Registry;
	if (!TestNotNull(TEXT("Paper2DPlus settings CDO exists"), Registry.Settings))
	{
		return false;
	}
	Registry.Settings->TagColors.Reset();
	Registry.Settings->TagColors.Emplace(
		Paper2DPlusCueTags::Default.GetTag(),
		FLinearColor::Red);

	UPaper2DPlusEditorTestMomentCue* Cue =
		NewObject<UPaper2DPlusEditorTestMomentCue>();
	Cue->CueTag = Paper2DPlusCueTags::Default.GetTag();
	// The override flag is what marks a colour as authored — never "differs from the default", which
	// made white unauthorable and mis-read near-white through FLinearColor::Equals' tolerance.
	Cue->bOverrideColor = true;
	Cue->Color = FLinearColor(0.08f, 0.16f, 0.30f, 1.0f);
	const FPlacementPresentation DarkPresentation = ResolvePlacement(*Cue, false);
	TestTrue(TEXT("Authored placement Color wins over an exact tag registry entry"),
		DarkPresentation.FillColor.Equals(Cue->Color));
	TestTrue(TEXT("Dark authored colors receive light label text"),
		DarkPresentation.LabelColor.Equals(FLinearColor::White));

	Cue->Color = FLinearColor(0.95f, 0.88f, 0.20f, 1.0f);
	const FPlacementPresentation BrightPresentation = ResolvePlacement(*Cue, false);
	TestTrue(TEXT("Bright authored colors remain authoritative"),
		BrightPresentation.FillColor.Equals(Cue->Color));
	TestTrue(TEXT("Bright authored colors receive dark label text"),
		BrightPresentation.LabelColor.R < 0.1f
			&& BrightPresentation.LabelColor.G < 0.1f
			&& BrightPresentation.LabelColor.B < 0.1f);

	Cue->Color = FLinearColor(0.20f, 0.20f, 0.20f, 1.0f);
	TestTrue(TEXT("A dark mid-luminance fill keeps white text"),
		ResolvePlacement(*Cue, false).LabelColor.Equals(FLinearColor::White));
	Cue->Color = FLinearColor(0.30f, 0.30f, 0.30f, 1.0f);
	TestTrue(TEXT("A light mid-luminance fill switches to black text"),
		ResolvePlacement(*Cue, false).LabelColor.Equals(FLinearColor::Black));

	// White is a legitimate authored choice, and it is exactly the value the old sentinel could not
	// express: the bar painted the registry colour while the property grid still showed white.
	Cue->Color = FLinearColor::White;
	TestTrue(TEXT("White is authorable when the override flag is set"),
		ResolvePlacement(*Cue, false).FillColor.Equals(FLinearColor::White));

	// And near-white, which FLinearColor::Equals' tolerance used to swallow whole.
	Cue->Color = FLinearColor(0.999f, 0.999f, 0.999f, 1.0f);
	TestTrue(TEXT("Near-white is authorable and is not rounded away"),
		ResolvePlacement(*Cue, false).FillColor.Equals(Cue->Color));

	// Clearing the flag returns the placement to the registry, so the two paths stay switchable.
	Cue->bOverrideColor = false;
	TestTrue(TEXT("Clearing the override returns the bar to the tag registry colour"),
		ResolvePlacement(*Cue, false).FillColor.Equals(FLinearColor::Red));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFreshDefaultTagUsesConventionColor,
	"Paper2DPlus.FrameCues.Timeline.FreshDefaultTagUsesConventionColor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFreshDefaultTagUsesConventionColor::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTimelinePresentation;
	using namespace Paper2DPlusFrameCueTimelinePresentationTest;
	FScopedTagColorRegistry Registry;
	if (!TestNotNull(TEXT("Paper2DPlus settings CDO exists"), Registry.Settings))
	{
		return false;
	}
	Registry.Settings->TagColors.Reset();

	UPaper2DPlusEditorTestMomentCue* Fresh =
		NewObject<UPaper2DPlusEditorTestMomentCue>();
	Fresh->Color = FLinearColor::White;
	Fresh->CueTag = Paper2DPlusCueTags::Default.GetTag();
	TestTrue(TEXT("The plugin-owned Cue root is registered"),
		Paper2DPlusCueTags::Cue.GetTag().IsValid());
	TestTrue(TEXT("The replaceable Default Cue tag is registered"),
		Paper2DPlusCueTags::Default.GetTag().IsValid());
	const FPlacementPresentation FreshPresentation =
		ResolvePlacement(*Fresh, false);
	TestTrue(TEXT("A fresh Default-tagged placement gets the code convention color"),
		FreshPresentation.FillColor.Equals(GetDefaultCueConventionColor()));
	TestTrue(TEXT("The default convention blue receives high-contrast black label text"),
		FreshPresentation.LabelColor.Equals(FLinearColor::Black));

	Fresh->CueTag = FGameplayTag();
	TestTrue(TEXT("An otherwise unauthored legacy placement gets the neutral fallback"),
		ResolvePlacement(*Fresh, false).FillColor.Equals(GetNeutralFallbackColor()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTagColorRegistryExactAndAncestorPrecedence,
	"Paper2DPlus.FrameCues.Timeline.TagColorRegistryExactAndAncestorPrecedence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTagColorRegistryExactAndAncestorPrecedence::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTimelinePresentation;
	using namespace Paper2DPlusFrameCueTimelinePresentationTest;
	FScopedTagColorRegistry Registry;
	if (!TestNotNull(TEXT("Paper2DPlus settings CDO exists"), Registry.Settings))
	{
		return false;
	}
	const FLinearColor AncestorColor(0.15f, 0.72f, 0.34f, 1.0f);
	const FLinearColor ExactColor(0.82f, 0.24f, 0.62f, 1.0f);
	Registry.Settings->TagColors.Reset();
	Registry.Settings->TagColors.Emplace(
		Paper2DPlusCueTags::Cue.GetTag(),
		AncestorColor);

	UPaper2DPlusEditorTestMomentCue* Cue =
		NewObject<UPaper2DPlusEditorTestMomentCue>();
	Cue->CueTag = Paper2DPlusCueTags::Default.GetTag();
	Cue->Color = FLinearColor::White;
	TestTrue(TEXT("The nearest registered ancestor overrides the code convention"),
		ResolvePlacement(*Cue, false).FillColor.Equals(AncestorColor));

	Registry.Settings->TagColors.Emplace(
		Paper2DPlusCueTags::Default.GetTag(),
		ExactColor);
	TestTrue(TEXT("An exact registry entry wins over its registered ancestor"),
		ResolvePlacement(*Cue, false).FillColor.Equals(ExactColor));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPlacementLabelUsesDebugNameThenCueType,
	"Paper2DPlus.FrameCues.Timeline.PlacementLabelUsesDebugNameThenCueType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPlacementLabelUsesDebugNameThenCueType::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTimelinePresentation;
	UPaper2DPlusEditorTestMomentCue* Cue =
		NewObject<UPaper2DPlusEditorTestMomentCue>();
	Cue->DebugName = TEXT("Hit Spark");
	TestEqual(TEXT("DebugName is the primary visible identity"),
		ResolvePlacement(*Cue, false).Label.ToString(),
		FString(TEXT("Hit Spark")));

	Cue->DebugName = NAME_None;
	TestEqual(TEXT("The Cue Type display name is the zero-authoring fallback"),
		ResolvePlacement(*Cue, false).Label.ToString(),
		Cue->GetClass()->GetDisplayNameText().ToString());
	TestTrue(TEXT("The fallback identity remains available when the bar label cannot fit"),
		BuildToolTip(*Cue, FText::GetEmpty()).ToString().Contains(
			Cue->GetClass()->GetDisplayNameText().ToString()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueAndOneFrameCueStateUseDistinctGeometry,
	"Paper2DPlus.FrameCues.Timeline.CueAndOneFrameCueStateUseDistinctGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueAndOneFrameCueStateUseDistinctGeometry::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTimelinePresentation;
	UPaper2DPlusEditorTestMomentCue* Cue =
		NewObject<UPaper2DPlusEditorTestMomentCue>();
	UPaper2DPlusEditorTestRangeCue* OneFrameState =
		NewObject<UPaper2DPlusEditorTestRangeCue>();
	OneFrameState->FrameCount = 1;
	const FVector2D AvailableSize(52.0f, 26.0f);
	const FPlacementGeometry CueGeometry = ResolveGeometry(*Cue, AvailableSize);
	const FPlacementGeometry StateGeometry =
		ResolveGeometry(*OneFrameState, AvailableSize);

	TestTrue(TEXT("A Cue uses the anchor shape with the trigger-edge diamond on the leading edge"),
		CueGeometry.Shape == EPlacementShape::CueAnchor
			&& CueGeometry.bDrawEdgeDiamond
			&& !CueGeometry.bEdgeDiamondAtEnd);
	TestTrue(TEXT("A one-frame Cue State uses the inset span shape and carries no diamond"),
		StateGeometry.Shape == EPlacementShape::CueStateSpan
			&& !StateGeometry.bDrawEdgeDiamond
			&& !StateGeometry.FillOffset.Equals(FVector2D::ZeroVector));
	TestTrue(TEXT("One-frame Cue State geometry cannot equal Cue geometry"),
		!CueGeometry.FillOffset.Equals(StateGeometry.FillOffset)
			|| !CueGeometry.FillSize.Equals(StateGeometry.FillSize)
			|| CueGeometry.bDrawEdgeDiamond != StateGeometry.bDrawEdgeDiamond);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusTriggerEdgeDiamondSitsOnItsFiringBoundary,
	"Paper2DPlus.FrameCues.Timeline.TriggerEdgeDiamondSitsOnItsFiringBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusTriggerEdgeDiamondSitsOnItsFiringBoundary::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTimelinePresentation;
	UPaper2DPlusEditorTestMomentCue* Cue =
		NewObject<UPaper2DPlusEditorTestMomentCue>();
	const FVector2D AvailableSize(52.0f, 26.0f);

	const FPlacementGeometry StartGeometry = ResolveGeometry(*Cue, AvailableSize);
	TestTrue(TEXT("FrameStart puts the diamond on the cell's leading edge"),
		StartGeometry.bDrawEdgeDiamond
			&& !StartGeometry.bEdgeDiamondAtEnd
			&& StartGeometry.EdgeDiamondOffset.X
				< AvailableSize.X * 0.5f - StartGeometry.EdgeDiamondSize.X);

	Cue->TriggerEdge = EPaper2DPlusCueTriggerEdge::FrameEnd;
	const FPlacementGeometry EndGeometry = ResolveGeometry(*Cue, AvailableSize);
	TestTrue(TEXT("FrameEnd puts the diamond on the cell's trailing edge"),
		EndGeometry.bDrawEdgeDiamond
			&& EndGeometry.bEdgeDiamondAtEnd
			&& EndGeometry.EdgeDiamondOffset.X
				> AvailableSize.X * 0.5f);
	TestTrue(TEXT("Both placements keep the diamond inside the cell"),
		StartGeometry.EdgeDiamondOffset.X >= 0.0f
			&& EndGeometry.EdgeDiamondOffset.X + EndGeometry.EdgeDiamondSize.X
				<= AvailableSize.X);

	TestTrue(TEXT("The end-edge tooltip says when the cue fires"),
		BuildToolTip(*Cue, FText::GetEmpty()).ToString().Contains(
			TEXT("fires when the frame ends")));
	TestTrue(TEXT("The end-edge accessible summary says when the cue fires"),
		BuildAccessibleSummary(*Cue, FText::GetEmpty()).ToString().Contains(
			TEXT("fires when the frame ends")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusErrorBadgeComposesAbovePlacementIdentity,
	"Paper2DPlus.FrameCues.Timeline.ErrorBadgeComposesAbovePlacementIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusErrorBadgeComposesAbovePlacementIdentity::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTimelinePresentation;
	UPaper2DPlusEditorTestMomentCue* Cue =
		NewObject<UPaper2DPlusEditorTestMomentCue>();
	Cue->Color = FLinearColor(0.22f, 0.41f, 0.74f, 1.0f);
	const FPlacementPresentation Healthy = ResolvePlacement(*Cue, false);
	const FPlacementPresentation Errored = ResolvePlacement(*Cue, true);
	TestFalse(TEXT("A healthy placement has no behavior-error badge"),
		Healthy.bShowErrorBadge);
	TestTrue(TEXT("An errored placement retains the badge"),
		Errored.bShowErrorBadge);
	TestTrue(TEXT("Badging does not replace identity color"),
		Errored.FillColor.Equals(Healthy.FillColor));
	TestTrue(TEXT("The badge layer is composited above fill, shape, and label"),
		ErrorBadgeLayerOffset > FillLayerOffset
			&& ErrorBadgeLayerOffset > ShapeLayerOffset
			&& ErrorBadgeLayerOffset > LabelLayerOffset);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAccessibleErrorAugmentsTimingIdentity,
	"Paper2DPlus.FrameCues.Timeline.AccessibleErrorAugmentsTimingIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAccessibleErrorAugmentsTimingIdentity::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTimelinePresentation;
	UPaper2DPlusEditorTestRangeCue* State =
		NewObject<UPaper2DPlusEditorTestRangeCue>();
	State->DebugName = TEXT("Parry Window");
	State->StartFrame = 4;
	State->FrameCount = 1;

	TArray<TObjectPtr<UPaper2DPlusCueBase>> Cues = { State };
	const TSharedRef<SFrameEventTimelineTrack> Track =
		SNew(SFrameEventTimelineTrack)
		.Cues(&Cues)
		.SelectedEventIndex(0);
	const FString TrackSummary = Track->GetAccessibleSummaryTextForCue(0).ToString();
	TestTrue(TEXT("The live timeline accessibility path uses the resolved DebugName"),
		TrackSummary.Contains(TEXT("Parry Window")));
	TestTrue(TEXT("The live timeline accessibility path identifies Cue State timing"),
		TrackSummary.Contains(TEXT("Cue State"))
			&& TrackSummary.Contains(TEXT("start frame 4"))
			&& TrackSummary.Contains(TEXT("duration 1 frames")));
#if WITH_ACCESSIBILITY
	const FString PublishedAccessibleText = Track->GetAccessibleText().ToString();
	TestTrue(TEXT("The timeline publishes the live summary through Slate accessibility"),
		PublishedAccessibleText.Contains(TEXT("Parry Window"))
			&& PublishedAccessibleText.Contains(TEXT("Cue State"))
			&& PublishedAccessibleText.Contains(TEXT("start frame 4"))
			&& PublishedAccessibleText.Contains(TEXT("duration 1 frames")));
#endif

	const FString ErroredSummary = BuildAccessibleSummary(
		*State,
		FText::FromString(TEXT("Fixture failure"))).ToString();
	TestTrue(TEXT("Behavior error details augment rather than replace timing"),
		ErroredSummary.Contains(TEXT("Parry Window"))
			&& ErroredSummary.Contains(TEXT("Cue State"))
			&& ErroredSummary.Contains(TEXT("start frame 4"))
			&& ErroredSummary.Contains(TEXT("duration 1 frames"))
			&& ErroredSummary.Contains(TEXT("Behavior error: Fixture failure")));
	const FString ErroredToolTip = BuildToolTip(
		*State,
		FText::FromString(TEXT("Fixture failure"))).ToString();
	TestTrue(TEXT("Hover identity also preserves form, timing, and error details"),
		ErroredToolTip.Contains(TEXT("Parry Window"))
			&& ErroredToolTip.Contains(TEXT("Cue State"))
			&& ErroredToolTip.Contains(TEXT("duration 1 frames"))
			&& ErroredToolTip.Contains(TEXT("Behavior error: Fixture failure")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCreationSeedsIdentityWithoutMutatingGlobalDefaults,
	"Paper2DPlus.FrameCues.Timeline.CreationSeedsIdentityWithoutMutatingGlobalDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCreationSeedsIdentityWithoutMutatingGlobalDefaults::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTimelinePresentationTest;
	FScopedTagColorRegistry Registry;
	if (!TestNotNull(TEXT("Paper2DPlus settings CDO exists"), Registry.Settings))
	{
		return false;
	}
	const TArray<FPaper2DPlusTagColor> SettingsBefore = Registry.Settings->TagColors;
	const FCueDefaultsSnapshot BaseBefore =
		FCueDefaultsSnapshot::Capture(*GetDefault<UPaper2DPlusCueBase>());
	const FCueDefaultsSnapshot CueBefore =
		FCueDefaultsSnapshot::Capture(*GetDefault<UPaper2DPlusCue>());
	const FCueDefaultsSnapshot StateBefore =
		FCueDefaultsSnapshot::Capture(*GetDefault<UPaper2DPlusCueState>());

	FScopedCreationPackage Fixture(TEXT("P2DPCueIdentityDefaults"));
	if (!TestNotNull(TEXT("Creation fixture package exists"), Fixture.Package))
	{
		return false;
	}
	FPaper2DPlusFrameCueTypeCreateRequest CueRequest;
	CueRequest.Package = Fixture.Package;
	CueRequest.AssetName = TEXT("BP_IdentityCue");
	const FPaper2DPlusFrameCueTypeCreateResult CueCreated =
		FPaper2DPlusFrameCueTypeAuthoring::CreateCueType(CueRequest);
	FPaper2DPlusFrameCueTypeCreateRequest StateRequest = CueRequest;
	StateRequest.AssetName = TEXT("BP_IdentityCueState");
	StateRequest.Kind = EPaper2DPlusFrameCueTypeKind::Range;
	const FPaper2DPlusFrameCueTypeCreateResult StateCreated =
		FPaper2DPlusFrameCueTypeAuthoring::CreateCueType(StateRequest);
	if (!TestTrue(TEXT("Cue creation succeeds"), CueCreated.IsSuccess())
		|| !TestTrue(TEXT("Cue State creation succeeds"), StateCreated.IsSuccess()))
	{
		return false;
	}

	auto AssertGeneratedIdentity = [this](
		const TCHAR* Label,
		const FPaper2DPlusFrameCueTypeCreateResult& Created)
	{
		const UPaper2DPlusCueBase* Defaults = GetGeneratedDefaults(Created);
		if (!TestNotNull(
			FString::Printf(TEXT("%s generated defaults exist"), Label),
			Defaults))
		{
			return false;
		}
		TestTrue(
			FString::Printf(TEXT("%s gets the replaceable Default Cue tag"), Label),
			Defaults->CueTag.MatchesTagExact(Paper2DPlusCueTags::Default.GetTag()));
		TestTrue(
			FString::Printf(TEXT("%s keeps Color designer-authored and replaceable"), Label),
			Defaults->Color.Equals(FLinearColor::White));
		TestTrue(
			FString::Printf(TEXT("%s remains born CosmeticOnly"), Label),
			Defaults->NetPolicy == EPaper2DPlusFrameCueNetPolicy::CosmeticOnly);
		return true;
	};
	TestTrue(TEXT("Cue generated identity is seeded"), AssertGeneratedIdentity(TEXT("Cue"), CueCreated));
	TestTrue(TEXT("Cue State generated identity is seeded"),
		AssertGeneratedIdentity(TEXT("Cue State"), StateCreated));

	// A later ordinary Blueprint compile must carry the generated CDO defaults forward.
	FKismetEditorUtilities::CompileBlueprint(
		CueCreated.CueType,
		EBlueprintCompileOptions::SkipGarbageCollection);
	FKismetEditorUtilities::CompileBlueprint(
		StateCreated.CueType,
		EBlueprintCompileOptions::SkipGarbageCollection);
	TestTrue(TEXT("Cue identity survives a subsequent Blueprint compile"),
		AssertGeneratedIdentity(TEXT("Recompiled Cue"), CueCreated));
	TestTrue(TEXT("Cue State identity survives a subsequent Blueprint compile"),
		AssertGeneratedIdentity(TEXT("Recompiled Cue State"), StateCreated));

	TestTrue(TEXT("The hidden native Cue base CDO is unchanged"),
		BaseBefore.Matches(*GetDefault<UPaper2DPlusCueBase>()));
	TestTrue(TEXT("The native Cue CDO is unchanged"),
		CueBefore.Matches(*GetDefault<UPaper2DPlusCue>()));
	TestTrue(TEXT("The native Cue State CDO is unchanged"),
		StateBefore.Matches(*GetDefault<UPaper2DPlusCueState>()));
	TestTrue(TEXT("Structured creation does not seed or mutate project Tag Colors"),
		EqualTagColorRegistries(SettingsBefore, Registry.Settings->TagColors));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
