// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "HitboxEditorPanel.h"
#include "CharacterProfileEditorModel.h"
#include "FlipbookListBuilder.h"
#include "HitboxDataProvider.h"
// SGameplayTagCombo (the clash-category picker, TASK-77) was added in UE 5.3.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
#include "SGameplayTagCombo.h"
#endif
#include "EditorCanvasUtils.h"
#include "LayerCompositeThumbnail.h"
#include "ProfilePropertyRow.h"
#include "SlateShortcutUtils.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SToolTip.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Engine/Texture2D.h"
#include "ScopedTransaction.h"
#include "Paper2DPlusSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "HitboxEditorPanel"

namespace
{
using AssetUtils = UPaper2DPlusCharacterProfileAsset;

int32 ClampFrameHitboxesToBounds(FFrameHitboxData& Frame, UPaperFlipbook* Flipbook, int32 FrameIndex)
{
	int32 BoundsWidth = 0;
	int32 BoundsHeight = 0;
	if (!AssetUtils::GetFrameSpriteBounds(Flipbook, FrameIndex, BoundsWidth, BoundsHeight))
	{
		return 0;
	}

	int32 ClampedCount = 0;
	for (FHitboxData& Hitbox : Frame.Hitboxes)
	{
		if (AssetUtils::ClampHitboxToBounds(Hitbox, BoundsWidth, BoundsHeight))
		{
			++ClampedCount;
		}
	}

	return ClampedCount;
}
}

const FName SHitboxEditorPanel::HitboxesPanelId(TEXT("Paper2DPlus.Hitbox.Hitboxes"));
const FName SHitboxEditorPanel::PropertiesPanelId(TEXT("Paper2DPlus.Hitbox.Properties"));
const FName SHitboxEditorPanel::FrameOperationsPanelId(TEXT("Paper2DPlus.Hitbox.FrameOperations"));

// ─────────────────────────────────────────────────────────────────────────────
// Construct / Destruct
// ─────────────────────────────────────────────────────────────────────────────

void SHitboxEditorPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	LayerAsset = InArgs._LayerAsset;
	HostContract = InArgs._HostContract.IsValid()
		? InArgs._HostContract
		: FProfileToolPanelHostContract::Embedded();
	if (Model.IsValid())
	{
		Asset = Model->GetAsset();
	}
	Provider = InArgs._FrameDataProvider;
	if (!Provider.IsValid())
	{
		Provider = MakeShared<FProfileHitboxDataProvider>(Model);
	}

	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}

	if (HostContract.OwnsEmbeddedNavigation())
	{
		InitializeSectionLayout();
	}

	if (Model.IsValid())
	{
		ModelFlipbookSelectionHandle = Model->OnFlipbookSelectionChanged.AddLambda([this](int32 /*NewIndex*/)
		{
			RefreshFrameList();
			RefreshHitboxList();
			RefreshPropertiesPanel();
			if (EditorCanvas.IsValid())
			{
				EditorCanvas->ClearSelection();
			}
			// Update 3D viewport
			if (Viewport3D.IsValid())
			{
				const FFrameHitboxData* Frame = GetCurrentFrame();
				Viewport3D->SetFrameData(Frame);
				Viewport3D->SetSprite(GetCurrentSprite());
			}
		});

		ModelFrameSelectionHandle = Model->OnFrameSelectionChanged.AddLambda([this]()
		{
			RefreshFrameList();
			RefreshHitboxList();
			RefreshPropertiesPanel();
			if (EditorCanvas.IsValid())
			{
				EditorCanvas->ClearSelection();
			}
			// Update 3D viewport
			if (Viewport3D.IsValid())
			{
				const FFrameHitboxData* Frame = GetCurrentFrame();
				Viewport3D->SetFrameData(Frame);
				Viewport3D->SetSprite(GetCurrentSprite());
			}
		});

		ModelGroupCollapseHandle = Model->OnGroupCollapseChanged.AddLambda([this]()
		{
		});

		// In the Character Layer editor the frame strip renders the layered composite; a selection change
		// changes which layers are visible, so repaint the strip cells (they read live visibility in OnPaint).
		// Harmless in the Character Profile editor — this delegate never fires there.
		ModelLayerVisibilityHandle = Model->OnLayerVisibilityChanged.AddLambda([this]()
		{
			if (LayerAsset.IsValid() && FrameListBox.IsValid())
			{
				FrameListBox->Invalidate(EInvalidateWidgetReason::Paint);
			}
			if (LayerAsset.IsValid() && EditorCanvas.IsValid())
			{
				EditorCanvas->ResetCachedGeometry();
				EditorCanvas->Invalidate(EInvalidateWidgetReason::Layout);
			}
		});

		ModelAssetExternallyModifiedHandle = Model->OnAssetExternallyModified.AddLambda([this]()
		{
			if (ActiveTransaction.IsValid()) return;
			Provider->InvalidateCachedViews();
			// External restores/reinstances can change array cardinality. Never carry a numeric
			// hitbox/socket selection into the replacement arrays.
			if (EditorCanvas.IsValid())
			{
				EditorCanvas->ClearSelection();
			}
			RefreshAll();
		});

		ModelAssetDataChangedHandle = Model->OnAssetDataChanged.AddLambda([this]()
		{
			if (!ActiveTransaction.IsValid()) Provider->InvalidateCachedViews();
			RefreshAll();
		});

		ModelDirectionalPreviewHandle = Model->OnDirectionalPreviewChanged.AddLambda([this]()
		{
			if (FrameListBox.IsValid())
			{
				RefreshFrameList();
			}
			if (EditorCanvas.IsValid())
			{
				EditorCanvas->ResetCachedGeometry();
				EditorCanvas->Invalidate(EInvalidateWidgetReason::Layout);
			}
			if (Viewport3D.IsValid())
			{
				Viewport3D->SetSprite(GetCurrentSprite());
			}
		});

		// Layer scope only: the provider resolves the stable selected LayerId live, so a structure-tree or
		// exact-canvas layer click re-scopes this whole panel —
		// drop the canvas selection (its indices reference the OLD scope's box array) and re-read.
		if (Provider->IsLayerScoped())
		{
			ModelLayerSelectionHandle = Model->OnLayerSelectionChanged.AddLambda([this](int32 /*NewIndex*/)
			{
				Provider->InvalidateCachedViews();
				if (EditorCanvas.IsValid())
				{
					EditorCanvas->ClearSelection();
					EditorCanvas->ResetCachedGeometry();
					EditorCanvas->Invalidate(EInvalidateWidgetReason::Layout);
				}
				RefreshFrameList();
				RefreshHitboxList();
				RefreshPropertiesPanel();
				if (Viewport3D.IsValid())
				{
					Viewport3D->SetFrameData(GetCurrentFrame());
					Viewport3D->SetSprite(GetCurrentSprite());
				}
			});
		}
	}

	const TSharedRef<SWidget> CentralWorkspace = BuildCentralWorkspace();
	if (HostContract.UsesExternalNavigation())
	{
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(4.0f)
			[
				CentralWorkspace
			]
		];
	}
	else
	{
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(4.0f)
			[
				SNew(SSplitter)
				.Orientation(Orient_Horizontal)
				+ SSplitter::Slot()
				.Value(0.70f)
				[
					CentralWorkspace
				]
				+ SSplitter::Slot()
				.Value(0.30f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(HitboxSidebarSectionsBox, SVerticalBox)
					]
				]
			]
		];
		RebuildHitboxSidebarSections();
	}
}

SHitboxEditorPanel::~SHitboxEditorPanel()
{
	// Close a final programmatic gesture even if editor shutdown bypassed tab activation/capture loss.
	EndTransaction();
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}

	if (Model.IsValid())
	{
		Model->OnFlipbookSelectionChanged.Remove(ModelFlipbookSelectionHandle);
		Model->OnFrameSelectionChanged.Remove(ModelFrameSelectionHandle);
		Model->OnGroupCollapseChanged.Remove(ModelGroupCollapseHandle);
		Model->OnLayerVisibilityChanged.Remove(ModelLayerVisibilityHandle);
		Model->OnAssetExternallyModified.Remove(ModelAssetExternallyModifiedHandle);
		Model->OnAssetDataChanged.Remove(ModelAssetDataChangedHandle);
		Model->OnLayerSelectionChanged.Remove(ModelLayerSelectionHandle);
		Model->OnDirectionalPreviewChanged.Remove(ModelDirectionalPreviewHandle);
	}
}

void SHitboxEditorPanel::GetContextualPanels(
	TArray<FProfileToolPanelDescriptor>& OutPanels) const
{
	if (!HostContract.UsesExternalNavigation())
	{
		return;
	}

	const TWeakPtr<SHitboxEditorPanel> WeakController =
		ConstCastSharedRef<SHitboxEditorPanel>(SharedThis(this));
	auto AddPanel = [&OutPanels, WeakController](
		FName PanelId,
		const FText& Label,
		const FText& ToolTip,
		TFunction<TSharedRef<SWidget>(SHitboxEditorPanel&)> Builder)
	{
		FProfileToolPanelDescriptor Descriptor;
		Descriptor.PanelId = PanelId;
		Descriptor.Label = Label;
		Descriptor.ToolTip = ToolTip;
		Descriptor.CapabilityId = PanelId;
		Descriptor.IsAvailable = [WeakController]() { return WeakController.IsValid(); };
		Descriptor.WidgetFactory = [WeakController, PanelId, Builder = MoveTemp(Builder)]() -> TSharedRef<SWidget>
		{
			const TSharedPtr<SHitboxEditorPanel> Controller = WeakController.Pin();
			if (!Controller.IsValid())
			{
				return SNullWidget::NullWidget;
			}
			++Controller->ContextPanelBuildCounts.FindOrAdd(PanelId);
			return Builder(*Controller);
		};
		OutPanels.Add(MoveTemp(Descriptor));
	};

	AddPanel(
		HitboxesPanelId,
		LOCTEXT("HitboxContextHitboxes", "Hitboxes"),
		LOCTEXT("HitboxContextHitboxesTip", "Add, select, and remove hitboxes and sockets on the current frame."),
		[](SHitboxEditorPanel& Controller) { return Controller.BuildHitboxList(); });
	AddPanel(
		PropertiesPanelId,
		LOCTEXT("HitboxContextProperties", "Properties"),
		LOCTEXT("HitboxContextPropertiesTip", "Edit the live canvas selection's type, position, size, and combat values."),
		[](SHitboxEditorPanel& Controller) { return Controller.BuildPropertiesPanel(); });
	AddPanel(
		FrameOperationsPanelId,
		LOCTEXT("HitboxContextFrameOperations", "Frame Operations"),
		LOCTEXT("HitboxContextFrameOperationsTip", "Copy, merge, clear, clamp, mirror, and batch frame hitbox data."),
		[](SHitboxEditorPanel& Controller) { return Controller.BuildCopyOperationsPanel(); });
}

void SHitboxEditorPanel::HandleHostDeactivated()
{
	if (FSlateApplication::IsInitialized()
		&& EditorCanvas.IsValid()
		&& EditorCanvas->HasMouseCapture())
	{
		// Capture loss resets the canvas drag state and calls OnEndTransaction for a real edit.
		FSlateApplication::Get().ReleaseAllPointerCapture();
	}
	// Synthetic/test gestures and any capture-loss edge that did not callback still close here. End is
	// idempotent, so a normal capture-loss callback plus this fallback remains one transaction close.
	EndTransaction();
}

void SHitboxEditorPanel::SetCanvasSelectionForTests(
	EHitboxSelectionType Type,
	int32 Index)
{
	if (EditorCanvas.IsValid())
	{
		EditorCanvas->SetSelection(Type, Index);
	}
}

EHitboxSelectionType SHitboxEditorPanel::GetCanvasSelectionTypeForTests() const
{
	return EditorCanvas.IsValid()
		? EditorCanvas->GetSelectionType()
		: EHitboxSelectionType::None;
}

int32 SHitboxEditorPanel::GetCanvasPrimarySelectionForTests() const
{
	return EditorCanvas.IsValid()
		? EditorCanvas->GetPrimarySelectedIndex()
		: INDEX_NONE;
}

bool SHitboxEditorPanel::BeginTransactionForTests(const FText& Description)
{
	const int32 PreviousCount = TransactionBeginCount;
	BeginTransaction(Description);
	return TransactionBeginCount == PreviousCount + 1;
}

void SHitboxEditorPanel::ArmCanvasDragForTests(
	EHitboxDragMode Mode,
	bool bTransactionOpen)
{
	if (EditorCanvas.IsValid()) EditorCanvas->ArmDragForTests(Mode, bTransactionOpen);
}

void SHitboxEditorPanel::SettleCanvasDragForTests()
{
	if (EditorCanvas.IsValid()) EditorCanvas->SettleDragForTests();
}

bool SHitboxEditorPanel::HasArmedCanvasDragForTests() const
{
	return EditorCanvas.IsValid() && EditorCanvas->HasArmedDragForTests();
}

// ─────────────────────────────────────────────────────────────────────────────
// FEditorUndoClient
// ─────────────────────────────────────────────────────────────────────────────

void SHitboxEditorPanel::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		Provider->InvalidateCachedViews();
		if (EditorCanvas.IsValid())
		{
			EditorCanvas->ClearSelection();
		}
		RefreshAll();
	}
}

void SHitboxEditorPanel::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		Provider->InvalidateCachedViews();
		if (EditorCanvas.IsValid())
		{
			EditorCanvas->ClearSelection();
		}
		RefreshAll();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Transaction support
// ─────────────────────────────────────────────────────────────────────────────

void SHitboxEditorPanel::BeginTransaction(const FText& Description)
{
	// The provider names the asset that owns the edited frame data: the profile asset in the profile
	// editor (byte-identical), the LAYER asset in the layer editor — save scope stays edit scope.
	UObject* Target = Provider.IsValid() ? Provider->GetTransactionTarget() : nullptr;
	if (!ActiveTransaction.IsValid() && Target)
	{
		ActiveTransaction = MakeUnique<FScopedTransaction>(Description);
		if (Provider->IsLayerScoped())
		{
			// Layer assets created headless can lack RF_Transactional.
			Target->SetFlags(RF_Transactional);
		}
		Target->Modify();
		Provider->BeginEdit();
		++TransactionBeginCount;
	}
}

void SHitboxEditorPanel::EndTransaction()
{
	if (!ActiveTransaction.IsValid())
	{
		return;
	}
	Provider->CommitEdit();
	ActiveTransaction.Reset();
	++TransactionEndCount;
	if (UObject* Target = Provider.IsValid() ? Provider->GetTransactionTarget() : nullptr)
	{
		Target->MarkPackageDirty();
	}
}

void SHitboxEditorPanel::CommitFlipbookRename(
	const FProfileAnimationIdentity& AnimationIdentity,
	const FString& NewName)
{
	// Renaming mutates the PROFILE, but in layer scope BeginTransaction enrolls the LAYER asset — and a
	// rename would also orphan the layer overrides' AnimationName keys. The layer surface hides the
	// rename affordance; this guard backs that up.
	if (Provider.IsValid() && Provider->IsLayerScoped())
	{
		return;
	}

	UPaper2DPlusCharacterProfileAsset* AssetPtr = Model.IsValid() ? Model->GetAsset() : nullptr;
	const int32 FlipbookIndex = Provider.IsValid()
		? Provider->ResolveAnimationIndex(AnimationIdentity)
		: INDEX_NONE;
	if (!AssetPtr || !AssetPtr->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return;
	}

	// Cheap no-op guard so an unchanged commit does not open an empty transaction or dirty the package.
	const FString Trimmed = NewName.TrimStartAndEnd();
	if (Trimmed.IsEmpty() ||
		Trimmed.Equals(AssetPtr->Flipbooks[FlipbookIndex].Identity.FlipbookName, ESearchCase::CaseSensitive))
	{
		return;
	}

	BeginTransaction(LOCTEXT("RenameFlipbookTransaction", "Rename Flipbook"));
	const bool bChanged = AssetPtr->RenameFlipbookAndPropagate(FlipbookIndex, Trimmed);
	EndTransaction();

	if (bChanged && Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Keyboard
// ─────────────────────────────────────────────────────────────────────────────

FReply SHitboxEditorPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Yield to any focused editable text widget
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return FReply::Unhandled();
	}

	// Modified editor commands (including the default Alt+D direction wheel) bubble to the toolkit.
	if (Paper2DPlusEditor::SlateShortcutUtils::HasEditorCommandModifier(InKeyEvent))
	{
		return FReply::Unhandled();
	}

	const FKey Key = InKeyEvent.GetKey();

	// Up/Down arrow: queue-aware flipbook navigation
	if (Key == EKeys::Up && Model.IsValid())
	{
		if (Model->StepQueue(-1) == INDEX_NONE)
		{
			int32 NewIdx = Model->GetVisualAdjacentFlipbookIndex(-1);
			if (NewIdx != INDEX_NONE) Model->SetSelectedFlipbook(NewIdx);
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Down && Model.IsValid())
	{
		if (Model->StepQueue(1) == INDEX_NONE)
		{
			int32 NewIdx = Model->GetVisualAdjacentFlipbookIndex(1);
			if (NewIdx != INDEX_NONE) Model->SetSelectedFlipbook(NewIdx);
		}
		return FReply::Handled();
	}

	// Left/Right arrow: queue-aware frame navigation
	if (Key == EKeys::Left && Model.IsValid())
	{
		int32 FrameIdx = Model->GetSelectedFrameIndex();
		if (FrameIdx > 0)
		{
			Model->SetSelectedFrame(FrameIdx - 1);
		}
		else if (Model->StepQueue(-1, /*bLandOnLastFrame=*/true) == INDEX_NONE)
		{
			int32 FrameCount = GetCurrentFrameCount();
			if (FrameCount > 0) Model->SetSelectedFrame(FrameCount - 1);
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Right && Model.IsValid())
	{
		int32 FrameIdx = Model->GetSelectedFrameIndex();
		int32 FrameCount = GetCurrentFrameCount();
		if (FrameIdx < FrameCount - 1)
		{
			Model->SetSelectedFrame(FrameIdx + 1);
		}
		else if (Model->StepQueue(1) == INDEX_NONE)
		{
			Model->SetSelectedFrame(0);
		}
		return FReply::Handled();
	}

	// 1/2: switch draw type
	if (Key == EKeys::One)
	{
		ActiveDrawType = EHitboxType::Attack;
		EnumAddFlags(HitboxVisibilityMask, EHitboxVisibility::Attack);
		RefreshHitboxList();
		return FReply::Handled();
	}
	if (Key == EKeys::Two)
	{
		ActiveDrawType = EHitboxType::Hurtbox;
		EnumAddFlags(HitboxVisibilityMask, EHitboxVisibility::Hurtbox);
		RefreshHitboxList();
		return FReply::Handled();
	}

	// E: Edit/Hitbox tool, Q: Socket tool
	if (Key == EKeys::E)
	{
		OnToolSelected(EHitboxEditorTool::Edit);
		return FReply::Handled();
	}
	if (Key == EKeys::Q)
	{
		OnToolSelected(EHitboxEditorTool::Socket);
		return FReply::Handled();
	}

	// Delete: delete selected hitbox/socket
	if (Key == EKeys::Delete)
	{
		if (EditorCanvas.IsValid() && EditorCanvas->GetSelectionType() != EHitboxSelectionType::None)
		{
			DeleteSelected();
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

// ─────────────────────────────────────────────────────────────────────────────
// Section layout (INI persistence — panel-local keys)
// ─────────────────────────────────────────────────────────────────────────────

void SHitboxEditorPanel::InitializeSectionLayout()
{
	const TArray<FName> DefaultHitboxSidebarOrder = { FName(TEXT("Hitboxes")), FName(TEXT("Properties")), FName(TEXT("FrameOps")) };
	LoadSectionOrder(TEXT("CharacterProfileEditor.HitboxPanel.SectionOrder"), DefaultHitboxSidebarOrder, HitboxSidebarSectionOrder);
}

void SHitboxEditorPanel::LoadSectionOrder(const FString& ConfigKey, const TArray<FName>& DefaultOrder, TArray<FName>& InOutOrder) const
{
	InOutOrder = DefaultOrder;
	if (GConfig)
	{
		FString Saved;
		if (GConfig->GetString(TEXT("CharacterProfileEditor"), *ConfigKey, Saved, GEditorPerProjectIni))
		{
			TArray<FString> Parts;
			Saved.ParseIntoArray(Parts, TEXT(","));
			TArray<FName> Loaded;
			for (const FString& Part : Parts)
			{
				FName N(*Part.TrimStartAndEnd());
				if (DefaultOrder.Contains(N))
				{
					Loaded.AddUnique(N);
				}
			}
			// Fill any missing sections
			for (const FName& D : DefaultOrder)
			{
				Loaded.AddUnique(D);
			}
			if (Loaded.Num() == DefaultOrder.Num())
			{
				InOutOrder = Loaded;
			}
		}
	}
}

void SHitboxEditorPanel::SaveSectionOrder(const FString& ConfigKey, const TArray<FName>& Order) const
{
	if (GConfig)
	{
		FString Joined;
		for (int32 i = 0; i < Order.Num(); ++i)
		{
			if (i > 0) Joined += TEXT(",");
			Joined += Order[i].ToString();
		}
		GConfig->SetString(TEXT("CharacterProfileEditor"), *ConfigKey, *Joined, GEditorPerProjectIni);
	}
}

bool SHitboxEditorPanel::CanMoveSection(const TArray<FName>& SectionOrder, FName SectionId, int32 Direction) const
{
	int32 Idx = SectionOrder.IndexOfByKey(SectionId);
	if (Idx == INDEX_NONE) return false;
	int32 Target = Idx + Direction;
	return Target >= 0 && Target < SectionOrder.Num();
}

void SHitboxEditorPanel::MoveSectionInOrder(TArray<FName>& SectionOrder, FName SectionId, int32 Direction, const FString& ConfigKey)
{
	int32 Idx = SectionOrder.IndexOfByKey(SectionId);
	if (Idx == INDEX_NONE) return;
	int32 Target = Idx + Direction;
	if (Target < 0 || Target >= SectionOrder.Num()) return;
	SectionOrder.Swap(Idx, Target);
	SaveSectionOrder(ConfigKey, SectionOrder);
}

void SHitboxEditorPanel::MoveHitboxSidebarSection(FName SectionId, int32 Direction)
{
	MoveSectionInOrder(HitboxSidebarSectionOrder, SectionId, Direction, TEXT("CharacterProfileEditor.HitboxPanel.SectionOrder"));
	RebuildHitboxSidebarSections();
}

TSharedRef<SWidget> SHitboxEditorPanel::BuildReorderableSectionCard(
	FName SectionId,
	const FText& SectionTitle,
	const FText& SectionTooltip,
	TSharedRef<SWidget> ContentWidget,
	bool bStretchContent)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(4, 2, 4, 4))
		[
			SNew(SVerticalBox)

			// Section header with reorder buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 2)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(SectionTitle)
					.ToolTipText(SectionTooltip)
					.Font(FAppStyle::GetFontStyle("BoldFont"))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 0, 0, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(0)
					.IsEnabled_Lambda([this, SectionId]() { return CanMoveSection(HitboxSidebarSectionOrder, SectionId, -1); })
					.OnClicked_Lambda([this, SectionId]() { MoveHitboxSidebarSection(SectionId, -1); return FReply::Handled(); })
					.ToolTipText(LOCTEXT("MoveUp", "Move section up"))
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Icons.ChevronUp"))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(0)
					.IsEnabled_Lambda([this, SectionId]() { return CanMoveSection(HitboxSidebarSectionOrder, SectionId, 1); })
					.OnClicked_Lambda([this, SectionId]() { MoveHitboxSidebarSection(SectionId, 1); return FReply::Handled(); })
					.ToolTipText(LOCTEXT("MoveDown", "Move section down"))
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Icons.ChevronDown"))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				ContentWidget
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Sidebar sections
// ─────────────────────────────────────────────────────────────────────────────

void SHitboxEditorPanel::RebuildHitboxSidebarSections()
{
	if (!HitboxSidebarSectionsBox.IsValid())
	{
		return;
	}

	HitboxSidebarSectionsBox->ClearChildren();

	for (const FName& SectionId : HitboxSidebarSectionOrder)
	{
		TSharedRef<SWidget> SectionContent = SNullWidget::NullWidget;
		FText SectionTitle;

		if (SectionId == FName(TEXT("Hitboxes")))
		{
			SectionTitle = LOCTEXT("HitboxSidebarSectionHitboxes", "Hitboxes");
			SectionContent = BuildHitboxList();
		}
		else if (SectionId == FName(TEXT("Properties")))
		{
			SectionTitle = LOCTEXT("HitboxSidebarSectionProperties", "Properties");
			SectionContent = BuildPropertiesPanel();
		}
		else if (SectionId == FName(TEXT("FrameOps")))
		{
			SectionTitle = LOCTEXT("HitboxSidebarSectionFrameOps", "Frame Operations");
			SectionContent = BuildCopyOperationsPanel();
		}
		else
		{
			continue;
		}

		HitboxSidebarSectionsBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			BuildReorderableSectionCard(
				FName(*FString::Printf(TEXT("HitboxPanel.Sidebar.%s"), *SectionId.ToString())),
				SectionTitle,
				LOCTEXT("HitboxSidebarSectionTooltip", "Hitbox editor section"),
				SectionContent)
		];
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Event handlers
// ─────────────────────────────────────────────────────────────────────────────

void SHitboxEditorPanel::OnFlipbookSelected(int32 Index)
{
	if (Model.IsValid())
	{
		Model->SetSelectedFlipbook(Index);
	}
}

void SHitboxEditorPanel::OnFrameSelected(int32 Index)
{
	if (Model.IsValid())
	{
		Model->SetSelectedFrame(Index);
	}
}

void SHitboxEditorPanel::OnToolSelected(EHitboxEditorTool Tool)
{
	CurrentTool = Tool;
	if (EditorCanvas.IsValid())
	{
		EditorCanvas->ClearSelection();
	}
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

void SHitboxEditorPanel::OnSelectionChanged(EHitboxSelectionType Type, int32 Index)
{
	RefreshHitboxList();
	RefreshPropertiesPanel();

	// Update 3D viewport selection
	if (Viewport3D.IsValid())
	{
		if (Type == EHitboxSelectionType::Hitbox)
		{
			Viewport3D->SetSelectedHitbox(Index);
			Viewport3D->SetSelectedSocket(-1);
		}
		else if (Type == EHitboxSelectionType::Socket)
		{
			Viewport3D->SetSelectedSocket(Index);
			Viewport3D->SetSelectedHitbox(-1);
		}
		else
		{
			Viewport3D->SetSelectedHitbox(-1);
			Viewport3D->SetSelectedSocket(-1);
		}
	}
}

void SHitboxEditorPanel::OnHitboxDataModified()
{
	RefreshHitboxList();
	RefreshPropertiesPanel();

	// Update 3D viewport
	if (Viewport3D.IsValid())
	{
		const FFrameHitboxData* Frame = GetCurrentFrame();
		Viewport3D->SetFrameData(Frame);
	}
}

void SHitboxEditorPanel::OnZoomChanged(float NewZoom)
{
	ZoomLevel = NewZoom;
}

// ─────────────────────────────────────────────────────────────────────────────
// Refresh
// ─────────────────────────────────────────────────────────────────────────────

void SHitboxEditorPanel::RefreshAll()
{
	// Re-resolve the asset from the model — it can change after Construct when the model is re-initialized
	// (e.g. the Character Layer editor's Base Profile picker calls Model->InitializeFromAsset on a new profile).
	if (Model.IsValid()) { Asset = Model->GetAsset(); }
	// The canvas + 3D viewport captured the OLD Asset weak ptr at Construct and resolve SPRITES/zoom from it
	// (frame DATA already routes through the provider live). On a Base Profile swap they must re-point too,
	// or the tab draws boxes against the previous profile's sprites while writes go to the new scope (Codex
	// P2). The provider (layer scope) reads the model live, so it needs no update.
	if (EditorCanvas.IsValid())
	{
		EditorCanvas->SetAsset(Asset);
		EditorCanvas->Invalidate(EInvalidateWidgetReason::Layout);
	}
	if (Viewport3D.IsValid())
	{
		// SetAsset only re-points the weak ptr; the 3D viewport renders a pushed frame-data snapshot + sprite,
		// so re-push them here too (RefreshAll is the base-profile-swap path — the selection-change handlers
		// already push, but a swap fires OnAssetDataChanged -> RefreshAll, not a selection change). Otherwise
		// the 3D view keeps drawing the previous profile's frame/sprite until the next scrub (Codex P2).
		Viewport3D->SetAsset(Asset);
		Viewport3D->SetFrameData(GetCurrentFrame());
		Viewport3D->SetSprite(GetCurrentSprite());
	}
	RefreshFrameList();
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

const FFlipbookProfileEntry* SHitboxEditorPanel::GetCurrentFlipbookData() const
{
	if (!Asset.IsValid() || !Model.IsValid()) return nullptr;
	int32 Idx = Model->GetSelectedFlipbookIndex();
	return Asset->Flipbooks.IsValidIndex(Idx) ? &Asset->Flipbooks[Idx] : nullptr;
}

FFlipbookProfileEntry* SHitboxEditorPanel::GetCurrentFlipbookDataMutable()
{
	if (!Asset.IsValid() || !Model.IsValid()) return nullptr;
	int32 Idx = Model->GetSelectedFlipbookIndex();
	return Asset->Flipbooks.IsValidIndex(Idx) ? &Asset->Flipbooks[Idx] : nullptr;
}

// The four frame resolvers below are thin provider delegations (see HitboxDataProvider.h): the profile
	// provider resolves unmanaged runtime rows or an attached Profile Baseline; the Layer provider adapts
	// the selected real layer's source-local channels. GetCurrentFrameMutable is FIND-ONLY — creation goes
// through Provider->EnsureFrameMutable at the explicit first-write sites.

const FFrameHitboxData* SHitboxEditorPanel::GetCurrentFrame() const
{
	if (!Model.IsValid()) return nullptr;
	return Provider->GetFrame(Model->GetSelectedFlipbookIndex(), Model->GetSelectedFrameIndex());
}

const FFrameHitboxData* SHitboxEditorPanel::GetCurrentFrame(int32 FrameIdx) const
{
	if (!Model.IsValid()) return nullptr;
	return Provider->GetFrame(Model->GetSelectedFlipbookIndex(), FrameIdx);
}

FFrameHitboxData* SHitboxEditorPanel::GetCurrentFrameMutable()
{
	if (!Model.IsValid()) return nullptr;
	return Provider->GetFrameMutable(Model->GetSelectedFlipbookIndex(), Model->GetSelectedFrameIndex());
}

FFrameHitboxData* SHitboxEditorPanel::GetCurrentFrameMutable(int32 FrameIdx)
{
	if (!Model.IsValid()) return nullptr;
	return Provider->GetFrameMutable(Model->GetSelectedFlipbookIndex(), FrameIdx);
}

int32 SHitboxEditorPanel::GetCurrentFrameCount() const
{
	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim) return 0;

	if (!Anim->Identity.Flipbook.IsNull())
	{
		UPaperFlipbook* FB = Anim->Identity.Flipbook.Get();
		if (FB) return FB->GetNumKeyFrames();
	}
	// Frame cardinality is Character Profile truth even while the soft flipbook is unloaded. Layer source
	// rows may be absent before first edit or intentionally retain grow-only history after a later shrink.
	return Anim->CombatData.Frames.Num();
}

UPaperSprite* SHitboxEditorPanel::GetCurrentSprite() const
{
	if (!Model.IsValid()) return nullptr;
	int32 FrameIdx = Model->GetSelectedFrameIndex();
	if (UPaperFlipbook* FB = GetPreviewFlipbook())
	{
		if (FrameIdx >= 0 && FrameIdx < FB->GetNumKeyFrames())
		{
			return FB->GetKeyFrameChecked(FrameIdx).Sprite;
		}
	}
	return nullptr;
}

UPaperFlipbook* SHitboxEditorPanel::GetPreviewFlipbook() const
{
	if (!Model.IsValid()) return nullptr;
	if (Model->IsDirectionalPreviewEnabled())
	{
		return Model->GetDirectionalPreviewFlipbook();
	}
	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	return Anim && !Anim->Identity.Flipbook.IsNull()
		? Anim->Identity.Flipbook.LoadSynchronous()
		: nullptr;
}

bool SHitboxEditorPanel::IsHitboxTypeVisible(EHitboxType Type) const
{
	if (Type == EHitboxType::Attack) return EnumHasAnyFlags(HitboxVisibilityMask, EHitboxVisibility::Attack);
	if (Type == EHitboxType::Hurtbox) return EnumHasAnyFlags(HitboxVisibilityMask, EHitboxVisibility::Hurtbox);
	return true;
}

void SHitboxEditorPanel::ForEachSelectedFrame(TFunctionRef<void(int32)> Op)
{
	if (!Model.IsValid()) return;
	const TSet<int32>& SelectedFrames = Model->GetSelectedFrames();
	TArray<int32> SortedFrames = SelectedFrames.Array();
	SortedFrames.Sort();
	for (int32 Idx : SortedFrames)
	{
		Op(Idx);
	}
}

void SHitboxEditorPanel::TriggerPendingRenameIfNeeded(TMap<int32, TSharedPtr<SInlineEditableTextBlock>>& NameTexts)
{
	if (PendingRenameFlipbookIndex != INDEX_NONE)
	{
		TSharedPtr<SInlineEditableTextBlock>* Found = NameTexts.Find(PendingRenameFlipbookIndex);
		if (Found && Found->IsValid())
		{
			(*Found)->EnterEditingMode();
			PendingRenameFlipbookIndex = INDEX_NONE;
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Central workspace / BuildToolbar
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SHitboxEditorPanel::BuildCentralWorkspace()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildToolbar()
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			BuildCanvasArea()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildFrameList()
		];
}

TSharedRef<SWidget> SHitboxEditorPanel::BuildToolbar()
{
	// Mode-scoped chrome: the hitbox filter/draw controls apply only to the Hitboxes tool and the
	// socket actions only to the Socket tool, so each cluster collapses while its tool is inactive.
	auto HitboxModeVisibility = [this]() { return CurrentTool == EHitboxEditorTool::Edit ? EVisibility::Visible : EVisibility::Collapsed; };
	auto SocketModeVisibility = [this]() { return CurrentTool == EHitboxEditorTool::Socket ? EVisibility::Visible : EVisibility::Collapsed; };

	auto BuildVisibilityCheckbox = [this, HitboxModeVisibility](EHitboxVisibility Mask, EHitboxType Type, const FText& Label, const FLinearColor& Color) -> TSharedRef<SWidget>
	{
		return SNew(SCheckBox)
			.Visibility_Lambda(HitboxModeVisibility)
			.IsChecked_Lambda([this, Mask]() { return EnumHasAnyFlags(HitboxVisibilityMask, Mask) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Mask, Type](ECheckBoxState State) {
				if (State == ECheckBoxState::Checked) { EnumAddFlags(HitboxVisibilityMask, Mask); }
				else                                  { EnumRemoveFlags(HitboxVisibilityMask, Mask); }
				if (State == ECheckBoxState::Unchecked && EditorCanvas.IsValid())
				{
					const FFrameHitboxData* Frame = GetCurrentFrame();
					if (Frame)
					{
						for (int32 Idx : EditorCanvas->GetSelectedIndices())
						{
							if (Frame->Hitboxes.IsValidIndex(Idx) && Frame->Hitboxes[Idx].Type == Type)
								EditorCanvas->RemoveFromSelection(Idx);
						}
					}
				}
				RefreshHitboxList();
				RefreshPropertiesPanel();
			})
			.ToolTipText(FText::Format(LOCTEXT("ShowTypeTooltip", "Show {0} hitboxes"), Label))
			[
				SNew(STextBlock)
				.Text(Label)
				.ColorAndOpacity(FSlateColor(Color))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
			];
	};

	// Side-by-side tool selector (the former left-hand "Tools" column, folded into this bar).
	auto BuildToolButton = [this](EHitboxEditorTool Tool, const FText& Label, const FText& Tooltip, const FName IconName,
		const FLinearColor& ActiveButtonColor, const FLinearColor& ActiveIconColor) -> TSharedRef<SWidget>
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ButtonColorAndOpacity_Lambda([this, Tool, ActiveButtonColor]() -> FLinearColor
			{
				return CurrentTool == Tool ? ActiveButtonColor : FLinearColor(0.12f, 0.12f, 0.12f, 1.0f);
			})
			.ToolTipText(Tooltip)
			.OnClicked_Lambda([this, Tool]() { OnToolSelected(Tool); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush(IconName))
					.ColorAndOpacity_Lambda([this, Tool, ActiveIconColor]() -> FSlateColor
					{
						return CurrentTool == Tool ? FSlateColor(ActiveIconColor) : FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f));
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Label)
					.ColorAndOpacity_Lambda([this, Tool]() -> FSlateColor
					{
						return CurrentTool == Tool ? FSlateColor(FLinearColor::White) : FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f));
					})
				]
			];
	};

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(4)
		[
			SNew(SWrapBox)
			.UseAllottedSize(true)

			// ── Tool selection (always visible) ──
			+ SWrapBox::Slot()
			.Padding(2, 0, 0, 0)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ToolLabel", "Tool:"))
			]

			+ SWrapBox::Slot()
			.Padding(4, 0, 2, 0)
			[
				BuildToolButton(EHitboxEditorTool::Edit,
					LOCTEXT("HitboxToolShort", "Hitboxes"),
					LOCTEXT("HitboxToolTooltip", "Hitbox Tool (E)\nDrag on empty space to draw new hitboxes\nClick to select, WASD to nudge, double-click to edit"),
					FName(TEXT("Icons.Transform")),
					FLinearColor(0.20f, 0.40f, 0.80f, 1.0f),
					FLinearColor(0.55f, 0.75f, 1.0f))
			]

			+ SWrapBox::Slot()
			.Padding(2, 0)
			[
				BuildToolButton(EHitboxEditorTool::Socket,
					LOCTEXT("SocketToolShort", "Socket"),
					LOCTEXT("SocketToolTooltip", "Socket Tool (Q)\nClick the canvas to place attachment points"),
					FName(TEXT("Icons.Plus")),
					FLinearColor(0.65f, 0.48f, 0.12f, 1.0f),
					FLinearColor(1.0f, 0.8f, 0.3f))
			]

			+ SWrapBox::Slot()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
			]

			// ── Hitbox-mode controls ──
			+ SWrapBox::Slot()
			.Padding(2)
			[
				SNew(SCheckBox)
				.Visibility_Lambda(HitboxModeVisibility)
				.ToolTipText(LOCTEXT("ShowHitboxesOnFrameStripTip",
					"Show translucent hitbox silhouettes over the frame strip thumbnails.\n"
					"Useful for seeing which frames have coverage without scrubbing.\n"
					"Respects the Attack/Hurtbox visibility toggles."))
				.IsChecked_Lambda([this]() { return bShowHitboxesOnFrameStrip ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					bShowHitboxesOnFrameStrip = (NewState == ECheckBoxState::Checked);
					if (FrameListBox.IsValid()) FrameListBox->Invalidate(EInvalidateWidgetReason::Paint);
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ShowHitboxesOnFrameStrip", "Hitbox Overlays"))
				]
			]

			+ SWrapBox::Slot()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
				.Visibility_Lambda(HitboxModeVisibility)
			]

			+ SWrapBox::Slot()
			.Padding(2, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ShowLabel", "Show:"))
				.Visibility_Lambda(HitboxModeVisibility)
			]

			+ SWrapBox::Slot()
			.Padding(4, 0, 2, 0)
			[
				BuildVisibilityCheckbox(EHitboxVisibility::Attack, EHitboxType::Attack, LOCTEXT("ATKFilter", "ATK"), FLinearColor::Red)
			]

			+ SWrapBox::Slot()
			.Padding(2, 0)
			[
				BuildVisibilityCheckbox(EHitboxVisibility::Hurtbox, EHitboxType::Hurtbox, LOCTEXT("HRTFilter", "HRT"), FLinearColor::Green)
			]

			+ SWrapBox::Slot()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
				.Visibility_Lambda(HitboxModeVisibility)
			]

			+ SWrapBox::Slot()
			.Padding(2, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DrawLabel", "Draw:"))
				.Visibility_Lambda(HitboxModeVisibility)
			]

			+ SWrapBox::Slot()
			.Padding(4, 0, 2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Visibility_Lambda(HitboxModeVisibility)
				.ButtonColorAndOpacity_Lambda([this]() -> FLinearColor
				{
					return ActiveDrawType == EHitboxType::Attack
						? FLinearColor(0.7f, 0.12f, 0.12f, 1.0f)
						: FLinearColor(0.12f, 0.12f, 0.12f, 1.0f);
				})
				.ToolTipText(LOCTEXT("DrawATKTooltip", "Draw Attack hitboxes (1)"))
				.OnClicked_Lambda([this]()
				{
					ActiveDrawType = EHitboxType::Attack;
					EnumAddFlags(HitboxVisibilityMask, EHitboxVisibility::Attack);
					RefreshHitboxList();
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 4, 0)
					[
						SNew(SBox)
						.WidthOverride(8.0f)
						.HeightOverride(8.0f)
						[
							SNew(SColorBlock).Color(FLinearColor::Red)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DrawATK", "Attack"))
						.ColorAndOpacity_Lambda([this]() -> FSlateColor
						{
							return ActiveDrawType == EHitboxType::Attack
								? FSlateColor(FLinearColor::White)
								: FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f));
						})
					]
				]
			]

			+ SWrapBox::Slot()
			.Padding(2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Visibility_Lambda(HitboxModeVisibility)
				.ButtonColorAndOpacity_Lambda([this]() -> FLinearColor
				{
					return ActiveDrawType == EHitboxType::Hurtbox
						? FLinearColor(0.1f, 0.55f, 0.1f, 1.0f)
						: FLinearColor(0.12f, 0.12f, 0.12f, 1.0f);
				})
				.ToolTipText(LOCTEXT("DrawHRTTooltip", "Draw Hurtbox hitboxes (2)"))
				.OnClicked_Lambda([this]()
				{
					ActiveDrawType = EHitboxType::Hurtbox;
					EnumAddFlags(HitboxVisibilityMask, EHitboxVisibility::Hurtbox);
					RefreshHitboxList();
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 4, 0)
					[
						SNew(SBox)
						.WidthOverride(8.0f)
						.HeightOverride(8.0f)
						[
							SNew(SColorBlock).Color(FLinearColor::Green)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DrawHRT", "Hurtbox"))
						.ColorAndOpacity_Lambda([this]() -> FSlateColor
						{
							return ActiveDrawType == EHitboxType::Hurtbox
								? FSlateColor(FLinearColor::White)
								: FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f));
						})
					]
				]
			]

			// ── Socket-mode controls ──
			+ SWrapBox::Slot()
			.Padding(2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Visibility_Lambda(SocketModeVisibility)
				.ToolTipText(LOCTEXT("AddSocketToolbarTooltip",
					"Add Socket\nCreate a new attachment point at the sprite center.\n"
					"You can also click anywhere on the canvas to place one."))
				.OnClicked_Lambda([this]() { AddNewSocket(); return FReply::Handled(); })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 4, 0)
					[
						SNew(SBox)
						.WidthOverride(8.0f)
						.HeightOverride(8.0f)
						[
							SNew(SColorBlock).Color(FLinearColor(0.95f, 0.85f, 0.30f))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AddSocketToolbar", "Add Socket"))
					]
				]
			]

			+ SWrapBox::Slot()
			.Padding(8, 0)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
				.Visibility_Lambda(SocketModeVisibility)
			]

			+ SWrapBox::Slot()
			.Padding(2)
			[
				SNew(SCheckBox)
				.Visibility_Lambda(SocketModeVisibility)
				.ToolTipText(LOCTEXT("ShowSocketsOnFrameStripTip",
					"Show socket markers over the frame strip thumbnails.\n"
					"Useful for seeing which frames have attachment points without scrubbing."))
				.IsChecked_Lambda([this]() { return bShowSocketsOnFrameStrip ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					bShowSocketsOnFrameStrip = (NewState == ECheckBoxState::Checked);
					if (FrameListBox.IsValid()) FrameListBox->Invalidate(EInvalidateWidgetReason::Paint);
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ShowSocketsOnFrameStrip", "Socket Overlays"))
				]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildFlipbookList (uses FFlipbookListBuilder)
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SHitboxEditorPanel::BuildFlipbookList()
{
	SAssignNew(FlipbookListBox, SVerticalBox);
	RefreshFlipbookList();

	FString SearchText;
	if (Model.IsValid())
	{
		SearchText = Model->GetFlipbookGroupSearchText();
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FlipbooksHeader", "Flipbooks"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 0, 4, 4)
		[
			SNew(SSearchBox)
			.HintText(LOCTEXT("HitboxSearchFlipbooks", "Search..."))
			.InitialText(FText::FromString(SearchText))
			.OnTextChanged_Lambda([this](const FText& NewText)
			{
				if (Model.IsValid())
				{
					Model->SetFlipbookGroupSearchText(NewText.ToString());
				}
			})
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox) + SScrollBox::Slot()[FlipbookListBox.ToSharedRef()]
		];
}

void SHitboxEditorPanel::RefreshFlipbookList()
{
	if (!FlipbookListBox.IsValid() || !Model.IsValid()) return;

	FlipbookListBox->ClearChildren();
	SidebarFlipbookNameTexts.Empty();

	if (!Asset.IsValid()) return;

	int32 SelectedFlipbookIndex = Model->GetSelectedFlipbookIndex();
	FString SearchText = Model->GetFlipbookGroupSearchText();

	TFunction<bool(int32)> SearchFilter = nullptr;
	if (!SearchText.IsEmpty())
	{
		SearchFilter = [this, SearchText](int32 Idx) -> bool
		{
			return Asset->Flipbooks[Idx].Identity.FlipbookName.Contains(SearchText, ESearchCase::IgnoreCase);
		};
	}

	FFlipbookListBuilder::Build(FlipbookListBox, Model,
		[this, SelectedFlipbookIndex](int32 i) -> TSharedRef<SWidget>
		{
			const FFlipbookProfileEntry& Anim = Asset->Flipbooks[i];
			const FProfileAnimationIdentity AnimationIdentity = Provider->GetAnimationIdentity(i);
			const bool bIsSelected = (i == SelectedFlipbookIndex);
			UPaperFlipbook* LoadedFlipbook = Anim.Identity.Flipbook.Get();
			const bool bHasAssignedFlipbook = !Anim.Identity.Flipbook.IsNull();
			const bool bHasLoadedFlipbook = LoadedFlipbook != nullptr;
			const int32 FrameCount = bHasLoadedFlipbook ? LoadedFlipbook->GetNumKeyFrames() : Anim.CombatData.Frames.Num();
			const FText SourceNameText = FText::FromString(
				bHasAssignedFlipbook ? Anim.Identity.Flipbook.GetAssetName() : TEXT("No Flipbook Assigned"));

			// Count total hitboxes by type across all frames
			int32 TotalAttackCount = 0, TotalHurtCount = 0, TotalSocketCount = 0;
			for (int32 FrameIndex = 0; FrameIndex < Provider->GetAuthoredFrameCount(i); ++FrameIndex)
			{
				const FFrameHitboxData* Frame = Provider->GetFrame(i, FrameIndex);
				if (!Frame) continue;
				for (const FHitboxData& HB : Frame->Hitboxes)
				{
					if (HB.Type == EHitboxType::Attack) TotalAttackCount++;
					else if (HB.Type == EHitboxType::Hurtbox) TotalHurtCount++;
				}
				TotalSocketCount += Frame->Sockets.Num();
			}

			TSharedPtr<SInlineEditableTextBlock> NameText;

			TSharedRef<SWidget> Item = SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
				.Padding(2)
				.OnMouseButtonDown_Lambda([this, AnimationIdentity](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
				{
					if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
					{
						const int32 LiveIndex = Provider.IsValid()
							? Provider->ResolveAnimationIndex(AnimationIdentity)
							: INDEX_NONE;
						if (LiveIndex != INDEX_NONE)
						{
							OnShowFlipbookContextMenu.ExecuteIfBound(LiveIndex);
						}
						return FReply::Handled();
					}
					return FReply::Unhandled();
				})
				.ToolTip(
					SNew(SToolTip)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 6)
						[
							SNew(STextBlock)
							.Text(FText::FromString(Anim.Identity.FlipbookName))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(128)
							.HeightOverride(128)
							[
								bHasLoadedFlipbook
									? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook))
									: StaticCastSharedRef<SWidget>(
										SNew(SBorder)
										.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
										.HAlign(HAlign_Center)
										.VAlign(VAlign_Center)
										[
											SNew(STextBlock)
											.Text(LOCTEXT("NoHitboxFlipbookTooltipPreview", "No FB"))
											.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
											.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
										])
							]
						]
					])
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.OnClicked_Lambda([this, AnimationIdentity]()
					{
						const int32 LiveIndex = Provider.IsValid()
							? Provider->ResolveAnimationIndex(AnimationIdentity)
							: INDEX_NONE;
						if (LiveIndex != INDEX_NONE)
						{
							OnFlipbookSelected(LiveIndex);
						}
						return FReply::Handled();
					})
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
						.BorderBackgroundColor(bIsSelected
							? FLinearColor(0.15f, 0.35f, 0.55f, 1.0f)
							: FLinearColor(0.03f, 0.03f, 0.03f, 1.0f))
						.Padding(FMargin(8, 6))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0, 0, 8, 0)
							[
								SNew(SBox)
								.WidthOverride(44)
								.HeightOverride(44)
								[
									bHasLoadedFlipbook
										? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook))
										: StaticCastSharedRef<SWidget>(
											SNew(SBorder)
											.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
											.HAlign(HAlign_Center)
											.VAlign(VAlign_Center)
											[
												SNew(STextBlock)
												.Text(LOCTEXT("NoHitboxFlipbookListPreview", "No FB"))
												.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
												.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
											])
								]
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.FillWidth(1.0f)
									.VAlign(VAlign_Center)
									[
										SAssignNew(NameText, SInlineEditableTextBlock)
										.Text(FText::FromString(Anim.Identity.FlipbookName))
										.OnTextCommitted_Lambda([this, AnimationIdentity](const FText& NewText, ETextCommit::Type CommitType)
										{
											if (CommitType != ETextCommit::OnCleared)
											{
												CommitFlipbookRename(AnimationIdentity, NewText.ToString());
											}
										})
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(6, 0, 0, 0)
									[
										SNew(STextBlock)
										.Text(FText::Format(LOCTEXT("HitboxFrameCountLabel", "{0} frames"), FText::AsNumber(FrameCount)))
										.Font(FAppStyle::GetFontStyle("SmallFont"))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
									]
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0, 2, 0, 0)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.FillWidth(1.0f)
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(SourceNameText)
										.Font(FAppStyle::GetFontStyle("SmallFont"))
										.ColorAndOpacity(bHasAssignedFlipbook ? FLinearColor(0.4f, 0.8f, 0.4f) : FLinearColor(0.6f, 0.4f, 0.4f))
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(6, 0, 0, 0)
									[
										(TotalAttackCount > 0 || TotalHurtCount > 0 || TotalSocketCount > 0)
										? StaticCastSharedRef<SWidget>(
											SNew(SHorizontalBox)
											+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
											[
												TotalAttackCount > 0
												? StaticCastSharedRef<SWidget>(SNew(SHorizontalBox)
													+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
													[
														SNew(SBox).WidthOverride(6).HeightOverride(6)
														[ SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.FilledCircle")).ColorAndOpacity(FLinearColor(0.95f, 0.30f, 0.30f)) ]
													]
													+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1, 0, 0, 0)
													[
														SNew(STextBlock).Text(FText::AsNumber(TotalAttackCount)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 7)).ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.9f)))
													])
												: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget)
											]
											+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
											[
												TotalHurtCount > 0
												? StaticCastSharedRef<SWidget>(SNew(SHorizontalBox)
													+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
													[
														SNew(SBox).WidthOverride(6).HeightOverride(6)
														[ SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.FilledCircle")).ColorAndOpacity(FLinearColor(0.30f, 0.90f, 0.30f)) ]
													]
													+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1, 0, 0, 0)
													[
														SNew(STextBlock).Text(FText::AsNumber(TotalHurtCount)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 7)).ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.9f)))
													])
												: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget)
											]
											+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
											[
												TotalSocketCount > 0
												? StaticCastSharedRef<SWidget>(SNew(SHorizontalBox)
													+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
													[
														SNew(SBox).WidthOverride(6).HeightOverride(6)
														[ SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.FilledCircle")).ColorAndOpacity(FLinearColor(0.95f, 0.85f, 0.30f)) ]
													]
													+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1, 0, 0, 0)
													[
														SNew(STextBlock).Text(FText::AsNumber(TotalSocketCount)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 7)).ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.9f)))
													])
												: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget)
											]
										)
										: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget)
									]
								]
							]
						]
					]
				];

			SidebarFlipbookNameTexts.Add(i, NameText);
			return Item;
		},
		[this]() { RefreshFlipbookList(); },
		SearchFilter);

	// Trigger pending rename
	TriggerPendingRenameIfNeeded(SidebarFlipbookNameTexts);

	LastKnownFlipbookCount = Asset->Flipbooks.Num();
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildFrameList
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SHitboxEditorPanel::BuildFrameList()
{
	SAssignNew(FrameListBox, SHorizontalBox);

	RefreshFrameList();

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.MaxHeight(80.0f)
		[
			SNew(SScrollBox)
			.Orientation(Orient_Horizontal)
			+ SScrollBox::Slot()
			[
				FrameListBox.ToSharedRef()
			]
		];
}

void SHitboxEditorPanel::RefreshFrameList()
{
	if (!FrameListBox.IsValid() || !Model.IsValid()) return;

	FrameListBox->ClearChildren();

	const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
	if (!Anim) return;

	int32 SelectedFlipbookIndex = Model->GetSelectedFlipbookIndex();
	int32 SelectedFrameIndex = Model->GetSelectedFrameIndex();
	const TSet<int32>& SelectedFrames = Model->GetSelectedFrames();

	UPaperFlipbook* Flipbook = GetPreviewFlipbook();

	// Iterate the flipbook's key frames, not the authored data rows — the strip must show every frame even
	// before the per-frame hitbox arrays are sized (or, in layer scope, before an override entry exists).
	static const TArray<FHitboxData> EmptyHitboxes;
	static const TArray<FSocketData> EmptySockets;
	const int32 FrameCount = GetCurrentFrameCount();
	for (int32 i = 0; i < FrameCount; i++)
	{
		const FFrameHitboxData* FramePtr = GetCurrentFrame(i);

		int32 AttackCount = 0, HurtCount = 0;
		if (FramePtr)
		{
			for (const FHitboxData& HB : FramePtr->Hitboxes)
			{
				if (HB.Type == EHitboxType::Attack) AttackCount++;
				else if (HB.Type == EHitboxType::Hurtbox) HurtCount++;
			}
		}
		const int32 SocketCount = FramePtr ? FramePtr->Sockets.Num() : 0;

		UPaperSprite* FrameSprite = nullptr;
		if (Flipbook && i < Flipbook->GetNumKeyFrames())
		{
			FrameSprite = Flipbook->GetKeyFrameChecked(i).Sprite;
		}

		// Build hitbox count badges
		TSharedRef<SHorizontalBox> BadgeRow = SNew(SHorizontalBox);
		auto AddBadge = [&](int32 Count, FLinearColor DotColor)
		{
			if (Count <= 0) return;
			BadgeRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 3, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(7).HeightOverride(7)
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Icons.FilledCircle"))
						.ColorAndOpacity(DotColor)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1, 0, 0, 0)
				[
					SNew(STextBlock)
					.Text(FText::AsNumber(Count))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.95f, 0.95f)))
				]
			];
		};
		AddBadge(AttackCount, FLinearColor(0.95f, 0.30f, 0.30f));
		AddBadge(HurtCount, FLinearColor(0.30f, 0.90f, 0.30f));
		AddBadge(SocketCount, FLinearColor(0.95f, 0.85f, 0.30f));

		// Hitbox silhouette / socket marker overlay — each channel follows its own toolbar toggle.
		TSharedRef<SFrameStripHitboxOverlay> HitboxPainter = SNew(SFrameStripHitboxOverlay)
			.Sprite(FrameSprite)
			.VisibilityMask_Lambda([this]() { return HitboxVisibilityMask; })
			.ShowHitboxes_Lambda([this]() { return bShowHitboxesOnFrameStrip; })
			.ShowSockets_Lambda([this]() { return bShowSocketsOnFrameStrip; });
		HitboxPainter->SetHitboxes(FramePtr ? FramePtr->Hitboxes : EmptyHitboxes);
		HitboxPainter->SetSockets(FramePtr ? FramePtr->Sockets : EmptySockets);

		TSharedRef<SWidget> HitboxSpriteOverlay = SNew(SBox)
			.Visibility_Lambda([this]()
			{
				return (bShowHitboxesOnFrameStrip || bShowSocketsOnFrameStrip)
					? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			[
				HitboxPainter
			];

		// I-frame toggle button
		TSharedRef<TWeakPtr<SWidget>> IFrameHoverRef = MakeShared<TWeakPtr<SWidget>>();
		TSharedRef<TWeakPtr<SWidget>> CellHoverRef = MakeShared<TWeakPtr<SWidget>>();

		TSharedPtr<SBox> IFrameBox;
		TSharedRef<SWidget> InvulOverlay = SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
				.Padding(0)
				.BorderBackgroundColor(FLinearColor(0.15f, 0.35f, 0.85f, 0.25f))
				.Visibility_Lambda([this, i]() -> EVisibility
				{
					const FFrameHitboxData* F = GetCurrentFrame(i);
					return (F && F->bInvulnerable) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
				})
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Top)
			.Padding(FMargin(0, 2, 2, 0))
			[
				SAssignNew(IFrameBox, SBox)
				.Visibility_Lambda([this, i, CellHoverRef, SelectedFrameIndex]() -> EVisibility
				{
					const FFrameHitboxData* F = GetCurrentFrame(i);
					const bool bInvuln = F && F->bInvulnerable;
					const bool bSel = (i == SelectedFrameIndex);
					TSharedPtr<SWidget> PinnedCell = CellHoverRef->Pin();
					const bool bCellHovered = PinnedCell.IsValid() && PinnedCell->IsHovered();
					return (bInvuln || bSel || bCellHovered) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ContentPadding(FMargin(0))
					.ToolTipText(LOCTEXT("ToggleIFrameTooltip", "Toggle invulnerable frame (i-frame)"))
					.OnClicked_Lambda([this, i]() -> FReply
					{
						if (FFrameHitboxData* F = GetCurrentFrameMutable(i))
						{
							BeginTransaction(LOCTEXT("ToggleInvulnerable", "Toggle Invulnerable Frame"));
							F->bInvulnerable = !F->bInvulnerable;
							EndTransaction();
							if (FrameListBox.IsValid()) FrameListBox->Invalidate(EInvalidateWidgetReason::Paint);
						}
						return FReply::Handled();
					})
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
						.BorderBackgroundColor_Lambda([IFrameHoverRef, this, i]() -> FSlateColor
						{
							const FFrameHitboxData* F = GetCurrentFrame(i);
							const bool bInvuln = F && F->bInvulnerable;
							TSharedPtr<SWidget> PinnedBtn = IFrameHoverRef->Pin();
							const bool bBtnHovered = PinnedBtn.IsValid() && PinnedBtn->IsHovered();
							if (bInvuln)
								return bBtnHovered ? FLinearColor(0.3f, 0.5f, 0.9f, 1.0f) : FLinearColor(0.2f, 0.4f, 0.8f, 1.0f);
							else
								return bBtnHovered ? FLinearColor(0.4f, 0.4f, 0.5f, 1.0f) : FLinearColor(0.25f, 0.25f, 0.3f, 1.0f);
						})
						.Padding(FMargin(1))
						[
							SNew(SBox)
							.WidthOverride(12.0f)
							.HeightOverride(12.0f)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("IFrameGlyph", "I"))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
								.ColorAndOpacity(FSlateColor(FLinearColor::White))
								.Justification(ETextJustify::Center)
							]
						]
					]
				]
			];
		*IFrameHoverRef = IFrameBox;

		FFrameStripCellArgs CellArgs;
		CellArgs.Sprite = FrameSprite;
		// In the Character Layer editor, render the layered composite as the cell background (the hitbox
		// silhouette overlay still draws on top). The base sprite stays as the fallback / silhouette anchor.
		// NOTE on cost: this strip is fully rebuilt (ClearChildren) on every frame-selection change — a
		// pre-existing pattern (the per-cell badges/silhouette are built from each frame's fixed data, and
		// OnHitboxDataModified doesn't rebuild the strip, so the rebuild-on-select is what keeps badges fresh).
		// Constructing a composite thumbnail per cell is cheap: SLayerCompositeThumbnail::Construct only stores
		// 3 members; the sprite loads happen in OnPaint and would occur whether or not the widget was rebuilt.
		// (The Overview strip has no per-frame badges to keep fresh, so it stays invalidate-only — see
		// SLayerOverviewPanel::RefreshFrameStrip.)
		if (LayerAsset.IsValid())
		{
			CellArgs.SpriteContentOverride = SNew(SLayerCompositeThumbnail)
				.LayerAsset(LayerAsset)
				.Model(Model)
				.FrameIndex(i);
		}
		CellArgs.FrameIndex = i;
		CellArgs.IsSelected = [SelectedFrameIndex, i]() { return i == SelectedFrameIndex; };
		CellArgs.IsMultiSelected = [&SelectedFrames, i]() { return SelectedFrames.Contains(i); };
		CellArgs.OnMouseButtonDown = [this, i, FrameSprite, SelectedFrameIndex](const FPointerEvent& MouseEvent) -> FReply
		{
			if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
			{
				OnShowSpriteContextMenu.ExecuteIfBound(FrameSprite, MouseEvent.GetScreenSpacePosition());
				return FReply::Handled();
			}
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				if (Model.IsValid())
				{
					int32 TotalFrames = GetCurrentFrameCount();
					Model->HandleFrameClick(i, MouseEvent.IsControlDown(), MouseEvent.IsShiftDown(), TotalFrames);
					OnFrameSelected(i);
				}
				return FReply::Handled();
			}
			return FReply::Unhandled();
		};
		CellArgs.BelowLabelContent = BadgeRow;
		CellArgs.SpriteOverlay = HitboxSpriteOverlay;
		// Frame-level fields (bInvulnerable) are base-profile-only by contract — no I-frame toggle on
		// the layer surface.
		if (Provider->SupportsFrameLevelFields())
		{
			CellArgs.Overlay = InvulOverlay;
		}

		TSharedRef<SWidget> CellWidget = FFrameStripCellUtils::Build(CellArgs);
		*CellHoverRef = CellWidget;

		FrameListBox->AddSlot()
		.AutoWidth()
		.Padding(0, 0, 4, 0)
		[
			CellWidget
		];
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildCanvasArea
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SHitboxEditorPanel::BuildCanvasArea()
{
	TSharedRef<SVerticalBox> CanvasArea = SNew(SVerticalBox)
		// Layer scope banner + merge dropdowns (collapsed entirely for the profile provider)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildLayerScopeHeader()
		]

		// Current flipbook header
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 2, 4, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() {
					const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData();
					if (!Anim || !Model.IsValid()) return FText::FromString(TEXT("No Flipbook"));
					// Same directional-empty explanation the other tools' titles carry: the boxes
					// keep drawing, but the missing sprite art must say why it is missing.
					if (!GetPreviewFlipbook() && Model->IsDirectionalPreviewEnabled())
					{
						return FText::FromString(Model->GetDirectionalPreview().Reason);
					}
					int32 FrameCount = GetCurrentFrameCount();
					int32 FrameIdx = Model->GetSelectedFrameIndex();
					return FText::Format(LOCTEXT("FlipbookTitleFmt", "{0}  Frame {1}/{2}"),
						FText::FromString(Anim->Identity.FlipbookName),
						FText::AsNumber(FrameIdx + 1),
						FText::AsNumber(FrameCount));
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]

			// Spacer
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			// 2D/3D View Toggle
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8, 0, 0, 0)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([]() { return GetDefault<UPaper2DPlusSettings>()->bEnable3DDepth ? EVisibility::Visible : EVisibility::Collapsed; })
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
					.IsChecked_Lambda([this]() { return bShow3DView ? ECheckBoxState::Unchecked : ECheckBoxState::Checked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						bShow3DView = false;
						if (CanvasViewSwitcher.IsValid())
						{
							CanvasViewSwitcher->SetActiveWidgetIndex(0);
						}
					})
					.ToolTipText(LOCTEXT("View2DTooltip", "2D View\nStandard top-down view for editing hitboxes"))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("View2D", "2D"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0, 0, 0)
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
					.IsChecked_Lambda([this]() { return bShow3DView ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						bShow3DView = true;
						if (CanvasViewSwitcher.IsValid())
						{
							CanvasViewSwitcher->SetActiveWidgetIndex(1);
						}
					})
					.ToolTipText(LOCTEXT("View3DTooltip", "3D View\nPerspective view to visualize hitbox depth (Z and Depth values)\nDrag to rotate, scroll to zoom"))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("View3D", "3D"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]
				]
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4)
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			[
				SAssignNew(CanvasViewSwitcher, SWidgetSwitcher)
				.WidgetIndex_Lambda([this]() { return (bShow3DView && GetDefault<UPaper2DPlusSettings>()->bEnable3DDepth) ? 1 : 0; })

				// Slot 0: 2D Canvas
				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(EditorCanvas, SCharacterProfileEditorCanvas)
					.Asset(Asset)
					.LayerAsset(LayerAsset)
					.Model(Model)
					.FrameDataProvider(Provider)
					.PreviewFlipbook_Lambda([this]() { return GetPreviewFlipbook(); })
					.SelectedFlipbookIndex_Lambda([this]() { return Model.IsValid() ? Model->GetSelectedFlipbookIndex() : 0; })
					.SelectedFrameIndex_Lambda([this]() { return Model.IsValid() ? Model->GetSelectedFrameIndex() : 0; })
					.CurrentTool_Lambda([this]() { return CurrentTool; })
					.Zoom_Lambda([this]() { return ZoomLevel; })
					.VisibilityMask_Lambda([this]() { return HitboxVisibilityMask; })
					.ActiveDrawType_Lambda([this]() { return ActiveDrawType; })
				]

				// Slot 1: 3D Viewport
				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(Viewport3D, SHitbox3DViewport)
					.Asset(Asset)
				]
			]

			// Layer-scope empty state: no real layer selected (or its id vanished). Never shows for Profile.
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.85f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Visibility_Lambda([this]() { return Provider->HasResolvedScope() ? EVisibility::Collapsed : EVisibility::Visible; })
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NoPartSelectedTitle", "No layer selected"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NoPartSelectedHint", "Choose a layer in Character Structure or click visible art\nto author its local hitboxes and sockets."))
						.Justification(ETextJustify::Center)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
					]
				]
			]
		];

	// Update 3D viewport with initial frame data
	if (Viewport3D.IsValid())
	{
		const FFrameHitboxData* Frame = GetCurrentFrame();
		Viewport3D->SetFrameData(Frame);
		Viewport3D->SetSprite(GetCurrentSprite());
	}

	if (EditorCanvas.IsValid())
	{
		EditorCanvas->OnSelectionChanged.BindLambda([this](EHitboxSelectionType Type, int32 Index)
		{
			OnSelectionChanged(Type, Index);
		});

		EditorCanvas->OnHitboxDataModified.BindLambda([this]()
		{
			OnHitboxDataModified();
		});

		EditorCanvas->OnRequestUndo.BindLambda([this]()
		{
			BeginTransaction(LOCTEXT("ModifyHitbox", "Modify Hitbox"));
		});

		EditorCanvas->OnEndTransaction.BindLambda([this]()
		{
			EndTransaction();
		});

		EditorCanvas->OnZoomChanged.BindLambda([this](float NewZoom)
		{
			OnZoomChanged(NewZoom);
		});

		EditorCanvas->OnToolChangeRequested.BindLambda([this](EHitboxEditorTool Tool)
		{
			OnToolSelected(Tool);
		});
	}

	return CanvasArea;
}

TSharedRef<SWidget> SHitboxEditorPanel::BuildLayerScopeHeader()
{
	if (!Provider->IsLayerScoped())
	{
		return SNullWidget::NullWidget;
	}

	if (MergePolicyOptions.Num() == 0)
	{
		MergePolicyOptions.Add(MakeShared<FString>(TEXT("Replace Lower")));
		MergePolicyOptions.Add(MakeShared<FString>(TEXT("Add")));
	}

	// One combo per box type. Find-only contract: a dropdown never creates an animation source row.
	auto MakeMergeCombo = [this](bool bAttack) -> TSharedRef<SWidget>
	{
		return SNew(SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&MergePolicyOptions)
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
			{
				return SNew(STextBlock).Text(FText::FromString(*Item)).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
			})
			.OnSelectionChanged_Lambda([this, bAttack](TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
			{
				if (!NewSelection.IsValid() || SelectInfo == ESelectInfo::Direct || !Model.IsValid()) return;
				const EPaper2DPlusLayerSourceMerge NewPolicy = NewSelection->Equals(TEXT("Add"))
					? EPaper2DPlusLayerSourceMerge::Add : EPaper2DPlusLayerSourceMerge::ReplaceLower;
				const int32 FlipbookIdx = Model->GetSelectedFlipbookIndex();
				EPaper2DPlusLayerSourceMerge Current;
				if (!Provider->TryGetMergePolicy(FlipbookIdx, bAttack, Current) || Current == NewPolicy) return;
				BeginTransaction(bAttack
					? LOCTEXT("SetAttackMerge", "Set Attack Box Merge Policy")
					: LOCTEXT("SetHurtMerge", "Set Hurt Box Merge Policy"));
				Provider->SetMergePolicy(FlipbookIdx, bAttack, NewPolicy);
				EndTransaction();
			})
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.Text_Lambda([this, bAttack]()
				{
					EPaper2DPlusLayerSourceMerge Policy;
					if (Model.IsValid() && Provider->TryGetMergePolicy(Model->GetSelectedFlipbookIndex(), bAttack, Policy))
					{
					return Policy == EPaper2DPlusLayerSourceMerge::Add
							? LOCTEXT("MergeAppend", "Add") : LOCTEXT("MergeReplace", "Replace Lower");
					}
					return LOCTEXT("MergeNone", "—");
				})
			];
	};

	const FText MergeTooltip = LOCTEXT("MergeComboTooltip",
		"How this layer's channel composes with lower included layers:\n"
		"Replace Lower — clears the lower channel only when this layer authors at least one value anywhere in the animation.\n"
		"Add — preserves the lower channel and adds this layer's values.\n"
		"The compiled final projection remains read-only.");

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(6, 3))
		.Visibility_Lambda([this]() { return Provider->HasResolvedScope() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					return FText::Format(LOCTEXT("LayerScopeBannerFmt", "Editing layer source: {0}"), Provider->GetScopeDisplayText());
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.75f, 0.45f)))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8, 0, 4, 0)
			[
				SNew(SHorizontalBox)
				.ToolTipText(MergeTooltip)
				.Visibility_Lambda([this]()
				{
					EPaper2DPlusLayerSourceMerge Dummy;
					return (Model.IsValid() && Provider->TryGetMergePolicy(Model->GetSelectedFlipbookIndex(), /*bAttack*/ true, Dummy))
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("AttackMergeLabel", "Attack:")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
				[
					MakeMergeCombo(/*bAttack*/ true)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("HurtMergeLabel", "Hurt:")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MakeMergeCombo(/*bAttack*/ false)
				]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildHitboxList
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SHitboxEditorPanel::BuildHitboxList()
{
	SAssignNew(HitboxListBox, SVerticalBox);

	HitboxListBox->AddSlot()
	.AutoHeight()
	.Padding(4)
	[
		SNew(SVerticalBox)

		// No inner title: the hosting section card/category already names this section.
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("AddHitboxTooltip", "Add Hitbox\nCreate a new hitbox on this frame"))
				.OnClicked_Lambda([this]() { AddNewHitbox(); return FReply::Handled(); })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.PlusCircle")).ColorAndOpacity(FLinearColor(0.3f, 0.8f, 0.3f))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("HitboxLabel", "Hitbox")).Font(FAppStyle::GetFontStyle("SmallFont"))
					]
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("AddSocketTooltip", "Add Socket\nCreate a new attachment point on this frame"))
				.OnClicked_Lambda([this]() { AddNewSocket(); return FReply::Handled(); })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.Plus")).ColorAndOpacity(FLinearColor(0.8f, 0.6f, 0.2f))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("SocketLabel", "Socket")).Font(FAppStyle::GetFontStyle("SmallFont"))
					]
				]
			]

			+ SHorizontalBox::Slot().FillWidth(1.0f)[ SNullWidget::NullWidget ]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("DeleteSelectedTooltip", "Delete Selected\nRemove the selected hitbox or socket"))
				.OnClicked_Lambda([this]() { DeleteSelected(); return FReply::Handled(); })
				[
					SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.Delete")).ColorAndOpacity(FLinearColor(0.8f, 0.3f, 0.3f))
				]
			]
		]
	];

	RefreshHitboxList();

	return HitboxListBox.ToSharedRef();
}

void SHitboxEditorPanel::RefreshHitboxList()
{
	if (!HitboxListBox.IsValid()) return;

	while (HitboxListBox->NumSlots() > 1)
	{
		HitboxListBox->RemoveSlot(HitboxListBox->GetSlot(1).GetWidget());
	}

	const FFrameHitboxData* Frame = GetCurrentFrame();
	if (!Frame) return;

	for (int32 i = 0; i < Frame->Hitboxes.Num(); i++)
	{
		const FHitboxData& HB = Frame->Hitboxes[i];
		if (!IsHitboxTypeVisible(HB.Type)) continue;

		bool bIsSelected = EditorCanvas.IsValid() &&
			EditorCanvas->GetSelectionType() == EHitboxSelectionType::Hitbox &&
			EditorCanvas->IsSelected(i);

		FString TypeStr = HB.Type == EHitboxType::Attack ? TEXT("ATK") : TEXT("HRT");
		FLinearColor TypeColor = HB.Type == EHitboxType::Attack ? FLinearColor::Red : FLinearColor::Green;

		HitboxListBox->AddSlot()
		.AutoHeight()
		.Padding(1)
		[
			SNew(SButton)
			.ButtonColorAndOpacity(bIsSelected ? TypeColor * 0.5f : FLinearColor(0.1f, 0.1f, 0.1f))
			.OnClicked_Lambda([this, i]()
			{
				if (EditorCanvas.IsValid())
				{
					if (FSlateApplication::Get().GetModifierKeys().IsShiftDown())
					{
						EditorCanvas->ToggleSelection(i);
					}
					else
					{
						EditorCanvas->SetSelection(EHitboxSelectionType::Hitbox, i);
					}
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("HitboxListItem", "[{0}] {1} ({2},{3}) {4}x{5}"),
					FText::AsNumber(i),
					FText::FromString(TypeStr),
					FText::AsNumber(HB.X),
					FText::AsNumber(HB.Y),
					FText::AsNumber(HB.Width),
					FText::AsNumber(HB.Height)))
				.ColorAndOpacity(FSlateColor(TypeColor))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
			]
		];
	}

	for (int32 i = 0; i < Frame->Sockets.Num(); i++)
	{
		const FSocketData& Sock = Frame->Sockets[i];
		bool bIsSelected = EditorCanvas.IsValid() &&
			EditorCanvas->GetSelectionType() == EHitboxSelectionType::Socket &&
			EditorCanvas->IsSelected(i);

		HitboxListBox->AddSlot()
		.AutoHeight()
		.Padding(1)
		[
			SNew(SButton)
			.ButtonColorAndOpacity(bIsSelected ? FLinearColor(0.4f, 0.4f, 0.0f) : FLinearColor(0.1f, 0.1f, 0.1f))
			.OnClicked_Lambda([this, i]()
			{
				if (EditorCanvas.IsValid())
				{
					EditorCanvas->SetSelection(EHitboxSelectionType::Socket, i);
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("SocketListItem", "[S] {0} ({1},{2})"),
					FText::FromString(Sock.Name),
					FText::AsNumber(Sock.X),
					FText::AsNumber(Sock.Y)))
				.ColorAndOpacity(FSlateColor(FLinearColor::Yellow))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
			]
		];
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildPropertiesPanel
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SHitboxEditorPanel::BuildPropertiesPanel()
{
	// No inner title: the hosting section card/category already names this section.
	// Every slot is dynamic, rebuilt per selection by RefreshPropertiesPanel.
	SAssignNew(PropertiesBox, SVerticalBox);

	RefreshPropertiesPanel();

	return PropertiesBox.ToSharedRef();
}

void SHitboxEditorPanel::RefreshPropertiesPanel()
{
	if (!PropertiesBox.IsValid()) return;
	LastPropertiesSelectionType = EHitboxSelectionType::None;
	LastPropertiesSelectionIndex = INDEX_NONE;

	while (PropertiesBox->NumSlots() > 0)
	{
		PropertiesBox->RemoveSlot(PropertiesBox->GetSlot(0).GetWidget());
	}

	if (!EditorCanvas.IsValid()) return;

	EHitboxSelectionType SelType = EditorCanvas->GetSelectionType();
	TArray<int32> SelIndices = EditorCanvas->GetSelectedIndices();
	int32 SelIndex = EditorCanvas->GetPrimarySelectedIndex();
	LastPropertiesSelectionType = SelType;
	LastPropertiesSelectionIndex = SelIndex;

	FFrameHitboxData* Frame = GetCurrentFrameMutable();
	if (!Frame) return;

	// TASK-77 U3: frame-level DEFENSE class picker (Armor/Parry/Invincible) — a property of the whole frame
	// (like bInvulnerable), so shown whenever a frame is active, independent of hitbox selection. When set,
	// an overlapping attack whose ClashCategory loses to this defense (per the project clash graph) is
	// suppressed. SGameplayTagCombo is UE 5.3+. (Intended for the defense categories; the picker spans the
	// whole Clash.Category root so the defense tags match the graph's edges.)
	// Character-wide-only by contract: real layers carry Attack/Hurt/socket channels only; frame flags,
	// Collision compatibility, and defense stay on the Profile Baseline.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	if (Provider->SupportsFrameLevelFields())
	{
		const FGameplayTag CurrentDefense = Frame->DefenseClass;
		PropertiesBox->AddSlot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("DefenseClassLabel", "Defense"),
				SNew(SGameplayTagCombo)
				.Filter(TEXT("Paper2DPlus.Clash.Category"))
				.Tag(CurrentDefense)
				.OnTagChanged_Lambda([this](const FGameplayTag NewTag)
				{
					FFrameHitboxData* F = GetCurrentFrameMutable();
					if (!F || F->DefenseClass == NewTag) return;
					BeginTransaction(LOCTEXT("ChangeDefenseClass", "Change Defense Class"));
					F->DefenseClass = NewTag;
					EndTransaction();
					RefreshPropertiesPanel();
				}))
		];
	}
#endif

	if (SelType == EHitboxSelectionType::None || SelIndices.Num() == 0) return;

	PropertiesBox->AddSlot().AutoHeight().Padding(4, 0, 4, 4)[ SNew(SSeparator) ];

	// Multi-select summary
	if (SelType == EHitboxSelectionType::Hitbox && SelIndices.Num() > 1)
	{
		PropertiesBox->AddSlot().AutoHeight().Padding(4, 2)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("MultiSelectInfo", "{0} hitboxes selected"), FText::AsNumber(SelIndices.Num())))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		];
		PropertiesBox->AddSlot().AutoHeight().Padding(4, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MultiSelectHint", "Move/Delete selected hitboxes as a group.\nUse arrow keys to nudge, Delete to remove all."))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
		];
		return;
	}

	if (SelType == EHitboxSelectionType::Hitbox && Frame->Hitboxes.IsValidIndex(SelIndex))
	{
		// Type selector (compound value: the two buttons fill the value column)
		PropertiesBox->AddSlot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("TypeLabel", "Type"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SButton).Text(LOCTEXT("AttackType", "Attack"))
					.ButtonColorAndOpacity_Lambda([this, SelIndex]()
					{
						FFrameHitboxData* F = GetCurrentFrameMutable();
						return (F && F->Hitboxes.IsValidIndex(SelIndex) && F->Hitboxes[SelIndex].Type == EHitboxType::Attack)
							? FLinearColor::Red * 0.5f : FLinearColor(0.15f, 0.15f, 0.15f);
					})
					.OnClicked_Lambda([this, SelIndex]()
					{
						if (FFrameHitboxData* F = GetCurrentFrameMutable())
						{
							if (F->Hitboxes.IsValidIndex(SelIndex))
							{
								BeginTransaction(LOCTEXT("ChangeType", "Change Hitbox Type"));
								F->Hitboxes[SelIndex].Type = EHitboxType::Attack;
								EndTransaction();
								RefreshHitboxList();
								RefreshPropertiesPanel();
							}
						}
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SButton).Text(LOCTEXT("HurtboxType", "Hurtbox"))
					.ButtonColorAndOpacity_Lambda([this, SelIndex]()
					{
						FFrameHitboxData* F = GetCurrentFrameMutable();
						return (F && F->Hitboxes.IsValidIndex(SelIndex) && F->Hitboxes[SelIndex].Type == EHitboxType::Hurtbox)
							? FLinearColor::Green * 0.5f : FLinearColor(0.15f, 0.15f, 0.15f);
					})
					.OnClicked_Lambda([this, SelIndex]()
					{
						if (FFrameHitboxData* F = GetCurrentFrameMutable())
						{
							if (F->Hitboxes.IsValidIndex(SelIndex))
							{
								BeginTransaction(LOCTEXT("ChangeType", "Change Hitbox Type"));
								F->Hitboxes[SelIndex].Type = EHitboxType::Hurtbox;
								EndTransaction();
								RefreshHitboxList();
								RefreshPropertiesPanel();
							}
						}
						return FReply::Handled();
					})
				],
				FText::GetEmpty(), 0.0f, 0.0f)
		];

		// Position
		auto MakeSpinBox = [this](auto ValueGetter, auto OnCommit, int32 MinVal, int32 MaxVal) {
			return SNew(SSpinBox<int32>).MinValue(MinVal).MaxValue(MaxVal).MinSliderValue(MinVal > -9999 ? MinVal : -500).MaxSliderValue(MaxVal < 9999 ? MaxVal : 500).Delta(1).SliderExponent(1.0f).Value_Lambda(ValueGetter).OnValueCommitted_Lambda(OnCommit);
		};

		// Float sibling for the TASK-146 float hitbox fields (Damage/Knockback): free-typed decimals,
		// no drag stepping.
		auto MakeFloatSpinBox = [this](auto ValueGetter, auto OnCommit, float MinVal, float MaxVal) {
			return SNew(SSpinBox<float>).MinValue(MinVal).MaxValue(MaxVal).MinSliderValue(MinVal).MaxSliderValue(MaxVal < 9999.f ? MaxVal : 500.f).SliderExponent(1.0f).Value_Lambda(ValueGetter).OnValueCommitted_Lambda(OnCommit);
		};

		PropertiesBox->AddSlot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("PosLabel", "Pos"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
				[
					MakeSpinBox(
						[this, SelIndex]() { FFrameHitboxData* F = GetCurrentFrameMutable(); return (F && F->Hitboxes.IsValidIndex(SelIndex)) ? F->Hitboxes[SelIndex].X : 0; },
						[this, SelIndex](int32 Val, ETextCommit::Type) { if (FFrameHitboxData* F = GetCurrentFrameMutable()) { if (F->Hitboxes.IsValidIndex(SelIndex)) { BeginTransaction(LOCTEXT("MoveHitbox", "Move Hitbox")); F->Hitboxes[SelIndex].X = Val; EndTransaction(); } } },
						-9999, 9999)
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					MakeSpinBox(
						[this, SelIndex]() { FFrameHitboxData* F = GetCurrentFrameMutable(); return (F && F->Hitboxes.IsValidIndex(SelIndex)) ? F->Hitboxes[SelIndex].Y : 0; },
						[this, SelIndex](int32 Val, ETextCommit::Type) { if (FFrameHitboxData* F = GetCurrentFrameMutable()) { if (F->Hitboxes.IsValidIndex(SelIndex)) { BeginTransaction(LOCTEXT("MoveHitbox", "Move Hitbox")); F->Hitboxes[SelIndex].Y = Val; EndTransaction(); } } },
						-9999, 9999)
				],
				FText::GetEmpty(), 0.0f, 0.0f)
		];

		// Size
		PropertiesBox->AddSlot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("SizeLabel", "Size"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
				[
					MakeSpinBox(
						[this, SelIndex]() { FFrameHitboxData* F = GetCurrentFrameMutable(); return (F && F->Hitboxes.IsValidIndex(SelIndex)) ? F->Hitboxes[SelIndex].Width : 16; },
						[this, SelIndex](int32 Val, ETextCommit::Type) { if (FFrameHitboxData* F = GetCurrentFrameMutable()) { if (F->Hitboxes.IsValidIndex(SelIndex)) { BeginTransaction(LOCTEXT("ResizeHitbox", "Resize Hitbox")); F->Hitboxes[SelIndex].Width = Val; EndTransaction(); } } },
						1, 9999)
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					MakeSpinBox(
						[this, SelIndex]() { FFrameHitboxData* F = GetCurrentFrameMutable(); return (F && F->Hitboxes.IsValidIndex(SelIndex)) ? F->Hitboxes[SelIndex].Height : 16; },
						[this, SelIndex](int32 Val, ETextCommit::Type) { if (FFrameHitboxData* F = GetCurrentFrameMutable()) { if (F->Hitboxes.IsValidIndex(SelIndex)) { BeginTransaction(LOCTEXT("ResizeHitbox", "Resize Hitbox")); F->Hitboxes[SelIndex].Height = Val; EndTransaction(); } } },
						1, 9999)
				],
				FText::GetEmpty(), 0.0f, 0.0f)
		];

		// Z and Depth (only when 3D depth is enabled)
		if (GetDefault<UPaper2DPlusSettings>()->bEnable3DDepth)
		{
			PropertiesBox->AddSlot().AutoHeight()
			[
				FProfilePropertyRowUtils::MakeRow(
					LOCTEXT("ZPosLabel", "Z Pos"),
					MakeSpinBox(
						[this, SelIndex]() { FFrameHitboxData* F = GetCurrentFrameMutable(); return (F && F->Hitboxes.IsValidIndex(SelIndex)) ? F->Hitboxes[SelIndex].Z : 0; },
						[this, SelIndex](int32 Val, ETextCommit::Type) { if (FFrameHitboxData* F = GetCurrentFrameMutable()) { if (F->Hitboxes.IsValidIndex(SelIndex)) { BeginTransaction(LOCTEXT("ChangeZPos", "Change Z Position")); F->Hitboxes[SelIndex].Z = Val; EndTransaction(); } } },
						-9999, 9999))
			];

			PropertiesBox->AddSlot().AutoHeight()
			[
				FProfilePropertyRowUtils::MakeRow(
					LOCTEXT("DepthLabel", "Depth"),
					MakeSpinBox(
						[this, SelIndex]() { FFrameHitboxData* F = GetCurrentFrameMutable(); return (F && F->Hitboxes.IsValidIndex(SelIndex)) ? F->Hitboxes[SelIndex].Depth : 0; },
						[this, SelIndex](int32 Val, ETextCommit::Type) { if (FFrameHitboxData* F = GetCurrentFrameMutable()) { if (F->Hitboxes.IsValidIndex(SelIndex)) { BeginTransaction(LOCTEXT("ChangeDepth", "Change Depth")); F->Hitboxes[SelIndex].Depth = Val; EndTransaction(); } } },
						0, 9999))
			];
		}

		// Damage, Knockback, and batch apply — only for attack hitboxes
		if (Frame->Hitboxes.IsValidIndex(SelIndex) && Frame->Hitboxes[SelIndex].Type == EHitboxType::Attack)
		{
			PropertiesBox->AddSlot().AutoHeight()
			[
				FProfilePropertyRowUtils::MakeRow(
					LOCTEXT("DamageLabel", "Damage"),
					MakeFloatSpinBox(
						[this, SelIndex]() { FFrameHitboxData* F = GetCurrentFrameMutable(); return (F && F->Hitboxes.IsValidIndex(SelIndex)) ? F->Hitboxes[SelIndex].Damage : 0.f; },
						[this, SelIndex](float Val, ETextCommit::Type) { if (FFrameHitboxData* F = GetCurrentFrameMutable()) { if (F->Hitboxes.IsValidIndex(SelIndex)) { BeginTransaction(LOCTEXT("ChangeDamage", "Change Damage")); F->Hitboxes[SelIndex].Damage = Val; EndTransaction(); } } },
						0.f, 9999.f))
			];

			PropertiesBox->AddSlot().AutoHeight()
			[
				FProfilePropertyRowUtils::MakeRow(
					LOCTEXT("KnockbackLabel", "Knockback"),
					MakeFloatSpinBox(
						[this, SelIndex]() { FFrameHitboxData* F = GetCurrentFrameMutable(); return (F && F->Hitboxes.IsValidIndex(SelIndex)) ? F->Hitboxes[SelIndex].Knockback : 0.f; },
						[this, SelIndex](float Val, ETextCommit::Type) { if (FFrameHitboxData* F = GetCurrentFrameMutable()) { if (F->Hitboxes.IsValidIndex(SelIndex)) { BeginTransaction(LOCTEXT("ChangeKnockback", "Change Knockback")); F->Hitboxes[SelIndex].Knockback = Val; EndTransaction(); } } },
						0.f, 9999.f))
			];

			// TASK-77: per-hitbox clash CATEGORY (Attack-only). Empty inherits the move's DefaultClashCategory;
			// the clash graph resolves who-beats-whom when two boxes overlap (SGameplayTagCombo is UE 5.3+).
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
			{
				FGameplayTag CurrentClash;
				if (Frame->Hitboxes.IsValidIndex(SelIndex)) { CurrentClash = Frame->Hitboxes[SelIndex].ClashCategory; }
				PropertiesBox->AddSlot().AutoHeight()
				[
					FProfilePropertyRowUtils::MakeRow(
						LOCTEXT("ClashCategoryLabel", "Clash"),
						SNew(SGameplayTagCombo)
						.Filter(TEXT("Paper2DPlus.Clash.Category"))
						.Tag(CurrentClash)
						.OnTagChanged_Lambda([this, SelIndex](const FGameplayTag NewTag)
						{
							FFrameHitboxData* F = GetCurrentFrameMutable();
							if (!F || !F->Hitboxes.IsValidIndex(SelIndex) || F->Hitboxes[SelIndex].ClashCategory == NewTag) return;
							BeginTransaction(LOCTEXT("ChangeClashCategory", "Change Clash Category"));
							F->Hitboxes[SelIndex].ClashCategory = NewTag;
							EndTransaction();
							RefreshPropertiesPanel();
						}))
				];
			}
#endif

			PropertiesBox->AddSlot().AutoHeight()
			[
				FProfilePropertyRowUtils::MakeRow(
					LOCTEXT("ApplyDmgKBTo", "Apply to"),
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
					[
						SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton.Default").Text(LOCTEXT("ApplyToFrame", "Frame"))
					.ToolTipText(LOCTEXT("ApplyDmgFrameTip", "Apply this hitbox's damage and knockback to all attack hitboxes in the current frame."))
					.OnClicked_Lambda([this, SelIndex]()
					{
						FFrameHitboxData* F = GetCurrentFrameMutable();
						if (!F || !F->Hitboxes.IsValidIndex(SelIndex)) return FReply::Handled();
						const float Dmg = F->Hitboxes[SelIndex].Damage;
						const float KB = F->Hitboxes[SelIndex].Knockback;
						BeginTransaction(LOCTEXT("BatchDmgFrame", "Apply Damage to Frame"));
						for (FHitboxData& HB : F->Hitboxes)
						{
							if (HB.Type == EHitboxType::Attack) { HB.Damage = Dmg; HB.Knockback = KB; }
						}
						EndTransaction();
						RefreshPropertiesPanel();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0)
				[
					SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton.Default").Text(LOCTEXT("ApplyToAllFrames", "All"))
					.ToolTipText(LOCTEXT("ApplyDmgAllTip", "Apply this hitbox's damage and knockback to all attack hitboxes across every frame."))
					.OnClicked_Lambda([this, SelIndex]()
					{
						FFrameHitboxData* F = GetCurrentFrameMutable();
						if (!F || !Model.IsValid() || !F->Hitboxes.IsValidIndex(SelIndex)) return FReply::Handled();
						const float Dmg = F->Hitboxes[SelIndex].Damage;
						const float KB = F->Hitboxes[SelIndex].Knockback;
						const int32 FlipbookIdx = Model->GetSelectedFlipbookIndex();
						const int32 FrameCount = Provider->GetAuthoredFrameCount(FlipbookIdx);
						BeginTransaction(LOCTEXT("BatchDmgAll", "Apply Damage to All Frames"));
						for (int32 i = 0; i < FrameCount; ++i)
						{
							if (FFrameHitboxData* FrameData = Provider->GetFrameMutable(FlipbookIdx, i))
							{
								for (FHitboxData& HB : FrameData->Hitboxes)
								{
									if (HB.Type == EHitboxType::Attack) { HB.Damage = Dmg; HB.Knockback = KB; }
								}
							}
						}
						EndTransaction();
						RefreshPropertiesPanel();
						return FReply::Handled();
					})
				],
				FText::GetEmpty(), 0.0f, 0.0f)
			];
		}
	}
	else if (SelType == EHitboxSelectionType::Socket && Frame->Sockets.IsValidIndex(SelIndex))
	{
		// Socket name
		PropertiesBox->AddSlot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("NameLabel", "Name"),
				SNew(SEditableTextBox)
				.Text_Lambda([this, SelIndex]() { FFrameHitboxData* F = GetCurrentFrameMutable(); return (F && F->Sockets.IsValidIndex(SelIndex)) ? FText::FromString(F->Sockets[SelIndex].Name) : FText(); })
				.OnTextCommitted_Lambda([this, SelIndex](const FText& Text, ETextCommit::Type) {
					if (FFrameHitboxData* F = GetCurrentFrameMutable()) { if (F->Sockets.IsValidIndex(SelIndex)) { BeginTransaction(LOCTEXT("RenameSocket", "Rename Socket")); F->Sockets[SelIndex].Name = Text.ToString(); EndTransaction(); RefreshHitboxList(); } }
				}))
		];

		// Socket position
		PropertiesBox->AddSlot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("SocketPosLabel", "Pos"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
				[
					SNew(SSpinBox<int32>).MinValue(-9999).MaxValue(9999).MinSliderValue(-500).MaxSliderValue(500).Delta(1).SliderExponent(1.0f).LinearDeltaSensitivity(1)
					.Value_Lambda([this, SelIndex]() { FFrameHitboxData* F = GetCurrentFrameMutable(); return (F && F->Sockets.IsValidIndex(SelIndex)) ? F->Sockets[SelIndex].X : 0; })
					.OnValueCommitted_Lambda([this, SelIndex](int32 Val, ETextCommit::Type) { if (FFrameHitboxData* F = GetCurrentFrameMutable()) { if (F->Sockets.IsValidIndex(SelIndex)) { BeginTransaction(LOCTEXT("MoveSocket", "Move Socket")); F->Sockets[SelIndex].X = Val; EndTransaction(); } } })
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SSpinBox<int32>).MinValue(-9999).MaxValue(9999).MinSliderValue(-500).MaxSliderValue(500).Delta(1).SliderExponent(1.0f).LinearDeltaSensitivity(1)
					.Value_Lambda([this, SelIndex]() { FFrameHitboxData* F = GetCurrentFrameMutable(); return (F && F->Sockets.IsValidIndex(SelIndex)) ? F->Sockets[SelIndex].Y : 0; })
					.OnValueCommitted_Lambda([this, SelIndex](int32 Val, ETextCommit::Type) { if (FFrameHitboxData* F = GetCurrentFrameMutable()) { if (F->Sockets.IsValidIndex(SelIndex)) { BeginTransaction(LOCTEXT("MoveSocket", "Move Socket")); F->Sockets[SelIndex].Y = Val; EndTransaction(); } } })
				],
				FText::GetEmpty(), 0.0f, 0.0f)
		];
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildCopyOperationsPanel
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SHitboxEditorPanel::BuildCopyOperationsPanel()
{
	if (CopySourceOptions.IsEmpty())
	{
		CopySourceOptions.Add(MakeShared<FString>(TEXT("Current Frame")));
		CopySourceOptions.Add(MakeShared<FString>(TEXT("Previous Frame")));
		CopyTargetOptions.Add(MakeShared<FString>(TEXT("All Frames")));
		CopyTargetOptions.Add(MakeShared<FString>(TEXT("Selected Frames")));
		CopyTargetOptions.Add(MakeShared<FString>(TEXT("Remaining Frames")));
		CopyScopeOptions.Add(MakeShared<FString>(TEXT("All Hitboxes")));
		CopyScopeOptions.Add(MakeShared<FString>(TEXT("Selected")));
		CopyScopeOptions.Add(MakeShared<FString>(TEXT("Attackboxes")));
		CopyScopeOptions.Add(MakeShared<FString>(TEXT("Hurtboxes")));
	}

	// Shared details-style rows; the hosting section card/category already titles this section,
	// so the old duplicated inner "Frame Operations" heading is gone.
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("CopyFromLabel", "From"),
				FProfilePropertyRowUtils::MakeStringCombo(&CopySourceOptions, &CopySourceIndex))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("CopyToLabel", "To"),
				FProfilePropertyRowUtils::MakeStringCombo(&CopyTargetOptions, &CopyTargetIndex))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("CopyScopeLabel", "Scope"),
				FProfilePropertyRowUtils::MakeStringCombo(&CopyScopeOptions, &CopyHitboxScopeIndex))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			FProfilePropertyRowUtils::MakeRow(
				LOCTEXT("MergeLabel", "Merge"),
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bCopyMerge ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bCopyMerge = (State == ECheckBoxState::Checked); }),
				LOCTEXT("MergeTip", "Merge: add hitboxes to existing ones instead of replacing"))
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(4, 6, 4, 2)
		[
				SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton.Default").Text(LOCTEXT("ApplyCopy", "Apply"))
				.IsEnabled_Lambda([this]()
				{
					if (CopyTargetIndex == 1 && Model.IsValid() && Model->GetSelectedFrames().Num() == 0) return false;
					if (CopyHitboxScopeIndex == 1 && EditorCanvas.IsValid() && EditorCanvas->GetSelectedIndices().Num() == 0) return false;
					return true;
				})
				.OnClicked_Lambda([this]()
				{
					// All frame-data access routes through the provider so the same copy op writes the
					// profile in the profile editor and the scoped variant's CombatFrames in the layer editor.
					if (!Model.IsValid()) return FReply::Handled();
					const int32 FlipbookIdx = Model->GetSelectedFlipbookIndex();

					int32 SelectedFrameIndex = Model->GetSelectedFrameIndex();
					int32 SourceFrame = SelectedFrameIndex;
					// 'Selected' hitbox scope filters by the canvas selection, which only exists for
					// the displayed frame — so it always sources from the current frame, ignoring the
					// Previous Frame option. Other scopes honor Previous Frame as before.
					if (CopyHitboxScopeIndex != 1 && CopySourceIndex == 1) { SourceFrame = SelectedFrameIndex - 1; if (SourceFrame < 0) return FReply::Handled(); }

					// Gate BEFORE opening the transaction: in layer scope an animation has no authored rows
					// until the first box is drawn, so a copy with no source is a true no-op — opening a
					// transaction would spuriously dirty the LAYER asset + push an empty undo entry.
					if (!Provider->GetFrame(FlipbookIdx, SourceFrame)) return FReply::Handled();

					BeginTransaction(LOCTEXT("CopyFrameOp", "Copy Frame Hitboxes"));

					if (CopyHitboxScopeIndex == 0)
					{
						const int32 FrameCount = Provider->GetAuthoredFrameCount(FlipbookIdx);
						if (CopyTargetIndex == 0) { Provider->CopyFrameDataToRange(FlipbookIdx, SourceFrame, 0, FrameCount - 1, true, bCopyMerge); }
						else if (CopyTargetIndex == 1) { ForEachSelectedFrame([&](int32 TargetIdx) { if (TargetIdx != SourceFrame) Provider->CopyFrameDataToRange(FlipbookIdx, SourceFrame, TargetIdx, TargetIdx, true, bCopyMerge); }); }
						else if (CopyTargetIndex == 2 && SourceFrame + 1 < FrameCount) { Provider->CopyFrameDataToRange(FlipbookIdx, SourceFrame, SourceFrame + 1, FrameCount - 1, true, bCopyMerge); }
					}
					else
					{
						const FFrameHitboxData* SourceData = Provider->GetFrame(FlipbookIdx, SourceFrame);
						if (!SourceData) { EndTransaction(); return FReply::Handled(); }
						TArray<FHitboxData> FilteredHitboxes;
						TArray<int32> CanvasSelectedIndices;
						if (CopyHitboxScopeIndex == 1 && EditorCanvas.IsValid()) CanvasSelectedIndices = EditorCanvas->GetSelectedIndices();
						for (int32 HBIdx = 0; HBIdx < SourceData->Hitboxes.Num(); ++HBIdx)
						{
							const FHitboxData& HB = SourceData->Hitboxes[HBIdx];
							bool bInclude = false;
							switch (CopyHitboxScopeIndex)
							{
							case 1: bInclude = CanvasSelectedIndices.Contains(HBIdx); break;
							case 2: bInclude = (HB.Type == EHitboxType::Attack); break;
							case 3: bInclude = (HB.Type == EHitboxType::Hurtbox); break;
							default: bInclude = true; break;
							}
							if (bInclude) FilteredHitboxes.Add(HB);
						}

						TArray<int32> TargetFrames;
						const int32 FrameCount = Provider->GetAuthoredFrameCount(FlipbookIdx);
						if (CopyTargetIndex == 0) { for (int32 i = 0; i < FrameCount; ++i) { if (i != SourceFrame) TargetFrames.Add(i); } }
						else if (CopyTargetIndex == 1) { ForEachSelectedFrame([&](int32 TargetIdx) { if (TargetIdx != SourceFrame) TargetFrames.Add(TargetIdx); }); }
						else if (CopyTargetIndex == 2) { for (int32 i = SourceFrame + 1; i < FrameCount; ++i) TargetFrames.Add(i); }

						UPaperFlipbook* Flipbook = nullptr;
						if (const FFlipbookProfileEntry* Anim = GetCurrentFlipbookData())
						{
							if (!Anim->Identity.Flipbook.IsNull()) Flipbook = Anim->Identity.Flipbook.LoadSynchronous();
						}
						for (int32 TargetIdx : TargetFrames)
						{
							FFrameHitboxData* TargetFrame = Provider->GetFrameMutable(FlipbookIdx, TargetIdx);
							if (!TargetFrame) continue;
							if (bCopyMerge) TargetFrame->Hitboxes.Append(FilteredHitboxes);
							else TargetFrame->Hitboxes = FilteredHitboxes;
							ClampFrameHitboxesToBounds(*TargetFrame, Flipbook, TargetIdx);
						}
					}

					EndTransaction();
					RefreshFrameList();
					RefreshHitboxList();
					RefreshPropertiesPanel();
					return FReply::Handled();
				})
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(4, 6, 4, 2)[ SNew(SSeparator) ]

		+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
			[
				SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton.Default").Text(LOCTEXT("ClearFrameShort", "Clear Frame"))
				.ToolTipText(LOCTEXT("ClearFrameTooltip", "Remove all hitboxes and sockets from this frame"))
				.OnClicked_Lambda([this]() { OnClearCurrentFrame(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0, 0, 0)
			[
				SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton.Default").Text(LOCTEXT("ClearSelectedShort", "Clear Selected"))
				.IsEnabled_Lambda([this]() { return Model.IsValid() && Model->GetSelectedFrames().Num() > 0; })
				.ToolTipText(LOCTEXT("ClearSelectedTooltip", "Remove all hitboxes and sockets from the selected frames"))
				.OnClicked_Lambda([this]()
				{
					if (!Model.IsValid()) return FReply::Handled();
					const int32 FlipbookIdx = Model->GetSelectedFlipbookIndex();
					// Gate BEFORE opening the transaction: if no selected frame carries any authored box/socket
					// (layer scope with no override), clearing is a no-op — don't dirty the asset or push an
					// empty undo entry.
					bool bAnythingToClear = false;
					ForEachSelectedFrame([&](int32 Idx)
					{
						if (const FFrameHitboxData* F = Provider->GetFrame(FlipbookIdx, Idx))
						{
							if (F->Hitboxes.Num() > 0 || F->Sockets.Num() > 0) { bAnythingToClear = true; }
						}
					});
					if (!bAnythingToClear) return FReply::Handled();

					BeginTransaction(LOCTEXT("ClearSelected", "Clear Selected Frames"));
					ForEachSelectedFrame([&](int32 Idx)
					{
						// Find-only: a frame with no authored data (layer scope, no override) has nothing to clear.
						if (FFrameHitboxData* F = Provider->GetFrameMutable(FlipbookIdx, Idx))
						{
							F->Hitboxes.Empty();
							F->Sockets.Empty();
						}
					});
					EndTransaction();
					RefreshFrameList();
					RefreshHitboxList();
					RefreshPropertiesPanel();
					return FReply::Handled();
				})
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Clear operations
// ─────────────────────────────────────────────────────────────────────────────

void SHitboxEditorPanel::OnClearCurrentFrame()
{
	// Find-only: clearing a frame that has no authored row is a no-op (nothing to clear) — never create an
	// override entry just to empty it.
	if (!GetCurrentFrameMutable()) return;
	BeginTransaction(LOCTEXT("ClearFrame", "Clear Frame"));
	if (FFrameHitboxData* Frame = GetCurrentFrameMutable())
	{
		Frame->Hitboxes.Empty();
		Frame->Sockets.Empty();
	}
	if (EditorCanvas.IsValid()) EditorCanvas->ClearSelection();
	EndTransaction();
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

// ─────────────────────────────────────────────────────────────────────────────
// Add / Delete
// ─────────────────────────────────────────────────────────────────────────────

void SHitboxEditorPanel::AddNewHitbox()
{
	// Genuine first-write site: create-on-first-edit so the layer scope authors an override entry (the
	// profile scope pre-syncs its rows, so Ensure is a plain find). Gate on CanEnsureFrame BEFORE opening
	// the transaction so a doomed add (no scope / bad frame) never commits an empty undo entry.
	if (!Model.IsValid() || !Provider->CanEnsureFrame(Model->GetSelectedFlipbookIndex(), Model->GetSelectedFrameIndex())) return;

	BeginTransaction(LOCTEXT("AddHitbox", "Add Hitbox"));
	FFrameHitboxData* Frame = Provider->EnsureFrameMutable(Model->GetSelectedFlipbookIndex(), Model->GetSelectedFrameIndex());
	if (!Frame) { EndTransaction(); return; }
	FHitboxData NewHitbox;
	NewHitbox.Type = ActiveDrawType;
	NewHitbox.Damage = 0;
	NewHitbox.Knockback = 0;

	FVector2D SpriteDims(128.0f, 128.0f);
	if (EditorCanvas.IsValid()) SpriteDims = EditorCanvas->GetSpriteDimensions();
	auto SnapVal = [](int32 Val) { return FMath::Max(16, (FMath::RoundToInt((float)Val / 16) * 16)); };
	int32 DefaultW = SnapVal(FMath::RoundToInt(SpriteDims.X * 0.25f));
	int32 DefaultH = SnapVal(FMath::RoundToInt(SpriteDims.Y * 0.25f));
	NewHitbox.Width = DefaultW;
	NewHitbox.Height = DefaultH;
	NewHitbox.X = FMath::RoundToInt((SpriteDims.X - DefaultW) * 0.5f / 16) * 16;
	NewHitbox.Y = FMath::RoundToInt((SpriteDims.Y - DefaultH) * 0.5f / 16) * 16;

	int32 NewIndex = Frame->Hitboxes.Add(NewHitbox);
	EndTransaction();

	if (EditorCanvas.IsValid()) EditorCanvas->SetSelection(EHitboxSelectionType::Hitbox, NewIndex);
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

void SHitboxEditorPanel::AddNewSocket()
{
	// Genuine first-write site (see AddNewHitbox): create-on-first-edit, guarded before the transaction.
	if (!Model.IsValid() || !Provider->CanEnsureFrame(Model->GetSelectedFlipbookIndex(), Model->GetSelectedFrameIndex())) return;

	BeginTransaction(LOCTEXT("AddSocket", "Add Socket"));
	FFrameHitboxData* Frame = Provider->EnsureFrameMutable(Model->GetSelectedFlipbookIndex(), Model->GetSelectedFrameIndex());
	if (!Frame) { EndTransaction(); return; }
	FSocketData NewSocket;
	NewSocket.Name = FString::Printf(TEXT("Socket%d"), Frame->Sockets.Num());

	FVector2D SpriteDims(128.0f, 128.0f);
	if (EditorCanvas.IsValid()) SpriteDims = EditorCanvas->GetSpriteDimensions();
	NewSocket.X = FMath::RoundToInt(SpriteDims.X * 0.5f / 16) * 16;
	NewSocket.Y = FMath::RoundToInt(SpriteDims.Y * 0.5f / 16) * 16;

	int32 NewIndex = Frame->Sockets.Add(NewSocket);
	EndTransaction();

	if (EditorCanvas.IsValid()) EditorCanvas->SetSelection(EHitboxSelectionType::Socket, NewIndex);
	RefreshHitboxList();
	RefreshPropertiesPanel();
}

void SHitboxEditorPanel::DeleteSelected()
{
	if (EditorCanvas.IsValid()) EditorCanvas->DeleteSelection();
}

#undef LOCTEXT_NAMESPACE
