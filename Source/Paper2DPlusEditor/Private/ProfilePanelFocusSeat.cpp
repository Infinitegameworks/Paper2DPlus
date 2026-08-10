// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfilePanelFocusSeat.h"

#include "SlateShortcutUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWidget.h"

bool FProfilePanelFocusSeat::ShouldRequestSeat(const SWidget& Panel) const
{
	return FSlateApplication::IsInitialized()
		&& !TimerHandle.IsValid()
		&& ShouldSeatNow(Panel);
}

void FProfilePanelFocusSeat::TrackTimer(const TSharedRef<FActiveTimerHandle>& Handle)
{
	TimerHandle = Handle;
}

TSharedPtr<FActiveTimerHandle> FProfilePanelFocusSeat::GetPendingTimer() const
{
	return TimerHandle.Pin();
}

EActiveTimerReturnType FProfilePanelFocusSeat::ApplySeat(
	const TSharedRef<SWidget>& Panel,
	const bool bHostActive)
{
	if (bHostActive && FSlateApplication::IsInitialized() && ShouldSeatNow(Panel.Get()))
	{
		// Docking activates the tab synchronously from OnFocusChanging. Suppress only that re-entry
		// so it cannot repeat the panel's full refresh while focus is being committed.
		TGuardValue<bool> ReentryGuard(bApplying, true);
		FSlateApplication::Get().SetKeyboardFocus(Panel, EFocusCause::SetDirectly);
	}
	TimerHandle.Reset();
	return EActiveTimerReturnType::Stop;
}

bool FProfilePanelFocusSeat::ShouldSeatNow(const SWidget& Panel) const
{
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}
	// The focused-widget type only matters when the focus path actually runs through this panel;
	// resolving it unconditionally would let an unrelated editor widget veto the seat.
	const bool bHasFocusedDescendants = Panel.HasFocusedDescendants();
	FString FocusedWidgetTypeName;
	if (bHasFocusedDescendants)
	{
		if (const TSharedPtr<SWidget> FocusedWidget =
			FSlateApplication::Get().GetKeyboardFocusedWidget())
		{
			FocusedWidgetTypeName = FocusedWidget->GetType().ToString();
		}
	}
	return Paper2DPlusEditor::SlateShortcutUtils::ShouldSeatDeferredHostFocus(
		Panel.HasKeyboardFocus(),
		bHasFocusedDescendants,
		FocusedWidgetTypeName);
}
