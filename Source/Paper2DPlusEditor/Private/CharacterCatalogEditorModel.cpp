// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterCatalogEditorModel.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "ISettingsModule.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"  // FPackageName::IsShortPackageName — guards FSoftObjectPath's ensure
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "Paper2DPlusAuthoringProgressTags.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusEffectLibraryIndex.h"
#include "Paper2DPlusSettings.h"
#include "ProfileRelationshipService.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusCharacterCatalogEditorModel"

namespace
{
	const FName CharacterDisplayNameTag(TEXT("Paper2DPlus.CharacterDisplayName"));

	FString CatalogModelNormalizePath(const FSoftObjectPath& Path)
	{
		return FProfileRelationshipService::NormalizeObjectPath(Path);
	}

	bool CatalogModelPathsEqual(const FSoftObjectPath& A, const FSoftObjectPath& B)
	{
		return CatalogModelNormalizePath(A) == CatalogModelNormalizePath(B);
	}

	bool CatalogModelIsRelevantAsset(const FAssetData& AssetData)
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		const FName ClassName = AssetData.AssetClass;
		return ClassName == UPaper2DPlusCharacterProfileAsset::StaticClass()->GetFName()
			|| ClassName == UPaper2DPlusCharacterLayerAsset::StaticClass()->GetFName()
			|| ClassName == UPaper2DPlusCombatProfileAsset::StaticClass()->GetFName()
			|| ClassName == UPaper2DPlusEffectProfileAsset::StaticClass()->GetFName();
#else
		const FTopLevelAssetPath ClassPath = AssetData.AssetClassPath;
		return ClassPath == UPaper2DPlusCharacterProfileAsset::StaticClass()->GetClassPathName()
			|| ClassPath == UPaper2DPlusCharacterLayerAsset::StaticClass()->GetClassPathName()
			|| ClassPath == UPaper2DPlusCombatProfileAsset::StaticClass()->GetClassPathName()
			|| ClassPath == UPaper2DPlusEffectProfileAsset::StaticClass()->GetClassPathName();
#endif
	}

	FText FriendlyAssetLabel(const FString& AssetName)
	{
		const FString TrimmedName = AssetName.TrimStartAndEnd();
		return TrimmedName.IsEmpty()
			? LOCTEXT("UnnamedCharacter", "Unnamed Character")
			: FText::FromString(FName::NameToDisplayString(TrimmedName, false));
	}

	bool IsDesignerCharacterLabel(const FString& Candidate)
	{
		return !Candidate.IsEmpty()
			&& !Candidate.Equals(TEXT("New Character Profile"), ESearchCase::IgnoreCase);
	}

	FText CharacterLabel(const FSoftObjectPath& Path)
	{
		const FString ObjectPath = Path.ToString();
		int32 DotIndex = INDEX_NONE;
		if (ObjectPath.FindLastChar(TEXT('.'), DotIndex) && DotIndex + 1 < ObjectPath.Len())
		{
			return FriendlyAssetLabel(ObjectPath.Mid(DotIndex + 1));
		}
		int32 SlashIndex = INDEX_NONE;
		if (ObjectPath.FindLastChar(TEXT('/'), SlashIndex) && SlashIndex + 1 < ObjectPath.Len())
		{
			return FriendlyAssetLabel(ObjectPath.Mid(SlashIndex + 1));
		}
		return FriendlyAssetLabel(ObjectPath);
	}

	FText CharacterLabel(
		const UPaper2DPlusCharacterProfileAsset* ResidentCharacter,
		const FAssetData& CharacterAssetData,
		const FSoftObjectPath& CharacterPath)
	{
		// Prefer live authored data when the Profile is already resident. Otherwise use the hidden
		// registry tag emitted by Character Profiles, then the concise AssetName. No branch loads an asset.
		if (ResidentCharacter)
		{
			const FString AuthoredName = ResidentCharacter->DisplayName.TrimStartAndEnd();
			if (IsDesignerCharacterLabel(AuthoredName))
			{
				return FText::FromString(AuthoredName);
			}
		}

		FString RegistryDisplayName;
		if (CharacterAssetData.IsValid()
			&& CharacterAssetData.GetTagValue(CharacterDisplayNameTag, RegistryDisplayName))
		{
			RegistryDisplayName.TrimStartAndEndInline();
			if (IsDesignerCharacterLabel(RegistryDisplayName))
			{
				return FText::FromString(RegistryDisplayName);
			}
		}

		if (CharacterAssetData.IsValid())
		{
			return FriendlyAssetLabel(CharacterAssetData.AssetName.ToString());
		}
		return CharacterLabel(CharacterPath);
	}

	FSoftObjectPath CatalogModelCompanionPath(
		const FPaper2DPlusCharacterCatalogEntry& Entry,
		EPaper2DPlusCatalogCompanion Companion)
	{
		switch (Companion)
		{
		case EPaper2DPlusCatalogCompanion::Layer:
			return Entry.LayerProfile.ToSoftObjectPath();
		case EPaper2DPlusCatalogCompanion::Effect:
			return Entry.EffectProfile.ToSoftObjectPath();
		default:
			return Entry.CombatProfile.ToSoftObjectPath();
		}
	}

	/**
	 * Fold one source's ticked-checklist progress into a row total.
	 *
	 * A resident asset is preferred over its registry tags because tags only refresh on SAVE: a designer
	 * ticking criteria in an open workspace must not see the Catalog contradict the panel in front of
	 * them. An unassigned companion is not a source at all; a present source that reports nothing usable
	 * is counted as missing so an unmigrated project reads "unknown" instead of a false 100%.
	 */
	void AccumulateAuthoringProgress(
		const FSoftObjectPath& AssetPath,
		const FAssetData* AssetData,
		FPaper2DPlusCatalogAuthoringProgress& OutProgress)
	{
		if (AssetPath.IsNull())
		{
			return;
		}

		int32 Done = 0;
		int32 Total = 0;
		bool bReported = false;

		if (const UObject* Resident = AssetPath.ResolveObject())
		{
			if (const UPaper2DPlusCharacterProfileAsset* Character =
				Cast<UPaper2DPlusCharacterProfileAsset>(Resident))
			{
				Character->GetAuthoringProgress(Done, Total);
				bReported = true;
			}
			else if (const UPaper2DPlusCharacterLayerAsset* Layer =
				Cast<UPaper2DPlusCharacterLayerAsset>(Resident))
			{
				Paper2DPlusAuthoringProgress::ComputeChecklistProgress(
					Layer->EditorCompletionFlags, Done, Total);
				bReported = true;
			}
			else if (const UPaper2DPlusEffectProfileAsset* Effect =
				Cast<UPaper2DPlusEffectProfileAsset>(Resident))
			{
				Paper2DPlusAuthoringProgress::ComputeChecklistProgress(
					Effect->EditorCompletionFlags, Done, Total);
				bReported = true;
			}
			else if (const UPaper2DPlusCombatProfileAsset* Combat =
				Cast<UPaper2DPlusCombatProfileAsset>(Resident))
			{
				Paper2DPlusAuthoringProgress::ComputeChecklistProgress(
					Combat->EditorCompletionFlags, Done, Total);
				bReported = true;
			}
		}

		if (!bReported && AssetData && AssetData->IsValid())
		{
			int32 TagDone = 0;
			int32 TagTotal = 0;
			// GetTagValue succeeds for a tag that merely exists and Atoi's garbage to 0, so the pair is
			// validated below rather than trusted here.
			if (AssetData->GetTagValue(Paper2DPlusAuthoringProgress::DoneTag(), TagDone)
				&& AssetData->GetTagValue(Paper2DPlusAuthoringProgress::TotalTag(), TagTotal))
			{
				Done = TagDone;
				Total = TagTotal;
				bReported = true;
			}
		}

		if (bReported && Paper2DPlusAuthoringProgress::IsAuthoringProgressPairValid(Done, Total))
		{
			OutProgress.Done += Done;
			OutProgress.Total += Total;
			++OutProgress.SourcesWithData;
			return;
		}
		if (bReported && Done == 0 && Total == 0)
		{
			// A well-formed "nothing to author" report (for example a Profile with no animations).
			// It counts as answered but contributes no denominator, so it can never fake completeness.
			++OutProgress.SourcesWithData;
			return;
		}
		++OutProgress.SourcesMissingData;
	}

	FText StatusText(EPaper2DPlusCatalogRowStatus Status)
	{
		switch (Status)
		{
		case EPaper2DPlusCatalogRowStatus::Complete:
			return LOCTEXT("StatusComplete", "[OK] Complete");
		case EPaper2DPlusCatalogRowStatus::OptionalMissing:
			return LOCTEXT("StatusOptional", "[-] Complete; optional profiles absent");
		case EPaper2DPlusCatalogRowStatus::RequiredMissing:
			return LOCTEXT("StatusRequired", "[!] Incomplete; required profile missing");
		case EPaper2DPlusCatalogRowStatus::MissingAsset:
			return LOCTEXT("StatusMissingAsset", "[X] Character Profile asset is missing");
		case EPaper2DPlusCatalogRowStatus::Warning:
			return LOCTEXT("StatusWarning", "[!] Warning");
		default:
			return LOCTEXT("StatusError", "[X] Error");
		}
	}

	FText StatusTooltip(EPaper2DPlusCatalogRowStatus Status)
	{
		switch (Status)
		{
		case EPaper2DPlusCatalogRowStatus::Complete:
			return LOCTEXT("CompleteTip", "Every declared requirement is assigned and the latest Catalog check has no row error.");
		case EPaper2DPlusCatalogRowStatus::OptionalMissing:
			return LOCTEXT("OptionalTip", "The character is complete. One or more optional companion profiles are intentionally unassigned.");
		case EPaper2DPlusCatalogRowStatus::RequiredMissing:
			return LOCTEXT("RequiredTip", "Assign every required companion or make the unneeded slot optional.");
		case EPaper2DPlusCatalogRowStatus::MissingAsset:
			return LOCTEXT("MissingAssetTip", "The saved entry points at a Character Profile that no longer exists. Restore the asset or remove the entry.");
		case EPaper2DPlusCatalogRowStatus::Warning:
			return LOCTEXT("WarningTip", "The latest Catalog check found one or more warnings for this character. See the Warnings tab.");
		default:
			return LOCTEXT("ErrorTip", "The latest Catalog check found one or more errors for this character. See the Warnings tab.");
		}
	}

	EPaper2DPlusCatalogRowStatus DeriveStatus(
		const FPaper2DPlusCharacterCatalogEditorRow& Row,
		const FPaper2DPlusCharacterCatalogCompletion& Completion,
		const FPaper2DPlusValidationSummary& Issues)
	{
		if (Row.bMissingAsset)
		{
			return EPaper2DPlusCatalogRowStatus::MissingAsset;
		}
		if (Issues.HasErrors())
		{
			return EPaper2DPlusCatalogRowStatus::Error;
		}
		if (!Completion.bComplete)
		{
			return EPaper2DPlusCatalogRowStatus::RequiredMissing;
		}
		if (Issues.HasWarnings())
		{
			return EPaper2DPlusCatalogRowStatus::Warning;
		}
		const bool bHasOptionalMissing =
			(!Row.Entry.Requirements.bRequireLayer && Row.Entry.LayerProfile.IsNull())
			|| (!Row.Entry.Requirements.bRequireEffect && Row.Entry.EffectProfile.IsNull())
			|| (!Row.Entry.Requirements.bRequireCombat && Row.Entry.CombatProfile.IsNull());
		return bHasOptionalMissing
			? EPaper2DPlusCatalogRowStatus::OptionalMissing
			: EPaper2DPlusCatalogRowStatus::Complete;
	}
}

bool FPaper2DPlusCharacterCatalogFilters::IsDefault() const
{
	return SearchText.IsEmpty()
		&& !Tag.IsValid()
		&& Group.IsNone()
		&& Requirement == EPaper2DPlusCatalogRequirementFilter::Any
		&& Completion == EPaper2DPlusCatalogCompletionFilter::Any
		&& Severity == EPaper2DPlusCatalogSeverityFilter::Any;
}

FCharacterCatalogEditorModel::~FCharacterCatalogEditorModel()
{
	Shutdown();
}

void FCharacterCatalogEditorModel::Initialize(
	UPaper2DPlusCharacterCatalogAsset* InCatalog,
	FAssetSnapshotProvider InAssetProvider,
	FSettingsSnapshotProvider InSettingsProvider,
	FNativeAssetResolver InAssetResolver,
	FCookRegistrationInspector InCookInspector)
{
	Shutdown();
	Catalog = InCatalog;
	AssetProvider = MoveTemp(InAssetProvider);
	SettingsProvider = MoveTemp(InSettingsProvider);
	AssetResolver = MoveTemp(InAssetResolver);
	CookInspector = MoveTemp(InCookInspector);
	SettingsChangedHandle = UPaper2DPlusSettings::OnCharacterCatalogSettingsChanged().AddRaw(
		this, &FCharacterCatalogEditorModel::HandleSettingsChanged);
	BindSourceDelegates();
	RebuildRows(true);
}

void FCharacterCatalogEditorModel::Shutdown()
{
	UnbindSourceDelegates();
	if (SettingsChangedHandle.IsValid())
	{
		UPaper2DPlusSettings::OnCharacterCatalogSettingsChanged().Remove(SettingsChangedHandle);
		SettingsChangedHandle.Reset();
	}
	Catalog.Reset();
	AssetProvider = FAssetSnapshotProvider();
	SettingsProvider = FSettingsSnapshotProvider();
	AssetResolver = FNativeAssetResolver();
	CookInspector = FCookRegistrationInspector();
	OpenAssetAction = FOpenAssetAction();
	OpenSettingsAction = FOpenSettingsAction();
	SetAuthorityAction = FSetAuthorityAction();
	CreateAssetAction = FCreateAssetAction();
	AuditReport = FPaper2DPlusCharacterCatalogAuditReport();
	bHasAuditReport = false;
	Rows.Reset();
	VisibleRows.Reset();
	SelectedCharacterPath.Reset();
	ModelChanged.Clear();
	SourceChanged.Clear();
}

void FCharacterCatalogEditorModel::BindSourceDelegates()
{
	if (BoundAssetRegistry
		|| !FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
	{
		return;
	}
	IAssetRegistry& Registry =
		FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	BoundAssetRegistry = &Registry;
	AssetAddedHandle = Registry.OnAssetAdded().AddRaw(
		this, &FCharacterCatalogEditorModel::HandleAssetChanged);
	AssetRemovedHandle = Registry.OnAssetRemoved().AddRaw(
		this, &FCharacterCatalogEditorModel::HandleAssetChanged);
	AssetRenamedHandle = Registry.OnAssetRenamed().AddRaw(
		this, &FCharacterCatalogEditorModel::HandleAssetRenamed);
	AssetUpdatedHandle = Registry.OnAssetUpdated().AddRaw(
		this, &FCharacterCatalogEditorModel::HandleAssetChanged);
}

void FCharacterCatalogEditorModel::UnbindSourceDelegates()
{
	if (RowRefreshTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RowRefreshTickerHandle);
		RowRefreshTickerHandle.Reset();
	}
	bRowRefreshPending = false;
	if (!BoundAssetRegistry)
	{
		return;
	}
	BoundAssetRegistry->OnAssetAdded().Remove(AssetAddedHandle);
	BoundAssetRegistry->OnAssetRemoved().Remove(AssetRemovedHandle);
	BoundAssetRegistry->OnAssetRenamed().Remove(AssetRenamedHandle);
	BoundAssetRegistry->OnAssetUpdated().Remove(AssetUpdatedHandle);
	AssetAddedHandle.Reset();
	AssetRemovedHandle.Reset();
	AssetRenamedHandle.Reset();
	AssetUpdatedHandle.Reset();
	BoundAssetRegistry = nullptr;
}

void FCharacterCatalogEditorModel::HandleAssetChanged(const FAssetData& AssetData)
{
	if (CatalogModelIsRelevantAsset(AssetData))
	{
		RequestRowRefresh();
	}
}

void FCharacterCatalogEditorModel::HandleAssetRenamed(
	const FAssetData& AssetData,
	const FString& OldObjectPath)
{
	HandleAssetChanged(AssetData);
}

void FCharacterCatalogEditorModel::HandleSettingsChanged()
{
	// Authority (banner text, audit gating) derives from the settings snapshot; re-project rows.
	RefreshFromSources();
}

void FCharacterCatalogEditorModel::RequestRowRefresh()
{
	if (bRowRefreshPending)
	{
		return;
	}
	bRowRefreshPending = true;
	RowRefreshTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FCharacterCatalogEditorModel::TickRowRefresh));
}

bool FCharacterCatalogEditorModel::TickRowRefresh(float DeltaTime)
{
	bRowRefreshPending = false;
	RowRefreshTickerHandle.Reset();
	RefreshFromSources();
	return false;
}

FPaper2DPlusCharacterCatalogSettingsSnapshot FCharacterCatalogEditorModel::GatherSettings() const
{
	if (SettingsProvider)
	{
		return SettingsProvider();
	}
	FPaper2DPlusCharacterCatalogSettingsSnapshot Snapshot;
	if (const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get())
	{
		Snapshot.DefaultCatalog = Settings->DefaultCharacterCatalog;
	}
	return Snapshot;
}

void FCharacterCatalogEditorModel::GatherAssets(TArray<FAssetData>& OutAssets) const
{
	OutAssets.Reset();
	if (AssetProvider)
	{
		AssetProvider(OutAssets);
		return;
	}
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter Filter;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	Filter.ClassNames.Add(UPaper2DPlusCharacterProfileAsset::StaticClass()->GetFName());
	Filter.ClassNames.Add(UPaper2DPlusCharacterLayerAsset::StaticClass()->GetFName());
	Filter.ClassNames.Add(UPaper2DPlusCombatProfileAsset::StaticClass()->GetFName());
	Filter.ClassNames.Add(UPaper2DPlusEffectProfileAsset::StaticClass()->GetFName());
#else
	Filter.ClassPaths.Add(UPaper2DPlusCharacterProfileAsset::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UPaper2DPlusCharacterLayerAsset::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UPaper2DPlusCombatProfileAsset::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UPaper2DPlusEffectProfileAsset::StaticClass()->GetClassPathName());
#endif
	Filter.bRecursiveClasses = true;
	Registry.GetAssets(Filter, OutAssets);
}

bool FCharacterCatalogEditorModel::IsAuthoritative() const
{
	const UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	if (!CatalogAsset)
	{
		return false;
	}
	return CatalogModelPathsEqual(
		FSoftObjectPath(CatalogAsset),
		GatherSettings().DefaultCatalog.ToSoftObjectPath());
}

FText FCharacterCatalogEditorModel::GetAuthorityStatusText() const
{
	if (IsAuthoritative())
	{
		return LOCTEXT("AuthorityYes", "Project Catalog — Blueprint defaults, the Catalog pin picker, and validation use this asset. Add Characters edits this roster; Unreal's Save command saves it to disk.");
	}
	return LOCTEXT("AuthorityNo", "This is not the Project Catalog. Saved rows remain available; choose Make Project Catalog so Blueprint defaults, pickers, and validation use this asset.");
}

bool FCharacterCatalogEditorModel::SetAsProjectCatalog(FText& OutMessage)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	if (SetAuthorityAction)
	{
		const bool bResult = SetAuthorityAction(CatalogAsset, OutMessage);
		if (bResult) RefreshFromSources();
		return bResult;
	}
	UPaper2DPlusSettings* Settings = GetMutableDefault<UPaper2DPlusSettings>();
	if (!CatalogAsset || !Settings)
	{
		OutMessage = LOCTEXT("AuthorityUnavailable", "The Catalog or Paper2DPlus project settings are unavailable.");
		return false;
	}
	if (CatalogModelPathsEqual(Settings->DefaultCharacterCatalog.ToSoftObjectPath(), FSoftObjectPath(CatalogAsset)))
	{
		OutMessage = LOCTEXT("AlreadyAuthority", "This Catalog is already the project authority.");
		return true;
	}

	// Config authority is deliberately separate from asset authoring: never Modify or dirty the Catalog.
	Settings->DefaultCharacterCatalog = CatalogAsset;
	Settings->SaveConfig();
	UPaper2DPlusSettings::OnCharacterCatalogSettingsChanged().Broadcast();
	OutMessage = LOCTEXT("AuthoritySet", "Project Character Catalog updated in Paper2DPlus settings. The Catalog asset itself was not modified.");
	return true;
}

bool FCharacterCatalogEditorModel::OpenProjectSettings() const
{
	if (OpenSettingsAction)
	{
		return OpenSettingsAction(TEXT("Paper2DPlus"));
	}
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>(TEXT("Settings"));
	if (!SettingsModule)
	{
		SettingsModule = &FModuleManager::LoadModuleChecked<ISettingsModule>(TEXT("Settings"));
	}
	if (!SettingsModule)
	{
		return false;
	}
	SettingsModule->ShowViewer(TEXT("Project"), TEXT("Plugins"), TEXT("Paper2DPlus"));
	return true;
}

void FCharacterCatalogEditorModel::RefreshFromSources()
{
	RebuildRows(true);
}

bool FCharacterCatalogEditorModel::RunAudit(FText& OutMessage)
{
	if (!IsAuthoritative())
	{
		OutMessage = LOCTEXT("AuditNeedsAuthority", "Set this asset as the Project Catalog before checking project warnings.");
		return false;
	}
	TArray<FAssetData> Assets;
	GatherAssets(Assets);
	FScopedSlowTask SlowTask(
		Rows.Num(),
		LOCTEXT("AuditProgress", "Checking Character Catalog warnings…"));
	if (Rows.Num() > 0
		&& FApp::CanEverRender()
		&& FSlateApplication::IsInitialized())
	{
		SlowTask.MakeDialog(/*bShowCancelButton=*/false);
	}
	AuditReport = FCharacterCatalogAuditService::BuildReport(
		Catalog.Get(),
		Assets,
		GatherSettings(),
		AssetResolver,
		CookInspector,
		[&SlowTask](
			int32 RowIndex,
			int32 RowCount,
			const FSoftObjectPath& CharacterPath)
		{
			const FString AssetName = CharacterPath.GetAssetName();
			const FText RowName = FText::FromString(
				AssetName.IsEmpty() ? CharacterPath.ToString() : AssetName);
			SlowTask.EnterProgressFrame(
				1.0f,
				FText::Format(
					LOCTEXT("AuditProgressRow", "Checking {0} ({1} of {2})"),
					RowName,
					FText::AsNumber(RowIndex + 1),
					FText::AsNumber(RowCount)));
		});
	bHasAuditReport = true;
	RebuildRows(false);
	OutMessage = FText::Format(
		LOCTEXT("AuditComplete", "Check complete: {0} characters, {1} errors, {2} warnings."),
		FText::AsNumber(AuditReport.Summary.NumCharacters),
		FText::AsNumber(AuditReport.Summary.Issues.NumErrors),
		FText::AsNumber(AuditReport.Summary.Issues.NumWarnings));
	return true;
}

void FCharacterCatalogEditorModel::RebuildRows(bool bClearAudit)
{
	if (bClearAudit)
	{
		bHasAuditReport = false;
		AuditReport = FPaper2DPlusCharacterCatalogAuditReport();
	}

	// Snapshot registry metadata once per model rebuild. Cards consume this immutable data and never
	// issue per-paint registry queries or synchronous loads, even for very large rosters.
	TArray<FAssetData> AssetSnapshot;
	GatherAssets(AssetSnapshot);
	TMap<FString, FAssetData> AssetDataByPath;
	AssetDataByPath.Reserve(AssetSnapshot.Num());
	for (const FAssetData& AssetData : AssetSnapshot)
	{
		if (!AssetData.IsValid())
		{
			continue;
		}
		const FString NormalizedAssetPath = CatalogModelNormalizePath(
			FProfileRelationshipService::GetAssetObjectPath(AssetData));
		if (!NormalizedAssetPath.IsEmpty())
		{
			AssetDataByPath.Add(NormalizedAssetPath, AssetData);
		}
	}

	// Rows are the SAVED roster in authored order: no discovery, no proposals, no re-sorting.
	Rows.Reset();
	TSet<FString> SeenPaths;
	const UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	const int32 NumEntries = CatalogAsset ? CatalogAsset->Entries.Num() : 0;
	for (int32 EntryIndex = 0; EntryIndex < NumEntries; ++EntryIndex)
	{
		const FPaper2DPlusCharacterCatalogEntry& Entry = CatalogAsset->Entries[EntryIndex];
		const FSoftObjectPath CharacterPath = Entry.CharacterProfile.ToSoftObjectPath();
		const FString NormalizedPath = CatalogModelNormalizePath(CharacterPath);
		if (CharacterPath.IsNull() || NormalizedPath.IsEmpty() || SeenPaths.Contains(NormalizedPath))
		{
			// Null and duplicate entries stay authorable via the Advanced raw array and are surfaced
			// by validation; the roster projects the same unique view GetCatalogEntries() returns.
			continue;
		}
		SeenPaths.Add(NormalizedPath);

		TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow> Row = MakeShared<FPaper2DPlusCharacterCatalogEditorRow>();
		Row->CharacterPath = CharacterPath;
		if (const FAssetData* CharacterAssetData = AssetDataByPath.Find(NormalizedPath))
		{
			Row->CharacterAssetData = *CharacterAssetData;
		}
		Row->bMissingAsset = !Row->CharacterAssetData.IsValid();
		Row->ResidentCharacterProfile = Cast<UPaper2DPlusCharacterProfileAsset>(
			CharacterPath.ResolveObject());
		Row->Entry = Entry;
		Row->DisplayName = CharacterLabel(
			Row->ResidentCharacterProfile.Get(),
			Row->CharacterAssetData,
			Row->CharacterPath);
		Row->Completion = CatalogAsset->GetEntryCompletion(Row->Entry);
		if (bHasAuditReport)
		{
			if (const FPaper2DPlusCharacterCatalogAuditRow* AuditRow = AuditReport.FindRow(Row->CharacterPath))
			{
				Row->Completion = AuditRow->RuntimeCompletion;
				Row->IssueSummary = AuditRow->IssueSummary;
			}
		}

		// Real authoring progress: the character plus each assigned companion. Every source resolves from
		// the one registry snapshot above or an already-resident object, so this stays load-free.
		Row->AuthoringProgress = FPaper2DPlusCatalogAuthoringProgress();
		AccumulateAuthoringProgress(
			Row->CharacterPath,
			AssetDataByPath.Find(NormalizedPath),
			Row->AuthoringProgress);
		for (const EPaper2DPlusCatalogCompanion Companion : {
			EPaper2DPlusCatalogCompanion::Layer,
			EPaper2DPlusCatalogCompanion::Effect,
			EPaper2DPlusCatalogCompanion::Combat })
		{
			const FSoftObjectPath CompanionPath = CatalogModelCompanionPath(Row->Entry, Companion);
			AccumulateAuthoringProgress(
				CompanionPath,
				AssetDataByPath.Find(CatalogModelNormalizePath(CompanionPath)),
				Row->AuthoringProgress);
		}

		Row->Status = DeriveStatus(*Row, Row->Completion, Row->IssueSummary);
		Row->StatusText = StatusText(Row->Status);
		Row->StatusTooltip = StatusTooltip(Row->Status);
		Rows.Add(MoveTemp(Row));
	}

	if (!SelectedCharacterPath.IsNull() && !FindRow(SelectedCharacterPath).IsValid())
	{
		SelectedCharacterPath.Reset();
	}
	RefilterRows();
}

void FCharacterCatalogEditorModel::RefilterRows()
{
	VisibleRows.Reset();
	// One pass over the groups, not one pass per row per group.
	const TMap<FString, FString> GroupLabelsByCharacter = BuildGroupLabelSearchIndex();
	for (const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow>& Row : Rows)
	{
		if (Row.IsValid() && PassesFilters(*Row, GroupLabelsByCharacter))
		{
			VisibleRows.Add(Row);
		}
	}

	// Scoped to one group, the grid must present the group's AUTHORED member order rather than Catalog
	// entry order — otherwise the rail's drag-reorder would rearrange data the designer cannot see.
	if (!Filters.Group.IsNone())
	{
		const int32 GroupIndex = FindGroupIndex(Filters.Group);
		const UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
		if (CatalogAsset && CatalogAsset->Groups.IsValidIndex(GroupIndex))
		{
			TMap<FString, int32> MemberOrder;
			const auto& Members = CatalogAsset->Groups[GroupIndex].Members;
			MemberOrder.Reserve(Members.Num());
			for (int32 MemberIndex = 0; MemberIndex < Members.Num(); ++MemberIndex)
			{
				const FString NormalizedMember =
					CatalogModelNormalizePath(Members[MemberIndex].ToSoftObjectPath());
				if (!NormalizedMember.IsEmpty())
				{
					// First occurrence wins, matching GetEntriesInGroup's duplicate handling.
					MemberOrder.FindOrAdd(NormalizedMember, MemberIndex);
				}
			}
			VisibleRows.StableSort([&MemberOrder](
				const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow>& A,
				const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow>& B)
			{
				const int32* OrderA = MemberOrder.Find(CatalogModelNormalizePath(A->CharacterPath));
				const int32* OrderB = MemberOrder.Find(CatalogModelNormalizePath(B->CharacterPath));
				return (OrderA ? *OrderA : MAX_int32) < (OrderB ? *OrderB : MAX_int32);
			});
		}
	}
	BroadcastChanged();
}

bool FCharacterCatalogEditorModel::RevealCharacterForNavigation(
	const FSoftObjectPath& CharacterPath)
{
	if (CharacterPath.IsNull() || !FindRow(CharacterPath).IsValid())
	{
		return false;
	}
	const bool bVisible = VisibleRows.ContainsByPredicate([&CharacterPath](
		const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow>& Row)
	{
		return Row.IsValid() && CatalogModelPathsEqual(Row->CharacterPath, CharacterPath);
	});
	if (!bVisible)
	{
		Filters = FPaper2DPlusCharacterCatalogFilters();
		VisibleRows = Rows;
	}
	SelectedCharacterPath = CharacterPath;
	BroadcastChanged();
	return true;
}

TMap<FString, FString> FCharacterCatalogEditorModel::BuildGroupLabelSearchIndex() const
{
	TMap<FString, FString> LabelsByCharacter;
	const UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	if (!CatalogAsset)
	{
		return LabelsByCharacter;
	}
	// Walk membership once, in the direction the search actually needs: character -> its group labels.
	for (const FPaper2DPlusCharacterCatalogGroup& Group : CatalogAsset->Groups)
	{
		const FString GroupLabels =
			TEXT(" ") + Group.GroupName.ToString() + TEXT(" ") + Group.DisplayName.ToString();
		for (const TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>& Member : Group.Members)
		{
			const FString NormalizedMember = CatalogModelNormalizePath(Member.ToSoftObjectPath());
			if (!NormalizedMember.IsEmpty())
			{
				LabelsByCharacter.FindOrAdd(NormalizedMember) += GroupLabels;
			}
		}
	}
	return LabelsByCharacter;
}

bool FCharacterCatalogEditorModel::PassesFilters(
	const FPaper2DPlusCharacterCatalogEditorRow& Row,
	const TMap<FString, FString>& GroupLabelsByCharacter) const
{
	if (!Filters.SearchText.IsEmpty())
	{
		FString SearchDocument = Row.DisplayName.ToString() + TEXT(" ") + Row.CharacterPath.ToString()
			+ TEXT(" ") + Row.Entry.Tags.ToStringSimple();
		if (const FString* GroupLabels =
			GroupLabelsByCharacter.Find(CatalogModelNormalizePath(Row.CharacterPath)))
		{
			SearchDocument += *GroupLabels;
		}
		TArray<FString> Tokens;
		Filters.SearchText.ToLower().ParseIntoArrayWS(Tokens);
		SearchDocument = SearchDocument.ToLower();
		for (const FString& Token : Tokens)
		{
			if (!SearchDocument.Contains(Token))
			{
				return false;
			}
		}
	}
	if (Filters.Tag.IsValid() && !Row.Entry.Tags.HasTag(Filters.Tag))
	{
		return false;
	}
	if (!Filters.Group.IsNone() && !IsInGroup(Filters.Group, Row.CharacterPath))
	{
		return false;
	}
	switch (Filters.Requirement)
	{
	case EPaper2DPlusCatalogRequirementFilter::Layer:
		if (!Row.Entry.Requirements.bRequireLayer) return false;
		break;
	case EPaper2DPlusCatalogRequirementFilter::Effect:
		if (!Row.Entry.Requirements.bRequireEffect) return false;
		break;
	case EPaper2DPlusCatalogRequirementFilter::Combat:
		if (!Row.Entry.Requirements.bRequireCombat) return false;
		break;
	default:
		break;
	}
	// Completion filters read AUTHORING progress, not required-companion presence: an unconfigured
	// requirement set used to make every row trivially "complete", which is the bug this replaces.
	// A row with no progress data is deliberately NOT complete, so an unmigrated project shows work.
	switch (Filters.Completion)
	{
	case EPaper2DPlusCatalogCompletionFilter::Complete:
		if (!Row.AuthoringProgress.IsFullyAuthored()) return false;
		break;
	case EPaper2DPlusCatalogCompletionFilter::Incomplete:
		if (!Row.AuthoringProgress.IsKnown() || Row.AuthoringProgress.IsFullyAuthored()) return false;
		break;
	case EPaper2DPlusCatalogCompletionFilter::Unknown:
		if (Row.AuthoringProgress.IsKnown()) return false;
		break;
	default:
		break;
	}
	const bool bError = Row.IssueSummary.HasErrors()
		|| Row.Status == EPaper2DPlusCatalogRowStatus::RequiredMissing
		|| Row.Status == EPaper2DPlusCatalogRowStatus::MissingAsset;
	const bool bWarning = Row.IssueSummary.HasWarnings()
		|| Row.Status == EPaper2DPlusCatalogRowStatus::Warning;
	if (Filters.Severity == EPaper2DPlusCatalogSeverityFilter::Errors && !bError) return false;
	if (Filters.Severity == EPaper2DPlusCatalogSeverityFilter::Warnings && !bWarning) return false;
	if (Filters.Severity == EPaper2DPlusCatalogSeverityFilter::Clean && (bError || bWarning)) return false;
	return true;
}

void FCharacterCatalogEditorModel::BroadcastChanged()
{
	SourceChanged.Broadcast();
	ModelChanged.Broadcast();
}

TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow> FCharacterCatalogEditorModel::FindRow(
	const FSoftObjectPath& CharacterPath) const
{
	const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow>* Found = Rows.FindByPredicate(
		[&CharacterPath](const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow>& Row)
	{
		return Row.IsValid() && CatalogModelPathsEqual(Row->CharacterPath, CharacterPath);
	});
	return Found ? *Found : TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow>();
}

TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow> FCharacterCatalogEditorModel::GetSelectedRow() const
{
	return FindRow(SelectedCharacterPath);
}

bool FCharacterCatalogEditorModel::SelectCharacter(const FSoftObjectPath& CharacterPath)
{
	if (!CharacterPath.IsNull() && !FindRow(CharacterPath).IsValid())
	{
		return false;
	}
	if (CatalogModelPathsEqual(SelectedCharacterPath, CharacterPath))
	{
		return true;
	}
	SelectedCharacterPath = CharacterPath;
	BroadcastChanged();
	return true;
}

void FCharacterCatalogEditorModel::SetSearchText(const FString& Text)
{
	if (Filters.SearchText == Text) return;
	Filters.SearchText = Text;
	RefilterRows();
}

void FCharacterCatalogEditorModel::SetTagFilter(FGameplayTag Tag)
{
	if (Filters.Tag == Tag) return;
	Filters.Tag = Tag;
	RefilterRows();
}

void FCharacterCatalogEditorModel::SetGroupFilter(FName Group)
{
	if (Filters.Group == Group) return;
	Filters.Group = Group;
	RefilterRows();
}

void FCharacterCatalogEditorModel::SetRequirementFilter(EPaper2DPlusCatalogRequirementFilter Filter)
{
	if (Filters.Requirement == Filter) return;
	Filters.Requirement = Filter;
	RefilterRows();
}

void FCharacterCatalogEditorModel::SetCompletionFilter(EPaper2DPlusCatalogCompletionFilter Filter)
{
	if (Filters.Completion == Filter) return;
	Filters.Completion = Filter;
	RefilterRows();
}

void FCharacterCatalogEditorModel::SetSeverityFilter(EPaper2DPlusCatalogSeverityFilter Filter)
{
	if (Filters.Severity == Filter) return;
	Filters.Severity = Filter;
	RefilterRows();
}

void FCharacterCatalogEditorModel::ClearFilters()
{
	if (Filters.IsDefault()) return;
	Filters = FPaper2DPlusCharacterCatalogFilters();
	RefilterRows();
}

int32 FCharacterCatalogEditorModel::FindEntryIndex(const FSoftObjectPath& CharacterPath) const
{
	const UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	if (!CatalogAsset) return INDEX_NONE;
	return CatalogAsset->Entries.IndexOfByPredicate([&CharacterPath](const FPaper2DPlusCharacterCatalogEntry& Entry)
	{
		return CatalogModelPathsEqual(Entry.CharacterProfile.ToSoftObjectPath(), CharacterPath);
	});
}

int32 FCharacterCatalogEditorModel::FindGroupIndex(FName GroupName) const
{
	const UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	if (!CatalogAsset || GroupName.IsNone()) return INDEX_NONE;
	return CatalogAsset->Groups.IndexOfByPredicate([GroupName](const FPaper2DPlusCharacterCatalogGroup& Group)
	{
		return Group.GroupName.IsEqual(GroupName, ENameCase::IgnoreCase);
	});
}

bool FCharacterCatalogEditorModel::IsInGroup(
	FName GroupName,
	const FSoftObjectPath& CharacterPath) const
{
	const int32 GroupIndex = FindGroupIndex(GroupName);
	const UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	if (!CatalogAsset || !CatalogAsset->Groups.IsValidIndex(GroupIndex)) return false;
	return CatalogAsset->Groups[GroupIndex].Members.ContainsByPredicate([&CharacterPath](const auto& Member)
	{
		return CatalogModelPathsEqual(Member.ToSoftObjectPath(), CharacterPath);
	});
}

void FCharacterCatalogEditorModel::RefreshAfterMutation()
{
	++MutationCount;
	bHasAuditReport = false;
	AuditReport = FPaper2DPlusCharacterCatalogAuditReport();
	if (UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get())
	{
		FPaper2DPlusEffectLibraryIndex::Get()->PublishChange(
			EPaper2DPlusEffectLibraryChangeDomain::Catalog,
			FSoftObjectPath(CatalogAsset));
	}
	RefreshFromSources();
}

void FCharacterCatalogEditorModel::RefreshAfterExternalMutation()
{
	RefreshAfterMutation();
}

bool FCharacterCatalogEditorModel::SetRequirement(
	const FSoftObjectPath& CharacterPath,
	EPaper2DPlusCatalogCompanion Companion,
	bool bRequired)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	const int32 Index = FindEntryIndex(CharacterPath);
	if (!CatalogAsset || !CatalogAsset->Entries.IsValidIndex(Index)) return false;
	bool* Value = Companion == EPaper2DPlusCatalogCompanion::Layer
		? &CatalogAsset->Entries[Index].Requirements.bRequireLayer
		: Companion == EPaper2DPlusCatalogCompanion::Effect
			? &CatalogAsset->Entries[Index].Requirements.bRequireEffect
			: &CatalogAsset->Entries[Index].Requirements.bRequireCombat;
	if (*Value == bRequired) return false;
	{
		FScopedTransaction Transaction(LOCTEXT("SetCatalogRequirement", "Set Character Catalog Requirement"));
		CatalogAsset->Modify();
		*Value = bRequired;
		CatalogAsset->MarkPackageDirty();
	}
	RefreshAfterMutation();
	return true;
}

bool FCharacterCatalogEditorModel::SetCompanionAssignment(
	const FSoftObjectPath& CharacterPath,
	EPaper2DPlusCatalogCompanion Companion,
	const FSoftObjectPath& AssetPath)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	const int32 Index = FindEntryIndex(CharacterPath);
	if (!CatalogAsset || !CatalogAsset->Entries.IsValidIndex(Index)) return false;
	FPaper2DPlusCharacterCatalogEntry& Entry = CatalogAsset->Entries[Index];
	if (CatalogModelPathsEqual(CatalogModelCompanionPath(Entry, Companion), AssetPath)) return false;
	{
		FScopedTransaction Transaction(LOCTEXT("AssignCatalogCompanion", "Assign Character Catalog Companion"));
		CatalogAsset->Modify();
		switch (Companion)
		{
		case EPaper2DPlusCatalogCompanion::Layer:
			Entry.LayerProfile = TSoftObjectPtr<UPaper2DPlusCharacterLayerAsset>(AssetPath);
			break;
		case EPaper2DPlusCatalogCompanion::Effect:
			Entry.EffectProfile = TSoftObjectPtr<UPaper2DPlusEffectProfileAsset>(AssetPath);
			break;
		default:
			Entry.CombatProfile = TSoftObjectPtr<UPaper2DPlusCombatProfileAsset>(AssetPath);
			break;
		}
		CatalogAsset->MarkPackageDirty();
	}
	RefreshAfterMutation();
	return true;
}

bool FCharacterCatalogEditorModel::SuggestCompanions(
	const FSoftObjectPath& CharacterPath,
	FPaper2DPlusCatalogCompanionSuggestion& OutResult)
{
	OutResult = FPaper2DPlusCatalogCompanionSuggestion();
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	const int32 EntryIndex = FindEntryIndex(CharacterPath);
	if (!CatalogAsset || !CatalogAsset->Entries.IsValidIndex(EntryIndex))
	{
		return false;
	}

	TArray<FAssetData> Assets;
	GatherAssets(Assets);
	const FPaper2DPlusProfileRelationshipIndex RelationshipIndex =
		FProfileRelationshipService::BuildCandidateIndex(Assets);

	// Resolve everything BEFORE opening the transaction so a run that suggests nothing opens none.
	TArray<TPair<EPaper2DPlusCatalogCompanion, FSoftObjectPath>> Fills;
	for (const EPaper2DPlusCatalogCompanion Companion : {
		EPaper2DPlusCatalogCompanion::Layer,
		EPaper2DPlusCatalogCompanion::Combat })
	{
		if (!CatalogModelCompanionPath(CatalogAsset->Entries[EntryIndex], Companion).IsNull())
		{
			// Never overwrite an authored assignment; this action only fills blanks.
			continue;
		}
		const FPaper2DPlusProfileRelationshipResolution Resolution =
			FProfileRelationshipService::SuggestCandidate(Companion, CharacterPath, RelationshipIndex);
		switch (Resolution.State)
		{
		case EPaper2DPlusProfileRelationshipState::Unique:
			Fills.Emplace(Companion, Resolution.SuggestedAssetPath);
			break;
		case EPaper2DPlusProfileRelationshipState::Ambiguous:
			OutResult.AmbiguousSlots.Add(Companion);
			break;
		case EPaper2DPlusProfileRelationshipState::LegacyUnknown:
			OutResult.LegacySlots.Add(Companion);
			break;
		default:
			OutResult.UnmatchedSlots.Add(Companion);
			break;
		}
	}

	if (Fills.IsEmpty())
	{
		return true;
	}
	{
		FScopedTransaction Transaction(LOCTEXT("SuggestCatalogCompanions", "Suggest Character Catalog Companions"));
		CatalogAsset->Modify();
		FPaper2DPlusCharacterCatalogEntry& Entry = CatalogAsset->Entries[EntryIndex];
		for (const TPair<EPaper2DPlusCatalogCompanion, FSoftObjectPath>& Fill : Fills)
		{
			if (Fill.Key == EPaper2DPlusCatalogCompanion::Layer)
			{
				Entry.LayerProfile = TSoftObjectPtr<UPaper2DPlusCharacterLayerAsset>(Fill.Value);
			}
			else
			{
				Entry.CombatProfile = TSoftObjectPtr<UPaper2DPlusCombatProfileAsset>(Fill.Value);
			}
			++OutResult.NumAssigned;
		}
		CatalogAsset->MarkPackageDirty();
	}
	RefreshAfterMutation();
	return true;
}

bool FCharacterCatalogEditorModel::SetTags(
	const FSoftObjectPath& CharacterPath,
	const FGameplayTagContainer& Tags)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	const int32 Index = FindEntryIndex(CharacterPath);
	if (!CatalogAsset || !CatalogAsset->Entries.IsValidIndex(Index)
		|| CatalogAsset->Entries[Index].Tags == Tags)
	{
		return false;
	}
	{
		FScopedTransaction Transaction(LOCTEXT("SetCatalogTags", "Set Character Catalog Tags"));
		CatalogAsset->Modify();
		CatalogAsset->Entries[Index].Tags = Tags;
		CatalogAsset->MarkPackageDirty();
	}
	RefreshAfterMutation();
	return true;
}

bool FCharacterCatalogEditorModel::SetExpectedAnimationTags(
	const FGameplayTagContainer& Tags)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	if (!CatalogAsset || CatalogAsset->ExpectedAnimationTags == Tags)
	{
		return false;
	}
	{
		FScopedTransaction Transaction(
			LOCTEXT("SetCatalogExpectedAnimationTags", "Set Catalog Expected Animation Tags"));
		CatalogAsset->Modify();
		CatalogAsset->ExpectedAnimationTags = Tags;
		CatalogAsset->MarkPackageDirty();
	}
	RefreshAfterMutation();
	return true;
}

bool FCharacterCatalogEditorModel::SetGroupAdditionalExpectedAnimationTags(
	FName GroupName,
	const FGameplayTagContainer& Tags)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	const int32 GroupIndex = FindGroupIndex(GroupName);
	if (!CatalogAsset || !CatalogAsset->Groups.IsValidIndex(GroupIndex)
		|| CatalogAsset->Groups[GroupIndex].AdditionalExpectedAnimationTags == Tags)
	{
		return false;
	}
	{
		FScopedTransaction Transaction(
			LOCTEXT(
				"SetCatalogGroupAdditionalExpectedAnimationTags",
				"Set Catalog Group Expected Animation Tags"));
		CatalogAsset->Modify();
		CatalogAsset->Groups[GroupIndex].AdditionalExpectedAnimationTags = Tags;
		CatalogAsset->MarkPackageDirty();
	}
	RefreshAfterMutation();
	return true;
}

bool FCharacterCatalogEditorModel::AddGroup(FName GroupName, const FText& DisplayName, FText& OutError)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	if (!CatalogAsset || GroupName.IsNone())
	{
		OutError = LOCTEXT("GroupNameRequired", "Enter a non-empty internal group name.");
		return false;
	}
	if (FindGroupIndex(GroupName) != INDEX_NONE)
	{
		OutError = LOCTEXT("GroupNameUnique", "Group names must be unique (case-insensitive).");
		return false;
	}
	{
		FScopedTransaction Transaction(LOCTEXT("AddCatalogGroup", "Add Character Catalog Group"));
		CatalogAsset->Modify();
		FPaper2DPlusCharacterCatalogGroup& Group = CatalogAsset->Groups.AddDefaulted_GetRef();
		Group.GroupName = GroupName;
		Group.DisplayName = DisplayName.IsEmpty() ? FText::FromName(GroupName) : DisplayName;
		CatalogAsset->MarkPackageDirty();
	}
	RefreshAfterMutation();
	return true;
}

bool FCharacterCatalogEditorModel::RemoveGroup(FName GroupName)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	const int32 Index = FindGroupIndex(GroupName);
	if (!CatalogAsset || !CatalogAsset->Groups.IsValidIndex(Index)) return false;
	{
		FScopedTransaction Transaction(LOCTEXT("RemoveCatalogGroup", "Remove Character Catalog Group"));
		CatalogAsset->Modify();
		CatalogAsset->Groups.RemoveAt(Index);
		CatalogAsset->MarkPackageDirty();
	}
	RefreshAfterMutation();
	return true;
}

bool FCharacterCatalogEditorModel::RenameGroup(
	FName OldName,
	FName NewName,
	const FText& DisplayName,
	FText& OutError)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	const int32 Index = FindGroupIndex(OldName);
	if (!CatalogAsset || !CatalogAsset->Groups.IsValidIndex(Index) || NewName.IsNone())
	{
		OutError = LOCTEXT("RenameGroupMissing", "The group or new group name is invalid.");
		return false;
	}
	const int32 ExistingIndex = FindGroupIndex(NewName);
	if (ExistingIndex != INDEX_NONE && ExistingIndex != Index)
	{
		OutError = LOCTEXT("RenameGroupUnique", "Another group already uses that name.");
		return false;
	}
	FPaper2DPlusCharacterCatalogGroup& Group = CatalogAsset->Groups[Index];
	if (Group.GroupName == NewName && Group.DisplayName.EqualTo(DisplayName)) return false;
	{
		FScopedTransaction Transaction(LOCTEXT("RenameCatalogGroup", "Rename Character Catalog Group"));
		CatalogAsset->Modify();
		Group.GroupName = NewName;
		Group.DisplayName = DisplayName.IsEmpty() ? FText::FromName(NewName) : DisplayName;
		CatalogAsset->MarkPackageDirty();
	}
	RefreshAfterMutation();
	return true;
}

bool FCharacterCatalogEditorModel::AddGroupMember(FName GroupName, const FSoftObjectPath& CharacterPath)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	const int32 GroupIndex = FindGroupIndex(GroupName);
	if (!CatalogAsset || !CatalogAsset->Groups.IsValidIndex(GroupIndex)
		|| FindEntryIndex(CharacterPath) == INDEX_NONE || IsInGroup(GroupName, CharacterPath))
	{
		return false;
	}
	{
		FScopedTransaction Transaction(LOCTEXT("AddCatalogGroupMember", "Add Character Catalog Group Member"));
		CatalogAsset->Modify();
		CatalogAsset->Groups[GroupIndex].Members.Add(
			TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>(CharacterPath));
		CatalogAsset->MarkPackageDirty();
	}
	RefreshAfterMutation();
	return true;
}

int32 FCharacterCatalogEditorModel::AddGroupMembers(
	FName GroupName,
	const TArray<FSoftObjectPath>& CharacterPaths)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	const int32 GroupIndex = FindGroupIndex(GroupName);
	if (!CatalogAsset || !CatalogAsset->Groups.IsValidIndex(GroupIndex))
	{
		return 0;
	}
	TArray<FSoftObjectPath> ToAdd;
	TSet<FString> Pending;
	for (const FSoftObjectPath& CharacterPath : CharacterPaths)
	{
		const FString NormalizedPath = CatalogModelNormalizePath(CharacterPath);
		if (CharacterPath.IsNull() || NormalizedPath.IsEmpty() || Pending.Contains(NormalizedPath)
			|| FindEntryIndex(CharacterPath) == INDEX_NONE
			|| IsInGroup(GroupName, CharacterPath))
		{
			continue;
		}
		Pending.Add(NormalizedPath);
		ToAdd.Add(CharacterPath);
	}
	if (ToAdd.IsEmpty())
	{
		return 0;
	}
	{
		FScopedTransaction Transaction(LOCTEXT("AddCatalogGroupMembers", "Add Characters to Group"));
		CatalogAsset->Modify();
		for (const FSoftObjectPath& CharacterPath : ToAdd)
		{
			CatalogAsset->Groups[GroupIndex].Members.Add(
				TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>(CharacterPath));
		}
		CatalogAsset->MarkPackageDirty();
	}
	RefreshAfterMutation();
	return ToAdd.Num();
}

bool FCharacterCatalogEditorModel::RemoveGroupMember(FName GroupName, const FSoftObjectPath& CharacterPath)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	const int32 GroupIndex = FindGroupIndex(GroupName);
	if (!CatalogAsset || !CatalogAsset->Groups.IsValidIndex(GroupIndex)) return false;
	auto& Members = CatalogAsset->Groups[GroupIndex].Members;
	const int32 MemberIndex = Members.IndexOfByPredicate([&CharacterPath](const auto& Member)
	{
		return CatalogModelPathsEqual(Member.ToSoftObjectPath(), CharacterPath);
	});
	if (!Members.IsValidIndex(MemberIndex)) return false;
	{
		FScopedTransaction Transaction(LOCTEXT("RemoveCatalogGroupMember", "Remove Character Catalog Group Member"));
		CatalogAsset->Modify();
		Members.RemoveAt(MemberIndex);
		CatalogAsset->MarkPackageDirty();
	}
	RefreshAfterMutation();
	return true;
}

bool FCharacterCatalogEditorModel::MoveGroupMember(
	FName GroupName,
	const FSoftObjectPath& CharacterPath,
	int32 Direction)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	const int32 GroupIndex = FindGroupIndex(GroupName);
	if (!CatalogAsset || !CatalogAsset->Groups.IsValidIndex(GroupIndex) || Direction == 0) return false;
	auto& Members = CatalogAsset->Groups[GroupIndex].Members;
	const int32 MemberIndex = Members.IndexOfByPredicate([&CharacterPath](const auto& Member)
	{
		return CatalogModelPathsEqual(Member.ToSoftObjectPath(), CharacterPath);
	});
	const int32 TargetIndex = MemberIndex + FMath::Clamp(Direction, -1, 1);
	if (!Members.IsValidIndex(MemberIndex) || !Members.IsValidIndex(TargetIndex)) return false;
	{
		FScopedTransaction Transaction(LOCTEXT("MoveCatalogGroupMember", "Reorder Character Catalog Group Member"));
		CatalogAsset->Modify();
		Members.Swap(MemberIndex, TargetIndex);
		CatalogAsset->MarkPackageDirty();
	}
	RefreshAfterMutation();
	return true;
}

bool FCharacterCatalogEditorModel::MoveGroupMemberToIndex(
	FName GroupName,
	const FSoftObjectPath& CharacterPath,
	int32 TargetIndex)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	const int32 GroupIndex = FindGroupIndex(GroupName);
	if (!CatalogAsset || !CatalogAsset->Groups.IsValidIndex(GroupIndex)) return false;
	auto& Members = CatalogAsset->Groups[GroupIndex].Members;
	const int32 FromIndex = Members.IndexOfByPredicate([&CharacterPath](const auto& Member)
	{
		return CatalogModelPathsEqual(Member.ToSoftObjectPath(), CharacterPath);
	});
	if (!Members.IsValidIndex(FromIndex)) return false;

	// Insert-before semantics: TargetIndex is a GAP, so Num() means append. Removing first shifts every
	// later gap down by one, which is why the > FromIndex case decrements.
	TargetIndex = FMath::Clamp(TargetIndex, 0, Members.Num());
	const int32 InsertAt = FMath::Clamp(
		TargetIndex > FromIndex ? TargetIndex - 1 : TargetIndex,
		0,
		Members.Num() - 1);
	if (InsertAt == FromIndex) return false;
	{
		FScopedTransaction Transaction(LOCTEXT("MoveCatalogGroupMember", "Reorder Character Catalog Group Member"));
		CatalogAsset->Modify();
		const TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> Moved = Members[FromIndex];
		Members.RemoveAt(FromIndex);
		Members.Insert(Moved, InsertAt);
		CatalogAsset->MarkPackageDirty();
	}
	RefreshAfterMutation();
	return true;
}

bool FCharacterCatalogEditorModel::MoveGroupMemberRelativeTo(
	FName GroupName,
	const FSoftObjectPath& CharacterPath,
	const FSoftObjectPath& AnchorPath,
	bool bInsertAfter)
{
	const UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	const int32 GroupIndex = FindGroupIndex(GroupName);
	if (!CatalogAsset || !CatalogAsset->Groups.IsValidIndex(GroupIndex)) return false;

	// Resolve the gap from the ANCHOR's own authored position. The caller knows which character was
	// dropped on, never where that character sits in Members — and under an active filter the visible
	// order is a subset, so any index the view could supply would address the wrong member.
	const auto& Members = CatalogAsset->Groups[GroupIndex].Members;
	const int32 AnchorIndex = Members.IndexOfByPredicate([&AnchorPath](const auto& Member)
	{
		return CatalogModelPathsEqual(Member.ToSoftObjectPath(), AnchorPath);
	});
	if (!Members.IsValidIndex(AnchorIndex)) return false;

	return MoveGroupMemberToIndex(
		GroupName,
		CharacterPath,
		bInsertAfter ? AnchorIndex + 1 : AnchorIndex);
}

bool FCharacterCatalogEditorModel::MoveGroupToIndex(FName GroupName, int32 TargetIndex)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	const int32 FromIndex = FindGroupIndex(GroupName);
	if (!CatalogAsset || !CatalogAsset->Groups.IsValidIndex(FromIndex)) return false;
	TargetIndex = FMath::Clamp(TargetIndex, 0, CatalogAsset->Groups.Num());
	const int32 InsertAt = FMath::Clamp(
		TargetIndex > FromIndex ? TargetIndex - 1 : TargetIndex,
		0,
		CatalogAsset->Groups.Num() - 1);
	if (InsertAt == FromIndex) return false;
	{
		FScopedTransaction Transaction(LOCTEXT("MoveCatalogGroup", "Reorder Character Catalog Group"));
		CatalogAsset->Modify();
		const FPaper2DPlusCharacterCatalogGroup Moved = CatalogAsset->Groups[FromIndex];
		CatalogAsset->Groups.RemoveAt(FromIndex);
		CatalogAsset->Groups.Insert(Moved, InsertAt);
		CatalogAsset->MarkPackageDirty();
	}
	RefreshAfterMutation();
	return true;
}

bool FCharacterCatalogEditorModel::MoveGroupRelativeTo(
	FName GroupName,
	FName AnchorGroupName,
	bool bInsertAfter)
{
	// The rail omits unnamed groups, so its row order is a projection of Groups, not Groups itself.
	// Resolving the anchor's authored index here is what makes a rail drop land where the designer
	// aimed it regardless of how many unnamed rows precede it.
	const int32 AnchorIndex = FindGroupIndex(AnchorGroupName);
	const UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	if (!CatalogAsset || !CatalogAsset->Groups.IsValidIndex(AnchorIndex)) return false;

	return MoveGroupToIndex(GroupName, bInsertAfter ? AnchorIndex + 1 : AnchorIndex);
}

FName FCharacterCatalogEditorModel::DeriveUniqueGroupName(const FText& DisplayLabel) const
{
	FString Slug;
	const FString Source = DisplayLabel.ToString();
	for (const TCHAR Character : Source)
	{
		if (FChar::IsAlnum(Character))
		{
			Slug.AppendChar(Character);
		}
		else if (FChar::IsWhitespace(Character) || Character == TEXT('-') || Character == TEXT('_'))
		{
			// Collapse runs of separators so "Boss  Fight" and "Boss-Fight" agree on one slug.
			if (!Slug.IsEmpty() && !Slug.EndsWith(TEXT("_")))
			{
				Slug.AppendChar(TEXT('_'));
			}
		}
	}
	Slug.RemoveFromEnd(TEXT("_"));
	if (Slug.IsEmpty())
	{
		Slug = TEXT("Group");
	}
	// Match the case-insensitive uniqueness AddGroup already enforces, so the derived name never
	// collides with an existing group and the caller never has to retry.
	FName Candidate(*Slug);
	int32 Suffix = 2;
	while (FindGroupIndex(Candidate) != INDEX_NONE)
	{
		Candidate = FName(*FString::Printf(TEXT("%s_%d"), *Slug, Suffix++));
	}
	return Candidate;
}

bool FCharacterCatalogEditorModel::MoveGroup(FName GroupName, int32 Direction)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	const int32 Index = FindGroupIndex(GroupName);
	const int32 Target = Index + FMath::Clamp(Direction, -1, 1);
	if (!CatalogAsset || Direction == 0 || !CatalogAsset->Groups.IsValidIndex(Index)
		|| !CatalogAsset->Groups.IsValidIndex(Target))
	{
		return false;
	}
	{
		FScopedTransaction Transaction(LOCTEXT("MoveCatalogGroup", "Reorder Character Catalog Group"));
		CatalogAsset->Modify();
		CatalogAsset->Groups.Swap(Index, Target);
		CatalogAsset->MarkPackageDirty();
	}
	RefreshAfterMutation();
	return true;
}

bool FCharacterCatalogEditorModel::IsCharacterInCatalog(const FSoftObjectPath& CharacterPath) const
{
	return FindEntryIndex(CharacterPath) != INDEX_NONE;
}

int32 FCharacterCatalogEditorModel::AddCharacters(const TArray<FSoftObjectPath>& CharacterPaths)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	if (!CatalogAsset)
	{
		return 0;
	}
	TArray<FSoftObjectPath> ToAdd;
	TSet<FString> PendingPaths;
	for (const FSoftObjectPath& CharacterPath : CharacterPaths)
	{
		const FString NormalizedPath = CatalogModelNormalizePath(CharacterPath);
		if (CharacterPath.IsNull() || NormalizedPath.IsEmpty()
			|| PendingPaths.Contains(NormalizedPath)
			|| FindEntryIndex(CharacterPath) != INDEX_NONE)
		{
			continue;
		}
		PendingPaths.Add(NormalizedPath);
		ToAdd.Add(CharacterPath);
	}
	if (ToAdd.IsEmpty())
	{
		return 0;
	}
	{
		FScopedTransaction Transaction(LOCTEXT("AddCatalogCharacters", "Add Characters to Catalog"));
		CatalogAsset->Modify();
		for (const FSoftObjectPath& CharacterPath : ToAdd)
		{
			FPaper2DPlusCharacterCatalogEntry& Entry = CatalogAsset->Entries.AddDefaulted_GetRef();
			Entry.CharacterProfile = TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>(CharacterPath);
		}
		CatalogAsset->MarkPackageDirty();
	}
	SelectedCharacterPath = ToAdd.Last();
	RefreshAfterMutation();
	return ToAdd.Num();
}

bool FCharacterCatalogEditorModel::RemoveCharacters(const TArray<FSoftObjectPath>& CharacterPaths)
{
	UPaper2DPlusCharacterCatalogAsset* CatalogAsset = Catalog.Get();
	if (!CatalogAsset)
	{
		return false;
	}
	TSet<FString> TargetPaths;
	int32 FirstVisibleIndex = INDEX_NONE;
	for (const FSoftObjectPath& CharacterPath : CharacterPaths)
	{
		const FString NormalizedPath = CatalogModelNormalizePath(CharacterPath);
		if (CharacterPath.IsNull() || NormalizedPath.IsEmpty()
			|| FindEntryIndex(CharacterPath) == INDEX_NONE)
		{
			continue;
		}
		TargetPaths.Add(NormalizedPath);
		const int32 VisibleIndex = VisibleRows.IndexOfByPredicate([&CharacterPath](
			const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow>& Candidate)
		{
			return Candidate.IsValid() && CatalogModelPathsEqual(Candidate->CharacterPath, CharacterPath);
		});
		if (VisibleIndex != INDEX_NONE
			&& (FirstVisibleIndex == INDEX_NONE || VisibleIndex < FirstVisibleIndex))
		{
			FirstVisibleIndex = VisibleIndex;
		}
	}
	if (TargetPaths.IsEmpty())
	{
		return false;
	}
	const auto IsTarget = [&TargetPaths](const FSoftObjectPath& CharacterPath)
	{
		return TargetPaths.Contains(CatalogModelNormalizePath(CharacterPath));
	};

	// The nearest surviving visible neighbor after the first removed row keeps keyboard flow alive.
	FSoftObjectPath ReplacementSelection;
	if (!SelectedCharacterPath.IsNull() && !IsTarget(SelectedCharacterPath))
	{
		ReplacementSelection = SelectedCharacterPath;
	}
	else if (FirstVisibleIndex != INDEX_NONE)
	{
		for (int32 Offset = 0; Offset < VisibleRows.Num(); ++Offset)
		{
			const int32 Forward = FirstVisibleIndex + Offset;
			const int32 Backward = FirstVisibleIndex - Offset;
			if (VisibleRows.IsValidIndex(Forward) && VisibleRows[Forward].IsValid()
				&& !IsTarget(VisibleRows[Forward]->CharacterPath))
			{
				ReplacementSelection = VisibleRows[Forward]->CharacterPath;
				break;
			}
			if (VisibleRows.IsValidIndex(Backward) && VisibleRows[Backward].IsValid()
				&& !IsTarget(VisibleRows[Backward]->CharacterPath))
			{
				ReplacementSelection = VisibleRows[Backward]->CharacterPath;
				break;
			}
		}
	}

	{
		FScopedTransaction Transaction(LOCTEXT("RemoveCatalogCharacters", "Remove Characters from Catalog"));
		CatalogAsset->Modify();
		CatalogAsset->Entries.RemoveAll([&IsTarget](const FPaper2DPlusCharacterCatalogEntry& Entry)
		{
			return IsTarget(Entry.CharacterProfile.ToSoftObjectPath());
		});
		for (FPaper2DPlusCharacterCatalogGroup& Group : CatalogAsset->Groups)
		{
			Group.Members.RemoveAll([&IsTarget](
				const TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>& Member)
			{
				return IsTarget(Member.ToSoftObjectPath());
			});
		}
		CatalogAsset->MarkPackageDirty();
	}

	SelectedCharacterPath = ReplacementSelection;
	RefreshAfterMutation();
	return true;
}

bool FCharacterCatalogEditorModel::RemoveCharacter(const FSoftObjectPath& CharacterPath)
{
	return RemoveCharacters({ CharacterPath });
}

FSoftObjectPath FCharacterCatalogEditorModel::GetCompanionPath(
	const FSoftObjectPath& CharacterPath,
	EPaper2DPlusCatalogCompanion Companion) const
{
	if (const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow> Row = FindRow(CharacterPath))
	{
		return CatalogModelCompanionPath(Row->Entry, Companion);
	}
	return FSoftObjectPath();
}

bool FCharacterCatalogEditorModel::OpenPath(const FSoftObjectPath& Path, FText& OutError) const
{
	if (Path.IsNull())
	{
		OutError = LOCTEXT("OpenMissingPath", "No asset is assigned.");
		return false;
	}
	if (!FApp::CanEverRender())
	{
		OutError = LOCTEXT("OpenHeadless", "Asset opening is unavailable in a headless process.");
		return false;
	}
	if (OpenAssetAction)
	{
		return OpenAssetAction(Path, OutError);
	}
	return FProfileRelationshipService::OpenRelatedAsset(Path, OutError);
}

bool FCharacterCatalogEditorModel::OpenCharacter(
	const FSoftObjectPath& CharacterPath,
	FText& OutError) const
{
	return OpenPath(CharacterPath, OutError);
}

bool FCharacterCatalogEditorModel::OpenCompanion(
	const FSoftObjectPath& CharacterPath,
	EPaper2DPlusCatalogCompanion Companion,
	FText& OutError) const
{
	return OpenPath(GetCompanionPath(CharacterPath, Companion), OutError);
}

bool FCharacterCatalogEditorModel::OpenIssueTarget(
	const FPaper2DPlusValidationIssue& Issue,
	FText& OutError) const
{
	if (Issue.ToolTarget.IsSet() && Issue.ToolTarget->ToolId == TEXT("Paper2DPlusSettings"))
	{
		if (OpenProjectSettings())
		{
			OutError = FText::GetEmpty();
			return true;
		}
		OutError = LOCTEXT("WarningSettingsUnavailable", "Paper2DPlus Project Settings could not be opened.");
		return false;
	}
	if (Issue.ToolTarget.IsSet() && Issue.ToolTarget->ToolId == TEXT("AssetManagerSettings"))
	{
		if (OpenSettingsAction)
		{
			if (OpenSettingsAction(TEXT("AssetManager")))
			{
				OutError = FText::GetEmpty();
				return true;
			}
			OutError = LOCTEXT("WarningAssetManagerSettingsUnavailable", "Asset Manager Project Settings could not be opened.");
			return false;
		}
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>(TEXT("Settings"));
		if (!SettingsModule)
		{
			SettingsModule = &FModuleManager::LoadModuleChecked<ISettingsModule>(TEXT("Settings"));
		}
		if (SettingsModule)
		{
			SettingsModule->ShowViewer(TEXT("Project"), TEXT("Engine"), TEXT("AssetManager"));
			OutError = FText::GetEmpty();
			return true;
		}
		OutError = LOCTEXT("WarningAssetManagerSettingsUnavailable", "Asset Manager Project Settings could not be opened.");
		return false;
	}
	const FSoftObjectPath TargetPath = Issue.GetNavigationAssetPath();
	if (!TargetPath.IsValid())
	{
		OutError = LOCTEXT("WarningTargetMissing", "This warning has no asset target to open.");
		return false;
	}
	if (Catalog.IsValid() && CatalogModelPathsEqual(TargetPath, FSoftObjectPath(Catalog.Get())))
	{
		OutError = FText::GetEmpty();
		return true;
	}
	return OpenPath(TargetPath, OutError);
}

UObject* FCharacterCatalogEditorModel::CreateCompanion(
	const FSoftObjectPath& CharacterPath,
	EPaper2DPlusCatalogCompanion Companion,
	const FString& DestinationPackagePath,
	const FString& DesiredAssetName,
	FText& OutError)
{
	if (FindEntryIndex(CharacterPath) == INDEX_NONE)
	{
		OutError = LOCTEXT("CreateNeedsSavedRow", "This character is not in the Catalog, so a companion cannot be assigned to it.");
		return nullptr;
	}
	UObject* Created = CreateAssetAction
		? CreateAssetAction(Companion, CharacterPath, DestinationPackagePath, DesiredAssetName, OutError)
		: FProfileRelationshipService::CreateRelatedAsset(
			Companion, CharacterPath, DestinationPackagePath, DesiredAssetName, OutError);
	if (!Created)
	{
		return nullptr;
	}
	SetCompanionAssignment(CharacterPath, Companion, FSoftObjectPath(Created));
	return Created;
}

bool FCharacterCatalogEditorModel::ActivateIssue(const FPaper2DPlusValidationIssue& Issue)
{
	FSoftObjectPath CharacterPath = Issue.CharacterPath;
	if (CharacterPath.IsNull() && Issue.ToolTarget.IsSet()
		&& !Issue.ToolTarget->ItemIdentity.IsEmpty())
	{
		// ItemIdentity is a free-form identity string, not necessarily an object path — it can be a
		// group or display name such as "Playable". FSoftObjectPath's CONSTRUCTOR ensures on a short
		// package name ("Cannot create SoftObjectPath with short package name"), so the IsValid()
		// check below is too late to help: the ensure has already fired and, under automation, an
		// ensure fails the whole test. Reject anything that is not path-shaped before constructing.
		const FString& ItemIdentity = Issue.ToolTarget->ItemIdentity;
		if (!FPackageName::IsShortPackageName(ItemIdentity))
		{
			const FSoftObjectPath CandidatePath(ItemIdentity);
			if (CandidatePath.IsValid())
			{
				CharacterPath = CandidatePath;
			}
		}
	}
	LastNavigationField = Issue.Field;
	LastNavigationTab = Issue.ToolTarget.IsSet() ? Issue.ToolTarget->TabId : NAME_None;
	if (!CharacterPath.IsNull() && !RevealCharacterForNavigation(CharacterPath))
	{
		return LastNavigationTab != NAME_None || LastNavigationField != NAME_None;
	}
	if (CharacterPath.IsNull())
	{
		BroadcastChanged();
	}
	return !CharacterPath.IsNull() || LastNavigationTab != NAME_None || LastNavigationField != NAME_None;
}

int32 FCharacterCatalogEditorModel::GetSourceDelegateCountForTests() const
{
	int32 Count = 0;
	Count += AssetAddedHandle.IsValid() ? 1 : 0;
	Count += AssetRemovedHandle.IsValid() ? 1 : 0;
	Count += AssetRenamedHandle.IsValid() ? 1 : 0;
	Count += AssetUpdatedHandle.IsValid() ? 1 : 0;
	Count += SettingsChangedHandle.IsValid() ? 1 : 0;
	return Count;
}

void FCharacterCatalogEditorModel::GetStatusPresentation(
	EPaper2DPlusCatalogRowStatus Status,
	FText& OutText,
	FText& OutTooltip)
{
	OutText = StatusText(Status);
	OutTooltip = StatusTooltip(Status);
}

FName FCharacterCatalogEditorModel::GetSourceType() const
{
	return TEXT("CharacterCatalog");
}

FString FCharacterCatalogEditorModel::GetLogicalCatalogScope() const
{
	return FString::Printf(TEXT("CharacterCatalog:%s"), Catalog.IsValid() ? *Catalog->GetPathName() : TEXT("None"));
}

void FCharacterCatalogEditorModel::GetItems(TArray<FProfilePickerItem>& OutItems) const
{
	OutItems.Reset();
	for (int32 Index = 0; Index < VisibleRows.Num(); ++Index)
	{
		const TSharedPtr<FPaper2DPlusCharacterCatalogEditorRow>& Row = VisibleRows[Index];
		if (!Row.IsValid()) continue;
		FProfilePickerItem Item;
		Item.Identity.SourceType = GetSourceType();
		Item.Identity.ObjectPath = Row->CharacterPath;
		Item.Label = Row->DisplayName;
		Item.SecondaryText = Row->StatusText;
		Item.SearchTags = Row->Entry.Tags;
		Item.CanonicalOrder = Index;
		Item.Aliases.Add(Row->CharacterPath.ToString());
		if (!Filters.Group.IsNone())
		{
			Item.Group = Filters.Group.ToString();
		}
		OutItems.Add(MoveTemp(Item));
	}
}

FProfileItemIdentity FCharacterCatalogEditorModel::GetSelectedIdentity() const
{
	FProfileItemIdentity Identity;
	Identity.SourceType = GetSourceType();
	Identity.ObjectPath = SelectedCharacterPath;
	return Identity;
}

bool FCharacterCatalogEditorModel::SelectItem(const FProfileItemIdentity& Identity)
{
	return Identity.SourceType == GetSourceType() && SelectCharacter(Identity.ObjectPath);
}

#undef LOCTEXT_NAMESPACE
