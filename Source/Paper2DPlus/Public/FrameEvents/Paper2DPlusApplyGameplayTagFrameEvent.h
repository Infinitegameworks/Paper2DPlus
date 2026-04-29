// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "FrameEvents/Paper2DPlusFrameEventState.h"
#include "Paper2DPlusApplyGameplayTagFrameEvent.generated.h"

/**
 * Ranged frame event that broadcasts a tag-apply / tag-remove request over the
 * owning component's `OnApplyGameplayTagsRequested` delegate when the range
 * begins / ends. Use for attack-state flags ("IsAttacking"), invuln windows,
 * super-armor ranges, air-attack lockouts.
 *
 * Paper2DPlus does NOT own a tag destination — it broadcasts the request and
 * lets the game bind to route into its own tag system (GAS loose tags, a
 * custom tag component, the player controller, etc.). This keeps the plugin
 * free of GAS module dependency while still offering a typed authoring class.
 *
 * If the character profile component has no OnApplyGameplayTagsRequested
 * binding the event is a silent no-op. A future version could log a warning
 * via the diagnostic-log-on-silent-swallow pattern (see
 * `docs/solutions/ue-audit-compound-patterns.md` Pattern 12) if this proves
 * to be a common authoring mistake.
 */
UCLASS(Blueprintable, DisplayName = "Apply Gameplay Tag Frame Event")
class PAPER2DPLUS_API UPaper2DPlusApplyGameplayTagFrameEvent : public UPaper2DPlusFrameEventState
{
	GENERATED_BODY()

public:
	/** Tags to apply on range begin and remove on range end. */
	UPROPERTY(EditAnywhere, Category = "Gameplay Tags")
	FGameplayTagContainer Tags;

	virtual void OnFrameEventBegin_Implementation(const FPaper2DPlusFrameEventContext& Context) override;
	virtual void OnFrameEventEnd_Implementation(const FPaper2DPlusFrameEventContext& Context) override;
};
