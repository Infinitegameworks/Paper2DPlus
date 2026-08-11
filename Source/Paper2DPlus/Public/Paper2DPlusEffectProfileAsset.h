// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"
#include "Engine/DataAsset.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
#include "UObject/AssetRegistryTagsContext.h"
#endif
#include "GameplayTagContainer.h"
#include "Paper2DPlusAuthoringProgressTags.h"
#include "Paper2DPlusEffectProfileAsset.generated.h"

class UPaper2DPlusCharacterLayerAsset;
class UPaper2DPlusCharacterProfileAsset;
class UPaperFlipbook;

UENUM(BlueprintType)
enum class EPaper2DPlusEffectProfileValidationSeverity : uint8
{
	Info,
	Warning,
	Error
};

/** Native-only result for inspecting a soft effect identity without loading its asset. */
enum class EPaper2DPlusEffectFlipbookPathStatus : uint8
{
	Unset,
	InvalidPath,
	Unknown,
	Missing,
	WrongClass,
	Valid
};

USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusEffectProfileValidationIssue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Validation")
	EPaper2DPlusEffectProfileValidationSeverity Severity = EPaper2DPlusEffectProfileValidationSeverity::Info;

	UPROPERTY(BlueprintReadOnly, Category = "Validation")
	FText Message;

	/** Compatibility issue identity. New rows use the flipbook name when no retained legacy name exists. */
	UPROPERTY(BlueprintReadOnly, Category = "Validation")
	FName EffectName;

	UPROPERTY(BlueprintReadOnly, Category = "Validation")
	FName Field;
};

/** Direct local settings carried by a Spawn Effect Cue. CategoryTag remains only for old resolver output. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusEffectSpawnSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	// Explicit null keeps stack-constructed USTRUCTs safe on UE 5.0.
	TObjectPtr<UPaperFlipbook> EffectFlipbook = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	FVector2D Offset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	float Rotation = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	FVector2D Scale = FVector2D(1.0, 1.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	bool bFlipWithCharacter = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	FLinearColor Tint = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "Effect", meta = (DeprecatedProperty, DeprecationMessage = "Effect category now belongs to Effect Profile library metadata, not spawn settings."))
	FGameplayTag CategoryTag;

	bool IsValid() const { return EffectFlipbook != nullptr; }
};

/**
 * One ordered Effect Profile library row. EffectFlipbook's object path is the sole identity.
 *
 * The properties in "Legacy" are deliberately retained and serialized for one transition release.
 * They are hidden from active authoring, never own current behavior, and are read only by the
 * idempotent Spawn Effect Cue migration and deprecated compatibility queries.
 */
USTRUCT(BlueprintType, meta = (HasNativeBreak = "/Script/Paper2DPlus.Paper2DPlusBlueprintLibrary.BreakEffectProfileEntry"))
struct PAPER2DPLUS_API FPaper2DPlusEffectProfileEntry
{
	GENERATED_BODY()

	/** Serialized identity. Kept soft so loading the library does not load every effect flipbook. */
	UPROPERTY(EditAnywhere, Category = "Effect Library")
	TSoftObjectPtr<UPaperFlipbook> EffectFlipbook;

	/** Strong transient projection populated only on rows explicitly returned by a loading query. */
	UPROPERTY(Transient)
	TObjectPtr<UPaperFlipbook> LoadedEffectFlipbook = nullptr;

	/** Optional presentation label. Consumers and selection never use it as identity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect Library")
	FText DisplayLabel;

	/** Exactly one strict child of Paper2DPlus.Effect.Type for a valid classified row. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect Library", meta = (Categories = "Paper2DPlus.Effect.Type"))
	FGameplayTag TypeTag;

	/** Optional strict children of Paper2DPlus.Effect.Descriptor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect Library", meta = (Categories = "Paper2DPlus.Effect.Descriptor"))
	FGameplayTagContainer DescriptorTags;

	/** An old off-taxonomy category preserved visibly until a designer explicitly remaps and clears it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Migration", meta = (DisplayName = "Legacy Category Awaiting Remap"))
	FGameplayTag LegacyCategoryAwaitingRemap;

	UPROPERTY(BlueprintReadOnly, Category = "Effect|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Effect identity is now the EffectFlipbook object path; use DisplayLabel only for presentation."))
	FName EffectName;

	UPROPERTY(BlueprintReadOnly, Category = "Effect|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Use TypeTag and DescriptorTags."))
	FGameplayTag CategoryTag;

	UPROPERTY(BlueprintReadOnly, Category = "Effect|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Spawn placement is local to each Spawn Effect Cue."))
	FVector2D Offset = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Effect|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Spawn placement is local to each Spawn Effect Cue."))
	float Rotation = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Effect|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Spawn placement is local to each Spawn Effect Cue."))
	FVector2D Scale = FVector2D(1.0, 1.0);

	UPROPERTY(BlueprintReadOnly, Category = "Effect|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Facing behavior is local to each Spawn Effect Cue."))
	bool bFlipWithCharacter = true;

	UPROPERTY(BlueprintReadOnly, Category = "Effect|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Tint is local to each Spawn Effect Cue."))
	FLinearColor Tint = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "Effect|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Attachment is local to the consuming cue/gameplay system."))
	FName SocketName;

	UPROPERTY(BlueprintReadOnly, Category = "Effect|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Layer scope is not part of the active Effect Profile contract."))
	FString LayerScope;

	/** Compatibility conversion used only by retained legacy profile/name resolution. */
	FPaper2DPlusEffectSpawnSettings ToSpawnSettings() const;

	FSoftObjectPath GetEffectFlipbookPath() const;
	UPaperFlipbook* GetLoadedEffectFlipbook() const;
	UPaperFlipbook* LoadEffectFlipbook() const;
};

UCLASS(BlueprintType, meta = (DisplayName = "Paper2D+ Effect Profile"))
class PAPER2DPLUS_API UPaper2DPlusEffectProfileAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static constexpr uint8 CurrentEffectLibrarySchemaVersion = 2;

#if WITH_EDITOR
	/** Authoring progress available to unloaded Character Catalog cards without loading this library. */
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override
	{
		Super::GetAssetRegistryTags(Context);
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect Profile")
	FString DisplayName;

#if WITH_EDITORONLY_DATA
	/** Manual editor workflow progress. Bits are owned by the shared Profile Completion panel. */
	UPROPERTY()
	int32 EditorCompletionFlags = 0;
#endif

	/** Retained relationship payload only. Character Catalog is the active relationship authority. */
	UPROPERTY(BlueprintReadOnly, Category = "Effect Profile|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Character Catalog owns Character-to-Effect Profile relationships."))
	TObjectPtr<UPaper2DPlusCharacterProfileAsset> CharacterProfile = nullptr;

	/** Retained relationship payload only. It is never loaded or validated by the Effect Profile. */
	UPROPERTY(BlueprintReadOnly, Category = "Effect Profile|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Character Catalog owns Character-to-Effect Profile relationships."))
	TObjectPtr<UPaper2DPlusCharacterLayerAsset> CharacterLayerAsset = nullptr;

	/** Ordered library. Valid queries deduplicate by normalized flipbook object path, first row wins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect Profile")
	TArray<FPaper2DPlusEffectProfileEntry> Effects;

	/** Zero is the pre-library schema. Fresh in-memory assets are stamped current in PostInitProperties. */
	UPROPERTY()
	uint8 EffectLibrarySchemaVersion = 0;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;

	/** Inspects a soft effect identity through resident state/Asset Registry metadata without loading it. */
	static EPaper2DPlusEffectFlipbookPathStatus InspectEffectFlipbookPathNoLoad(
		const FSoftObjectPath& EffectFlipbookPath);
	/** Returns the known redirector destination path through Registry metadata, or the original path. */
	static FSoftObjectPath GetCanonicalEffectFlipbookPathNoLoad(
		const FSoftObjectPath& EffectFlipbookPath);

	/** No-load row count for indexed Blueprint iteration. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile")
	int32 GetEffectEntryCount() const { return Effects.Num(); }

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile")
	bool GetEffectEntryByIndex(int32 Index, FPaper2DPlusEffectProfileEntry& OutEntry) const;

	/** Loads only the requested row and returns a normal Blueprint object reference. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile")
	UPaperFlipbook* GetEffectFlipbookByIndex(int32 Index) const;

	/** Ordered unique membership, skipping null rows. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile")
	TArray<UPaperFlipbook*> GetEffectFlipbooks() const;

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile")
	bool ContainsEffectFlipbook(UPaperFlipbook* EffectFlipbook) const;

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile", meta = (GameplayTagFilter = "Paper2DPlus.Effect.Type"))
	TArray<UPaperFlipbook*> GetEffectFlipbooksByType(FGameplayTag TypeTag, bool bExactMatch = false) const;

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile", meta = (GameplayTagFilter = "Paper2DPlus.Effect.Descriptor"))
	TArray<UPaperFlipbook*> GetEffectFlipbooksByDescriptor(FGameplayTag DescriptorTag, bool bExactMatch = false) const;

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile", meta = (GameplayTagFilter = "Paper2DPlus.Effect.Descriptor"))
	TArray<UPaperFlipbook*> GetEffectFlipbooksWithAllDescriptors(const FGameplayTagContainer& DescriptorTags, bool bExactMatch = false) const;

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile", meta = (GameplayTagFilter = "Paper2DPlus.Effect.Descriptor"))
	TArray<UPaperFlipbook*> GetEffectFlipbooksWithAnyDescriptors(const FGameplayTagContainer& DescriptorTags, bool bExactMatch = false) const;

	/** Compatibility name resolver. Successful cue migrations do not call this at runtime. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile", meta = (DeprecatedFunction, DeprecationMessage = "Spawn Effect Cues now store a direct flipbook and local settings."))
	bool ResolveEffectSpawnSettings(FName EffectName, FPaper2DPlusEffectSpawnSettings& OutSettings) const;

	/** Compatibility category query. Use type/descriptor flipbook queries for new work. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile", meta = (DeprecatedFunction, DeprecationMessage = "Use GetEffectFlipbooksByType or descriptor queries."))
	void GetEffectsByCategory(FGameplayTag CategoryTag, TArray<FPaper2DPlusEffectProfileEntry>& OutEffects) const;

	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Effect Profile")
	bool ValidateEffectProfileAsset(TArray<FPaper2DPlusEffectProfileValidationIssue>& OutIssues) const;

	/** Idempotently copies compatible legacy metadata into the active library schema without clearing it. */
	int32 MigrateLegacyLibrarySchema();

	/** Retained, load-order-safe name lookup used by migration and compatibility wrappers only. */
	const FPaper2DPlusEffectProfileEntry* FindEffectEntry(FName EffectName) const;
	const FPaper2DPlusEffectProfileEntry* FindEffectEntryByFlipbook(const UPaperFlipbook* EffectFlipbook) const;
	bool ResolveLegacyEffectSpawnSettings(FName EffectName, FPaper2DPlusEffectSpawnSettings& OutSettings) const;

private:
	void AddValidationIssue(
		TArray<FPaper2DPlusEffectProfileValidationIssue>& OutIssues,
		EPaper2DPlusEffectProfileValidationSeverity Severity,
		const FText& Message,
		FName EffectName = NAME_None,
		FName Field = NAME_None) const;
};
