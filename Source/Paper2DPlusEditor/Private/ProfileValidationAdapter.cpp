// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfileValidationAdapter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "CharacterCatalogAuditService.h"
#include "CharacterCatalogSourceTypes.h"
#include "CharacterLayerAssetEditorToolkit.h"
#include "CharacterLayerBakeCore.h"
#include "CharacterLayerBakeCoordinator.h"
#include "FrameCueTrackLayoutDiagnostics.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueMigrationService.h"
#include "Internationalization/Text.h"
#include "Misc/PackageName.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusAppearanceResolver.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusSettings.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusProfileValidationAdapter"

namespace
{
	thread_local const Paper2DPlusProfileValidationAdapter::FCatalogValidationContext*
		GInjectedCatalogValidationContext = nullptr;

	const Paper2DPlusProfileValidationAdapter::FCatalogValidationContext*
	GetInjectedCatalogValidationContext()
	{
		return GInjectedCatalogValidationContext
			&& GInjectedCatalogValidationContext->IsUsable()
			? GInjectedCatalogValidationContext
			: nullptr;
	}

	FPaper2DPlusValidationIssue MakeIssue(
		const UObject& Asset,
		EPaper2DPlusValidationSeverity Severity,
		FName Code,
		FName Scope,
		const FString& ItemIdentity,
		FName Field,
		const FText& Message,
		const FText& Remediation,
		const FString& StableDiscriminator = FString())
	{
		FPaper2DPlusValidationIssue Result;
		Result.Severity = Severity;
		Result.Code = Code;
		Result.StableDiscriminator = StableDiscriminator;
		Result.AssetPath = FSoftObjectPath(Asset.GetPathName());
		Result.AssetType = Asset.GetClass()->GetFName();
		Result.Scope = Scope;
		Result.ItemIdentity = ItemIdentity;
		Result.Field = Field;
		Result.Message = Message;
		Result.Remediation = Remediation;
		return Result;
	}

	FString StableTextDiscriminator(const FText& Text)
	{
		FString Serialized;
		FTextStringHelper::WriteToBuffer(
			Serialized, Text, /*bRequiresQuotes=*/false, /*bStripPackageNamespace=*/true);
		return Serialized;
	}

	FName MakeFieldCode(const TCHAR* Prefix, FName Field)
	{
		return FName(*FString::Printf(
			TEXT("%s.%s"),
			Prefix,
			Field.IsNone() ? TEXT("NativeIssue") : *Field.ToString()));
	}

	EPaper2DPlusValidationSeverity CharacterSeverity(ECharacterProfileValidationSeverity Severity)
	{
		switch (Severity)
		{
		case ECharacterProfileValidationSeverity::Error:
			return EPaper2DPlusValidationSeverity::Error;
		case ECharacterProfileValidationSeverity::Warning:
			return EPaper2DPlusValidationSeverity::Warning;
		default:
			return EPaper2DPlusValidationSeverity::Info;
		}
	}

	EPaper2DPlusValidationSeverity LayerSeverity(ECharacterLayerValidationSeverity Severity)
	{
		switch (Severity)
		{
		case ECharacterLayerValidationSeverity::Error:
			return EPaper2DPlusValidationSeverity::Error;
		case ECharacterLayerValidationSeverity::Warning:
			return EPaper2DPlusValidationSeverity::Warning;
		default:
			return EPaper2DPlusValidationSeverity::Info;
		}
	}

	EPaper2DPlusValidationSeverity EffectSeverity(EPaper2DPlusEffectProfileValidationSeverity Severity)
	{
		switch (Severity)
		{
		case EPaper2DPlusEffectProfileValidationSeverity::Error:
			return EPaper2DPlusValidationSeverity::Error;
		case EPaper2DPlusEffectProfileValidationSeverity::Warning:
			return EPaper2DPlusValidationSeverity::Warning;
		default:
			return EPaper2DPlusValidationSeverity::Info;
		}
	}

	EPaper2DPlusValidationSeverity CombatSeverity(EPaper2DPlusCombatValidationSeverity Severity)
	{
		switch (Severity)
		{
		case EPaper2DPlusCombatValidationSeverity::Error:
			return EPaper2DPlusValidationSeverity::Error;
		case EPaper2DPlusCombatValidationSeverity::Warning:
			return EPaper2DPlusValidationSeverity::Warning;
		default:
			return EPaper2DPlusValidationSeverity::Info;
		}
	}

	FName MigrationIssueCode(EPaper2DPlusFrameCueMigrationIssueKind Kind)
	{
		const TCHAR* Suffix = TEXT("Unknown");
		switch (Kind)
		{
		case EPaper2DPlusFrameCueMigrationIssueKind::UnsupportedExecutableEvent: Suffix = TEXT("UnsupportedExecutableEvent"); break;
		case EPaper2DPlusFrameCueMigrationIssueKind::ConvertibleBuiltInEvent: Suffix = TEXT("ConvertibleBuiltInEvent"); break;
		case EPaper2DPlusFrameCueMigrationIssueKind::MigratedCueNeedsReceiverAcknowledgement: Suffix = TEXT("ReceiverAcknowledgement"); break;
		case EPaper2DPlusFrameCueMigrationIssueKind::NullLegacyEvent: Suffix = TEXT("NullLegacyEvent"); break;
		case EPaper2DPlusFrameCueMigrationIssueKind::InvalidLegacyAnchor: Suffix = TEXT("InvalidLegacyAnchor"); break;
		case EPaper2DPlusFrameCueMigrationIssueKind::InvalidLegacyRange: Suffix = TEXT("InvalidLegacyRange"); break;
		case EPaper2DPlusFrameCueMigrationIssueKind::InvalidLegacyNetworkPolicy: Suffix = TEXT("InvalidLegacyNetworkPolicy"); break;
		case EPaper2DPlusFrameCueMigrationIssueKind::NullCue: Suffix = TEXT("NullCue"); break;
		case EPaper2DPlusFrameCueMigrationIssueKind::InvalidCueAnchor: Suffix = TEXT("InvalidCueAnchor"); break;
		case EPaper2DPlusFrameCueMigrationIssueKind::InvalidCueRange: Suffix = TEXT("InvalidCueRange"); break;
		case EPaper2DPlusFrameCueMigrationIssueKind::InvalidCueNetworkPolicy: Suffix = TEXT("InvalidCueNetworkPolicy"); break;
		default: break;
		}
		return FName(*FString::Printf(TEXT("Paper2DPlus.FrameCueMigration.%s"), Suffix));
	}

	/**
	 * What to actually DO about one migration issue.
	 *
	 * A legacy Frame Event row has no conversion target any more, so telling a designer to "resave" it
	 * would be a lie that costs them a save cycle. Only a Cue that was already migrated in an earlier
	 * release has an acknowledgement to give.
	 */
	FText MigrationIssueRemediation(EPaper2DPlusFrameCueMigrationIssueKind Kind)
	{
		switch (Kind)
		{
		case EPaper2DPlusFrameCueMigrationIssueKind::ConvertibleBuiltInEvent:
		case EPaper2DPlusFrameCueMigrationIssueKind::UnsupportedExecutableEvent:
			return LOCTEXT(
				"LegacyFrameEventRemediation",
				"Re-author this legacy Frame Event row as a Frame Cue Type placement, then remove the legacy row. Nothing converts it automatically.");
		case EPaper2DPlusFrameCueMigrationIssueKind::MigratedCueNeedsReceiverAcknowledgement:
			return LOCTEXT(
				"CueMigrationRemediation",
				"Confirm the game Blueprint receiver wiring this migrated Cue needs, then acknowledge it on the placement.");
		default:
			return LOCTEXT(
				"BlockingCueMigrationRemediation",
				"Replace or repair this legacy/invalid Cue payload before packaging.");
		}
	}

	void ProjectFrameCueMigration(
		const UObject& Asset,
		const FPaper2DPlusFrameCueMigrationReport& Report,
		TArray<FPaper2DPlusValidationIssue>& OutIssues)
	{
		for (const FPaper2DPlusFrameCueMigrationIssue& Source : Report.Issues)
		{
			FPaper2DPlusValidationIssue Issue = MakeIssue(
				Asset,
				Source.Disposition == EPaper2DPlusFrameCueMigrationDisposition::Blocking
					? EPaper2DPlusValidationSeverity::Error
					: EPaper2DPlusValidationSeverity::Warning,
				MigrationIssueCode(Source.Kind),
				TEXT("FrameCueMigration"),
				Source.SourcePath,
				TEXT("FrameCues"),
				FText::FromString(Source.Message),
				MigrationIssueRemediation(Source.Kind));
			Issue.StableKey = Source.GetStableKey();
			OutIssues.Add(MoveTemp(Issue));
		}
	}

	void ProjectFrameCueTrackLayoutDiagnostics(
		const UObject& Asset,
		const TArray<Paper2DPlusFrameCueTrackLayoutDiagnostics::FIssue>& SourceIssues,
		TArray<FPaper2DPlusValidationIssue>& OutIssues)
	{
		for (const Paper2DPlusFrameCueTrackLayoutDiagnostics::FIssue& Source : SourceIssues)
		{
			const FString ItemIdentity = FString::Printf(
				TEXT("%s / %s"),
				*Source.DomainIdentity,
				*Source.ItemIdentity);
			FPaper2DPlusValidationIssue Issue = MakeIssue(
				Asset,
				EPaper2DPlusValidationSeverity::Warning,
				Source.Code,
				TEXT("FrameCueTrackLayout"),
				ItemIdentity,
				TEXT("CueTrackLayout"),
				FText::FromString(Source.Message),
				LOCTEXT(
					"FrameCueTrackLayoutRemediation",
					"Open Frame Cues to inspect the Default fallback. Restore a known-good asset or deliberately recreate the affected track/placement; validation never repairs or dirties layout data."),
				FString::Printf(
					TEXT("%s|%s|%s"),
					*Source.Code.ToString(),
					*Source.DomainIdentity,
					*Source.ItemIdentity));

			FPaper2DPlusValidationToolTarget Target;
			Target.AssetPath = FSoftObjectPath(&Asset);
			Target.ToolId = TEXT("FrameCues");
			Target.TabId = Asset.IsA<UPaper2DPlusCharacterLayerAsset>()
				? FCharacterLayerAssetEditorToolkit::FrameCuesTabId
				: FName(TEXT("CharacterProfileEditor_FrameEvents"));
			Target.ItemIdentity = Source.DomainIdentity;
			Target.Field = TEXT("CueTrackLayout");
			Issue.ToolTarget = Target;
			OutIssues.Add(MoveTemp(Issue));
		}
	}

	void ProjectLayerBakeIntegrity(
		const UPaper2DPlusCharacterLayerAsset& Asset,
		TArray<FPaper2DPlusValidationIssue>& OutIssues)
	{
#if WITH_EDITORONLY_DATA
		const UPaper2DPlusCharacterProfileAsset* Profile = Asset.BaseProfile.Get();
		const FCharacterLayerBakeStatusSnapshot Status =
			CharacterLayerBakeCoordinator::EvaluateStatus(&Asset, Profile);
		const auto AddBakeIssue = [&Asset, &OutIssues](
			EPaper2DPlusValidationSeverity Severity,
			FName Code,
			const FString& Item,
			const FString& Message,
			const FText& Remediation)
		{
			FPaper2DPlusValidationIssue Issue = MakeIssue(
				Asset,
				Severity,
				Code,
				TEXT("BakeIntegrity"),
				Item,
				TEXT("BakeStatus"),
				FText::FromString(Message),
				Remediation);
			FPaper2DPlusValidationToolTarget Target;
			Target.AssetPath = FSoftObjectPath(&Asset);
			Target.ToolId = TEXT("LayerBake");
			Target.TabId = FCharacterLayerAssetEditorToolkit::CompletionTabId;
			Target.ItemIdentity = Item;
			Issue.ToolTarget = Target;
			OutIssues.Add(MoveTemp(Issue));
		};

		if (Status.Status == ECharacterLayerBakeStatus::RecoveryRequired)
		{
			AddBakeIssue(
				EPaper2DPlusValidationSeverity::Error,
				TEXT("Paper2DPlus.Layer.Bake.RecoveryRequired"),
				TEXT("Lifecycle"),
				Status.Blockers.IsEmpty()
					? TEXT("The Layer bake set is Recovery Required.")
					: FString::Join(Status.Blockers, TEXT(" ")),
				LOCTEXT("BakeRecoveryRemediation", "Use the Layer editor's Repair command. Do not bake, rebase, detach, or edit the manifest by hand."));
		}
		else if (Status.Status == ECharacterLayerBakeStatus::Detached)
		{
			AddBakeIssue(
				EPaper2DPlusValidationSeverity::Info,
				TEXT("Paper2DPlus.Layer.Bake.Detached"),
				TEXT("Lifecycle"),
				TEXT("Canonical output is frozen in place and detached from Layer management."),
				LOCTEXT("BakeDetachedRemediation", "No action is required. The retained manifest/archive are forensic evidence, not an active ownership claim."));
		}

		for (const FSoftObjectPath& MissingPackage : Status.MissingPackages)
		{
			AddBakeIssue(
				EPaper2DPlusValidationSeverity::Error,
				TEXT("Paper2DPlus.Layer.Bake.TouchedPackage"),
				MissingPackage.ToString(),
				FString::Printf(
					TEXT("Manifest package '%s' is missing from disk."),
					*MissingPackage.ToString()),
				LOCTEXT("BakeMissingPackageRemediation", "Restore the package or use the verified Layer recovery workflow; never remove manifest paths by hand."));
		}

		const bool bCanInterpretManagedRecords =
			(Asset.BakeAttachmentState == ECharacterLayerBakeAttachmentState::Attached
				|| Asset.BakeAttachmentState == ECharacterLayerBakeAttachmentState::RecoveryRequired)
			&& Asset.BakeManifest.ManifestVersion <= Paper2DPlusLayerBakeVersion::CurrentManifestVersion
			&& Asset.BakeManifest.DigestVersion <= CharacterLayerBakeCore::CurrentDigestVersion;
		if (bCanInterpretManagedRecords)
		{
			for (const FCharacterLayerAnimationBakeRecord& Record : Asset.BakeManifest.Animations)
			{
				TArray<FString> MissingFields;
				if (Record.Flipbook.IsNull()) MissingFields.Add(TEXT("Flipbook"));
				if (Record.ManagedTexture.IsNull()) MissingFields.Add(TEXT("ManagedTexture"));
				if (Record.ManagedSprites.IsEmpty()) MissingFields.Add(TEXT("ManagedSprites"));
				if (MissingFields.IsEmpty()) continue;

				const FString Item = Record.LegacyAnimationName.IsEmpty()
					? Record.Flipbook.ToSoftObjectPath().ToString()
					: Record.LegacyAnimationName;
				AddBakeIssue(
					EPaper2DPlusValidationSeverity::Error,
					TEXT("Paper2DPlus.Layer.Bake.ManagedAssets"),
					Item.IsEmpty() ? TEXT("ManifestAnimation") : Item,
					FString::Printf(
						TEXT("The animation manifest record is missing required managed backing: %s."),
						*FString::Join(MissingFields, TEXT(", "))),
					LOCTEXT("BakeManagedAssetsRemediation", "Use Repair or explicitly overwrite from Layer Source after verifying ownership; never reconstruct managed paths by hand."));
			}
		}

		for (const FCharacterLayerBakeAnimationStatus& Animation : Status.Animations)
		{
			const FString Item = Animation.AnimationName.IsEmpty()
				? Animation.FlipbookPath.ToString() : Animation.AnimationName;
			if (Animation.bOutputConflict)
			{
				AddBakeIssue(
					EPaper2DPlusValidationSeverity::Error,
					TEXT("Paper2DPlus.Layer.Bake.OutputConflict"),
					Item,
					Animation.Diagnostics.IsEmpty()
						? TEXT("Managed output differs from the committed manifest.")
						: FString::Join(Animation.Diagnostics, TEXT(" ")),
					LOCTEXT("BakeConflictRemediation", "Review the exact overwrite scope, then explicitly Overwrite from Layer Source; competing ownership must be resolved rather than overwritten."));
			}
			else if (Animation.bSourceDrift)
			{
				AddBakeIssue(
					EPaper2DPlusValidationSeverity::Warning,
					TEXT("Paper2DPlus.Layer.Bake.SourceDrift"),
					Item,
					TEXT("Layer source changed after this animation's last successful bake."),
					LOCTEXT("BakeSourceRemediation", "Run Bake Current or Bake All. The Default Appearance, never preview eyes, determines the result."));
			}
		}

		if (Status.bPartialSave)
		{
			AddBakeIssue(
				EPaper2DPlusValidationSeverity::Error,
				TEXT("Paper2DPlus.Layer.Bake.PartialSave"),
				TEXT("SaveBakeSet"),
				TEXT("The latest Save Bake Set was partial, or a manifest package is missing."),
				Status.Status == ECharacterLayerBakeStatus::RecoveryRequired
					? LOCTEXT("BakePartialRecoveryRemediation", "Complete Repair first, then retry Save Bake Set so every manifest-bounded package and the final Layer checkpoint become durable.")
					: LOCTEXT("BakePartialSaveRemediation", "Retry Save Bake Set. It saves exactly the manifest-bounded package set and checkpoints this report last."));
		}
		else if (Status.bNeedsSave)
		{
			AddBakeIssue(
				EPaper2DPlusValidationSeverity::Info,
				TEXT("Paper2DPlus.Layer.Bake.NeedsSave"),
				TEXT("SaveBakeSet"),
				TEXT("Verified bake-set packages have unsaved changes."),
				LOCTEXT("BakeNeedsSaveRemediation", "Use Save Bake Set to persist the exact consistency set."));
		}
		if (Status.bAdoptionBlocked)
		{
			AddBakeIssue(
				EPaper2DPlusValidationSeverity::Warning,
				TEXT("Paper2DPlus.Layer.Bake.AdoptionBlocked"),
				TEXT("Adoption"),
				FString::Join(Status.AdoptionAnalysis.Blockers, TEXT(" ")),
				LOCTEXT("BakeAdoptionBlockedRemediation", "Resolve the reported ownership/source blocker before choosing Adopt and Bake All."));
		}
#endif
	}

	void ValidateCatalog(
		const UPaper2DPlusCharacterCatalogAsset& Catalog,
		TArray<FPaper2DPlusValidationIssue>& OutIssues)
	{
		TArray<FAssetData> Assets;
		FPaper2DPlusCharacterCatalogSettingsSnapshot Snapshot;
		FCharacterCatalogAuditService::FNativeAssetResolver AssetResolver;
		FCharacterCatalogAuditService::FCookRegistrationInspector CookInspector;
		if (const Paper2DPlusProfileValidationAdapter::FCatalogValidationContext* Injected =
			GetInjectedCatalogValidationContext())
		{
			Assets = *Injected->Assets;
			Snapshot = *Injected->Settings;
			AssetResolver = Injected->AssetResolver;
			CookInspector = Injected->CookInspector;
		}
		else
		{
			FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
				AssetRegistryConstants::ModuleName);
			IAssetRegistry& Registry = RegistryModule.Get();
			Registry.SearchAllAssets(/*bSynchronousSearch=*/true);
			Registry.GetAllAssets(Assets);

			if (const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get())
			{
				Snapshot.DefaultCatalog = Settings->DefaultCharacterCatalog;
			}
		}

		FPaper2DPlusCharacterCatalogAuditReport Report =
			FCharacterCatalogAuditService::BuildReport(
				&Catalog,
				Assets,
				Snapshot,
				AssetResolver,
				CookInspector);
		OutIssues.Append(MoveTemp(Report.Issues));
	}

}

namespace Paper2DPlusProfileValidationAdapter
{
	FScopedCatalogValidationContext::FScopedCatalogValidationContext(
		const FCatalogValidationContext& InContext)
		: PreviousContext(GInjectedCatalogValidationContext)
	{
		check(InContext.IsUsable());
		GInjectedCatalogValidationContext = &InContext;
	}

	FScopedCatalogValidationContext::~FScopedCatalogValidationContext()
	{
		GInjectedCatalogValidationContext = PreviousContext;
	}

	void ProjectConfiguredCatalogReferenceProblem(
		const FSoftObjectPath& ConfiguredCatalogPath,
		EPaper2DPlusConfiguredCatalogReferenceProblem Problem,
		FName ActualAssetType,
		TArray<FPaper2DPlusValidationIssue>& OutIssues)
	{
		FPaper2DPlusValidationIssue Issue;
		Issue.Severity = EPaper2DPlusValidationSeverity::Error;
		Issue.AssetPath = ConfiguredCatalogPath;
		Issue.AssetType = UPaper2DPlusCharacterCatalogAsset::StaticClass()->GetFName();
		Issue.Scope = TEXT("Settings");
		Issue.Field = TEXT("DefaultCharacterCatalog");

		switch (Problem)
		{
		case EPaper2DPlusConfiguredCatalogReferenceProblem::Unset:
			Issue.Code = TEXT("Paper2DPlus.Catalog.Settings.MissingAuthority");
			Issue.StableDiscriminator = TEXT("Unset");
			Issue.Message = LOCTEXT(
				"UnsetConfiguredCatalog",
				"The project has no Default Character Catalog configured.");
			Issue.Remediation = LOCTEXT(
				"UnsetConfiguredCatalogRemediation",
				"Choose one Paper2DPlus Character Catalog in Project Settings, then rerun validation.");
			break;

		case EPaper2DPlusConfiguredCatalogReferenceProblem::Missing:
			Issue.Code = TEXT("Paper2DPlus.Catalog.Settings.MissingAuthorityAsset");
			Issue.StableDiscriminator = TEXT("MissingAsset");
			Issue.Message = LOCTEXT(
				"MissingConfiguredCatalog",
				"The Default Character Catalog path is not present in the Asset Registry.");
			Issue.Remediation = LOCTEXT(
				"MissingConfiguredCatalogRemediation",
				"Restore the configured Catalog asset or choose an existing Paper2DPlus Character Catalog in Project Settings.");
			break;

		default:
		{
			Issue.Code = TEXT("Paper2DPlus.Catalog.Settings.InvalidAuthorityClass");
			Issue.StableDiscriminator = TEXT("WrongClass");
			const FString ActualClassDisplay = ActualAssetType.IsNone()
				? FString(TEXT("an unsupported asset class"))
				: ActualAssetType.ToString();
			Issue.Message = FText::Format(
				LOCTEXT(
					"WrongConfiguredCatalogClass",
					"The Default Character Catalog path resolves to '{0}', not a Paper2DPlus Character Catalog."),
				FText::FromString(ActualClassDisplay));
			Issue.Remediation = LOCTEXT(
				"WrongConfiguredCatalogClassRemediation",
				"Choose a Paper2DPlus Character Catalog asset in Project Settings.");
			break;
		}
		}

		Issue.StableKey = FPaper2DPlusValidationService::MakeStableKey(Issue);
		OutIssues.Add(MoveTemp(Issue));
	}

	void ProjectCharacterIssues(
		const UPaper2DPlusCharacterProfileAsset& Asset,
		const TArray<FCharacterProfileValidationIssue>& SourceIssues,
		TArray<FPaper2DPlusValidationIssue>& OutIssues)
	{
		for (const FCharacterProfileValidationIssue& Source : SourceIssues)
		{
			OutIssues.Add(MakeIssue(
				Asset,
				CharacterSeverity(Source.Severity),
				TEXT("Paper2DPlus.Character.NativeIssue"),
				TEXT("CharacterProfile"),
				Source.Context,
				NAME_None,
				FText::FromString(Source.Message),
				LOCTEXT("CharacterRemediation", "Review the named Character Profile item and correct the reported data."),
				Source.Message));
		}
	}

	void ProjectLayerIssues(
		const UPaper2DPlusCharacterLayerAsset& Asset,
		const TArray<FCharacterLayerValidationIssue>& SourceIssues,
		TArray<FPaper2DPlusValidationIssue>& OutIssues)
	{
		for (const FCharacterLayerValidationIssue& Source : SourceIssues)
		{
			OutIssues.Add(MakeIssue(
				Asset,
				LayerSeverity(Source.Severity),
				TEXT("Paper2DPlus.Layer.NativeIssue"),
				TEXT("Layer"),
				Source.LayerName,
				NAME_None,
				FText::FromString(Source.Message),
				LOCTEXT("LayerRemediation", "Review the named Layer item and correct the reported data."),
				Source.Message));
		}
	}

	void ProjectGenericLayerAppearanceIssues(
		const UPaper2DPlusCharacterLayerAsset& Asset,
		TArray<FPaper2DPlusValidationIssue>& OutIssues)
	{
		if (!Paper2DPlusAppearanceResolver::UsesGenericAppearanceSchema(&Asset))
		{
			return; // Pre-conversion assets remain the explicit converter's responsibility.
		}
		const TArray<FString> SourceIssues =
			Paper2DPlusAppearanceResolver::ValidateGenericAppearanceSource(&Asset);
		for (int32 IssueIndex = 0; IssueIndex < SourceIssues.Num(); ++IssueIndex)
		{
			FPaper2DPlusValidationIssue Issue = MakeIssue(
				Asset,
				EPaper2DPlusValidationSeverity::Error,
				TEXT("Paper2DPlus.Layer.Appearance.Invalid"),
				TEXT("Appearance"),
				FString::Printf(TEXT("Issue.%d"), IssueIndex),
				TEXT("Appearance"),
				FText::FromString(SourceIssues[IssueIndex]),
				LOCTEXT("GenericAppearanceRemediation", "Open Appearance in the Layer Workspace and repair the preset, default, Layer ID, or Exclusive Group reference."),
				SourceIssues[IssueIndex]);
			FPaper2DPlusValidationToolTarget Target;
			Target.AssetPath = FSoftObjectPath(&Asset);
			Target.ToolId = TEXT("LayerAppearance");
			Target.TabId = FCharacterLayerAssetEditorToolkit::AppearanceTabId;
			Target.ItemIdentity = FString::Printf(TEXT("Issue.%d"), IssueIndex);
			Target.Field = TEXT("Appearance");
			Issue.ToolTarget = MoveTemp(Target);
			OutIssues.Add(MoveTemp(Issue));
		}
	}

	void ProjectEffectIssues(
		const UPaper2DPlusEffectProfileAsset& Asset,
		const TArray<FPaper2DPlusEffectProfileValidationIssue>& SourceIssues,
		TArray<FPaper2DPlusValidationIssue>& OutIssues)
	{
		for (const FPaper2DPlusEffectProfileValidationIssue& Source : SourceIssues)
		{
			const FPaper2DPlusEffectProfileEntry* MatchedEntry = Asset.Effects.FindByPredicate(
				[&Source](const FPaper2DPlusEffectProfileEntry& Entry)
				{
					const FSoftObjectPath EffectPath = Entry.GetEffectFlipbookPath();
					return (!Entry.EffectName.IsNone()
						&& Entry.EffectName.ToString().Equals(Source.EffectName.ToString(), ESearchCase::IgnoreCase))
						|| (!EffectPath.IsNull()
							&& EffectPath.GetAssetName().Equals(Source.EffectName.ToString(), ESearchCase::IgnoreCase));
				});
			const FSoftObjectPath MatchedEffectPath = MatchedEntry
				? MatchedEntry->GetEffectFlipbookPath()
				: FSoftObjectPath();
			const FString StableItemIdentity = !MatchedEffectPath.IsNull()
				? MatchedEffectPath.ToString()
				: Source.EffectName.ToString();
			FPaper2DPlusValidationIssue Issue = MakeIssue(
				Asset,
				EffectSeverity(Source.Severity),
				MakeFieldCode(TEXT("Paper2DPlus.Effect"), Source.Field),
				TEXT("Effect"),
				StableItemIdentity,
				Source.Field,
				Source.Message,
				LOCTEXT("EffectRemediation", "Review the named Effect entry and correct the reported field."),
				StableTextDiscriminator(Source.Message));
			FPaper2DPlusValidationToolTarget Target;
			Target.ToolId = TEXT("EffectProfile");
			Target.TabId = !MatchedEffectPath.IsNull()
				? FName(TEXT("EffectProfileEditor_Details"))
				: FName(TEXT("EffectProfileEditor_AdvancedDetails"));
			Target.ItemIdentity = StableItemIdentity;
			// U15's focused Details panel owns an explicit cross-version field-focus seam. Unknown legacy
			// fields intentionally focus its Advanced Details action instead of a stale struct copy.
			Target.Field = Source.Field;
			Issue.ToolTarget = Target;
			OutIssues.Add(MoveTemp(Issue));
		}
	}

	void ProjectCombatIssues(
		const UPaper2DPlusCombatProfileAsset& Asset,
		const TArray<FPaper2DPlusCombatValidationIssue>& SourceIssues,
		TArray<FPaper2DPlusValidationIssue>& OutIssues)
	{
		for (const FPaper2DPlusCombatValidationIssue& Source : SourceIssues)
		{
			FPaper2DPlusValidationIssue Issue = MakeIssue(
				Asset,
				CombatSeverity(Source.Severity),
				MakeFieldCode(TEXT("Paper2DPlus.Combat"), Source.Field),
				TEXT("Combat"),
				Source.MoveName.ToString(),
				Source.Field,
				Source.Message,
				LOCTEXT("CombatRemediation", "Review the named Combat Profile move or field and correct the reported data."),
				StableTextDiscriminator(Source.Message));
			FPaper2DPlusValidationToolTarget Target;
			Target.ToolId = TEXT("CombatProfile");
			Target.TabId = Source.MoveName.IsNone()
				? FName(TEXT("CombatProfileEditor_Details"))
				: FName(TEXT("CombatProfileEditor_Setup"));
			Target.ItemIdentity = Source.MoveName.ToString();
			// See Effect above: exact move+tab is truthful; property-row focus is not currently portable.
			Target.Field = NAME_None;
			Issue.ToolTarget = Target;
			OutIssues.Add(MoveTemp(Issue));
		}
	}

	void RegisterBuiltInAdapters(FPaper2DPlusValidationService& Service)
	{
		Service.RegisterAdapter(
			TEXT("Paper2DPlus.CharacterProfile"),
			FName(*UPaper2DPlusCharacterProfileAsset::StaticClass()->GetPathName()),
			FPaper2DPlusValidationAdapterDelegate::CreateLambda(
				[](const UObject& Object, TArray<FPaper2DPlusValidationIssue>& OutIssues)
				{
					const UPaper2DPlusCharacterProfileAsset& Asset = *CastChecked<UPaper2DPlusCharacterProfileAsset>(&Object);
					TArray<FCharacterProfileValidationIssue> SourceIssues;
					Asset.ValidateCharacterProfileAsset(SourceIssues);
					ProjectCharacterIssues(Asset, SourceIssues, OutIssues);
					ProjectFrameCueMigration(
						Asset,
						FPaper2DPlusFrameCueMigrationService::AnalyzeCharacterProfile(Asset),
						OutIssues);
					TArray<Paper2DPlusFrameCueTrackLayoutDiagnostics::FIssue> LayoutIssues;
					Paper2DPlusFrameCueTrackLayoutDiagnostics::AnalyzeCharacterProfile(
						Asset, LayoutIssues);
					ProjectFrameCueTrackLayoutDiagnostics(Asset, LayoutIssues, OutIssues);
				}));

		Service.RegisterAdapter(
			TEXT("Paper2DPlus.CharacterLayer"),
			FName(*UPaper2DPlusCharacterLayerAsset::StaticClass()->GetPathName()),
			FPaper2DPlusValidationAdapterDelegate::CreateLambda(
				[](const UObject& Object, TArray<FPaper2DPlusValidationIssue>& OutIssues)
				{
					const UPaper2DPlusCharacterLayerAsset& Asset = *CastChecked<UPaper2DPlusCharacterLayerAsset>(&Object);
					ProjectLayerIssues(Asset, Asset.ValidateLayerAsset(), OutIssues);
					ProjectGenericLayerAppearanceIssues(Asset, OutIssues);
					ProjectFrameCueMigration(
						Asset,
						FPaper2DPlusFrameCueMigrationService::AnalyzeCharacterLayer(Asset),
						OutIssues);
					TArray<Paper2DPlusFrameCueTrackLayoutDiagnostics::FIssue> LayoutIssues;
					Paper2DPlusFrameCueTrackLayoutDiagnostics::AnalyzeCharacterLayer(
						Asset, LayoutIssues);
					ProjectFrameCueTrackLayoutDiagnostics(Asset, LayoutIssues, OutIssues);
					ProjectLayerBakeIntegrity(Asset, OutIssues);
				}));

		Service.RegisterAdapter(
			TEXT("Paper2DPlus.EffectProfile"),
			FName(*UPaper2DPlusEffectProfileAsset::StaticClass()->GetPathName()),
			FPaper2DPlusValidationAdapterDelegate::CreateLambda(
				[](const UObject& Object, TArray<FPaper2DPlusValidationIssue>& OutIssues)
				{
					const UPaper2DPlusEffectProfileAsset& Asset = *CastChecked<UPaper2DPlusEffectProfileAsset>(&Object);
					TArray<FPaper2DPlusEffectProfileValidationIssue> SourceIssues;
					Asset.ValidateEffectProfileAsset(SourceIssues);
					ProjectEffectIssues(Asset, SourceIssues, OutIssues);
				}));

		Service.RegisterAdapter(
			TEXT("Paper2DPlus.CombatProfile"),
			FName(*UPaper2DPlusCombatProfileAsset::StaticClass()->GetPathName()),
			FPaper2DPlusValidationAdapterDelegate::CreateLambda(
				[](const UObject& Object, TArray<FPaper2DPlusValidationIssue>& OutIssues)
				{
					const UPaper2DPlusCombatProfileAsset& Asset = *CastChecked<UPaper2DPlusCombatProfileAsset>(&Object);
					TArray<FPaper2DPlusCombatValidationIssue> SourceIssues;
					Asset.ValidateCombatProfileAsset(SourceIssues);
					ProjectCombatIssues(Asset, SourceIssues, OutIssues);
				}));

		Service.RegisterAdapter(
			TEXT("Paper2DPlus.CharacterCatalog"),
			FName(*UPaper2DPlusCharacterCatalogAsset::StaticClass()->GetPathName()),
			FPaper2DPlusValidationAdapterDelegate::CreateLambda(
				[](const UObject& Object, TArray<FPaper2DPlusValidationIssue>& OutIssues)
				{
					ValidateCatalog(*CastChecked<UPaper2DPlusCharacterCatalogAsset>(&Object), OutIssues);
				}));
	}
}

#undef LOCTEXT_NAMESPACE
