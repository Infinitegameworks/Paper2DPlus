// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FCharacterProfileEditorModel;
class FFrameCueDataProvider;
class SFrameEventEditor;

/** Full Frame Cue authoring mode for one selected real layer. It mounts the proven SFrameEventEditor once,
 *  with the canonical Layer provider; there is no second Cue editor or preview host. */
class PAPER2DPLUSEDITOR_API SLayerCueInspector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLayerCueInspector) {}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		SLATE_ARGUMENT(TSharedPtr<SFrameEventEditor>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void HandleHostActivated();
	void HandleHostDeactivated();
	TSharedPtr<SFrameEventEditor> GetControllerForTests() const { return Controller; }
	TSharedPtr<FFrameCueDataProvider> GetProviderForTests() const;

private:
	FText GetSourceSummary() const;
	TSharedPtr<FCharacterProfileEditorModel> Model;
	TSharedPtr<SFrameEventEditor> Controller;
};
