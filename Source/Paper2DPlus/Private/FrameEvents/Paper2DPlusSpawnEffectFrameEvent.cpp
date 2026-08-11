// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameEvents/Paper2DPlusSpawnEffectFrameEvent.h"

UPaper2DPlusSpawnEffectFrameEvent::UPaper2DPlusSpawnEffectFrameEvent()
{
	// Visual effects are world-visible cosmetics — networked-correct default (TASK-57 U1).
	NetPolicy = EPaper2DPlusFrameEventNetPolicy::CosmeticOnly;
}
FPaper2DPlusEffectSpawnSettings UPaper2DPlusSpawnEffectFrameEvent::GetDirectSpawnSettings() const
{
	FPaper2DPlusEffectSpawnSettings Settings;
	Settings.EffectFlipbook = EffectFlipbook;
	Settings.Offset = Offset;
	Settings.Rotation = Rotation;
	Settings.Scale = Scale;
	Settings.bFlipWithCharacter = bFlipWithCharacter;
	Settings.Tint = Tint;
	return Settings;
}

bool UPaper2DPlusSpawnEffectFrameEvent::ResolveSpawnSettings(FPaper2DPlusEffectSpawnSettings& OutSettings) const
{
	OutSettings = GetDirectSpawnSettings();

	if (EffectProfile && !EffectName.IsNone())
	{
		FPaper2DPlusEffectSpawnSettings ProfileSettings;
		if (EffectProfile->ResolveEffectSpawnSettings(EffectName, ProfileSettings))
		{
			OutSettings = ProfileSettings;
			if (bOverrideEffectFlipbook)
			{
				OutSettings.EffectFlipbook = EffectFlipbook;
			}
			if (bOverrideOffset)
			{
				OutSettings.Offset = Offset;
			}
			if (bOverrideRotation)
			{
				OutSettings.Rotation = Rotation;
			}
			if (bOverrideScale)
			{
				OutSettings.Scale = Scale;
			}
			if (bOverrideFlipWithCharacter)
			{
				OutSettings.bFlipWithCharacter = bFlipWithCharacter;
			}
			if (bOverrideTint)
			{
				OutSettings.Tint = Tint;
			}
		}
	}

	return OutSettings.IsValid();
}
