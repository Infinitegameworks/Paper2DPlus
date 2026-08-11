// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "EffectProfileContextResolver.h"

#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusEffectLibraryIndex.h"
#include "Paper2DPlusSettings.h"
#include "PaperFlipbook.h"

#define LOCTEXT_NAMESPACE "EffectProfileContextResolver"

namespace EffectProfileContextResolverPrivate
{
	FString Normalize(const FSoftObjectPath& Path)
	{
		FString Result = Path.ToString();
		Result.TrimStartAndEndInline();
		Result.ToLowerInline();
		return Result;
	}

	FSoftObjectPath CanonicalEffectPath(const FSoftObjectPath& Path)
	{
		return UPaper2DPlusEffectProfileAsset::GetCanonicalEffectFlipbookPathNoLoad(Path);
	}

	FString NormalizeEffect(const FSoftObjectPath& Path)
	{
		return Normalize(CanonicalEffectPath(Path));
	}
}

bool FPaper2DPlusEffectProfileContext::IsAllowed(const FSoftObjectPath& Path) const
{
	if (!bChoicesScoped) return true;
	if (Path.IsNull() || !IsReady()) return false;
	const FString Key = EffectProfileContextResolverPrivate::NormalizeEffect(Path);
	return AllowedFlipbooks.ContainsByPredicate([&Key](const FSoftObjectPath& Candidate)
	{
		return EffectProfileContextResolverPrivate::NormalizeEffect(Candidate) == Key;
	});
}

bool FPaper2DPlusEffectProfileContext::IsOutOfLibrary(const FSoftObjectPath& ExistingValue) const
{
	return bChoicesScoped && !ExistingValue.IsNull() && !IsAllowed(ExistingValue);
}

FEffectProfileContextResolver::FEffectProfileContextResolver(
	UPaper2DPlusCharacterProfileAsset* InCharacter,
	TSharedPtr<FPaper2DPlusEffectLibraryIndex> InLibraryIndex)
	: Character(InCharacter)
	, LibraryIndex(InLibraryIndex.IsValid()
		? MoveTemp(InLibraryIndex)
		: FPaper2DPlusEffectLibraryIndex::Get())
{
	CachedContext.CharacterPath = FSoftObjectPath(InCharacter);
	BindDelegates();
}

FEffectProfileContextResolver::~FEffectProfileContextResolver()
{
	UnbindDelegates();
}

void FEffectProfileContextResolver::BindDelegates()
{
	SettingsChangedHandle = UPaper2DPlusSettings::OnCharacterCatalogSettingsChanged().AddRaw(
		this, &FEffectProfileContextResolver::Invalidate);
	if (LibraryIndex.IsValid())
	{
		LibraryInvalidatedHandle = LibraryIndex->OnInvalidated().AddRaw(
			this, &FEffectProfileContextResolver::HandleLibraryInvalidated);
	}
}

void FEffectProfileContextResolver::UnbindDelegates()
{
	if (SettingsChangedHandle.IsValid())
	{
		UPaper2DPlusSettings::OnCharacterCatalogSettingsChanged().Remove(SettingsChangedHandle);
	}
	if (LibraryIndex.IsValid() && LibraryInvalidatedHandle.IsValid())
	{
		LibraryIndex->OnInvalidated().Remove(LibraryInvalidatedHandle);
	}
}

void FEffectProfileContextResolver::SetCharacter(
	UPaper2DPlusCharacterProfileAsset* InCharacter)
{
	if (Character.Get() == InCharacter)
	{
		return;
	}
	Character = InCharacter;
	Invalidate();
}

void FEffectProfileContextResolver::Invalidate()
{
	CachedContext = FPaper2DPlusEffectProfileContext();
	CachedContext.CharacterPath = FSoftObjectPath(Character.Get());
	ResolvedCatalog.Reset();
	ResolvedEffect.Reset();
	++Revision;
	Invalidated.Broadcast();
}

bool FEffectProfileContextResolver::IsRelevantPath(const FSoftObjectPath& Path) const
{
	if (Path.IsNull()) return false;
	const FString Key = EffectProfileContextResolverPrivate::Normalize(Path);
	const FString EffectKey = EffectProfileContextResolverPrivate::NormalizeEffect(Path);
	return Key == EffectProfileContextResolverPrivate::Normalize(CachedContext.CatalogPath)
		|| Key == EffectProfileContextResolverPrivate::Normalize(CachedContext.CharacterPath)
		|| Key == EffectProfileContextResolverPrivate::Normalize(CachedContext.EffectProfilePath)
		|| CachedContext.AllowedFlipbooks.ContainsByPredicate([&EffectKey](const FSoftObjectPath& Candidate)
		{
			return EffectProfileContextResolverPrivate::NormalizeEffect(Candidate) == EffectKey;
		});
}

void FEffectProfileContextResolver::HandleLibraryInvalidated(
	const FPaper2DPlusEffectLibraryChange& Change)
{
	using Domain = EPaper2DPlusEffectLibraryChangeDomain;
	if (!CachedContext.IsResolved())
	{
		if (Change.Affects(Domain::Catalog)
			|| Change.Affects(Domain::EffectProfile)
			|| Change.Affects(Domain::AssetDiscovery))
		{
			Invalidate();
		}
		return;
	}

	const bool bGeneralDomainChange = Change.NormalizedPaths.IsEmpty()
		&& (Change.Affects(Domain::Catalog) || Change.Affects(Domain::EffectProfile));
	const bool bRelevantPath = Change.NormalizedPaths.ContainsByPredicate(
		[this](const FString& NormalizedPath)
		{
			return IsRelevantPath(FSoftObjectPath(NormalizedPath));
		});
	if (bGeneralDomainChange || bRelevantPath)
	{
		Invalidate();
	}
}

FPaper2DPlusEffectProfileContext FEffectProfileContextResolver::ResolveLoadedContext(
	const UPaper2DPlusCharacterCatalogAsset* Catalog,
	const UPaper2DPlusCharacterProfileAsset* InCharacter,
	bool bCatalogConfigured,
	const FSoftObjectPath& ConfiguredCatalogPath,
	bool bCatalogLoadFailed)
{
	FPaper2DPlusEffectProfileContext Result;
	Result.CatalogPath = !ConfiguredCatalogPath.IsNull() ? ConfiguredCatalogPath : FSoftObjectPath(Catalog);
	Result.CharacterPath = FSoftObjectPath(InCharacter);
	if (!bCatalogConfigured)
	{
		Result.Status = EPaper2DPlusEffectContextStatus::UnscopedNoDefaultCatalog;
		Result.bChoicesScoped = true;
		Result.Guidance = LOCTEXT("NoCatalog", "No default Character Catalog is configured, so Character Effects has no recommendations. Configure a Catalog or use All Project Effects.");
		return Result;
	}

	Result.bChoicesScoped = true;
	if (!Catalog || bCatalogLoadFailed)
	{
		Result.Status = EPaper2DPlusEffectContextStatus::ScopedCatalogUnavailable;
		Result.Guidance = LOCTEXT("CatalogUnavailable", "The configured Character Catalog could not be read. No new Effect choices are shown until it is fixed.");
		return Result;
	}
	if (!InCharacter)
	{
		Result.Status = EPaper2DPlusEffectContextStatus::ScopedCharacterMissing;
		Result.Guidance = LOCTEXT("CharacterMissing", "This editor has no Character Profile context, so the Catalog cannot choose an Effect library.");
		return Result;
	}

	const FPaper2DPlusCharacterCatalogEntry* Entry = Catalog->Entries.FindByPredicate(
		[CharacterKey = EffectProfileContextResolverPrivate::Normalize(FSoftObjectPath(InCharacter))]
		(const FPaper2DPlusCharacterCatalogEntry& Candidate)
		{
			return EffectProfileContextResolverPrivate::Normalize(Candidate.CharacterProfile.ToSoftObjectPath()) == CharacterKey;
		});
	if (!Entry)
	{
		Result.Status = EPaper2DPlusEffectContextStatus::ScopedCharacterMissing;
		Result.Guidance = LOCTEXT("CharacterNotInCatalog", "This Character Profile is not in the project Catalog. Sync or add it before choosing library Effects.");
		return Result;
	}
	Result.EffectProfilePath = Entry->EffectProfile.ToSoftObjectPath();
	if (Result.EffectProfilePath.IsNull())
	{
		Result.Status = EPaper2DPlusEffectContextStatus::ScopedEffectUnassigned;
		Result.Guidance = LOCTEXT("EffectUnassigned", "This Catalog row has no Effect Profile assignment. Assign or create one in the Catalog.");
		return Result;
	}

	const UPaper2DPlusEffectProfileAsset* Effect = Entry->EffectProfile.Get();
	if (!Effect)
	{
		Result.Status = EPaper2DPlusEffectContextStatus::ScopedEffectUnavailable;
		Result.Guidance = LOCTEXT("EffectUnavailable", "The Catalog's Effect Profile assignment could not be loaded. Existing cue values are preserved.");
		return Result;
	}

	TSet<FSoftObjectPath> Seen;
	for (const FPaper2DPlusEffectProfileEntry& EffectEntry : Effect->Effects)
	{
		const FSoftObjectPath Path = EffectProfileContextResolverPrivate::CanonicalEffectPath(
			EffectEntry.GetEffectFlipbookPath());
		if (!Path.IsNull() && !Seen.Contains(Path))
		{
			Seen.Add(Path);
			Result.AllowedFlipbooks.Add(Path);
		}
	}
	Result.AllowedFlipbooks.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B)
	{
		return A.ToString() < B.ToString();
	});
	Result.Status = EPaper2DPlusEffectContextStatus::ScopedReady;
	Result.Guidance = FText::Format(
		LOCTEXT("Ready", "Choices are scoped to {0} unique flipbook(s) in the Catalog-assigned Effect Profile."),
		FText::AsNumber(Result.AllowedFlipbooks.Num()));
	return Result;
}

const FPaper2DPlusEffectProfileContext& FEffectProfileContextResolver::ResolveForPicker()
{
	if (CachedContext.IsResolved()) return CachedContext;
	const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();
	const TSoftObjectPtr<UPaper2DPlusCharacterCatalogAsset> CatalogRef = Settings
		? Settings->DefaultCharacterCatalog
		: TSoftObjectPtr<UPaper2DPlusCharacterCatalogAsset>();
	if (CatalogRef.IsNull())
	{
		CachedContext = ResolveLoadedContext(nullptr, Character.Get(), false);
		return CachedContext;
	}

	UPaper2DPlusCharacterCatalogAsset* Catalog = CatalogRef.LoadSynchronous();
	ResolvedCatalog = Catalog;
	if (!Catalog)
	{
		CachedContext = ResolveLoadedContext(
			nullptr, Character.Get(), true, CatalogRef.ToSoftObjectPath(), true);
		return CachedContext;
	}

	// ResolveLoadedContext intentionally does not load its Effect slot. Resolve it once here at the
	// explicit picker boundary, then call the pure projection again with the now-live reference.
	const FString CharacterKey = EffectProfileContextResolverPrivate::Normalize(FSoftObjectPath(Character.Get()));
	const FPaper2DPlusCharacterCatalogEntry* Entry = Catalog->Entries.FindByPredicate(
		[&CharacterKey](const FPaper2DPlusCharacterCatalogEntry& Candidate)
		{
			return EffectProfileContextResolverPrivate::Normalize(Candidate.CharacterProfile.ToSoftObjectPath()) == CharacterKey;
		});
	if (Entry && !Entry->EffectProfile.IsNull())
	{
		ResolvedEffect = Entry->EffectProfile.LoadSynchronous();
	}
	CachedContext = ResolveLoadedContext(Catalog, Character.Get(), true, CatalogRef.ToSoftObjectPath());
	return CachedContext;
}

#undef LOCTEXT_NAMESPACE
