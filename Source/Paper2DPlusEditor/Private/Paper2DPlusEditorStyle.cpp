// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusEditorStyle.h"
#include "Paper2DPlusEditorIcons.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

TSharedPtr<FSlateStyleSet> FPaper2DPlusEditorStyle::StyleInstance = nullptr;

FName FPaper2DPlusEditorStyle::GetStyleSetName()
{
	static const FName StyleSetName(TEXT("Paper2DPlusEditorStyle"));
	return StyleSetName;
}

const ISlateStyle& FPaper2DPlusEditorStyle::Get()
{
	return *StyleInstance;
}

void FPaper2DPlusEditorStyle::Register()
{
	if (StyleInstance.IsValid())
	{
		return;
	}

	TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>(GetStyleSetName());

	// Content root = the plugin's Resources/ folder (ships with the plugin; editor-only).
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Paper2DPlus"));
	if (Plugin.IsValid())
	{
		Style->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));
	}

	const FVector2D Icon16(16.0f, 16.0f);

	// Custom bundled icons (Resources/Icons/*.svg) — bespoke, sensible glyphs where the engine app-style
	// has no fitting brush. Add an icon = drop an SVG here + one Set() line. (FrameTiming = clock; the
	// engine FAppStyle has no clock — Sequencer's time icons live in Sequencer's own style.)
	auto SetSvg = [&Style, &Icon16](const TCHAR* BrushName, const TCHAR* SvgName)
	{
		Style->Set(BrushName, new FSlateVectorImageBrush(Style->RootToContentDir(SvgName, TEXT(".svg")), Icon16));
	};
	SetSvg(TEXT("Paper2DPlus.Tab.FrameTiming"), TEXT("Icons/FrameTiming")); // clock
	SetSvg(TEXT("Paper2DPlus.Tab.Animations"),  TEXT("Icons/Animations"));  // film strip
	SetSvg(TEXT("Paper2DPlus.Tab.Hitbox"),      TEXT("Icons/Hitbox"));      // bounds box + hit point
	SetSvg(TEXT("Paper2DPlus.Tab.Sprite"),      TEXT("Icons/Sprite"));      // image (frame + sun + hills)
	SetSvg(Paper2DPlusEditorIcons::MenuPaper2DPlusActions,    TEXT("Icons/Paper2DPlus"));          // stacked sprite frames + plus
	SetSvg(Paper2DPlusEditorIcons::MenuAddToCharacterProfile, TEXT("Icons/AddToCharacterProfile")); // profile card + plus
	SetSvg(Paper2DPlusEditorIcons::MenuFlipbookDraw,          TEXT("Icons/FlipbookDraw"));          // flipbook frame + pencil

	StyleInstance = Style;
	FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
}

void FPaper2DPlusEditorStyle::Unregister()
{
	if (StyleInstance.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
		StyleInstance.Reset();
	}
}
