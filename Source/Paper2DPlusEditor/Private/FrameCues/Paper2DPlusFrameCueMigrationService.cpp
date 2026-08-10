// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCueMigrationService.h"

#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameEvents/Paper2DPlusApplyGameplayTagFrameEvent.h"
#include "FrameEvents/Paper2DPlusCameraShakeFrameEvent.h"
#include "FrameEvents/Paper2DPlusFrameEvent.h"
#include "FrameEvents/Paper2DPlusFrameEventState.h"
#include "FrameEvents/Paper2DPlusPlaySoundFrameEvent.h"
#include "FrameEvents/Paper2DPlusScreenFlashFrameEvent.h"
#include "FrameEvents/Paper2DPlusSpawnEffectFrameEvent.h"
#include "FrameEvents/Paper2DPlusSpawnProjectileFrameEvent.h"
#include "K2Node_Event.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "UObject/UnrealType.h"

namespace Paper2DPlusFrameCueMigrationInternal
{
	/**
	 * True for one of the six exact native legacy Frame Event classes.
	 *
	 * It says only "this row was authored with a shipped legacy Frame Event", NOT that a conversion
	 * target exists. The built-in Cue Types those rows used to convert into no longer exist, so nothing
	 * anywhere converts them; the classification survives purely to tell a designer which rows they are
	 * looking at.
	 */
	bool IsExactLegacyBuiltInEvent(const UPaper2DPlusFrameEventBase& Event)
	{
		const UClass* Class = Event.GetClass();
		return Class == UPaper2DPlusPlaySoundFrameEvent::StaticClass()
			|| Class == UPaper2DPlusSpawnEffectFrameEvent::StaticClass()
			|| Class == UPaper2DPlusSpawnProjectileFrameEvent::StaticClass()
			|| Class == UPaper2DPlusCameraShakeFrameEvent::StaticClass()
			|| Class == UPaper2DPlusScreenFlashFrameEvent::StaticClass()
			|| Class == UPaper2DPlusApplyGameplayTagFrameEvent::StaticClass();
	}

	int32 GetLegacyAnchor(const UPaper2DPlusFrameEventBase& Event)
	{
		if (const UPaper2DPlusFrameEventState* Range = Cast<UPaper2DPlusFrameEventState>(&Event))
		{
			return Range->StartFrame;
		}
		if (const UPaper2DPlusFrameEvent* Moment = Cast<UPaper2DPlusFrameEvent>(&Event))
		{
			return Moment->TriggerFrame;
		}
		return INDEX_NONE;
	}

	FString LegacyPolicy(const UPaper2DPlusFrameEventBase& Event)
	{
		const int64 Value = static_cast<int64>(Event.NetPolicy);
		const UEnum* Enum = StaticEnum<EPaper2DPlusFrameEventNetPolicy>();
		const FString Name = Enum ? Enum->GetNameStringByValue(Value) : FString();
		return Name.IsEmpty() ? FString::Printf(TEXT("Invalid(%lld)"), Value) : Name;
	}

	FString CuePolicy(const UPaper2DPlusCueBase& Cue, bool& bOutValid)
	{
		const int64 Value = static_cast<int64>(Cue.NetPolicy);
		const UEnum* Enum = StaticEnum<EPaper2DPlusFrameCueNetPolicy>();
		bOutValid = Enum && Enum->IsValidEnumValue(Value);
		const FString Name = Enum ? Enum->GetNameStringByValue(Value) : FString();
		return Name.IsEmpty() ? FString::Printf(TEXT("Invalid(%lld)"), Value) : Name;
	}

	void CollectPayloadProperties(const UObject& Object, TArray<FString>& OutNames)
	{
		static const TSet<FName> CommonNames = {
			TEXT("Color"), TEXT("DebugName"), TEXT("NetPolicy"), TEXT("bSkipInEditorPreview"),
			TEXT("TriggerFrame"), TEXT("StartFrame"), TEXT("FrameCount"), TEXT("UberGraphFrame")
		};
		for (TFieldIterator<FProperty> It(Object.GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property || CommonNames.Contains(Property->GetFName())
				|| Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)
				|| !Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
			{
				continue;
			}
			OutNames.AddUnique(Property->GetName());
		}
		OutNames.Sort();
	}

	void CollectBlueprintGraphs(const UObject& Object, TArray<FString>& OutNames)
	{
		const UBlueprint* Blueprint = Cast<UBlueprint>(Object.GetClass()->ClassGeneratedBy);
		if (!Blueprint)
		{
			return;
		}
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (const UEdGraph* Graph : Graphs)
		{
			if (!Graph || Graph->Nodes.Num() == 0)
			{
				continue;
			}
			bool bNamedOverride = false;
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
				if (EventNode && EventNode->bOverrideFunction)
				{
					const FName FunctionName = EventNode->EventReference.GetMemberName();
					if (!FunctionName.IsNone())
					{
						OutNames.AddUnique(Graph->GetName() + TEXT("::") + FunctionName.ToString());
						bNamedOverride = true;
					}
				}
			}
			if (!bNamedOverride)
			{
				OutNames.AddUnique(Graph->GetName());
			}
		}
		OutNames.Sort();
	}

	int32 FrameCountForEntry(const FFlipbookProfileEntry& Entry)
	{
		if (const UPaperFlipbook* Flipbook = Entry.Identity.Flipbook.Get())
		{
			return Flipbook->GetNumKeyFrames();
		}
		return Entry.CombatData.Frames.Num() > 0 ? Entry.CombatData.Frames.Num() : INDEX_NONE;
	}

	int32 FrameCountForLayerAnimation(const FCharacterLayerAuthoredAnimationData& Animation)
	{
		if (const UPaperFlipbook* Flipbook = Animation.Flipbook.Get())
		{
			return Flipbook->GetNumKeyFrames();
		}
		return Animation.Frames.Num() > 0 ? Animation.Frames.Num() : INDEX_NONE;
	}

	FPaper2DPlusFrameCueMigrationIssue MakeBaseIssue(
		const UObject& Asset,
		const FString& SourcePath,
		const FString& AnimationName)
	{
		FPaper2DPlusFrameCueMigrationIssue Issue;
		Issue.AssetPath = Asset.GetPathName();
		Issue.SourcePath = SourcePath;
		Issue.AnimationName = AnimationName;
		return Issue;
	}

	void AnalyzeLegacyEvent(
		const UObject& Asset,
		const FString& SourcePath,
		const FString& AnimationName,
		int32 AnimationFrameCount,
		const UPaper2DPlusFrameEventBase* Event,
		FPaper2DPlusFrameCueMigrationReport& Report)
	{
		FPaper2DPlusFrameCueMigrationIssue Issue = MakeBaseIssue(Asset, SourcePath, AnimationName);
		if (!Event)
		{
			Issue.Disposition = EPaper2DPlusFrameCueMigrationDisposition::Blocking;
			Issue.Kind = EPaper2DPlusFrameCueMigrationIssueKind::NullLegacyEvent;
			Issue.Message = TEXT("A legacy Frame Event placement is null and must be removed before compatibility cleanup.");
			Report.Issues.Add(MoveTemp(Issue));
			return;
		}

		Issue.AnchorFrame = GetLegacyAnchor(*Event);
		Issue.AnimationFrameCount = AnimationFrameCount;
		if (const UPaper2DPlusFrameEventState* Range = Cast<UPaper2DPlusFrameEventState>(Event))
		{
			Issue.AuthoredFrameCount = Range->FrameCount;
		}
		else
		{
			Issue.AuthoredFrameCount = 1;
		}
		Issue.ClassPath = Event->GetClass()->GetPathName();
		Issue.NetworkPolicyContext = LegacyPolicy(*Event);
		CollectPayloadProperties(*Event, Issue.PayloadPropertyNames);
		CollectBlueprintGraphs(*Event, Issue.BlueprintGraphNames);
		if (IsExactLegacyBuiltInEvent(*Event))
		{
			Issue.Disposition = EPaper2DPlusFrameCueMigrationDisposition::Notice;
			Issue.Kind = EPaper2DPlusFrameCueMigrationIssueKind::ConvertibleBuiltInEvent;
			Issue.Message = TEXT("A legacy built-in Frame Event row remains. It is inert: nothing converts it and it never runs. Re-author it as a Frame Cue Type, then delete the legacy row.");
		}
		else
		{
			Issue.Disposition = EPaper2DPlusFrameCueMigrationDisposition::Blocking;
			Issue.Kind = EPaper2DPlusFrameCueMigrationIssueKind::UnsupportedExecutableEvent;
			Issue.Message = TEXT("A custom executable Frame Event row remains. It is inert: nothing converts it and it never runs. Copy its payload and Blueprint graph into a Frame Cue Type before removing legacy shells.");
		}
		Report.Issues.Add(MoveTemp(Issue));

		const int32 Anchor = GetLegacyAnchor(*Event);
		if (Anchor < 0 || (AnimationFrameCount > 0 && Anchor >= AnimationFrameCount))
		{
			FPaper2DPlusFrameCueMigrationIssue Invalid = MakeBaseIssue(Asset, SourcePath, AnimationName);
			Invalid.Disposition = EPaper2DPlusFrameCueMigrationDisposition::Blocking;
			Invalid.Kind = EPaper2DPlusFrameCueMigrationIssueKind::InvalidLegacyAnchor;
			Invalid.AnchorFrame = Anchor;
			Invalid.AnimationFrameCount = AnimationFrameCount;
			Invalid.ClassPath = Event->GetClass()->GetPathName();
			Invalid.Message = TEXT("The legacy Frame Event anchor is outside the animation's definitive key-frame range.");
			Report.Issues.Add(MoveTemp(Invalid));
		}
		if (const UPaper2DPlusFrameEventState* Range = Cast<UPaper2DPlusFrameEventState>(Event))
		{
			const bool bInvalidCount = Range->FrameCount < 1;
			const bool bPastEnd = AnimationFrameCount > 0 && Range->FrameCount > 0 && Range->StartFrame >= 0
				&& static_cast<int64>(Range->StartFrame) + Range->FrameCount > AnimationFrameCount;
			if (bInvalidCount || bPastEnd)
			{
				FPaper2DPlusFrameCueMigrationIssue Invalid = MakeBaseIssue(Asset, SourcePath, AnimationName);
				Invalid.Disposition = EPaper2DPlusFrameCueMigrationDisposition::Blocking;
				Invalid.Kind = EPaper2DPlusFrameCueMigrationIssueKind::InvalidLegacyRange;
				Invalid.AnchorFrame = Range->StartFrame;
				Invalid.AuthoredFrameCount = Range->FrameCount;
				Invalid.AnimationFrameCount = AnimationFrameCount;
				Invalid.ClassPath = Event->GetClass()->GetPathName();
				Invalid.Message = TEXT("The legacy ranged Frame Event has an invalid count or extends past the animation.");
				Report.Issues.Add(MoveTemp(Invalid));
			}
		}
		const UEnum* LegacyPolicyEnum = StaticEnum<EPaper2DPlusFrameEventNetPolicy>();
		if (!LegacyPolicyEnum || !LegacyPolicyEnum->IsValidEnumValue(static_cast<int64>(Event->NetPolicy)))
		{
			FPaper2DPlusFrameCueMigrationIssue Invalid = MakeBaseIssue(Asset, SourcePath, AnimationName);
			Invalid.Disposition = EPaper2DPlusFrameCueMigrationDisposition::Blocking;
			Invalid.Kind = EPaper2DPlusFrameCueMigrationIssueKind::InvalidLegacyNetworkPolicy;
			Invalid.AnchorFrame = Anchor;
			Invalid.ClassPath = Event->GetClass()->GetPathName();
			Invalid.NetworkPolicyContext = LegacyPolicy(*Event);
			Invalid.Message = TEXT("The legacy Frame Event stores an unknown network policy value.");
			Report.Issues.Add(MoveTemp(Invalid));
		}
	}

	void AnalyzeCue(
		const UObject& Asset,
		const FString& SourcePath,
		const FString& AnimationName,
		int32 AnimationFrameCount,
		const UPaper2DPlusCueBase* Cue,
		FPaper2DPlusFrameCueMigrationReport& Report)
	{
		FPaper2DPlusFrameCueMigrationIssue Base = MakeBaseIssue(Asset, SourcePath, AnimationName);
		Base.AnimationFrameCount = AnimationFrameCount;
		if (!Cue)
		{
			Base.Disposition = EPaper2DPlusFrameCueMigrationDisposition::Blocking;
			Base.Kind = EPaper2DPlusFrameCueMigrationIssueKind::NullCue;
			Base.Message = TEXT("A Frame Cue placement is null.");
			Report.Issues.Add(MoveTemp(Base));
			return;
		}

		Base.AnchorFrame = Cue->GetPrimaryAnchorFrame();
		Base.AuthoredFrameCount = Cue->IsRangeCue() ? Cue->GetCueFrameCount() : 1;
		Base.ClassPath = Cue->GetClass()->GetPathName();
		bool bPolicyValid = false;
		Base.NetworkPolicyContext = CuePolicy(*Cue, bPolicyValid);

		if (Base.AnchorFrame < 0 || (AnimationFrameCount > 0 && Base.AnchorFrame >= AnimationFrameCount))
		{
			FPaper2DPlusFrameCueMigrationIssue Issue = Base;
			Issue.Disposition = EPaper2DPlusFrameCueMigrationDisposition::Blocking;
			Issue.Kind = EPaper2DPlusFrameCueMigrationIssueKind::InvalidCueAnchor;
			Issue.Message = TEXT("The Frame Cue anchor is outside the animation's definitive key-frame range.");
			Report.Issues.Add(MoveTemp(Issue));
		}

		if (const UPaper2DPlusCueState* Range = Cast<UPaper2DPlusCueState>(Cue))
		{
			const bool bBadCount = Range->FrameCount < 1;
			const bool bPastEnd = AnimationFrameCount > 0 && Range->FrameCount > 0
				&& static_cast<int64>(Range->StartFrame) + Range->FrameCount > AnimationFrameCount;
			if (bBadCount || bPastEnd)
			{
				FPaper2DPlusFrameCueMigrationIssue Issue = Base;
				Issue.AuthoredFrameCount = Range->FrameCount;
				Issue.Disposition = EPaper2DPlusFrameCueMigrationDisposition::Blocking;
				Issue.Kind = EPaper2DPlusFrameCueMigrationIssueKind::InvalidCueRange;
				Issue.Message = TEXT("The Cue State has an invalid frame count or extends past the animation.");
				Report.Issues.Add(MoveTemp(Issue));
			}
		}

		if (!bPolicyValid)
		{
			FPaper2DPlusFrameCueMigrationIssue Issue = Base;
			Issue.Disposition = EPaper2DPlusFrameCueMigrationDisposition::Blocking;
			Issue.Kind = EPaper2DPlusFrameCueMigrationIssueKind::InvalidCueNetworkPolicy;
			Issue.Message = TEXT("The Frame Cue stores an unknown network policy value.");
			Report.Issues.Add(MoveTemp(Issue));
		}

#if WITH_EDITORONLY_DATA
		if (Cue->bMigratedFromFrameEvent && !Cue->bMigrationReceiverAcknowledged)
		{
			FPaper2DPlusFrameCueMigrationIssue Issue = Base;
			Issue.Disposition = EPaper2DPlusFrameCueMigrationDisposition::Notice;
			Issue.Kind = EPaper2DPlusFrameCueMigrationIssueKind::MigratedCueNeedsReceiverAcknowledgement;
			Issue.Message = TEXT("This Cue was migrated from a legacy Frame Event and still needs explicit acknowledgement that game Blueprint receiver wiring exists.");
			Report.Issues.Add(MoveTemp(Issue));
		}
#endif
	}
}

FString FPaper2DPlusFrameCueMigrationIssue::GetStableKey() const
{
	return FString::Printf(TEXT("%s|%s|%s|%03d|%s|%08d"),
		*AssetPath, *AnimationName, *SourcePath, static_cast<int32>(Kind), *ClassPath, AnchorFrame);
}

bool FPaper2DPlusFrameCueMigrationReport::HasBlockingIssues() const
{
	return GetBlockingIssueCount() > 0;
}

int32 FPaper2DPlusFrameCueMigrationReport::GetBlockingIssueCount() const
{
	int32 Count = 0;
	for (const FPaper2DPlusFrameCueMigrationIssue& Issue : Issues)
	{
		if (Issue.Disposition == EPaper2DPlusFrameCueMigrationDisposition::Blocking)
		{
			++Count;
		}
	}
	return Count;
}

int32 FPaper2DPlusFrameCueMigrationReport::GetNoticeCount() const
{
	return Issues.Num() - GetBlockingIssueCount();
}

void FPaper2DPlusFrameCueMigrationReport::SortDeterministically()
{
	Issues.StableSort([](const FPaper2DPlusFrameCueMigrationIssue& A, const FPaper2DPlusFrameCueMigrationIssue& B)
	{
		return A.GetStableKey() < B.GetStableKey();
	});
}

FPaper2DPlusFrameCueMigrationReport FPaper2DPlusFrameCueMigrationService::AnalyzeCharacterProfile(
	const UPaper2DPlusCharacterProfileAsset& Asset)
{
	FPaper2DPlusFrameCueMigrationReport Report;
	AppendCharacterProfile(Asset, Report);
	Report.SortDeterministically();
	return Report;
}

void FPaper2DPlusFrameCueMigrationService::AppendCharacterProfile(
	const UPaper2DPlusCharacterProfileAsset& Asset,
	FPaper2DPlusFrameCueMigrationReport& InOutReport)
{
	using namespace Paper2DPlusFrameCueMigrationInternal;
	for (int32 AnimationIndex = 0; AnimationIndex < Asset.Flipbooks.Num(); ++AnimationIndex)
	{
		const FFlipbookProfileEntry& Entry = Asset.Flipbooks[AnimationIndex];
		const FString Animation = Entry.Identity.FlipbookName.IsEmpty()
			? FString::Printf(TEXT("Animation[%d]"), AnimationIndex) : Entry.Identity.FlipbookName;
		const int32 FrameCount = FrameCountForEntry(Entry);
		for (int32 Index = 0; Index < Entry.FrameEventData.FrameEvents.Num(); ++Index)
		{
			AnalyzeLegacyEvent(Asset, FString::Printf(TEXT("FrameEvents[%d]"), Index), Animation, FrameCount,
				Entry.FrameEventData.FrameEvents[Index], InOutReport);
		}
		for (int32 Index = 0; Index < Entry.FrameEventData.FrameCues.Num(); ++Index)
		{
			AnalyzeCue(Asset, FString::Printf(TEXT("FrameCues[%d]"), Index), Animation, FrameCount,
				Entry.FrameEventData.FrameCues[Index], InOutReport);
		}
		for (int32 ExcludedIndex = 0; ExcludedIndex < Entry.CombatData.ExcludedFrames.Num(); ++ExcludedIndex)
		{
			const FExcludedFlipbookFrameData& Excluded = Entry.CombatData.ExcludedFrames[ExcludedIndex];
			for (int32 EventIndex = 0; EventIndex < Excluded.StashedFrameEvents.Num(); ++EventIndex)
			{
				AnalyzeLegacyEvent(Asset,
					FString::Printf(TEXT("ExcludedFrames[%d].StashedFrameEvents[%d]"), ExcludedIndex, EventIndex),
					Animation, INDEX_NONE, Excluded.StashedFrameEvents[EventIndex], InOutReport);
			}
			for (int32 CueIndex = 0; CueIndex < Excluded.StashedFrameCues.Num(); ++CueIndex)
			{
				AnalyzeCue(Asset,
					FString::Printf(TEXT("ExcludedFrames[%d].StashedFrameCues[%d]"), ExcludedIndex, CueIndex),
					Animation, INDEX_NONE, Excluded.StashedFrameCues[CueIndex], InOutReport);
			}
		}
	}
}

FPaper2DPlusFrameCueMigrationReport FPaper2DPlusFrameCueMigrationService::AnalyzeCharacterLayer(
	const UPaper2DPlusCharacterLayerAsset& Asset)
{
	FPaper2DPlusFrameCueMigrationReport Report;
	AppendCharacterLayer(Asset, Report);
	Report.SortDeterministically();
	return Report;
}

void FPaper2DPlusFrameCueMigrationService::AppendCharacterLayer(
	const UPaper2DPlusCharacterLayerAsset& Asset,
	FPaper2DPlusFrameCueMigrationReport& InOutReport)
{
	using namespace Paper2DPlusFrameCueMigrationInternal;
	for (int32 LayerIndex = 0; LayerIndex < Asset.Layers.Num(); ++LayerIndex)
	{
		const FCharacterLayer& Layer = Asset.Layers[LayerIndex];
		for (int32 AnimationIndex = 0; AnimationIndex < Layer.AuthoredAnimations.Num(); ++AnimationIndex)
		{
			const FCharacterLayerAuthoredAnimationData& Animation = Layer.AuthoredAnimations[AnimationIndex];
			const FString AnimationName = Animation.Flipbook.IsNull()
				? Animation.LegacyAnimationName
				: Animation.Flipbook.ToSoftObjectPath().GetAssetName();
			const int32 FrameCount = FrameCountForLayerAnimation(Animation);
			for (int32 CueIndex = 0; CueIndex < Animation.FrameCues.Num(); ++CueIndex)
			{
				AnalyzeCue(Asset,
					FString::Printf(TEXT("Layers[%d:%s].AuthoredAnimations[%d].FrameCues[%d]"),
						LayerIndex, *Layer.LayerName, AnimationIndex, CueIndex),
					AnimationName, FrameCount, Animation.FrameCues[CueIndex], InOutReport);
			}
		}
	}
}
