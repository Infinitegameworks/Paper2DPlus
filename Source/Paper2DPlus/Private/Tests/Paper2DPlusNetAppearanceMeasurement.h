// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Net/RepLayout.h"
#include "Paper2DPlusLayerRenderComponent.h"
#include "Paper2DPlusNetTypes.h"
#include "UObject/CoreNet.h"
#include "UObject/UnrealType.h"

/**
 * Test/profile-only measurement of the replicated RepAppearance property's value payload.
 *
 * This deliberately uses FRepLayout::SerializePropertiesForStruct with FNetBitWriter so every
 * reflected leaf follows the engine's replicated-property NetSerialize path on UE 5.0-5.8. The
 * result is the exact property VALUE payload for this struct. It is not a packet capture and does
 * not include RepLayout handles/change masks, actor/channel/bunch headers, reliability, packet
 * framing, packet-handler overhead, relevancy, retransmission, or per-connection fan-out.
 */
namespace Paper2DPlusNetAppearanceMeasurement
{
	struct FPayloadResult
	{
		int64 PayloadBits = 0;
		int64 PayloadBytesCeil = 0;
		bool bRoundTripMatched = false;
		bool bReplicatedPropertyVerified = false;
		bool bExcludesTierCachePixelState = false;
	};

	inline bool StateEquals(
		const FPaper2DPlusRepAppearanceState& A,
		const FPaper2DPlusRepAppearanceState& B)
	{
		return A.Sequence == B.Sequence
			&& A.PayloadVersion == B.PayloadVersion
			&& A.Appearance == B.Appearance;
	}

	inline bool NameIsPresentationOnly(const FString& Name)
	{
		return Name.Contains(TEXT("Tier"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Cache"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Pixel"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Texture"), ESearchCase::IgnoreCase);
	}

	inline bool PropertyTreeExcludesPresentationState(
		const FProperty* Property,
		const FString& Path,
		FString& OutError,
		TSet<const UStruct*>& VisitedStructs);

	inline bool StructExcludesPresentationState(
		const UStruct* Struct,
		const FString& Path,
		FString& OutError,
		TSet<const UStruct*>& VisitedStructs)
	{
		if (!Struct || VisitedStructs.Contains(Struct))
		{
			return Struct != nullptr;
		}
		VisitedStructs.Add(Struct);

		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (!PropertyTreeExcludesPresentationState(
				*It,
				Path + TEXT(".") + It->GetName(),
				OutError,
				VisitedStructs))
			{
				return false;
			}
		}
		return true;
	}

	inline bool PropertyTreeExcludesPresentationState(
		const FProperty* Property,
		const FString& Path,
		FString& OutError,
		TSet<const UStruct*>& VisitedStructs)
	{
		if (!Property)
		{
			OutError = FString::Printf(TEXT("Null reflected property at '%s'."), *Path);
			return false;
		}
		if (NameIsPresentationOnly(Property->GetName()))
		{
			OutError = FString::Printf(
				TEXT("RepAppearance contains presentation-only field '%s'."), *Path);
			return false;
		}
		if (CastField<FObjectPropertyBase>(Property))
		{
			OutError = FString::Printf(
				TEXT("RepAppearance contains object/resource field '%s'."), *Path);
			return false;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			return StructExcludesPresentationState(
				StructProperty->Struct, Path, OutError, VisitedStructs);
		}
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			return PropertyTreeExcludesPresentationState(
				ArrayProperty->Inner,
				Path + TEXT("[]"),
				OutError,
				VisitedStructs);
		}
		if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			return PropertyTreeExcludesPresentationState(
				SetProperty->ElementProp,
				Path + TEXT("{}"),
				OutError,
				VisitedStructs);
		}
		if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			return PropertyTreeExcludesPresentationState(
				MapProperty->KeyProp,
				Path + TEXT("{Key}"),
				OutError,
				VisitedStructs)
				&& PropertyTreeExcludesPresentationState(
					MapProperty->ValueProp,
					Path + TEXT("{Value}"),
					OutError,
					VisitedStructs);
		}
		return true;
	}

	inline bool VerifyReplicatedPropertyContract(FString& OutError)
	{
		const FStructProperty* ReplicatedProperty = FindFProperty<FStructProperty>(
			UPaper2DPlusLayerRenderComponent::StaticClass(), TEXT("RepAppearance"));
		if (!ReplicatedProperty)
		{
			OutError = TEXT("UPaper2DPlusLayerRenderComponent.RepAppearance is not a struct property.");
			return false;
		}
		if (!ReplicatedProperty->HasAnyPropertyFlags(CPF_Net))
		{
			OutError = TEXT("UPaper2DPlusLayerRenderComponent.RepAppearance is not marked replicated.");
			return false;
		}
		if (ReplicatedProperty->Struct != FPaper2DPlusRepAppearanceState::StaticStruct())
		{
			OutError = TEXT("RepAppearance no longer uses FPaper2DPlusRepAppearanceState.");
			return false;
		}
		return true;
	}

	inline bool VerifyNoPresentationState(FString& OutError)
	{
		TSet<const UStruct*> VisitedStructs;
		return StructExcludesPresentationState(
			FPaper2DPlusRepAppearanceState::StaticStruct(),
			TEXT("FPaper2DPlusRepAppearanceState"),
			OutError,
			VisitedStructs);
	}

	inline bool Measure(
		const FPaper2DPlusRepAppearanceState& State,
		FPayloadResult& OutResult,
		FString& OutError)
	{
		OutResult = FPayloadResult();
		OutResult.bReplicatedPropertyVerified = VerifyReplicatedPropertyContract(OutError);
		if (!OutResult.bReplicatedPropertyVerified)
		{
			return false;
		}
		OutResult.bExcludesTierCachePixelState = VerifyNoPresentationState(OutError);
		if (!OutResult.bExcludesTierCachePixelState)
		{
			return false;
		}

		UScriptStruct* AppearanceStruct = FPaper2DPlusRepAppearanceState::StaticStruct();
		const TSharedPtr<FRepLayout> RepLayout = FRepLayout::CreateFromStruct(AppearanceStruct);
		if (!RepLayout.IsValid())
		{
			OutError = TEXT("Failed to create the RepLayout for FPaper2DPlusRepAppearanceState.");
			return false;
		}

		// Ample for the bounded 4/8/12-layer profile fixtures while still failing closed.
		constexpr int64 MaximumPayloadBits = 8ll * 1024ll * 1024ll;
		FPaper2DPlusRepAppearanceState MutableState = State;
		FNetBitWriter Writer(nullptr, MaximumPayloadBits);
		bool bHasUnmapped = false;
		RepLayout->SerializePropertiesForStruct(
			AppearanceStruct,
			Writer,
			nullptr,
			FRepObjectDataBuffer(reinterpret_cast<uint8*>(&MutableState)),
			bHasUnmapped);
		if (Writer.IsError() || bHasUnmapped)
		{
			OutError = Writer.IsError()
				? TEXT("RepAppearance replicated-property serialization overflowed or failed.")
				: TEXT("RepAppearance unexpectedly serialized an unmapped object reference.");
			return false;
		}

		OutResult.PayloadBits = Writer.GetNumBits();
		OutResult.PayloadBytesCeil = (OutResult.PayloadBits + 7ll) / 8ll;
		if (OutResult.PayloadBits <= 0)
		{
			OutError = TEXT("RepAppearance replicated-property serialization produced no payload bits.");
			return false;
		}

		FPaper2DPlusRepAppearanceState RoundTrip;
		FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
		bool bReadHasUnmapped = false;
		RepLayout->SerializePropertiesForStruct(
			AppearanceStruct,
			Reader,
			nullptr,
			FRepObjectDataBuffer(reinterpret_cast<uint8*>(&RoundTrip)),
			bReadHasUnmapped);
		OutResult.bRoundTripMatched = !Reader.IsError()
			&& !bReadHasUnmapped
			&& StateEquals(State, RoundTrip);
		if (!OutResult.bRoundTripMatched)
		{
			OutError = TEXT("RepAppearance replicated-property payload did not round-trip exactly.");
			return false;
		}
		return true;
	}
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
