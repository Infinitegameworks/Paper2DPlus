// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/UserDefinedEnum.h"
#include "Paper2DPlusEditorTestFrameCueTypes.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "FrameCues/Paper2DPlusFrameCueTypeEditor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/EngineVersionComparison.h"
#include "PackageTools.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Engine/UserDefinedStruct.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif

namespace Paper2DPlusFrameCueTypeAuthoringTest
{
	bool ChangeFixtureVariableType(
		UUserDefinedStruct* Struct,
		const FGuid VariableGuid,
		const FEdGraphPinType& NewType)
	{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		FString ErrorMessage;
		FStructVariableDescription* Variable =
			FStructureEditorUtils::GetVarDescByGuid(Struct, VariableGuid);
		if (!Variable
			|| !FStructureEditorUtils::CanHaveAMemberVariableOfType(Struct, NewType, &ErrorMessage)
			|| Variable->ToPinType() == NewType)
		{
			return false;
		}

		// UE 5.0's ChangeVariableType carries the seed bool's CurrentDefaultValue into
		// the new property. Mirror its structural edit while clearing both serialized
		// default channels before compilation, so dependent UDS recompilation is valid.
		FStructureEditorUtils::ModifyStructData(Struct);
		Variable = FStructureEditorUtils::GetVarDescByGuid(Struct, VariableGuid);
		UUserDefinedStructEditorData* EditorData =
			CastChecked<UUserDefinedStructEditorData>(Struct->EditorData);
		Variable->VarName = FName(*FString::Printf(
			TEXT("%s_%u_%s"),
			*Variable->FriendlyName,
			EditorData->GenerateUniqueNameIdForMemberVariable(),
			*Variable->VarGuid.ToString(EGuidFormats::Digits)));
		Variable->DefaultValue.Reset();
		// The 5.0 compiler refreshes CurrentDefaultValue from the old bool instance before
		// sanitizing properties. Keep the channels deliberately unequal so that refresh
		// cannot copy "False" back into the authored DefaultValue; CreateVariables then
		// normalizes CurrentDefaultValue from the empty new-type default.
		Variable->CurrentDefaultValue = TEXT("__P2DP_TypeChangePending__");
		if (!Variable->SetPinType(NewType))
		{
			return false;
		}
		FStructureEditorUtils::OnStructureChanged(
			Struct,
			FStructureEditorUtils::EStructureEditorChangeInfo::VariableTypeChanged);
		return Struct->Status == EUserDefinedStructureStatus::UDSS_UpToDate;
#else
		return FStructureEditorUtils::ChangeVariableType(Struct, VariableGuid, NewType);
#endif
	}

	struct FScopedAuthoringPackage
	{
		explicit FScopedAuthoringPackage(
			FAutomationTestBase& InTest,
			const TCHAR* Stem)
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

		~FScopedAuthoringPackage()
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
					TEXT("Cue Type authoring fixture cleanup failed for '%s': %s"),
					*PackageName,
					*Error.ToString()));
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}

		FAutomationTestBase& Test;
		FString PackageName;
		UPackage* Package = nullptr;
	};

	UPaper2DPlusFrameCueBlueprint* MakeCompiledCueType(
		UClass* ParentClass,
		const TCHAR* Name,
		const bool bAddPayload = true,
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
					FName(Name)),
				BPTYPE_Normal,
				UPaper2DPlusFrameCueBlueprint::StaticClass(),
				UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass(),
				NAME_None));
		if (!Blueprint
			|| !FPaper2DPlusFrameCueTypeEditor::PrepareNewlyCreatedBlueprint(*Blueprint))
		{
			return nullptr;
		}

		if (bAddPayload)
		{
			FEdGraphPinType IntegerType;
			IntegerType.PinCategory = UEdGraphSchema_K2::PC_Int;
			if (!FBlueprintEditorUtils::AddMemberVariable(
				Blueprint, TEXT("Power"), IntegerType, TEXT("12")))
			{
				return nullptr;
			}
			FBPVariableDescription* Power = Blueprint->NewVariables.FindByPredicate(
				[](const FBPVariableDescription& Variable)
				{
					return Variable.VarName == TEXT("Power");
				});
			if (!Power)
			{
				return nullptr;
			}
			Power->PropertyFlags &= ~CPF_DisableEditOnInstance;
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		}

		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return Blueprint->Status == BS_Error ? nullptr : Blueprint;
	}

	bool AddCuePayloadVariable(
		UPaper2DPlusFrameCueBlueprint& Blueprint,
		const FName Name,
		const FEdGraphPinType& Type,
		const FString& DefaultValue = FString())
	{
		if (!FBlueprintEditorUtils::AddMemberVariable(&Blueprint, Name, Type, DefaultValue))
		{
			return false;
		}
		FBPVariableDescription* Variable = Blueprint.NewVariables.FindByPredicate(
			[Name](const FBPVariableDescription& Candidate)
			{
				return Candidate.VarName == Name;
			});
		if (!Variable)
		{
			return false;
		}
		Variable->PropertyFlags &= ~CPF_DisableEditOnInstance;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		return true;
	}

	FString ExportDefaultForCopy(const FProperty& Property, UObject& Defaults)
	{
		FString Value;
#if UE_VERSION_OLDER_THAN(5, 5, 0)
		Property.ExportTextItem(
			Value,
			Property.ContainerPtrToValuePtr<void>(&Defaults),
			nullptr,
			&Defaults,
			PPF_Copy);
#else
		Property.ExportTextItem_Direct(
			Value,
			Property.ContainerPtrToValuePtr<void>(&Defaults),
			nullptr,
			&Defaults,
			PPF_Copy);
#endif
		return Value;
	}

	bool SeedDurableBaseline(UPaper2DPlusFrameCueBlueprint& Blueprint, FText* OutError = nullptr)
	{
		FPaper2DPlusFrameCueSchema Schema;
		FString Snapshot;
		TMap<FName, FString> Defaults;
		FText Error;
		if (!FPaper2DPlusFrameCueTypeAuthoring::DescribeSchema(
				Blueprint,
				EPaper2DPlusFrameCueSchemaSource::Compiled,
				Schema,
				&Error)
			|| !FPaper2DPlusFrameCueTypeAuthoring::SerializeSchemaSnapshot(
				Schema,
				Snapshot,
				&Error)
			|| !FPaper2DPlusFrameCueTypeAuthoring::CaptureDefaultValues(
				Blueprint,
				Defaults,
				&Error))
		{
			if (OutError)
			{
				*OutError = Error;
			}
			return false;
		}
		Blueprint.DurableSchemaVersion = Schema.Version;
		Blueprint.DurableSchemaFingerprint = Schema.Fingerprint;
		Blueprint.DurableSchemaSnapshot = MoveTemp(Snapshot);
		Blueprint.DurableVariables = Blueprint.NewVariables;
		Blueprint.DurableDefaultValues = MoveTemp(Defaults);
		if (UPackage* Package = Blueprint.GetOutermost())
		{
			Package->SetDirtyFlag(false);
		}
		if (OutError)
		{
			*OutError = FText::GetEmpty();
		}
		return true;
	}

	const FPaper2DPlusFrameCueSchemaDependency* FindDependency(
		const FPaper2DPlusFrameCueSchema& Schema,
		const EPaper2DPlusFrameCueSchemaDependencyKind Kind,
		const UObject& Object)
	{
		const FSoftObjectPath Path(&Object);
		return Schema.Dependencies.FindByPredicate(
			[Kind, &Path](const FPaper2DPlusFrameCueSchemaDependency& Dependency)
			{
				return Dependency.Kind == Kind && Dependency.ObjectPath == Path;
			});
	}

	bool HasDiagnostic(
		const FPaper2DPlusFrameCueTypeDescriptor& Descriptor,
		const EPaper2DPlusFrameCueDiagnosticCode Code)
	{
		return Descriptor.Diagnostics.ContainsByPredicate(
			[Code](const FPaper2DPlusFrameCueAuthoringDiagnostic& Diagnostic)
			{
				return Diagnostic.Code == Code;
			});
	}

	FPaper2DPlusFrameCueSchema MakeSimpleSchema()
	{
		FPaper2DPlusFrameCueSchema Schema;
		Schema.Kind = EPaper2DPlusFrameCueTypeKind::Moment;
		Schema.ParentClassPath = FSoftObjectPath(UPaper2DPlusCue::StaticClass());

		FPaper2DPlusFrameCueSchemaField Field;
		Field.FieldId = FGuid::NewGuid();
		Field.Name = TEXT("Power");
		Field.TypeKey = TEXT("int");
		Field.DefaultValue = TEXT("12");
		Schema.Fields.Add(MoveTemp(Field));
		return Schema;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeClassificationAndTiming,
	"Paper2DPlus.FrameCues.CueType.Authoring.ClassificationAndTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeClassificationAndTiming::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringTest;

	TestTrue(TEXT("Native Cue is classified as Moment"),
		FPaper2DPlusFrameCueTypeAuthoring::ClassifyKind(
			UPaper2DPlusEditorTestMomentCue::StaticClass())
			== EPaper2DPlusFrameCueTypeKind::Moment);
	TestTrue(TEXT("Native Cue State is classified as Range"),
		FPaper2DPlusFrameCueTypeAuthoring::ClassifyKind(
			UPaper2DPlusEditorTestRangeCue::StaticClass())
			== EPaper2DPlusFrameCueTypeKind::Range);
	TestEqual(TEXT("Instant timing base presents the designer name Cue"),
		UPaper2DPlusCue::StaticClass()->GetDisplayNameText().ToString(),
		FString(TEXT("Cue")));
	TestEqual(TEXT("State timing base presents the designer name Cue State"),
		UPaper2DPlusCueState::StaticClass()->GetDisplayNameText().ToString(),
		FString(TEXT("Cue State")));
	TestTrue(TEXT("Unrelated class is rejected"),
		FPaper2DPlusFrameCueTypeAuthoring::ClassifyKind(UObject::StaticClass())
			== EPaper2DPlusFrameCueTypeKind::Invalid);

	const FProperty* TriggerFrame = FindFProperty<FProperty>(
		UPaper2DPlusCue::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusCue, TriggerFrame));
	const FProperty* StartFrame = FindFProperty<FProperty>(
		UPaper2DPlusCueState::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusCueState, StartFrame));
	const FProperty* FrameCount = FindFProperty<FProperty>(
		UPaper2DPlusCueState::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusCueState, FrameCount));
	const FProperty* EmitUpdates = FindFProperty<FProperty>(
		UPaper2DPlusCueState::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusCueState, bEmitUpdates));
	const FProperty* Color = FindFProperty<FProperty>(
		UPaper2DPlusCueBase::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusCueBase, Color));

	TestTrue(TEXT("Moment anchor is placement timing"),
		FPaper2DPlusFrameCueTypeAuthoring::IsPlacementTimingProperty(TriggerFrame));
	const FProperty* TriggerEdge = FindFProperty<FProperty>(
		UPaper2DPlusCue::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusCue, TriggerEdge));
	TestTrue(TEXT("TriggerEdge is placement timing — excluded from the durable schema, so adding "
		"it forced no Cue Type restage"),
		FPaper2DPlusFrameCueTypeAuthoring::IsPlacementTimingProperty(TriggerEdge));

	// TASK-173: automation fixtures keep the production Ready gate (tests place through it) while
	// every designer-facing picker filters them out; the shipped native cue must never match.
	{
		const UClass* FixtureClass = FindObject<UClass>(
			nullptr, TEXT("/Script/Paper2DPlus.Paper2DPlusTestMomentCue"));
		if (TestNotNull(TEXT("runtime automation fixture class resolves"), FixtureClass))
		{
			TestTrue(TEXT("the fixture carries the automation-fixture marker"),
				FPaper2DPlusFrameCueTypeAuthoring::IsAutomationFixtureCueClass(FixtureClass));
			TestTrue(TEXT("the fixture STAYS Ready for the placement-authoring gate"),
				FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(
					const_cast<UClass*>(FixtureClass)).Availability
					== EPaper2DPlusFrameCueTypeAvailability::Ready);
		}
		const UClass* ShippedNative = FindObject<UClass>(
			nullptr, TEXT("/Script/Paper2DPlus.Paper2DPlusSpawnFlipbookCue"));
		if (TestNotNull(TEXT("shipped native cue class resolves"), ShippedNative))
		{
			TestFalse(TEXT("the shipped native cue is not an automation fixture"),
				FPaper2DPlusFrameCueTypeAuthoring::IsAutomationFixtureCueClass(ShippedNative));
		}
	}
	TestTrue(TEXT("Range start is placement timing"),
		FPaper2DPlusFrameCueTypeAuthoring::IsPlacementTimingProperty(StartFrame));
	TestTrue(TEXT("Range count is placement timing"),
		FPaper2DPlusFrameCueTypeAuthoring::IsPlacementTimingProperty(FrameCount));
	TestFalse(TEXT("Range update policy remains a class default"),
		FPaper2DPlusFrameCueTypeAuthoring::IsPlacementTimingProperty(EmitUpdates));
	TestFalse(TEXT("Cue payload is not placement timing"),
		FPaper2DPlusFrameCueTypeAuthoring::IsPlacementTimingProperty(Color));
	TestTrue(TEXT("Null detail rows remain visible"),
		FPaper2DPlusFrameCueTypeAuthoring::IsCueAuthoringPropertyVisible(nullptr));
	TestTrue(TEXT("Range update policy remains visible"),
		FPaper2DPlusFrameCueTypeAuthoring::IsCueAuthoringPropertyVisible(EmitUpdates));
	TestFalse(TEXT("Placement timing is hidden from class-default authoring"),
		FPaper2DPlusFrameCueTypeAuthoring::IsCueAuthoringPropertyVisible(TriggerFrame));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeSchemaAndReadiness,
	"Paper2DPlus.FrameCues.CueType.Authoring.SchemaAndReadiness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeSchemaAndReadiness::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringTest;

	UPaper2DPlusFrameCueBlueprint* Blueprint = MakeCompiledCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_AuthoringSchema"));
	if (!TestNotNull(TEXT("Specialized Cue Type compiles"), Blueprint))
	{
		return false;
	}

	FPaper2DPlusFrameCueSchema FirstSchema;
	FPaper2DPlusFrameCueSchema SecondSchema;
	FText Error;
	TestTrue(TEXT("Compiled schema can be described"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeSchema(
			*Blueprint,
			EPaper2DPlusFrameCueSchemaSource::Compiled,
			FirstSchema,
			&Error));
	TestTrue(TEXT("Repeated schema description succeeds"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeSchema(
			*Blueprint,
			EPaper2DPlusFrameCueSchemaSource::Compiled,
			SecondSchema,
			&Error));
	TestFalse(TEXT("Compiled schema fingerprint is non-empty"),
		FirstSchema.Fingerprint.IsEmpty());
	TestEqual(TEXT("Schema fingerprint is deterministic"),
		FirstSchema.Fingerprint, SecondSchema.Fingerprint);
	TestEqual(TEXT("Canonical schema text is deterministic"),
		FirstSchema.CanonicalText, SecondSchema.CanonicalText);
	TestEqual(TEXT("One direct payload field is described"),
		FirstSchema.Fields.Num(), 1);
	if (FirstSchema.Fields.Num() == 1)
	{
		TestEqual(TEXT("Payload field identity is retained"),
			FirstSchema.Fields[0].Name, FName(TEXT("Power")));
		TestEqual(TEXT("Payload default is canonicalized"),
			FirstSchema.Fields[0].DefaultValue, FString(TEXT("12")));
	}

	FPaper2DPlusFrameCueTypeDescriptor Descriptor =
		FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(Blueprint->GeneratedClass);
	TestTrue(TEXT("Unsaved compiled schema is not placement-ready"),
		Descriptor.Availability
			== EPaper2DPlusFrameCueTypeAvailability::NeedsDurableSave);

	TestTrue(TEXT("Full durable recovery baseline is established"),
		SeedDurableBaseline(*Blueprint, &Error));
	Descriptor = FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(Blueprint->GeneratedClass);
	TestTrue(TEXT("Matching durable schema is placement-ready"),
		Descriptor.Availability == EPaper2DPlusFrameCueTypeAvailability::Ready);

	const FPaper2DPlusFrameCueSchemaPreflight Preflight =
		FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(*Blueprint);
	TestTrue(TEXT("Matching durable schema passes non-mutating preflight"),
		Preflight.bCanPlace && Preflight.bCanPersist);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeUserDefinedDependencies,
	"Paper2DPlus.FrameCues.CueType.Authoring.UserDefinedDependencies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeUserDefinedDependencies::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringTest;
	FScopedAuthoringPackage DependencyFixture(*this, TEXT("P2DPCueTypeDependencies"));
	FScopedAuthoringPackage CueFixture(*this, TEXT("P2DPCueTypeDependencyCue"));
	if (!TestNotNull(TEXT("Dependency fixture package exists"), DependencyFixture.Package)
		|| !TestNotNull(TEXT("Dependency Cue package exists"), CueFixture.Package))
	{
		return false;
	}

	UUserDefinedEnum* RecursiveEnum = Cast<UUserDefinedEnum>(
		FEnumEditorUtils::CreateUserDefinedEnum(
			DependencyFixture.Package,
			TEXT("E_RecursiveCuePayload"),
			RF_Public | RF_Standalone | RF_Transactional));
	UUserDefinedEnum* DirectEnum = Cast<UUserDefinedEnum>(
		FEnumEditorUtils::CreateUserDefinedEnum(
			DependencyFixture.Package,
			TEXT("E_DirectCuePayload"),
			RF_Public | RF_Standalone | RF_Transactional));
	UUserDefinedStruct* InnerStruct = FStructureEditorUtils::CreateUserDefinedStruct(
		DependencyFixture.Package,
		TEXT("S_InnerCuePayload"),
		RF_Public | RF_Standalone | RF_Transactional);
	if (!TestNotNull(TEXT("Recursive enum fixture exists"), RecursiveEnum)
		|| !TestNotNull(TEXT("Direct enum fixture exists"), DirectEnum)
		|| !TestNotNull(TEXT("Inner struct fixture exists"), InnerStruct))
	{
		return false;
	}

	FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(RecursiveEnum);
	FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(DirectEnum);
	TArray<FStructVariableDescription>& InnerVariables =
		FStructureEditorUtils::GetVarDesc(InnerStruct);
	if (!TestTrue(TEXT("New user-defined struct has a seed field"), InnerVariables.Num() > 0))
	{
		return false;
	}
	FEdGraphPinType RecursiveEnumType;
	RecursiveEnumType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	RecursiveEnumType.PinSubCategoryObject = RecursiveEnum;
	if (!TestTrue(TEXT("Inner struct accepts a user-defined enum field"),
		ChangeFixtureVariableType(
			InnerStruct,
			InnerVariables[0].VarGuid,
			RecursiveEnumType)))
	{
		return false;
	}

	UUserDefinedStruct* OuterStruct = FStructureEditorUtils::CreateUserDefinedStruct(
		DependencyFixture.Package,
		TEXT("S_OuterCuePayload"),
		RF_Public | RF_Standalone | RF_Transactional);
	if (!TestNotNull(TEXT("Outer struct fixture exists"), OuterStruct))
	{
		return false;
	}
	TArray<FStructVariableDescription>& OuterVariables =
		FStructureEditorUtils::GetVarDesc(OuterStruct);
	if (!TestTrue(TEXT("Outer user-defined struct has a seed field"), OuterVariables.Num() > 0))
	{
		return false;
	}
	FEdGraphPinType InnerArrayType;
	InnerArrayType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	InnerArrayType.PinSubCategoryObject = InnerStruct;
	InnerArrayType.ContainerType = EPinContainerType::Array;
	if (!TestTrue(TEXT("Outer struct accepts an array of the inner user-defined struct"),
		ChangeFixtureVariableType(
			OuterStruct,
			OuterVariables[0].VarGuid,
			InnerArrayType)))
	{
		return false;
	}

	UPaper2DPlusFrameCueBlueprint* Cue = MakeCompiledCueType(
		UPaper2DPlusCue::StaticClass(),
		TEXT("BP_UserDefinedDependencies"),
		false,
		CueFixture.Package);
	if (!TestNotNull(TEXT("Dependency Cue fixture is created"), Cue))
	{
		return false;
	}
	FEdGraphPinType OuterArrayType;
	OuterArrayType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	OuterArrayType.PinSubCategoryObject = OuterStruct;
	OuterArrayType.ContainerType = EPinContainerType::Array;
	FEdGraphPinType DirectEnumMapType;
	DirectEnumMapType.PinCategory = UEdGraphSchema_K2::PC_Name;
	DirectEnumMapType.ContainerType = EPinContainerType::Map;
	DirectEnumMapType.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Byte;
	DirectEnumMapType.PinValueType.TerminalSubCategoryObject = DirectEnum;
	if (!TestTrue(TEXT("Cue accepts a container of the outer user-defined struct"),
		AddCuePayloadVariable(*Cue, TEXT("Payloads"), OuterArrayType))
		|| !TestTrue(TEXT("Cue accepts a map with a direct user-defined enum value"),
			AddCuePayloadVariable(*Cue, TEXT("ModesByName"), DirectEnumMapType)))
	{
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(Cue);
	if (!TestTrue(TEXT("Dependency Cue compiles"), Cue->Status != BS_Error))
	{
		return false;
	}

	FPaper2DPlusFrameCueSchema Baseline;
	FText Error;
	if (!TestTrue(TEXT("Dependency Cue schema is described"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeSchema(
			*Cue,
			EPaper2DPlusFrameCueSchemaSource::Compiled,
			Baseline,
			&Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	const FPaper2DPlusFrameCueSchemaDependency* OuterDependency = FindDependency(
		Baseline,
		EPaper2DPlusFrameCueSchemaDependencyKind::UserDefinedStruct,
		*OuterStruct);
	const FPaper2DPlusFrameCueSchemaDependency* InnerDependency = FindDependency(
		Baseline,
		EPaper2DPlusFrameCueSchemaDependencyKind::UserDefinedStruct,
		*InnerStruct);
	const FPaper2DPlusFrameCueSchemaDependency* RecursiveEnumDependency = FindDependency(
		Baseline,
		EPaper2DPlusFrameCueSchemaDependencyKind::UserDefinedEnum,
		*RecursiveEnum);
	const FPaper2DPlusFrameCueSchemaDependency* DirectEnumDependency = FindDependency(
		Baseline,
		EPaper2DPlusFrameCueSchemaDependencyKind::UserDefinedEnum,
		*DirectEnum);
	if (!TestNotNull(TEXT("Direct outer struct dependency is discovered through an array"),
		OuterDependency)
		|| !TestNotNull(TEXT("Recursive inner struct dependency is discovered"), InnerDependency)
		|| !TestNotNull(TEXT("Recursive enum dependency is discovered through nested containers"),
			RecursiveEnumDependency)
		|| !TestNotNull(TEXT("Direct enum dependency is discovered from a map value"),
			DirectEnumDependency))
	{
		return false;
	}
	TestEqual(TEXT("Each user-defined dependency is de-duplicated"),
		Baseline.Dependencies.Num(), 4);
	TestFalse(TEXT("Outer struct dependency has a real fingerprint"),
		OuterDependency->Fingerprint.IsEmpty());
	TestFalse(TEXT("Recursive enum dependency has a real fingerprint"),
		RecursiveEnumDependency->Fingerprint.IsEmpty());

	TestTrue(TEXT("Dependency Cue full durable baseline is established"),
		SeedDurableBaseline(*Cue, &Error));
	DependencyFixture.Package->SetDirtyFlag(false);
	TestTrue(TEXT("Clean dependency baseline is placement-ready"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(Cue->GeneratedClass).Availability
			== EPaper2DPlusFrameCueTypeAvailability::Ready);

	const FString DirectEnumFingerprint = DirectEnumDependency->Fingerprint;
	FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(DirectEnum);
	FKismetEditorUtilities::CompileBlueprint(Cue);
	CueFixture.Package->SetDirtyFlag(false);
	FPaper2DPlusFrameCueSchema AfterDirectEnum;
	if (!TestTrue(TEXT("Schema remains describable after a direct enum edit"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeSchema(
			*Cue,
			EPaper2DPlusFrameCueSchemaSource::Compiled,
			AfterDirectEnum,
			&Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	const FPaper2DPlusFrameCueSchemaDependency* ChangedDirectEnum = FindDependency(
		AfterDirectEnum,
		EPaper2DPlusFrameCueSchemaDependencyKind::UserDefinedEnum,
		*DirectEnum);
	TestTrue(TEXT("A direct map-value enum edit changes its dependency fingerprint"),
		ChangedDirectEnum && ChangedDirectEnum->Fingerprint != DirectEnumFingerprint);
	TestTrue(TEXT("A direct enum edit invalidates placement readiness"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(Cue->GeneratedClass).Availability
			!= EPaper2DPlusFrameCueTypeAvailability::Ready);

	TestTrue(TEXT("Direct enum change can establish a new full durable baseline"),
		SeedDurableBaseline(*Cue, &Error));
	DependencyFixture.Package->SetDirtyFlag(false);
	const FPaper2DPlusFrameCueSchemaDependency* BeforeInnerStruct = FindDependency(
		AfterDirectEnum,
		EPaper2DPlusFrameCueSchemaDependencyKind::UserDefinedStruct,
		*InnerStruct);
	if (!TestNotNull(TEXT("Recursive inner struct remains in the dependency set"),
		BeforeInnerStruct))
	{
		return false;
	}
	const FString InnerFingerprint = BeforeInnerStruct->Fingerprint;
	FEdGraphPinType IntegerType;
	IntegerType.PinCategory = UEdGraphSchema_K2::PC_Int;
	TestTrue(TEXT("Inner struct accepts a new field"),
		FStructureEditorUtils::AddVariable(InnerStruct, IntegerType));
	FKismetEditorUtilities::CompileBlueprint(Cue);
	CueFixture.Package->SetDirtyFlag(false);
	FPaper2DPlusFrameCueSchema AfterInnerStruct;
	if (!TestTrue(TEXT("Schema remains describable after a recursive struct edit"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeSchema(
			*Cue,
			EPaper2DPlusFrameCueSchemaSource::Compiled,
			AfterInnerStruct,
			&Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	const FPaper2DPlusFrameCueSchemaDependency* ChangedInner = FindDependency(
		AfterInnerStruct,
		EPaper2DPlusFrameCueSchemaDependencyKind::UserDefinedStruct,
		*InnerStruct);
	TestTrue(TEXT("A recursive struct edit changes its dependency fingerprint"),
		ChangedInner && ChangedInner->Fingerprint != InnerFingerprint);
	TestTrue(TEXT("A recursive struct edit invalidates placement readiness"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(Cue->GeneratedClass).Availability
			!= EPaper2DPlusFrameCueTypeAvailability::Ready);

	TestTrue(TEXT("Nested struct change can establish a new full durable baseline"),
		SeedDurableBaseline(*Cue, &Error));
	DependencyFixture.Package->SetDirtyFlag(false);
	const FPaper2DPlusFrameCueSchemaDependency* BeforeRecursiveEnum = FindDependency(
		AfterInnerStruct,
		EPaper2DPlusFrameCueSchemaDependencyKind::UserDefinedEnum,
		*RecursiveEnum);
	const FString RecursiveEnumFingerprint = BeforeRecursiveEnum
		? BeforeRecursiveEnum->Fingerprint
		: FString();
	FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(RecursiveEnum);
	FKismetEditorUtilities::CompileBlueprint(Cue);
	CueFixture.Package->SetDirtyFlag(false);
	FPaper2DPlusFrameCueSchema AfterRecursiveEnum;
	if (!TestTrue(TEXT("Schema remains describable after a recursively referenced enum edit"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeSchema(
			*Cue,
			EPaper2DPlusFrameCueSchemaSource::Compiled,
			AfterRecursiveEnum,
			&Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	const FPaper2DPlusFrameCueSchemaDependency* ChangedRecursiveEnum = FindDependency(
		AfterRecursiveEnum,
		EPaper2DPlusFrameCueSchemaDependencyKind::UserDefinedEnum,
		*RecursiveEnum);
	TestTrue(TEXT("A recursively referenced enum edit changes its dependency fingerprint"),
		ChangedRecursiveEnum
			&& ChangedRecursiveEnum->Fingerprint != RecursiveEnumFingerprint);
	TestTrue(TEXT("A recursively referenced enum edit invalidates placement readiness"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(Cue->GeneratedClass).Availability
			!= EPaper2DPlusFrameCueTypeAvailability::Ready);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeParentReadiness,
	"Paper2DPlus.FrameCues.CueType.Authoring.ParentReadiness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeParentReadiness::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringTest;
	FScopedAuthoringPackage ParentFixture(*this, TEXT("P2DPCueTypeParent"));
	FScopedAuthoringPackage ChildFixture(*this, TEXT("P2DPCueTypeChild"));
	if (!TestNotNull(TEXT("Parent fixture package exists"), ParentFixture.Package)
		|| !TestNotNull(TEXT("Child fixture package exists"), ChildFixture.Package))
	{
		return false;
	}

	UPaper2DPlusFrameCueBlueprint* Parent = MakeCompiledCueType(
		UPaper2DPlusCue::StaticClass(),
		TEXT("BP_ReadyParentCue"),
		true,
		ParentFixture.Package);
	if (!TestNotNull(TEXT("Specialized parent Cue Type compiles"), Parent))
	{
		return false;
	}
	FPaper2DPlusFrameCueSchema ParentSchema;
	FText Error;
	if (!TestTrue(TEXT("Parent Cue Type schema is described"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeSchema(
			*Parent,
			EPaper2DPlusFrameCueSchemaSource::Compiled,
			ParentSchema,
			&Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	TestTrue(TEXT("Parent establishes a full durable baseline"),
		SeedDurableBaseline(*Parent, &Error));

	UPaper2DPlusFrameCueBlueprint* Child = MakeCompiledCueType(
		Parent->GeneratedClass,
		TEXT("BP_ChildCue"),
		false,
		ChildFixture.Package);
	if (!TestNotNull(TEXT("Specialized child Cue Type compiles"), Child))
	{
		return false;
	}
	FPaper2DPlusFrameCueSchema ChildSchema;
	if (!TestTrue(TEXT("Child Cue Type schema is described"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeSchema(
			*Child,
			EPaper2DPlusFrameCueSchemaSource::Compiled,
			ChildSchema,
			&Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	TestTrue(TEXT("Child establishes a full durable baseline"),
		SeedDurableBaseline(*Child, &Error));
	TestTrue(TEXT("Child is ready only after both durable baselines match"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(Child->GeneratedClass).Availability
			== EPaper2DPlusFrameCueTypeAvailability::Ready);

	ParentFixture.Package->SetDirtyFlag(true);
	FPaper2DPlusFrameCueTypeDescriptor ChildDescriptor =
		FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(Child->GeneratedClass);
	TestTrue(TEXT("A dirty specialized parent blocks child placement"),
		ChildDescriptor.Availability
			== EPaper2DPlusFrameCueTypeAvailability::NeedsDurableSave);
	TestTrue(TEXT("Dirty parent failure is attributed to parent readiness"),
		HasDiagnostic(
			ChildDescriptor,
			EPaper2DPlusFrameCueDiagnosticCode::ParentCueTypeNotReady));
	ParentFixture.Package->SetDirtyFlag(false);

	const FString DurableParentFingerprint = Parent->DurableSchemaFingerprint;
	Parent->DurableSchemaFingerprint = TEXT("STALE-PARENT-SCHEMA");
	ParentFixture.Package->SetDirtyFlag(false);
	ChildDescriptor = FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(Child->GeneratedClass);
	TestTrue(TEXT("A stale parent durable schema blocks child placement"),
		ChildDescriptor.Availability
			== EPaper2DPlusFrameCueTypeAvailability::NeedsDurableSave);
	TestTrue(TEXT("Stale parent schema failure is attributed to parent readiness"),
		HasDiagnostic(
			ChildDescriptor,
			EPaper2DPlusFrameCueDiagnosticCode::ParentCueTypeNotReady));
	Parent->DurableSchemaFingerprint = DurableParentFingerprint;

	const auto PreviousStatus = Parent->Status;
	Parent->Status = BS_Dirty;
	ChildDescriptor = FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(Child->GeneratedClass);
	TestTrue(TEXT("An uncompiled specialized parent blocks child placement"),
		ChildDescriptor.Availability == EPaper2DPlusFrameCueTypeAvailability::NeedsCompile);
	TestTrue(TEXT("Uncompiled parent failure is attributed to parent readiness"),
		HasDiagnostic(
			ChildDescriptor,
			EPaper2DPlusFrameCueDiagnosticCode::ParentCueTypeNotReady));
	Parent->Status = PreviousStatus;

	UClass* ParentClass = Parent->GeneratedClass;
	const EClassFlags PreviousClassFlags = ParentClass->ClassFlags;
	ParentClass->ClassFlags |= CLASS_NewerVersionExists;
	ChildDescriptor = FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(Child->GeneratedClass);
	TestTrue(TEXT("A stale specialized parent class blocks child placement"),
		ChildDescriptor.Availability == EPaper2DPlusFrameCueTypeAvailability::Invalid);
	TestTrue(TEXT("Stale parent class failure is attributed to parent readiness"),
		HasDiagnostic(
			ChildDescriptor,
			EPaper2DPlusFrameCueDiagnosticCode::ParentCueTypeNotReady));
	ParentClass->ClassFlags = PreviousClassFlags;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeStructuredCreation,
	"Paper2DPlus.FrameCues.CueType.Authoring.StructuredCreation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeStructuredCreation::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringTest;
	FScopedAuthoringPackage Fixture(*this, TEXT("P2DPCueTypeCreate"));
	if (!TestNotNull(TEXT("Creation fixture package exists"), Fixture.Package))
	{
		return false;
	}

	FPaper2DPlusFrameCueTypeCreateRequest MomentRequest;
	MomentRequest.Package = Fixture.Package;
	MomentRequest.AssetName = TEXT("BP_AuthoringMoment");
	MomentRequest.ObjectFlags = RF_Public | RF_Standalone | RF_Transactional;
	const FPaper2DPlusFrameCueTypeCreateResult Moment =
		FPaper2DPlusFrameCueTypeAuthoring::CreateCueType(MomentRequest);
	if (!TestTrue(TEXT("Structured Cue creation succeeds"), Moment.IsSuccess())
		|| !TestNotNull(TEXT("Structured Cue returns its asset"), Moment.CueType))
	{
		return false;
	}
	TestTrue(TEXT("Created asset uses the specialized Blueprint envelope"),
		Moment.CueType->IsA<UPaper2DPlusFrameCueBlueprint>());
	TestTrue(TEXT("Moment selection maps to exactly the native Moment base"),
		Moment.CueType->ParentClass == UPaper2DPlusCue::StaticClass());
	TestTrue(TEXT("Caller-supplied asset flags are retained"),
		Moment.CueType->HasAllFlags(MomentRequest.ObjectFlags));
	TestNotNull(
		TEXT("Created Cue Type has a generated class"),
		Moment.CueType->GeneratedClass.Get());
	if (Moment.CueType->GeneratedClass)
	{
		TestTrue(TEXT("Generated class uses the specialized runtime envelope"),
			Cast<UPaper2DPlusFrameCueBlueprintGeneratedClass>(
				Moment.CueType->GeneratedClass.Get()) != nullptr);
		TestTrue(TEXT("Generated class has the exact selected native parent"),
			Moment.CueType->GeneratedClass->GetSuperClass()
				== UPaper2DPlusCue::StaticClass());
	}
	TestEqual(TEXT("New specialized Cue Type owns no graphs beyond its permitted event graph"),
		FPaper2DPlusFrameCueTypeAuthoring::InventoryLegacyGraphs(*Moment.CueType)
			.CountUnsupportedGraphs(),
		0);
	TestTrue(TEXT("New specialized Cue Type carries its permitted behavior event graph"),
		FPaper2DPlusFrameCueTypeAuthoring::InventoryLegacyGraphs(*Moment.CueType).HasEventGraphs());
	FText ValidationError;
	TestTrue(TEXT("New Cue passes the complete U8 compiled contract"),
		UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
			*Moment.CueType, &ValidationError));
	TestTrue(TEXT("New package remains dirty until a real save succeeds"),
		Fixture.Package->IsDirty());
	TestTrue(TEXT("Dirty new type cannot be placed"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(
			Moment.CueType->GeneratedClass).Availability
			== EPaper2DPlusFrameCueTypeAvailability::NeedsDurableSave);

	FPaper2DPlusFrameCueTypeCreateRequest RangeRequest = MomentRequest;
	RangeRequest.Kind = EPaper2DPlusFrameCueTypeKind::Range;
	RangeRequest.AssetName = TEXT("BP_AuthoringRange");
	const FPaper2DPlusFrameCueTypeCreateResult Range =
		FPaper2DPlusFrameCueTypeAuthoring::CreateCueType(RangeRequest);
	if (!TestTrue(TEXT("Structured Cue State creation succeeds"), Range.IsSuccess())
		|| !TestNotNull(TEXT("Structured Cue State returns its asset"), Range.CueType))
	{
		return false;
	}
	TestTrue(TEXT("Range selection maps to exactly the native Range base"),
		Range.CueType->ParentClass == UPaper2DPlusCueState::StaticClass());
	TestTrue(TEXT("Range generated envelope has the exact selected native parent"),
		Range.CueType->GeneratedClass
			&& Range.CueType->GeneratedClass->GetSuperClass()
				== UPaper2DPlusCueState::StaticClass());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeCreationFailures,
	"Paper2DPlus.FrameCues.CueType.Authoring.CreationFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeCreationFailures::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringTest;
	FScopedAuthoringPackage Fixture(*this, TEXT("P2DPCueTypeCreateFailures"));
	if (!TestNotNull(TEXT("Failure fixture package exists"), Fixture.Package))
	{
		return false;
	}

	FPaper2DPlusFrameCueTypeCreateRequest InvalidRequest;
	InvalidRequest.Package = Fixture.Package;
	InvalidRequest.AssetName = TEXT("BP_InvalidKind");
	InvalidRequest.Kind = static_cast<EPaper2DPlusFrameCueTypeKind>(255);
	const FPaper2DPlusFrameCueTypeCreateResult Invalid =
		FPaper2DPlusFrameCueTypeAuthoring::CreateCueType(InvalidRequest);
	TestTrue(TEXT("Creation rejects every kind except Moment or Range"),
		Invalid.Status == EPaper2DPlusFrameCueTypeCreateStatus::InvalidKind);
	TestNull(TEXT("Invalid kind creates no object"),
		StaticFindObject(UObject::StaticClass(), Fixture.Package, TEXT("BP_InvalidKind")));

	UObject* Occupant = NewObject<UBlueprint>(
		Fixture.Package, TEXT("BP_Collision"), RF_Public | RF_Standalone);
	Fixture.Package->SetDirtyFlag(false);
	FPaper2DPlusFrameCueTypeCreateRequest CollisionRequest;
	CollisionRequest.Package = Fixture.Package;
	CollisionRequest.AssetName = TEXT("BP_Collision");
	const FPaper2DPlusFrameCueTypeCreateResult Collision =
		FPaper2DPlusFrameCueTypeAuthoring::CreateCueType(CollisionRequest);
	TestTrue(TEXT("Creation rejects any occupied object name"),
		Collision.Status == EPaper2DPlusFrameCueTypeCreateStatus::NameCollision);
	TestTrue(TEXT("Collision rejection preserves the existing occupant"),
		StaticFindObject(UObject::StaticClass(), Fixture.Package, TEXT("BP_Collision"))
			== Occupant);

	Fixture.Package->SetDirtyFlag(false);
	FPaper2DPlusFrameCueTypeCreateRequest ForcedFailureRequest;
	ForcedFailureRequest.Package = Fixture.Package;
	ForcedFailureRequest.AssetName = TEXT("BP_ForcedFailure");
	ForcedFailureRequest.bFailAfterCreateForTests = true;
	const FPaper2DPlusFrameCueTypeCreateResult ForcedFailure =
		FPaper2DPlusFrameCueTypeAuthoring::CreateCueType(ForcedFailureRequest);
	TestFalse(TEXT("Injected post-create failure is reported"), ForcedFailure.IsSuccess());
	TestNull(TEXT("Failure cleanup removes the Blueprint object path"),
		StaticFindObject(UObject::StaticClass(), Fixture.Package, TEXT("BP_ForcedFailure")));
	TestNull(TEXT("Failure cleanup removes the generated-class export path"),
		StaticFindObject(UObject::StaticClass(), Fixture.Package, TEXT("BP_ForcedFailure_C")));
	TestFalse(TEXT("Failure cleanup restores the package dirty state"),
		Fixture.Package->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeDurableSaveCandidate,
	"Paper2DPlus.FrameCues.CueType.Authoring.DurableSaveCandidate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeDurableSaveCandidate::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringTest;
	FScopedAuthoringPackage Fixture(*this, TEXT("P2DPCueTypeDurableCandidate"));
	if (!TestNotNull(TEXT("Durable candidate fixture package exists"), Fixture.Package))
	{
		return false;
	}
	FPaper2DPlusFrameCueTypeCreateRequest Request;
	Request.Package = Fixture.Package;
	Request.AssetName = TEXT("BP_DurableCandidate");
	const FPaper2DPlusFrameCueTypeCreateResult Created =
		FPaper2DPlusFrameCueTypeAuthoring::CreateCueType(Request);
	if (!TestTrue(TEXT("Durable candidate fixture creation succeeds"), Created.IsSuccess())
		|| !TestNotNull(TEXT("Durable candidate fixture returns its asset"), Created.CueType))
	{
		return false;
	}

	const FPaper2DPlusFrameCueDurableSchemaCandidate Candidate =
		FPaper2DPlusFrameCueTypeAuthoring::PrepareDurableSchemaSaveCandidate(
			*Created.CueType);
	TestTrue(TEXT("Compiled schema produces a durable save candidate"), Candidate.bSuccess);
	TestFalse(TEXT("Durable save candidate fingerprint is non-empty"),
		Candidate.Fingerprint.IsEmpty());
	TestFalse(TEXT("Durable save candidate persists a full schema snapshot"),
		Candidate.SchemaSnapshot.IsEmpty());
	TestEqual(TEXT("Behavior-free save candidate reports schema v1"),
		Candidate.Version,
		FPaper2DPlusFrameCueTypeAuthoring::BehaviorFreeSchemaVersion);
	TestEqual(TEXT("Behavior-free save candidate writes schema v1"),
		Created.CueType->DurableSchemaVersion,
		FPaper2DPlusFrameCueTypeAuthoring::BehaviorFreeSchemaVersion);
	TestEqual(TEXT("Save candidate writes the validated compiled fingerprint"),
		Created.CueType->DurableSchemaFingerprint,
		Candidate.Fingerprint);
	TestEqual(TEXT("Save candidate writes the exact schema baseline"),
		Created.CueType->DurableSchemaSnapshot,
		Candidate.SchemaSnapshot);
	FPaper2DPlusFrameCueSchema ReloadedBaseline;
	FText SnapshotError;
	TestTrue(TEXT("Persisted schema baseline round-trips structurally"),
		FPaper2DPlusFrameCueTypeAuthoring::DeserializeSchemaSnapshot(
			Created.CueType->DurableSchemaSnapshot,
			ReloadedBaseline,
			&SnapshotError));
	TestEqual(TEXT("Round-tripped baseline retains its exact fingerprint"),
		ReloadedBaseline.Fingerprint,
		Candidate.Fingerprint);
	TestEqual(TEXT("Round-tripped behavior-free baseline retains schema v1"),
		ReloadedBaseline.Version,
		FPaper2DPlusFrameCueTypeAuthoring::BehaviorFreeSchemaVersion);
	TestTrue(TEXT("Preparing the save candidate leaves the package dirty"),
		Fixture.Package->IsDirty());
	TestTrue(TEXT("Matching but unsaved candidate is never placement-ready"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(
			Created.CueType->GeneratedClass).Availability
			== EPaper2DPlusFrameCueTypeAvailability::NeedsDurableSave);

	Fixture.Package->SetDirtyFlag(false); // Simulates a successful caller-owned package save.
	TestTrue(TEXT("Clean matching durable baseline becomes placement-ready"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(
			Created.CueType->GeneratedClass).Availability
			== EPaper2DPlusFrameCueTypeAvailability::Ready);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCueTypeExactOpaqueDefaults,
	"Paper2DPlus.FrameCues.CueType.Authoring.ExactOpaqueDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCueTypeExactOpaqueDefaults::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringTest;
	FScopedAuthoringPackage Fixture(*this, TEXT("P2DPCueTypeExactOpaqueDefaults"));
	if (!TestNotNull(TEXT("Exact-default fixture package exists"), Fixture.Package))
	{
		return false;
	}
	UPaper2DPlusFrameCueBlueprint* Blueprint = MakeCompiledCueType(
		UPaper2DPlusCue::StaticClass(),
		TEXT("BP_ExactOpaqueDefaults"),
		false,
		Fixture.Package);
	if (!TestNotNull(TEXT("Exact-default Cue Type fixture compiles"), Blueprint))
	{
		return false;
	}

	FEdGraphPinType StringType;
	StringType.PinCategory = UEdGraphSchema_K2::PC_String;
	FEdGraphPinType NameType;
	NameType.PinCategory = UEdGraphSchema_K2::PC_Name;
	FEdGraphPinType TextType;
	TextType.PinCategory = UEdGraphSchema_K2::PC_Text;
	if (!TestTrue(TEXT("Fixture adds an FString payload"),
			AddCuePayloadVariable(*Blueprint, TEXT("StringPath"), StringType))
		|| !TestTrue(TEXT("Fixture adds an FName payload"),
			AddCuePayloadVariable(*Blueprint, TEXT("NamePath"), NameType))
		|| !TestTrue(TEXT("Fixture adds an FText payload"),
			AddCuePayloadVariable(*Blueprint, TEXT("TextPath"), TextType)))
	{
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (!TestTrue(TEXT("Exact-default fixture remains compilable"), Blueprint->Status != BS_Error))
	{
		return false;
	}

	FStrProperty* StringProperty =
		FindFProperty<FStrProperty>(Blueprint->GeneratedClass, TEXT("StringPath"));
	FNameProperty* NameProperty =
		FindFProperty<FNameProperty>(Blueprint->GeneratedClass, TEXT("NamePath"));
	FTextProperty* TextProperty =
		FindFProperty<FTextProperty>(Blueprint->GeneratedClass, TEXT("TextPath"));
	UObject* Defaults = Blueprint->GeneratedClass
		? Blueprint->GeneratedClass->GetDefaultObject(false)
		: nullptr;
	if (!TestNotNull(TEXT("Generated FString property exists"), StringProperty)
		|| !TestNotNull(TEXT("Generated FName property exists"), NameProperty)
		|| !TestNotNull(TEXT("Generated FText property exists"), TextProperty)
		|| !TestNotNull(TEXT("Generated defaults exist"), Defaults))
	{
		return false;
	}

	const FString CurrentPaths[] = {
		TEXT("/Script/Paper2DPlus.Paper2DPlusCueBase"),
		TEXT("/Script/Paper2DPlus.Paper2DPlusCue"),
		TEXT("/Script/Paper2DPlus.Paper2DPlusCueState")
	};
	const FString LegacyPaths[] = {
		TEXT("/Script/Paper2DPlus.Paper2DPlusFrameCue"),
		TEXT("/Script/Paper2DPlus.Paper2DPlusMomentCue"),
		TEXT("/Script/Paper2DPlus.Paper2DPlusRangeCue")
	};
	StringProperty->SetPropertyValue_InContainer(Defaults, CurrentPaths[0]);
	NameProperty->SetPropertyValue_InContainer(Defaults, FName(*CurrentPaths[1]));
	TextProperty->SetPropertyValue_InContainer(Defaults, FText::FromString(CurrentPaths[2]));

	TMap<FName, FString> CapturedDefaults;
	FText Error;
	if (!TestTrue(TEXT("Exact opaque defaults are captured"),
		FPaper2DPlusFrameCueTypeAuthoring::CaptureDefaultValues(
			*Blueprint,
			CapturedDefaults,
			&Error)))
	{
		AddError(Error.ToString());
		return false;
	}

	const FProperty* Properties[] = { StringProperty, NameProperty, TextProperty };
	const FName PropertyNames[] = { TEXT("StringPath"), TEXT("NamePath"), TEXT("TextPath") };
	TMap<FName, FString> DefaultsToRestore;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Properties); ++Index)
	{
		const FString Expected = ExportDefaultForCopy(*Properties[Index], *Defaults);
		const FString* Captured = CapturedDefaults.Find(PropertyNames[Index]);
		if (!TestNotNull(TEXT("Captured map retains every opaque payload"), Captured))
		{
			return false;
		}
		TestEqual(TEXT("Capture preserves the property's exact exported text"), *Captured, Expected);
		TestTrue(TEXT("Capture retains the current class-path literal"),
			Captured->Contains(CurrentPaths[Index]));
		TestFalse(TEXT("Capture does not substitute the retired class-path literal"),
			Captured->Contains(LegacyPaths[Index]));
		DefaultsToRestore.Add(PropertyNames[Index], *Captured);
	}

	StringProperty->SetPropertyValue_InContainer(Defaults, TEXT("ChangedString"));
	NameProperty->SetPropertyValue_InContainer(Defaults, FName(TEXT("ChangedName")));
	TextProperty->SetPropertyValue_InContainer(Defaults, FText::FromString(TEXT("ChangedText")));
	if (!TestTrue(TEXT("Exact opaque defaults can be reapplied"),
		FPaper2DPlusFrameCueTypeAuthoring::ApplyDefaultValues(
			*Blueprint,
			DefaultsToRestore,
			&Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	TestEqual(TEXT("FString class-path literal restores exactly"),
		StringProperty->GetPropertyValue_InContainer(Defaults),
		CurrentPaths[0]);
	TestEqual(TEXT("FName class-path literal restores exactly"),
		NameProperty->GetPropertyValue_InContainer(Defaults),
		FName(*CurrentPaths[1]));
	TestEqual(TEXT("FText class-path literal restores exactly"),
		TextProperty->GetPropertyValue_InContainer(Defaults).ToString(),
		CurrentPaths[2]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeSchemaDiff,
	"Paper2DPlus.FrameCues.CueType.Authoring.SchemaDiff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeSchemaDiff::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringTest;

	const FPaper2DPlusFrameCueSchema Baseline = MakeSimpleSchema();
	TestTrue(TEXT("Equal schemas are identical"),
		FPaper2DPlusFrameCueTypeAuthoring::DiffSchemas(Baseline, Baseline).Compatibility
			== EPaper2DPlusFrameCueSchemaCompatibility::Identical);

	FPaper2DPlusFrameCueSchema Added = Baseline;
	FPaper2DPlusFrameCueSchemaField AddedField;
	AddedField.FieldId = FGuid::NewGuid();
	AddedField.Name = TEXT("Radius");
	AddedField.TypeKey = TEXT("real:float");
	AddedField.DefaultValue = TEXT("1");
	Added.Fields.Add(MoveTemp(AddedField));
	TestTrue(TEXT("Adding a serializable field is compatible"),
		FPaper2DPlusFrameCueTypeAuthoring::DiffSchemas(Baseline, Added).Compatibility
			== EPaper2DPlusFrameCueSchemaCompatibility::Compatible);

	FPaper2DPlusFrameCueSchema Renamed = Baseline;
	Renamed.Fields[0].Name = TEXT("ImpactPower");
	TestTrue(TEXT("Same-GUID rename is conditional"),
		FPaper2DPlusFrameCueTypeAuthoring::DiffSchemas(Baseline, Renamed).Compatibility
			== EPaper2DPlusFrameCueSchemaCompatibility::Conditional);

	FPaper2DPlusFrameCueSchema Removed = Baseline;
	Removed.Fields.Reset();
	TestTrue(TEXT("Removing a field is destructive"),
		FPaper2DPlusFrameCueTypeAuthoring::DiffSchemas(Baseline, Removed).Compatibility
			== EPaper2DPlusFrameCueSchemaCompatibility::Destructive);

	FPaper2DPlusFrameCueSchema Retyped = Baseline;
	Retyped.Fields[0].TypeKey = TEXT("real:float");
	TestTrue(TEXT("Changing a field type is destructive"),
		FPaper2DPlusFrameCueTypeAuthoring::DiffSchemas(Baseline, Retyped).Compatibility
			== EPaper2DPlusFrameCueSchemaCompatibility::Destructive);

	const TCHAR* LegacyClassPaths[] = {
		TEXT("/Script/Paper2DPlus.Paper2DPlusFrameCue"),
		TEXT("/Script/Paper2DPlus.Paper2DPlusMomentCue"),
		TEXT("/Script/Paper2DPlus.Paper2DPlusRangeCue")
	};
	const TCHAR* CurrentClassPaths[] = {
		TEXT("/Script/Paper2DPlus.Paper2DPlusCueBase"),
		TEXT("/Script/Paper2DPlus.Paper2DPlusCue"),
		TEXT("/Script/Paper2DPlus.Paper2DPlusCueState")
	};
	FPaper2DPlusFrameCueSchema LegacyIdentity = Baseline;
	FPaper2DPlusFrameCueSchema CurrentIdentity = Baseline;
	LegacyIdentity.bValid = true;
	CurrentIdentity.bValid = true;
	LegacyIdentity.ParentClassPath = FSoftObjectPath(LegacyClassPaths[1]);
	CurrentIdentity.ParentClassPath = FSoftObjectPath(CurrentClassPaths[1]);
	LegacyIdentity.Fields.Reset();
	CurrentIdentity.Fields.Reset();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(LegacyClassPaths); ++Index)
	{
		FPaper2DPlusFrameCueSchemaField& LegacyField =
			LegacyIdentity.Fields.AddDefaulted_GetRef();
		LegacyField.FieldId = FGuid(0, 0, 0, Index + 1);
		LegacyField.Name = FName(*FString::Printf(TEXT("LegacyClass%d"), Index));
		LegacyField.FriendlyName = TEXT("Class Reference");
		LegacyField.Category = TEXT("Payload");
		LegacyField.TypeKey = FString::Printf(
			TEXT("category=softclass;object=%s"),
			LegacyClassPaths[Index]);
		LegacyField.DefaultValue = FString::Printf(
			TEXT("SoftClass'%s'"),
			LegacyClassPaths[Index]);
		FPaper2DPlusFrameCueSchemaMetadata& LegacyMetadata =
			LegacyField.Metadata.AddDefaulted_GetRef();
		LegacyMetadata.Key = TEXT("ClassPath");
		LegacyMetadata.Value = TEXT("ExactMetadata");

		FPaper2DPlusFrameCueSchemaField& CurrentField =
			CurrentIdentity.Fields.AddDefaulted_GetRef();
		CurrentField = LegacyField;
		CurrentField.TypeKey = FString::Printf(
			TEXT("category=softclass;object=%s"),
			CurrentClassPaths[Index]);
		CurrentField.DefaultValue = FString::Printf(
			TEXT("SoftClass'%s'"),
			CurrentClassPaths[Index]);
	}
	FPaper2DPlusFrameCueInheritedDefault& LegacyInherited =
		LegacyIdentity.InheritedDefaults.AddDefaulted_GetRef();
	LegacyInherited.OwnerClassPath = FSoftObjectPath(LegacyClassPaths[2]);
	LegacyInherited.Name = TEXT("CueClass");
	LegacyInherited.TypeKey = FString::Printf(
		TEXT("category=class;object=%s"),
		LegacyClassPaths[1]);
	LegacyInherited.DefaultValue = FString::Printf(
		TEXT("Class'%s'"),
		LegacyClassPaths[0]);
	FPaper2DPlusFrameCueInheritedDefault& CurrentInherited =
		CurrentIdentity.InheritedDefaults.AddDefaulted_GetRef();
	CurrentInherited = LegacyInherited;
	CurrentInherited.OwnerClassPath = FSoftObjectPath(CurrentClassPaths[2]);
	CurrentInherited.TypeKey = FString::Printf(
		TEXT("category=class;object=%s"),
		CurrentClassPaths[1]);
	CurrentInherited.DefaultValue = FString::Printf(
		TEXT("Class'%s'"),
		CurrentClassPaths[0]);
	FPaper2DPlusFrameCueSchemaDependency& LegacyClassDependency =
		LegacyIdentity.Dependencies.AddDefaulted_GetRef();
	LegacyClassDependency.ObjectPath = FSoftObjectPath(LegacyClassPaths[0]);
	LegacyClassDependency.Fingerprint = TEXT("same");
	FPaper2DPlusFrameCueSchemaDependency& CurrentClassDependency =
		CurrentIdentity.Dependencies.AddDefaulted_GetRef();
	CurrentClassDependency.ObjectPath = FSoftObjectPath(CurrentClassPaths[0]);
	CurrentClassDependency.Fingerprint = TEXT("same");
	const FPaper2DPlusFrameCueSchemaDiff RenamedIdentityDiff =
		FPaper2DPlusFrameCueTypeAuthoring::DiffSchemas(
			LegacyIdentity,
			CurrentIdentity);
	TestTrue(TEXT("Renamed native class tokens preserve durable schema identity"),
		RenamedIdentityDiff.Compatibility
			== EPaper2DPlusFrameCueSchemaCompatibility::Identical);
	TestEqual(TEXT("Renamed native class tokens produce no false schema changes"),
		RenamedIdentityDiff.Changes.Num(),
		0);
	FString LegacyIdentitySnapshot;
	FString CurrentIdentitySnapshot;
	FText SnapshotError;
	if (!TestTrue(TEXT("Legacy reflection identity serializes"),
		FPaper2DPlusFrameCueTypeAuthoring::SerializeSchemaSnapshot(
			LegacyIdentity,
			LegacyIdentitySnapshot,
			&SnapshotError))
		|| !TestTrue(TEXT("Current reflection identity serializes"),
			FPaper2DPlusFrameCueTypeAuthoring::SerializeSchemaSnapshot(
			CurrentIdentity,
			CurrentIdentitySnapshot,
			&SnapshotError)))
	{
		AddError(SnapshotError.ToString());
		return false;
	}
	TestEqual(TEXT("Typed class/object defaults retain their pre-rename canonical form"),
		CurrentIdentitySnapshot,
		LegacyIdentitySnapshot);

	FPaper2DPlusFrameCueSchema LegacyOpaqueText = Baseline;
	LegacyOpaqueText.bValid = true;
	LegacyOpaqueText.Fields[0].TypeKey = TEXT("category=string;");
	LegacyOpaqueText.Fields[0].FriendlyName = LegacyClassPaths[0];
	LegacyOpaqueText.Fields[0].Category = LegacyClassPaths[1];
	LegacyOpaqueText.Fields[0].DefaultValue = LegacyClassPaths[2];
	FPaper2DPlusFrameCueSchemaMetadata& LegacyOpaqueMetadata =
		LegacyOpaqueText.Fields[0].Metadata.AddDefaulted_GetRef();
	LegacyOpaqueMetadata.Key = TEXT("OpaqueClassPath");
	LegacyOpaqueMetadata.Value = LegacyClassPaths[0];
	FPaper2DPlusFrameCueSchema CurrentOpaqueText = LegacyOpaqueText;
	CurrentOpaqueText.Fields[0].FriendlyName = CurrentClassPaths[0];
	CurrentOpaqueText.Fields[0].Category = CurrentClassPaths[1];
	CurrentOpaqueText.Fields[0].DefaultValue = CurrentClassPaths[2];
	CurrentOpaqueText.Fields[0].Metadata[0].Value = CurrentClassPaths[0];
	const FPaper2DPlusFrameCueSchemaDiff OpaqueTextDiff =
		FPaper2DPlusFrameCueTypeAuthoring::DiffSchemas(
			LegacyOpaqueText,
			CurrentOpaqueText);
	TestTrue(TEXT("Opaque class-path text remains a visible schema edit"),
		OpaqueTextDiff.Compatibility
			== EPaper2DPlusFrameCueSchemaCompatibility::Compatible);
	TestTrue(TEXT("Opaque default edits retain the focused diff kind"),
		OpaqueTextDiff.Changes.ContainsByPredicate(
			[](const FPaper2DPlusFrameCueSchemaChange& Change)
			{
				return Change.Kind
					== EPaper2DPlusFrameCueSchemaChangeKind::FieldDefaultChanged;
			}));
	TestTrue(TEXT("Opaque presentation edits retain the focused diff kind"),
		OpaqueTextDiff.Changes.ContainsByPredicate(
			[](const FPaper2DPlusFrameCueSchemaChange& Change)
			{
				return Change.Kind
					== EPaper2DPlusFrameCueSchemaChangeKind::FieldMetadataChanged;
			}));

	FString LegacyOpaqueSnapshot;
	FString CurrentOpaqueSnapshot;
	if (!TestTrue(TEXT("Legacy opaque schema serializes"),
		FPaper2DPlusFrameCueTypeAuthoring::SerializeSchemaSnapshot(
			LegacyOpaqueText,
			LegacyOpaqueSnapshot,
			&SnapshotError))
		|| !TestTrue(TEXT("Current opaque schema serializes"),
			FPaper2DPlusFrameCueTypeAuthoring::SerializeSchemaSnapshot(
			CurrentOpaqueText,
			CurrentOpaqueSnapshot,
			&SnapshotError)))
	{
		AddError(SnapshotError.ToString());
		return false;
	}
	TestNotEqual(TEXT("Opaque class-path text remains distinct in canonical snapshots"),
		CurrentOpaqueSnapshot,
		LegacyOpaqueSnapshot);
	FPaper2DPlusFrameCueSchema ReloadedOpaqueSchema;
	if (!TestTrue(TEXT("Current opaque schema snapshot reloads"),
		FPaper2DPlusFrameCueTypeAuthoring::DeserializeSchemaSnapshot(
			CurrentOpaqueSnapshot,
			ReloadedOpaqueSchema,
			&SnapshotError)))
	{
		AddError(SnapshotError.ToString());
		return false;
	}
	TestEqual(TEXT("Opaque friendly name survives canonical serialization"),
		ReloadedOpaqueSchema.Fields[0].FriendlyName,
		CurrentClassPaths[0]);
	TestEqual(TEXT("Opaque category survives canonical serialization"),
		ReloadedOpaqueSchema.Fields[0].Category,
		CurrentClassPaths[1]);
	TestEqual(TEXT("Opaque default survives canonical serialization"),
		ReloadedOpaqueSchema.Fields[0].DefaultValue,
		CurrentClassPaths[2]);
	TestEqual(TEXT("Opaque metadata survives canonical serialization"),
		ReloadedOpaqueSchema.Fields[0].Metadata[0].Value,
		CurrentClassPaths[0]);

	FPaper2DPlusFrameCueSchema InheritedDefaultChanged = CurrentIdentity;
	InheritedDefaultChanged.InheritedDefaults[0].DefaultValue = TEXT("None");
	const FPaper2DPlusFrameCueSchemaDiff InheritedDefaultDiff =
		FPaper2DPlusFrameCueTypeAuthoring::DiffSchemas(
			CurrentIdentity,
			InheritedDefaultChanged);
	TestTrue(TEXT("Inherited class-default edits are reported as compatible changes"),
		InheritedDefaultDiff.Compatibility
			== EPaper2DPlusFrameCueSchemaCompatibility::Compatible);
	TestTrue(TEXT("Inherited class-default edits use the focused diff kind"),
		InheritedDefaultDiff.Changes.Num() == 1
			&& InheritedDefaultDiff.Changes[0].Kind
				== EPaper2DPlusFrameCueSchemaChangeKind::InheritedDefaultChanged);

	FPaper2DPlusFrameCueSchemaDependency OldDependency;
	OldDependency.ObjectPath = FSoftObjectPath(TEXT("/Game/Data/S_CuePayload.S_CuePayload"));
	OldDependency.Fingerprint = TEXT("old");
	FPaper2DPlusFrameCueSchema WithDependency = Baseline;
	WithDependency.Dependencies.Add(OldDependency);
	FPaper2DPlusFrameCueSchema DependencyChanged = WithDependency;
	DependencyChanged.Dependencies[0].Fingerprint = TEXT("new");
	TestTrue(TEXT("Unproven referenced schema changes are dependency changes"),
		FPaper2DPlusFrameCueTypeAuthoring::DiffSchemas(
			WithDependency, DependencyChanged).Compatibility
			== EPaper2DPlusFrameCueSchemaCompatibility::DependencyChange);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeLegacyInventoryAndDiscovery,
	"Paper2DPlus.FrameCues.CueType.Authoring.LegacyInventoryAndDiscovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeLegacyInventoryAndDiscovery::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringTest;

	UPaper2DPlusFrameCueBlueprint* Legacy = MakeCompiledCueType(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_LegacyInventory"), false);
	if (!TestNotNull(TEXT("Legacy inventory fixture compiles before graph injection"), Legacy))
	{
		return false;
	}
	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(
		Legacy,
		TEXT("PreservedLegacyBehavior"),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph(
		Legacy, Graph, true, static_cast<UFunction*>(nullptr));
	const int32 FunctionGraphCountBefore = Legacy->FunctionGraphs.Num();
	const FPaper2DPlusFrameCueLegacyGraphInventory Inventory =
		FPaper2DPlusFrameCueTypeAuthoring::InventoryLegacyGraphs(*Legacy);
	TestEqual(TEXT("Authored legacy function graph is inventoried"),
		Inventory.CountUnsupportedGraphs(), 1);
	TestEqual(TEXT("Inventory never mutates the Blueprint"),
		Legacy->FunctionGraphs.Num(), FunctionGraphCountBefore);
	TestTrue(TEXT("Behavior-bearing specialized asset is quarantined"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(Legacy->GeneratedClass).Availability
			== EPaper2DPlusFrameCueTypeAvailability::Invalid);

	UPaper2DPlusFrameCueBlueprint* Ready = MakeCompiledCueType(
		UPaper2DPlusCueState::StaticClass(), TEXT("BP_P2DP_LoadedDiscovery"));
	if (!TestNotNull(TEXT("Loaded discovery fixture compiles"), Ready))
	{
		return false;
	}
	FPaper2DPlusFrameCueSchema ReadySchema;
	FText Error;
	if (!TestTrue(TEXT("Loaded discovery fixture schema is described"),
		FPaper2DPlusFrameCueTypeAuthoring::DescribeSchema(
			*Ready,
			EPaper2DPlusFrameCueSchemaSource::Compiled,
			ReadySchema,
			&Error)))
	{
		return false;
	}
	TestTrue(TEXT("Loaded discovery fixture establishes a full durable baseline"),
		SeedDurableBaseline(*Ready, &Error));

	const FPaper2DPlusFrameCueTypeDiscoveryResult Discovery =
		FPaper2DPlusFrameCueTypeAuthoring::DiscoverCueTypes(false);
	const FSoftObjectPath ReadyPath(Ready->GeneratedClass);
	TestTrue(TEXT("Loaded ready Cue Type is discovered without Asset Registry scan"),
		Discovery.Types.ContainsByPredicate(
			[&ReadyPath](const FPaper2DPlusFrameCueTypeDescriptor& Type)
			{
				return Type.ClassPath == ReadyPath;
			}));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
