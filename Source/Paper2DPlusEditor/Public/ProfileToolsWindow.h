// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Textures/SlateIcon.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class FCharacterProfileEditorModel;
class SWidgetSwitcher;
class SWindow;
class UPaper2DPlusCharacterProfileAsset;

/**
 * One entry in the Profile Tools window's left-hand list.
 *
 * The list is built from a descriptor array so adding a tool is a local change -- append a
 * descriptor with its surface factory -- rather than an edit to the shell. Nothing in the shell
 * switches on a tool's identity.
 */
struct FProfileToolDescriptor
{
	/** Stable identity for entry points, the console seam, and tests. Never shown to the user. */
	FName Id;
	FText Label;
	FText Tooltip;
	FSlateIcon Icon;
	/** Builds this tool's surface once, when the window is constructed. */
	TFunction<TSharedRef<SWidget>()> MakeSurface;
};

/**
 * The Profile Tools window -- occasional-use Character Profile tools, deliberately outside the
 * Character Profile editor's tab set so that editor stays sleek.
 *
 * Master-detail: a scrollable tool list on the left, the selected tool's surface on the right.
 * The switcher lives INSIDE a splitter slot and never the reverse: SSplitter::FOnSlotResized
 * silently never fires when a splitter is hosted inside an SWidgetSwitcher, with no assert and a
 * divider that still looks draggable. Inactive switcher slots also do not run active timers
 * (timers fire only from SWidget::Paint), so no tool surface may depend on ticking while hidden.
 *
 * Replaces FCharacterDataEditorToolkit. That was a second asset-editor toolkit with its own model
 * and layout key, and a persisted closed-tab state could restore it with zero docked tabs and no
 * route back (TASK-139). A plain SWindow has no layout state to persist, which removes that
 * failure class rather than re-versioning around it -- at the cost of the asset-lifetime services
 * InitAssetEditor used to provide, which this window pays for explicitly by watching for deletion
 * and object replacement itself.
 */
class PAPER2DPLUSEDITOR_API SProfileToolsWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProfileToolsWindow) {}
		SLATE_ARGUMENT(UPaper2DPlusCharacterProfileAsset*, Profile)
		/** Selects this tool on open. NAME_None (or an unknown id) selects the first entry. */
		SLATE_ARGUMENT(FName, InitialToolId)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SProfileToolsWindow() override;

	/** Stable tool identities. */
	static const FName ToolId_CharacterData;
	static const FName ToolId_CharacterSizing;
	static const FName ToolId_SpriteBounds;
	static const FName ToolId_ReExtract;
	static const FName ToolId_Validation;
	static const FName ToolId_PaperZDSequences;

	/**
	 * THE entry point. Single-instance: a second request focuses the live window and selects
	 * ToolId on it rather than constructing a second one.
	 */
	static void OpenProfileTools(UPaper2DPlusCharacterProfileAsset* Profile, FName ToolId = NAME_None);

	/** Selects by identity. Returns false when no descriptor carries that id. */
	bool SelectToolById(FName ToolId);

	/**
	 * Selects by index. No-op when the index already matches the selection -- INCLUDING when it is
	 * out of range. A guard that only no-ops on valid indices lets a mirrored selection re-enter and
	 * re-broadcast forever, which is a synchronous within-frame editor freeze rather than a hang
	 * that looks like one.
	 */
	void SelectTool(int32 Index);

	int32 GetSelectedToolIndex() const { return SelectedToolIndex; }
	const TArray<TSharedPtr<FProfileToolDescriptor>>& GetTools() const { return Tools; }
	UPaper2DPlusCharacterProfileAsset* GetProfile() const { return Profile.Get(); }

#if WITH_DEV_AUTOMATION_TESTS
	/** Counts surface construction so a test can prove re-selection does not rebuild. */
	int32 GetSurfaceBuildCountForTests() const { return SurfaceBuildCount; }
	static TSharedPtr<SProfileToolsWindow> GetActiveContentForTests();
	TSharedPtr<class SProfileSpriteBoundsTool> GetSpriteBoundsToolForTests() const { return SpriteBoundsTool; }
	TSharedPtr<class SProfileReExtractTool> GetReExtractToolForTests() const { return ReExtractTool; }
#endif

private:
	void BuildTools();
	TSharedRef<ITableRow> MakeToolRow(
		TSharedPtr<FProfileToolDescriptor> Item,
		const TSharedRef<STableViewBase>& OwnerTable);
	void HandleToolSelectionChanged(TSharedPtr<FProfileToolDescriptor> Item, ESelectInfo::Type SelectInfo);

	/** Closes the hosting window when the edited profile is deleted out from under it. */
	void HandleAssetsPreDelete(const TArray<UObject*>& Objects);
	/** Rebinds through a reimport/replacement rather than leaving tools writing into a dead object. */
	void HandleObjectsReplaced(const TMap<UObject*, UObject*>& Replacements);
	void CloseHostWindow();

	/** Sprite Bounds reports; Re-extract fixes. The window owns the hop so neither tool has to know
	 *  the other exists. */
	void HandleRouteToReExtract(FString AnimationName);

	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> Profile;
	TSharedPtr<FCharacterProfileEditorModel> EditorModel;

	TArray<TSharedPtr<FProfileToolDescriptor>> Tools;
	TSharedPtr<SListView<TSharedPtr<FProfileToolDescriptor>>> ToolListView;
	TSharedPtr<SWidgetSwitcher> ToolSwitcher;
	TSharedPtr<class SProfileSpriteBoundsTool> SpriteBoundsTool;
	TSharedPtr<class SProfileReExtractTool> ReExtractTool;
	int32 SelectedToolIndex = INDEX_NONE;
	/** Guards the list -> model -> list mirror so a programmatic selection cannot re-enter. */
	bool bSelectingTool = false;
	int32 SurfaceBuildCount = 0;

	FDelegateHandle AssetsPreDeleteHandle;
	FDelegateHandle ObjectsReplacedHandle;
};
