// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ISlateStyle;
class FSlateStyleSet;

/**
 * The Paper2DPlus editor's own Slate style set (audit — UI/icon standardization).
 *
 * Bundles CUSTOM plugin icons (SVG, under Resources/Icons/) so editor glyphs aren't limited to borrowed
 * engine app-style brushes — e.g. the Frame Timing clock the engine FAppStyle doesn't provide. Brush names
 * are referenced via Paper2DPlusEditorIcons (style set name = "Paper2DPlusEditorStyle"). Registered on
 * module startup, unregistered on shutdown. To add an icon: drop an SVG in Resources/Icons/ and add one
 * Style->Set(...) line in Register().
 */
class FPaper2DPlusEditorStyle
{
public:
	static void Register();
	static void Unregister();

	/** Style set name — keep in sync with Paper2DPlusEditorIcons::StyleSet. */
	static FName GetStyleSetName();
	static const ISlateStyle& Get();

private:
	static TSharedPtr<FSlateStyleSet> StyleInstance;
};
