// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CharacterProfileJsonInteraction.h"
#include "FrameCueTrackLayoutDiagnostics.h"
#include "Paper2DPlusEditorTestFrameCueTypes.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusValidationService.h"

namespace Paper2DPlusFrameCueTrackLayoutDiagnosticsTest
{
	using namespace Paper2DPlusFrameCueTrackLayoutDiagnostics;

	UPaper2DPlusEditorTestMomentCue* NewCue(UObject* Outer, const TCHAR* DebugName, int32 Frame = 0)
	{
		UPaper2DPlusEditorTestMomentCue* Cue = NewObject<UPaper2DPlusEditorTestMomentCue>(Outer);
		Cue->DebugName = DebugName;
		Cue->TriggerFrame = Frame;
		return Cue;
	}

	FPaper2DPlusFrameCueTrackDefinition MakeTrack(const FGuid& TrackId, const TCHAR* Name)
	{
		FPaper2DPlusFrameCueTrackDefinition Track;
		Track.TrackId = TrackId;
		Track.DisplayName = Name;
		return Track;
	}

	bool HasIssue(
		const TArray<FIssue>& Issues,
		EIssueKind Kind,
		const TCHAR* DomainFragment = nullptr,
		const TCHAR* ItemFragment = nullptr)
	{
		return Issues.ContainsByPredicate([=](const FIssue& Issue)
		{
			return Issue.Kind == Kind
				&& (!DomainFragment || Issue.DomainIdentity.Contains(DomainFragment))
				&& (!ItemFragment || Issue.ItemIdentity.Contains(ItemFragment));
		});
	}

	bool HasProjectedCode(
		const TArray<FPaper2DPlusValidationIssue>& Issues,
		EIssueKind Kind)
	{
		const FName Code = GetIssueCode(Kind);
		return Issues.ContainsByPredicate([Code](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Code == Code;
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTrackLayoutDiagnosticsProfileTest,
	"Paper2DPlus.FrameCueTracks.Diagnostics.ProfileActiveStashedBaselineReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTrackLayoutDiagnosticsProfileTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTrackLayoutDiagnosticsTest;
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	Profile->Flipbooks.SetNum(2);
	FFlipbookProfileEntry& First = Profile->Flipbooks[0];
	First.Identity.FlipbookName = TEXT("first");
	FFlipbookProfileEntry& Second = Profile->Flipbooks[1];
	Second.Identity.FlipbookName = TEXT("second");

	UPaper2DPlusEditorTestMomentCue* Duplicate = NewCue(Profile, TEXT("Duplicate"));
	UPaper2DPlusEditorTestMomentCue* Stashed = NewCue(Profile, TEXT("Stashed"));
	UPaper2DPlusEditorTestMomentCue* OtherAnimation = NewCue(Profile, TEXT("OtherAnimation"));
	UPaper2DPlusEditorTestMomentCue* Orphan = NewCue(Profile, TEXT("Orphan"));
	UPaper2DPlusEditorTestMomentCue* WrongOuter = NewCue(GetTransientPackage(), TEXT("WrongOuter"));
	First.FrameEventData.FrameCues = { Duplicate, Duplicate, WrongOuter };
	First.CombatData.ExcludedFrames.AddDefaulted_GetRef().StashedFrameCues.Add(Stashed);
	Second.FrameEventData.FrameCues.Add(OtherAnimation);

	const FGuid StableTrack = FGuid::NewGuid();
	const FGuid AmbiguousTrack = FGuid::NewGuid();
	const FGuid DuplicateNameTrack = FGuid::NewGuid();
	First.FrameEventData.CueTrackLayout.OptionalTracks = {
		MakeTrack(StableTrack, TEXT("Gameplay")),
		MakeTrack(DuplicateNameTrack, TEXT("gameplay")),
		MakeTrack(AmbiguousTrack, TEXT("Audio")),
		MakeTrack(AmbiguousTrack, TEXT("Audio Duplicate")),
		MakeTrack(FGuid(), TEXT("   "))
	};
	First.FrameEventData.CueTrackLayout.CueTrackIds.Add(Duplicate, AmbiguousTrack);
	First.FrameEventData.CueTrackLayout.CueTrackIds.Add(Stashed, StableTrack);
	First.FrameEventData.CueTrackLayout.CueTrackIds.Add(OtherAnimation, StableTrack);
	First.FrameEventData.CueTrackLayout.CueTrackIds.Add(Orphan, StableTrack);
	First.FrameEventData.CueTrackLayout.CueTrackIds.Add(WrongOuter, FGuid());
	First.FrameEventData.CueTrackLayout.CueTrackIds.Add(
		TObjectPtr<UPaper2DPlusCueBase>(), StableTrack);

	FPaper2DPlusCharacterBaselineAnimation& Baseline = Profile->CharacterBaseline.AddDefaulted_GetRef();
	Baseline.LegacyAnimationName = TEXT("baseline");
	UPaper2DPlusEditorTestMomentCue* BaselineCue = NewCue(Profile, TEXT("BaselineCue"));
	Baseline.FrameCues.Add(BaselineCue);
	Baseline.CueTrackLayout.OptionalTracks.Add(MakeTrack(FGuid::NewGuid(), TEXT("Baseline")));
	Baseline.CueTrackLayout.CueTrackIds.Add(BaselineCue, FGuid::NewGuid());

	Profile->GetOutermost()->SetDirtyFlag(false);
	const int32 TrackCountBefore = First.FrameEventData.CueTrackLayout.OptionalTracks.Num();
	const int32 MembershipCountBefore = First.FrameEventData.CueTrackLayout.CueTrackIds.Num();
	TArray<FIssue> Issues;
	AnalyzeCharacterProfile(*Profile, Issues);

	TestTrue(TEXT("duplicate pointers in one active/stashed domain are diagnosed"),
		HasIssue(Issues, EIssueKind::DuplicateCueInDomain, TEXT("Profile[0]"), TEXT("Duplicate")));
	TestTrue(TEXT("wrong-outer Cue is diagnosed"),
		HasIssue(Issues, EIssueKind::WrongCueOuter, TEXT("Profile[0]"), TEXT("WrongOuter")));
	TestTrue(TEXT("duplicate track GUID is diagnosed"),
		HasIssue(Issues, EIssueKind::DuplicateTrackId, TEXT("Profile[0]")));
	TestTrue(TEXT("invalid track GUID is diagnosed"),
		HasIssue(Issues, EIssueKind::InvalidTrackId, TEXT("Profile[0]")));
	TestTrue(TEXT("case-insensitive duplicate names are diagnosed"),
		HasIssue(Issues, EIssueKind::DuplicateTrackName, TEXT("Profile[0]")));
	TestTrue(TEXT("null membership Cue is diagnosed"),
		HasIssue(Issues, EIssueKind::NullMembershipCue, TEXT("Profile[0]")));
	TestTrue(TEXT("cross-animation membership is diagnosed"),
		HasIssue(Issues, EIssueKind::CrossDomainMembership, TEXT("Profile[0]"), TEXT("OtherAnimation")));
	TestTrue(TEXT("stale assignment outside the complete domain is diagnosed"),
		HasIssue(Issues, EIssueKind::AssignmentOutsideDomain, TEXT("Profile[0]"), TEXT("Orphan")));
	TestTrue(TEXT("baseline layout participates in the same analyzer"),
		HasIssue(Issues, EIssueKind::UnknownMembershipTrackId, TEXT("Baseline[0]"), TEXT("BaselineCue")));
	TestFalse(TEXT("a valid stashed membership is part of the owning domain, not stale"),
		HasIssue(Issues, EIssueKind::AssignmentOutsideDomain, TEXT("Profile[0]"), TEXT("Stashed")));
	TestFalse(TEXT("ambiguous stored track projects safely to Default"),
		First.FrameEventData.CueTrackLayout.ResolveStoredTrackId(Duplicate).IsValid());
	TestEqual(TEXT("diagnostics do not prune track definitions"),
		First.FrameEventData.CueTrackLayout.OptionalTracks.Num(), TrackCountBefore);
	TestEqual(TEXT("diagnostics do not prune membership"),
		First.FrameEventData.CueTrackLayout.CueTrackIds.Num(), MembershipCountBefore);
	TestFalse(TEXT("read-only diagnostics do not dirty the asset"), Profile->GetOutermost()->IsDirty());

	TArray<FPaper2DPlusValidationIssue> Projected;
	FPaper2DPlusValidationService::Get().ValidateObject(Profile, Projected);
	TestTrue(TEXT("shared validation projects named-track diagnostics"),
		HasProjectedCode(Projected, EIssueKind::CrossDomainMembership));
	TestFalse(TEXT("validation projection also remains read-only"), Profile->GetOutermost()->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTrackLayoutDiagnosticsLayerTest,
	"Paper2DPlus.FrameCueTracks.Diagnostics.LayerAuthoredOnlyReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTrackLayoutDiagnosticsLayerTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTrackLayoutDiagnosticsTest;
	UPaper2DPlusCharacterLayerAsset* Asset = NewObject<UPaper2DPlusCharacterLayerAsset>();
	FCharacterLayer& Layer = Asset->Layers.AddDefaulted_GetRef();
	Layer.LayerId = FGuid::NewGuid();
	Layer.LayerName = TEXT("Weapon");
	Layer.AuthoredAnimations.SetNum(2);
	FCharacterLayerAuthoredAnimationData& First = Layer.AuthoredAnimations[0];
	First.LegacyAnimationName = TEXT("first");
	FCharacterLayerAuthoredAnimationData& Second = Layer.AuthoredAnimations[1];
	Second.LegacyAnimationName = TEXT("second");
	UPaper2DPlusEditorTestMomentCue* FirstCue = NewCue(Asset, TEXT("LayerFirst"));
	UPaper2DPlusEditorTestMomentCue* SecondCue = NewCue(Asset, TEXT("LayerSecond"));
	First.FrameCues.Add(FirstCue);
	Second.FrameCues.Add(SecondCue);
	const FGuid TrackId = FGuid::NewGuid();
	First.CueTrackLayout.OptionalTracks.Add(MakeTrack(TrackId, TEXT("Gameplay")));
	First.CueTrackLayout.CueTrackIds.Add(SecondCue, TrackId);

	FCharacterLayerAuthoredAnimationData& Cooked = Layer.CookedGameplayAnimations.AddDefaulted_GetRef();
	Cooked.LegacyAnimationName = TEXT("CookedOnly");
	Cooked.CueTrackLayout.OptionalTracks.Add(MakeTrack(FGuid(), TEXT(" CookedOnly ")));

	Asset->GetOutermost()->SetDirtyFlag(false);
	TArray<FIssue> Issues;
	AnalyzeCharacterLayer(*Asset, Issues);
	TestTrue(TEXT("cross-animation authored membership is diagnosed"),
		HasIssue(Issues, EIssueKind::CrossDomainMembership, TEXT("Authored[0]"), TEXT("LayerSecond")));
	TestFalse(TEXT("CookedGameplayAnimations is never treated as a layout-authoring domain"),
		Issues.ContainsByPredicate([](const FIssue& Issue)
		{
			return Issue.DomainIdentity.Contains(TEXT("CookedOnly"))
				|| Issue.ItemIdentity.Contains(TEXT("CookedOnly"));
		}));
	TestFalse(TEXT("Layer diagnostics do not dirty the asset"), Asset->GetOutermost()->IsDirty());

	TArray<FPaper2DPlusValidationIssue> Projected;
	FPaper2DPlusValidationService::Get().ValidateObject(Asset, Projected);
	TestTrue(TEXT("Layer shared validation projects track diagnostics"),
		HasProjectedCode(Projected, EIssueKind::CrossDomainMembership));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTrackLayoutJsonDisclosureTest,
	"Paper2DPlus.FrameCueTracks.Json.LayoutNeutralDisclosureApplyCancelAndWarning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTrackLayoutJsonDisclosureTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTrackLayoutDiagnosticsTest;
	using namespace Paper2DPlusCharacterProfileJsonInteraction;

	UPaper2DPlusCharacterProfileAsset* Source = NewObject<UPaper2DPlusCharacterProfileAsset>();
	Source->DisplayName = TEXT("Imported Profile");
	FFlipbookProfileEntry& SourceEntry = Source->Flipbooks.AddDefaulted_GetRef();
	SourceEntry.Identity.FlipbookName = TEXT("attack");
	UPaper2DPlusEditorTestMomentCue* SourceCue = NewCue(Source, TEXT("ImportedCue"), 2);
	SourceEntry.FrameEventData.FrameCues.Add(SourceCue);
	const FGuid SourceTrackId = FGuid::NewGuid();
	SourceEntry.FrameEventData.CueTrackLayout.OptionalTracks.Add(
		MakeTrack(SourceTrackId, TEXT("Audio")));
	SourceEntry.FrameEventData.CueTrackLayout.CueTrackIds.Add(SourceCue, SourceTrackId);

	FString Json;
	TestTrue(TEXT("source exports deterministic JSON"), Source->ExportToJsonString(Json));
	TestFalse(TEXT("exported JSON excludes the editor-only layout field"),
		Json.Contains(TEXT("CueTrackLayout"), ESearchCase::IgnoreCase));
	const FString ExportCopy = GetExportLayoutDisclosure().ToString();
	TestTrue(TEXT("interactive export copy says track organization is excluded"),
		ExportCopy.Contains(TEXT("track names, order, and membership"), ESearchCase::IgnoreCase)
		&& ExportCopy.Contains(TEXT("excluded"), ESearchCase::IgnoreCase));
	const FString ImportCopy = BuildImportLayoutPreview(TEXT("C:/Temp/Profile.json")).ToString();
	TestTrue(TEXT("interactive import preview names Default and Cancel"),
		ImportCopy.Contains(TEXT("Default"))
		&& ImportCopy.Contains(TEXT("Cancel"))
		&& ImportCopy.Contains(TEXT("track names, order, and membership"), ESearchCase::IgnoreCase));

	UPaper2DPlusCharacterProfileAsset* Target = NewObject<UPaper2DPlusCharacterProfileAsset>();
	Target->DisplayName = TEXT("Before Cancel");
	FFlipbookProfileEntry& ExistingEntry = Target->Flipbooks.AddDefaulted_GetRef();
	ExistingEntry.Identity.FlipbookName = TEXT("existing");
	UPaper2DPlusEditorTestMomentCue* ExistingCue = NewCue(Target, TEXT("ExistingCue"));
	ExistingEntry.FrameEventData.FrameCues.Add(ExistingCue);
	const FGuid ExistingTrackId = FGuid::NewGuid();
	ExistingEntry.FrameEventData.CueTrackLayout.OptionalTracks.Add(
		MakeTrack(ExistingTrackId, TEXT("Keep On Cancel")));
	ExistingEntry.FrameEventData.CueTrackLayout.CueTrackIds.Add(ExistingCue, ExistingTrackId);
	Target->GetOutermost()->SetDirtyFlag(false);

	TArray<FPaper2DPlusCharacterProfileJsonImportWarning> Warnings;
	TestEqual(TEXT("Cancel returns an explicit terminal result"),
		ApplyImportString(*Target, Json, EAppReturnType::Cancel, Warnings),
		EImportResult::Cancelled);
	TestEqual(TEXT("Cancel preserves semantic data"), Target->DisplayName, FString(TEXT("Before Cancel")));
	TestTrue(TEXT("Cancel preserves the exact Cue object"),
		Target->Flipbooks[0].FrameEventData.FrameCues.Contains(ExistingCue));
	TestEqual(TEXT("Cancel preserves named-track organization"),
		Target->Flipbooks[0].FrameEventData.CueTrackLayout.ResolveStoredTrackId(ExistingCue),
		ExistingTrackId);
	TestTrue(TEXT("Cancel returns no import warning because nothing was applied"), Warnings.IsEmpty());
	TestFalse(TEXT("Cancel does not dirty the asset"), Target->GetOutermost()->IsDirty());

	TestEqual(TEXT("OK is the Apply boundary"),
		ApplyImportString(*Target, Json, EAppReturnType::Ok, Warnings),
		EImportResult::Applied);
	TestEqual(TEXT("Apply imports semantic data"), Target->DisplayName, Source->DisplayName);
	TestEqual(TEXT("Apply returns one structured layout-reset advisory"), Warnings.Num(), 1);
	if (!Warnings.IsEmpty())
	{
		TestEqual(TEXT("structured warning has the stable code"), Warnings[0].Code,
			Paper2DPlusCharacterProfileJson::GetTrackLayoutResetWarningCode());
	}
	TestEqual(TEXT("imported Cue count"), Target->Flipbooks[0].FrameEventData.FrameCues.Num(), 1);
	TestTrue(TEXT("imported Cues use Default"),
		Target->Flipbooks[0].FrameEventData.CueTrackLayout.OptionalTracks.IsEmpty()
		&& Target->Flipbooks[0].FrameEventData.CueTrackLayout.CueTrackIds.IsEmpty());

	UPaper2DPlusCharacterProfileAsset* NonInteractive = NewObject<UPaper2DPlusCharacterProfileAsset>();
	AddExpectedError(
		TEXT("Paper2DPlus.CharacterProfile.Json.TrackLayoutReset"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	TestTrue(TEXT("legacy/programmatic import emits the structured warning without a modal"),
		NonInteractive->ImportFromJsonString(Json));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
