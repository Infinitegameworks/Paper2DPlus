// Copyright 2026 Infinite Gameworks. All Rights Reserved.

/** FSpriteExtractorActions — Content browser menu registration: Extract Sprites, Combine Textures, Import Aseprite, Bulk Extract to CharacterProfile. Also handles texture combination and sprite repacking. */

#include "SpriteExtractorWindow.h"
#include "AsepriteImporter.h"
#include "BulkSpriteExtractorWindow.h"
#include "SpriteExtractionUtils.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "ToolMenus.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "PaperSprite.h"
#include "PaperFlipbook.h"
#include "ScopedTransaction.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "SpriteExtractor"

// ============================================
// Console commands — make the extractors scriptable / ECABridge-launchable.
// The Tools-menu entry hides when an asset editor is focused, so a console path
// that always works is needed. File-scope FAutoConsoleCommand objects, mirroring
// Paper2DPlus.OpenDrawEditor in FlipbookDrawEditorToolkit.cpp.
// ============================================
namespace Paper2DPlusExtractorConsole
{
	/** Resolve UTexture2D args from console paths; falls back to the current Content Browser
	 *  selection when no path args are given (mirrors the menu entries). */
	static TArray<UTexture2D*> ResolveTextureArgs(const TArray<FString>& Args)
	{
		TArray<UTexture2D*> Textures;
		for (const FString& Arg : Args)
		{
			if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Arg))
			{
				Textures.Add(Tex);
			}
		}
		if (Textures.Num() == 0)
		{
			FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			TArray<FAssetData> SelectedAssets;
			CBModule.Get().GetSelectedAssets(SelectedAssets);
			for (const FAssetData& Asset : SelectedAssets)
			{
				if (UTexture2D* Tex = Cast<UTexture2D>(Asset.GetAsset()))
				{
					Textures.Add(Tex);
				}
			}
		}
		return Textures;
	}

	FAutoConsoleCommandWithWorldAndArgs GOpenSpriteExtractorCmd(
		TEXT("Paper2DPlus.OpenSpriteExtractor"),
		TEXT("Open the Paper2D+ Sprite Extractor. Arg: optional /Game texture path (else the current Content Browser selection); no texture = empty extractor."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
		{
			const TArray<UTexture2D*> Textures = ResolveTextureArgs(Args);
			if (Textures.Num() > 0)
			{
				FSpriteExtractorActions::OpenSpriteExtractorForTexture(Textures[0]);
			}
			else
			{
				FSpriteExtractorActions::OpenSpriteExtractor();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GOpenDebakeVariantsCmd(
		TEXT("Paper2DPlus.OpenDebakeVariants"),
		TEXT("Open the Paper2D+ Bulk Sprite Extractor with the sheets pre-grouped as a de-bake set. Args: optional /Game texture paths for the variant sheets (else the current Content Browser selection)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
		{
			const TArray<UTexture2D*> Resolved = ResolveTextureArgs(Args);
			TArray<TSoftObjectPtr<UTexture2D>> Textures;
			for (UTexture2D* Tex : Resolved)
			{
				Textures.Add(TSoftObjectPtr<UTexture2D>(Tex));
			}
			// Group creation gates on >= 2 sheets (toast), so an empty/short list is safe to open.
			SBulkSpriteExtractorWindow::OpenBulkExtractorForDebake(Textures);
		}));

	FAutoConsoleCommandWithWorldAndArgs GOpenBulkExtractorCmd(
		TEXT("Paper2DPlus.OpenBulkExtractor"),
		TEXT("Open the Paper2D+ Bulk Sprite Extractor. Args: optional /Game texture paths (else the current Content Browser selection)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
		{
			const TArray<UTexture2D*> Resolved = ResolveTextureArgs(Args);
			TArray<TSoftObjectPtr<UTexture2D>> Textures;
			for (UTexture2D* Tex : Resolved)
			{
				Textures.Add(TSoftObjectPtr<UTexture2D>(Tex));
			}
			// OpenBulkExtractor surfaces its own "no textures" dialog, so an empty list is safe.
			SBulkSpriteExtractorWindow::OpenBulkExtractor(Textures);
		}));
}

// ============================================
// FSpriteExtractorActions Implementation
// ============================================

void FSpriteExtractorActions::RegisterMenus()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		// Extend texture context menu — add Paper2D+ submenu in its own section positioned directly after Paper2D's "GetAssetActions"
		// section (Paper2D's "Sprite Actions" submenu is an FExtender appended at the tail of GetAssetActions, so this sibling section lands right below it).
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.Texture2D");
		if (Menu)
		{
			FToolMenuSection& Section = Menu->AddSection(
				"Paper2DPlusActions",
				FText::GetEmpty(),
				FToolMenuInsert("GetAssetActions", EToolMenuInsertType::After));
			Section.AddSubMenu(
				"Paper2DPlusActions",
				LOCTEXT("Paper2DPlusActionsLabel", "Paper2D+ Actions"),
				LOCTEXT("Paper2DPlusActionsTooltip", "Paper2D+ sprite extraction and import tools"),
				FNewToolMenuDelegate::CreateLambda([](UToolMenu* SubMenu)
				{
					FToolMenuSection& SubSection = SubMenu->FindOrAddSection("Default");
					SubSection.AddMenuEntry(
						"ExtractSprites",
						LOCTEXT("ExtractSprites", "Extract Sprites"),
						LOCTEXT("ExtractSpritesTooltip", "Open the Paper2D+ sprite extractor for this texture"),
						FSlateIcon("PaperStyle", "AssetActions.ExtractSprites"),
						FUIAction(FExecuteAction::CreateLambda([]()
						{
							FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
							TArray<FAssetData> SelectedAssets;
							ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

							for (const FAssetData& Asset : SelectedAssets)
							{
								if (UTexture2D* Texture = Cast<UTexture2D>(Asset.GetAsset()))
								{
									OpenSpriteExtractorForTexture(Texture);
									break;
								}
							}
						}))
					);

					SubSection.AddMenuEntry(
						"ImportAsepriteFile",
						LOCTEXT("ImportAseprite", "Import Aseprite File"),
						LOCTEXT("ImportAsepriteTooltip", "Import an Aseprite (.ase/.aseprite) file and create Paper2D sprites/flipbooks"),
						FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Import"),
						FUIAction(FExecuteAction::CreateStatic(&FAsepriteImporter::ShowImportDialog))
					);

					SubSection.AddMenuEntry(
						"BulkExtractTextures",
						LOCTEXT("BulkExtractTextures", "Bulk Extract Textures"),
						LOCTEXT("BulkExtractTexturesTooltip",
							"Open the bulk extractor with all selected textures loaded as one alignment group. Auto-pad, trim, and extract sprites with cross-sheet alignment."),
						FSlateIcon("PaperStyle", "AssetActions.ExtractSprites"),
						FUIAction(
							FExecuteAction::CreateLambda([]()
							{
								FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
								TArray<FAssetData> SelectedAssets;
								ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

								TArray<TSoftObjectPtr<UTexture2D>> Textures;
								for (const FAssetData& Asset : SelectedAssets)
								{
									if (UTexture2D* Texture = Cast<UTexture2D>(Asset.GetAsset()))
									{
										Textures.Add(TSoftObjectPtr<UTexture2D>(Texture));
									}
								}
								if (Textures.Num() >= 1)
								{
									SBulkSpriteExtractorWindow::OpenBulkExtractor(Textures);
								}
							}),
							FCanExecuteAction::CreateLambda([]() -> bool
							{
								FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
								TArray<FAssetData> SelectedAssets;
								ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);
								int32 TexCount = 0;
								for (const FAssetData& Asset : SelectedAssets)
								{
									if (Asset.GetClass() == UTexture2D::StaticClass()) TexCount++;
								}
								return TexCount >= 1;
							})
						)
					);

					SubSection.AddMenuEntry(
						"CombineTextures",
						LOCTEXT("CombineTextures", "Combine into Spritesheet"),
						LOCTEXT("CombineTexturesTooltip", "Combine selected textures into a single spritesheet and open in the sprite extractor"),
						FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Merge"),
						FUIAction(
							FExecuteAction::CreateLambda([]()
							{
								FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
								TArray<FAssetData> SelectedAssets;
								ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

								TArray<UTexture2D*> Textures;
								for (const FAssetData& Asset : SelectedAssets)
								{
									if (UTexture2D* Texture = Cast<UTexture2D>(Asset.GetAsset()))
									{
										Textures.Add(Texture);
									}
								}
								if (Textures.Num() >= 2)
								{
									CombineTexturesAndOpen(Textures);
								}
							}),
							FCanExecuteAction::CreateLambda([]() -> bool
							{
								FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
								TArray<FAssetData> SelectedAssets;
								ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);
								int32 TexCount = 0;
								for (const FAssetData& Asset : SelectedAssets)
								{
									if (Asset.GetClass() == UTexture2D::StaticClass()) TexCount++;
								}
								return TexCount >= 2;
							})
						)
					);

					SubSection.AddMenuEntry(
						"DebakeSharedBase",
						LOCTEXT("DebakeSharedBase", "De-bake Shared Base..."),
						LOCTEXT("DebakeSharedBaseTooltip",
							"Recover a clean shared base + per-variant overlay textures from N variant sheets of one animation with per-variant art baked in (consensus vote + optional VFX masks)."),
						FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Blend"),
						FUIAction(
							FExecuteAction::CreateLambda([]()
							{
								FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
								TArray<FAssetData> SelectedAssets;
								ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

								TArray<TSoftObjectPtr<UTexture2D>> Textures;
								for (const FAssetData& Asset : SelectedAssets)
								{
									// Exact-class match (like CanExecute below): a UTexture2D-derived
									// asset (atlas, lightmap) in the selection must not become a variant.
									if (Asset.GetClass() != UTexture2D::StaticClass())
									{
										continue;
									}
									if (UTexture2D* Texture = Cast<UTexture2D>(Asset.GetAsset()))
									{
										Textures.Add(TSoftObjectPtr<UTexture2D>(Texture));
									}
								}
								if (Textures.Num() >= 2)
								{
									SBulkSpriteExtractorWindow::OpenBulkExtractorForDebake(Textures);
								}
							}),
							FCanExecuteAction::CreateLambda([]() -> bool
							{
								FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
								TArray<FAssetData> SelectedAssets;
								ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);
								int32 TexCount = 0;
								for (const FAssetData& Asset : SelectedAssets)
								{
									if (Asset.GetClass() == UTexture2D::StaticClass()) TexCount++;
								}
								return TexCount >= 2;
							})
						)
					);
				}),
				false,
				FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Plus")
			);
		}

		// Extend PaperSprite context menu — add "Repackage as New Texture"
		UToolMenu* SpriteMenu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.PaperSprite");
		if (SpriteMenu)
		{
			FToolMenuSection& SpriteSection = SpriteMenu->FindOrAddSection("GetAssetActions");
			SpriteSection.AddMenuEntry(
				"RepackAsNewTexture",
				LOCTEXT("RepackAsNewTexture", "Repackage as New Texture"),
				LOCTEXT("RepackAsNewTextureTooltip", "Combine the selected sprites into a new packed texture strip"),
				FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Merge"),
				FUIAction(
					FExecuteAction::CreateLambda([]()
					{
						FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
						TArray<FAssetData> SelectedAssets;
						CBModule.Get().GetSelectedAssets(SelectedAssets);

						TArray<UPaperSprite*> Sprites;
						for (const FAssetData& Asset : SelectedAssets)
						{
							if (UPaperSprite* Sprite = Cast<UPaperSprite>(Asset.GetAsset()))
							{
								Sprites.Add(Sprite);
							}
						}
						if (Sprites.Num() > 0)
						{
							RepackSpritesAsNewTexture(Sprites);
						}
					}),
					FCanExecuteAction::CreateLambda([]() -> bool
					{
						FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
						TArray<FAssetData> SelectedAssets;
						CBModule.Get().GetSelectedAssets(SelectedAssets);
						for (const FAssetData& Asset : SelectedAssets)
						{
							if (Asset.GetClass()->IsChildOf(UPaperSprite::StaticClass())) return true;
						}
						return false;
					})
				)
			);
		}

		// Add to Tools menu
		UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		if (ToolsMenu)
		{
			FToolMenuSection& Section = ToolsMenu->FindOrAddSection("Paper2DPlus");
			Section.AddMenuEntry(
				"OpenSpriteExtractor",
				LOCTEXT("OpenSpriteExtractor", "Sprite Extractor"),
				LOCTEXT("OpenSpriteExtractorTooltip", "Open the Paper2D+ Sprite Extractor"),
				FSlateIcon(FAppStyle::Get().GetStyleSetName(), "ClassIcon.PaperSprite"),
				FUIAction(FExecuteAction::CreateStatic(&FSpriteExtractorActions::OpenSpriteExtractor))
			);
		}
	}));
}

void FSpriteExtractorActions::UnregisterMenus()
{
	// Menus are automatically cleaned up when the module shuts down
}

void FSpriteExtractorActions::OpenSpriteExtractor()
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("SpriteExtractorTitle", "Paper2D+ Sprite Extractor"))
		.ClientSize(FVector2D(1400, 800))
		.SupportsMinimize(true)
		.SupportsMaximize(true);

	Window->SetContent(
		SNew(SSpriteExtractorWindow)
	);

	FSlateApplication::Get().AddWindow(Window);
}

void FSpriteExtractorActions::OpenSpriteExtractorForReExtract(UTexture2D* Texture, UPaperFlipbook* Flipbook, int32 FlipbookIndex, UPaper2DPlusCharacterProfileAsset* ProfileAsset)
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::Format(LOCTEXT("ReExtractTitle", "Re-extract Sprites: {0}"), FText::FromString(Flipbook ? Flipbook->GetName() : TEXT("Unknown"))))
		.ClientSize(FVector2D(1400, 800))
		.SupportsMinimize(true)
		.SupportsMaximize(true);

	TSharedRef<SSpriteExtractorWindow> ExtractorWidget = SNew(SSpriteExtractorWindow);
	ExtractorWidget->SetReExtractMode(Flipbook, FlipbookIndex, ProfileAsset);

	if (Texture)
	{
		ExtractorWidget->SetInitialTexture(Texture);
	}

	Window->SetContent(ExtractorWidget);
	FSlateApplication::Get().AddWindow(Window);
}

void FSpriteExtractorActions::OpenSpriteExtractorForTexture(UTexture2D* Texture)
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("SpriteExtractorTitle", "Paper2D+ Sprite Extractor"))
		.ClientSize(FVector2D(1400, 800))
		.SupportsMinimize(true)
		.SupportsMaximize(true);

	TSharedRef<SSpriteExtractorWindow> ExtractorWidget = SNew(SSpriteExtractorWindow);

	// Set the initial texture
	if (Texture)
	{
		ExtractorWidget->SetInitialTexture(Texture);
	}

	Window->SetContent(ExtractorWidget);

	FSlateApplication::Get().AddWindow(Window);
}

void FSpriteExtractorActions::CombineTexturesAndOpen(const TArray<UTexture2D*>& Textures)
{
	if (Textures.Num() < 2) return;

	// Sort textures by name for consistent animation frame ordering
	TArray<UTexture2D*> Sorted = Textures;
	Sorted.Sort([](const UTexture2D& A, const UTexture2D& B)
	{
		return A.GetName() < B.GetName();
	});

	// Load pixel data for all textures and compute combined dimensions
	struct FTexData { TArray<FColor> Pixels; int32 W = 0; int32 H = 0; };
	TArray<FTexData> TexDatas;
	TexDatas.SetNum(Sorted.Num());

	int32 MaxH = 0;
	int32 TotalW = 0;
	bool bAllLoaded = true;

	for (int32 i = 0; i < Sorted.Num(); i++)
	{
		// Ensure CPU access
		FSpriteExtractionUtils::ApplyPaper2DSettings(Sorted[i]);

		if (!FSpriteExtractionUtils::LoadTextureData(Sorted[i], TexDatas[i].Pixels, TexDatas[i].W, TexDatas[i].H))
		{
			// Try forcing CPU access if initial load fails
			FSpriteExtractionUtils::ForceCPUAccess(Sorted[i]);
			if (!FSpriteExtractionUtils::LoadTextureData(Sorted[i], TexDatas[i].Pixels, TexDatas[i].W, TexDatas[i].H))
			{
				bAllLoaded = false;
				break;
			}
		}
		MaxH = FMath::Max(MaxH, TexDatas[i].H);
		TotalW += TexDatas[i].W;
	}

	if (!bAllLoaded || TotalW <= 0 || MaxH <= 0)
	{
		FNotificationInfo Info(LOCTEXT("CombineFailed", "Failed to load one or more textures for combining."));
		Info.ExpireDuration = 5.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	const int64 CombinedBytes = (int64)TotalW * MaxH * sizeof(FColor);
	if (CombinedBytes > 512 * 1024 * 1024) // 512 MB guard
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("CombineTooLarge", "Combined texture would be {0}x{1} ({2} MB) — too large. Reduce the number of textures."),
			FText::AsNumber(TotalW), FText::AsNumber(MaxH), FText::AsNumber(CombinedBytes / (1024 * 1024))));
		Info.ExpireDuration = 8.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	// Create combined pixel buffer (transparent black)
	TArray<FColor> CombinedPixels;
	CombinedPixels.SetNumZeroed(TotalW * MaxH);

	// Copy each texture into the strip, bottom-aligned so every cell's ground line / feet sit on the
	// strip's bottom edge (sprites are authored ground-down). TASK-24: this previously hard-coded
	// YOffset=0 (top-align), which left shorter inputs' content floating high and misaligned the
	// per-cell baselines for mixed-height inputs — contradicting this code's own stated intent.
	int32 XOffset = 0;
	for (int32 i = 0; i < Sorted.Num(); i++)
	{
		const FTexData& TD = TexDatas[i];
		const int32 YOffset = MaxH - TD.H; // bottom-align: shorter textures pad at TOP, feet on the bottom edge

		for (int32 Y = 0; Y < TD.H; Y++)
		{
			for (int32 X = 0; X < TD.W; X++)
			{
				CombinedPixels[(YOffset + Y) * TotalW + (XOffset + X)] = TD.Pixels[Y * TD.W + X];
			}
		}
		XOffset += TD.W;
	}

	// Determine output path from first texture's location
	FString FirstTexPath = Sorted[0]->GetPathName();
	FString OutputPath = FPackageName::GetLongPackagePath(FirstTexPath);

	// Derive a name from the common prefix of selected textures
	FString CommonPrefix = Sorted[0]->GetName();
	for (int32 i = 1; i < Sorted.Num(); i++)
	{
		const FString& Name = Sorted[i]->GetName();
		int32 Match = 0;
		while (Match < CommonPrefix.Len() && Match < Name.Len() && CommonPrefix[Match] == Name[Match])
		{
			Match++;
		}
		CommonPrefix = CommonPrefix.Left(Match);
	}
	// Trim trailing separators
	while (CommonPrefix.Len() > 0 && (CommonPrefix[CommonPrefix.Len() - 1] == '_' || CommonPrefix[CommonPrefix.Len() - 1] == '-'))
	{
		CommonPrefix = CommonPrefix.Left(CommonPrefix.Len() - 1);
	}
	if (CommonPrefix.IsEmpty()) { CommonPrefix = TEXT("Combined"); }
	FString CombinedName = CommonPrefix + TEXT("_Sheet");

	// Create the texture asset
	FString PackageName = OutputPath / CombinedName;
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package) return;

	// Find-reuse-or-create guarding against an explicit-name NewObject collision when Combine is run
	// twice on the same selection (U7) — see FSpriteExtractionUtils::GetOrCreateTextureForName.
	UTexture2D* CombinedTexture = FSpriteExtractionUtils::GetOrCreateTextureForName(Package, CombinedName);
	if (!CombinedTexture) return;

	CombinedTexture->Source.Init(TotalW, MaxH, 1, 1, TSF_BGRA8);
	{
		uint8* DestData = CombinedTexture->Source.LockMip(0);
		FMemory::Memcpy(DestData, CombinedPixels.GetData(), TotalW * MaxH * sizeof(FColor));
		CombinedTexture->Source.UnlockMip(0);
	}

	// Apply Paper2D settings
	CombinedTexture->CompressionSettings = TC_EditorIcon;
	CombinedTexture->Filter = TF_Nearest;
	CombinedTexture->MipGenSettings = TMGS_NoMipmaps;
	CombinedTexture->LODGroup = TEXTUREGROUP_Pixels2D;
	CombinedTexture->NeverStream = true;
	CombinedTexture->SRGB = true;
	CombinedTexture->UpdateResource();
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(CombinedTexture);

	// Open the extractor with the combined sheet
	OpenSpriteExtractorForTexture(CombinedTexture);

	FNotificationInfo Info(FText::Format(
		LOCTEXT("CombineSuccess", "Combined {0} textures into {1} ({2}x{3})"),
		FText::AsNumber(Sorted.Num()), FText::FromString(CombinedName),
		FText::AsNumber(TotalW), FText::AsNumber(MaxH)));
	Info.ExpireDuration = 6.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

void FSpriteExtractorActions::RepackSpritesAsNewTexture(const TArray<UPaperSprite*>& Sprites)
{
	if (Sprites.Num() == 0) return;

	// Sort by name for consistent ordering
	TArray<UPaperSprite*> Sorted = Sprites;
	Sorted.Sort([](const UPaperSprite& A, const UPaperSprite& B) { return A.GetName() < B.GetName(); });

	// Collect source regions. All sprites must share one source texture — repack
	// samples pixel data from a single texture, so mixed-source selections would
	// silently corrupt the output (later sprites' UVs sampled from the wrong texture).
	// See PR #98 review finding #3.
	TArray<FIntRect> Regions;
	UTexture2D* SharedSourceTexture = nullptr;
	for (UPaperSprite* S : Sorted)
	{
		if (!S) continue;
		UTexture2D* Tex = Cast<UTexture2D>(S->GetSourceTexture());
		if (!SharedSourceTexture)
		{
			SharedSourceTexture = Tex;
		}
		else if (Tex != SharedSourceTexture)
		{
			FNotificationInfo Info(FText::Format(
				NSLOCTEXT("Paper2DPlus", "RepackMixedSources",
					"Cannot repack sprites from multiple source textures. Selection mixes {0} with {1}."),
				FText::FromString(SharedSourceTexture ? SharedSourceTexture->GetName() : TEXT("<null>")),
				FText::FromString(Tex ? Tex->GetName() : TEXT("<null>"))));
			Info.ExpireDuration = 5.0f;
			Info.bUseThrobber = false;
			TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
			if (Item.IsValid()) { Item->SetCompletionState(SNotificationItem::CS_Fail); }
			return;
		}

		FVector2D UV = S->GetSourceUV();
		FVector2D Sz = S->GetSourceSize();
		Regions.Add(FIntRect(
			FMath::RoundToInt(UV.X), FMath::RoundToInt(UV.Y),
			FMath::RoundToInt(UV.X + Sz.X), FMath::RoundToInt(UV.Y + Sz.Y)));
	}

	if (Regions.Num() == 0 || !SharedSourceTexture) return;

	// Determine output path — same folder as the first sprite
	FString OutputPath = FPackageName::GetLongPackagePath(Sorted[0]->GetOutermost()->GetName());

	// Build a name from common prefix
	FString CommonPrefix = Sorted[0]->GetName();
	for (int32 i = 1; i < Sorted.Num(); i++)
	{
		const FString& Name = Sorted[i]->GetName();
		int32 Len = FMath::Min(CommonPrefix.Len(), Name.Len());
		int32 Match = 0;
		while (Match < Len && CommonPrefix[Match] == Name[Match]) Match++;
		CommonPrefix = CommonPrefix.Left(Match);
	}
	while (CommonPrefix.Len() > 0 && (CommonPrefix.EndsWith(TEXT("_")) || CommonPrefix.EndsWith(TEXT("-"))))
	{
		CommonPrefix = CommonPrefix.Left(CommonPrefix.Len() - 1);
	}
	if (CommonPrefix.IsEmpty()) CommonPrefix = TEXT("Sprites");
	FString TextureName = CommonPrefix + TEXT("_Packed");

	// Use CreatePackedTexture to build the new texture
	UTexture2D* NewTexture = FSpriteExtractionUtils::CreatePackedTexture(
		SharedSourceTexture, Regions, TextureName, OutputPath);

	if (!NewTexture)
	{
		FNotificationInfo Info(LOCTEXT("RepackFailed", "Failed to create packed texture."));
		Info.ExpireDuration = 5.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	// Apply Paper2D settings
	FSpriteExtractionUtils::ApplyPaper2DSettings(NewTexture);

	// Compute cell dimensions for sprite UV assignment
	int32 CellW = 0, CellH = 0;
	for (const FIntRect& R : Regions)
	{
		CellW = FMath::Max(CellW, R.Width());
		CellH = FMath::Max(CellH, R.Height());
	}

	// Update each sprite to point to the new packed texture
	FScopedTransaction Transaction(LOCTEXT("RepackSpritesTxn", "Repackage Sprites as New Texture"));
	for (int32 i = 0; i < Sorted.Num(); i++)
	{
		UPaperSprite* S = Sorted[i];
		S->Modify();

		const int32 PadX = (CellW - Regions[i].Width()) / 2;
		const int32 PadY = (CellH - Regions[i].Height()) / 2;
		const FIntPoint NewOffset(i * CellW + PadX, PadY);
		const FIntPoint SpriteDim(Regions[i].Width(), Regions[i].Height());

		FSpriteAssetInitParameters InitParams;
		InitParams.Texture = NewTexture;
		InitParams.Offset = NewOffset;
		InitParams.Dimension = SpriteDim;
		S->InitializeSprite(InitParams);
		S->SetPivotMode(ESpritePivotMode::Center_Center, FVector2D::ZeroVector);
		S->MarkPackageDirty();
	}

	FNotificationInfo Info(FText::Format(
		LOCTEXT("RepackSuccess", "Repacked {0} sprites into {1} ({2}x{3})"),
		FText::AsNumber(Sorted.Num()), FText::FromString(TextureName),
		FText::AsNumber(CellW * Sorted.Num()), FText::AsNumber(CellH)));
	Info.ExpireDuration = 6.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

#undef LOCTEXT_NAMESPACE
