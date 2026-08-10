// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"

#include "FrameCues/Paper2DPlusFrameCue.h"
#include "Paper2DPlusModule.h"
#include "Containers/Ticker.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR && !(ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION <= 2)
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "Paper2DPlusFrameCueBlueprint"

#if WITH_EDITOR
namespace Paper2DPlusFrameCueBlueprintInternal
{
	FPaper2DPlusFrameCueCompileValidation CompileValidationDelegate;
	FPaper2DPlusFrameCueCompiledSchemaValidation CompiledSchemaValidationDelegate;
	FPaper2DPlusFrameCueDurableSaveValidation DurableSaveValidationDelegate;
	FPaper2DPlusFrameCueAutomaticSavePreparation AutomaticSavePreparationDelegate;
	FPaper2DPlusFrameCuePersistRefusalNotification PersistRefusalNotificationDelegate;

	void NotifyPersistRefused(
		const UPaper2DPlusFrameCueBlueprint& Blueprint,
		const FText& Reason)
	{
		if (PersistRefusalNotificationDelegate.IsBound())
		{
			PersistRefusalNotificationDelegate.Execute(Blueprint, Reason);
		}
	}
	struct FAutomaticDurableSaveRollback
	{
		uint64 AttemptGeneration = 0;
		FTSTicker::FDelegateHandle TickerHandle;
	};
	TMap<
		TWeakObjectPtr<UPaper2DPlusFrameCueBlueprint>,
		FAutomaticDurableSaveRollback> AutomaticDurableSaveRollbacks;

	void CancelAutomaticDurableSaveRollback(
		UPaper2DPlusFrameCueBlueprint& Blueprint)
	{
		const TWeakObjectPtr<UPaper2DPlusFrameCueBlueprint> WeakBlueprint(&Blueprint);
		if (const FAutomaticDurableSaveRollback* Rollback =
			AutomaticDurableSaveRollbacks.Find(WeakBlueprint))
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Rollback->TickerHandle);
		}
		AutomaticDurableSaveRollbacks.Remove(WeakBlueprint);
	}
#if WITH_DEV_AUTOMATION_TESTS
	bool bPersistentSaveGuardBypassForAutomation = false;
#endif

	void SetError(FText* OutError, const FText& Error)
	{
		if (OutError)
		{
			*OutError = Error;
		}
	}

}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UPaper2DPlusFrameCueBlueprint::PreSaveRoot(
	FObjectPreSaveRootContext ObjectSaveContext)
{
	Super::PreSaveRoot(ObjectSaveContext);
	AutomaticDurableSavePreparationError = FText::GetEmpty();

	const bool bBypassSaveGuard =
#if WITH_DEV_AUTOMATION_TESTS
		Paper2DPlusFrameCueBlueprintInternal::bPersistentSaveGuardBypassForAutomation;
#else
		false;
#endif
	const bool bOrdinaryEditorSave = !bBypassSaveGuard
		&& !ObjectSaveContext.IsCooking()
		&& !ObjectSaveContext.IsProceduralSave()
		&& !HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject);
	if (!bOrdinaryEditorSave)
	{
		return;
	}

	// A failed synchronous save leaves its transaction pending until the next core tick so the
	// success-only package-saved event has the final say. A retry may begin before that tick; restore
	// the prior baseline first so the retry snapshots durable disk state, not the failed candidate.
	if (bAutomaticDurableSaveAttemptActive)
	{
		RestoreAutomaticDurableSaveSnapshot();
	}

	// The restricted editor's own Save command has already staged an exact candidate. Save All,
	// Content Browser Save, and editor shutdown arrive here without that private toolkit call.
	FText ValidationError;
	const bool bCompiledBlueprintIsCurrent =
		Status == BS_UpToDate || Status == BS_UpToDateWithWarnings;
	if (bCompiledBlueprintIsCurrent
		&& Paper2DPlusFrameCueBlueprintInternal::DurableSaveValidationDelegate.IsBound()
		&& Paper2DPlusFrameCueBlueprintInternal::DurableSaveValidationDelegate.Execute(
			*this,
			&ValidationError))
	{
		return;
	}

	// Snapshot before the editor bridge compiles: compilation can reinstance generated classes and
	// dirty loaded placement owners, while these five fields remain the one durable-baseline commit.
	++AutomaticDurableSaveAttemptGeneration;
	bAutomaticDurableSaveAttemptActive = true;
	if (UPackage* Package = GetOutermost())
	{
		bAutomaticDurableSavePackageWasDirty = Package->IsDirty();
	}
	AutomaticDurableSavePreviousVersion = DurableSchemaVersion;
	AutomaticDurableSavePreviousFingerprint = DurableSchemaFingerprint;
	AutomaticDurableSavePreviousSchemaSnapshot = DurableSchemaSnapshot;
	AutomaticDurableSavePreviousVariables = DurableVariables;
	AutomaticDurableSavePreviousDefaultValues = DurableDefaultValues;

	FText PreparationError;
	const bool bPrepared =
		Paper2DPlusFrameCueBlueprintInternal::AutomaticSavePreparationDelegate.IsBound()
		&& Paper2DPlusFrameCueBlueprintInternal::AutomaticSavePreparationDelegate.Execute(
			*this,
			&PreparationError);
	if (!bPrepared)
	{
		RestoreAutomaticDurableSaveSnapshot();
		AutomaticDurableSavePreparationError = PreparationError.IsEmpty()
			? LOCTEXT(
				"AutomaticSavePreparationUnavailable",
				"The Paper2D+ editor could not prepare this Cue Type for an ordinary package save.")
			: PreparationError;
	}
}

void UPaper2DPlusFrameCueBlueprint::PostSaveRoot(
	FObjectPostSaveRootContext ObjectSaveContext)
{
	Super::PostSaveRoot(ObjectSaveContext);
	if (bAutomaticDurableSaveAttemptActive)
	{
		if (!ObjectSaveContext.SaveSucceeded())
		{
			RestoreAutomaticDurableSaveSnapshot();
		}
		else
		{
			// UE 5.0-5.8 leave this context's success bit at its default even when the later package
			// write fails. Keep the snapshot alive: PackageSavedWithContextEvent commits a real
			// success before this one-shot fallback rolls back every other outcome.
			QueueAutomaticDurableSaveFailureRollback();
		}
	}
	AutomaticDurableSavePreparationError = FText::GetEmpty();
}

void UPaper2DPlusFrameCueBlueprint::CommitAutomaticDurableSaveAfterPackageSaved()
{
	if (!bAutomaticDurableSaveAttemptActive)
	{
		return;
	}

	// The success-only package event is the durable authority. Transient retry state no longer
	// represents unsaved work; the written class/defaults are now the placement baseline.
	PendingDefaultProposalValues.Reset();
	bHasPendingDefaultProposal = false;
	bPendingDefaultProposalNeedsCompile = false;
	ConfirmedDestructiveSchemaFingerprint.Reset();
	ClearAutomaticDurableSaveTransaction();
}

void UPaper2DPlusFrameCueBlueprint::RestoreAutomaticDurableSaveSnapshot()
{
	if (!bAutomaticDurableSaveAttemptActive)
	{
		return;
	}

	DurableSchemaVersion = AutomaticDurableSavePreviousVersion;
	DurableSchemaFingerprint = AutomaticDurableSavePreviousFingerprint;
	DurableSchemaSnapshot = AutomaticDurableSavePreviousSchemaSnapshot;
	DurableVariables = AutomaticDurableSavePreviousVariables;
	DurableDefaultValues = AutomaticDurableSavePreviousDefaultValues;
	if (UPackage* Package = GetOutermost())
	{
		Package->SetDirtyFlag(bAutomaticDurableSavePackageWasDirty);
	}
	ClearAutomaticDurableSaveTransaction();
}

void UPaper2DPlusFrameCueBlueprint::ClearAutomaticDurableSaveTransaction()
{
	Paper2DPlusFrameCueBlueprintInternal::CancelAutomaticDurableSaveRollback(*this);
	bAutomaticDurableSaveAttemptActive = false;
	bAutomaticDurableSavePackageWasDirty = false;
	AutomaticDurableSavePreviousVersion = 0;
	AutomaticDurableSavePreviousFingerprint.Reset();
	AutomaticDurableSavePreviousSchemaSnapshot.Reset();
	AutomaticDurableSavePreviousVariables.Reset();
	AutomaticDurableSavePreviousDefaultValues.Reset();
}

void UPaper2DPlusFrameCueBlueprint::QueueAutomaticDurableSaveFailureRollback()
{
	using namespace Paper2DPlusFrameCueBlueprintInternal;
	CancelAutomaticDurableSaveRollback(*this);
	const uint64 AttemptGeneration = AutomaticDurableSaveAttemptGeneration;
	const TWeakObjectPtr<UPaper2DPlusFrameCueBlueprint> WeakBlueprint(this);
	const FTSTicker::FDelegateHandle TickerHandle =
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda(
				[WeakBlueprint, AttemptGeneration](float)
				{
					if (const FAutomaticDurableSaveRollback* Rollback =
						AutomaticDurableSaveRollbacks.Find(WeakBlueprint);
						Rollback && Rollback->AttemptGeneration == AttemptGeneration)
					{
						// This callback is already executing and returns false below, so remove only
						// the registry entry before rollback clears the transaction.
						AutomaticDurableSaveRollbacks.Remove(WeakBlueprint);
					}
					if (UPaper2DPlusFrameCueBlueprint* Blueprint = WeakBlueprint.Get())
					{
						Blueprint->RollbackAutomaticDurableSaveIfPending(AttemptGeneration);
					}
					return false;
				}));
	FAutomaticDurableSaveRollback Rollback;
	Rollback.AttemptGeneration = AttemptGeneration;
	Rollback.TickerHandle = TickerHandle;
	AutomaticDurableSaveRollbacks.Add(WeakBlueprint, MoveTemp(Rollback));
}

void UPaper2DPlusFrameCueBlueprint::RollbackAutomaticDurableSaveIfPending(
	const uint64 AttemptGeneration)
{
	if (bAutomaticDurableSaveAttemptActive
		&& AutomaticDurableSaveAttemptGeneration == AttemptGeneration)
	{
		RestoreAutomaticDurableSaveSnapshot();
	}
}

void UPaper2DPlusFrameCueBlueprint::ResolveAllAutomaticDurableSaveTransactionsForShutdown()
{
	using namespace Paper2DPlusFrameCueBlueprintInternal;
	TArray<TWeakObjectPtr<UPaper2DPlusFrameCueBlueprint>> PendingBlueprints;
	PendingBlueprints.Reserve(AutomaticDurableSaveRollbacks.Num());
	for (const TPair<
		TWeakObjectPtr<UPaper2DPlusFrameCueBlueprint>,
		FAutomaticDurableSaveRollback>& Pair : AutomaticDurableSaveRollbacks)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Pair.Value.TickerHandle);
		PendingBlueprints.Add(Pair.Key);
	}
	AutomaticDurableSaveRollbacks.Reset();

	for (const TWeakObjectPtr<UPaper2DPlusFrameCueBlueprint>& WeakBlueprint :
		PendingBlueprints)
	{
		if (UPaper2DPlusFrameCueBlueprint* Blueprint = WeakBlueprint.Get())
		{
			Blueprint->RestoreAutomaticDurableSaveSnapshot();
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS
int32 UPaper2DPlusFrameCueBlueprint::GetAutomaticDurableSaveRollbackCountForTests()
{
	return Paper2DPlusFrameCueBlueprintInternal::AutomaticDurableSaveRollbacks.Num();
}
#endif
#endif

void UPaper2DPlusFrameCueBlueprint::Serialize(FArchive& Ar)
{
#if WITH_EDITOR
	const bool bBypassSaveGuard =
#if WITH_DEV_AUTOMATION_TESTS
		Paper2DPlusFrameCueBlueprintInternal::bPersistentSaveGuardBypassForAutomation;
#else
		false;
#endif
	const bool bGuardedPersistentSave = !bBypassSaveGuard
		&& Ar.IsSaving()
		&& Ar.IsPersistent()
		&& !Ar.IsCooking()
		&& !Ar.IsObjectReferenceCollector()
		&& !(Ar.GetPortFlags() & PPF_Duplicate)
		&& !HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject);
	if (bGuardedPersistentSave)
	{
		FText MetadataError;
		if (!Paper2DPlusFrameCueBlueprintInternal::DurableSaveValidationDelegate.IsBound()
			|| !Paper2DPlusFrameCueBlueprintInternal::DurableSaveValidationDelegate.Execute(
				*this,
				&MetadataError))
		{
			if (!AutomaticDurableSavePreparationError.IsEmpty())
			{
				MetadataError = AutomaticDurableSavePreparationError;
			}
			if (MetadataError.IsEmpty())
			{
				MetadataError = LOCTEXT(
					"MissingDurableSaveValidationDelegate",
					"The Paper2D+ editor schema service is unavailable, so the staged durable Cue Type baseline cannot be validated.");
			}
			UE_LOG(
				LogPaper2DPlus,
				Error,
				TEXT("Cannot persist Frame Cue Type asset '%s': %s"),
				*GetPathName(),
				*MetadataError.ToString());
			Paper2DPlusFrameCueBlueprintInternal::NotifyPersistRefused(*this, MetadataError);
			Ar.SetError();
		}
	}
#endif

	// Durable metadata must be prepared before Super serializes reflected properties.
	Super::Serialize(Ar);

#if WITH_EDITOR
	if (bGuardedPersistentSave && !Ar.IsError())
	{
		FText ContractError;
		if (!ValidateCompiledDataOnlyContract(*this, &ContractError))
		{
			UE_LOG(
				LogPaper2DPlus,
				Error,
				TEXT("Cannot save invalid Frame Cue Type asset '%s': %s"),
				*GetPathName(),
				*ContractError.ToString());
			Paper2DPlusFrameCueBlueprintInternal::NotifyPersistRefused(*this, ContractError);
			Ar.SetError();
		}
	}
#endif
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
void UPaper2DPlusFrameCueBlueprint::SetPersistentSaveGuardBypassForAutomation(
	bool bBypass)
{
	Paper2DPlusFrameCueBlueprintInternal::bPersistentSaveGuardBypassForAutomation = bBypass;
}
#endif

#if WITH_EDITOR
void UPaper2DPlusFrameCueBlueprint::SetCompileValidationDelegate(
	FPaper2DPlusFrameCueCompileValidation InDelegate)
{
	Paper2DPlusFrameCueBlueprintInternal::CompileValidationDelegate =
		MoveTemp(InDelegate);
}

void UPaper2DPlusFrameCueBlueprint::ClearCompileValidationDelegate()
{
	Paper2DPlusFrameCueBlueprintInternal::CompileValidationDelegate.Unbind();
}

bool UPaper2DPlusFrameCueBlueprint::IsCompileValidationDelegateBound()
{
	return Paper2DPlusFrameCueBlueprintInternal::CompileValidationDelegate.IsBound();
}

void UPaper2DPlusFrameCueBlueprint::SetCompiledSchemaValidationDelegate(
	FPaper2DPlusFrameCueCompiledSchemaValidation InDelegate)
{
	Paper2DPlusFrameCueBlueprintInternal::CompiledSchemaValidationDelegate =
		MoveTemp(InDelegate);
}

void UPaper2DPlusFrameCueBlueprint::ClearCompiledSchemaValidationDelegate()
{
	Paper2DPlusFrameCueBlueprintInternal::CompiledSchemaValidationDelegate.Unbind();
}

bool UPaper2DPlusFrameCueBlueprint::IsCompiledSchemaValidationDelegateBound()
{
	return Paper2DPlusFrameCueBlueprintInternal::CompiledSchemaValidationDelegate.IsBound();
}

void UPaper2DPlusFrameCueBlueprint::SetDurableSaveValidationDelegate(
	FPaper2DPlusFrameCueDurableSaveValidation InDelegate)
{
	Paper2DPlusFrameCueBlueprintInternal::DurableSaveValidationDelegate = MoveTemp(InDelegate);
}

void UPaper2DPlusFrameCueBlueprint::ClearDurableSaveValidationDelegate()
{
	Paper2DPlusFrameCueBlueprintInternal::DurableSaveValidationDelegate.Unbind();
}

bool UPaper2DPlusFrameCueBlueprint::IsDurableSaveValidationDelegateBound()
{
	return Paper2DPlusFrameCueBlueprintInternal::DurableSaveValidationDelegate.IsBound();
}

void UPaper2DPlusFrameCueBlueprint::SetAutomaticSavePreparationDelegate(
	FPaper2DPlusFrameCueAutomaticSavePreparation InDelegate)
{
	Paper2DPlusFrameCueBlueprintInternal::AutomaticSavePreparationDelegate =
		MoveTemp(InDelegate);
}

void UPaper2DPlusFrameCueBlueprint::ClearAutomaticSavePreparationDelegate()
{
	Paper2DPlusFrameCueBlueprintInternal::AutomaticSavePreparationDelegate.Unbind();
}

bool UPaper2DPlusFrameCueBlueprint::IsAutomaticSavePreparationDelegateBound()
{
	return Paper2DPlusFrameCueBlueprintInternal::AutomaticSavePreparationDelegate.IsBound();
}

void UPaper2DPlusFrameCueBlueprint::SetPersistRefusalNotificationDelegate(
	FPaper2DPlusFrameCuePersistRefusalNotification InDelegate)
{
	Paper2DPlusFrameCueBlueprintInternal::PersistRefusalNotificationDelegate =
		MoveTemp(InDelegate);
}

void UPaper2DPlusFrameCueBlueprint::ClearPersistRefusalNotificationDelegate()
{
	Paper2DPlusFrameCueBlueprintInternal::PersistRefusalNotificationDelegate.Unbind();
}

void UPaper2DPlusFrameCueBlueprint::ExecuteCompileValidation(
	UPaper2DPlusFrameCueBlueprint& Blueprint,
	FKismetCompilerContext* CompilerContext)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (Paper2DPlusFrameCueBlueprintInternal::bPersistentSaveGuardBypassForAutomation)
	{
		return;
	}
#endif
	Paper2DPlusFrameCueBlueprintInternal::CompileValidationDelegate.ExecuteIfBound(
		Blueprint,
		CompilerContext);
}

uint64 UPaper2DPlusFrameCueBlueprint::GetForbiddenPayloadPropertyFlags()
{
	// Every allowed Cue payload field must survive package save/load, asset duplication,
	// copy/paste, undo/redo, and editor stripping with the same authored value. These flags are
	// stable across the supported UE 5.0-5.8 range and each violates at least one of those gates.
	return CPF_Transient
		| CPF_Config
		| CPF_GlobalConfig
		| CPF_DuplicateTransient
		| CPF_Deprecated
		| CPF_NonTransactional
		| CPF_EditorOnly
		| CPF_TextExportTransient
		| CPF_NonPIEDuplicateTransient
		| CPF_SkipSerialization;
}

#if !UE_VERSION_OLDER_THAN(5, 1, 0)
bool UPaper2DPlusFrameCueBlueprint::AllowFunctionOverride(
	const UFunction* const InFunction) const
{
	if (!InFunction || !Super::AllowFunctionOverride(InFunction))
	{
		return false;
	}

	// Even declared Cue events stay out of stock SMyBlueprint. Its private implementation path
	// creates a function graph when no event graph exists. The restricted editor's Override control
	// calls AddDefaultEventNode instead, which preserves the permitted graph and parent-call wiring.
	return false;
}
#endif

bool UPaper2DPlusFrameCueBlueprint::IsPermittedGeneratedCueFunction(
	const UBlueprint& Blueprint,
	const UFunction* Function)
{
	if (!Function)
	{
		return false;
	}
	const UBlueprintGeneratedClass* GeneratedClass =
		Cast<UBlueprintGeneratedClass>(Blueprint.GeneratedClass.Get());
	if (GeneratedClass && Function == GeneratedClass->UberGraphFunction)
	{
		return true;
	}
	if (!Paper2DPlusFrameCueBehavior::IsDeclaredEventName(Function->GetFName()))
	{
		return false;
	}
	// An override is only permitted when the parent chain really declares that behavior event, so a
	// custom event cannot borrow a declared name to smuggle an arbitrary signature onto the class.
	const UFunction* SuperFunction = Blueprint.ParentClass
		? Blueprint.ParentClass->FindFunctionByName(Function->GetFName())
		: nullptr;
	while (SuperFunction && SuperFunction->GetSuperFunction())
	{
		SuperFunction = SuperFunction->GetSuperFunction();
	}
	const UClass* DeclaringClass = SuperFunction ? SuperFunction->GetOwnerClass() : nullptr;
	return DeclaringClass
		&& DeclaringClass->HasAnyClassFlags(CLASS_Native)
		&& DeclaringClass->IsChildOf(UPaper2DPlusCueBase::StaticClass())
		&& SuperFunction->HasAnyFunctionFlags(FUNC_BlueprintEvent);
}

bool UPaper2DPlusFrameCueBlueprint::IsGeneratedEventGraphFrameProperty(
	const UClass* GeneratedClass,
	const FProperty* Property)
{
	if (!Property)
	{
		return false;
	}
	const UBlueprintGeneratedClass* BlueprintClass =
		Cast<UBlueprintGeneratedClass>(GeneratedClass);
	if (!BlueprintClass)
	{
		return false;
	}
	if (BlueprintClass->UberGraphFramePointerProperty
		&& Property == BlueprintClass->UberGraphFramePointerProperty)
	{
		return true;
	}
	// The cached pointer is only linked on engine configurations that use the persistent frame, so
	// fall back to the reserved name — but only on a class that actually owns an event graph, so the
	// exemption can never launder an authored payload field named after the frame.
	return BlueprintClass->UberGraphFunction != nullptr
		&& Property->GetFName() == UBlueprintGeneratedClass::GetUberGraphFrameName();
}

bool UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(
	const UBlueprint& Blueprint,
	FText* OutError)
{
	using namespace Paper2DPlusFrameCueBlueprintInternal;
	const UPaper2DPlusFrameCueBlueprint* CueBlueprint =
		Cast<UPaper2DPlusFrameCueBlueprint>(&Blueprint);
	if (!CueBlueprint)
	{
		SetError(OutError, LOCTEXT(
			"InvalidCueBlueprintEnvelope",
			"Frame Cue Types must use the Paper2D+ restricted behavior-capable Blueprint envelope."));
		return false;
	}
	if (Blueprint.BlueprintType != BPTYPE_Normal || Blueprint.bGenerateAbstractClass)
	{
		SetError(OutError, LOCTEXT(
			"InvalidCueBlueprintKind",
			"Frame Cue Types must be normal, instantiable Blueprint classes."));
		return false;
	}

	if (!Blueprint.ParentClass
		|| (!Blueprint.ParentClass->IsChildOf(UPaper2DPlusCue::StaticClass())
			&& !Blueprint.ParentClass->IsChildOf(UPaper2DPlusCueState::StaticClass())))
	{
		SetError(OutError, LOCTEXT(
			"InvalidCueParent",
			"Frame Cue Types must derive from the Paper2D+ Cue or Cue State base."));
		return false;
	}

	// Only an actual native Cue class may bypass specialized-parent validation. Treat every
	// non-native parent as authored/generated state even if its CompiledFromBlueprint flag was
	// stale or maliciously cleared; that flag is part of what the recursive envelope validates.
	if (!Blueprint.ParentClass->HasAnyClassFlags(CLASS_Native))
	{
		const UPaper2DPlusFrameCueBlueprint* ParentCueBlueprint =
			Cast<UPaper2DPlusFrameCueBlueprint>(Blueprint.ParentClass->ClassGeneratedBy);
		if (!ParentCueBlueprint
			|| !Blueprint.ParentClass->GetClass()->IsChildOf(
				UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass()))
		{
			SetError(OutError, LOCTEXT(
				"BehaviorBlueprintParent",
				"Frame Cue Types may inherit only from native Cue bases or another "
				"Paper2D+ Cue Type. The selected Blueprint parent is outside the restricted Cue "
				"Type contract."));
			return false;
		}

		FText ParentError;
		if (!ValidateDataOnlyContract(*ParentCueBlueprint, &ParentError))
		{
			SetError(OutError, FText::Format(
				LOCTEXT(
					"InvalidCueTypeParent",
					"Parent Cue Type '{0}' violates the restricted Cue Type contract: {1}"),
				FText::FromString(ParentCueBlueprint->GetPathName()),
				ParentError));
			return false;
		}
	}

	int32 CompilerExtensionCount = 0;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION <= 2
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const TArray<TObjectPtr<UBlueprintExtension>>& BlueprintExtensions =
		CueBlueprint->Extensions;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#else
	const TArrayView<const TObjectPtr<UBlueprintExtension>> BlueprintExtensions =
		CueBlueprint->GetExtensions();
#endif
	for (const TObjectPtr<UBlueprintExtension>& Extension : BlueprintExtensions)
	{
		if (!Extension
			|| !Extension->IsA<UPaper2DPlusFrameCueBlueprintCompilerExtension>())
		{
			SetError(OutError, LOCTEXT(
				"UnsupportedBlueprintExtension",
				"Frame Cue Types cannot contain Blueprint compiler extensions other than the "
				"Paper2D+ Cue Type guard."));
			return false;
		}
		++CompilerExtensionCount;
	}
	if (CompilerExtensionCount != 1)
	{
		SetError(OutError, LOCTEXT(
			"MissingCompilerGuard",
			"The Frame Cue Type is missing its Paper2D+ Cue Type compiler guard."));
		return false;
	}

	// Cue behavior is authored as overrides of the declared Cue events, which live in the one
	// permitted event graph. Every other graph family stays rejected verbatim: functions, macros,
	// delegate signatures, and interface graphs can all carry behavior the envelope cannot bound.
	if (Blueprint.FunctionGraphs.Num() > 0
		|| Blueprint.MacroGraphs.Num() > 0
		|| Blueprint.DelegateSignatureGraphs.Num() > 0)
	{
		SetError(OutError, FText::Format(
			LOCTEXT(
				"BehaviorGraphsPresent",
				"Frame Cue Types may only implement their declared Cue events, but this asset "
				"contains {0} function, macro, or delegate graph(s). Use the unsupported legacy "
				"Blueprint recovery action to inspect or remove them."),
			Blueprint.FunctionGraphs.Num()
				+ Blueprint.MacroGraphs.Num()
				+ Blueprint.DelegateSignatureGraphs.Num()));
		return false;
	}
	if (Blueprint.UbergraphPages.Num() > 1)
	{
		SetError(OutError, LOCTEXT(
			"MultipleEventGraphsPresent",
			"Frame Cue Types author their behavior in one event graph; this asset contains more "
			"than one."));
		return false;
	}
	for (const TObjectPtr<UEdGraph>& EventGraph : Blueprint.UbergraphPages)
	{
		if (!EventGraph)
		{
			SetError(OutError, LOCTEXT(
				"NullEventGraphPresent",
				"The Frame Cue Type has a missing event graph page."));
			return false;
		}
	}

	if (Blueprint.SimpleConstructionScript
		|| Blueprint.ComponentTemplates.Num() > 0
		|| Blueprint.ComponentClassOverrides.Num() > 0
		|| Blueprint.InheritableComponentHandler)
	{
		SetError(OutError, LOCTEXT(
			"ComponentsPresent",
			"Frame Cue Types cannot contain components or a construction script."));
		return false;
	}

	if (Blueprint.Timelines.Num() > 0)
	{
		SetError(OutError, LOCTEXT(
			"TimelinesPresent",
			"Frame Cue Types cannot contain timelines."));
		return false;
	}

	if (Blueprint.ImplementedInterfaces.Num() > 0)
	{
		SetError(OutError, LOCTEXT(
			"InterfacesPresent",
			"Frame Cue Types cannot implement Blueprint interfaces."));
		return false;
	}

	static const FName DelegateCategory(TEXT("delegate"));
	static const FName MulticastDelegateCategory(TEXT("mcdelegate"));
	static const FName BlueprintGetterMetadata(TEXT("BlueprintGetter"));
	static const FName BlueprintSetterMetadata(TEXT("BlueprintSetter"));
	for (const FBPVariableDescription& Variable : Blueprint.NewVariables)
	{
		if (Variable.VarType.PinCategory == DelegateCategory
			|| Variable.VarType.PinCategory == MulticastDelegateCategory)
		{
			SetError(OutError, FText::Format(
				LOCTEXT(
					"DelegateFieldPresent",
					"Field '{0}' is a delegate. Frame Cue payload fields must contain data only."),
				FText::FromName(Variable.VarName)));
			return false;
		}
		if (Variable.HasMetaData(BlueprintGetterMetadata)
			|| Variable.HasMetaData(BlueprintSetterMetadata))
		{
			SetError(OutError, FText::Format(
				LOCTEXT(
					"BehaviorAccessorFieldPresent",
					"Field '{0}' declares a Blueprint getter or setter. Cue payload access must "
					"remain direct data and cannot call behavior."),
				FText::FromName(Variable.VarName)));
			return false;
		}

		if ((Variable.PropertyFlags & GetForbiddenPayloadPropertyFlags()) != 0)
		{
			SetError(OutError, FText::Format(
				LOCTEXT(
					"NonPersistentFieldPresent",
					"Field '{0}' is transient, config-driven, editor-only, deprecated, or otherwise "
					"excluded from reliable save, duplication, copy, or undo. Cue payload fields "
					"must preserve their authored value everywhere."),
				FText::FromName(Variable.VarName)));
			return false;
		}

		if ((Variable.PropertyFlags & (CPF_Net | CPF_RepNotify)) != 0
			|| !Variable.RepNotifyFunc.IsNone()
			|| Variable.ReplicationCondition != COND_None)
		{
			SetError(OutError, FText::Format(
				LOCTEXT(
					"ReplicatedFieldPresent",
					"Field '{0}' uses replication or RepNotify. Cue payload fields are "
					"serialized data and cannot own network callbacks."),
				FText::FromName(Variable.VarName)));
			return false;
		}
	}

	return true;
}

bool UPaper2DPlusFrameCueBlueprint::ValidateGeneratedClassEnvelope(
	const UBlueprint& Blueprint,
	FText* OutError,
	const UClass* ExpectedGeneratedClass)
{
	using namespace Paper2DPlusFrameCueBlueprintInternal;

	const UClass* GeneratedClass = Blueprint.GeneratedClass;
	if (!GeneratedClass)
	{
		SetError(OutError, LOCTEXT(
			"MissingGeneratedCueClass",
			"The Frame Cue Type has no generated class. Recreate or explicitly recover the asset; "
			"an established Cue Type is never rebuilt implicitly."));
		return false;
	}
	if (!GeneratedClass->GetClass()->IsChildOf(
		UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass()))
	{
		SetError(OutError, LOCTEXT(
			"PlainGeneratedCueClass",
			"The Frame Cue Type does not use the Paper2D+ cook-validating generated-class envelope."));
		return false;
	}
	if (ExpectedGeneratedClass && GeneratedClass != ExpectedGeneratedClass)
	{
		SetError(OutError, LOCTEXT(
			"UnexpectedGeneratedCueClass",
			"The Frame Cue Type points at a different generated class than the class being validated."));
		return false;
	}
	if (GeneratedClass->ClassGeneratedBy != &Blueprint)
	{
		SetError(OutError, LOCTEXT(
			"WrongGeneratedCueClassOwner",
			"The generated Cue class is not owned by this Frame Cue Type asset."));
		return false;
	}
	if (GeneratedClass->GetSuperClass() != Blueprint.ParentClass)
	{
		SetError(OutError, LOCTEXT(
			"WrongGeneratedCueSuperClass",
			"The generated Cue class does not derive directly from the asset's selected Cue parent."));
		return false;
	}
	FName CanonicalGeneratedClassName;
	FName CanonicalSkeletonClassName;
	Blueprint.GetBlueprintClassNames(
		CanonicalGeneratedClassName,
		CanonicalSkeletonClassName);
	if (GeneratedClass->GetOuter() != Blueprint.GetOutermost()
		|| GeneratedClass->GetFName() != CanonicalGeneratedClassName
		|| !GeneratedClass->HasAnyFlags(RF_Public)
		|| GeneratedClass->HasAnyFlags(RF_Transient))
	{
		SetError(OutError, LOCTEXT(
			"NonCanonicalGeneratedCueClass",
			"The generated Cue class is not the canonical public export in its Frame Cue Type package."));
		return false;
	}
	if (!GeneratedClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
		|| GeneratedClass->HasAnyClassFlags(
			CLASS_NewerVersionExists | CLASS_Abstract | CLASS_Transient)
		|| GeneratedClass == Blueprint.SkeletonGeneratedClass
		|| GeneratedClass->GetName().StartsWith(TEXT("REINST_"))
		|| GeneratedClass->GetName().StartsWith(TEXT("SKEL_"))
		|| GeneratedClass->GetName().StartsWith(TEXT("TRASHCLASS_")))
	{
		SetError(OutError, LOCTEXT(
			"NonAuthoritativeGeneratedCueClass",
			"The Frame Cue Type points at a stale, skeleton, reinstancing, or uncompiled class."));
		return false;
	}

	return true;
}

bool UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
	const UBlueprint& Blueprint,
	FText* OutError,
	const UClass* ExpectedGeneratedClass)
{
	using namespace Paper2DPlusFrameCueBlueprintInternal;

	if (!ValidateDataOnlyContract(Blueprint, OutError)
		|| !ValidateGeneratedClassEnvelope(Blueprint, OutError, ExpectedGeneratedClass))
	{
		return false;
	}
	if (Blueprint.Status != BS_UpToDate
		&& Blueprint.Status != BS_UpToDateWithWarnings)
	{
		SetError(OutError, LOCTEXT(
			"CueBlueprintNotFullyCompiled",
			"The Frame Cue Type must finish a clean full compile before it can be saved or cooked."));
		return false;
	}

	const UClass* GeneratedClass = Blueprint.GeneratedClass;
	const UPaper2DPlusFrameCueBlueprintGeneratedClass* CueGeneratedClass =
		CastChecked<UPaper2DPlusFrameCueBlueprintGeneratedClass>(GeneratedClass);
	const UObject* GeneratedDefaults = GeneratedClass->GetDefaultObject(false);
	if (!GeneratedDefaults
		|| GeneratedDefaults->GetClass() != GeneratedClass
		|| GeneratedDefaults->GetOuter() != GeneratedClass->GetOuter()
		|| GeneratedDefaults->GetFName() != GeneratedClass->GetDefaultObjectName()
		|| !GeneratedDefaults->HasAllFlags(
			RF_ClassDefaultObject | RF_ArchetypeObject)
		|| GeneratedDefaults->HasAnyFlags(RF_Transient))
	{
		SetError(OutError, LOCTEXT(
			"InvalidGeneratedCueDefaults",
			"The Frame Cue Type does not have a valid default object for its current generated class."));
		return false;
	}

	// BlueprintGraph stays editor-only. Its one dependency-free bridge validates both the authored
	// synchronous graph policy and exact source/generated field parity before generated artifacts are
	// inspected, so latent/async/timer source cannot be hidden by the functions or bindings it
	// produced. Save and cook fail closed if the editor boundary is unavailable.
	if (!CompiledSchemaValidationDelegate.IsBound())
	{
		SetError(OutError, LOCTEXT(
			"MissingCompiledSchemaValidation",
			"The Paper2D+ editor schema validator is unavailable; the Cue Type cannot be saved or cooked safely."));
		return false;
	}
	FText SchemaError;
	if (!CompiledSchemaValidationDelegate.Execute(Blueprint, &SchemaError))
	{
		SetError(
			OutError,
			SchemaError.IsEmpty()
				? LOCTEXT(
					"CompiledSchemaMismatch",
					"The generated Cue payload types do not match the authored schema.")
				: SchemaError);
		return false;
	}

	if (!Blueprint.ParentClass->HasAnyClassFlags(CLASS_Native))
	{
		const UPaper2DPlusFrameCueBlueprint* ParentCueBlueprint =
			Cast<UPaper2DPlusFrameCueBlueprint>(Blueprint.ParentClass->ClassGeneratedBy);
		FText ParentError;
		if (!ParentCueBlueprint
			|| !ValidateCompiledDataOnlyContract(
				*ParentCueBlueprint,
				&ParentError,
				Blueprint.ParentClass))
		{
			SetError(OutError, FText::Format(
				LOCTEXT(
					"InvalidCompiledCueTypeParent",
					"Parent Cue Type '{0}' is not ready to persist: {1}"),
				FText::FromString(Blueprint.ParentClass->GetPathName()),
				ParentError.IsEmpty()
					? LOCTEXT("MissingCompiledCueTypeParent", "its specialized asset is unavailable")
					: ParentError));
			return false;
		}
	}

	// The event-graph function and its frame pointer are the compiler's own representation of the
	// permitted behavior graph, so they are allowlisted here and exempted from the authored-payload
	// checks below. Every other generated behavior artifact stays rejected verbatim.
	const bool bHasGeneratedBehaviorState =
		CueGeneratedClass->DynamicBindingObjects.Num() > 0
		|| CueGeneratedClass->ComponentTemplates.Num() > 0
		|| CueGeneratedClass->Timelines.Num() > 0
		|| CueGeneratedClass->ComponentClassOverrides.Num() > 0
		|| CueGeneratedClass->SimpleConstructionScript
		|| CueGeneratedClass->InheritableComponentHandler
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
		|| CueGeneratedClass->FieldNotifies.Num() > 0
#endif
		;
	if (bHasGeneratedBehaviorState)
	{
		SetError(OutError, LOCTEXT(
			"GeneratedCueBehaviorStatePresent",
			"The generated Cue class contains generated runtime behavior such as dynamic "
			"bindings, components, timelines, construction data, or field notifications."));
		return false;
	}

	for (TFieldIterator<UFunction> FunctionIt(
		GeneratedClass,
		EFieldIteratorFlags::ExcludeSuper);
		FunctionIt;
		++FunctionIt)
	{
		if (IsPermittedGeneratedCueFunction(Blueprint, *FunctionIt))
		{
			continue;
		}
		SetError(OutError, FText::Format(
			LOCTEXT(
				"GeneratedCueFunctionPresent",
				"The generated Cue class owns executable function '{0}', which is not one of the "
				"declared Cue behavior events. Recompile after removing unsupported behavior."),
			FText::FromName(FunctionIt->GetFName())));
		return false;
	}

	int32 GeneratedPropertyCount = 0;
	for (TFieldIterator<FProperty> PropertyIt(
		GeneratedClass,
		EFieldIteratorFlags::ExcludeSuper);
		PropertyIt;
		++PropertyIt)
	{
		const FProperty* Property = *PropertyIt;
		if (IsGeneratedEventGraphFrameProperty(GeneratedClass, Property))
		{
			continue;
		}
		++GeneratedPropertyCount;
		const FBPVariableDescription* SourceVariable = Blueprint.NewVariables.FindByPredicate(
			[Property](const FBPVariableDescription& Variable)
			{
				return Variable.VarName == Property->GetFName();
			});
		if (!SourceVariable)
		{
			SetError(OutError, FText::Format(
				LOCTEXT(
					"GeneratedOnlyCueField",
					"Generated field '{0}' has no matching authored Cue payload field."),
				FText::FromName(Property->GetFName())));
			return false;
		}
		if (Property->IsA<FDelegateProperty>()
			|| Property->IsA<FMulticastDelegateProperty>())
		{
			SetError(OutError, FText::Format(
				LOCTEXT(
					"GeneratedDelegateCueField",
					"Generated field '{0}' is a delegate. Cue payload fields must contain data only."),
				FText::FromName(Property->GetFName())));
			return false;
		}
		static const FName BlueprintGetterMetadata(TEXT("BlueprintGetter"));
		static const FName BlueprintSetterMetadata(TEXT("BlueprintSetter"));
		if (Property->HasMetaData(BlueprintGetterMetadata)
			|| Property->HasMetaData(BlueprintSetterMetadata))
		{
			SetError(OutError, FText::Format(
				LOCTEXT(
					"GeneratedBehaviorAccessorCueField",
					"Generated field '{0}' declares a Blueprint getter or setter and can execute "
					"behavior during payload access."),
				FText::FromName(Property->GetFName())));
			return false;
		}
		if (Property->HasAnyPropertyFlags(
			static_cast<EPropertyFlags>(GetForbiddenPayloadPropertyFlags())))
		{
			SetError(OutError, FText::Format(
				LOCTEXT(
					"GeneratedNonPersistentCueField",
					"Generated field '{0}' is excluded from reliable save, duplication, copy, cook, "
					"or undo."),
				FText::FromName(Property->GetFName())));
			return false;
		}
		if (Property->HasAnyPropertyFlags(CPF_Net | CPF_RepNotify)
			|| !Property->RepNotifyFunc.IsNone()
			|| Property->GetBlueprintReplicationCondition() != COND_None)
		{
			SetError(OutError, FText::Format(
				LOCTEXT(
					"GeneratedReplicatedCueField",
					"Generated field '{0}' uses replication or RepNotify."),
				FText::FromName(Property->GetFName())));
			return false;
		}
		if ((static_cast<uint64>(Property->GetPropertyFlags())
			& SourceVariable->PropertyFlags) != SourceVariable->PropertyFlags)
		{
			SetError(OutError, FText::Format(
				LOCTEXT(
					"GeneratedCueFieldFlagMismatch",
					"Generated field '{0}' does not match the authored payload field flags."),
				FText::FromName(Property->GetFName())));
			return false;
		}
	}

	if (GeneratedPropertyCount != Blueprint.NewVariables.Num())
	{
		SetError(OutError, LOCTEXT(
			"GeneratedCueFieldCountMismatch",
			"The generated Cue payload schema does not match the authored field inventory."));
		return false;
	}

	return true;
}

void UPaper2DPlusFrameCueBlueprint::PostInitProperties()
{
	Super::PostInitProperties();
	EnsureDataOnlyCompilerExtension();
}

void UPaper2DPlusFrameCueBlueprint::PostLoad()
{
	Super::PostLoad();
	EnsureDataOnlyCompilerExtension();
}

void UPaper2DPlusFrameCueBlueprint::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode == EDuplicateMode::PIE);
	EnsureDataOnlyCompilerExtension();
}

void UPaper2DPlusFrameCueBlueprint::EnsureDataOnlyCompilerExtension()
{
	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return;
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION <= 2
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const bool bAlreadyPresent = Extensions.ContainsByPredicate(
		[](const UBlueprintExtension* Extension)
		{
			return Extension
				&& Extension->IsA<UPaper2DPlusFrameCueBlueprintCompilerExtension>();
		});
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#else
	const bool bAlreadyPresent = GetExtensions().ContainsByPredicate(
		[](const TObjectPtr<UBlueprintExtension>& Extension)
		{
			return Extension
				&& Extension->IsA<UPaper2DPlusFrameCueBlueprintCompilerExtension>();
		});
#endif
	if (bAlreadyPresent)
	{
		return;
	}

	UPaper2DPlusFrameCueBlueprintCompilerExtension* Extension =
		NewObject<UPaper2DPlusFrameCueBlueprintCompilerExtension>(this);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION <= 2
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Extensions.Add(Extension);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#else
	AddExtension(Extension);
#endif
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION <= 2
EDataValidationResult UPaper2DPlusFrameCueBlueprint::IsDataValid(
	TArray<FText>& ValidationErrors)
{
	const EDataValidationResult SuperResult = Super::IsDataValid(ValidationErrors);
	FText ContractError;
	if (!ValidateCompiledDataOnlyContract(*this, &ContractError))
	{
		ValidationErrors.Add(ContractError);
		return EDataValidationResult::Invalid;
	}
	return SuperResult;
}
#else
EDataValidationResult UPaper2DPlusFrameCueBlueprint::IsDataValid(
	FDataValidationContext& Context) const
{
	const EDataValidationResult SuperResult = Super::IsDataValid(Context);
	FText ContractError;
	if (!ValidateCompiledDataOnlyContract(*this, &ContractError))
	{
		Context.AddError(ContractError);
		return EDataValidationResult::Invalid;
	}
	return SuperResult;
}
#endif
#endif // WITH_EDITOR

void UPaper2DPlusFrameCueBlueprintGeneratedClass::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

#if WITH_EDITOR
	if (Ar.IsSaving()
		&& Ar.IsCooking()
		&& !Ar.IsObjectReferenceCollector())
	{
		const UPaper2DPlusFrameCueBlueprint* Blueprint =
			Cast<UPaper2DPlusFrameCueBlueprint>(ClassGeneratedBy);
		FText ContractError;
		const bool bContractValid = Blueprint
			&& UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
				*Blueprint,
				&ContractError,
				this);
		if (!bContractValid)
		{
			if (!Blueprint)
			{
				ContractError = LOCTEXT(
					"MissingCueBlueprintAtCook",
					"The generated Cue class is not owned by a Paper2D+ Frame Cue Type asset.");
			}
			UE_LOG(
				LogPaper2DPlus,
				Error,
				TEXT("Cannot cook invalid Frame Cue Type class '%s': %s"),
				*GetPathName(),
				*ContractError.ToString());
			// This is the cooked export's write archive (not the harvest/reference-collector pass),
			// so the error propagates through the linker and makes SavePackage/UAT fail closed.
			Ar.SetError();
		}
	}
#endif
}

#if WITH_EDITORONLY_DATA
void UPaper2DPlusFrameCueBlueprintCompilerExtension::HandleGenerateFunctionGraphs(
	FKismetCompilerContext* CompilerContext)
{
#if WITH_EDITOR
	UPaper2DPlusFrameCueBlueprint* Blueprint = CompilerContext
		? GetTypedOuter<UPaper2DPlusFrameCueBlueprint>()
		: nullptr;
	if (!Blueprint)
	{
		return;
	}
	// Engine versions differ on whether CreateBlueprint already made and compiled the default Event
	// Graph. That one engine-owned creation compile is quarantined by BS_BeingCreated;
	// PrepareNewlyCreatedBlueprint then normalizes the asset onto exactly one permitted event graph,
	// installs the custom generated-class envelope, seeds the declared behavior event stubs, and
	// performs the first authoritative compile.
	if (Blueprint->bIsNewlyCreated && Blueprint->Status == BS_BeingCreated)
	{
		return;
	}

	UPaper2DPlusFrameCueBlueprint::ExecuteCompileValidation(
		*Blueprint,
		CompilerContext);
#endif
}
#endif // WITH_EDITORONLY_DATA

#undef LOCTEXT_NAMESPACE
