// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CharacterProfileEditorModel.h"
#include "CurveTrackPanel.h"
#include "Editor.h"
#include "FrameCueDataProvider.h"
#include "Paper2DPlusEditorTestFrameCueTypes.h"
#include "FrameCues/Paper2DPlusFrameCueDispatch.h"
#include "FrameEventEditor.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "ProfileToolPanelProvider.h"
#include "SFrameCueTimeline.h"
#include "SFrameEventTimelineTrack.h"

namespace Paper2DPlusUnifiedCueTimelineParityTest
{
	struct FFixture
	{
		UPaper2DPlusCharacterProfileAsset* Profile = nullptr;
		UPaper2DPlusCharacterLayerAsset* Layer = nullptr;
		TSharedPtr<FCharacterProfileEditorModel> Model;
	};

	FFixture MakeFixture(bool bAddCurve)
	{
		FFixture Fixture;
		Fixture.Profile = NewObject<UPaper2DPlusCharacterProfileAsset>(
			GetTransientPackage(), NAME_None, RF_Transactional);
		FFlipbookProfileEntry& Entry = Fixture.Profile->Flipbooks.AddDefaulted_GetRef();
		Entry.Identity.FlipbookName = TEXT("attack");
		Entry.CombatData.Frames.SetNum(6);
		if (bAddCurve)
		{
			FPaper2DPlusFrameCurve& DamageCurve = Entry.CurveData.Curves.Add(TEXT("Damage"));
			DamageCurve.SetKeyValue(2, 3.5f);
		}

		Fixture.Layer = NewObject<UPaper2DPlusCharacterLayerAsset>(
			GetTransientPackage(), NAME_None, RF_Transactional);
		Fixture.Layer->BaseProfile = Fixture.Profile;
		FCharacterLayer& Layer = Fixture.Layer->Layers.AddDefaulted_GetRef();
		Layer.LayerId = FGuid::NewGuid();
		Layer.LayerName = TEXT("Weapon");

		Fixture.Model = MakeShared<FCharacterProfileEditorModel>();
		Fixture.Model->InitializeFromAsset(Fixture.Profile);
		Fixture.Model->SetSecondaryWatchedObject(Fixture.Layer);
		Fixture.Model->SetSelectedFlipbook(0);
		Fixture.Model->SetSelectedFrame(0);
		Fixture.Model->SetSelectedLayerById(Layer.LayerId);
		return Fixture;
	}

	TArray<FString> CaptureDispatchTrace(
		const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues,
		int32& OutActiveRangeCount)
	{
		TArray<FString> Trace;
		TSet<TObjectPtr<UPaper2DPlusCueBase>> ActiveRanges;
		FPaper2DPlusFrameCueContext Context;
		Context.AnimationName = TEXT("attack");

		const auto Dispatch = [&Cues, &Trace, &ActiveRanges, &Context](
			int32 PreviousFrame,
			int32 CurrentFrame)
		{
			Context.PreviousFrame = PreviousFrame;
			Context.CurrentFrame = CurrentFrame;
			Paper2DPlusFrameCues::DispatchFrameTransition(
				Cues,
				Context,
				ActiveRanges,
				[](UPaper2DPlusCueBase&) { return true; },
				[&Trace](
					UPaper2DPlusCueBase& Cue,
					const FPaper2DPlusFrameCueContext& Notification)
				{
					Trace.Add(FString::Printf(
						TEXT("%s|%d|%d>%d|%d|%d"),
						*Cue.DebugName.ToString(),
						static_cast<int32>(Notification.Phase),
						Notification.PreviousFrame,
						Notification.CurrentFrame,
						static_cast<int32>(Notification.EndReason),
						Notification.bIsCompressed ? 1 : 0));
				});
		};

		Dispatch(INDEX_NONE, 0);
		Dispatch(0, 1);
		Dispatch(1, 2);
		Dispatch(2, 3);
		Dispatch(3, 4);
		Dispatch(4, 5);
		OutActiveRangeCount = ActiveRanges.Num();
		return Trace;
	}

	bool TraceContains(const TArray<FString>& Trace, const TCHAR* CueName, EPaper2DPlusFrameCuePhase Phase)
	{
		const FString Prefix = FString::Printf(
			TEXT("%s|%d|"), CueName, static_cast<int32>(Phase));
		return Trace.ContainsByPredicate(
			[&Prefix](const FString& Record) { return Record.StartsWith(Prefix); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusUnifiedCueTimelineHostParityTest,
	"Paper2DPlus.FrameCues.UnifiedTimeline.ProfileAndLayerHostParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusUnifiedCueTimelineHostParityTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusUnifiedCueTimelineParityTest;
	if (!TestNotNull(TEXT("GEditor is available for production Frame Cue editors"), GEditor))
	{
		return false;
	}

	FFixture Fixture = MakeFixture(true);
	Fixture.Profile->AddToRoot();
	Fixture.Layer->AddToRoot();
	TSharedPtr<FProfileFrameCueDataProvider> ProfileProvider =
		MakeShared<FProfileFrameCueDataProvider>(Fixture.Model);
	TSharedPtr<FLayerFrameCueDataProvider> LayerProvider =
		MakeShared<FLayerFrameCueDataProvider>(Fixture.Layer, Fixture.Model);

	TSharedPtr<SFrameEventEditor> ProfileEditor = SNew(SFrameEventEditor)
		.Model(Fixture.Model)
		.DataProvider(ProfileProvider)
		.HostContract(FProfileToolPanelHostContract::Embedded());
	TSharedPtr<SFrameEventEditor> LayerEditor = SNew(SFrameEventEditor)
		.Model(Fixture.Model)
		.DataProvider(LayerProvider)
		.HostContract(FProfileToolPanelHostContract::External());
	LayerEditor->HandleHostActivated();

	TSharedPtr<SFrameCueTimeline> ProfileTimeline = ProfileEditor->GetUnifiedTimelineForTests();
	TSharedPtr<SFrameCueTimeline> LayerTimeline = LayerEditor->GetUnifiedTimelineForTests();
	TSharedPtr<SCurveTrackStack> ProfileCurves = ProfileEditor->GetCurveStackForTests();
	TSharedPtr<SCurveTrackStack> LayerCurves = LayerEditor->GetCurveStackForTests();
	if (TestTrue(TEXT("Profile host mounts the production unified timeline"), ProfileTimeline.IsValid())
		&& TestTrue(TEXT("Layer host mounts the production unified timeline"), LayerTimeline.IsValid()))
	{
		TestNotNull(TEXT("Profile host mounts the shared Cue lane tooling"), ProfileTimeline->GetCueLane().Get());
		TestNotNull(TEXT("Layer host mounts the shared Cue lane tooling"), LayerTimeline->GetCueLane().Get());
		TestTrue(TEXT("Profile Add Cue action exists before the first Cue"),
			ProfileTimeline->GetAddCueFocusTarget().IsValid());
		TestTrue(TEXT("Layer Add Cue action exists before the first Cue"),
			LayerTimeline->GetAddCueFocusTarget().IsValid());
		TestEqual(TEXT("empty Profile source still mounts Default"),
			ProfileTimeline->GetVisibleTrackCountForTests(), 1);
		TestEqual(TEXT("empty selected-Layer source still mounts Default"),
			LayerTimeline->GetVisibleTrackCountForTests(), 1);
		TestTrue(TEXT("Profile timeline keeps its curve region mounted"),
			ProfileTimeline->HasPersistentCurveRegionForTests());
		TestTrue(TEXT("Layer timeline keeps its curve region mounted"),
			LayerTimeline->HasPersistentCurveRegionForTests());
		TestTrue(TEXT("Profile timeline mounts the shared resizable legend gutter"),
			ProfileTimeline->HasResizableGutterForTests());
		TestTrue(TEXT("Layer timeline mounts the same shared resizable legend gutter"),
			LayerTimeline->HasResizableGutterForTests());
	}

	if (TestTrue(TEXT("Profile host exposes the integrated curve stack"), ProfileCurves.IsValid())
		&& TestTrue(TEXT("Layer host exposes the integrated curve stack"), LayerCurves.IsValid()))
	{
		ProfileCurves->RebuildRowsImmediatelyForTests();
		LayerCurves->RebuildRowsImmediatelyForTests();
		const TArray<FName> ExpectedCurves = { FName(TEXT("Damage")) };
		TestEqual(TEXT("Profile graph exposes the authored Profile curve"),
			ProfileCurves->GetGraphCurveNamesForTests(), ExpectedCurves);
		TestEqual(TEXT("selected-Layer graph exposes the same actual Profile curve"),
			LayerCurves->GetGraphCurveNamesForTests(), ExpectedCurves);
		TestTrue(TEXT("Profile graph is editable"), ProfileCurves->IsCurveGraphInteractiveForTests());
		TestFalse(TEXT("selected-Layer graph is read-only"), LayerCurves->IsCurveGraphInteractiveForTests());
		TestTrue(TEXT("Profile Details may mutate the authored curve"),
			ProfileCurves->CanMutateCurve(TEXT("Damage")));
		TestFalse(TEXT("Layer Details fail-closes Profile-owned curve mutations"),
			LayerCurves->CanMutateCurve(TEXT("Damage")));
		TestEqual(TEXT("Profile graph width stays frame-aligned"),
			ProfileCurves->GetCurveBodyWidthForTests(),
			6.0f * Paper2DPlusFrameCueTimeline::FTimingGeometry::PixelsPerKeyFrame);
		TestEqual(TEXT("Layer graph width stays identically frame-aligned"),
			LayerCurves->GetCurveBodyWidthForTests(),
			ProfileCurves->GetCurveBodyWidthForTests());
	}

	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* ProfileCues = ProfileProvider->GetCues(0);
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>* LayerCues = LayerProvider->GetCues(0);
	TestTrue(TEXT("Profile fixture remains an empty Cue source"), ProfileCues && ProfileCues->IsEmpty());
	TestTrue(TEXT("selected Layer remains empty without materializing authoring storage"),
		LayerCues == nullptr || LayerCues->IsEmpty());

	UPaper2DPlusEditorTestMomentCue* TrackACue = NewObject<UPaper2DPlusEditorTestMomentCue>(
		Fixture.Profile, NAME_None, RF_Transactional);
	UPaper2DPlusEditorTestMomentCue* TrackBCue = NewObject<UPaper2DPlusEditorTestMomentCue>(
		Fixture.Profile, NAME_None, RF_Transactional);
	Fixture.Profile->Flipbooks[0].FrameEventData.FrameCues = { TrackACue, TrackBCue };
	const FProfileScopedAnimationIdentity Scope =
		ProfileProvider->GetScopedAnimationIdentity(0);
	const FGuid TrackA = ProfileProvider->AddTrack(Scope, TEXT("Track A"));
	const FGuid TrackB = ProfileProvider->AddTrack(Scope, TEXT("Track B"));
	ProfileProvider->AssignCueToTrack(
		ProfileProvider->GetCueIdentity(Scope, TrackACue), TrackA);
	ProfileProvider->AssignCueToTrack(
		ProfileProvider->GetCueIdentity(Scope, TrackBCue), TrackB);
	ProfileTimeline->RefreshTrackHeaders();
	ProfileTimeline->HandleCueSelected(0);
	TestEqual(TEXT("first Cue selects its exact organizational track"),
		ProfileTimeline->GetActiveTrackId(), TrackA);
	ProfileTimeline->HandleCueSelected(1);
	TestEqual(TEXT("cross-track Cue selection activates the new track"),
		ProfileTimeline->GetActiveTrackId(), TrackB);
	TestEqual(TEXT("cross-track primary selection retains the new track"),
		ProfileTimeline->GetPrimarySelection().TrackId, TrackB);

	const FProfileScopedAnimationIdentity LayerScope =
		LayerProvider->GetScopedAnimationIdentity(0);
	const FGuid LayerContextTrack = LayerProvider->AddTrack(
		LayerScope, TEXT("Layer Context"));
	TestTrue(TEXT("selected Layer creates a context-target track"), LayerContextTrack.IsValid());
	LayerTimeline->RefreshTrackHeaders();

	const auto VerifyEmptyTrackContext = [this](
		const TCHAR* HostLabel,
		const TSharedPtr<SFrameCueTimeline>& Timeline,
		const FGuid ExpectedTrackId)
	{
		const TSharedPtr<SFrameEventTimelineTrack> Lane = Timeline.IsValid()
			? Timeline->GetCueLane()
			: nullptr;
		if (!TestTrue(
			FString::Printf(TEXT("%s Cue lane exists"), HostLabel),
			Lane.IsValid()))
		{
			return;
		}
		TestTrue(
			FString::Printf(TEXT("%s Cue lane binds the Add Cue context action"), HostLabel),
			Lane->HasAddCueContextHandlerForTests());

		const TArray<Paper2DPlusFrameCueTimeline::FPackedTrack> PackedTracks =
			Lane->GetPackedTracksForPresentation();
		float TargetY = 0.0f;
		bool bFoundTrack = false;
		for (const Paper2DPlusFrameCueTimeline::FPackedTrack& PackedTrack : PackedTracks)
		{
			const float TrackHeight = Lane->GetPackedTrackHeightForPresentation(PackedTrack);
			if (PackedTrack.TrackId == ExpectedTrackId)
			{
				TargetY += TrackHeight * 0.5f;
				bFoundTrack = true;
				break;
			}
			TargetY += TrackHeight;
		}
		if (!TestTrue(
			FString::Printf(TEXT("%s target track is visible"), HostLabel),
			bFoundTrack))
		{
			return;
		}

		int32 ContextFrame = INDEX_NONE;
		FGuid ContextTrack;
		const FVector2D EmptyTrackPoint(
			3.5f * SFrameEventTimelineTrack::ColumnWidth,
			TargetY);
		TestTrue(
			FString::Printf(TEXT("%s empty track point resolves an Add Cue target"), HostLabel),
			Lane->ResolveAddCueContextTargetForTests(
				EmptyTrackPoint, ContextFrame, ContextTrack));
		TestEqual(
			FString::Printf(TEXT("%s context action preserves the clicked frame"), HostLabel),
			ContextFrame,
			3);
		TestEqual(
			FString::Printf(TEXT("%s context action preserves the clicked track"), HostLabel),
			ContextTrack,
			ExpectedTrackId);
	};

	VerifyEmptyTrackContext(TEXT("Profile"), ProfileTimeline, TrackB);
	if (LayerContextTrack.IsValid())
	{
		VerifyEmptyTrackContext(TEXT("Layer"), LayerTimeline, LayerContextTrack);
	}

	LayerEditor->HandleHostDeactivated();
	ProfileEditor->HandleHostDeactivated();
	LayerCurves.Reset();
	ProfileCurves.Reset();
	LayerTimeline.Reset();
	ProfileTimeline.Reset();
	LayerEditor.Reset();
	ProfileEditor.Reset();
	LayerProvider.Reset();
	ProfileProvider.Reset();
	Fixture.Model.Reset();
	Fixture.Layer->RemoveFromRoot();
	Fixture.Profile->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusUnifiedCueTimelineTrackDispatchInvarianceTest,
	"Paper2DPlus.FrameCues.UnifiedTimeline.NamedTrackLayoutPreservesDispatchTrace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusUnifiedCueTimelineTrackDispatchInvarianceTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusUnifiedCueTimelineParityTest;
	if (!TestNotNull(TEXT("GEditor is available for named-track transactions"), GEditor))
	{
		return false;
	}

	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusUnifiedCueTimelineParityTest",
		"ResetTrackDispatchInvariance",
		"Reset Named Track Dispatch Invariance Test"));
	FFixture Fixture = MakeFixture(false);
	Fixture.Profile->AddToRoot();
	FFlipbookProfileEntry& Entry = Fixture.Profile->Flipbooks[0];

	UPaper2DPlusEditorTestMomentCue* MomentA = NewObject<UPaper2DPlusEditorTestMomentCue>(
		Fixture.Profile, NAME_None, RF_Transactional);
	MomentA->DebugName = TEXT("MomentA");
	MomentA->TriggerFrame = 2;
	UPaper2DPlusEditorTestRangeCue* RangeA = NewObject<UPaper2DPlusEditorTestRangeCue>(
		Fixture.Profile, NAME_None, RF_Transactional);
	RangeA->DebugName = TEXT("RangeA");
	RangeA->StartFrame = 1;
	RangeA->FrameCount = 3;
	RangeA->bEmitUpdates = true;
	UPaper2DPlusEditorTestMomentCue* MomentB = NewObject<UPaper2DPlusEditorTestMomentCue>(
		Fixture.Profile, NAME_None, RF_Transactional);
	MomentB->DebugName = TEXT("MomentB");
	MomentB->TriggerFrame = 2;
	UPaper2DPlusEditorTestRangeCue* RangeB = NewObject<UPaper2DPlusEditorTestRangeCue>(
		Fixture.Profile, NAME_None, RF_Transactional);
	RangeB->DebugName = TEXT("RangeB");
	RangeB->StartFrame = 2;
	RangeB->FrameCount = 2;
	RangeB->bEmitUpdates = true;
	Entry.FrameEventData.FrameCues = { MomentA, RangeA, MomentB, RangeB };
	const TArray<TObjectPtr<UPaper2DPlusCueBase>> AuthoredOrder = Entry.FrameEventData.FrameCues;

	int32 ActiveBefore = INDEX_NONE;
	const TArray<FString> TraceBefore = CaptureDispatchTrace(
		Entry.FrameEventData.FrameCues, ActiveBefore);
	TestEqual(TEXT("baseline lifecycle closes every overlapping Range"), ActiveBefore, 0);
	TestTrue(TEXT("baseline trace includes first same-frame Moment"),
		TraceContains(TraceBefore, TEXT("MomentA"), EPaper2DPlusFrameCuePhase::Trigger));
	TestTrue(TEXT("baseline trace includes second same-frame Moment"),
		TraceContains(TraceBefore, TEXT("MomentB"), EPaper2DPlusFrameCuePhase::Trigger));
	for (const TCHAR* RangeName : { TEXT("RangeA"), TEXT("RangeB") })
	{
		TestTrue(FString::Printf(TEXT("%s emits Begin"), RangeName),
			TraceContains(TraceBefore, RangeName, EPaper2DPlusFrameCuePhase::Begin));
		TestTrue(FString::Printf(TEXT("%s emits Update"), RangeName),
			TraceContains(TraceBefore, RangeName, EPaper2DPlusFrameCuePhase::Update));
		TestTrue(FString::Printf(TEXT("%s emits End"), RangeName),
			TraceContains(TraceBefore, RangeName, EPaper2DPlusFrameCuePhase::End));
	}

	FProfileFrameCueDataProvider Provider(Fixture.Model);
	const FProfileScopedAnimationIdentity Scope = Provider.GetScopedAnimationIdentity(0);
	const FGuid MomentsTrack = Provider.AddTrack(Scope, TEXT("Moments"));
	const FGuid RangesTrack = Provider.AddTrack(Scope, TEXT("Ranges"));
	TestTrue(TEXT("Moments named track is created"), MomentsTrack.IsValid());
	TestTrue(TEXT("Ranges named track is created"), RangesTrack.IsValid());
	TestTrue(TEXT("Moment A is assigned to Moments"),
		Provider.AssignCueToTrack(Provider.GetCueIdentity(Scope, MomentA), MomentsTrack));
	TestTrue(TEXT("Range A is assigned to Ranges"),
		Provider.AssignCueToTrack(Provider.GetCueIdentity(Scope, RangeA), RangesTrack));
	TestTrue(TEXT("Moment B is assigned to Moments"),
		Provider.AssignCueToTrack(Provider.GetCueIdentity(Scope, MomentB), MomentsTrack));
	TestTrue(TEXT("Range B is assigned to Ranges"),
		Provider.AssignCueToTrack(Provider.GetCueIdentity(Scope, RangeB), RangesTrack));
	TestTrue(TEXT("Ranges presentation track can be reordered first"),
		Provider.ReorderTrack(Scope, RangesTrack, 0));
	TestTrue(TEXT("same-frame Moment can be reassigned across named tracks"),
		Provider.AssignCueToTrack(Provider.GetCueIdentity(Scope, MomentB), RangesTrack));
	TestTrue(TEXT("overlapping Range can be reassigned across named tracks"),
		Provider.AssignCueToTrack(Provider.GetCueIdentity(Scope, RangeA), MomentsTrack));

	TestEqual(TEXT("track organization preserves authoritative Cue count"),
		Entry.FrameEventData.FrameCues.Num(), AuthoredOrder.Num());
	for (int32 CueIndex = 0; CueIndex < AuthoredOrder.Num(); ++CueIndex)
	{
		TestTrue(
			FString::Printf(TEXT("authoritative Cue %d remains in authored order"), CueIndex),
			Entry.FrameEventData.FrameCues.IsValidIndex(CueIndex)
				&& Entry.FrameEventData.FrameCues[CueIndex] == AuthoredOrder[CueIndex]);
	}
	const TArray<FPaper2DPlusFrameCueTrackDefinition> OrderedTracks =
		Provider.GetOptionalTracks(Scope);
	if (TestEqual(TEXT("two optional track definitions remain"), OrderedTracks.Num(), 2))
	{
		TestEqual(TEXT("reordered Ranges track is first"), OrderedTracks[0].TrackId, RangesTrack);
		TestEqual(TEXT("Moments track is second"), OrderedTracks[1].TrackId, MomentsTrack);
	}
	TestEqual(TEXT("Moment B resolves to its reassigned track"),
		Provider.ResolveCueTrackId(Scope, MomentB), RangesTrack);
	TestEqual(TEXT("Range A resolves to its reassigned track"),
		Provider.ResolveCueTrackId(Scope, RangeA), MomentsTrack);

	int32 ActiveAfter = INDEX_NONE;
	const TArray<FString> TraceAfter = CaptureDispatchTrace(
		Entry.FrameEventData.FrameCues, ActiveAfter);
	TestEqual(TEXT("organized lifecycle still closes every overlapping Range"), ActiveAfter, 0);
	if (TestEqual(TEXT("named-track organization preserves notification count"),
		TraceAfter.Num(), TraceBefore.Num()))
	{
		for (int32 NotificationIndex = 0; NotificationIndex < TraceBefore.Num(); ++NotificationIndex)
		{
			TestEqual(
				FString::Printf(
					TEXT("notification %d preserves Cue, phase, transition, reason, and compression"),
					NotificationIndex),
				TraceAfter[NotificationIndex],
				TraceBefore[NotificationIndex]);
		}
	}

	Fixture.Model.Reset();
	Fixture.Profile->RemoveFromRoot();
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusUnifiedCueTimelineParityTest",
		"EndTrackDispatchInvariance",
		"End Named Track Dispatch Invariance Test"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
