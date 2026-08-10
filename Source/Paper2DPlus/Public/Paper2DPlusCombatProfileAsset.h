// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"
#include "Engine/DataAsset.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
#include "UObject/AssetRegistryTagsContext.h"
#endif
#include "Paper2DPlusAuthoringProgressTags.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCombatProfileTypes.h"
#include "Paper2DPlusCombatProfileAsset.generated.h"

UCLASS(BlueprintType)
class PAPER2DPLUS_API UPaper2DPlusCombatProfileAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static constexpr uint8 CurrentVariableOverrideSchemaVersion = 1;
#if WITH_EDITOR
	/** Stable passive relationship metadata consumed by ProfileRelationshipService without loading. */
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override
	{
		Super::GetAssetRegistryTags(Context);
		Context.AddTag(FAssetRegistryTag(
			TEXT("Paper2DPlus.CharacterProfile"),
			CharacterProfile ? CharacterProfile->GetPathName() : FString(TEXT("None")),
			FAssetRegistryTag::TT_Hidden));
		int32 ProgressDone = 0;
		int32 ProgressTotal = 0;
		Paper2DPlusAuthoringProgress::ComputeChecklistProgress(
			EditorCompletionFlags, ProgressDone, ProgressTotal);
		Context.AddTag(FAssetRegistryTag(
			Paper2DPlusAuthoringProgress::DoneTag(),
			FString::FromInt(ProgressDone),
			FAssetRegistryTag::TT_Hidden));
		Context.AddTag(FAssetRegistryTag(
			Paper2DPlusAuthoringProgress::TotalTag(),
			FString::FromInt(ProgressTotal),
			FAssetRegistryTag::TT_Hidden));
	}
#else
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override
	{
		Super::GetAssetRegistryTags(OutTags);
		OutTags.Emplace(
			TEXT("Paper2DPlus.CharacterProfile"),
			CharacterProfile ? CharacterProfile->GetPathName() : FString(TEXT("None")),
			FAssetRegistryTag::TT_Hidden);
		int32 ProgressDone = 0;
		int32 ProgressTotal = 0;
		Paper2DPlusAuthoringProgress::ComputeChecklistProgress(
			EditorCompletionFlags, ProgressDone, ProgressTotal);
		OutTags.Emplace(
			Paper2DPlusAuthoringProgress::DoneTag(),
			FString::FromInt(ProgressDone),
			FAssetRegistryTag::TT_Hidden);
		OutTags.Emplace(
			Paper2DPlusAuthoringProgress::TotalTag(),
			FString::FromInt(ProgressTotal),
			FAssetRegistryTag::TT_Hidden);
	}
#endif
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Profile")
	TObjectPtr<UPaper2DPlusCharacterProfileAsset> CharacterProfile = nullptr;

#if WITH_EDITORONLY_DATA
	/** Manual editor workflow progress. Bits are owned by the shared Profile Completion panel. */
	UPROPERTY()
	int32 EditorCompletionFlags = 0;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Profile|Variables")
	TArray<FPaper2DPlusCombatVariableDefinition> VariableDefinitions;

	/** Authored default values for the global scoring variables, keyed by the variable's Paper2DPlus.Combat.Var tag. */
	UPROPERTY(EditAnywhere, Category = "Combat Profile|Variables")
	TMap<FGameplayTag, FPaper2DPlusCombatVariableValue> GlobalVariables;

	/**
	 * 0 = legacy dense scope maps whose inherited values were materialized snapshots.
	 * 1 = sparse tag/move/scenario override maps. Old assets remain at 0 until the designer runs the
	 * behavior-preserving review action; PostLoad never guesses which legacy rows were intentional.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Combat Profile|Variables", AdvancedDisplay)
	uint8 VariableOverrideSchemaVersion = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Profile|Defaults")
	TArray<FPaper2DPlusCombatTagDefaults> TagDefaults;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Profile|Attacks")
	TArray<FPaper2DPlusCombatAttackOption> AttackOptions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Profile|Scoring")
	TArray<FPaper2DPlusCombatScoringProfile> ScoringProfiles;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Profile|Lab")
	TArray<FPaper2DPlusCombatScenarioPreset> ScenarioPresets;

	// --- Transitions: DORMANT FOUNDATION (TASK-108 U1 / R14) ---
	// Re-homes the outcome-condition and cancel-category concepts removed from the CharacterProfile's
	// pure From→To transition rows. Authored + serialized here but CONSUMED BY NOTHING yet — a future
	// combat-profile outcome-branch/cancel-window driver is the intended reader.
	// NOTE: this DELIBERATELY crosses the documented "Combat Profile is advisory-only / no serialized
	// derived facts" boundary (user-approved, TASK-108): these are authored foundation data, not
	// derived facts, but they are the first serialized fields whose consumer does not exist yet.

	/** Dormant: outcome-condition rules for From→To move pairs on the linked CharacterProfile. */
	UPROPERTY(EditAnywhere, Category = "Combat Profile|Transitions")
	TArray<FPaper2DPlusTransitionOutcomeRule> TransitionOutcomeRules;

	/** Dormant: the cancel-category registry ("Cancel_<Category>" gate-curve convention). */
	UPROPERTY(EditAnywhere, Category = "Combat Profile|Transitions")
	TArray<FPaper2DPlusCancelCategoryEntry> CancelCategories;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Combat Profile")
	void RebuildVariableBags();

#if WITH_EDITOR
	/** Preserve authored values and consideration references when the guided editor changes a variable tag. */
	bool RenameVariableTag(FGameplayTag OldTag, FGameplayTag NewTag);

	/**
	 * Review legacy dense maps without data loss: remove only rows exactly equal to their current inherited
	 * value, retain every behavior-changing row as an explicit override, then adopt sparse schema v1.
	 * Returns the number of no-op snapshots removed. The editor wraps this in one transaction.
	 */
	int32 AdoptSparseVariableOverrides();
#endif

	bool HasLegacyDenseVariableOverrides() const
	{
		return VariableOverrideSchemaVersion < CurrentVariableOverrideSchemaVersion;
	}

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Combat Profile")
	int32 GenerateAttackOptionsFromCharacterProfile(bool bOnlyMissing = true);

	/**
	 * Backfill unique legacy MoveName rows and refresh cached names from canonical flipbook identity.
	 * Never loads a soft object; ambiguous or dangling rows remain untouched for validation.
	 */
	int32 RefreshAttackOptionMoveBindings();

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Combat Profile")
	bool ValidateCombatProfileAsset(TArray<FPaper2DPlusCombatValidationIssue>& OutIssues) const;

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Combat Profile")
	void BuildAttackCatalog(TArray<FPaper2DPlusCombatAttackDerivedData>& OutCatalog) const;

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Combat Profile")
	void ScoreAttackOptions(
		const FPaper2DPlusCombatRuntimeContext& Context,
		TArray<FPaper2DPlusCombatRankedOption>& OutRankedOptions,
		FName ScoringProfileName = NAME_None) const;

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Combat Profile")
	bool GetCombatDecision(
		const FPaper2DPlusCombatRuntimeContext& Context,
		FPaper2DPlusCombatDecision& OutDecision,
		FName ScoringProfileName = NAME_None) const;

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Combat Profile")
	bool PickWeightedCombatAttack(
		const FPaper2DPlusCombatRuntimeContext& Context,
		UPARAM(ref) FRandomStream& RandomStream,
		FPaper2DPlusCombatDecision& OutDecision,
		FName ScoringProfileName = NAME_None) const;

	const FPaper2DPlusCombatAttackOption* FindAttackOption(FName MoveName) const;
	const FPaper2DPlusCombatAttackOption* FindAttackOption(const FFlipbookProfileEntry& MoveEntry) const;
	const FFlipbookProfileEntry* ResolveAttackOptionMove(
		const FPaper2DPlusCombatAttackOption& Option,
		bool* bOutAmbiguousLegacyName = nullptr) const;
	const FPaper2DPlusCombatTagDefaults* FindTagDefaults(FGameplayTag AttackTag) const;
	const FPaper2DPlusCombatScoringProfile* FindScoringProfile(FName ProfileName = NAME_None) const;
	const FPaper2DPlusCombatVariableDefinition* FindVariableDefinition(FGameplayTag VariableTag) const;

	bool TryGetFloatVariable(const TMap<FGameplayTag, FPaper2DPlusCombatVariableValue>& Variables, FGameplayTag VariableTag, float& OutValue) const;
	bool TryGetBoolVariable(const TMap<FGameplayTag, FPaper2DPlusCombatVariableValue>& Variables, FGameplayTag VariableTag, bool& bOutValue) const;

	/** Prune removed variables; optionally materialize every definition (Global only—other scopes stay sparse overrides). */
	void RebuildVariableBag(
		TMap<FGameplayTag, FPaper2DPlusCombatVariableValue>& Variables,
		bool bEnsureDefinedEntries = true) const;

private:
	void AddValidationIssue(
		TArray<FPaper2DPlusCombatValidationIssue>& OutIssues,
		EPaper2DPlusCombatValidationSeverity Severity,
		const FText& Message,
		FName Field = NAME_None,
		FName MoveName = NAME_None) const;

	FGameplayTag FindFirstAttackTagForMove(FName MoveName) const;

	/** Fold any legacy free-form variable identity (VariableName_DEPRECATED) into a Paper2DPlus.Combat.Var tag. */
	void MigrateVariableIdentity();
	const FFlipbookProfileEntry* FindUniqueMoveByName(FName MoveName, bool* bOutAmbiguous = nullptr) const;
	const FFlipbookProfileEntry* FindMoveByFlipbookPath(const FSoftObjectPath& FlipbookPath) const;
};
