// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SHitboxEditorPanel;

/** Right-side host for the contextual panels exported by the one shared SHitboxEditorPanel controller. */
class PAPER2DPLUSEDITOR_API SLayerHitboxInspector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLayerHitboxInspector) {}
		SLATE_ARGUMENT(TSharedPtr<SHitboxEditorPanel>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	TSharedPtr<SHitboxEditorPanel> GetControllerForTests() const { return Controller; }

private:
	FText GetProjectionSummary() const;
	TSharedPtr<SHitboxEditorPanel> Controller;
};

