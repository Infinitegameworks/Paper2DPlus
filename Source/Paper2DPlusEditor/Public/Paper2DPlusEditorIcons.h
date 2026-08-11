// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Centralized Paper2DPlus editor icon brush names (audit — UI/icon standardization).
 *
 * THE single place to choose every editor tab / context-menu / asset-action glyph, so a concept has ONE
 * icon everywhere and the Character Profile + Character Layer editors can't drift apart. Before this, tab
 * icons were ad-hoc engine-brush borrows: the Profile editor duplicated Sprite==Hitbox and shared one icon
 * across Frame Timing / Frame Events / Root Motion, while the Layer editor used a pencil for all of its
 * (mirrored) tabs. Names resolve against FAppStyle (the engine editor style); call sites build
 * FSlateIcon(FAppStyle::Get().GetStyleSetName(), <name>). To move to a bespoke FSlateStyleSet with custom PNGs
 * later, register these names in that style set (or repoint them here) — no call-site changes needed.
 *
 * Header has no FAppStyle / engine-style include (just string constants) so it's safe in any TU incl. UE 5.0.
 */
namespace Paper2DPlusEditorIcons
{
	// The plugin's OWN style set (FPaper2DPlusEditorStyle) — holds CUSTOM bundled icons (Resources/Icons/*.svg,
	// e.g. the Frame Timing clock the engine app-style lacks). Engine-brush names below resolve via
	// FAppStyle::Get().GetStyleSetName(); custom ones (prefixed "Paper2DPlus.") resolve via this set. Keep in
	// sync with FPaper2DPlusEditorStyle::GetStyleSetName().
	inline constexpr const TCHAR* StyleSet = TEXT("Paper2DPlusEditorStyle");

	// --- Editor tabs (used IDENTICALLY by FCharacterProfileAssetEditorToolkit and
	//     FCharacterLayerAssetEditorToolkit so each concept shows the SAME distinct glyph in both). ---
	inline constexpr const TCHAR* TabFlipbooks   = TEXT("LevelEditor.Tabs.Outliner");        // flipbook list sidebar (engine list glyph — sensible)
	inline constexpr const TCHAR* TabAnimations  = TEXT("Paper2DPlus.Tab.Animations");       // merged Animations tab — CUSTOM film strip (StyleSet)
	inline constexpr const TCHAR* TabHitbox      = TEXT("Paper2DPlus.Tab.Hitbox");           // hitbox authoring — CUSTOM bounds box (StyleSet)
	inline constexpr const TCHAR* TabSprite      = TEXT("Paper2DPlus.Tab.Sprite");           // sprite alignment — CUSTOM image (StyleSet)
	inline constexpr const TCHAR* TabFrameTiming = TEXT("Paper2DPlus.Tab.FrameTiming");      // frame timing — CUSTOM clock SVG (resolves via StyleSet, NOT FAppStyle)
	inline constexpr const TCHAR* TabFrameEvents = TEXT("AnimNotifyEditor.AnimNotify");      // frame events — engine anim-notify marker (thematic)
	inline constexpr const TCHAR* TabRootMotion  = TEXT("AnimGraph.Attribute.RootMotionDelta.Icon"); // root motion — engine root-motion glyph (thematic)

	// --- Flipbook context-menu actions (reuse the tab glyph where the entry opens that tab). ---
	inline constexpr const TCHAR* MenuEditHitbox     = TabHitbox;
	inline constexpr const TCHAR* MenuEditSprite     = TabSprite;
	inline constexpr const TCHAR* MenuEditTiming     = TabFrameTiming;
	inline constexpr const TCHAR* MenuEditEvents     = TabFrameEvents;
	inline constexpr const TCHAR* MenuEditRootMotion = TabRootMotion;
	inline constexpr const TCHAR* MenuOpenFlipbook   = TEXT("LevelEditor.OpenContentBrowser");
	inline constexpr const TCHAR* MenuBrowse         = TEXT("SystemWideCommands.FindInContentBrowser");
	inline constexpr const TCHAR* MenuAddToQueue     = TEXT("Icons.Plus");
	inline constexpr const TCHAR* MenuPaper2DPlusActions     = TEXT("Paper2DPlus.Menu.Actions");
	inline constexpr const TCHAR* MenuAddToCharacterProfile  = TEXT("Paper2DPlus.Menu.AddToCharacterProfile");
	inline constexpr const TCHAR* MenuFlipbookDraw           = TEXT("Paper2DPlus.Menu.FlipbookDraw");

	// --- Asset-action / toolbar glyphs. ---
	inline constexpr const TCHAR* ActionValidate = TEXT("Icons.Check");   // was a misleading help "?" glyph
	inline constexpr const TCHAR* ActionExport   = TEXT("Icons.Save");
	inline constexpr const TCHAR* ActionImport   = TEXT("Icons.Import");
}
