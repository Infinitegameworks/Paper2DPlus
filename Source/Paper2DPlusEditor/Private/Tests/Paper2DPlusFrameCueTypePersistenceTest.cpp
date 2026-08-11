// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "FrameCues/K2Node_ListenForFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueDispatch.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PackageTools.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace Paper2DPlusFrameCueTypePersistenceTest
{
	struct FScopedFixturePackage
	{
		FString PackageName;
		FString FilePath;

		FScopedFixturePackage()
		{
			const FGuid Guid = FGuid::NewGuid();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
			const FString GuidString = Guid.ToString(EGuidFormats::Digits).ToLower();
#else
			const FString GuidString = Guid.ToString(EGuidFormats::DigitsLower);
#endif
			PackageName = FString::Printf(
				TEXT("/Game/__AutomationTemp__/P2DPFrameCueTypePersistence_%s"),
				*GuidString);
			FilePath = FPackageName::LongPackageNameToFilename(
				PackageName,
				FPackageName::GetAssetPackageExtension());
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
		}

		~FScopedFixturePackage()
		{
			FText IgnoredError;
			Unload(IgnoredError);
			DeleteFiles();
		}

		bool Unload(FText& OutError) const
		{
			UPackage* Package = FindPackage(nullptr, *PackageName);
			if (!Package)
			{
				return true;
			}
			if (Package->IsRooted())
			{
				Package->RemoveFromRoot();
			}
			Package->SetDirtyFlag(false);
			TArray<UPackage*> PackagesToUnload{Package};
			const bool bUnloaded =
				UPackageTools::UnloadPackages(PackagesToUnload, OutError, true);
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			return bUnloaded && FindPackage(nullptr, *PackageName) == nullptr;
		}

		bool Cleanup(FText& OutError) const
		{
			const bool bUnloaded = Unload(OutError);
			DeleteFiles();
			return bUnloaded && !IFileManager::Get().FileExists(*FilePath);
		}

		void DeleteFiles() const
		{
			IFileManager& FileManager = IFileManager::Get();
			FileManager.Delete(*FilePath, false, true, true);
			FileManager.Delete(*FPaths::ChangeExtension(FilePath, TEXT("uexp")), false, true, true);
			FileManager.Delete(*FPaths::ChangeExtension(FilePath, TEXT("ubulk")), false, true, true);
			FileManager.Delete(*FPaths::ChangeExtension(FilePath, TEXT("uptnl")), false, true, true);
			FileManager.DeleteDirectory(*FPaths::GetPath(FilePath), false, false);
		}
	};

	UK2Node_CallFunction* AddDeferredTimerCall(
		UPaper2DPlusFrameCueBlueprint& Blueprint)
	{
		UEdGraph* Graph =
			FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(Blueprint);
		if (!Graph)
		{
			return nullptr;
		}
		FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
		UK2Node_CallFunction* Node = Creator.CreateNode(false);
		Node->FunctionReference.SetExternalMember(
			GET_FUNCTION_NAME_CHECKED(
				UKismetSystemLibrary,
				K2_SetTimerForNextTick),
			UKismetSystemLibrary::StaticClass());
		Creator.Finalize();
		return Node;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeSavedPlacementLifecycleTest,
	"Paper2DPlus.FrameCues.CueType.Persistence.SavedPlacementPayloadAndLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeSavedPlacementLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypePersistenceTest;
	FScopedFixturePackage Fixture;
	const FName CueTypeName(TEXT("BP_PersistentRangeCue"));
	const FName ProfileName(TEXT("PersistentCueProfile"));
	const FName PayloadName(TEXT("Volume"));
	constexpr float AuthoredVolume = 0.8f;

	UPackage* Package = CreatePackage(*Fixture.PackageName);
	if (!TestNotNull(TEXT("Temporary Cue Type package was created"), Package))
	{
		return false;
	}
	Package->AddToRoot();

	FPaper2DPlusFrameCueTypeCreateRequest CreateRequest;
	CreateRequest.Package = Package;
	CreateRequest.AssetName = CueTypeName;
	CreateRequest.Kind = EPaper2DPlusFrameCueTypeKind::Range;
	const FPaper2DPlusFrameCueTypeCreateResult Created =
		FPaper2DPlusFrameCueTypeAuthoring::CreateCueType(CreateRequest);
	UPaper2DPlusFrameCueBlueprint* CueType = Created.CueType;
	if (!TestTrue(TEXT("Specialized Cue State Type was created"), Created.IsSuccess())
		|| !TestNotNull(TEXT("Created Cue Type asset is valid"), CueType))
	{
		Package->RemoveFromRoot();
		return false;
	}

	FEdGraphPinType FloatType;
	FloatType.PinCategory = UEdGraphSchema_K2::PC_Real;
	FloatType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	if (!TestTrue(
			TEXT("Payload field was added"),
			FBlueprintEditorUtils::AddMemberVariable(
				CueType,
				PayloadName,
				FloatType,
				TEXT("0.5"))))
	{
		Package->RemoveFromRoot();
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(CueType);
	if (!TestTrue(
			TEXT("Cue Type recompiles after adding payload"),
			CueType->Status == BS_UpToDate || CueType->Status == BS_UpToDateWithWarnings))
	{
		Package->RemoveFromRoot();
		return false;
	}

	const FPaper2DPlusFrameCueDurableSchemaCandidate DurableCandidate =
		FPaper2DPlusFrameCueTypeAuthoring::PrepareDurableSchemaSaveCandidate(*CueType);
	if (!TestTrue(TEXT("Current schema is eligible for durable save"), DurableCandidate.bSuccess))
	{
		AddError(DurableCandidate.Error.ToString());
		Package->RemoveFromRoot();
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(
			Package,
			ProfileName,
			RF_Public | RF_Standalone | RF_Transactional);
	FFlipbookProfileEntry& Entry = Profile->Flipbooks.AddDefaulted_GetRef();
	Entry.Identity.FlipbookName = TEXT("Attack");
	Entry.CombatData.Frames.SetNum(4);
	UPaper2DPlusCueState* Placement = Cast<UPaper2DPlusCueState>(
		NewObject<UPaper2DPlusCueBase>(
			Profile,
			CueType->GeneratedClass,
			TEXT("PersistentRangePlacement"),
			RF_Transactional));
	if (!TestNotNull(TEXT("Custom Range placement was created"), Placement))
	{
		Package->RemoveFromRoot();
		return false;
	}
	Placement->StartFrame = 1;
	Placement->FrameCount = 2;
	FFloatProperty* VolumeProperty =
		FindFProperty<FFloatProperty>(Placement->GetClass(), PayloadName);
	if (!TestNotNull(TEXT("Compiled payload property exists"), VolumeProperty))
	{
		Package->RemoveFromRoot();
		return false;
	}
	VolumeProperty->SetPropertyValue_InContainer(Placement, AuthoredVolume);
	Entry.FrameEventData.FrameCues.Add(Placement);
	Package->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const bool bSaved = UPackage::SavePackage(
		Package,
		Profile,
		*Fixture.FilePath,
		SaveArgs);
	Package->RemoveFromRoot();
	if (!TestTrue(TEXT("Specialized Cue Type and placement save to disk"), bSaved))
	{
		return false;
	}

	FText UnloadError;
	if (!TestTrue(TEXT("Fixture unloads before reload"), Fixture.Unload(UnloadError)))
	{
		AddError(UnloadError.ToString());
		return false;
	}
	CueType = nullptr;
	Profile = nullptr;
	Placement = nullptr;
	VolumeProperty = nullptr;

	UPackage* LoadedPackage = LoadPackage(nullptr, *Fixture.PackageName, LOAD_None);
	if (!TestNotNull(TEXT("Saved Cue Type fixture reloads"), LoadedPackage))
	{
		return false;
	}
	UPaper2DPlusFrameCueBlueprint* LoadedCueType =
		FindObject<UPaper2DPlusFrameCueBlueprint>(LoadedPackage, *CueTypeName.ToString());
	UPaper2DPlusCharacterProfileAsset* LoadedProfile =
		FindObject<UPaper2DPlusCharacterProfileAsset>(LoadedPackage, *ProfileName.ToString());
	if (!TestNotNull(TEXT("Specialized Cue Type survives reload"), LoadedCueType)
		|| !TestNotNull(TEXT("Profile survives reload"), LoadedProfile))
	{
		return false;
	}

	TestEqual(TEXT("Specialized Cue Type keeps one permitted behavior event graph"),
		LoadedCueType->UbergraphPages.Num(), 1);
	TestEqual(TEXT("Specialized Cue Type owns no function graphs"),
		LoadedCueType->FunctionGraphs.Num(), 0);
	TestEqual(TEXT("One animation survives"), LoadedProfile->Flipbooks.Num(), 1);
	if (LoadedProfile->Flipbooks.Num() != 1)
	{
		return false;
	}
	TArray<TObjectPtr<UPaper2DPlusCueBase>>& LoadedCues =
		LoadedProfile->Flipbooks[0].FrameEventData.FrameCues;
	if (!TestEqual(TEXT("One custom placement survives"), LoadedCues.Num(), 1))
	{
		return false;
	}
	UPaper2DPlusCueState* LoadedPlacement = Cast<UPaper2DPlusCueState>(LoadedCues[0]);
	if (!TestNotNull(TEXT("Reloaded placement retains Range lifecycle"), LoadedPlacement))
	{
		return false;
	}
	TestTrue(
		TEXT("Reloaded placement uses the reloaded Cue Type class"),
		LoadedPlacement->GetClass()->ClassGeneratedBy == LoadedCueType);
	TestTrue(
		TEXT("Reloaded generated class keeps the specialized runtime envelope"),
		LoadedPlacement->GetClass()->GetClass()->IsChildOf(
			UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass()));
	TestEqual(TEXT("Range anchor survives reload"), LoadedPlacement->StartFrame, 1);
	TestEqual(TEXT("Range length survives reload"), LoadedPlacement->FrameCount, 2);

	const FFloatProperty* LoadedVolumeProperty =
		FindFProperty<FFloatProperty>(LoadedPlacement->GetClass(), PayloadName);
	if (TestNotNull(TEXT("Reloaded payload field survives"), LoadedVolumeProperty))
	{
		const UObject* LoadedDefaults = LoadedPlacement->GetClass()->GetDefaultObject();
		if (TestNotNull(TEXT("Reloaded Cue Type defaults survive"), LoadedDefaults))
		{
			TestEqual(
				TEXT("Reloaded class default remains distinct from the placement override"),
				LoadedVolumeProperty->GetPropertyValue_InContainer(LoadedDefaults),
				0.5f);
		}
		TestEqual(
			TEXT("Reloaded authored payload value survives"),
			LoadedVolumeProperty->GetPropertyValue_InContainer(LoadedPlacement),
			AuthoredVolume);
	}

	const FPaper2DPlusFrameCueTypeDescriptor ReloadedDescriptor =
		FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(LoadedPlacement->GetClass());
	TestTrue(
		TEXT("Reloaded, durably saved Cue Type is placement-ready"),
		ReloadedDescriptor.Availability == EPaper2DPlusFrameCueTypeAvailability::Ready);

	UBlueprint* ListenerBlueprint = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(),
		GetTransientPackage(),
		MakeUniqueObjectName(
			GetTransientPackage(),
			UBlueprint::StaticClass(),
			TEXT("BP_ReloadedFrameCueListener")),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		NAME_None);
	UEdGraph* ListenerGraph = ListenerBlueprint
		? FBlueprintEditorUtils::FindEventGraph(ListenerBlueprint)
		: nullptr;
	if (!TestNotNull(TEXT("typed listener fixture graph exists"), ListenerGraph))
	{
		return false;
	}
	UK2Node_ListenForFrameCue* ListenerNode =
		NewObject<UK2Node_ListenForFrameCue>(ListenerGraph);
	ListenerGraph->AddNode(ListenerNode);
	ListenerNode->AllocateDefaultPins();
	UEdGraphPin* ListenerClassPin = ListenerNode->FindPin(TEXT("CueClass"), EGPD_Input);
	UEdGraphPin* ListenerCuePin = ListenerNode->FindPin(TEXT("Cue"), EGPD_Output);
	if (!TestNotNull(TEXT("typed listener exposes Cue Class"), ListenerClassPin)
		|| !TestNotNull(TEXT("typed listener exposes Cue payload"), ListenerCuePin))
	{
		return false;
	}
	ListenerClassPin->DefaultObject = LoadedCueType->GeneratedClass;
	ListenerNode->PinDefaultValueChanged(ListenerClassPin);
	TestTrue(
		TEXT("typed listener resolves the exact saved-and-reloaded custom Cue Type"),
		ListenerNode->GetSelectedCueClass() == LoadedCueType->GeneratedClass
			&& ListenerCuePin->PinType.PinSubCategoryObject.Get()
				== LoadedCueType->GeneratedClass);
	UClass* TypedListenerOutput = Cast<UClass>(
		ListenerCuePin->PinType.PinSubCategoryObject.Get());
	TestTrue(
		TEXT("typed listener output exposes the reloaded custom payload field"),
		TypedListenerOutput
			&& FindFProperty<FFloatProperty>(TypedListenerOutput, PayloadName)
				== LoadedVolumeProperty);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ListenerBlueprint);
	FKismetEditorUtilities::CompileBlueprint(ListenerBlueprint);
	TestTrue(
		TEXT("typed listener compiles against the saved-and-reloaded Cue Type"),
		ListenerBlueprint->Status != BS_Error
			&& ListenerBlueprint->GeneratedClass != nullptr);

	TArray<EPaper2DPlusFrameCuePhase> Phases;
	TArray<EPaper2DPlusFrameCueEndReason> EndReasons;
	TSet<TObjectPtr<UPaper2DPlusCueBase>> ActiveRanges;
	auto Dispatch = [&](int32 PreviousFrame, int32 CurrentFrame)
	{
		FPaper2DPlusFrameCueContext Context;
		Context.PreviousFrame = PreviousFrame;
		Context.CurrentFrame = CurrentFrame;
		Paper2DPlusFrameCues::DispatchFrameTransition(
			LoadedCues,
			Context,
			ActiveRanges,
			[](UPaper2DPlusCueBase&) { return true; },
			[&](UPaper2DPlusCueBase& Cue, const FPaper2DPlusFrameCueContext& Notification)
			{
				if (&Cue == LoadedPlacement)
				{
					Phases.Add(Notification.Phase);
					EndReasons.Add(Notification.EndReason);
				}
			});
	};
	Dispatch(0, 1);
	Dispatch(1, 2);
	Dispatch(2, 3);
	if (TestEqual(TEXT("Reloaded custom Range emits Begin and End"), Phases.Num(), 2))
	{
		TestTrue(TEXT("Reloaded custom Range begins"), Phases[0] == EPaper2DPlusFrameCuePhase::Begin);
		TestTrue(TEXT("Reloaded custom Range ends"), Phases[1] == EPaper2DPlusFrameCuePhase::End);
		TestTrue(
			TEXT("Reloaded custom Range completes normally"),
			EndReasons[1] == EPaper2DPlusFrameCueEndReason::Completed);
	}
	TestEqual(TEXT("Reloaded custom Range leaves no active state"), ActiveRanges.Num(), 0);

	// CreateBlueprint gives this transient listener RF_Standalone. Tear it down before package
	// unload so its typed pins and compiled class cannot retain the reloaded Cue Type package.
	ListenerClassPin->DefaultObject = nullptr;
	ListenerCuePin->PinType.PinSubCategoryObject = nullptr;
	if (ListenerBlueprint->GeneratedClass)
	{
		ListenerBlueprint->GeneratedClass->ClearFlags(RF_Standalone);
	}
	if (ListenerBlueprint->SkeletonGeneratedClass)
	{
		ListenerBlueprint->SkeletonGeneratedClass->ClearFlags(RF_Standalone);
	}
	ListenerBlueprint->ClearFlags(RF_Standalone);
	ListenerBlueprint->MarkAsGarbage();
	ListenerClassPin = nullptr;
	ListenerCuePin = nullptr;
	ListenerNode = nullptr;
	ListenerGraph = nullptr;
	ListenerBlueprint = nullptr;
	TypedListenerOutput = nullptr;
	LoadedVolumeProperty = nullptr;
	ActiveRanges.Reset();
	LoadedPlacement = nullptr;
	LoadedProfile = nullptr;
	LoadedCueType = nullptr;
	LoadedPackage = nullptr;
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	FText CleanupError;
	if (!TestTrue(TEXT("Temporary Cue Type fixture cleans up"), Fixture.Cleanup(CleanupError)))
	{
		AddError(CleanupError.ToString());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeDeferredPersistencePolicyTest,
	"Paper2DPlus.FrameCues.CueType.Persistence.DeferredBehaviorFailsSaveAndCookBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeDeferredPersistencePolicyTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypePersistenceTest;
	FScopedFixturePackage Fixture;
	UPackage* Package = CreatePackage(*Fixture.PackageName);
	if (!TestNotNull(TEXT("Deferred persistence fixture package"), Package))
	{
		return false;
	}
	Package->AddToRoot();

	FPaper2DPlusFrameCueTypeCreateRequest CreateRequest;
	CreateRequest.Package = Package;
	CreateRequest.AssetName = TEXT("BP_DeferredPersistenceCue");
	CreateRequest.Kind = EPaper2DPlusFrameCueTypeKind::Moment;
	const FPaper2DPlusFrameCueTypeCreateResult Created =
		FPaper2DPlusFrameCueTypeAuthoring::CreateCueType(CreateRequest);
	UPaper2DPlusFrameCueBlueprint* Blueprint = Created.CueType;
	if (!TestTrue(TEXT("Deferred persistence fixture starts as a valid Cue Type"),
		Created.IsSuccess())
		|| !TestNotNull(TEXT("Deferred persistence Cue Type"), Blueprint))
	{
		Package->RemoveFromRoot();
		return false;
	}
	Blueprint->SetFlags(RF_Public | RF_Standalone | RF_Transactional);

	const FPaper2DPlusFrameCueDurableSchemaCandidate ValidCandidate =
		FPaper2DPlusFrameCueTypeAuthoring::PrepareDurableSchemaSaveCandidate(
			*Blueprint);
	if (!TestTrue(TEXT("A valid baseline is staged before deferred tampering"),
		ValidCandidate.bSuccess))
	{
		AddError(ValidCandidate.Error.ToString());
		Package->RemoveFromRoot();
		return false;
	}
	const int32 BaselineVersion = Blueprint->DurableSchemaVersion;
	const FString BaselineFingerprint = Blueprint->DurableSchemaFingerprint;
	const FString BaselineSnapshot = Blueprint->DurableSchemaSnapshot;

	UK2Node_CallFunction* TimerNode = AddDeferredTimerCall(*Blueprint);
	if (!TestNotNull(TEXT("Deferred timer call is injected into source"), TimerNode))
	{
		Package->RemoveFromRoot();
		return false;
	}
	// Deliberately model stale-valid compiled state: every persistence boundary must compare the
	// authored graph instead of trusting a previously generated class/status.
	Blueprint->Status = BS_UpToDate;
	Package->MarkPackageDirty();

	FText PolicyError;
	TestFalse(TEXT("Deferred source fails the authoritative graph policy"),
		FPaper2DPlusFrameCueTypeAuthoring::ValidateSynchronousBehavior(
			*Blueprint,
			&PolicyError));
	TestTrue(TEXT("Deferred persistence diagnosis names the timer call"),
		PolicyError.ToString().Contains(TEXT("K2_SetTimerForNextTick"))
		&& PolicyError.ToString().Contains(TEXT("schedules work")));

	const FPaper2DPlusFrameCueSchemaPreflight Preflight =
		FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(*Blueprint);
	TestFalse(TEXT("Protected save preflight rejects deferred source"),
		Preflight.bCanCompile || Preflight.bCanPersist);

	const FPaper2DPlusFrameCueDurableSchemaCandidate RejectedCandidate =
		FPaper2DPlusFrameCueTypeAuthoring::PrepareDurableSchemaSaveCandidate(
			*Blueprint);
	TestFalse(TEXT("Explicit durable staging rejects deferred source"),
		RejectedCandidate.bSuccess);
	TestEqual(TEXT("Explicit durable staging uses the policy diagnosis"),
		RejectedCandidate.Error.ToString(),
		PolicyError.ToString());

	FText DurableError;
	TestFalse(TEXT("Durable persistence validation rejects deferred source"),
		FPaper2DPlusFrameCueTypeAuthoring::ValidateDurableSaveMetadataForPersistence(
			*Blueprint,
			&DurableError));
	TestEqual(TEXT("Durable validation uses the policy diagnosis"),
		DurableError.ToString(),
		PolicyError.ToString());

	FText CookError;
	TestFalse(TEXT("The generated-class cook seam rejects deferred source"),
		UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
			*Blueprint,
			&CookError,
			Blueprint->GeneratedClass));
	TestEqual(TEXT("Cook validation uses the policy diagnosis"),
		CookError.ToString(),
		PolicyError.ToString());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	AddExpectedError(
		TEXT("Cannot persist Frame Cue Type asset"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	const bool bSaved = UPackage::SavePackage(
		Package,
		Blueprint,
		*Fixture.FilePath,
		SaveArgs);
	TestFalse(TEXT("Ordinary package save fails closed for deferred behavior"), bSaved);
	UEdGraph* BehaviorGraph =
		FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(*Blueprint);
	TestTrue(TEXT("Rejected persistence leaves the authored timer node untouched"),
		BehaviorGraph && BehaviorGraph->Nodes.Contains(TimerNode));
	TestEqual(TEXT("Rejected persistence preserves the durable schema version"),
		Blueprint->DurableSchemaVersion,
		BaselineVersion);
	TestEqual(TEXT("Rejected persistence preserves the durable fingerprint"),
		Blueprint->DurableSchemaFingerprint,
		BaselineFingerprint);
	TestEqual(TEXT("Rejected persistence preserves the durable snapshot"),
		Blueprint->DurableSchemaSnapshot,
		BaselineSnapshot);

	Package->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
