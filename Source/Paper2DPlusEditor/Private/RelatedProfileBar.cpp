// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "RelatedProfileBar.h"

#include "Misc/App.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusSettings.h"
#include "ProfileRelationshipService.h"
#include "ScopedTransaction.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusRelatedProfileBar"

namespace
{
	bool RelatedBarPathsEqual(const FSoftObjectPath& A, const FSoftObjectPath& B)
	{
		return FProfileRelationshipService::NormalizeObjectPath(A)
			== FProfileRelationshipService::NormalizeObjectPath(B);
	}

	FString RelatedBarCompanionSuffix(EPaper2DPlusCatalogCompanion Companion)
	{
		switch (Companion)
		{
		case EPaper2DPlusCatalogCompanion::Layer: return TEXT("_Layers");
		case EPaper2DPlusCatalogCompanion::Effect: return TEXT("_Effects");
		default: return TEXT("_Combat");
		}
	}
}

void SRelatedProfileBar::Construct(const FArguments& InArgs)
{
	EditedAsset = InArgs._EditedAsset;
	SettingsChangedHandle = UPaper2DPlusSettings::OnCharacterCatalogSettingsChanged().AddSP(
		this, &SRelatedProfileBar::HandleSettingsChanged);
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(
		this, &SRelatedProfileBar::HandleObjectPropertyChanged);
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			SAssignNew(Host, SVerticalBox)
		]
	];
	Refresh();
}

SRelatedProfileBar::~SRelatedProfileBar()
{
	if (SettingsChangedHandle.IsValid())
	{
		UPaper2DPlusSettings::OnCharacterCatalogSettingsChanged().Remove(SettingsChangedHandle);
	}
	if (PropertyChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
	}
}

FPaper2DPlusRelatedProfileContext SRelatedProfileBar::BuildContext(
	UObject* InEditedAsset,
	UPaper2DPlusCharacterCatalogAsset* LoadedCatalogOverride)
{
	FPaper2DPlusRelatedProfileContext Result;
	if (!InEditedAsset)
	{
		Result.StatusText = LOCTEXT("NoAsset", "Related profiles unavailable: the edited asset no longer exists.");
		return Result;
	}
	Result.CurrentAssetPath = FSoftObjectPath(InEditedAsset);

	UPaper2DPlusCharacterCatalogAsset* Catalog = LoadedCatalogOverride;
	if (!Catalog)
	{
		const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();
		Result.CatalogPath = Settings ? Settings->DefaultCharacterCatalog.ToSoftObjectPath() : FSoftObjectPath();
		if (Settings && !Settings->DefaultCharacterCatalog.IsNull())
		{
			// Resolve one small directory asset for an explicitly opened editor; never bulk-load its entries.
			Catalog = Settings->DefaultCharacterCatalog.LoadSynchronous();
		}
	}
	if (Catalog)
	{
		Result.CatalogPath = FSoftObjectPath(Catalog);
	}

	if (UPaper2DPlusCharacterProfileAsset* Character = Cast<UPaper2DPlusCharacterProfileAsset>(InEditedAsset))
	{
		Result.CharacterPath = FSoftObjectPath(Character);
	}
	else if (UPaper2DPlusCharacterLayerAsset* Layer = Cast<UPaper2DPlusCharacterLayerAsset>(InEditedAsset))
	{
		Result.CharacterPath = Layer->BaseProfile.ToSoftObjectPath();
		Result.LayerPath = FSoftObjectPath(Layer);
	}
	else if (UPaper2DPlusCombatProfileAsset* Combat = Cast<UPaper2DPlusCombatProfileAsset>(InEditedAsset))
	{
		Result.CharacterPath = Combat->CharacterProfile ? FSoftObjectPath(Combat->CharacterProfile) : FSoftObjectPath();
		Result.CombatPath = FSoftObjectPath(Combat);
	}
	else if (UPaper2DPlusEffectProfileAsset* Effect = Cast<UPaper2DPlusEffectProfileAsset>(InEditedAsset))
	{
		Result.EffectPath = FSoftObjectPath(Effect);
		if (Catalog)
		{
			for (const FPaper2DPlusCharacterCatalogEntry& Entry : Catalog->Entries)
			{
				if (!RelatedBarPathsEqual(Entry.EffectProfile.ToSoftObjectPath(), Result.EffectPath)) continue;
				if (Result.CharacterPath.IsNull()) Result.CharacterPath = Entry.CharacterProfile.ToSoftObjectPath();
				else if (!RelatedBarPathsEqual(Result.CharacterPath, Entry.CharacterProfile.ToSoftObjectPath()))
				{
					Result.bEffectAssociationAmbiguous = true;
				}
			}
		}
	}

	if (Catalog && !Result.CharacterPath.IsNull())
	{
		if (const FPaper2DPlusCharacterCatalogEntry* Entry = Catalog->Entries.FindByPredicate([&Result](const FPaper2DPlusCharacterCatalogEntry& Candidate)
		{
			return RelatedBarPathsEqual(Candidate.CharacterProfile.ToSoftObjectPath(), Result.CharacterPath);
		}))
		{
			Result.bHasCatalogEntry = true;
			Result.LayerPath = Entry->LayerProfile.ToSoftObjectPath();
			Result.EffectPath = Entry->EffectProfile.ToSoftObjectPath();
			Result.CombatPath = Entry->CombatProfile.ToSoftObjectPath();
		}
	}

	if (!Catalog)
	{
		Result.StatusText = LOCTEXT("NoCatalog", "No project Character Catalog is configured or loadable. Open Paper2DPlus Project Settings.");
	}
	else if (Result.bEffectAssociationAmbiguous)
	{
		Result.StatusText = LOCTEXT("EffectAmbiguous", "This Effect Profile is assigned to multiple Catalog characters. Open the Catalog to resolve the duplicate forward assignments.");
	}
	else if (Result.CharacterPath.IsNull())
	{
		Result.StatusText = Cast<UPaper2DPlusEffectProfileAsset>(InEditedAsset)
			? LOCTEXT("EffectUnassigned", "This Effect Profile has no inward Character link by design and is not assigned by any default Catalog row.")
			: LOCTEXT("NoCharacter", "This profile has no Character Profile relationship yet.");
	}
	else if (!Result.bHasCatalogEntry)
	{
		Result.StatusText = LOCTEXT("NotCataloged", "The related Character Profile is not in the saved default Catalog. Review and Sync the Catalog before creating companions here.");
	}
	else
	{
		Result.StatusText = LOCTEXT("Ready", "Related profiles resolve through the default Catalog row. Effect ownership is Catalog-to-Effect only.");
		Result.bReady = true;
	}
	return Result;
}

void SRelatedProfileBar::Refresh()
{
	Context = BuildContext(EditedAsset.Get());
	LoadedCatalog = Cast<UPaper2DPlusCharacterCatalogAsset>(Context.CatalogPath.ResolveObject());
	if (!Host.IsValid()) return;
	Host->ClearChildren();
	Host->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("RelatedProfilesHeading", "Related Profiles"))
		.Font(FAppStyle::GetFontStyle("BoldFont"))
	];
	Host->AddSlot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
		.Text(Context.StatusText)
		.ToolTipText(Context.StatusText)
		.AccessibleText(FText::Format(
			LOCTEXT("RelatedProblemAccessible", "Related profile status: {0}"),
			Context.StatusText))
		.ColorAndOpacity(Context.bReady
			? FSlateColor::UseSubduedForeground()
			: FSlateColor::UseForeground())
		.AutoWrapText(true)
	];
	Host->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		BuildAssetAction(LOCTEXT("Catalog", "Catalog"), Context.CatalogPath)
	];
	Host->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		BuildAssetAction(LOCTEXT("Character", "Character"), Context.CharacterPath)
	];
	Host->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		BuildAssetAction(LOCTEXT("Layer", "Layer"), Context.LayerPath, EPaper2DPlusCatalogCompanion::Layer)
	];
	Host->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		BuildAssetAction(LOCTEXT("Effect", "Effect"), Context.EffectPath, EPaper2DPlusCatalogCompanion::Effect)
	];
	Host->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
	[
		BuildAssetAction(LOCTEXT("Combat", "Combat"), Context.CombatPath, EPaper2DPlusCatalogCompanion::Combat)
	];
}

TSharedRef<SWidget> SRelatedProfileBar::BuildAssetAction(
	const FText& Label,
	const FSoftObjectPath& Path,
	TOptional<EPaper2DPlusCatalogCompanion> MissingCompanion)
{
	const bool bCurrent = !Path.IsNull() && RelatedBarPathsEqual(Path, Context.CurrentAssetPath);
	if (!Path.IsNull())
	{
		return SNew(SButton)
			.Text(bCurrent
				? FText::Format(LOCTEXT("Current", "{0} ✓"), Label)
				: Label)
			.ContentPadding(FMargin(5.0f, 1.0f))
			.ToolTipText(bCurrent
				? FText::Format(LOCTEXT("CurrentTip", "Current related {0}: {1}"), Label, FText::FromString(Path.ToString()))
				: FText::Format(LOCTEXT("OpenTip", "Open related {0}: {1}"), Label, FText::FromString(Path.ToString())))
			.AccessibleText(bCurrent ? FText::Format(LOCTEXT("CurrentAccessible", "Current asset is the related {0} profile"), Label)
				: FText::Format(LOCTEXT("OpenAccessible", "Open related {0} profile"), Label))
			.IsEnabled(!bCurrent)
			.OnClicked_Lambda([this, Path]()
			{
				FText Error;
				if (!OpenPath(Path, Error)) ShowMessage(Error);
				return FReply::Handled();
			});
	}
	if (MissingCompanion.IsSet())
	{
		return SNew(SButton)
			.Text(FText::Format(LOCTEXT("Create", "+ {0}"), Label))
			.ContentPadding(FMargin(5.0f, 1.0f))
			.ToolTipText(MissingCompanion.GetValue() == EPaper2DPlusCatalogCompanion::Effect
				? LOCTEXT("CreateEffect", "Create and assign an Effect Profile in the Catalog row only; no inward Effect relationship is written.")
				: LOCTEXT("CreateCompanion", "Create the companion with an inward Character relationship and assign it to the Catalog row."))
			.IsEnabled(Context.bHasCatalogEntry && !Context.CharacterPath.IsNull())
			.OnClicked_Lambda([this, Companion = MissingCompanion.GetValue()]()
			{
				FText Error;
				if (!CreateAndAssign(Companion, Error)) ShowMessage(Error);
				return FReply::Handled();
			});
	}
	return SNew(SButton)
		.Text(FText::Format(LOCTEXT("Missing", "{0} —"), Label))
		.ContentPadding(FMargin(5.0f, 1.0f))
		.ToolTipText(FText::Format(LOCTEXT("MissingTip", "No related {0} is assigned."), Label))
		.IsEnabled(false);
}

bool SRelatedProfileBar::OpenPath(const FSoftObjectPath& Path, FText& OutError) const
{
	if (!FApp::CanEverRender())
	{
		OutError = LOCTEXT("Headless", "Asset opening is unavailable in a headless process.");
		return false;
	}
	return FProfileRelationshipService::OpenRelatedAsset(Path, OutError);
}

UObject* SRelatedProfileBar::CreateAndAssign(
	EPaper2DPlusCatalogCompanion Companion,
	FText& OutError)
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = LoadedCatalog.Get();
	if (!Catalog || !Context.bHasCatalogEntry || Context.CharacterPath.IsNull())
	{
		OutError = LOCTEXT("CreateNeedsCatalog", "A saved default Catalog row is required before creating a related profile here.");
		return nullptr;
	}
	const FString CharacterPackage = FPackageName::ObjectPathToPackageName(Context.CharacterPath.ToString());
	const FString Destination = FPackageName::GetLongPackagePath(CharacterPackage);
	const FString AssetName = FPackageName::GetShortName(CharacterPackage) + RelatedBarCompanionSuffix(Companion);
	UObject* Created = FProfileRelationshipService::CreateRelatedAsset(
		Companion, Context.CharacterPath, Destination, AssetName, OutError);
	if (!Created) return nullptr;

	const int32 EntryIndex = Catalog->Entries.IndexOfByPredicate([this](const FPaper2DPlusCharacterCatalogEntry& Entry)
	{
		return RelatedBarPathsEqual(Entry.CharacterProfile.ToSoftObjectPath(), Context.CharacterPath);
	});
	if (!Catalog->Entries.IsValidIndex(EntryIndex))
	{
		OutError = LOCTEXT("CatalogRowVanished", "The Catalog row changed while creating the asset. The new asset remains available but was not assigned.");
		return Created;
	}
	{
		FScopedTransaction Transaction(LOCTEXT("AssignCreatedRelated", "Assign Created Related Profile"));
		Catalog->Modify();
		FPaper2DPlusCharacterCatalogEntry& Entry = Catalog->Entries[EntryIndex];
		switch (Companion)
		{
		case EPaper2DPlusCatalogCompanion::Layer:
			Entry.LayerProfile = CastChecked<UPaper2DPlusCharacterLayerAsset>(Created);
			break;
		case EPaper2DPlusCatalogCompanion::Effect:
			Entry.EffectProfile = CastChecked<UPaper2DPlusEffectProfileAsset>(Created);
			break;
		default:
			Entry.CombatProfile = CastChecked<UPaper2DPlusCombatProfileAsset>(Created);
			break;
		}
		Catalog->MarkPackageDirty();
	}
	OutError = LOCTEXT("Created", "Related profile created and assigned. Save remains explicit.");
	Refresh();
	return Created;
}

void SRelatedProfileBar::ShowMessage(const FText& Message) const
{
	if (!Message.IsEmpty()) FMessageDialog::Open(EAppMsgType::Ok, Message);
}

void SRelatedProfileBar::HandleSettingsChanged()
{
	Refresh();
}

void SRelatedProfileBar::HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	if (Object && (Object == EditedAsset.Get() || Object == LoadedCatalog.Get()))
	{
		Refresh();
	}
}

#undef LOCTEXT_NAMESPACE
