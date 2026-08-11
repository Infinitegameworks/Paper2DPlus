// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWidget.h"

namespace Paper2DPlusEditor::SlateShortcutUtils
{
	/** Pure characterization seam shared by the focus query and headless shortcut-guard tests. */
	FORCEINLINE bool IsInputWidgetTypeName(const FString& TypeName)
	{
		return TypeName.Contains(TEXT("SEditableText"))
			|| TypeName.Contains(TEXT("SMultiLineEditableText"))
			|| TypeName.Contains(TEXT("SInlineEditableTextBlock"))
			|| TypeName.Contains(TEXT("SNumericEntryBox"))
			|| TypeName.Contains(TEXT("SSpinBox"))
			|| TypeName.Contains(TEXT("SSearchBox"))
			|| TypeName.StartsWith(TEXT("SComboBox"))
			|| TypeName.StartsWith(TEXT("SComboButton"))
			|| TypeName.StartsWith(TEXT("SComboRow"));
	}

	/** Narrow sibling of IsInputWidgetTypeName: TEXT-ENTRY widgets only (a live caret).
	 *  IsInputWidgetTypeName deliberately also matches SNumericEntryBox / SSpinBox / SSearchBox /
	 *  the SComboBox family, which is right for shortcuts that would fight a focused control but
	 *  wrong for window-wide navigation keys — a merely-focused (not yet typing) spinbox would
	 *  swallow every arrow press. Those widgets move focus onto an inner SEditableText once the
	 *  user is actually typing, so matching "SEditableText" alone still preserves caret movement.
	 *  Note "SEditableText" also matches SEditableTextBox by substring, which is intended. */
	FORCEINLINE bool IsTextEntryWidgetTypeName(const FString& TypeName)
	{
		return TypeName.Contains(TEXT("SEditableText"))
			|| TypeName.Contains(TEXT("SMultiLineEditableText"))
			|| TypeName.Contains(TEXT("SInlineEditableTextBlock"));
	}

	FORCEINLINE bool IsFocusedInputWidget()
	{
		const TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (!FocusedWidget.IsValid())
		{
			return false;
		}

		return IsInputWidgetTypeName(FocusedWidget->GetType().ToString());
	}

	FORCEINLINE bool IsFocusedTextEntryWidget()
	{
		const TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (!FocusedWidget.IsValid())
		{
			return false;
		}

		return IsTextEntryWidgetTypeName(FocusedWidget->GetType().ToString());
	}

	FORCEINLINE bool ShouldIgnoreShortcutForFocusedWidget()
	{
		return IsFocusedInputWidget();
	}

	/** Guard for window-wide navigation keys routed through OnPreviewKeyDown: yield to a live caret
	 *  only. Use ShouldIgnoreShortcutForFocusedWidget for ordinary bubbled shortcuts. */
	FORCEINLINE bool ShouldIgnoreTypingShortcutForFocusedWidget()
	{
		return IsFocusedTextEntryWidget();
	}

	/** Descendants that take keyboard focus for their own gestures but deliberately leave Space (and
	 *  the other playback keys) unhandled so they bubble to the hosting tool panel. SCurveEditor is
	 *  the coerce-not-suppress house rule's subject; the Cue lane handles only Delete/arrows/chords. */
	FORCEINLINE bool IsSpaceForwardingWidgetTypeName(const FString& TypeName)
	{
		return TypeName == TEXT("SCurveEditor")
			|| TypeName == TEXT("SFrameEventTimelineTrack");
	}

	/** Pure deferred host-focus seat decision shared by the tool panels' activation seat and its
	 *  headless tests. True = the panel should claim keyboard focus; false = leave focus where it is.
	 *
	 *  A focused descendant is NOT proof a prior seat is still good: a button or spinbox left focused
	 *  from the previous activation swallows Space (Space = Accept on SButton) and the panel's
	 *  playback toggle goes dead. Only two descendant families may keep their focus — widgets that
	 *  forward Space back to the panel, and a live text caret, which must never lose keystrokes.
	 *  Text-entry widgets move focus onto an inner SEditableText while the user is actually typing,
	 *  so the narrow caret check is the mid-edit signal; a merely focused spinbox or search-box shell
	 *  is stale focus and the panel reclaims the keys. */
	FORCEINLINE bool ShouldSeatDeferredHostFocus(
		const bool bPanelHasKeyboardFocus,
		const bool bPanelHasFocusedDescendants,
		const FString& FocusedWidgetTypeName)
	{
		if (bPanelHasKeyboardFocus)
		{
			return false;
		}
		if (!bPanelHasFocusedDescendants)
		{
			return true;
		}
		if (IsSpaceForwardingWidgetTypeName(FocusedWidgetTypeName))
		{
			return false;
		}
		if (IsTextEntryWidgetTypeName(FocusedWidgetTypeName))
		{
			return false;
		}
		return true;
	}
}
