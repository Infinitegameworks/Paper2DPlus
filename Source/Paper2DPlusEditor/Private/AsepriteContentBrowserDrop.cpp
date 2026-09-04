// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AsepriteContentBrowserDrop.h"

#include "BulkSpriteExtractorWindow.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserDelegates.h"      // FAssetViewDragAndDropExtender
#include "ContentBrowserDataSubsystem.h"  // virtual -> internal path conversion
#include "IContentBrowserDataModule.h"
#include "Input/DragAndDrop.h"            // FExternalDragOperation
#include "Modules/ModuleManager.h"

namespace Paper2DPlusEditor::AsepriteContentBrowserDrop
{
	bool IsAsepriteFile(const FString& Path)
	{
		return Path.EndsWith(TEXT(".ase"), ESearchCase::IgnoreCase)
			|| Path.EndsWith(TEXT(".aseprite"), ESearchCase::IgnoreCase);
	}

	void SplitDroppedFiles(const TArray<FString>& Files, TArray<FString>& OutAseFiles, TArray<FString>& OutOtherFiles)
	{
		OutAseFiles.Reset();
		OutOtherFiles.Reset();
		for (const FString& File : Files)
		{
			(IsAsepriteFile(File) ? OutAseFiles : OutOtherFiles).Add(File);
		}
	}

	namespace
	{
		/** The delegate handle of OUR extender's drop delegate — the extender array has no handle API of
		 *  its own, so this is how Unregister finds the entry it added. */
		FDelegateHandle GDropHandle;

		/** The payload's operation as an external FILE drag, or null for anything else (asset drags,
		 *  folder drags, an external drag that carries text). */
		const FExternalDragOperation* AsExternalFileDrag(const TSharedPtr<FDragDropOperation>& Operation)
		{
			if (!Operation.IsValid() || !Operation->IsOfType<FExternalDragOperation>())
			{
				return nullptr;
			}
			const FExternalDragOperation* External = static_cast<const FExternalDragOperation*>(Operation.Get());
			return External->HasFiles() ? External : nullptr;
		}

		bool CarriesAsepriteFiles(const FExternalDragOperation* External)
		{
			if (!External)
			{
				return false;
			}
			for (const FString& File : External->GetFiles())
			{
				if (IsAsepriteFile(File))
				{
					return true;
				}
			}
			return false;
		}

		/** The folder the files were dropped on, as an INTERNAL package path ("/Game/Characters").
		 *  The payload carries the Content Browser's VIRTUAL paths ("/All/Game/Characters"), which no
		 *  package call accepts, so the conversion is not optional. */
		FString ResolveDropFolder(const TArray<FName>& VirtualPaths)
		{
			UContentBrowserDataSubsystem* Subsystem = IContentBrowserDataModule::Get().GetSubsystem();
			for (const FName& VirtualPath : VirtualPaths)
			{
				if (!Subsystem)
				{
					break;
				}
				const FString VirtualString = VirtualPath.ToString();
				FString Internal;
				if (Subsystem->TryConvertVirtualPath(FStringView(VirtualString), Internal) == EContentBrowserPathType::Internal
					&& Internal.StartsWith(TEXT("/")))
				{
					return Internal;
				}
			}
			// A drop on a virtual-only folder (the "All" root, a collection) has no package folder.
			return TEXT("/Game");
		}

		bool OnDragOver(const FAssetViewDragAndDropExtender::FPayload& Payload)
		{
			return CarriesAsepriteFiles(AsExternalFileDrag(Payload.DragDropOp));
		}

		bool OnDrop(const FAssetViewDragAndDropExtender::FPayload& Payload)
		{
			const FExternalDragOperation* External = AsExternalFileDrag(Payload.DragDropOp);
			if (!External)
			{
				return false;
			}

			TArray<FString> AseFiles;
			TArray<FString> OtherFiles;
			SplitDroppedFiles(External->GetFiles(), AseFiles, OtherFiles);
			if (AseFiles.Num() == 0)
			{
				return false; // not ours — the ordinary import path takes the drop
			}

			const FString Folder = ResolveDropFolder(Payload.PackagePaths);

			// Everything that is NOT an Aseprite file goes down the road it always took, so a mixed
			// drop (a .ase beside its reference .png) still imports the .png.
			if (OtherFiles.Num() > 0)
			{
				IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
				AssetTools.ImportAssets(OtherFiles, Folder);
			}

			// LAST, so the window ends up in front of whatever the import above may have opened.
			SBulkSpriteExtractorWindow::OpenBulkExtractorForAseFiles(AseFiles, Folder);
			return true;
		}
	}

	void Register()
	{
		if (GDropHandle.IsValid())
		{
			return;
		}
		FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		FAssetViewDragAndDropExtender Extender(
			FAssetViewDragAndDropExtender::FOnDropDelegate::CreateStatic(&OnDrop),
			FAssetViewDragAndDropExtender::FOnDragOverDelegate::CreateStatic(&OnDragOver));
		GDropHandle = Extender.OnDropDelegate.GetHandle();
		ContentBrowser.GetAssetViewDragAndDropExtenders().Add(MoveTemp(Extender));
	}

	void Unregister()
	{
		if (!GDropHandle.IsValid())
		{
			return;
		}
		if (FContentBrowserModule* ContentBrowser = FModuleManager::GetModulePtr<FContentBrowserModule>("ContentBrowser"))
		{
			ContentBrowser->GetAssetViewDragAndDropExtenders().RemoveAll(
				[](const FAssetViewDragAndDropExtender& Extender)
				{
					return Extender.OnDropDelegate.GetHandle() == GDropHandle;
				});
		}
		GDropHandle.Reset();
	}
}
