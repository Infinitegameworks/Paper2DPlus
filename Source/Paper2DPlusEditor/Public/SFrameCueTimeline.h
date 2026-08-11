// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProfileToolPanelProvider.h"
#include "Widgets/SCompoundWidget.h"

class FFrameCueDataProvider;
class FMenuBuilder;
class SFrameEventTimelineTrack;
class SButton;
class SComboButton;
class SSplitter;
class SVerticalBox;
class UPaper2DPlusCharacterProfileAsset;
class UPaper2DPlusCueBase;
struct FPaper2DPlusFrameCueTrackLayout;

namespace Paper2DPlusFrameCueTimeline
{
	enum class ETimingContext : uint8
	{
		NoAnimation,
		ResolvedZeroFrames,
		Timed
	};

	enum class EDragAxis : uint8
	{
		Pending,
		Horizontal,
		Vertical
	};

	enum class EPrimarySelectionKind : uint8
	{
		None,
		Track,
		Cue,
		Curve
	};

	/**
	 * Timeline-owned primary selection. The active Add Cue track is intentionally stored by the
	 * widget separately, so selecting a curve cannot silently retarget a later Add Cue action.
	 */
	struct PAPER2DPLUSEDITOR_API FPrimarySelection
	{
		EPrimarySelectionKind Kind = EPrimarySelectionKind::None;
		FProfileScopedAnimationIdentity Scope;
		FGuid TrackId;
		TWeakObjectPtr<UPaper2DPlusCueBase> Cue;
		FName CurveName;

		void Clear();
		void SelectTrack(FGuid InTrackId);
		void SelectCue(UPaper2DPlusCueBase* InCue, FGuid InTrackId);
		void SelectCurve(FName InCurveName);
		bool IsInScope(const FProfileScopedAnimationIdentity& CurrentScope) const;
		bool Reconcile(
			bool bScopeValid,
			const TSet<FGuid>& ValidOptionalTrackIds,
			const TSet<TWeakObjectPtr<UPaper2DPlusCueBase>>& ValidCues,
			const TSet<FName>* ValidCurveNames = nullptr);
	};

	/** Deterministic dominant-axis lock. Equal diagonals resolve horizontally; Range handles forbid vertical. */
	PAPER2DPLUSEDITOR_API EDragAxis ResolveDragAxis(
		FVector2D ScreenDelta,
		float TriggerDistance,
		bool bAllowVertical);

	/** Signed, bounded per-pointer-event scroll step for a viewport edge band. */
	PAPER2DPLUSEDITOR_API float ComputeEdgeAutoscrollDelta(
		float PointerCoordinate,
		float ViewportExtent,
		float EdgeBand = 32.0f,
		float MaximumStep = 24.0f);

	/** Minimal scroll offset that reveals an item without moving an already visible item. */
	PAPER2DPLUSEDITOR_API float ResolveRevealScrollOffset(
		float CurrentOffset,
		float ViewportExtent,
		float ItemStart,
		float ItemEnd,
		float Margin = 16.0f);

	/** Single key-frame geometry authority shared by ruler, Cue lanes, playhead, and curves. */
	struct PAPER2DPLUSEDITOR_API FTimingGeometry
	{
		static constexpr float PixelsPerKeyFrame = 52.0f;
		static constexpr float MinimumNonTimingBodyWidth = 312.0f;

		static ETimingContext ResolveContext(bool bHasResolvedAnimation, int32 FrameCount);
		static float GetBodyWidth(bool bHasResolvedAnimation, int32 FrameCount);
		static bool TryResolveFrameAtX(
			bool bHasResolvedAnimation,
			int32 FrameCount,
			float LocalX,
			int32& OutFrameIndex);
	};

	/** Bounded editor-only legend gutter shared by the ruler, Cue lanes, and curve legend. */
	struct PAPER2DPLUSEDITOR_API FGutterGeometry
	{
		static constexpr float DefaultWidth = 148.0f;
		static constexpr float MinimumWidth = 124.0f;
		static constexpr float MaximumWidth = 184.0f;
		/** Visible timing viewport floor; the horizontally scrolling content keeps its 312px floor. */
		static constexpr float MinimumBodyViewportWidth = 156.0f;

		static float ClampRequestedWidth(float RequestedWidth);
	};

	/** Layout-only input. TrackId is invalid for the implicit Default track. */
	struct PAPER2DPLUSEDITOR_API FPlacement
	{
		int32 CueIndex = INDEX_NONE;
		FGuid TrackId;
		int32 StartFrame = 0;
		int32 FrameCount = 1;
	};

	/** Deterministic, transient presentation result. Never serialize Sublane. */
	struct PAPER2DPLUSEDITOR_API FPackedPlacement
	{
		int32 CueIndex = INDEX_NONE;
		FGuid TrackId;
		int32 StartFrame = 0;
		int32 EndFrameInclusive = 0;
		int32 Sublane = 0;
	};

	/** Default is always the first result and uses an invalid TrackId. */
	struct PAPER2DPLUSEDITOR_API FPackedTrack
	{
		FGuid TrackId;
		TArray<FPackedPlacement> Placements;
		int32 SublaneCount = 1;

		bool IsDefault() const { return !TrackId.IsValid(); }
	};

	/**
	 * Greedily packs placements into the first non-intersecting sublane per named track.
	 *
	 * Ordering is StartFrame, inclusive EndFrame, then authoritative CueIndex. Unknown,
	 * duplicate, or invalid track IDs project to Default without mutating source data.
	 */
	PAPER2DPLUSEDITOR_API TArray<FPackedTrack> PackPlacements(
		const TArray<FPlacement>& Placements,
		const TArray<FGuid>& OrderedOptionalTrackIds);
}

DECLARE_DELEGATE_OneParam(
	FOnFrameCuePrimarySelectionChanged,
	const Paper2DPlusFrameCueTimeline::FPrimarySelection&);

/**
 * Persistent Frame Cues timing surface: primary actions, one bounded resizable legend gutter, one
 * scrolling ruler/body, compact named Cue lanes, and the shared curve stack beneath them.
 */
class PAPER2DPLUSEDITOR_API SFrameCueTimeline final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFrameCueTimeline) {}
		SLATE_ARGUMENT(TSharedPtr<FFrameCueDataProvider>, DataProvider)
		SLATE_ATTRIBUTE(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>, Asset)
		SLATE_ATTRIBUTE(const TArray<TObjectPtr<UPaper2DPlusCueBase>>*, Cues)
		SLATE_ATTRIBUTE(int32, SelectedFlipbookIndex)
		SLATE_ATTRIBUTE(int32, SelectedCueIndex)
		SLATE_ATTRIBUTE(int32, SelectedFrameIndex)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, RulerContent)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, CurveContent)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, CurveLegendContent)
		SLATE_EVENT(FSimpleDelegate, OnAddCue)
		SLATE_ARGUMENT(TFunction<TSharedRef<SWidget>()>, GetAddCurveMenuContent)
		SLATE_EVENT(FSimpleDelegate, OnStructureChanged)
		SLATE_EVENT(FOnFrameCuePrimarySelectionChanged, OnPrimarySelectionChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	TSharedPtr<SFrameEventTimelineTrack> GetCueLane() const { return CueLane; }
	TWeakPtr<SWidget> GetAddCueFocusTarget() const { return AddCueButton; }
	FGuid GetActiveTrackId() const { return ActiveTrackId; }
	const Paper2DPlusFrameCueTimeline::FPrimarySelection& GetPrimarySelection() const
	{
		return PrimarySelection;
	}
	void SetActiveTrackId(const FGuid& TrackId);
	void HandleCueSelected(int32 CueIndex);
	void HandleCurveSelected(FName CurveName);
	void ClearPrimarySelection();
	void ReconcileCurveSelection(const TSet<FName>& ValidCurveNames);
	void RefreshTrackHeaders();
	void HandleHostDeactivated();

	int32 GetVisibleTrackCountForTests() const { return VisibleTrackCount; }
	bool HasPersistentCurveRegionForTests() const { return CurveContent.IsValid(); }
	/** Named (optional) tracks only — the implicit Default lane is never counted. */
	int32 GetNamedTrackCountForTests() const { return GetNamedTrackCount(); }
	/** The overflow that now owns every track command; proves management stayed reachable. */
	bool HasManageTracksOverflowForTests() const { return ManageTracksButton.IsValid(); }
	TSharedRef<SWidget> BuildManageTracksMenuForTests() { return BuildManageTracksMenu(); }
	int32 GetMissingCuePlacementCountForTests() const { return GetMissingCuePlacementCount(); }
	bool RemoveAllMissingCuePlacementsForTests() { return RemoveAllMissingCuePlacements(); }
	/** Runs exactly what the overflow's New track entry runs. */
	void AddTrackFromOverflowForTests() { HandleAddTrack(); }
	void SelectTrackForTests(FGuid TrackId) { SelectTrack(TrackId); }
	bool HasResizableGutterForTests() const;
	float GetRequestedGutterWidthForTests() const { return RequestedGutterWidth; }
	bool ResizeGutterThroughSlotForTests(float Width);

private:
	TSharedPtr<FFrameCueDataProvider> DataProvider;
	TAttribute<TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>> Asset;
	TAttribute<const TArray<TObjectPtr<UPaper2DPlusCueBase>>*> CueSource;
	TAttribute<int32> SelectedFlipbookIndex;
	TAttribute<int32> SelectedCueIndex;
	TAttribute<int32> SelectedFrameIndex;
	TSharedPtr<SWidget> RulerContent;
	TSharedPtr<SWidget> CurveContent;
	TSharedPtr<SWidget> CurveLegendContent;
	FSimpleDelegate OnAddCue;
	TFunction<TSharedRef<SWidget>()> GetAddCurveMenuContent;
	FSimpleDelegate OnStructureChanged;
	FOnFrameCuePrimarySelectionChanged OnPrimarySelectionChanged;

	TSharedPtr<SFrameEventTimelineTrack> CueLane;
	TSharedPtr<SButton> AddCueButton;
	/** The one labeled overflow that replaced the persistent + Add Track button. */
	TSharedPtr<SComboButton> ManageTracksButton;
	TSharedPtr<class SBorder> MissingCueNotice;
	TSharedPtr<SVerticalBox> TrackHeadersBox;
	TSharedPtr<class SScrollBox> VerticalScrollBox;
	TSharedPtr<class SScrollBox> HorizontalScrollBox;
	TSharedPtr<SSplitter> TimelineSplitter;
	FGuid ActiveTrackId;
	Paper2DPlusFrameCueTimeline::FPrimarySelection PrimarySelection;
	int32 VisibleTrackCount = 0;
	float RequestedGutterWidth =
		Paper2DPlusFrameCueTimeline::FGutterGeometry::DefaultWidth;

	static constexpr float RulerHeaderHeight = 72.0f;
	static constexpr float CurveHeaderHeight = 34.0f;

	FProfileScopedAnimationIdentity GetScopeIdentity() const;
	bool HasResolvedAnimation() const;
	int32 GetFrameCount() const;
	bool CanAddCue() const;
	bool CanEditCurves() const;
	int32 GetMissingCuePlacementCount() const;
	FText GetMissingCueNoticeText() const;
	bool RemoveAllMissingCuePlacements();
	FReply HandleRemoveAllMissingCuePlacements();
	/** Copy-free read of the editor-only track sidecar for this scope; null when nothing is resolved. */
	const FPaper2DPlusFrameCueTrackLayout* GetTrackLayoutForRead() const;
	/** Optional authored tracks; the implicit Default lane is not one of them. */
	int32 GetNamedTrackCount() const;
	bool HasNamedTracks() const;
	FText GetActiveTrackLabel() const;
	/** Live Add-Cue-target check for one header row; an invalid id means the implicit Default lane. */
	bool IsTrackRowActive(const FGuid& TrackId) const;
	FReply HandleAddCue();
	FReply HandleAddTrack();
	/** Every track command, plus the Add Cue target choice, in one overflow. */
	TSharedRef<SWidget> BuildManageTracksMenu();
	TSharedRef<SWidget> BuildTrackMenu(FGuid TrackId, FString DisplayName, int32 OptionalIndex);
	/** Shared body so the lane-header combo and the overflow submenu can never drift apart. */
	void PopulateTrackMenu(
		FMenuBuilder& MenuBuilder,
		FGuid TrackId,
		FString DisplayName,
		int32 OptionalIndex);
	bool RunTrackCommand(TFunctionRef<bool()> Command);
	void NotifyStructureChanged();
	void SelectTrack(FGuid TrackId);
	void BroadcastPrimarySelection();
	void ReconcilePrimarySelection(const TSet<FName>* ValidCurveNames = nullptr);
	void HandleEdgeAutoscrollRequested(
		Paper2DPlusFrameCueTimeline::EDragAxis Axis,
		FVector2D ScreenPosition);
	void HandleGutterSlotResized(float NewWidth);
	void RevealCueSelection(int32 CueIndex);
	void RevealScrollRange(
		const TSharedPtr<class SScrollBox>& ScrollBox,
		float ItemStart,
		float ItemEnd);
};
