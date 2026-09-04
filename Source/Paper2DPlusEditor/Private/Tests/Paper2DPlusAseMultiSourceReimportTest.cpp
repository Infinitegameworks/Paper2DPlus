// Copyright 2026 Infinite Gameworks. All Rights Reserved.
//
// Paper2DPlusAseMultiSourceReimportTest.cpp — the two multi-source .ase defects reported 2026-09-04:
//
//   1. Dangling sprite references were invisible. A Layer Profile could record sprite references to
//      assets nothing ever created (an engine-invalid character in a layer name; a per-source replay of
//      a row two sources share) and neither the import, validation nor load complained — the only
//      symptom was a character missing a garment plus LogCoreRedirects noise that failed unrelated
//      tests. Pin: ValidateSpriteReferences() reports exactly one Error per layer with missing frames,
//      and ValidateLayerAsset() carries it.
//   2. Force Full Reimport of ONE source of a Layer Profile whose layer name is shared with ANOTHER
//      source rebuilt the shared row from that source alone. Pin: the replay is refused, names the
//      shared layer, and leaves the asset untouched; a source that shares nothing still replays.
//
// The .ase writer is a per-file copy (AseMulti_ prefix) by the unity-build rule: every test file in
// this module is a separate translation unit only nominally, so shared helpers collide.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AsepriteImporter.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "PackageTools.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperSprite.h"
#include "TextureCompiler.h"
#include "TextureWatcherService.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace Paper2DPlusAseMultiSourceReimportTest
{
	/** Little-endian byte writer for crafting minimal .ase buffers. */
	struct FAseMultiWriter
	{
		TArray<uint8> Bytes;

		void U8(uint8 V) { Bytes.Add(V); }
		void U16(uint16 V) { Bytes.Add(V & 0xFF); Bytes.Add((V >> 8) & 0xFF); }
		void S16(int16 V) { U16(static_cast<uint16>(V)); }
		void U32(uint32 V)
		{
			Bytes.Add(V & 0xFF); Bytes.Add((V >> 8) & 0xFF);
			Bytes.Add((V >> 16) & 0xFF); Bytes.Add((V >> 24) & 0xFF);
		}
		void Zeros(int32 Count) { Bytes.AddZeroed(Count); }
		void Str(const FString& S)
		{
			const FTCHARToUTF8 Utf8(*S);
			U16(static_cast<uint16>(Utf8.Length()));
			Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		}
		void Append(const TArray<uint8>& Other) { Bytes.Append(Other); }
	};

	static TArray<uint8> AseMulti_Chunk(uint16 ChunkType, const TArray<uint8>& Payload)
	{
		FAseMultiWriter W;
		W.U32(static_cast<uint32>(Payload.Num() + 6));
		W.U16(ChunkType);
		W.Append(Payload);
		return W.Bytes;
	}

	static TArray<uint8> AseMulti_LayerChunk(const FString& Name)
	{
		FAseMultiWriter P;
		P.U16(1);   // visible
		P.U16(0);   // normal layer
		P.U16(0);   // child level
		P.U16(0); P.U16(0);
		P.U16(0);   // blend mode
		P.U8(255);  // opacity
		P.Zeros(3);
		P.Str(Name);
		return AseMulti_Chunk(0x2004, P.Bytes);
	}

	static TArray<uint8> AseMulti_RawCelChunk(uint16 LayerIndex, uint16 W, uint16 H, const FColor& Fill)
	{
		FAseMultiWriter P;
		P.U16(LayerIndex);
		P.S16(0); P.S16(0);
		P.U8(255);
		P.U16(0);   // ASE_CEL_RAW
		P.Zeros(7);
		P.U16(W); P.U16(H);
		for (int32 i = 0; i < W * H; ++i)
		{
			P.U8(Fill.R); P.U8(Fill.G); P.U8(Fill.B); P.U8(Fill.A);
		}
		return AseMulti_Chunk(0x2005, P.Bytes);
	}

	static TArray<uint8> AseMulti_TagChunk(const FString& TagName, uint16 From, uint16 To)
	{
		FAseMultiWriter P;
		P.U16(1);        // one tag
		P.Zeros(8);
		P.U16(From); P.U16(To);
		P.U8(0);         // forward
		P.Zeros(8);
		P.U8(0); P.U8(0); P.U8(0); P.U8(0);
		P.Str(TagName);
		return AseMulti_Chunk(0x2018, P.Bytes);
	}

	static TArray<uint8> AseMulti_Frame(const TArray<TArray<uint8>>& Chunks)
	{
		int32 ChunkBytes = 0;
		for (const TArray<uint8>& C : Chunks) { ChunkBytes += C.Num(); }

		FAseMultiWriter W;
		W.U32(static_cast<uint32>(16 + ChunkBytes));
		W.U16(0xF1FA);
		W.U16(static_cast<uint16>(Chunks.Num()));
		W.U16(100);   // duration ms
		W.Zeros(2);
		W.U32(static_cast<uint32>(Chunks.Num()));
		for (const TArray<uint8>& C : Chunks) { W.Append(C); }
		return W.Bytes;
	}

	/** One 4x4 frame carrying the given visual layers (in order) and one tag with the given name. */
	static TArray<uint8> AseMulti_File(const TArray<FString>& LayerNames, const FString& TagName, const FColor& Fill)
	{
		TArray<TArray<uint8>> Chunks;
		for (const FString& LayerName : LayerNames)
		{
			Chunks.Add(AseMulti_LayerChunk(LayerName));
		}
		Chunks.Add(AseMulti_TagChunk(TagName, 0, 0));
		for (int32 LayerIdx = 0; LayerIdx < LayerNames.Num(); ++LayerIdx)
		{
			Chunks.Add(AseMulti_RawCelChunk(static_cast<uint16>(LayerIdx), 4, 4, Fill));
		}

		FAseMultiWriter W;
		const TArray<uint8> Frame = AseMulti_Frame(Chunks);
		W.U32(static_cast<uint32>(128 + Frame.Num()));
		W.U16(0xA5E0);
		W.U16(1);    // frame count
		W.U16(4); W.U16(4);
		W.U16(32);   // color depth
		W.U32(0);
		W.U16(100);
		W.Zeros(128 - W.Bytes.Num());
		W.Append(Frame);
		return W.Bytes;
	}

	/** Parse + composite + run the layered import (fresh, or into ExistingAsset) under a cost scope. */
	static UPaper2DPlusCharacterLayerAsset* AseMulti_Import(
		FAutomationTestBase& Test, const TCHAR* Label,
		const TArray<uint8>& Buffer, const FString& AseFile, const FString& OutputRoot,
		const FString& AssetPrefix, UPaper2DPlusCharacterLayerAsset* ExistingAsset)
	{
		if (!Test.TestTrue(FString::Printf(TEXT("%s: fixture .ase written"), Label),
			FFileHelper::SaveArrayToFile(Buffer, *AseFile)))
		{
			return nullptr;
		}
		FAsepriteParsedData Parsed;
		FString Error;
		if (!Test.TestTrue(FString::Printf(TEXT("%s: ParseFile succeeds (%s)"), Label, *Error),
			FAsepriteImporter::ParseFile(AseFile, Parsed, Error)))
		{
			return nullptr;
		}
		const FPerLayerBufferMap PerLayerBuffers = FAsepriteImporter::CompositePerLayer(Parsed);

		FAsepriteLayerImportSettings Settings;
		FAsepriteImporter::InitDefaultSelection(Parsed, Settings);
		Settings.ImportMode = EAsepriteImportMode::LayerAssetNewProfile;
		Settings.OutputPath = OutputRoot;
		Settings.AssetPrefix = AssetPrefix;
		Settings.bOrganizeIntoSubfolders = false;
		Settings.bUserConfirmed = true;
		Settings.bKeepSourceInProject = false;
		Settings.SourceFilePath = AseFile;
		if (ExistingAsset)
		{
			Settings.ExistingLayerAsset = ExistingAsset;
		}

		FAsepriteImportCostScope CostScope;
		UObject* Result = FAsepriteImporter::ImportAsLayeredAsset(Parsed, PerLayerBuffers, Settings);
		UPaper2DPlusCharacterLayerAsset* Asset = Cast<UPaper2DPlusCharacterLayerAsset>(Result);
		Test.TestNotNull(FString::Printf(TEXT("%s: layered import produced the Layer Profile"), Label), Asset);
		return Asset;
	}

	/** Everything a per-source replay could change, flattened so before/after compare as one string. */
	static FString AseMulti_Snapshot(const UPaper2DPlusCharacterLayerAsset& Asset)
	{
		FString Out;
		for (const FCharacterLayer& Layer : Asset.Layers)
		{
			Out += FString::Printf(TEXT("L[%s] tex=%s\n"), *Layer.LayerName, *Layer.SourceTexture.ToString());
			for (const FCharacterLayerAnimationMapping& Mapping : Layer.AnimationSprites)
			{
				Out += FString::Printf(TEXT("  A[%s]"), *Mapping.AnimationName);
				for (const TSoftObjectPtr<UPaperSprite>& Sprite : Mapping.Sprites)
				{
					Out += TEXT(" ") + Sprite.ToString();
				}
				Out += TEXT("\n");
			}
		}
#if WITH_EDITORONLY_DATA
		for (const FAsepriteSourceContext& Source : Asset.ImportedAseSources)
		{
			Out += FString::Printf(TEXT("S[%s] hash=%s prefix=%s\n"),
				*Source.StoredSourcePath, *Source.ContentHash, *Source.AssetPrefix);
			TArray<FString> Keys;
			Source.LayerContentHashes.GetKeys(Keys);
			Keys.Sort();
			for (const FString& Key : Keys)
			{
				Out += FString::Printf(TEXT("  %s=%s\n"), *Key, *Source.LayerContentHashes[Key]);
			}
		}
#endif
		return Out;
	}

	static void AseMulti_ClearAllDirtyUnder(const FString& OutputRoot)
	{
		IAssetRegistry& AssetRegistry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPath(FName(*OutputRoot), Assets, true, false);
		for (const FAssetData& Data : Assets)
		{
			if (UObject* Asset = Data.IsAssetLoaded() ? Data.GetAsset() : nullptr)
			{
				if (UPackage* Package = Asset->GetOutermost())
				{
					Package->SetDirtyFlag(false);
				}
			}
		}
	}

	/** Drops every fixture asset under OutputRoot: registry row out, dirty flag cleared, packages
	 *  unloaded and GC'd. Nothing was saved to disk. */
	static void AseMulti_DropAllUnder(const FString& OutputRoot)
	{
		IAssetRegistry& AssetRegistry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPath(FName(*OutputRoot), Assets, true, false);
		TArray<UPackage*> Packages;
		TArray<UTexture*> Textures;
		for (const FAssetData& Data : Assets)
		{
			UObject* Asset = Data.IsAssetLoaded() ? Data.GetAsset() : nullptr;
			if (!Asset)
			{
				continue;
			}
			if (UTexture* Texture = Cast<UTexture>(Asset))
			{
				Textures.Add(Texture);
			}
			if (Asset->IsAsset())
			{
				FAssetRegistryModule::AssetDeleted(Asset);
			}
			if (UPackage* Package = Asset->GetOutermost())
			{
				Package->SetDirtyFlag(false);
				Packages.AddUnique(Package);
			}
		}
		if (Textures.Num() > 0)
		{
			FTextureCompilingManager::Get().FinishCompilation(Textures);
		}
		if (Packages.Num() > 0)
		{
			FText UnloadFailure;
			UPackageTools::UnloadPackages(Packages, UnloadFailure, true);
		}
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}

	/** RAII: the watcher is shut down for the duration of a direct-import fixture and restored after —
	 *  its import-tail RefreshAssetMapping would otherwise register the fixture's source directory. */
	class FAseMultiScopedWatcherShutdown final
	{
	public:
		explicit FAseMultiScopedWatcherShutdown(FAutomationTestBase& InTest)
			: Test(InTest)
		{
			Test.TestTrue(TEXT("the watcher released every callback for the fixture window"),
				FTextureWatcherService::Get().Shutdown());
		}
		~FAseMultiScopedWatcherShutdown()
		{
			FTextureWatcherService::Get().Initialize();
			Test.TestTrue(TEXT("the watcher is restored after the fixture window"),
				FTextureWatcherService::Get().IsInitialized());
		}
	private:
		FAutomationTestBase& Test;
	};

	static FString AseMulti_FixturePath(const FString& Stem)
	{
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation"));
		IFileManager::Get().MakeDirectory(*Dir, /*Tree*/ true);
		return Dir / (Stem + TEXT(".ase"));
	}
}

// ============================================================
// (1) Dangling sprite references are a validation Error
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerDanglingSpriteReferenceTest,
	"Paper2DPlus.Layer.Validation.DanglingSpriteReferenceIsAnErrorNamingTheLayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerDanglingSpriteReferenceTest::RunTest(const FString& Parameters)
{
	const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);

	UPaper2DPlusCharacterLayerAsset* Asset =
		NewObject<UPaper2DPlusCharacterLayerAsset>(GetTransientPackage(), NAME_None, RF_Transient);
	UPaperSprite* Resident = NewObject<UPaperSprite>(GetTransientPackage(), NAME_None, RF_Transient);

	// "Hat": one resident sprite, one empty frame, and TWO references to assets that were never created.
	{
		FCharacterLayer& Hat = Asset->Layers.AddDefaulted_GetRef();
		Hat.LayerName = TEXT("Head/Hat");
		Hat.LayerId = FGuid::NewGuid();
		FCharacterLayerAnimationMapping& Idle = Hat.AnimationSprites.AddDefaulted_GetRef();
		Idle.AnimationName = TEXT("Idle");
		Idle.Sprites.Add(Resident);
		Idle.Sprites.Add(TSoftObjectPtr<UPaperSprite>());
		Idle.Sprites.Add(TSoftObjectPtr<UPaperSprite>(FSoftObjectPath(FString::Printf(
			TEXT("/Game/__AutomationTemp__/AseMultiNope_%s/Hat_Idle_0.Hat_Idle_0"), *Token))));
		Idle.Sprites.Add(TSoftObjectPtr<UPaperSprite>(FSoftObjectPath(FString::Printf(
			TEXT("/Game/__AutomationTemp__/AseMultiNope_%s/Hat_Idle_1.Hat_Idle_1"), *Token))));
	}
	// "Body": resident only — must produce no row.
	{
		FCharacterLayer& Body = Asset->Layers.AddDefaulted_GetRef();
		Body.LayerName = TEXT("Body");
		Body.LayerId = FGuid::NewGuid();
		FCharacterLayerAnimationMapping& Idle = Body.AnimationSprites.AddDefaulted_GetRef();
		Idle.AnimationName = TEXT("Idle");
		Idle.Sprites.Add(Resident);
		Idle.Sprites.Add(TSoftObjectPtr<UPaperSprite>());
	}

	const TArray<FCharacterLayerValidationIssue> Issues = Asset->ValidateSpriteReferences();
	TestEqual(TEXT("exactly one layer is reported"), Issues.Num(), 1);
	if (Issues.Num() == 1)
	{
		TestEqual(TEXT("the row is an Error"), Issues[0].Severity, ECharacterLayerValidationSeverity::Error);
		TestEqual(TEXT("the row names the layer"), Issues[0].LayerName, FString(TEXT("Head/Hat")));
		TestTrue(TEXT("the row counts the missing frames (empty frames and resident sprites excluded)"),
			Issues[0].Message.Contains(TEXT("references 2 sprite asset(s)")));
		TestTrue(TEXT("the row names the first missing path"),
			Issues[0].Message.Contains(TEXT("Hat_Idle_0")));
	}

	// The native validator carries the row, so every Validate surface (Layer adapter -> panel, Content
	// Browser action, JSON commandlet) sees it without any of them knowing about sprites.
	const TArray<FCharacterLayerValidationIssue> Native = Asset->ValidateLayerAsset();
	const int32 DanglingRows = Native.FilterByPredicate([](const FCharacterLayerValidationIssue& Issue)
	{
		return Issue.LayerName == TEXT("Head/Hat") && Issue.Message.Contains(TEXT("do not exist"));
	}).Num();
	TestEqual(TEXT("ValidateLayerAsset carries exactly the dangling row for the layer"), DanglingRows, 1);
	TestFalse(TEXT("no row is produced for the layer whose references all resolve"),
		Native.ContainsByPredicate([](const FCharacterLayerValidationIssue& Issue)
		{
			return Issue.LayerName == TEXT("Body");
		}));
	return true;
}

// ============================================================
// (2) Force Full Reimport refuses a source whose layers are shared with another source
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAseMultiSourceForceReimportRefusesSharedRowTest,
	"Paper2DPlus.Editor.AsepriteMultiSource.ForceFullReimportRefusesASharedLayerAndReplaysAnUnsharedOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAseMultiSourceForceReimportRefusesSharedRowTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusAseMultiSourceReimportTest;

	const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
	const FString OutputRoot = FString::Printf(TEXT("/Game/__AutomationTemp__/AseMulti_%s"), *Token);
	const FString FileA = AseMulti_FixturePath(TEXT("AseMultiA_") + Token);
	const FString FileB = AseMulti_FixturePath(TEXT("AseMultiB_") + Token);
	const FString FileC = AseMulti_FixturePath(TEXT("AseMultiC_") + Token);
	ON_SCOPE_EXIT
	{
		AseMulti_DropAllUnder(OutputRoot);
		IFileManager::Get().Delete(*FileA, /*RequireExists*/ false);
		IFileManager::Get().Delete(*FileB, /*RequireExists*/ false);
		IFileManager::Get().Delete(*FileC, /*RequireExists*/ false);
	};

	FAseMultiScopedWatcherShutdown WatcherWindow(*this);

	// A and B share the layer "Shared" (one row, two contributors); C shares nothing.
	UPaper2DPlusCharacterLayerAsset* Asset = AseMulti_Import(*this, TEXT("A fresh"),
		AseMulti_File({ TEXT("Torso"), TEXT("Shared") }, TEXT("Idle"), FColor(200, 80, 40, 255)),
		FileA, OutputRoot, TEXT("MultiA"), nullptr);
	if (!Asset)
	{
		return false;
	}
	UPaper2DPlusCharacterLayerAsset* Merged = AseMulti_Import(*this, TEXT("B into A's asset"),
		AseMulti_File({ TEXT("Cape"), TEXT("Shared") }, TEXT("Walk"), FColor(40, 80, 200, 255)),
		FileB, OutputRoot, TEXT("MultiB"), Asset);
	UPaper2DPlusCharacterLayerAsset* MergedC = AseMulti_Import(*this, TEXT("C into A's asset"),
		AseMulti_File({ TEXT("Boots") }, TEXT("Run"), FColor(40, 200, 80, 255)),
		FileC, OutputRoot, TEXT("MultiC"), Asset);
	if (!TestTrue(TEXT("B merged into the same Layer Profile"), Merged == Asset)
		|| !TestTrue(TEXT("C merged into the same Layer Profile"), MergedC == Asset))
	{
		return false;
	}
	if (!TestEqual(TEXT("three sources are recorded"), Asset->ImportedAseSources.Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("the shared name is ONE row"), Asset->Layers.FilterByPredicate(
		[](const FCharacterLayer& Layer) { return Layer.LayerName == TEXT("Shared"); }).Num(), 1);

	// Materialized copies: a replay restamps (and can reallocate) ImportedAseSources.
	const FAsepriteSourceContext SourceA = Asset->ImportedAseSources[0];
	const FAsepriteSourceContext SourceC = Asset->ImportedAseSources[2];
	TestEqual(TEXT("A is the first recorded source"), SourceA.AssetPrefix, FString(TEXT("MultiA")));
	TestEqual(TEXT("C is the third recorded source"), SourceC.AssetPrefix, FString(TEXT("MultiC")));

	TArray<FString> OtherSources;
	const TArray<FString> SharedByA = FAsepriteImporter::CollectLayersSharedWithOtherSources(*Asset, SourceA, &OtherSources);
	TestEqual(TEXT("A shares exactly one layer"), SharedByA.Num(), 1);
	TestTrue(TEXT("the shared layer is 'Shared'"), SharedByA.Contains(TEXT("Shared")));
	TestEqual(TEXT("the sharing source is B alone"), OtherSources.Num(), 1);
	TestEqual(TEXT("C shares nothing"), FAsepriteImporter::CollectLayersSharedWithOtherSources(*Asset, SourceC).Num(), 0);

	// THE POINT: replaying A is refused and the asset is byte-for-byte what it was.
	AseMulti_ClearAllDirtyUnder(OutputRoot);
	const FString Before = AseMulti_Snapshot(*Asset);
	FAsepriteImportCostReport RefusedReport;
	const bool bReplayedA = FAsepriteImporter::ForceReimportLayerAssetSource(
		*Asset, SourceA, /*bForceFullReimport*/ true, &RefusedReport);
	TestFalse(TEXT("Force Full Reimport of a source with a shared layer is refused"), bReplayedA);
	TestTrue(TEXT("the refusal is recorded on the audit report"),
		RefusedReport.DecisionLines.ContainsByPredicate([](const FString& Line)
		{
			return Line.StartsWith(TEXT("refused")) && Line.Contains(TEXT("Shared"));
		}));
	TestEqual(TEXT("the asset is untouched by the refusal"), AseMulti_Snapshot(*Asset), Before);
	TestFalse(TEXT("the refusal dirtied nothing"), Asset->GetOutermost()->IsDirty());
	TestEqual(TEXT("the refusal wrote no sheet"), RefusedReport.SheetsWritten, 0);

	// And a source that shares nothing still replays — the guard is discriminating, and the replay's
	// own post-import dangling check passes on real art.
	FAsepriteImportCostReport ReplayReport;
	const bool bReplayedC = FAsepriteImporter::ForceReimportLayerAssetSource(
		*Asset, SourceC, /*bForceFullReimport*/ true, &ReplayReport);
	TestTrue(TEXT("Force Full Reimport of the unshared source replays"), bReplayedC);
	TestFalse(TEXT("the replay reports no dangling reference"),
		ReplayReport.DecisionLines.ContainsByPredicate([](const FString& Line)
		{
			return Line.StartsWith(TEXT("dangling"));
		}));
	TestEqual(TEXT("the replayed asset validates clean of dangling references"),
		Asset->ValidateSpriteReferences().Num(), 0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
#endif // WITH_EDITOR
