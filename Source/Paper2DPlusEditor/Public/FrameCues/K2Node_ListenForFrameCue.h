// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_ListenForFrameCue.generated.h"

class UPaper2DPlusCueBase;

/** Async Frame Cue listener whose Cue output follows the literal Cue Class selection. */
UCLASS()
class PAPER2DPLUSEDITOR_API UK2Node_ListenForFrameCue : public UK2Node_BaseAsyncTask
{
	GENERATED_BODY()

public:
	UK2Node_ListenForFrameCue(const FObjectInitializer& ObjectInitializer);

	virtual void AllocateDefaultPins() override;
	virtual void PinDefaultValueChanged(UEdGraphPin* ChangedPin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void PostReconstructNode() override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetMenuCategory() const override;

	UClass* GetSelectedCueClass() const;

private:
	static const FName CueClassPinName;
	static const FName CueOutputPinName;
	void RefreshCueOutputType();
};
