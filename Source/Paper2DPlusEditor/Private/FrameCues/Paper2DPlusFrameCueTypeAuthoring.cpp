// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"

#include "Algo/Sort.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Blueprint/BlueprintSupport.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/UserDefinedEnum.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "Paper2DPlusCueTags.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_MacroInstance.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Engine/UserDefinedStruct.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif

#define LOCTEXT_NAMESPACE "Paper2DPlusFrameCueTypeAuthoring"

namespace Paper2DPlusFrameCueTypeAuthoringInternal
{
	constexpr EClassFlags RejectedClassFlags =
		CLASS_Abstract
		| CLASS_Deprecated
		| CLASS_Hidden
		| CLASS_HideDropDown
		| CLASS_NewerVersionExists
		| CLASS_Transient;

	void SetError(FText* OutError, const FText& Error)
	{
		if (OutError)
		{
			*OutError = Error;
		}
	}

	/**
	 * One event page plus every graph collapsed inside it.
	 *
	 * Collapsing nodes into a composite moves them onto a child graph while they still compile into the
	 * same ubergraph, so a scan that only walked the page itself would report an implemented behavior
	 * event as missing the moment a designer tidied the graph.
	 */
	void CollectGraphWithSubGraphs(const UEdGraph* Graph, TArray<const UEdGraph*>& OutGraphs)
	{
		if (!Graph || OutGraphs.Contains(Graph))
		{
			return;
		}
		OutGraphs.Add(Graph);
		for (const TObjectPtr<UEdGraph>& SubGraph : Graph->SubGraphs)
		{
			CollectGraphWithSubGraphs(SubGraph, OutGraphs);
		}
	}

	void AddDiagnostic(
		TArray<FPaper2DPlusFrameCueAuthoringDiagnostic>& Diagnostics,
		const EPaper2DPlusFrameCueDiagnosticCode Code,
		const FText& Message,
		const EPaper2DPlusFrameCueDiagnosticSeverity Severity =
			EPaper2DPlusFrameCueDiagnosticSeverity::Error)
	{
		FPaper2DPlusFrameCueAuthoringDiagnostic& Diagnostic = Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Code = Code;
		Diagnostic.Severity = Severity;
		Diagnostic.Message = Message;
	}

	struct FSynchronousBehaviorViolation
	{
		const UBlueprint* SourceBlueprint = nullptr;
		const UEdGraph* Graph = nullptr;
		const UEdGraphNode* Node = nullptr;
		FString BehaviorEvents;
		FString OffendingIdentity;
		FText Reason;
	};

	struct FSynchronousBehaviorScanState
	{
		TSet<const UBlueprint*> VisitedBlueprints;
		TSet<const UEdGraph*> VisitedGraphs;
	};

	bool ValidateCompiledSchemaParityAfterSynchronousBehavior(
		const UBlueprint& Blueprint,
		FText* OutError);

	FString JoinNames(const TArray<FName>& Names)
	{
		FString Result;
		for (const FName Name : Names)
		{
			if (!Result.IsEmpty())
			{
				Result += TEXT(", ");
			}
			Result += Name.ToString();
		}
		return Result;
	}

	FString GetBehaviorEventLabel(const UBlueprint& Blueprint)
	{
		TArray<FName> Events;
		TArray<const UEdGraph*> SearchGraphs;
		for (const TObjectPtr<UEdGraph>& EventGraph : Blueprint.UbergraphPages)
		{
			CollectGraphWithSubGraphs(EventGraph, SearchGraphs);
		}
		for (const UEdGraph* Graph : SearchGraphs)
		{
			for (const TObjectPtr<UEdGraphNode>& Node : Graph->Nodes)
			{
				const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
				if (!EventNode || !EventNode->bOverrideFunction)
				{
					continue;
				}
				const FName EventName = EventNode->EventReference.GetMemberName();
				if (Paper2DPlusFrameCueBehavior::IsDeclaredEventName(EventName))
				{
					Events.AddUnique(EventName);
				}
			}
		}
		if (Events.IsEmpty())
		{
			Events = Paper2DPlusFrameCueBehavior::GetDeclaredEventNamesForClass(
				Blueprint.ParentClass);
		}
		Events.Sort([](const FName& Left, const FName& Right)
		{
			return Left.LexicalLess(Right);
		});
		const FString Label = JoinNames(Events);
		return Label.IsEmpty() ? TEXT("<unknown Cue behavior event>") : Label;
	}

	bool IsDeferredTimerFunction(const UFunction& Function)
	{
		if (Function.GetOuterUClass() != UKismetSystemLibrary::StaticClass())
		{
			return false;
		}
		static const TSet<FName> DeferredTimerFunctions = {
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_SetTimer),
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_SetTimerDelegate),
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_SetTimerForNextTick),
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_SetTimerForNextTickDelegate)
		};
		return DeferredTimerFunctions.Contains(Function.GetFName());
	}

	bool ReturnsAsyncActionProxy(const UFunction& Function)
	{
		for (TFieldIterator<FProperty> It(
			&Function,
			EFieldIteratorFlags::ExcludeSuper);
			It;
			++It)
		{
			const FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				continue;
			}
			const FObjectPropertyBase* ObjectProperty =
				CastField<FObjectPropertyBase>(Property);
			return ObjectProperty
				&& ObjectProperty->PropertyClass
				&& ObjectProperty->PropertyClass->IsChildOf(
					UBlueprintAsyncActionBase::StaticClass());
		}
		return false;
	}

	bool SetDeferredCallViolation(
		const UBlueprint& SourceBlueprint,
		const UEdGraph& Graph,
		const UEdGraphNode& Node,
		const FString& BehaviorEvents,
		const FString& OffendingIdentity,
		const FText& Reason,
		FSynchronousBehaviorViolation& OutViolation)
	{
		OutViolation.SourceBlueprint = &SourceBlueprint;
		OutViolation.Graph = &Graph;
		OutViolation.Node = &Node;
		OutViolation.BehaviorEvents = BehaviorEvents;
		OutViolation.OffendingIdentity = OffendingIdentity;
		OutViolation.Reason = Reason;
		return false;
	}

	bool ScanSynchronousBehaviorGraph(
		const UBlueprint& SourceBlueprint,
		const UEdGraph* Graph,
		const FString& BehaviorEvents,
		FSynchronousBehaviorScanState& State,
		FSynchronousBehaviorViolation& OutViolation)
	{
		if (!Graph || State.VisitedGraphs.Contains(Graph))
		{
			return true;
		}
		State.VisitedGraphs.Add(Graph);

		for (const TObjectPtr<UEdGraphNode>& NodeObject : Graph->Nodes)
		{
			const UEdGraphNode* Node = NodeObject;
			if (!Node
				|| Node->GetDesiredEnabledState() == ENodeEnabledState::Disabled)
			{
				continue;
			}

			if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
			{
				const UFunction* Function = CallNode->GetTargetFunction();
				if (Function && IsDeferredTimerFunction(*Function))
				{
					return SetDeferredCallViolation(
						SourceBlueprint,
						*Graph,
						*Node,
						BehaviorEvents,
						Function->GetPathName(),
						LOCTEXT(
							"DeferredTimerCueBehavior",
							"the timer call schedules work after the current Cue callback returns."),
						OutViolation);
				}
				if ((Function
						&& Function->HasMetaData(FBlueprintMetadata::MD_Latent))
					|| CallNode->IsLatentFunction())
				{
					return SetDeferredCallViolation(
						SourceBlueprint,
						*Graph,
						*Node,
						BehaviorEvents,
						Function
							? Function->GetPathName()
							: CallNode->GetClass()->GetPathName(),
						LOCTEXT(
							"LatentFunctionCueBehavior",
							"the referenced function is latent and can resume after the scoped Cue invocation has ended."),
						OutViolation);
				}
				if (Function && ReturnsAsyncActionProxy(*Function))
				{
					return SetDeferredCallViolation(
						SourceBlueprint,
						*Graph,
						*Node,
						BehaviorEvents,
						Function->GetPathName(),
						LOCTEXT(
							"AsyncFactoryCueBehavior",
							"the function creates an async-action proxy whose callbacks outlive synchronous Cue dispatch."),
						OutViolation);
				}
				if (Function)
				{
					const UClass* FunctionOwner = Function->GetOuterUClass();
					const UBlueprint* FunctionBlueprint = FunctionOwner
						? Cast<UBlueprint>(FunctionOwner->ClassGeneratedBy)
						: nullptr;
					if (FunctionBlueprint)
					{
						const TObjectPtr<UEdGraph>* FunctionGraph =
							FunctionBlueprint->FunctionGraphs.FindByPredicate(
								[Function](const TObjectPtr<UEdGraph>& Candidate)
								{
									return Candidate
										&& Candidate->GetFName() == Function->GetFName();
								});
						if (!FunctionGraph || !FunctionGraph->Get())
						{
							return SetDeferredCallViolation(
								SourceBlueprint,
								*Graph,
								*Node,
								BehaviorEvents,
								Function->GetPathName(),
								LOCTEXT(
									"UnresolvedBlueprintFunctionCueBehavior",
									"the Blueprint-generated function body is unavailable, so it cannot be proven synchronous."),
								OutViolation);
						}
						if (!ScanSynchronousBehaviorGraph(
							*FunctionBlueprint,
							FunctionGraph->Get(),
							BehaviorEvents,
							State,
							OutViolation))
						{
							return false;
						}
					}
				}
			}

			if (Node->IsA<UK2Node_BaseAsyncTask>())
			{
				return SetDeferredCallViolation(
					SourceBlueprint,
					*Graph,
					*Node,
					BehaviorEvents,
					Node->GetClass()->GetPathName(),
					LOCTEXT(
						"AsyncNodeCueBehavior",
						"the node is an async task/action whose callbacks can run after the scoped Cue invocation has ended."),
					OutViolation);
			}

			for (UEdGraph* SubGraph : Node->GetSubGraphs())
			{
				if (!ScanSynchronousBehaviorGraph(
					SourceBlueprint,
					SubGraph,
					BehaviorEvents,
					State,
					OutViolation))
				{
					return false;
				}
			}

			if (const UK2Node_MacroInstance* MacroNode =
				Cast<UK2Node_MacroInstance>(Node))
			{
				const UEdGraph* MacroGraph = MacroNode->GetMacroGraph();
				if (!MacroGraph)
				{
					return SetDeferredCallViolation(
						SourceBlueprint,
						*Graph,
						*Node,
						BehaviorEvents,
						MacroNode->GetClass()->GetPathName(),
						LOCTEXT(
							"UnresolvedMacroCueBehavior",
							"the referenced macro graph is unavailable, so its behavior cannot be proven synchronous."),
						OutViolation);
				}
				if (!ScanSynchronousBehaviorGraph(
					SourceBlueprint,
					MacroGraph,
					BehaviorEvents,
					State,
					OutViolation))
				{
					return false;
				}
			}
		}

		for (const TObjectPtr<UEdGraph>& SubGraph : Graph->SubGraphs)
		{
			if (!ScanSynchronousBehaviorGraph(
				SourceBlueprint,
				SubGraph,
				BehaviorEvents,
				State,
				OutViolation))
			{
				return false;
			}
		}
		return true;
	}

	bool ScanSynchronousBehaviorBlueprint(
		const UBlueprint& Blueprint,
		FSynchronousBehaviorScanState& State,
		FSynchronousBehaviorViolation& OutViolation)
	{
		if (State.VisitedBlueprints.Contains(&Blueprint))
		{
			return true;
		}
		State.VisitedBlueprints.Add(&Blueprint);

		const FString BehaviorEvents = GetBehaviorEventLabel(Blueprint);
		for (const TObjectPtr<UEdGraph>& EventGraph : Blueprint.UbergraphPages)
		{
			if (!ScanSynchronousBehaviorGraph(
				Blueprint,
				EventGraph,
				BehaviorEvents,
				State,
				OutViolation))
			{
				return false;
			}
		}

		const UClass* ParentClass = Blueprint.ParentClass;
		if (ParentClass && !ParentClass->HasAnyClassFlags(CLASS_Native))
		{
			if (const UBlueprint* ParentBlueprint =
				Cast<UBlueprint>(ParentClass->ClassGeneratedBy))
			{
				return ScanSynchronousBehaviorBlueprint(
					*ParentBlueprint,
					State,
					OutViolation);
			}
		}
		return true;
	}

	bool IsTransientCompilerClass(const UClass& CueClass)
	{
		const FString Name = CueClass.GetName();
		return Name.StartsWith(TEXT("SKEL_"))
			|| Name.StartsWith(TEXT("REINST_"))
			|| Name.StartsWith(TEXT("TRASHCLASS_"));
	}

	bool IsEditorOnlyClass(const UClass& CueClass)
	{
		for (const UClass* Current = &CueClass; Current; Current = Current->GetSuperClass())
		{
			if (Current->IsEditorOnly()
				|| (Current->GetOutermost()
					&& Current->GetOutermost()->HasAnyPackageFlags(PKG_EditorOnly)))
			{
				return true;
			}
			if (Current == UPaper2DPlusCueBase::StaticClass())
			{
				break;
			}
		}
		return false;
	}

	/**
	 * Stable serialized identity for Cue Type durable schemas.
	 *
	 * v1/v2 fingerprints and snapshots predate the v8 public class rename, so the three renamed
	 * native classes keep emitting their legacy path tokens here. Runtime reflection and saved
	 * UObject references use the new classes through CoreRedirects; reflected class identities in
	 * durable-schema text stay byte-identical, which keeps existing Cue Types Ready without restaging.
	 */
	FSoftObjectPath GetDurableSchemaClassPath(const UClass* CueClass)
	{
		if (CueClass == UPaper2DPlusCueBase::StaticClass())
		{
			return FSoftObjectPath(
				TEXT("/Script/Paper2DPlus.Paper2DPlusFrameCue"));
		}
		if (CueClass == UPaper2DPlusCue::StaticClass())
		{
			return FSoftObjectPath(
				TEXT("/Script/Paper2DPlus.Paper2DPlusMomentCue"));
		}
		if (CueClass == UPaper2DPlusCueState::StaticClass())
		{
			return FSoftObjectPath(
				TEXT("/Script/Paper2DPlus.Paper2DPlusRangeCue"));
		}
		return FSoftObjectPath(CueClass);
	}

	void ReplaceDurableClassPathToken(
		FString& Value,
		const TCHAR* CurrentPath,
		const TCHAR* LegacyPath)
	{
		const int32 CurrentPathLength = FCString::Strlen(CurrentPath);
		int32 SearchFrom = 0;
		while (SearchFrom < Value.Len())
		{
			const int32 Match = Value.Find(
				CurrentPath,
				ESearchCase::CaseSensitive,
				ESearchDir::FromStart,
				SearchFrom);
			if (Match == INDEX_NONE)
			{
				return;
			}

			const int32 End = Match + CurrentPathLength;
			const bool bEndsAtTokenBoundary = End == Value.Len()
				|| (!FChar::IsAlnum(Value[End]) && Value[End] != TEXT('_'));
			if (!bEndsAtTokenBoundary)
			{
				SearchFrom = End;
				continue;
			}

			Value = Value.Left(Match) + LegacyPath + Value.Mid(End);
			SearchFrom = Match + FCString::Strlen(LegacyPath);
		}
	}

	FString NormalizeDurableSchemaText(FString Value)
	{
		// Longest names first so a shared "Cue" prefix can never consume CueBase/CueState.
		ReplaceDurableClassPathToken(
			Value,
			TEXT("/Script/Paper2DPlus.Paper2DPlusCueState"),
			TEXT("/Script/Paper2DPlus.Paper2DPlusRangeCue"));
		ReplaceDurableClassPathToken(
			Value,
			TEXT("/Script/Paper2DPlus.Paper2DPlusCueBase"),
			TEXT("/Script/Paper2DPlus.Paper2DPlusFrameCue"));
		ReplaceDurableClassPathToken(
			Value,
			TEXT("/Script/Paper2DPlus.Paper2DPlusCue"),
			TEXT("/Script/Paper2DPlus.Paper2DPlusMomentCue"));
		return Value;
	}

	FSoftObjectPath NormalizeDurableSchemaPath(const FSoftObjectPath& Path)
	{
		return FSoftObjectPath(NormalizeDurableSchemaText(Path.ToString()));
	}

	bool TypeKeyComponentCarriesClassOrObjectIdentity(
		const FString& TypeKey,
		const TCHAR* ComponentPrefix)
	{
		const FName Categories[] = {
			UEdGraphSchema_K2::PC_Object,
			UEdGraphSchema_K2::PC_Class,
			UEdGraphSchema_K2::PC_SoftObject,
			UEdGraphSchema_K2::PC_SoftClass,
			UEdGraphSchema_K2::PC_Interface
		};
		for (const FName Category : Categories)
		{
			const FString CategoryText = Category.ToString();
			const FString Component =
				FString(ComponentPrefix) + CategoryText + TEXT(";");
			if (TypeKey.StartsWith(Component)
				|| TypeKey.Contains(TEXT(";") + Component))
			{
				return true;
			}
		}
		return false;
	}

	FString NormalizeDurableTypedDefault(FString Value, const FString& TypeKey)
	{
		// Never canonicalize opaque FString/FName/FText or mixed map content. The default text is
		// replaceable only when every serialized value is proven to be a reflected object identity.
		const bool bPrimaryCarriesIdentity =
			TypeKeyComponentCarriesClassOrObjectIdentity(TypeKey, TEXT("category="));
		const bool bMixedMap = TypeKey.Contains(FString::Printf(
			TEXT(";container=%d;"),
			static_cast<int32>(EPinContainerType::Map)))
			&& !TypeKeyComponentCarriesClassOrObjectIdentity(
				TypeKey,
				TEXT("value_category="));
		return bPrimaryCarriesIdentity && !bMixedMap
			? NormalizeDurableSchemaText(MoveTemp(Value))
			: MoveTemp(Value);
	}

	void NormalizeDurableSchema(FPaper2DPlusFrameCueSchema& Schema)
	{
		Schema.ParentClassPath = NormalizeDurableSchemaPath(Schema.ParentClassPath);
		for (FPaper2DPlusFrameCueSchemaField& Field : Schema.Fields)
		{
			Field.TypeKey = NormalizeDurableSchemaText(MoveTemp(Field.TypeKey));
			Field.DefaultValue =
				NormalizeDurableTypedDefault(MoveTemp(Field.DefaultValue), Field.TypeKey);
		}
		for (FPaper2DPlusFrameCueInheritedDefault& Default : Schema.InheritedDefaults)
		{
			Default.OwnerClassPath = NormalizeDurableSchemaPath(Default.OwnerClassPath);
			Default.TypeKey = NormalizeDurableSchemaText(MoveTemp(Default.TypeKey));
			Default.DefaultValue =
				NormalizeDurableTypedDefault(MoveTemp(Default.DefaultValue), Default.TypeKey);
		}
		for (FPaper2DPlusFrameCueSchemaDependency& Dependency : Schema.Dependencies)
		{
			Dependency.ObjectPath = NormalizeDurableSchemaPath(Dependency.ObjectPath);
		}
	}

	EPaper2DPlusFrameCueSchemaContainer ConvertContainer(const EPinContainerType Container)
	{
		switch (Container)
		{
		case EPinContainerType::Array:
			return EPaper2DPlusFrameCueSchemaContainer::Array;
		case EPinContainerType::Set:
			return EPaper2DPlusFrameCueSchemaContainer::Set;
		case EPinContainerType::Map:
			return EPaper2DPlusFrameCueSchemaContainer::Map;
		default:
			return EPaper2DPlusFrameCueSchemaContainer::None;
		}
	}

	FString ObjectPath(const TWeakObjectPtr<UObject>& Object)
	{
		return Object.IsValid() ? Object->GetPathName() : FString();
	}

	FString MakePinTypeKey(const FEdGraphPinType& Type)
	{
		const FSimpleMemberReference& Member = Type.PinSubCategoryMemberReference;
		return NormalizeDurableSchemaText(FString::Printf(
			TEXT("category=%s;subcategory=%s;object=%s;member_parent=%s;member_name=%s;")
			TEXT("member_guid=%s;container=%d;ref=%d;const=%d;weak=%d;wrapper=%d;")
			TEXT("value_category=%s;value_subcategory=%s;value_object=%s;value_const=%d;value_weak=%d"),
			*Type.PinCategory.ToString(),
			*Type.PinSubCategory.ToString(),
			*ObjectPath(Type.PinSubCategoryObject),
			Member.MemberParent ? *Member.MemberParent->GetPathName() : TEXT(""),
			*Member.MemberName.ToString(),
			*Member.MemberGuid.ToString(EGuidFormats::Digits),
			static_cast<int32>(Type.ContainerType),
			Type.bIsReference ? 1 : 0,
			Type.bIsConst ? 1 : 0,
			Type.bIsWeakPointer ? 1 : 0,
			Type.bIsUObjectWrapper ? 1 : 0,
			*Type.PinValueType.TerminalCategory.ToString(),
			*Type.PinValueType.TerminalSubCategory.ToString(),
			*ObjectPath(Type.PinValueType.TerminalSubCategoryObject),
			Type.PinValueType.bTerminalIsConst ? 1 : 0,
			Type.PinValueType.bTerminalIsWeakPointer ? 1 : 0));
	}

	FString ExportCanonicalValue(
		const FProperty& Property,
		const void* Value,
		UObject* Owner);

	FString ExportScalarValue(
		const FProperty& Property,
		const void* Value,
		UObject* Owner)
	{
		FString Result;
#if UE_VERSION_OLDER_THAN(5, 5, 0)
		Property.ExportTextItem(Result, Value, nullptr, Owner, PPF_None);
#else
		Property.ExportTextItem_Direct(Result, Value, nullptr, Owner, PPF_None);
#endif
		return (CastField<FObjectPropertyBase>(&Property)
			|| CastField<FInterfaceProperty>(&Property))
			? NormalizeDurableSchemaText(MoveTemp(Result))
			: MoveTemp(Result);
	}

	FString JoinCanonicalValues(const TCHAR* Prefix, TArray<FString>& Values)
	{
		FString Result(Prefix);
		Result += TEXT("[");
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			if (Index > 0)
			{
				Result += TEXT(",");
			}
			Result += FString::Printf(TEXT("%d:"), Values[Index].Len());
			Result += Values[Index];
		}
		Result += TEXT("]");
		return Result;
	}

	FString ExportCanonicalValue(
		const FProperty& Property,
		const void* Value,
		UObject* Owner)
	{
		if (!Value)
		{
			return TEXT("null");
		}
		if (const FArrayProperty* Array = CastField<FArrayProperty>(&Property))
		{
			FScriptArrayHelper Helper(Array, Value);
			TArray<FString> Elements;
			Elements.Reserve(Helper.Num());
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				Elements.Add(ExportCanonicalValue(*Array->Inner, Helper.GetRawPtr(Index), Owner));
			}
			return JoinCanonicalValues(TEXT("array"), Elements);
		}
		if (const FSetProperty* Set = CastField<FSetProperty>(&Property))
		{
			FScriptSetHelper Helper(Set, Value);
			TArray<FString> Elements;
			Elements.Reserve(Helper.Num());
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
			{
				if (Helper.IsValidIndex(Index))
				{
					Elements.Add(ExportCanonicalValue(
						*Set->ElementProp, Helper.GetElementPtr(Index), Owner));
				}
			}
			Elements.Sort();
			return JoinCanonicalValues(TEXT("set"), Elements);
		}
		if (const FMapProperty* Map = CastField<FMapProperty>(&Property))
		{
			FScriptMapHelper Helper(Map, Value);
			TArray<FString> Entries;
			Entries.Reserve(Helper.Num());
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
			{
				if (Helper.IsValidIndex(Index))
				{
					const FString Key = ExportCanonicalValue(
						*Map->KeyProp, Helper.GetKeyPtr(Index), Owner);
					const FString MapValue = ExportCanonicalValue(
						*Map->ValueProp, Helper.GetValuePtr(Index), Owner);
					Entries.Add(FString::Printf(
						TEXT("%d:%s=%d:%s"),
						Key.Len(), *Key, MapValue.Len(), *MapValue));
				}
			}
			Entries.Sort();
			return JoinCanonicalValues(TEXT("map"), Entries);
		}
		if (const FStructProperty* Struct = CastField<FStructProperty>(&Property))
		{
			TArray<const FProperty*> Properties;
			for (TFieldIterator<FProperty> It(
				Struct->Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				Properties.Add(*It);
			}
			Properties.Sort([](const FProperty& Left, const FProperty& Right)
			{
				return Left.GetName() < Right.GetName();
			});

			TArray<FString> Members;
			Members.Reserve(Properties.Num());
			for (const FProperty* Member : Properties)
			{
				const FString MemberValue = ExportCanonicalValue(
					*Member, Member->ContainerPtrToValuePtr<void>(Value), Owner);
				Members.Add(FString::Printf(
					TEXT("%d:%s=%d:%s"),
					Member->GetName().Len(),
					*Member->GetName(),
					MemberValue.Len(),
					*MemberValue));
			}
			return JoinCanonicalValues(TEXT("struct"), Members);
		}

		return ExportScalarValue(Property, Value, Owner);
	}

	FString EscapeToken(const FString& Value)
	{
		return FString::Printf(TEXT("%d:%s"), Value.Len(), *Value);
	}

	FString FieldSortKey(const FPaper2DPlusFrameCueSchemaField& Field)
	{
		return Field.FieldId.IsValid()
			? TEXT("0:") + Field.FieldId.ToString(EGuidFormats::Digits)
			: TEXT("1:") + Field.Name.ToString();
	}

	FString BuildCanonicalSchema(FPaper2DPlusFrameCueSchema& Schema)
	{
		NormalizeDurableSchema(Schema);
		Schema.Fields.Sort([](
			const FPaper2DPlusFrameCueSchemaField& Left,
			const FPaper2DPlusFrameCueSchemaField& Right)
		{
			return FieldSortKey(Left) < FieldSortKey(Right);
		});
		for (FPaper2DPlusFrameCueSchemaField& Field : Schema.Fields)
		{
			Field.Metadata.Sort([](
				const FPaper2DPlusFrameCueSchemaMetadata& Left,
				const FPaper2DPlusFrameCueSchemaMetadata& Right)
			{
				return Left.Key == Right.Key
					? Left.Value < Right.Value
					: Left.Key.LexicalLess(Right.Key);
			});
		}
		Schema.InheritedDefaults.Sort([](
			const FPaper2DPlusFrameCueInheritedDefault& Left,
			const FPaper2DPlusFrameCueInheritedDefault& Right)
		{
			const FString LeftKey = Left.OwnerClassPath.ToString() + TEXT(":") + Left.Name.ToString();
			const FString RightKey = Right.OwnerClassPath.ToString() + TEXT(":") + Right.Name.ToString();
			return LeftKey < RightKey;
		});
		Schema.Dependencies.Sort([](
			const FPaper2DPlusFrameCueSchemaDependency& Left,
			const FPaper2DPlusFrameCueSchemaDependency& Right)
		{
			const FString LeftKey = FString::FromInt(static_cast<int32>(Left.Kind))
				+ TEXT(":") + Left.ObjectPath.ToString();
			const FString RightKey = FString::FromInt(static_cast<int32>(Right.Kind))
				+ TEXT(":") + Right.ObjectPath.ToString();
			return LeftKey < RightKey;
		});

		Schema.BehaviorEvents.Sort([](const FName& Left, const FName& Right)
		{
			return Left.LexicalLess(Right);
		});

		FString Canonical = FString::Printf(
			TEXT("schema=%d;kind=%d;parent=%s\n"),
			Schema.Version,
			static_cast<int32>(Schema.Kind),
			*EscapeToken(Schema.ParentClassPath.ToString()));
		// Emitted only when behavior exists, so a payload-only Cue Type keeps the exact canonical
		// text (and therefore the exact fingerprint) it had before Cue behavior was introduced.
		for (const FName& BehaviorEvent : Schema.BehaviorEvents)
		{
			Canonical += TEXT("behavior;") + EscapeToken(BehaviorEvent.ToString()) + TEXT("\n");
		}
		for (const FPaper2DPlusFrameCueSchemaField& Field : Schema.Fields)
		{
			Canonical += TEXT("field;");
			Canonical += EscapeToken(Field.FieldId.ToString(EGuidFormats::Digits)) + TEXT(";");
			Canonical += EscapeToken(Field.Name.ToString()) + TEXT(";");
			Canonical += EscapeToken(Field.FriendlyName) + TEXT(";");
			Canonical += EscapeToken(Field.Category) + TEXT(";");
			Canonical += EscapeToken(Field.TypeKey) + TEXT(";");
			Canonical += FString::FromInt(static_cast<int32>(Field.Container)) + TEXT(";");
			Canonical += FString::Printf(
				TEXT("%llu;"), static_cast<unsigned long long>(Field.PropertyFlags));
			Canonical += EscapeToken(Field.DefaultValue) + TEXT("\n");
			for (const FPaper2DPlusFrameCueSchemaMetadata& Metadata : Field.Metadata)
			{
				Canonical += TEXT("metadata;") + EscapeToken(Metadata.Key.ToString())
					+ TEXT(";") + EscapeToken(Metadata.Value) + TEXT("\n");
			}
		}
		for (const FPaper2DPlusFrameCueInheritedDefault& Default : Schema.InheritedDefaults)
		{
			Canonical += TEXT("inherited;") + EscapeToken(Default.OwnerClassPath.ToString())
				+ TEXT(";") + EscapeToken(Default.Name.ToString())
				+ TEXT(";") + EscapeToken(Default.TypeKey)
				+ TEXT(";") + EscapeToken(Default.DefaultValue) + TEXT("\n");
		}
		for (const FPaper2DPlusFrameCueSchemaDependency& Dependency : Schema.Dependencies)
		{
			Canonical += TEXT("dependency;")
				+ FString::FromInt(static_cast<int32>(Dependency.Kind))
				+ TEXT(";") + EscapeToken(Dependency.ObjectPath.ToString())
				+ TEXT(";") + EscapeToken(Dependency.Fingerprint) + TEXT("\n");
		}
		return Canonical;
	}

	FString HashCanonicalText(const FString& Canonical)
	{
		FTCHARToUTF8 Utf8(*Canonical);
		FSHA1 Sha;
		Sha.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		Sha.Final();
		uint8 Digest[FSHA1::DigestSize];
		Sha.GetHash(Digest);
		FString Result;
		Result.Reserve(FSHA1::DigestSize * 2);
		for (const uint8 Byte : Digest)
		{
			Result += FString::Printf(TEXT("%02X"), Byte);
		}
		return Result;
	}

	FString BuildUserDefinedEnumFingerprint(const UUserDefinedEnum& Enum)
	{
		FString Canonical = TEXT("enum;") + EscapeToken(Enum.GetPathName())
			+ TEXT(";") + FString::FromInt(static_cast<int32>(Enum.GetCppForm())) + TEXT("\n");
		for (int32 Index = 0; Index < Enum.NumEnums(); ++Index)
		{
			Canonical += TEXT("value;")
				+ EscapeToken(Enum.GetNameByIndex(Index).ToString()) + TEXT(";")
				+ EscapeToken(Enum.GetAuthoredNameStringByIndex(Index)) + TEXT(";")
				+ FString::Printf(TEXT("%lld\n"), static_cast<long long>(Enum.GetValueByIndex(Index)));
		}
		return HashCanonicalText(Canonical);
	}

	FString BuildUserDefinedStructFingerprint(const UUserDefinedStruct& Struct)
	{
		FString Canonical = TEXT("struct;") + EscapeToken(Struct.GetPathName())
			+ TEXT(";") + EscapeToken(Struct.Guid.ToString(EGuidFormats::Digits))
			+ TEXT(";") + FString::FromInt(static_cast<int32>(Struct.Status)) + TEXT("\n");

		if (const TArray<FStructVariableDescription>* Descriptions =
			FStructureEditorUtils::GetVarDescPtr(&Struct))
		{
			TArray<const FStructVariableDescription*> SortedDescriptions;
			SortedDescriptions.Reserve(Descriptions->Num());
			for (const FStructVariableDescription& Description : *Descriptions)
			{
				SortedDescriptions.Add(&Description);
			}
			SortedDescriptions.Sort([](
				const FStructVariableDescription& Left,
				const FStructVariableDescription& Right)
			{
				const FString LeftKey = Left.VarGuid.IsValid()
					? Left.VarGuid.ToString(EGuidFormats::Digits)
					: Left.VarName.ToString();
				const FString RightKey = Right.VarGuid.IsValid()
					? Right.VarGuid.ToString(EGuidFormats::Digits)
					: Right.VarName.ToString();
				return LeftKey < RightKey;
			});

			for (const FStructVariableDescription* Description : SortedDescriptions)
			{
				const FString TypeKey = MakePinTypeKey(Description->ToPinType());
				const FString DefaultValue = NormalizeDurableTypedDefault(
					Description->DefaultValue,
					TypeKey);
				const FString CurrentDefaultValue = NormalizeDurableTypedDefault(
					Description->CurrentDefaultValue,
					TypeKey);
				Canonical += TEXT("source_field;")
					+ EscapeToken(Description->VarGuid.ToString(EGuidFormats::Digits)) + TEXT(";")
					+ EscapeToken(Description->VarName.ToString()) + TEXT(";")
					+ EscapeToken(Description->FriendlyName) + TEXT(";")
					+ EscapeToken(TypeKey) + TEXT(";")
					+ EscapeToken(DefaultValue) + TEXT(";")
					+ EscapeToken(CurrentDefaultValue) + TEXT(";")
					+ EscapeToken(Description->ToolTip) + TEXT(";")
					+ FString::Printf(
						TEXT("%d;%d;%d;%d;%d\n"),
						Description->bInvalidMember ? 1 : 0,
						Description->bDontEditOnInstance ? 1 : 0,
						Description->bEnableSaveGame ? 1 : 0,
						Description->bEnableMultiLineText ? 1 : 0,
						Description->bEnable3dWidget ? 1 : 0);
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
				TArray<FName> MetadataKeys;
				Description->MetaData.GetKeys(MetadataKeys);
				MetadataKeys.Sort(FNameLexicalLess());
				for (const FName Key : MetadataKeys)
				{
					Canonical += TEXT("source_metadata;") + EscapeToken(Key.ToString())
						+ TEXT(";") + EscapeToken(Description->MetaData.FindRef(Key)) + TEXT("\n");
				}
#endif
			}
		}

		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		TArray<const FProperty*> Properties;
		for (TFieldIterator<FProperty> It(&Struct, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			Properties.Add(*It);
		}
		Properties.Sort([](const FProperty& Left, const FProperty& Right)
		{
			return Left.GetName() < Right.GetName();
		});
		const uint8* DefaultData = Struct.GetDefaultInstance();
		for (const FProperty* Property : Properties)
		{
			FEdGraphPinType PinType;
			const FString TypeKey = K2Schema && K2Schema->ConvertPropertyToPinType(Property, PinType)
				? MakePinTypeKey(PinType)
				: Property->GetClass()->GetName();
			const FString DefaultValue = DefaultData
				? ExportCanonicalValue(
					*Property,
					Property->ContainerPtrToValuePtr<void>(const_cast<uint8*>(DefaultData)),
					const_cast<UUserDefinedStruct*>(&Struct))
				: FString();
			Canonical += TEXT("compiled_field;") + EscapeToken(Property->GetName())
				+ TEXT(";") + EscapeToken(TypeKey)
				+ TEXT(";") + FString::Printf(
					TEXT("%llu;"), static_cast<unsigned long long>(Property->GetPropertyFlags()))
				+ EscapeToken(DefaultValue) + TEXT("\n");
		}
		return HashCanonicalText(Canonical);
	}

	class FSchemaDependencyCollector
	{
	public:
		explicit FSchemaDependencyCollector(FPaper2DPlusFrameCueSchema& InSchema)
			: Schema(InSchema)
		{
		}

		void CollectPinType(const FEdGraphPinType& Type)
		{
			CollectObject(Type.PinSubCategoryObject.Get());
			CollectObject(Type.PinValueType.TerminalSubCategoryObject.Get());
		}

		void CollectProperty(const FProperty& Property)
		{
			if (const FArrayProperty* Array = CastField<FArrayProperty>(&Property))
			{
				CollectProperty(*Array->Inner);
				return;
			}
			if (const FSetProperty* Set = CastField<FSetProperty>(&Property))
			{
				CollectProperty(*Set->ElementProp);
				return;
			}
			if (const FMapProperty* Map = CastField<FMapProperty>(&Property))
			{
				CollectProperty(*Map->KeyProp);
				CollectProperty(*Map->ValueProp);
				return;
			}
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(&Property))
			{
				CollectObject(StructProperty->Struct);
				return;
			}
			if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(&Property))
			{
				CollectObject(EnumProperty->GetEnum());
				return;
			}
			if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(&Property))
			{
				CollectObject(NumericProperty->GetIntPropertyEnum());
			}
		}

	private:
		void AddDependency(
			const EPaper2DPlusFrameCueSchemaDependencyKind Kind,
			const UObject& Object,
			const FString& Fingerprint)
		{
			const FSoftObjectPath Path(&Object);
			FPaper2DPlusFrameCueSchemaDependency* Existing = Schema.Dependencies.FindByPredicate(
				[Kind, &Path](const FPaper2DPlusFrameCueSchemaDependency& Dependency)
				{
					return Dependency.Kind == Kind && Dependency.ObjectPath == Path;
				});
			if (!Existing)
			{
				Existing = &Schema.Dependencies.AddDefaulted_GetRef();
				Existing->Kind = Kind;
				Existing->ObjectPath = Path;
			}
			Existing->Fingerprint = Fingerprint;
		}

		void CollectObject(const UObject* Object)
		{
			if (const UUserDefinedEnum* Enum = Cast<UUserDefinedEnum>(Object))
			{
				AddDependency(
					EPaper2DPlusFrameCueSchemaDependencyKind::UserDefinedEnum,
					*Enum,
					BuildUserDefinedEnumFingerprint(*Enum));
				return;
			}

			const UUserDefinedStruct* Struct = Cast<UUserDefinedStruct>(Object);
			if (!Struct)
			{
				return;
			}
			AddDependency(
				EPaper2DPlusFrameCueSchemaDependencyKind::UserDefinedStruct,
				*Struct,
				BuildUserDefinedStructFingerprint(*Struct));

			const FString Path = Struct->GetPathName();
			if (ExpandedStructs.Contains(Path))
			{
				return;
			}
			ExpandedStructs.Add(Path);

			if (const TArray<FStructVariableDescription>* Descriptions =
				FStructureEditorUtils::GetVarDescPtr(Struct))
			{
				for (const FStructVariableDescription& Description : *Descriptions)
				{
					CollectPinType(Description.ToPinType());
				}
			}
			for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				CollectProperty(**It);
			}
		}

		FPaper2DPlusFrameCueSchema& Schema;
		TSet<FString> ExpandedStructs;
	};

	bool MetadataEqual(
		const TArray<FPaper2DPlusFrameCueSchemaMetadata>& Left,
		const TArray<FPaper2DPlusFrameCueSchemaMetadata>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		TArray<FString> LeftKeys;
		TArray<FString> RightKeys;
		LeftKeys.Reserve(Left.Num());
		RightKeys.Reserve(Right.Num());
		for (const FPaper2DPlusFrameCueSchemaMetadata& Item : Left)
		{
			LeftKeys.Add(Item.Key.ToString() + TEXT("=") + Item.Value);
		}
		for (const FPaper2DPlusFrameCueSchemaMetadata& Item : Right)
		{
			RightKeys.Add(Item.Key.ToString() + TEXT("=") + Item.Value);
		}
		LeftKeys.Sort();
		RightKeys.Sort();
		return LeftKeys == RightKeys;
	}

	int32 CompatibilityRank(const EPaper2DPlusFrameCueSchemaCompatibility Compatibility)
	{
		switch (Compatibility)
		{
		case EPaper2DPlusFrameCueSchemaCompatibility::Identical: return 0;
		case EPaper2DPlusFrameCueSchemaCompatibility::Compatible: return 1;
		case EPaper2DPlusFrameCueSchemaCompatibility::Conditional: return 2;
		case EPaper2DPlusFrameCueSchemaCompatibility::DependencyChange: return 3;
		case EPaper2DPlusFrameCueSchemaCompatibility::Destructive: return 4;
		case EPaper2DPlusFrameCueSchemaCompatibility::Invalid: return 5;
		default: return 5;
		}
	}

	void PromoteCompatibility(
		FPaper2DPlusFrameCueSchemaDiff& Diff,
		const EPaper2DPlusFrameCueSchemaCompatibility Candidate)
	{
		if (CompatibilityRank(Candidate) > CompatibilityRank(Diff.Compatibility))
		{
			Diff.Compatibility = Candidate;
		}
	}

	void AddChange(
		FPaper2DPlusFrameCueSchemaDiff& Diff,
		const EPaper2DPlusFrameCueSchemaChangeKind Kind,
		const FPaper2DPlusFrameCueSchemaField* OldField,
		const FPaper2DPlusFrameCueSchemaField* NewField,
		const FText& Summary,
		const EPaper2DPlusFrameCueSchemaCompatibility Compatibility)
	{
		FPaper2DPlusFrameCueSchemaChange& Change = Diff.Changes.AddDefaulted_GetRef();
		Change.Kind = Kind;
		Change.FieldId = OldField ? OldField->FieldId : (NewField ? NewField->FieldId : FGuid());
		Change.OldName = OldField ? OldField->Name : NAME_None;
		Change.NewName = NewField ? NewField->Name : NAME_None;
		Change.Summary = Summary;
		PromoteCompatibility(Diff, Compatibility);
	}

	FString DependencyKey(const FPaper2DPlusFrameCueSchemaDependency& Dependency)
	{
		return FString::FromInt(static_cast<int32>(Dependency.Kind))
			+ TEXT(":") + Dependency.ObjectPath.ToString();
	}

	FString InheritedDefaultKey(const FPaper2DPlusFrameCueInheritedDefault& Default)
	{
		return Default.OwnerClassPath.ToString() + TEXT(":") + Default.Name.ToString();
	}

	void AddLoadFailure(
		FPaper2DPlusFrameCueTypeDiscoveryResult& Result,
		const FString& GeneratedClassObjectPath)
	{
		FPaper2DPlusFrameCueTypeDescriptor& Rejected = Result.RejectedTypes.AddDefaulted_GetRef();
		Rejected.ClassPath = FSoftObjectPath(GeneratedClassObjectPath);
		Rejected.DisplayName = FText::FromString(
			FPackageName::ObjectPathToObjectName(GeneratedClassObjectPath));
		Rejected.PickerLabel = Rejected.DisplayName;
		Rejected.Availability = EPaper2DPlusFrameCueTypeAvailability::Invalid;
		AddDiagnostic(
			Rejected.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::DiscoveryLoadFailed,
			FText::Format(
				LOCTEXT("ColdCueTypeLoadFailed", "Cue Type class '{0}' could not be loaded."),
				FText::FromString(GeneratedClassObjectPath)));
	}

	void DisambiguateAndSort(TArray<FPaper2DPlusFrameCueTypeDescriptor>& Types)
	{
		TMap<FString, int32> DisplayNameCounts;
		for (const FPaper2DPlusFrameCueTypeDescriptor& Type : Types)
		{
			++DisplayNameCounts.FindOrAdd(Type.DisplayName.ToString());
		}
		for (FPaper2DPlusFrameCueTypeDescriptor& Type : Types)
		{
			const FString DisplayName = Type.DisplayName.ToString();
			Type.PickerLabel = DisplayNameCounts.FindRef(DisplayName) > 1
				? FText::Format(
					LOCTEXT("DisambiguatedCueTypeLabel", "{0} — {1}"),
					Type.DisplayName,
					FText::FromString(FPackageName::ObjectPathToPackageName(
						Type.ClassPath.ToString())))
				: Type.DisplayName;
		}
		Types.Sort([](
			const FPaper2DPlusFrameCueTypeDescriptor& Left,
			const FPaper2DPlusFrameCueTypeDescriptor& Right)
		{
			const FString LeftLabel = Left.PickerLabel.ToString();
			const FString RightLabel = Right.PickerLabel.ToString();
			return LeftLabel == RightLabel
				? Left.ClassPath.ToString() < Right.ClassPath.ToString()
				: LeftLabel < RightLabel;
		});
	}

	bool NormalizeNewCueType(
		UPaper2DPlusFrameCueBlueprint& Blueprint,
		FText& OutError)
	{
		if (!Blueprint.bIsNewlyCreated)
		{
			OutError = LOCTEXT(
				"ExistingCueTypeNormalizationDenied",
				"Only a newly allocated Cue Type may be normalized during structured creation.");
			return false;
		}
		if (Blueprint.ParentClass != UPaper2DPlusCue::StaticClass()
			&& Blueprint.ParentClass != UPaper2DPlusCueState::StaticClass())
		{
			OutError = LOCTEXT(
				"StructuredCueTypeParentInvalid",
				"Structured Cue Type creation accepts exactly the native Cue or Cue State base.");
			return false;
		}
		if (Blueprint.FunctionGraphs.Num() > 0
			|| Blueprint.MacroGraphs.Num() > 0
			|| Blueprint.DelegateSignatureGraphs.Num() > 0
			|| Blueprint.UbergraphPages.Num() > 1)
		{
			OutError = LOCTEXT(
				"StructuredCueTypeUnexpectedGraphs",
				"The newly allocated Cue Type contains unexpected behavior graphs.");
			return false;
		}

		// Engine versions disagree on whether CreateBlueprint already made the default Event Graph.
		// Either way the normalized shape is one empty event graph, ready for its seeded events.
		if (Blueprint.UbergraphPages.Num() == 1)
		{
			const UEdGraph* EventGraph = Blueprint.UbergraphPages[0];
			if (!EventGraph || EventGraph->Nodes.Num() > 0)
			{
				OutError = LOCTEXT(
					"StructuredCueTypeNonEmptyGraph",
					"The newly allocated Cue Type contains authored graph content.");
				return false;
			}
		}
		else if (!FPaper2DPlusFrameCueTypeAuthoring::EnsureBehaviorEventGraph(Blueprint))
		{
			OutError = LOCTEXT(
				"StructuredCueTypeEventGraphFailed",
				"The new Cue Type's behavior event graph could not be created.");
			return false;
		}

		if (Blueprint.UbergraphPages.Num() != 1
			|| Blueprint.FunctionGraphs.Num() > 0
			|| Blueprint.MacroGraphs.Num() > 0
			|| Blueprint.DelegateSignatureGraphs.Num() > 0)
		{
			OutError = LOCTEXT(
				"StructuredCueTypeGraphNormalizationFailed",
				"The new Cue Type could not be normalized onto one permitted behavior event graph.");
			return false;
		}

		const bool bHasCorrectEnvelope = Blueprint.GeneratedClass
			&& Cast<UPaper2DPlusFrameCueBlueprintGeneratedClass>(
				Blueprint.GeneratedClass.Get())
			&& Blueprint.GeneratedClass->ClassGeneratedBy == &Blueprint
			&& Blueprint.GeneratedClass->GetSuperClass() == Blueprint.ParentClass;
		if (!bHasCorrectEnvelope)
		{
			FName GeneratedClassName;
			FName SkeletonClassName;
			Blueprint.GetBlueprintClassNames(GeneratedClassName, SkeletonClassName);
			FBlueprintEditorUtils::RemoveGeneratedClasses(&Blueprint);

			UPaper2DPlusFrameCueBlueprintGeneratedClass* GeneratedClass =
				NewObject<UPaper2DPlusFrameCueBlueprintGeneratedClass>(
					Blueprint.GetOutermost(),
					GeneratedClassName,
					RF_Public | RF_Transactional);
			if (!GeneratedClass)
			{
				OutError = LOCTEXT(
					"StructuredCueTypeEnvelopeAllocationFailed",
					"The specialized generated-class envelope could not be allocated.");
				return false;
			}
			Blueprint.GeneratedClass = GeneratedClass;
			GeneratedClass->ClassGeneratedBy = &Blueprint;
			GeneratedClass->SetSuperStruct(Blueprint.ParentClass);
		}
		return true;
	}

	/**
	 * Writes the replaceable identity and networking defaults a brand-new Cue Type is born with.
	 *
	 * A Cue Type is behavior-capable from creation, and the overwhelmingly common behavior a designer
	 * writes is cosmetic — a sound, an effect, screen feedback. The runtime base default of
	 * LocalAlways would run every one of those on a dedicated server with zero designer action, so
	 * structured creation stamps the networked-correct default instead. It stays an ordinary class
	 * default: visible and editable in the restricted editor's Class Defaults under Networking, and
	 * carried across recompiles like any other authored default.
	 *
	 * The Cue tag follows the same boundary: only this generated Cue Type CDO receives the native,
	 * replaceable Default identity. Native Cue CDOs, project settings/config, placements, and Effect
	 * Profiles are never touched. Color deliberately remains white so explicit authored Color keeps
	 * its existing meaning and the timeline may resolve the tag convention underneath it.
	 */
	bool ApplyCreationDefaults(UBlueprint& Blueprint)
	{
		UClass* GeneratedClass = Blueprint.GeneratedClass;
		UPaper2DPlusCueBase* GeneratedDefaults = GeneratedClass
			? Cast<UPaper2DPlusCueBase>(GeneratedClass->GetDefaultObject(false))
			: nullptr;
		if (!GeneratedDefaults)
		{
			return false;
		}
		GeneratedDefaults->Modify();
		GeneratedDefaults->NetPolicy = EPaper2DPlusFrameCueNetPolicy::CosmeticOnly;
		GeneratedDefaults->CueTag = Paper2DPlusCueTags::Default.GetTag();
		return true;
	}

	bool CleanupCreatedCueType(
		UBlueprint& Blueprint,
		UPackage& OriginalPackage,
		const FName OriginalName,
		const bool bPackageWasDirty)
	{
		FBlueprintEditorUtils::RemoveGeneratedClasses(&Blueprint);
		Blueprint.ClearFlags(RF_Public | RF_Standalone | RF_Transactional);
		const bool bRenamed = Blueprint.Rename(
			nullptr,
			GetTransientPackage(),
			REN_DontCreateRedirectors | REN_NonTransactional);
		if (bRenamed)
		{
			Blueprint.MarkAsGarbage();
		}
		OriginalPackage.SetDirtyFlag(bPackageWasDirty);
		return bRenamed
			&& StaticFindObject(
				UObject::StaticClass(), &OriginalPackage, *OriginalName.ToString()) == nullptr;
	}
}

EPaper2DPlusFrameCueTypeKind FPaper2DPlusFrameCueTypeAuthoring::ClassifyKind(
	const UClass* CueClass)
{
	if (!CueClass)
	{
		return EPaper2DPlusFrameCueTypeKind::Invalid;
	}
	if (CueClass->IsChildOf(UPaper2DPlusCueState::StaticClass()))
	{
		return EPaper2DPlusFrameCueTypeKind::Range;
	}
	if (CueClass->IsChildOf(UPaper2DPlusCue::StaticClass()))
	{
		return EPaper2DPlusFrameCueTypeKind::Moment;
	}
	return EPaper2DPlusFrameCueTypeKind::Invalid;
}

FPaper2DPlusFrameCueTypeDescriptor FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(
	UClass* CueClass)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringInternal;
	FPaper2DPlusFrameCueTypeDescriptor Result;
	Result.Class = CueClass;
	if (!CueClass)
	{
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::NullClass,
			LOCTEXT("NullCueType", "The Cue Type class is null."));
		return Result;
	}

	Result.ClassPath = FSoftObjectPath(CueClass);
	Result.DisplayName = CueClass->GetDisplayNameText();
	Result.PickerLabel = Result.DisplayName;
	Result.Kind = ClassifyKind(CueClass);
	if (Result.Kind == EPaper2DPlusFrameCueTypeKind::Invalid)
	{
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::NotFrameCue,
			LOCTEXT("NotFrameCueType", "The class is not a Paper2D+ Cue or Cue State."));
		return Result;
	}
	if (CueClass->HasAnyClassFlags(RejectedClassFlags) || IsTransientCompilerClass(*CueClass))
	{
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::NonAuthorableClass,
			LOCTEXT(
				"NonAuthorableCueType",
				"The Cue Type is abstract, hidden, deprecated, transient, or stale."));
		return Result;
	}
	if (IsEditorOnlyClass(*CueClass))
	{
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::EditorOnlyClass,
			LOCTEXT("EditorOnlyCueType", "Editor-only Cue Types cannot be placed in cooked data."));
		return Result;
	}

	if (CueClass->HasAnyClassFlags(CLASS_Native))
	{
		Result.Origin = EPaper2DPlusFrameCueTypeOrigin::Native;
		Result.Availability = EPaper2DPlusFrameCueTypeAvailability::Ready;
		return Result;
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(CueClass->ClassGeneratedBy);
	if (!Blueprint)
	{
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::MissingBlueprint,
			LOCTEXT("MissingCueBlueprint", "The generated Cue class has no owning Blueprint asset."));
		return Result;
	}
	Result.BlueprintPath = FSoftObjectPath(Blueprint);
	UPaper2DPlusFrameCueBlueprint* Specialized = Cast<UPaper2DPlusFrameCueBlueprint>(Blueprint);
	Result.Origin = Specialized
		? EPaper2DPlusFrameCueTypeOrigin::SpecializedBlueprint
		: EPaper2DPlusFrameCueTypeOrigin::LegacyBlueprint;

	// A specialized Cue Type may own one event graph implementing its declared behavior events; the
	// compiled envelope below is what proves the graph produced nothing beyond those overrides. A
	// legacy Cue Blueprint is still quarantined by ANY graph because it has no such envelope.
	const FPaper2DPlusFrameCueLegacyGraphInventory Graphs = InventoryLegacyGraphs(*Blueprint);
	const int32 QuarantinedGraphCount = Specialized
		? Graphs.CountUnsupportedGraphs()
		: Graphs.Graphs.Num();
	if (QuarantinedGraphCount > 0)
	{
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::LegacyBehaviorGraphs,
			FText::Format(
				LOCTEXT(
					"CueBehaviorGraphs",
					"Cue Blueprint contains {0} authored behavior graph(s) and is quarantined."),
				QuarantinedGraphCount));
		return Result;
	}
	if (!Specialized)
	{
		Result.Availability = EPaper2DPlusFrameCueTypeAvailability::LegacyNeedsBaseline;
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::LegacyBaselineRequired,
			LOCTEXT(
				"LegacyCueNeedsBaseline",
				"Graphless legacy Cue Blueprint needs explicit protected-baseline adoption before placement."),
			EPaper2DPlusFrameCueDiagnosticSeverity::Warning);
		return Result;
	}

	FText SynchronousBehaviorError;
	if (!ValidateSynchronousBehavior(*Blueprint, &SynchronousBehaviorError))
	{
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::DeferredBehavior,
			SynchronousBehaviorError);
		return Result;
	}

	if (Blueprint->Status != BS_UpToDate
		&& Blueprint->Status != BS_UpToDateWithWarnings)
	{
		Result.Availability = Blueprint->Status == BS_Error
			? EPaper2DPlusFrameCueTypeAvailability::Invalid
			: EPaper2DPlusFrameCueTypeAvailability::NeedsCompile;
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::CompileRequired,
			Blueprint->Status == BS_Error
				? LOCTEXT("CueTypeCompileError", "The Cue Type has compile errors.")
				: LOCTEXT("CueTypeCompileRequired", "Compile the Cue Type before placing it."));
		return Result;
	}

	FText ValidationError;
	if (!UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(*Blueprint, &ValidationError))
	{
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::InvalidDataOnlyContract,
			ValidationError);
		return Result;
	}
	if (!UPaper2DPlusFrameCueBlueprint::ValidateGeneratedClassEnvelope(
		*Blueprint, &ValidationError, CueClass))
	{
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::InvalidGeneratedEnvelope,
			ValidationError);
		return Result;
	}
	if (!ValidateCompiledSchemaParityAfterSynchronousBehavior(*Blueprint, &ValidationError))
	{
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::InvalidCompiledSchema,
			ValidationError);
		return Result;
	}
	if (Blueprint->ParentClass && !Blueprint->ParentClass->HasAnyClassFlags(CLASS_Native))
	{
		const FPaper2DPlusFrameCueTypeDescriptor ParentType =
			DescribeCueType(Blueprint->ParentClass);
		if (ParentType.Availability != EPaper2DPlusFrameCueTypeAvailability::Ready)
		{
			Result.Availability = ParentType.Availability;
			const FText ParentReason = ParentType.Diagnostics.Num() > 0
				? ParentType.Diagnostics[0].Message
				: LOCTEXT(
					"ParentCueTypeUnavailableReason",
					"the parent has not completed its protected compile/save workflow");
			AddDiagnostic(
				Result.Diagnostics,
				EPaper2DPlusFrameCueDiagnosticCode::ParentCueTypeNotReady,
				FText::Format(
					LOCTEXT(
						"ParentCueTypeNotReady",
						"Parent Cue Type '{0}' is not placement-ready: {1}"),
					FText::FromString(Blueprint->ParentClass->GetPathName()),
					ParentReason),
				ParentType.Availability == EPaper2DPlusFrameCueTypeAvailability::NeedsCompile
					|| ParentType.Availability
						== EPaper2DPlusFrameCueTypeAvailability::NeedsDurableSave
					? EPaper2DPlusFrameCueDiagnosticSeverity::Warning
					: EPaper2DPlusFrameCueDiagnosticSeverity::Error);
			return Result;
		}
	}
	// Preserve U8's complete fail-closed envelope (generated behavior state, functions, field flags,
	// CDO identity, recursive specialized parents, and the runtime/editor parity bridge). The U2
	// schema below adds readiness/fingerprinting; it does not replace any existing protection.
	if (!UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
		*Blueprint, &ValidationError, CueClass))
	{
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::InvalidDataOnlyContract,
			ValidationError);
		return Result;
	}

	FPaper2DPlusFrameCueSchema CompiledSchema;
	if (!DescribeSchema(
		*Blueprint,
		EPaper2DPlusFrameCueSchemaSource::Compiled,
		CompiledSchema,
		&ValidationError))
	{
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::InvalidCompiledSchema,
			ValidationError);
		return Result;
	}
	Result.CompiledSchemaFingerprint = CompiledSchema.Fingerprint;
	Result.DurableSchemaFingerprint = Specialized->DurableSchemaFingerprint;
	if (Blueprint->GetOutermost() && Blueprint->GetOutermost()->IsDirty())
	{
		Result.Availability = EPaper2DPlusFrameCueTypeAvailability::NeedsDurableSave;
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::DirtyPackage,
			LOCTEXT(
				"CueTypePackageDirty",
				"The Cue Type package has unsaved changes. Finish a successful save before placement."),
			EPaper2DPlusFrameCueDiagnosticSeverity::Warning);
		return Result;
	}
	// A version 1 stamp stays Ready: it can only describe a payload-only type, whose fingerprint is
	// unchanged under version 2, so nothing about it needs restaging before it can be placed.
	if (!IsAcceptedDurableSchemaVersion(Specialized->DurableSchemaVersion)
		|| (Specialized->DurableSchemaVersion < CurrentSchemaVersion
			&& CompiledSchema.BehaviorEvents.Num() > 0)
		|| Specialized->DurableSchemaFingerprint.IsEmpty())
	{
		Result.Availability = EPaper2DPlusFrameCueTypeAvailability::NeedsDurableSave;
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::DurableSchemaMissing,
			LOCTEXT(
				"CueTypeDurableSchemaMissing",
				"Save the compiled Cue Type through the protected authoring workflow before placement."),
			EPaper2DPlusFrameCueDiagnosticSeverity::Warning);
		return Result;
	}
	if (Specialized->DurableSchemaFingerprint != CompiledSchema.Fingerprint)
	{
		Result.Availability = EPaper2DPlusFrameCueTypeAvailability::NeedsDurableSave;
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::DurableSchemaMismatch,
			LOCTEXT(
				"CueTypeDurableSchemaMismatch",
				"The compiled Cue Type differs from its last durable schema and must be reviewed and saved."),
			EPaper2DPlusFrameCueDiagnosticSeverity::Warning);
		return Result;
	}
	FText DurablePayloadError;
	if (!ValidateDurableSaveMetadataForPersistence(*Specialized, &DurablePayloadError))
	{
		Result.Availability = EPaper2DPlusFrameCueTypeAvailability::NeedsDurableSave;
		AddDiagnostic(
			Result.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::DurableSchemaMissing,
			DurablePayloadError.IsEmpty()
				? LOCTEXT(
					"CueTypeDurableRecoveryPayloadMissing",
					"Save this Cue Type once through the protected authoring workflow to establish its full recovery baseline.")
				: DurablePayloadError,
			EPaper2DPlusFrameCueDiagnosticSeverity::Warning);
		return Result;
	}

	Result.Availability = EPaper2DPlusFrameCueTypeAvailability::Ready;
	return Result;
}

FPaper2DPlusFrameCueTypeDiscoveryResult FPaper2DPlusFrameCueTypeAuthoring::DiscoverCueTypes(
	const bool bIncludeUnloadedBlueprints)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringInternal;
	FPaper2DPlusFrameCueTypeDiscoveryResult Result;
	TMap<FString, UClass*> Candidates;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (ClassifyKind(*It) != EPaper2DPlusFrameCueTypeKind::Invalid)
		{
			Candidates.FindOrAdd(It->GetPathName(), *It);
		}
	}

	if (bIncludeUnloadedBlueprints)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
			TEXT("AssetRegistry")).Get();
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		TArray<FName> BaseClassNames;
		BaseClassNames.Add(UPaper2DPlusCueBase::StaticClass()->GetFName());
		TSet<FName> ExcludedClassNames;
		TSet<FName> DerivedClassNames;
		Registry.GetDerivedClassNames(BaseClassNames, ExcludedClassNames, DerivedClassNames);

		// UE 5.0 returns simple FNames here. Resolve them through each Blueprint asset's exact
		// GeneratedClassPath tag; attempting LoadObject on the simple name cannot cold-load a class.
		TArray<FAssetData> BlueprintAssets;
		Registry.GetAssetsByClass(
			UBlueprint::StaticClass()->GetFName(), BlueprintAssets, true);
		for (const FAssetData& Asset : BlueprintAssets)
		{
			const FAssetTagValueRef GeneratedClassTag =
				Asset.TagsAndValues.FindTag(FBlueprintTags::GeneratedClassPath);
			if (!GeneratedClassTag.IsSet())
			{
				continue;
			}
			const FString GeneratedClassObjectPath = FPackageName::ExportTextPathToObjectPath(
				GeneratedClassTag.AsString());
			const FName GeneratedClassName(
				*FPackageName::ObjectPathToObjectName(GeneratedClassObjectPath));
			if (!DerivedClassNames.Contains(GeneratedClassName)
				|| Candidates.Contains(GeneratedClassObjectPath))
			{
				continue;
			}
			if (UClass* LoadedClass = LoadObject<UClass>(nullptr, *GeneratedClassObjectPath))
			{
				Candidates.Add(LoadedClass->GetPathName(), LoadedClass);
			}
			else
			{
				AddLoadFailure(Result, GeneratedClassObjectPath);
			}
		}
#else
		TArray<FTopLevelAssetPath> BaseClassPaths;
		BaseClassPaths.Add(UPaper2DPlusCueBase::StaticClass()->GetClassPathName());
		TSet<FTopLevelAssetPath> ExcludedClassPaths;
		TSet<FTopLevelAssetPath> DerivedClassPaths;
		Registry.GetDerivedClassNames(BaseClassPaths, ExcludedClassPaths, DerivedClassPaths);
		for (const FTopLevelAssetPath& DerivedClassPath : DerivedClassPaths)
		{
			const FString ObjectPath = DerivedClassPath.ToString();
			if (Candidates.Contains(ObjectPath))
			{
				continue;
			}
			if (UClass* LoadedClass = LoadObject<UClass>(nullptr, *ObjectPath))
			{
				Candidates.Add(LoadedClass->GetPathName(), LoadedClass);
			}
			else
			{
				AddLoadFailure(Result, ObjectPath);
			}
		}
#endif
	}

	for (const TPair<FString, UClass*>& Candidate : Candidates)
	{
		FPaper2DPlusFrameCueTypeDescriptor Description = DescribeCueType(Candidate.Value);
		if (Description.Availability == EPaper2DPlusFrameCueTypeAvailability::Ready)
		{
			Result.Types.Add(MoveTemp(Description));
		}
		else
		{
			Result.RejectedTypes.Add(MoveTemp(Description));
		}
	}
	DisambiguateAndSort(Result.Types);
	DisambiguateAndSort(Result.RejectedTypes);
	return Result;
}

FPaper2DPlusFrameCueTypeCreateResult FPaper2DPlusFrameCueTypeAuthoring::CreateCueType(
	const FPaper2DPlusFrameCueTypeCreateRequest& Request)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringInternal;
	FPaper2DPlusFrameCueTypeCreateResult Result;
	if (!Request.Package)
	{
		Result.Status = EPaper2DPlusFrameCueTypeCreateStatus::InvalidPackage;
		Result.Error = LOCTEXT("CreateCueTypeMissingPackage", "Choose a package for the new Cue Type.");
		return Result;
	}

	FText InvalidNameReason;
	if (Request.AssetName.IsNone()
		|| !Request.AssetName.IsValidXName(INVALID_OBJECTNAME_CHARACTERS, &InvalidNameReason))
	{
		Result.Status = EPaper2DPlusFrameCueTypeCreateStatus::InvalidName;
		Result.Error = InvalidNameReason.IsEmpty()
			? LOCTEXT("CreateCueTypeMissingName", "Choose a valid asset name for the new Cue Type.")
			: InvalidNameReason;
		return Result;
	}

	UClass* ParentClass = nullptr;
	if (Request.Kind == EPaper2DPlusFrameCueTypeKind::Moment)
	{
		ParentClass = UPaper2DPlusCue::StaticClass();
	}
	else if (Request.Kind == EPaper2DPlusFrameCueTypeKind::Range)
	{
		ParentClass = UPaper2DPlusCueState::StaticClass();
	}
	else
	{
		Result.Status = EPaper2DPlusFrameCueTypeCreateStatus::InvalidKind;
		Result.Error = LOCTEXT(
			"CreateCueTypeInvalidKind",
			"Cue Type creation accepts exactly Cue or Cue State.");
		return Result;
	}

	constexpr EObjectFlags ForbiddenCreateFlags =
		RF_Transient
		| RF_ClassDefaultObject
		| RF_ArchetypeObject
		| RF_DefaultSubObject
		| RF_MarkAsRootSet;
	if ((Request.ObjectFlags & ForbiddenCreateFlags) != RF_NoFlags)
	{
		Result.Status = EPaper2DPlusFrameCueTypeCreateStatus::InvalidFlags;
		Result.Error = LOCTEXT(
			"CreateCueTypeInvalidFlags",
			"Cue Type asset flags cannot make the asset transient, an archetype, a default object, or rooted.");
		return Result;
	}

	if (StaticFindObject(
		UObject::StaticClass(), Request.Package, *Request.AssetName.ToString()))
	{
		Result.Status = EPaper2DPlusFrameCueTypeCreateStatus::NameCollision;
		Result.Error = FText::Format(
			LOCTEXT("CreateCueTypeNameCollision", "An object named '{0}' already exists in the package."),
			FText::FromName(Request.AssetName));
		return Result;
	}

	const bool bPackageWasDirty = Request.Package->IsDirty();
	UBlueprint* RawBlueprint = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		Request.Package,
		Request.AssetName,
		BPTYPE_Normal,
		UPaper2DPlusFrameCueBlueprint::StaticClass(),
		UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass(),
		FName(TEXT("Paper2DPlusFrameCueTypeAuthoring")));
	if (!RawBlueprint)
	{
		Request.Package->SetDirtyFlag(bPackageWasDirty);
		Result.Status = EPaper2DPlusFrameCueTypeCreateStatus::CreationFailed;
		Result.Error = LOCTEXT(
			"CreateCueTypeAllocationFailed",
			"Unreal could not allocate the specialized Cue Type Blueprint.");
		return Result;
	}
	RawBlueprint->SetFlags(Request.ObjectFlags | RF_Transactional);

	const auto FailAfterCreation = [
		&Result,
		RawBlueprint,
		&Request,
		bPackageWasDirty](
		const EPaper2DPlusFrameCueTypeCreateStatus FailureStatus,
		const FText& FailureError)
	{
		const bool bCleaned = CleanupCreatedCueType(
			*RawBlueprint,
			*Request.Package,
			Request.AssetName,
			bPackageWasDirty);
		Result.Status = bCleaned
			? FailureStatus
			: EPaper2DPlusFrameCueTypeCreateStatus::CleanupFailed;
		Result.CueType = nullptr;
		Result.Error = bCleaned
			? FailureError
			: FText::Format(
				LOCTEXT(
					"CreateCueTypeCleanupFailed",
					"Cue Type creation failed and its temporary object could not be removed atomically: {0}"),
				FailureError);
		return Result;
	};

	UPaper2DPlusFrameCueBlueprint* Blueprint =
		Cast<UPaper2DPlusFrameCueBlueprint>(RawBlueprint);
	if (!Blueprint)
	{
		return FailAfterCreation(
			EPaper2DPlusFrameCueTypeCreateStatus::CreationFailed,
			LOCTEXT(
				"CreateCueTypeWrongEnvelope",
				"Unreal created the wrong Blueprint asset envelope."));
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (Request.bFailAfterCreateForTests)
	{
		return FailAfterCreation(
			EPaper2DPlusFrameCueTypeCreateStatus::NormalizationFailed,
			LOCTEXT(
				"CreateCueTypeInjectedFailure",
				"Automation injected a post-allocation creation failure."));
	}
#endif

	FText Error;
	if (!NormalizeNewCueType(*Blueprint, Error))
	{
		return FailAfterCreation(
			EPaper2DPlusFrameCueTypeCreateStatus::NormalizationFailed,
			Error);
	}

	FKismetEditorUtilities::CompileBlueprint(
		Blueprint,
		EBlueprintCompileOptions::SkipGarbageCollection);
	// Seeding needs the first compile's skeleton class to resolve the declared events; recompile so
	// the generated class carries the seeded overrides before the envelope is validated.
	if (SeedDeclaredBehaviorEvents(*Blueprint))
	{
		FKismetEditorUtilities::CompileBlueprint(
			Blueprint,
			EBlueprintCompileOptions::SkipGarbageCollection);
	}
	if (Blueprint->Status != BS_UpToDate
		&& Blueprint->Status != BS_UpToDateWithWarnings)
	{
		return FailAfterCreation(
			EPaper2DPlusFrameCueTypeCreateStatus::CompileFailed,
			LOCTEXT(
				"CreateCueTypeCompileFailed",
				"The specialized Cue Type did not complete a clean compile."));
	}
	if (!ApplyCreationDefaults(*Blueprint))
	{
		return FailAfterCreation(
			EPaper2DPlusFrameCueTypeCreateStatus::NormalizationFailed,
			LOCTEXT(
				"CreateCueTypeDefaultsFailed",
				"The new Cue Type's identity and networking defaults could not be written to its class defaults."));
	}

	if (!ValidateCompiledSchemaParity(*Blueprint, &Error)
		|| !UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
			*Blueprint, &Error, Blueprint->GeneratedClass))
	{
		return FailAfterCreation(
			EPaper2DPlusFrameCueTypeCreateStatus::ValidationFailed,
			Error.IsEmpty()
				? LOCTEXT(
					"CreateCueTypeValidationFailed",
					"The compiled Cue Type failed its restricted behavior/payload validation envelope.")
				: Error);
	}

	Blueprint->MarkPackageDirty();
	Result.Status = EPaper2DPlusFrameCueTypeCreateStatus::Succeeded;
	Result.CueType = Blueprint;
	Result.Error = FText::GetEmpty();
	return Result;
}

bool FPaper2DPlusFrameCueTypeAuthoring::DescribeSchema(
	const UBlueprint& Blueprint,
	const EPaper2DPlusFrameCueSchemaSource Source,
	FPaper2DPlusFrameCueSchema& OutSchema,
	FText* OutError)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringInternal;
	OutSchema = FPaper2DPlusFrameCueSchema();
	FText SynchronousBehaviorError;
	if (!ValidateSynchronousBehavior(Blueprint, &SynchronousBehaviorError))
	{
		AddDiagnostic(
			OutSchema.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::DeferredBehavior,
			SynchronousBehaviorError);
		SetError(OutError, SynchronousBehaviorError);
		return false;
	}
	OutSchema.BehaviorEvents = Source == EPaper2DPlusFrameCueSchemaSource::Compiled
		? GetCompiledBehaviorEvents(Blueprint)
		: GetAuthoredBehaviorEvents(Blueprint);
	OutSchema.Version = OutSchema.BehaviorEvents.Num() > 0
		? CurrentSchemaVersion
		: BehaviorFreeSchemaVersion;
	OutSchema.Kind = ClassifyKind(Blueprint.GeneratedClass
		? Blueprint.GeneratedClass
		: Blueprint.ParentClass);
	OutSchema.ParentClassPath = GetDurableSchemaClassPath(Blueprint.ParentClass);
	if (OutSchema.Kind == EPaper2DPlusFrameCueTypeKind::Invalid || !Blueprint.ParentClass)
	{
		const FText Error = LOCTEXT(
			"SchemaInvalidParent",
			"Cue schema requires a Cue or Cue State parent class.");
		AddDiagnostic(
			OutSchema.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::InvalidCompiledSchema,
			Error);
		SetError(OutError, Error);
		return false;
	}

	const UClass* GeneratedClass = Blueprint.GeneratedClass;
	UObject* GeneratedDefaults = GeneratedClass
		? GeneratedClass->GetDefaultObject(false)
		: nullptr;
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	if (Source == EPaper2DPlusFrameCueSchemaSource::Compiled
		&& (!GeneratedClass || !GeneratedDefaults || !K2Schema))
	{
		const FText Error = LOCTEXT(
			"SchemaCompiledStateUnavailable",
			"The generated Cue payload schema/default object is unavailable.");
		AddDiagnostic(
			OutSchema.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::InvalidCompiledSchema,
			Error);
		SetError(OutError, Error);
		return false;
	}
	if (Source == EPaper2DPlusFrameCueSchemaSource::Compiled
		&& !ValidateCompiledSchemaParityAfterSynchronousBehavior(Blueprint, OutError))
	{
		AddDiagnostic(
			OutSchema.Diagnostics,
			EPaper2DPlusFrameCueDiagnosticCode::InvalidCompiledSchema,
			OutError ? *OutError : LOCTEXT("CompiledParityFailed", "Compiled schema parity failed."));
		return false;
	}

	static const FName DelegateCategory(TEXT("delegate"));
	static const FName MulticastDelegateCategory(TEXT("mcdelegate"));
	static const FName BlueprintGetterMetadata(TEXT("BlueprintGetter"));
	static const FName BlueprintSetterMetadata(TEXT("BlueprintSetter"));
	const UPaper2DPlusFrameCueBlueprint* SpecializedBlueprint =
		Cast<UPaper2DPlusFrameCueBlueprint>(&Blueprint);
	FSchemaDependencyCollector DependencyCollector(OutSchema);
	for (const FBPVariableDescription& Variable : Blueprint.NewVariables)
	{
		const bool bInvalidField =
			Variable.VarType.PinCategory == DelegateCategory
			|| Variable.VarType.PinCategory == MulticastDelegateCategory
			|| Variable.HasMetaData(BlueprintGetterMetadata)
			|| Variable.HasMetaData(BlueprintSetterMetadata)
			|| (Variable.PropertyFlags
				& UPaper2DPlusFrameCueBlueprint::GetForbiddenPayloadPropertyFlags()) != 0
			|| (Variable.PropertyFlags & (CPF_Net | CPF_RepNotify)) != 0
			|| !Variable.RepNotifyFunc.IsNone()
			|| Variable.ReplicationCondition != COND_None;
		if (bInvalidField)
		{
			const FText Error = FText::Format(
				LOCTEXT(
					"InvalidSchemaPayloadField",
					"Payload field '{0}' is not durable data-only Cue schema."),
				FText::FromName(Variable.VarName));
			AddDiagnostic(
				OutSchema.Diagnostics,
				EPaper2DPlusFrameCueDiagnosticCode::InvalidPayloadField,
				Error);
			SetError(OutError, Error);
			return false;
		}

		FPaper2DPlusFrameCueSchemaField& Field = OutSchema.Fields.AddDefaulted_GetRef();
		Field.FieldId = Variable.VarGuid;
		Field.Name = Variable.VarName;
		Field.FriendlyName = Variable.FriendlyName;
		Field.Category = Variable.Category.ToString();
		Field.PropertyFlags = Variable.PropertyFlags;
		Field.Container = ConvertContainer(Variable.VarType.ContainerType);
		Field.TypeKey = MakePinTypeKey(Variable.VarType);
		const FString* PendingDefault = Source == EPaper2DPlusFrameCueSchemaSource::Authored
			&& SpecializedBlueprint
			&& SpecializedBlueprint->bHasPendingDefaultProposal
			&& SpecializedBlueprint->bPendingDefaultProposalNeedsCompile
			? SpecializedBlueprint->PendingDefaultProposalValues.Find(Variable.VarName)
			: nullptr;
		Field.DefaultValue = PendingDefault
			? *PendingDefault
			: Variable.DefaultValue.TrimStartAndEnd();
		const bool bHasBufferedCompilerDefault = PendingDefault
			|| !Variable.DefaultValue.IsEmpty();
		if (Source == EPaper2DPlusFrameCueSchemaSource::Authored
			&& !bHasBufferedCompilerDefault
			&& GeneratedClass
			&& GeneratedDefaults
			&& K2Schema)
		{
			const FProperty* ExistingProperty =
				FindFProperty<FProperty>(GeneratedClass, Variable.VarName);
			FEdGraphPinType ExistingType;
			if (ExistingProperty
				&& ExistingProperty->GetOwnerClass() == GeneratedClass
				&& K2Schema->ConvertPropertyToPinType(ExistingProperty, ExistingType)
				&& MakePinTypeKey(ExistingType) == Field.TypeKey)
			{
				// Class-default edits mutate the current CDO before the next compile. Use that
				// value whenever the generated property still represents the authored field.
				Field.DefaultValue = ExportCanonicalValue(
					*ExistingProperty,
					ExistingProperty->ContainerPtrToValuePtr<void>(GeneratedDefaults),
					GeneratedDefaults);
			}
		}
		DependencyCollector.CollectPinType(Variable.VarType);
		for (const FBPVariableMetaDataEntry& VariableMetadata : Variable.MetaDataArray)
		{
			FPaper2DPlusFrameCueSchemaMetadata& Metadata = Field.Metadata.AddDefaulted_GetRef();
			Metadata.Key = VariableMetadata.DataKey;
			Metadata.Value = VariableMetadata.DataValue;
		}

		if (Source == EPaper2DPlusFrameCueSchemaSource::Compiled)
		{
			const FProperty* Property = FindFProperty<FProperty>(GeneratedClass, Variable.VarName);
			check(Property && Property->GetOwnerClass() == GeneratedClass);
			FEdGraphPinType CompiledType;
			check(K2Schema->ConvertPropertyToPinType(Property, CompiledType));
			Field.TypeKey = MakePinTypeKey(CompiledType);
			Field.Container = ConvertContainer(CompiledType.ContainerType);
			Field.DefaultValue = ExportCanonicalValue(
				*Property,
				Property->ContainerPtrToValuePtr<void>(GeneratedDefaults),
				GeneratedDefaults);
			DependencyCollector.CollectProperty(*Property);
		}
	}

	if (GeneratedClass && GeneratedDefaults && K2Schema)
	{
		for (TFieldIterator<FProperty> It(
			GeneratedClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			const UClass* OwnerClass = Property->GetOwnerClass();
			if (!OwnerClass
				|| OwnerClass == GeneratedClass
				|| !OwnerClass->IsChildOf(UPaper2DPlusCueBase::StaticClass())
				|| IsPlacementTimingProperty(Property)
				|| Property->HasAnyPropertyFlags(CPF_EditorOnly | CPF_Transient))
			{
				continue;
			}
			FEdGraphPinType PropertyType;
			if (!K2Schema->ConvertPropertyToPinType(Property, PropertyType))
			{
				continue;
			}
			FPaper2DPlusFrameCueInheritedDefault& Default =
				OutSchema.InheritedDefaults.AddDefaulted_GetRef();
			Default.OwnerClassPath = GetDurableSchemaClassPath(OwnerClass);
			Default.Name = Property->GetFName();
			Default.TypeKey = MakePinTypeKey(PropertyType);
			Default.DefaultValue = ExportCanonicalValue(
				*Property,
				Property->ContainerPtrToValuePtr<void>(GeneratedDefaults),
				GeneratedDefaults);
			DependencyCollector.CollectProperty(*Property);
		}
	}

	if (Blueprint.ParentClass && !Blueprint.ParentClass->HasAnyClassFlags(CLASS_Native))
	{
		if (const UPaper2DPlusFrameCueBlueprint* ParentBlueprint =
			Cast<UPaper2DPlusFrameCueBlueprint>(Blueprint.ParentClass->ClassGeneratedBy))
		{
			FPaper2DPlusFrameCueSchemaDependency& Dependency =
				OutSchema.Dependencies.AddDefaulted_GetRef();
			Dependency.Kind = EPaper2DPlusFrameCueSchemaDependencyKind::ParentCueType;
			Dependency.ObjectPath = FSoftObjectPath(Blueprint.ParentClass);
			Dependency.Fingerprint = ParentBlueprint->DurableSchemaFingerprint;
		}
	}

	OutSchema.CanonicalText = BuildCanonicalSchema(OutSchema);
	OutSchema.Fingerprint = HashCanonicalText(OutSchema.CanonicalText);
	OutSchema.bValid = true;
	if (OutError)
	{
		*OutError = FText::GetEmpty();
	}
	return true;
}

FPaper2DPlusFrameCueSchemaDiff FPaper2DPlusFrameCueTypeAuthoring::DiffSchemas(
	const FPaper2DPlusFrameCueSchema& InputBaseline,
	const FPaper2DPlusFrameCueSchema& InputCandidate)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringInternal;
	FPaper2DPlusFrameCueSchema Baseline = InputBaseline;
	FPaper2DPlusFrameCueSchema Candidate = InputCandidate;
	NormalizeDurableSchema(Baseline);
	NormalizeDurableSchema(Candidate);
	FPaper2DPlusFrameCueSchemaDiff Diff;
	// A payload-only baseline stays at version 1 and only rises to 2 when behavior is added, so the
	// version pair differing across the accepted range is an ordinary behavior edit, not corruption.
	if (!IsAcceptedDurableSchemaVersion(Baseline.Version)
		|| !IsAcceptedDurableSchemaVersion(Candidate.Version)
		|| Baseline.Kind == EPaper2DPlusFrameCueTypeKind::Invalid
		|| Candidate.Kind == EPaper2DPlusFrameCueTypeKind::Invalid)
	{
		AddChange(
			Diff,
			EPaper2DPlusFrameCueSchemaChangeKind::InvalidSchema,
			nullptr,
			nullptr,
			LOCTEXT("InvalidSchemaDiff", "The schema version or Cue kind is invalid."),
			EPaper2DPlusFrameCueSchemaCompatibility::Invalid);
		return Diff;
	}
	if (Baseline.Kind != Candidate.Kind)
	{
		AddChange(
			Diff,
			EPaper2DPlusFrameCueSchemaChangeKind::KindChanged,
			nullptr,
			nullptr,
			LOCTEXT(
				"CueKindChanged",
				"Changing between the Cue and Cue State lifecycles is destructive."),
			EPaper2DPlusFrameCueSchemaCompatibility::Destructive);
	}
	if (Baseline.ParentClassPath != Candidate.ParentClassPath)
	{
		AddChange(
			Diff,
			EPaper2DPlusFrameCueSchemaChangeKind::ParentChanged,
			nullptr,
			nullptr,
			LOCTEXT("CueParentChanged", "Changing the Cue parent class is destructive."),
			EPaper2DPlusFrameCueSchemaCompatibility::Destructive);
	}
	if (Baseline.BehaviorEvents != Candidate.BehaviorEvents)
	{
		// Implementing or removing a Cue event changes no serialized placement data, so it needs no
		// destructive confirmation; it is still reported so the save review names what changed.
		AddChange(
			Diff,
			EPaper2DPlusFrameCueSchemaChangeKind::BehaviorChanged,
			nullptr,
			nullptr,
			LOCTEXT(
				"CueBehaviorChanged",
				"The implemented Cue behavior events changed."),
			EPaper2DPlusFrameCueSchemaCompatibility::Compatible);
	}

	TSet<int32> MatchedCandidateFields;
	for (const FPaper2DPlusFrameCueSchemaField& OldField : Baseline.Fields)
	{
		int32 NewIndex = INDEX_NONE;
		if (OldField.FieldId.IsValid())
		{
			NewIndex = Candidate.Fields.IndexOfByPredicate(
				[&OldField](const FPaper2DPlusFrameCueSchemaField& Field)
				{
					return Field.FieldId == OldField.FieldId;
				});
		}
		else
		{
			NewIndex = Candidate.Fields.IndexOfByPredicate(
				[&OldField](const FPaper2DPlusFrameCueSchemaField& Field)
				{
					return !Field.FieldId.IsValid() && Field.Name == OldField.Name;
				});
		}
		if (NewIndex == INDEX_NONE)
		{
			AddChange(
				Diff,
				EPaper2DPlusFrameCueSchemaChangeKind::FieldRemoved,
				&OldField,
				nullptr,
				FText::Format(
					LOCTEXT("CueFieldRemoved", "Field '{0}' was removed."),
					FText::FromName(OldField.Name)),
				EPaper2DPlusFrameCueSchemaCompatibility::Destructive);
			continue;
		}

		MatchedCandidateFields.Add(NewIndex);
		const FPaper2DPlusFrameCueSchemaField& NewField = Candidate.Fields[NewIndex];
		if (OldField.Name != NewField.Name)
		{
			AddChange(
				Diff,
				EPaper2DPlusFrameCueSchemaChangeKind::FieldRenamed,
				&OldField,
				&NewField,
				FText::Format(
					LOCTEXT("CueFieldRenamed", "Field '{0}' was renamed to '{1}' with the same GUID."),
					FText::FromName(OldField.Name),
					FText::FromName(NewField.Name)),
				EPaper2DPlusFrameCueSchemaCompatibility::Conditional);
		}
		if (OldField.Container != NewField.Container)
		{
			AddChange(
				Diff,
				EPaper2DPlusFrameCueSchemaChangeKind::FieldContainerChanged,
				&OldField,
				&NewField,
				FText::Format(
					LOCTEXT("CueFieldContainerChanged", "Field '{0}' changed container shape."),
					FText::FromName(OldField.Name)),
				EPaper2DPlusFrameCueSchemaCompatibility::Destructive);
		}
		if (OldField.TypeKey != NewField.TypeKey)
		{
			AddChange(
				Diff,
				EPaper2DPlusFrameCueSchemaChangeKind::FieldTypeChanged,
				&OldField,
				&NewField,
				FText::Format(
					LOCTEXT("CueFieldTypeChanged", "Field '{0}' changed type."),
					FText::FromName(OldField.Name)),
				EPaper2DPlusFrameCueSchemaCompatibility::Destructive);
		}
		if (OldField.DefaultValue != NewField.DefaultValue)
		{
			AddChange(
				Diff,
				EPaper2DPlusFrameCueSchemaChangeKind::FieldDefaultChanged,
				&OldField,
				&NewField,
				FText::Format(
					LOCTEXT("CueFieldDefaultChanged", "Field '{0}' changed its class default."),
					FText::FromName(OldField.Name)),
				EPaper2DPlusFrameCueSchemaCompatibility::Compatible);
		}
		if (OldField.PropertyFlags != NewField.PropertyFlags)
		{
			AddChange(
				Diff,
				EPaper2DPlusFrameCueSchemaChangeKind::FieldFlagsChanged,
				&OldField,
				&NewField,
				FText::Format(
					LOCTEXT("CueFieldFlagsChanged", "Field '{0}' changed presentation/access flags."),
					FText::FromName(OldField.Name)),
				EPaper2DPlusFrameCueSchemaCompatibility::Compatible);
		}
		if (!MetadataEqual(OldField.Metadata, NewField.Metadata)
			|| OldField.FriendlyName != NewField.FriendlyName
			|| OldField.Category != NewField.Category)
		{
			AddChange(
				Diff,
				EPaper2DPlusFrameCueSchemaChangeKind::FieldMetadataChanged,
				&OldField,
				&NewField,
				FText::Format(
					LOCTEXT("CueFieldMetadataChanged", "Field '{0}' changed presentation metadata."),
					FText::FromName(OldField.Name)),
				EPaper2DPlusFrameCueSchemaCompatibility::Compatible);
		}
	}

	for (int32 Index = 0; Index < Candidate.Fields.Num(); ++Index)
	{
		if (!MatchedCandidateFields.Contains(Index))
		{
			const FPaper2DPlusFrameCueSchemaField& NewField = Candidate.Fields[Index];
			AddChange(
				Diff,
				EPaper2DPlusFrameCueSchemaChangeKind::FieldAdded,
				nullptr,
				&NewField,
				FText::Format(
					LOCTEXT("CueFieldAdded", "Serializable field '{0}' was added."),
					FText::FromName(NewField.Name)),
				EPaper2DPlusFrameCueSchemaCompatibility::Compatible);
		}
	}

	TMap<FString, const FPaper2DPlusFrameCueInheritedDefault*> BaselineInheritedDefaults;
	TMap<FString, const FPaper2DPlusFrameCueInheritedDefault*> CandidateInheritedDefaults;
	for (const FPaper2DPlusFrameCueInheritedDefault& Default : Baseline.InheritedDefaults)
	{
		BaselineInheritedDefaults.Add(InheritedDefaultKey(Default), &Default);
	}
	for (const FPaper2DPlusFrameCueInheritedDefault& Default : Candidate.InheritedDefaults)
	{
		CandidateInheritedDefaults.Add(InheritedDefaultKey(Default), &Default);
	}
	TSet<FString> InheritedDefaultKeys;
	BaselineInheritedDefaults.GetKeys(InheritedDefaultKeys);
	for (const TPair<FString, const FPaper2DPlusFrameCueInheritedDefault*>& Pair :
		CandidateInheritedDefaults)
	{
		InheritedDefaultKeys.Add(Pair.Key);
	}
	for (const FString& Key : InheritedDefaultKeys)
	{
		const FPaper2DPlusFrameCueInheritedDefault* const* OldDefault =
			BaselineInheritedDefaults.Find(Key);
		const FPaper2DPlusFrameCueInheritedDefault* const* NewDefault =
			CandidateInheritedDefaults.Find(Key);
		if (!OldDefault || !NewDefault)
		{
			AddChange(
				Diff,
				EPaper2DPlusFrameCueSchemaChangeKind::InheritedDefaultChanged,
				nullptr,
				nullptr,
				FText::Format(
					LOCTEXT(
						"CueInheritedFieldChanged",
						"Inherited Cue field '{0}' was added or removed."),
					FText::FromString(Key)),
				EPaper2DPlusFrameCueSchemaCompatibility::Destructive);
			continue;
		}
		if ((*OldDefault)->TypeKey != (*NewDefault)->TypeKey)
		{
			AddChange(
				Diff,
				EPaper2DPlusFrameCueSchemaChangeKind::InheritedDefaultChanged,
				nullptr,
				nullptr,
				FText::Format(
					LOCTEXT(
						"CueInheritedFieldTypeChanged",
						"Inherited Cue field '{0}' changed type."),
					FText::FromString(Key)),
				EPaper2DPlusFrameCueSchemaCompatibility::Destructive);
			continue;
		}
		if ((*OldDefault)->DefaultValue != (*NewDefault)->DefaultValue)
		{
			AddChange(
				Diff,
				EPaper2DPlusFrameCueSchemaChangeKind::InheritedDefaultChanged,
				nullptr,
				nullptr,
				FText::Format(
					LOCTEXT(
						"CueInheritedFieldDefaultChanged",
						"Inherited Cue field '{0}' changed its class default."),
					FText::FromString(Key)),
				EPaper2DPlusFrameCueSchemaCompatibility::Compatible);
		}
	}

	TMap<FString, FString> BaselineDependencies;
	TMap<FString, FString> CandidateDependencies;
	for (const FPaper2DPlusFrameCueSchemaDependency& Dependency : Baseline.Dependencies)
	{
		BaselineDependencies.Add(DependencyKey(Dependency), Dependency.Fingerprint);
	}
	for (const FPaper2DPlusFrameCueSchemaDependency& Dependency : Candidate.Dependencies)
	{
		CandidateDependencies.Add(DependencyKey(Dependency), Dependency.Fingerprint);
	}
	TSet<FString> DependencyKeys;
	BaselineDependencies.GetKeys(DependencyKeys);
	for (const TPair<FString, FString>& Pair : CandidateDependencies)
	{
		DependencyKeys.Add(Pair.Key);
	}
	for (const FString& Key : DependencyKeys)
	{
		const FString* OldFingerprint = BaselineDependencies.Find(Key);
		const FString* NewFingerprint = CandidateDependencies.Find(Key);
		if (!OldFingerprint || !NewFingerprint || *OldFingerprint != *NewFingerprint)
		{
			AddChange(
				Diff,
				EPaper2DPlusFrameCueSchemaChangeKind::DependencyChanged,
				nullptr,
				nullptr,
				FText::Format(
					LOCTEXT("CueDependencyChanged", "Referenced schema '{0}' changed and needs proof."),
					FText::FromString(Key)),
				EPaper2DPlusFrameCueSchemaCompatibility::DependencyChange);
		}
	}
	return Diff;
}

bool FPaper2DPlusFrameCueTypeAuthoring::SerializeSchemaSnapshot(
	const FPaper2DPlusFrameCueSchema& Schema,
	FString& OutSnapshot,
	FText* OutError)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringInternal;
	OutSnapshot.Reset();
	if (!Schema.bValid)
	{
		SetError(OutError, LOCTEXT(
			"CannotSerializeInvalidCueSchema",
			"An invalid Cue Type schema cannot become the durable baseline."));
		return false;
	}

	FPaper2DPlusFrameCueSchema Normalized = Schema;
	Normalized.CanonicalText = BuildCanonicalSchema(Normalized);
	Normalized.Fingerprint = HashCanonicalText(Normalized.CanonicalText);
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), Normalized.Version);
	Root->SetNumberField(TEXT("kind"), static_cast<int32>(Normalized.Kind));
	Root->SetStringField(TEXT("parent"), Normalized.ParentClassPath.ToString());
	Root->SetStringField(TEXT("fingerprint"), Normalized.Fingerprint);
	// Written only when behavior exists, so a payload-only snapshot stays byte-identical to the one
	// a version 1 build produced and an existing asset never fails its staged-baseline comparison.
	if (Normalized.BehaviorEvents.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> BehaviorEvents;
		for (const FName& BehaviorEvent : Normalized.BehaviorEvents)
		{
			BehaviorEvents.Add(MakeShared<FJsonValueString>(BehaviorEvent.ToString()));
		}
		Root->SetArrayField(TEXT("behaviors"), BehaviorEvents);
	}

	TArray<TSharedPtr<FJsonValue>> Fields;
	for (const FPaper2DPlusFrameCueSchemaField& Field : Normalized.Fields)
	{
		TSharedRef<FJsonObject> JsonField = MakeShared<FJsonObject>();
		JsonField->SetStringField(TEXT("id"), Field.FieldId.ToString(EGuidFormats::Digits));
		JsonField->SetStringField(TEXT("name"), Field.Name.ToString());
		JsonField->SetStringField(TEXT("friendly"), Field.FriendlyName);
		JsonField->SetStringField(TEXT("category"), Field.Category);
		JsonField->SetStringField(TEXT("type"), Field.TypeKey);
		JsonField->SetNumberField(TEXT("container"), static_cast<int32>(Field.Container));
		JsonField->SetStringField(
			TEXT("flags"),
			FString::Printf(TEXT("%llu"), static_cast<unsigned long long>(Field.PropertyFlags)));
		JsonField->SetStringField(TEXT("default"), Field.DefaultValue);
		TArray<TSharedPtr<FJsonValue>> Metadata;
		for (const FPaper2DPlusFrameCueSchemaMetadata& Entry : Field.Metadata)
		{
			TSharedRef<FJsonObject> JsonMetadata = MakeShared<FJsonObject>();
			JsonMetadata->SetStringField(TEXT("key"), Entry.Key.ToString());
			JsonMetadata->SetStringField(TEXT("value"), Entry.Value);
			Metadata.Add(MakeShared<FJsonValueObject>(JsonMetadata));
		}
		JsonField->SetArrayField(TEXT("metadata"), Metadata);
		Fields.Add(MakeShared<FJsonValueObject>(JsonField));
	}
	Root->SetArrayField(TEXT("fields"), Fields);

	TArray<TSharedPtr<FJsonValue>> Defaults;
	for (const FPaper2DPlusFrameCueInheritedDefault& Default : Normalized.InheritedDefaults)
	{
		TSharedRef<FJsonObject> JsonDefault = MakeShared<FJsonObject>();
		JsonDefault->SetStringField(TEXT("owner"), Default.OwnerClassPath.ToString());
		JsonDefault->SetStringField(TEXT("name"), Default.Name.ToString());
		JsonDefault->SetStringField(TEXT("type"), Default.TypeKey);
		JsonDefault->SetStringField(TEXT("value"), Default.DefaultValue);
		Defaults.Add(MakeShared<FJsonValueObject>(JsonDefault));
	}
	Root->SetArrayField(TEXT("inherited_defaults"), Defaults);

	TArray<TSharedPtr<FJsonValue>> Dependencies;
	for (const FPaper2DPlusFrameCueSchemaDependency& Dependency : Normalized.Dependencies)
	{
		TSharedRef<FJsonObject> JsonDependency = MakeShared<FJsonObject>();
		JsonDependency->SetNumberField(TEXT("kind"), static_cast<int32>(Dependency.Kind));
		JsonDependency->SetStringField(TEXT("path"), Dependency.ObjectPath.ToString());
		JsonDependency->SetStringField(TEXT("fingerprint"), Dependency.Fingerprint);
		Dependencies.Add(MakeShared<FJsonValueObject>(JsonDependency));
	}
	Root->SetArrayField(TEXT("dependencies"), Dependencies);

	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutSnapshot);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		SetError(OutError, LOCTEXT(
			"CueSchemaSnapshotSerializeFailed",
			"The Cue Type durable schema snapshot could not be serialized."));
		return false;
	}
	SetError(OutError, FText::GetEmpty());
	return true;
}

bool FPaper2DPlusFrameCueTypeAuthoring::DeserializeSchemaSnapshot(
	const FString& Snapshot,
	FPaper2DPlusFrameCueSchema& OutSchema,
	FText* OutError)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringInternal;
	OutSchema = FPaper2DPlusFrameCueSchema();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Snapshot);
	if (Snapshot.IsEmpty() || !FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		SetError(OutError, LOCTEXT(
			"CueSchemaSnapshotParseFailed",
			"The Cue Type has no readable durable schema baseline. Save it once without schema edits before evolving its payload."));
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* FieldValues = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* DefaultValues = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* DependencyValues = nullptr;
	if (!Root->HasField(TEXT("version"))
		|| !Root->HasField(TEXT("kind"))
		|| !Root->HasField(TEXT("parent"))
		|| !Root->HasField(TEXT("fingerprint"))
		|| !Root->TryGetArrayField(TEXT("fields"), FieldValues)
		|| !Root->TryGetArrayField(TEXT("inherited_defaults"), DefaultValues)
		|| !Root->TryGetArrayField(TEXT("dependencies"), DependencyValues))
	{
		SetError(OutError, LOCTEXT(
			"CueSchemaSnapshotIncomplete",
			"The Cue Type durable schema baseline is incomplete and cannot authorize compilation."));
		return false;
	}

	OutSchema.Version = static_cast<int32>(Root->GetNumberField(TEXT("version")));
	OutSchema.Kind = static_cast<EPaper2DPlusFrameCueTypeKind>(
		static_cast<int32>(Root->GetNumberField(TEXT("kind"))));
	OutSchema.ParentClassPath = FSoftObjectPath(Root->GetStringField(TEXT("parent")));
	const FString ExpectedFingerprint = Root->GetStringField(TEXT("fingerprint"));
	const TArray<TSharedPtr<FJsonValue>>* BehaviorValues = nullptr;
	if (Root->TryGetArrayField(TEXT("behaviors"), BehaviorValues) && BehaviorValues)
	{
		for (const TSharedPtr<FJsonValue>& Value : *BehaviorValues)
		{
			FString BehaviorName;
			if (!Value.IsValid() || !Value->TryGetString(BehaviorName))
			{
				SetError(OutError, LOCTEXT(
					"CueSchemaSnapshotBadBehavior",
					"The durable Cue schema contains an invalid behavior record."));
				return false;
			}
			OutSchema.BehaviorEvents.Add(FName(*BehaviorName));
		}
	}
	for (const TSharedPtr<FJsonValue>& Value : *FieldValues)
	{
		const TSharedPtr<FJsonObject> JsonField = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!JsonField.IsValid())
		{
			SetError(OutError, LOCTEXT("CueSchemaSnapshotBadField", "The durable Cue schema contains an invalid field record."));
			return false;
		}
		FPaper2DPlusFrameCueSchemaField& Field = OutSchema.Fields.AddDefaulted_GetRef();
		FGuid::Parse(JsonField->GetStringField(TEXT("id")), Field.FieldId);
		Field.Name = FName(*JsonField->GetStringField(TEXT("name")));
		Field.FriendlyName = JsonField->GetStringField(TEXT("friendly"));
		Field.Category = JsonField->GetStringField(TEXT("category"));
		Field.TypeKey = JsonField->GetStringField(TEXT("type"));
		Field.Container = static_cast<EPaper2DPlusFrameCueSchemaContainer>(
			static_cast<int32>(JsonField->GetNumberField(TEXT("container"))));
		Field.PropertyFlags = FCString::Strtoui64(
			*JsonField->GetStringField(TEXT("flags")), nullptr, 10);
		Field.DefaultValue = JsonField->GetStringField(TEXT("default"));
		const TArray<TSharedPtr<FJsonValue>>* MetadataValues = nullptr;
		if (!JsonField->TryGetArrayField(TEXT("metadata"), MetadataValues))
		{
			SetError(OutError, LOCTEXT("CueSchemaSnapshotBadMetadata", "The durable Cue schema contains incomplete field metadata."));
			return false;
		}
		for (const TSharedPtr<FJsonValue>& MetadataValue : *MetadataValues)
		{
			const TSharedPtr<FJsonObject> JsonMetadata = MetadataValue.IsValid()
				? MetadataValue->AsObject()
				: nullptr;
			if (!JsonMetadata.IsValid())
			{
				return false;
			}
			FPaper2DPlusFrameCueSchemaMetadata& Metadata = Field.Metadata.AddDefaulted_GetRef();
			Metadata.Key = FName(*JsonMetadata->GetStringField(TEXT("key")));
			Metadata.Value = JsonMetadata->GetStringField(TEXT("value"));
		}
	}

	for (const TSharedPtr<FJsonValue>& Value : *DefaultValues)
	{
		const TSharedPtr<FJsonObject> JsonDefault = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!JsonDefault.IsValid())
		{
			return false;
		}
		FPaper2DPlusFrameCueInheritedDefault& Default =
			OutSchema.InheritedDefaults.AddDefaulted_GetRef();
		Default.OwnerClassPath = FSoftObjectPath(JsonDefault->GetStringField(TEXT("owner")));
		Default.Name = FName(*JsonDefault->GetStringField(TEXT("name")));
		Default.TypeKey = JsonDefault->GetStringField(TEXT("type"));
		Default.DefaultValue = JsonDefault->GetStringField(TEXT("value"));
	}

	for (const TSharedPtr<FJsonValue>& Value : *DependencyValues)
	{
		const TSharedPtr<FJsonObject> JsonDependency = Value.IsValid()
			? Value->AsObject()
			: nullptr;
		if (!JsonDependency.IsValid())
		{
			return false;
		}
		FPaper2DPlusFrameCueSchemaDependency& Dependency =
			OutSchema.Dependencies.AddDefaulted_GetRef();
		Dependency.Kind = static_cast<EPaper2DPlusFrameCueSchemaDependencyKind>(
			static_cast<int32>(JsonDependency->GetNumberField(TEXT("kind"))));
		Dependency.ObjectPath = FSoftObjectPath(JsonDependency->GetStringField(TEXT("path")));
		Dependency.Fingerprint = JsonDependency->GetStringField(TEXT("fingerprint"));
	}

	OutSchema.CanonicalText = BuildCanonicalSchema(OutSchema);
	OutSchema.Fingerprint = HashCanonicalText(OutSchema.CanonicalText);
	OutSchema.bValid = IsAcceptedDurableSchemaVersion(OutSchema.Version)
		&& OutSchema.Kind != EPaper2DPlusFrameCueTypeKind::Invalid
		&& OutSchema.Fingerprint == ExpectedFingerprint;
	if (!OutSchema.bValid)
	{
		SetError(OutError, LOCTEXT(
			"CueSchemaSnapshotFingerprintMismatch",
			"The durable Cue schema baseline is stale or corrupt and cannot authorize compilation."));
		return false;
	}
	SetError(OutError, FText::GetEmpty());
	return true;
}

bool FPaper2DPlusFrameCueTypeAuthoring::CaptureDefaultValues(
	const UBlueprint& Blueprint,
	TMap<FName, FString>& OutValues,
	FText* OutError)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringInternal;
	OutValues.Reset();
	UClass* GeneratedClass = Blueprint.GeneratedClass;
	UObject* Defaults = GeneratedClass ? GeneratedClass->GetDefaultObject(false) : nullptr;
	if (!GeneratedClass || !Defaults)
	{
		SetError(OutError, LOCTEXT("CueDefaultsUnavailable", "The generated Cue Type defaults are unavailable."));
		return false;
	}
	for (TFieldIterator<FProperty> It(GeneratedClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		const FProperty* Property = *It;
		if (IsPlacementTimingProperty(Property)
			|| Property->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly))
		{
			continue;
		}
		FString Value;
#if UE_VERSION_OLDER_THAN(5, 5, 0)
		Property->ExportTextItem(
			Value,
			Property->ContainerPtrToValuePtr<void>(Defaults),
			nullptr,
			Defaults,
			PPF_Copy);
#else
		Property->ExportTextItem_Direct(
			Value,
			Property->ContainerPtrToValuePtr<void>(Defaults),
			nullptr,
			Defaults,
			PPF_Copy);
#endif
		OutValues.Add(Property->GetFName(), MoveTemp(Value));
	}
	SetError(OutError, FText::GetEmpty());
	return true;
}

bool FPaper2DPlusFrameCueTypeAuthoring::ApplyDefaultValues(
	UBlueprint& Blueprint,
	const TMap<FName, FString>& Values,
	FText* OutError,
	bool bRequireEveryProperty)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringInternal;
	UClass* GeneratedClass = Blueprint.GeneratedClass;
	UObject* Defaults = GeneratedClass ? GeneratedClass->GetDefaultObject(false) : nullptr;
	if (!GeneratedClass || !Defaults)
	{
		SetError(OutError, LOCTEXT("CueDefaultsRestoreUnavailable", "The generated Cue Type defaults cannot be restored."));
		return false;
	}
	Defaults->Modify();
	for (const TPair<FName, FString>& Pair : Values)
	{
		FProperty* Property = FindFProperty<FProperty>(GeneratedClass, Pair.Key);
		if (!Property)
		{
			if (bRequireEveryProperty)
			{
				SetError(OutError, FText::Format(
					LOCTEXT(
						"CueDefaultRestorePropertyMissing",
						"Cue field '{0}' is missing from the generated class, so its default value cannot be restored safely."),
					FText::FromName(Pair.Key)));
				return false;
			}
			continue;
		}
		if (IsPlacementTimingProperty(Property))
		{
			continue;
		}
		void* Destination = Property->ContainerPtrToValuePtr<void>(Defaults);
#if UE_VERSION_OLDER_THAN(5, 5, 0)
		const TCHAR* End = Property->ImportText(*Pair.Value, Destination, PPF_Copy, Defaults);
#else
		const TCHAR* End = Property->ImportText_Direct(*Pair.Value, Destination, Defaults, PPF_Copy);
#endif
		if (!End)
		{
			SetError(OutError, FText::Format(
				LOCTEXT("CueDefaultRestoreFailed", "Default value for Cue field '{0}' could not be restored."),
				FText::FromName(Pair.Key)));
			return false;
		}
	}
	SetError(OutError, FText::GetEmpty());
	return true;
}

FPaper2DPlusFrameCueSchemaPreflight FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(
	const UBlueprint& Blueprint)
{
	FPaper2DPlusFrameCueSchemaPreflight Result;
	Result.Type = DescribeCueType(Blueprint.GeneratedClass);
	FText SchemaError;
	const bool bHasCurrentSchema = DescribeSchema(
		Blueprint,
		EPaper2DPlusFrameCueSchemaSource::Authored,
		Result.CurrentSchema,
		&SchemaError);
	if (!bHasCurrentSchema)
	{
		Result.CurrentSchemaError = SchemaError.IsEmpty()
			? LOCTEXT(
				"PreflightAuthoredSchemaIndescribable",
				"The authored payload schema could not be described.")
			: SchemaError;
	}

	if (const UPaper2DPlusFrameCueBlueprint* Specialized =
		Cast<UPaper2DPlusFrameCueBlueprint>(&Blueprint))
	{
		FText BaselineError;
		Result.bHasDurableBaseline = DeserializeSchemaSnapshot(
			Specialized->DurableSchemaSnapshot,
			Result.DurableSchema,
			&BaselineError);
		if (Result.bHasDurableBaseline
			&& Result.DurableSchema.Fingerprint != Specialized->DurableSchemaFingerprint)
		{
			BaselineError = LOCTEXT(
				"PreflightBaselineFingerprintMismatch",
				"The stored durable schema snapshot does not match the asset's durable fingerprint.");
		}
		Result.bHasDurableBaseline = Result.bHasDurableBaseline
			&& Result.DurableSchema.Fingerprint
				== Specialized->DurableSchemaFingerprint;
		if (!Result.bHasDurableBaseline && !Specialized->DurableSchemaFingerprint.IsEmpty())
		{
			Result.DurableBaselineError = BaselineError.IsEmpty()
				? LOCTEXT(
					"PreflightBaselineUnavailable",
					"The stored durable schema baseline could not be read.")
				: BaselineError;
		}
		if (Result.bHasDurableBaseline && bHasCurrentSchema)
		{
			Result.Diff = DiffSchemas(Result.DurableSchema, Result.CurrentSchema);
		}
		else if (Specialized->DurableSchemaFingerprint.IsEmpty() && bHasCurrentSchema)
		{
			// A never-saved specialized asset has no dependent durable payload to protect.
			Result.Diff.Compatibility = EPaper2DPlusFrameCueSchemaCompatibility::Compatible;
		}
		else
		{
			Result.Diff.Compatibility = EPaper2DPlusFrameCueSchemaCompatibility::Invalid;
		}
	}
	else
	{
		// Generic legacy assets have no specialized durable snapshot. Their restricted recovery
		// surface remains readable, but schema compilation cannot silently establish a baseline.
		Result.Diff.Compatibility = EPaper2DPlusFrameCueSchemaCompatibility::Invalid;
	}

	Result.bRequiresDestructiveConfirmation =
		Result.Diff.Compatibility == EPaper2DPlusFrameCueSchemaCompatibility::Conditional
		|| Result.Diff.Compatibility == EPaper2DPlusFrameCueSchemaCompatibility::DependencyChange
		|| Result.Diff.Compatibility == EPaper2DPlusFrameCueSchemaCompatibility::Destructive;
	Result.bCanCompile = bHasCurrentSchema
		&& Result.Diff.Compatibility != EPaper2DPlusFrameCueSchemaCompatibility::Invalid;
	Result.bCanPlace = Result.Type.Availability
		== EPaper2DPlusFrameCueTypeAvailability::Ready;
	Result.bCanPersist = Result.bCanCompile && Result.CurrentSchema.bValid;
	return Result;
}

FPaper2DPlusFrameCueDurableSchemaCandidate
FPaper2DPlusFrameCueTypeAuthoring::PrepareDurableSchemaSaveCandidate(
	UPaper2DPlusFrameCueBlueprint& Blueprint)
{
	FPaper2DPlusFrameCueDurableSchemaCandidate Result;
	FText Error;
	if (!ValidateCompiledSchemaParity(Blueprint, &Error)
		|| !UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
			Blueprint, &Error, Blueprint.GeneratedClass))
	{
		Result.Error = Error.IsEmpty()
			? LOCTEXT(
				"DurableCandidateCompiledValidationFailed",
				"The Cue Type is not a valid compiled save candidate.")
			: Error;
		return Result;
	}

	FPaper2DPlusFrameCueSchema Schema;
	if (!DescribeSchema(
		Blueprint,
		EPaper2DPlusFrameCueSchemaSource::Compiled,
		Schema,
		&Error))
	{
		Result.Error = Error.IsEmpty()
			? LOCTEXT(
				"DurableCandidateSchemaFailed",
				"The compiled Cue Type schema could not be fingerprinted.")
			: Error;
		return Result;
	}
	Result.Version = Schema.Version;
	if (!SerializeSchemaSnapshot(Schema, Result.SchemaSnapshot, &Error))
	{
		Result.Error = Error;
		return Result;
	}
	TMap<FName, FString> DefaultValues;
	if (!CaptureDefaultValues(Blueprint, DefaultValues, &Error))
	{
		Result.Error = Error;
		return Result;
	}

	UPackage* Package = Blueprint.GetOutermost();
	if (!Package)
	{
		Result.Error = LOCTEXT(
			"DurableCandidateMissingPackage",
			"The Cue Type has no package to save.");
		return Result;
	}

	const int32 PreviousVersion = Blueprint.DurableSchemaVersion;
	const FString PreviousFingerprint = Blueprint.DurableSchemaFingerprint;
	const FString PreviousSnapshot = Blueprint.DurableSchemaSnapshot;
	const TArray<FBPVariableDescription> PreviousVariables = Blueprint.DurableVariables;
	const TMap<FName, FString> PreviousDefaults = Blueprint.DurableDefaultValues;
	const bool bPackageWasDirty = Package->IsDirty();
	Blueprint.DurableSchemaVersion = Result.Version;
	Blueprint.DurableSchemaFingerprint = Schema.Fingerprint;
	Blueprint.DurableSchemaSnapshot = Result.SchemaSnapshot;
	Blueprint.DurableVariables = Blueprint.NewVariables;
	Blueprint.DurableDefaultValues = MoveTemp(DefaultValues);
	Blueprint.MarkPackageDirty();
	if (!Package->IsDirty())
	{
		Blueprint.DurableSchemaVersion = PreviousVersion;
		Blueprint.DurableSchemaFingerprint = PreviousFingerprint;
		Blueprint.DurableSchemaSnapshot = PreviousSnapshot;
		Blueprint.DurableVariables = PreviousVariables;
		Blueprint.DurableDefaultValues = PreviousDefaults;
		Package->SetDirtyFlag(bPackageWasDirty);
		Result.Error = LOCTEXT(
			"DurableCandidateCouldNotDirtyPackage",
			"The Cue Type package cannot be marked for saving; its durable schema was not changed.");
		return Result;
	}

	Result.bSuccess = true;
	Result.Fingerprint = Schema.Fingerprint;
	Result.Error = FText::GetEmpty();
	return Result;
}

bool FPaper2DPlusFrameCueTypeAuthoring::ValidateDurableSaveMetadataForPersistence(
	UPaper2DPlusFrameCueBlueprint& Blueprint,
	FText* OutError)
{
	FText Error;
	if (!ValidateCompiledSchemaParity(Blueprint, &Error)
		|| !UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
			Blueprint, &Error, Blueprint.GeneratedClass))
	{
		Paper2DPlusFrameCueTypeAuthoringInternal::SetError(OutError, Error);
		return false;
	}
	FPaper2DPlusFrameCueSchema Schema;
	FString Snapshot;
	TMap<FName, FString> Defaults;
	if (!DescribeSchema(
		Blueprint,
		EPaper2DPlusFrameCueSchemaSource::Compiled,
		Schema,
		&Error)
		|| !SerializeSchemaSnapshot(Schema, Snapshot, &Error)
		|| !CaptureDefaultValues(Blueprint, Defaults, &Error))
	{
		Paper2DPlusFrameCueTypeAuthoringInternal::SetError(OutError, Error);
		return false;
	}

	bool bVariablesMatch = Blueprint.DurableVariables.Num() == Blueprint.NewVariables.Num();
	const UScriptStruct* VariableStruct = FBPVariableDescription::StaticStruct();
	for (int32 Index = 0; bVariablesMatch && Index < Blueprint.NewVariables.Num(); ++Index)
	{
		bVariablesMatch = VariableStruct
			&& VariableStruct->CompareScriptStruct(
				&Blueprint.DurableVariables[Index],
				&Blueprint.NewVariables[Index],
				PPF_None);
	}
	bool bDefaultsMatch = Blueprint.DurableDefaultValues.Num() == Defaults.Num();
	for (const TPair<FName, FString>& Pair : Defaults)
	{
		const FString* DurableValue = Blueprint.DurableDefaultValues.Find(Pair.Key);
		bDefaultsMatch = bDefaultsMatch && DurableValue && *DurableValue == Pair.Value;
	}
	const bool bExplicitCandidateMatches =
		IsAcceptedDurableSchemaVersion(Blueprint.DurableSchemaVersion)
		&& (Blueprint.DurableSchemaVersion >= CurrentSchemaVersion
			|| Schema.BehaviorEvents.Num() == 0)
		&& Blueprint.DurableSchemaFingerprint == Schema.Fingerprint
		&& Blueprint.DurableSchemaSnapshot == Snapshot
		&& bVariablesMatch
		&& bDefaultsMatch;
	if (!bExplicitCandidateMatches)
	{
		Paper2DPlusFrameCueTypeAuthoringInternal::SetError(
			OutError,
			LOCTEXT(
				"DurableCueSchemaNotExplicitlyStaged",
				"This Cue Type's current compiled schema was not staged by the protected Frame Cue Type Save workflow. Open it in the Frame Cue Type editor and save there; Save All will not guess or advance a durable baseline."));
		return false;
	}
	Paper2DPlusFrameCueTypeAuthoringInternal::SetError(OutError, FText::GetEmpty());
	return true;
}

bool Paper2DPlusFrameCueTypeAuthoringInternal::
	ValidateCompiledSchemaParityAfterSynchronousBehavior(
		const UBlueprint& Blueprint,
		FText* OutError)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringInternal;
	const UClass* GeneratedClass = Blueprint.GeneratedClass;
	const UObject* GeneratedDefaults = GeneratedClass
		? GeneratedClass->GetDefaultObject(false)
		: nullptr;
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if (!GeneratedClass || !GeneratedDefaults || !Schema)
	{
		SetError(OutError, LOCTEXT(
			"CompiledSchemaUnavailable",
			"The generated Cue payload schema is unavailable."));
		return false;
	}

	TSet<FName> SourceFieldNames;
	for (const FBPVariableDescription& Variable : Blueprint.NewVariables)
	{
		SourceFieldNames.Add(Variable.VarName);
		const FProperty* Property = FindFProperty<FProperty>(GeneratedClass, Variable.VarName);
		if (!Property || Property->GetOwnerClass() != GeneratedClass)
		{
			SetError(OutError, FText::Format(
				LOCTEXT(
					"CompiledCueFieldMissing",
					"Authored field '{0}' is missing from the current generated Cue class."),
				FText::FromName(Variable.VarName)));
			return false;
		}

		FEdGraphPinType CompiledType;
		if (!Schema->ConvertPropertyToPinType(Property, CompiledType)
			|| CompiledType != Variable.VarType)
		{
			SetError(OutError, FText::Format(
				LOCTEXT(
					"CompiledCueFieldTypeMismatch",
					"Generated field '{0}' does not match the authored payload type."),
				FText::FromName(Variable.VarName)));
			return false;
		}
	}

	for (TFieldIterator<FProperty> It(
		GeneratedClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		// The compiler's event-graph frame pointer belongs to the permitted behavior graph, not to
		// the authored payload schema, so it never participates in source/generated field parity.
		if (UPaper2DPlusFrameCueBlueprint::IsGeneratedEventGraphFrameProperty(GeneratedClass, *It))
		{
			continue;
		}
		if (!SourceFieldNames.Contains(It->GetFName()))
		{
			SetError(OutError, FText::Format(
				LOCTEXT(
					"GeneratedOnlyCueField",
					"Generated Cue field '{0}' has no authored payload declaration."),
				FText::FromName(It->GetFName())));
			return false;
		}
	}

	if (OutError)
	{
		*OutError = FText::GetEmpty();
	}
	return true;
}

bool FPaper2DPlusFrameCueTypeAuthoring::ValidateCompiledSchemaParity(
	const UBlueprint& Blueprint,
	FText* OutError)
{
	if (!ValidateSynchronousBehavior(Blueprint, OutError))
	{
		return false;
	}
	return Paper2DPlusFrameCueTypeAuthoringInternal::
		ValidateCompiledSchemaParityAfterSynchronousBehavior(Blueprint, OutError);
}

bool FPaper2DPlusFrameCueTypeAuthoring::ValidateSynchronousBehavior(
	const UBlueprint& Blueprint,
	FText* OutError)
{
	using namespace Paper2DPlusFrameCueTypeAuthoringInternal;
	FSynchronousBehaviorScanState State;
	FSynchronousBehaviorViolation Violation;
	if (ScanSynchronousBehaviorBlueprint(Blueprint, State, Violation))
	{
		SetError(OutError, FText::GetEmpty());
		return true;
	}

	const FString RootPath = Blueprint.GetPathName();
	const FString SourcePath = Violation.SourceBlueprint
		? Violation.SourceBlueprint->GetPathName()
		: RootPath;
	const FString GraphPath = Violation.Graph
		? Violation.Graph->GetPathName()
		: TEXT("<unknown graph>");
	const FString NodeIdentity = Violation.OffendingIdentity.IsEmpty()
		? (Violation.Node
			? Violation.Node->GetClass()->GetPathName()
			: TEXT("<unknown node>"))
		: Violation.OffendingIdentity;
	const FText Error = FText::Format(
		LOCTEXT(
			"DeferredCueBehaviorRejected",
			"Frame Cue Type '{0}' behavior event(s) '{1}' contains deferred node/call "
			"'{2}', reached from authored Cue Type '{3}' through graph '{4}': {5} Frame Cue placements are "
			"shared, and their owner/component/context exists only for synchronous dispatch, "
			"so deferred work cannot safely resume on the placement."),
		FText::FromString(RootPath),
		FText::FromString(Violation.BehaviorEvents),
		FText::FromString(NodeIdentity),
		FText::FromString(SourcePath),
		FText::FromString(GraphPath),
		Violation.Reason);
	SetError(OutError, Error);
	return false;
}

bool FPaper2DPlusFrameCueTypeAuthoring::IsPlacementTimingProperty(
	const FProperty* Property)
{
	if (!Property)
	{
		return false;
	}
	static const FProperty* TriggerFrame = FindFProperty<FProperty>(
		UPaper2DPlusCue::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusCue, TriggerFrame));
	static const FProperty* TriggerEdge = FindFProperty<FProperty>(
		UPaper2DPlusCue::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusCue, TriggerEdge));
	static const FProperty* StartFrame = FindFProperty<FProperty>(
		UPaper2DPlusCueState::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusCueState, StartFrame));
	static const FProperty* FrameCount = FindFProperty<FProperty>(
		UPaper2DPlusCueState::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusCueState, FrameCount));
	return Property == TriggerFrame
		|| Property == TriggerEdge
		|| Property == StartFrame
		|| Property == FrameCount;
}

bool FPaper2DPlusFrameCueTypeAuthoring::IsCueAuthoringPropertyVisible(
	const FProperty* Property)
{
	return !Property || !IsPlacementTimingProperty(Property);
}

bool FPaper2DPlusFrameCueTypeAuthoring::IsAutomationFixtureCueClass(const UClass* CueClass)
{
	return CueClass
		&& CueClass->HasMetaData(TEXT("Paper2DPlusAutomationFixture"));
}

FPaper2DPlusFrameCueLegacyGraphInventory
FPaper2DPlusFrameCueTypeAuthoring::InventoryLegacyGraphs(const UBlueprint& Blueprint)
{
	FPaper2DPlusFrameCueLegacyGraphInventory Result;
	TSet<const UEdGraph*> Seen;
	const auto AddGraph = [&Blueprint, &Result, &Seen](const UEdGraph* Graph, const FName Kind)
	{
		if (!Graph
			|| Graph->HasAnyFlags(RF_Transient)
			|| Graph->GetTypedOuter<UBlueprint>() != &Blueprint
			|| Seen.Contains(Graph))
		{
			return;
		}
		Seen.Add(Graph);
		FPaper2DPlusFrameCueLegacyGraphEntry& Entry = Result.Graphs.AddDefaulted_GetRef();
		Entry.GraphPath = FSoftObjectPath(Graph);
		Entry.GraphName = Graph->GetFName();
		Entry.GraphKind = Kind;
		Entry.NodeCount = Graph->Nodes.Num();
	};

	for (const TObjectPtr<UEdGraph>& Graph : Blueprint.UbergraphPages)
	{
		AddGraph(Graph, TEXT("Event"));
	}
	for (const TObjectPtr<UEdGraph>& Graph : Blueprint.FunctionGraphs)
	{
		AddGraph(Graph, TEXT("Function"));
	}
	for (const TObjectPtr<UEdGraph>& Graph : Blueprint.MacroGraphs)
	{
		AddGraph(Graph, TEXT("Macro"));
	}
	for (const TObjectPtr<UEdGraph>& Graph : Blueprint.DelegateSignatureGraphs)
	{
		AddGraph(Graph, TEXT("Delegate"));
	}
	for (const FBPInterfaceDescription& Interface : Blueprint.ImplementedInterfaces)
	{
		for (const TObjectPtr<UEdGraph>& Graph : Interface.Graphs)
		{
			AddGraph(Graph, TEXT("Interface"));
		}
	}
	Result.Graphs.Sort([](
		const FPaper2DPlusFrameCueLegacyGraphEntry& Left,
		const FPaper2DPlusFrameCueLegacyGraphEntry& Right)
	{
		const FString LeftKey = Left.GraphKind.ToString() + TEXT(":")
			+ Left.GraphName.ToString() + TEXT(":") + Left.GraphPath.ToString();
		const FString RightKey = Right.GraphKind.ToString() + TEXT(":")
			+ Right.GraphName.ToString() + TEXT(":") + Right.GraphPath.ToString();
		return LeftKey < RightKey;
	});
	return Result;
}

TArray<FName> FPaper2DPlusFrameCueTypeAuthoring::GetAuthoredBehaviorEvents(
	const UBlueprint& Blueprint)
{
	TArray<const UEdGraph*> SearchGraphs;
	for (const TObjectPtr<UEdGraph>& EventGraph : Blueprint.UbergraphPages)
	{
		Paper2DPlusFrameCueTypeAuthoringInternal::CollectGraphWithSubGraphs(EventGraph, SearchGraphs);
	}

	TArray<FName> Events;
	for (const UEdGraph* EventGraph : SearchGraphs)
	{
		for (const TObjectPtr<UEdGraphNode>& Node : EventGraph->Nodes)
		{
			const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
			if (!EventNode || !EventNode->bOverrideFunction)
			{
				continue;
			}
			// A seeded stub is an automatically placed ghost until the designer authors into it, and
			// the compiler generates nothing for it. Mirror that exactly, or a fresh Cue Type would
			// describe behavior it does not have and lose its unchanged version 1 fingerprint.
			if (EventNode->IsAutomaticallyPlacedGhostNode()
				|| EventNode->GetDesiredEnabledState() == ENodeEnabledState::Disabled)
			{
				continue;
			}
			const FName EventName = EventNode->EventReference.GetMemberName();
			if (Paper2DPlusFrameCueBehavior::IsDeclaredEventName(EventName))
			{
				Events.AddUnique(EventName);
			}
		}
	}
	Events.Sort([](const FName& Left, const FName& Right) { return Left.LexicalLess(Right); });
	return Events;
}

TArray<FName> FPaper2DPlusFrameCueTypeAuthoring::GetCompiledBehaviorEvents(
	const UBlueprint& Blueprint)
{
	TArray<FName> Events;
	if (const UClass* GeneratedClass = Blueprint.GeneratedClass)
	{
		for (TFieldIterator<UFunction> It(GeneratedClass, EFieldIteratorFlags::ExcludeSuper);
			It;
			++It)
		{
			if (Paper2DPlusFrameCueBehavior::IsDeclaredEventName(It->GetFName()))
			{
				Events.AddUnique(It->GetFName());
			}
		}
	}
	Events.Sort([](const FName& Left, const FName& Right) { return Left.LexicalLess(Right); });
	return Events;
}

bool FPaper2DPlusFrameCueTypeAuthoring::IsAcceptedDurableSchemaVersion(const int32 Version)
{
	return Version >= BehaviorFreeSchemaVersion && Version <= CurrentSchemaVersion;
}

UEdGraph* FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(const UBlueprint& Blueprint)
{
	for (const TObjectPtr<UEdGraph>& EventGraph : Blueprint.UbergraphPages)
	{
		if (EventGraph)
		{
			return EventGraph;
		}
	}
	return nullptr;
}

UEdGraph* FPaper2DPlusFrameCueTypeAuthoring::EnsureBehaviorEventGraph(UBlueprint& Blueprint)
{
	if (UEdGraph* Existing = FindBehaviorEventGraph(Blueprint))
	{
		return Existing;
	}
	// A Cue Type authored before behavior existed has no event graph; the first implemented event
	// creates the one permitted page instead of refusing the action.
	UEdGraph* EventGraph = FBlueprintEditorUtils::CreateNewGraph(
		&Blueprint,
		UEdGraphSchema_K2::GN_EventGraph,
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass());
	if (!EventGraph)
	{
		return nullptr;
	}
	FBlueprintEditorUtils::AddUbergraphPage(&Blueprint, EventGraph);
	return EventGraph;
}

UK2Node_Event* FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventNode(
	const UBlueprint& Blueprint,
	const FName EventName)
{
	if (!Paper2DPlusFrameCueBehavior::IsDeclaredEventName(EventName))
	{
		return nullptr;
	}
	const UEdGraph* BehaviorGraph = FindBehaviorEventGraph(Blueprint);
	if (!BehaviorGraph)
	{
		return nullptr;
	}
	TArray<const UEdGraph*> SearchGraphs;
	Paper2DPlusFrameCueTypeAuthoringInternal::CollectGraphWithSubGraphs(BehaviorGraph, SearchGraphs);
	for (const UEdGraph* Graph : SearchGraphs)
	{
		for (const TObjectPtr<UEdGraphNode>& Node : Graph->Nodes)
		{
			UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
			if (EventNode
				&& EventNode->bOverrideFunction
				&& EventNode->EventReference.GetMemberName() == EventName)
			{
				return EventNode;
			}
		}
	}
	return nullptr;
}

bool FPaper2DPlusFrameCueTypeAuthoring::EnsureBehaviorEventNode(
	UBlueprint& Blueprint,
	const FName EventName)
{
	if (!Blueprint.ParentClass
		|| !Paper2DPlusFrameCueBehavior::GetDeclaredEventNamesForClass(
			Blueprint.ParentClass).Contains(EventName))
	{
		return false;
	}
	if (FindBehaviorEventNode(Blueprint, EventName))
	{
		return false;
	}
	UEdGraph* BehaviorGraph = EnsureBehaviorEventGraph(Blueprint);
	if (!BehaviorGraph)
	{
		return false;
	}

	int32 NodePosY = 0;
	for (const TObjectPtr<UEdGraphNode>& Node : BehaviorGraph->Nodes)
	{
		if (Node)
		{
			NodePosY = FMath::Max(NodePosY, Node->NodePosY + Node->NodeHeight + 200);
		}
	}
	if (!FKismetEditorUtilities::AddDefaultEventNode(
		&Blueprint,
		BehaviorGraph,
		EventName,
		Blueprint.ParentClass,
		NodePosY))
	{
		return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
	return true;
}

bool FPaper2DPlusFrameCueTypeAuthoring::SeedDeclaredBehaviorEvents(UBlueprint& Blueprint)
{
	bool bSeeded = false;
	for (const FName& EventName :
		Paper2DPlusFrameCueBehavior::GetSeedEventNamesForClass(Blueprint.ParentClass))
	{
		bSeeded |= EnsureBehaviorEventNode(Blueprint, EventName);
	}
	return bSeeded;
}

int32 FPaper2DPlusFrameCueLegacyGraphInventory::CountUnsupportedGraphs() const
{
	static const FName EventGraphKind(TEXT("Event"));
	int32 Count = 0;
	for (const FPaper2DPlusFrameCueLegacyGraphEntry& Entry : Graphs)
	{
		Count += Entry.GraphKind == EventGraphKind ? 0 : 1;
	}
	return Count;
}

bool FPaper2DPlusFrameCueLegacyGraphInventory::HasEventGraphs() const
{
	static const FName EventGraphKind(TEXT("Event"));
	return Graphs.ContainsByPredicate(
		[](const FPaper2DPlusFrameCueLegacyGraphEntry& Entry)
		{
			return Entry.GraphKind == EventGraphKind;
		});
}

#undef LOCTEXT_NAMESPACE
