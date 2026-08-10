// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusLayerBakeTypes.h"

#if WITH_EDITOR

#include "Paper2DPlusCharacterLayerAsset.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "Misc/Crc.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

namespace
{
	class FLayerDigestBuilder
	{
	public:
		void AddString(const FString& Value)
		{
			FTCHARToUTF8 Utf8(*Value);
			AddUInt32(static_cast<uint32>(Utf8.Length()));
			if (Utf8.Length() > 0)
			{
				Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			}
		}

		void AddBool(bool bValue) { Bytes.Add(bValue ? 1 : 0); }

		void AddInt32(int32 Value)
		{
			AddUInt32(static_cast<uint32>(Value));
		}

		void AddUInt32(uint32 Value)
		{
			Bytes.Add(static_cast<uint8>(Value & 0xff));
			Bytes.Add(static_cast<uint8>((Value >> 8) & 0xff));
			Bytes.Add(static_cast<uint8>((Value >> 16) & 0xff));
			Bytes.Add(static_cast<uint8>((Value >> 24) & 0xff));
		}

		void AddFloat(float Value)
		{
			uint32 Bits = 0;
			static_assert(sizeof(Bits) == sizeof(Value), "Unexpected float size");
			FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
			AddUInt32(Bits);
		}

		void AddGuid(const FGuid& Value)
		{
			AddUInt32(Value.A);
			AddUInt32(Value.B);
			AddUInt32(Value.C);
			AddUInt32(Value.D);
		}

		void AddVector2D(const FVector2D& Value)
		{
			AddFloat(static_cast<float>(Value.X));
			AddFloat(static_cast<float>(Value.Y));
		}

		template <typename StructType>
		void AddStruct(const StructType& Value)
		{
			TArray<uint8> Serialized;
			FMemoryWriter Writer(Serialized, true);
			FObjectAndNameAsStringProxyArchive Archive(Writer, false);
			Archive.ArNoDelta = true;
			StructType::StaticStruct()->SerializeItem(Archive, const_cast<StructType*>(&Value), nullptr);
			AddUInt32(static_cast<uint32>(Serialized.Num()));
			Bytes.Append(Serialized);
		}

		void AddCue(const UPaper2DPlusCueBase* Cue)
		{
			if (!Cue)
			{
				AddBool(false);
				return;
			}

			AddBool(true);
			AddString(Cue->GetClass()->GetPathName());
			TArray<uint8> Serialized;
			FMemoryWriter Writer(Serialized, true);
			FObjectAndNameAsStringProxyArchive Archive(Writer, false);
			Archive.ArNoDelta = true;
			const_cast<UPaper2DPlusCueBase*>(Cue)->Serialize(Archive);
			AddUInt32(static_cast<uint32>(Serialized.Num()));
			Bytes.Append(Serialized);
		}

		FString Finalize() const
		{
			FMD5 Md5;
			if (!Bytes.IsEmpty())
			{
				Md5.Update(Bytes.GetData(), Bytes.Num());
			}
			uint8 Digest[16];
			Md5.Final(Digest);
			return BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
		}

	private:
		TArray<uint8> Bytes;
	};
}

FString Paper2DPlusLayerBake::ComputeSourceDigest(const UPaper2DPlusCharacterLayerAsset& Asset)
{
	FLayerDigestBuilder Digest;
	Digest.AddString(TEXT("Paper2DPlus.LayerSource.v1"));
	Digest.AddString(Asset.BaseProfile.ToSoftObjectPath().ToString().ToLower());

#if WITH_EDITORONLY_DATA
	Digest.AddUInt32(static_cast<uint32>(Asset.Layers.Num()));
	for (const FCharacterLayer& Layer : Asset.Layers)
	{
		// Deliberately exclude LayerGroups and Layer.GroupId. Organization is never composition.
		Digest.AddGuid(Layer.LayerId);
		Digest.AddString(Layer.LayerName.ToLower());
		Digest.AddUInt32(static_cast<uint32>(Layer.CompositionMode));
		Digest.AddString(Layer.SourceTexture.ToSoftObjectPath().ToString().ToLower());
		Digest.AddVector2D(Layer.DefaultOffsetPx);

		Digest.AddUInt32(static_cast<uint32>(Layer.AnimationSprites.Num()));
		for (const FCharacterLayerAnimationMapping& Mapping : Layer.AnimationSprites)
		{
			Digest.AddString(Mapping.Flipbook.ToSoftObjectPath().ToString().ToLower());
			Digest.AddString(Mapping.AnimationName.ToLower());
			Digest.AddUInt32(static_cast<uint32>(Mapping.Sprites.Num()));
			for (const TSoftObjectPtr<UPaperSprite>& Sprite : Mapping.Sprites)
			{
				Digest.AddString(Sprite.ToSoftObjectPath().ToString().ToLower());
			}
		}

		Digest.AddUInt32(static_cast<uint32>(Layer.AnimationOffsets.Num()));
		for (const FCharacterLayerAnimationOffset& Offset : Layer.AnimationOffsets)
		{
			Digest.AddString(Offset.Flipbook.ToSoftObjectPath().ToString().ToLower());
			Digest.AddString(Offset.AnimationName.ToLower());
			Digest.AddVector2D(Offset.OffsetPx);
		}

		Digest.AddUInt32(static_cast<uint32>(Layer.AuthoredAnimations.Num()));
		for (const FCharacterLayerAuthoredAnimationData& Animation : Layer.AuthoredAnimations)
		{
			Digest.AddString(Animation.Flipbook.ToSoftObjectPath().ToString().ToLower());
			Digest.AddString(Animation.LegacyAnimationName.ToLower());
			Digest.AddUInt32(static_cast<uint32>(Animation.AttackMerge));
			Digest.AddUInt32(static_cast<uint32>(Animation.HurtMerge));
			Digest.AddUInt32(static_cast<uint32>(Animation.SocketMerge));
			Digest.AddUInt32(static_cast<uint32>(Animation.Frames.Num()));
			for (const FCharacterLayerAuthoredFrameData& Frame : Animation.Frames)
			{
				Digest.AddStruct(Frame);
			}
			Digest.AddUInt32(static_cast<uint32>(Animation.FrameCues.Num()));
			for (const UPaper2DPlusCueBase* Cue : Animation.FrameCues)
			{
				Digest.AddCue(Cue);
			}
		}
	}
#endif

	return Digest.Finalize();
}

FGuid Paper2DPlusLayerBake::MakeStableDecisionId(const FString& StableSourceKey)
{
	const FString Canonical = StableSourceKey.ToLower();
	return FGuid(
		FCrc::StrCrc32(*(Canonical + TEXT("|a"))),
		FCrc::StrCrc32(*(Canonical + TEXT("|b"))),
		FCrc::StrCrc32(*(Canonical + TEXT("|c"))),
		FCrc::StrCrc32(*(Canonical + TEXT("|d"))));
}

#endif // WITH_EDITOR
