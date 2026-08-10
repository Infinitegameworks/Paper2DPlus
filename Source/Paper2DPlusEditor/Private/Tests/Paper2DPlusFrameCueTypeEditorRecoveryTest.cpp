// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "FrameCues/Paper2DPlusFrameCueTypeEditor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "Misc/Guid.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PackageTools.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

namespace Paper2DPlusFrameCueTypeEditorRecoveryTest
{
	class FScopedLogTokenCapture : public FOutputDevice
	{
	public:
		explicit FScopedLogTokenCapture(FString InToken)
			: Token(MoveTemp(InToken))
		{
			if (GLog)
			{
				GLog->AddOutputDevice(this);
			}
		}

		virtual ~FScopedLogTokenCapture() override
		{
			if (GLog)
			{
				GLog->RemoveOutputDevice(this);
			}
		}

		virtual void Serialize(
			const TCHAR* Message,
			ELogVerbosity::Type,
			const FName&) override
		{
			if (Message && FCString::Strstr(Message, *Token))
			{
				bMatched = true;
			}
		}

		bool Matched() const
		{
			return bMatched;
		}

	private:
		FString Token;
		bool bMatched = false;
	};

	UPaper2DPlusFrameCueBlueprint* MakeCompiledCueType(const TCHAR* Stem)
	{
		const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString PackageName = FString::Printf(
			TEXT("/Game/__Paper2DPlusTests/%s_%s"), Stem, *Token);
		UPackage* Package = CreatePackage(*PackageName);
		const FName AssetName(*FString::Printf(TEXT("%s_%s"), Stem, *Token));
		UPaper2DPlusFrameCueBlueprint* Blueprint =
			Cast<UPaper2DPlusFrameCueBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UPaper2DPlusCue::StaticClass(),
				Package,
				AssetName,
				BPTYPE_Normal,
				UPaper2DPlusFrameCueBlueprint::StaticClass(),
				UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass(),
				NAME_None));
		if (!Blueprint || !FPaper2DPlusFrameCueTypeEditor::PrepareNewlyCreatedBlueprint(*Blueprint))
		{
			return nullptr;
		}

		FEdGraphPinType StrengthType;
		StrengthType.PinCategory = UEdGraphSchema_K2::PC_Real;
		StrengthType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		if (!FBlueprintEditorUtils::AddMemberVariable(
			Blueprint, TEXT("Strength"), StrengthType, TEXT("1.25")))
		{
			return nullptr;
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		Blueprint->bIsNewlyCreated = false;
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings
			? Blueprint
			: nullptr;
	}

	bool AddPrintStringBehaviorBody(
		UPaper2DPlusFrameCueBlueprint& Blueprint,
		const FString& Token)
	{
		UK2Node_Event* EventNode =
			FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventNode(
				Blueprint,
				TEXT("OnCueTriggered"));
		UEdGraph* BehaviorGraph = EventNode ? EventNode->GetGraph() : nullptr;
		const UEdGraphSchema_K2* Schema =
			BehaviorGraph
				? Cast<UEdGraphSchema_K2>(BehaviorGraph->GetSchema())
				: nullptr;
		if (!EventNode || !BehaviorGraph || !Schema)
		{
			return false;
		}
		if (EventNode->IsAutomaticallyPlacedGhostNode())
		{
			EventNode->SetEnabledState(ENodeEnabledState::Enabled, true);
		}

		FGraphNodeCreator<UK2Node_CallFunction> CallCreator(*BehaviorGraph);
		UK2Node_CallFunction* PrintNode = CallCreator.CreateNode(false);
		PrintNode->FunctionReference.SetExternalMember(
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString),
			UKismetSystemLibrary::StaticClass());
		PrintNode->NodePosX = EventNode->NodePosX + 320;
		PrintNode->NodePosY = EventNode->NodePosY;
		CallCreator.Finalize();

		UEdGraphPin* EventThen =
			EventNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* PrintExec = PrintNode->GetExecPin();
		UEdGraphPin* StringPin = PrintNode->FindPin(TEXT("InString"), EGPD_Input);
		UEdGraphPin* PrintToScreenPin =
			PrintNode->FindPin(TEXT("bPrintToScreen"), EGPD_Input);
		UEdGraphPin* PrintToLogPin =
			PrintNode->FindPin(TEXT("bPrintToLog"), EGPD_Input);
		if (!EventThen
			|| !PrintExec
			|| !StringPin
			|| !PrintToScreenPin
			|| !PrintToLogPin
			|| !Schema->TryCreateConnection(EventThen, PrintExec))
		{
			return false;
		}
		Schema->TrySetDefaultValue(*StringPin, Token);
		Schema->TrySetDefaultValue(*PrintToScreenPin, TEXT("false"));
		Schema->TrySetDefaultValue(*PrintToLogPin, TEXT("true"));
		if (StringPin->DefaultValue != Token
			|| PrintToScreenPin->DefaultValue != TEXT("false")
			|| PrintToLogPin->DefaultValue != TEXT("true"))
		{
			return false;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(&Blueprint);
		return true;
	}

	UEdGraph* AddLegacyGraph(
		UBlueprint& Blueprint,
		TArray<TObjectPtr<UEdGraph>>& Collection,
		const TCHAR* Name)
	{
		UEdGraph* Graph = NewObject<UEdGraph>(&Blueprint, FName(Name), RF_Transactional);
		UEdGraphNode_Comment* Node = NewObject<UEdGraphNode_Comment>(Graph);
		Node->CreateNewGuid();
		Graph->AddNode(Node, false, false);
		Collection.Add(Graph);
		return Graph;
	}

	void CloseEditor(const TSharedRef<FPaper2DPlusFrameCueTypeEditor>& Editor)
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
		Editor->CloseWindow(EAssetEditorCloseReason::AssetUnloadingOrInvalid);
#else
		Editor->CloseWindow();
#endif
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeEditorDurableSaveRecoveryTest,
	"Paper2DPlus.FrameCues.CueType.EditorRecovery.DurableSaveMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusFrameCueTypeEditorDurableSaveRecoveryTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeEditorRecoveryTest;

	UPaper2DPlusFrameCueBlueprint* FailedSaveCue = MakeCompiledCueType(
		TEXT("BP_P2DP_CueTypeFailedSaveRecovery"));
	if (!TestNotNull(TEXT("failed-save Cue Type fixture"), FailedSaveCue))
	{
		return false;
	}
	UPackage* FailedPackage = FailedSaveCue->GetOutermost();
	FailedSaveCue->DurableSchemaVersion = 77;
	FailedSaveCue->DurableSchemaFingerprint = TEXT("previous-durable-fingerprint");
	FailedSaveCue->DurableSchemaSnapshot = TEXT("previous-durable-snapshot");
	FailedSaveCue->DurableVariables = FailedSaveCue->NewVariables;
	FailedSaveCue->DurableDefaultValues.Add(TEXT("Strength"), TEXT("1.25"));
	FailedPackage->SetDirtyFlag(true);
	const int32 AuthoredFieldCount = FailedSaveCue->NewVariables.Num();

	FPaper2DPlusFrameCueDurableSaveAttempt FailedAttempt;
	FText PrepareError;
	if (!TestTrue(
		TEXT("valid compiled Cue Type prepares a durable save candidate"),
		FPaper2DPlusFrameCueTypeEditor::PrepareDurableSchemaSaveAttempt(
			*FailedSaveCue, FailedAttempt, &PrepareError)))
	{
		AddError(PrepareError.ToString());
		return false;
	}
	TestFalse(
		TEXT("candidate metadata is not mistaken for the previous durable metadata"),
		FailedSaveCue->DurableSchemaFingerprint
			== FailedAttempt.PreviousFingerprint);

	FText FinalizeError;
	TestFalse(
		TEXT("a package that remains dirty is not a durable save"),
		FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(
			*FailedSaveCue, FailedAttempt, &FinalizeError));
	TestEqual(
		TEXT("failed save restores the previous durable schema version"),
		FailedSaveCue->DurableSchemaVersion,
		77);
	TestEqual(
		TEXT("failed save restores the previous durable fingerprint"),
		FailedSaveCue->DurableSchemaFingerprint,
		FString(TEXT("previous-durable-fingerprint")));
	TestEqual(
		TEXT("failed save restores the previous durable schema snapshot"),
		FailedSaveCue->DurableSchemaSnapshot,
		FString(TEXT("previous-durable-snapshot")));
	TestEqual(
		TEXT("failed save restores the previous durable variable model"),
		FailedSaveCue->DurableVariables.Num(),
		AuthoredFieldCount);
	TestEqual(
		TEXT("failed save restores the previous durable class default"),
		FailedSaveCue->DurableDefaultValues.FindRef(FName(TEXT("Strength"))),
		FString(TEXT("1.25")));
	TestTrue(
		TEXT("failed save restores the author's pre-attempt dirty state"),
		FailedPackage->IsDirty());
	TestEqual(
		TEXT("failed save retains authored fields for a retry"),
		FailedSaveCue->NewVariables.Num(),
		AuthoredFieldCount);
	TestFalse(
		TEXT("failed durability never unlocks the placement commit boundary"),
		FPaper2DPlusFrameCueTypeEditor::IsDurablyReadyForPlacement(*FailedSaveCue));

	FPaper2DPlusFrameCueDurableSaveAttempt MismatchedAttempt;
	if (!TestTrue(
		TEXT("failed-save edits remain retryable as another candidate"),
		FPaper2DPlusFrameCueTypeEditor::PrepareDurableSchemaSaveAttempt(
			*FailedSaveCue, MismatchedAttempt, &PrepareError)))
	{
		AddError(PrepareError.ToString());
		return false;
	}
	FailedSaveCue->DurableSchemaFingerprint = TEXT("not-the-candidate-fingerprint");
	FailedPackage->SetDirtyFlag(false);
	TestFalse(
		TEXT("a clean package with mismatched readiness is still a failed durable save"),
		FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(
			*FailedSaveCue, MismatchedAttempt, &FinalizeError));
	TestEqual(
		TEXT("readiness mismatch restores the prior durable fingerprint"),
		FailedSaveCue->DurableSchemaFingerprint,
		FString(TEXT("previous-durable-fingerprint")));
	TestTrue(
		TEXT("readiness mismatch restores the prior dirty state for retry"),
		FailedPackage->IsDirty());

	UPaper2DPlusFrameCueBlueprint* SuccessfulSaveCue = MakeCompiledCueType(
		TEXT("BP_P2DP_CueTypeSuccessfulSaveRecovery"));
	if (!TestNotNull(TEXT("successful-save Cue Type fixture"), SuccessfulSaveCue))
	{
		return false;
	}
	UPackage* SuccessfulPackage = SuccessfulSaveCue->GetOutermost();
	SuccessfulSaveCue->DurableSchemaVersion = 88;
	SuccessfulSaveCue->DurableSchemaFingerprint = TEXT("older-durable-fingerprint");
	SuccessfulPackage->SetDirtyFlag(true);

	FPaper2DPlusFrameCueDurableSaveAttempt SuccessfulAttempt;
	if (!TestTrue(
		TEXT("second Cue Type prepares its candidate"),
		FPaper2DPlusFrameCueTypeEditor::PrepareDurableSchemaSaveAttempt(
			*SuccessfulSaveCue, SuccessfulAttempt, &PrepareError)))
	{
		AddError(PrepareError.ToString());
		return false;
	}
	SuccessfulPackage->SetDirtyFlag(false);
	TestTrue(
		TEXT("clean package with the exact candidate fingerprint completes durability"),
		FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(
			*SuccessfulSaveCue, SuccessfulAttempt, &FinalizeError));
	TestEqual(
		TEXT("successful save retains the candidate schema version"),
		SuccessfulSaveCue->DurableSchemaVersion,
		SuccessfulAttempt.CandidateVersion);
	TestEqual(
		TEXT("successful save retains the candidate fingerprint"),
		SuccessfulSaveCue->DurableSchemaFingerprint,
		SuccessfulAttempt.CandidateFingerprint);
	TestTrue(
		TEXT("only a successful durable save unlocks placement commit"),
		FPaper2DPlusFrameCueTypeEditor::IsDurablyReadyForPlacement(*SuccessfulSaveCue));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeDirectSaveValidationTest,
	"Paper2DPlus.FrameCues.CueType.EditorRecovery.SaveAllCompilesAndStagesCompatibleEdits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusFrameCueTypeDirectSaveValidationTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeEditorRecoveryTest;
	UPaper2DPlusFrameCueBlueprint* CueType = MakeCompiledCueType(
		TEXT("BP_P2DP_DirectSaveValidation"));
	if (!TestNotNull(TEXT("direct-save validation fixture"), CueType))
	{
		return false;
	}

	TestTrue(
		TEXT("editor module installs ordinary-save preparation"),
		UPaper2DPlusFrameCueBlueprint::IsAutomaticSavePreparationDelegateBound());

	// Reproduce the designer path exactly: add a payload field, do not press Compile, then let an
	// ordinary package save (Save All / Content Browser Save) own compilation and durable staging.
	FEdGraphPinType RadiusType;
	RadiusType.PinCategory = UEdGraphSchema_K2::PC_Real;
	RadiusType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	if (!TestTrue(
		TEXT("a compatible payload edit is authored without an explicit compile"),
		FBlueprintEditorUtils::AddMemberVariable(
			CueType,
			TEXT("Radius"),
			RadiusType,
			TEXT("32.0"))))
	{
		return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(CueType);

	const FString PackageName = CueType->GetOutermost()->GetName();
	const FName CueTypeName = CueType->GetFName();
	const FString SaveFilename = FPackageName::LongPackageNameToFilename(
		PackageName,
		FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(
		*FPaths::GetPath(SaveFilename),
		/*Tree=*/true);
	ON_SCOPE_EXIT
	{
		if (UPackage* PackageToUnload = FindPackage(nullptr, *PackageName))
		{
			PackageToUnload->SetDirtyFlag(false);
			if (PackageToUnload->IsRooted())
			{
				PackageToUnload->RemoveFromRoot();
			}
			TArray<UPackage*> PackagesToUnload{PackageToUnload};
			FText UnloadError;
			if (!UPackageTools::UnloadPackages(
					PackagesToUnload,
					UnloadError,
					/*bUnloadDirtyPackages=*/true))
			{
				AddError(FString::Printf(
					TEXT("Cue Type round-trip cleanup could not unload its package: %s"),
					*UnloadError.ToString()));
			}
		}
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		IFileManager& FileManager = IFileManager::Get();
		const auto DeleteSavedArtifact = [this, &FileManager](const FString& Filename)
		{
			if (FileManager.FileExists(*Filename)
				&& !FileManager.Delete(*Filename, false, true, true))
			{
				AddError(FString::Printf(
					TEXT("Cue Type round-trip cleanup could not delete '%s'."),
					*Filename));
			}
		};
		DeleteSavedArtifact(SaveFilename);
		DeleteSavedArtifact(FPaths::ChangeExtension(SaveFilename, TEXT("uexp")));
		DeleteSavedArtifact(FPaths::ChangeExtension(SaveFilename, TEXT("ubulk")));
		DeleteSavedArtifact(FPaths::ChangeExtension(SaveFilename, TEXT("uptnl")));
	};

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!TestTrue(
		TEXT("ordinary package save compiles and persists the compatible Cue Type edit"),
		UPackage::SavePackage(
			CueType->GetOutermost(),
			CueType,
			*SaveFilename,
			SaveArgs)))
	{
		return false;
	}

	// Prove that PackageSavedWithContextEvent canceled the queued rollback.
	FTSTicker::GetCoreTicker().Tick(0.0f);

	TestNotNull(
		TEXT("ordinary save compiled the newly authored payload field"),
		FindFProperty<FFloatProperty>(CueType->GeneratedClass, TEXT("Radius")));
	FText Error;
	TestTrue(
		TEXT("ordinary save wrote the exact current durable baseline"),
		FPaper2DPlusFrameCueTypeAuthoring::ValidateDurableSaveMetadataForPersistence(
			*CueType,
			&Error));
	TestFalse(
		TEXT("successful ordinary save leaves the Cue Type package clean"),
		CueType->GetOutermost()->IsDirty());
	TestTrue(
		TEXT("successful ordinary save makes the Cue Type placement-ready"),
		FPaper2DPlusFrameCueTypeEditor::IsDurablyReadyForPlacement(*CueType));

	UPackage* FirstSavedPackage = CueType->GetOutermost();
	FirstSavedPackage->SetDirtyFlag(false);
	CueType = nullptr;
	TArray<UPackage*> FirstPackagesToUnload{FirstSavedPackage};
	FText FirstUnloadError;
	if (!TestTrue(
		TEXT("the first Save All unloads before variable persistence is inspected"),
		UPackageTools::UnloadPackages(
			FirstPackagesToUnload,
			FirstUnloadError,
			/*bUnloadDirtyPackages=*/true)))
	{
		AddError(FirstUnloadError.ToString());
		return false;
	}
	FirstSavedPackage = nullptr;
	if (!TestNull(
		TEXT("the first-save Cue Type is absent from memory before reload"),
		FindPackage(nullptr, *PackageName)))
	{
		return false;
	}

	UPackage* ReloadedPackage = LoadPackage(nullptr, *PackageName, LOAD_None);
	UPaper2DPlusFrameCueBlueprint* ReloadedCueType =
		ReloadedPackage
			? FindObject<UPaper2DPlusFrameCueBlueprint>(
				ReloadedPackage,
				*CueTypeName.ToString())
			: nullptr;
	if (!TestNotNull(
		TEXT("the Cue Type reloads from its first Save All"),
		ReloadedCueType))
	{
		return false;
	}
	TestTrue(
		TEXT("the first Save All persists the authored payload variable"),
		ReloadedCueType->NewVariables.ContainsByPredicate(
			[](const FBPVariableDescription& Variable)
			{
				return Variable.VarName == TEXT("Radius");
			}));
	TestTrue(
		TEXT("the first Save All persists Radius in its durable variable baseline"),
		ReloadedCueType->DurableVariables.ContainsByPredicate(
			[](const FBPVariableDescription& Variable)
			{
				return Variable.VarName == TEXT("Radius");
			}));
	const FFloatProperty* ReloadedRadiusProperty =
		FindFProperty<FFloatProperty>(
			ReloadedCueType->GeneratedClass,
			TEXT("Radius"));
	if (TestNotNull(
		TEXT("the first saved generated class reloads the Radius payload field"),
		ReloadedRadiusProperty))
	{
		TestEqual(
			TEXT("the first Save All persists the authored Radius default"),
			ReloadedRadiusProperty->GetPropertyValue_InContainer(
				ReloadedCueType->GeneratedClass->GetDefaultObject()),
			32.0f);
	}
	FText ReloadedError;
	TestTrue(
		TEXT("the first Save All persists a valid durable baseline"),
		FPaper2DPlusFrameCueTypeAuthoring::ValidateDurableSaveMetadataForPersistence(
			*ReloadedCueType,
			&ReloadedError));
	TestTrue(
		TEXT("the first saved Cue Type reloads placement-ready"),
		FPaper2DPlusFrameCueTypeEditor::IsDurablyReadyForPlacement(
			*ReloadedCueType));

	const FString BehaviorToken = FString::Printf(
		TEXT("P2DP_SAVE_ALL_BEHAVIOR_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	if (!TestTrue(
		TEXT("an existing Cue event receives a behavior-body-only edit"),
		AddPrintStringBehaviorBody(*ReloadedCueType, BehaviorToken)))
	{
		return false;
	}
	TestFalse(
		TEXT("the behavior-only edit leaves generated bytecode stale before Save All"),
		ReloadedCueType->Status == BS_UpToDate
			|| ReloadedCueType->Status == BS_UpToDateWithWarnings);
	if (!TestTrue(
		TEXT("ordinary package save recompiles and persists a behavior-body-only edit"),
		UPackage::SavePackage(
			ReloadedCueType->GetOutermost(),
			ReloadedCueType,
			*SaveFilename,
			SaveArgs)))
	{
		return false;
	}
	FTSTicker::GetCoreTicker().Tick(0.0f);

	TestTrue(
		TEXT("behavior-only Save All leaves current generated bytecode"),
		ReloadedCueType->Status == BS_UpToDate
			|| ReloadedCueType->Status == BS_UpToDateWithWarnings);

	UPackage* BehaviorSavedPackage = ReloadedCueType->GetOutermost();
	BehaviorSavedPackage->SetDirtyFlag(false);
	ReloadedCueType = nullptr;
	ReloadedPackage = nullptr;
	TArray<UPackage*> BehaviorPackagesToUnload{BehaviorSavedPackage};
	FText BehaviorUnloadError;
	if (!TestTrue(
		TEXT("behavior Save All unloads before its on-disk verification"),
		UPackageTools::UnloadPackages(
			BehaviorPackagesToUnload,
			BehaviorUnloadError,
			/*bUnloadDirtyPackages=*/true)))
	{
		AddError(BehaviorUnloadError.ToString());
		return false;
	}
	BehaviorSavedPackage = nullptr;
	if (!TestNull(
		TEXT("behavior-save Cue Type is absent from memory before reload"),
		FindPackage(nullptr, *PackageName)))
	{
		return false;
	}

	UPackage* BehaviorReloadedPackage = LoadPackage(nullptr, *PackageName, LOAD_None);
	UPaper2DPlusFrameCueBlueprint* BehaviorReloadedCueType =
		BehaviorReloadedPackage
			? FindObject<UPaper2DPlusFrameCueBlueprint>(
				BehaviorReloadedPackage,
				*CueTypeName.ToString())
			: nullptr;
	if (!TestNotNull(
		TEXT("behavior Save All reloads from its saved package"),
		BehaviorReloadedCueType))
	{
		return false;
	}

	UPaper2DPlusCue* BehaviorCue = NewObject<UPaper2DPlusCue>(
		GetTransientPackage(),
		BehaviorReloadedCueType->GeneratedClass);
	if (!TestNotNull(
		TEXT("reloaded behavior Cue uses the class read from disk"),
		BehaviorCue))
	{
		return false;
	}
	FScopedLogTokenCapture BehaviorCapture(BehaviorToken);
	FPaper2DPlusFrameCueContext BehaviorContext;
	BehaviorContext.Phase = EPaper2DPlusFrameCuePhase::Trigger;
	Paper2DPlusFrameCueBehavior::ExecuteCueBehavior(
		*BehaviorCue,
		BehaviorContext);
	if (GLog)
	{
		GLog->FlushThreadedLogs();
	}
	TestTrue(
		TEXT("executing the reloaded Cue runs the behavior body persisted by Save All"),
	BehaviorCapture.Matched());
	BehaviorCue->MarkAsGarbage();
	BehaviorCue = nullptr;
	BehaviorReloadedCueType = nullptr;
	BehaviorReloadedPackage = nullptr;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeAutoSaveRollbackTest,
	"Paper2DPlus.FrameCues.CueType.EditorRecovery.AutoSaveCopyDoesNotAdvanceCanonicalBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusFrameCueTypeAutoSaveRollbackTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeEditorRecoveryTest;
	UPaper2DPlusFrameCueBlueprint* CueType = MakeCompiledCueType(
		TEXT("BP_P2DP_AutoSaveRollback"));
	if (!TestNotNull(TEXT("autosave rollback fixture"), CueType))
	{
		return false;
	}

	const FPaper2DPlusFrameCueDurableSchemaCandidate InitialCandidate =
		FPaper2DPlusFrameCueTypeAuthoring::PrepareDurableSchemaSaveCandidate(*CueType);
	if (!TestTrue(
		TEXT("fixture begins from a valid canonical Cue Type baseline"),
		InitialCandidate.bSuccess))
	{
		AddError(InitialCandidate.Error.ToString());
		return false;
	}
	UPackage* Package = CueType->GetOutermost();
	Package->SetDirtyFlag(false);

	const int32 PreviousVersion = CueType->DurableSchemaVersion;
	const FString PreviousFingerprint = CueType->DurableSchemaFingerprint;
	const FString PreviousSnapshot = CueType->DurableSchemaSnapshot;

	FEdGraphPinType RadiusType;
	RadiusType.PinCategory = UEdGraphSchema_K2::PC_Real;
	RadiusType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	if (!TestTrue(
		TEXT("a compatible payload edit is authored before autosave"),
		FBlueprintEditorUtils::AddMemberVariable(
			CueType,
			TEXT("Radius"),
			RadiusType,
			TEXT("32.0"))))
	{
		return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(CueType);
	const bool bPackageWasDirty = Package->IsDirty();

	const FString SaveDirectory =
		FPaths::ProjectSavedDir() / TEXT("Automation/FrameCueSaveAll");
	IFileManager::Get().MakeDirectory(*SaveDirectory, /*Tree=*/true);
	const FString AutoSaveFilename = FPaths::CreateTempFilename(
		*SaveDirectory,
		TEXT("P2DP_CueType_AutoSave_"),
		TEXT(".uasset"));
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(
			*AutoSaveFilename,
			/*RequireExists=*/false,
			/*EvenReadOnly=*/true,
			/*Quiet=*/true);
	};

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError | SAVE_FromAutosave;
	if (!TestTrue(
		TEXT("autosave writes a valid recovery copy"),
		UPackage::SavePackage(
			Package,
			CueType,
			*AutoSaveFilename,
			SaveArgs)))
	{
		return false;
	}

	// Autosave success deliberately does not commit. Model editor/module shutdown before the next
	// core tick: cancellation must remove the thunk and restore the same canonical snapshot that the
	// normal fallback would restore.
	TestEqual(
		TEXT("autosave queues exactly one failure fallback"),
		UPaper2DPlusFrameCueBlueprint::
			GetAutomaticDurableSaveRollbackCountForTests(),
		1);
	UPaper2DPlusFrameCueBlueprint::
		ResolveAllAutomaticDurableSaveTransactionsForShutdown();
	TestEqual(
		TEXT("shutdown removes every queued automatic-save fallback"),
		UPaper2DPlusFrameCueBlueprint::
			GetAutomaticDurableSaveRollbackCountForTests(),
		0);
	FTSTicker::GetCoreTicker().Tick(0.0f);

	TestNotNull(
		TEXT("autosave compiled and retained the authored payload edit"),
		FindFProperty<FFloatProperty>(CueType->GeneratedClass, TEXT("Radius")));
	TestEqual(
		TEXT("autosave restores the canonical durable schema version"),
		CueType->DurableSchemaVersion,
		PreviousVersion);
	TestEqual(
		TEXT("autosave restores the canonical durable fingerprint"),
		CueType->DurableSchemaFingerprint,
		PreviousFingerprint);
	TestEqual(
		TEXT("autosave restores the canonical durable schema snapshot"),
		CueType->DurableSchemaSnapshot,
		PreviousSnapshot);
	TestFalse(
		TEXT("autosave does not add the recovery candidate to the canonical durable model"),
		CueType->DurableVariables.ContainsByPredicate(
			[](const FBPVariableDescription& Variable)
			{
				return Variable.VarName == FName(TEXT("Radius"));
			}));
	TestEqual(
		TEXT("autosave restores the author's dirty canonical package"),
		Package->IsDirty(),
		bPackageWasDirty);
	TestFalse(
		TEXT("autosave never unlocks placement for the canonically unsaved schema"),
		FPaper2DPlusFrameCueTypeEditor::IsDurablyReadyForPlacement(*CueType));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeFailedSaveAllRollbackTest,
	"Paper2DPlus.FrameCues.CueType.EditorRecovery.FailedSaveAllRestoresDurableBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusFrameCueTypeFailedSaveAllRollbackTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeEditorRecoveryTest;
	UPaper2DPlusFrameCueBlueprint* CueType = MakeCompiledCueType(
		TEXT("BP_P2DP_FailedSaveAllRollback"));
	if (!TestNotNull(TEXT("failed Save All fixture"), CueType))
	{
		return false;
	}

	const FPaper2DPlusFrameCueDurableSchemaCandidate InitialCandidate =
		FPaper2DPlusFrameCueTypeAuthoring::PrepareDurableSchemaSaveCandidate(*CueType);
	if (!TestTrue(
		TEXT("fixture begins from a valid durable Cue Type baseline"),
		InitialCandidate.bSuccess))
	{
		AddError(InitialCandidate.Error.ToString());
		return false;
	}
	UPackage* Package = CueType->GetOutermost();
	Package->SetDirtyFlag(false);

	const int32 PreviousVersion = CueType->DurableSchemaVersion;
	const FString PreviousFingerprint = CueType->DurableSchemaFingerprint;
	const FString PreviousSnapshot = CueType->DurableSchemaSnapshot;
	const int32 PreviousVariableCount = CueType->DurableVariables.Num();
	const int32 PreviousDefaultCount = CueType->DurableDefaultValues.Num();

	FEdGraphPinType RadiusType;
	RadiusType.PinCategory = UEdGraphSchema_K2::PC_Real;
	RadiusType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	if (!TestTrue(
		TEXT("a compatible payload edit is authored before the failed ordinary save"),
		FBlueprintEditorUtils::AddMemberVariable(
			CueType,
			TEXT("Radius"),
			RadiusType,
			TEXT("32.0"))))
	{
		return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(CueType);
	const bool bPackageWasDirty = Package->IsDirty();

	const FString FailureParent =
		FPaths::ProjectSavedDir() / TEXT("Automation/FrameCueSaveAll");
	IFileManager::Get().MakeDirectory(*FailureParent, /*Tree=*/true);
	const FString BlockingFile = FailureParent / FString::Printf(
		TEXT("P2DP_CueType_SaveAll_Blocker_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	TUniquePtr<FArchive> BlockingFileWriter(
		IFileManager::Get().CreateFileWriter(*BlockingFile));
	if (!TestTrue(
		TEXT("failure fixture creates a regular file where the save needs a directory"),
		BlockingFileWriter.IsValid()))
	{
		return false;
	}
	BlockingFileWriter.Reset();
	const FString FailureFilename = BlockingFile / TEXT("CueType.uasset");
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(
			*FailureFilename,
			/*RequireExists=*/false,
			/*EvenReadOnly=*/true,
			/*Quiet=*/true);
		IFileManager::Get().DeleteDirectory(
			*BlockingFile,
			/*RequireExists=*/false,
			/*Tree=*/true);
		IFileManager::Get().Delete(
			*BlockingFile,
			/*RequireExists=*/false,
			/*EvenReadOnly=*/true,
			/*Quiet=*/true);
	};

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	// No category prefix in the pattern: UE 5.0-5.5 capture the bare "Error moving file '...'"
	// message while 5.6+ include the "LogFileManager:" prefix, and the expectation must match
	// both shapes. Older engines also retry the blocked move with MoveFile warnings first;
	// warnings do not fail the test and the final error still occurs exactly once.
	AddExpectedError(
		TEXT("Error moving file"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	TestFalse(
		TEXT("a regular-file parent forces a late package-write failure"),
		UPackage::SavePackage(
			Package,
			CueType,
			*FailureFilename,
			SaveArgs));

	// The failure-only path has no package-saved event. Pump the one-shot fallback registered by
	// PostSaveRoot so it can restore the exact baseline held before automatic preparation.
	FTSTicker::GetCoreTicker().Tick(0.0f);

	TestNotNull(
		TEXT("PreSaveRoot compiled the compatible payload edit before the late failure"),
		FindFProperty<FFloatProperty>(CueType->GeneratedClass, TEXT("Radius")));
	TestEqual(
		TEXT("failed Save All restores the durable schema version"),
		CueType->DurableSchemaVersion,
		PreviousVersion);
	TestEqual(
		TEXT("failed Save All restores the durable fingerprint"),
		CueType->DurableSchemaFingerprint,
		PreviousFingerprint);
	TestEqual(
		TEXT("failed Save All restores the durable schema snapshot"),
		CueType->DurableSchemaSnapshot,
		PreviousSnapshot);
	TestEqual(
		TEXT("failed Save All restores the durable variable model"),
		CueType->DurableVariables.Num(),
		PreviousVariableCount);
	TestFalse(
		TEXT("the failed candidate field is absent from the restored durable variable model"),
		CueType->DurableVariables.ContainsByPredicate(
			[](const FBPVariableDescription& Variable)
			{
				return Variable.VarName == FName(TEXT("Radius"));
			}));
	TestEqual(
		TEXT("failed Save All restores the durable default-value model"),
		CueType->DurableDefaultValues.Num(),
		PreviousDefaultCount);
	TestFalse(
		TEXT("the failed candidate default is absent from the restored durable defaults"),
		CueType->DurableDefaultValues.Contains(FName(TEXT("Radius"))));
	TestEqual(
		TEXT("failed Save All restores the author's pre-attempt dirty state"),
		Package->IsDirty(),
		bPackageWasDirty);
	TestFalse(
		TEXT("failed Save All never unlocks placement for the unsaved authored schema"),
		FPaper2DPlusFrameCueTypeEditor::IsDurablyReadyForPlacement(*CueType));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeFingerprintOnlyBaselineTest,
	"Paper2DPlus.FrameCues.CueType.EditorRecovery.FingerprintOnlyBaselineMustBeSeeded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusFrameCueTypeFingerprintOnlyBaselineTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeEditorRecoveryTest;
	UPaper2DPlusFrameCueBlueprint* CueType = MakeCompiledCueType(
		TEXT("BP_P2DP_FingerprintOnlyBaseline"));
	if (!TestNotNull(TEXT("fingerprint-only baseline fixture"), CueType))
	{
		return false;
	}

	FPaper2DPlusFrameCueSchema CompiledSchema;
	FText Error;
	if (!TestTrue(
		TEXT("fixture compiled schema is describable"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeSchema(
			*CueType,
			EPaper2DPlusFrameCueSchemaSource::Compiled,
			CompiledSchema,
			&Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	CueType->DurableSchemaVersion =
		FPaper2DPlusFrameCueTypeAuthoring::CurrentSchemaVersion;
	CueType->DurableSchemaFingerprint = CompiledSchema.Fingerprint;
	CueType->DurableSchemaSnapshot.Reset();
	CueType->DurableVariables.Reset();
	CueType->DurableDefaultValues.Reset();
	CueType->GetOutermost()->SetDirtyFlag(false);

	const FPaper2DPlusFrameCueSchemaPreflight LegacyPreflight =
		FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(*CueType);
	TestFalse(TEXT("a fingerprint alone is not a recoverable durable baseline"),
		LegacyPreflight.bHasDurableBaseline);
	TestFalse(TEXT("schema compilation is blocked until the full baseline is seeded"),
		LegacyPreflight.bCanCompile);

	const FPaper2DPlusFrameCueDurableSchemaCandidate Seeded =
		FPaper2DPlusFrameCueTypeAuthoring::PrepareDurableSchemaSaveCandidate(*CueType);
	if (!TestTrue(TEXT("protected unchanged Save seeds the full baseline"), Seeded.bSuccess))
	{
		AddError(Seeded.Error.ToString());
		return false;
	}
	const FPaper2DPlusFrameCueSchemaPreflight SeededPreflight =
		FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(*CueType);
	TestTrue(TEXT("seeded baseline is structurally recoverable"),
		SeededPreflight.bHasDurableBaseline);
	TestTrue(TEXT("seeded baseline re-enables schema compilation"),
		SeededPreflight.bCanCompile);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeEditorDefaultCompileRecoveryTest,
	"Paper2DPlus.FrameCues.CueType.EditorRecovery.DefaultCompileAndSaveFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusFrameCueTypeEditorDefaultCompileRecoveryTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeEditorRecoveryTest;
	UPaper2DPlusFrameCueBlueprint* CueType = MakeCompiledCueType(
		TEXT("BP_P2DP_DefaultCompileRecovery"));
	if (!TestNotNull(TEXT("default recovery Cue Type fixture"), CueType))
	{
		return false;
	}
	const FPaper2DPlusFrameCueDurableSchemaCandidate Baseline =
		FPaper2DPlusFrameCueTypeAuthoring::PrepareDurableSchemaSaveCandidate(*CueType);
	if (!TestTrue(TEXT("fixture establishes a full durable schema baseline"), Baseline.bSuccess))
	{
		AddError(Baseline.Error.ToString());
		return false;
	}
	TestEqual(TEXT("fixture baseline records the durable Strength default"),
		FCString::Atof(*CueType->DurableDefaultValues.FindRef(FName(TEXT("Strength")))),
		1.25f);
	CueType->GetOutermost()->SetDirtyFlag(false);

	UObject* ProposedDefaults = CueType->GeneratedClass
		? CueType->GeneratedClass->GetDefaultObject(false)
		: nullptr;
	FFloatProperty* StrengthProperty = CueType->GeneratedClass
		? FindFProperty<FFloatProperty>(CueType->GeneratedClass, TEXT("Strength"))
		: nullptr;
	if (!TestNotNull(TEXT("fixture exposes generated defaults"), ProposedDefaults)
		|| !TestNotNull(TEXT("fixture exposes the Strength field"), StrengthProperty))
	{
		return false;
	}
	ProposedDefaults->Modify();
	StrengthProperty->SetPropertyValue_InContainer(ProposedDefaults, 2.75f);
	CueType->MarkPackageDirty();

	TSharedPtr<FPaper2DPlusFrameCueTypeEditor> Editor =
		MakeShared<FPaper2DPlusFrameCueTypeEditor>();
	Editor->InitFrameCueTypeEditor(EToolkitMode::Standalone, nullptr, CueType);
	if (!TestTrue(TEXT("default-only proposal compiles"), Editor->CompileDataOnlyBlueprint()))
	{
		CloseEditor(Editor.ToSharedRef());
		return false;
	}
	TestTrue(TEXT("default-only compile retains recovery state until durable save"),
		Editor->HasCompiledRecoveryStateForTests());

	FPaper2DPlusFrameCueDurableSaveAttempt FailedAttempt;
	FText Error;
	if (!TestTrue(TEXT("default-only proposal prepares a save attempt"),
		FPaper2DPlusFrameCueTypeEditor::PrepareDurableSchemaSaveAttempt(
			*CueType, FailedAttempt, &Error)))
	{
		AddError(Error.ToString());
		CloseEditor(Editor.ToSharedRef());
		return false;
	}
	TestFalse(TEXT("dirty package simulates a failed default-only save"),
		FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(
			*CueType, FailedAttempt, &Error));
	if (!TestTrue(TEXT("failed default-only save restores the durable class"),
		Editor->RestoreDurableSchemaAfterFailureForTests(*CueType, &Error)))
	{
		AddError(Error.ToString());
		CloseEditor(Editor.ToSharedRef());
		return false;
	}

	UObject* RestoredDefaults = CueType->GeneratedClass
		? CueType->GeneratedClass->GetDefaultObject(false)
		: nullptr;
	const FFloatProperty* RestoredStrength = CueType->GeneratedClass
		? FindFProperty<FFloatProperty>(CueType->GeneratedClass, TEXT("Strength"))
		: nullptr;
	if (TestNotNull(TEXT("durable generated default object is restored"), RestoredDefaults)
		&& TestNotNull(TEXT("durable Strength field is restored"), RestoredStrength))
	{
		TestEqual(TEXT("failed save leaves the active CDO at its durable default"),
			RestoredStrength->GetPropertyValue_InContainer(RestoredDefaults), 1.25f);
	}

	const FBPVariableDescription* BufferedStrength = CueType->NewVariables.FindByPredicate(
		[](const FBPVariableDescription& Variable)
		{
			return Variable.VarName == TEXT("Strength");
		});
	if (TestNotNull(TEXT("failed save retains the proposed Strength field for retry"),
		BufferedStrength))
	{
		TestFalse(TEXT("retry proposal retains an explicit buffered default"),
			BufferedStrength->DefaultValue.IsEmpty());
		TestEqual(TEXT("retry buffer retains the proposed default"),
			FCString::Atof(*BufferedStrength->DefaultValue), 2.75f);
	}

	// Recovery must not depend on keeping the original toolkit alive. Closing and reopening proves
	// that the proposal is carried by the Blueprint's compiler buffer, not an editor-owned pointer.
	CloseEditor(Editor.ToSharedRef());
	Editor.Reset();
	const TSharedRef<FPaper2DPlusFrameCueTypeEditor> RetryEditor =
		MakeShared<FPaper2DPlusFrameCueTypeEditor>();
	RetryEditor->InitFrameCueTypeEditor(EToolkitMode::Standalone, nullptr, CueType);
	FPaper2DPlusFrameCueDurableSaveAttempt RetryAttempt;
	if (TestTrue(TEXT("reopened Save preparation automatically rebuilds the proposal"),
		RetryEditor->PrepareCurrentBlueprintForPersistenceForTests(RetryAttempt, &Error)))
	{
		TestTrue(TEXT("automatic retry rebuilds protected recovery state"),
			RetryEditor->HasCompiledRecoveryStateForTests());
		UObject* RetriedDefaults = CueType->GeneratedClass->GetDefaultObject(false);
		const FFloatProperty* RetriedStrength =
			FindFProperty<FFloatProperty>(CueType->GeneratedClass, TEXT("Strength"));
		if (TestNotNull(TEXT("retry generated class exposes Strength"), RetriedStrength))
		{
			TestEqual(TEXT("retry compile reapplies the proposed default to the new CDO"),
				RetriedStrength->GetPropertyValue_InContainer(RetriedDefaults), 2.75f);
		}
		CueType->GetOutermost()->SetDirtyFlag(false);
		if (TestTrue(TEXT("automatic retry candidate can complete a durable commit"),
			FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(
				*CueType, RetryAttempt, &Error)))
		{
			RetryEditor->CompleteDurableSchemaCommitForTests();
		}
	}

	CloseEditor(RetryEditor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeEditorEmptyDefaultRecoveryTest,
	"Paper2DPlus.FrameCues.CueType.EditorRecovery.EmptyStringDefaultSaveFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusFrameCueTypeEditorEmptyDefaultRecoveryTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeEditorRecoveryTest;
	UPaper2DPlusFrameCueBlueprint* CueType = MakeCompiledCueType(
		TEXT("BP_P2DP_EmptyDefaultRecovery"));
	if (!TestNotNull(TEXT("empty-default recovery Cue Type fixture"), CueType))
	{
		return false;
	}

	FEdGraphPinType MessageType;
	MessageType.PinCategory = UEdGraphSchema_K2::PC_String;
	if (!TestTrue(TEXT("fixture adds a string payload field"),
		FBlueprintEditorUtils::AddMemberVariable(
			CueType, TEXT("Message"), MessageType, FString())))
	{
		return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(CueType);
	FKismetEditorUtilities::CompileBlueprint(CueType);
	FStrProperty* MessageProperty =
		FindFProperty<FStrProperty>(CueType->GeneratedClass, TEXT("Message"));
	UObject* Defaults = CueType->GeneratedClass->GetDefaultObject(false);
	if (!TestNotNull(TEXT("fixture exposes Message"), MessageProperty)
		|| !TestNotNull(TEXT("fixture exposes generated defaults"), Defaults))
	{
		return false;
	}
	Defaults->Modify();
	MessageProperty->SetPropertyValue_InContainer(Defaults, FString(TEXT("Durable")));
	CueType->MarkPackageDirty();

	const FPaper2DPlusFrameCueDurableSchemaCandidate Baseline =
		FPaper2DPlusFrameCueTypeAuthoring::PrepareDurableSchemaSaveCandidate(*CueType);
	if (!TestTrue(TEXT("fixture establishes the nonempty durable default"), Baseline.bSuccess))
	{
		AddError(Baseline.Error.ToString());
		return false;
	}
	CueType->GetOutermost()->SetDirtyFlag(false);
	MessageProperty->SetPropertyValue_InContainer(Defaults, FString());
	CueType->MarkPackageDirty();

	TSharedPtr<FPaper2DPlusFrameCueTypeEditor> Editor =
		MakeShared<FPaper2DPlusFrameCueTypeEditor>();
	Editor->InitFrameCueTypeEditor(EToolkitMode::Standalone, nullptr, CueType);
	if (!TestTrue(TEXT("explicit empty default compiles as a protected proposal"),
		Editor->CompileDataOnlyBlueprint()))
	{
		CloseEditor(Editor.ToSharedRef());
		return false;
	}

	FPaper2DPlusFrameCueDurableSaveAttempt FailedAttempt;
	FText Error;
	if (!TestTrue(TEXT("empty-default proposal prepares a save attempt"),
		FPaper2DPlusFrameCueTypeEditor::PrepareDurableSchemaSaveAttempt(
			*CueType, FailedAttempt, &Error)))
	{
		AddError(Error.ToString());
		CloseEditor(Editor.ToSharedRef());
		return false;
	}
	TestFalse(TEXT("dirty package simulates a failed empty-default save"),
		FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(
			*CueType, FailedAttempt, &Error));
	if (!TestTrue(TEXT("failed empty-default save restores durable state"),
		Editor->RestoreDurableSchemaAfterFailureForTests(*CueType, &Error)))
	{
		AddError(Error.ToString());
		CloseEditor(Editor.ToSharedRef());
		return false;
	}

	MessageProperty = FindFProperty<FStrProperty>(CueType->GeneratedClass, TEXT("Message"));
	Defaults = CueType->GeneratedClass->GetDefaultObject(false);
	if (TestNotNull(TEXT("rollback restores Message"), MessageProperty))
	{
		TestEqual(TEXT("rollback keeps the active CDO at the durable string"),
			MessageProperty->GetPropertyValue_InContainer(Defaults), FString(TEXT("Durable")));
	}
	const FString* PendingMessage =
		CueType->PendingDefaultProposalValues.Find(FName(TEXT("Message")));
	if (TestNotNull(TEXT("pending map retains an explicit Message entry"), PendingMessage))
	{
		TestTrue(TEXT("pending map distinguishes the exact empty proposal"),
			PendingMessage->IsEmpty());
	}
	TestTrue(TEXT("rollback marks the exact pending proposal for recompilation"),
		CueType->bHasPendingDefaultProposal
		&& CueType->bPendingDefaultProposalNeedsCompile);

	CloseEditor(Editor.ToSharedRef());
	Editor.Reset();
	const TSharedRef<FPaper2DPlusFrameCueTypeEditor> RetryEditor =
		MakeShared<FPaper2DPlusFrameCueTypeEditor>();
	RetryEditor->InitFrameCueTypeEditor(EToolkitMode::Standalone, nullptr, CueType);
	FPaper2DPlusFrameCueDurableSaveAttempt RetryAttempt;
	if (TestTrue(TEXT("reopened Save preparation reapplies the exact empty proposal"),
		RetryEditor->PrepareCurrentBlueprintForPersistenceForTests(RetryAttempt, &Error)))
	{
		MessageProperty = FindFProperty<FStrProperty>(CueType->GeneratedClass, TEXT("Message"));
		Defaults = CueType->GeneratedClass->GetDefaultObject(false);
		if (TestNotNull(TEXT("retry generated class exposes Message"), MessageProperty))
		{
			TestTrue(TEXT("retry generated CDO receives the proposed empty string"),
				MessageProperty->GetPropertyValue_InContainer(Defaults).IsEmpty());
		}
		CueType->GetOutermost()->SetDirtyFlag(false);
		if (TestTrue(TEXT("empty-default retry completes its durable commit"),
			FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(
				*CueType, RetryAttempt, &Error)))
		{
			RetryEditor->CompleteDurableSchemaCommitForTests();
			TestFalse(TEXT("durable commit clears transient pending-default state"),
				CueType->bHasPendingDefaultProposal);
		}
	}

	CloseEditor(RetryEditor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeEditorDestructiveCompileRecoveryTest,
	"Paper2DPlus.FrameCues.CueType.EditorRecovery.DestructiveCompileAndSaveFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusFrameCueTypeEditorDestructiveCompileRecoveryTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeEditorRecoveryTest;
	UPaper2DPlusFrameCueBlueprint* CueType = MakeCompiledCueType(
		TEXT("BP_P2DP_DestructiveCompileRecovery"));
	if (!TestNotNull(TEXT("destructive recovery Cue Type fixture"), CueType))
	{
		return false;
	}
	const FPaper2DPlusFrameCueDurableSchemaCandidate Baseline =
		FPaper2DPlusFrameCueTypeAuthoring::PrepareDurableSchemaSaveCandidate(*CueType);
	if (!TestTrue(TEXT("fixture establishes a full durable schema baseline"), Baseline.bSuccess))
	{
		return false;
	}
	CueType->GetOutermost()->SetDirtyFlag(false);

	TStrongObjectPtr<UPaper2DPlusCharacterProfileAsset> Profile(
		NewObject<UPaper2DPlusCharacterProfileAsset>(GetTransientPackage()));
	FFlipbookProfileEntry& Entry = Profile->Flipbooks.AddDefaulted_GetRef();
	Entry.Identity.FlipbookName = TEXT("Attack");
	UPaper2DPlusCueBase* Placement = NewObject<UPaper2DPlusCueBase>(
		Profile.Get(),
		CueType->GeneratedClass,
		TEXT("RecoverablePlacement"),
		RF_Transactional);
	Entry.FrameEventData.FrameCues.Add(Placement);
	FFloatProperty* StrengthProperty =
		FindFProperty<FFloatProperty>(Placement->GetClass(), TEXT("Strength"));
	if (!TestNotNull(TEXT("fixture placement exposes durable Strength"), StrengthProperty))
	{
		return false;
	}
	StrengthProperty->SetPropertyValue_InContainer(Placement, 9.0f);

	const TSharedRef<FPaper2DPlusFrameCueTypeEditor> Editor =
		MakeShared<FPaper2DPlusFrameCueTypeEditor>();
	Editor->InitFrameCueTypeEditor(EToolkitMode::Standalone, nullptr, CueType);
	FBlueprintEditorUtils::RemoveMemberVariable(CueType, TEXT("Strength"));
	const FPaper2DPlusFrameCueSchemaPreflight DestructivePreflight =
		FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(*CueType);
	TestTrue(TEXT("removing a durable field is destructive"),
		DestructivePreflight.bRequiresDestructiveConfirmation
		&& DestructivePreflight.Diff.Compatibility
			== EPaper2DPlusFrameCueSchemaCompatibility::Destructive);

	int32 ConfirmationCount = 0;
	FPaper2DPlusFrameCueTypeEditor::SetDestructiveCompileConfirmationForTests(
		[&ConfirmationCount](const FPaper2DPlusFrameCueSchemaPreflight&)
		{
			++ConfirmationCount;
			return false;
		});
	TestFalse(TEXT("rejected destructive confirmation blocks compilation"),
		Editor->CompileDataOnlyBlueprint());
	TestEqual(TEXT("destructive confirmation is requested exactly once"), ConfirmationCount, 1);
	TestNotNull(TEXT("rejection preserves the durable generated field"),
		FindFProperty<FFloatProperty>(CueType->GeneratedClass, TEXT("Strength")));

	FPaper2DPlusFrameCueTypeEditor::SetDestructiveCompileConfirmationForTests(
		[](const FPaper2DPlusFrameCueSchemaPreflight&) { return true; });
	if (!TestTrue(TEXT("confirmed destructive edit compiles"),
		Editor->CompileDataOnlyBlueprint()))
	{
		FPaper2DPlusFrameCueTypeEditor::ClearDestructiveCompileConfirmationForTests();
		CloseEditor(Editor);
		return false;
	}
	TestTrue(TEXT("confirmed compile retains a recovery snapshot until durable save"),
		Editor->HasCompiledRecoveryStateForTests());
	TestNull(TEXT("confirmed compile removes the field from the proposed generated class"),
		FindFProperty<FFloatProperty>(CueType->GeneratedClass, TEXT("Strength")));

	FPaper2DPlusFrameCueDurableSaveAttempt FailedAttempt;
	FText Error;
	if (!TestTrue(TEXT("compiled proposal prepares a save attempt"),
		FPaper2DPlusFrameCueTypeEditor::PrepareDurableSchemaSaveAttempt(
			*CueType, FailedAttempt, &Error)))
	{
		AddError(Error.ToString());
		FPaper2DPlusFrameCueTypeEditor::ClearDestructiveCompileConfirmationForTests();
		CloseEditor(Editor);
		return false;
	}
	TestFalse(TEXT("dirty package simulates a failed durable save"),
		FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(
			*CueType, FailedAttempt, &Error));
	if (!TestTrue(TEXT("failed save restores durable class and loaded placements"),
		Editor->RestoreDurableSchemaAfterFailureForTests(*CueType, &Error)))
	{
		AddError(Error.ToString());
	}

	UPaper2DPlusCueBase* RestoredPlacement = Entry.FrameEventData.FrameCues.Num() == 1
		? Entry.FrameEventData.FrameCues[0]
		: nullptr;
	const FFloatProperty* RestoredStrength = RestoredPlacement
		? FindFProperty<FFloatProperty>(RestoredPlacement->GetClass(), TEXT("Strength"))
		: nullptr;
	if (TestNotNull(TEXT("durable generated field is restored"), RestoredStrength)
		&& TestNotNull(TEXT("placement is reacquired after reinstancing"), RestoredPlacement))
	{
		TestEqual(TEXT("placement override survives the failed destructive save"),
			RestoredStrength->GetPropertyValue_InContainer(RestoredPlacement), 9.0f);
		TestTrue(TEXT("restored placement keeps its owning Profile"),
			RestoredPlacement->GetOuter() == Profile.Get());
	}
	TestFalse(TEXT("the rejected-on-disk proposal remains in the variable model for retry"),
		CueType->NewVariables.ContainsByPredicate(
			[](const FBPVariableDescription& Variable)
			{
				return Variable.VarName == TEXT("Strength");
			}));
	TestTrue(TEXT("failed save leaves the proposal dirty and not placement-ready"),
		CueType->GetOutermost()->IsDirty()
		&& !FPaper2DPlusFrameCueTypeEditor::IsDurablyReadyForPlacement(*CueType));

	FPaper2DPlusFrameCueTypeEditor::ClearDestructiveCompileConfirmationForTests();
	CloseEditor(Editor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeEditorLegacyRecoveryTest,
	"Paper2DPlus.FrameCues.CueType.EditorRecovery.LegacyGraphBanner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusFrameCueTypeEditorLegacyRecoveryTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeEditorRecoveryTest;

	UBlueprint* Legacy = NewObject<UBlueprint>(
		GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(),
			FName(TEXT("BP_P2DP_GenericLegacyCue"))),
		RF_Transactional);
	Legacy->ParentClass = UPaper2DPlusCue::StaticClass();
	AddLegacyGraph(*Legacy, Legacy->UbergraphPages, TEXT("LegacyEvent"));
	AddLegacyGraph(*Legacy, Legacy->FunctionGraphs, TEXT("LegacyFunction"));
	AddLegacyGraph(*Legacy, Legacy->MacroGraphs, TEXT("LegacyMacro"));
	AddLegacyGraph(*Legacy, Legacy->DelegateSignatureGraphs, TEXT("LegacyDelegate"));

	TestTrue(
		TEXT("explicit recovery mode accepts a generic Cue Blueprint"),
		FPaper2DPlusFrameCueTypeEditor::IsSupportedBlueprint(Legacy));
	TestTrue(
		TEXT("behavior-bearing legacy Cue exposes the recovery action"),
		FPaper2DPlusFrameCueTypeEditor::ShouldOfferLegacyBlueprintRecovery(*Legacy));
	const FString Warning =
		FPaper2DPlusFrameCueTypeEditor::BuildLegacyContentWarningText(*Legacy).ToString();
	TestTrue(TEXT("banner reports the exact total graph count"), Warning.Contains(TEXT("4 graphs")));
	TestTrue(TEXT("banner reports the Event graph count"), Warning.Contains(TEXT("1 Event")));
	TestTrue(TEXT("banner reports the Function graph count"), Warning.Contains(TEXT("1 Function")));
	TestTrue(TEXT("banner reports the Macro graph count"), Warning.Contains(TEXT("1 Macro")));
	TestTrue(TEXT("banner reports the Delegate graph count"), Warning.Contains(TEXT("1 Delegate")));
	TestTrue(TEXT("banner reports the exact preserved node count"), Warning.Contains(TEXT("4 nodes")));

	bool bRecoveryCommandRan = false;
	TestTrue(
		TEXT("warning-gated command seam runs for behavior-bearing legacy content"),
		FPaper2DPlusFrameCueTypeEditor::ExecuteLegacyBlueprintRecovery(
			*Legacy,
			[&bRecoveryCommandRan](UBlueprint& Blueprint)
			{
				bRecoveryCommandRan = true;
				return true;
			}));
	TestTrue(TEXT("generic editor recovery command was invoked"), bRecoveryCommandRan);
	TestTrue(
		TEXT("restricted warning and recovery routing preserve every legacy graph array"),
		Legacy->UbergraphPages.Num() == 1
			&& Legacy->FunctionGraphs.Num() == 1
			&& Legacy->MacroGraphs.Num() == 1
			&& Legacy->DelegateSignatureGraphs.Num() == 1);

	UPaper2DPlusFrameCueBlueprint* Graphless = MakeCompiledCueType(
		TEXT("BP_P2DP_GraphlessCueNoRecovery"));
	if (!TestNotNull(TEXT("graphless specialized fixture"), Graphless))
	{
		return false;
	}
	bRecoveryCommandRan = false;
	TestFalse(
		TEXT("graphless specialized Cue Type never offers generic Blueprint recovery"),
		FPaper2DPlusFrameCueTypeEditor::ShouldOfferLegacyBlueprintRecovery(*Graphless));
	TestFalse(
		TEXT("recovery seam rejects a graphless specialized Cue Type"),
		FPaper2DPlusFrameCueTypeEditor::ExecuteLegacyBlueprintRecovery(
			*Graphless,
			[&bRecoveryCommandRan](UBlueprint& Blueprint)
			{
				bRecoveryCommandRan = true;
				return true;
			}));
	TestFalse(TEXT("rejected recovery never invokes the generic editor command"), bRecoveryCommandRan);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeConfirmedDestructiveOrdinarySaveTest,
	"Paper2DPlus.FrameCues.CueType.EditorRecovery.ConfirmedDestructiveEditCommitsThroughOrdinarySave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusFrameCueTypeConfirmedDestructiveOrdinarySaveTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeEditorRecoveryTest;
	UPaper2DPlusFrameCueBlueprint* CueType = MakeCompiledCueType(
		TEXT("BP_P2DP_ConfirmedDestructiveOrdinarySave"));
	if (!TestNotNull(TEXT("confirmed-destructive ordinary-save fixture"), CueType))
	{
		return false;
	}
	const FPaper2DPlusFrameCueDurableSchemaCandidate Baseline =
		FPaper2DPlusFrameCueTypeAuthoring::PrepareDurableSchemaSaveCandidate(*CueType);
	if (!TestTrue(TEXT("fixture establishes a full durable schema baseline"), Baseline.bSuccess))
	{
		return false;
	}
	CueType->GetOutermost()->SetDirtyFlag(false);

	const TSharedRef<FPaper2DPlusFrameCueTypeEditor> Editor =
		MakeShared<FPaper2DPlusFrameCueTypeEditor>();
	Editor->InitFrameCueTypeEditor(EToolkitMode::Standalone, nullptr, CueType);
	ON_SCOPE_EXIT
	{
		FPaper2DPlusFrameCueTypeEditor::ClearDestructiveCompileConfirmationForTests();
	};

	FBlueprintEditorUtils::RemoveMemberVariable(CueType, TEXT("Strength"));
	FPaper2DPlusFrameCueTypeEditor::SetDestructiveCompileConfirmationForTests(
		[](const FPaper2DPlusFrameCueSchemaPreflight&) { return true; });
	if (!TestTrue(TEXT("confirmed destructive removal compiles"),
		Editor->CompileDataOnlyBlueprint()))
	{
		CloseEditor(Editor);
		return false;
	}

	const FPaper2DPlusFrameCueSchemaPreflight ConfirmedPreflight =
		FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(*CueType);
	TestTrue(TEXT("confirmed compile records the reviewed authored-schema fingerprint"),
		!CueType->ConfirmedDestructiveSchemaFingerprint.IsEmpty()
		&& CueType->ConfirmedDestructiveSchemaFingerprint
			== ConfirmedPreflight.CurrentSchema.Fingerprint);

	// The recorded confirmation is fingerprint-exact: one more schema edit must diverge from it,
	// which is precisely what re-arms the ordinary-save refusal for unreviewed changes.
	FEdGraphPinType ExtraType;
	ExtraType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	if (TestTrue(TEXT("a further schema edit is authorable"),
		FBlueprintEditorUtils::AddMemberVariable(
			CueType, TEXT("ExtraUnreviewed"), ExtraType, TEXT("true"))))
	{
		const FPaper2DPlusFrameCueSchemaPreflight DivergedPreflight =
			FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(*CueType);
		TestTrue(TEXT("an unreviewed follow-up edit no longer matches the confirmation"),
			DivergedPreflight.CurrentSchema.Fingerprint
				!= CueType->ConfirmedDestructiveSchemaFingerprint);
		FBlueprintEditorUtils::RemoveMemberVariable(CueType, TEXT("ExtraUnreviewed"));
		const FPaper2DPlusFrameCueSchemaPreflight RestoredPreflight =
			FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(*CueType);
		TestTrue(TEXT("undoing the follow-up edit restores the confirmed fingerprint"),
			RestoredPreflight.CurrentSchema.Fingerprint
				== CueType->ConfirmedDestructiveSchemaFingerprint);
	}

	// The designer's actual gesture: an ordinary package save (Save All / Content Browser Save)
	// with no further visit to the restricted editor. The confirmed change must stage and commit.
	const FString PackageName = CueType->GetOutermost()->GetName();
	const FString SaveFilename = FPackageName::LongPackageNameToFilename(
		PackageName,
		FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SaveFilename), /*Tree=*/true);
	ON_SCOPE_EXIT
	{
		if (UPackage* PackageToUnload = FindPackage(nullptr, *PackageName))
		{
			PackageToUnload->SetDirtyFlag(false);
			if (PackageToUnload->IsRooted())
			{
				PackageToUnload->RemoveFromRoot();
			}
			TArray<UPackage*> PackagesToUnload{PackageToUnload};
			FText UnloadError;
			if (!UPackageTools::UnloadPackages(
					PackagesToUnload,
					UnloadError,
					/*bUnloadDirtyPackages=*/true))
			{
				AddError(FString::Printf(
					TEXT("confirmed-destructive save cleanup could not unload its package: %s"),
					*UnloadError.ToString()));
			}
		}
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		IFileManager& FileManager = IFileManager::Get();
		const auto DeleteSavedArtifact = [this, &FileManager](const FString& Filename)
		{
			if (FileManager.FileExists(*Filename)
				&& !FileManager.Delete(*Filename, false, true, true))
			{
				AddError(FString::Printf(
					TEXT("confirmed-destructive save cleanup could not delete '%s'."),
					*Filename));
			}
		};
		DeleteSavedArtifact(SaveFilename);
		DeleteSavedArtifact(FPaths::ChangeExtension(SaveFilename, TEXT("uexp")));
		DeleteSavedArtifact(FPaths::ChangeExtension(SaveFilename, TEXT("ubulk")));
		DeleteSavedArtifact(FPaths::ChangeExtension(SaveFilename, TEXT("uptnl")));
	};

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const bool bOrdinarySaveSucceeded = UPackage::SavePackage(
		CueType->GetOutermost(),
		CueType,
		*SaveFilename,
		SaveArgs);
	TestTrue(
		TEXT("an ordinary save commits the compile-confirmed destructive schema change"),
		bOrdinarySaveSucceeded);
	FTSTicker::GetCoreTicker().Tick(0.0f);

	if (bOrdinarySaveSucceeded)
	{
		FText Error;
		TestTrue(TEXT("ordinary save advanced the durable baseline to the confirmed schema"),
			FPaper2DPlusFrameCueTypeAuthoring::ValidateDurableSaveMetadataForPersistence(
				*CueType, &Error));
		TestNull(TEXT("the removed durable field stays removed after the commit"),
			FindFProperty<FFloatProperty>(CueType->GeneratedClass, TEXT("Strength")));
		TestTrue(TEXT("the commit consumes the one-shot confirmation"),
			CueType->ConfirmedDestructiveSchemaFingerprint.IsEmpty());
		const FPaper2DPlusFrameCueSchemaPreflight CommittedPreflight =
			FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(*CueType);
		TestTrue(TEXT("the committed asset reads back as an identical, compilable baseline"),
			CommittedPreflight.bHasDurableBaseline
			&& CommittedPreflight.bCanCompile
			&& CommittedPreflight.Diff.Compatibility
				== EPaper2DPlusFrameCueSchemaCompatibility::Identical);
	}

	CloseEditor(Editor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeBaselineRecoveryCompileEscapeTest,
	"Paper2DPlus.FrameCues.CueType.EditorRecovery.UntrustedBaselineCompileEscape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusFrameCueTypeBaselineRecoveryCompileEscapeTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeEditorRecoveryTest;
	UPaper2DPlusFrameCueBlueprint* CueType = MakeCompiledCueType(
		TEXT("BP_P2DP_BaselineRecoveryEscape"));
	if (!TestNotNull(TEXT("baseline-recovery escape fixture"), CueType))
	{
		return false;
	}
	const FPaper2DPlusFrameCueDurableSchemaCandidate Baseline =
		FPaper2DPlusFrameCueTypeAuthoring::PrepareDurableSchemaSaveCandidate(*CueType);
	if (!TestTrue(TEXT("fixture establishes a full durable schema baseline"), Baseline.bSuccess))
	{
		return false;
	}

	// The user-reported wedge shape: a non-empty durable fingerprint whose snapshot cannot back
	// it. Every schema compile refused, while the same broken baseline also failed every save —
	// and the refusal used to demand exactly the save that could not happen.
	CueType->DurableSchemaSnapshot = TEXT("not-a-schema-snapshot");
	const FPaper2DPlusFrameCueSchemaPreflight WedgedPreflight =
		FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(*CueType);
	TestTrue(TEXT("an unreadable snapshot behind a live fingerprint blocks schema compiles"),
		!WedgedPreflight.bCanCompile && !WedgedPreflight.bHasDurableBaseline);
	TestTrue(TEXT("the wedge names the baseline, not the authored schema"),
		WedgedPreflight.CurrentSchemaError.IsEmpty()
		&& !WedgedPreflight.DurableBaselineError.IsEmpty());

	const TSharedRef<FPaper2DPlusFrameCueTypeEditor> Editor =
		MakeShared<FPaper2DPlusFrameCueTypeEditor>();
	Editor->InitFrameCueTypeEditor(EToolkitMode::Standalone, nullptr, CueType);
	ON_SCOPE_EXIT
	{
		FPaper2DPlusFrameCueTypeEditor::ClearBaselineRecoveryCompileConfirmationForTests();
		FPaper2DPlusFrameCueTypeEditor::ClearDestructiveCompileConfirmationForTests();
	};

	int32 DestructiveConfirmations = 0;
	FPaper2DPlusFrameCueTypeEditor::SetDestructiveCompileConfirmationForTests(
		[&DestructiveConfirmations](const FPaper2DPlusFrameCueSchemaPreflight&)
		{
			++DestructiveConfirmations;
			return true;
		});
	int32 RecoveryConfirmations = 0;
	FPaper2DPlusFrameCueTypeEditor::SetBaselineRecoveryCompileConfirmationForTests(
		[&RecoveryConfirmations](const FPaper2DPlusFrameCueSchemaPreflight&)
		{
			++RecoveryConfirmations;
			return false;
		});
	TestFalse(TEXT("a declined baseline-recovery consent still blocks the compile"),
		Editor->CompileDataOnlyBlueprint());
	TestEqual(TEXT("the baseline-recovery consent is requested exactly once"),
		RecoveryConfirmations, 1);

	FPaper2DPlusFrameCueTypeEditor::SetBaselineRecoveryCompileConfirmationForTests(
		[&RecoveryConfirmations](const FPaper2DPlusFrameCueSchemaPreflight&)
		{
			++RecoveryConfirmations;
			return true;
		});
	if (!TestTrue(TEXT("granted baseline-recovery consent lets the compile proceed"),
		Editor->CompileDataOnlyBlueprint()))
	{
		CloseEditor(Editor);
		return false;
	}
	TestEqual(TEXT("the destructive dialog never fires for the baseline-recovery escape"),
		DestructiveConfirmations, 0);
	const FPaper2DPlusFrameCueSchemaPreflight EscapedPreflight =
		FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(*CueType);
	TestTrue(TEXT("the consented compile records the reviewed authored-schema fingerprint"),
		!CueType->ConfirmedDestructiveSchemaFingerprint.IsEmpty()
		&& CueType->ConfirmedDestructiveSchemaFingerprint
			== EscapedPreflight.CurrentSchema.Fingerprint);

	// The consent dialog's promise: the next successful save re-establishes the baseline from
	// the compiled schema.
	FPaper2DPlusFrameCueDurableSaveAttempt RepairAttempt;
	FText Error;
	if (!TestTrue(TEXT("the protected save path stages a repair candidate"),
		FPaper2DPlusFrameCueTypeEditor::PrepareDurableSchemaSaveAttempt(
			*CueType, RepairAttempt, &Error)))
	{
		AddError(Error.ToString());
		CloseEditor(Editor);
		return false;
	}
	CueType->GetOutermost()->SetDirtyFlag(false);
	if (TestTrue(TEXT("the repair candidate completes its durable commit"),
		FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(
			*CueType, RepairAttempt, &Error)))
	{
		Editor->CompleteDurableSchemaCommitForTests();
		const FPaper2DPlusFrameCueSchemaPreflight RepairedPreflight =
			FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(*CueType);
		TestTrue(TEXT("a successful save rebuilds a trusted, compilable baseline"),
			RepairedPreflight.bHasDurableBaseline
			&& RepairedPreflight.bCanCompile
			&& RepairedPreflight.Diff.Compatibility
				== EPaper2DPlusFrameCueSchemaCompatibility::Identical);
		TestTrue(TEXT("the durable commit consumes the recovery confirmation"),
			CueType->ConfirmedDestructiveSchemaFingerprint.IsEmpty());
	}

	CloseEditor(Editor);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
