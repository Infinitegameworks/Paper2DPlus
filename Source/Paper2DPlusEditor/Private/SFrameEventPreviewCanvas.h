// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"

class FFrameEventPreviewViewportClient;
class FPaper2DPlusFrameCuePreviewHost;
class FPreviewScene;
class UPaperFlipbook;
class UPaper2DPlusCharacterProfileAsset;
class UPaper2DPlusCharacterLayerAsset;
class FCharacterProfileEditorModel;
class UPaper2DPlusCueBase;
class UPaper2DPlusFrameCuePreviewContext;
class UPaper2DPlusSpawnFlipbookCue;

DECLARE_DELEGATE_OneParam(FOnPreviewCueOffsetDragStarted, UPaper2DPlusCueBase*);
DECLARE_DELEGATE_OneParam(FOnPreviewCueOffsetChanged, FVector2D);
DECLARE_DELEGATE(FOnPreviewCueOffsetDragEnded);

/**
 * Frame Cue preview viewport -- renders an isolated editor world plus optional adapter overlays.
 *
 * Ordinary Cue behavior owns the primary result: spawned actors/components, Paper2D/particle effects,
 * sound and world debug drawing render through the same scene. Adapters remain an advanced overlay
 * seam. The viewport owns exactly ONE edit gesture: when the selected placement is the native Spawn
 * Flipbook Cue, its authored Offset is draggable through the standard translate widget; the owning
 * editor supplies the transaction through the drag delegates, so the viewport still writes nothing.
 */
class SFrameEventPreviewCanvas : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SFrameEventPreviewCanvas) {}
		SLATE_ATTRIBUTE(UPaperFlipbook*, Flipbook)
		SLATE_ATTRIBUTE(int32, FrameIndex)
		// Bindable (not a one-time argument): the Character Layer editor's Base-Profile picker reinitializes
		// the shared model to a new profile, so the canvas must re-resolve the live asset every read (sprite
		// offsets, selected-cue status) instead of holding a stale construction-time pointer. Mirrors the
		// SSpriteThumbnail.Sprite bindable-attribute pattern.
		SLATE_ATTRIBUTE(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Asset)
		/** Optional canonical Layer source: paints the live composite through the same placement math. */
		SLATE_ARGUMENT(TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset>, LayerAsset)
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		/** Live provider-backed cue source. Optional for legacy standalone tests/hosts. */
		SLATE_ATTRIBUTE(const TArray<TObjectPtr<UPaper2DPlusCueBase>>*, Cues)
		SLATE_ATTRIBUTE(int32, FlipbookIndex)
		SLATE_ATTRIBUTE(int32, SelectedEventIndex)
		SLATE_ARGUMENT(FPaper2DPlusFrameCuePreviewHost*, PreviewHost)
		/** Offset-drag gesture brackets. The editor owns the transaction and the property write. */
		SLATE_EVENT(FOnPreviewCueOffsetDragStarted, OnCueOffsetDragStarted)
		SLATE_EVENT(FOnPreviewCueOffsetChanged, OnCueOffsetChanged)
		SLATE_EVENT(FOnPreviewCueOffsetDragEnded, OnCueOffsetDragEnded)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFrameEventPreviewCanvas() override;

	/** Readable non-color-only preview state, public as an editor-test seam. */
	static FText GetPreviewStatusText(
		const UPaper2DPlusCueBase* SelectedCue,
		const UPaper2DPlusFrameCuePreviewContext* Context,
		int32 HostResourceCount = INDEX_NONE);
	FText GetAccessibleSummaryText() const;
	FPreviewScene* GetViewportPreviewSceneForTests() const;
	bool IsStandardGridEnabledForTests() const;
	bool AreDesignerCueVisualsEnabledForTests() const;
	FString GetDisabledDesignerCueVisualsForTests() const;
	/** Drives the real viewport client's world clock without requiring a rendered Slate frame. */
	void TickViewportForTests(float DeltaSeconds);
	/** Source edits/profile swaps invalidate the paint-hot overlay metric cache. */
	void ResetCachedGeometry();

	// ── Spawn Flipbook Cue offset gizmo ──────────────────────────────────────────────────────────
	// The viewport client surfaces the standard translate widget for exactly one placement class.
	// The canvas converts world drags into authored-offset space; the editor owns the transaction.

	/** The selected placement when (and only when) it is the one draggable native cue class. */
	UPaper2DPlusSpawnFlipbookCue* ResolveDraggableSpawnCue() const;
	bool CanDragSelectedCueOffset() const;
	/** Where the translate widget sits: the cue's effect transform against the preview subject. */
	FVector GetSelectedCueOffsetWorldLocation() const;
	/** Full effect transform for the ghost footprint; false when no draggable cue is selected. */
	bool TryGetSelectedCueEffectTransform(FTransform& OutTransform) const;
	bool HasActiveOffsetInteraction() const { return bOffsetDragLive; }
	/**
	 * Settles a live offset gesture exactly once (host deactivation, selection loss, placement
	 * replacement). Flags reset BEFORE the end delegate fires so a re-entrant settle is a no-op.
	 */
	void CancelActiveInteraction();
	void BeginOffsetInteractionForTests();
	void ApplyOffsetDragDeltaForTests(const FVector& WorldDelta)
	{
		HandleOffsetDragDelta(WorldDelta);
	}

	/** Viewport-client callbacks; not for external callers. */
	void HandleOffsetDragStarted();
	void HandleOffsetDragDelta(const FVector& WorldDelta);
	void HandleOffsetDragEnded();

	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual void Tick(
		const FGeometry& AllottedGeometry,
		const double InCurrentTime,
		const float InDeltaTime) override;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	TAttribute<UPaperFlipbook*> Flipbook;
	TAttribute<int32> FrameIndex;
	TAttribute<TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>> Asset;
	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerAsset;
	TWeakPtr<FCharacterProfileEditorModel> ModelWeak;
	TAttribute<const TArray<TObjectPtr<UPaper2DPlusCueBase>>*> CueSource;
	TAttribute<int32> FlipbookIndex;
	TAttribute<int32> SelectedEventIndex;
	FPaper2DPlusFrameCuePreviewHost* PreviewHost = nullptr;
	TSharedPtr<FFrameEventPreviewViewportClient> PreviewViewportClient;
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> CachedLayerProfile;
	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> CachedLayerAsset;
	int32 CachedLayerFlipbookIndex = INDEX_NONE;
	int32 CachedLayerFrameIndex = INDEX_NONE;
	uint32 LayerSourceRevision = 1;
	uint32 CachedLayerSourceRevision = 0;
	uint32 CachedPreviewWorldRevision = MAX_uint32;
	uint32 CachedPreviewSubjectRevision = MAX_uint32;

	FOnPreviewCueOffsetDragStarted OnCueOffsetDragStartedDelegate;
	FOnPreviewCueOffsetChanged OnCueOffsetChangedDelegate;
	FOnPreviewCueOffsetDragEnded OnCueOffsetDragEndedDelegate;
	/** Unrounded offset accumulator for the live gesture; the authored value is written per emit. */
	FVector2D OffsetDragAccumulator = FVector2D::ZeroVector;
	bool bOffsetDragLive = false;

	const UPaper2DPlusCueBase* ResolveSelectedCue() const;
	FName ResolveAnimationName() const;
	/**
	 * Anchor basis for the gizmo and drag conversion: the real anchor resolver against a locally
	 * built preview context (so Profile Socket anchors sit where the effect will actually spawn),
	 * falling back to the subject component when the resolver cannot succeed mid-authoring.
	 */
	bool TryResolveGizmoBasis(
		const UPaper2DPlusSpawnFlipbookCue& Cue,
		FVector& OutAnchorLocation,
		float& OutAbsScaleX,
		float& OutAbsScaleZ,
		bool& bOutFacingLeft) const;
};
