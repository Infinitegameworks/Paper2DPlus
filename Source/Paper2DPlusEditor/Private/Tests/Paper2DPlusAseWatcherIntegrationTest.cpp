// Copyright 2026 Infinite Gameworks. All Rights Reserved.

/**
 * Behavioral integration coverage for mapped Aseprite source watching.
 *
 * Each fixture begins as a saved, unloaded Character Layer asset whose source path and last-imported
 * hash exist only in Asset Registry tags. The tests then cross the production watcher, debounce, full
 * layered-import, generated-package, texture Source, and package-persistence boundaries. The expected
 * MD5 values are fixed oracles computed outside FAsepriteImporter.
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "PackageTools.h"
#include "TextureCompiler.h"

#include "AsepriteImporter.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusSettings.h"
#include "TextureWatcherService.h"
#include "UObject/UnrealType.h"
#include "Runtime/Launch/Resources/Version.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorValidatorSubsystem.h" // UDataValidationSettings lives here on UE 5.0
#endif

#if WITH_DEV_AUTOMATION_TESTS

namespace Paper2DPlusAseWatcherIntegrationTest
{
	static const FColor Red(255, 0, 0, 255);
	static const FColor Green(0, 255, 0, 255);
	static const FColor Blue(0, 0, 255, 255);
	static const FString RedHash(TEXT("adf1fb669fa106312ff6d7933d719a3b"));
	static const FString GreenHash(TEXT("0db2eeeb2bb29db033b290596b7c5960"));
	static const FString BlueHash(TEXT("15014cea90312afd4b247bdfbdc80669"));

	/** Little-endian writer for the smallest useful one-layer .ase fixture. */
	struct FAseImportBufWriter
	{
		TArray<uint8> Bytes;

		void U8(const uint8 Value) { Bytes.Add(Value); }
		void U16(const uint16 Value)
		{
			Bytes.Add(Value & 0xff);
			Bytes.Add((Value >> 8) & 0xff);
		}
		void S16(const int16 Value) { U16(static_cast<uint16>(Value)); }
		void U32(const uint32 Value)
		{
			Bytes.Add(Value & 0xff);
			Bytes.Add((Value >> 8) & 0xff);
			Bytes.Add((Value >> 16) & 0xff);
			Bytes.Add((Value >> 24) & 0xff);
		}
		void Zeros(const int32 Count) { Bytes.AddZeroed(Count); }
		void String(const FString& Value)
		{
			const FTCHARToUTF8 Utf8(*Value);
			U16(static_cast<uint16>(Utf8.Length()));
			Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		}
		void Append(const TArray<uint8>& Other) { Bytes.Append(Other); }
	};

	static TArray<uint8> AseImport_Chunk(const uint16 ChunkType, const TArray<uint8>& Payload)
	{
		FAseImportBufWriter Writer;
		Writer.U32(static_cast<uint32>(Payload.Num() + 6));
		Writer.U16(ChunkType);
		Writer.Append(Payload);
		return MoveTemp(Writer.Bytes);
	}

	static TArray<uint8> AseImport_LayerChunk(const FString& Name)
	{
		FAseImportBufWriter Payload;
		Payload.U16(1); // visible
		Payload.U16(0); // normal layer
		Payload.U16(0); // child level
		Payload.U16(0);
		Payload.U16(0);
		Payload.U16(0); // normal blend
		Payload.U8(255);
		Payload.Zeros(3);
		Payload.String(Name);
		return AseImport_Chunk(0x2004, Payload.Bytes);
	}

	static TArray<uint8> AseImport_RawCelChunk(
		const uint16 LayerIndex,
		const int16 X,
		const int16 Y,
		const uint16 Width,
		const uint16 Height,
		const FColor& Fill)
	{
		FAseImportBufWriter Payload;
		Payload.U16(LayerIndex);
		Payload.S16(X);
		Payload.S16(Y);
		Payload.U8(255);
		Payload.U16(0); // raw cel
		Payload.Zeros(7);
		Payload.U16(Width);
		Payload.U16(Height);
		for (int32 PixelIndex = 0; PixelIndex < Width * Height; ++PixelIndex)
		{
			Payload.U8(Fill.R);
			Payload.U8(Fill.G);
			Payload.U8(Fill.B);
			Payload.U8(Fill.A);
		}
		return AseImport_Chunk(0x2005, Payload.Bytes);
	}

	static TArray<uint8> AseImport_Frame(const TArray<TArray<uint8>>& Chunks)
	{
		int32 ChunkBytes = 0;
		for (const TArray<uint8>& Chunk : Chunks)
		{
			ChunkBytes += Chunk.Num();
		}

		FAseImportBufWriter Writer;
		Writer.U32(static_cast<uint32>(16 + ChunkBytes));
		Writer.U16(0xf1fa);
		Writer.U16(static_cast<uint16>(Chunks.Num()));
		Writer.U16(100);
		Writer.Zeros(2);
		Writer.U32(static_cast<uint32>(Chunks.Num()));
		for (const TArray<uint8>& Chunk : Chunks)
		{
			Writer.Append(Chunk);
		}
		return MoveTemp(Writer.Bytes);
	}

	static TArray<uint8> AseImport_File(
		const uint16 CanvasWidth,
		const uint16 CanvasHeight,
		const TArray<TArray<uint8>>& Frames)
	{
		int32 FrameBytes = 0;
		for (const TArray<uint8>& Frame : Frames)
		{
			FrameBytes += Frame.Num();
		}

		FAseImportBufWriter Writer;
		Writer.U32(static_cast<uint32>(128 + FrameBytes));
		Writer.U16(0xa5e0);
		Writer.U16(static_cast<uint16>(Frames.Num()));
		Writer.U16(CanvasWidth);
		Writer.U16(CanvasHeight);
		Writer.U16(32); // RGBA
		Writer.U32(0);
		Writer.U16(100);
		Writer.Zeros(128 - Writer.Bytes.Num());
		for (const TArray<uint8>& Frame : Frames)
		{
			Writer.Append(Frame);
		}
		return MoveTemp(Writer.Bytes);
	}

	static TArray<uint8> MakeOnePixelAse(const FColor& Color)
	{
		return AseImport_File(1, 1, {
			AseImport_Frame({
				AseImport_LayerChunk(TEXT("Body")),
				AseImport_RawCelChunk(0, 0, 0, 1, 1, Color)
			})
		});
	}

	static void AppendFailure(FString& OutError, const FString& Failure)
	{
		if (!OutError.IsEmpty())
		{
			OutError += TEXT("; ");
		}
		OutError += Failure;
	}

	static bool ReadFirstSourcePixel(UTexture2D* Texture, FColor& OutPixel, FString& OutError)
	{
		if (!Texture)
		{
			OutError = TEXT("Body has no source texture");
			return false;
		}

		TArray<UTexture*> Textures { Texture };
		FTextureCompilingManager::Get().FinishCompilation(Textures);
		if (Texture->Source.GetSizeX() != 1
			|| Texture->Source.GetSizeY() != 1
			|| Texture->Source.GetFormat() != TSF_BGRA8)
		{
			OutError = FString::Printf(
				TEXT("Body source is %dx%d format %d instead of 1x1 BGRA8"),
				Texture->Source.GetSizeX(),
				Texture->Source.GetSizeY(),
				static_cast<int32>(Texture->Source.GetFormat()));
			return false;
		}

		uint8* Pixels = Texture->Source.LockMip(0);
		if (!Pixels)
		{
			OutError = TEXT("Body source mip 0 could not be locked");
			return false;
		}
		FMemory::Memcpy(&OutPixel, Pixels, sizeof(FColor));
		Texture->Source.UnlockMip(0);
		return true;
	}

	struct FFixturePackage
	{
		UPackage* Package = nullptr;
		UObject* RootAsset = nullptr;
	};

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	/**
	 * UE 5.0's validate-on-save has no bLoadAssetsForValidation opt-out (added in 5.1), so
	 * UEditorValidatorSubsystem::IsAssetValid calls FAssetData::GetAsset() and LOADS every package saved
	 * that tick. That re-loads the deliberately cold registry seed these tests assert is nonresident, and
	 * reports the seed's intentionally-empty pre-import appearance data as an automation Error. 5.1+ skips
	 * assets that are not already resident; this restores that behaviour on 5.0 for the fixture's lifetime.
	 * In-memory CDO only (nothing is written to config), restored in the destructor.
	 */
	class FScopedDisableValidateOnSave final
	{
	public:
		FScopedDisableValidateOnSave()
		{
			if (UDataValidationSettings* Settings = GetMutableDefault<UDataValidationSettings>())
			{
				bPreviousValue = Settings->bValidateOnSave != 0;
				Settings->bValidateOnSave = false;
			}
		}
		~FScopedDisableValidateOnSave()
		{
			if (UDataValidationSettings* Settings = GetMutableDefault<UDataValidationSettings>())
			{
				Settings->bValidateOnSave = bPreviousValue;
			}
		}
	private:
		bool bPreviousValue = true;
	};
#endif // UE 5.0 only

	class FScopedMappedAseFixture final
	{
	public:
		explicit FScopedMappedAseFixture(FAutomationTestBase& InTest)
			: Test(InTest)
		{
			const FGuid Guid = FGuid::NewGuid();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
			const FString Token = Guid.ToString(EGuidFormats::Digits).ToLower();
#else
			const FString Token = Guid.ToString(EGuidFormats::DigitsLower);
#endif
			FixtureLeaf = FString::Printf(TEXT("AseWatcher_%s"), *Token);
			OutputRoot = FString::Printf(TEXT("/Game/__AutomationTemp__/%s"), *FixtureLeaf);
			LayerPackageName = OutputRoot / (AssetPrefix + TEXT("_Layers"));
			LayerAssetName = AssetPrefix + TEXT("_Layers");
			LayerPackageFile = FPackageName::LongPackageNameToFilename(
				LayerPackageName, FPackageName::GetAssetPackageExtension());
			LayerAssetPath = FSoftObjectPath(FString::Printf(
				TEXT("%s.%s"), *LayerPackageName, *LayerAssetName));

			ContentDirectory = FPaths::ConvertRelativePathToFull(
				FPaths::ProjectContentDir() / TEXT("__AutomationTemp__") / FixtureLeaf);
			SourceDirectory = FPaths::ConvertRelativePathToFull(
				FPaths::ProjectContentDir() / TEXT("__AutomationTemp__")
					/ (FixtureLeaf + TEXT("_Source")));
			SourceFile = SourceDirectory / TEXT("source.ase");
			FPaths::NormalizeFilename(LayerPackageFile);
			FPaths::NormalizeDirectoryName(ContentDirectory);
			FPaths::NormalizeDirectoryName(SourceDirectory);
			FPaths::NormalizeFilename(SourceFile);
		}

		~FScopedMappedAseFixture()
		{
			if (bCleaned)
			{
				return;
			}
			FString CleanupError;
			if (!Cleanup(CleanupError))
			{
				Test.AddError(FString::Printf(
					TEXT("Mapped Aseprite watcher fixture cleanup failed: %s"),
					*CleanupError));
			}
		}

		bool CreateColdRedSeed(
			const bool bPreserveModuleStartedContentWatcher,
			FString& OutError)
		{
			FTextureWatcherService& Watcher = FTextureWatcherService::Get();
			if (bPreserveModuleStartedContentWatcher)
			{
				if (!Watcher.IsInitialized())
				{
					OutError = TEXT("the module-started TextureWatcherService was not active during live fixture setup");
					return false;
				}
			}
			else if (!Watcher.Shutdown())
			{
				OutError = TEXT("TextureWatcherService did not release every callback before fixture creation");
				return false;
			}

			IFileManager& Files = IFileManager::Get();
			if (FindPackage(nullptr, *LayerPackageName)
				|| Files.FileExists(*LayerPackageFile)
				|| Files.DirectoryExists(*ContentDirectory)
				|| Files.DirectoryExists(*SourceDirectory))
			{
				OutError = TEXT("the GUID-scoped fixture unexpectedly existed before setup");
				return false;
			}
			if (!Files.MakeDirectory(*FPaths::GetPath(LayerPackageFile), true)
				|| !Files.MakeDirectory(*SourceDirectory, true))
			{
				OutError = TEXT("could not create the exact fixture directories");
				return false;
			}

			if (!WriteSource(Red, RedHash, OutError))
			{
				return false;
			}

			StoredSourcePath = SourceFile;
			const FString ProjectDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
			if (!FPaths::MakePathRelativeTo(StoredSourcePath, *ProjectDirectory))
			{
				OutError = TEXT("could not independently make the source path project-relative");
				return false;
			}
			FPaths::NormalizeFilename(StoredSourcePath);
			if (!FPaths::IsRelative(StoredSourcePath)
				|| StoredSourcePath.StartsWith(TEXT("..")))
			{
				OutError = FString::Printf(
					TEXT("the stored source path is not safely project-relative: '%s'"),
					*StoredSourcePath);
				return false;
			}

			UPackage* Package = CreatePackage(*LayerPackageName);
			UPaper2DPlusCharacterLayerAsset* LayerAsset = Package
				? NewObject<UPaper2DPlusCharacterLayerAsset>(
					Package,
					FName(*LayerAssetName),
					RF_Public | RF_Standalone | RF_Transactional)
				: nullptr;
			if (!Package || !LayerAsset)
			{
				OutError = TEXT("could not create the cold Character Layer asset");
				return false;
			}

			LayerAsset->DisplayName = AssetPrefix;
			LayerAsset->SourceAseFilePath = StoredSourcePath;
			LayerAsset->ImportedAseContentHash = RedHash;
			LayerAsset->ImportOutputPath = OutputRoot;
			LayerAsset->ImportAssetPrefix = AssetPrefix;
			LayerAsset->bImportOrganizeIntoSubfolders = false;
			LayerAsset->BaseProfile.Reset();
			LayerAsset->Layers.Reset();
			FAssetRegistryModule::AssetCreated(LayerAsset);
			bSeedAssetRegistered = true;
			Package->MarkPackageDirty();

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			if (!UPackage::SavePackage(Package, LayerAsset, *LayerPackageFile, SaveArgs)
				|| !Files.FileExists(*LayerPackageFile))
			{
				OutError = TEXT("the seed Character Layer package did not save to disk");
				return false;
			}
			Package->SetDirtyFlag(false);

			TArray<FFixturePackage> SeedPackage { { Package, LayerAsset } };
			LayerAsset = nullptr;
			Package = nullptr;
			if (!UnloadPackages(SeedPackage, OutError))
			{
				return false;
			}

			IAssetRegistry& AssetRegistry =
				FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			AssetRegistry.ScanFilesSynchronous({ LayerPackageFile }, true);
			if (!VerifyColdRegistrySeed(AssetRegistry, OutError))
			{
				return false;
			}
			if (bPreserveModuleStartedContentWatcher)
			{
				// Discover the newly saved registry row without replacing the module-owned recursive
				// Project Content callback. RefreshAssetMapping deliberately does not register nested
				// Content directories a second time.
				Watcher.RefreshAssetMapping();
				if (!Watcher.IsInitialized() || LayerAssetPath.ResolveObject())
				{
					OutError = TEXT("refreshing the registry map loaded the cold asset or stopped the module watcher");
					return false;
				}
			}
			return true;
		}

		bool WriteSource(const FColor& Color, const FString& ExpectedHash, FString& OutError) const
		{
			const TArray<uint8> Bytes = MakeOnePixelAse(Color);
			if (!FFileHelper::SaveArrayToFile(Bytes, *SourceFile))
			{
				OutError = FString::Printf(TEXT("could not write '%s'"), *SourceFile);
				return false;
			}

			const FString ActualHash = FAsepriteImporter::HashAseFileContent(SourceFile);
			if (!ActualHash.Equals(ExpectedHash, ESearchCase::IgnoreCase))
			{
				OutError = FString::Printf(
					TEXT("fixture MD5 is '%s', expected independent oracle '%s'"),
					*ActualHash, *ExpectedHash);
				return false;
			}
			return true;
		}

		bool ObserveLoadedResult(
			const FColor& ExpectedColor,
			const FString& ExpectedHash,
			const bool bRequireDirty,
			FString& OutDiagnostic) const
		{
			UPaper2DPlusCharacterLayerAsset* LayerAsset =
				Cast<UPaper2DPlusCharacterLayerAsset>(LayerAssetPath.ResolveObject());
			return VerifyLayerAsset(
				LayerAsset, ExpectedColor, ExpectedHash, bRequireDirty, nullptr, OutDiagnostic);
		}

		bool SaveUnloadReloadAndVerify(
			const FColor& ExpectedColor,
			const FString& ExpectedHash,
			FString& OutError)
		{
			UPaper2DPlusCharacterLayerAsset* LayerAsset =
				Cast<UPaper2DPlusCharacterLayerAsset>(LayerAssetPath.ResolveObject());
			if (!LayerAsset || LayerAsset->Layers.Num() != 1)
			{
				OutError = TEXT("the updated Character Layer vanished before persistence verification");
				return false;
			}
			const FSoftObjectPath TexturePath = LayerAsset->Layers[0].SourceTexture.ToSoftObjectPath();
			if (TexturePath.IsNull())
			{
				OutError = TEXT("the updated Body texture has no persistent object path");
				return false;
			}

			FTextureWatcherService& Watcher = FTextureWatcherService::Get();
			if (!Watcher.Shutdown())
			{
				OutError = TEXT("TextureWatcherService failed to shut down before persistence verification");
				return false;
			}

			TArray<FFixturePackage> Packages;
			TArray<UObject*> RegisteredAssets;
			if (!GatherFixturePackages(Packages, RegisteredAssets, OutError)
				|| !SaveFixturePackages(Packages, OutError))
			{
				return false;
			}

			TArray<FString> SavedFiles;
			SavedFiles.Reserve(Packages.Num());
			for (const FFixturePackage& Record : Packages)
			{
				SavedFiles.Add(FPackageName::LongPackageNameToFilename(
					Record.Package->GetName(), FPackageName::GetAssetPackageExtension()));
			}
			LayerAsset = nullptr;
			RegisteredAssets.Reset();
			if (!UnloadPackages(Packages, OutError))
			{
				return false;
			}

			IAssetRegistry& AssetRegistry =
				FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			AssetRegistry.ScanFilesSynchronous(SavedFiles, true);
			UPaper2DPlusCharacterLayerAsset* Reloaded =
				Cast<UPaper2DPlusCharacterLayerAsset>(LayerAssetPath.TryLoad());
			return VerifyLayerAsset(
				Reloaded, ExpectedColor, ExpectedHash, false, &TexturePath, OutError);
		}

		bool Cleanup(FString& OutError)
		{
			if (bCleaned)
			{
				return true;
			}

			FTextureWatcherService& Watcher = FTextureWatcherService::Get();
			const bool bWatcherStopped = Watcher.Shutdown();
			if (!bWatcherStopped)
			{
				AppendFailure(OutError, TEXT("TextureWatcherService did not release every callback during cleanup"));
			}

			TArray<FFixturePackage> Packages;
			TArray<UObject*> RegisteredAssets;
			FString GatherError;
			const bool bGathered = GatherFixturePackages(Packages, RegisteredAssets, GatherError);
			if (!bGathered)
			{
				AppendFailure(OutError, GatherError);
			}

			FinishTextureCompilation(Packages);
			for (UObject* Asset : RegisteredAssets)
			{
				if (IsValid(Asset) && Asset->IsAsset())
				{
					FAssetRegistryModule::AssetDeleted(Asset);
				}
			}
			bSeedAssetRegistered = false;

			FString UnloadError;
			const bool bUnloaded = UnloadPackages(Packages, UnloadError);
			if (!bUnloaded)
			{
				AppendFailure(OutError, UnloadError);
			}

			IFileManager& Files = IFileManager::Get();
			const FString SafeContentRoot = FPaths::ConvertRelativePathToFull(
				FPaths::ProjectContentDir() / TEXT("__AutomationTemp__"));
			const bool bSafelyBounded =
				OutputRoot.StartsWith(TEXT("/Game/__AutomationTemp__/AseWatcher_"))
				&& LayerPackageName.StartsWith(OutputRoot + TEXT("/"))
				&& FPaths::GetCleanFilename(ContentDirectory) == FixtureLeaf
				&& FPaths::GetCleanFilename(SourceDirectory) == FixtureLeaf + TEXT("_Source")
				&& FPaths::IsUnderDirectory(ContentDirectory, SafeContentRoot)
				&& FPaths::IsUnderDirectory(SourceDirectory, SafeContentRoot);

			bool bContentDeleted = false;
			bool bSourceDeleted = false;
			if (!bSafelyBounded)
			{
				AppendFailure(OutError, FString::Printf(
					TEXT("refused unsafe cleanup of '%s' and '%s'"),
					*ContentDirectory, *SourceDirectory));
			}
			else
			{
				bContentDeleted = !Files.DirectoryExists(*ContentDirectory)
					|| (Files.DeleteDirectory(*ContentDirectory, false, true)
						&& !Files.DirectoryExists(*ContentDirectory));
				bSourceDeleted = !Files.DirectoryExists(*SourceDirectory)
					|| (Files.DeleteDirectory(*SourceDirectory, false, true)
						&& !Files.DirectoryExists(*SourceDirectory));
				if (!bContentDeleted)
				{
					AppendFailure(OutError, TEXT("the exact generated Content directory remains"));
				}
				if (!bSourceDeleted)
				{
					AppendFailure(OutError, TEXT("the exact Intermediate source directory remains"));
				}
			}

			IAssetRegistry& AssetRegistry =
				FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			TArray<FAssetData> RemainingAssets;
			AssetRegistry.GetAssetsByPath(FName(*OutputRoot), RemainingAssets, true, false);
			const bool bRegistryEmpty = RemainingAssets.IsEmpty();
			if (!bRegistryEmpty)
			{
				AppendFailure(OutError, FString::Printf(
					TEXT("%d fixture assets remain in the Asset Registry"), RemainingAssets.Num()));
			}

			const bool bNoLoadedPackages = !HasLoadedFixturePackage();
			if (!bNoLoadedPackages)
			{
				AppendFailure(OutError, TEXT("one or more generated fixture packages remain loaded"));
			}

			Watcher.Initialize();
			const bool bWatcherRestored = Watcher.IsInitialized();
			if (!bWatcherRestored)
			{
				AppendFailure(OutError, TEXT("TextureWatcherService did not restart after cleanup"));
			}

			bCleaned = bWatcherStopped
				&& bGathered
				&& bUnloaded
				&& bSafelyBounded
				&& bContentDeleted
				&& bSourceDeleted
				&& bRegistryEmpty
				&& bNoLoadedPackages
				&& bWatcherRestored;
			return bCleaned;
		}

		const FString& GetSourceFile() const { return SourceFile; }
		const FSoftObjectPath& GetLayerAssetPath() const { return LayerAssetPath; }

	private:
		bool VerifyColdRegistrySeed(IAssetRegistry& AssetRegistry, FString& OutError) const
		{
			if (FindPackage(nullptr, *LayerPackageName) || LayerAssetPath.ResolveObject())
			{
				OutError = TEXT("the seed package remained resident after the required unload and GC");
				return false;
			}

			TArray<FAssetData> AssetData;
			AssetRegistry.GetAssetsByPath(FName(*OutputRoot), AssetData, true, false);
			const FAssetData* SeedData = AssetData.FindByPredicate([this](const FAssetData& Candidate)
			{
				return Candidate.PackageName == FName(*LayerPackageName)
					&& Candidate.AssetName == FName(*LayerAssetName);
			});
			if (!SeedData)
			{
				OutError = TEXT("the saved Character Layer asset is absent from the Asset Registry");
				return false;
			}
			if (SeedData->IsAssetLoaded())
			{
				OutError = TEXT("the Asset Registry seed is not cold; the asset is still loaded");
				return false;
			}

			FString RegistrySource;
			FString RegistryHash;
			if (!SeedData->GetTagValue(TEXT("Paper2DPlus.SourceAseFile"), RegistrySource)
				|| !SeedData->GetTagValue(TEXT("Paper2DPlus.SourceAseHash"), RegistryHash)
				|| RegistrySource != StoredSourcePath
				|| !RegistryHash.Equals(RedHash, ESearchCase::IgnoreCase))
			{
				OutError = FString::Printf(
					TEXT("cold registry tags are source='%s' hash='%s', expected source='%s' hash='%s'"),
					*RegistrySource, *RegistryHash, *StoredSourcePath, *RedHash);
				return false;
			}
			return true;
		}

		bool VerifyLayerAsset(
			UPaper2DPlusCharacterLayerAsset* LayerAsset,
			const FColor& ExpectedColor,
			const FString& ExpectedHash,
			const bool bRequireDirty,
			const FSoftObjectPath* ExpectedTexturePath,
			FString& OutDiagnostic) const
		{
			if (!LayerAsset)
			{
				OutDiagnostic = TEXT("the mapped Character Layer asset could not be loaded");
				return false;
			}
			if (!LayerAsset->ImportedAseContentHash.Equals(ExpectedHash, ESearchCase::IgnoreCase))
			{
				OutDiagnostic = FString::Printf(
					TEXT("the imported hash is '%s', expected '%s'"),
					*LayerAsset->ImportedAseContentHash, *ExpectedHash);
				return false;
			}
			if (LayerAsset->Layers.Num() != 1
				|| LayerAsset->Layers[0].LayerName != TEXT("Body"))
			{
				OutDiagnostic = FString::Printf(
					TEXT("the full import produced %d layers instead of exactly one Body layer"),
					LayerAsset->Layers.Num());
				return false;
			}

			auto ObjectPathForPackage = [](const FString& PackageName)
			{
				return FSoftObjectPath(FString::Printf(
					TEXT("%s.%s"), *PackageName, *FPackageName::GetShortName(PackageName)));
			};
			const FString ProfilePackageName = OutputRoot / (AssetPrefix + TEXT("_Profile"));
			const FString ProfileSheetPackageName = OutputRoot / (AssetPrefix + TEXT("_Sheet"));
			const FString ProfileSpritePackageName = OutputRoot / (AssetPrefix + TEXT("_00"));
			const FString ProfileFlipbookPackageName = OutputRoot / (AssetPrefix + TEXT("_All"));
			const FString LayerOutputRoot = OutputRoot / AssetPrefix;
			const FString LayerTexturePackageName =
				LayerOutputRoot / (AssetPrefix + TEXT("_Body_Sheet"));
			const FString LayerSpritePackageName =
				LayerOutputRoot / (AssetPrefix + TEXT("_Body_00"));

			const FSoftObjectPath ExpectedProfilePath = ObjectPathForPackage(ProfilePackageName);
			const FSoftObjectPath ExpectedProfileSheetPath = ObjectPathForPackage(ProfileSheetPackageName);
			const FSoftObjectPath ExpectedProfileSpritePath = ObjectPathForPackage(ProfileSpritePackageName);
			const FSoftObjectPath ExpectedProfileFlipbookPath = ObjectPathForPackage(ProfileFlipbookPackageName);
			const FSoftObjectPath ExpectedLayerTexturePath = ObjectPathForPackage(LayerTexturePackageName);
			const FSoftObjectPath ExpectedLayerSpritePath = ObjectPathForPackage(LayerSpritePackageName);

			const FSoftObjectPath ActualTexturePath =
				LayerAsset->Layers[0].SourceTexture.ToSoftObjectPath();
			if (ActualTexturePath != ExpectedLayerTexturePath)
			{
				OutDiagnostic = FString::Printf(
					TEXT("the Body sheet path is '%s', expected full-import path '%s'"),
					*ActualTexturePath.ToString(), *ExpectedLayerTexturePath.ToString());
				return false;
			}
			if (ExpectedTexturePath && ActualTexturePath != *ExpectedTexturePath)
			{
				OutDiagnostic = FString::Printf(
					TEXT("the persisted Body texture path changed from '%s' to '%s'"),
					*ExpectedTexturePath->ToString(), *ActualTexturePath.ToString());
				return false;
			}

			const FCharacterLayer& BodyLayer = LayerAsset->Layers[0];
			const FString ExpectedLayerAnimationName = AssetPrefix + TEXT("_Body");
			if (BodyLayer.AnimationSprites.Num() != 1
				|| BodyLayer.AnimationSprites[0].AnimationName != ExpectedLayerAnimationName
				|| BodyLayer.AnimationSprites[0].Sprites.Num() != 1)
			{
				OutDiagnostic = FString::Printf(
					TEXT("the Body layer has %d animation mappings; expected one '%s' mapping with one frame"),
					BodyLayer.AnimationSprites.Num(), *ExpectedLayerAnimationName);
				return false;
			}
			const FSoftObjectPath ActualLayerSpritePath =
				BodyLayer.AnimationSprites[0].Sprites[0].ToSoftObjectPath();
			if (ActualLayerSpritePath != ExpectedLayerSpritePath)
			{
				OutDiagnostic = FString::Printf(
					TEXT("the layer animation sprite path is '%s', expected '%s'"),
					*ActualLayerSpritePath.ToString(), *ExpectedLayerSpritePath.ToString());
				return false;
			}
			UPaperSprite* LayerSprite = BodyLayer.AnimationSprites[0].Sprites[0].LoadSynchronous();
			UTexture2D* LayerTexture = BodyLayer.SourceTexture.LoadSynchronous();
			if (!LayerSprite || !LayerTexture)
			{
				OutDiagnostic = TEXT("the full-import layer sprite or sheet could not be loaded");
				return false;
			}

			if (LayerAsset->BaseProfile.ToSoftObjectPath() != ExpectedProfilePath)
			{
				OutDiagnostic = FString::Printf(
					TEXT("the full reimport linked BaseProfile '%s', expected '%s'"),
					*LayerAsset->BaseProfile.ToSoftObjectPath().ToString(),
					*ExpectedProfilePath.ToString());
				return false;
			}
			UPaper2DPlusCharacterProfileAsset* Profile = LayerAsset->BaseProfile.LoadSynchronous();
			if (!Profile || Profile->Flipbooks.Num() != 1)
			{
				OutDiagnostic = FString::Printf(
					TEXT("the generated BaseProfile has %d flipbook entries instead of one"),
					Profile ? Profile->Flipbooks.Num() : 0);
				return false;
			}
			const FFlipbookProfileEntry& ProfileEntry = Profile->Flipbooks[0];
			if (ProfileEntry.Identity.FlipbookName != TEXT("All")
				|| ProfileEntry.SourceTexture.ToSoftObjectPath() != ExpectedProfileSheetPath
				|| ProfileEntry.Identity.Flipbook.ToSoftObjectPath() != ExpectedProfileFlipbookPath)
			{
				OutDiagnostic = FString::Printf(
					TEXT("profile output mismatch: animation='%s' sheet='%s' flipbook='%s'"),
					*ProfileEntry.Identity.FlipbookName,
					*ProfileEntry.SourceTexture.ToSoftObjectPath().ToString(),
					*ProfileEntry.Identity.Flipbook.ToSoftObjectPath().ToString());
				return false;
			}
			UPaperFlipbook* ProfileFlipbook = ProfileEntry.Identity.Flipbook.LoadSynchronous();
			UTexture2D* ProfileSheet = ProfileEntry.SourceTexture.LoadSynchronous();
			UPaperSprite* ProfileSprite = ProfileFlipbook && ProfileFlipbook->GetNumKeyFrames() == 1
				? ProfileFlipbook->GetKeyFrameChecked(0).Sprite
				: nullptr;
			if (!ProfileSprite || FSoftObjectPath(ProfileSprite) != ExpectedProfileSpritePath)
			{
				OutDiagnostic = FString::Printf(
					TEXT("the generated profile flipbook does not contain the exact sprite '%s'"),
					*ExpectedProfileSpritePath.ToString());
				return false;
			}
			if (!ProfileSheet)
			{
				OutDiagnostic = TEXT("the generated profile sprite-sheet could not be loaded");
				return false;
			}
			if (bRequireDirty)
			{
				const TArray<UObject*> PersistedOutputs {
					LayerAsset,
					LayerTexture,
					LayerSprite,
					Profile,
					ProfileSheet,
					ProfileFlipbook,
					ProfileSprite
				};
				for (UObject* Output : PersistedOutputs)
				{
					if (!Output || !Output->GetOutermost()->IsDirty())
					{
						OutDiagnostic = FString::Printf(
							TEXT("the successful watcher import did not dirty generated output '%s'"),
							Output ? *Output->GetPathName() : TEXT("<null>"));
						return false;
					}
				}
			}

			FColor ActualColor;
			if (!ReadFirstSourcePixel(LayerTexture, ActualColor, OutDiagnostic))
			{
				return false;
			}
			if (ActualColor != ExpectedColor)
			{
				OutDiagnostic = FString::Printf(
					TEXT("the generated Body pixel is RGBA(%u,%u,%u,%u), expected RGBA(%u,%u,%u,%u)"),
					ActualColor.R, ActualColor.G, ActualColor.B, ActualColor.A,
					ExpectedColor.R, ExpectedColor.G, ExpectedColor.B, ExpectedColor.A);
				return false;
			}

			FColor ActualProfileColor(0, 0, 0, 0);
			FString ProfilePixelError;
			if (!ReadFirstSourcePixel(ProfileSheet, ActualProfileColor, ProfilePixelError)
				|| ActualProfileColor != ExpectedColor)
			{
				OutDiagnostic = FString::Printf(
					TEXT("the profile sprite-sheet did not receive the same full reimport: %s; pixel=RGBA(%u,%u,%u,%u)"),
					*ProfilePixelError,
					ActualProfileColor.R, ActualProfileColor.G, ActualProfileColor.B, ActualProfileColor.A);
				return false;
			}
			return true;
		}

		bool GatherFixturePackages(
			TArray<FFixturePackage>& OutPackages,
			TArray<UObject*>& OutRegisteredAssets,
			FString& OutError) const
		{
			TMap<UPackage*, UObject*> RootByPackage;
			IAssetRegistry& AssetRegistry =
				FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			TArray<FAssetData> AssetData;
			AssetRegistry.GetAssetsByPath(FName(*OutputRoot), AssetData, true, false);
			for (const FAssetData& Data : AssetData)
			{
				UObject* Asset = Data.GetAsset();
				if (!Asset || !IsFixturePackageName(Data.PackageName.ToString()))
				{
					continue;
				}
				UPackage* Package = Asset->GetOutermost();
				RootByPackage.FindOrAdd(Package) = Asset;
				OutRegisteredAssets.AddUnique(Asset);
			}

			for (TObjectIterator<UPackage> It; It; ++It)
			{
				UPackage* Package = *It;
				if (!IsValid(Package) || !IsFixturePackageName(Package->GetName()))
				{
					continue;
				}
				if (!RootByPackage.Contains(Package))
				{
					const FString AssetName = FPackageName::GetShortName(Package->GetName());
					if (UObject* RootAsset = FindObject<UObject>(Package, *AssetName))
					{
						RootByPackage.Add(Package, RootAsset);
						if (bSeedAssetRegistered && Package->GetName() == LayerPackageName)
						{
							OutRegisteredAssets.AddUnique(RootAsset);
						}
					}
				}
			}

			OutPackages.Reserve(RootByPackage.Num());
			for (const TPair<UPackage*, UObject*>& Pair : RootByPackage)
			{
				if (!Pair.Key || !Pair.Value)
				{
					continue;
				}
				OutPackages.Add({ Pair.Key, Pair.Value });
			}
			OutPackages.Sort([](const FFixturePackage& Left, const FFixturePackage& Right)
			{
				return Left.Package->GetName() < Right.Package->GetName();
			});

			if ((bSeedAssetRegistered || IFileManager::Get().FileExists(*LayerPackageFile))
				&& !OutPackages.ContainsByPredicate([this](const FFixturePackage& Record)
				{
					return Record.Package && Record.Package->GetName() == LayerPackageName;
				}))
			{
				OutError = TEXT("the seed Character Layer package could not be gathered for exact cleanup");
				return false;
			}
			return true;
		}

		bool SaveFixturePackages(TArray<FFixturePackage>& Packages, FString& OutError) const
		{
			FinishTextureCompilation(Packages);
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			for (const FFixturePackage& Record : Packages)
			{
				if (!Record.Package || !Record.RootAsset || !Record.RootAsset->IsAsset())
				{
					OutError = TEXT("a generated package has no top-level asset to persist");
					return false;
				}
				const FString FilePath = FPackageName::LongPackageNameToFilename(
					Record.Package->GetName(), FPackageName::GetAssetPackageExtension());
				if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true)
					|| !UPackage::SavePackage(Record.Package, Record.RootAsset, *FilePath, SaveArgs)
					|| !IFileManager::Get().FileExists(*FilePath))
				{
					OutError = FString::Printf(
						TEXT("generated package '%s' did not persist"), *Record.Package->GetName());
					return false;
				}
				Record.Package->SetDirtyFlag(false);
			}
			return true;
		}

		static void FinishTextureCompilation(const TArray<FFixturePackage>& Packages)
		{
			TArray<UTexture*> Textures;
			for (const FFixturePackage& Record : Packages)
			{
				if (UTexture* Texture = Cast<UTexture>(Record.RootAsset))
				{
					Textures.AddUnique(Texture);
				}
			}
			if (!Textures.IsEmpty())
			{
				FTextureCompilingManager::Get().FinishCompilation(Textures);
			}
		}

		bool UnloadPackages(TArray<FFixturePackage>& Packages, FString& OutError) const
		{
			FinishTextureCompilation(Packages);
			TArray<UPackage*> PackagesToUnload;
			TArray<FString> PackageNames;
			for (const FFixturePackage& Record : Packages)
			{
				if (!Record.Package)
				{
					continue;
				}
				if (Record.Package->IsRooted())
				{
					Record.Package->RemoveFromRoot();
				}
				Record.Package->SetDirtyFlag(false);
				PackagesToUnload.AddUnique(Record.Package);
				PackageNames.AddUnique(Record.Package->GetName());
			}

			FText UnloadFailure;
			const bool bUnloaded = PackagesToUnload.IsEmpty()
				|| UPackageTools::UnloadPackages(PackagesToUnload, UnloadFailure, true);
			Packages.Reset();
			PackagesToUnload.Reset();
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

			TArray<FString> StillLoaded;
			for (const FString& PackageName : PackageNames)
			{
				if (FindPackage(nullptr, *PackageName))
				{
					StillLoaded.Add(PackageName);
				}
			}
			if (!bUnloaded || !StillLoaded.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("fixture package unload failed (%s); still loaded: %s"),
					*UnloadFailure.ToString(), *FString::Join(StillLoaded, TEXT(", ")));
				return false;
			}
			return true;
		}

		bool IsFixturePackageName(const FString& PackageName) const
		{
			return PackageName == OutputRoot
				|| PackageName.StartsWith(OutputRoot + TEXT("/"), ESearchCase::CaseSensitive);
		}

		bool HasLoadedFixturePackage() const
		{
			for (TObjectIterator<UPackage> It; It; ++It)
			{
				if (IsValid(*It) && IsFixturePackageName(It->GetName()))
				{
					return true;
				}
			}
			return false;
		}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		// First member: constructed before, destroyed after, everything the fixture saves.
		FScopedDisableValidateOnSave ValidateOnSaveGuard;
#endif
		FAutomationTestBase& Test;
		const FString AssetPrefix = TEXT("WatcherHero");
		FString FixtureLeaf;
		FString OutputRoot;
		FString LayerPackageName;
		FString LayerAssetName;
		FString LayerPackageFile;
		FString ContentDirectory;
		FString SourceDirectory;
		FString SourceFile;
		FString StoredSourcePath;
		FSoftObjectPath LayerAssetPath;
		bool bSeedAssetRegistered = false;
		bool bCleaned = false;
	};

	/**
	 * Flips the Live .ase Auto-Reimport project setting through the SAME committed-edit path the
	 * Project Settings panel uses (PostEditChangeProperty -> OnAseLiveReimportSettingChanged), so the
	 * watcher's off-to-on reconcile subscription is exercised, not simulated. Nothing is written to
	 * config on this path. The destructor restores the original value SILENTLY by design: teardown
	 * must not broadcast a transition that would reconcile real project files mid-suite.
	 */
	class FScopedAseLiveReimportSetting final
	{
	public:
		FScopedAseLiveReimportSetting()
			: bOriginalValue(UPaper2DPlusSettings::Get()->bEnableAseLiveReimport)
		{
		}

		~FScopedAseLiveReimportSetting()
		{
			GetMutableDefault<UPaper2DPlusSettings>()->bEnableAseLiveReimport = bOriginalValue;
		}

		void SetThroughEditPath(const bool bEnabled) const
		{
			UPaper2DPlusSettings* Settings = GetMutableDefault<UPaper2DPlusSettings>();
			Settings->bEnableAseLiveReimport = bEnabled;
			FProperty* Property = FindFProperty<FProperty>(
				UPaper2DPlusSettings::StaticClass(),
				GET_MEMBER_NAME_CHECKED(UPaper2DPlusSettings, bEnableAseLiveReimport));
			FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
			Settings->PostEditChangeProperty(Event);
		}

	private:
		bool bOriginalValue = true;
	};

	struct FWaitState
	{
		explicit FWaitState(const uint64 InStartingSuccessfulReimportEpoch)
			: StartingSuccessfulReimportEpoch(InStartingSuccessfulReimportEpoch)
		{
		}

		void RestartClock()
		{
			StartedSeconds = FPlatformTime::Seconds();
		}

		double StartedSeconds = FPlatformTime::Seconds();
		uint64 StartingSuccessfulReimportEpoch = 0;
		uint64 StartingDroppedAseChangeEpoch = 0;
		/** Set by an earlier chained latent command when it already failed and finished the fixture,
		 *  so a follow-up command exits instead of timing out against a cleaned-up watcher. */
		bool bAborted = false;
		/** Keeps a test's scoped setting override alive until its latent chain completes. */
		TSharedPtr<FScopedAseLiveReimportSetting> SettingGuard;
	};

	static void FinishFixture(
		const TSharedRef<FScopedMappedAseFixture>& Fixture,
		FAutomationTestBase& Test)
	{
		FString CleanupError;
		const bool bCleaned = Fixture->Cleanup(CleanupError);
		Test.TestTrue(TEXT("the GUID-scoped watcher fixture is removed and the service restored"), bCleaned);
		if (!bCleaned && !CleanupError.IsEmpty())
		{
			Test.AddError(CleanupError);
		}
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_FIVE_PARAMETER(
	FPaper2DPlusWaitForMappedAseReimport,
	TSharedRef<Paper2DPlusAseWatcherIntegrationTest::FScopedMappedAseFixture>, Fixture,
	TSharedRef<Paper2DPlusAseWatcherIntegrationTest::FWaitState>, State,
	FAutomationTestBase*, Test,
	FColor, ExpectedColor,
	FString, ExpectedHash);

bool FPaper2DPlusWaitForMappedAseReimport::Update()
{
	using namespace Paper2DPlusAseWatcherIntegrationTest;
	if (State->bAborted)
	{
		return true; // an earlier chained command already failed and finished the fixture
	}
	FTextureWatcherService& Watcher = FTextureWatcherService::Get();
	const uint64 CompletedEpoch = Watcher.GetSuccessfulAseReimportEpochForTests(
		Fixture->GetSourceFile());
	const bool bProductionReimportCompleted =
		CompletedEpoch > State->StartingSuccessfulReimportEpoch;
	const bool bQueueDrained = Watcher.IsPendingChangeDrainedForTests(Fixture->GetSourceFile());
	if (bProductionReimportCompleted)
	{
		if (!Fixture->GetLayerAssetPath().ResolveObject())
		{
			Test->AddError(
				TEXT("the watcher reported a successful mapped reimport without lazy-loading the cold registry asset"));
			FinishFixture(Fixture, *Test);
			return true;
		}

		FString Diagnostic;
		if (!Fixture->ObserveLoadedResult(ExpectedColor, ExpectedHash, true, Diagnostic))
		{
			Test->AddError(FString::Printf(
				TEXT("the production reimport completed without the required full-import outputs: %s"),
				*Diagnostic));
			FinishFixture(Fixture, *Test);
			return true;
		}
		if (!bQueueDrained)
		{
			return false;
		}

		FString PersistenceError;
		const bool bPersisted = Fixture->SaveUnloadReloadAndVerify(
			ExpectedColor, ExpectedHash, PersistenceError);
		Test->TestTrue(
			TEXT("the exact profile, flipbook, profile sprite, layer animation sprite, hash, and pixels survive save/unload/reload"),
			bPersisted);
		if (!bPersisted && !PersistenceError.IsEmpty())
		{
			Test->AddError(PersistenceError);
		}
		FinishFixture(Fixture, *Test);
		return true;
	}

	const double ElapsedSeconds = FPlatformTime::Seconds() - State->StartedSeconds;
	if (ElapsedSeconds >= 15.0)
	{
		Test->AddError(FString::Printf(
			TEXT("Mapped .ase reimport timed out: successfulEpoch=%llu baseline=%llu queueDrained=%s"),
			static_cast<unsigned long long>(CompletedEpoch),
			static_cast<unsigned long long>(State->StartingSuccessfulReimportEpoch),
			bQueueDrained ? TEXT("true") : TEXT("false")));
		FinishFixture(Fixture, *Test);
		return true;
	}
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusMappedAseLiveEditTest,
	"Paper2DPlus.EditorServices.TextureWatcher.MappedAseLiveEditReimportsAndPersists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusMappedAseLiveEditTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAseWatcherIntegrationTest;
	if (!TestNotNull(TEXT("editor automation exposes GEditor for the production debounce timer"), GEditor))
	{
		return false;
	}
	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	if (!TestFalse(TEXT("the Asset Registry initial scan has completed"), AssetRegistry.IsLoadingAssets()))
	{
		return false;
	}
	FTextureWatcherService& Watcher = FTextureWatcherService::Get();
	if (!TestTrue(TEXT("TextureWatcherService is active before the test"), Watcher.IsInitialized()))
	{
		return false;
	}

	TSharedRef<FScopedMappedAseFixture> Fixture = MakeShared<FScopedMappedAseFixture>(*this);
	FString SetupError;
	const bool bSeedCreated = Fixture->CreateColdRedSeed(
		/*bPreserveModuleStartedContentWatcher=*/ true,
		SetupError);
	if (!TestTrue(
		FString::Printf(TEXT("the red registry-backed seed is saved cold (%s)"), *SetupError),
		bSeedCreated))
	{
		if (!SetupError.IsEmpty())
		{
			AddError(SetupError);
		}
		return false;
	}

	if (!TestTrue(TEXT("the module-started Project Content watcher remains active"), Watcher.IsInitialized()))
	{
		return false;
	}
	if (!TestNull(
		TEXT("the mapped registry asset is still cold immediately before the one live filesystem edit"),
		Fixture->GetLayerAssetPath().ResolveObject()))
	{
		return false;
	}
	const uint64 StartingSuccessfulReimportEpoch =
		Watcher.GetSuccessfulAseReimportEpochForTests(Fixture->GetSourceFile());

	FString WriteError;
	const bool bGreenWritten = Fixture->WriteSource(Green, GreenHash, WriteError);
	if (!TestTrue(
		FString::Printf(TEXT("the artist's green .ase edit reaches the live filesystem (%s)"), *WriteError),
		bGreenWritten))
	{
		if (!WriteError.IsEmpty())
		{
			AddError(WriteError);
		}
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FPaper2DPlusWaitForMappedAseReimport(
		Fixture, MakeShared<FWaitState>(StartingSuccessfulReimportEpoch), this, Green, GreenHash));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusMappedAseOfflineChangeTest,
	"Paper2DPlus.EditorServices.TextureWatcher.MappedAseOfflineChangeReconcilesAndPersists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusMappedAseOfflineChangeTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAseWatcherIntegrationTest;
	if (!TestNotNull(TEXT("editor automation exposes GEditor for the production debounce timer"), GEditor))
	{
		return false;
	}
	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	if (!TestFalse(TEXT("the Asset Registry initial scan has completed"), AssetRegistry.IsLoadingAssets()))
	{
		return false;
	}
	FTextureWatcherService& Watcher = FTextureWatcherService::Get();
	if (!TestTrue(TEXT("TextureWatcherService is active before the test"), Watcher.IsInitialized()))
	{
		return false;
	}

	TSharedRef<FScopedMappedAseFixture> Fixture = MakeShared<FScopedMappedAseFixture>(*this);
	FString SetupError;
	const bool bSeedCreated = Fixture->CreateColdRedSeed(
		/*bPreserveModuleStartedContentWatcher=*/ false,
		SetupError);
	if (!TestTrue(
		FString::Printf(TEXT("the red registry-backed seed is saved cold (%s)"), *SetupError),
		bSeedCreated))
	{
		if (!SetupError.IsEmpty())
		{
			AddError(SetupError);
		}
		return false;
	}

	if (!TestNull(
		TEXT("the offline edit begins with the saved Character Layer unloaded"),
		Fixture->GetLayerAssetPath().ResolveObject()))
	{
		return false;
	}
	const uint64 StartingSuccessfulReimportEpoch =
		Watcher.GetSuccessfulAseReimportEpochForTests(Fixture->GetSourceFile());
	FString WriteError;
	const bool bBlueWritten = Fixture->WriteSource(Blue, BlueHash, WriteError);
	if (!TestTrue(
		FString::Printf(TEXT("the .ase turns blue while TextureWatcherService is stopped (%s)"), *WriteError),
		bBlueWritten))
	{
		if (!WriteError.IsEmpty())
		{
			AddError(WriteError);
		}
		return false;
	}

	Watcher.Initialize();
	if (!TestTrue(TEXT("TextureWatcherService restarts and runs startup reconcile"), Watcher.IsInitialized()))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FPaper2DPlusWaitForMappedAseReimport(
		Fixture, MakeShared<FWaitState>(StartingSuccessfulReimportEpoch), this, Blue, BlueHash));
	return true;
}

// TASK-192 U1: waits until the watcher deliberately DROPS a mapped .ase change because Live .ase
// Auto-Reimport is off, proves nothing was reimported or even loaded, then re-enables the setting
// through the production edit path so the next chained command can observe the healing reimport.
DEFINE_LATENT_AUTOMATION_COMMAND_FOUR_PARAMETER(
	FPaper2DPlusWaitForDroppedAseThenReenable,
	TSharedRef<Paper2DPlusAseWatcherIntegrationTest::FScopedMappedAseFixture>, Fixture,
	TSharedRef<Paper2DPlusAseWatcherIntegrationTest::FWaitState>, State,
	FAutomationTestBase*, Test,
	TSharedRef<Paper2DPlusAseWatcherIntegrationTest::FWaitState>, HealState);

bool FPaper2DPlusWaitForDroppedAseThenReenable::Update()
{
	using namespace Paper2DPlusAseWatcherIntegrationTest;
	FTextureWatcherService& Watcher = FTextureWatcherService::Get();

	const uint64 SuccessEpoch =
		Watcher.GetSuccessfulAseReimportEpochForTests(Fixture->GetSourceFile());
	if (SuccessEpoch > State->StartingSuccessfulReimportEpoch)
	{
		Test->AddError(TEXT("a mapped .ase reimport ran while Live .ase Auto-Reimport was disabled"));
		HealState->bAborted = true;
		FinishFixture(Fixture, *Test);
		return true;
	}

	const uint64 DropEpoch = Watcher.GetDroppedAseChangeEpochForTests(Fixture->GetSourceFile());
	if (DropEpoch > State->StartingDroppedAseChangeEpoch)
	{
		// The live edit travelled the watcher, debounce, and processing pipeline and was refused at
		// the setting gate: nothing was reimported, and the cold registry asset was never even loaded.
		if (Fixture->GetLayerAssetPath().ResolveObject() != nullptr)
		{
			Test->AddError(TEXT("the dropped .ase change loaded the mapped registry asset anyway"));
			HealState->bAborted = true;
			FinishFixture(Fixture, *Test);
			return true;
		}

		// Off-to-on transition, committed through the production settings edit path: the watcher's
		// subscription runs the offline reconcile synchronously, re-queueing this file by content-hash
		// mismatch WITHOUT the file being written again. The next chained command waits for that
		// healing reimport to complete and verify.
		HealState->SettingGuard->SetThroughEditPath(true);
		HealState->RestartClock();
		return true;
	}

	const double ElapsedSeconds = FPlatformTime::Seconds() - State->StartedSeconds;
	if (ElapsedSeconds >= 15.0)
	{
		Test->AddError(FString::Printf(
			TEXT("The disabled-setting drop was never observed: dropEpoch=%llu baseline=%llu successEpoch=%llu"),
			static_cast<unsigned long long>(DropEpoch),
			static_cast<unsigned long long>(State->StartingDroppedAseChangeEpoch),
			static_cast<unsigned long long>(SuccessEpoch)));
		HealState->bAborted = true;
		FinishFixture(Fixture, *Test);
		return true;
	}
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusMappedAseLiveEditDroppedWhileSettingOffTest,
	"Paper2DPlus.EditorServices.TextureWatcher.AseLiveEditDroppedWhileSettingOffThenHealsOnReenable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusMappedAseLiveEditDroppedWhileSettingOffTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAseWatcherIntegrationTest;
	if (!TestNotNull(TEXT("editor automation exposes GEditor for the production debounce timer"), GEditor))
	{
		return false;
	}
	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	if (!TestFalse(TEXT("the Asset Registry initial scan has completed"), AssetRegistry.IsLoadingAssets()))
	{
		return false;
	}
	FTextureWatcherService& Watcher = FTextureWatcherService::Get();
	if (!TestTrue(TEXT("TextureWatcherService is active before the test"), Watcher.IsInitialized()))
	{
		return false;
	}
	// An unconfigured project must behave exactly as it did before the setting existed.
	if (!TestTrue(TEXT("Live .ase Auto-Reimport defaults to enabled"),
		UPaper2DPlusSettings::Get()->bEnableAseLiveReimport))
	{
		return false;
	}

	TSharedRef<FScopedMappedAseFixture> Fixture = MakeShared<FScopedMappedAseFixture>(*this);
	FString SetupError;
	const bool bSeedCreated = Fixture->CreateColdRedSeed(
		/*bPreserveModuleStartedContentWatcher=*/ true,
		SetupError);
	if (!TestTrue(
		FString::Printf(TEXT("the red registry-backed seed is saved cold (%s)"), *SetupError),
		bSeedCreated))
	{
		if (!SetupError.IsEmpty())
		{
			AddError(SetupError);
		}
		return false;
	}

	const uint64 StartingSuccessfulReimportEpoch =
		Watcher.GetSuccessfulAseReimportEpochForTests(Fixture->GetSourceFile());
	const uint64 StartingDroppedAseChangeEpoch =
		Watcher.GetDroppedAseChangeEpochForTests(Fixture->GetSourceFile());

	// Turn the setting OFF through the same committed-edit path the Project Settings panel uses.
	// The guard lives on the latent wait states so it outlives RunTest and restores on any exit.
	TSharedRef<FScopedAseLiveReimportSetting> SettingGuard = MakeShared<FScopedAseLiveReimportSetting>();
	SettingGuard->SetThroughEditPath(false);

	FString WriteError;
	const bool bGreenWritten = Fixture->WriteSource(Green, GreenHash, WriteError);
	if (!TestTrue(
		FString::Printf(TEXT("the artist's green .ase edit reaches the live filesystem (%s)"), *WriteError),
		bGreenWritten))
	{
		if (!WriteError.IsEmpty())
		{
			AddError(WriteError);
		}
		FinishFixture(Fixture, *this);
		return false;
	}

	TSharedRef<FWaitState> DropState = MakeShared<FWaitState>(StartingSuccessfulReimportEpoch);
	DropState->StartingDroppedAseChangeEpoch = StartingDroppedAseChangeEpoch;
	DropState->SettingGuard = SettingGuard;
	TSharedRef<FWaitState> HealState = MakeShared<FWaitState>(StartingSuccessfulReimportEpoch);
	HealState->SettingGuard = SettingGuard;

	ADD_LATENT_AUTOMATION_COMMAND(FPaper2DPlusWaitForDroppedAseThenReenable(
		Fixture, DropState, this, HealState));
	ADD_LATENT_AUTOMATION_COMMAND(FPaper2DPlusWaitForMappedAseReimport(
		Fixture, HealState, this, Green, GreenHash));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusMappedAseOfflineChangeHeldWhileSettingOffTest,
	"Paper2DPlus.EditorServices.TextureWatcher.AseOfflineChangeHeldWhileSettingOffThenReconcilesOnReenable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusMappedAseOfflineChangeHeldWhileSettingOffTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAseWatcherIntegrationTest;
	if (!TestNotNull(TEXT("editor automation exposes GEditor for the production debounce timer"), GEditor))
	{
		return false;
	}
	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	if (!TestFalse(TEXT("the Asset Registry initial scan has completed"), AssetRegistry.IsLoadingAssets()))
	{
		return false;
	}
	FTextureWatcherService& Watcher = FTextureWatcherService::Get();
	if (!TestTrue(TEXT("TextureWatcherService is active before the test"), Watcher.IsInitialized()))
	{
		return false;
	}

	TSharedRef<FScopedMappedAseFixture> Fixture = MakeShared<FScopedMappedAseFixture>(*this);
	FString SetupError;
	const bool bSeedCreated = Fixture->CreateColdRedSeed(
		/*bPreserveModuleStartedContentWatcher=*/ false,
		SetupError);
	if (!TestTrue(
		FString::Printf(TEXT("the red registry-backed seed is saved cold (%s)"), *SetupError),
		bSeedCreated))
	{
		if (!SetupError.IsEmpty())
		{
			AddError(SetupError);
		}
		return false;
	}

	const uint64 StartingSuccessfulReimportEpoch =
		Watcher.GetSuccessfulAseReimportEpochForTests(Fixture->GetSourceFile());
	FString WriteError;
	const bool bBlueWritten = Fixture->WriteSource(Blue, BlueHash, WriteError);
	if (!TestTrue(
		FString::Printf(TEXT("the .ase turns blue while TextureWatcherService is stopped (%s)"), *WriteError),
		bBlueWritten))
	{
		if (!WriteError.IsEmpty())
		{
			AddError(WriteError);
		}
		FinishFixture(Fixture, *this);
		return false;
	}

	// Startup with the setting OFF: the watcher must come alive (so re-enabling needs no restart and
	// non-.ase texture reimport keeps its directory callbacks) while the startup reconcile queues
	// NOTHING for the blue drift. The registry scan completed above, so Initialize runs the reconcile
	// synchronously — the negative below is deterministic, not a timing bet.
	TSharedRef<FScopedAseLiveReimportSetting> SettingGuard = MakeShared<FScopedAseLiveReimportSetting>();
	SettingGuard->SetThroughEditPath(false);
	Watcher.Initialize();

	bool bHeldBackCorrectly = true;
	bHeldBackCorrectly &= TestTrue(
		TEXT("the watcher stays alive while Live .ase Auto-Reimport is disabled"),
		Watcher.IsInitialized());
	bHeldBackCorrectly &= TestFalse(
		TEXT("the disabled startup reconcile queues nothing for the offline blue drift"),
		Watcher.IsAseChangePendingForTests(Fixture->GetSourceFile()));
	bHeldBackCorrectly &= TestTrue(
		TEXT("no reimport ran while the setting was off"),
		Watcher.GetSuccessfulAseReimportEpochForTests(Fixture->GetSourceFile())
			== StartingSuccessfulReimportEpoch);
	bHeldBackCorrectly &= TestNull(
		TEXT("the drifted Character Layer stays cold while the setting is off"),
		Fixture->GetLayerAssetPath().ResolveObject());
	if (!bHeldBackCorrectly)
	{
		FinishFixture(Fixture, *this);
		return false;
	}

	// Off-to-on transition through the production settings edit path: the watcher's subscription
	// re-runs the reconcile synchronously and must queue the blue drift by content-hash mismatch —
	// the file itself is NOT written again.
	SettingGuard->SetThroughEditPath(true);
	if (!TestTrue(
		TEXT("re-enabling the setting queues the drifted file without an editor restart"),
		Watcher.IsAseChangePendingForTests(Fixture->GetSourceFile())))
	{
		FinishFixture(Fixture, *this);
		return false;
	}

	TSharedRef<FWaitState> HealState = MakeShared<FWaitState>(StartingSuccessfulReimportEpoch);
	HealState->SettingGuard = SettingGuard;
	ADD_LATENT_AUTOMATION_COMMAND(FPaper2DPlusWaitForMappedAseReimport(
		Fixture, HealState, this, Blue, BlueHash));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
