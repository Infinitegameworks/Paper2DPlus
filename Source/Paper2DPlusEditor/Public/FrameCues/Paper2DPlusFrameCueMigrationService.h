// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UPaper2DPlusCharacterLayerAsset;
class UPaper2DPlusCharacterProfileAsset;

/** Whether a migration issue prevents removal of the executable Frame Event compatibility layer. */
enum class EPaper2DPlusFrameCueMigrationDisposition : uint8
{
	Notice,
	Blocking
};

/** Stable issue identity used by validators, commandlets, and editor issue panels. */
enum class EPaper2DPlusFrameCueMigrationIssueKind : uint8
{
	UnsupportedExecutableEvent,
	ConvertibleBuiltInEvent,
	MigratedCueNeedsReceiverAcknowledgement,
	NullLegacyEvent,
	InvalidLegacyAnchor,
	InvalidLegacyRange,
	InvalidLegacyNetworkPolicy,
	NullCue,
	InvalidCueAnchor,
	InvalidCueRange,
	InvalidCueNetworkPolicy
};

/** One deterministic, read-only migration diagnostic. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueMigrationIssue
{
	EPaper2DPlusFrameCueMigrationDisposition Disposition = EPaper2DPlusFrameCueMigrationDisposition::Notice;
	EPaper2DPlusFrameCueMigrationIssueKind Kind = EPaper2DPlusFrameCueMigrationIssueKind::UnsupportedExecutableEvent;
	FString AssetPath;
	FString SourcePath;
	FString AnimationName;
	int32 AnchorFrame = INDEX_NONE;
	int32 AuthoredFrameCount = 0;
	int32 AnimationFrameCount = INDEX_NONE;
	FString ClassPath;
	FString NetworkPolicyContext;
	TArray<FString> PayloadPropertyNames;
	TArray<FString> BlueprintGraphNames;
	FString Message;

	FString GetStableKey() const;
};

/** Sorted aggregate suitable for direct validator or commandlet projection. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueMigrationReport
{
	TArray<FPaper2DPlusFrameCueMigrationIssue> Issues;

	bool HasBlockingIssues() const;
	int32 GetBlockingIssueCount() const;
	int32 GetNoticeCount() const;
	void SortDeterministically();
};

/** Read-only Frame Event-to-Cue migration inventory for Character Profile and Character Layer assets. */
class PAPER2DPLUSEDITOR_API FPaper2DPlusFrameCueMigrationService
{
public:
	static FPaper2DPlusFrameCueMigrationReport AnalyzeCharacterProfile(
		const UPaper2DPlusCharacterProfileAsset& Asset);

	static FPaper2DPlusFrameCueMigrationReport AnalyzeCharacterLayer(
		const UPaper2DPlusCharacterLayerAsset& Asset);

	static void AppendCharacterProfile(
		const UPaper2DPlusCharacterProfileAsset& Asset,
		FPaper2DPlusFrameCueMigrationReport& InOutReport);

	static void AppendCharacterLayer(
		const UPaper2DPlusCharacterLayerAsset& Asset,
		FPaper2DPlusFrameCueMigrationReport& InOutReport);
};
