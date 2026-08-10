// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusFrameGeometry.h"

namespace Paper2DPlusFrameGeometry
{
	namespace
	{
		void FrameGeometryGetScaledHitboxRect(
			const FHitboxData& Hitbox,
			bool bFacingLeft,
			float ScaleX,
			float ScaleY,
			float& OutX,
			float& OutZ,
			float& OutWidth,
			float& OutHeight)
		{
			OutX = static_cast<float>(Hitbox.X) * ScaleX;
			OutZ = static_cast<float>(Hitbox.Y) * ScaleY;
			OutWidth = static_cast<float>(Hitbox.Width) * ScaleX;
			OutHeight = static_cast<float>(Hitbox.Height) * ScaleY;
			if (bFacingLeft)
			{
				OutX = -(OutX + OutWidth);
			}
		}

		FVector FrameGeometryMakeWorldSocketLocation(
			float SocketX,
			float SocketY,
			const FVector& WorldOrigin,
			bool bFacingLeft,
			float ScaleX,
			float ScaleY)
		{
			float X = SocketX * ScaleX;
			if (bFacingLeft)
			{
				X = -X;
			}

			return FVector(
				WorldOrigin.X + X,
				WorldOrigin.Y,
				WorldOrigin.Z + SocketY * ScaleY);
		}
	}

	FVector2D ConvertFrameDataFromTopLeftToPivotSpace(
		FFrameHitboxData& InOutFrameData,
		const FVector2D& PivotLocal)
	{
		if (!FMath::IsFinite(PivotLocal.X) || !FMath::IsFinite(PivotLocal.Y))
		{
			return FVector2D::ZeroVector;
		}

		const int32 PivotX = FMath::FloorToInt(PivotLocal.X);
		const int32 PivotY = FMath::FloorToInt(PivotLocal.Y);
		for (FHitboxData& Hitbox : InOutFrameData.Hitboxes)
		{
			Hitbox.X -= PivotX;
			Hitbox.Y = PivotY - Hitbox.Y - Hitbox.Height;
		}
		for (FSocketData& Socket : InOutFrameData.Sockets)
		{
			Socket.X -= PivotX;
			Socket.Y = PivotY - Socket.Y;
		}

		return FVector2D(
			PivotLocal.X - static_cast<float>(PivotX),
			PivotLocal.Y - static_cast<float>(PivotY));
	}

	FVector ApplyPivotFractionToWorldOrigin(
		const FVector& WorldOrigin,
		const FVector2D& PivotFraction,
		bool bFacingLeft,
		float ScaleX,
		float ScaleY)
	{
		FVector Result = WorldOrigin;
		Result.X += (bFacingLeft ? PivotFraction.X : -PivotFraction.X) * ScaleX;
		Result.Z += PivotFraction.Y * ScaleY;
		return Result;
	}

	FWorldHitbox MakeWorldHitbox(
		const FHitboxData& Hitbox,
		const FVector& WorldOrigin,
		bool bFacingLeft,
		float ScaleX,
		float ScaleY,
		const FGameplayTag& MoveDefaultClashCategory,
		const FGameplayTag& FrameDefenseClass)
	{
		float X = 0.0f;
		float Z = 0.0f;
		float Width = 0.0f;
		float Height = 0.0f;
		FrameGeometryGetScaledHitboxRect(Hitbox, bFacingLeft, ScaleX, ScaleY, X, Z, Width, Height);

		FWorldHitbox Result;
		Result.Type = Hitbox.Type;
		Result.Center = FVector(
			WorldOrigin.X + X + Width * 0.5f,
			WorldOrigin.Y,
			WorldOrigin.Z + Z + Height * 0.5f);
		Result.Extents = FVector(Width * 0.5f, 2.0f, Height * 0.5f);
		Result.Damage = Hitbox.Damage;
		Result.Knockback = Hitbox.Knockback;
		Result.ClashCategory = Hitbox.GetResolvedClashCategory(MoveDefaultClashCategory);
		Result.DefenseClass = FrameDefenseClass;
		return Result;
	}

	FWorldSocket MakeWorldSocket(
		const FSocketData& Socket,
		const FVector& WorldOrigin,
		bool bFacingLeft,
		float ScaleX,
		float ScaleY)
	{
		FWorldSocket Result;
		Result.Name = Socket.Name;
		Result.Location = FrameGeometryMakeWorldSocketLocation(
			static_cast<float>(Socket.X),
			static_cast<float>(Socket.Y),
			WorldOrigin,
			bFacingLeft,
			ScaleX,
			ScaleY);
		return Result;
	}

	bool TryMakeWorldSocketLocationFromTopLeft(
		const FSocketData& Socket,
		const FVector2D& PivotLocal,
		const FVector& WorldOrigin,
		bool bFacingLeft,
		float ScaleX,
		float ScaleY,
		FVector& OutLocation)
	{
		OutLocation = FVector::ZeroVector;
		if (WorldOrigin.ContainsNaN()
			|| !FMath::IsFinite(PivotLocal.X)
			|| !FMath::IsFinite(PivotLocal.Y)
			|| !FMath::IsFinite(ScaleX)
			|| !FMath::IsFinite(ScaleY))
		{
			return false;
		}

		const int32 PivotX = FMath::FloorToInt(PivotLocal.X);
		const int32 PivotY = FMath::FloorToInt(PivotLocal.Y);
		const FVector2D PivotFraction(
			PivotLocal.X - static_cast<float>(PivotX),
			PivotLocal.Y - static_cast<float>(PivotY));
		const FVector FractionAdjustedOrigin = ApplyPivotFractionToWorldOrigin(
			WorldOrigin,
			PivotFraction,
			bFacingLeft,
			ScaleX,
			ScaleY);

		OutLocation = FrameGeometryMakeWorldSocketLocation(
			static_cast<float>(Socket.X) - static_cast<float>(PivotX),
			static_cast<float>(PivotY) - static_cast<float>(Socket.Y),
			FractionAdjustedOrigin,
			bFacingLeft,
			ScaleX,
			ScaleY);
		if (OutLocation.ContainsNaN())
		{
			OutLocation = FVector::ZeroVector;
			return false;
		}
		return true;
	}
}
