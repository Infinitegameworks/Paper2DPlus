// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/WeakObjectPtr.h"

class FProperty;
class UBlueprint;
class UClass;
class UEdGraph;
class UK2Node_Event;
class UPackage;
class UPaper2DPlusFrameCueBlueprint;

/** Lifecycle shape owned by a concrete Frame Cue class. */
enum class EPaper2DPlusFrameCueTypeKind : uint8
{
	Invalid,
	Moment,
	Range
};

/** Where a Cue Type's authorable class came from. */
enum class EPaper2DPlusFrameCueTypeOrigin : uint8
{
	Native,
	SpecializedBlueprint,
	LegacyBlueprint
};

/** Placement readiness. Only Ready types belong in a placement picker. */
enum class EPaper2DPlusFrameCueTypeAvailability : uint8
{
	Invalid,
	NeedsCompile,
	NeedsDurableSave,
	LegacyNeedsBaseline,
	Ready
};

enum class EPaper2DPlusFrameCueDiagnosticSeverity : uint8
{
	Info,
	Warning,
	Error
};

enum class EPaper2DPlusFrameCueDiagnosticCode : uint8
{
	None,
	NullClass,
	NotFrameCue,
	NonAuthorableClass,
	EditorOnlyClass,
	MissingBlueprint,
	CompileRequired,
	InvalidDataOnlyContract,
	InvalidGeneratedEnvelope,
	InvalidCompiledSchema,
	DeferredBehavior,
	ParentCueTypeNotReady,
	DurableSchemaMissing,
	DurableSchemaMismatch,
	DirtyPackage,
	LegacyBehaviorGraphs,
	LegacyBaselineRequired,
	DiscoveryLoadFailed,
	InvalidPayloadField
};

struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueAuthoringDiagnostic
{
	EPaper2DPlusFrameCueDiagnosticSeverity Severity =
		EPaper2DPlusFrameCueDiagnosticSeverity::Error;
	EPaper2DPlusFrameCueDiagnosticCode Code =
		EPaper2DPlusFrameCueDiagnosticCode::None;
	FText Message;
};

enum class EPaper2DPlusFrameCueSchemaSource : uint8
{
	Authored,
	Compiled
};

enum class EPaper2DPlusFrameCueSchemaContainer : uint8
{
	None,
	Array,
	Set,
	Map
};

enum class EPaper2DPlusFrameCueSchemaDependencyKind : uint8
{
	ParentCueType,
	UserDefinedStruct,
	UserDefinedEnum
};

enum class EPaper2DPlusFrameCueSchemaCompatibility : uint8
{
	Identical,
	Compatible,
	Conditional,
	DependencyChange,
	Destructive,
	Invalid
};

enum class EPaper2DPlusFrameCueSchemaChangeKind : uint8
{
	FieldAdded,
	FieldRemoved,
	FieldRenamed,
	FieldTypeChanged,
	FieldContainerChanged,
	FieldDefaultChanged,
	FieldMetadataChanged,
	FieldFlagsChanged,
	DependencyChanged,
	InheritedDefaultChanged,
	ParentChanged,
	KindChanged,
	BehaviorChanged,
	InvalidSchema
};

struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueSchemaMetadata
{
	FName Key;
	FString Value;
};

/** Direct payload field declared by one Cue Type Blueprint. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueSchemaField
{
	FGuid FieldId;
	FName Name;
	FString FriendlyName;
	FString Category;
	FString TypeKey;
	EPaper2DPlusFrameCueSchemaContainer Container =
		EPaper2DPlusFrameCueSchemaContainer::None;
	uint64 PropertyFlags = 0;
	FString DefaultValue;
	TArray<FPaper2DPlusFrameCueSchemaMetadata> Metadata;
};

/** Inherited class default that changes placement behavior but is not placement timing. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueInheritedDefault
{
	FSoftObjectPath OwnerClassPath;
	FName Name;
	FString TypeKey;
	FString DefaultValue;
};

struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueSchemaDependency
{
	EPaper2DPlusFrameCueSchemaDependencyKind Kind =
		EPaper2DPlusFrameCueSchemaDependencyKind::ParentCueType;
	FSoftObjectPath ObjectPath;
	FString Fingerprint;
};

/**
 * Deterministic, transient schema description. Only its fingerprint and snapshot are persisted.
 *
 * Version 1 describes a payload-only Cue Type. Version 2 additionally describes implemented Cue
 * behavior events, and is emitted only when at least one exists, so a behavior-free type still
 * fingerprints byte-identically to its version 1 baseline and never needs a restaging pass.
 */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueSchema
{
	int32 Version = 1;
	EPaper2DPlusFrameCueTypeKind Kind = EPaper2DPlusFrameCueTypeKind::Invalid;
	FSoftObjectPath ParentClassPath;
	/** Declared Cue behavior events this type implements, sorted; empty for a payload-only type. */
	TArray<FName> BehaviorEvents;
	TArray<FPaper2DPlusFrameCueSchemaField> Fields;
	TArray<FPaper2DPlusFrameCueInheritedDefault> InheritedDefaults;
	TArray<FPaper2DPlusFrameCueSchemaDependency> Dependencies;
	TArray<FPaper2DPlusFrameCueAuthoringDiagnostic> Diagnostics;
	FString CanonicalText;
	FString Fingerprint;
	bool bValid = false;
};

struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueSchemaChange
{
	EPaper2DPlusFrameCueSchemaChangeKind Kind =
		EPaper2DPlusFrameCueSchemaChangeKind::InvalidSchema;
	FGuid FieldId;
	FName OldName;
	FName NewName;
	FText Summary;
};

struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueSchemaDiff
{
	EPaper2DPlusFrameCueSchemaCompatibility Compatibility =
		EPaper2DPlusFrameCueSchemaCompatibility::Identical;
	TArray<FPaper2DPlusFrameCueSchemaChange> Changes;
};

struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueLegacyGraphEntry
{
	FSoftObjectPath GraphPath;
	FName GraphName;
	FName GraphKind;
	int32 NodeCount = 0;
};

/** Read-only inventory of authored graphs; compiler intermediates are deliberately excluded. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueLegacyGraphInventory
{
	TArray<FPaper2DPlusFrameCueLegacyGraphEntry> Graphs;

	bool HasBehaviorGraphs() const { return Graphs.Num() > 0; }

	/** Graphs a specialized Cue Type may never own: functions, macros, delegates, interfaces. */
	int32 CountUnsupportedGraphs() const;

	/** True when the asset carries at least one permitted Cue event graph. */
	bool HasEventGraphs() const;
};

struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueTypeDescriptor
{
	TWeakObjectPtr<UClass> Class;
	FSoftObjectPath ClassPath;
	FSoftObjectPath BlueprintPath;
	FText DisplayName;
	FText PickerLabel;
	EPaper2DPlusFrameCueTypeKind Kind = EPaper2DPlusFrameCueTypeKind::Invalid;
	EPaper2DPlusFrameCueTypeOrigin Origin = EPaper2DPlusFrameCueTypeOrigin::Native;
	EPaper2DPlusFrameCueTypeAvailability Availability =
		EPaper2DPlusFrameCueTypeAvailability::Invalid;
	FString CompiledSchemaFingerprint;
	FString DurableSchemaFingerprint;
	TArray<FPaper2DPlusFrameCueAuthoringDiagnostic> Diagnostics;
};

struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueTypeDiscoveryResult
{
	/** Ready, concrete Cue Types, sorted by picker label and then class path. */
	TArray<FPaper2DPlusFrameCueTypeDescriptor> Types;
	/** Rejected/not-yet-ready classes retained for diagnostics and recovery UI. */
	TArray<FPaper2DPlusFrameCueTypeDescriptor> RejectedTypes;
};

/** Non-mutating persistence/placement decision; compile/save/recovery is intentionally elsewhere. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueSchemaPreflight
{
	FPaper2DPlusFrameCueSchema CurrentSchema;
	FPaper2DPlusFrameCueSchema DurableSchema;
	FPaper2DPlusFrameCueSchemaDiff Diff;
	FPaper2DPlusFrameCueTypeDescriptor Type;
	bool bHasDurableBaseline = false;
	bool bRequiresDestructiveConfirmation = false;
	bool bCanCompile = false;
	bool bCanPlace = false;
	bool bCanPersist = false;

	/**
	 * Why the authored schema could not be described, when it could not. Refusal surfaces must
	 * present this instead of guessing: an invalid authored schema and an untrusted durable
	 * baseline both land in Diff.Compatibility == Invalid, and only these texts tell them apart.
	 */
	FText CurrentSchemaError;

	/** Why the stored durable baseline was rejected, when the asset carries one that failed. */
	FText DurableBaselineError;
};

enum class EPaper2DPlusFrameCueTypeCreateStatus : uint8
{
	Succeeded,
	InvalidPackage,
	InvalidName,
	InvalidKind,
	InvalidFlags,
	NameCollision,
	CreationFailed,
	NormalizationFailed,
	CompileFailed,
	ValidationFailed,
	CleanupFailed
};

/** Exact non-interactive input used by both Content Browser and inline Cue Type creation. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueTypeCreateRequest
{
	UPackage* Package = nullptr;
	FName AssetName;
	EPaper2DPlusFrameCueTypeKind Kind = EPaper2DPlusFrameCueTypeKind::Moment;
	EObjectFlags ObjectFlags = RF_Public | RF_Standalone | RF_Transactional;

#if WITH_DEV_AUTOMATION_TESTS
	/** Exercises post-allocation rollback without inventing a production recovery path. */
	bool bFailAfterCreateForTests = false;
#endif
};

struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueTypeCreateResult
{
	EPaper2DPlusFrameCueTypeCreateStatus Status =
		EPaper2DPlusFrameCueTypeCreateStatus::CreationFailed;
	UPaper2DPlusFrameCueBlueprint* CueType = nullptr;
	FText Error;

	bool IsSuccess() const
	{
		return Status == EPaper2DPlusFrameCueTypeCreateStatus::Succeeded && CueType != nullptr;
	}
};

/** Validated schema values written immediately before a caller-owned package save attempt. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueDurableSchemaCandidate
{
	bool bSuccess = false;
	int32 Version = 0;
	FString Fingerprint;
	FString SchemaSnapshot;
	FText Error;
};

/**
 * Shared non-Slate Cue Type classification, discovery, schema, and detail-filtering service.
 *
 * Mutation is narrowly bounded to failure-atomic creation and preparing an unsaved durable schema
 * candidate. Package saving, legacy migration, destructive recovery, and placement remain outside.
 */
class PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueTypeAuthoring
{
public:
	/** Current behavior-capable schema version; behavior-free saves retain BehaviorFreeSchemaVersion. */
	static constexpr int32 CurrentSchemaVersion = 2;
	/** Version a payload-only Cue Type still describes, so its fingerprint never changes. */
	static constexpr int32 BehaviorFreeSchemaVersion = 1;

	/** True for a durable stamp this build can still read without a restaging pass. */
	static bool IsAcceptedDurableSchemaVersion(int32 Version);

	static EPaper2DPlusFrameCueTypeKind ClassifyKind(const UClass* CueClass);
	static FPaper2DPlusFrameCueTypeDescriptor DescribeCueType(UClass* CueClass);

	/**
	 * Discovers loaded/native classes and, by default, cold Blueprint-generated classes through the
	 * Asset Registry. UE 5.0 maps simple derived class names back through GeneratedClassPath tags.
	 */
	static FPaper2DPlusFrameCueTypeDiscoveryResult DiscoverCueTypes(
		bool bIncludeUnloadedBlueprints = true);

	/**
	 * Creates one specialized, compiled behavior-capable Cue Type under the exact caller package/name.
	 * Only the internal Moment (Cue) and Range (Cue State) kinds are accepted. Any failure after
	 * allocation removes every created export
	 * and restores the package dirty state; this method never saves the package.
	 */
	static FPaper2DPlusFrameCueTypeCreateResult CreateCueType(
		const FPaper2DPlusFrameCueTypeCreateRequest& Request);

	static bool DescribeSchema(
		const UBlueprint& Blueprint,
		EPaper2DPlusFrameCueSchemaSource Source,
		FPaper2DPlusFrameCueSchema& OutSchema,
		FText* OutError = nullptr);
	static FPaper2DPlusFrameCueSchemaDiff DiffSchemas(
		const FPaper2DPlusFrameCueSchema& Baseline,
		const FPaper2DPlusFrameCueSchema& Candidate);
	static FPaper2DPlusFrameCueSchemaPreflight BuildSchemaPreflight(
		const UBlueprint& Blueprint);
	static bool SerializeSchemaSnapshot(
		const FPaper2DPlusFrameCueSchema& Schema,
		FString& OutSnapshot,
		FText* OutError = nullptr);
	static bool DeserializeSchemaSnapshot(
		const FString& Snapshot,
		FPaper2DPlusFrameCueSchema& OutSchema,
		FText* OutError = nullptr);
	static bool CaptureDefaultValues(
		const UBlueprint& Blueprint,
		TMap<FName, FString>& OutValues,
		FText* OutError = nullptr);
	static bool ApplyDefaultValues(
		UBlueprint& Blueprint,
		const TMap<FName, FString>& Values,
		FText* OutError = nullptr,
		bool bRequireEveryProperty = true);

	/**
	 * Validates the complete compiled U8/U2 envelope, then writes the schema's applicable
	 * version/fingerprint as an unsaved candidate. The package remains dirty and therefore cannot
	 * be placed until the caller completes a real package save.
	 */
	static FPaper2DPlusFrameCueDurableSchemaCandidate PrepareDurableSchemaSaveCandidate(
		UPaper2DPlusFrameCueBlueprint& Blueprint);

	/** Runtime-envelope save bridge used by Save All and other non-toolkit package saves. */
	static bool ValidateDurableSaveMetadataForPersistence(
		UPaper2DPlusFrameCueBlueprint& Blueprint,
		FText* OutError = nullptr);

	/** Synchronous-source policy plus exact source/generated field parity for save/cook guards. */
	static bool ValidateCompiledSchemaParity(
		const UBlueprint& Blueprint,
		FText* OutError = nullptr);

	/**
	 * Enforces the shared-placement execution envelope across the complete authored behavior source.
	 *
	 * The scan is semantic, recursive, and cycle-safe: it follows collapsed/subgraphs, referenced
	 * macros, and specialized Cue parents, and rejects latent functions, async actions, and known
	 * timer/self-scheduling calls. It never rewrites the graph or contributes to durable fingerprints.
	 */
	static bool ValidateSynchronousBehavior(
		const UBlueprint& Blueprint,
		FText* OutError = nullptr);

	/** True only for TriggerFrame, StartFrame, and FrameCount as owned by their runtime bases. */
	static bool IsPlacementTimingProperty(const FProperty* Property);

	/**
	 * True for concrete automation-fixture cue classes marked meta=(Paper2DPlusAutomationFixture).
	 * Fixtures stay fully Ready for the placement-authoring commit gate (tests keep exercising the
	 * production path), but every designer-facing picker filters on this, so a Fab user's + Add
	 * Cue list never shows an automation type (TASK-173). Metadata rather than HideDropdown
	 * because HideDropdown sits in RejectedClassFlags and would break the Ready gate the tests
	 * need; metadata rather than a WITH_DEV_AUTOMATION_TESTS UCLASS guard because UHT does not
	 * portably honor that symbol across UE 5.0-5.8.
	 */
	static bool IsAutomationFixtureCueClass(const UClass* CueClass);
	static bool IsCueAuthoringPropertyVisible(const FProperty* Property);

	/** Inventories authored graph arrays without GetAllGraphs and without mutating the asset. */
	static FPaper2DPlusFrameCueLegacyGraphInventory InventoryLegacyGraphs(
		const UBlueprint& Blueprint);

	/** Declared Cue behavior events authored in this asset's event graph, sorted by name. */
	static TArray<FName> GetAuthoredBehaviorEvents(const UBlueprint& Blueprint);

	/** Declared Cue behavior events the current generated class implements, sorted by name. */
	static TArray<FName> GetCompiledBehaviorEvents(const UBlueprint& Blueprint);

	/** The one event graph a Cue Type authors behavior in, or null when none exists yet. */
	static UEdGraph* FindBehaviorEventGraph(const UBlueprint& Blueprint);

	/** Creates the one permitted event graph when the asset does not have it yet. */
	static UEdGraph* EnsureBehaviorEventGraph(UBlueprint& Blueprint);

	/** The override node for one declared Cue behavior event, or null when not implemented. */
	static UK2Node_Event* FindBehaviorEventNode(const UBlueprint& Blueprint, FName EventName);

	/** Adds the override node for one declared Cue behavior event. True only when one was added. */
	static bool EnsureBehaviorEventNode(UBlueprint& Blueprint, FName EventName);

	/** Seeds the guaranteed behavior events for the asset's Cue kind. True when anything was added. */
	static bool SeedDeclaredBehaviorEvents(UBlueprint& Blueprint);
};
