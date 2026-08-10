// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "UObject/SoftObjectPtr.h"

class UPaper2DPlusCharacterCatalogAsset;

/** Settings snapshot used by live and injected Catalog source providers. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusCharacterCatalogSettingsSnapshot
{
	TSoftObjectPtr<UPaper2DPlusCharacterCatalogAsset> DefaultCatalog;
};

/**
 * Injection seams shared by the Catalog editor model and audit service. The asset provider returns
 * registry metadata for the four Paper2DPlus profile classes; neither provider may load an asset.
 */
namespace Paper2DPlusCharacterCatalogSource
{
	using FAssetSnapshotProvider = TFunction<void(TArray<FAssetData>&)>;
	using FSettingsSnapshotProvider = TFunction<FPaper2DPlusCharacterCatalogSettingsSnapshot()>;
}
