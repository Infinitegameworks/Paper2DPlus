// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Widgets/SCompoundWidget.h"

class UObject;

/** Prepared related-profile context. Every identity is a soft path; companion assets stay unloaded. */
struct PAPER2DPLUSEDITOR_API FPaper2DPlusRelatedProfileContext
{
	FSoftObjectPath CatalogPath;
	FSoftObjectPath CharacterPath;
	FSoftObjectPath LayerPath;
	FSoftObjectPath EffectPath;
	FSoftObjectPath CombatPath;
	FSoftObjectPath CurrentAssetPath;
	FText StatusText;
	bool bHasCatalogEntry = false;
	bool bEffectAssociationAmbiguous = false;
	/** True when the default Catalog resolves the complete companion set without warnings. */
	bool bReady = false;
};

/** Dockable open/create navigation shared by Character, Layer, Effect, and Combat editors. */
class PAPER2DPLUSEDITOR_API SRelatedProfileBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRelatedProfileBar) {}
		SLATE_ARGUMENT(UObject*, EditedAsset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SRelatedProfileBar() override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	void Refresh();
	const FPaper2DPlusRelatedProfileContext& GetContextForTests() const { return Context; }
	static FPaper2DPlusRelatedProfileContext BuildContext(
		UObject* EditedAsset,
		UPaper2DPlusCharacterCatalogAsset* LoadedCatalogOverride = nullptr);

private:
	TSharedRef<SWidget> BuildAssetAction(
		const FText& Label,
		const FSoftObjectPath& Path,
		TOptional<EPaper2DPlusCatalogCompanion> MissingCompanion = TOptional<EPaper2DPlusCatalogCompanion>());
	bool OpenPath(const FSoftObjectPath& Path, FText& OutError) const;
	UObject* CreateAndAssign(EPaper2DPlusCatalogCompanion Companion, FText& OutError);
	void ShowMessage(const FText& Message) const;
	void HandleSettingsChanged();
	void HandleObjectPropertyChanged(UObject* Object, struct FPropertyChangedEvent& Event);

	TWeakObjectPtr<UObject> EditedAsset;
	TWeakObjectPtr<UPaper2DPlusCharacterCatalogAsset> LoadedCatalog;
	FPaper2DPlusRelatedProfileContext Context;
	TSharedPtr<class SVerticalBox> Host;
	FDelegateHandle SettingsChangedHandle;
	FDelegateHandle PropertyChangedHandle;
};
