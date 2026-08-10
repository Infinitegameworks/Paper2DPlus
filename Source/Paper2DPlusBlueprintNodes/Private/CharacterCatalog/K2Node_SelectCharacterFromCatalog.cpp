// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterCatalog/K2Node_SelectCharacterFromCatalog.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "KismetCompiler.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusCharacterCatalogBlueprintNodes"

const FName UK2Node_SelectCharacterFromCatalog::CatalogPinName(TEXT("Catalog"));
const FName UK2Node_SelectCharacterFromCatalog::CharacterPinName(TEXT("Character"));
const FName UK2Node_SelectCharacterFromCatalog::EntryPinName(TEXT("Entry"));
const FName UK2Node_SelectCharacterFromCatalog::FoundPinName(TEXT("Found"));

void UK2Node_SelectCharacterFromCatalog::AllocateDefaultPins()
{
	UEdGraphPin* CatalogPin = CreatePin(
		EGPD_Input,
		UEdGraphSchema_K2::PC_Object,
		UPaper2DPlusCharacterCatalogAsset::StaticClass(),
		CatalogPinName);
	CatalogPin->PinFriendlyName = LOCTEXT("CatalogPinLabel", "Catalog");

	UEdGraphPin* CharacterPin = CreatePin(
		EGPD_Input,
		UEdGraphSchema_K2::PC_SoftObject,
		UPaper2DPlusCharacterProfileAsset::StaticClass(),
		CharacterPinName);
	CharacterPin->PinFriendlyName = LOCTEXT("CharacterPinLabel", "Character");

	UEdGraphPin* EntryPin = CreatePin(
		EGPD_Output,
		UEdGraphSchema_K2::PC_Struct,
		FPaper2DPlusCharacterCatalogEntry::StaticStruct(),
		EntryPinName);
	EntryPin->PinFriendlyName = LOCTEXT("EntryPinLabel", "Entry");

	UEdGraphPin* FoundPin = CreatePin(
		EGPD_Output,
		UEdGraphSchema_K2::PC_Boolean,
		FoundPinName);
	FoundPin->PinFriendlyName = LOCTEXT("FoundPinLabel", "Found");

	Super::AllocateDefaultPins();
}

void UK2Node_SelectCharacterFromCatalog::ExpandNode(
	FKismetCompilerContext& CompilerContext,
	UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	UFunction* LookupFunction = UPaper2DPlusCharacterCatalogAsset::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UPaper2DPlusCharacterCatalogAsset, FindEntryByCharacterProfile));
	if (!LookupFunction)
	{
		CompilerContext.MessageLog.Error(TEXT("@@ could not resolve the Character Catalog lookup function."), this);
		BreakAllNodeLinks();
		return;
	}

	UK2Node_CallFunction* LookupNode =
		CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	LookupNode->SetFromFunction(LookupFunction);
	LookupNode->AllocateDefaultPins();

	UEdGraphPin* CatalogPin = FindPin(CatalogPinName, EGPD_Input);
	UEdGraphPin* CharacterPin = FindPin(CharacterPinName, EGPD_Input);
	UEdGraphPin* EntryPin = FindPin(EntryPinName, EGPD_Output);
	UEdGraphPin* FoundPin = FindPin(FoundPinName, EGPD_Output);
	UEdGraphPin* LookupCatalogPin = LookupNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
	UEdGraphPin* LookupCharacterPin = LookupNode->FindPin(TEXT("CharacterProfile"), EGPD_Input);
	UEdGraphPin* LookupEntryPin = LookupNode->FindPin(TEXT("OutEntry"), EGPD_Output);
	UEdGraphPin* LookupFoundPin = LookupNode->GetReturnValuePin();

	if (!CatalogPin || !CharacterPin || !EntryPin || !FoundPin
		|| !LookupCatalogPin || !LookupCharacterPin || !LookupEntryPin || !LookupFoundPin)
	{
		CompilerContext.MessageLog.Error(TEXT("@@ could not create its internal Character Catalog lookup."), this);
		BreakAllNodeLinks();
		return;
	}

	CompilerContext.MovePinLinksToIntermediate(*CatalogPin, *LookupCatalogPin);
	CompilerContext.MovePinLinksToIntermediate(*CharacterPin, *LookupCharacterPin);
	CompilerContext.MovePinLinksToIntermediate(*EntryPin, *LookupEntryPin);
	CompilerContext.MovePinLinksToIntermediate(*FoundPin, *LookupFoundPin);
	BreakAllNodeLinks();
}

void UK2Node_SelectCharacterFromCatalog::ValidateNodeDuringCompilation(
	FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);

	const UEdGraphPin* CatalogPin = FindPin(CatalogPinName, EGPD_Input);
	const UEdGraphPin* CharacterPin = FindPin(CharacterPinName, EGPD_Input);
	if (!CatalogPin || (CatalogPin->LinkedTo.Num() == 0
		&& !Cast<UPaper2DPlusCharacterCatalogAsset>(CatalogPin->DefaultObject)))
	{
		MessageLog.Error(TEXT("@@ requires a loaded Character Catalog object."), this);
	}

	if (!CharacterPin)
	{
		MessageLog.Error(TEXT("@@ is missing its Character selection pin."), this);
		return;
	}
	if (CharacterPin->LinkedTo.Num() > 0)
	{
		return;
	}

	const FSoftObjectPath CharacterPath = GetLiteralCharacterProfilePath();
	if (CharacterPath.IsNull())
	{
		MessageLog.Error(TEXT("@@ requires a Character Profile selection."), this);
		return;
	}
	if (CatalogPin && CatalogPin->LinkedTo.Num() > 0)
	{
		// A runtime Catalog connection cannot be evaluated safely by the editor. Found remains the
		// authoritative runtime signal; the picker still uses the project default for authoring options.
		return;
	}

	if (UPaper2DPlusCharacterCatalogAsset* Catalog = ResolveCatalogForEditor())
	{
		FPaper2DPlusCharacterCatalogEntry Entry;
		if (!Catalog->FindEntryByCharacterProfile(
			TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>(CharacterPath), Entry))
		{
			MessageLog.Warning(
				TEXT("@@ selects a Character Profile that is no longer present in the resolved Catalog."),
				this);
		}
	}
}

void UK2Node_SelectCharacterFromCatalog::GetMenuActions(
	FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner);
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FText UK2Node_SelectCharacterFromCatalog::GetMenuCategory() const
{
	return LOCTEXT("CatalogNodeCategory", "Paper2DPlus|Character Catalog");
}

FText UK2Node_SelectCharacterFromCatalog::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("SelectCharacterFromCatalogTitle", "Select Character from Catalog");
}

FText UK2Node_SelectCharacterFromCatalog::GetTooltipText() const
{
	return LOCTEXT(
		"SelectCharacterFromCatalogTooltip",
		"Selects one Character Profile through a Catalog-filtered asset picker and returns the complete "
		"saved Catalog entry. The Catalog must already be loaded; the lookup never loads companion assets. "
		"When Catalog is connected dynamically, the editor picker uses the project default Catalog.");
}

UPaper2DPlusCharacterCatalogAsset*
UK2Node_SelectCharacterFromCatalog::ResolveCatalogForEditor() const
{
	const UEdGraphPin* CatalogPin = FindPin(CatalogPinName, EGPD_Input);
	if (CatalogPin)
	{
		if (UPaper2DPlusCharacterCatalogAsset* LiteralCatalog =
			Cast<UPaper2DPlusCharacterCatalogAsset>(CatalogPin->DefaultObject))
		{
			return LiteralCatalog;
		}
	}

	TSoftObjectPtr<UPaper2DPlusCharacterCatalogAsset> DefaultCatalog =
		UPaper2DPlusBlueprintLibrary::GetDefaultCharacterCatalog();
	if (UPaper2DPlusCharacterCatalogAsset* LoadedCatalog = DefaultCatalog.Get())
	{
		return LoadedCatalog;
	}
	return DefaultCatalog.LoadSynchronous();
}

TSet<FSoftObjectPath>
UK2Node_SelectCharacterFromCatalog::GetSelectableCharacterProfilePaths() const
{
	TSet<FSoftObjectPath> Paths;
	const UPaper2DPlusCharacterCatalogAsset* Catalog = ResolveCatalogForEditor();
	if (!Catalog)
	{
		return Paths;
	}

	for (const FPaper2DPlusCharacterCatalogEntry& Entry : Catalog->GetCatalogEntries())
	{
		const FSoftObjectPath Path = Entry.CharacterProfile.ToSoftObjectPath();
		if (!Path.IsNull())
		{
			Paths.Add(Path);
		}
	}
	return Paths;
}

bool UK2Node_SelectCharacterFromCatalog::IsCharacterProfileSelectable(
	const FSoftObjectPath& CharacterProfilePath) const
{
	return !CharacterProfilePath.IsNull()
		&& GetSelectableCharacterProfilePaths().Contains(CharacterProfilePath);
}

FSoftObjectPath UK2Node_SelectCharacterFromCatalog::GetLiteralCharacterProfilePath() const
{
	const UEdGraphPin* CharacterPin = FindPin(CharacterPinName, EGPD_Input);
	if (!CharacterPin || CharacterPin->LinkedTo.Num() > 0)
	{
		return FSoftObjectPath();
	}
	if (CharacterPin->DefaultObject)
	{
		return FSoftObjectPath(CharacterPin->DefaultObject);
	}
	return FSoftObjectPath(CharacterPin->DefaultValue);
}

#undef LOCTEXT_NAMESPACE
