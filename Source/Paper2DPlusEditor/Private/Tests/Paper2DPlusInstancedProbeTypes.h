// Copyright 2026 Infinite Gameworks. All Rights Reserved.
//
// Phase 0 HARD GATE types for the Instanced UPROPERTY probe test.
// See Paper2DPlusInstancedProbeTest.cpp in this directory for the 5 automation tests,
// and docs/plans/2026-04-08-feat-frame-events-and-data-layer-decomposition-plan.md
// Phase 0 section for the design rationale.
//
// These types mirror the proposed Frame Events storage shape:
//   UPaper2DPlusCharacterProfileAsset (UPrimaryDataAsset)
//     +-- TArray<FFlipbookProfileEntry>
//          +-- FFlipbookFrameEventData (USTRUCT)
//               +-- UPROPERTY(Instanced) TArray<TObjectPtr<UPaper2DPlusFrameEventBase>>
//
// The probe validates that UE 5.7 handles UPROPERTY(Instanced) correctly when
// the array lives inside a USTRUCT nested inside a UPrimaryDataAsset. If the
// probe fails, the plan pivots to a UFlipbookEventContainer UObject-wrapper
// fallback (see plan Risk 1 mitigation).

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/DataAsset.h"
#include "Paper2DPlusInstancedProbeTypes.generated.h"


/** Probe subobject base. Test creates instances of this via NewObject and stores
 *  them in a UPROPERTY(Instanced) TArray inside a nested USTRUCT. */
UCLASS()
class UPaper2DPlusProbeBase : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Probe")
	int32 ProbeValue = 0;
};


/** Probe wrapper USTRUCT. This is the layer under scrutiny -- the question is
 *  whether UPROPERTY(Instanced) on a TArray<TObjectPtr<>> inside a USTRUCT
 *  behaves the same as it would on a direct UCLASS field. */
USTRUCT()
struct FPaper2DPlusProbeStruct
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Instanced, Category = "Probe")
	TArray<TObjectPtr<UPaper2DPlusProbeBase>> ProbeArray;
};


/** Probe UPrimaryDataAsset. Mirrors UPaper2DPlusCharacterProfileAsset -- a data
 *  asset holding a TArray of the USTRUCT wrapper above. */
UCLASS()
class UPaper2DPlusProbeAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Probe")
	TArray<FPaper2DPlusProbeStruct> ProbeEntries;
};
