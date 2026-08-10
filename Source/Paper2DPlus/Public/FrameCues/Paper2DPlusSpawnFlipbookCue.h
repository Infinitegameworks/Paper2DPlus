// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "PaperFlipbookComponent.h"
#include "Paper2DPlusSpawnFlipbookCue.generated.h"

class UPaperFlipbook;

/**
 * One-shot visual flipbook effect owned by whichever Spawn Flipbook Cue created it.
 *
 * Destroys itself when playback finishes through a bound zero-parameter UFUNCTION rather than a
 * stack-local timer handle, so rapid re-triggering can never orphan an instance. Looping is forced
 * off by the spawn site: a component that never finishes would never release itself.
 */
UCLASS(NotBlueprintable, HideCategories = (Object))
class PAPER2DPLUS_API UPaper2DPlusEffectFlipbookComponent : public UPaperFlipbookComponent
{
	GENERATED_BODY()

public:
	UPaper2DPlusEffectFlipbookComponent();

private:
	UFUNCTION()
	void HandleEffectFinishedPlaying();
};

/**
 * THE one concrete native Cue: spawns a self-destroying one-shot flipbook effect when its frame
 * is crossed.
 *
 * v8.0.0 deletes the six former built-in Cue classes and ships exactly this one native Cue in
 * their place — a deliberate, user-approved decision, because a placeable "spawn an effect here"
 * Cue with an editor-draggable offset proved too useful to require every project to re-author it.
 * Everything else stays true to the v8 model: the placement stores what it needs directly (never
 * an Effect Profile row lookup), the soft Flipbook reference is warmed structurally through
 * CollectWarmableEffectArt, behavior runs synchronously on the shared placement, and the spawned
 * component owns its own lifetime after the callback returns.
 *
 * The Offset is authored in the character's local space (X right before facing, Y up) and is the
 * property the Frame Cues preview lets designers drag directly on the canvas.
 */
UCLASS(meta = (DisplayName = "Spawn Flipbook Cue"))
class PAPER2DPLUS_API UPaper2DPlusSpawnFlipbookCue : public UPaper2DPlusCue
{
	GENERATED_BODY()

public:
	UPaper2DPlusSpawnFlipbookCue();

	/** The effect art. Soft on purpose: warmed before the animation plays, never loaded on a server. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	TSoftObjectPtr<UPaperFlipbook> EffectFlipbook;

	/**
	 * Local offset from the anchor in world units: +X in front of the character (pre-facing),
	 * +Y up. Draggable directly on the Frame Cues preview canvas.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	FVector2D Offset = FVector2D::ZeroVector;

	/** Rotation in degrees in the sprite plane. Mirrored with the character when flipping. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	float Rotation = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	FVector2D Scale = FVector2D(1.0f, 1.0f);

	/** Mirror the offset, rotation, and art across the character's facing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	bool bFlipWithCharacter = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	FLinearColor Tint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect", meta = (ClampMin = "0.01"))
	float PlayRate = 1.0f;

	/** Follow the character while playing instead of staying where it spawned. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	bool bAttachToCharacter = false;

	/** Spawn relative to the render origin or to an authored Character Profile socket. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	EPaper2DPlusFrameCueAnchorKind Anchor = EPaper2DPlusFrameCueAnchorKind::RenderOrigin;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement",
		meta = (EditCondition = "Anchor == EPaper2DPlusFrameCueAnchorKind::ProfileSocket"))
	FString ProfileSocketName;

	virtual void OnCueTriggered_Implementation(
		const FPaper2DPlusFrameCueContext& Context) override;

	/**
	 * Pure placement math shared by runtime spawn, the editor preview ghost, and tests. Built
	 * from the anchor's world location plus its ABSOLUTE X/Z scale and one facing sign — the
	 * Paper2DPlusLayerDraw convention — rather than raw transform composition, because a
	 * character may encode facing as negative X scale OR a 180-degree yaw and the two compose
	 * differently. When bInFlipWithCharacter is set and the character faces left, the offset X,
	 * the angle, and the art (via negative X scale) all mirror together.
	 */
	static FTransform ComputeEffectWorldTransform(
		const FVector& AnchorWorldLocation,
		float AbsAnchorScaleX,
		float AbsAnchorScaleZ,
		bool bFacingLeft,
		const FVector2D& InOffset,
		float InRotationDegrees,
		const FVector2D& InScale,
		bool bInFlipWithCharacter);

	/**
	 * The spawn body, exposed so the editor preview host and tests can drive the exact runtime
	 * path against an explicit world/context. Returns the spawned component or null (no world,
	 * unresolvable anchor, or missing art).
	 */
	UPaper2DPlusEffectFlipbookComponent* SpawnEffect(
		const FPaper2DPlusFrameCueContext& Context) const;
};
