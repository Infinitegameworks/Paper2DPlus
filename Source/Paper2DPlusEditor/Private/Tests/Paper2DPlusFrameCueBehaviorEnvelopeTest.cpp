// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/TimelineTemplate.h"
#include "FrameCues/K2Node_ListenForFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "FrameCues/Paper2DPlusFrameCueTypeEditor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_MacroInstance.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/EngineVersionComparison.h"
#include "PackageTools.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "Widgets/Docking/SDockTab.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
#include "Subsystems/AssetEditorSubsystem.h"
#endif

namespace Paper2DPlusCueBehaviorEnvelopeTest
{
	const FName OnCueTriggeredName(TEXT("OnCueTriggered"));
	const FName OnCueBeginName(TEXT("OnCueBegin"));

	/** Real (non-transient) package so availability classification can observe a clean save state. */
	struct FCueBehavior_ScopedPackage
	{
		FCueBehavior_ScopedPackage(FAutomationTestBase& InTest, const TCHAR* Stem)
			: Test(InTest)
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

		~FCueBehavior_ScopedPackage()
		{
			if (!Package)
			{
				return;
			}
			Package->SetDirtyFlag(false);
			if (Package->IsRooted())
			{
				Package->RemoveFromRoot();
			}
			FText Error;
			TArray<UPackage*> Packages = { Package };
			if (!UPackageTools::UnloadPackages(Packages, Error, true))
			{
				Test.AddError(FString::Printf(
					TEXT("Cue behavior fixture cleanup failed for '%s': %s"),
					*PackageName,
					*Error.ToString()));
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}

		FAutomationTestBase& Test;
		FString PackageName;
		UPackage* Package = nullptr;
	};

	UPaper2DPlusFrameCueBlueprint* CueBehavior_MakeCueType(
		UClass* ParentClass,
		const TCHAR* Stem,
		UObject* Outer = nullptr)
	{
		Outer = Outer ? Outer : GetTransientPackage();
		UPaper2DPlusFrameCueBlueprint* Blueprint =
			Cast<UPaper2DPlusFrameCueBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				ParentClass,
				Outer,
				MakeUniqueObjectName(
					Outer,
					UPaper2DPlusFrameCueBlueprint::StaticClass(),
					FName(Stem)),
				BPTYPE_Normal,
				UPaper2DPlusFrameCueBlueprint::StaticClass(),
				UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass(),
				NAME_None));
		if (!Blueprint
			|| !FPaper2DPlusFrameCueTypeEditor::PrepareNewlyCreatedBlueprint(*Blueprint))
		{
			return nullptr;
		}
		return Blueprint->Status == BS_Error ? nullptr : Blueprint;
	}

	/** Reduces a freshly seeded Cue Type to the exact shape a pre-behavior asset was saved in. */
	bool CueBehavior_StripToLegacyShape(UPaper2DPlusFrameCueBlueprint& Blueprint)
	{
		while (Blueprint.UbergraphPages.Num() > 0)
		{
			UEdGraph* EventGraph = Blueprint.UbergraphPages[0];
			if (!EventGraph)
			{
				Blueprint.UbergraphPages.RemoveAt(0);
				continue;
			}
			FBlueprintEditorUtils::RemoveGraph(
				&Blueprint, EventGraph, EGraphRemoveFlags::MarkTransient);
		}
		Blueprint.LastEditedDocuments.Reset();
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		FKismetEditorUtilities::CompileBlueprint(&Blueprint);
		return Blueprint.UbergraphPages.Num() == 0 && Blueprint.Status != BS_Error;
	}

	/** Keeps a fixture alive for as long as a toolkit is open on it. */
	struct FCueBehavior_ScopedRoot
	{
		explicit FCueBehavior_ScopedRoot(UObject* InObject)
			: Object(InObject)
		{
			if (Object)
			{
				Object->AddToRoot();
			}
		}

		~FCueBehavior_ScopedRoot()
		{
			if (Object && Object->IsRooted())
			{
				Object->RemoveFromRoot();
			}
		}

		UObject* Object = nullptr;
	};

	/** Closes an opened restricted toolkit exactly once, whatever the test does afterwards. */
	struct FCueBehavior_ScopedEditorClose
	{
		explicit FCueBehavior_ScopedEditorClose(
			const TSharedRef<FPaper2DPlusFrameCueTypeEditor>& InEditor)
			: Editor(InEditor)
		{
		}

		~FCueBehavior_ScopedEditorClose()
		{
			Close();
		}

		void Close()
		{
			if (!Editor.IsValid())
			{
				return;
			}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
			Editor->CloseWindow(EAssetEditorCloseReason::AssetUnloadingOrInvalid);
#else
			Editor->CloseWindow();
#endif
			Editor.Reset();
		}

		TSharedPtr<FPaper2DPlusFrameCueTypeEditor> Editor;
	};

	/** Every remembered document entry, in order, as the asset persists them. */
	bool CueBehavior_DocumentPathsMatch(
		const TArray<FEditedDocumentInfo>& Actual,
		const TArray<FEditedDocumentInfo>& Expected)
	{
		if (Actual.Num() != Expected.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			if (Actual[Index].EditedObjectPath != Expected[Index].EditedObjectPath)
			{
				return false;
			}
		}
		return true;
	}

	/**
	 * Turns a seeded stub into real behavior.
	 *
	 * Creation seeds an automatically placed ghost node, exactly like the engine's BeginPlay stub:
	 * it is visible and implementable but compiles to nothing until the designer authors into it.
	 * Enabling the seeded nodes is the minimal equivalent of that first authoring action.
	 */
	bool CueBehavior_ImplementSeededEvent(
		UPaper2DPlusFrameCueBlueprint& Blueprint,
		const FName EventName)
	{
		UK2Node_Event* EventNode =
			FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventNode(Blueprint, EventName);
		if (!EventNode)
		{
			return false;
		}
		UEdGraph* BehaviorGraph = EventNode->GetGraph();
		if (!BehaviorGraph)
		{
			return false;
		}
		for (const TObjectPtr<UEdGraphNode>& Node : BehaviorGraph->Nodes)
		{
			if (Node && Node->IsAutomaticallyPlacedGhostNode())
			{
				Node->SetEnabledState(ENodeEnabledState::Enabled, true);
			}
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		FKismetEditorUtilities::CompileBlueprint(&Blueprint);
		return Blueprint.Status != BS_Error;
	}

	bool CueBehavior_AddCustomEvent(
		UPaper2DPlusFrameCueBlueprint& Blueprint,
		const FName CustomEventName)
	{
		UEdGraph* EventGraph = FPaper2DPlusFrameCueTypeAuthoring::EnsureBehaviorEventGraph(Blueprint);
		if (!EventGraph)
		{
			return false;
		}
		UK2Node_CustomEvent* CustomEvent = NewObject<UK2Node_CustomEvent>(EventGraph);
		CustomEvent->CustomFunctionName = CustomEventName;
		CustomEvent->bIsEditable = true;
		CustomEvent->CreateNewGuid();
		CustomEvent->PostPlacedNewNode();
		CustomEvent->AllocateDefaultPins();
		CustomEvent->NodePosY = 600;
		EventGraph->AddNode(CustomEvent, false, false);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		FKismetEditorUtilities::CompileBlueprint(&Blueprint);
		return true;
	}

	UK2Node_CallFunction* CueBehavior_AddCall(
		UEdGraph& Graph,
		UClass& FunctionOwner,
		const FName FunctionName)
	{
		FGraphNodeCreator<UK2Node_CallFunction> Creator(Graph);
		UK2Node_CallFunction* Node = Creator.CreateNode(false);
		Node->FunctionReference.SetExternalMember(FunctionName, &FunctionOwner);
		Creator.Finalize();
		return Node;
	}

	UK2Node_ListenForFrameCue* CueBehavior_AddAsyncNode(UEdGraph& Graph)
	{
		UK2Node_ListenForFrameCue* Node =
			NewObject<UK2Node_ListenForFrameCue>(&Graph);
		Node->CreateNewGuid();
		Node->AllocateDefaultPins();
		Graph.AddNode(Node, false, false);
		return Node;
	}

	UK2Node_MacroInstance* CueBehavior_AddMacroInstance(
		UEdGraph& Graph,
		UEdGraph& MacroGraph)
	{
		UK2Node_MacroInstance* Node =
			NewObject<UK2Node_MacroInstance>(&Graph);
		Node->SetMacroGraph(&MacroGraph);
		Node->CreateNewGuid();
		Graph.AddNode(Node, false, false);
		return Node;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorEnvelopeAcceptTest,
	"Paper2DPlus.FrameCues.CueType.Behavior.EnvelopePermitsDeclaredEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorEnvelopeAcceptTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorEnvelopeTest;

	FCueBehavior_ScopedPackage Fixture(*this, TEXT("CueBehaviorAccept"));
	if (!TestNotNull(TEXT("Behavior fixture package"), Fixture.Package))
	{
		return false;
	}

	UPaper2DPlusFrameCueBlueprint* Blueprint = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(),
		TEXT("BP_P2DP_BehaviorAccept"),
		Fixture.Package);
	if (!TestNotNull(TEXT("Behavior-carrying Cue Type fixture"), Blueprint))
	{
		return false;
	}

	TestNotNull(TEXT("Creation pre-seeds the declared Moment behavior event"),
		FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventNode(*Blueprint, OnCueTriggeredName));

	// A seeded-but-unauthored Cue Type is still behavior-free, so its schema must stay at version 1
	// with the exact fingerprint a pre-behavior build produced.
	FPaper2DPlusFrameCueSchema SeededSchema;
	FText SeedSchemaError;
	if (TestTrue(TEXT("A freshly seeded Cue Type describes a compiled schema"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeSchema(
			*Blueprint,
			EPaper2DPlusFrameCueSchemaSource::Compiled,
			SeededSchema,
			&SeedSchemaError)))
	{
		TestEqual(TEXT("An unauthored seeded stub does not raise the schema version"),
			SeededSchema.Version,
			FPaper2DPlusFrameCueTypeAuthoring::BehaviorFreeSchemaVersion);
		TestEqual(TEXT("An unauthored seeded stub declares no behavior events"),
			SeededSchema.BehaviorEvents.Num(), 0);
	}
	FPaper2DPlusFrameCueSchema SeededAuthoredSchema;
	if (FPaper2DPlusFrameCueTypeAuthoring::DescribeSchema(
		*Blueprint,
		EPaper2DPlusFrameCueSchemaSource::Authored,
		SeededAuthoredSchema,
		&SeedSchemaError))
	{
		TestEqual(TEXT("Authored and compiled behavior descriptions agree on a seeded stub"),
			SeededAuthoredSchema.BehaviorEvents.Num(), 0);
	}

	if (!TestTrue(TEXT("The seeded stub can be authored into real behavior"),
		CueBehavior_ImplementSeededEvent(*Blueprint, OnCueTriggeredName)))
	{
		return false;
	}

	FText ContractError;
	TestTrue(TEXT("Authoring contract accepts an implemented declared Cue event"),
		UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(*Blueprint, &ContractError));
	TestTrue(TEXT("Compiled contract accepts an implemented declared Cue event"),
		UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
			*Blueprint, &ContractError, Blueprint->GeneratedClass));
	TestTrue(TEXT("Accepted behavior reports no envelope error"), ContractError.IsEmpty());
	TestTrue(TEXT("Editor/runtime schema parity accepts the generated behavior class"),
		FPaper2DPlusFrameCueTypeAuthoring::ValidateCompiledSchemaParity(*Blueprint, &ContractError));

	UBlueprintGeneratedClass* GeneratedClass =
		Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass.Get());
	if (!TestNotNull(TEXT("Behavior fixture compiles a generated class"), GeneratedClass))
	{
		return false;
	}
	TestTrue(TEXT("Implemented behavior produces the event-graph function"),
		GeneratedClass->UberGraphFunction != nullptr);
	TestTrue(TEXT("The event-graph function is allowlisted by the envelope"),
		UPaper2DPlusFrameCueBlueprint::IsPermittedGeneratedCueFunction(
			*Blueprint, GeneratedClass->UberGraphFunction));

	const UFunction* EventStub = GeneratedClass->FindFunctionByName(
		OnCueTriggeredName, EIncludeSuperFlag::ExcludeSuper);
	TestNotNull(TEXT("Implemented behavior produces the declared event override"), EventStub);
	TestTrue(TEXT("The declared event override is allowlisted by the envelope"),
		UPaper2DPlusFrameCueBlueprint::IsPermittedGeneratedCueFunction(*Blueprint, EventStub));

	if (GeneratedClass->UberGraphFramePointerProperty)
	{
		TestTrue(TEXT("The generated event-graph frame property is exempted from payload parity"),
			UPaper2DPlusFrameCueBlueprint::IsGeneratedEventGraphFrameProperty(
				GeneratedClass, GeneratedClass->UberGraphFramePointerProperty));
	}

	FPaper2DPlusFrameCueSchema CompiledSchema;
	FText SchemaError;
	if (TestTrue(TEXT("Behavior-carrying Cue Type describes a compiled schema"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeSchema(
			*Blueprint,
			EPaper2DPlusFrameCueSchemaSource::Compiled,
			CompiledSchema,
			&SchemaError)))
	{
		TestEqual(TEXT("Behavior raises the described schema to version 2"),
			CompiledSchema.Version,
			FPaper2DPlusFrameCueTypeAuthoring::CurrentSchemaVersion);
		TestTrue(TEXT("The described schema names the implemented behavior event"),
			CompiledSchema.BehaviorEvents.Contains(OnCueTriggeredName));
		TestTrue(TEXT("The behavior digest reaches the fingerprinted canonical text"),
			CompiledSchema.CanonicalText.Contains(TEXT("behavior;")));
	}

	const FPaper2DPlusFrameCueDurableSchemaCandidate Candidate =
		FPaper2DPlusFrameCueTypeAuthoring::PrepareDurableSchemaSaveCandidate(*Blueprint);
	TestTrue(TEXT("Behavior-carrying Cue Type stages a durable save candidate"), Candidate.bSuccess);
	TestEqual(TEXT("Durable staging stamps the current schema version"),
		Candidate.Version,
		FPaper2DPlusFrameCueTypeAuthoring::CurrentSchemaVersion);
	TestFalse(TEXT("Durable staging produces a fingerprint"), Candidate.Fingerprint.IsEmpty());
	TestTrue(TEXT("The staged behavior baseline satisfies the durable-save guard"),
		FPaper2DPlusFrameCueTypeAuthoring::ValidateDurableSaveMetadataForPersistence(
			*Blueprint, &ContractError));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorEnvelopeRejectTest,
	"Paper2DPlus.FrameCues.CueType.Behavior.EnvelopeRejectsEverythingElse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorEnvelopeRejectTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorEnvelopeTest;

	// The refusal reaches the compiler log only on engines whose compile runs full-Blueprint data
	// validation (BP.bDoFullDataValidationDuringCompilation, UE 5.6+, default on) - older engines
	// validate only the CDO at compile, so no log line exists there to expect. The substantive
	// assertions below prove the envelope refusal itself on every engine either way.
	if (const IConsoleVariable* FullBlueprintValidation =
			IConsoleManager::Get().FindConsoleVariable(
				TEXT("BP.bDoFullDataValidationDuringCompilation"));
		FullBlueprintValidation && FullBlueprintValidation->GetBool())
	{
		AddExpectedError(
			TEXT("is not one of the declared Cue behavior events"),
			EAutomationExpectedErrorFlags::Contains,
			0);
	}

	// A custom event lives in the permitted graph yet compiles to an own function that is not one of
	// the declared Cue events. This is the exact smuggling shape the allowlist has to fail closed on.
	UPaper2DPlusFrameCueBlueprint* SmuggledFunction = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_BehaviorSmuggledFunction"));
	if (TestNotNull(TEXT("Smuggled-function fixture"), SmuggledFunction))
	{
		TestTrue(TEXT("Smuggled custom event is authored into the permitted graph"),
			CueBehavior_AddCustomEvent(*SmuggledFunction, TEXT("SmuggledCueBehavior")));
		FText SmuggledError;
		TestFalse(TEXT("Compiled envelope rejects a non-allowlisted generated function"),
			UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
				*SmuggledFunction, &SmuggledError, SmuggledFunction->GeneratedClass));
		TestFalse(TEXT("Rejected smuggled function reports an actionable error"),
			SmuggledError.IsEmpty());
		const UFunction* Smuggled = SmuggledFunction->GeneratedClass
			? SmuggledFunction->GeneratedClass->FindFunctionByName(
				TEXT("SmuggledCueBehavior"), EIncludeSuperFlag::ExcludeSuper)
			: nullptr;
		if (Smuggled)
		{
			TestFalse(TEXT("Allowlist refuses the smuggled generated function"),
				UPaper2DPlusFrameCueBlueprint::IsPermittedGeneratedCueFunction(
					*SmuggledFunction, Smuggled));
		}
	}

	FText Error;

	UPaper2DPlusFrameCueBlueprint* DelegateFixture = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_BehaviorDelegateField"));
	if (TestNotNull(TEXT("Delegate rejection fixture"), DelegateFixture))
	{
		FEdGraphPinType IntegerType;
		IntegerType.PinCategory = UEdGraphSchema_K2::PC_Int;
		FBlueprintEditorUtils::AddMemberVariable(
			DelegateFixture, TEXT("OnSomething"), IntegerType, FString());
		if (FBPVariableDescription* Field = DelegateFixture->NewVariables.FindByPredicate(
			[](const FBPVariableDescription& Variable)
			{
				return Variable.VarName == TEXT("OnSomething");
			}))
		{
			Field->VarType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
		}
		TestFalse(TEXT("Delegate payload fields are still rejected"),
			UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(*DelegateFixture, &Error));
	}

	UPaper2DPlusFrameCueBlueprint* ReplicationFixture = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_BehaviorReplicatedField"));
	if (TestNotNull(TEXT("Replication rejection fixture"), ReplicationFixture))
	{
		FEdGraphPinType IntegerType;
		IntegerType.PinCategory = UEdGraphSchema_K2::PC_Int;
		FBlueprintEditorUtils::AddMemberVariable(
			ReplicationFixture, TEXT("Power"), IntegerType, TEXT("3"));
		if (FBPVariableDescription* Power = ReplicationFixture->NewVariables.FindByPredicate(
			[](const FBPVariableDescription& Variable)
			{
				return Variable.VarName == TEXT("Power");
			}))
		{
			Power->PropertyFlags |= CPF_Net;
		}
		TestFalse(TEXT("Replicated payload fields are still rejected"),
			UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(*ReplicationFixture, &Error));
	}

	UPaper2DPlusFrameCueBlueprint* ComponentFixture = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_BehaviorComponents"));
	if (TestNotNull(TEXT("Component rejection fixture"), ComponentFixture))
	{
		ComponentFixture->SimpleConstructionScript =
			NewObject<USimpleConstructionScript>(ComponentFixture);
		TestFalse(TEXT("Components and construction scripts are still rejected"),
			UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(*ComponentFixture, &Error));
		ComponentFixture->SimpleConstructionScript = nullptr;
	}

	UPaper2DPlusFrameCueBlueprint* TimelineFixture = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_BehaviorTimelines"));
	if (TestNotNull(TEXT("Timeline rejection fixture"), TimelineFixture))
	{
		TimelineFixture->Timelines.Add(NewObject<UTimelineTemplate>(TimelineFixture));
		TestFalse(TEXT("Timelines are still rejected"),
			UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(*TimelineFixture, &Error));
		TimelineFixture->Timelines.Reset();
	}

	UPaper2DPlusFrameCueBlueprint* InterfaceFixture = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_BehaviorInterfaces"));
	if (TestNotNull(TEXT("Interface rejection fixture"), InterfaceFixture))
	{
		InterfaceFixture->ImplementedInterfaces.Add(FBPInterfaceDescription());
		TestFalse(TEXT("Blueprint interfaces are still rejected"),
			UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(*InterfaceFixture, &Error));
		InterfaceFixture->ImplementedInterfaces.Reset();
	}

	UPaper2DPlusFrameCueBlueprint* MacroFixture = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_BehaviorMacros"));
	if (TestNotNull(TEXT("Macro rejection fixture"), MacroFixture))
	{
		UEdGraph* MacroGraph = FBlueprintEditorUtils::CreateNewGraph(
			MacroFixture,
			TEXT("SmuggledMacro"),
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());
		MacroFixture->MacroGraphs.Add(MacroGraph);
		TestFalse(TEXT("Macro graphs are still rejected"),
			UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(*MacroFixture, &Error));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorSchemaCompatibilityTest,
	"Paper2DPlus.FrameCues.CueType.Behavior.SchemaV1StaysReadyWithoutRestaging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorSchemaCompatibilityTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorEnvelopeTest;

	TestTrue(TEXT("The pre-behavior schema stamp stays acceptable"),
		FPaper2DPlusFrameCueTypeAuthoring::IsAcceptedDurableSchemaVersion(
			FPaper2DPlusFrameCueTypeAuthoring::BehaviorFreeSchemaVersion));
	TestTrue(TEXT("The current schema stamp is acceptable"),
		FPaper2DPlusFrameCueTypeAuthoring::IsAcceptedDurableSchemaVersion(
			FPaper2DPlusFrameCueTypeAuthoring::CurrentSchemaVersion));
	TestFalse(TEXT("An unstamped baseline is not acceptable"),
		FPaper2DPlusFrameCueTypeAuthoring::IsAcceptedDurableSchemaVersion(0));
	TestFalse(TEXT("A future schema stamp is not acceptable"),
		FPaper2DPlusFrameCueTypeAuthoring::IsAcceptedDurableSchemaVersion(
			FPaper2DPlusFrameCueTypeAuthoring::CurrentSchemaVersion + 1));

	FCueBehavior_ScopedPackage Fixture(*this, TEXT("CueBehaviorLegacy"));
	if (!TestNotNull(TEXT("Legacy-compat fixture package"), Fixture.Package))
	{
		return false;
	}

	UPaper2DPlusFrameCueBlueprint* Blueprint = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(),
		TEXT("BP_P2DP_BehaviorLegacyShape"),
		Fixture.Package);
	if (!TestNotNull(TEXT("Legacy-shape Cue Type fixture"), Blueprint))
	{
		return false;
	}
	if (!TestTrue(TEXT("Fixture is reduced to the pre-behavior asset shape"),
		CueBehavior_StripToLegacyShape(*Blueprint)))
	{
		return false;
	}

	FPaper2DPlusFrameCueSchema BehaviorFreeSchema;
	FText SchemaError;
	if (TestTrue(TEXT("A behavior-free Cue Type still describes a compiled schema"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeSchema(
			*Blueprint,
			EPaper2DPlusFrameCueSchemaSource::Compiled,
			BehaviorFreeSchema,
			&SchemaError)))
	{
		TestEqual(TEXT("A behavior-free Cue Type keeps the pre-behavior schema version"),
			BehaviorFreeSchema.Version,
			FPaper2DPlusFrameCueTypeAuthoring::BehaviorFreeSchemaVersion);
		TestEqual(TEXT("A behavior-free Cue Type names no behavior events"),
			BehaviorFreeSchema.BehaviorEvents.Num(), 0);
		TestFalse(TEXT("A behavior-free canonical text carries no behavior digest"),
			BehaviorFreeSchema.CanonicalText.Contains(TEXT("behavior;")));
	}

	const FPaper2DPlusFrameCueDurableSchemaCandidate Candidate =
		FPaper2DPlusFrameCueTypeAuthoring::PrepareDurableSchemaSaveCandidate(*Blueprint);
	if (!TestTrue(TEXT("Behavior-free Cue Type stages a durable candidate"), Candidate.bSuccess))
	{
		return false;
	}
	TestEqual(TEXT("The staged fingerprint matches the behavior-free schema"),
		Candidate.Fingerprint,
		BehaviorFreeSchema.Fingerprint);

	// Model an asset a version 1 build saved: the same payload snapshot and fingerprint, stamped 1.
	Blueprint->DurableSchemaVersion =
		FPaper2DPlusFrameCueTypeAuthoring::BehaviorFreeSchemaVersion;
	Fixture.Package->SetDirtyFlag(false);

	FText PersistenceError;
	TestTrue(TEXT("A version 1 stamp re-saves cleanly under the version 2 schema service"),
		FPaper2DPlusFrameCueTypeAuthoring::ValidateDurableSaveMetadataForPersistence(
			*Blueprint, &PersistenceError));
	if (!PersistenceError.IsEmpty())
	{
		AddError(FString::Printf(
			TEXT("Version 1 re-save was refused: %s"), *PersistenceError.ToString()));
	}

	const FPaper2DPlusFrameCueTypeDescriptor Descriptor =
		FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(Blueprint->GeneratedClass);
	TestTrue(TEXT("A version 1 stamped Cue Type classifies Ready without restaging"),
		Descriptor.Availability == EPaper2DPlusFrameCueTypeAvailability::Ready);
	if (Descriptor.Availability != EPaper2DPlusFrameCueTypeAvailability::Ready
		&& Descriptor.Diagnostics.Num() > 0)
	{
		AddError(FString::Printf(
			TEXT("Version 1 Cue Type was not Ready: %s"),
			*Descriptor.Diagnostics[0].Message.ToString()));
	}

	// Implementing behavior on that same asset is an ordinary, non-destructive schema change.
	TestTrue(TEXT("A pre-behavior Cue Type can gain its first behavior event"),
		FPaper2DPlusFrameCueTypeAuthoring::EnsureBehaviorEventNode(*Blueprint, OnCueTriggeredName));
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	const FPaper2DPlusFrameCueSchemaPreflight Preflight =
		FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(*Blueprint);
	TestTrue(TEXT("Adding behavior to a version 1 baseline is not a destructive schema change"),
		!Preflight.bRequiresDestructiveConfirmation);
	TestTrue(TEXT("Adding behavior to a version 1 baseline can still compile and persist"),
		Preflight.bCanCompile && Preflight.bCanPersist);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorCookValidatorTest,
	"Paper2DPlus.FrameCues.CueType.Behavior.CookValidatorAcceptsBehaviorRejectsInvalid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorCookValidatorTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorEnvelopeTest;

	// Same engine seam as the envelope-reject test above: only UE 5.6+ compiles route the refusal
	// into a loggable message, so the expectation exists only where the message can.
	if (const IConsoleVariable* FullBlueprintValidation =
			IConsoleManager::Get().FindConsoleVariable(
				TEXT("BP.bDoFullDataValidationDuringCompilation"));
		FullBlueprintValidation && FullBlueprintValidation->GetBool())
	{
		AddExpectedError(
			TEXT("is not one of the declared Cue behavior events"),
			EAutomationExpectedErrorFlags::Contains,
			0);
	}

	// The cooked-export boundary calls ValidateCompiledDataOnlyContract with the exact class being
	// serialized, so proving that call shape proves the cook verdict without running a real cook.
	UPaper2DPlusFrameCueBlueprint* ValidBehavior = CueBehavior_MakeCueType(
		UPaper2DPlusCueState::StaticClass(), TEXT("BP_P2DP_BehaviorCookValid"));
	if (!TestNotNull(TEXT("Cook-valid behavior fixture"), ValidBehavior))
	{
		return false;
	}
	TestNotNull(TEXT("Cue State Type seeds its Begin behavior event"),
		FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventNode(*ValidBehavior, OnCueBeginName));

	FText CookError;
	TestTrue(TEXT("The cook validator accepts a valid behavior-carrying generated class"),
		UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
			*ValidBehavior, &CookError, ValidBehavior->GeneratedClass));

	UPaper2DPlusFrameCueBlueprint* InvalidBehavior = CueBehavior_MakeCueType(
		UPaper2DPlusCueState::StaticClass(), TEXT("BP_P2DP_BehaviorCookInvalid"));
	if (!TestNotNull(TEXT("Cook-invalid behavior fixture"), InvalidBehavior))
	{
		return false;
	}
	TestTrue(TEXT("Invalid fixture smuggles a non-declared generated function"),
		CueBehavior_AddCustomEvent(*InvalidBehavior, TEXT("CookSmuggledBehavior")));
	TestFalse(TEXT("The cook validator still fails the deliberately invalid fixture"),
		UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
			*InvalidBehavior, &CookError, InvalidBehavior->GeneratedClass));
	TestFalse(TEXT("The rejected cook fixture reports an actionable error"), CookError.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorCollapsedGraphTest,
	"Paper2DPlus.FrameCues.CueType.Behavior.CollapsedEventStaysDiscoverable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorCollapsedGraphTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorEnvelopeTest;

	UPaper2DPlusFrameCueBlueprint* Blueprint = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_BehaviorCollapsed"));
	if (!TestNotNull(TEXT("Collapsed-graph behavior fixture"), Blueprint))
	{
		return false;
	}
	TestTrue(TEXT("The seeded event is implemented before it is tidied away"),
		CueBehavior_ImplementSeededEvent(*Blueprint, OnCueTriggeredName));

	UK2Node_Event* EventNode =
		FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventNode(*Blueprint, OnCueTriggeredName);
	UEdGraph* EventGraph = FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(*Blueprint);
	if (!TestNotNull(TEXT("Implemented behavior event node"), EventNode)
		|| !TestNotNull(TEXT("Permitted behavior event graph"), EventGraph))
	{
		return false;
	}
	TestTrue(TEXT("An implemented event is authored"),
		FPaper2DPlusFrameCueTypeAuthoring::GetAuthoredBehaviorEvents(*Blueprint)
			.Contains(OnCueTriggeredName));

	// Tidying a graph into a collapsed composite moves nodes onto a child graph. They still belong to
	// the same event page and still compile into the same ubergraph, so a scan that stopped at the
	// page itself would report an implemented event as missing the moment the designer tidied up.
	UEdGraph* CollapsedGraph = NewObject<UEdGraph>(EventGraph, NAME_None, RF_Transactional);
	CollapsedGraph->Schema = UEdGraphSchema_K2::StaticClass();
	EventGraph->SubGraphs.Add(CollapsedGraph);
	EventGraph->Nodes.Remove(EventNode);
	CollapsedGraph->Nodes.Add(EventNode);
	EventNode->Rename(nullptr, CollapsedGraph, REN_DontCreateRedirectors | REN_NonTransactional);

	TestNotNull(TEXT("A collapsed behavior event is still found by name"),
		FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventNode(*Blueprint, OnCueTriggeredName));
	TestTrue(TEXT("A collapsed behavior event is still reported as authored"),
		FPaper2DPlusFrameCueTypeAuthoring::GetAuthoredBehaviorEvents(*Blueprint)
			.Contains(OnCueTriggeredName));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorSynchronousPolicyTest,
	"Paper2DPlus.FrameCues.CueType.Behavior.SynchronousPolicyTraversesDeferredGraphs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorSynchronousPolicyTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorEnvelopeTest;

	auto ExpectRejected = [this](
		UPaper2DPlusFrameCueBlueprint* Blueprint,
		const TCHAR* Label,
		const TArray<FString>& RequiredDiagnosticText)
	{
		if (!TestNotNull(Label, Blueprint))
		{
			return false;
		}
		FText Error;
		const bool bRejected =
			!FPaper2DPlusFrameCueTypeAuthoring::ValidateSynchronousBehavior(
				*Blueprint,
				&Error);
		TestTrue(*FString::Printf(TEXT("%s is rejected"), Label), bRejected);
		for (const FString& Required : RequiredDiagnosticText)
		{
			TestTrue(
				*FString::Printf(
					TEXT("%s diagnostic names '%s'"),
					Label,
					*Required),
				Error.ToString().Contains(Required));
		}
		return bRejected;
	};

	UPaper2DPlusFrameCueBlueprint* SynchronousFixture = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_SynchronousCall"));
	UEdGraph* SynchronousGraph = SynchronousFixture
		? FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(*SynchronousFixture)
		: nullptr;
	if (TestNotNull(TEXT("Synchronous call fixture"), SynchronousFixture)
		&& TestNotNull(TEXT("Synchronous call graph"), SynchronousGraph))
	{
		CueBehavior_AddCall(
			*SynchronousGraph,
			*UKismetSystemLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
		FText Error;
		TestTrue(TEXT("An ordinary synchronous function call remains valid"),
			FPaper2DPlusFrameCueTypeAuthoring::ValidateSynchronousBehavior(
				*SynchronousFixture,
				&Error));
		TestTrue(TEXT("A valid synchronous scan has no diagnostic"), Error.IsEmpty());
	}

	UPaper2DPlusFrameCueBlueprint* DelayFixture = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_DirectDelay"));
	UEdGraph* DelayGraph = DelayFixture
		? FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(*DelayFixture)
		: nullptr;
	if (TestNotNull(TEXT("Direct Delay graph"), DelayGraph))
	{
		CueBehavior_AddCall(
			*DelayGraph,
			*UKismetSystemLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Delay));
		ExpectRejected(
			DelayFixture,
			TEXT("Direct latent call"),
			{ DelayFixture->GetPathName(), TEXT("OnCueTriggered"), TEXT("Delay"), TEXT("latent") });
	}

	UPaper2DPlusFrameCueBlueprint* TimerFixture = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_DeferredTimer"));
	UEdGraph* TimerGraph = TimerFixture
		? FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(*TimerFixture)
		: nullptr;
	if (TestNotNull(TEXT("Deferred timer graph"), TimerGraph))
	{
		CueBehavior_AddCall(
			*TimerGraph,
			*UKismetSystemLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_SetTimerForNextTick));
		ExpectRejected(
			TimerFixture,
			TEXT("Deferred timer/self-scheduling call"),
			{ TEXT("OnCueTriggered"), TEXT("K2_SetTimerForNextTick"), TEXT("schedules work") });
	}

	const FName WrapperFunctionName(TEXT("ScheduleDeferredCueWork"));
	UBlueprint* WrapperLibrary = FKismetEditorUtilities::CreateBlueprint(
		UBlueprintFunctionLibrary::StaticClass(),
		GetTransientPackage(),
		MakeUniqueObjectName(
			GetTransientPackage(),
			UBlueprint::StaticClass(),
			TEXT("BP_P2DP_DeferredWrapperLibrary")),
		BPTYPE_FunctionLibrary,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		NAME_None);
	UEdGraph* WrapperGraph = WrapperLibrary
		? FBlueprintEditorUtils::CreateNewGraph(
			WrapperLibrary,
			WrapperFunctionName,
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass())
		: nullptr;
	if (TestNotNull(TEXT("Blueprint helper library fixture"), WrapperLibrary)
		&& TestNotNull(TEXT("Blueprint helper function graph"), WrapperGraph))
	{
		FBlueprintEditorUtils::AddFunctionGraph(
			WrapperLibrary,
			WrapperGraph,
			true,
			static_cast<UFunction*>(nullptr));
		CueBehavior_AddCall(
			*WrapperGraph,
			*UKismetSystemLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_SetTimerForNextTick));
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WrapperLibrary);
		FKismetEditorUtilities::CompileBlueprint(WrapperLibrary);

		UPaper2DPlusFrameCueBlueprint* WrappedTimerFixture = CueBehavior_MakeCueType(
			UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_WrappedDeferredTimer"));
		UEdGraph* WrappedTimerCaller = WrappedTimerFixture
			? FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(
				*WrappedTimerFixture)
			: nullptr;
		if (TestNotNull(TEXT("Wrapped deferred timer Cue Type"), WrappedTimerFixture)
			&& TestNotNull(TEXT("Wrapped deferred timer caller graph"), WrappedTimerCaller)
			&& TestNotNull(
				TEXT("Blueprint helper generated class"),
				WrapperLibrary->GeneratedClass.Get()))
		{
			CueBehavior_AddCall(
				*WrappedTimerCaller,
				*WrapperLibrary->GeneratedClass.Get(),
				WrapperFunctionName);
			ExpectRejected(
				WrappedTimerFixture,
				TEXT("Deferred timer hidden behind a Blueprint helper"),
				{
					WrappedTimerFixture->GetPathName(),
					WrapperLibrary->GetPathName(),
					WrapperFunctionName.ToString(),
					TEXT("K2_SetTimerForNextTick"),
					TEXT("schedules work")
				});
		}
	}

	UPaper2DPlusFrameCueBlueprint* AsyncFixture = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_AsyncAction"));
	UEdGraph* AsyncGraph = AsyncFixture
		? FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(*AsyncFixture)
		: nullptr;
	if (TestNotNull(TEXT("Async action graph"), AsyncGraph))
	{
		CueBehavior_AddAsyncNode(*AsyncGraph);
		ExpectRejected(
			AsyncFixture,
			TEXT("Async task/action"),
			{ TEXT("OnCueTriggered"), TEXT("K2Node_ListenForFrameCue"), TEXT("async") });
		FText DirectPolicyError;
		FText CookPolicyError;
		FPaper2DPlusFrameCueTypeAuthoring::ValidateSynchronousBehavior(
			*AsyncFixture,
			&DirectPolicyError);
		// Model an asset whose last generated class is still marked current after source injection.
		// The cook boundary must inspect authored source before accepting that stale valid class.
		AsyncFixture->Status = BS_UpToDate;
		TestFalse(TEXT("Stale compiled state cannot hide newly authored async source at cook"),
			UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
				*AsyncFixture,
				&CookPolicyError,
				AsyncFixture->GeneratedClass));
		TestEqual(TEXT("Cook and direct async validation use the same policy diagnosis"),
			CookPolicyError.ToString(),
			DirectPolicyError.ToString());
	}

	UPaper2DPlusFrameCueBlueprint* CollapsedFixture = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_CollapsedLatent"));
	UEdGraph* CollapsedRoot = CollapsedFixture
		? FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(*CollapsedFixture)
		: nullptr;
	if (TestNotNull(TEXT("Collapsed latent root graph"), CollapsedRoot))
	{
		UEdGraph* CollapsedGraph = NewObject<UEdGraph>(
			CollapsedRoot,
			TEXT("CollapsedDeferredBehavior"),
			RF_Transactional);
		CollapsedGraph->Schema = UEdGraphSchema_K2::StaticClass();
		CollapsedRoot->SubGraphs.Add(CollapsedGraph);
		CueBehavior_AddCall(
			*CollapsedGraph,
			*UKismetSystemLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, RetriggerableDelay));
		ExpectRejected(
			CollapsedFixture,
			TEXT("Collapsed latent content"),
			{ TEXT("OnCueTriggered"), TEXT("RetriggerableDelay"), TEXT("CollapsedDeferredBehavior") });
	}

	UPaper2DPlusFrameCueBlueprint* MacroFixture = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_ExternalLatentMacro"));
	UEdGraph* MacroCallerGraph = MacroFixture
		? FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(*MacroFixture)
		: nullptr;
	UBlueprint* ExternalMacroOwner = NewObject<UBlueprint>(
		GetTransientPackage(),
		MakeUniqueObjectName(
			GetTransientPackage(),
			UBlueprint::StaticClass(),
			TEXT("BP_P2DP_ExternalMacroOwner")));
	UEdGraph* ExternalMacroGraph = ExternalMacroOwner
		? NewObject<UEdGraph>(
			ExternalMacroOwner,
			TEXT("DeferredExternalMacro"),
			RF_Transactional)
		: nullptr;
	if (TestNotNull(TEXT("External latent macro caller graph"), MacroCallerGraph)
		&& TestNotNull(TEXT("External latent macro owner"), ExternalMacroOwner)
		&& TestNotNull(TEXT("External latent macro graph"), ExternalMacroGraph))
	{
		ExternalMacroGraph->Schema = UEdGraphSchema_K2::StaticClass();
		ExternalMacroOwner->MacroGraphs.Add(ExternalMacroGraph);
		CueBehavior_AddCall(
			*ExternalMacroGraph,
			*UKismetSystemLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Delay));
		CueBehavior_AddMacroInstance(*MacroCallerGraph, *ExternalMacroGraph);
		ExpectRejected(
			MacroFixture,
			TEXT("Referenced external latent macro"),
			{ TEXT("OnCueTriggered"), TEXT("Delay"), TEXT("DeferredExternalMacro") });
	}

	UPaper2DPlusFrameCueBlueprint* CyclicMacroFixture = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_CyclicMacro"));
	UEdGraph* CyclicCallerGraph = CyclicMacroFixture
		? FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(*CyclicMacroFixture)
		: nullptr;
	UBlueprint* CyclicMacroOwner = NewObject<UBlueprint>(
		GetTransientPackage(),
		MakeUniqueObjectName(
			GetTransientPackage(),
			UBlueprint::StaticClass(),
			TEXT("BP_P2DP_CyclicMacroOwner")));
	UEdGraph* CyclicMacroGraph = CyclicMacroOwner
		? NewObject<UEdGraph>(
			CyclicMacroOwner,
			TEXT("CycleSafeMacro"),
			RF_Transactional)
		: nullptr;
	if (TestNotNull(TEXT("Cycle-safe macro caller graph"), CyclicCallerGraph)
		&& TestNotNull(TEXT("Cycle-safe macro owner"), CyclicMacroOwner)
		&& TestNotNull(TEXT("Cycle-safe macro graph"), CyclicMacroGraph))
	{
		CyclicMacroGraph->Schema = UEdGraphSchema_K2::StaticClass();
		CyclicMacroOwner->MacroGraphs.Add(CyclicMacroGraph);
		CueBehavior_AddMacroInstance(*CyclicMacroGraph, *CyclicMacroGraph);
		CueBehavior_AddMacroInstance(*CyclicCallerGraph, *CyclicMacroGraph);
		FText Error;
		TestTrue(TEXT("A recursive macro without deferred content terminates and remains valid"),
			FPaper2DPlusFrameCueTypeAuthoring::ValidateSynchronousBehavior(
				*CyclicMacroFixture,
				&Error));
	}

	UPaper2DPlusFrameCueBlueprint* ParentFixture = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_SynchronousParent"));
	UPaper2DPlusFrameCueBlueprint* ChildFixture = ParentFixture
		? CueBehavior_MakeCueType(
			ParentFixture->GeneratedClass,
			TEXT("BP_P2DP_InheritedDeferredChild"))
		: nullptr;
	FText InheritanceError;
	if (TestNotNull(TEXT("Specialized parent Cue Type"), ParentFixture)
		&& TestNotNull(TEXT("Specialized child Cue Type"), ChildFixture))
	{
		TestTrue(TEXT("A valid synchronous specialized parent is accepted"),
			FPaper2DPlusFrameCueTypeAuthoring::ValidateSynchronousBehavior(
				*ChildFixture,
				&InheritanceError));
		UEdGraph* ParentGraph =
			FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(*ParentFixture);
		if (TestNotNull(TEXT("Specialized parent behavior graph"), ParentGraph))
		{
			CueBehavior_AddCall(
				*ParentGraph,
				*UKismetSystemLibrary::StaticClass(),
				GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Delay));
			ExpectRejected(
				ChildFixture,
				TEXT("Inherited deferred parent behavior"),
				{
					ChildFixture->GetPathName(),
					ParentFixture->GetPathName(),
					TEXT("OnCueTriggered"),
					TEXT("Delay")
				});
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusCueBehaviorRestoredDocumentGateTest,
	"Paper2DPlus.FrameCues.CueType.Behavior.RestoredDocumentsAreGatedToThePermittedGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusCueBehaviorRestoredDocumentGateTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueBehaviorEnvelopeTest;
	if (!TestNotNull(TEXT("GEditor hosts the restricted Cue Type editor"), GEditor))
	{
		return false;
	}

	UPaper2DPlusFrameCueBlueprint* Blueprint = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_BehaviorDocuments"));
	if (!TestNotNull(TEXT("Restored-document fixture"), Blueprint))
	{
		return false;
	}
	UEdGraph* BehaviorGraph = FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(*Blueprint);
	if (!TestNotNull(TEXT("Permitted behavior event graph"), BehaviorGraph))
	{
		return false;
	}

	// An asset last edited in a full Blueprint editor remembers whatever it had open, and reopening it
	// restores those documents from inside the Blueprint editor's own initialization. Reopening a Cue
	// Type must not bring back a surface the envelope forbids — and must not rewrite what the asset
	// remembers either: opening a restricted editor is an inspection, never an edit.
	UEdGraph* ForbiddenGraph = NewObject<UEdGraph>(Blueprint, NAME_None, RF_Transactional);
	ForbiddenGraph->Schema = UEdGraphSchema_K2::StaticClass();
	Blueprint->LastEditedDocuments.Reset();
	Blueprint->LastEditedDocuments.Add(FEditedDocumentInfo(ForbiddenGraph));
	Blueprint->LastEditedDocuments.Add(FEditedDocumentInfo(BehaviorGraph));
	Blueprint->LastEditedDocuments.Add(FEditedDocumentInfo(nullptr));
	const TArray<FEditedDocumentInfo> PersistedDocuments = Blueprint->LastEditedDocuments;

	FCueBehavior_ScopedRoot BlueprintRoot(Blueprint);
	const TSharedRef<FPaper2DPlusFrameCueTypeEditor> Editor =
		MakeShared<FPaper2DPlusFrameCueTypeEditor>();
	Editor->InitFrameCueTypeEditor(EToolkitMode::Standalone, nullptr, Blueprint);
	FCueBehavior_ScopedEditorClose EditorClose(Editor);

	TArray<TSharedPtr<SDockTab>> ForbiddenTabs;
	TestFalse(TEXT("Startup restores no tab for a forbidden remembered document"),
		Editor->FindOpenTabsContainingDocument(ForbiddenGraph, ForbiddenTabs));
	TestEqual(TEXT("No forbidden document tab is left open"), ForbiddenTabs.Num(), 0);
	TestFalse(TEXT("The forbidden document stays unopenable after startup"),
		Editor->OpenDocument(ForbiddenGraph, FDocumentTracker::OpenNewDocument).IsValid());
	TArray<TSharedPtr<SDockTab>> BehaviorTabs;
	TestTrue(TEXT("The permitted behavior graph is the one document that opens"),
		Editor->FindOpenTabsContainingDocument(BehaviorGraph, BehaviorTabs));

	TestTrue(TEXT("Opening the restricted editor preserves every remembered document entry"),
		CueBehavior_DocumentPathsMatch(Blueprint->LastEditedDocuments, PersistedDocuments));
	TestEqual(TEXT("The forbidden entry is still the asset's first remembered document"),
		Blueprint->LastEditedDocuments.Num() > 0
			? Blueprint->LastEditedDocuments[0].EditedObjectPath
			: FSoftObjectPath(),
		FSoftObjectPath(ForbiddenGraph));
	EditorClose.Close();

	// A Cue Type with no event graph at all has no permitted document to restore, so it opens with no
	// document tab at all — while still keeping what it remembers.
	UPaper2DPlusFrameCueBlueprint* LegacyBlueprint = CueBehavior_MakeCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_BehaviorFreeDocuments"));
	if (!TestNotNull(TEXT("Behavior-free restored-document fixture"), LegacyBlueprint))
	{
		return false;
	}
	if (!TestTrue(TEXT("Fixture reduces to the pre-behavior shape"),
		CueBehavior_StripToLegacyShape(*LegacyBlueprint)))
	{
		return false;
	}
	UEdGraph* LegacyForbiddenGraph = NewObject<UEdGraph>(LegacyBlueprint, NAME_None, RF_Transactional);
	LegacyForbiddenGraph->Schema = UEdGraphSchema_K2::StaticClass();
	LegacyBlueprint->LastEditedDocuments.Reset();
	LegacyBlueprint->LastEditedDocuments.Add(FEditedDocumentInfo(LegacyForbiddenGraph));
	const TArray<FEditedDocumentInfo> PersistedLegacyDocuments = LegacyBlueprint->LastEditedDocuments;

	FCueBehavior_ScopedRoot LegacyBlueprintRoot(LegacyBlueprint);
	const TSharedRef<FPaper2DPlusFrameCueTypeEditor> LegacyEditor =
		MakeShared<FPaper2DPlusFrameCueTypeEditor>();
	LegacyEditor->InitFrameCueTypeEditor(EToolkitMode::Standalone, nullptr, LegacyBlueprint);
	FCueBehavior_ScopedEditorClose LegacyEditorClose(LegacyEditor);

	TArray<TSharedPtr<SDockTab>> LegacyForbiddenTabs;
	TestFalse(TEXT("A behavior-free Cue Type restores no document tab at all"),
		LegacyEditor->FindOpenTabsContainingDocument(LegacyForbiddenGraph, LegacyForbiddenTabs));
	TestEqual(TEXT("No behavior-free document tab is left open"), LegacyForbiddenTabs.Num(), 0);
	TestTrue(TEXT("A behavior-free Cue Type keeps its remembered documents untouched"),
		CueBehavior_DocumentPathsMatch(
			LegacyBlueprint->LastEditedDocuments, PersistedLegacyDocuments));
	LegacyEditorClose.Close();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
