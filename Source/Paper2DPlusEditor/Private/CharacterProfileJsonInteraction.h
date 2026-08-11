// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/MessageDialog.h"
#include "Paper2DPlusCharacterProfileAsset.h"

/**
 * The ONE interactive Character Profile JSON boundary.
 *
 * Both entry points — the Content Browser asset action and the Character Profile editor's Asset menu —
 * call the Run* helpers below, so file dialogs, the layout disclosure, the Apply/Cancel boundary, warning
 * reporting, and the success/failure notifications cannot drift between the two routes. The pure
 * disclosure/apply seams stay separately callable because focused tests assert on them without Slate.
 */
namespace Paper2DPlusCharacterProfileJsonInteraction
{
	enum class EImportResult : uint8
	{
		Cancelled,
		Failed,
		Applied
	};

	FText GetExportLayoutDisclosure();
	FText BuildImportLayoutPreview(const FString& FilePath);

	/** The single Apply/Cancel mutation boundary used by every interactive import route. */
	EImportResult ApplyImportString(
		UPaper2DPlusCharacterProfileAsset& Asset,
		const FString& JsonString,
		EAppReturnType::Type Response,
		TArray<FPaper2DPlusCharacterProfileJsonImportWarning>& OutWarnings);

	/** Interactive export: save-file dialog, write, and the success/failure notification. */
	void RunInteractiveExport(UPaper2DPlusCharacterProfileAsset& Asset);

	/** Interactive import: open-file dialog, read, layout disclosure, ApplyImportString, warnings, and the
	 *  success/failure notification. Cancelling at either the file dialog or the disclosure is silent. */
	void RunInteractiveImport(UPaper2DPlusCharacterProfileAsset& Asset);
}
