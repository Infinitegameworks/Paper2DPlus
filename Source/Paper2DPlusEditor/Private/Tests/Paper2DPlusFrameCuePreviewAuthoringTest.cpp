// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Paper2DPlusEditorTestFrameCueTypes.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewAdapter.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewAuthoring.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewBlueprintLibrary.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePreviewTypedUtilityCompileTest,
	"Paper2DPlus.FrameCues.Editor.PreviewAuthoring.TypedUtilityCompiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePreviewTypedUtilityCompileTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusEditorTestMomentCue* ConcreteCue = NewObject<UPaper2DPlusEditorTestMomentCue>();
	TestTrue(TEXT("Derived matching returns the cue"),
		UPaper2DPlusFrameCuePreviewBlueprintLibrary::MatchFrameCueForPreview(
			ConcreteCue, UPaper2DPlusCue::StaticClass(), false) == ConcreteCue);
	TestNull(TEXT("Exact matching rejects a derived cue"),
		UPaper2DPlusFrameCuePreviewBlueprintLibrary::MatchFrameCueForPreview(
			ConcreteCue, UPaper2DPlusCue::StaticClass(), true));

	const FName BlueprintName = MakeUniqueObjectName(
		GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_FrameCuePreviewTypedFixture"));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		UPaper2DPlusFrameCuePreviewAdapter::StaticClass(),
		GetTransientPackage(),
		BlueprintName,
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		NAME_None);
	UEdGraph* Graph = Blueprint ? FBlueprintEditorUtils::FindEventGraph(Blueprint) : nullptr;
	if (!TestNotNull(TEXT("Transient adapter Blueprint"), Blueprint)
		|| !TestNotNull(TEXT("Transient adapter event graph"), Graph))
	{
		return false;
	}

	UFunction* MatchFunction = UPaper2DPlusFrameCuePreviewBlueprintLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UPaper2DPlusFrameCuePreviewBlueprintLibrary, MatchFrameCueForPreview));
	UK2Node_CallFunction* MatchNode = NewObject<UK2Node_CallFunction>(Graph);
	MatchNode->SetFromFunction(MatchFunction);
	MatchNode->CreateNewGuid();
	Graph->AddNode(MatchNode, false, false);
	MatchNode->AllocateDefaultPins();

	UEdGraphPin* ClassPin = MatchNode->FindPin(TEXT("CueClass"), EGPD_Input);
	UEdGraphPin* ResultPin = MatchNode->GetReturnValuePin();
	if (!TestNotNull(TEXT("Literal cue-class pin"), ClassPin)
		|| !TestNotNull(TEXT("Typed result pin"), ResultPin))
	{
		return false;
	}
	ClassPin->DefaultObject = UPaper2DPlusEditorTestMomentCue::StaticClass();
	MatchNode->PinDefaultValueChanged(ClassPin);
	TestTrue(TEXT("Literal class determines the concrete result without a cast"),
		ResultPin->PinType.PinSubCategoryObject.Get() == UPaper2DPlusEditorTestMomentCue::StaticClass());

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	TestTrue(TEXT("Transient preview-adapter Blueprint compiles"), Blueprint->Status != BS_Error);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCuePreviewAdapterCreationTest,
	"Paper2DPlus.FrameCues.Editor.PreviewAuthoring.ValidatesBeforeCreate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCuePreviewAdapterCreationTest::RunTest(const FString& Parameters)
{
	const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FPaper2DPlusFrameCuePreviewAdapterCreateRequest Request;
	Request.PackagePath = TEXT("/Game/__Paper2DPlusAutomation");
	Request.AssetName = TEXT("BP_FrameCuePreviewAdapter_") + Suffix;

	const FString ExpectedPackage = Request.PackagePath + TEXT("/") + Request.AssetName;
	FString LongPackageName;
	FText Error;
	TestFalse(TEXT("Missing cue class is rejected"),
		FPaper2DPlusFrameCuePreviewAuthoring::ValidateCreateRequest(Request, LongPackageName, Error));
	TestNull(TEXT("Invalid request creates no Blueprint"),
		FPaper2DPlusFrameCuePreviewAuthoring::CreatePreviewAdapterBlueprint(Request, Error));
	TestNull(TEXT("Invalid request creates no package"), FindPackage(nullptr, *ExpectedPackage));

	Request.SupportedCueClass = UPaper2DPlusEditorTestMomentCue::StaticClass();
	const TArray<FString> InvalidRoots = {
		TEXT("/Engine/FrameCuePreviews"),
		TEXT("/Paper2DPlus/FrameCuePreviews"),
		TEXT("/GameLike/FrameCuePreviews") };
	for (const FString& InvalidRoot : InvalidRoots)
	{
		Request.PackagePath = InvalidRoot;
		TestFalse(*FString::Printf(TEXT("Non-project root is rejected: %s"), *InvalidRoot),
			FPaper2DPlusFrameCuePreviewAuthoring::ValidateCreateRequest(
				Request, LongPackageName, Error));
		TestNull(TEXT("Rejected root creates no loaded package"),
			FindPackage(nullptr, *(InvalidRoot + TEXT("/") + Request.AssetName)));
	}
	Request.PackagePath = TEXT("/Game/__Paper2DPlusAutomation");
	TestTrue(TEXT("Valid request passes without creating the package"),
		FPaper2DPlusFrameCuePreviewAuthoring::ValidateCreateRequest(Request, LongPackageName, Error));
	TestNull(TEXT("Validation remains side-effect free"), FindPackage(nullptr, *ExpectedPackage));

	UBlueprint* Created = FPaper2DPlusFrameCuePreviewAuthoring::CreatePreviewAdapterBlueprint(Request, Error);
	if (TestNotNull(TEXT("Valid request creates an adapter Blueprint"), Created))
	{
		TestTrue(TEXT("Created adapter compiles"), Created->Status != BS_Error);
		const UPaper2DPlusFrameCuePreviewAdapter* Defaults = Created->GeneratedClass
			? Cast<UPaper2DPlusFrameCuePreviewAdapter>(Created->GeneratedClass->GetDefaultObject())
			: nullptr;
		if (TestNotNull(TEXT("Created adapter defaults"), Defaults))
		{
			TestTrue(TEXT("Supported cue class is initialized"),
				Defaults->SupportedCueClass == UPaper2DPlusEditorTestMomentCue::StaticClass());
			TestTrue(TEXT("Derived-class policy is initialized"), Defaults->bIncludeDerivedCueClasses);
		}
		ObjectTools::DeleteSingleObject(Created, false);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
