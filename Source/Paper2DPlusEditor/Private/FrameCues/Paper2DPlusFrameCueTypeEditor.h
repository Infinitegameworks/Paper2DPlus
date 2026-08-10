// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "BlueprintEditor.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"

class FPaper2DPlusFrameCuePendingPlacement;
class IToolkitHost;
class UPackage;
class UPaper2DPlusFrameCueBlueprint;
class UBlueprint;
class UEdGraph;
class SWidget;
class UTimelineTemplate;
class UK2Node_Event;
struct FPaper2DPlusFrameCueCompiledRecoveryState;
struct FPaper2DPlusFrameCueSchemaPreflight;

/** Previous durable metadata plus the exact candidate expected from one synchronous save attempt. */
struct FPaper2DPlusFrameCueDurableSaveAttempt
{
	TWeakObjectPtr<UPaper2DPlusFrameCueBlueprint> CueType;
	TWeakObjectPtr<UPackage> Package;
	int32 PreviousVersion = 0;
	FString PreviousFingerprint;
	FString PreviousSchemaSnapshot;
	TArray<FBPVariableDescription> PreviousVariables;
	TMap<FName, FString> PreviousDefaultValues;
	bool bPackageWasDirty = false;
	int32 CandidateVersion = 0;
	FString CandidateFingerprint;
	FString CandidateSchemaSnapshot;
	bool bPrepared = false;
};

/** Restricted editor for behavior-capable Cue Types and explicit inspection of quarantined legacy Cues. */
class FPaper2DPlusFrameCueTypeEditor : public FBlueprintEditor
{
public:
	static const FName CueTypeModeName;
	static const FName ToolbarOwnerName;
	static const FName StandardToolbarLayoutName;
	FPaper2DPlusFrameCueTypeEditor();
	virtual ~FPaper2DPlusFrameCueTypeEditor() override;

	void InitFrameCueTypeEditor(
		EToolkitMode::Type Mode,
		const TSharedPtr<IToolkitHost>& InitToolkitHost,
		UBlueprint* Blueprint,
		TSharedPtr<FPaper2DPlusFrameCuePendingPlacement> InPendingPlacement = nullptr);

	static bool IsSupportedBlueprint(const UBlueprint* Blueprint);
	static bool ShouldOfferLegacyBlueprintRecovery(const UBlueprint& Blueprint);
	static FText BuildLegacyContentWarningText(const UBlueprint& Blueprint);
	static bool ExecuteLegacyBlueprintRecovery(
		UBlueprint& Blueprint,
		TFunctionRef<bool(UBlueprint&)> OpenCommand);

	/**
	 * Writes the candidate durable metadata while retaining enough state to undo only that write if
	 * the caller-owned package save is cancelled or fails.
	 */
	static bool PrepareDurableSchemaSaveAttempt(
		UPaper2DPlusFrameCueBlueprint& Blueprint,
		FPaper2DPlusFrameCueDurableSaveAttempt& OutAttempt,
		FText* OutError = nullptr);
	/** Keeps candidate metadata only when the package is clean and the saved class is placement-ready. */
	static bool FinalizeDurableSchemaSaveAttempt(
		UPaper2DPlusFrameCueBlueprint& Blueprint,
		const FPaper2DPlusFrameCueDurableSaveAttempt& Attempt,
		FText* OutError = nullptr);
	static bool IsDurablyReadyForPlacement(const UBlueprint& Blueprint);

#if WITH_DEV_AUTOMATION_TESTS
	static void SetDestructiveCompileConfirmationForTests(
		TFunction<bool(const FPaper2DPlusFrameCueSchemaPreflight&)> InConfirmation);
	static void ClearDestructiveCompileConfirmationForTests();
	static void SetBaselineRecoveryCompileConfirmationForTests(
		TFunction<bool(const FPaper2DPlusFrameCueSchemaPreflight&)> InConfirmation);
	static void ClearBaselineRecoveryCompileConfirmationForTests();
	bool RestoreDurableSchemaAfterFailureForTests(
		UPaper2DPlusFrameCueBlueprint& Blueprint,
		FText* OutError = nullptr)
	{
		return RestoreDurableSchemaAfterFailure(Blueprint, OutError);
	}
	bool HasCompiledRecoveryStateForTests() const
	{
		return CompiledRecoveryState.IsValid();
	}
	bool PrepareCurrentBlueprintForPersistenceForTests(
		FPaper2DPlusFrameCueDurableSaveAttempt& OutAttempt,
		FText* OutError = nullptr)
	{
		return PrepareCurrentBlueprintForPersistence(OutAttempt, OutError);
	}
	void CompleteDurableSchemaCommitForTests()
	{
		CompleteDurableSchemaCommit();
	}
#endif

	/**
	 * Normalizes a brand-new specialized asset onto exactly one event graph and seeds the declared
	 * Cue behavior events for its kind, so a fresh Cue Type opens on an implementable graph.
	 * Returns false instead of deleting anything when unexpected authored graph content is present.
	 */
	static bool PrepareNewlyCreatedBlueprint(UBlueprint& Blueprint, FText* OutError = nullptr);

	/** The one event graph a Cue Type authors its behavior in, or null when none exists yet. */
	static UEdGraph* FindCueBehaviorGraph(const UBlueprint& Blueprint);

	/** Finds the override node for one declared Cue behavior event in the permitted event graph. */
	static UK2Node_Event* FindCueBehaviorEventNode(
		const UBlueprint& Blueprint,
		FName EventName);

	/** Adds the override node for one declared Cue behavior event. True only when one was added. */
	static bool AddCueBehaviorEventNode(UBlueprint& Blueprint, FName EventName);

	/** Implements the event if needed, then opens the behavior graph focused on its node. */
	bool ShowCueBehaviorEvent(FName EventName);

	/** Stock My Blueprint for healthy Cue Types; a local warning/recovery body for quarantine. */
	TSharedRef<SWidget> CreateMyBlueprintTabBody(
		const TWeakPtr<FPaper2DPlusFrameCueTypeEditor>& WeakEditor);

	/** True only when the asset is safe to expose through stock My Blueprint. */
	static bool ShouldUseStockMyBlueprint(const UBlueprint& Blueprint);

	/** True only for the one permitted Cue behavior event graph of the edited asset. */
	bool IsPermittedBehaviorDocument(const UObject* DocumentID) const;

	/**
	 * The remembered open-document entries this toolkit is allowed to restore: the one permitted
	 * behavior event graph, and nothing else.
	 *
	 * Opening the toolkit restores the documents the asset was last edited with, and that restore runs
	 * inside the Blueprint editor's own initialization rather than through this class, so an asset
	 * carrying a stale bookmark from a full Blueprint editor would otherwise reopen a surface the Cue
	 * Type envelope forbids. This filter is a VIEW handed to that restore for the duration of
	 * initialization only — opening a Cue Type never edits the asset, so the persisted recovery
	 * metadata of a legacy asset survives being inspected exactly as it was authored.
	 */
	static TArray<FEditedDocumentInfo> CollectPermittedEditedDocuments(const UBlueprint& Blueprint);

	/**
	 * Deliberately hides FBlueprintEditor::OpenDocument so the restricted editor can host the one
	 * permitted behavior event graph while still refusing every function, macro, delegate, and
	 * quarantined legacy graph the envelope rejects.
	 */
	TSharedPtr<SDockTab> OpenDocument(
		const UObject* DocumentID,
		FDocumentTracker::EOpenDocumentCause Cause);

	virtual void JumpToHyperlink(
		const UObject* ObjectReference,
		bool bRequestRename = false) override;

	static TArray<FName> GetAuthoringTabIdsForTests();
	static TArray<FName> GetCueOverrideEventNames(const UBlueprint& Blueprint);
	static TArray<FName> GetBlockedBlueprintCommandNamesForTests();
	static TArray<FName> GetBlockedFullBlueprintCommandNamesForTests();
	static TArray<FName> GetBlockedMyBlueprintCommandNamesForTests();
	static TArray<FName> GetBlockedGenericCommandNamesForTests();
	static bool ValidateDataOnlyBlueprint(
		const UBlueprint& Blueprint,
		FText* OutError = nullptr);
	static bool ValidateCompiledDataOnlyBlueprint(
		const UBlueprint& Blueprint,
		FText* OutError = nullptr);
	static bool ValidateCompiledSchemaParity(
		const UBlueprint& Blueprint,
		FText* OutError = nullptr);
	bool ShowPayloadVariableDetails(FName VariableName);
	bool CompileDataOnlyBlueprint();
	bool HasPendingPlacement() const;
	bool CanCreateAndPlacePendingCue() const;
	bool CreateAndPlacePendingCue();
	bool OpenLegacyBlueprintRecovery();
	bool EnforceDataOnlyAuthoringInvariantForTests();

#if WITH_DEV_AUTOMATION_TESTS
	bool IsNewDocumentVisibleForTests(ECreatedDocumentType GraphType) const
	{
		return NewDocument_IsVisibleForType(GraphType);
	}
	bool IsMyBlueprintSectionVisibleForTests(NodeSectionID::Type Section) const
	{
		return IsSectionVisible(Section);
	}
	bool IsUsingStockMyBlueprintForTests() const
	{
		return bUsingStockMyBlueprint;
	}
	TArray<FName> GetRegisteredAuthoringTabIdsForTests() const;
	void ReevaluateMyBlueprintExposureForTests()
	{
		UpdateMyBlueprintExposure();
	}
#endif

	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual bool IsInAScriptingMode() const override;
	virtual bool IsCompilingEnabled() const override;
	virtual void Compile() override;
	virtual bool CanSaveAsset() const override;
	virtual void SaveAsset_Execute() override;
	virtual bool CanSaveAssetAs() const override;
	virtual void SaveAssetAs_Execute() override;

protected:
	virtual void CreateDefaultTabContents(const TArray<UBlueprint*>& InBlueprints) override;
	virtual void CreateDefaultCommands() override;
	virtual void OnBlueprintChangedImpl(
		UBlueprint* InBlueprint,
		bool bIsJustBeingCompiled = false) override;
	virtual void RegisterApplicationModes(
		const TArray<UBlueprint*>& InBlueprints,
		bool bShouldOpenInDefaultsMode,
		bool bNewlyCreated = false) override;
	virtual bool NewDocument_IsVisibleForType(ECreatedDocumentType GraphType) const override;
	virtual bool IsSectionVisible(NodeSectionID::Type InSectionID) const override;

private:
	struct FAcceptedPayloadFieldState
	{
		uint64 PropertyFlags = 0;
		FName RepNotifyFunc = NAME_None;
		TEnumAsByte<ELifetimeCondition> ReplicationCondition = COND_None;
	};

	void CaptureAuthoringBaseline();
	void ScheduleDataOnlyInvariantEnforcement();
	bool EnforceDataOnlyAuthoringInvariant(bool bNotifyAuthor);
	void ReportDataOnlyViolation(const FText& Action, const FText& Error) const;
	bool ValidateCurrentBlueprint(FText* OutError = nullptr) const;
	bool PrepareCurrentBlueprintForPersistence(
		FPaper2DPlusFrameCueDurableSaveAttempt& OutAttempt,
		FText* OutError = nullptr);
	bool ConfirmSchemaPreflight(
		const FPaper2DPlusFrameCueSchemaPreflight& Preflight) const;
	/**
	 * Informed-consent escape for the untrusted-durable-baseline wedge: without it, the compile
	 * gate demanded a save the same broken baseline made impossible. Proceeding compiles with no
	 * placement-value recovery snapshot; the next successful save re-establishes the baseline.
	 */
	bool ConfirmBaselineUnavailableCompile(
		const FPaper2DPlusFrameCueSchemaPreflight& Preflight) const;
	bool CaptureCompileRecoveryState(
		UPaper2DPlusFrameCueBlueprint& Blueprint,
		const FPaper2DPlusFrameCueSchemaPreflight& Preflight,
		FText* OutError = nullptr);
	bool RestoreDurableSchemaAfterFailure(
		UPaper2DPlusFrameCueBlueprint& Blueprint,
		FText* OutError = nullptr);
	void CompleteDurableSchemaCommit();
	static void RestoreDurableSchemaSaveAttempt(
		UPaper2DPlusFrameCueBlueprint& Blueprint,
		const FPaper2DPlusFrameCueDurableSaveAttempt& Attempt);
	TSharedRef<SWidget> BuildCueOverrideMenu(
		const TWeakPtr<FPaper2DPlusFrameCueTypeEditor>& WeakEditor);
	TSharedRef<SWidget> CreateLegacyQuarantineBody(
		const TWeakPtr<FPaper2DPlusFrameCueTypeEditor>& WeakEditor);
	void AcceptPermittedBehaviorGraph();
	void UpdateMyBlueprintExposure();

	TSharedPtr<FPaper2DPlusFrameCuePendingPlacement> PendingPlacement;
	TSet<TWeakObjectPtr<UEdGraph>> AcceptedBehaviorGraphs;
	TSet<TWeakObjectPtr<UTimelineTemplate>> AcceptedTimelines;
	TMap<FGuid, FAcceptedPayloadFieldState> AcceptedPayloadFieldStates;
	TUniquePtr<FPaper2DPlusFrameCueCompiledRecoveryState> CompiledRecoveryState;
	bool bAuthoringBaselineCaptured = false;
	bool bDataOnlyEnforcementPending = false;
	bool bEnforcingDataOnlyInvariant = false;
	bool bUsingStockMyBlueprint = false;
};
