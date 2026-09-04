// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPtr.h"
#include "Paper2DPlusFrameCurve.h"
#include "Paper2DPlusTypes.h"
#include "Paper2DPlusSettings.generated.h"

class UMaterialInterface;
class UPaper2DPlusCharacterCatalogAsset;

/**
 * Mapping from a GameplayTag to a human-readable description for tag mappings.
 * Uses TArray<FTagMappingDescription> instead of TMap<FGameplayTag, FText>
 * to work around UE-230676 (TMap<FGameplayTag, FText> Config serialization bug).
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FTagMappingDescription
{
	GENERATED_BODY()

	UPROPERTY(config, EditAnywhere, Category = "Tag Mapping Description", meta = (Categories = "Paper2DPlus.Animation"))
	FGameplayTag Tag;

	UPROPERTY(config, EditAnywhere, Category = "Tag Mapping Description")
	FText Description;
};

/**
 * One entry in the project-wide tag → color registry (the "Tag Colors" feature).
 *
 * Stored as a TArray<FPaper2DPlusTagColor> rather than a TMap<FGameplayTag, FLinearColor>
 * for the SAME reason FTagMappingDescription exists: TMap<FGameplayTag, ...> config
 * serialization is unreliable (UE-230676). The array is the on-disk source of truth;
 * lookups go through UPaper2DPlusSettings::ResolveTagColor (first-match-wins, like
 * GetDescriptionForTag).
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusTagColor
{
	GENERATED_BODY()

	/** The tag whose color this row defines. A color on an ancestor tag (e.g. Paper2DPlus.Phase)
	 *  tints every descendant that has no color of its own. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Tag Colors")
	FGameplayTag Tag;

	/** The color used wherever Paper2DPlus renders this tag (group tiles, phase badges, chips, pickers). */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Tag Colors", meta = (HideAlphaChannel))
	FLinearColor Color = FLinearColor::Gray;

	FPaper2DPlusTagColor() = default;
	FPaper2DPlusTagColor(const FGameplayTag& InTag, const FLinearColor& InColor)
		: Tag(InTag), Color(InColor) {}
};

/**
 * One hitbox-layer name-prefix rule for Aseprite import (case-insensitive StartsWith match, first
 * matching row wins). An .ase layer whose name starts with Prefix is classified as a DATA layer of
 * the given hitbox type: it never composites into the imported art, and its opaque pixel regions
 * become per-frame hitboxes. The "socket_<Name>" convention is separate and always active.
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusHitboxLayerPrefix
{
	GENERATED_BODY()

	/** Case-insensitive layer-name prefix (e.g. "attack", "hurtbox", "hitbox"). */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Hitbox Layer Prefix")
	FString Prefix;

	/** Hitbox type authored for layers matching this prefix. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Hitbox Layer Prefix")
	EHitboxType Type = EHitboxType::Attack;

	FPaper2DPlusHitboxLayerPrefix() = default;
	FPaper2DPlusHitboxLayerPrefix(const FString& InPrefix, EHitboxType InType)
		: Prefix(InPrefix), Type(InType) {}
};

UENUM(BlueprintType)
enum class EPaper2DPlusKnownCurveSemantic : uint8
{
	/** Generic scalar value; the row behaves like a normal editable curve. */
	Continuous		UMETA(DisplayName = "Continuous"),
	/** 0/1 step window, normally used by cancel gates. */
	StepWindow		UMETA(DisplayName = "Step Window"),
	/** Whole-frame count, such as HitStop freeze frames. */
	FrameCount		UMETA(DisplayName = "Frame Count"),
	/** Boolean-ish 0/1 stepped value. */
	BooleanStep		UMETA(DisplayName = "Boolean Step")
};

/**
 * A well-known auxiliary curve name the editor offers as a pick when adding a curve (TASK-74).
 * Names are free-form FNames; this registry makes common curves discoverable and gives the editor
 * enough authoring metadata to render and constrain domain-specific timeline rows (TASK-74.1).
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusKnownCurve
{
	GENERATED_BODY()

	/** Well-known curve name (matches the FName key in FFlipbookCurveData::Curves). */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Known Curves")
	FName Name;

	/** One-line description shown as a tooltip in the editor curve picker (PR2). */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Known Curves")
	FString Description;

	/** Suggested default value a query returns when this curve is unauthored on a move. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Known Curves")
	float DefaultValue = 0.f;

	/** What kind of value this curve represents; drives editor row range/snap/label behavior. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Known Curves")
	EPaper2DPlusKnownCurveSemantic Semantic = EPaper2DPlusKnownCurveSemantic::Continuous;

	/** Interpolation mode seeded when the curve is first added to a move. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Known Curves")
	EPaper2DPlusCurveInterp DefaultMode = EPaper2DPlusCurveInterp::Linear;

	/** Keep the editor row vertically locked to ValueMin..ValueMax instead of auto-fitting keys. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Known Curves")
	bool bUseFixedValueRange = false;

	/** Lower display/validation range for this curve when a range is enabled. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Known Curves")
	float ValueMin = 0.f;

	/** Upper display/validation range for this curve when a range is enabled. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Known Curves")
	float ValueMax = 1.f;

	/** Validation hint: values outside ValueMin..ValueMax are probably authoring mistakes. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Known Curves")
	bool bWarnWhenOutsideValueRange = false;

	/** Snap edited output values to OutputSnap while dragging in the curve row. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Known Curves")
	bool bSnapOutputValues = false;

	/** Output snap step used when bSnapOutputValues is true. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Known Curves", meta = (ClampMin = "0.0"))
	float OutputSnap = 1.f;

	/** Short value-unit suffix shown in editor hints, e.g. "frames". */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Known Curves")
	FString Units;

	FPaper2DPlusKnownCurve() = default;
	FPaper2DPlusKnownCurve(FName InName, const FString& InDescription, float InDefaultValue)
		: Name(InName), Description(InDescription), DefaultValue(InDefaultValue) {}
};

/**
 * Project-wide settings for Paper2DPlus.
 * Appears in Project Settings > Plugins > Paper2DPlus.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Paper2DPlus"))
class PAPER2DPLUS_API UPaper2DPlusSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPaper2DPlusSettings();

	/**
	 * Project-designated Character Catalog. Runtime access remains soft: Paper2DPlus never loads this
	 * reference implicitly and the host project's Asset Manager policy remains responsible for cooking it.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Character Catalog",
		meta = (DisplayName = "Default Character Catalog", AllowedClasses = "/Script/Paper2DPlus.Paper2DPlusCharacterCatalogAsset"))
	TSoftObjectPtr<UPaper2DPlusCharacterCatalogAsset> DefaultCharacterCatalog;

	// ---- Hybrid runtime appearance budgets (client-local presentation only) ----

	/** Maximum characters that may temporarily show authored live layers while a matching composite is pending.
	 *  Requests are granted centrally by priority/distance; this never delays logical appearance or gameplay data. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Runtime Appearance",
		meta = (ClampMin = "0", DisplayName = "Maximum Concurrent Live Handoffs"))
	int32 AppearanceMaxConcurrentLiveHandoffs = 10;

	/** Maximum composite frame-build units the one world scheduler may grant in a game frame. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Runtime Appearance",
		meta = (ClampMin = "0", DisplayName = "Composite Build Units Per Frame"))
	int32 AppearanceCompositeBuildUnitsPerFrame = 2;

	/** Hard game-thread preparation/submission budget for composite work in one frame. The unit cap remains an
	 *  independent deterministic safety limit. A unit already in progress may consume the remaining milliseconds;
	 *  the cache refuses every later claim once either limit is reached. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Runtime Appearance",
		meta = (ClampMin = "0.0", Units = "ms", DisplayName = "Composite Work Budget"))
	float AppearanceCompositeWorkBudgetMs = 0.300f;

	/** Reference near tier supports at most two independently animated channels beside the base composite. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Runtime Appearance",
		meta = (ClampMin = "0", ClampMax = "2", DisplayName = "Maximum Independent Channels"))
	int32 AppearanceMaxIndependentChannels = 2;

	/** Hard process-local cache residency budget in bytes. Consumer-owned evicted resources are not misreported as
	 *  cache residency; they remain alive only until their last visible component releases them. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Runtime Appearance",
		meta = (ClampMin = "0", DisplayName = "Transient Composite Cache Bytes"))
	int64 AppearanceTransientCacheBytes = 134217728;

	/** Closest-view distance at or below which a stable actor may keep up to two independent channels. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Runtime Appearance",
		meta = (ClampMin = "0.0", Units = "cm", DisplayName = "Near Tier Distance"))
	float AppearanceNearDistance = 3000.0f;

	/** Local actors beyond this distance are descriptor-only even when inside the padded view cone. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Runtime Appearance",
		meta = (ClampMin = "0.0", Units = "cm", DisplayName = "Maximum Appearance Distance"))
	float AppearanceMaximumVisibleDistance = 100000.0f;

	/** Conservative horizontal view-cone padding used by the primitive-free promotion test. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Runtime Appearance",
		meta = (ClampMin = "0.0", ClampMax = "45.0", Units = "deg", DisplayName = "View Frustum Padding"))
	float AppearanceFrustumPaddingDegrees = 8.0f;

	// (The old "required tag mappings" list is GONE — legacy-cleanup 2026-07. It silently
	// re-added its keys to every asset on each details refresh, resurrecting groups the user
	// had deliberately removed; group presence now derives purely from each asset's own
	// non-empty TagMappings.)

	/** Optional descriptions for each tag mapping, shown as tooltips in the editor. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Tag Mappings")
	TArray<FTagMappingDescription> TagMappingDescriptions;

	/** Enable depth (Z axis) for hitboxes and 3D viewport. When enabled, hitbox collision checks consider depth and the 3D viewer shows depth positioning. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Hitbox", meta = (DisplayName = "Enable 3D Depth"))
	bool bEnable3DDepth = false;

	/** The project-default hit-priority / clash matrix (TASK-77). One graph governs the whole game; a
	 *  game-mode/subsystem can override it at runtime. Empty = no clash resolution (all overlaps trade). */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Hitbox",
		meta = (DisplayName = "Default Clash Graph", AllowedClasses = "/Script/Paper2DPlus.Paper2DPlusClashGraphAsset"))
	TSoftObjectPtr<class UPaper2DPlusClashGraphAsset> DefaultClashGraph;

	/**
	 * On layered Aseprite import, detect normal-map layers by name convention (NormalLayerSuffixes) and pair each to its
	 * base art layer: a normal sheet is generated alongside the base sheet and attached to the base sprites' secondary
	 * texture slot (AdditionalSourceTextures[0] -> material AdditionalTexture0). Provably a byte-identical no-op when no
	 * normal layers are present, so leaving this on is safe for flat/ungrouped files. (TASK-72)
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Normal Map Import",
		meta = (DisplayName = "Pair Normal Maps On Import"))
	bool bPairNormalMapsOnImport = true;

	/**
	 * Name suffixes that mark a layer as the normal map for its base sibling (case-insensitive). A layer named
	 * "<baseName><suffix>" pairs with the base art layer "<baseName>" when one exists. Default {"_normal","_n"} — the
	 * Laigter / SpriteIlluminator convention. Suffixes are tried longest-first so "_normal" wins over "_n". (TASK-72)
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Normal Map Import",
		meta = (DisplayName = "Normal Layer Suffixes"))
	TArray<FString> NormalLayerSuffixes = { TEXT("_normal"), TEXT("_n") };

	/**
	 * Sprite-lit base material assigned to the generated base sprites when a paired normal map is attached (TASK-72).
	 * NULL-SAFE and project-assigned: the actual lit UMaterial sampling AdditionalTexture0 as a Normal lives in Content
	 * and is NEVER staged by the plugin (CanContainContent:false) — assign it here per project. While this is null the
	 * normal texture is still attached (harmless under the default unlit material) and a single log hint is emitted that
	 * a lit material must be assigned to actually see depth lighting. Deliberately NOT a ConstructorHelpers::FObjectFinder
	 * to a Content path — that would hard-depend on an unstaged asset and assert on a clean checkout (mirrors
	 * UPaper2DPlusLayerRenderComponent::RecolorBaseMaterial).
	 */
	UPROPERTY(config, EditDefaultsOnly, BlueprintReadOnly, Category = "Normal Map Import",
		meta = (DisplayName = "Sprite Lit Material", AllowedClasses = "/Script/Engine.MaterialInterface"))
	TSoftObjectPtr<UMaterialInterface> SpriteLitMaterial;

	/**
	 * Layer-name prefixes that mark an .ase layer as hitbox DATA on Aseprite import (case-insensitive,
	 * first matching row wins). Matching layers never composite into the imported art; their opaque
	 * pixel regions become per-frame hitboxes of the row's type. Defaults cover the shipped
	 * "attack*"/"hurtbox*" convention plus the common generic "hitbox*" (imported as Attack). An
	 * EMPTY list falls back to those same defaults at import time — clearing it cannot silently bake
	 * data layers into art. The "socket_<Name>" convention is separate and always active.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Aseprite Import",
		meta = (DisplayName = "Hitbox Layer Name Prefixes", TitleProperty = "Prefix"))
	TArray<FPaper2DPlusHitboxLayerPrefix> HitboxLayerNamePrefixes = {
		FPaper2DPlusHitboxLayerPrefix(TEXT("attack"), EHitboxType::Attack),
		FPaper2DPlusHitboxLayerPrefix(TEXT("hurtbox"), EHitboxType::Hurtbox),
		FPaper2DPlusHitboxLayerPrefix(TEXT("hitbox"), EHitboxType::Attack) };

	/**
	 * Automatically re-run the layered import when a tracked .ase source file changes on disk — a save
	 * from Aseprite while the editor is open, or an offline edit found at editor startup. Turn this off
	 * to stop the automatic reimport entirely; changed files are still detected by content hash, and
	 * everything that drifted is reimported the moment the setting is turned back on, so no edit is
	 * lost while it is off. Non-Aseprite source textures keep reimporting either way.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Aseprite Import",
		meta = (DisplayName = "Live .ase Auto-Reimport"))
	bool bEnableAseLiveReimport = true;

	/**
	 * Well-known auxiliary curve names offered as picks in the Frame Cues curve tracks. Each carries
	 * a description, default value, and semantic authoring hints. Free-form names are still allowed —
	 * this is a discoverability/typo aid, not a whitelist.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Curves",
		meta = (DisplayName = "Known Curves", TitleProperty = "Name"))
	TArray<FPaper2DPlusKnownCurve> KnownCurves;

	/**
	 * Authoring convention for interpreting "HitStop" curve values as frames (fighting-game 60ths by default),
	 * independent of a flipbook's FramesPerSecond. Paper2DPlus does not automatically read that curve: game logic
	 * may query it and pass its chosen duration to TriggerHitStop.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Hit Stop", meta = (ClampMin = "1.0"))
	float HitStopFramesPerSecond = 60.f;

	/** Legacy input-buffer setting. Retained under its original reflected/config name so historical
	 *  DefaultGame.ini values load; the transition driver was removed and this value is ignored. */
	UPROPERTY(config, meta = (DeprecatedProperty, DeprecationMessage = "The Paper2DPlus transition driver and input buffer were removed; this value is ignored."))
	int32 InputBufferGraceFrames_DEPRECATED = 6;

	/**
	 * Project-wide tag → color registry. Wherever Paper2DPlus shows a gameplay tag — the Character
	 * Profile Animation Map group tiles, phase badges, inline tag chips, and the native tag-picker
	 * chip — the color comes from here. This is an OVERRIDE list: it starts empty, and each consumer
	 * keeps its own built-in fallback (the phase badge's color table, the group tile's hash palette),
	 * so an unconfigured project looks exactly as before. Add a row to override a tag's color. A color
	 * on an ANCESTOR tag tints all descendants with no color of their own.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Tag Colors",
		meta = (DisplayName = "Tag Colors", TitleProperty = "Tag"))
	TArray<FPaper2DPlusTagColor> TagColors;

	/**
	 * When true, Paper2DPlus colorizes the native gameplay-tag picker chip editor-wide (any FGameplayTag
	 * property shows its registered color, Fab-plugin style). This overrides Unreal's default FGameplayTag
	 * property widget, so disable it if it interferes with other tag UI. Takes effect on editor restart.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Tag Colors",
		meta = (DisplayName = "Colorize Gameplay Tag Pickers"))
	bool bColorizeGameplayTagPickers = true;

	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
	virtual FName GetSectionName() const override { return FName(TEXT("Paper2DPlus")); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Find description for a tag mapping. Returns empty FText if not found. */
	FText GetDescriptionForTag(const FGameplayTag& Tag) const;

	/** Singleton accessor via GetDefault<>(). */
	static const UPaper2DPlusSettings* Get();

	// ---- Tag color registry API (THE single resolution source — never read TagColors directly) ----

	/**
	 * Resolve a tag's registered color. Resolution order:
	 *   1. exact match in TagColors,
	 *   2. nearest registered ANCESTOR tag (Paper2DPlus.Phase.Active → Paper2DPlus.Phase → …),
	 *   3. not found → bOutFound = false (the caller applies its own legacy fallback).
	 * Pure / worldless-safe (reads GetDefault<>()).
	 */
	static FLinearColor ResolveTagColor(const FGameplayTag& Tag, bool& bOutFound);

	/** Convenience: ResolveTagColor with an explicit fallback baked in. */
	static FLinearColor ResolveTagColorOrDefault(const FGameplayTag& Tag, const FLinearColor& Fallback);

	/** True if the registry has at least one entry (callers can skip a lookup when false). */
	static bool HasAnyTagColors();

	/**
	 * Set (or clear, when bRemove) a tag's color on the mutable default, persist it to the project's
	 * default config (DefaultGame.ini), and broadcast OnTagColorsChanged so open editors repaint.
	 * Editor-authoring entry point used by the in-editor color swatch.
	 */
	static void SetTagColor(const FGameplayTag& Tag, const FLinearColor& Color);
	static void ClearTagColor(const FGameplayTag& Tag);

	/** Fires after the registry changes (settings edit or SetTagColor/ClearTagColor) so UI can refresh. */
	static FSimpleMulticastDelegate& OnTagColorsChanged();

	/** Fires once after a committed Catalog authority/root edit; passive discovery subscribes to this. */
	static FSimpleMulticastDelegate& OnCharacterCatalogSettingsChanged();

	/** Fires after a committed edit of Live .ase Auto-Reimport. The texture watcher subscribes so the
	 *  off-to-on transition can reconcile files that drifted while the setting was off, without an
	 *  editor restart. */
	static FSimpleMulticastDelegate& OnAseLiveReimportSettingChanged();
};
