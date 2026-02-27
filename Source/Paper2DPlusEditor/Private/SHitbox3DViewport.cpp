// Copyright 2026 Infinite Gameworks. All Rights Reserved.

// SHitbox3DViewport.cpp - 3D viewport and client implementations
// Split from CharacterDataAssetEditor.cpp for maintainability

#include "CharacterDataAssetEditor.h"
#include "PaperSprite.h"
#include "PaperSpriteComponent.h"
#include "PaperFlipbook.h"
#include "Paper2DPlusCharacterDataAsset.h"
#include "UnrealClient.h"
#include "SceneView.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "CharacterDataAssetEditor"

// ==========================================
// FHitbox3DViewportClient Implementation
// ==========================================

FHitbox3DViewportClient::FHitbox3DViewportClient(FPreviewScene* InPreviewScene, const TSharedRef<SHitbox3DViewport>& InViewport)
	: FEditorViewportClient(nullptr, InPreviewScene, StaticCastSharedRef<SEditorViewport>(InViewport))
{
	// Front-facing camera looking at the sprite (in XZ plane facing -Y)
	SetViewLocation(FVector(50, -200, -50));
	SetViewRotation(FRotator(10, 90, 0));

	SetViewportType(LVT_Perspective);

	// Standard perspective controls (RMB rotate, MMB pan, scroll zoom)
	// Orbit mode disabled for more intuitive navigation
	bUsingOrbitCamera = false;

	// Enable built-in editor grid
	EngineShowFlags.SetGrid(true);

	SetRealtime(false);
}

FHitbox3DViewportClient::~FHitbox3DViewportClient()
{
}

void FHitbox3DViewportClient::SetHitboxData(const FFrameHitboxData* InFrameData)
{
	FrameDataCopy = InFrameData ? TOptional<FFrameHitboxData>(*InFrameData) : TOptional<FFrameHitboxData>();
	Invalidate();
}

FLinearColor FHitbox3DViewportClient::GetHitboxColor(EHitboxType Type) const
{
	switch (Type)
	{
		case EHitboxType::Attack: return FLinearColor(1.0f, 0.2f, 0.2f);
		case EHitboxType::Hurtbox: return FLinearColor(0.2f, 0.9f, 0.2f);
		case EHitboxType::Collision: return FLinearColor(0.3f, 0.5f, 1.0f);
		default: return FLinearColor::White;
	}
}

void FHitbox3DViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);

	if (!FrameDataCopy.IsSet()) return;

	const FFrameHitboxData& FrameData = FrameDataCopy.GetValue();

	// Draw hitboxes as 3D wireframe boxes
	// Coordinate mapping: HB.X -> WorldX, HB.Y -> -WorldZ (screen Y-down to world Z-up), HB.Z -> WorldY (depth)
	for (int32 i = 0; i < FrameData.Hitboxes.Num(); i++)
	{
		const FHitboxData& HB = FrameData.Hitboxes[i];
		FLinearColor Color = GetHitboxColor(HB.Type);

		bool bSelected = (i == SelectedHitboxIndex);
		if (bSelected)
		{
			Color = Color * 1.5f;
			Color.A = 1.0f;
		}

		float EffectiveDepth = (HB.Depth > 0) ? static_cast<float>(HB.Depth) : 20.0f;

		// Invert Y axis: screen Y-down -> world Z-up
		float WorldZMax = -static_cast<float>(HB.Y);
		float WorldZMin = -static_cast<float>(HB.Y + HB.Height);

		FVector Min(HB.X, HB.Z, WorldZMin);
		FVector Max(HB.X + HB.Width, HB.Z + EffectiveDepth, WorldZMax);

		float Thickness = bSelected ? 3.0f : 1.5f;
		DrawWireBox(PDI, FBox(Min, Max), Color, SDPG_Foreground, Thickness);

		if (bSelected)
		{
			float MarkerSize = 5.0f;
			PDI->DrawPoint(Min, Color, MarkerSize, SDPG_Foreground);
			PDI->DrawPoint(Max, Color, MarkerSize, SDPG_Foreground);
			PDI->DrawPoint(FVector(Min.X, Min.Y, Max.Z), Color, MarkerSize, SDPG_Foreground);
			PDI->DrawPoint(FVector(Max.X, Max.Y, Min.Z), Color, MarkerSize, SDPG_Foreground);
		}
	}

	// Draw sockets as 3D crosses
	for (int32 i = 0; i < FrameData.Sockets.Num(); i++)
	{
		const FSocketData& Sock = FrameData.Sockets[i];

		bool bSelected = (i == SelectedSocketIndex);
		FLinearColor SocketColor = bSelected ? FLinearColor::White : FLinearColor::Yellow;
		float CrossSize = 15.0f;
		float Thickness = bSelected ? 3.0f : 2.0f;

		FVector Pos(Sock.X, 0, -static_cast<float>(Sock.Y));

		PDI->DrawLine(Pos - FVector(CrossSize, 0, 0), Pos + FVector(CrossSize, 0, 0), SocketColor, SDPG_Foreground, Thickness);
		PDI->DrawLine(Pos - FVector(0, CrossSize, 0), Pos + FVector(0, CrossSize, 0), SocketColor, SDPG_Foreground, Thickness);
		PDI->DrawLine(Pos - FVector(0, 0, CrossSize), Pos + FVector(0, 0, CrossSize), SocketColor, SDPG_Foreground, Thickness);

		PDI->DrawPoint(Pos, SocketColor, 8.0f, SDPG_Foreground);
	}
}

void FHitbox3DViewportClient::DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);

	FString HelpText = TEXT("RMB + Drag: Rotate | MMB + Drag: Pan | Scroll: Zoom | F: Focus");
	int32 XPos = 10;
	int32 YPos = InViewport.GetSizeXY().Y - 25;

	Canvas.DrawShadowedString(XPos, YPos, *HelpText, GEngine->GetSmallFont(), FLinearColor(0.7f, 0.7f, 0.7f));
}

void FHitbox3DViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);
	Invalidate();
}

bool FHitbox3DViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	if (EventArgs.Event == IE_Pressed && EventArgs.Key == EKeys::F)
	{
		FocusOnHitboxes();
		return true;
	}

	return FEditorViewportClient::InputKey(EventArgs);
}

void FHitbox3DViewportClient::FocusOnHitboxes()
{
	if (!FrameDataCopy.IsSet() || FrameDataCopy.GetValue().Hitboxes.Num() == 0) return;

	const FFrameHitboxData& FrameData = FrameDataCopy.GetValue();

	FBox Bounds(EForceInit::ForceInit);
	for (const FHitboxData& HB : FrameData.Hitboxes)
	{
		float EffectiveDepth = (HB.Depth > 0) ? static_cast<float>(HB.Depth) : 20.0f;
		float WorldZMax = -static_cast<float>(HB.Y);
		float WorldZMin = -static_cast<float>(HB.Y + HB.Height);

		Bounds += FVector(HB.X, HB.Z, WorldZMin);
		Bounds += FVector(HB.X + HB.Width, HB.Z + EffectiveDepth, WorldZMax);
	}

	FVector Center = Bounds.GetCenter();
	float Radius = FMath::Max(Bounds.GetExtent().GetMax() * 2.5f, 50.0f);

	SetViewLocation(Center + FVector(0, -Radius, Radius * 0.2f));
	SetViewRotation(FRotator(-5, 90, 0));
}

// ==========================================
// SHitbox3DViewport Implementation
// ==========================================

void SHitbox3DViewport::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;

	PreviewScene = MakeShared<FPreviewScene>(FPreviewScene::ConstructionValues());

	// Create sprite component for displaying the current frame's sprite
	SpriteComponent = NewObject<UPaperSpriteComponent>();
	PreviewScene->AddComponent(SpriteComponent, FTransform::Identity);

	SEditorViewport::Construct(SEditorViewport::FArguments());
}

SHitbox3DViewport::~SHitbox3DViewport()
{
	if (SpriteComponent)
	{
		PreviewScene->RemoveComponent(SpriteComponent);
		SpriteComponent = nullptr;
	}

	if (ViewportClient.IsValid())
	{
		ViewportClient->Viewport = nullptr;
	}
}

TSharedRef<FEditorViewportClient> SHitbox3DViewport::MakeEditorViewportClient()
{
	ViewportClient = MakeShareable(new FHitbox3DViewportClient(PreviewScene.Get(), SharedThis(this)));
	return ViewportClient.ToSharedRef();
}

void SHitbox3DViewport::SetFrameData(const FFrameHitboxData* InFrameData)
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->SetHitboxData(InFrameData);
	}
}

void SHitbox3DViewport::SetSprite(UPaperSprite* InSprite)
{
	if (!SpriteComponent) return;

	SpriteComponent->SetSprite(InSprite);

	if (InSprite)
	{
		// Position and scale so the sprite's top-left pixel aligns with world (0, 0, 0)
		// and 1 pixel = 1 world unit (matching hitbox wireframe coordinates)
		FVector2D Pivot = InSprite->GetPivotPosition();
		float PPU = InSprite->GetPixelsPerUnrealUnit();
		if (PPU <= 0.0f) PPU = 1.0f;

		// Scale: sprite renders at 1/PPU world units per pixel, we want 1:1
		SpriteComponent->SetWorldScale3D(FVector(PPU, PPU, PPU));

		// Offset: place pivot so top-left pixel (0,0) lands at world (0, 0, 0)
		// After scaling, local (-PivotX/PPU) * PPU = -PivotX → need to shift by +PivotX
		// Similarly for Z: local (PivotY/PPU) * PPU = PivotY → need to shift by -PivotY
		SpriteComponent->SetWorldLocation(FVector(Pivot.X, 0.0f, -Pivot.Y));

		// Auto-focus camera on the sprite center on first display
		if (ViewportClient.IsValid())
		{
			FVector2D SrcSize = InSprite->GetSourceSize();
			FVector Center(SrcSize.X * 0.5f, 0.0f, -SrcSize.Y * 0.5f);
			float Radius = FMath::Max(FMath::Max(SrcSize.X, SrcSize.Y) * 1.5f, 100.0f);

			ViewportClient->SetViewLocation(Center + FVector(0, -Radius, Radius * 0.15f));
			ViewportClient->SetViewRotation(FRotator(-5, 90, 0));
		}
	}
}

void SHitbox3DViewport::SetSelectedHitbox(int32 Index)
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->SetSelectedHitbox(Index);
	}
}

void SHitbox3DViewport::SetSelectedSocket(int32 Index)
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->SetSelectedSocket(Index);
	}
}

void SHitbox3DViewport::RefreshViewport()
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->Invalidate();
	}
}

#undef LOCTEXT_NAMESPACE
