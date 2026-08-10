// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfileRelationshipService.h"

#include "AssetToolsModule.h"
#include "CharacterLayerAssetFactory.h"
#include "CombatProfileAssetFactory.h"
#include "Editor.h"
#include "EffectProfileAssetFactory.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusProfileRelationshipService"

namespace
{
	bool RelationshipPathLess(const FSoftObjectPath& A, const FSoftObjectPath& B)
	{
		return A.ToString().Compare(B.ToString(), ESearchCase::IgnoreCase) < 0;
	}

	bool RelationshipAssetDataIsClass(const FAssetData& AssetData, const UClass* Class)
	{
		if (!Class)
		{
			return false;
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		return AssetData.AssetClass == Class->GetFName();
#else
		return AssetData.AssetClassPath == Class->GetClassPathName();
#endif
	}

	bool LegacyAssetsContainKind(
		const FPaper2DPlusProfileRelationshipIndex& Index,
		EPaper2DPlusCatalogCompanion Companion)
	{
		for (const FAssetData& LegacyAsset : Index.LegacyAssets)
		{
			EPaper2DPlusCatalogCompanion LegacyKind = EPaper2DPlusCatalogCompanion::Layer;
			if (FProfileRelationshipService::GetCompanionKind(LegacyAsset, LegacyKind)
				&& LegacyKind == Companion)
			{
				return true;
			}
		}
		return false;
	}
}

const TArray<FPaper2DPlusProfileRelationshipCandidate>& FPaper2DPlusProfileRelationshipIndex::GetCandidates(
	EPaper2DPlusCatalogCompanion Companion) const
{
	if (Companion == EPaper2DPlusCatalogCompanion::Layer)
	{
		return LayerCandidates;
	}
	if (Companion == EPaper2DPlusCatalogCompanion::Combat)
	{
		return CombatCandidates;
	}
	static const TArray<FPaper2DPlusProfileRelationshipCandidate> Empty;
	return Empty;
}

const FPaper2DPlusProfileRelationshipCandidate* FPaper2DPlusProfileRelationshipIndex::FindByAssetPath(
	EPaper2DPlusCatalogCompanion Companion,
	const FSoftObjectPath& AssetPath) const
{
	const FString WantedPath = FProfileRelationshipService::NormalizeObjectPath(AssetPath);
	if (WantedPath.IsEmpty())
	{
		return nullptr;
	}
	return GetCandidates(Companion).FindByPredicate(
		[&WantedPath](const FPaper2DPlusProfileRelationshipCandidate& Candidate)
		{
			return FProfileRelationshipService::NormalizeObjectPath(Candidate.AssetPath) == WantedPath;
		});
}

void FPaper2DPlusProfileRelationshipIndex::AddCandidate(
	FPaper2DPlusProfileRelationshipCandidate Candidate)
{
	if (Candidate.AssetPath.IsNull())
	{
		return;
	}
	TArray<FPaper2DPlusProfileRelationshipCandidate>* Target = nullptr;
	if (Candidate.Companion == EPaper2DPlusCatalogCompanion::Layer)
	{
		Target = &LayerCandidates;
	}
	else if (Candidate.Companion == EPaper2DPlusCatalogCompanion::Combat)
	{
		Target = &CombatCandidates;
	}
	if (!Target)
	{
		return;
	}

	const FString CandidatePath = FProfileRelationshipService::NormalizeObjectPath(Candidate.AssetPath);
	if (FPaper2DPlusProfileRelationshipCandidate* Existing = Target->FindByPredicate(
		[&CandidatePath](const FPaper2DPlusProfileRelationshipCandidate& Item)
		{
			return FProfileRelationshipService::NormalizeObjectPath(Item.AssetPath) == CandidatePath;
		}))
	{
		// An explicit legacy inspection supersedes the missing-tag placeholder, never the reverse.
		if (Candidate.bFromLegacyInspection || !Existing->bFromLegacyInspection)
		{
			*Existing = MoveTemp(Candidate);
		}
		return;
	}
	Target->Add(MoveTemp(Candidate));
}

void FPaper2DPlusProfileRelationshipIndex::SortDeterministically()
{
	auto SortCandidates = [](TArray<FPaper2DPlusProfileRelationshipCandidate>& Candidates)
	{
		Candidates.Sort([](
			const FPaper2DPlusProfileRelationshipCandidate& A,
			const FPaper2DPlusProfileRelationshipCandidate& B)
		{
			return RelationshipPathLess(A.AssetPath, B.AssetPath);
		});
	};
	SortCandidates(LayerCandidates);
	SortCandidates(CombatCandidates);
	LegacyAssets.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return RelationshipPathLess(
			FProfileRelationshipService::GetAssetObjectPath(A),
			FProfileRelationshipService::GetAssetObjectPath(B));
	});
}

const FName& FProfileRelationshipService::CharacterProfileRelationshipTag()
{
	static const FName Tag(TEXT("Paper2DPlus.CharacterProfile"));
	return Tag;
}

FString FProfileRelationshipService::NormalizeObjectPath(const FSoftObjectPath& Path)
{
	FString Normalized = Path.ToString();
	Normalized.TrimStartAndEndInline();
	Normalized.ToLowerInline();
	return Normalized;
}

FSoftObjectPath FProfileRelationshipService::GetAssetObjectPath(const FAssetData& AssetData)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	return FSoftObjectPath(AssetData.ObjectPath.ToString());
#else
	return AssetData.GetSoftObjectPath();
#endif
}

bool FProfileRelationshipService::GetCompanionKind(
	const FAssetData& AssetData,
	EPaper2DPlusCatalogCompanion& OutCompanion)
{
	if (RelationshipAssetDataIsClass(AssetData, UPaper2DPlusCharacterLayerAsset::StaticClass()))
	{
		OutCompanion = EPaper2DPlusCatalogCompanion::Layer;
		return true;
	}
	if (RelationshipAssetDataIsClass(AssetData, UPaper2DPlusCombatProfileAsset::StaticClass()))
	{
		OutCompanion = EPaper2DPlusCatalogCompanion::Combat;
		return true;
	}
	return false;
}

FSoftObjectPath FProfileRelationshipService::ExtractRelationshipPath(
	const FAssetData& AssetData,
	bool& bOutTagPresent)
{
	bOutTagPresent = false;
	FString Value;
	if (!AssetData.GetTagValue(CharacterProfileRelationshipTag(), Value))
	{
		return FSoftObjectPath();
	}

	bOutTagPresent = true;
	Value.TrimStartAndEndInline();
	if (Value.IsEmpty() || Value.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		return FSoftObjectPath();
	}
	const FString ObjectPath = FPackageName::ExportTextPathToObjectPath(Value);
	return FSoftObjectPath(ObjectPath.IsEmpty() ? Value : ObjectPath);
}

FPaper2DPlusProfileRelationshipIndex FProfileRelationshipService::BuildCandidateIndex(
	const TArray<FAssetData>& Assets)
{
	FPaper2DPlusProfileRelationshipIndex Index;
	for (const FAssetData& AssetData : Assets)
	{
		EPaper2DPlusCatalogCompanion Companion = EPaper2DPlusCatalogCompanion::Layer;
		if (!GetCompanionKind(AssetData, Companion))
		{
			continue;
		}

		bool bTagPresent = false;
		const FSoftObjectPath RelationshipPath = ExtractRelationshipPath(AssetData, bTagPresent);
		if (!bTagPresent)
		{
			Index.LegacyAssets.Add(AssetData);
			continue;
		}

		FPaper2DPlusProfileRelationshipCandidate Candidate;
		Candidate.Companion = Companion;
		Candidate.AssetData = AssetData;
		Candidate.AssetPath = GetAssetObjectPath(AssetData);
		Candidate.CharacterProfilePath = RelationshipPath;
		Index.AddCandidate(MoveTemp(Candidate));
	}
	Index.SortDeterministically();
	return Index;
}

int32 FProfileRelationshipService::InspectLegacyRelationships(
	FPaper2DPlusProfileRelationshipIndex& Index,
	const FExplicitAssetLoader& Loader)
{
	int32 LoadedCount = 0;
	const TArray<FAssetData> LegacyAssets = Index.LegacyAssets;
	TArray<FAssetData> UnresolvedLegacyAssets;
	Index.LegacyAssets.Reset();

	for (const FAssetData& AssetData : LegacyAssets)
	{
		EPaper2DPlusCatalogCompanion Companion = EPaper2DPlusCatalogCompanion::Layer;
		if (!GetCompanionKind(AssetData, Companion))
		{
			continue;
		}

		UObject* Object = Loader ? Loader(AssetData) : AssetData.GetAsset();
		++LoadedCount;
		FSoftObjectPath RelationshipPath;
		if (const UPaper2DPlusCharacterLayerAsset* Layer = Cast<UPaper2DPlusCharacterLayerAsset>(Object))
		{
			RelationshipPath = Layer->BaseProfile.ToSoftObjectPath();
		}
		else if (const UPaper2DPlusCombatProfileAsset* Combat = Cast<UPaper2DPlusCombatProfileAsset>(Object))
		{
			RelationshipPath = Combat->CharacterProfile
				? FSoftObjectPath(Combat->CharacterProfile.Get())
				: FSoftObjectPath();
		}
		else
		{
			// A failed compatibility load is still unknown. Never convert load failure into an empty
			// relationship that could clear an Automatic assignment.
			UnresolvedLegacyAssets.Add(AssetData);
			continue;
		}

		FPaper2DPlusProfileRelationshipCandidate Candidate;
		Candidate.Companion = Companion;
		Candidate.AssetData = AssetData;
		Candidate.AssetPath = GetAssetObjectPath(AssetData);
		Candidate.CharacterProfilePath = RelationshipPath;
		Candidate.bFromLegacyInspection = true;
		Index.AddCandidate(MoveTemp(Candidate));
	}

	Index.LegacyAssets = MoveTemp(UnresolvedLegacyAssets);
	Index.SortDeterministically();
	return LoadedCount;
}

namespace
{
	void GatherInwardCandidatePaths(
		EPaper2DPlusCatalogCompanion Companion,
		const FSoftObjectPath& CharacterProfilePath,
		const FPaper2DPlusProfileRelationshipIndex& Index,
		FPaper2DPlusProfileRelationshipResolution& Result)
	{
		const FString WantedCharacter =
			FProfileRelationshipService::NormalizeObjectPath(CharacterProfilePath);
		for (const FPaper2DPlusProfileRelationshipCandidate& Candidate : Index.GetCandidates(Companion))
		{
			if (!WantedCharacter.IsEmpty()
				&& FProfileRelationshipService::NormalizeObjectPath(Candidate.CharacterProfilePath)
					== WantedCharacter)
			{
				Result.CandidatePaths.Add(Candidate.AssetPath);
			}
		}
		Result.CandidatePaths.Sort(RelationshipPathLess);
	}
}

FPaper2DPlusProfileRelationshipResolution FProfileRelationshipService::ResolveAssigned(
	EPaper2DPlusCatalogCompanion Companion,
	const FSoftObjectPath& CharacterProfilePath,
	const FSoftObjectPath& AssignedAssetPath,
	const FPaper2DPlusProfileRelationshipIndex& Index)
{
	FPaper2DPlusProfileRelationshipResolution Result;
	Result.AssignedAssetPath = AssignedAssetPath;
	Result.bManualAssignmentPreserved = true;

	if (Companion != EPaper2DPlusCatalogCompanion::Layer
		&& Companion != EPaper2DPlusCatalogCompanion::Combat)
	{
		return Result;
	}
	GatherInwardCandidatePaths(Companion, CharacterProfilePath, Index, Result);

	if (AssignedAssetPath.IsNull())
	{
		Result.State = EPaper2DPlusProfileRelationshipState::None;
		return Result;
	}

	if (const FPaper2DPlusProfileRelationshipCandidate* Assigned =
		Index.FindByAssetPath(Companion, AssignedAssetPath))
	{
		// An unset inward link still publishes the tag (as the literal "None"), so it arrives here as a
		// candidate with an EMPTY path. That is "not linked yet", not "linked to someone else", and
		// only the latter is a contradiction worth an Error.
		if (Assigned->CharacterProfilePath.IsNull())
		{
			Result.State = EPaper2DPlusProfileRelationshipState::ManualUnlinked;
			return Result;
		}
		Result.State = NormalizeObjectPath(Assigned->CharacterProfilePath)
				== NormalizeObjectPath(CharacterProfilePath)
			? EPaper2DPlusProfileRelationshipState::ManualValid
			: EPaper2DPlusProfileRelationshipState::ManualMismatch;
		return Result;
	}

	for (const FAssetData& LegacyAsset : Index.LegacyAssets)
	{
		if (NormalizeObjectPath(GetAssetObjectPath(LegacyAsset)) == NormalizeObjectPath(AssignedAssetPath))
		{
			Result.State = EPaper2DPlusProfileRelationshipState::LegacyUnknown;
			return Result;
		}
	}
	Result.State = EPaper2DPlusProfileRelationshipState::MissingAssignedAsset;
	return Result;
}

FPaper2DPlusProfileRelationshipResolution FProfileRelationshipService::SuggestCandidate(
	EPaper2DPlusCatalogCompanion Companion,
	const FSoftObjectPath& CharacterProfilePath,
	const FPaper2DPlusProfileRelationshipIndex& Index)
{
	FPaper2DPlusProfileRelationshipResolution Result;

	if (Companion != EPaper2DPlusCatalogCompanion::Layer
		&& Companion != EPaper2DPlusCatalogCompanion::Combat)
	{
		return Result;
	}
	GatherInwardCandidatePaths(Companion, CharacterProfilePath, Index, Result);

	if (Result.CandidatePaths.Num() > 1)
	{
		Result.State = EPaper2DPlusProfileRelationshipState::Ambiguous;
		return Result;
	}
	if (LegacyAssetsContainKind(Index, Companion))
	{
		// Even one known match is not safely unique while an untagged legacy asset might be a second.
		Result.State = EPaper2DPlusProfileRelationshipState::LegacyUnknown;
		return Result;
	}
	if (Result.CandidatePaths.Num() == 1)
	{
		Result.State = EPaper2DPlusProfileRelationshipState::Unique;
		Result.SuggestedAssetPath = Result.CandidatePaths[0];
		return Result;
	}
	Result.State = EPaper2DPlusProfileRelationshipState::None;
	return Result;
}

bool FProfileRelationshipService::OpenRelatedAsset(const FSoftObjectPath& AssetPath, FText& OutError)
{
	OutError = FText::GetEmpty();
	if (AssetPath.IsNull())
	{
		OutError = LOCTEXT("OpenMissingPath", "Choose a related asset before opening it.");
		return false;
	}
	UObject* Asset = AssetPath.TryLoad();
	if (!Asset)
	{
		OutError = FText::Format(
			LOCTEXT("OpenLoadFailed", "The related asset '{0}' could not be loaded."),
			FText::FromString(AssetPath.ToString()));
		return false;
	}
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor
		? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()
		: nullptr;
	if (!AssetEditorSubsystem || !AssetEditorSubsystem->OpenEditorForAsset(Asset))
	{
		OutError = LOCTEXT("OpenEditorFailed", "Unreal could not open the related asset editor.");
		return false;
	}
	return true;
}

UObject* FProfileRelationshipService::CreateRelatedAsset(
	EPaper2DPlusCatalogCompanion Companion,
	const FSoftObjectPath& CharacterProfilePath,
	const FString& DestinationPackagePath,
	const FString& DesiredAssetName,
	FText& OutError)
{
	OutError = FText::GetEmpty();
	FString PackagePath = DestinationPackagePath.TrimStartAndEnd();
	while (PackagePath.EndsWith(TEXT("/")))
	{
		PackagePath.LeftChopInline(1);
	}
	const FString AssetName = ObjectTools::SanitizeObjectName(DesiredAssetName.TrimStartAndEnd());
	if (CharacterProfilePath.IsNull() || AssetName.IsEmpty())
	{
		OutError = LOCTEXT("CreateMissingInput", "Choose a Character Profile, package path, and asset name.");
		return nullptr;
	}
	if (PackagePath != TEXT("/Game") && !PackagePath.StartsWith(TEXT("/Game/")))
	{
		OutError = LOCTEXT("CreateOutsideGame", "Related assets must be created under /Game.");
		return nullptr;
	}
	FText PathReason;
	const FString RequestedPackage = PackagePath + TEXT("/") + AssetName;
	if (!FPackageName::IsValidLongPackageName(RequestedPackage, false, &PathReason))
	{
		OutError = FText::Format(LOCTEXT("CreateInvalidPath", "The related asset path is invalid: {0}"), PathReason);
		return nullptr;
	}

	UClass* AssetClass = nullptr;
	UFactory* Factory = nullptr;
	UPaper2DPlusCharacterProfileAsset* LoadedCharacter = nullptr;
	if (Companion == EPaper2DPlusCatalogCompanion::Layer)
	{
		AssetClass = UPaper2DPlusCharacterLayerAsset::StaticClass();
		Factory = NewObject<UCharacterLayerAssetFactory>();
	}
	else if (Companion == EPaper2DPlusCatalogCompanion::Combat)
	{
		LoadedCharacter = Cast<UPaper2DPlusCharacterProfileAsset>(CharacterProfilePath.TryLoad());
		if (!LoadedCharacter)
		{
			OutError = LOCTEXT("CreateCombatCharacterLoadFailed", "The Character Profile could not be loaded for the new Combat Profile.");
			return nullptr;
		}
		AssetClass = UPaper2DPlusCombatProfileAsset::StaticClass();
		Factory = NewObject<UCombatProfileAssetFactory>();
	}
	else if (Companion == EPaper2DPlusCatalogCompanion::Effect)
	{
		AssetClass = UPaper2DPlusEffectProfileAsset::StaticClass();
		Factory = NewObject<UEffectProfileAssetFactory>();
	}
	else
	{
		OutError = LOCTEXT("CreateUnsupportedCompanion", "That related asset type is not supported.");
		return nullptr;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	FString UniquePackageName;
	FString UniqueAssetName;
	AssetTools.CreateUniqueAssetName(RequestedPackage, FString(), UniquePackageName, UniqueAssetName);
	UObject* Created = AssetTools.CreateAsset(
		UniqueAssetName,
		FPackageName::GetLongPackagePath(UniquePackageName),
		AssetClass,
		Factory);
	if (!Created)
	{
		OutError = LOCTEXT("CreateFailed", "Unreal could not create the related asset.");
		return nullptr;
	}

	if (UPaper2DPlusCharacterLayerAsset* Layer = Cast<UPaper2DPlusCharacterLayerAsset>(Created))
	{
		Layer->BaseProfile = TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>(CharacterProfilePath);
	}
	else if (UPaper2DPlusCombatProfileAsset* Combat = Cast<UPaper2DPlusCombatProfileAsset>(Created))
	{
		Combat->CharacterProfile = LoadedCharacter;
	}
	// Deliberately no Effect write: the Catalog row owns Character-to-Effect assignment.
	Created->MarkPackageDirty();
	return Created;
}

#undef LOCTEXT_NAMESPACE
