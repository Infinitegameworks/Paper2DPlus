// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusFrameData.h"
#include "Paper2DPlusAnimationTagQuery.h" // TASK-108 U6 (R9): the effective-tag batch for the export column
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "PaperFlipbook.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"

/** FPaper2DPlusFrameData — single source of truth for the TASK-15 fighting-game frame-data
 *  table: pure worldless compute (per-move summary) + CSV/JSON export. Shared by the BP query,
 *  the Content-Browser export action, and the editor's read-only Frame Data tab. */

namespace
{
	/** Display name for the built-in phase. */
	FString FrameData_PhaseToString(EAnimationPhase Phase)
	{
		switch (Phase)
		{
			case EAnimationPhase::Startup:  return TEXT("Startup");
			case EAnimationPhase::Active:   return TEXT("Active");
			case EAnimationPhase::Recovery: return TEXT("Recovery");
			default:                        return TEXT("None");
		}
	}

	/** Quote+escape a field for CSV (RFC-4180 style: wrap in quotes, double embedded quotes). */
	FString FrameData_CsvQuote(const FString& In)
	{
		FString Escaped = In;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}

	/** TASK-108 U6 (R9): semicolon-joined full tag names for the AnimationTags export cell (container
	 *  order — deterministic per asset: own, then chain-inherited, then group-implied, as the batch
	 *  builds EffectiveTags). Empty container -> empty string (empty cell). */
	FString FrameData_JoinAnimationTags(const FGameplayTagContainer& EffectiveTags)
	{
		FString Joined;
		for (auto It = EffectiveTags.CreateConstIterator(); It; ++It)
		{
			if (!Joined.IsEmpty())
			{
				Joined += TEXT(";");
			}
			Joined += It->GetTagName().ToString();
		}
		return Joined;
	}
}

FPaper2DPlusMoveFrameData FPaper2DPlusFrameData::ComputeMoveFrameDataForEntry(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	const FFlipbookProfileEntry& Entry,
	bool bAllowSynchronousFlipbookLoad)
{
	FPaper2DPlusMoveFrameData Out;
	Out.FlipbookName = Entry.Identity.FlipbookName;

	// Base-only by contract: equip-aware = Paper2DPlusLayerCombat::ComposeCombatFrames (the component cached tier).
	// --- Per-frame combat aggregates (from authored FFrameHitboxData, source of truth for hitboxes) ---
	for (const FFrameHitboxData& Frame : Entry.CombatData.Frames)
	{
		bool bFrameHasAttack = false;
		for (const FHitboxData& Hitbox : Frame.Hitboxes)
		{
			if (Hitbox.Type != EHitboxType::Attack)
			{
				continue;
			}
			bFrameHasAttack = true;
			Out.MaxDamage = FMath::Max(Out.MaxDamage, Hitbox.Damage);
			Out.MaxKnockback = FMath::Max(Out.MaxKnockback, Hitbox.Knockback);
		}
		if (bFrameHasAttack)
		{
			++Out.ActiveFrames;
		}
		if (Frame.bInvulnerable)
		{
			++Out.IFrameCount;
		}
	}

	// Reuse the canonical reach math so the value matches AI range queries.
	Out.MaxReach = UPaper2DPlusBlueprintLibrary::GetMaxAttackReach(Entry);

	Out.bHasRootMotion = Entry.HasRootMotion();

	// --- Timing: key-frame count + per-frame durations from the live flipbook ---
	// Per-frame duration access mirrors FFlipbookTimingData::ReadFromFlipbook / FrameTimingEditor:
	// each key frame's FrameRun is a tick count, FPS converts ticks -> seconds.
	UPaperFlipbook* Flipbook = bAllowSynchronousFlipbookLoad
		? Entry.Identity.Flipbook.LoadSynchronous()
		: Entry.Identity.Flipbook.Get();
	if (Flipbook)
	{
		const int32 NumKeyFrames = Flipbook->GetNumKeyFrames();
		Out.TotalKeyFrames = NumKeyFrames;

		int32 TotalTicks = 0;
		for (int32 i = 0; i < NumKeyFrames; ++i)
		{
			TotalTicks += FMath::Max(Flipbook->GetKeyFrameChecked(i).FrameRun, 1);
		}
		Out.TotalDurationFrames = TotalTicks;

		const float FPS = Flipbook->GetFramesPerSecond();
		Out.TotalDurationMs = (FPS > 0.f) ? (static_cast<float>(TotalTicks) / FPS) * 1000.f : 0.f;
	}
	else
	{
		// No live flipbook resolvable (e.g. worldless construction without a Flipbook ref): fall back
		// to the authored per-frame combat-data count so the row is still meaningful.
		Out.TotalKeyFrames = Entry.CombatData.Frames.Num();
	}

	// Active/I-frame counts are aggregated from the authored per-frame combat data, which can legitimately be LONGER
	// than the live key-frame count (grow-only sync keeps stale rows for excluded frames). Clamp so the table can never
	// report "Active > Frames", which reads as nonsensical to a designer. No-op when the counts already fit.
	if (Out.TotalKeyFrames > 0)
	{
		Out.ActiveFrames = FMath::Min(Out.ActiveFrames, Out.TotalKeyFrames);
		Out.IFrameCount = FMath::Min(Out.IFrameCount, Out.TotalKeyFrames);
	}

	// --- Phase: the flipbook's own phase tag (Paper2DPlus.Phase.Startup/Active/Recovery) ---
	// The ONE phase identity since phase groups were removed (legacy-cleanup 2026-07). Leaf-name
	// match mirrors the editor's GetPhaseTagBadge convention.
	if (Entry.EditorMeta.PhaseTag.IsValid())
	{
		FString TagName = Entry.EditorMeta.PhaseTag.GetTagName().ToString();
		int32 LastDot = INDEX_NONE;
		const FString Leaf = TagName.FindLastChar(TEXT('.'), LastDot) ? TagName.Mid(LastDot + 1) : TagName;
		if (Leaf.Equals(TEXT("Startup"), ESearchCase::IgnoreCase))
		{
			Out.Phase = EAnimationPhase::Startup;
		}
		else if (Leaf.Equals(TEXT("Active"), ESearchCase::IgnoreCase))
		{
			Out.Phase = EAnimationPhase::Active;
		}
		else if (Leaf.Equals(TEXT("Recovery"), ESearchCase::IgnoreCase))
		{
			Out.Phase = EAnimationPhase::Recovery;
		}
	}

	// Count the distinct authored Cancel_<Category> step curves on this move. These are reserved design-data
	// conventions only: the former transition driver was removed and Paper2DPlus does not execute them.
	// IsCancelCurveName remains the single-source detector for reporting/export compatibility.
	int32 CancelWindows = 0;
	for (const TPair<FName, FPaper2DPlusFrameCurve>& CurvePair : Entry.CurveData.Curves)
	{
		if (FFlipbookTransitionData::IsCancelCurveName(CurvePair.Key))
		{
			++CancelWindows;
		}
	}
	Out.CancelWindowCount = CancelWindows;

	return Out;
}

bool FPaper2DPlusFrameData::ComputeMoveFrameData(
	const UPaper2DPlusCharacterProfileAsset* Asset, const FString& FlipbookName, FPaper2DPlusMoveFrameData& Out)
{
	Out = FPaper2DPlusMoveFrameData();
	if (!Asset)
	{
		return false;
	}

	for (const FFlipbookProfileEntry& Entry : Asset->Flipbooks)
	{
		if (Entry.Identity.FlipbookName.Equals(FlipbookName, ESearchCase::IgnoreCase))
		{
			Out = ComputeMoveFrameDataForEntry(Asset, Entry);
			// R9: effective tags via the batch (one build for this single-move call).
			const TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet> TagMap =
				Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(Asset);
			if (const Paper2DPlusAnimationTagQuery::FAnimationTagSet* TagSet =
				TagMap.Find(Entry.Identity.FlipbookName.ToLower()))
			{
				Out.AnimationTags = FrameData_JoinAnimationTags(TagSet->EffectiveTags);
			}
			return true;
		}
	}
	return false;
}

void FPaper2DPlusFrameData::ComputeAllMoveFrameData(
	const UPaper2DPlusCharacterProfileAsset* Asset, TArray<FPaper2DPlusMoveFrameData>& Out)
{
	Out.Reset();
	if (!Asset)
	{
		return;
	}
	// R9: ONE effective-tag batch for the whole table (per-entry batching would be O(N*(V+E))).
	const TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet> TagMap =
		Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(Asset);
	Out.Reserve(Asset->Flipbooks.Num());
	for (const FFlipbookProfileEntry& Entry : Asset->Flipbooks)
	{
		FPaper2DPlusMoveFrameData Row = ComputeMoveFrameDataForEntry(Asset, Entry);
		if (const Paper2DPlusAnimationTagQuery::FAnimationTagSet* TagSet =
			TagMap.Find(Entry.Identity.FlipbookName.ToLower()))
		{
			Row.AnimationTags = FrameData_JoinAnimationTags(TagSet->EffectiveTags);
		}
		Out.Add(MoveTemp(Row));
	}
}

FString FPaper2DPlusFrameData::GetCsvHeader()
{
	// AnimationTags is APPENDED LAST (TASK-108 U6, R9) so pre-existing column indices never shift.
	// (PhaseGroupName column REMOVED with the phase-groups feature, legacy-cleanup 2026-07 —
	// a deliberate one-time column shift for consumers past Phase.)
	return TEXT("FlipbookName,TotalKeyFrames,TotalDurationFrames,TotalDurationMs,ActiveFrames,")
		   TEXT("IFrameCount,MaxDamage,MaxKnockback,MaxReach,HasRootMotion,Phase,CancelWindowCount,")
		   TEXT("AnimationTags");
}

FString FPaper2DPlusFrameData::ExportFrameDataToCsv(const TArray<FPaper2DPlusMoveFrameData>& Rows)
{
	FString Result = GetCsvHeader();
	Result += LINE_TERMINATOR;

	for (const FPaper2DPlusMoveFrameData& Row : Rows)
	{
		TArray<FString> Cols;
		Cols.Add(FrameData_CsvQuote(Row.FlipbookName));
		Cols.Add(FString::FromInt(Row.TotalKeyFrames));
		Cols.Add(FString::FromInt(Row.TotalDurationFrames));
		Cols.Add(FString::SanitizeFloat(Row.TotalDurationMs));
		Cols.Add(FString::FromInt(Row.ActiveFrames));
		Cols.Add(FString::FromInt(Row.IFrameCount));
		// MinFractionalDigits 0: whole-number damage still exports as "12" (CSV byte-compatible with
		// the int32 era); fractional values gain a decimal part (TASK-146).
		Cols.Add(FString::SanitizeFloat(Row.MaxDamage, 0));
		Cols.Add(FString::SanitizeFloat(Row.MaxKnockback, 0));
		Cols.Add(FString::SanitizeFloat(Row.MaxReach));
		Cols.Add(Row.bHasRootMotion ? TEXT("true") : TEXT("false"));
		Cols.Add(FrameData_CsvQuote(FrameData_PhaseToString(Row.Phase)));
		Cols.Add(FString::FromInt(Row.CancelWindowCount));
		Cols.Add(FrameData_CsvQuote(Row.AnimationTags)); // LAST (R9 — never shift existing indices)

		Result += FString::Join(Cols, TEXT(","));
		Result += LINE_TERMINATOR;
	}

	return Result;
}

FString FPaper2DPlusFrameData::ExportFrameDataToJson(const TArray<FPaper2DPlusMoveFrameData>& Rows)
{
	TArray<TSharedPtr<FJsonValue>> MoveArray;
	MoveArray.Reserve(Rows.Num());

	for (const FPaper2DPlusMoveFrameData& Row : Rows)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("FlipbookName"), Row.FlipbookName);
		Obj->SetNumberField(TEXT("TotalKeyFrames"), Row.TotalKeyFrames);
		Obj->SetNumberField(TEXT("TotalDurationFrames"), Row.TotalDurationFrames);
		Obj->SetNumberField(TEXT("TotalDurationMs"), Row.TotalDurationMs);
		Obj->SetNumberField(TEXT("ActiveFrames"), Row.ActiveFrames);
		Obj->SetNumberField(TEXT("IFrameCount"), Row.IFrameCount);
		Obj->SetNumberField(TEXT("MaxDamage"), Row.MaxDamage);
		Obj->SetNumberField(TEXT("MaxKnockback"), Row.MaxKnockback);
		Obj->SetNumberField(TEXT("MaxReach"), Row.MaxReach);
		Obj->SetBoolField(TEXT("HasRootMotion"), Row.bHasRootMotion);
		Obj->SetStringField(TEXT("Phase"), FrameData_PhaseToString(Row.Phase));
		Obj->SetNumberField(TEXT("CancelWindowCount"), Row.CancelWindowCount);
		Obj->SetStringField(TEXT("AnimationTags"), Row.AnimationTags); // LAST (R9)
		MoveArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("Moves"), MoveArray);

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}
