// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "EffectProfileEditorModel.h"

#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "GameplayTagsManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Crc.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusEffectLibraryIndex.h"
#include "Paper2DPlusEffectTags.h"
#include "PaperFlipbook.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "EffectProfileEditorModel"

namespace EffectProfileEditorModelPrivate
{
	const FName SourceType(TEXT("Effect"));
	const TCHAR* SelectionConfigRoot = TEXT("Paper2DPlus.EffectProfileEditor.Selection");

	FString TagLeaf(const FGameplayTag& Tag)
	{
		if (!Tag.IsValid())
		{
			return FString();
		}
		const FString Full = Tag.ToString();
		int32 Dot = INDEX_NONE;
		return Full.FindLastChar(TEXT('.'), Dot) ? Full.RightChop(Dot + 1) : Full;
	}

	int32 IssueRank(EEffectProfileRowIssueState State)
	{
		return static_cast<int32>(State);
	}

	EEffectProfileRowIssueState ProjectIssueState(EPaper2DPlusEffectProfileValidationSeverity Severity)
	{
		switch (Severity)
		{
		case EPaper2DPlusEffectProfileValidationSeverity::Error:
			return EEffectProfileRowIssueState::Error;
		case EPaper2DPlusEffectProfileValidationSeverity::Warning:
			return EEffectProfileRowIssueState::Warning;
		default:
			return EEffectProfileRowIssueState::Info;
		}
	}

	bool IssueMatchesEntry(
		const FPaper2DPlusEffectProfileValidationIssue& Issue,
		const FPaper2DPlusEffectProfileEntry& Entry)
	{
		if (Issue.EffectName.IsNone())
		{
			return false;
		}
		if (!Entry.EffectName.IsNone()
			&& Entry.EffectName.ToString().Equals(Issue.EffectName.ToString(), ESearchCase::IgnoreCase))
		{
			return true;
		}
		const FSoftObjectPath EffectPath = Entry.GetEffectFlipbookPath();
		return !EffectPath.IsNull()
			&& EffectPath.GetAssetName().Equals(Issue.EffectName.ToString(), ESearchCase::IgnoreCase);
	}
}

FText FEffectProfileIntakeResult::BuildSummary() const
{
	if (RequestedCount <= 0)
	{
		return LOCTEXT("IntakeNothing", "No assets were supplied.");
	}
	FString Detail;
	if (!DuplicateNames.IsEmpty())
	{
		Detail += FString::Printf(
			TEXT(" Duplicates: %s."),
			*FString::Join(DuplicateNames, TEXT(", ")));
	}
	if (!RejectedNames.IsEmpty())
	{
		Detail += FString::Printf(
			TEXT(" Rejected: %s."),
			*FString::Join(RejectedNames, TEXT(", ")));
	}
	const FText Summary = FText::Format(
		LOCTEXT("IntakeSummary", "Added {0}. Skipped {1} duplicate(s). Rejected {2} incompatible asset(s)."),
		FText::AsNumber(AddedCount),
		FText::AsNumber(DuplicateCount),
		FText::AsNumber(RejectedCount));
	return Detail.IsEmpty()
		? Summary
		: FText::Format(LOCTEXT("IntakeSummaryWithNames", "{0}{1}"), Summary, FText::FromString(Detail));
}

void FEffectProfileEditorModel::Initialize(
	UPaper2DPlusEffectProfileAsset* InAsset,
	bool bRestoreSelection)
{
	Asset = InAsset;
	bUseSelectionConfig = bRestoreSelection;
	SelectedEffectPath.Reset();
	SelectedFlipbook.Reset();
	DescriptorFilter.Reset();
	if (bUseSelectionConfig)
	{
		LoadSelectionFromConfig();
	}
	RefreshFromAsset();
}

FSoftObjectPath FEffectProfileEditorModel::MakeEffectPath(const UPaperFlipbook* Flipbook)
{
	return Flipbook ? FSoftObjectPath(Flipbook) : FSoftObjectPath();
}

FString FEffectProfileEditorModel::NormalizedPath(const UPaperFlipbook* Flipbook)
{
	return NormalizedPath(MakeEffectPath(Flipbook));
}

FString FEffectProfileEditorModel::NormalizedPath(const FSoftObjectPath& Path)
{
	return Path.IsNull()
		? FString()
		: UPaper2DPlusEffectProfileAsset::GetCanonicalEffectFlipbookPathNoLoad(Path)
			.ToString().ToLower();
}

bool FEffectProfileEditorModel::IsStrictChildOf(FGameplayTag Tag, FGameplayTag Root)
{
	return Tag.IsValid() && Root.IsValid() && Tag != Root && Tag.MatchesTag(Root);
}

int32 FEffectProfileEditorModel::ResolveIndex(const FSoftObjectPath& EffectPath) const
{
	const UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get();
	const FString Wanted = NormalizedPath(EffectPath);
	if (!CurrentAsset || Wanted.IsEmpty())
	{
		return INDEX_NONE;
	}
	return CurrentAsset->Effects.IndexOfByPredicate([&Wanted](const FPaper2DPlusEffectProfileEntry& Entry)
	{
		return NormalizedPath(Entry.GetEffectFlipbookPath()) == Wanted;
	});
}

int32 FEffectProfileEditorModel::ResolveSelectedIndex() const
{
	return ResolveIndex(SelectedEffectPath);
}

UPaperFlipbook* FEffectProfileEditorModel::GetSelectedFlipbook() const
{
	return ResolveSelectedIndex() != INDEX_NONE ? SelectedFlipbook.Get() : nullptr;
}

const FPaper2DPlusEffectProfileEntry* FEffectProfileEditorModel::GetSelectedEntry() const
{
	const int32 Index = ResolveSelectedIndex();
	const UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get();
	return CurrentAsset && CurrentAsset->Effects.IsValidIndex(Index)
		? &CurrentAsset->Effects[Index]
		: nullptr;
}

void FEffectProfileEditorModel::RefreshFromAsset()
{
	const FSoftObjectPath OldPath = SelectedEffectPath;
	UPaperFlipbook* OldFlipbook = SelectedFlipbook.Get();
	UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get();

	if (!CurrentAsset)
	{
		SelectedEffectPath.Reset();
		SelectedFlipbook.Reset();
		RowIssueStates.Reset();
	}
	else
	{
		// A live object pointer carries selection across an editor rename. The path is refreshed only when
		// that exact object is still a library member; removing a row deliberately leaves the last path
		// reserved so one undo can restore the same selection.
		if (UPaperFlipbook* LiveSelected = SelectedFlipbook.Get())
		{
			const FSoftObjectPath LivePath = MakeEffectPath(LiveSelected);
			int32 LiveIndex = ResolveIndex(LivePath);
			if (LiveIndex == INDEX_NONE && !OldPath.IsNull())
			{
				// A live selected object can outlast its serialized soft path during an editor rename.
				// Match the exact pre-rename identity without resolving/loading that stale path, then
				// update the row to the object's new path as part of the rename fixup.
				LiveIndex = CurrentAsset->Effects.IndexOfByPredicate(
					[&OldPath](const FPaper2DPlusEffectProfileEntry& Entry)
					{
						return Entry.GetEffectFlipbookPath() == OldPath;
					});
				if (CurrentAsset->Effects.IsValidIndex(LiveIndex))
				{
					CurrentAsset->Effects[LiveIndex].EffectFlipbook = LiveSelected;
					CurrentAsset->MarkPackageDirty();
				}
			}
			if (LiveIndex != INDEX_NONE)
			{
				SelectedEffectPath = LivePath;
			}
			else
			{
				SelectedFlipbook.Reset();
			}
		}

		if (!SelectedEffectPath.IsNull() && !SelectedFlipbook.IsValid())
		{
			int32 Index = ResolveIndex(SelectedEffectPath);
			if (Index == INDEX_NONE)
			{
				// A redirect-resolved old config path may still name the same renamed object.
				UObject* ResolvedObject = SelectedEffectPath.ResolveObject();
				if (!ResolvedObject)
				{
					ResolvedObject = SelectedEffectPath.TryLoad();
				}
				UPaperFlipbook* Resolved = Cast<UPaperFlipbook>(ResolvedObject);
				if (Resolved)
				{
					Index = ResolveIndex(MakeEffectPath(Resolved));
				}
			}
			if (CurrentAsset->Effects.IsValidIndex(Index))
			{
				const FPaper2DPlusEffectProfileEntry& Entry = CurrentAsset->Effects[Index];
				SelectedEffectPath = Entry.GetEffectFlipbookPath();
				SelectedFlipbook.Reset(Entry.LoadEffectFlipbook());
			}
		}

		RebuildValidationProjection();
	}

	++RefreshRevision;
	if (bUseSelectionConfig && OldPath != SelectedEffectPath)
	{
		SaveSelectionToConfig();
	}
	SourceChanged.Broadcast();
	if (OldPath != SelectedEffectPath || OldFlipbook != SelectedFlipbook.Get())
	{
		SelectedEffectChanged.Broadcast(GetSelectedFlipbook());
	}
}

void FEffectProfileEditorModel::RebuildValidationProjection()
{
	RowIssueStates.Reset();
	const UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get();
	if (!CurrentAsset)
	{
		return;
	}

	TArray<FPaper2DPlusEffectProfileValidationIssue> Issues;
	CurrentAsset->ValidateEffectProfileAsset(Issues);
	for (const FPaper2DPlusEffectProfileEntry& Entry : CurrentAsset->Effects)
	{
		const FString Path = NormalizedPath(Entry.GetEffectFlipbookPath());
		if (Path.IsEmpty())
		{
			continue;
		}
		EEffectProfileRowIssueState Worst = EEffectProfileRowIssueState::None;
		for (const FPaper2DPlusEffectProfileValidationIssue& Issue : Issues)
		{
			if (EffectProfileEditorModelPrivate::IssueMatchesEntry(Issue, Entry))
			{
				const EEffectProfileRowIssueState State =
					EffectProfileEditorModelPrivate::ProjectIssueState(Issue.Severity);
				if (EffectProfileEditorModelPrivate::IssueRank(State)
					> EffectProfileEditorModelPrivate::IssueRank(Worst))
				{
					Worst = State;
				}
			}
		}
		if (Worst != EEffectProfileRowIssueState::None)
		{
			RowIssueStates.Add(Path, Worst);
		}
	}
}

void FEffectProfileEditorModel::SetSelectionInternal(
	const FSoftObjectPath& NewPath,
	UPaperFlipbook* NewFlipbook,
	bool bPersist)
{
	const bool bChanged = SelectedEffectPath != NewPath || SelectedFlipbook.Get() != NewFlipbook;
	SelectedEffectPath = NewPath;
	SelectedFlipbook.Reset(NewFlipbook);
	if (bPersist && bUseSelectionConfig)
	{
		SaveSelectionToConfig();
	}
	if (bChanged)
	{
		SourceChanged.Broadcast();
		SelectedEffectChanged.Broadcast(NewFlipbook);
	}
}

bool FEffectProfileEditorModel::SelectEffectPath(const FSoftObjectPath& EffectPath)
{
	const int32 Index = ResolveIndex(EffectPath);
	UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get();
	if (!CurrentAsset || !CurrentAsset->Effects.IsValidIndex(Index))
	{
		return false;
	}
	const FPaper2DPlusEffectProfileEntry& Entry = CurrentAsset->Effects[Index];
	UPaperFlipbook* Flipbook = Entry.LoadEffectFlipbook();
	SetSelectionInternal(Entry.GetEffectFlipbookPath(), Flipbook, true);
	return true;
}

bool FEffectProfileEditorModel::SelectEffectIdentityString(const FString& Identity)
{
	if (Identity.IsEmpty())
	{
		return false;
	}
	const FSoftObjectPath DirectPath(Identity);
	if (!DirectPath.IsNull() && SelectEffectPath(DirectPath))
	{
		return true;
	}
	UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get();
	if (!CurrentAsset)
	{
		return false;
	}
	for (const FPaper2DPlusEffectProfileEntry& Entry : CurrentAsset->Effects)
	{
		const FSoftObjectPath EffectPath = Entry.GetEffectFlipbookPath();
		if (EffectPath.IsNull())
		{
			continue;
		}
		if (EffectPath.GetAssetName().Equals(Identity, ESearchCase::IgnoreCase)
			|| (!Entry.EffectName.IsNone()
				&& Entry.EffectName.ToString().Equals(Identity, ESearchCase::IgnoreCase))
			|| (!Entry.DisplayLabel.IsEmpty()
				&& Entry.DisplayLabel.ToString().Equals(Identity, ESearchCase::IgnoreCase)))
		{
			return SelectEffectPath(EffectPath);
		}
	}
	return false;
}

void FEffectProfileEditorModel::ClearSelection()
{
	SetSelectionInternal(FSoftObjectPath(), nullptr, true);
}

FEffectProfileIntakeResult FEffectProfileEditorModel::AddFlipbooks(
	const TArray<UPaperFlipbook*>& Flipbooks)
{
	FEffectProfileIntakeResult Result;
	Result.RequestedCount = Flipbooks.Num();
	UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get();
	if (!CurrentAsset)
	{
		Result.RejectedCount = Flipbooks.Num();
		return Result;
	}

	TSet<FString> ExistingPaths;
	for (const FPaper2DPlusEffectProfileEntry& Entry : CurrentAsset->Effects)
	{
		const FString Path = NormalizedPath(Entry.GetEffectFlipbookPath());
		if (!Path.IsEmpty())
		{
			ExistingPaths.Add(Path);
		}
	}

	TArray<UPaperFlipbook*> Accepted;
	TSet<FString> BatchPaths;
	for (UPaperFlipbook* Flipbook : Flipbooks)
	{
		if (!Flipbook)
		{
			++Result.RejectedCount;
			Result.RejectedNames.Add(TEXT("(null)"));
			continue;
		}
		const FString Path = NormalizedPath(Flipbook);
		if (Path.IsEmpty())
		{
			++Result.RejectedCount;
			Result.RejectedNames.Add(Flipbook->GetName());
			continue;
		}
		if (ExistingPaths.Contains(Path) || BatchPaths.Contains(Path))
		{
			++Result.DuplicateCount;
			Result.DuplicateNames.Add(Flipbook->GetName());
			continue;
		}
		BatchPaths.Add(Path);
		Accepted.Add(Flipbook);
	}

	if (Accepted.IsEmpty())
	{
		return Result;
	}

	const bool bHadResolvedSelection = HasResolvedSelection();
	{
		FScopedTransaction Transaction(LOCTEXT("AddEffects", "Add Effect Flipbooks"));
		CurrentAsset->Modify();
		for (UPaperFlipbook* Flipbook : Accepted)
		{
			FPaper2DPlusEffectProfileEntry& Entry = CurrentAsset->Effects.AddDefaulted_GetRef();
			Entry.EffectFlipbook = Flipbook;
			Result.AddedPaths.Add(MakeEffectPath(Flipbook));
		}
		CurrentAsset->MarkPackageDirty();
	}
	Result.AddedCount = Accepted.Num();
	++MutationCount;
	if (!bHadResolvedSelection)
	{
		SelectedEffectPath = Result.AddedPaths[0];
		SelectedFlipbook.Reset(Accepted[0]);
		if (bUseSelectionConfig)
		{
			SaveSelectionToConfig();
		}
	}
	CommitMutationFinished();
	if (!bHadResolvedSelection)
	{
		// Selection fields were seeded before refresh so the source projects the first row immediately;
		// notify preview/future Layers observers explicitly about the empty-to-selected transition.
		SelectedEffectChanged.Broadcast(GetSelectedFlipbook());
	}
	return Result;
}

FEffectProfileIntakeResult FEffectProfileEditorModel::AddAssetData(
	const TArray<FAssetData>& Assets)
{
	FEffectProfileIntakeResult Result;
	Result.RequestedCount = Assets.Num();
	TArray<UPaperFlipbook*> Flipbooks;
	for (const FAssetData& AssetData : Assets)
	{
		UPaperFlipbook* Flipbook = Cast<UPaperFlipbook>(AssetData.GetAsset());
		if (Flipbook)
		{
			Flipbooks.Add(Flipbook);
		}
		else
		{
			++Result.RejectedCount;
			Result.RejectedNames.Add(AssetData.AssetName.IsNone()
				? TEXT("(unknown asset)") : AssetData.AssetName.ToString());
		}
	}
	FEffectProfileIntakeResult FlipbookResult = AddFlipbooks(Flipbooks);
	Result.AddedCount = FlipbookResult.AddedCount;
	Result.DuplicateCount = FlipbookResult.DuplicateCount;
	Result.AddedPaths = MoveTemp(FlipbookResult.AddedPaths);
	Result.DuplicateNames = MoveTemp(FlipbookResult.DuplicateNames);
	Result.RejectedCount += FlipbookResult.RejectedCount;
	Result.RejectedNames.Append(FlipbookResult.RejectedNames);
	return Result;
}

bool FEffectProfileEditorModel::RemoveSelected()
{
	UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get();
	const int32 Index = ResolveSelectedIndex();
	if (!CurrentAsset || !CurrentAsset->Effects.IsValidIndex(Index))
	{
		return false;
	}
	{
		FScopedTransaction Transaction(LOCTEXT("RemoveEffect", "Remove Effect Flipbook"));
		CurrentAsset->Modify();
		CurrentAsset->Effects.RemoveAt(Index);
		CurrentAsset->MarkPackageDirty();
	}
	++MutationCount;
	CommitMutationFinished();
	return true;
}

bool FEffectProfileEditorModel::CanMoveSelected(int32 Direction) const
{
	const UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get();
	const int32 Index = ResolveSelectedIndex();
	return Direction != 0 && CurrentAsset && CurrentAsset->Effects.IsValidIndex(Index)
		&& CurrentAsset->Effects.IsValidIndex(Index + Direction);
}

bool FEffectProfileEditorModel::MoveSelected(int32 Direction)
{
	UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get();
	const int32 Index = ResolveSelectedIndex();
	const int32 Target = Index + Direction;
	if (!CurrentAsset || !CurrentAsset->Effects.IsValidIndex(Index)
		|| !CurrentAsset->Effects.IsValidIndex(Target) || Direction == 0)
	{
		return false;
	}
	{
		FScopedTransaction Transaction(LOCTEXT("ReorderEffect", "Reorder Effect Flipbook"));
		CurrentAsset->Modify();
		CurrentAsset->Effects.Swap(Index, Target);
		CurrentAsset->MarkPackageDirty();
	}
	++MutationCount;
	CommitMutationFinished();
	return true;
}

bool FEffectProfileEditorModel::SetSelectedDisplayLabel(const FText& NewLabel)
{
	UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get();
	const int32 Index = ResolveSelectedIndex();
	if (!CurrentAsset || !CurrentAsset->Effects.IsValidIndex(Index)
		|| CurrentAsset->Effects[Index].DisplayLabel.EqualTo(NewLabel))
	{
		return false;
	}
	{
		FScopedTransaction Transaction(LOCTEXT("SetEffectLabel", "Set Effect Display Label"));
		CurrentAsset->Modify();
		CurrentAsset->Effects[Index].DisplayLabel = NewLabel;
		CurrentAsset->MarkPackageDirty();
	}
	++MutationCount;
	CommitMutationFinished();
	return true;
}

bool FEffectProfileEditorModel::SetSelectedTypeTag(FGameplayTag NewType)
{
	const FGameplayTag TypeRoot = Paper2DPlusEffectTags::Type.GetTag();
	if (NewType.IsValid() && !IsStrictChildOf(NewType, TypeRoot))
	{
		return false;
	}
	UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get();
	const int32 Index = ResolveSelectedIndex();
	if (!CurrentAsset || !CurrentAsset->Effects.IsValidIndex(Index)
		|| CurrentAsset->Effects[Index].TypeTag == NewType)
	{
		return false;
	}
	{
		FScopedTransaction Transaction(LOCTEXT("SetEffectType", "Set Effect Type"));
		CurrentAsset->Modify();
		CurrentAsset->Effects[Index].TypeTag = NewType;
		CurrentAsset->MarkPackageDirty();
	}
	++MutationCount;
	CommitMutationFinished();
	return true;
}

bool FEffectProfileEditorModel::SetSelectedDescriptorTags(
	const FGameplayTagContainer& NewDescriptors)
{
	const FGameplayTag DescriptorRoot = Paper2DPlusEffectTags::Descriptor.GetTag();
	for (const FGameplayTag& Tag : NewDescriptors)
	{
		if (!IsStrictChildOf(Tag, DescriptorRoot))
		{
			return false;
		}
	}
	UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get();
	const int32 Index = ResolveSelectedIndex();
	if (!CurrentAsset || !CurrentAsset->Effects.IsValidIndex(Index)
		|| CurrentAsset->Effects[Index].DescriptorTags == NewDescriptors)
	{
		return false;
	}
	{
		FScopedTransaction Transaction(LOCTEXT("SetEffectDescriptors", "Set Effect Descriptors"));
		CurrentAsset->Modify();
		CurrentAsset->Effects[Index].DescriptorTags = NewDescriptors;
		CurrentAsset->MarkPackageDirty();
	}
	++MutationCount;
	CommitMutationFinished();
	return true;
}

bool FEffectProfileEditorModel::OpenSelectedSource() const
{
	UPaperFlipbook* Flipbook = GetSelectedFlipbook();
	UAssetEditorSubsystem* AssetEditors = GEditor
		? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	return Flipbook && AssetEditors && AssetEditors->OpenEditorForAsset(Flipbook);
}

void FEffectProfileEditorModel::SetDescriptorFilter(
	const FGameplayTagContainer& NewFilter)
{
	if (DescriptorFilter == NewFilter)
	{
		return;
	}
	DescriptorFilter = NewFilter;
	SourceChanged.Broadcast();
}

void FEffectProfileEditorModel::ClearDescriptorFilter()
{
	SetDescriptorFilter(FGameplayTagContainer());
}

bool FEffectProfileEditorModel::PassesDescriptorFilter(
	const FPaper2DPlusEffectProfileEntry& Entry) const
{
	for (const FGameplayTag& FilterTag : DescriptorFilter)
	{
		if (!Entry.DescriptorTags.HasTag(FilterTag))
		{
			return false;
		}
	}
	return true;
}

int32 FEffectProfileEditorModel::GetLibraryEntryCount() const
{
	const UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get();
	if (!CurrentAsset)
	{
		return 0;
	}
	TSet<FString> Seen;
	for (const FPaper2DPlusEffectProfileEntry& Entry : CurrentAsset->Effects)
	{
		const FString Path = NormalizedPath(Entry.GetEffectFlipbookPath());
		if (!Path.IsEmpty())
		{
			Seen.Add(Path);
		}
	}
	return Seen.Num();
}

int32 FEffectProfileEditorModel::GetVisibleEntryCount() const
{
	TArray<FProfilePickerItem> Items;
	GetItems(Items);
	return Items.Num();
}

UPaperFlipbook* FEffectProfileEditorModel::ResolveFlipbook(
	const FProfileItemIdentity& Identity) const
{
	const FPaper2DPlusEffectProfileEntry* Entry = ResolveEntry(Identity);
	return Entry ? Entry->LoadEffectFlipbook() : nullptr;
}

const FPaper2DPlusEffectProfileEntry* FEffectProfileEditorModel::ResolveEntry(
	const FProfileItemIdentity& Identity) const
{
	if (Identity.SourceType != GetSourceType())
	{
		return nullptr;
	}
	const int32 Index = ResolveIndex(Identity.ObjectPath);
	const UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get();
	return CurrentAsset && CurrentAsset->Effects.IsValidIndex(Index)
		? &CurrentAsset->Effects[Index] : nullptr;
}

EEffectProfileRowIssueState FEffectProfileEditorModel::GetRowIssueState(
	const FSoftObjectPath& EffectPath) const
{
	const EEffectProfileRowIssueState* State = RowIssueStates.Find(NormalizedPath(EffectPath));
	return State ? *State : EEffectProfileRowIssueState::None;
}

FText FEffectProfileEditorModel::GetRowIssueStateText(
	const FSoftObjectPath& EffectPath) const
{
	switch (GetRowIssueState(EffectPath))
	{
	case EEffectProfileRowIssueState::Error:
		return LOCTEXT("RowError", "Error");
	case EEffectProfileRowIssueState::Warning:
		return LOCTEXT("RowWarning", "Warning");
	case EEffectProfileRowIssueState::Info:
		return LOCTEXT("RowInfo", "Info");
	default:
		return FText::GetEmpty();
	}
}

FName FEffectProfileEditorModel::GetSourceType() const
{
	return EffectProfileEditorModelPrivate::SourceType;
}

FString FEffectProfileEditorModel::GetLogicalCatalogScope() const
{
	const UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get();
	return FString::Printf(TEXT("Effect:%s"),
		CurrentAsset ? *CurrentAsset->GetPathName() : TEXT("None"));
}

void FEffectProfileEditorModel::GetItems(TArray<FProfilePickerItem>& OutItems) const
{
	OutItems.Reset();
	const UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get();
	if (!CurrentAsset)
	{
		return;
	}
	TSet<FString> Seen;
	for (int32 Index = 0; Index < CurrentAsset->Effects.Num(); ++Index)
	{
		const FPaper2DPlusEffectProfileEntry& Entry = CurrentAsset->Effects[Index];
		const FSoftObjectPath Path = Entry.GetEffectFlipbookPath();
		const FString Normalized = NormalizedPath(Path);
		if (Normalized.IsEmpty() || Seen.Contains(Normalized))
		{
			continue;
		}
		Seen.Add(Normalized);
		if (!PassesDescriptorFilter(Entry))
		{
			continue;
		}

		const FString EffectName = Path.GetAssetName();
		FProfilePickerItem Item;
		Item.Identity.SourceType = GetSourceType();
		Item.Identity.ObjectPath = Path;
		Item.Identity.FallbackKey = EffectName;
		Item.Label = Entry.DisplayLabel.IsEmpty()
			? FText::FromString(EffectName) : Entry.DisplayLabel;
		Item.Aliases.Add(EffectName);
		Item.Aliases.Add(Path.ToString());
		if (!Entry.DisplayLabel.IsEmpty())
		{
			Item.Aliases.Add(Entry.DisplayLabel.ToString());
		}
		Item.Group = Entry.TypeTag.IsValid()
			? EffectProfileEditorModelPrivate::TagLeaf(Entry.TypeTag)
			: LOCTEXT("Unclassified", "Unclassified").ToString();
		Item.SearchTags = Entry.DescriptorTags;
		if (Entry.TypeTag.IsValid())
		{
			Item.SearchTags.AddTag(Entry.TypeTag);
		}
		const FString DescriptorText = Entry.DescriptorTags.IsEmpty()
			? LOCTEXT("NoDescriptors", "No descriptors").ToString()
			: Entry.DescriptorTags.ToStringSimple();
		Item.SecondaryText = FText::Format(
			LOCTEXT("EffectSecondary", "{0} - {1}"),
			FText::FromString(EffectName),
			FText::FromString(DescriptorText));
		Item.CanonicalOrder = Index;
		OutItems.Add(MoveTemp(Item));
	}
}

FProfileItemIdentity FEffectProfileEditorModel::GetSelectedIdentity() const
{
	FProfileItemIdentity Identity;
	if (!SelectedEffectPath.IsNull())
	{
		Identity.SourceType = GetSourceType();
		Identity.ObjectPath = SelectedEffectPath;
		Identity.FallbackKey = SelectedEffectPath.GetAssetName();
	}
	return Identity;
}

bool FEffectProfileEditorModel::SelectItem(const FProfileItemIdentity& Identity)
{
	return Identity.SourceType == GetSourceType() && SelectEffectPath(Identity.ObjectPath);
}

FString FEffectProfileEditorModel::SelectionConfigSection(
	const UPaper2DPlusEffectProfileAsset* InAsset)
{
	const FString Scope = InAsset ? InAsset->GetPathName() : TEXT("None");
	return FString::Printf(TEXT("%s.%08X"),
		EffectProfileEditorModelPrivate::SelectionConfigRoot,
		FCrc::StrCrc32(*Scope));
}

void FEffectProfileEditorModel::LoadSelectionFromConfig()
{
	SelectedEffectPath.Reset();
	if (!GConfig || !Asset.IsValid())
	{
		return;
	}
	const FString Section = SelectionConfigSection(Asset.Get());
	FString StoredScope;
	FString StoredPath;
	if (GConfig->GetString(*Section, TEXT("Scope"), StoredScope, GEditorPerProjectIni)
		&& StoredScope.Equals(Asset->GetPathName(), ESearchCase::CaseSensitive)
		&& GConfig->GetString(*Section, TEXT("SelectedEffect"), StoredPath, GEditorPerProjectIni))
	{
		SelectedEffectPath = FSoftObjectPath(StoredPath);
	}
}

void FEffectProfileEditorModel::SaveSelectionToConfig() const
{
	if (!GConfig || !Asset.IsValid())
	{
		return;
	}
	const FString Section = SelectionConfigSection(Asset.Get());
	GConfig->SetString(*Section, TEXT("Scope"), *Asset->GetPathName(), GEditorPerProjectIni);
	GConfig->SetString(*Section, TEXT("SelectedEffect"), *SelectedEffectPath.ToString(), GEditorPerProjectIni);
	// Intentionally no Flush: the engine persists EditorPerProject config safely.
}

void FEffectProfileEditorModel::ResetSelectionConfigForTests(
	const UPaper2DPlusEffectProfileAsset* InAsset)
{
	if (GConfig)
	{
		GConfig->EmptySection(*SelectionConfigSection(InAsset), GEditorPerProjectIni);
	}
}

void FEffectProfileEditorModel::CommitMutationFinished()
{
	if (UPaper2DPlusEffectProfileAsset* CurrentAsset = Asset.Get())
	{
		FPaper2DPlusEffectLibraryIndex::Get()->PublishChange(
			EPaper2DPlusEffectLibraryChangeDomain::EffectProfile,
			FSoftObjectPath(CurrentAsset));
	}
	RefreshFromAsset();
}

void FEffectProfileEditorModel::RefreshAfterExternalMutation()
{
	CommitMutationFinished();
}

void FEffectProfilePlaybackState::SetFlipbook(UPaperFlipbook* InFlipbook)
{
	Flipbook = InFlipbook;
	CurrentKeyFrame = 0;
	FrameRunProgress = 0;
	TimeAccumulator = 0.0f;
	bPlaying = false;
}

int32 FEffectProfilePlaybackState::GetNumKeyFrames() const
{
	return Flipbook.IsValid() ? Flipbook->GetNumKeyFrames() : 0;
}

void FEffectProfilePlaybackState::Play()
{
	if (GetNumKeyFrames() > 0)
	{
		bPlaying = true;
	}
}

void FEffectProfilePlaybackState::SeekKeyFrame(int32 KeyFrameIndex)
{
	const int32 NumFrames = GetNumKeyFrames();
	CurrentKeyFrame = NumFrames > 0 ? FMath::Clamp(KeyFrameIndex, 0, NumFrames - 1) : 0;
	FrameRunProgress = 0;
	TimeAccumulator = 0.0f;
}

void FEffectProfilePlaybackState::Tick(float DeltaSeconds)
{
	UPaperFlipbook* CurrentFlipbook = Flipbook.Get();
	const int32 NumFrames = GetNumKeyFrames();
	if (!bPlaying || !CurrentFlipbook || NumFrames <= 0 || DeltaSeconds <= 0.0f)
	{
		return;
	}
	if (NumFrames == 1)
	{
		CurrentKeyFrame = 0;
		if (!bLooping)
		{
			bPlaying = false;
		}
		return;
	}

	float FPS = CurrentFlipbook->GetFramesPerSecond();
	if (FPS <= 0.0f)
	{
		FPS = 15.0f;
	}
	const float SecondsPerFrame = 1.0f / FPS;
	TimeAccumulator += DeltaSeconds;
	while (bPlaying && TimeAccumulator >= SecondsPerFrame)
	{
		TimeAccumulator -= SecondsPerFrame;
		++FrameRunProgress;
		const int32 FrameRun = FMath::Max(
			CurrentFlipbook->GetKeyFrameChecked(CurrentKeyFrame).FrameRun,
			1);
		if (FrameRunProgress < FrameRun)
		{
			continue;
		}
		FrameRunProgress = 0;
		if (CurrentKeyFrame + 1 < NumFrames)
		{
			++CurrentKeyFrame;
		}
		else if (bLooping)
		{
			CurrentKeyFrame = 0;
		}
		else
		{
			bPlaying = false;
		}
	}
}

FText FEffectProfilePlaybackState::GetFrameStatusText() const
{
	const int32 NumFrames = GetNumKeyFrames();
	if (NumFrames <= 0)
	{
		return LOCTEXT("NoPreviewFrames", "No frames");
	}
	return FText::Format(
		LOCTEXT("PreviewFrameStatus", "Frame {0} of {1}"),
		FText::AsNumber(CurrentKeyFrame + 1),
		FText::AsNumber(NumFrames));
}

#undef LOCTEXT_NAMESPACE
