// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "HAL/PlatformProperties.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusDirectionalAnimationLibrary.h"
#include "Paper2DPlusModule.h"
#include "PaperFlipbook.h"
#include "UObject/Package.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace Paper2DPlusDirectionalAnimationCookedRuntimeTest
{
	static const TCHAR* GeneratedClassPath =
		TEXT("/Game/Paper2DPlusDirectionalAutomation/P2DPDirectionalAnimationBlueprintFixture.BP_DirectionalAnimationCookedRuntimeFixture_C");
	static const TCHAR* ProfilePath =
		TEXT("/Game/Paper2DPlusDirectionalAutomation/P2DPDirectionalAnimationProfile.DirectionalProfile");
	static const TCHAR* DirectionalBasePath =
		TEXT("/Game/Paper2DPlusDirectionalAutomation/P2DPDirectionalBase.DirectionalBase");
	static const TCHAR* SlotZeroPath =
		TEXT("/Game/Paper2DPlusDirectionalAutomation/P2DPDirectionalSlotZero.DirectionalSlotZero");
	static const TCHAR* SlotThreePath =
		TEXT("/Game/Paper2DPlusDirectionalAutomation/P2DPDirectionalSlotThree.DirectionalSlotThree");
	static const TCHAR* LegacyBasePath =
		TEXT("/Game/Paper2DPlusDirectionalAutomation/P2DPDirectionalLegacyBase.DirectionalLegacyBase");
	static const TCHAR* ConfiguredEmptyBasePath =
		TEXT("/Game/Paper2DPlusDirectionalAutomation/P2DPDirectionalConfiguredEmptyBase.DirectionalConfiguredEmptyBase");

	static const FName ResolveProofFunctionName(TEXT("RunDirectionalResolveProof"));
	static const FName EnumerationProofFunctionName(TEXT("RunDirectionalEnumerationProof"));

	int64 ReadEnumValue(UFunction& Function, uint8* Parameters, const FName Name)
	{
		if (const FEnumProperty* EnumProperty = FindFProperty<FEnumProperty>(&Function, Name))
		{
			const void* Value = EnumProperty->ContainerPtrToValuePtr<void>(Parameters);
			const FNumericProperty* Underlying = EnumProperty->GetUnderlyingProperty();
			return Underlying->GetSignedIntPropertyValue(Value);
		}
		if (const FByteProperty* ByteProperty = FindFProperty<FByteProperty>(&Function, Name))
		{
			return ByteProperty->GetPropertyValue_InContainer(Parameters);
		}
		return INDEX_NONE;
	}

	bool ReadBool(UFunction& Function, uint8* Parameters, const FName Name)
	{
		const FBoolProperty* Property = FindFProperty<FBoolProperty>(&Function, Name);
		return Property && Property->GetPropertyValue_InContainer(Parameters);
	}

	int32 ReadInt(UFunction& Function, uint8* Parameters, const FName Name)
	{
		const FIntProperty* Property = FindFProperty<FIntProperty>(&Function, Name);
		return Property ? Property->GetPropertyValue_InContainer(Parameters) : MAX_int32;
	}

	UObject* ReadObject(UFunction& Function, uint8* Parameters, const FName Name)
	{
		const FObjectPropertyBase* Property =
			FindFProperty<FObjectPropertyBase>(&Function, Name);
		return Property ? Property->GetObjectPropertyValue_InContainer(Parameters) : nullptr;
	}

	bool HasHardFlipbookProjection(UFunction& Function, const FName Name)
	{
		const FObjectPropertyBase* Property =
			FindFProperty<FObjectPropertyBase>(&Function, Name);
		return Property && Property->PropertyClass == UPaperFlipbook::StaticClass();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalAnimationCookedRuntimeTest,
	"Paper2DPlus.DirectionalAnimation.CookedRuntime.BlueprintQueries",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalAnimationCookedRuntimeTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusDirectionalAnimationCookedRuntimeTest;

	if (!FParse::Param(
		FCommandLine::Get(),
		TEXT("Paper2DPlusDirectionalAnimationCookedRuntimeProof")))
	{
		AddError(TEXT(
			"Cooked directional-animation proof requires "
			"-Paper2DPlusDirectionalAnimationCookedRuntimeProof in a cooked client."));
		return false;
	}

	TestFalse(TEXT("Cooked runtime proof excludes editor code"), WITH_EDITOR != 0);
	TestFalse(TEXT("Cooked runtime proof excludes editor-only data"), WITH_EDITORONLY_DATA != 0);
	TestTrue(TEXT("Target platform requires cooked data"), FPlatformProperties::RequiresCookedData());
	TestEqual(TEXT("Directional cooked proof compiles with WITH_PAPERZD=0"), WITH_PAPERZD, 0);

	FModuleManager& ModuleManager = FModuleManager::Get();
	TestFalse(
		TEXT("Paper2DPlusEditor has no staged runtime module binary"),
		ModuleManager.ModuleExists(TEXT("Paper2DPlusEditor")));
	TestFalse(
		TEXT("Paper2DPlusEditor is not loaded"),
		ModuleManager.IsModuleLoaded(TEXT("Paper2DPlusEditor")));
	TestFalse(
		TEXT("PaperZD has no staged runtime module binary"),
		ModuleManager.ModuleExists(TEXT("PaperZD")));

	UClass* BlueprintClass = LoadObject<UClass>(nullptr, GeneratedClassPath);
	if (!TestNotNull(TEXT("Cooked directional Blueprint class loads"), BlueprintClass))
	{
		return false;
	}
	TestEqual(
		TEXT("Cooked directional Blueprint class keeps its deterministic path"),
		BlueprintClass->GetPathName(),
		FString(GeneratedClassPath));
	TestTrue(
		TEXT("Cooked directional Blueprint class belongs to a cooked package"),
		BlueprintClass->GetOutermost()->HasAnyPackageFlags(PKG_Cooked));

	UPaper2DPlusCharacterProfileAsset* Profile =
		LoadObject<UPaper2DPlusCharacterProfileAsset>(nullptr, ProfilePath);
	UPaperFlipbook* DirectionalBase = LoadObject<UPaperFlipbook>(nullptr, DirectionalBasePath);
	UPaperFlipbook* SlotZero = LoadObject<UPaperFlipbook>(nullptr, SlotZeroPath);
	UPaperFlipbook* LegacyBase = LoadObject<UPaperFlipbook>(nullptr, LegacyBasePath);
	UPaperFlipbook* ConfiguredEmptyBase =
		LoadObject<UPaperFlipbook>(nullptr, ConfiguredEmptyBasePath);
	if (!TestNotNull(TEXT("Cooked directional Profile loads"), Profile)
		|| !TestNotNull(TEXT("Cooked directional base loads"), DirectionalBase)
		|| !TestNotNull(TEXT("Cooked slot-zero alias key loads"), SlotZero)
		|| !TestNotNull(TEXT("Cooked legacy base loads"), LegacyBase)
		|| !TestNotNull(TEXT("Cooked configured-empty base loads"), ConfiguredEmptyBase))
	{
		return false;
	}
	TestTrue(
		TEXT("Cooked Character Profile package carries PKG_Cooked"),
		Profile->GetOutermost()->HasAnyPackageFlags(PKG_Cooked));
	TestEqual(TEXT("Cooked Profile retains three logical animations"), Profile->Flipbooks.Num(), 3);
	TestEqual(
		TEXT("Cooked Profile retains current directional schema 9"),
		Profile->GetCharacterProfileJsonSchemaVersion(),
		9);

	TestNull(
		TEXT("Slot three starts cold before the Blueprint resolver selects its soft reference"),
		FindObject<UPaperFlipbook>(nullptr, SlotThreePath));

	UFunction* ResolveProof = BlueprintClass->FindFunctionByName(ResolveProofFunctionName);
	UFunction* EnumerationProof = BlueprintClass->FindFunctionByName(EnumerationProofFunctionName);
	if (!TestNotNull(TEXT("Cooked Blueprint resolve proof function"), ResolveProof)
		|| !TestNotNull(TEXT("Cooked Blueprint enumeration proof function"), EnumerationProof))
	{
		return false;
	}
	TestTrue(
		TEXT("Cooked Blueprint resolve proof remains a static Blueprint function"),
		(ResolveProof->FunctionFlags & (FUNC_Static | FUNC_BlueprintCallable))
			== (FUNC_Static | FUNC_BlueprintCallable));
	TestTrue(
		TEXT("Occupied resolver output has hard UPaperFlipbook projection"),
		HasHardFlipbookProjection(*ResolveProof, TEXT("OccupiedFlipbook")));
	TestTrue(
		TEXT("Variant resolver output has hard UPaperFlipbook projection"),
		HasHardFlipbookProjection(*ResolveProof, TEXT("VariantFlipbook")));
	TestTrue(
		TEXT("Exact-empty resolver output has hard UPaperFlipbook projection"),
		HasHardFlipbookProjection(*ResolveProof, TEXT("EmptyFlipbook")));

	FStructOnScope ResolveParameters(ResolveProof);
	BlueprintClass->GetDefaultObject()->ProcessEvent(
		ResolveProof,
		ResolveParameters.GetStructMemory());
	uint8* ResolveMemory = ResolveParameters.GetStructMemory();
	TestTrue(
		TEXT("Blueprint Has Multi Direction recognizes the canonical base"),
		ReadBool(*ResolveProof, ResolveMemory, TEXT("HasBase")));
	TestTrue(
		TEXT("Blueprint Has Multi Direction recognizes a variant owner alias"),
		ReadBool(*ResolveProof, ResolveMemory, TEXT("HasVariant")));
	TestEqual(
		TEXT("Blueprint occupied resolution returns Success"),
		ReadEnumValue(*ResolveProof, ResolveMemory, TEXT("OccupiedResult")),
		static_cast<int64>(EPaper2DPlusDirectionalAnimationResult::Success));
	TestEqual(
		TEXT("Blueprint occupied resolution selects slot three"),
		ReadInt(*ResolveProof, ResolveMemory, TEXT("OccupiedSlotIndex")),
		3);
	UPaperFlipbook* SlotThree = Cast<UPaperFlipbook>(
		ReadObject(*ResolveProof, ResolveMemory, TEXT("OccupiedFlipbook")));
	if (!TestNotNull(TEXT("Blueprint occupied resolution loads slot three"), SlotThree))
	{
		return false;
	}
	TestEqual(
		TEXT("Blueprint occupied resolution loads the exact staged soft asset"),
		SlotThree->GetPathName(),
		FString(SlotThreePath));

	TestEqual(
		TEXT("Blueprint variant-key resolution aliases the same logical owner"),
		ReadEnumValue(*ResolveProof, ResolveMemory, TEXT("VariantResult")),
		static_cast<int64>(EPaper2DPlusDirectionalAnimationResult::Success));
	TestTrue(
		TEXT("Blueprint variant-key resolution returns the same slot-three asset"),
		ReadObject(*ResolveProof, ResolveMemory, TEXT("VariantFlipbook")) == SlotThree);
	TestEqual(
		TEXT("Blueprint variant-key resolution keeps slot index three"),
		ReadInt(*ResolveProof, ResolveMemory, TEXT("VariantSlotIndex")),
		3);

	TestEqual(
		TEXT("Blueprint exact empty sector reports Direction Unoccupied"),
		ReadEnumValue(*ResolveProof, ResolveMemory, TEXT("EmptyResult")),
		static_cast<int64>(EPaper2DPlusDirectionalAnimationResult::DirectionUnoccupied));
	TestNull(
		TEXT("Blueprint exact-empty failure clears the flipbook output"),
		ReadObject(*ResolveProof, ResolveMemory, TEXT("EmptyFlipbook")));
	// Deliberate contract change (TASK-190 review): the empty-sector failure reports WHICH sector
	// resolved so a Blueprint can implement its own fallback. The fixture probes (-1, 0) = slot 6.
	TestEqual(
		TEXT("Blueprint exact-empty failure reports the resolved empty sector"),
		ReadInt(*ResolveProof, ResolveMemory, TEXT("EmptySlotIndex")),
		6);

	TestEqual(
		TEXT("Blueprint legacy base fallback returns Success"),
		ReadEnumValue(*ResolveProof, ResolveMemory, TEXT("BaseResult")),
		static_cast<int64>(EPaper2DPlusDirectionalAnimationResult::Success));
	TestTrue(
		TEXT("Blueprint legacy base fallback returns its canonical base"),
		ReadObject(*ResolveProof, ResolveMemory, TEXT("BaseFlipbook")) == LegacyBase);
	TestEqual(
		TEXT("Blueprint legacy base fallback uses no directional slot"),
		ReadInt(*ResolveProof, ResolveMemory, TEXT("BaseSlotIndex")),
		INDEX_NONE);
	TestEqual(
		TEXT("Blueprint configured-empty fallback returns Success"),
		ReadEnumValue(*ResolveProof, ResolveMemory, TEXT("ConfiguredEmptyResult")),
		static_cast<int64>(EPaper2DPlusDirectionalAnimationResult::Success));
	TestTrue(
		TEXT("Blueprint configured-empty fallback returns its canonical base"),
		ReadObject(*ResolveProof, ResolveMemory, TEXT("ConfiguredEmptyFlipbook"))
			== ConfiguredEmptyBase);
	TestEqual(
		TEXT("Blueprint configured-empty fallback uses no directional slot"),
		ReadInt(*ResolveProof, ResolveMemory, TEXT("ConfiguredEmptySlotIndex")),
		INDEX_NONE);

	const FArrayProperty* OccupiedSlotsProperty =
		FindFProperty<FArrayProperty>(EnumerationProof, TEXT("OccupiedSlots"));
	const FStructProperty* OccupiedSlotInner = OccupiedSlotsProperty
		? CastField<FStructProperty>(OccupiedSlotsProperty->Inner)
		: nullptr;
	const FObjectPropertyBase* OccupiedRecordFlipbookProperty = OccupiedSlotInner
		? FindFProperty<FObjectPropertyBase>(
			OccupiedSlotInner->Struct,
			TEXT("Flipbook"))
		: nullptr;
	TestTrue(
		TEXT("Blueprint occupied array keeps the exact public slot struct"),
		OccupiedSlotInner
			&& OccupiedSlotInner->Struct
				== FPaper2DPlusOccupiedDirectionSlot::StaticStruct());
	TestTrue(
		TEXT("Blueprint occupied slot records project hard UPaperFlipbook values"),
		OccupiedRecordFlipbookProperty
			&& OccupiedRecordFlipbookProperty->PropertyClass
				== UPaperFlipbook::StaticClass());

	FStructOnScope EnumerationParameters(EnumerationProof);
	BlueprintClass->GetDefaultObject()->ProcessEvent(
		EnumerationProof,
		EnumerationParameters.GetStructMemory());
	uint8* EnumerationMemory = EnumerationParameters.GetStructMemory();
	TestEqual(
		TEXT("Blueprint occupied-slot enumeration returns Success"),
		ReadEnumValue(
			*EnumerationProof,
			EnumerationMemory,
			TEXT("EnumerationResult")),
		static_cast<int64>(EPaper2DPlusDirectionalAnimationResult::Success));
	if (!TestNotNull(TEXT("Blueprint occupied-slot array property"), OccupiedSlotsProperty)
		|| !TestNotNull(TEXT("Blueprint occupied-slot inner struct"), OccupiedSlotInner))
	{
		return false;
	}
	void* ArrayValue = OccupiedSlotsProperty->ContainerPtrToValuePtr<void>(EnumerationMemory);
	FScriptArrayHelper OccupiedSlots(OccupiedSlotsProperty, ArrayValue);
	TestEqual(TEXT("Blueprint enumeration returns exactly two sparse slots"), OccupiedSlots.Num(), 2);
	const FIntProperty* SlotIndexProperty = FindFProperty<FIntProperty>(
		OccupiedSlotInner->Struct,
		TEXT("SlotIndex"));
	if (!TestNotNull(TEXT("Occupied slot index reflection"), SlotIndexProperty)
		|| !TestNotNull(
			TEXT("Occupied slot flipbook reflection"),
			OccupiedRecordFlipbookProperty)
		|| OccupiedSlots.Num() != 2)
	{
		return false;
	}
	const void* FirstSlot = OccupiedSlots.GetRawPtr(0);
	const void* SecondSlot = OccupiedSlots.GetRawPtr(1);
	TestEqual(
		TEXT("Blueprint enumeration sorts slot zero first"),
		SlotIndexProperty->GetPropertyValue_InContainer(FirstSlot),
		0);
	TestEqual(
		TEXT("Blueprint enumeration sorts slot three second"),
		SlotIndexProperty->GetPropertyValue_InContainer(SecondSlot),
		3);
	TestTrue(
		TEXT("Blueprint enumeration returns the exact slot-zero flipbook"),
		OccupiedRecordFlipbookProperty->GetObjectPropertyValue_InContainer(FirstSlot)
			== SlotZero);
	TestTrue(
		TEXT("Blueprint enumeration returns the exact slot-three flipbook"),
		OccupiedRecordFlipbookProperty->GetObjectPropertyValue_InContainer(SecondSlot)
			== SlotThree);

	if (!HasAnyErrors())
	{
		UE_LOG(
			LogPaper2DPlus,
			Display,
			TEXT("P2DP_DIRECTIONAL_COOKED_RUNTIME_PASS has_base=1 has_variant=1 occupied_result=Success occupied_slot=3 variant_result=Success empty_result=DirectionUnoccupied empty_flipbook=null empty_slot=6 base_result=Success base_slot=-1 configured_empty_result=Success occupied_slots=0,3 hard_flipbook_projection=1 with_paperzd=0"));
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
