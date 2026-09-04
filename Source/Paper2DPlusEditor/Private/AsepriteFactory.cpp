// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AsepriteFactory.h"
#include "BulkSpriteExtractorWindow.h" // TASK-189: .ase drops route to the bulk window
#include "Paper2DPlusCharacterLayerAsset.h"
#include "PaperFlipbook.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FeedbackContext.h" // FFeedbackContext::Logf — UE 5.7's PCH pulled this in transitively; older engines need it explicit
#include "Misc/PackageName.h" // FPackageName::GetLongPackagePath — the output FOLDER from the factory's package InParent

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
	// TASK-189: this factory no longer imports anything. Aseprite files are batch sources now — one
	// Character Profile, one Layer Profile and one Import All for the whole set — so the file is
	// handed to the Bulk Sprite Extractor and the factory reports itself canceled.
	//
	// A Content Browser DROP never reaches this factory any more: AsepriteContentBrowserDrop claims
	// external `.ase`/`.aseprite` drops on the asset view and passes the WHOLE file list to the window
	// in one call. What still arrives here is the Import button / File > Import path (one file per
	// multi-file import, see the log line below) and any host that calls the factory directly.
	//
	// InParent is the PACKAGE being created ("/Game/MainChar/NCBJ_File"), so the output FOLDER is its
	// PARENT path; using the package path itself nested every import inside a folder named after the file.
	const FString OutputPath = InParent
		? FPackageName::GetLongPackagePath(InParent->GetPathName())
		: TEXT("/Game");

	// Unattended import (automation task, commandlet, -unattended): opening a window there is a hang.
	// Report the no-error cancel shape and say why.
	if (!FSlateApplication::IsInitialized())
	{
		if (Warn)
		{
			Warn->Logf(ELogVerbosity::Warning,
				TEXT("Aseprite drop ignored (no Slate application): .ase import runs through the Bulk Sprite Extractor window. File: %s"),
				*Filename);
		}
		bOutOperationCanceled = true;
		return nullptr;
	}

	SBulkSpriteExtractorWindow::OpenBulkExtractorForAseFiles({ Filename }, OutputPath);

	if (Warn)
	{
		// ONE file per import, by engine design: AssetTools declares bImportWasCancelled once OUTSIDE
		// its per-file loop and the loop condition reads it, so the cancel shape that suppresses the
		// error dialog also stops the remaining files. Every multi-file door goes around this factory:
		// a Content Browser drop (AsepriteContentBrowserDrop), a drop onto the open window, and
		// Paper2D+ Actions > Import Aseprite Files... all deliver the whole set in one call.
		Warn->Logf(ELogVerbosity::Log,
			TEXT("Aseprite file handed to the Bulk Sprite Extractor: %s (output %s). Only the first file of a multi-file Import arrives here; drag the files into the Content Browser, drop them onto the open window, or use Import Aseprite Files... for a set."),
			*Filename, *OutputPath);
	}

	bOutOperationCanceled = true;
	return nullptr;
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
