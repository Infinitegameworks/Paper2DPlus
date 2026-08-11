// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCueEditorSettings.h"

namespace
{
	FSimpleMulticastDelegate GPaper2DPlusPreviewAdaptersChanged;
}

FSimpleMulticastDelegate& UPaper2DPlusFrameCueEditorSettings::OnPreviewAdaptersChanged()
{
	return GPaper2DPlusPreviewAdaptersChanged;
}

void UPaper2DPlusFrameCueEditorSettings::NotifyPreviewAdaptersChanged()
{
	GPaper2DPlusPreviewAdaptersChanged.Broadcast();
}

bool UPaper2DPlusFrameCueEditorSettings::RegisterPreviewAdapter(
	const TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>& AdapterClass,
	bool bSaveConfig)
{
	if (AdapterClass.IsNull() || PreviewAdapters.Contains(AdapterClass))
	{
		return false;
	}

	PreviewAdapters.Add(AdapterClass);
	if (bSaveConfig)
	{
		SaveConfig();
	}
	NotifyPreviewAdaptersChanged();
	return true;
}

#if WITH_EDITOR
void UPaper2DPlusFrameCueEditorSettings::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	const FName MemberPropertyName = PropertyChangedEvent.MemberProperty
		? PropertyChangedEvent.MemberProperty->GetFName()
		: NAME_None;
	const FName PreviewAdaptersName =
		GET_MEMBER_NAME_CHECKED(UPaper2DPlusFrameCueEditorSettings, PreviewAdapters);
	if (PropertyName == PreviewAdaptersName || MemberPropertyName == PreviewAdaptersName)
	{
		NotifyPreviewAdaptersChanged();
	}
}
#endif
