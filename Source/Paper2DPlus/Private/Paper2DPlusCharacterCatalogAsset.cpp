// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusCharacterCatalogAsset.h"

#include "Algo/Sort.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusEffectProfileAsset.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusCharacterCatalogAsset"

namespace
{
	int32 CatalogSeverityRank(EPaper2DPlusCharacterCatalogIssueSeverity Severity)
	{
		switch (Severity)
		{
		case EPaper2DPlusCharacterCatalogIssueSeverity::Error:
			return 0;
		case EPaper2DPlusCharacterCatalogIssueSeverity::Warning:
			return 1;
		default:
			return 2;
		}
	}
}

const FPrimaryAssetType& UPaper2DPlusCharacterCatalogAsset::CharacterCatalogPrimaryAssetType()
{
	static const FPrimaryAssetType Type(TEXT("Paper2DPlusCharacterCatalog"));
	return Type;
}

FPrimaryAssetId UPaper2DPlusCharacterCatalogAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(CharacterCatalogPrimaryAssetType(), GetFName());
}

FString UPaper2DPlusCharacterCatalogAsset::NormalizeProfilePath(const FSoftObjectPath& Path)
{
	FString Normalized = Path.ToString();
	Normalized.TrimStartAndEndInline();
	Normalized.ToLowerInline();
	return Normalized;
}

bool UPaper2DPlusCharacterCatalogAsset::TagsContain(
	const FGameplayTagContainer& Container,
	FGameplayTag Tag,
	bool bExactMatch)
{
	return Tag.IsValid() && (bExactMatch ? Container.HasTagExact(Tag) : Container.HasTag(Tag));
}

TArray<FPaper2DPlusCharacterCatalogEntry> UPaper2DPlusCharacterCatalogAsset::GetCatalogEntries() const
{
	TArray<FPaper2DPlusCharacterCatalogEntry> Result;
	Result.Reserve(Entries.Num());
	TSet<FString> SeenPaths;

	for (const FPaper2DPlusCharacterCatalogEntry& Entry : Entries)
	{
		const FString Path = NormalizeProfilePath(Entry.CharacterProfile.ToSoftObjectPath());
		if (Path.IsEmpty() || SeenPaths.Contains(Path))
		{
			continue;
		}
		SeenPaths.Add(Path);
		Result.Add(Entry);
	}
	return Result;
}

bool UPaper2DPlusCharacterCatalogAsset::FindEntryByCharacterProfile(
	TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> CharacterProfile,
	FPaper2DPlusCharacterCatalogEntry& OutEntry) const
{
	OutEntry = FPaper2DPlusCharacterCatalogEntry();
	const FString WantedPath = NormalizeProfilePath(CharacterProfile.ToSoftObjectPath());
	if (WantedPath.IsEmpty())
	{
		return false;
	}

	for (const FPaper2DPlusCharacterCatalogEntry& Entry : Entries)
	{
		if (NormalizeProfilePath(Entry.CharacterProfile.ToSoftObjectPath()) == WantedPath)
		{
			OutEntry = Entry;
			return true;
		}
	}
	return false;
}

TArray<FPaper2DPlusCharacterCatalogEntry> UPaper2DPlusCharacterCatalogAsset::GetEntriesWithTag(
	FGameplayTag Tag,
	bool bExactMatch) const
{
	TArray<FPaper2DPlusCharacterCatalogEntry> Result;
	if (!Tag.IsValid())
	{
		return Result;
	}

	for (const FPaper2DPlusCharacterCatalogEntry& Entry : GetCatalogEntries())
	{
		if (TagsContain(Entry.Tags, Tag, bExactMatch))
		{
			Result.Add(Entry);
		}
	}
	return Result;
}

TArray<FPaper2DPlusCharacterCatalogEntry> UPaper2DPlusCharacterCatalogAsset::GetEntriesWithAllTags(
	const FGameplayTagContainer& Tags,
	bool bExactMatch) const
{
	TArray<FPaper2DPlusCharacterCatalogEntry> Result;
	TArray<FGameplayTag> QueryTags;
	Tags.GetGameplayTagArray(QueryTags);
	if (QueryTags.IsEmpty())
	{
		return Result;
	}
	for (const FGameplayTag Tag : QueryTags)
	{
		if (!Tag.IsValid())
		{
			return Result;
		}
	}

	for (const FPaper2DPlusCharacterCatalogEntry& Entry : GetCatalogEntries())
	{
		bool bMatches = true;
		for (const FGameplayTag Tag : QueryTags)
		{
			if (!TagsContain(Entry.Tags, Tag, bExactMatch))
			{
				bMatches = false;
				break;
			}
		}
		if (bMatches)
		{
			Result.Add(Entry);
		}
	}
	return Result;
}

TArray<FPaper2DPlusCharacterCatalogEntry> UPaper2DPlusCharacterCatalogAsset::GetEntriesWithAnyTags(
	const FGameplayTagContainer& Tags,
	bool bExactMatch) const
{
	TArray<FPaper2DPlusCharacterCatalogEntry> Result;
	TArray<FGameplayTag> QueryTags;
	Tags.GetGameplayTagArray(QueryTags);
	if (QueryTags.IsEmpty())
	{
		return Result;
	}
	for (const FGameplayTag Tag : QueryTags)
	{
		if (!Tag.IsValid())
		{
			return Result;
		}
	}

	for (const FPaper2DPlusCharacterCatalogEntry& Entry : GetCatalogEntries())
	{
		for (const FGameplayTag Tag : QueryTags)
		{
			if (TagsContain(Entry.Tags, Tag, bExactMatch))
			{
				Result.Add(Entry);
				break;
			}
		}
	}
	return Result;
}

TArray<FName> UPaper2DPlusCharacterCatalogAsset::GetCatalogGroupNames() const
{
	TArray<FName> Result;
	TSet<FName> SeenNames;
	for (const FPaper2DPlusCharacterCatalogGroup& Group : Groups)
	{
		if (!Group.GroupName.IsNone() && !SeenNames.Contains(Group.GroupName))
		{
			SeenNames.Add(Group.GroupName);
			Result.Add(Group.GroupName);
		}
	}
	return Result;
}

const FPaper2DPlusCharacterCatalogGroup* UPaper2DPlusCharacterCatalogAsset::FindAddressableGroup(FName GroupName) const
{
	if (GroupName.IsNone())
	{
		return nullptr;
	}
	return Groups.FindByPredicate(
		[GroupName](const FPaper2DPlusCharacterCatalogGroup& Group)
		{
			return Group.GroupName == GroupName;
		});
}

TArray<FPaper2DPlusCharacterCatalogEntry> UPaper2DPlusCharacterCatalogAsset::GetEntriesInGroup(FName GroupName) const
{
	TArray<FPaper2DPlusCharacterCatalogEntry> Result;
	const FPaper2DPlusCharacterCatalogGroup* FoundGroup = FindAddressableGroup(GroupName);
	if (!FoundGroup)
	{
		return Result;
	}

	TMap<FString, FPaper2DPlusCharacterCatalogEntry> EntryByPath;
	for (const FPaper2DPlusCharacterCatalogEntry& Entry : GetCatalogEntries())
	{
		EntryByPath.Add(NormalizeProfilePath(Entry.CharacterProfile.ToSoftObjectPath()), Entry);
	}

	TSet<FString> AddedPaths;
	for (const TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>& Member : FoundGroup->Members)
	{
		const FString Path = NormalizeProfilePath(Member.ToSoftObjectPath());
		const FPaper2DPlusCharacterCatalogEntry* Entry = EntryByPath.Find(Path);
		if (!Path.IsEmpty() && Entry && !AddedPaths.Contains(Path))
		{
			AddedPaths.Add(Path);
			Result.Add(*Entry);
		}
	}
	return Result;
}

bool UPaper2DPlusCharacterCatalogAsset::GetExpectedAnimationTagsForCharacter(
	TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> CharacterProfile,
	FGameplayTagContainer& OutExpectedTags) const
{
	OutExpectedTags.Reset();
	const FString WantedPath = NormalizeProfilePath(CharacterProfile.ToSoftObjectPath());
	if (WantedPath.IsEmpty()
		|| !Entries.ContainsByPredicate([&WantedPath](const FPaper2DPlusCharacterCatalogEntry& Entry)
		{
			return NormalizeProfilePath(Entry.CharacterProfile.ToSoftObjectPath()) == WantedPath;
		}))
	{
		return false;
	}

	OutExpectedTags.AppendTags(ExpectedAnimationTags);
	for (const FPaper2DPlusCharacterCatalogGroup& Group : Groups)
	{
		// Same validity rule as GetEntriesInGroup: an unnamed group or a later duplicate-named
		// group is unaddressable, so its extras never enter the expected-tag union.
		if (FindAddressableGroup(Group.GroupName) != &Group)
		{
			continue;
		}

		const bool bIsMember = Group.Members.ContainsByPredicate(
			[&WantedPath](const TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>& Member)
			{
				return NormalizeProfilePath(Member.ToSoftObjectPath()) == WantedPath;
			});
		if (bIsMember)
		{
			OutExpectedTags.AppendTags(Group.AdditionalExpectedAnimationTags);
		}
	}
	return true;
}

FPaper2DPlusCharacterCatalogCompletion UPaper2DPlusCharacterCatalogAsset::GetEntryCompletion(
	const FPaper2DPlusCharacterCatalogEntry& Entry) const
{
	FPaper2DPlusCharacterCatalogCompletion Result;
	auto AddRequirement = [&Result](
		bool bRequired,
		bool bPresent,
		EPaper2DPlusCatalogCompanion Companion)
	{
		if (!bRequired)
		{
			return;
		}
		++Result.RequiredCount;
		if (bPresent)
		{
			++Result.PresentRequiredCount;
		}
		else
		{
			Result.MissingRequiredCompanions.Add(Companion);
		}
	};

	AddRequirement(Entry.Requirements.bRequireLayer, !Entry.LayerProfile.IsNull(), EPaper2DPlusCatalogCompanion::Layer);
	AddRequirement(Entry.Requirements.bRequireEffect, !Entry.EffectProfile.IsNull(), EPaper2DPlusCatalogCompanion::Effect);
	AddRequirement(Entry.Requirements.bRequireCombat, !Entry.CombatProfile.IsNull(), EPaper2DPlusCatalogCompanion::Combat);
	Result.bComplete = Result.MissingRequiredCompanions.IsEmpty();
	return Result;
}

bool UPaper2DPlusCharacterCatalogAsset::GetCharacterCompletion(
	TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> CharacterProfile,
	FPaper2DPlusCharacterCatalogCompletion& OutCompletion) const
{
	OutCompletion = FPaper2DPlusCharacterCatalogCompletion();
	FPaper2DPlusCharacterCatalogEntry Entry;
	if (!FindEntryByCharacterProfile(CharacterProfile, Entry))
	{
		OutCompletion.bComplete = false;
		return false;
	}
	OutCompletion = GetEntryCompletion(Entry);
	return true;
}

bool UPaper2DPlusCharacterCatalogAsset::ValidateCharacterCatalogAsset(
	TArray<FPaper2DPlusCharacterCatalogIssue>& OutIssues) const
{
	OutIssues.Reset();
	auto AddIssue = [&OutIssues](
		EPaper2DPlusCharacterCatalogIssueSeverity Severity,
		FName Code,
		const FSoftObjectPath& CharacterPath,
		FName Scope,
		FName Field,
		const FText& Message)
	{
		FPaper2DPlusCharacterCatalogIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Severity = Severity;
		Issue.Code = Code;
		Issue.CharacterPath = CharacterPath;
		Issue.Scope = Scope;
		Issue.Field = Field;
		Issue.Message = Message;
	};

	TMap<FString, int32> FirstEntryIndexByPath;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FPaper2DPlusCharacterCatalogEntry& Entry = Entries[Index];
		const FSoftObjectPath CharacterPath = Entry.CharacterProfile.ToSoftObjectPath();
		const FString NormalizedPath = NormalizeProfilePath(CharacterPath);
		if (NormalizedPath.IsEmpty())
		{
			AddIssue(
				EPaper2DPlusCharacterCatalogIssueSeverity::Error,
				TEXT("Entry.MissingCharacter"),
				CharacterPath,
				TEXT("Entry"),
				TEXT("CharacterProfile"),
				FText::Format(LOCTEXT("MissingCharacter", "Catalog entry {0} has no Character Profile."), FText::AsNumber(Index)));
			continue;
		}

		if (const int32* FirstIndex = FirstEntryIndexByPath.Find(NormalizedPath))
		{
			AddIssue(
				EPaper2DPlusCharacterCatalogIssueSeverity::Error,
				TEXT("Entry.DuplicateCharacter"),
				CharacterPath,
				TEXT("Entry"),
				TEXT("CharacterProfile"),
				FText::Format(
					LOCTEXT("DuplicateCharacter", "Catalog entry {0} duplicates Character Profile '{1}' from entry {2}; runtime queries use the first entry."),
					FText::AsNumber(Index),
					FText::FromString(CharacterPath.ToString()),
					FText::AsNumber(*FirstIndex)));
		}
		else
		{
			FirstEntryIndexByPath.Add(NormalizedPath, Index);
		}

		const FPaper2DPlusCharacterCatalogCompletion Completion = GetEntryCompletion(Entry);
		for (const EPaper2DPlusCatalogCompanion Missing : Completion.MissingRequiredCompanions)
		{
			const FName MissingField = Missing == EPaper2DPlusCatalogCompanion::Layer
				? FName(TEXT("LayerProfile"))
				: Missing == EPaper2DPlusCatalogCompanion::Effect
					? FName(TEXT("EffectProfile"))
					: FName(TEXT("CombatProfile"));
			AddIssue(
				EPaper2DPlusCharacterCatalogIssueSeverity::Error,
				TEXT("Entry.MissingRequiredCompanion"),
				CharacterPath,
				TEXT("Completion"),
				MissingField,
				FText::Format(
					LOCTEXT("MissingRequiredCompanion", "Character '{0}' is missing required companion '{1}'."),
					FText::FromString(CharacterPath.ToString()),
					FText::FromName(MissingField)));
		}
	}

	TMap<FName, int32> FirstGroupIndexByName;
	for (int32 GroupIndex = 0; GroupIndex < Groups.Num(); ++GroupIndex)
	{
		const FPaper2DPlusCharacterCatalogGroup& Group = Groups[GroupIndex];
		if (Group.GroupName.IsNone())
		{
			AddIssue(
				EPaper2DPlusCharacterCatalogIssueSeverity::Error,
				TEXT("Group.MissingName"),
				FSoftObjectPath(),
				TEXT("Group"),
				TEXT("GroupName"),
				FText::Format(LOCTEXT("MissingGroupName", "Catalog group {0} has no name."), FText::AsNumber(GroupIndex)));
		}
		else if (const int32* FirstIndex = FirstGroupIndexByName.Find(Group.GroupName))
		{
			AddIssue(
				EPaper2DPlusCharacterCatalogIssueSeverity::Error,
				TEXT("Group.DuplicateName"),
				FSoftObjectPath(),
				Group.GroupName,
				TEXT("GroupName"),
				FText::Format(
					LOCTEXT("DuplicateGroupName", "Group '{0}' duplicates group {1}; runtime queries use the first group."),
					FText::FromName(Group.GroupName),
					FText::AsNumber(*FirstIndex)));
		}
		else
		{
			FirstGroupIndexByName.Add(Group.GroupName, GroupIndex);
		}

		TSet<FString> SeenMembers;
		for (int32 MemberIndex = 0; MemberIndex < Group.Members.Num(); ++MemberIndex)
		{
			const FSoftObjectPath MemberPath = Group.Members[MemberIndex].ToSoftObjectPath();
			const FString NormalizedMemberPath = NormalizeProfilePath(MemberPath);
			if (NormalizedMemberPath.IsEmpty())
			{
				AddIssue(
					EPaper2DPlusCharacterCatalogIssueSeverity::Warning,
					TEXT("Group.MissingMember"),
					MemberPath,
					Group.GroupName,
					TEXT("Members"),
					FText::Format(LOCTEXT("MissingGroupMember", "Group '{0}' member {1} is empty and is skipped at runtime."), FText::FromName(Group.GroupName), FText::AsNumber(MemberIndex)));
				continue;
			}

			if (SeenMembers.Contains(NormalizedMemberPath))
			{
				AddIssue(
					EPaper2DPlusCharacterCatalogIssueSeverity::Warning,
					TEXT("Group.DuplicateMember"),
					MemberPath,
					Group.GroupName,
					TEXT("Members"),
					FText::Format(LOCTEXT("DuplicateGroupMember", "Group '{0}' repeats '{1}'; later occurrences are skipped at runtime."), FText::FromName(Group.GroupName), FText::FromString(MemberPath.ToString())));
				continue;
			}
			SeenMembers.Add(NormalizedMemberPath);

			if (!FirstEntryIndexByPath.Contains(NormalizedMemberPath))
			{
				AddIssue(
					EPaper2DPlusCharacterCatalogIssueSeverity::Warning,
					TEXT("Group.OutOfCatalogMember"),
					MemberPath,
					Group.GroupName,
					TEXT("Members"),
					FText::Format(LOCTEXT("OutOfCatalogGroupMember", "Group '{0}' references '{1}', which is not an active Catalog entry and is skipped at runtime."), FText::FromName(Group.GroupName), FText::FromString(MemberPath.ToString())));
			}
		}
	}

	OutIssues.Sort([](const FPaper2DPlusCharacterCatalogIssue& A, const FPaper2DPlusCharacterCatalogIssue& B)
	{
		const int32 SeverityA = CatalogSeverityRank(A.Severity);
		const int32 SeverityB = CatalogSeverityRank(B.Severity);
		if (SeverityA != SeverityB)
		{
			return SeverityA < SeverityB;
		}
		const FString PathA = A.CharacterPath.ToString();
		const FString PathB = B.CharacterPath.ToString();
		const int32 PathCompare = PathA.Compare(PathB, ESearchCase::IgnoreCase);
		if (PathCompare != 0)
		{
			return PathCompare < 0;
		}
		const int32 ScopeCompare = A.Scope.ToString().Compare(B.Scope.ToString(), ESearchCase::IgnoreCase);
		if (ScopeCompare != 0)
		{
			return ScopeCompare < 0;
		}
		const int32 CodeCompare = A.Code.ToString().Compare(B.Code.ToString(), ESearchCase::IgnoreCase);
		if (CodeCompare != 0)
		{
			return CodeCompare < 0;
		}
		return A.Message.ToString().Compare(B.Message.ToString(), ESearchCase::IgnoreCase) < 0;
	});

	return !OutIssues.ContainsByPredicate([](const FPaper2DPlusCharacterCatalogIssue& Issue)
	{
		return Issue.Severity == EPaper2DPlusCharacterCatalogIssueSeverity::Error;
	});
}

#undef LOCTEXT_NAMESPACE
