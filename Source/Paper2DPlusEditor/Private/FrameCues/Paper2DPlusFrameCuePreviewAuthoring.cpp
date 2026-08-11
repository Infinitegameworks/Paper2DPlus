// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCuePreviewAuthoring.h"

#include "AssetToolsModule.h"
#include "Factories/BlueprintFactory.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewAdapter.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusFrameCuePreviewAuthoring"

bool FPaper2DPlusFrameCuePreviewAuthoring::ValidateCreateRequest(
	const FPaper2DPlusFrameCuePreviewAdapterCreateRequest& Request,
	FString& OutLongPackageName,
	FText& OutError)
{
	OutLongPackageName.Reset();
	OutError = FText::GetEmpty();

	FString PackagePath = Request.PackagePath.TrimStartAndEnd();
	while (PackagePath.EndsWith(TEXT("/")))
	{
		PackagePath.LeftChopInline(1);
	}
	const FString AssetName = Request.AssetName.TrimStartAndEnd();
	OutLongPackageName = PackagePath + TEXT("/") + AssetName;

	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		OutError = LOCTEXT("MissingPathOrName", "Choose a package path and asset name before creating a preview adapter.");
		return false;
	}
	if (PackagePath != TEXT("/Game") && !PackagePath.StartsWith(TEXT("/Game/")))
	{
		OutError = LOCTEXT(
			"PreviewAdapterOutsideGame",
			"Frame Cue Preview Adapters must be created under /Game so they remain project-owned editor assets.");
		return false;
	}

	FText PathReason;
	if (!FPackageName::IsValidLongPackageName(OutLongPackageName, false, &PathReason)
		|| !FPackageName::IsValidObjectPath(OutLongPackageName + TEXT(".") + AssetName, &PathReason))
	{
		OutError = FText::Format(LOCTEXT("InvalidAssetPath", "The preview-adapter asset path is invalid: {0}"), PathReason);
		return false;
	}

	const UClass* CueClass = Request.SupportedCueClass.Get();
	if (!CueClass || !CueClass->IsChildOf(UPaper2DPlusCueBase::StaticClass()))
	{
		OutError = LOCTEXT("InvalidCueClass", "Choose a Frame Cue class for this preview adapter.");
		return false;
	}
	if (CueClass->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		OutError = LOCTEXT("StaleCueClass", "The selected Frame Cue class is deprecated or was replaced. Choose its current class.");
		return false;
	}

	if (FindPackage(nullptr, *OutLongPackageName) || FPackageName::DoesPackageExist(OutLongPackageName))
	{
		OutError = LOCTEXT("AssetAlreadyExists", "An asset package already exists at that path. Choose another name.");
		return false;
	}

	return true;
}

UBlueprint* FPaper2DPlusFrameCuePreviewAuthoring::CreatePreviewAdapterBlueprint(
	const FPaper2DPlusFrameCuePreviewAdapterCreateRequest& Request,
	FText& OutError)
{
	FString LongPackageName;
	if (!ValidateCreateRequest(Request, LongPackageName, OutError))
	{
		return nullptr;
	}

	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = UPaper2DPlusFrameCuePreviewAdapter::StaticClass();

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UBlueprint* Blueprint = Cast<UBlueprint>(AssetTools.CreateAsset(
		Request.AssetName.TrimStartAndEnd(),
		FPackageName::GetLongPackagePath(LongPackageName),
		UBlueprint::StaticClass(),
		Factory));
	if (!Blueprint)
	{
		OutError = LOCTEXT("CreateFailed", "Unreal could not create the preview-adapter Blueprint.");
		return nullptr;
	}

	if (!Blueprint->GeneratedClass)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
	}
	UPaper2DPlusFrameCuePreviewAdapter* AdapterDefaults = Blueprint->GeneratedClass
		? Cast<UPaper2DPlusFrameCuePreviewAdapter>(Blueprint->GeneratedClass->GetDefaultObject())
		: nullptr;
	if (!AdapterDefaults)
	{
		ObjectTools::DeleteSingleObject(Blueprint, false);
		OutError = LOCTEXT("MissingAdapterDefaults", "The new Blueprint did not generate Frame Cue preview-adapter defaults.");
		return nullptr;
	}

	AdapterDefaults->SupportedCueClass = Request.SupportedCueClass;
	AdapterDefaults->bIncludeDerivedCueClasses = Request.bIncludeDerivedCueClasses;
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (Blueprint->Status == BS_Error)
	{
		ObjectTools::DeleteSingleObject(Blueprint, false);
		OutError = LOCTEXT("CompileFailed", "The preview-adapter Blueprint failed its initial compile and was not kept.");
		return nullptr;
	}

	OutError = FText::GetEmpty();
	return Blueprint;
}

#undef LOCTEXT_NAMESPACE
