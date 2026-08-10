// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FAnimationProfilePickerSource;
class FCharacterProfileEditorModel;
class FFrameCueDataProvider;
class FHitboxFrameDataProvider;
class IProfileItemPickerSource;
class SAnimationProfileSwitcher;
class SFrameEventEditor;
class SHitboxEditorPanel;
class SLayerArtInspector;
class SLayerAppearancePanel;
class SLayerCueInspector;
class SLayerHitboxInspector;
class SLayerOverviewPanel;
class SLayerStructureTree;
class SProfileNavigatorPanel;
class SWidgetSwitcher;
class UPaper2DPlusCharacterLayerAsset;

enum class ELayerAuthoringMode : uint8
{
	Art,
	Hitboxes,
	FrameCues,
	Appearance
};

struct PAPER2DPLUSEDITOR_API FLayerWorkspaceResponsiveState
{
	bool bShowAnimationDrawer = false;
	bool bShowStructureColumn = true;
	bool bShowStructureOverlayButton = false;

	static FLayerWorkspaceResponsiveState Resolve(float Width, bool bDrawerRequested);
};

/**
 * U11 Layer-first home: one compact shared animation picker, optional virtualized drawer, stable structure,
	 * proven preview/frame strip, selected-layer inspectors, and generic Appearance authoring.
 */
class PAPER2DPLUSEDITOR_API SLayerAuthoringWorkspace : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLayerAuthoringWorkspace) {}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		SLATE_ARGUMENT(UPaper2DPlusCharacterLayerAsset*, LayerAsset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SLayerAuthoringWorkspace() override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	TSharedPtr<SLayerOverviewPanel> GetOverviewPanel() const { return OverviewPanel; }
	TSharedPtr<SLayerStructureTree> GetStructureTreeForTests() const { return StructureTree; }
	TSharedPtr<SHitboxEditorPanel> GetHitboxControllerForTests() const { return HitboxController; }
	TSharedPtr<SLayerCueInspector> GetCueInspectorForTests() const { return CueInspector; }
	TSharedPtr<SLayerArtInspector> GetArtInspectorForTests() const { return ArtInspector; }
	TSharedPtr<SLayerAppearancePanel> GetAppearancePanelForTests() const { return AppearancePanel; }
	TSharedPtr<IProfileItemPickerSource> GetAnimationSourceForTests() const;
	ELayerAuthoringMode GetModeForTests() const { return ActiveMode; }
	void SetModeForTests(ELayerAuthoringMode Mode);
	/** Settle active gestures/playback before a non-undoable multi-package publish command. */
	void PrepareForPublish();
	/** Restore only lifecycle state that was active before PrepareForPublish. */
	void RestoreAfterPublish();
	bool IsAnimationDrawerVisibleForTests() const;
#if WITH_DEV_AUTOMATION_TESTS
	TSharedPtr<SAnimationProfileSwitcher> GetAnimationSwitcherForTests() const { return AnimationSwitcher; }
	TSharedPtr<SProfileNavigatorPanel> GetAnimationDrawerForTests() const { return AnimationDrawer; }
#endif
	/** Live inspector attributes; public so the small shared inspector-page builder can bind them safely. */
	FText GetSelectedLayerTitle() const;
	FText GetSelectedLayerSummary() const;

private:
	FReply NavigateAnimation(int32 Delta);
	bool CanNavigateAnimation(int32 Delta) const;
	FReply ToggleAnimationDrawer();
	FReply ToggleStructureOverlay();
	FReply SetMode(ELayerAuthoringMode Mode);
	ECheckBoxState GetModeCheckState(ELayerAuthoringMode Mode) const;
	EVisibility GetAnimationDrawerVisibility() const;
	EVisibility GetStructureColumnVisibility() const;
	EVisibility GetStructureOverlayButtonVisibility() const;
	EVisibility GetStructureOverlayVisibility() const;
	FText GetEmptyProfileText() const;
	void LoadPreferences();
	void SavePreferences() const;
	void HandleDrawerResized(float NewSize);

	TSharedPtr<FCharacterProfileEditorModel> Model;
	TWeakObjectPtr<UPaper2DPlusCharacterLayerAsset> LayerAsset;
	TSharedPtr<FAnimationProfilePickerSource> AnimationSource;
	TSharedPtr<SAnimationProfileSwitcher> AnimationSwitcher;
	TSharedPtr<SProfileNavigatorPanel> AnimationDrawer;
	TSharedPtr<SLayerStructureTree> StructureTree;
	TSharedPtr<SLayerStructureTree> OverlayStructureTree;
	TSharedPtr<SLayerOverviewPanel> OverviewPanel;
	TSharedPtr<SLayerArtInspector> ArtInspector;
	TSharedPtr<SLayerAppearancePanel> AppearancePanel;
	TSharedPtr<SLayerHitboxInspector> HitboxInspector;
	TSharedPtr<SLayerCueInspector> CueInspector;
	TSharedPtr<FHitboxFrameDataProvider> LayerHitboxProvider;
	TSharedPtr<FFrameCueDataProvider> LayerCueProvider;
	TSharedPtr<SHitboxEditorPanel> HitboxController;
	TSharedPtr<SFrameEventEditor> CueController;
	TSharedPtr<SWidgetSwitcher> CentralModeSwitcher;
	TSharedPtr<SWidgetSwitcher> InspectorSwitcher;
	ELayerAuthoringMode ActiveMode = ELayerAuthoringMode::Art;
	bool bAnimationDrawerRequested = false;
	bool bStructureOverlayOpen = false;
	bool bPublishPrepared = false;
	float AnimationDrawerRatio = 0.22f;
	float LastWidth = 1200.0f;
	FLayerWorkspaceResponsiveState ResponsiveState;
};
