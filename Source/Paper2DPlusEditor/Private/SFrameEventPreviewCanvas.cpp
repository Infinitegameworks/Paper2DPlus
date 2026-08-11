// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "SFrameEventPreviewCanvas.h"
#include "CharacterProfileEditorModel.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewHost.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusAppearanceResolver.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusLayerDraw.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewContext.h"
#include "FrameCues/Paper2DPlusSpawnFlipbookCue.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "UnrealWidgetFwd.h"
#include "AudioDevice.h"
#include "EditorCanvasUtils.h"
#include "EditorViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "FrameEventPreviewCanvas"

/** Read-only orthographic client for the host-owned EditorPreview world. */
class FFrameEventPreviewViewportClient : public FEditorViewportClient
{
public:
	FFrameEventPreviewViewportClient(
		FPaper2DPlusFrameCuePreviewHost* InPreviewHost,
		const TSharedRef<SFrameEventPreviewCanvas>& InViewport)
		: FEditorViewportClient(
			nullptr,
			InPreviewHost ? InPreviewHost->GetPreviewScene() : nullptr,
			StaticCastSharedRef<SEditorViewport>(InViewport))
		, PreviewHost(InPreviewHost)
		, OwnerCanvas(InViewport)
	{
		SetRealtime(true);
		SetViewModes(VMI_Lit, VMI_Lit);
		SetViewportType(LVT_OrthoXZ);
		// Behave like Unreal's normal animation preview rather than a thumbnail renderer. The old
		// stripped flags removed the familiar grid and also suppressed translucent/post-process and
		// editor-primitive output created by designer Cue graphs.
		//
		// This block MUST come after SetViewModes/SetViewportType: both re-run the engine's
		// ApplyViewMode, and on UE 5.0-5.3 that function force-clears PostProcessing for every
		// orthographic view (the clamp is gone in 5.4+). The flag is an ordinary user-toggleable
		// state in ortho viewports on those engines - the clamp is only a view-mode-application
		// default - so asserting it afterwards keeps the designer-visuals contract uniform across
		// the supported range.
		DrawHelper.bDrawGrid = true;
		EngineShowFlags.SetGrid(true);
		EngineShowFlags.SetCompositeEditorPrimitives(true);
		EngineShowFlags.SetPaper2DSprites(true);
		EngineShowFlags.SetBillboardSprites(true);
		EngineShowFlags.SetParticles(true);
		EngineShowFlags.SetTranslucency(true);
		EngineShowFlags.SetSeparateTranslucency(true);
		EngineShowFlags.SetPostProcessing(true);
		EngineShowFlags.SetPostProcessMaterial(true);

		const FVector PaperPlaneNormal(0.0f, 1.0f, 0.0f);
		SetInitialViewTransform(
			LVT_OrthoXZ,
			-100.0f * PaperPlaneNormal,
			PaperPlaneNormal.Rotation().GetInverse(),
			1.0f);
	}

	virtual void Tick(float DeltaSeconds) override
	{
		// The visible realtime viewport is the one world-clock authority. This lets ordinary Cue timers
		// and latent work advance even when they create no immediate resource, without turning every
		// historical Cue invocation into a permanent global ticker.
		if (PreviewHost)
		{
			PreviewHost->Tick(DeltaSeconds);
		}
		ApplyCameraOffset();
		FEditorViewportClient::Tick(DeltaSeconds);
	}

	virtual void SetupViewForRendering(
		FSceneViewFamily& ViewFamily,
		FSceneView& View) override
	{
		FEditorViewportClient::SetupViewForRendering(ViewFamily, View);
		if (!bHasAudioFocus || !PreviewHost)
		{
			return;
		}
		if (UWorld* World = PreviewHost->GetPreviewWorld())
		{
			if (FAudioDevice* AudioDevice = World->GetAudioDeviceRaw())
			{
				FTransform ListenerTransform(GetViewRotation());
				ListenerTransform.SetLocation(GetViewLocation());
				AudioDevice->SetListener(World, 0, ListenerTransform, 0.0f);
			}
		}
	}

	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override
	{
		FEditorViewportClient::Draw(View, PDI);
		if (!PDI || !PreviewHost)
		{
			return;
		}
		const UPaper2DPlusFrameCuePreviewContext* Context = PreviewHost->GetContext();
		const AActor* Subject = PreviewHost->GetPreviewActor();
		if (!Context || !Subject)
		{
			return;
		}

		// These are render-pass projections, not line-batcher resources. Drawing them through PDI
		// prevents a 60 Hz preview from accumulating one-second DrawDebug primitives after the owning
		// adapter handle has expired.
		const FTransform SubjectTransform = Subject->GetActorTransform();
		const auto ToWorld = [&SubjectTransform](const FVector2D& Point)
		{
			return SubjectTransform.TransformPosition(FVector(Point.X, -1.0f, Point.Y));
		};
		for (const FPaper2DPlusPreviewProjectile& Projectile
			: Context->GetProjectileProxies())
		{
			for (int32 PointIndex = 1;
				PointIndex < Projectile.Trajectory.Num();
				++PointIndex)
			{
				PDI->DrawTranslucentLine(
					ToWorld(Projectile.Trajectory[PointIndex - 1]),
					ToWorld(Projectile.Trajectory[PointIndex]),
					Projectile.Color.CopyWithNewOpacity(0.55f),
					SDPG_Foreground,
					1.5f,
					/*DepthBias=*/0.0f,
					/*bScreenSpace=*/true);
			}
			PDI->DrawPoint(
				ToWorld(Projectile.Position),
				Projectile.Color,
				9.0f,
				SDPG_Foreground);
		}
		for (const FPaper2DPlusPreviewShape& Shape : Context->GetShapes())
		{
			const FVector2D HalfSize(
				FMath::Max(0.5f, Shape.Size.X * 0.5f),
				FMath::Max(0.5f, Shape.Size.Y * 0.5f));
			const FVector2D Corners[] = {
				Shape.Center + FVector2D(-HalfSize.X, -HalfSize.Y),
				Shape.Center + FVector2D(HalfSize.X, -HalfSize.Y),
				Shape.Center + FVector2D(HalfSize.X, HalfSize.Y),
				Shape.Center + FVector2D(-HalfSize.X, HalfSize.Y)
			};
			for (int32 CornerIndex = 0; CornerIndex < UE_ARRAY_COUNT(Corners); ++CornerIndex)
			{
				PDI->DrawTranslucentLine(
					ToWorld(Corners[CornerIndex]),
					ToWorld(Corners[(CornerIndex + 1) % UE_ARRAY_COUNT(Corners)]),
					Shape.Color,
					SDPG_Foreground,
					1.5f,
					/*DepthBias=*/0.0f,
					/*bScreenSpace=*/true);
			}
		}

		DrawSpawnFlipbookGhost(PDI);
	}

	/**
	 * Wire footprint of the selected Spawn Flipbook Cue's effect at its authored offset. The real
	 * effect only exists while playing; this ghost is what the designer drags between plays. Paint
	 * never loads: an unresolved soft flipbook falls back to a small fixed footprint.
	 */
	void DrawSpawnFlipbookGhost(FPrimitiveDrawInterface* PDI) const
	{
		const TSharedPtr<SFrameEventPreviewCanvas> Canvas = OwnerCanvas.Pin();
		if (!Canvas.IsValid())
		{
			return;
		}
		const UPaper2DPlusSpawnFlipbookCue* SpawnCue = Canvas->ResolveDraggableSpawnCue();
		FTransform EffectTransform;
		if (!SpawnCue || !Canvas->TryGetSelectedCueEffectTransform(EffectTransform))
		{
			return;
		}

		FVector2D HalfSize(8.0f, 8.0f);
		if (const UPaperFlipbook* Art = SpawnCue->EffectFlipbook.Get())
		{
			if (const UPaperSprite* Sprite =
				Art->GetSpriteAtFrame(0))
			{
				const FBoxSphereBounds Bounds = Sprite->GetRenderBounds();
				HalfSize = FVector2D(
					FMath::Max(1.0f, static_cast<float>(Bounds.BoxExtent.X)),
					FMath::Max(1.0f, static_cast<float>(Bounds.BoxExtent.Z)));
			}
		}

		const FLinearColor GhostColor =
			SpawnCue->Tint.CopyWithNewOpacity(0.9f);
		const FVector Corners[] = {
			EffectTransform.TransformPosition(FVector(-HalfSize.X, 0.0f, -HalfSize.Y)),
			EffectTransform.TransformPosition(FVector(HalfSize.X, 0.0f, -HalfSize.Y)),
			EffectTransform.TransformPosition(FVector(HalfSize.X, 0.0f, HalfSize.Y)),
			EffectTransform.TransformPosition(FVector(-HalfSize.X, 0.0f, HalfSize.Y))
		};
		for (int32 CornerIndex = 0; CornerIndex < UE_ARRAY_COUNT(Corners); ++CornerIndex)
		{
			PDI->DrawTranslucentLine(
				Corners[CornerIndex],
				Corners[(CornerIndex + 1) % UE_ARRAY_COUNT(Corners)],
				GhostColor,
				SDPG_Foreground,
				1.5f,
				/*DepthBias=*/0.0f,
				/*bScreenSpace=*/true);
		}
		PDI->DrawPoint(
			EffectTransform.GetLocation(),
			GhostColor,
			7.0f,
			SDPG_Foreground);
	}

	// ── Spawn Flipbook Cue offset gizmo: the standard translate widget over one placement class. ──

	virtual UE::Widget::EWidgetMode GetWidgetMode() const override
	{
		const TSharedPtr<SFrameEventPreviewCanvas> Canvas = OwnerCanvas.Pin();
		return Canvas.IsValid() && Canvas->CanDragSelectedCueOffset()
			? UE::Widget::WM_Translate
			: UE::Widget::WM_None;
	}

	virtual FVector GetWidgetLocation() const override
	{
		const TSharedPtr<SFrameEventPreviewCanvas> Canvas = OwnerCanvas.Pin();
		return Canvas.IsValid()
			? Canvas->GetSelectedCueOffsetWorldLocation()
			: FVector::ZeroVector;
	}

	virtual FMatrix GetWidgetCoordSystem() const override
	{
		return FMatrix::Identity;
	}

	virtual ECoordSystem GetWidgetCoordSystemSpace() const override
	{
		return COORD_World;
	}

	virtual void TrackingStarted(
		const FInputEventState& InInputState,
		bool bIsDraggingWidget,
		bool bNudge) override
	{
		if (bIsDraggingWidget && !bOffsetDragInProgress)
		{
			const TSharedPtr<SFrameEventPreviewCanvas> Canvas = OwnerCanvas.Pin();
			if (Canvas.IsValid() && Canvas->CanDragSelectedCueOffset())
			{
				bOffsetDragInProgress = true;
				Canvas->HandleOffsetDragStarted();
			}
		}
		FEditorViewportClient::TrackingStarted(InInputState, bIsDraggingWidget, bNudge);
	}

	virtual void TrackingStopped() override
	{
		if (bOffsetDragInProgress)
		{
			bOffsetDragInProgress = false;
			if (const TSharedPtr<SFrameEventPreviewCanvas> Canvas = OwnerCanvas.Pin())
			{
				Canvas->HandleOffsetDragEnded();
			}
		}
		FEditorViewportClient::TrackingStopped();
	}

	virtual bool InputWidgetDelta(
		FViewport* InViewport,
		EAxisList::Type CurrentAxis,
		FVector& Drag,
		FRotator& Rot,
		FVector& Scale) override
	{
		if (bOffsetDragInProgress && CurrentAxis != EAxisList::None)
		{
			if (!Drag.IsNearlyZero())
			{
				if (const TSharedPtr<SFrameEventPreviewCanvas> Canvas = OwnerCanvas.Pin())
				{
					Canvas->HandleOffsetDragDelta(Drag);
				}
			}
			return true;
		}
		return FEditorViewportClient::InputWidgetDelta(
			InViewport, CurrentAxis, Drag, Rot, Scale);
	}

	/** Canvas-owned cancel already fired the end delegate; only the tracking flag needs clearing. */
	void CancelOffsetTracking()
	{
		bOffsetDragInProgress = false;
	}

	virtual void ReceivedFocus(FViewport* InViewport) override
	{
		FEditorViewportClient::ReceivedFocus(InViewport);
		SetAudioFocus();
	}

	virtual void LostFocus(FViewport* InViewport) override
	{
		ClearAudioFocus();
		FEditorViewportClient::LostFocus(InViewport);
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	virtual bool InputKey(
		FViewport* InViewport,
		int32 ControllerId,
		FKey Key,
		EInputEvent Event,
		float AmountDepressed = 1.0f,
		bool bGamepad = false) override
	{
		if (Event == IE_Pressed && Key == EKeys::F)
		{
			FocusOnSubject(true);
			return true;
		}
		return FEditorViewportClient::InputKey(
			InViewport, ControllerId, Key, Event, AmountDepressed, bGamepad);
	}
#else
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override
	{
		if (EventArgs.Event == IE_Pressed && EventArgs.Key == EKeys::F)
		{
			FocusOnSubject(true);
			return true;
		}
		return FEditorViewportClient::InputKey(EventArgs);
	}
#endif

	void FocusOnSubject(bool bForce)
	{
		UPaperFlipbookComponent* Component =
			PreviewHost ? PreviewHost->GetFlipbookComponent() : nullptr;
		UPaperFlipbook* CurrentFlipbook = Component ? Component->GetFlipbook() : nullptr;
		if (!Component || !CurrentFlipbook)
		{
			return;
		}
		const FBox Bounds = PreviewHost->GetSubjectVisualBounds();
		const FIntPoint ViewportSize = Viewport
			? Viewport->GetSizeXY()
			: FIntPoint::ZeroValue;
		const bool bSameSubjectAndSize =
			FocusedFlipbook.Get() == CurrentFlipbook
			&& FocusedViewportSize == ViewportSize;
		const bool bAlreadyContained =
			FocusedBounds.IsValid
			&& Bounds.IsValid
			&& FocusedBounds.IsInsideOrOn(Bounds.Min)
			&& FocusedBounds.IsInsideOrOn(Bounds.Max);
		if (!bForce
			&& bSameSubjectAndSize
			&& bAlreadyContained)
		{
			return;
		}
		if (Bounds.IsValid)
		{
			// Trimmed frames and Layer sprites can change bounds from frame to frame. Keep the largest
			// bounds seen for this subject/viewport so ordinary playback never pumps the camera or
			// discards a designer's manual pan. A newly exposed extreme may expand the framing, but
			// normal frame changes never shrink or recenter it. F explicitly resets to the current frame.
			FBox BoundsToFocus = Bounds;
			if (!bForce && bSameSubjectAndSize && FocusedBounds.IsValid)
			{
				BoundsToFocus += FocusedBounds;
			}
			// FocusViewportOnBox writes an absolute camera transform. Remove the prior shake offset,
			// focus, then reapply the current aggregate so resize/refocus never loses or doubles it.
			SetViewLocation(GetViewLocation() - AppliedCameraOffset);
			AppliedCameraOffset = FVector::ZeroVector;
			FocusViewportOnBox(BoundsToFocus, /*bInstant=*/true);
			ApplyCameraOffset();
			FocusedFlipbook = CurrentFlipbook;
			FocusedViewportSize = ViewportSize;
			FocusedBounds = BoundsToFocus;
		}
	}

	void SetHostPreviewScene(FPreviewScene* InPreviewScene)
	{
		// FEditorViewportClient keeps an externally owned raw pointer. The host publishes nullptr
		// before final destruction. Routine seeks preserve the FPreviewScene owner and republish the
		// same pointer after replacing only its UWorld, so retain the camera cache in that case.
		const bool bOwnerChanged = PreviewScene != InPreviewScene;
		PreviewScene = InPreviewScene;
		if (bOwnerChanged)
		{
			SetViewLocation(GetViewLocation() - AppliedCameraOffset);
			AppliedCameraOffset = FVector::ZeroVector;
			InvalidateSubjectFocus();
		}
		if (Viewport)
		{
			Viewport->Invalidate();
		}
	}

	bool IsStandardGridEnabled() const
	{
		return DrawHelper.bDrawGrid && EngineShowFlags.Grid;
	}

	// Names every disabled designer-visual flag so a cross-version regression identifies itself
	// (the 5.0-5.3 ortho PostProcessing clamp cost a full gate cycle to name blind).
	FString GetDisabledDesignerCueVisuals() const
	{
		TArray<FString> Disabled;
		if (!EngineShowFlags.CompositeEditorPrimitives)
		{
			Disabled.Add(TEXT("CompositeEditorPrimitives"));
		}
		if (!EngineShowFlags.Paper2DSprites)
		{
			Disabled.Add(TEXT("Paper2DSprites"));
		}
		if (!EngineShowFlags.BillboardSprites)
		{
			Disabled.Add(TEXT("BillboardSprites"));
		}
		if (!EngineShowFlags.Particles)
		{
			Disabled.Add(TEXT("Particles"));
		}
		if (!EngineShowFlags.Translucency)
		{
			Disabled.Add(TEXT("Translucency"));
		}
		if (!EngineShowFlags.SeparateTranslucency)
		{
			Disabled.Add(TEXT("SeparateTranslucency"));
		}
		if (!EngineShowFlags.PostProcessing)
		{
			Disabled.Add(TEXT("PostProcessing"));
		}
		if (!EngineShowFlags.PostProcessMaterial)
		{
			Disabled.Add(TEXT("PostProcessMaterial"));
		}
		return FString::Join(Disabled, TEXT(", "));
	}

	bool AreDesignerCueVisualsEnabled() const
	{
		return GetDisabledDesignerCueVisuals().IsEmpty();
	}

	void InvalidateSubjectFocus()
	{
		// Do not focus synchronously: source/visibility delegates fire before the next canvas Tick has
		// synchronized the replacement Layer components. Clearing the cache makes that post-sync Tick
		// perform the one authoritative refocus against current bounds.
		FocusedFlipbook.Reset();
		FocusedViewportSize = FIntPoint::ZeroValue;
		FocusedBounds = FBox(ForceInit);
	}

private:
	void ApplyCameraOffset()
	{
		const UPaper2DPlusFrameCuePreviewContext* Context =
			PreviewHost ? PreviewHost->GetContext() : nullptr;
		const FVector2D Offset2D =
			Context ? Context->GetCameraOffset() : FVector2D::ZeroVector;
		// Adapter camera-shake units are intentionally viewport/world approximation units: +X right,
		// +Y up in the Paper2D X/Z plane.
		const FVector DesiredOffset(Offset2D.X, 0.0f, Offset2D.Y);
		SetViewLocation(GetViewLocation() + DesiredOffset - AppliedCameraOffset);
		AppliedCameraOffset = DesiredOffset;
	}

	FPaper2DPlusFrameCuePreviewHost* PreviewHost = nullptr;
	TWeakPtr<SFrameEventPreviewCanvas> OwnerCanvas;
	bool bOffsetDragInProgress = false;
	TWeakObjectPtr<UPaperFlipbook> FocusedFlipbook;
	FIntPoint FocusedViewportSize = FIntPoint::ZeroValue;
	FBox FocusedBounds = FBox(ForceInit);
	FVector AppliedCameraOffset = FVector::ZeroVector;
};

void SFrameEventPreviewCanvas::Construct(const FArguments& InArgs)
{
	Flipbook = InArgs._Flipbook;
	FrameIndex = InArgs._FrameIndex;
	Asset = InArgs._Asset;
	LayerAsset = InArgs._LayerAsset;
	ModelWeak = InArgs._Model;
	CueSource = InArgs._Cues;
	FlipbookIndex = InArgs._FlipbookIndex;
	SelectedEventIndex = InArgs._SelectedEventIndex;
	PreviewHost = InArgs._PreviewHost;
	OnCueOffsetDragStartedDelegate = InArgs._OnCueOffsetDragStarted;
	OnCueOffsetChangedDelegate = InArgs._OnCueOffsetChanged;
	OnCueOffsetDragEndedDelegate = InArgs._OnCueOffsetDragEnded;
	SEditorViewport::Construct(SEditorViewport::FArguments());
	SetClipping(EWidgetClipping::ClipToBounds);
}

SFrameEventPreviewCanvas::~SFrameEventPreviewCanvas()
{
	if (PreviewHost)
	{
		PreviewHost->SetPreviewSceneChanged(
			TFunction<void(FPreviewScene*)>());
	}
	if (PreviewViewportClient.IsValid())
	{
		PreviewViewportClient->Viewport = nullptr;
		PreviewViewportClient.Reset();
	}
	PreviewHost = nullptr;
}

/**
 * What this viewport is showing, in words, for the selected placement.
 */
FText SFrameEventPreviewCanvas::GetPreviewStatusText(
	const UPaper2DPlusCueBase* SelectedCue,
	const UPaper2DPlusFrameCuePreviewContext* Context,
	int32 HostResourceCount)
{
	if (!SelectedCue)
	{
		return FText::GetEmpty();
	}

	const int32 ResourceCount = HostResourceCount != INDEX_NONE
		? HostResourceCount
		: (Context ? Context->GetOwnedResourceCount() : 0);
	if (ResourceCount > 0)
	{
		return FText::Format(
			LOCTEXT("ActivePreviewStatus", "Preview active: {0} isolated resource(s)."),
			FText::AsNumber(ResourceCount));
	}
	return LOCTEXT(
		"ReadyPreviewStatus",
		"Automatic isolated preview.");
}

FText SFrameEventPreviewCanvas::GetAccessibleSummaryText() const
{
	const UPaper2DPlusCueBase* SelectedCue = ResolveSelectedCue();
	if (!SelectedCue)
	{
		return LOCTEXT("PreviewCanvasAccessibleEmpty", "Frame Cue preview canvas. No Cue selected.");
	}
	return FText::Format(
		LOCTEXT("PreviewCanvasAccessible", "Frame Cue preview canvas for {0}. {1}"),
		SelectedCue->GetClass()->GetDisplayNameText(),
		GetPreviewStatusText(
			SelectedCue,
			PreviewHost ? PreviewHost->GetContext() : nullptr,
			PreviewHost ? PreviewHost->GetOwnedResourceCount() : INDEX_NONE));
}

FPreviewScene* SFrameEventPreviewCanvas::GetViewportPreviewSceneForTests() const
{
	return PreviewViewportClient.IsValid()
		? PreviewViewportClient->GetPreviewScene()
		: nullptr;
}

bool SFrameEventPreviewCanvas::IsStandardGridEnabledForTests() const
{
	return PreviewViewportClient.IsValid()
		&& PreviewViewportClient->IsStandardGridEnabled();
}

bool SFrameEventPreviewCanvas::AreDesignerCueVisualsEnabledForTests() const
{
	return PreviewViewportClient.IsValid()
		&& PreviewViewportClient->AreDesignerCueVisualsEnabled();
}

FString SFrameEventPreviewCanvas::GetDisabledDesignerCueVisualsForTests() const
{
	if (!PreviewViewportClient.IsValid())
	{
		return TEXT("NoViewportClient");
	}
	return PreviewViewportClient->GetDisabledDesignerCueVisuals();
}

void SFrameEventPreviewCanvas::TickViewportForTests(float DeltaSeconds)
{
	if (PreviewViewportClient.IsValid())
	{
		PreviewViewportClient->Tick(DeltaSeconds);
	}
}

void SFrameEventPreviewCanvas::ResetCachedGeometry()
{
	++LayerSourceRevision;
	if (PreviewViewportClient.IsValid())
	{
		PreviewViewportClient->InvalidateSubjectFocus();
	}
}

FVector2D SFrameEventPreviewCanvas::ComputeDesiredSize(float) const
{
	return FVector2D(256, 256);
}

TSharedRef<FEditorViewportClient> SFrameEventPreviewCanvas::MakeEditorViewportClient()
{
	PreviewViewportClient = MakeShareable(new FFrameEventPreviewViewportClient(
		PreviewHost, SharedThis(this)));
	if (PreviewHost)
	{
		const TWeakPtr<FFrameEventPreviewViewportClient> WeakViewportClient =
			PreviewViewportClient;
		PreviewHost->SetPreviewSceneChanged(
			[WeakViewportClient](FPreviewScene* PreviewScene)
			{
				if (const TSharedPtr<FFrameEventPreviewViewportClient> Client =
					WeakViewportClient.Pin())
				{
					Client->SetHostPreviewScene(PreviewScene);
				}
			});
	}
	return PreviewViewportClient.ToSharedRef();
}

void SFrameEventPreviewCanvas::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SEditorViewport::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (PreviewHost)
	{
		UPaperFlipbook* CurrentFlipbook = Flipbook.Get(nullptr);
		UPaper2DPlusCharacterProfileAsset* AssetPtr =
			Asset.Get(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>()).Get();
		const int32 CurrentFrame = FrameIndex.Get(INDEX_NONE);
		PreviewHost->SyncSubject(
			AssetPtr,
			CurrentFlipbook,
			CurrentFrame,
			ResolveAnimationName());

		UPaper2DPlusCharacterLayerAsset* Layers = LayerAsset.Get();
		const int32 CurrentFlipbookIndex = FlipbookIndex.Get(INDEX_NONE);
		const uint32 PreviewWorldRevision = PreviewHost->GetPreviewWorldRevision();
		const uint32 PreviewSubjectRevision = PreviewHost->GetPreviewSubjectRevision();
		const bool bLayerSyncRequired =
			CachedLayerProfile.Get() != AssetPtr
			|| CachedLayerAsset.Get() != Layers
			|| CachedLayerFlipbookIndex != CurrentFlipbookIndex
			|| CachedLayerFrameIndex != CurrentFrame
			|| CachedLayerSourceRevision != LayerSourceRevision
			|| CachedPreviewWorldRevision != PreviewWorldRevision
			|| CachedPreviewSubjectRevision != PreviewSubjectRevision;
		if (bLayerSyncRequired)
		{
			TArray<FPaper2DPlusPreviewLayerSprite> PreviewLayers;
			if (Layers
				&& AssetPtr
				&& AssetPtr->Flipbooks.IsValidIndex(CurrentFlipbookIndex)
				&& CurrentFrame != INDEX_NONE)
			{
				const FFlipbookProfileEntry& AnimData =
					AssetPtr->Flipbooks[CurrentFlipbookIndex];
				const TSharedPtr<FCharacterProfileEditorModel> Model = ModelWeak.Pin();
				FPaper2DPlusAppearanceDescriptor DefaultAppearance;
				Paper2DPlusAppearanceResolver::BuildDefaultDescriptor(
					Layers, DefaultAppearance);
				TArray<FString> VisibleLayerNames;
				for (const FCharacterLayer& Layer : Layers->Layers)
				{
					const bool bVisible = Model.IsValid()
						? Model->IsLayerVisible(Layer.LayerName)
						: DefaultAppearance.ActiveLayerIds.Contains(Layer.LayerId);
					if (bVisible)
					{
						VisibleLayerNames.Add(Layer.LayerName);
					}
				}
				Layers->SortVisibleLayersForEffectivePaintOrder(VisibleLayerNames);
				for (const FString& LayerName : VisibleLayerNames)
				{
					const FCharacterLayer* Layer = Layers->GetLayerByName(LayerName);
					UPaperSprite* LayerSprite = Layer
						? Layer->GetSpriteForFrame(
							AnimData.Identity.FlipbookName,
							CurrentFrame,
							/*bAllowSyncLoad=*/false)
						: nullptr;
					if (!LayerSprite)
					{
						continue;
					}
					FPaper2DPlusPreviewLayerSprite& PreviewLayer =
						PreviewLayers.AddDefaulted_GetRef();
					PreviewLayer.Sprite = LayerSprite;
					PreviewLayer.TotalOffsetPx =
						Paper2DPlusLayerDraw::ResolveTotalOffsetPx(
							&AnimData,
							CurrentFrame,
							Layer,
							AnimData.Identity.FlipbookName);
				}
			}
			PreviewHost->SyncLayerSprites(PreviewLayers);
			CachedLayerProfile = AssetPtr;
			CachedLayerAsset = Layers;
			CachedLayerFlipbookIndex = CurrentFlipbookIndex;
			CachedLayerFrameIndex = CurrentFrame;
			CachedLayerSourceRevision = LayerSourceRevision;
			CachedPreviewWorldRevision = PreviewWorldRevision;
			CachedPreviewSubjectRevision = PreviewSubjectRevision;
		}
		PreviewHost->SyncAdapterVisuals();
	}
	// Never auto-frame while the viewport client is tracking a gesture (offset-gizmo drag or a
	// camera pan): a mid-gesture camera write would fight the pointer math.
	if (PreviewViewportClient.IsValid() && !PreviewViewportClient->IsTracking())
	{
		PreviewViewportClient->FocusOnSubject(false);
	}
}

int32 SFrameEventPreviewCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = SEditorViewport::OnPaint(
		Args,
		AllottedGeometry,
		MyCullingRect,
		OutDrawElements,
		LayerId,
		InWidgetStyle,
		bParentEnabled);
	++LayerId;
	const FVector2D GeomSize = AllottedGeometry.GetLocalSize();

	const UPaper2DPlusFrameCuePreviewContext* Context =
		PreviewHost ? PreviewHost->GetContext() : nullptr;

	// Spatial adapter proxies render in the preview world with ordinary Cue output. Only the genuinely
	// screen-space color overlay remains in Slate.
	if (Context)
	{
		FLinearColor Overlay;
		if (Context->GetOverlay(Overlay))
		{
			const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId++,
				AllottedGeometry.ToPaintGeometry(),
				WhiteBrush,
				ESlateDrawEffect::None,
				Overlay);
		}
	}

	const UPaper2DPlusCueBase* SelectedCue = ResolveSelectedCue();

	const FText StatusText = GetPreviewStatusText(
		SelectedCue,
		Context,
		PreviewHost ? PreviewHost->GetOwnedResourceCount() : INDEX_NONE);
	if (!StatusText.IsEmpty())
	{
		const FSlateBrush* WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");
		const FVector2D StatusPosition(6.0f, FMath::Max(4.0f, GeomSize.Y - 25.0f));
		const FVector2D StatusSize(FMath::Max(1.0f, GeomSize.X - 12.0f), 20.0f);
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId++,
			MakePaintGeometry(AllottedGeometry, StatusSize, FSlateLayoutTransform(StatusPosition)),
			WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.025f, 0.025f, 0.025f, 0.86f));
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId++,
			MakePaintGeometry(AllottedGeometry, StatusSize - FVector2D(8.0f, 0.0f),
				FSlateLayoutTransform(StatusPosition + FVector2D(4.0f, 2.0f))),
			StatusText.ToString(),
			FCoreStyle::GetDefaultFontStyle("Regular", 8),
			ESlateDrawEffect::None,
			FLinearColor(0.88f, 0.88f, 0.88f));
	}

	return LayerId;
}

const UPaper2DPlusCueBase* SFrameEventPreviewCanvas::ResolveSelectedCue() const
{
	const int32 SelectedCueIndex = SelectedEventIndex.Get(INDEX_NONE);
	if (const TArray<TObjectPtr<UPaper2DPlusCueBase>>* ProviderCues =
		CueSource.Get(static_cast<const TArray<TObjectPtr<UPaper2DPlusCueBase>>*>(nullptr)))
	{
		return ProviderCues->IsValidIndex(SelectedCueIndex)
			? (*ProviderCues)[SelectedCueIndex].Get()
			: nullptr;
	}

	UPaper2DPlusCharacterProfileAsset* AssetPtr =
		Asset.Get(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>()).Get();
	const int32 SelectedFlipbook = FlipbookIndex.Get(INDEX_NONE);
	if (!AssetPtr || !AssetPtr->Flipbooks.IsValidIndex(SelectedFlipbook))
	{
		return nullptr;
	}
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues =
		AssetPtr->Flipbooks[SelectedFlipbook].FrameEventData.FrameCues;
	return Cues.IsValidIndex(SelectedCueIndex) ? Cues[SelectedCueIndex].Get() : nullptr;
}

FName SFrameEventPreviewCanvas::ResolveAnimationName() const
{
	UPaper2DPlusCharacterProfileAsset* AssetPtr =
		Asset.Get(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>()).Get();
	const int32 CurrentFlipbookIndex = FlipbookIndex.Get(INDEX_NONE);
	if (AssetPtr && AssetPtr->Flipbooks.IsValidIndex(CurrentFlipbookIndex))
	{
		return FName(*AssetPtr->Flipbooks[CurrentFlipbookIndex].Identity.FlipbookName);
	}
	return NAME_None;
}

UPaper2DPlusSpawnFlipbookCue* SFrameEventPreviewCanvas::ResolveDraggableSpawnCue() const
{
	// The one deliberate class test in this viewport: v8 ships exactly one native cue, and its
	// draggable offset is that class's unique editor affordance. The cast is written against the
	// live selected placement, so a Blueprint subclass of the native cue also qualifies.
	return const_cast<UPaper2DPlusSpawnFlipbookCue*>(
		Cast<const UPaper2DPlusSpawnFlipbookCue>(ResolveSelectedCue()));
}

bool SFrameEventPreviewCanvas::CanDragSelectedCueOffset() const
{
	return ResolveDraggableSpawnCue() != nullptr
		&& PreviewHost
		&& PreviewHost->GetFlipbookComponent();
}

bool SFrameEventPreviewCanvas::TryResolveGizmoBasis(
	const UPaper2DPlusSpawnFlipbookCue& Cue,
	FVector& OutAnchorLocation,
	float& OutAbsScaleX,
	float& OutAbsScaleZ,
	bool& bOutFacingLeft) const
{
	const UPaperFlipbookComponent* Subject =
		PreviewHost ? PreviewHost->GetFlipbookComponent() : nullptr;
	if (!Subject)
	{
		return false;
	}
	const FVector SubjectScale = Subject->GetComponentScale();
	OutAnchorLocation = Subject->GetComponentLocation();
	OutAbsScaleX = FMath::Abs(SubjectScale.X);
	OutAbsScaleZ = FMath::Abs(SubjectScale.Z);
	bOutFacingLeft = UPaper2DPlusCharacterProfileComponent::ResolveFacingLeft(
		SubjectScale,
		Subject->GetComponentRotation().Yaw);

	if (Cue.Anchor == EPaper2DPlusFrameCueAnchorKind::ProfileSocket)
	{
		// Run the REAL anchor resolver against a locally built preview context so a
		// socket-anchored gizmo sits where the effect will actually spawn. Mid-authoring
		// failures (no socket yet, dangling name) keep the render-origin basis above so the
		// gizmo never vanishes while the designer is still typing the socket name.
		AActor* PreviewActor = PreviewHost->GetPreviewActor();
		FPaper2DPlusFrameCueContext AnchorContext;
		AnchorContext.OwningActor = PreviewActor;
		AnchorContext.ProfileComponent = PreviewActor
			? PreviewActor->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>()
			: nullptr;
		AnchorContext.Flipbook = Flipbook.Get(nullptr);
		AnchorContext.AnimationName = ResolveAnimationName();
		AnchorContext.CurrentFrame = FrameIndex.Get(0);
		AnchorContext.CharacterProfile =
			Asset.Get(TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset>()).Get();
		AnchorContext.PlaybackComponent = const_cast<UPaperFlipbookComponent*>(Subject);
		AnchorContext.SetEvaluationMode(
			EPaper2DPlusFrameCueEvaluationMode::EditorSelectionPreview);

		FTransform SocketTransform;
		UPaperFlipbookComponent* AttachmentComponent = nullptr;
		if (UPaper2DPlusBlueprintLibrary::ResolveFrameCueAnchor(
				AnchorContext,
				EPaper2DPlusFrameCueAnchorKind::ProfileSocket,
				Cue.ProfileSocketName,
				SocketTransform,
				AttachmentComponent)
			== EPaper2DPlusFrameCueAnchorResult::Success)
		{
			OutAnchorLocation = SocketTransform.GetLocation();
		}
	}
	return true;
}

bool SFrameEventPreviewCanvas::TryGetSelectedCueEffectTransform(FTransform& OutTransform) const
{
	const UPaper2DPlusSpawnFlipbookCue* Cue = ResolveDraggableSpawnCue();
	FVector AnchorLocation;
	float AbsScaleX = 1.0f;
	float AbsScaleZ = 1.0f;
	bool bFacingLeft = false;
	if (!Cue
		|| !TryResolveGizmoBasis(*Cue, AnchorLocation, AbsScaleX, AbsScaleZ, bFacingLeft))
	{
		return false;
	}
	OutTransform = UPaper2DPlusSpawnFlipbookCue::ComputeEffectWorldTransform(
		AnchorLocation,
		AbsScaleX,
		AbsScaleZ,
		bFacingLeft,
		Cue->Offset,
		Cue->Rotation,
		Cue->Scale,
		Cue->bFlipWithCharacter);
	return true;
}

FVector SFrameEventPreviewCanvas::GetSelectedCueOffsetWorldLocation() const
{
	FTransform EffectTransform;
	return TryGetSelectedCueEffectTransform(EffectTransform)
		? EffectTransform.GetLocation()
		: FVector::ZeroVector;
}

void SFrameEventPreviewCanvas::HandleOffsetDragStarted()
{
	if (bOffsetDragLive)
	{
		return;
	}
	UPaper2DPlusSpawnFlipbookCue* Cue = ResolveDraggableSpawnCue();
	if (!Cue)
	{
		return;
	}
	bOffsetDragLive = true;
	OffsetDragAccumulator = Cue->Offset;
	OnCueOffsetDragStartedDelegate.ExecuteIfBound(Cue);
}

void SFrameEventPreviewCanvas::HandleOffsetDragDelta(const FVector& WorldDelta)
{
	const UPaper2DPlusSpawnFlipbookCue* Cue = ResolveDraggableSpawnCue();
	FVector AnchorLocation;
	float AbsScaleX = 1.0f;
	float AbsScaleZ = 1.0f;
	bool bFacingLeft = false;
	if (!bOffsetDragLive
		|| !Cue
		|| !TryResolveGizmoBasis(*Cue, AnchorLocation, AbsScaleX, AbsScaleZ, bFacingLeft))
	{
		return;
	}
	// The world drag runs through the SAME mirror/scale mapping the spawn math applies, inverted:
	// dragging the gizmo left always moves the effect left on screen, whichever way the character
	// faces, and the authored offset stays in character-local units.
	const float MirrorSign =
		(Cue->bFlipWithCharacter && bFacingLeft) ? -1.0f : 1.0f;
	OffsetDragAccumulator.X +=
		MirrorSign * WorldDelta.X / FMath::Max(AbsScaleX, KINDA_SMALL_NUMBER);
	OffsetDragAccumulator.Y +=
		WorldDelta.Z / FMath::Max(AbsScaleZ, KINDA_SMALL_NUMBER);
	OnCueOffsetChangedDelegate.ExecuteIfBound(OffsetDragAccumulator);
}

void SFrameEventPreviewCanvas::HandleOffsetDragEnded()
{
	if (!bOffsetDragLive)
	{
		return;
	}
	bOffsetDragLive = false;
	OnCueOffsetDragEndedDelegate.ExecuteIfBound();
}

void SFrameEventPreviewCanvas::CancelActiveInteraction()
{
	if (!bOffsetDragLive)
	{
		return;
	}
	// Reset the flag BEFORE any delegate fires so a re-entrant settle (capture loss, host
	// deactivation racing mouse-up) is a no-op and the End delegate fires exactly once.
	bOffsetDragLive = false;
	if (PreviewViewportClient.IsValid())
	{
		PreviewViewportClient->CancelOffsetTracking();
	}
	OnCueOffsetDragEndedDelegate.ExecuteIfBound();
}

void SFrameEventPreviewCanvas::BeginOffsetInteractionForTests()
{
	HandleOffsetDragStarted();
}

#undef LOCTEXT_NAMESPACE
