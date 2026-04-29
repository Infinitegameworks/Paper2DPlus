// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "Paper2DPlusValidateCommandlet.generated.h"

/**
 * Commandlet that walks every `UPaper2DPlusCharacterProfileAsset` in the project and
 * runs `ValidateCharacterProfileAsset` on each. Designed for fab-pipeline CI and
 * pre-commit hooks so data-integrity issues (stale phase group refs, orphan tag
 * mappings, null sprites, out-of-range frame events) are caught before runtime.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe <uproject> -run=Paper2DPlusValidate [-JsonOutput=path.json]
 *
 * Exit codes:
 *   0  — no errors (warnings and info-level issues are OK)
 *   1  — one or more Error-severity issues found
 *   2  — infrastructure failure (asset registry unavailable, etc.)
 *
 * Plain-text output is always written to stdout. `-JsonOutput=<path>` additionally
 * writes a structured JSON report for tool integration.
 *
 * Tracked as AI-7 in docs/future-features.md.
 */
UCLASS()
class PAPER2DPLUSEDITOR_API UPaper2DPlusValidateCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UPaper2DPlusValidateCommandlet();

	virtual int32 Main(const FString& Params) override;
};
