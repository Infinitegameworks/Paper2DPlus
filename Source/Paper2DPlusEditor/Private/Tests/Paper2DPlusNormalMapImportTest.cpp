// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "AsepriteImporter.h"
#include "PaperSprite.h"
#include "Engine/Texture2D.h"
#include "TextureCompiler.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

/**
 * Worldless tests for normal-map layer pairing on Aseprite import (TASK-72). Two surfaces are pinned:
 *   (1) the PURE pairing seam FAsepriteImporter::DeriveNormalMapPairings — convention detection (_n / _normal,
 *       case-insensitive, longest-suffix-first), pairing only when a base sibling exists, excluding paired normals
 *       from the visual set, and the byte-identical no-op when no normal layers are present; and
 *   (2) the texture/attach seam — CreateNormalMapSheetTexture produces a TC_Normalmap / SRGB-off /
 *       WorldNormalMap texture, and AttachNormalMapToSprites wires it into a sprite's AdditionalSourceTextures[0]
 *       (+ the null-safe lit material into DefaultMaterial).
 *
 * Helpers carry a FILE-UNIQUE PREFIX (NormalMapImport_*) because unity builds concatenate test .cpp files into one
 * TU — generic anon-namespace helper names collide (see CLAUDE.md UNITY-BUILD TEST-HELPER FILE-UNIQUE-NAME RULE).
 */
namespace Paper2DPlusNormalMapImportTestUtils
{
	FString NormalMapImport_TempDir()
	{
		return FString::Printf(TEXT("/Temp/P2DPNormalMapTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	}

	// A single solid WxH FColor buffer.
	TArray<FColor> NormalMapImport_Solid(int32 W, int32 H, FColor C)
	{
		TArray<FColor> P; P.Init(C, W * H); return P;
	}

	// Materialise one WxH buffer into a real UPaperSprite (its own 1-cell sheet) — the importer's own pipeline.
	UPaperSprite* NormalMapImport_MakeSprite(const TArray<FColor>& Pixels, int32 W, int32 H, const FString& Dir, const FString& Name)
	{
		TArray<TArray<FColor>> Frames; Frames.Add(Pixels);
		UTexture2D* Tex = FAsepriteImporter::CreatePerLayerSpriteSheetTexture(Frames, W, H, Dir, Name + TEXT("_Sheet"));
		if (!Tex) { return nullptr; }
		TArray<UPaperSprite*> Sprites = FAsepriteImporter::CreateSpritesFromSheet(Tex, W, H, 1, Dir, Name);
		return Sprites.Num() > 0 ? Sprites[0] : nullptr;
	}
}
namespace Paper2DPlusNormalMapImportTestUtils
{

// ─── Pure pairing: detects _n and _normal, case-insensitive, pairs only when a base exists ───────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNormalMapPairBasic,
	"Paper2DPlus.NormalMapImport.PairsByConvention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNormalMapPairBasic::RunTest(const FString& Parameters)
{
	// body(0)/body_n(1) pair; head(2)/Head_Normal(3) pair (case-insensitive). "armor_n"(4) has NO base => unpaired.
	const TArray<FString> Names = { TEXT("body"), TEXT("body_n"), TEXT("head"), TEXT("Head_Normal"), TEXT("armor_n") };
	const TArray<FString> Suffixes = { TEXT("_normal"), TEXT("_n") };

	TMap<int32, int32> BaseToNormal;
	TSet<int32> PairedNormals;
	FAsepriteImporter::DeriveNormalMapPairings(Names, Suffixes, BaseToNormal, PairedNormals);

	TestEqual(TEXT("two pairings formed"), BaseToNormal.Num(), 2);
	if (const int32* N = BaseToNormal.Find(0)) { TestEqual(TEXT("body -> body_n"), *N, 1); } else { AddError(TEXT("body unpaired")); }
	if (const int32* N = BaseToNormal.Find(2)) { TestEqual(TEXT("head -> Head_Normal (case-insensitive)"), *N, 3); } else { AddError(TEXT("head unpaired")); }

	TestTrue(TEXT("body_n consumed as normal"), PairedNormals.Contains(1));
	TestTrue(TEXT("Head_Normal consumed as normal"), PairedNormals.Contains(3));
	// armor_n matched a suffix but has no "armor" base sibling -> NOT consumed, stays a visual layer.
	TestFalse(TEXT("armor_n (no base) NOT consumed"), PairedNormals.Contains(4));
	// Bases are never consumed as normals.
	TestFalse(TEXT("body base not in paired set"), PairedNormals.Contains(0));
	TestFalse(TEXT("head base not in paired set"), PairedNormals.Contains(2));
	return true;
}

// ─── Pure pairing: longest suffix wins so "_normal" strips correctly over "_n" ───────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNormalMapLongestSuffixWins,
	"Paper2DPlus.NormalMapImport.LongestSuffixWins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNormalMapLongestSuffixWins::RunTest(const FString& Parameters)
{
	// "torso_normal" ends in both "_n"... no, only "_normal" and not "_n" here; the discriminating case is a layer
	// that ends in "_normal": stripping "_n" would yield "torso_norma" (no base), stripping "_normal" yields "torso"
	// (base exists). Longest-first must pick "_normal".
	const TArray<FString> Names = { TEXT("torso"), TEXT("torso_normal") };
	const TArray<FString> Suffixes = { TEXT("_n"), TEXT("_normal") }; // intentionally listed shortest-first

	TMap<int32, int32> BaseToNormal;
	TSet<int32> PairedNormals;
	FAsepriteImporter::DeriveNormalMapPairings(Names, Suffixes, BaseToNormal, PairedNormals);

	TestEqual(TEXT("one pairing"), BaseToNormal.Num(), 1);
	if (const int32* N = BaseToNormal.Find(0)) { TestEqual(TEXT("torso -> torso_normal"), *N, 1); } else { AddError(TEXT("torso unpaired - _n likely beat _normal")); }
	TestTrue(TEXT("torso_normal consumed"), PairedNormals.Contains(1));
	return true;
}

// ─── Pure pairing: byte-identical no-op when there are no normal layers ───────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNormalMapNoNormalsNoOp,
	"Paper2DPlus.NormalMapImport.NoNormalsIsNoOp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNormalMapNoNormalsNoOp::RunTest(const FString& Parameters)
{
	const TArray<FString> Names = { TEXT("body"), TEXT("head"), TEXT("arm") };
	const TArray<FString> Suffixes = { TEXT("_normal"), TEXT("_n") };

	TMap<int32, int32> BaseToNormal;
	TSet<int32> PairedNormals;
	FAsepriteImporter::DeriveNormalMapPairings(Names, Suffixes, BaseToNormal, PairedNormals);

	TestEqual(TEXT("no pairings"), BaseToNormal.Num(), 0);
	TestEqual(TEXT("no consumed normals"), PairedNormals.Num(), 0);

	// Also: empty suffix list and a lone "_n" (no base) are both no-ops.
	TMap<int32, int32> B2; TSet<int32> P2;
	FAsepriteImporter::DeriveNormalMapPairings(Names, {}, B2, P2);
	TestEqual(TEXT("empty suffix list -> no pairings"), B2.Num(), 0);

	const TArray<FString> LoneNormal = { TEXT("_n") }; // matches suffix but strips to empty base
	TMap<int32, int32> B3; TSet<int32> P3;
	FAsepriteImporter::DeriveNormalMapPairings(LoneNormal, Suffixes, B3, P3);
	TestEqual(TEXT("lone '_n' (empty base) -> no pairings"), B3.Num(), 0);
	TestEqual(TEXT("lone '_n' not consumed"), P3.Num(), 0);
	return true;
}

// ─── Texture/attach seam: a paired normal yields a TC_Normalmap texture on the sprite's AdditionalSourceTextures ───

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNormalMapAttachToSprite,
	"Paper2DPlus.NormalMapImport.NormalTextureAttachedToSprite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNormalMapAttachToSprite::RunTest(const FString& Parameters)
{
	const int32 W = 4, H = 4;
	const FString Dir = NormalMapImport_TempDir();

	// A base sprite + a separate normal sheet built from a normal-layer buffer (flat-blue tangent normal).
	UPaperSprite* BaseSprite = NormalMapImport_MakeSprite(NormalMapImport_Solid(W, H, FColor(200, 100, 50, 255)), W, H, Dir, TEXT("base"));
	if (!TestNotNull(TEXT("base sprite materialised"), BaseSprite)) { return false; }
	// AdditionalSourceTextures is protected on UPaperSprite; read it through the public baked getter (typedef
	// FAdditionalSpriteTextureArray = TArray<UTexture*>). A fresh sprite has none.
	{ FAdditionalSpriteTextureArray Baked0; BaseSprite->GetBakedAdditionalSourceTextures(Baked0); TestEqual(TEXT("base sprite starts with no additional textures"), Baked0.Num(), 0); }

	TArray<TArray<FColor>> NormalFrames; NormalFrames.Add(NormalMapImport_Solid(W, H, FColor(128, 128, 255, 255)));
	UTexture2D* NormalTex = FAsepriteImporter::CreateNormalMapSheetTexture(NormalFrames, W, H, Dir, TEXT("base_Sheet_N"));
	if (!TestNotNull(TEXT("normal texture created"), NormalTex)) { return false; }
	// Force the real async build to complete while the test is active. This makes any source-gamma/settings race a
	// deterministic regression instead of a background-worker crash that depends on DDC warmth and suite timing.
	FTextureCompilingManager::Get().FinishCompilation({ NormalTex });

	// Normal-map texture settings.
	TestEqual(TEXT("CompressionSettings == TC_Normalmap"), (int32)NormalTex->CompressionSettings.GetValue(), (int32)TC_Normalmap);
	TestFalse(TEXT("SRGB disabled on normal map"), NormalTex->SRGB != 0);
	TestEqual(TEXT("LODGroup == TEXTUREGROUP_WorldNormalMap"), (int32)NormalTex->LODGroup.GetValue(), (int32)TEXTUREGROUP_WorldNormalMap);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
	TestEqual(TEXT("source gamma remains linear through compilation"),
		static_cast<int32>(NormalTex->Source.GetGammaSpace(0)),
		static_cast<int32>(EGammaSpace::Linear));
#endif

	// Attach with a null-safe lit material (transient material, nothing staged).
	UMaterialInterface* LitMat = NewObject<UMaterial>(GetTransientPackage());
	TArray<UPaperSprite*> Sprites; Sprites.Add(BaseSprite);
	int32 Touched = 0;
	FAsepriteImporter::AttachNormalMapToSprites(Sprites, NormalTex, LitMat, Touched);

	TestEqual(TEXT("one sprite touched"), Touched, 1);
	{
		FAdditionalSpriteTextureArray Baked; BaseSprite->GetBakedAdditionalSourceTextures(Baked);
		if (TestEqual(TEXT("AdditionalSourceTextures has one entry"), Baked.Num(), 1))
		{
			TestEqual(TEXT("slot 0 is the normal texture"), (UTexture*)Baked[0], (UTexture*)NormalTex);
		}
	}
	TestEqual(TEXT("lit material assigned as DefaultMaterial"), BaseSprite->GetDefaultMaterial(), LitMat);

	// Re-attach must NOT accumulate a second entry — InitializeSprite REPLACES the additional-texture list each call.
	FAsepriteImporter::AttachNormalMapToSprites(Sprites, NormalTex, LitMat, Touched);
	{ FAdditionalSpriteTextureArray BakedAgain; BaseSprite->GetBakedAdditionalSourceTextures(BakedAgain); TestEqual(TEXT("re-attach keeps a single slot entry"), BakedAgain.Num(), 1); }
	return true;
}

// ─── Texture/attach seam: a null lit material still attaches the normal texture (no crash, slot set) ──

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNormalMapNullMaterialStillAttaches,
	"Paper2DPlus.NormalMapImport.NullLitMaterialStillAttaches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNormalMapNullMaterialStillAttaches::RunTest(const FString& Parameters)
{
	const int32 W = 4, H = 4;
	const FString Dir = NormalMapImport_TempDir();

	UPaperSprite* BaseSprite = NormalMapImport_MakeSprite(NormalMapImport_Solid(W, H, FColor::White), W, H, Dir, TEXT("base"));
	if (!TestNotNull(TEXT("base sprite materialised"), BaseSprite)) { return false; }
	UMaterialInterface* StockMaterial = BaseSprite->GetDefaultMaterial();

	TArray<TArray<FColor>> NormalFrames; NormalFrames.Add(NormalMapImport_Solid(W, H, FColor(128, 128, 255, 255)));
	UTexture2D* NormalTex = FAsepriteImporter::CreateNormalMapSheetTexture(NormalFrames, W, H, Dir, TEXT("base_Sheet_N"));
	if (!TestNotNull(TEXT("normal texture created"), NormalTex)) { return false; }
	FTextureCompilingManager::Get().FinishCompilation({ NormalTex });

	TArray<UPaperSprite*> Sprites; Sprites.Add(BaseSprite);
	int32 Touched = 0;
	FAsepriteImporter::AttachNormalMapToSprites(Sprites, NormalTex, /*LitMaterial*/ nullptr, Touched);

	TestEqual(TEXT("sprite touched"), Touched, 1);
	{
		FAdditionalSpriteTextureArray BakedNull; BaseSprite->GetBakedAdditionalSourceTextures(BakedNull);
		if (TestEqual(TEXT("normal texture still attached when material null"), BakedNull.Num(), 1))
		{
			TestEqual(TEXT("slot 0 is the normal texture"), (UTexture*)BakedNull[0], (UTexture*)NormalTex);
		}
	}
	// Null lit material must NOT overwrite the sprite's stock default material.
	TestEqual(TEXT("DefaultMaterial unchanged when lit material null"), BaseSprite->GetDefaultMaterial(), StockMaterial);
	return true;
}

// Reimport/reuse seam: replacing one named texture must retire its previous async build first.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNormalMapRapidReuse,
	"Paper2DPlus.NormalMapImport.RapidNamedTextureReuseIsFenced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNormalMapRapidReuse::RunTest(const FString& Parameters)
{
	const FString Dir = NormalMapImport_TempDir();
	TArray<TArray<FColor>> FirstFrames;
	FirstFrames.Add(NormalMapImport_Solid(1, 1, FColor(128, 128, 255, 255)));
	UTexture2D* FirstTexture = FAsepriteImporter::CreateNormalMapSheetTexture(
		FirstFrames, 1, 1, Dir, TEXT("rapid_reuse_N"));
	if (!TestNotNull(TEXT("first named normal texture is created"), FirstTexture))
	{
		return false;
	}

	// Deliberately do not finish FirstTexture here. The second import must fence the outstanding
	// build before replacing platform/source data on the same UObject.
	TArray<TArray<FColor>> ReplacementFrames;
	ReplacementFrames.Add(NormalMapImport_Solid(2, 1, FColor(64, 192, 255, 255)));
	UTexture2D* ReusedTexture = FAsepriteImporter::CreateNormalMapSheetTexture(
		ReplacementFrames, 2, 1, Dir, TEXT("rapid_reuse_N"));
	if (!TestNotNull(TEXT("second named normal texture is created"), ReusedTexture))
	{
		return false;
	}
	TestTrue(TEXT("same package/name reuses the existing texture object"),
		ReusedTexture == FirstTexture);
	FTextureCompilingManager::Get().FinishCompilation({ ReusedTexture });
	TestEqual(TEXT("reused texture exposes the replacement source width"),
		static_cast<int32>(ReusedTexture->Source.GetSizeX()), 2);
	TestEqual(TEXT("reused texture exposes the replacement source height"),
		static_cast<int32>(ReusedTexture->Source.GetSizeY()), 1);
	TestEqual(TEXT("reused texture remains a normal map"),
		static_cast<int32>(ReusedTexture->CompressionSettings.GetValue()),
		static_cast<int32>(TC_Normalmap));
	TestFalse(TEXT("reused normal map remains linear"), ReusedTexture->SRGB != 0);
	return true;
}

// ─── Enablement prune: a disabled base releases its (enabled) normal back to the visual set ───────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusNormalMapDisabledBaseReleasesNormal,
	"Paper2DPlus.NormalMapImport.DisabledBaseReleasesNormal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusNormalMapDisabledBaseReleasesNormal::RunTest(const FString& Parameters)
{
	// body(0)/body_n(1) and head(2)/head_n(3) both paired. The user imports body but DISABLES head. head_n must be
	// released back to the visual set (not silently dropped); body's pairing is untouched.
	TMap<int32, int32> BaseToNormal; BaseToNormal.Add(0, 1); BaseToNormal.Add(2, 3);
	TSet<int32> PairedNormals; PairedNormals.Add(1); PairedNormals.Add(3);
	TSet<int32> EnabledBases; EnabledBases.Add(0); EnabledBases.Add(1); EnabledBases.Add(3); // base 2 (head) NOT enabled

	FAsepriteImporter::PruneNormalPairingsToEnabledBases(BaseToNormal, PairedNormals, EnabledBases);

	TestEqual(TEXT("only the enabled-base pairing remains"), BaseToNormal.Num(), 1);
	TestTrue(TEXT("body->body_n kept"), BaseToNormal.Contains(0));
	TestFalse(TEXT("head->head_n dropped (base disabled)"), BaseToNormal.Contains(2));
	TestTrue(TEXT("body_n still consumed as a normal"), PairedNormals.Contains(1));
	TestFalse(TEXT("head_n released back to the visual set"), PairedNormals.Contains(3));
	return true;
}

} // namespace Paper2DPlusNormalMapImportTestUtils

#endif // WITH_EDITOR
