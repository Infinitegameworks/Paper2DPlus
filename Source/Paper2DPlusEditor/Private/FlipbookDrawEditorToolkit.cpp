// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FlipbookDrawEditorToolkit.h"
#include "FlipbookDrawModel.h"
#include "FlipbookDrawCanvas.h"
#include "FlipbookDrawToolsPanel.h"
#include "FlipbookPixelEdit.h"
#include "EditorCanvasUtils.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "HAL/IConsoleManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Engine/Texture2D.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "FlipbookDrawEditor"

const FName FFlipbookDrawEditorToolkit::CanvasTabId(TEXT("FlipbookDrawEditor_Canvas"));
const FName FFlipbookDrawEditorToolkit::ToolsTabId(TEXT("FlipbookDrawEditor_Tools"));

// ============================================================================
// Console commands — let ECABridge / scripts open the draw editor and verify a
// drawn pixel without needing a screenshot of the floating editor window.
// ============================================================================
namespace
{
	TWeakObjectPtr<UPaperFlipbook> GLastDrawFlipbook;

	UPaperFlipbook* ResolveDrawFlipbook(const TArray<FString>& Args)
	{
		if (Args.Num() > 0)
		{
			return LoadObject<UPaperFlipbook>(nullptr, *Args[0]);
		}
		// No path given → open the first UPaperFlipbook the asset registry knows about.
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> Assets;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
		ARM.Get().GetAssetsByClass(UPaperFlipbook::StaticClass()->GetClassPathName(), Assets);
#else
		ARM.Get().GetAssetsByClass(UPaperFlipbook::StaticClass()->GetFName(), Assets); // UE 5.0: GetAssetsByClass takes an FName
#endif
		for (const FAssetData& AD : Assets)
		{
			if (UPaperFlipbook* FB = Cast<UPaperFlipbook>(AD.GetAsset()))
			{
				return FB;
			}
		}
		return nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs GOpenDrawEditorCmd(
		TEXT("Paper2DPlus.OpenDrawEditor"),
		TEXT("Open the Paper2D+ Flipbook Draw editor. Arg: optional /Game object path; default = first flipbook found."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
		{
			UPaperFlipbook* FB = ResolveDrawFlipbook(Args);
			if (!FB)
			{
				UE_LOG(LogTemp, Warning, TEXT("[DrawTool] OpenDrawEditor: no flipbook found/loaded"));
				return;
			}
			GLastDrawFlipbook = FB;
			TSharedRef<FFlipbookDrawEditorToolkit> Toolkit = MakeShared<FFlipbookDrawEditorToolkit>();
			Toolkit->InitEditor(EToolkitMode::Standalone, nullptr, FB);
			UE_LOG(LogTemp, Display, TEXT("[DrawTool] OpenDrawEditor: opened %s (%d frames)"), *FB->GetPathName(), FB->GetNumKeyFrames());
		}));

	FAutoConsoleCommandWithWorldAndArgs GDrawProbeCmd(
		TEXT("Paper2DPlus.DrawProbe"),
		TEXT("Log a source pixel of the last-opened draw flipbook. Args: [x] [y] [frame] (default: center, frame 0)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
		{
			UPaperFlipbook* FB = GLastDrawFlipbook.Get();
			if (!FB || FB->GetNumKeyFrames() == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[DrawTool] DrawProbe: no active draw flipbook"));
				return;
			}
			const int32 Frame = (Args.Num() > 2) ? FCString::Atoi(*Args[2]) : 0;
			UPaperSprite* Sprite = (Frame >= 0 && Frame < FB->GetNumKeyFrames()) ? FB->GetKeyFrameChecked(Frame).Sprite : nullptr;
			TArray<FColor> Px; int32 W = 0, H = 0;
			if (!Sprite || !FFlipbookPixelEdit::ReadFrame(Sprite, Px, W, H))
			{
				UE_LOG(LogTemp, Warning, TEXT("[DrawTool] DrawProbe: ReadFrame failed (frame %d)"), Frame);
				return;
			}
			int32 X = (Args.Num() > 0) ? FCString::Atoi(*Args[0]) : W / 2;
			int32 Y = (Args.Num() > 1) ? FCString::Atoi(*Args[1]) : H / 2;
			X = FMath::Clamp(X, 0, W - 1);
			Y = FMath::Clamp(Y, 0, H - 1);
			const FColor C = Px[Y * W + X];
			UE_LOG(LogTemp, Display, TEXT("[DrawTool] DrawProbe frame=%d (%d,%d) of %dx%d = R%d G%d B%d A%d"),
				Frame, X, Y, W, H, C.R, C.G, C.B, C.A);
		}));
}

FFlipbookDrawEditorToolkit::~FFlipbookDrawEditorToolkit()
{
	Canvas.Reset();
	Model.Reset();
}

void FFlipbookDrawEditorToolkit::InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UPaperFlipbook* InFlipbook)
{
	EditedFlipbook = InFlipbook;

	Model = MakeShared<FFlipbookDrawModel>();
	Model->SetFlipbook(InFlipbook);

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("FlipbookDrawEditor_Layout_v2")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.18f)
				->AddTab(ToolsTabId, ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.82f)
				->AddTab(CanvasTabId, ETabState::OpenedTab)
				->SetForegroundTab(CanvasTabId)
			)
		);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = false;

	FAssetEditorToolkit::InitAssetEditor(
		Mode,
		InitToolkitHost,
		TEXT("Paper2DPlusFlipbookDrawEditorApp"),
		Layout,
		bCreateDefaultStandaloneMenu,
		bCreateDefaultToolbar,
		InFlipbook
	);

	// Heads-up: drawing edits the frames' source texture(s) IN PLACE (destructive; Ctrl+S to save).
	// Anything else that samples those textures is affected — a precise external-referencer warning is a
	// documented follow-up; for now we set the expectation up front.
	{
		TSet<UTexture2D*> SourceTextures;
		for (int32 i = 0; i < InFlipbook->GetNumKeyFrames(); ++i)
		{
			if (UPaperSprite* Sprite = InFlipbook->GetKeyFrameChecked(i).Sprite)
			{
				if (UTexture2D* Tex = Cast<UTexture2D>(Sprite->GetSourceTexture()))
				{
					SourceTextures.Add(Tex);
				}
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[FlipbookDraw] Editing %s — %d frame(s) across %d source texture(s); edits are in-place (Ctrl+S to save)."),
			*InFlipbook->GetName(), InFlipbook->GetNumKeyFrames(), SourceTextures.Num());

		FNotificationInfo Info(FText::FromString(TEXT("Paper2D+ Draw: edits write into the frame's source texture in place. Ctrl+S to save.")));
		Info.ExpireDuration = 5.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
}

void FFlipbookDrawEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_FlipbookDrawEditor", "Flipbook Draw"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(CanvasTabId, FOnSpawnTab::CreateSP(this, &FFlipbookDrawEditorToolkit::SpawnTab_Canvas))
		.SetDisplayName(LOCTEXT("CanvasTab", "Canvas"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(ToolsTabId, FOnSpawnTab::CreateSP(this, &FFlipbookDrawEditorToolkit::SpawnTab_Tools))
		.SetDisplayName(LOCTEXT("ToolsTab", "Tools"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FFlipbookDrawEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(CanvasTabId);
	InTabManager->UnregisterTabSpawner(ToolsTabId);
}

TSharedRef<SDockTab> FFlipbookDrawEditorToolkit::SpawnTab_Tools(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ToolsTabLabel", "Tools"))
		[
			SNew(SFlipbookDrawToolsPanel)
			.Model(Model)
			.OnFrameTransform(FOnFlipbookFrameTransform::CreateLambda([this](EFlipbookPixelTransform Transform)
			{
				if (Canvas.IsValid())
				{
					Canvas->ApplyFrameTransform(Transform);
				}
			}))
			.CanFrameTransform_Lambda([this]()
			{
				return Canvas.IsValid() && Canvas->CanApplyFrameTransform();
			})
		];
}

TSharedRef<SWidget> FFlipbookDrawEditorToolkit::BuildFrameStrip()
{
	TSharedRef<SScrollBox> Strip = SNew(SScrollBox).Orientation(Orient_Horizontal);

	const int32 NumFrames = Model.IsValid() ? Model->GetNumFrames() : 0;
	TWeakPtr<FFlipbookDrawModel> ModelWeak = Model;

	for (int32 i = 0; i < NumFrames; ++i)
	{
		FFrameStripCellArgs CellArgs;
		CellArgs.Sprite = Model->GetSpriteAt(i);
		CellArgs.FrameIndex = i;
		CellArgs.IsSelected = [ModelWeak, i]() -> bool
		{
			TSharedPtr<FFlipbookDrawModel> M = ModelWeak.Pin();
			return M.IsValid() && M->GetCurrentFrame() == i;
		};
		CellArgs.OnMouseButtonDown = [ModelWeak, i](const FPointerEvent&) -> FReply
		{
			if (TSharedPtr<FFlipbookDrawModel> M = ModelWeak.Pin())
			{
				M->SetCurrentFrame(i);
			}
			return FReply::Handled();
		};

		Strip->AddSlot().Padding(2.0f, 0.0f)
		[
			FFrameStripCellUtils::Build(CellArgs)
		];
	}

	return Strip;
}

TSharedRef<SDockTab> FFlipbookDrawEditorToolkit::SpawnTab_Canvas(const FSpawnTabArgs& Args)
{
	TWeakPtr<FFlipbookDrawModel> ModelWeak = Model;

	Canvas = SNew(SFlipbookDrawCanvas).Model(Model);

	return SNew(SDockTab)
		.Label(LOCTEXT("CanvasTabLabel", "Canvas"))
		[
			SNew(SVerticalBox)

			// Title: "{FlipbookName}   Frame N/Total"
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.0f, 2.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				.Text_Lambda([ModelWeak]() -> FText
				{
					TSharedPtr<FFlipbookDrawModel> M = ModelWeak.Pin();
					if (!M.IsValid() || !M->GetFlipbook())
					{
						return LOCTEXT("NoFlipbook", "No Flipbook");
					}
					return FText::Format(
						LOCTEXT("CanvasTitleFmt", "{0}   Frame {1}/{2}"),
						FText::FromString(M->GetFlipbook()->GetName()),
						FText::AsNumber(M->GetCurrentFrame() + 1),
						FText::AsNumber(M->GetNumFrames()));
				})
			]

			// Canvas (clip-to-bounds host so zoomed/panned draws can't bleed)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(0.0f)
				.Clipping(EWidgetClipping::ClipToBounds)
				[
					Canvas.ToSharedRef()
				]
			]

			// Frame strip
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f, 2.0f)
			[
				SNew(SBox)
				.HeightOverride(72.0f)
				[
					BuildFrameStrip()
				]
			]
		];
}

FName FFlipbookDrawEditorToolkit::GetToolkitFName() const
{
	return FName("FlipbookDrawEditor");
}

FText FFlipbookDrawEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "Paper2D+ Flipbook Draw");
}

FString FFlipbookDrawEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "FlipbookDraw ").ToString();
}

FLinearColor FFlipbookDrawEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.2f, 0.4f, 0.7f, 1.0f);
}

#undef LOCTEXT_NAMESPACE
