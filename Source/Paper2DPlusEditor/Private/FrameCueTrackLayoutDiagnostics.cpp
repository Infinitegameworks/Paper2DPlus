// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCueTrackLayoutDiagnostics.h"

#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"

namespace Paper2DPlusFrameCueTrackLayoutDiagnostics
{
namespace
{
	FString TrackDiagnostics_CueIdentity(const UPaper2DPlusCueBase* Cue)
	{
		if (!Cue)
		{
			return TEXT("<null>");
		}
		const FString PlacementLabel = Cue->DebugName.IsNone()
			? Cue->GetName()
			: Cue->DebugName.ToString();
		return FString::Printf(
			TEXT("%s:%s[%s]"),
			*Cue->GetClass()->GetName(),
			*PlacementLabel,
			*Cue->GetName());
	}

	FString TrackDiagnostics_GuidIdentity(const FGuid& Guid)
	{
		return Guid.IsValid()
			? Guid.ToString(EGuidFormats::DigitsWithHyphens)
			: TEXT("<invalid-guid>");
	}

	void TrackDiagnostics_AddUniqueIssue(
		TArray<FIssue>& OutIssues,
		TSet<FString>& InOutStableKeys,
		EIssueKind Kind,
		const FString& DomainIdentity,
		const FString& ItemIdentity,
		const FString& Message)
	{
		const FName Code = GetIssueCode(Kind);
		const FString StableKey = FString::Printf(
			TEXT("%s|%s|%s"),
			*Code.ToString(),
			*DomainIdentity,
			*ItemIdentity);
		if (InOutStableKeys.Contains(StableKey))
		{
			return;
		}
		InOutStableKeys.Add(StableKey);

		FIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Kind = Kind;
		Issue.Code = Code;
		Issue.DomainIdentity = DomainIdentity;
		Issue.ItemIdentity = ItemIdentity;
		Issue.Message = Message;
	}

	FString TrackDiagnostics_ProfileAnimationIdentity(const FFlipbookProfileEntry& Entry, int32 Index)
	{
		FString Identity = Entry.Identity.Flipbook.ToSoftObjectPath().ToString();
		if (Identity.IsEmpty())
		{
			Identity = Entry.Identity.FlipbookName;
		}
		return FString::Printf(TEXT("Profile[%d]:%s"), Index, *Identity);
	}

	FString TrackDiagnostics_BaselineAnimationIdentity(
		const FPaper2DPlusCharacterBaselineAnimation& Entry,
		int32 Index)
	{
		FString Identity = Entry.Flipbook.ToSoftObjectPath().ToString();
		if (Identity.IsEmpty())
		{
			Identity = Entry.LegacyAnimationName;
		}
		return FString::Printf(TEXT("Baseline[%d]:%s"), Index, *Identity);
	}

	FString TrackDiagnostics_LayerAnimationIdentity(
		const FCharacterLayer& Layer,
		int32 LayerIndex,
		const FCharacterLayerAuthoredAnimationData& Entry,
		int32 Index)
	{
		FString AnimationIdentity = Entry.Flipbook.ToSoftObjectPath().ToString();
		if (AnimationIdentity.IsEmpty())
		{
			AnimationIdentity = Entry.LegacyAnimationName;
		}
		return FString::Printf(
			TEXT("Layer[%d:%s:%s]/Authored[%d]:%s"),
			LayerIndex,
			*TrackDiagnostics_GuidIdentity(Layer.LayerId),
			*Layer.LayerName,
			Index,
			*AnimationIdentity);
	}
}

FName GetIssueCode(EIssueKind Kind)
{
	switch (Kind)
	{
	case EIssueKind::InvalidTrackId:
		return TEXT("Paper2DPlus.FrameCueTrackLayout.InvalidTrackId");
	case EIssueKind::DuplicateTrackId:
		return TEXT("Paper2DPlus.FrameCueTrackLayout.DuplicateTrackId");
	case EIssueKind::EmptyTrackName:
		return TEXT("Paper2DPlus.FrameCueTrackLayout.EmptyTrackName");
	case EIssueKind::NonCanonicalTrackName:
		return TEXT("Paper2DPlus.FrameCueTrackLayout.NonCanonicalTrackName");
	case EIssueKind::DuplicateTrackName:
		return TEXT("Paper2DPlus.FrameCueTrackLayout.DuplicateTrackName");
	case EIssueKind::NullMembershipCue:
		return TEXT("Paper2DPlus.FrameCueTrackLayout.NullMembershipCue");
	case EIssueKind::InvalidMembershipTrackId:
		return TEXT("Paper2DPlus.FrameCueTrackLayout.InvalidMembershipTrackId");
	case EIssueKind::UnknownMembershipTrackId:
		return TEXT("Paper2DPlus.FrameCueTrackLayout.UnknownMembershipTrackId");
	case EIssueKind::AssignmentOutsideDomain:
		return TEXT("Paper2DPlus.FrameCueTrackLayout.AssignmentOutsideDomain");
	case EIssueKind::CrossDomainMembership:
		return TEXT("Paper2DPlus.FrameCueTrackLayout.CrossDomainMembership");
	case EIssueKind::DuplicateCueInDomain:
		return TEXT("Paper2DPlus.FrameCueTrackLayout.DuplicateCueInDomain");
	case EIssueKind::CueInMultipleDomains:
		return TEXT("Paper2DPlus.FrameCueTrackLayout.CueInMultipleDomains");
	case EIssueKind::WrongCueOuter:
		return TEXT("Paper2DPlus.FrameCueTrackLayout.WrongCueOuter");
	default:
		return TEXT("Paper2DPlus.FrameCueTrackLayout.Unknown");
	}
}

void AnalyzeDomains(const TArray<FDomain>& Domains, TArray<FIssue>& OutIssues)
{
	OutIssues.Reset();
	TSet<FString> StableIssueKeys;

	TMap<const UPaper2DPlusCueBase*, TSet<int32>> CueDomainIndices;
	for (int32 DomainIndex = 0; DomainIndex < Domains.Num(); ++DomainIndex)
	{
		const FDomain& Domain = Domains[DomainIndex];
		for (const UPaper2DPlusCueBase* Cue : Domain.ActiveCues)
		{
			if (Cue) CueDomainIndices.FindOrAdd(Cue).Add(DomainIndex);
		}
		for (const UPaper2DPlusCueBase* Cue : Domain.StashedCues)
		{
			if (Cue) CueDomainIndices.FindOrAdd(Cue).Add(DomainIndex);
		}
	}

	for (int32 DomainIndex = 0; DomainIndex < Domains.Num(); ++DomainIndex)
	{
		const FDomain& Domain = Domains[DomainIndex];
		if (!Domain.Layout)
		{
			continue;
		}

		TMap<const UPaper2DPlusCueBase*, int32> LocalOccurrences;
		for (const UPaper2DPlusCueBase* Cue : Domain.ActiveCues)
		{
			if (Cue) ++LocalOccurrences.FindOrAdd(Cue);
		}
		for (const UPaper2DPlusCueBase* Cue : Domain.StashedCues)
		{
			if (Cue) ++LocalOccurrences.FindOrAdd(Cue);
		}

		for (const TPair<const UPaper2DPlusCueBase*, int32>& Pair : LocalOccurrences)
		{
			const UPaper2DPlusCueBase* Cue = Pair.Key;
			const FString Item = TrackDiagnostics_CueIdentity(Cue);
			if (Pair.Value != 1)
			{
				TrackDiagnostics_AddUniqueIssue(
					OutIssues,
					StableIssueKeys,
					EIssueKind::DuplicateCueInDomain,
					Domain.Identity,
					Item,
					FString::Printf(TEXT("Cue placement appears %d times in this animation domain; its named-track assignment projects to Default."), Pair.Value));
			}
			if (Domain.ExpectedCueOuter && Cue->GetOuter() != Domain.ExpectedCueOuter)
			{
				TrackDiagnostics_AddUniqueIssue(
					OutIssues,
					StableIssueKeys,
					EIssueKind::WrongCueOuter,
					Domain.Identity,
					Item,
					TEXT("Cue placement has the wrong owning object; its named-track assignment projects to Default."));
			}
			const TSet<int32>* OwningDomains = CueDomainIndices.Find(Cue);
			if (OwningDomains && OwningDomains->Num() > 1)
			{
				TrackDiagnostics_AddUniqueIssue(
					OutIssues,
					StableIssueKeys,
					EIssueKind::CueInMultipleDomains,
					Domain.Identity,
					Item,
					TEXT("The same Cue object appears in more than one animation domain; every ambiguous named-track projection fails safely to Default."));
			}
		}

		TMap<FGuid, int32> TrackIdCounts;
		TMap<FString, int32> TrackNameCounts;
		for (const FPaper2DPlusFrameCueTrackDefinition& Track : Domain.Layout->OptionalTracks)
		{
			if (Track.TrackId.IsValid()) ++TrackIdCounts.FindOrAdd(Track.TrackId);
			const FString NormalizedName = FPaper2DPlusFrameCueTrackLayout::NormalizeTrackName(Track.DisplayName);
			if (!NormalizedName.IsEmpty()) ++TrackNameCounts.FindOrAdd(NormalizedName.ToLower());
		}

		for (int32 TrackIndex = 0; TrackIndex < Domain.Layout->OptionalTracks.Num(); ++TrackIndex)
		{
			const FPaper2DPlusFrameCueTrackDefinition& Track = Domain.Layout->OptionalTracks[TrackIndex];
			const FString TrackItem = FString::Printf(TEXT("Track[%d]:%s"), TrackIndex, *TrackDiagnostics_GuidIdentity(Track.TrackId));
			const FString NormalizedName = FPaper2DPlusFrameCueTrackLayout::NormalizeTrackName(Track.DisplayName);
			if (!Track.TrackId.IsValid())
			{
				TrackDiagnostics_AddUniqueIssue(OutIssues, StableIssueKeys, EIssueKind::InvalidTrackId, Domain.Identity, TrackItem,
					TEXT("Optional named tracks require a valid GUID; this definition cannot receive Cue membership."));
			}
			else if (TrackIdCounts.FindRef(Track.TrackId) > 1)
			{
				TrackDiagnostics_AddUniqueIssue(OutIssues, StableIssueKeys, EIssueKind::DuplicateTrackId, Domain.Identity, TrackItem,
					TEXT("More than one optional track uses this GUID; memberships to it are ambiguous and project to Default."));
			}
			if (NormalizedName.IsEmpty())
			{
				TrackDiagnostics_AddUniqueIssue(OutIssues, StableIssueKeys, EIssueKind::EmptyTrackName, Domain.Identity, TrackItem,
					TEXT("Optional named tracks require a non-empty designer label."));
			}
			else
			{
				if (NormalizedName != Track.DisplayName)
				{
					TrackDiagnostics_AddUniqueIssue(OutIssues, StableIssueKeys, EIssueKind::NonCanonicalTrackName, Domain.Identity, TrackItem,
						TEXT("Track label contains leading or trailing whitespace; use an explicit repair/rename to normalize it."));
				}
				if (TrackNameCounts.FindRef(NormalizedName.ToLower()) > 1)
				{
					TrackDiagnostics_AddUniqueIssue(OutIssues, StableIssueKeys, EIssueKind::DuplicateTrackName, Domain.Identity, TrackItem,
						TEXT("Optional track labels must be unique in this animation, ignoring case."));
				}
			}
		}

		for (const TPair<TObjectPtr<UPaper2DPlusCueBase>, FGuid>& Membership : Domain.Layout->CueTrackIds)
		{
			const UPaper2DPlusCueBase* Cue = Membership.Key.Get();
			const FString Item = TrackDiagnostics_CueIdentity(Cue);
			if (!Cue)
			{
				TrackDiagnostics_AddUniqueIssue(OutIssues, StableIssueKeys, EIssueKind::NullMembershipCue, Domain.Identity, Item,
					TEXT("A named-track membership has no Cue object and is ignored."));
				continue;
			}
			if (Domain.ExpectedCueOuter && Cue->GetOuter() != Domain.ExpectedCueOuter)
			{
				TrackDiagnostics_AddUniqueIssue(OutIssues, StableIssueKeys, EIssueKind::WrongCueOuter, Domain.Identity, Item,
					TEXT("A named-track membership references a Cue owned by another object and projects to Default."));
			}
			if (!Membership.Value.IsValid())
			{
				TrackDiagnostics_AddUniqueIssue(OutIssues, StableIssueKeys, EIssueKind::InvalidMembershipTrackId, Domain.Identity, Item,
					TEXT("Default is represented by no membership entry; an invalid stored GUID is corrupt and projects to Default."));
			}
			else if (!Domain.Layout->IsTrackIdValid(Membership.Value))
			{
				TrackDiagnostics_AddUniqueIssue(OutIssues, StableIssueKeys, EIssueKind::UnknownMembershipTrackId, Domain.Identity, Item,
					FString::Printf(TEXT("Membership references missing or ambiguous track %s and projects to Default."), *TrackDiagnostics_GuidIdentity(Membership.Value)));
			}

			const int32 LocalCount = LocalOccurrences.FindRef(Cue);
			if (LocalCount == 0)
			{
				const TSet<int32>* OwningDomains = CueDomainIndices.Find(Cue);
				const bool bOwnedByAnotherDomain = OwningDomains && OwningDomains->Num() > 0;
				TrackDiagnostics_AddUniqueIssue(
					OutIssues,
					StableIssueKeys,
					bOwnedByAnotherDomain ? EIssueKind::CrossDomainMembership : EIssueKind::AssignmentOutsideDomain,
					Domain.Identity,
					Item,
					bOwnedByAnotherDomain
						? TEXT("This layout references a Cue from another animation domain; the assignment projects to Default.")
						: TEXT("This layout retains a Cue that is outside its complete active/stashed domain; the assignment projects to Default."));
			}
		}
	}

	OutIssues.Sort([](const FIssue& A, const FIssue& B)
	{
		const int32 DomainOrder = A.DomainIdentity.Compare(B.DomainIdentity, ESearchCase::CaseSensitive);
		if (DomainOrder != 0) return DomainOrder < 0;
		const int32 CodeOrder = A.Code.ToString().Compare(B.Code.ToString(), ESearchCase::CaseSensitive);
		if (CodeOrder != 0) return CodeOrder < 0;
		return A.ItemIdentity.Compare(B.ItemIdentity, ESearchCase::CaseSensitive) < 0;
	});
}

void AnalyzeCharacterProfile(
	const UPaper2DPlusCharacterProfileAsset& Asset,
	TArray<FIssue>& OutIssues)
{
	TArray<FDomain> Domains;
#if WITH_EDITORONLY_DATA
	Domains.Reserve(Asset.Flipbooks.Num() + Asset.CharacterBaseline.Num());
	for (int32 Index = 0; Index < Asset.Flipbooks.Num(); ++Index)
	{
		const FFlipbookProfileEntry& Entry = Asset.Flipbooks[Index];
		FDomain& Domain = Domains.AddDefaulted_GetRef();
		Domain.Identity = TrackDiagnostics_ProfileAnimationIdentity(Entry, Index);
		Domain.ExpectedCueOuter = &Asset;
		Domain.Layout = &Entry.FrameEventData.CueTrackLayout;
		for (const UPaper2DPlusCueBase* Cue : Entry.FrameEventData.FrameCues)
		{
			Domain.ActiveCues.Add(Cue);
		}
		for (const FExcludedFlipbookFrameData& Excluded : Entry.CombatData.ExcludedFrames)
		{
			for (const UPaper2DPlusCueBase* Cue : Excluded.StashedFrameCues)
			{
				Domain.StashedCues.Add(Cue);
			}
		}
	}

	for (int32 Index = 0; Index < Asset.CharacterBaseline.Num(); ++Index)
	{
		const FPaper2DPlusCharacterBaselineAnimation& Entry = Asset.CharacterBaseline[Index];
		FDomain& Domain = Domains.AddDefaulted_GetRef();
		Domain.Identity = TrackDiagnostics_BaselineAnimationIdentity(Entry, Index);
		Domain.ExpectedCueOuter = &Asset;
		Domain.Layout = &Entry.CueTrackLayout;
		for (const UPaper2DPlusCueBase* Cue : Entry.FrameCues)
		{
			Domain.ActiveCues.Add(Cue);
		}
	}
#endif
	AnalyzeDomains(Domains, OutIssues);
}

void AnalyzeCharacterLayer(
	const UPaper2DPlusCharacterLayerAsset& Asset,
	TArray<FIssue>& OutIssues)
{
	TArray<FDomain> Domains;
#if WITH_EDITORONLY_DATA
	for (int32 LayerIndex = 0; LayerIndex < Asset.Layers.Num(); ++LayerIndex)
	{
		const FCharacterLayer& Layer = Asset.Layers[LayerIndex];
		Domains.Reserve(Domains.Num() + Layer.AuthoredAnimations.Num());
		for (int32 Index = 0; Index < Layer.AuthoredAnimations.Num(); ++Index)
		{
			const FCharacterLayerAuthoredAnimationData& Entry = Layer.AuthoredAnimations[Index];
			FDomain& Domain = Domains.AddDefaulted_GetRef();
			Domain.Identity = TrackDiagnostics_LayerAnimationIdentity(
				Layer, LayerIndex, Entry, Index);
			Domain.ExpectedCueOuter = &Asset;
			Domain.Layout = &Entry.CueTrackLayout;
			for (const UPaper2DPlusCueBase* Cue : Entry.FrameCues)
			{
				Domain.ActiveCues.Add(Cue);
			}
		}
	}
#endif
	AnalyzeDomains(Domains, OutIssues);
}
}
