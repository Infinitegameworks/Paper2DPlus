// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/MessageDialog.h"

namespace Paper2DPlusEditor::DestructiveActionUtils
{
struct FDestructiveActionPrompt
{
	FText Title;
	FText Body;
	FText Consequence;
	TArray<FString> AffectedItems;
	bool bCanUndo = true;
	int32 MaxAffectedItems = 8;
};

inline void AppendParagraph(FString& Message, const FText& Paragraph)
{
	if (Paragraph.IsEmpty())
	{
		return;
	}

	if (!Message.IsEmpty())
	{
		Message += LINE_TERMINATOR;
		Message += LINE_TERMINATOR;
	}

	Message += Paragraph.ToString();
}

inline void AppendAffectedItems(FString& Message, const TArray<FString>& Items, int32 MaxItems)
{
	if (Items.Num() == 0)
	{
		return;
	}

	if (!Message.IsEmpty())
	{
		Message += LINE_TERMINATOR;
		Message += LINE_TERMINATOR;
	}

	Message += TEXT("Affected:");
	const int32 DisplayCount = MaxItems <= 0 ? Items.Num() : FMath::Min(Items.Num(), MaxItems);
	for (int32 Index = 0; Index < DisplayCount; ++Index)
	{
		Message += LINE_TERMINATOR;
		Message += FString::Printf(TEXT("  - %s"), *Items[Index]);
	}

	const int32 Remaining = Items.Num() - DisplayCount;
	if (Remaining > 0)
	{
		Message += LINE_TERMINATOR;
		Message += FString::Printf(TEXT("  - ... and %d more"), Remaining);
	}
}

inline bool Confirm(const FDestructiveActionPrompt& Prompt)
{
	FString Message;
	AppendParagraph(Message, Prompt.Body);
	AppendAffectedItems(Message, Prompt.AffectedItems, Prompt.MaxAffectedItems);
	AppendParagraph(Message, Prompt.Consequence);
	AppendParagraph(Message, Prompt.bCanUndo
		? FText::FromString(TEXT("This action can be undone with Ctrl+Z."))
		: FText::FromString(TEXT("This action cannot be undone.")));
	AppendParagraph(Message, FText::FromString(TEXT("Continue?")));

	const FText DialogMessage = FText::FromString(Message);
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	return FMessageDialog::Open(EAppMsgType::YesNo, DialogMessage, Prompt.Title) == EAppReturnType::Yes;
#else
	return FMessageDialog::Open(EAppMsgType::YesNo, DialogMessage, &Prompt.Title) == EAppReturnType::Yes;
#endif
}
}
