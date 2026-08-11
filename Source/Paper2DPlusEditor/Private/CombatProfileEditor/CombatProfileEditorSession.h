// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusCombatProfileTypes.h"
#include "ProfileItemPicker.h"

class UPaper2DPlusCombatProfileAsset;
struct FFlipbookProfileEntry;

enum class ECombatProfileValueSource : uint8
{
	BuiltInDefault,
	Global,
	AttackTag,
	Move
};

/** Plain-language projection used by the attack inspector; never serialized. */
struct FCombatProfileEffectiveAttackSummary
{
	float BaseWeight = 1.0f;
	ECombatProfileValueSource BaseWeightSource = ECombatProfileValueSource::BuiltInDefault;
	int32 GlobalConsiderationCount = 0;
	int32 TagConsiderationCount = 0;
	int32 MoveConsiderationCount = 0;
	bool bHasMoveTuning = false;
	bool bHasTagDefaults = false;

	FText GetBaseWeightProvenanceText() const;
};

DECLARE_MULTICAST_DELEGATE(FOnCombatProfileSessionChanged);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnCombatProfileAttackSelectionChanged, const FProfileItemIdentity&);

/**
 * One transient source of truth shared by every Combat editor panel.
 *
 * Selection is path-first and name-fallback; it is never an array index. Refreshes
 * rebuild derived data without touching the asset, while explicit mutations are
 * transactional and refresh all consumers exactly once.
 */
class FCombatProfileEditorSession final : public TSharedFromThis<FCombatProfileEditorSession>
{
public:
	explicit FCombatProfileEditorSession(UPaper2DPlusCombatProfileAsset* InAsset);

	UPaper2DPlusCombatProfileAsset* GetAsset() const { return Asset.Get(); }
	const TArray<FPaper2DPlusCombatAttackDerivedData>& GetCatalog() const { return Catalog; }
	const TArray<FPaper2DPlusCombatRankedOption>& GetRankedOptions() const { return RankedOptions; }
	const FPaper2DPlusCombatRuntimeContext& GetPreviewContext() const { return PreviewContext; }
	FPaper2DPlusCombatRuntimeContext& EditPreviewContext() { return PreviewContext; }
	const FProfileItemIdentity& GetSelectedAttack() const { return SelectedAttack; }
	FName GetScoringProfileName() const { return ScoringProfileName; }
	uint32 GetRevision() const { return Revision; }

	/** Mutation-free refresh. Stable selection survives reorder and rename through object path. */
	void RefreshFromAsset();
	void RefreshScores();
	void SetScoringProfileName(FName InName);
	bool SelectAttack(const FProfileItemIdentity& Identity);
	bool SelectAttackByName(FName MoveName);
	void ClearSelection();

	/** Adds only missing eligible move rows; returns how many rows were added. */
	int32 GenerateMissingAttackOptions();

	const FPaper2DPlusCombatAttackOption* GetSelectedOption() const;
	FPaper2DPlusCombatAttackOption* GetMutableSelectedOption();
	FPaper2DPlusCombatAttackOption* CustomizeSelectedAttack();
	bool CommitSelectedOption(const FPaper2DPlusCombatAttackOption& EditedOption);
	/** Removes the selected move's tuning row so it scores from tag/global defaults again. Undoable. */
	bool RemoveSelectedOptionRow();
	const FPaper2DPlusCombatAttackDerivedData* GetSelectedCatalogRow() const;
	FCombatProfileEffectiveAttackSummary GetSelectedEffectiveSummary() const;

	FProfileItemIdentity MakeAttackIdentity(FName MoveName) const;
	bool ResolveAttackIdentity(const FProfileItemIdentity& Candidate, FProfileItemIdentity& OutResolved) const;

	FOnCombatProfileSessionChanged& OnDataChanged() { return DataChanged; }
	FOnCombatProfileSessionChanged& OnScoreChanged() { return ScoreChanged; }
	FOnCombatProfileAttackSelectionChanged& OnSelectionChanged() { return SelectionChanged; }

private:
	const FFlipbookProfileEntry* FindMoveByIdentity(const FProfileItemIdentity& Identity) const;
	bool CatalogContainsMove(FName MoveName) const;
	void BroadcastRefresh(bool bSelectionChanged);

	TWeakObjectPtr<UPaper2DPlusCombatProfileAsset> Asset;
	TArray<FPaper2DPlusCombatAttackDerivedData> Catalog;
	TArray<FPaper2DPlusCombatRankedOption> RankedOptions;
	FPaper2DPlusCombatRuntimeContext PreviewContext;
	FProfileItemIdentity SelectedAttack;
	FName ScoringProfileName;
	uint32 Revision = 0;
	FOnCombatProfileSessionChanged DataChanged;
	FOnCombatProfileSessionChanged ScoreChanged;
	FOnCombatProfileAttackSelectionChanged SelectionChanged;
};

/** Shared U1 picker adapter for Combat attacks. */
class FCombatAttackPickerSource final : public IProfileItemPickerSource
{
public:
	explicit FCombatAttackPickerSource(TSharedPtr<FCombatProfileEditorSession> InSession);
	virtual ~FCombatAttackPickerSource() override;

	virtual FName GetSourceType() const override;
	virtual FString GetLogicalCatalogScope() const override;
	virtual void GetItems(TArray<FProfilePickerItem>& OutItems) const override;
	virtual FProfileItemIdentity GetSelectedIdentity() const override;
	virtual bool SelectItem(const FProfileItemIdentity& Identity) override;
	virtual FOnProfileItemSourceChanged& OnSourceChanged() override { return SourceChanged; }

private:
	TSharedPtr<FCombatProfileEditorSession> Session;
	FOnProfileItemSourceChanged SourceChanged;
	FDelegateHandle DataChangedHandle;
	FDelegateHandle SelectionChangedHandle;
};
