// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Templates/SharedPointer.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FCharacterProfileEditorModel;
class UPaper2DPlusCharacterProfileAsset;

/**
 * Stable identity captured when an animation assignment target is built.
 *
 * The array index is only a capture-time hint. Assignment re-resolves the soft path plus authored
 * name uniquely, so a reordered array remains safe while a stale or duplicated identity fails closed.
 */
struct FExpectedTagAnimationTarget
{
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> OwningAsset;
	int32 CapturedIndex = INDEX_NONE;
	FSoftObjectPath FlipbookPath;
	FString AuthoredName;

	static FExpectedTagAnimationTarget Capture(
		UPaper2DPlusCharacterProfileAsset* Asset,
		int32 FlipbookIndex);

	/** Structural stamp validity only; assignment still re-resolves uniqueness against live data. */
	bool IsStamped() const;
};

/** Read-only preflight used by drop targets for cursor feedback. */
enum class EExpectedTagAssignmentDisposition : uint8
{
	Rejected,
	NoChange,
	Assignable
};

/** Result of one explicit designer assignment gesture. */
enum class EExpectedTagAssignmentResult : uint8
{
	Rejected,
	NoChange,
	Applied
};

/**
 * One mutation funnel for expected-tag assignment.
 *
 * Applied writes only FFlipbookEditorMetadata::AnimationTags and always performs one transaction,
 * one Asset->Modify(), one package-dirty mark, and one model data-change notification.
 */
class FExpectedTagAssignment
{
public:
	static EExpectedTagAssignmentDisposition Evaluate(
		const FGameplayTag& ExpectedTag,
		TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> SourceAsset,
		const FExpectedTagAnimationTarget& Target,
		const TSharedPtr<FCharacterProfileEditorModel>& Model);

	static EExpectedTagAssignmentResult Assign(
		const FGameplayTag& ExpectedTag,
		TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> SourceAsset,
		const FExpectedTagAnimationTarget& Target,
		const TSharedPtr<FCharacterProfileEditorModel>& Model);
};
