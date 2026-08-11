// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "Blueprint/BlueprintExtension.h"
#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/ObjectSaveContext.h"
// The UHT-generated companion for this header emits package-flag code that needs the complete
// UPackage type. UE 5.6+ pulls it in transitively; 5.0-5.5 do not, and the failure lands in
// Paper2DPlusFrameCueBlueprint.gen.cpp rather than here (C2027: undefined type 'UPackage').
#include "UObject/Package.h"
#include "Paper2DPlusFrameCueBlueprint.generated.h"

class FKismetCompilerContext;
class UPaper2DPlusFrameCueBlueprint;

#if WITH_EDITOR
DECLARE_DELEGATE_TwoParams(
	FPaper2DPlusFrameCueCompileValidation,
	UPaper2DPlusFrameCueBlueprint&,
	FKismetCompilerContext*);

DECLARE_DELEGATE_RetVal_TwoParams(
	bool,
	FPaper2DPlusFrameCueCompiledSchemaValidation,
	const UBlueprint&,
	FText*);

/** Editor-module bridge that rejects persistence unless durable metadata was explicitly staged. */
DECLARE_DELEGATE_RetVal_TwoParams(
	bool,
	FPaper2DPlusFrameCueDurableSaveValidation,
	UPaper2DPlusFrameCueBlueprint&,
	FText*);

/**
 * Editor-module bridge used by ordinary package saves (including Save All).
 *
 * The editor module owns compilation and schema authoring; the runtime envelope owns the
 * failure-atomic PreSaveRoot/PostSaveRoot transaction around that preparation.
 */
DECLARE_DELEGATE_RetVal_TwoParams(
	bool,
	FPaper2DPlusFrameCueAutomaticSavePreparation,
	UPaper2DPlusFrameCueBlueprint&,
	FText*);

/**
 * Editor-module bridge fired when a persistent save is refused (Ar.SetError during Serialize).
 *
 * The engine's own failed-save prompt never explains why a package write failed, so without this
 * seam the refusal reason exists only in the log while the user faces a Retry loop that can never
 * succeed. The editor module binds it to an on-screen notification.
 */
DECLARE_DELEGATE_TwoParams(
	FPaper2DPlusFrameCuePersistRefusalNotification,
	const UPaper2DPlusFrameCueBlueprint&,
	const FText&);
#endif

/**
 * Per-asset compiler backstop for the restricted, behavior-capable Frame Cue Type contract.
 *
 * The restricted toolkit permits exactly the declared Cue behavior events and durable payload
 * fields. This extension additionally makes direct/programmatic compilation fail closed if any
 * unsupported graph or generated behavior is injected through another editor utility or an older
 * engine workflow.
 */
UCLASS()
class PAPER2DPLUS_API UPaper2DPlusFrameCueBlueprintCompilerExtension
	: public UBlueprintExtension
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
private:
	virtual void HandleGenerateFunctionGraphs(
		class FKismetCompilerContext* CompilerContext) override;
#endif
};

/**
 * Runtime generated-class envelope for a designer-authored Cue Type.
 *
 * Unlike UBlueprint, this class is an export in the cooked package. Its cook serialization is the
 * final fail-closed boundary that prevents an invalid behavior-bearing Cue Type from being staged.
 */
UCLASS()
class PAPER2DPLUS_API UPaper2DPlusFrameCueBlueprintGeneratedClass
	: public UBlueprintGeneratedClass
{
	GENERATED_BODY()

public:
	virtual void Serialize(FArchive& Ar) override;
};

/**
 * Persistent asset envelope for designer-authored Frame Cue Types.
 *
 * The generated class remains an ordinary Cue or Cue State subclass. This envelope exists so
 * the saved asset has a stable runtime-module class path while all creation, compilation, and UI
 * behavior remains in Paper2DPlusEditor.
 */
UCLASS()
class PAPER2DPLUS_API UPaper2DPlusFrameCueBlueprint : public UBlueprint
{
	GENERATED_BODY()

public:
	virtual void Serialize(FArchive& Ar) override;

#if WITH_EDITOR
	virtual void PreSaveRoot(FObjectPreSaveRootContext ObjectSaveContext) override;
	virtual void PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext) override;

	/**
	 * Validates the restricted behavior/payload contract shared by editor compile/save and cook
	 * checks. Existing unsupported graphs are reported, never removed. The function name is retained
	 * for source compatibility with the original payload-only envelope.
	 */
	static bool ValidateDataOnlyContract(
		const UBlueprint& Blueprint,
		FText* OutError = nullptr);

	/** Validates the specialized generated-class identity without requiring a current schema compile. */
	static bool ValidateGeneratedClassEnvelope(
		const UBlueprint& Blueprint,
		FText* OutError = nullptr,
		const UClass* ExpectedGeneratedClass = nullptr);

	/** Validates the complete source/generated contract required immediately before save or cook. */
	static bool ValidateCompiledDataOnlyContract(
		const UBlueprint& Blueprint,
		FText* OutError = nullptr,
		const UClass* ExpectedGeneratedClass = nullptr);

	/** Property flags that would make an authored payload field unsafe to persist or transact. */
	static uint64 GetForbiddenPayloadPropertyFlags();

	/**
	 * True when the generated class may own this function: the compiler's own event-graph function,
	 * or an override of a behavior event declared by the Cue bases. Everything else is behavior
	 * smuggled past the authoring surface and fails the envelope.
	 */
	static bool IsPermittedGeneratedCueFunction(
		const UBlueprint& Blueprint,
		const UFunction* Function);

	/**
	 * True for the compiler-generated event-graph frame pointer. It is a transient implementation
	 * detail of the permitted event graph, never an authored payload field.
	 */
	static bool IsGeneratedEventGraphFrameProperty(
		const UClass* GeneratedClass,
		const FProperty* Property);

	/** Editor module bridge used by the persistent per-Blueprint compiler extension. */
	static void SetCompileValidationDelegate(
		FPaper2DPlusFrameCueCompileValidation InDelegate);
	static void ClearCompileValidationDelegate();
	static bool IsCompileValidationDelegateBound();
	static void ExecuteCompileValidation(
		UPaper2DPlusFrameCueBlueprint& Blueprint,
		FKismetCompilerContext* CompilerContext);

	/** Editor-only type-parity bridge used by direct package save and real cook validation. */
	static void SetCompiledSchemaValidationDelegate(
		FPaper2DPlusFrameCueCompiledSchemaValidation InDelegate);
	static void ClearCompiledSchemaValidationDelegate();
	static bool IsCompiledSchemaValidationDelegateBound();
	static void SetDurableSaveValidationDelegate(
		FPaper2DPlusFrameCueDurableSaveValidation InDelegate);
	static void ClearDurableSaveValidationDelegate();
	static bool IsDurableSaveValidationDelegateBound();
	static void SetAutomaticSavePreparationDelegate(
		FPaper2DPlusFrameCueAutomaticSavePreparation InDelegate);
	static void ClearAutomaticSavePreparationDelegate();
	static bool IsAutomaticSavePreparationDelegateBound();
	static void SetPersistRefusalNotificationDelegate(
		FPaper2DPlusFrameCuePersistRefusalNotification InDelegate);
	static void ClearPersistRefusalNotificationDelegate();

	/**
	 * Commits a prepared ordinary-save transaction after CoreUObject broadcasts the exact
	 * package-saved event. PostSaveRoot cannot be the authority: UE 5.0-5.8 do not propagate the
	 * final FSavePackageResultStruct into FObjectPostSaveRootContext::SaveSucceeded().
	 */
	void CommitAutomaticDurableSaveAfterPackageSaved();

	/**
	 * Cancels every queued failure fallback and restores each pending pre-save snapshot. The editor
	 * and runtime module shutdown paths both call this so no ticker thunk can outlive plugin code.
	 */
	static void ResolveAllAutomaticDurableSaveTransactionsForShutdown();

#if WITH_DEV_AUTOMATION_TESTS
	/** Narrow test seam used only to persist the intentionally invalid real-cook fixture. */
	static void SetPersistentSaveGuardBypassForAutomation(bool bBypass);
	static int32 GetAutomaticDurableSaveRollbackCountForTests();
#endif

	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual UClass* GetBlueprintClass() const override
	{
		return UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass();
	}

	virtual bool SupportsGlobalVariables() const override { return true; }
	virtual bool SupportsLocalVariables() const override { return false; }
	virtual bool SupportsFunctions() const override { return false; }
	virtual bool SupportsMacros() const override { return false; }
	virtual bool SupportsDelegates() const override { return false; }
	/** Cue behavior is authored as overrides of the declared Cue events in one permitted event graph. */
	virtual bool SupportsEventGraphs() const override { return true; }
	virtual bool SupportsAnimLayers() const override { return false; }

#if !UE_VERSION_OLDER_THAN(5, 1, 0)
	/**
	 * Stock My Blueprint must never offer its private ImplementFunction route. Cue behavior
	 * overrides are created only through the editor's AddDefaultEventNode path.
	 */
	virtual bool AllowFunctionOverride(const UFunction* const InFunction) const override;
#endif

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION <= 2
	virtual EDataValidationResult IsDataValid(TArray<FText>& ValidationErrors) override;
#else
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
#endif

#if WITH_EDITORONLY_DATA
	/** Format of the last schema snapshot durably saved with this asset. */
	UPROPERTY()
	int32 DurableSchemaVersion = 1;

	/** Stable digest of the last schema/default state durably saved with this asset. */
	UPROPERTY()
	FString DurableSchemaFingerprint;

	/** Versioned full schema baseline used to classify the next edit before compilation. */
	UPROPERTY()
	FString DurableSchemaSnapshot;

	/** Exact authored variable model from the last durable save, retained for failed-save recovery. */
	UPROPERTY()
	TArray<FBPVariableDescription> DurableVariables;

	/** Exact generated-class defaults from the last durable save, keyed by reflected property name. */
	UPROPERTY()
	TMap<FName, FString> DurableDefaultValues;

	/**
	 * Exact unsaved default proposal retained across restricted-editor close/reopen after rollback.
	 * Transient state cannot be mistaken for durable metadata by Save All or autosave.
	 */
	UPROPERTY(Transient, DuplicateTransient)
	TMap<FName, FString> PendingDefaultProposalValues;

	/** True even when the exact pending proposal is an empty string or contains no authored fields. */
	UPROPERTY(Transient, DuplicateTransient)
	bool bHasPendingDefaultProposal = false;

	/** True while the durable generated CDO still needs to be rebuilt into the pending proposal. */
	UPROPERTY(Transient, DuplicateTransient)
	bool bPendingDefaultProposalNeedsCompile = false;

	/**
	 * Authored-schema fingerprint whose destructive/conditional diff the author explicitly
	 * confirmed through the restricted editor's compile dialog, valid for this in-memory asset
	 * only. Ordinary saves (Save All, Content Browser, editor-close prompts) may stage a schema
	 * change past the fail-closed refusal only while the current authored schema still equals this
	 * exact confirmation; any further schema edit changes the fingerprint and restores the refusal.
	 * Never serialized: a confirmation must not outlive the session that reviewed the change list.
	 */
	FString ConfirmedDestructiveSchemaFingerprint;

	// Non-reflected save-transaction state. Save All may compile and stage a compatible Cue Type in
	// PreSaveRoot; the success-only package event commits that exact candidate, while the next-tick
	// failure fallback restores this snapshot. Keeping it outside UPROPERTY serialization prevents a
	// half-finished attempt from becoming asset data.
	bool bAutomaticDurableSaveAttemptActive = false;
	bool bAutomaticDurableSavePackageWasDirty = false;
	uint64 AutomaticDurableSaveAttemptGeneration = 0;
	int32 AutomaticDurableSavePreviousVersion = 0;
	FString AutomaticDurableSavePreviousFingerprint;
	FString AutomaticDurableSavePreviousSchemaSnapshot;
	TArray<FBPVariableDescription> AutomaticDurableSavePreviousVariables;
	TMap<FName, FString> AutomaticDurableSavePreviousDefaultValues;
	FText AutomaticDurableSavePreparationError;
#endif

#if WITH_EDITOR
private:
	void EnsureDataOnlyCompilerExtension();
	void RestoreAutomaticDurableSaveSnapshot();
	void ClearAutomaticDurableSaveTransaction();
	void QueueAutomaticDurableSaveFailureRollback();
	void RollbackAutomaticDurableSaveIfPending(uint64 AttemptGeneration);
#endif
};
