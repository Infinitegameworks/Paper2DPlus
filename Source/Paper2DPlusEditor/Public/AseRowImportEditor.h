// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AsepriteImporter.h"           // FAsepriteParsedData / FPerLayerBufferMap
#include "BulkSpriteExtractorWindow.h"  // FBulkExtractorTextureState
#include "Widgets/SCompoundWidget.h"

class SLayerImportPreviewCanvas;
class STextBlock;
class SVerticalBox;

/**
 * What the per-row editor needs from the bulk window that owns the row.
 *
 * A closure seam rather than a back-pointer, matching `FWardrobeHostApi`: the editor never reaches
 * into the window's state, and the window keeps every decision about batch-wide settings.
 */
struct FAseRowEditorHostApi
{
	/** The batch's "Separate flipbooks only" setting — LIVE, because the designer can change it in
	 *  the batch bar while this window is open. No Layer Profile means no Sections to write. */
	TFunction<bool()> IsSeparateFlipbooksOnly;

	/** Open the Section-suggestion proposal window for this row. */
	TFunction<void()> OpenSectionSuggestions;
};

/**
 * The per-row `.ase` import editor: composited preview, per-layer import/visibility list, per-tag
 * animation checkboxes, and the Section-suggestion action.
 *
 * This is where the retired `SAsepiteLayerImportDialog`'s authoring surface ended up. The difference
 * that matters is ownership: the modal dialog was handed parsed data owned by the factory, whereas a
 * bulk row deliberately keeps only a SUMMARY (a 20-file batch that pinned every decoded frame would
 * cost hundreds of megabytes). So this widget parses on construction and releases on destruction —
 * the full pixel cost exists for exactly as long as the window is open.
 */
class PAPER2DPLUSEDITOR_API SAseRowImportEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAseRowImportEditor) {}
		SLATE_ARGUMENT(TSharedPtr<FBulkExtractorTextureState>, RowState)
		SLATE_ARGUMENT(FAseRowEditorHostApi, Host)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SAseRowImportEditor();

	/** Test seam: how many layer rows the list is currently showing (collapsed groups excluded). */
	int32 GetLayerRowCountForTests() const { return LayerRowCount; }

private:
	TSharedRef<SWidget> BuildSummary();
	TSharedRef<SWidget> BuildPreview();
	TSharedRef<SWidget> BuildLayers();
	TSharedRef<SWidget> BuildAnimations();
	TSharedRef<SWidget> BuildSections();

	void RebuildLayerList();
	void SetPreviewFrame(int32 NewFrame);
	void StepPreviewFrame(int32 Delta);
	/** The frames the scrubber may reach: the scoped tag's [From, To], else the whole file. */
	void GetPreviewRange(int32& OutFrom, int32& OutTo) const;
	int32 CountEnabledLayers() const;
	int32 CountEnabledTags() const;

	TSharedPtr<FBulkExtractorTextureState> RowState;
	FAseRowEditorHostApi Host;

	/** Parsed on Construct, released in the destructor — see the class comment. Null when the file
	 *  failed to parse, in which case the editor renders the row's own error instead of a preview. */
	TSharedPtr<FAsepriteParsedData> ParsedData;
	TSharedPtr<FPerLayerBufferMap> PerLayerBuffers;
	FString ParseError;

	TSharedPtr<SLayerImportPreviewCanvas> PreviewCanvas;
	TSharedPtr<SVerticalBox> LayerListBox;

	/** Collapsed group state (key = layer index of the group layer). */
	TMap<int32, bool> GroupCollapsed;

	/** Tag the preview is scoped to (INDEX_NONE = the whole file). Clicking a tag row scopes the
	 *  scrubber to that animation; clicking it again unscopes. */
	int32 PreviewTagIndex = INDEX_NONE;
	int32 PreviewFrame = 0;
	int32 LayerRowCount = 0;
};
