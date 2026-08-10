// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProfileItemPicker.h"

class FCharacterProfileEditorModel;
class UPaper2DPlusCharacterProfileAsset;

/** Character/Layer animation projection backed by the shared editor model. */
class PAPER2DPLUSEDITOR_API FAnimationProfilePickerSource final : public IProfileItemPickerSource
{
public:
	explicit FAnimationProfilePickerSource(TSharedPtr<FCharacterProfileEditorModel> InModel);
	virtual ~FAnimationProfilePickerSource() override;

	virtual FName GetSourceType() const override;
	virtual FString GetLogicalCatalogScope() const override;
	virtual void GetItems(TArray<FProfilePickerItem>& OutItems) const override;
	virtual FProfileItemIdentity GetSelectedIdentity() const override;
	virtual bool SelectItem(const FProfileItemIdentity& Identity) override;
	virtual FOnProfileItemSourceChanged& OnSourceChanged() override { return SourceChanged; }

	/** Exact path first, then case-insensitive name fallback; never selects a neighbor. */
	int32 ResolveIndex(const FProfileItemIdentity& Identity) const;
	void NotifySourceChanged();

private:
	FProfileItemIdentity MakeIdentity(int32 Index) const;

	TSharedPtr<FCharacterProfileEditorModel> Model;
	FOnProfileItemSourceChanged SourceChanged;
	FDelegateHandle SelectionChangedHandle;
	FDelegateHandle AssetDataChangedHandle;
	FDelegateHandle ExternalModifiedHandle;
};

