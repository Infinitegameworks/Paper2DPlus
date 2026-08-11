// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

#include "CharacterLayerBakeCore.h"
#include "CharacterProfileEditorModel.h"
#include "FrameCueDataProvider.h"
#include "Paper2DPlusEditorTestFrameCueTypes.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "PackageTools.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace Paper2DPlusFrameCueTrackLayoutTest
{
	struct FFixture
	{
		UPaper2DPlusCharacterProfileAsset* Profile = nullptr;
		UPaper2DPlusCharacterLayerAsset* LayerAsset = nullptr;
		TSharedPtr<FCharacterProfileEditorModel> Model;
	};

	FFixture MakeFixture()
	{
		FFixture Fixture;
		Fixture.Profile = NewObject<UPaper2DPlusCharacterProfileAsset>(
			GetTransientPackage(), NAME_None, RF_Transactional);
		FFlipbookProfileEntry& Entry = Fixture.Profile->Flipbooks.AddDefaulted_GetRef();
		Entry.Identity.FlipbookName = TEXT("attack");
		Entry.CombatData.Frames.SetNum(5);

		Fixture.LayerAsset = NewObject<UPaper2DPlusCharacterLayerAsset>(
			GetTransientPackage(), NAME_None, RF_Transactional);
		Fixture.LayerAsset->BaseProfile = Fixture.Profile;
		FCharacterLayer& Layer = Fixture.LayerAsset->Layers.AddDefaulted_GetRef();
		Layer.LayerId = FGuid::NewGuid();
		Layer.LayerName = TEXT("Weapon");

		Fixture.Model = MakeShared<FCharacterProfileEditorModel>();
		Fixture.Model->InitializeFromAsset(Fixture.Profile);
		Fixture.Model->SetSecondaryWatchedObject(Fixture.LayerAsset);
		Fixture.Model->SetSelectedFlipbook(0);
		Fixture.Model->SetSelectedLayerById(Layer.LayerId);
		return Fixture;
	}

	UPaper2DPlusEditorTestMomentCue* MakeCue(UObject* Outer, const TCHAR* Name, int32 Frame)
	{
		UPaper2DPlusEditorTestMomentCue* Cue = NewObject<UPaper2DPlusEditorTestMomentCue>(
			Outer, NAME_None, RF_Transactional);
		Cue->DebugName = Name;
		Cue->TriggerFrame = Frame;
		return Cue;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTrackLayoutSchemaTest,
	"Paper2DPlus.FrameCueTracks.EditorOnlySchemaUsesOneObjectKeyLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTrackLayoutSchemaTest::RunTest(const FString& Parameters)
{
	const FStructProperty* ProfileLayout = FindFProperty<FStructProperty>(
		FFlipbookFrameEventData::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FFlipbookFrameEventData, CueTrackLayout));
	const FStructProperty* BaselineLayout = FindFProperty<FStructProperty>(
		FPaper2DPlusCharacterBaselineAnimation::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FPaper2DPlusCharacterBaselineAnimation, CueTrackLayout));
	const FStructProperty* LayerLayout = FindFProperty<FStructProperty>(
		FCharacterLayerAuthoredAnimationData::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FCharacterLayerAuthoredAnimationData, CueTrackLayout));

	TestNotNull(TEXT("Profile Cue source exposes track layout"), ProfileLayout);
	TestNotNull(TEXT("baseline Cue source exposes the same track layout"), BaselineLayout);
	TestNotNull(TEXT("Layer authored Cue source exposes the same track layout"), LayerLayout);
	if (ProfileLayout && BaselineLayout && LayerLayout)
	{
		TestTrue(TEXT("all three sources use one reflected layout shape"),
			ProfileLayout->Struct == FPaper2DPlusFrameCueTrackLayout::StaticStruct()
			&& BaselineLayout->Struct == ProfileLayout->Struct
			&& LayerLayout->Struct == ProfileLayout->Struct);
		TestTrue(TEXT("Profile layout is editor-only"), ProfileLayout->HasAnyPropertyFlags(CPF_EditorOnly));
		TestTrue(TEXT("baseline layout is editor-only"), BaselineLayout->HasAnyPropertyFlags(CPF_EditorOnly));
		TestTrue(TEXT("Layer layout is editor-only"), LayerLayout->HasAnyPropertyFlags(CPF_EditorOnly));
	}

	const FMapProperty* Membership = FindFProperty<FMapProperty>(
		FPaper2DPlusFrameCueTrackLayout::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FPaper2DPlusFrameCueTrackLayout, CueTrackIds));
	TestNotNull(TEXT("selected identity shape is a reflected Cue-object membership map"), Membership);
	if (Membership)
	{
		const FObjectPropertyBase* KeyProperty = CastField<FObjectPropertyBase>(Membership->KeyProp);
		const FStructProperty* ValueProperty = CastField<FStructProperty>(Membership->ValueProp);
		TestTrue(TEXT("membership keys are Frame Cue object references"),
			KeyProperty && KeyProperty->PropertyClass->IsChildOf(UPaper2DPlusCueBase::StaticClass()));
		TestTrue(TEXT("membership values are stable track GUIDs"),
			ValueProperty && ValueProperty->Struct == TBaseStructure<FGuid>::Get());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTrackLayoutProviderTest,
	"Paper2DPlus.FrameCueTracks.ProviderCommandsPreserveCueAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTrackLayoutProviderTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTrackLayoutTest;
	if (!TestNotNull(TEXT("GEditor is available for provider transactions"), GEditor))
	{
		return false;
	}

	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusFrameCueTrackLayoutTest", "ResetProvider", "Reset Track Provider Test"));
	FFixture Fixture = MakeFixture();
	Fixture.Profile->AddToRoot();
	FProfileFrameCueDataProvider Provider(Fixture.Model);
	const FProfileScopedAnimationIdentity Scope = Provider.GetScopedAnimationIdentity(0);
	UPaper2DPlusEditorTestMomentCue* First = MakeCue(Fixture.Profile, TEXT("First"), 2);
	UPaper2DPlusEditorTestMomentCue* Second = MakeCue(Fixture.Profile, TEXT("Second"), 2);
	Fixture.Profile->Flipbooks[0].FrameEventData.FrameCues = { First, Second };

	const FString FirstDigest = CharacterLayerBakeCore::ComputeCueSemanticDigest(First);
	const FString SecondDigest = CharacterLayerBakeCore::ComputeCueSemanticDigest(Second);
	const TArray<TObjectPtr<UPaper2DPlusCueBase>> OriginalOrder =
		Fixture.Profile->Flipbooks[0].FrameEventData.FrameCues;
	FString JsonBeforeLayout;
	TestTrue(TEXT("Profile exports before track organization"),
		Fixture.Profile->ExportToJsonString(JsonBeforeLayout));
	Fixture.Profile->GetOutermost()->SetDirtyFlag(false);
	TestFalse(TEXT("an old asset projects every Cue to Default"),
		Provider.ResolveCueTrackId(Scope, First).IsValid());
	TestFalse(TEXT("read-only Default projection does not dirty the asset"),
		Fixture.Profile->GetOutermost()->IsDirty());

	const FGuid AudioTrack = Provider.AddTrack(Scope, TEXT("  Audio  "));
	const FGuid ImpactTrack = Provider.AddTrack(Scope, TEXT("Impact"));
	TestTrue(TEXT("first optional track receives a valid ID"), AudioTrack.IsValid());
	TestTrue(TEXT("second optional track receives a distinct valid ID"),
		ImpactTrack.IsValid() && ImpactTrack != AudioTrack);
	const TArray<FPaper2DPlusFrameCueTrackDefinition> Tracks = Provider.GetOptionalTracks(Scope);
	TestEqual(TEXT("track names are trimmed"), Tracks[0].DisplayName, FString(TEXT("Audio")));
	TestEqual(TEXT("default suggested optional name begins at Track 2"),
		Provider.SuggestTrackName(Scope), FString(TEXT("Track 2")));
	TestFalse(TEXT("case-insensitive duplicate track names are rejected"),
		Provider.AddTrack(Scope, TEXT("audio")).IsValid());

	const FFrameCueStableIdentity FirstIdentity = Provider.GetCueIdentity(Scope, First);
	TestTrue(TEXT("Cue assignment succeeds"), Provider.AssignCueToTrack(FirstIdentity, AudioTrack));
	TestEqual(TEXT("assigned Cue resolves to Audio"),
		Provider.ResolveCueTrackId(Scope, First), AudioTrack);
	TestTrue(TEXT("undo restores Default membership"), GEditor->UndoTransaction(true));
	TestFalse(TEXT("assignment undo projects the Cue to Default"),
		Provider.ResolveCueTrackId(Scope, First).IsValid());
	TestTrue(TEXT("redo restores the exact named membership"), GEditor->RedoTransaction());
	TestEqual(TEXT("assignment redo restores Audio"),
		Provider.ResolveCueTrackId(Scope, First), AudioTrack);
	FString JsonAfterLayout;
	TestTrue(TEXT("Profile exports after track organization"),
		Fixture.Profile->ExportToJsonString(JsonAfterLayout));
	TestEqual(TEXT("Character Profile JSON is byte-identical with or without track layout"),
		JsonAfterLayout, JsonBeforeLayout);
	TestFalse(TEXT("track layout field is absent from deterministic Profile JSON"),
		JsonAfterLayout.Contains(TEXT("CueTrackLayout"), ESearchCase::IgnoreCase));
	TestFalse(TEXT("unassigned Cue remains on Default"),
		Provider.ResolveCueTrackId(Scope, Second).IsValid());

	// Undo/redo transaction deserialization may rebuild the reflected Flipbooks array, so never retain
	// an interior FFlipbookProfileEntry reference across that boundary.
	Fixture.Profile->Flipbooks[0].FrameEventData.FrameCues.Swap(0, 1);
	TestEqual(TEXT("reordering the authoritative array cannot move object-key membership"),
		Provider.ResolveCueTrackId(Scope, First), AudioTrack);
	TestTrue(TEXT("track organization never reorders the authoritative Cue array"),
		Fixture.Profile->Flipbooks[0].FrameEventData.FrameCues[0] == OriginalOrder[1]
		&& Fixture.Profile->Flipbooks[0].FrameEventData.FrameCues[1] == OriginalOrder[0]);
	TestEqual(TEXT("track assignment does not enter the first Cue semantic digest"),
		CharacterLayerBakeCore::ComputeCueSemanticDigest(First), FirstDigest);
	TestEqual(TEXT("track assignment does not enter the second Cue semantic digest"),
		CharacterLayerBakeCore::ComputeCueSemanticDigest(Second), SecondDigest);

	const FFrameCueStableIdentity DuplicateIdentity = Provider.DuplicateCue(FirstIdentity);
	UPaper2DPlusCueBase* Duplicate = Provider.ResolveCue(DuplicateIdentity);
	TestNotNull(TEXT("provider duplicates the placement"), Duplicate);
	TestTrue(TEXT("duplicate has independent object identity"), Duplicate && Duplicate != First);
	TestEqual(TEXT("duplicate inherits source track membership"),
		Provider.ResolveCueTrackId(Scope, Duplicate), AudioTrack);

	const FFrameCueTrackUsage AudioUsage = Provider.GetTrackUsage(Scope, AudioTrack);
	TestEqual(TEXT("usage counts both active Audio placements"), AudioUsage.ActiveCueCount, 2);
	TestEqual(TEXT("usage has no stashed Cues yet"), AudioUsage.StashedCueCount, 0);

	TWeakObjectPtr<UPaper2DPlusCueBase> WeakDuplicate = Duplicate;
	TestTrue(TEXT("provider deletion removes the duplicated placement"),
		Provider.DeleteCue(DuplicateIdentity));
	TestFalse(TEXT("deleted placement no longer has a retained membership key"),
		Fixture.Profile->Flipbooks[0].FrameEventData.CueTrackLayout.CueTrackIds.Contains(Duplicate));
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusFrameCueTrackLayoutTest", "ForgetDelete", "Forget Track Delete"));
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestFalse(TEXT("membership does not retain a permanently deleted Cue through GC"),
		WeakDuplicate.IsValid());

	TestTrue(TEXT("track reorder changes definitions only"),
		Provider.ReorderTrack(Scope, ImpactTrack, 0));
	TestTrue(TEXT("track rename normalizes the new label"),
		Provider.RenameTrack(Scope, ImpactTrack, TEXT("  VFX  ")));
	TestEqual(TEXT("renamed/reordered track is first"),
		Provider.GetOptionalTracks(Scope)[0].DisplayName, FString(TEXT("VFX")));
	TestTrue(TEXT("moving a non-empty track to Default preserves its Cues"),
		Provider.RemoveTrack(
			Scope,
			AudioTrack,
			EPaper2DPlusFrameCueTrackRemovalMode::MoveCuesToDefault));
	TestTrue(TEXT("tracked Cue still exists after Move to Default"),
		Fixture.Profile->Flipbooks[0].FrameEventData.FrameCues.Contains(First));
	TestFalse(TEXT("moved Cue now resolves to Default"),
		Provider.ResolveCueTrackId(Scope, First).IsValid());

	Fixture.Profile->RemoveFromRoot();
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusFrameCueTrackLayoutTest", "EndProvider", "End Track Provider Test"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueMissingPlacementRecoveryTest,
	"Paper2DPlus.FrameCueTracks.MissingPlacementsAreCountedAndRemovedAtomically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueMissingPlacementRecoveryTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTrackLayoutTest;
	if (!TestNotNull(TEXT("GEditor is available for missing-placement transactions"), GEditor))
	{
		return false;
	}

	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusFrameCueTrackLayoutTest",
		"ResetMissingPlacementRecovery",
		"Reset Missing Frame Cue Placement Recovery Test"));
	FFixture Fixture = MakeFixture();
	Fixture.Profile->AddToRoot();
	FProfileFrameCueDataProvider Provider(Fixture.Model);
	const FProfileScopedAnimationIdentity Scope = Provider.GetScopedAnimationIdentity(0);
	UPaper2DPlusEditorTestMomentCue* First =
		MakeCue(Fixture.Profile, TEXT("FirstValid"), 1);
	UPaper2DPlusEditorTestMomentCue* Second =
		MakeCue(Fixture.Profile, TEXT("SecondValid"), 4);
	Fixture.Profile->Flipbooks[0].FrameEventData.FrameCues = {
		First,
		nullptr,
		Second,
		nullptr,
		nullptr
	};
	const FString FirstDigest = CharacterLayerBakeCore::ComputeCueSemanticDigest(First);
	const FString SecondDigest = CharacterLayerBakeCore::ComputeCueSemanticDigest(Second);
	const FGuid GameplayTrack = Provider.AddTrack(Scope, TEXT("Gameplay"));
	TestTrue(TEXT("valid Cue receives a named track before recovery"),
		Provider.AssignCueToTrack(Provider.GetCueIdentity(Scope, First), GameplayTrack));
	TestFalse(TEXT("second valid Cue remains on Default before recovery"),
		Provider.ResolveCueTrackId(Scope, Second).IsValid());

	// Isolate the recovery command so one Undo must restore its exact five-slot source shape.
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusFrameCueTrackLayoutTest",
		"BeginMissingPlacementRecovery",
		"Begin Missing Frame Cue Placement Recovery Test"));
	TestEqual(TEXT("provider counts only the three literal null slots"),
		Provider.GetMissingCuePlacementCount(Scope), 3);
	TestEqual(TEXT("one provider command removes every missing placement"),
		Provider.RemoveAllMissingCuePlacements(Scope), 3);

	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Repaired =
		Fixture.Profile->Flipbooks[0].FrameEventData.FrameCues;
	const bool bValidCuesSurvived =
		Repaired.Num() == 2 && Repaired[0] == First && Repaired[1] == Second;
	TestTrue(TEXT("valid Cue order and object identity survive recovery"),
		bValidCuesSurvived);
	if (bValidCuesSurvived)
	{
		TestEqual(TEXT("first valid Cue payload survives recovery"),
			CharacterLayerBakeCore::ComputeCueSemanticDigest(Repaired[0]), FirstDigest);
		TestEqual(TEXT("second valid Cue payload survives recovery"),
			CharacterLayerBakeCore::ComputeCueSemanticDigest(Repaired[1]), SecondDigest);
	}
	TestEqual(TEXT("named-track membership survives recovery"),
		Provider.ResolveCueTrackId(Scope, First), GameplayTrack);
	TestFalse(TEXT("Default-track membership survives recovery"),
		Provider.ResolveCueTrackId(Scope, Second).IsValid());
	TestEqual(TEXT("a second recovery command is a no-op"),
		Provider.RemoveAllMissingCuePlacements(Scope), 0);

	TestTrue(TEXT("one Undo restores the recovery transaction"),
		GEditor->UndoTransaction(true));
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Undone =
		Fixture.Profile->Flipbooks[0].FrameEventData.FrameCues;
	TestTrue(TEXT("Undo restores the exact five-slot null pattern"),
		Undone.Num() == 5
		&& Undone[0] == First
		&& Undone[1] == nullptr
		&& Undone[2] == Second
		&& Undone[3] == nullptr
		&& Undone[4] == nullptr);
	TestEqual(TEXT("Undo restores the missing-placement count"),
		Provider.GetMissingCuePlacementCount(Scope), 3);

	TestTrue(TEXT("one Redo reapplies the recovery transaction"),
		GEditor->RedoTransaction());
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Redone =
		Fixture.Profile->Flipbooks[0].FrameEventData.FrameCues;
	TestTrue(TEXT("Redo returns to the two valid placements in order"),
		Redone.Num() == 2 && Redone[0] == First && Redone[1] == Second);
	TestEqual(TEXT("Redo clears the missing-placement count"),
		Provider.GetMissingCuePlacementCount(Scope), 0);
	TestEqual(TEXT("Redo preserves the named-track membership"),
		Provider.ResolveCueTrackId(Scope, First), GameplayTrack);

	Fixture.Profile->RemoveFromRoot();
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusFrameCueTrackLayoutTest",
		"EndMissingPlacementRecovery",
		"End Missing Frame Cue Placement Recovery Test"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTrackLayoutReplacementAndSourceTest,
	"Paper2DPlus.FrameCueTracks.ObjectKeyDuplicateReplacementStashAndSourceParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTrackLayoutReplacementAndSourceTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTrackLayoutTest;
	if (!TestNotNull(TEXT("GEditor is available for provider transactions"), GEditor))
	{
		return false;
	}

	FFixture Fixture = MakeFixture();
	Fixture.Profile->AddToRoot();
	FProfileFrameCueDataProvider Provider(Fixture.Model);
	const FProfileScopedAnimationIdentity Scope = Provider.GetScopedAnimationIdentity(0);
	FFlipbookProfileEntry& Entry = Fixture.Profile->Flipbooks[0];
	UPaper2DPlusEditorTestMomentCue* Active = MakeCue(Fixture.Profile, TEXT("Active"), 1);
	UPaper2DPlusEditorTestMomentCue* Stashed = MakeCue(Fixture.Profile, TEXT("Stashed"), 3);
	Entry.FrameEventData.FrameCues.Add(Active);
	Entry.CombatData.ExcludedFrames.AddDefaulted_GetRef().StashedFrameCues.Add(Stashed);
	const FGuid TrackId = Provider.AddTrack(Scope, TEXT("Gameplay"));
	TestTrue(TEXT("active Cue assignment succeeds"),
		Provider.AssignCueToTrack(Provider.GetCueIdentity(Scope, Active), TrackId));
	TestTrue(TEXT("stashed Cue can retain the same source-layout membership"),
		Entry.FrameEventData.CueTrackLayout.AssignCue(Stashed, TrackId));
	const FFrameCueTrackUsage Usage = Provider.GetTrackUsage(Scope, TrackId);
	TestEqual(TEXT("active domain count"), Usage.ActiveCueCount, 1);
	TestEqual(TEXT("excluded/stashed domain count"), Usage.StashedCueCount, 1);

	UPaper2DPlusEditorTestMomentCue* Replacement = DuplicateObject<UPaper2DPlusEditorTestMomentCue>(Active, Fixture.Profile);
	FPaper2DPlusFrameCueReplacementMap Replacements;
	Replacements.Add(Active, Replacement);
	TArray<TObjectPtr<UPaper2DPlusCueBase>> ReplacementArray = { Replacement };
	TestTrue(TEXT("whole-array replacement and membership remap are one provider transaction"),
		Provider.ReplaceCueArrayAtomically(Scope, MoveTemp(ReplacementArray), Replacements));
	TestEqual(TEXT("compatible replacement keeps intended membership"),
		Provider.ResolveCueTrackId(Scope, Replacement), TrackId);
	TestFalse(TEXT("old replacement key is gone"),
		Entry.FrameEventData.CueTrackLayout.CueTrackIds.Contains(Active));

	UPaper2DPlusCharacterProfileAsset* DuplicateProfile =
		CastChecked<UPaper2DPlusCharacterProfileAsset>(StaticDuplicateObject(
			Fixture.Profile, GetTransientPackage(), NAME_None));
	DuplicateProfile->AddToRoot();
	TSharedPtr<FCharacterProfileEditorModel> DuplicateModel = MakeShared<FCharacterProfileEditorModel>();
	DuplicateModel->InitializeFromAsset(DuplicateProfile);
	FProfileFrameCueDataProvider DuplicateProvider(DuplicateModel);
	UPaper2DPlusCueBase* DuplicatedCue =
		DuplicateProfile->Flipbooks[0].FrameEventData.FrameCues[0];
	TestTrue(TEXT("whole-asset duplication deep-copies the Cue"), DuplicatedCue != Replacement);
	TestEqual(TEXT("preferred object-key map is automatically rekeyed by asset duplication"),
		DuplicateProvider.ResolveCueTrackId(
			DuplicateProvider.GetScopedAnimationIdentity(0), DuplicatedCue),
		TrackId);
	TestFalse(TEXT("duplicate layout retains no source-object key"),
		DuplicateProfile->Flipbooks[0].FrameEventData.CueTrackLayout.CueTrackIds.Contains(Replacement));
	UPaper2DPlusCueBase* DuplicatedStashedCue =
		DuplicateProfile->Flipbooks[0].CombatData.ExcludedFrames[0].StashedFrameCues[0];
	TestTrue(TEXT("whole-asset duplication deep-copies the stashed Cue"),
		DuplicatedStashedCue != Stashed);
	TestEqual(TEXT("nested excluded/stashed membership is rekeyed by asset duplication"),
		DuplicateProvider.ResolveCueTrackId(
			DuplicateProvider.GetScopedAnimationIdentity(0), DuplicatedStashedCue),
		TrackId);

	const FGuid LayerTrack = [&]()
	{
		FLayerFrameCueDataProvider LayerProvider(Fixture.LayerAsset, Fixture.Model);
		const FProfileScopedAnimationIdentity LayerScope = LayerProvider.GetScopedAnimationIdentity(0);
		return LayerProvider.AddTrack(LayerScope, TEXT("Layer Track"));
	}();
	TestTrue(TEXT("Layer source accepts optional tracks"), LayerTrack.IsValid());
	TestEqual(TEXT("adding the first Layer track creates exactly one authored source row"),
		Fixture.LayerAsset->Layers[0].AuthoredAnimations.Num(), 1);
	TestEqual(TEXT("Layer authored source stores the reusable layout shape"),
		Fixture.LayerAsset->Layers[0].AuthoredAnimations[0].CueTrackLayout.OptionalTracks.Num(), 1);

	const FGuid OwnerToken = FGuid::NewGuid();
	TestTrue(TEXT("runtime source captures into Character baseline"),
		Fixture.Profile->CaptureCharacterBaselineFromRuntime(OwnerToken, TEXT("/Game/Test/Layer"), true));
	TestEqual(TEXT("baseline captures optional definitions"),
		Fixture.Profile->CharacterBaseline[0].CueTrackLayout.OptionalTracks.Num(), 1);
	TestEqual(TEXT("baseline remaps membership to its duplicated Cue object"),
		Fixture.Profile->CharacterBaseline[0].CueTrackLayout.ResolveStoredTrackId(
			Fixture.Profile->CharacterBaseline[0].FrameCues[0]),
		TrackId);

	// Return authority to the original Profile row so removal can prove its complete active+stashed domain.
	Fixture.Profile->LayerBakeOwnerToken.Invalidate();
	TestTrue(TEXT("moving the track to Default includes active and stashed membership"),
		Provider.RemoveTrack(
			Scope,
			TrackId,
			EPaper2DPlusFrameCueTrackRemovalMode::MoveCuesToDefault));
	TestTrue(TEXT("active replacement remains"),
		Entry.FrameEventData.FrameCues.Contains(Replacement));
	TestTrue(TEXT("stashed Cue remains"),
		Entry.CombatData.ExcludedFrames[0].StashedFrameCues.Contains(Stashed));

	DuplicateProfile->RemoveFromRoot();
	Fixture.Profile->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTrackLayoutSaveLoadTest,
	"Paper2DPlus.FrameCueTracks.ObjectKeyMapSaveLoadRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTrackLayoutSaveLoadTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTrackLayoutTest;
	const FGuid FixtureGuid = FGuid::NewGuid();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	const FString GuidString = FixtureGuid.ToString(EGuidFormats::Digits).ToLower();
#else
	const FString GuidString = FixtureGuid.ToString(EGuidFormats::DigitsLower);
#endif
	const FString PackageName = FString::Printf(
		TEXT("/Game/__AutomationTemp__/P2DPTrackLayout_%s/TrackLayoutPackage"),
		*GuidString);
	const FString FilePath = FPackageName::LongPackageNameToFilename(
		PackageName, FPackageName::GetAssetPackageExtension());
	const FString TempDirectory = FPaths::GetPath(FilePath);
	IFileManager::Get().MakeDirectory(*TempDirectory, true);

	FGuid TrackId;
	{
		UPackage* Package = CreatePackage(*PackageName);
		if (!TestNotNull(TEXT("fixture package created"), Package)) return false;
		Package->AddToRoot();
		UPaper2DPlusCharacterProfileAsset* Profile =
			NewObject<UPaper2DPlusCharacterProfileAsset>(
				Package,
				TEXT("TrackLayoutAsset"),
				RF_Public | RF_Standalone | RF_Transactional);
		FFlipbookProfileEntry& Entry = Profile->Flipbooks.AddDefaulted_GetRef();
		Entry.Identity.FlipbookName = TEXT("save_load");
		UPaper2DPlusEditorTestMomentCue* Cue = MakeCue(Profile, TEXT("SavedCue"), 4);
		Entry.FrameEventData.FrameCues.Add(Cue);
		TestTrue(TEXT("fixture track added"),
			Entry.FrameEventData.CueTrackLayout.AddTrack(TEXT("Audio"), FGuid(), TrackId));
		TestTrue(TEXT("fixture membership assigned"),
			Entry.FrameEventData.CueTrackLayout.AssignCue(Cue, TrackId));

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Package, Profile, *FilePath, SaveArgs);
		Package->RemoveFromRoot();
		if (!TestTrue(TEXT("track-layout fixture saved"), bSaved)) return false;
	}

	if (UPackage* Resident = FindPackage(nullptr, *PackageName))
	{
		Resident->SetDirtyFlag(false);
		TArray<UPackage*> PackagesToUnload = { Resident };
		FText UnloadError;
		if (!TestTrue(FString::Printf(TEXT("fixture unloaded before reload%s%s"),
			UnloadError.IsEmpty() ? TEXT("") : TEXT(": "),
			UnloadError.IsEmpty() ? TEXT("") : *UnloadError.ToString()),
			UPackageTools::UnloadPackages(PackagesToUnload, UnloadError, true)))
		{
			return false;
		}
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	UPackage* LoadedPackage = LoadPackage(nullptr, *PackageName, LOAD_None);
	UPaper2DPlusCharacterProfileAsset* Loaded = LoadedPackage
		? FindObject<UPaper2DPlusCharacterProfileAsset>(LoadedPackage, TEXT("TrackLayoutAsset"))
		: nullptr;
	if (TestNotNull(TEXT("saved Profile reloaded"), Loaded)
		&& TestEqual(TEXT("reloaded Cue count"),
			Loaded->Flipbooks[0].FrameEventData.FrameCues.Num(), 1))
	{
		UPaper2DPlusCueBase* LoadedCue = Loaded->Flipbooks[0].FrameEventData.FrameCues[0];
		TestEqual(TEXT("object-key membership resolves after linker round trip"),
			Loaded->Flipbooks[0].FrameEventData.CueTrackLayout.ResolveStoredTrackId(LoadedCue),
			TrackId);
		TestTrue(TEXT("reloaded membership map is keyed by the reloaded Cue object"),
			Loaded->Flipbooks[0].FrameEventData.CueTrackLayout.CueTrackIds.Contains(LoadedCue));
	}

	if (LoadedPackage)
	{
		LoadedPackage->SetDirtyFlag(false);
		TArray<UPackage*> PackagesToUnload = { LoadedPackage };
		FText Ignore;
		UPackageTools::UnloadPackages(PackagesToUnload, Ignore, true);
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	IFileManager& FileManager = IFileManager::Get();
	for (const FString& PackageFile : {
		FilePath,
		FPaths::ChangeExtension(FilePath, TEXT("uexp")),
		FPaths::ChangeExtension(FilePath, TEXT("ubulk")),
		FPaths::ChangeExtension(FilePath, TEXT("uptnl")) })
	{
		if (FileManager.FileExists(*PackageFile))
		{
			FileManager.Delete(*PackageFile, false, true, true);
		}
	}
	if (FileManager.DirectoryExists(*TempDirectory))
	{
		FileManager.DeleteDirectory(*TempDirectory, false, false);
	}
	TestFalse(TEXT("fixture directory cleaned"), FileManager.DirectoryExists(*TempDirectory));
	return true;
}

#endif // WITH_EDITOR
