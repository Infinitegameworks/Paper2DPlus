// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CombatProfileEditor/CombatProfileEditorSession.h"

#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "PaperFlipbook.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "CombatProfileEditorSession"

namespace CombatProfileEditorSessionPrivate
{
	const FName AttackSourceType(TEXT("CombatAttack"));

	FString FirstTagLeaf(const FGameplayTagContainer& Tags)
	{
		TArray<FGameplayTag> Sorted;
		Tags.GetGameplayTagArray(Sorted);
		Sorted.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.GetTagName().LexicalLess(B.GetTagName());
		});
		if (Sorted.IsEmpty())
		{
			return FString();
		}
		FString Result = Sorted[0].ToString();
		int32 LastDot = INDEX_NONE;
		return Result.FindLastChar(TEXT('.'), LastDot) ? Result.Mid(LastDot + 1) : Result;
	}
}

FText FCombatProfileEffectiveAttackSummary::GetBaseWeightProvenanceText() const
{
	switch (BaseWeightSource)
	{
	case ECombatProfileValueSource::Move:
		return LOCTEXT("WeightFromMove", "Move override");
	case ECombatProfileValueSource::AttackTag:
		return LOCTEXT("WeightFromTag", "Attack-tag default");
	case ECombatProfileValueSource::Global:
		return LOCTEXT("WeightFromGlobal", "Global profile value");
	default:
		return LOCTEXT("WeightBuiltIn", "Built-in default");
	}
}

FCombatProfileEditorSession::FCombatProfileEditorSession(UPaper2DPlusCombatProfileAsset* InAsset)
	: Asset(InAsset)
{
	PreviewContext.DistanceToTarget = 100.0f;
	PreviewContext.SelfHealthPercent = 1.0f;
	PreviewContext.TargetHealthPercent = 1.0f;
	RefreshFromAsset();
}

bool FCombatProfileEditorSession::CatalogContainsMove(FName MoveName) const
{
	return Catalog.ContainsByPredicate([MoveName](const FPaper2DPlusCombatAttackDerivedData& Row)
	{
		return Row.MoveName.IsEqual(MoveName, ENameCase::IgnoreCase);
	});
}

const FFlipbookProfileEntry* FCombatProfileEditorSession::FindMoveByIdentity(
	const FProfileItemIdentity& Identity) const
{
	const UPaper2DPlusCombatProfileAsset* Combat = Asset.Get();
	const UPaper2DPlusCharacterProfileAsset* Character = Combat ? Combat->CharacterProfile : nullptr;
	if (!Character || Identity.SourceType != CombatProfileEditorSessionPrivate::AttackSourceType)
	{
		return nullptr;
	}

	if (!Identity.ObjectPath.IsNull())
	{
		for (const FFlipbookProfileEntry& Entry : Character->Flipbooks)
		{
			if (Entry.Identity.Flipbook.ToSoftObjectPath() == Identity.ObjectPath)
			{
				return CatalogContainsMove(FName(*Entry.Identity.FlipbookName)) ? &Entry : nullptr;
			}
		}
	}

	if (Identity.FallbackKey.IsEmpty())
	{
		return nullptr;
	}
	const FFlipbookProfileEntry* Match = nullptr;
	for (const FFlipbookProfileEntry& Entry : Character->Flipbooks)
	{
		if (Entry.Identity.FlipbookName.Equals(Identity.FallbackKey, ESearchCase::IgnoreCase)
			&& CatalogContainsMove(FName(*Entry.Identity.FlipbookName)))
		{
			if (Match)
			{
				return nullptr; // Ambiguous legacy identity fails closed.
			}
			Match = &Entry;
		}
	}
	return Match;
}

FProfileItemIdentity FCombatProfileEditorSession::MakeAttackIdentity(FName MoveName) const
{
	FProfileItemIdentity Identity;
	Identity.SourceType = CombatProfileEditorSessionPrivate::AttackSourceType;
	Identity.FallbackKey = MoveName.ToString();
	const UPaper2DPlusCombatProfileAsset* Combat = Asset.Get();
	const UPaper2DPlusCharacterProfileAsset* Character = Combat ? Combat->CharacterProfile : nullptr;
	if (!Character)
	{
		return Identity;
	}

	const FFlipbookProfileEntry* Match = nullptr;
	for (const FFlipbookProfileEntry& Entry : Character->Flipbooks)
	{
		if (Entry.Identity.FlipbookName.Equals(Identity.FallbackKey, ESearchCase::IgnoreCase))
		{
			if (Match)
			{
				return Identity; // Leave ambiguous names as explicit fallback-only identities.
			}
			Match = &Entry;
		}
	}
	if (Match)
	{
		Identity.ObjectPath = Match->Identity.Flipbook.ToSoftObjectPath();
		Identity.FallbackKey = Match->Identity.FlipbookName;
	}
	return Identity;
}

bool FCombatProfileEditorSession::ResolveAttackIdentity(
	const FProfileItemIdentity& Candidate,
	FProfileItemIdentity& OutResolved) const
{
	OutResolved = FProfileItemIdentity();
	if (const FFlipbookProfileEntry* Entry = FindMoveByIdentity(Candidate))
	{
		OutResolved = MakeAttackIdentity(FName(*Entry->Identity.FlipbookName));
		return true;
	}

	// Catalogs can include an intentional name-only tuning row when no Character move
	// is bound. It remains selectable only when the fallback name is unique.
	if (Candidate.SourceType != CombatProfileEditorSessionPrivate::AttackSourceType
		|| Candidate.FallbackKey.IsEmpty())
	{
		return false;
	}
	const FPaper2DPlusCombatAttackDerivedData* Match = nullptr;
	for (const FPaper2DPlusCombatAttackDerivedData& Row : Catalog)
	{
		if (Row.MoveName.ToString().Equals(Candidate.FallbackKey, ESearchCase::IgnoreCase))
		{
			if (Match)
			{
				return false;
			}
			Match = &Row;
		}
	}
	if (!Match)
	{
		return false;
	}
	OutResolved = MakeAttackIdentity(Match->MoveName);
	return true;
}

void FCombatProfileEditorSession::BroadcastRefresh(bool bSelectionChanged)
{
	++Revision;
	DataChanged.Broadcast();
	if (bSelectionChanged)
	{
		SelectionChanged.Broadcast(SelectedAttack);
	}
}

void FCombatProfileEditorSession::RefreshFromAsset()
{
	UPaper2DPlusCombatProfileAsset* Combat = Asset.Get();
	const FProfileItemIdentity PreviousSelection = SelectedAttack;
	Catalog.Reset();
	RankedOptions.Reset();
	if (!Combat)
	{
		SelectedAttack = FProfileItemIdentity();
		BroadcastRefresh(PreviousSelection.IsValid());
		return;
	}

	Combat->BuildAttackCatalog(Catalog);
	FProfileItemIdentity Resolved;
	if (PreviousSelection.IsValid() && ResolveAttackIdentity(PreviousSelection, Resolved))
	{
		SelectedAttack = Resolved;
	}
	else
	{
		SelectedAttack = FProfileItemIdentity();
	}
	Combat->ScoreAttackOptions(PreviewContext, RankedOptions, ScoringProfileName);
	BroadcastRefresh(!PreviousSelection.Matches(SelectedAttack));
}

void FCombatProfileEditorSession::RefreshScores()
{
	RankedOptions.Reset();
	if (const UPaper2DPlusCombatProfileAsset* Combat = Asset.Get())
	{
		Combat->ScoreAttackOptions(PreviewContext, RankedOptions, ScoringProfileName);
	}
	++Revision;
	ScoreChanged.Broadcast();
}

void FCombatProfileEditorSession::SetScoringProfileName(FName InName)
{
	if (ScoringProfileName == InName)
	{
		return;
	}
	ScoringProfileName = InName;
	RefreshScores();
}

bool FCombatProfileEditorSession::SelectAttack(const FProfileItemIdentity& Identity)
{
	FProfileItemIdentity Resolved;
	if (!ResolveAttackIdentity(Identity, Resolved))
	{
		return false;
	}
	if (SelectedAttack.Matches(Resolved))
	{
		return true;
	}
	SelectedAttack = MoveTemp(Resolved);
	SelectionChanged.Broadcast(SelectedAttack);
	return true;
}

bool FCombatProfileEditorSession::SelectAttackByName(FName MoveName)
{
	return SelectAttack(MakeAttackIdentity(MoveName));
}

void FCombatProfileEditorSession::ClearSelection()
{
	if (!SelectedAttack.IsValid())
	{
		return;
	}
	SelectedAttack = FProfileItemIdentity();
	SelectionChanged.Broadcast(SelectedAttack);
}

int32 FCombatProfileEditorSession::GenerateMissingAttackOptions()
{
	UPaper2DPlusCombatProfileAsset* Combat = Asset.Get();
	if (!Combat || !Combat->CharacterProfile)
	{
		return 0;
	}
	const FScopedTransaction Transaction(LOCTEXT("GenerateMissingAttacks", "Generate Missing Combat Attack Rows"));
	Combat->Modify();
	const int32 Added = Combat->GenerateAttackOptionsFromCharacterProfile(true);
	if (Added > 0)
	{
		Combat->RefreshAttackOptionMoveBindings();
		Combat->RebuildVariableBags();
		Combat->MarkPackageDirty();
	}
	RefreshFromAsset();
	return Added;
}

const FPaper2DPlusCombatAttackDerivedData* FCombatProfileEditorSession::GetSelectedCatalogRow() const
{
	if (!SelectedAttack.IsValid())
	{
		return nullptr;
	}
	return Catalog.FindByPredicate([this](const FPaper2DPlusCombatAttackDerivedData& Row)
	{
		return MakeAttackIdentity(Row.MoveName).Matches(SelectedAttack);
	});
}

const FPaper2DPlusCombatAttackOption* FCombatProfileEditorSession::GetSelectedOption() const
{
	const UPaper2DPlusCombatProfileAsset* Combat = Asset.Get();
	if (!Combat || !SelectedAttack.IsValid())
	{
		return nullptr;
	}
	for (const FPaper2DPlusCombatAttackOption& Option : Combat->AttackOptions)
	{
		if (const FFlipbookProfileEntry* Entry = Combat->ResolveAttackOptionMove(Option))
		{
			if (MakeAttackIdentity(FName(*Entry->Identity.FlipbookName)).Matches(SelectedAttack))
			{
				return &Option;
			}
		}
		else if (SelectedAttack.ObjectPath.IsNull()
			&& Option.MoveName.ToString().Equals(SelectedAttack.FallbackKey, ESearchCase::IgnoreCase))
		{
			return &Option;
		}
	}
	return nullptr;
}

FPaper2DPlusCombatAttackOption* FCombatProfileEditorSession::GetMutableSelectedOption()
{
	UPaper2DPlusCombatProfileAsset* Combat = Asset.Get();
	const FPaper2DPlusCombatAttackOption* Selected = GetSelectedOption();
	if (!Combat || !Selected)
	{
		return nullptr;
	}
	const int32 Index = static_cast<int32>(Selected - Combat->AttackOptions.GetData());
	return Combat->AttackOptions.IsValidIndex(Index) ? &Combat->AttackOptions[Index] : nullptr;
}

FPaper2DPlusCombatAttackOption* FCombatProfileEditorSession::CustomizeSelectedAttack()
{
	UPaper2DPlusCombatProfileAsset* Combat = Asset.Get();
	const FPaper2DPlusCombatAttackDerivedData* Row = GetSelectedCatalogRow();
	if (!Combat || !Row) return nullptr;
	if (FPaper2DPlusCombatAttackOption* Existing = GetMutableSelectedOption()) return Existing;

	const FScopedTransaction Transaction(LOCTEXT("CustomizeSelectedAttack", "Customize Combat Attack"));
	Combat->Modify();
	FPaper2DPlusCombatAttackOption Option;
	Option.MoveName = Row->MoveName;
	if (!SelectedAttack.ObjectPath.IsNull())
	{
		Option.MoveFlipbook = TSoftObjectPtr<UPaperFlipbook>(SelectedAttack.ObjectPath);
	}
	Option.AttackTag = Row->AttackTag;
	Option.RoleTags = Row->RoleTags;
	Option.BaseWeight = Row->BaseWeight;
	const FPaper2DPlusCombatTagDefaults* Defaults = Combat->FindTagDefaults(Row->AttackTag);
	Option.bOverridePreferredRange = Defaults && Defaults->bOverridePreferredRange;
	Option.PreferredRangeLocal = Row->PreferredRangeLocal;
	// Variable maps are sparse overrides; an untouched customization continues inheriting tag/global values.
	Combat->AttackOptions.Add(MoveTemp(Option));
	Combat->RefreshAttackOptionMoveBindings();
	Combat->RebuildVariableBags();
	Combat->MarkPackageDirty();
	RefreshFromAsset();
	return GetMutableSelectedOption();
}

bool FCombatProfileEditorSession::CommitSelectedOption(
	const FPaper2DPlusCombatAttackOption& EditedOption)
{
	UPaper2DPlusCombatProfileAsset* Combat = Asset.Get();
	FPaper2DPlusCombatAttackOption* Live = GetMutableSelectedOption();
	if (!Combat || !Live) return false;

	// The guided inspector edits tuning, not ownership. Keeping the canonical move
	// identity here prevents a struct-details edit from silently relinking a row.
	const FName CanonicalMoveName = Live->MoveName;
	const TSoftObjectPtr<UPaperFlipbook> CanonicalFlipbook = Live->MoveFlipbook;
	const FScopedTransaction Transaction(LOCTEXT("EditSelectedAttack", "Edit Combat Attack"));
	Combat->Modify();
	FPaper2DPlusCombatAttackOption::StaticStruct()->CopyScriptStruct(Live, &EditedOption);
	Live->MoveName = CanonicalMoveName;
	Live->MoveFlipbook = CanonicalFlipbook;
	Combat->RefreshAttackOptionMoveBindings();
	Combat->RebuildVariableBags();
	Combat->MarkPackageDirty();
	RefreshFromAsset();
	return true;
}

bool FCombatProfileEditorSession::RemoveSelectedOptionRow()
{
	UPaper2DPlusCombatProfileAsset* Combat = Asset.Get();
	FPaper2DPlusCombatAttackOption* Live = GetMutableSelectedOption();
	if (!Combat || !Live)
	{
		return false;
	}
	const int32 Index = static_cast<int32>(Live - Combat->AttackOptions.GetData());
	if (!Combat->AttackOptions.IsValidIndex(Index))
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("RevertAttackTuning", "Revert Combat Attack to Inherited Defaults"));
	Combat->Modify();
	Combat->AttackOptions.RemoveAt(Index);
	Combat->RefreshAttackOptionMoveBindings();
	Combat->RebuildVariableBags();
	Combat->MarkPackageDirty();
	RefreshFromAsset();
	return true;
}

FCombatProfileEffectiveAttackSummary FCombatProfileEditorSession::GetSelectedEffectiveSummary() const
{
	FCombatProfileEffectiveAttackSummary Summary;
	const UPaper2DPlusCombatProfileAsset* Combat = Asset.Get();
	const FPaper2DPlusCombatAttackDerivedData* CatalogRow = GetSelectedCatalogRow();
	const FPaper2DPlusCombatAttackOption* Move = GetSelectedOption();
	if (!Combat || !CatalogRow)
	{
		return Summary;
	}

	Summary.BaseWeight = CatalogRow->BaseWeight;
	Summary.bHasMoveTuning = Move != nullptr;
	if (Move)
	{
		Summary.BaseWeightSource = ECombatProfileValueSource::Move;
		Summary.MoveConsiderationCount = Move->Considerations.Num();
	}
	if (const FPaper2DPlusCombatTagDefaults* Tag = Combat->FindTagDefaults(CatalogRow->AttackTag))
	{
		Summary.bHasTagDefaults = true;
		Summary.TagConsiderationCount = Tag->Considerations.Num();
		if (!Move)
		{
			Summary.BaseWeightSource = ECombatProfileValueSource::AttackTag;
		}
	}
	if (const FPaper2DPlusCombatScoringProfile* Profile = Combat->FindScoringProfile(ScoringProfileName))
	{
		Summary.GlobalConsiderationCount = Profile->GlobalConsiderations.Num();
	}
	return Summary;
}

FCombatAttackPickerSource::FCombatAttackPickerSource(TSharedPtr<FCombatProfileEditorSession> InSession)
	: Session(MoveTemp(InSession))
{
	if (Session.IsValid())
	{
		DataChangedHandle = Session->OnDataChanged().AddLambda([this]() { SourceChanged.Broadcast(); });
		SelectionChangedHandle = Session->OnSelectionChanged().AddLambda(
			[this](const FProfileItemIdentity&) { SourceChanged.Broadcast(); });
	}
}

FCombatAttackPickerSource::~FCombatAttackPickerSource()
{
	if (Session.IsValid())
	{
		Session->OnDataChanged().Remove(DataChangedHandle);
		Session->OnSelectionChanged().Remove(SelectionChangedHandle);
	}
}

FName FCombatAttackPickerSource::GetSourceType() const
{
	return CombatProfileEditorSessionPrivate::AttackSourceType;
}

FString FCombatAttackPickerSource::GetLogicalCatalogScope() const
{
	const UPaper2DPlusCombatProfileAsset* Combat = Session.IsValid() ? Session->GetAsset() : nullptr;
	return Combat
		? FString::Printf(TEXT("CombatProfile:%s"), *Combat->GetPathName())
		: TEXT("CombatProfile:<none>");
}

void FCombatAttackPickerSource::GetItems(TArray<FProfilePickerItem>& OutItems) const
{
	OutItems.Reset();
	if (!Session.IsValid())
	{
		return;
	}
	const TArray<FPaper2DPlusCombatAttackDerivedData>& Rows = Session->GetCatalog();
	OutItems.Reserve(Rows.Num());
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const FPaper2DPlusCombatAttackDerivedData& Row = Rows[Index];
		FProfilePickerItem Item;
		Item.Identity = Session->MakeAttackIdentity(Row.MoveName);
		Item.Label = FText::FromName(Row.MoveName);
		Item.CanonicalOrder = Index;
		Item.SearchTags = Row.RoleTags;
		if (Row.AttackTag.IsValid())
		{
			Item.SearchTags.AddTag(Row.AttackTag);
			Item.Aliases.Add(Row.AttackTag.ToString());
		}
		Item.Group = CombatProfileEditorSessionPrivate::FirstTagLeaf(Row.RoleTags);
		if (Item.Group.IsEmpty())
		{
			Item.Group = Row.AttackTag.IsValid() ? Row.AttackTag.ToString() : TEXT("Uncategorized");
		}
		// Same vocabulary as the browser cards and inspector: a move is either customized on this
		// profile or still scoring from its attack-tag defaults.
		Item.SecondaryText = FText::Format(
			Row.bHasCombatProfileOption
				? LOCTEXT("TunedAttack", "Customized · reach {0}")
				: LOCTEXT("InheritedAttack", "Tag defaults · reach {0}"),
			FText::AsNumber(FMath::RoundToInt(Row.ForwardRangeLocal.Y)));
		OutItems.Add(MoveTemp(Item));
	}
}

FProfileItemIdentity FCombatAttackPickerSource::GetSelectedIdentity() const
{
	return Session.IsValid() ? Session->GetSelectedAttack() : FProfileItemIdentity();
}

bool FCombatAttackPickerSource::SelectItem(const FProfileItemIdentity& Identity)
{
	return Session.IsValid() && Session->SelectAttack(Identity);
}

#undef LOCTEXT_NAMESPACE
