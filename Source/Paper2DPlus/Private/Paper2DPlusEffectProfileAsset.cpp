// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusEffectProfileAsset.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "Paper2DPlusEffectTags.h"
#include "PaperFlipbook.h"
#include "Runtime/Launch/Resources/Version.h"
#include "UObject/ObjectRedirector.h"

namespace
{
	IAssetRegistry& GetEffectAssetRegistry()
	{
		return FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	}

	FSoftObjectPath GetRedirectedObjectPathNoLoad(
		IAssetRegistry& AssetRegistry,
		const FSoftObjectPath& ObjectPath)
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		const FName RedirectedPath = AssetRegistry.GetRedirectedObjectPath(
			FName(*ObjectPath.ToString()));
		return RedirectedPath.IsNone()
			? ObjectPath
			: FSoftObjectPath(RedirectedPath.ToString());
#else
		const FSoftObjectPath RedirectedPath = AssetRegistry.GetRedirectedObjectPath(ObjectPath);
		return RedirectedPath.IsNull() ? ObjectPath : RedirectedPath;
#endif
	}

	bool TryGetEffectAssetDataNoLoad(
		IAssetRegistry& AssetRegistry,
		const FSoftObjectPath& ObjectPath,
		FAssetData& OutAssetData)
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		OutAssetData = AssetRegistry.GetAssetByObjectPath(FName(*ObjectPath.ToString()), false);
		return OutAssetData.IsValid();
#else
		return AssetRegistry.TryGetAssetByObjectPath(ObjectPath, OutAssetData)
			== UE::AssetRegistry::EExists::Exists;
#endif
	}

	FSoftObjectPath CanonicalizeEffectPathNoLoad(const FSoftObjectPath& ObjectPath)
	{
		if (!ObjectPath.IsValid())
		{
			return ObjectPath;
		}
		if (const UObject* ResidentObject = ObjectPath.ResolveObject())
		{
			// ResolveObject already follows a resident UObjectRedirector. Always derive identity from
			// the resolved object so redirect aliases behave the same before and after residency.
			return FSoftObjectPath(ResidentObject);
		}
		else
		{
			IAssetRegistry& AssetRegistry = GetEffectAssetRegistry();
			FAssetData AssetData;
			if (!TryGetEffectAssetDataNoLoad(AssetRegistry, ObjectPath, AssetData)
				|| !AssetData.IsRedirector())
			{
				return ObjectPath;
			}
		}

		IAssetRegistry& AssetRegistry = GetEffectAssetRegistry();
		const FSoftObjectPath RedirectedPath =
			GetRedirectedObjectPathNoLoad(AssetRegistry, ObjectPath);
		return RedirectedPath.IsValid() ? RedirectedPath : ObjectPath;
	}

	bool NamesEqual(FName Left, FName Right)
	{
		return Left.ToString().Equals(Right.ToString(), ESearchCase::IgnoreCase);
	}

	FString FlipbookIdentity(const FSoftObjectPath& FlipbookPath)
	{
		return FlipbookPath.IsNull()
			? FString()
			: CanonicalizeEffectPathNoLoad(FlipbookPath).ToString().ToLower();
	}

	FString FlipbookIdentity(const UPaperFlipbook* Flipbook)
	{
		return Flipbook ? FlipbookIdentity(FSoftObjectPath(Flipbook)) : FString();
	}

	bool IsStrictChildOf(FGameplayTag Tag, FGameplayTag Root)
	{
		return Tag.IsValid() && Root.IsValid() && Tag != Root && Tag.MatchesTag(Root);
	}

	bool TagMatches(FGameplayTag Value, FGameplayTag Query, bool bExactMatch)
	{
		return Value.IsValid() && Query.IsValid()
			&& (bExactMatch ? Value == Query : Value.MatchesTag(Query));
	}

	bool ContainerMatchesTag(const FGameplayTagContainer& Values, FGameplayTag Query, bool bExactMatch)
	{
		for (const FGameplayTag& Value : Values)
		{
			if (TagMatches(Value, Query, bExactMatch))
			{
				return true;
			}
		}
		return false;
	}

	template <typename PredicateType>
	TArray<UPaperFlipbook*> CollectUniqueFlipbooks(
		const TArray<FPaper2DPlusEffectProfileEntry>& Entries,
		PredicateType Matches)
	{
		TArray<UPaperFlipbook*> Result;
		TSet<FString> SeenPaths;
		Result.Reserve(Entries.Num());

		for (const FPaper2DPlusEffectProfileEntry& Entry : Entries)
		{
			const FString Identity = FlipbookIdentity(Entry.GetEffectFlipbookPath());
			if (Identity.IsEmpty() || SeenPaths.Contains(Identity))
			{
				continue;
			}

			SeenPaths.Add(Identity);
			// Duplicate metadata is invalid. First authored row is nevertheless the deterministic truth
			// for every query, so later duplicates cannot make an identity appear in a filter the first
			// row did not match.
			if (Matches(Entry))
			{
				if (UPaperFlipbook* Flipbook = Entry.LoadEffectFlipbook())
				{
					Result.Add(Flipbook);
				}
			}
		}
		return Result;
	}

	FName ValidationIdentity(const FPaper2DPlusEffectProfileEntry& Entry)
	{
		if (!Entry.EffectName.IsNone())
		{
			return Entry.EffectName;
		}
		const FString AssetName = Entry.GetEffectFlipbookPath().GetAssetName();
		return AssetName.IsEmpty() ? NAME_None : FName(*AssetName);
	}
}

FPaper2DPlusEffectSpawnSettings FPaper2DPlusEffectProfileEntry::ToSpawnSettings() const
{
	FPaper2DPlusEffectSpawnSettings Settings;
	Settings.EffectFlipbook = LoadEffectFlipbook();
	Settings.Offset = Offset;
	Settings.Rotation = Rotation;
	Settings.Scale = Scale;
	Settings.bFlipWithCharacter = bFlipWithCharacter;
	Settings.Tint = Tint;
	Settings.CategoryTag = CategoryTag;
	return Settings;
}

FSoftObjectPath FPaper2DPlusEffectProfileEntry::GetEffectFlipbookPath() const
{
	return EffectFlipbook.ToSoftObjectPath();
}

UPaperFlipbook* FPaper2DPlusEffectProfileEntry::GetLoadedEffectFlipbook() const
{
	return LoadedEffectFlipbook ? LoadedEffectFlipbook.Get() : EffectFlipbook.Get();
}

UPaperFlipbook* FPaper2DPlusEffectProfileEntry::LoadEffectFlipbook() const
{
	if (UPaperFlipbook* Loaded = GetLoadedEffectFlipbook())
	{
		return Loaded;
	}
	return EffectFlipbook.LoadSynchronous();
}

FPrimaryAssetId UPaper2DPlusEffectProfileAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("Paper2DPlusEffectProfile"), GetFName());
}

void UPaper2DPlusEffectProfileAsset::PostInitProperties()
{
	Super::PostInitProperties();
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad | RF_WasLoaded))
	{
		EffectLibrarySchemaVersion = CurrentEffectLibrarySchemaVersion;
	}
}

void UPaper2DPlusEffectProfileAsset::PostLoad()
{
	Super::PostLoad();
	if (MigrateLegacyLibrarySchema() > 0)
	{
		MarkPackageDirty();
	}
}

int32 UPaper2DPlusEffectProfileAsset::MigrateLegacyLibrarySchema()
{
	if (EffectLibrarySchemaVersion >= CurrentEffectLibrarySchemaVersion)
	{
		return 0;
	}

	int32 Changes = 0;
	const FGameplayTag TypeRoot = Paper2DPlusEffectTags::Type.GetTag();
	const FGameplayTag DescriptorRoot = Paper2DPlusEffectTags::Descriptor.GetTag();

	for (FPaper2DPlusEffectProfileEntry& Entry : Effects)
	{
		if (Entry.DisplayLabel.IsEmpty() && !Entry.EffectName.IsNone())
		{
			Entry.DisplayLabel = FText::FromName(Entry.EffectName);
			++Changes;
		}

		if (!Entry.CategoryTag.IsValid())
		{
			continue;
		}

		if (IsStrictChildOf(Entry.CategoryTag, TypeRoot))
		{
			if (!Entry.TypeTag.IsValid())
			{
				Entry.TypeTag = Entry.CategoryTag;
				++Changes;
			}
			else if (Entry.TypeTag != Entry.CategoryTag && !Entry.LegacyCategoryAwaitingRemap.IsValid())
			{
				Entry.LegacyCategoryAwaitingRemap = Entry.CategoryTag;
				++Changes;
			}
		}
		else if (IsStrictChildOf(Entry.CategoryTag, DescriptorRoot))
		{
			if (!Entry.DescriptorTags.HasTagExact(Entry.CategoryTag))
			{
				Entry.DescriptorTags.AddTag(Entry.CategoryTag);
				++Changes;
			}
		}
		else if (!Entry.LegacyCategoryAwaitingRemap.IsValid())
		{
			Entry.LegacyCategoryAwaitingRemap = Entry.CategoryTag;
			++Changes;
		}
	}

	EffectLibrarySchemaVersion = CurrentEffectLibrarySchemaVersion;
	return Changes + 1; // Persisting the version stamp is itself one migration change.
}

bool UPaper2DPlusEffectProfileAsset::GetEffectEntryByIndex(
	int32 Index,
	FPaper2DPlusEffectProfileEntry& OutEntry) const
{
	if (!Effects.IsValidIndex(Index))
	{
		OutEntry = FPaper2DPlusEffectProfileEntry();
		return false;
	}

	OutEntry = Effects[Index];
	OutEntry.LoadedEffectFlipbook = OutEntry.LoadEffectFlipbook();
	return true;
}

EPaper2DPlusEffectFlipbookPathStatus UPaper2DPlusEffectProfileAsset::InspectEffectFlipbookPathNoLoad(
	const FSoftObjectPath& EffectFlipbookPath)
{
	if (EffectFlipbookPath.IsNull())
	{
		return EPaper2DPlusEffectFlipbookPathStatus::Unset;
	}
	if (!EffectFlipbookPath.IsValid())
	{
		return EPaper2DPlusEffectFlipbookPathStatus::InvalidPath;
	}

	if (const UObject* ResidentObject = EffectFlipbookPath.ResolveObject())
	{
		if (ResidentObject->IsA<UPaperFlipbook>())
		{
			return EPaper2DPlusEffectFlipbookPathStatus::Valid;
		}
		const UObjectRedirector* Redirector = Cast<UObjectRedirector>(ResidentObject);
		if (!Redirector)
		{
			return EPaper2DPlusEffectFlipbookPathStatus::WrongClass;
		}
		if (Redirector->DestinationObject)
		{
			return Redirector->DestinationObject->IsA<UPaperFlipbook>()
				? EPaper2DPlusEffectFlipbookPathStatus::Valid
				: EPaper2DPlusEffectFlipbookPathStatus::WrongClass;
		}
	}

	IAssetRegistry& AssetRegistry = GetEffectAssetRegistry();
	FAssetData AssetData;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	AssetData = AssetRegistry.GetAssetByObjectPath(FName(*EffectFlipbookPath.ToString()), false);
	if (!AssetData.IsValid())
	{
		return AssetRegistry.IsLoadingAssets()
			? EPaper2DPlusEffectFlipbookPathStatus::Unknown
			: EPaper2DPlusEffectFlipbookPathStatus::Missing;
	}
#else
	switch (AssetRegistry.TryGetAssetByObjectPath(EffectFlipbookPath, AssetData))
	{
	case UE::AssetRegistry::EExists::Unknown:
		return EPaper2DPlusEffectFlipbookPathStatus::Unknown;
	case UE::AssetRegistry::EExists::DoesNotExist:
		return EPaper2DPlusEffectFlipbookPathStatus::Missing;
	default:
		break;
	}
#endif
	if (AssetData.IsRedirector())
	{
		const FSoftObjectPath RedirectedPath =
			GetRedirectedObjectPathNoLoad(AssetRegistry, EffectFlipbookPath);
		if (RedirectedPath.IsValid() && RedirectedPath != EffectFlipbookPath)
		{
			return InspectEffectFlipbookPathNoLoad(RedirectedPath);
		}
	}

	return AssetData.IsInstanceOf(UPaperFlipbook::StaticClass())
		? EPaper2DPlusEffectFlipbookPathStatus::Valid
		: EPaper2DPlusEffectFlipbookPathStatus::WrongClass;
}

FSoftObjectPath UPaper2DPlusEffectProfileAsset::GetCanonicalEffectFlipbookPathNoLoad(
	const FSoftObjectPath& EffectFlipbookPath)
{
	return CanonicalizeEffectPathNoLoad(EffectFlipbookPath);
}

UPaperFlipbook* UPaper2DPlusEffectProfileAsset::GetEffectFlipbookByIndex(int32 Index) const
{
	return Effects.IsValidIndex(Index) ? Effects[Index].LoadEffectFlipbook() : nullptr;
}

TArray<UPaperFlipbook*> UPaper2DPlusEffectProfileAsset::GetEffectFlipbooks() const
{
	return CollectUniqueFlipbooks(Effects, [](const FPaper2DPlusEffectProfileEntry&) { return true; });
}

bool UPaper2DPlusEffectProfileAsset::ContainsEffectFlipbook(UPaperFlipbook* EffectFlipbook) const
{
	return FindEffectEntryByFlipbook(EffectFlipbook) != nullptr;
}

TArray<UPaperFlipbook*> UPaper2DPlusEffectProfileAsset::GetEffectFlipbooksByType(
	FGameplayTag TypeTag,
	bool bExactMatch) const
{
	if (!TypeTag.IsValid())
	{
		return TArray<UPaperFlipbook*>();
	}
	return CollectUniqueFlipbooks(Effects,
		[TypeTag, bExactMatch](const FPaper2DPlusEffectProfileEntry& Entry)
		{
			return TagMatches(Entry.TypeTag, TypeTag, bExactMatch);
		});
}

TArray<UPaperFlipbook*> UPaper2DPlusEffectProfileAsset::GetEffectFlipbooksByDescriptor(
	FGameplayTag DescriptorTag,
	bool bExactMatch) const
{
	if (!DescriptorTag.IsValid())
	{
		return TArray<UPaperFlipbook*>();
	}
	return CollectUniqueFlipbooks(Effects,
		[DescriptorTag, bExactMatch](const FPaper2DPlusEffectProfileEntry& Entry)
		{
			return ContainerMatchesTag(Entry.DescriptorTags, DescriptorTag, bExactMatch);
		});
}

TArray<UPaperFlipbook*> UPaper2DPlusEffectProfileAsset::GetEffectFlipbooksWithAllDescriptors(
	const FGameplayTagContainer& DescriptorTags,
	bool bExactMatch) const
{
	if (DescriptorTags.IsEmpty())
	{
		return TArray<UPaperFlipbook*>();
	}

	return CollectUniqueFlipbooks(Effects,
		[&DescriptorTags, bExactMatch](const FPaper2DPlusEffectProfileEntry& Entry)
		{
			for (const FGameplayTag& Query : DescriptorTags)
			{
				if (!ContainerMatchesTag(Entry.DescriptorTags, Query, bExactMatch))
				{
					return false;
				}
			}
			return true;
		});
}

TArray<UPaperFlipbook*> UPaper2DPlusEffectProfileAsset::GetEffectFlipbooksWithAnyDescriptors(
	const FGameplayTagContainer& DescriptorTags,
	bool bExactMatch) const
{
	if (DescriptorTags.IsEmpty())
	{
		return TArray<UPaperFlipbook*>();
	}

	return CollectUniqueFlipbooks(Effects,
		[&DescriptorTags, bExactMatch](const FPaper2DPlusEffectProfileEntry& Entry)
		{
			for (const FGameplayTag& Query : DescriptorTags)
			{
				if (ContainerMatchesTag(Entry.DescriptorTags, Query, bExactMatch))
				{
					return true;
				}
			}
			return false;
		});
}

const FPaper2DPlusEffectProfileEntry* UPaper2DPlusEffectProfileAsset::FindEffectEntry(FName EffectName) const
{
	if (EffectName.IsNone())
	{
		return nullptr;
	}

	for (const FPaper2DPlusEffectProfileEntry& Entry : Effects)
	{
		if (NamesEqual(Entry.EffectName, EffectName))
		{
			return &Entry;
		}
	}
	return nullptr;
}

const FPaper2DPlusEffectProfileEntry* UPaper2DPlusEffectProfileAsset::FindEffectEntryByFlipbook(
	const UPaperFlipbook* EffectFlipbook) const
{
	const FString WantedIdentity = FlipbookIdentity(EffectFlipbook);
	if (WantedIdentity.IsEmpty())
	{
		return nullptr;
	}

	for (const FPaper2DPlusEffectProfileEntry& Entry : Effects)
	{
		if (FlipbookIdentity(Entry.GetEffectFlipbookPath()) == WantedIdentity)
		{
			return &Entry;
		}
	}
	return nullptr;
}

bool UPaper2DPlusEffectProfileAsset::ResolveLegacyEffectSpawnSettings(
	FName EffectName,
	FPaper2DPlusEffectSpawnSettings& OutSettings) const
{
	OutSettings = FPaper2DPlusEffectSpawnSettings();
	const FPaper2DPlusEffectProfileEntry* Entry = FindEffectEntry(EffectName);
	if (!Entry)
	{
		return false;
	}

	OutSettings = Entry->ToSpawnSettings();
	return OutSettings.IsValid();
}

bool UPaper2DPlusEffectProfileAsset::ResolveEffectSpawnSettings(
	FName EffectName,
	FPaper2DPlusEffectSpawnSettings& OutSettings) const
{
	return ResolveLegacyEffectSpawnSettings(EffectName, OutSettings);
}

void UPaper2DPlusEffectProfileAsset::GetEffectsByCategory(
	FGameplayTag CategoryTag,
	TArray<FPaper2DPlusEffectProfileEntry>& OutEffects) const
{
	OutEffects.Reset();
	for (const FPaper2DPlusEffectProfileEntry& Entry : Effects)
	{
		if (!CategoryTag.IsValid() || Entry.CategoryTag.MatchesTag(CategoryTag))
		{
			FPaper2DPlusEffectProfileEntry& OutEntry = OutEffects.Add_GetRef(Entry);
			OutEntry.LoadedEffectFlipbook = OutEntry.LoadEffectFlipbook();
		}
	}
}

void UPaper2DPlusEffectProfileAsset::AddValidationIssue(
	TArray<FPaper2DPlusEffectProfileValidationIssue>& OutIssues,
	EPaper2DPlusEffectProfileValidationSeverity Severity,
	const FText& Message,
	FName EffectName,
	FName Field) const
{
	FPaper2DPlusEffectProfileValidationIssue Issue;
	Issue.Severity = Severity;
	Issue.Message = Message;
	Issue.EffectName = EffectName;
	Issue.Field = Field;
	OutIssues.Add(MoveTemp(Issue));
}

bool UPaper2DPlusEffectProfileAsset::ValidateEffectProfileAsset(
	TArray<FPaper2DPlusEffectProfileValidationIssue>& OutIssues) const
{
	OutIssues.Reset();
	TSet<FString> SeenFlipbookPaths;
	const FGameplayTag TypeRoot = Paper2DPlusEffectTags::Type.GetTag();
	const FGameplayTag DescriptorRoot = Paper2DPlusEffectTags::Descriptor.GetTag();

	for (int32 Index = 0; Index < Effects.Num(); ++Index)
	{
		const FPaper2DPlusEffectProfileEntry& Entry = Effects[Index];
		const FName Identity = ValidationIdentity(Entry);
		const FText RowLabel = !Entry.DisplayLabel.IsEmpty()
			? Entry.DisplayLabel
			: (Identity.IsNone()
				? FText::Format(NSLOCTEXT("Paper2DPlus", "EffectProfileRowIndex", "row {0}"), FText::AsNumber(Index))
				: FText::FromName(Identity));

		const FSoftObjectPath FlipbookPath = Entry.GetEffectFlipbookPath();
		const EPaper2DPlusEffectFlipbookPathStatus FlipbookStatus =
			InspectEffectFlipbookPathNoLoad(FlipbookPath);
		if (FlipbookStatus == EPaper2DPlusEffectFlipbookPathStatus::Unset)
		{
			AddValidationIssue(
				OutIssues,
				EPaper2DPlusEffectProfileValidationSeverity::Error,
				FText::Format(NSLOCTEXT("Paper2DPlus", "EffectProfileNullFlipbook", "Effect library {0} has no flipbook. Assign one or remove the row."), RowLabel),
				Identity,
				TEXT("EffectFlipbook"));
		}
		else if (FlipbookStatus == EPaper2DPlusEffectFlipbookPathStatus::InvalidPath
			|| FlipbookStatus == EPaper2DPlusEffectFlipbookPathStatus::Missing
			|| FlipbookStatus == EPaper2DPlusEffectFlipbookPathStatus::WrongClass)
		{
			AddValidationIssue(
				OutIssues,
				EPaper2DPlusEffectProfileValidationSeverity::Error,
				FText::Format(
					NSLOCTEXT("Paper2DPlus", "EffectProfileInvalidFlipbook", "Effect library {0} references a missing asset or an asset that is not a Paper Flipbook: '{1}'. Assign a valid Paper Flipbook or remove the row."),
					RowLabel,
					FText::FromString(FlipbookPath.ToString())),
				Identity,
				TEXT("EffectFlipbook"));
		}
		else
		{
			if (FlipbookStatus == EPaper2DPlusEffectFlipbookPathStatus::Unknown)
			{
				AddValidationIssue(
					OutIssues,
					EPaper2DPlusEffectProfileValidationSeverity::Info,
					FText::Format(
						NSLOCTEXT("Paper2DPlus", "EffectProfileFlipbookDiscoveryPending", "Effect library {0} is waiting for Asset Registry discovery to confirm Flipbook '{1}'. Validation will refresh when discovery completes."),
						RowLabel,
						FText::FromString(FlipbookPath.ToString())),
					Identity,
					TEXT("EffectFlipbook"));
			}
			const FString Path = FlipbookIdentity(FlipbookPath);
			if (SeenFlipbookPaths.Contains(Path))
			{
				AddValidationIssue(
					OutIssues,
					EPaper2DPlusEffectProfileValidationSeverity::Error,
					FText::Format(NSLOCTEXT("Paper2DPlus", "EffectProfileDuplicateFlipbook", "Flipbook '{0}' appears more than once. Keep one ordered library row per flipbook."), FText::FromString(FlipbookPath.ToString())),
					Identity,
					TEXT("EffectFlipbook"));
			}
			SeenFlipbookPaths.Add(Path);
		}

		if (!Entry.TypeTag.IsValid())
		{
			AddValidationIssue(
				OutIssues,
				EPaper2DPlusEffectProfileValidationSeverity::Warning,
				FText::Format(NSLOCTEXT("Paper2DPlus", "EffectProfileMissingType", "Effect library {0} has no primary Type tag."), RowLabel),
				Identity,
				TEXT("TypeTag"));
		}
		else if (!IsStrictChildOf(Entry.TypeTag, TypeRoot))
		{
			AddValidationIssue(
				OutIssues,
				EPaper2DPlusEffectProfileValidationSeverity::Warning,
				FText::Format(NSLOCTEXT("Paper2DPlus", "EffectProfileOffRootType", "Effect library {0} uses Type '{1}', which must be a child of Paper2DPlus.Effect.Type."), RowLabel, FText::FromString(Entry.TypeTag.ToString())),
				Identity,
				TEXT("TypeTag"));
		}

		for (const FGameplayTag& Descriptor : Entry.DescriptorTags)
		{
			if (!IsStrictChildOf(Descriptor, DescriptorRoot))
			{
				AddValidationIssue(
					OutIssues,
					EPaper2DPlusEffectProfileValidationSeverity::Warning,
					FText::Format(NSLOCTEXT("Paper2DPlus", "EffectProfileOffRootDescriptor", "Effect library {0} uses Descriptor '{1}', which must be a child of Paper2DPlus.Effect.Descriptor."), RowLabel, FText::FromString(Descriptor.ToString())),
					Identity,
					TEXT("DescriptorTags"));
			}
		}

		FGameplayTag PendingLegacyCategory = Entry.LegacyCategoryAwaitingRemap;
		if (!PendingLegacyCategory.IsValid() && Entry.CategoryTag.IsValid()
			&& Entry.TypeTag != Entry.CategoryTag
			&& !Entry.DescriptorTags.HasTagExact(Entry.CategoryTag))
		{
			PendingLegacyCategory = Entry.CategoryTag;
		}
		if (PendingLegacyCategory.IsValid())
		{
			AddValidationIssue(
				OutIssues,
				EPaper2DPlusEffectProfileValidationSeverity::Warning,
				FText::Format(NSLOCTEXT("Paper2DPlus", "EffectProfileLegacyCategoryRemap", "Effect library {0} retains legacy category '{1}'. Remap it to Type/Descriptors, then clear Legacy Category Awaiting Remap."), RowLabel, FText::FromString(PendingLegacyCategory.ToString())),
				Identity,
				TEXT("LegacyCategoryAwaitingRemap"));
		}
	}

	return !OutIssues.ContainsByPredicate([](const FPaper2DPlusEffectProfileValidationIssue& Issue)
	{
		return Issue.Severity == EPaper2DPlusEffectProfileValidationSeverity::Error;
	});
}
