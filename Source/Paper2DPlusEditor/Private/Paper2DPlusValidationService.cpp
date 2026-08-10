// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusValidationService.h"

#include "ProfileValidationAdapter.h"
#include "UObject/Class.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusValidationService"

namespace
{
	int32 SeveritySortRank(EPaper2DPlusValidationSeverity Severity)
	{
		switch (Severity)
		{
		case EPaper2DPlusValidationSeverity::Error:
			return 0;
		case EPaper2DPlusValidationSeverity::Warning:
			return 1;
		default:
			return 2;
		}
	}

	FString EscapeStableKeyPart(FString Value)
	{
		Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Value.ReplaceInline(TEXT("|"), TEXT("\\|"));
		Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		return Value;
	}

	bool IssueLess(const FPaper2DPlusValidationIssue& A, const FPaper2DPlusValidationIssue& B)
	{
		const int32 ASeverity = SeveritySortRank(A.Severity);
		const int32 BSeverity = SeveritySortRank(B.Severity);
		if (ASeverity != BSeverity)
		{
			return ASeverity < BSeverity;
		}

		const FString APath = A.AssetPath.ToString();
		const FString BPath = B.AssetPath.ToString();
		if (APath != BPath) return APath < BPath;
		if (A.AssetType != B.AssetType) return A.AssetType.ToString() < B.AssetType.ToString();
		const FString ACharacterPath = A.CharacterPath.ToString();
		const FString BCharacterPath = B.CharacterPath.ToString();
		if (ACharacterPath != BCharacterPath) return ACharacterPath < BCharacterPath;
		if (A.Scope != B.Scope) return A.Scope.ToString() < B.Scope.ToString();
		if (A.ItemIdentity != B.ItemIdentity) return A.ItemIdentity < B.ItemIdentity;
		if (A.Field != B.Field) return A.Field.ToString() < B.Field.ToString();
		if (A.Code != B.Code) return A.Code.ToString() < B.Code.ToString();

		return A.StableKey < B.StableKey;
	}
}

FText FPaper2DPlusValidationSummary::GetStatusText() const
{
	if (bValidating)
	{
		return LOCTEXT("ValidatingStatus", "Validating…");
	}
	if (NumErrors > 0)
	{
		return FText::Format(
			LOCTEXT("ErrorStatus", "{0} errors, {1} warnings, {2} info"),
			FText::AsNumber(NumErrors),
			FText::AsNumber(NumWarnings),
			FText::AsNumber(NumInfo));
	}
	if (NumWarnings > 0)
	{
		return FText::Format(
			LOCTEXT("WarningStatus", "No errors, {0} warnings, {1} info"),
			FText::AsNumber(NumWarnings),
			FText::AsNumber(NumInfo));
	}
	if (NumInfo > 0)
	{
		return FText::Format(
			LOCTEXT("InfoStatus", "No errors or warnings, {0} info"),
			FText::AsNumber(NumInfo));
	}
	return LOCTEXT("CleanStatus", "No validation issues");
}

FText FPaper2DPlusValidationSummary::GetAccessibleText() const
{
	return GetStatusText();
}

FName FPaper2DPlusValidationSummary::GetStatusIconName() const
{
	if (bValidating) return TEXT("Icons.Refresh");
	if (NumErrors > 0) return FPaper2DPlusValidationService::SeverityIconName(EPaper2DPlusValidationSeverity::Error);
	if (NumWarnings > 0) return FPaper2DPlusValidationService::SeverityIconName(EPaper2DPlusValidationSeverity::Warning);
	if (NumInfo > 0) return FPaper2DPlusValidationService::SeverityIconName(EPaper2DPlusValidationSeverity::Info);
	return TEXT("Icons.Check");
}

FPaper2DPlusValidationSummary FPaper2DPlusValidationSummary::FromIssues(
	const TArray<FPaper2DPlusValidationIssue>& Issues,
	bool bInValidating)
{
	FPaper2DPlusValidationSummary Result;
	Result.bValidating = bInValidating;
	for (const FPaper2DPlusValidationIssue& Issue : Issues)
	{
		switch (Issue.Severity)
		{
		case EPaper2DPlusValidationSeverity::Error:
			++Result.NumErrors;
			break;
		case EPaper2DPlusValidationSeverity::Warning:
			++Result.NumWarnings;
			break;
		default:
			++Result.NumInfo;
			break;
		}
	}
	return Result;
}

FPaper2DPlusValidationService& FPaper2DPlusValidationService::Get()
{
	static FPaper2DPlusValidationService Instance;
	return Instance;
}

FPaper2DPlusValidationService::FPaper2DPlusValidationService()
{
	Paper2DPlusProfileValidationAdapter::RegisterBuiltInAdapters(*this);
}

bool FPaper2DPlusValidationService::RegisterAdapter(
	FName AdapterId,
	FName AssetClassPath,
	FPaper2DPlusValidationAdapterDelegate Adapter)
{
	if (AdapterId.IsNone() || AssetClassPath.IsNone() || !Adapter.IsBound())
	{
		return false;
	}

	Adapters.RemoveAll([AdapterId](const FAdapterEntry& Entry)
	{
		return Entry.AdapterId == AdapterId;
	});

	FAdapterEntry& Entry = Adapters.AddDefaulted_GetRef();
	Entry.AdapterId = AdapterId;
	Entry.AssetClassPath = AssetClassPath;
	Entry.Adapter = MoveTemp(Adapter);

	Adapters.Sort([](const FAdapterEntry& A, const FAdapterEntry& B)
	{
		return A.AdapterId.ToString() < B.AdapterId.ToString();
	});

	AdaptersChanged.Broadcast();
	return true;
}

bool FPaper2DPlusValidationService::UnregisterAdapter(FName AdapterId)
{
	const int32 Removed = Adapters.RemoveAll([AdapterId](const FAdapterEntry& Entry)
	{
		return Entry.AdapterId == AdapterId;
	});
	if (Removed > 0)
	{
		AdaptersChanged.Broadcast();
		return true;
	}
	return false;
}

const FPaper2DPlusValidationService::FAdapterEntry* FPaper2DPlusValidationService::FindAdapter(
	const UObject* Asset) const
{
	if (!Asset)
	{
		return nullptr;
	}

	// Most-derived matching class wins. AdapterId ordering (maintained at registration) is the stable
	// tie-breaker when a host deliberately registers more than one adapter for the same class path.
	for (const UClass* Class = Asset->GetClass(); Class; Class = Class->GetSuperClass())
	{
		const FName ClassPath(*Class->GetPathName());
		for (const FAdapterEntry& Entry : Adapters)
		{
			if (Entry.AssetClassPath == ClassPath)
			{
				return &Entry;
			}
		}
	}
	return nullptr;
}

bool FPaper2DPlusValidationService::HasAdapterFor(const UObject* Asset) const
{
	return FindAdapter(Asset) != nullptr;
}

void FPaper2DPlusValidationService::GetRegisteredAssetClassPaths(TArray<FName>& OutClassPaths) const
{
	OutClassPaths.Reset(Adapters.Num());
	for (const FAdapterEntry& Entry : Adapters)
	{
		OutClassPaths.AddUnique(Entry.AssetClassPath);
	}
	OutClassPaths.Sort(FNameLexicalLess());
}

bool FPaper2DPlusValidationService::ValidateObject(
	const UObject* Asset,
	TArray<FPaper2DPlusValidationIssue>& OutIssues) const
{
	OutIssues.Reset();
	const FAdapterEntry* Entry = FindAdapter(Asset);
	if (!Asset || !Entry)
	{
		return false;
	}

	Entry->Adapter.Execute(*Asset, OutIssues);
	const FSoftObjectPath AssetPath(Asset->GetPathName());
	for (FPaper2DPlusValidationIssue& Issue : OutIssues)
	{
		if (!Issue.AssetPath.IsValid())
		{
			Issue.AssetPath = AssetPath;
		}
		if (Issue.AssetType.IsNone())
		{
			Issue.AssetType = Asset->GetClass()->GetFName();
		}
	}

	NormalizeAndSort(OutIssues);
	return true;
}

void FPaper2DPlusValidationService::NormalizeAndSort(
	TArray<FPaper2DPlusValidationIssue>& InOutIssues)
{
	for (FPaper2DPlusValidationIssue& Issue : InOutIssues)
	{
		if (Issue.Code.IsNone())
		{
			Issue.Code = TEXT("Paper2DPlus.Validation.Unspecified");
		}
		if (Issue.StableKey.IsEmpty())
		{
			Issue.StableKey = MakeStableKey(Issue);
		}
	}

	InOutIssues.Sort(IssueLess);
	TSet<FString> SeenKeys;
	InOutIssues.RemoveAll([&SeenKeys](const FPaper2DPlusValidationIssue& Issue)
	{
		if (SeenKeys.Contains(Issue.StableKey))
		{
			return true;
		}
		SeenKeys.Add(Issue.StableKey);
		return false;
	});
}

FString FPaper2DPlusValidationService::MakeStableKey(const FPaper2DPlusValidationIssue& Issue)
{
	TArray<FString> Parts;
	Parts.Reserve(8);
	Parts.Add(EscapeStableKeyPart(Issue.AssetPath.ToString()));
	Parts.Add(EscapeStableKeyPart(Issue.AssetType.ToString()));
	Parts.Add(EscapeStableKeyPart(Issue.CharacterPath.ToString()));
	Parts.Add(EscapeStableKeyPart(Issue.Code.ToString()));
	Parts.Add(EscapeStableKeyPart(Issue.StableDiscriminator));
	Parts.Add(EscapeStableKeyPart(Issue.Scope.ToString()));
	Parts.Add(EscapeStableKeyPart(Issue.ItemIdentity));
	Parts.Add(EscapeStableKeyPart(Issue.Field.ToString()));
	return FString::Join(Parts, TEXT("|"));
}

FText FPaper2DPlusValidationService::SeverityText(EPaper2DPlusValidationSeverity Severity)
{
	switch (Severity)
	{
	case EPaper2DPlusValidationSeverity::Error:
		return LOCTEXT("SeverityError", "Error");
	case EPaper2DPlusValidationSeverity::Warning:
		return LOCTEXT("SeverityWarning", "Warning");
	default:
		return LOCTEXT("SeverityInfo", "Info");
	}
}

FName FPaper2DPlusValidationService::SeverityIconName(EPaper2DPlusValidationSeverity Severity)
{
	switch (Severity)
	{
	case EPaper2DPlusValidationSeverity::Error:
		return TEXT("Icons.Error");
	case EPaper2DPlusValidationSeverity::Warning:
		return TEXT("Icons.Warning");
	default:
		return TEXT("Icons.Info");
	}
}

#undef LOCTEXT_NAMESPACE
