// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterCatalog/Paper2DPlusCatalogGraphPinFactory.h"

#include "AssetRegistry/AssetData.h"
#include "CharacterCatalog/K2Node_SelectCharacterFromCatalog.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "EdGraph/EdGraphPin.h"
#include "IContentBrowserSingleton.h"
#include "KismetPins/SGraphPinObject.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Selection.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusCatalogGraphPin"

namespace
{
	class SPaper2DPlusCatalogCharacterPin : public SGraphPinObject
	{
	public:
		SLATE_BEGIN_ARGS(SPaper2DPlusCatalogCharacterPin) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
		{
			SGraphPinObject::Construct(SGraphPinObject::FArguments(), InGraphPinObj);
		}

	protected:
		virtual FText GetDefaultComboText() const override
		{
			return LOCTEXT("ChooseCatalogCharacter", "Choose Catalog Character");
		}

		virtual FOnClicked GetOnUseButtonDelegate() override
		{
			return FOnClicked::CreateSP(this, &SPaper2DPlusCatalogCharacterPin::OnClickUseCatalogSelection);
		}

		virtual TSharedRef<SWidget> GenerateAssetPicker() override
		{
			const bool bHasCatalog = RefreshSelectablePaths();

			FContentBrowserModule& ContentBrowserModule =
				FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
			FAssetPickerConfig PickerConfig;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 0
			PickerConfig.Filter.ClassNames.Add(UPaper2DPlusCharacterProfileAsset::StaticClass()->GetFName());
#else
			PickerConfig.Filter.ClassPaths.Add(
				UPaper2DPlusCharacterProfileAsset::StaticClass()->GetClassPathName());
#endif
			PickerConfig.Filter.bRecursiveClasses = true;
			PickerConfig.bAllowNullSelection = true;
			PickerConfig.OnShouldFilterAsset =
				FOnShouldFilterAsset::CreateSP(this, &SPaper2DPlusCatalogCharacterPin::ShouldFilterAsset);
			PickerConfig.OnAssetSelected =
				FOnAssetSelected::CreateSP(this, &SPaper2DPlusCatalogCharacterPin::HandleAssetSelected);
			PickerConfig.OnAssetEnterPressed =
				FOnAssetEnterPressed::CreateSP(this, &SPaper2DPlusCatalogCharacterPin::HandleAssetEnterPressed);
			PickerConfig.InitialAssetViewType = EAssetViewType::List;
			PickerConfig.bAllowDragging = false;
			PickerConfig.InitialAssetSelection = GetAssetData(false);
			PickerConfig.AssetShowWarningText = bHasCatalog
				? LOCTEXT("EmptyCatalog", "The resolved Character Catalog has no selectable profiles.")
				: LOCTEXT("MissingCatalog", "Set the Catalog pin or configure the project default Catalog.");

			return SNew(SBox)
				.HeightOverride(300.0f)
				.WidthOverride(360.0f)
				[
					ContentBrowserModule.Get().CreateAssetPicker(PickerConfig)
				];
		}

		virtual void OnAssetSelectedFromPicker(const FAssetData& AssetData) override
		{
			if (AssetData.IsValid() && ShouldFilterAsset(AssetData))
			{
				return;
			}
			SGraphPinObject::OnAssetSelectedFromPicker(AssetData);
		}

	private:
		bool ShouldFilterAsset(const FAssetData& AssetData) const
		{
			if (!AssetData.IsValid())
			{
				return false;
			}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
			const FSoftObjectPath AssetPath = AssetData.ToSoftObjectPath();
#else
			const FSoftObjectPath AssetPath = AssetData.GetSoftObjectPath();
#endif
			return !SelectablePaths.Contains(AssetPath);
		}

		void HandleAssetSelected(const FAssetData& AssetData)
		{
			OnAssetSelectedFromPicker(AssetData);
		}

		void HandleAssetEnterPressed(const TArray<FAssetData>& Assets)
		{
			if (Assets.Num() > 0)
			{
				OnAssetSelectedFromPicker(Assets[0]);
			}
		}

		FReply OnClickUseCatalogSelection()
		{
			RefreshSelectablePaths();
			FEditorDelegates::LoadSelectedAssetsIfNeeded.Broadcast();
			if (!GEditor)
			{
				return FReply::Handled();
			}

			if (UObject* SelectedObject = GEditor->GetSelectedObjects()->GetTop(
				UPaper2DPlusCharacterProfileAsset::StaticClass()))
			{
				OnAssetSelectedFromPicker(FAssetData(SelectedObject, true));
			}
			return FReply::Handled();
		}

		bool RefreshSelectablePaths()
		{
			const UK2Node_SelectCharacterFromCatalog* Node = GraphPinObj
				? Cast<UK2Node_SelectCharacterFromCatalog>(GraphPinObj->GetOwningNode())
				: nullptr;
			SelectablePaths = Node
				? Node->GetSelectableCharacterProfilePaths()
				: TSet<FSoftObjectPath>();
			return Node && Node->ResolveCatalogForEditor();
		}

		TSet<FSoftObjectPath> SelectablePaths;
	};
}

TSharedPtr<SGraphPin> FPaper2DPlusCatalogGraphPinFactory::CreatePin(UEdGraphPin* Pin) const
{
	if (Pin
		&& Pin->PinName == UK2Node_SelectCharacterFromCatalog::CharacterPinName
		&& Cast<UK2Node_SelectCharacterFromCatalog>(Pin->GetOwningNode()))
	{
		return SNew(SPaper2DPlusCatalogCharacterPin, Pin);
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
