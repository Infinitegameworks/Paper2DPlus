// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusClashGraphAsset.h"
#include "Paper2DPlusClash.h"

UPaper2DPlusClashGraphAsset::UPaper2DPlusClashGraphAsset()
{
	// Seed the genre-default RPS edges so a freshly created asset is immediately useful + conflict-free.
	Graph = Paper2DPlusClash::MakeDefaultClashGraph();
}

EClashOutcome UPaper2DPlusClashGraphAsset::ResolveClash(FGameplayTag A, FGameplayTag B) const
{
	return Paper2DPlusClash::ResolveClash(A, B, Graph);
}

void UPaper2DPlusClashGraphAsset::GetConcreteEdgeTags(TArray<FGameplayTag>& OutTags) const
{
	OutTags.Reset();
	for (const FClashEdge& Edge : Graph.Edges)
	{
		if (Edge.Winner.IsValid())
		{
			OutTags.AddUnique(Edge.Winner);
		}
		if (Edge.Loser.IsValid())
		{
			OutTags.AddUnique(Edge.Loser);
		}
	}
}

bool UPaper2DPlusClashGraphAsset::ValidateClashGraphAsset(const TArray<FGameplayTag>& ExtraCategories, TArray<FClashValidationIssue>& OutIssues) const
{
	// Concrete tags = the graph's own edge tags (deduped, first-seen order) + any project-assigned hitbox
	// categories the caller supplies — so an authored hitbox category that conflicts with the graph surfaces.
	TArray<FGameplayTag> ConcreteTags;
	GetConcreteEdgeTags(ConcreteTags);
	for (const FGameplayTag& Extra : ExtraCategories)
	{
		if (Extra.IsValid())
		{
			ConcreteTags.AddUnique(Extra);
		}
	}
	return Paper2DPlusClash::ValidateClashGraph(Graph, ConcreteTags, OutIssues);
}
