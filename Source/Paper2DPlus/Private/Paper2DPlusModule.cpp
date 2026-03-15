// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusModule.h"
#include "UObject/CoreRedirects.h"

DEFINE_LOG_CATEGORY(LogPaper2DPlus);

#define LOCTEXT_NAMESPACE "FPaper2DPlusModule"

void FPaper2DPlusModule::StartupModule()
{
	// Register asset redirects for backward compatibility
	TArray<FCoreRedirect> Redirects;

	// Redirect old BlueprintHitbox asset classes to new Paper2DPlus names
	// NOTE: Collapsed chain — points directly to CharacterProfile (skipping CharacterData)
	Redirects.Emplace(ECoreRedirectFlags::Type_Class,
		TEXT("/Script/BlueprintHitbox.HitboxDataAsset"),
		TEXT("/Script/Paper2DPlus.Paper2DPlusCharacterProfileAsset"));

	// CharacterData -> CharacterProfile rename (2026-03-12)
	Redirects.Emplace(ECoreRedirectFlags::Type_Class,
		TEXT("/Script/Paper2DPlus.Paper2DPlusCharacterDataAsset"),
		TEXT("/Script/Paper2DPlus.Paper2DPlusCharacterProfileAsset"));

	Redirects.Emplace(ECoreRedirectFlags::Type_Struct,
		TEXT("/Script/BlueprintHitbox.AnimationHitboxData"),
		TEXT("/Script/Paper2DPlus.FlipbookHitboxData"));

	Redirects.Emplace(ECoreRedirectFlags::Type_Struct,
		TEXT("/Script/BlueprintHitbox.FrameHitboxData"),
		TEXT("/Script/Paper2DPlus.FrameHitboxData"));

	Redirects.Emplace(ECoreRedirectFlags::Type_Struct,
		TEXT("/Script/BlueprintHitbox.HitboxData"),
		TEXT("/Script/Paper2DPlus.HitboxData"));

	Redirects.Emplace(ECoreRedirectFlags::Type_Struct,
		TEXT("/Script/BlueprintHitbox.SocketData"),
		TEXT("/Script/Paper2DPlus.SocketData"));

	FCoreRedirects::AddRedirectList(Redirects, TEXT("Paper2DPlus"));
}

void FPaper2DPlusModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPaper2DPlusModule, Paper2DPlus)
