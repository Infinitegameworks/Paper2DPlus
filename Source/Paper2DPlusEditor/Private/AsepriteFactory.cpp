// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AsepriteFactory.h"
#include "AsepriteImporter.h"
#include "AsepriteLayerImportDialog.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "PaperFlipbook.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FeedbackContext.h" // FFeedbackContext::Logf — UE 5.7's PCH pulled this in transitively; older engines need it explicit

#define LOCTEXT_NAMESPACE "AsepriteFactory"

UAsepriteFactory::UAsepriteFactory()
{
	bCreateNew = false;
	bEditorImport = true;
	bText = false;
	SupportedClass = UObject::StaticClass();
	Formats.Add(TEXT("ase;Aseprite Sprite File"));
	Formats.Add(TEXT("aseprite;Aseprite Sprite File"));
}

UObject* UAsepriteFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	const FString OutputPath = InParent ? InParent->GetPathName() : TEXT("/Game");
	const FString AssetPrefix = InName.ToString();

	// 1. Parse the Aseprite file
	FAsepriteParsedData ParsedData;
	FString ErrorMsg;
	if (!FAsepriteImporter::ParseFile(Filename, ParsedData, ErrorMsg))
	{
		if (Warn)
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("Aseprite parse failed: %s"), *ErrorMsg);
		}
		bOutOperationCanceled = false;
		return nullptr;
	}

	// 2. Run per-layer compositing for preview buffers
	TMap<int32, TArray<TArray<FColor>>> PerLayerBuffers = FAsepriteImporter::CompositePerLayer(ParsedData);

	// 3. Create and show the modal import dialog
	TSharedRef<SWindow> DialogWindow = SNew(SWindow)
		.Title(LOCTEXT("LayerImportDialogTitle", "Import Aseprite Layers"))
		.ClientSize(FVector2D(900, 600))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.IsTopmostWindow(true);

	TSharedPtr<SAsepiteLayerImportDialog> Dialog;
	DialogWindow->SetContent(
		SAssignNew(Dialog, SAsepiteLayerImportDialog)
			.ParsedData(&ParsedData)
			.PerLayerBuffers(&PerLayerBuffers)
			.ParentWindow(DialogWindow)
			.DefaultOutputPath(OutputPath)
			.DefaultAssetPrefix(AssetPrefix)
	);

	FSlateApplication::Get().AddModalWindow(DialogWindow, FSlateApplication::Get().GetActiveTopLevelWindow());

	// 4. Read dialog results
	const FAsepriteLayerImportSettings& Settings = Dialog->GetImportSettings();
	if (!Settings.bUserConfirmed)
	{
		bOutOperationCanceled = true;
		return nullptr;
	}

	// 5. Populate SourceFilePath on a mutable copy of settings for the pipeline
	FAsepriteLayerImportSettings MutableSettings = Settings;
	MutableSettings.SourceFilePath = Filename;

	// 6. Run the asset generation pipeline
	UObject* Result = FAsepriteImporter::ImportAsLayeredAsset(ParsedData, PerLayerBuffers, MutableSettings);
	bOutOperationCanceled = (Result == nullptr);
	return Result;
}

bool UAsepriteFactory::FactoryCanImport(const FString& Filename)
{
	return Filename.EndsWith(TEXT(".ase")) || Filename.EndsWith(TEXT(".aseprite"));
}

bool UAsepriteFactory::DoesSupportClass(UClass* Class)
{
	return Class == UObject::StaticClass()
		|| Class == UPaperFlipbook::StaticClass()
		|| Class == UTexture2D::StaticClass()
		|| Class == UPaper2DPlusCharacterLayerAsset::StaticClass();
}

UClass* UAsepriteFactory::ResolveSupportedClass()
{
	return UObject::StaticClass();
}

FText UAsepriteFactory::GetDisplayName() const
{
	return LOCTEXT("AsepriteFactoryDisplayName", "Aseprite File");
}

#undef LOCTEXT_NAMESPACE
