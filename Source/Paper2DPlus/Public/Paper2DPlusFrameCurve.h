// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/RichCurve.h"
#include "Paper2DPlusFrameCurve.generated.h"

/**
 * Per-curve interpolation mode (TASK-74). One mode per curve (Key Decision: "Per-curve interpolation
 * mode"). Maps 1:1 onto the engine's ERichCurveInterpMode used by the embedded FRichCurve keys:
 * Linear -> RCIM_Linear, Constant (step) -> RCIM_Constant, Cubic -> RCIM_Cubic.
 */
UENUM(BlueprintType)
enum class EPaper2DPlusCurveInterp : uint8
{
	/** Straight-line interpolation between key-frame values (default). */
	Linear		UMETA(DisplayName = "Linear"),
	/** Step/hold — the value holds until the next key (for discrete signals e.g. armor on/off). */
	Constant	UMETA(DisplayName = "Constant"),
	/** Smooth cubic interpolation between key-frame values. */
	Cubic		UMETA(DisplayName = "Cubic")
};

/**
 * One named auxiliary per-frame float curve (TASK-74).
 *
 * Keys are pinned to ANIMATION KEY-FRAME indices: a key's RichCurve Time (X) is the integer key-frame
 * index, its Value (Y) is the scalar at that frame. Between keys the value is interpolated per the
 * curve's single Mode. Sampling at an integer key frame is exact; sub-frame playback times interpolate.
 *
 * Storage is an embedded FRichCurve so the engine handles serialization, interpolation, and (in PR2)
 * the editor curve widget. All key math funnels through Eval/SetKeyValue here so consumers never poke
 * the RichCurve directly.
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusFrameCurve
{
	GENERATED_BODY()

	/** Interpolation mode applied to EVERY key on this curve (one mode per curve). BlueprintReadOnly so
	 *  callers can't desync the keys — mutate via SetMode(), which re-stamps all existing keys' RichCurve
	 *  interp modes. (The PR2 editor routes the mode dropdown through SetMode.) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Curves")
	EPaper2DPlusCurveInterp Mode = EPaper2DPlusCurveInterp::Linear;

	/** Underlying rich curve. Key Time = key-frame index (float), Key Value = scalar at that frame.
	 *  NOTE: FRichCurve is USTRUCT() (not BlueprintType), so this field is EditAnywhere only — BP code
	 *  reads/writes curves through the library query + SetKeyValue/Eval, never this field directly. */
	UPROPERTY(EditAnywhere, Category = "Curves")
	FRichCurve Curve;

	/** Translate the per-curve Mode to the engine's RichCurve interp enum. */
	static ERichCurveInterpMode ToRichCurveInterpMode(EPaper2DPlusCurveInterp InMode)
	{
		switch (InMode)
		{
		case EPaper2DPlusCurveInterp::Constant:	return RCIM_Constant;
		case EPaper2DPlusCurveInterp::Cubic:	return RCIM_Cubic;
		case EPaper2DPlusCurveInterp::Linear:
		default:								return RCIM_Linear;
		}
	}

	/** Add or update a key at the given key-frame index, stamping it with this curve's Mode.
	 *  Frame is the integer key-frame index (stored as the key Time). */
	void SetKeyValue(int32 Frame, float Value)
	{
		const FKeyHandle Handle = Curve.UpdateOrAddKey(static_cast<float>(Frame), Value);
		Curve.SetKeyInterpMode(Handle, ToRichCurveInterpMode(Mode));
	}

	/** Evaluate the curve at the given (possibly fractional) frame. Returns Default when the curve has
	 *  no keys (an unauthored curve reads as the supplied default, R4).
	 *
	 *  CROSS-VERSION (UE 5.0-5.8): Linear/Cubic delegate to FRichCurve::Eval (identical on all engines).
	 *  CONSTANT (step) curves are evaluated MANUALLY because the engine changed the EvalForTwoKeys constant
	 *  branch in UE 5.3: on UE 5.2 and earlier a step curve sampled EXACTLY on a key time returns the LEFT
	 *  key value, while UE 5.3+ correctly returns that key own value (InTime less-than Key2.Time picks the
	 *  left key, else the right key). Cancel/HitStop/HitWindow step curves are sampled exactly on integer
	 *  key frames, so on 5.0-5.2 the un-fixed engine read the wrong (off-by-one-key) value, a real gameplay
	 *  divergence (cancel windows opening a frame late, hit-windows mis-indexing). The manual lookup below
	 *  returns the value of the latest key whose time is at-or-before Frame (matching 5.3+ on every engine),
	 *  or the earliest key value when Frame is before all keys. */
	float Eval(float Frame, float Default) const
	{
		if (Curve.GetNumKeys() == 0)
		{
			return Default;
		}
		if (Mode == EPaper2DPlusCurveInterp::Constant)
		{
			const TArray<FRichCurveKey>& Keys = Curve.GetConstRefOfKeys();
			const FRichCurveKey* Best = &Keys[0]; // before-first-key => first (earliest) key's value
			for (const FRichCurveKey& Key : Keys)
			{
				if (Frame >= Key.Time && Key.Time >= Best->Time)
				{
					Best = &Key;
				}
			}
			return Best->Value;
		}
		return Curve.Eval(Frame, Default);
	}

	/** True if this curve has at least one authored key. */
	bool HasKeys() const { return Curve.GetNumKeys() > 0; }

	/** True if a key sits on the given key-frame index (rounded key Time == Frame). Lets callers probe
	 *  before opening a transaction so a no-op delete/move never dirties the asset. */
	bool HasKeyAtFrame(int32 Frame) const
	{
		for (const FRichCurveKey& Key : Curve.GetConstRefOfKeys())
		{
			if (FMath::RoundToInt(Key.Time) == Frame)
			{
				return true;
			}
		}
		return false;
	}

	/** Remove the key whose rounded Time equals the given key-frame index. Returns false when no key
	 *  sits on that frame. THE single delete funnel — editor code must call this instead of scanning
	 *  FKeyHandles on the embedded RichCurve (the old editor-side scan was the TASK-84 5.1 break site:
	 *  FKeyHandle::IsValid() is not a member pre-5.x). */
	bool DeleteKeyAtFrame(int32 Frame)
	{
		for (auto It = Curve.GetKeyHandleIterator(); It; ++It)
		{
			if (FMath::RoundToInt(Curve.GetKeyTime(*It)) == Frame)
			{
				Curve.DeleteKey(*It);
				return true;
			}
		}
		return false;
	}

	/** Move the key on OldFrame to NewFrame, keeping its value. Returns false when no key sits on
	 *  OldFrame. Moving onto an OCCUPIED frame REPLACES the occupant (dedupe — the moved key wins),
	 *  matching the editor coerce-funnel's "most recently changed key wins" collision rule. The write
	 *  lands through SetKeyValue, so the curve's Mode invariant is re-stamped exactly like every other
	 *  write funnel (a key can never desync from the per-curve Mode by being moved). A same-frame move
	 *  is a successful position no-op — but it STILL lands through SetKeyValue so the Mode re-stamp
	 *  applies even then (every successful move goes through the write funnel, no exceptions). */
	bool MoveKeyToFrame(int32 OldFrame, int32 NewFrame)
	{
		// Find the source key first so an absent source never mutates anything.
		float Value = 0.f;
		bool bFound = false;
		for (auto It = Curve.GetKeyHandleIterator(); It; ++It)
		{
			if (FMath::RoundToInt(Curve.GetKeyTime(*It)) == OldFrame)
			{
				Value = Curve.GetKeyValue(*It);
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			return false;
		}
		if (OldFrame == NewFrame)
		{
			// Same-frame: UpdateOrAddKey updates the key in place and SetKeyValue re-stamps Mode, so a
			// no-op move can never leave a raw-added key desynced from the per-curve Mode.
			SetKeyValue(OldFrame, Value);
			return true;
		}
		// Delete source, then any occupant of the destination (moved key wins), then re-add through
		// the SetKeyValue funnel so Mode is stamped.
		DeleteKeyAtFrame(OldFrame);
		DeleteKeyAtFrame(NewFrame);
		SetKeyValue(NewFrame, Value);
		return true;
	}

	/** Change the per-curve interpolation mode and re-stamp every existing key to match. */
	void SetMode(EPaper2DPlusCurveInterp InMode)
	{
		Mode = InMode;
		const ERichCurveInterpMode RichMode = ToRichCurveInterpMode(Mode);
		for (auto It = Curve.GetKeyHandleIterator(); It; ++It)
		{
			Curve.SetKeyInterpMode(*It, RichMode);
		}
	}
};

/**
 * Sub-struct holding all named auxiliary curves for one animation (TASK-74). Sibling to the other
 * FFlipbookProfileEntry sub-structs (FFlipbookFrameEventData / FFlipbookMotionData). Empty by default
 * and additive-optional, so existing assets deserialize byte-identically (no schema bump, no migration).
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FFlipbookCurveData
{
	GENERATED_BODY()

	/** Named float curves keyed by FName. Free-form names; a project-settings registry
	 *  (UPaper2DPlusSettings::KnownCurves) lists well-known names for the editor picker (PR2).
	 *  EditAnywhere only — BP reads curve values through the library query, not this map directly
	 *  (the embedded FRichCurve is not BlueprintType). */
	UPROPERTY(EditAnywhere, Category = "Curves")
	TMap<FName, FPaper2DPlusFrameCurve> Curves;

	/** True if any curve is authored on this animation. */
	bool HasCurves() const { return Curves.Num() > 0; }
};
