// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterCatalogAuditService.h"

#include "CharacterCoverage/CharacterCoverageResolver.h"
#include "Engine/AssetManager.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "ProfileRelationshipService.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusCharacterCatalogAudit"

namespace
{
	FString NormalizePath(const FSoftObjectPath& Path)
	{
		return FProfileRelationshipService::NormalizeObjectPath(Path);
	}

	bool PathLess(const FSoftObjectPath& A, const FSoftObjectPath& B)
	{
		return NormalizePath(A).Compare(NormalizePath(B), ESearchCase::IgnoreCase) < 0;
	}

	bool PathsEqual(const FSoftObjectPath& A, const FSoftObjectPath& B)
	{
		return NormalizePath(A) == NormalizePath(B);
	}

	bool AssetDataIsClass(const FAssetData& AssetData, const UClass* Class)
	{
		if (!Class)
		{
			return false;
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		return AssetData.AssetClass == Class->GetFName();
#else
		return AssetData.AssetClassPath == Class->GetClassPathName();
#endif
	}

	bool HasAssetOfClass(
		const TArray<FAssetData>& Assets,
		const FSoftObjectPath& AssetPath,
		const UClass* Class)
	{
		const FString Wanted = NormalizePath(AssetPath);
		return !Wanted.IsEmpty() && Assets.ContainsByPredicate([&Wanted, Class](const FAssetData& AssetData)
		{
			return AssetDataIsClass(AssetData, Class)
				&& NormalizePath(FProfileRelationshipService::GetAssetObjectPath(AssetData)) == Wanted;
		});
	}

	EPaper2DPlusValidationSeverity CatalogSeverity(
		EPaper2DPlusCharacterCatalogIssueSeverity Severity)
	{
		switch (Severity)
		{
		case EPaper2DPlusCharacterCatalogIssueSeverity::Error:
			return EPaper2DPlusValidationSeverity::Error;
		case EPaper2DPlusCharacterCatalogIssueSeverity::Warning:
			return EPaper2DPlusValidationSeverity::Warning;
		default:
			return EPaper2DPlusValidationSeverity::Info;
		}
	}

	FName CatalogIssueCode(FName NativeCode)
	{
		return FName(*FString::Printf(
			TEXT("Paper2DPlus.Catalog.%s"),
			NativeCode.IsNone() ? TEXT("Structural") : *NativeCode.ToString()));
	}

	FName CompanionField(EPaper2DPlusCatalogCompanion Companion)
	{
		switch (Companion)
		{
		case EPaper2DPlusCatalogCompanion::Layer:
			return TEXT("LayerProfile");
		case EPaper2DPlusCatalogCompanion::Effect:
			return TEXT("EffectProfile");
		default:
			return TEXT("CombatProfile");
		}
	}

	FText CompanionLabel(EPaper2DPlusCatalogCompanion Companion)
	{
		switch (Companion)
		{
		case EPaper2DPlusCatalogCompanion::Layer:
			return LOCTEXT("LayerLabel", "Layer");
		case EPaper2DPlusCatalogCompanion::Effect:
			return LOCTEXT("EffectLabel", "Effect");
		default:
			return LOCTEXT("CombatLabel", "Combat");
		}
	}

	FString CompanionCodeSegment(EPaper2DPlusCatalogCompanion Companion)
	{
		switch (Companion)
		{
		case EPaper2DPlusCatalogCompanion::Layer:
			return TEXT("Layer");
		case EPaper2DPlusCatalogCompanion::Effect:
			return TEXT("Effect");
		default:
			return TEXT("Combat");
		}
	}

	FName CompanionAssetType(EPaper2DPlusCatalogCompanion Companion)
	{
		switch (Companion)
		{
		case EPaper2DPlusCatalogCompanion::Layer:
			return UPaper2DPlusCharacterLayerAsset::StaticClass()->GetFName();
		case EPaper2DPlusCatalogCompanion::Effect:
			return UPaper2DPlusEffectProfileAsset::StaticClass()->GetFName();
		default:
			return UPaper2DPlusCombatProfileAsset::StaticClass()->GetFName();
		}
	}

	FSoftObjectPath CompanionPath(
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

	bool IsRequired(
		const FPaper2DPlusCharacterCatalogEntry& Entry,
		EPaper2DPlusCatalogCompanion Companion)
	{
		switch (Companion)
		{
		case EPaper2DPlusCatalogCompanion::Layer:
			return Entry.Requirements.bRequireLayer;
		case EPaper2DPlusCatalogCompanion::Effect:
			return Entry.Requirements.bRequireEffect;
		default:
			return Entry.Requirements.bRequireCombat;
		}
	}

	FPaper2DPlusValidationToolTarget MakeCatalogTarget(
		const FSoftObjectPath& CatalogPath,
		const FSoftObjectPath& CharacterPath,
		FName Scope,
		FName Field)
	{
		// The Groups rail lives inside the Roster tab, so every Catalog target lands on the Roster and the
		// ToolId is the discriminator that tells the toolkit to scope the rail instead of the Details focus.
		FPaper2DPlusValidationToolTarget Target;
		Target.AssetPath = CatalogPath;
		Target.ToolId = Scope == TEXT("Group")
			? TEXT("CharacterCatalogGroups")
			: TEXT("CharacterCatalog");
		Target.TabId = FName(TEXT("CharacterCatalogEditor_Roster"));
		Target.ItemIdentity = CharacterPath.IsValid() ? CharacterPath.ToString() : Scope.ToString();
		Target.Field = Field;
		return Target;
	}

	FPaper2DPlusValidationIssue MakeCatalogIssue(
		const FSoftObjectPath& CatalogPath,
		EPaper2DPlusValidationSeverity Severity,
		FName Code,
		const FSoftObjectPath& CharacterPath,
		FName Scope,
		FName Field,
		const FText& Message,
		const FText& Remediation)
	{
		FPaper2DPlusValidationIssue Issue;
		Issue.Severity = Severity;
		Issue.Code = Code;
		Issue.AssetPath = CatalogPath;
		Issue.AssetType = UPaper2DPlusCharacterCatalogAsset::StaticClass()->GetFName();
		Issue.CharacterPath = CharacterPath;
		Issue.Scope = Scope;
		Issue.ItemIdentity = CharacterPath.IsValid()
			? CharacterPath.ToString()
			: Scope.ToString();
		Issue.Field = Field;
		Issue.Message = Message;
		Issue.Remediation = Remediation;
		Issue.ToolTarget = MakeCatalogTarget(CatalogPath, CharacterPath, Scope, Field);
		return Issue;
	}

	void SetAffectedAsset(
		FPaper2DPlusValidationIssue& Issue,
		const FSoftObjectPath& AffectedPath,
		FName AffectedType)
	{
		if (AffectedPath.IsValid())
		{
			Issue.AssetPath = AffectedPath;
		}
		Issue.AssetType = AffectedType;
	}

	// Deep-validation resolve. A caller-supplied resolver MAY load (tests inject a fixture map; the editor
	// model can opt in). With NO resolver this is registry-only and never blocks the audit/UI thread:
	// existence and class were already answered from FAssetData, so we return an already-resident object for
	// free deep validation and otherwise null. A TryLoad here would cold-load up to four companions per
	// character (~800 synchronous loads on a 200-character catalog); AppendNativeIssues emits a deferred
	// deep-validation notice for the not-resident case instead.
	UObject* ResolveAssetExplicitly(
		const FSoftObjectPath& AssetPath,
		const FCharacterCatalogAuditService::FNativeAssetResolver& Resolver)
	{
		if (Resolver)
		{
			return Resolver(AssetPath);
		}
		return AssetPath.ResolveObject();
	}

	FName OpenToolForAsset(const UObject& Asset)
	{
		if (Asset.IsA<UPaper2DPlusCharacterProfileAsset>()) return TEXT("CharacterProfile");
		if (Asset.IsA<UPaper2DPlusCharacterLayerAsset>()) return TEXT("CharacterLayer");
		if (Asset.IsA<UPaper2DPlusEffectProfileAsset>()) return TEXT("EffectProfile");
		if (Asset.IsA<UPaper2DPlusCombatProfileAsset>()) return TEXT("CombatProfile");
		return NAME_None;
	}

	struct FNativeAssetAuditCacheEntry
	{
		// A deep audit can remain open behind a progress dialog for many rows. Keep every resolved
		// object alive until the report is complete rather than relying on a raw pointer across callbacks.
		TStrongObjectPtr<UObject> ResolvedAsset;
		TArray<FPaper2DPlusValidationIssue> Issues;
	};

	UObject* AppendNativeIssues(
		const FSoftObjectPath& AssetPath,
		const FSoftObjectPath& CharacterPath,
		const FCharacterCatalogAuditService::FNativeAssetResolver& Resolver,
		TMap<FString, FNativeAssetAuditCacheEntry>& Cache,
		TArray<FPaper2DPlusValidationIssue>& OutIssues)
	{
		const FString Key = NormalizePath(AssetPath);
		if (Key.IsEmpty())
		{
			return nullptr;
		}

		FNativeAssetAuditCacheEntry* Cached = Cache.Find(Key);
		if (!Cached)
		{
			FNativeAssetAuditCacheEntry Projected;
			Projected.ResolvedAsset = TStrongObjectPtr<UObject>(
				ResolveAssetExplicitly(AssetPath, Resolver));
			if (Projected.ResolvedAsset.IsValid())
			{
				FPaper2DPlusValidationService::Get().ValidateObject(
					Projected.ResolvedAsset.Get(),
					Projected.Issues);
				for (FPaper2DPlusValidationIssue& Issue : Projected.Issues)
				{
					// The Catalog reference is the stable identity even when an injected resolver uses a fixture object.
					Issue.AssetPath = AssetPath;
					if (Issue.AssetType.IsNone())
					{
						Issue.AssetType = Projected.ResolvedAsset->GetClass()->GetFName();
					}
					if (!Issue.ToolTarget.IsSet())
					{
						FPaper2DPlusValidationToolTarget Target;
						Target.AssetPath = AssetPath;
						Target.ToolId = OpenToolForAsset(*Projected.ResolvedAsset.Get());
						Target.ItemIdentity = Issue.ItemIdentity;
						Target.Field = Issue.Field;
						Issue.ToolTarget = Target;
					}
					else if (!Issue.ToolTarget->AssetPath.IsValid())
					{
						Issue.ToolTarget->AssetPath = AssetPath;
					}
					Issue.StableKey.Reset();
				}
			}
			else if (Resolver)
			{
				// A bound resolver is the explicit deep-audit contract. Returning null means that
				// contract could not be completed, so fail deterministically instead of caching silence.
				FPaper2DPlusValidationIssue& LoadFailure =
					Projected.Issues.AddDefaulted_GetRef();
				LoadFailure.Severity = EPaper2DPlusValidationSeverity::Error;
				LoadFailure.Code =
					TEXT("Paper2DPlus.Catalog.Validation.AssetLoadFailed");
				LoadFailure.AssetPath = AssetPath;
				LoadFailure.Scope = TEXT("Validation");
				LoadFailure.Message = FText::Format(
					LOCTEXT(
						"ExplicitAssetLoadFailed",
						"Assigned asset '{0}' could not be loaded for its explicit validation checks."),
					FText::FromString(AssetPath.ToString()));
				LoadFailure.Remediation = LOCTEXT(
					"ExplicitAssetLoadFailedRemediation",
					"Restore or reassign the asset, then choose Check Again.");
				FPaper2DPlusValidationToolTarget Target;
				Target.AssetPath = AssetPath;
				LoadFailure.ToolTarget = Target;
			}
			else
			{
				// Registry-only mode proved existence and class from FAssetData but did not cold-load the
				// object, so its native deep validators did not run. Surface one Info notice per companion
				// rather than blocking the audit thread; opening the asset (or an opt-in resolver) runs the
				// full native validation. Correctness is preserved: missing/wrong-class assignments still
				// raise their existing Error checks upstream of this deep-validation pass.
				FPaper2DPlusValidationIssue& Deferred = Projected.Issues.AddDefaulted_GetRef();
				Deferred.Severity = EPaper2DPlusValidationSeverity::Info;
				Deferred.Code = TEXT("Paper2DPlus.Catalog.Validation.DeferredDeepValidation");
				Deferred.AssetPath = AssetPath;
				Deferred.Scope = TEXT("Relationship");
				Deferred.Message = FText::Format(
					LOCTEXT("DeferredDeepValidation", "Assigned asset '{0}' is present but not loaded, so its own checks did not run."),
					FText::FromString(AssetPath.ToString()));
				Deferred.Remediation = LOCTEXT("DeferredDeepValidationRemediation", "Open the asset to run its full checks, then choose Check Again.");
				FPaper2DPlusValidationToolTarget Target;
				Target.AssetPath = AssetPath;
				Deferred.ToolTarget = Target;
			}
			Cached = &Cache.Add(Key, MoveTemp(Projected));
		}

		for (const FPaper2DPlusValidationIssue& Source : Cached->Issues)
		{
			FPaper2DPlusValidationIssue Issue = Source;
			Issue.CharacterPath = CharacterPath;
			Issue.StableKey.Reset();
			OutIssues.Add(MoveTemp(Issue));
		}
		return Cached->ResolvedAsset.Get();
	}

	void AppendCoverageIssues(
		const UPaper2DPlusCharacterCatalogAsset& CatalogAsset,
		const FSoftObjectPath& CatalogPath,
		const FPaper2DPlusCharacterCatalogEntry& Entry,
		const UPaper2DPlusCharacterProfileAsset& Profile,
		TArray<FPaper2DPlusValidationIssue>& OutIssues)
	{
		FGameplayTagContainer ExpectedTags;
		CatalogAsset.GetExpectedAnimationTagsForCharacter(
			Entry.CharacterProfile,
			ExpectedTags);
		const FCharacterCoverageResolveResult Coverage =
			FCharacterCoverageResolver::Resolve(&Profile, ExpectedTags);
		const FSoftObjectPath CharacterPath = Entry.CharacterProfile.ToSoftObjectPath();
		for (const FCharacterCoverageRow& CoverageRow : Coverage.Rows)
		{
			if (CoverageRow.Status == ECharacterCoverageStatus::Covered)
			{
				continue;
			}

			FPaper2DPlusValidationIssue Issue = MakeCatalogIssue(
				CatalogPath,
				EPaper2DPlusValidationSeverity::Warning,
				TEXT("Paper2DPlus.Catalog.Coverage.MissingExpectedTag"),
				CharacterPath,
				TEXT("Coverage"),
				TEXT("AnimationTags"),
				FText::Format(
					LOCTEXT(
						"MissingExpectedTag",
						"Character '{0}' does not author expected animation tag '{1}'."),
					FText::FromString(CharacterPath.GetAssetName()),
					FText::FromName(CoverageRow.ExpectedTag.GetTagName())),
				LOCTEXT(
					"MissingExpectedTagRemediation",
					"Open the Character Profile and add this exact tag to an animation's Animation Tags; group or chain inheritance does not satisfy Catalog coverage."));
			Issue.StableDiscriminator = CoverageRow.ExpectedTag.ToString();
			if (Issue.ToolTarget.IsSet())
			{
				// Activation still selects this Character's Roster card first, while Open follows the
				// affected Profile path so the designer can author the missing exact tag.
				Issue.ToolTarget->AssetPath = CharacterPath;
			}
			OutIssues.Add(MoveTemp(Issue));
		}
	}

	/**
	 * Layer/Combat relationship state over the saved entry.
	 *
	 * A companion that points at a DIFFERENT Character, or that no longer exists, is an Error. Two
	 * softer states are Warnings because they describe unfinished authoring rather than a
	 * contradiction: a companion that names no Character yet, and one that predates the inward
	 * relationship tag entirely. An EMPTY slot is still the requirement check's job for presence — but
	 * it is checked here for AMBIGUITY, because "several assets claim this Character" is a fact only
	 * this index can see and it silently disappeared when the link modes were removed.
	 */
	void AddAssignedCompanionIssues(
		const FSoftObjectPath& CatalogPath,
		const FSoftObjectPath& CharacterPath,
		const FPaper2DPlusCharacterCatalogEntry& Entry,
		EPaper2DPlusCatalogCompanion Companion,
		const FPaper2DPlusProfileRelationshipIndex& Index,
		TArray<FPaper2DPlusValidationIssue>& OutIssues)
	{
		const FName Field = CompanionField(Companion);
		const FString CodeSegment = CompanionCodeSegment(Companion);
		const FText Label = CompanionLabel(Companion);
		const FSoftObjectPath AssignedPath = CompanionPath(Entry, Companion);
		if (AssignedPath.IsNull())
		{
			// Nothing assigned: report only that the choice is not obvious. Severity follows whether the
			// slot is required, matching the behaviour this check replaced.
			const FPaper2DPlusProfileRelationshipResolution Suggestion =
				FProfileRelationshipService::SuggestCandidate(Companion, CharacterPath, Index);
			if (Suggestion.State == EPaper2DPlusProfileRelationshipState::Ambiguous)
			{
				OutIssues.Add(MakeCatalogIssue(
					CatalogPath,
					IsRequired(Entry, Companion)
						? EPaper2DPlusValidationSeverity::Error
						: EPaper2DPlusValidationSeverity::Warning,
					FName(*FString::Printf(TEXT("Paper2DPlus.Catalog.Relationship.%s.Ambiguous"), *CodeSegment)),
					CharacterPath,
					TEXT("Relationship"),
					Field,
					FText::Format(
						LOCTEXT("AmbiguousRelationship", "Character '{0}' has {1} matching {2} Profiles, so no candidate is selected."),
						FText::FromString(CharacterPath.ToString()),
						FText::AsNumber(Suggestion.CandidatePaths.Num()),
						Label),
					LOCTEXT("AmbiguousRelationshipRemediation", "Choose one companion manually or remove the extra inward Character relationship.")));
			}
			return;
		}
		const FPaper2DPlusProfileRelationshipResolution Resolution =
			FProfileRelationshipService::ResolveAssigned(Companion, CharacterPath, AssignedPath, Index);

		if (Resolution.State == EPaper2DPlusProfileRelationshipState::ManualMismatch)
		{
			FPaper2DPlusValidationIssue Issue = MakeCatalogIssue(
				CatalogPath,
				EPaper2DPlusValidationSeverity::Error,
				FName(*FString::Printf(TEXT("Paper2DPlus.Catalog.Relationship.%s.ManualMismatch"), *CodeSegment)),
				CharacterPath,
				TEXT("Relationship"),
				Field,
				FText::Format(
					LOCTEXT("ManualRelationshipMismatch", "Assigned {0} Profile '{1}' points to a different Character Profile than '{2}'."),
					Label,
					FText::FromString(Resolution.AssignedAssetPath.ToString()),
					FText::FromString(CharacterPath.ToString())),
				LOCTEXT("ManualRelationshipMismatchRemediation", "Assign the matching companion or correct the companion's inward Character relationship."));
			SetAffectedAsset(Issue, Resolution.AssignedAssetPath, CompanionAssetType(Companion));
			OutIssues.Add(MoveTemp(Issue));
		}
		else if (Resolution.State == EPaper2DPlusProfileRelationshipState::MissingAssignedAsset)
		{
			FPaper2DPlusValidationIssue Issue = MakeCatalogIssue(
				CatalogPath,
				EPaper2DPlusValidationSeverity::Error,
				FName(*FString::Printf(TEXT("Paper2DPlus.Catalog.Relationship.%s.ManualMissing"), *CodeSegment)),
				CharacterPath,
				TEXT("Relationship"),
				Field,
				FText::Format(
					LOCTEXT("ManualRelationshipMissing", "Assigned {0} Profile '{1}' is missing or is not a {0} Profile."),
					Label,
					FText::FromString(Resolution.AssignedAssetPath.ToString())),
				LOCTEXT("ManualRelationshipMissingRemediation", "Restore the asset, choose another companion, or clear the optional assignment."));
			SetAffectedAsset(Issue, Resolution.AssignedAssetPath, CompanionAssetType(Companion));
			OutIssues.Add(MoveTemp(Issue));
		}
		else if (Resolution.State == EPaper2DPlusProfileRelationshipState::ManualUnlinked)
		{
			// Warning, not Error: the assignment is real and the companion exists — it simply has not
			// been pointed back at a Character yet, which is an ordinary mid-authoring state and must
			// not fail the validation gate.
			FPaper2DPlusValidationIssue Issue = MakeCatalogIssue(
				CatalogPath,
				EPaper2DPlusValidationSeverity::Warning,
				FName(*FString::Printf(TEXT("Paper2DPlus.Catalog.Relationship.%s.Unlinked"), *CodeSegment)),
				CharacterPath,
				TEXT("Relationship"),
				Field,
				FText::Format(
					LOCTEXT("UnlinkedRelationship", "Assigned {0} Profile '{1}' does not name a Character Profile of its own."),
					Label,
					FText::FromString(Resolution.AssignedAssetPath.ToString())),
				LOCTEXT("UnlinkedRelationshipRemediation", "Open the companion and set its Character Profile, so the relationship reads the same from both ends."));
			SetAffectedAsset(Issue, Resolution.AssignedAssetPath, CompanionAssetType(Companion));
			OutIssues.Add(MoveTemp(Issue));
		}
		else if (Resolution.State == EPaper2DPlusProfileRelationshipState::LegacyUnknown)
		{
			// The companion predates the inward relationship tag, so nothing can confirm or deny that it
			// belongs to this Character. Silence here would ship a possibly-wrong pairing unremarked.
			FPaper2DPlusValidationIssue Issue = MakeCatalogIssue(
				CatalogPath,
				EPaper2DPlusValidationSeverity::Warning,
				FName(*FString::Printf(TEXT("Paper2DPlus.Catalog.Relationship.%s.LegacyUnknown"), *CodeSegment)),
				CharacterPath,
				TEXT("Relationship"),
				Field,
				FText::Format(
					LOCTEXT("LegacyRelationship", "Assigned {0} Profile '{1}' publishes no Character relationship, so its pairing cannot be verified."),
					Label,
					FText::FromString(Resolution.AssignedAssetPath.ToString())),
				LOCTEXT("LegacyRelationshipRemediation", "Open and resave the companion so it publishes its Character Profile relationship."));
			SetAffectedAsset(Issue, Resolution.AssignedAssetPath, CompanionAssetType(Companion));
			OutIssues.Add(MoveTemp(Issue));
		}
	}

	bool CookRuleBlocksProduction(EPrimaryAssetCookRule CookRule)
	{
		return CookRule == EPrimaryAssetCookRule::NeverCook
			|| CookRule == EPrimaryAssetCookRule::DevelopmentCook
			|| CookRule == EPrimaryAssetCookRule::DevelopmentAlwaysCook;
	}
}

const FPaper2DPlusCharacterCatalogAuditRow* FPaper2DPlusCharacterCatalogAuditReport::FindRow(
	const FSoftObjectPath& CharacterPath) const
{
	const FString Wanted = NormalizePath(CharacterPath);
	return Rows.FindByPredicate([&Wanted](const FPaper2DPlusCharacterCatalogAuditRow& Row)
	{
		return NormalizePath(Row.CharacterPath) == Wanted;
	});
}

FPaper2DPlusCharacterCatalogCookInspection FCharacterCatalogAuditService::InspectHostAssetManager(
	const FSoftObjectPath& CatalogPath)
{
	FPaper2DPlusCharacterCatalogCookInspection Result;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 3
	UAssetManager* AssetManager = UAssetManager::GetIfValid();
#else
	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
#endif
	if (!AssetManager || CatalogPath.IsNull())
	{
		return Result;
	}

	FPrimaryAssetTypeInfo TypeInfo;
	const FPrimaryAssetType CatalogType = UPaper2DPlusCharacterCatalogAsset::CharacterCatalogPrimaryAssetType();
	Result.bTypeRegistered = AssetManager->GetPrimaryAssetTypeInfo(CatalogType, TypeInfo);
	if (Result.bTypeRegistered)
	{
		Result.bTypeIsEditorOnly = TypeInfo.bIsEditorOnly;
	}

	const FPrimaryAssetId RegisteredId = AssetManager->GetPrimaryAssetIdForPath(CatalogPath);
	Result.RegisteredPrimaryAssetType = RegisteredId.PrimaryAssetType.GetName();
	Result.bCatalogPathRegistered = RegisteredId.IsValid()
		&& RegisteredId.PrimaryAssetType == CatalogType;
	if (Result.bCatalogPathRegistered)
	{
		Result.bProductionCookBlocked = CookRuleBlocksProduction(
			AssetManager->GetPrimaryAssetRules(RegisteredId).CookRule);
	}
	return Result;
}

FPaper2DPlusCharacterCatalogAuditReport FCharacterCatalogAuditService::BuildReport(
	const UPaper2DPlusCharacterCatalogAsset* CatalogAsset,
	const TArray<FAssetData>& Assets,
	const FPaper2DPlusCharacterCatalogSettingsSnapshot& Settings,
	const FNativeAssetResolver& AssetResolver,
	const FCookRegistrationInspector& CookInspector,
	const FRowProgressCallback& RowProgress)
{
	FPaper2DPlusCharacterCatalogAuditReport Report;
	Report.CatalogPath = CatalogAsset
		? FSoftObjectPath(CatalogAsset->GetPathName())
		: Settings.DefaultCatalog.ToSoftObjectPath();
	const FName CatalogType = UPaper2DPlusCharacterCatalogAsset::StaticClass()->GetFName();

	const bool bHasCatalog = CatalogAsset != nullptr && Report.CatalogPath.IsValid();
	const bool bAuthorityMatches = bHasCatalog
		&& PathsEqual(Settings.DefaultCatalog.ToSoftObjectPath(), Report.CatalogPath);
	if (!bAuthorityMatches)
	{
		FPaper2DPlusValidationIssue Issue = MakeCatalogIssue(
			Report.CatalogPath,
			EPaper2DPlusValidationSeverity::Error,
			TEXT("Paper2DPlus.Catalog.Settings.InvalidAuthority"),
			FSoftObjectPath(),
			TEXT("Settings"),
			TEXT("DefaultCharacterCatalog"),
			LOCTEXT("InvalidAuthority", "This Catalog is not the project-designated Paper2DPlus Character Catalog."),
			LOCTEXT("InvalidAuthorityRemediation", "Choose exactly one Default Character Catalog in Paper2DPlus project settings."));
		Issue.AssetType = CatalogType;
		if (Issue.ToolTarget.IsSet())
		{
			Issue.ToolTarget->ToolId = TEXT("Paper2DPlusSettings");
			Issue.ToolTarget->TabId = NAME_None;
		}
		Report.Issues.Add(MoveTemp(Issue));
	}

	if (bAuthorityMatches)
	{
		Report.CookInspection = CookInspector
			? CookInspector(Report.CatalogPath)
			: InspectHostAssetManager(Report.CatalogPath);
		if (!Report.CookInspection.IsCookReady())
		{
			FPaper2DPlusValidationIssue Issue = MakeCatalogIssue(
				Report.CatalogPath,
				EPaper2DPlusValidationSeverity::Error,
				TEXT("Paper2DPlus.Catalog.Cook.MissingRegistration"),
				FSoftObjectPath(),
				TEXT("Cook"),
				NAME_None,
				LOCTEXT("MissingCookRegistration", "The configured Character Catalog is not registered as a production-cookable Paper2DPlusCharacterCatalog Primary Asset."),
				LOCTEXT("MissingCookRegistrationRemediation", "Add a non-editor Paper2DPlusCharacterCatalog scan rule in the host project's Asset Manager settings and ensure the Catalog path is included."));
			if (Issue.ToolTarget.IsSet())
			{
				Issue.ToolTarget->ToolId = TEXT("AssetManagerSettings");
				Issue.ToolTarget->TabId = NAME_None;
			}
			Report.Issues.Add(MoveTemp(Issue));
		}
	}

	if (CatalogAsset)
	{
		TArray<FPaper2DPlusCharacterCatalogIssue> CatalogIssues;
		CatalogAsset->ValidateCharacterCatalogAsset(CatalogIssues);
		for (const FPaper2DPlusCharacterCatalogIssue& Source : CatalogIssues)
		{
			const bool bGroupIssue = Source.Code.ToString().StartsWith(TEXT("Group."));
			FPaper2DPlusValidationIssue Issue = MakeCatalogIssue(
				Report.CatalogPath,
				CatalogSeverity(Source.Severity),
				CatalogIssueCode(Source.Code),
				Source.CharacterPath,
				Source.Scope,
				Source.Field,
				Source.Message,
				bGroupIssue
					? LOCTEXT("GroupRemediation", "Edit the named group so its name and ordered membership are valid.")
					: LOCTEXT("CatalogEntryRemediation", "Edit the named Catalog entry and correct the reported field."));
			if (bGroupIssue && Issue.ToolTarget.IsSet())
			{
				// A Group.* code can arrive with a member CharacterPath, which would otherwise leave the
				// target aimed at that character; re-aim it at the group row on the Roster's rail.
				Issue.ToolTarget->ToolId = TEXT("CharacterCatalogGroups");
				Issue.ToolTarget->TabId = TEXT("CharacterCatalogEditor_Roster");
				Issue.ToolTarget->ItemIdentity = Source.Scope.ToString();
			}
			if (Source.CharacterPath.IsValid())
			{
				Issue.ItemIdentity = Source.CharacterPath.ToString();
			}
			else if (!Source.Scope.IsNone())
			{
				Issue.ItemIdentity = Source.Scope.ToString();
			}
			Report.Issues.Add(MoveTemp(Issue));
		}
	}

	const FPaper2DPlusProfileRelationshipIndex RelationshipIndex =
		FProfileRelationshipService::BuildCandidateIndex(Assets);
	TMap<FString, FNativeAssetAuditCacheEntry> NativeIssueCache;
	const TArray<FPaper2DPlusCharacterCatalogEntry> UniqueEntries = CatalogAsset
		? CatalogAsset->GetCatalogEntries()
		: TArray<FPaper2DPlusCharacterCatalogEntry>();
	const int32 RowCount = UniqueEntries.Num();
	for (int32 RowIndex = 0; RowIndex < RowCount; ++RowIndex)
	{
		const FPaper2DPlusCharacterCatalogEntry& Entry = UniqueEntries[RowIndex];
		const FSoftObjectPath CharacterPath = Entry.CharacterProfile.ToSoftObjectPath();
		if (RowProgress)
		{
			RowProgress(RowIndex, RowCount, CharacterPath);
		}
		FPaper2DPlusCharacterCatalogAuditRow& AuditRow = Report.Rows.AddDefaulted_GetRef();
		AuditRow.CharacterPath = CharacterPath;
		AuditRow.RuntimeCompletion = CatalogAsset->GetEntryCompletion(Entry);

		const bool bCharacterAssetPresent = HasAssetOfClass(
			Assets, CharacterPath, UPaper2DPlusCharacterProfileAsset::StaticClass());
		if (!bCharacterAssetPresent)
		{
			Report.Issues.Add(MakeCatalogIssue(
				Report.CatalogPath,
				EPaper2DPlusValidationSeverity::Error,
				TEXT("Paper2DPlus.Catalog.Entry.MissingCharacterAsset"),
				CharacterPath,
				TEXT("Entry"),
				TEXT("CharacterProfile"),
				FText::Format(
					LOCTEXT("MissingCharacterAsset", "Character Profile '{0}' is missing from the Asset Registry."),
					FText::FromString(CharacterPath.ToString())),
				LOCTEXT("MissingCharacterAssetRemediation", "Restore the asset or remove this character from the Catalog.")));
		}

		for (const EPaper2DPlusCatalogCompanion Companion : {
			EPaper2DPlusCatalogCompanion::Layer,
			EPaper2DPlusCatalogCompanion::Effect,
			EPaper2DPlusCatalogCompanion::Combat })
		{
			if (!IsRequired(Entry, Companion)
				|| !CompanionPath(Entry, Companion).IsNull())
			{
				continue;
			}
			const FName Field = CompanionField(Companion);
			Report.Issues.Add(MakeCatalogIssue(
				Report.CatalogPath,
				EPaper2DPlusValidationSeverity::Error,
				TEXT("Paper2DPlus.Catalog.Entry.MissingRequiredCompanion"),
				CharacterPath,
				TEXT("Completion"),
				Field,
				FText::Format(
					LOCTEXT("MissingRequiredCompanion", "Character '{0}' is missing required companion '{1}'."),
					FText::FromString(CharacterPath.ToString()),
					FText::FromName(Field)),
				LOCTEXT("MissingRequiredCompanionRemediation", "Assign the companion or mark that slot optional.")));
		}

		AddAssignedCompanionIssues(
			Report.CatalogPath,
			CharacterPath,
			Entry,
			EPaper2DPlusCatalogCompanion::Layer,
			RelationshipIndex,
			Report.Issues);
		AddAssignedCompanionIssues(
			Report.CatalogPath,
			CharacterPath,
			Entry,
			EPaper2DPlusCatalogCompanion::Combat,
			RelationshipIndex,
			Report.Issues);

		const FSoftObjectPath EffectPath = Entry.EffectProfile.ToSoftObjectPath();
		if (!EffectPath.IsNull()
			&& !HasAssetOfClass(Assets, EffectPath, UPaper2DPlusEffectProfileAsset::StaticClass()))
		{
			FPaper2DPlusValidationIssue Issue = MakeCatalogIssue(
				Report.CatalogPath,
				EPaper2DPlusValidationSeverity::Error,
				TEXT("Paper2DPlus.Catalog.Effect.DeletedAssignment"),
				CharacterPath,
				TEXT("Relationship"),
				TEXT("EffectProfile"),
				FText::Format(
					LOCTEXT("DeletedEffectAssignment", "Assigned Effect Profile '{0}' is missing or is not an Effect Profile."),
					FText::FromString(EffectPath.ToString())),
				LOCTEXT("DeletedEffectAssignmentRemediation", "Restore the Effect Profile, assign another library, or clear the optional assignment."));
			SetAffectedAsset(Issue, EffectPath, UPaper2DPlusEffectProfileAsset::StaticClass()->GetFName());
			Report.Issues.Add(MoveTemp(Issue));
		}

		if (bCharacterAssetPresent)
		{
			UObject* ResolvedCharacter = AppendNativeIssues(
				CharacterPath,
				CharacterPath,
				AssetResolver,
				NativeIssueCache,
				Report.Issues);
			if (AssetResolver)
			{
				if (const UPaper2DPlusCharacterProfileAsset* Profile =
					Cast<UPaper2DPlusCharacterProfileAsset>(ResolvedCharacter))
				{
					AppendCoverageIssues(
						*CatalogAsset,
						Report.CatalogPath,
						Entry,
						*Profile,
						Report.Issues);
				}
			}
		}

		const FSoftObjectPath LayerPath = Entry.LayerProfile.ToSoftObjectPath();
		if (HasAssetOfClass(Assets, LayerPath, UPaper2DPlusCharacterLayerAsset::StaticClass()))
		{
			AppendNativeIssues(LayerPath, CharacterPath, AssetResolver, NativeIssueCache, Report.Issues);
		}
		if (HasAssetOfClass(Assets, EffectPath, UPaper2DPlusEffectProfileAsset::StaticClass()))
		{
			AppendNativeIssues(EffectPath, CharacterPath, AssetResolver, NativeIssueCache, Report.Issues);
		}
		const FSoftObjectPath CombatPath = Entry.CombatProfile.ToSoftObjectPath();
		if (HasAssetOfClass(Assets, CombatPath, UPaper2DPlusCombatProfileAsset::StaticClass()))
		{
			AppendNativeIssues(CombatPath, CharacterPath, AssetResolver, NativeIssueCache, Report.Issues);
		}
	}

	Report.Rows.Sort([](
		const FPaper2DPlusCharacterCatalogAuditRow& A,
		const FPaper2DPlusCharacterCatalogAuditRow& B)
	{
		return PathLess(A.CharacterPath, B.CharacterPath);
	});
	FPaper2DPlusValidationService::NormalizeAndSort(Report.Issues);

	Report.Summary.NumCharacters = Report.Rows.Num();
	Report.Summary.Issues = FPaper2DPlusValidationSummary::FromIssues(Report.Issues);
	for (FPaper2DPlusCharacterCatalogAuditRow& Row : Report.Rows)
	{
		TArray<FPaper2DPlusValidationIssue> RowIssues;
		for (const FPaper2DPlusValidationIssue& Issue : Report.Issues)
		{
			if (PathsEqual(Issue.CharacterPath, Row.CharacterPath))
			{
				RowIssues.Add(Issue);
			}
		}
		Row.IssueSummary = FPaper2DPlusValidationSummary::FromIssues(RowIssues);
		Row.bAuditComplete = Row.RuntimeCompletion.bComplete && !Row.IssueSummary.HasErrors();
		if (Row.RuntimeCompletion.bComplete) ++Report.Summary.NumRuntimeComplete;
		if (Row.bAuditComplete) ++Report.Summary.NumAuditComplete;
		if (Row.IssueSummary.HasWarnings()) ++Report.Summary.NumRowsWithWarnings;
		if (Row.IssueSummary.HasErrors()) ++Report.Summary.NumRowsWithErrors;
	}
	return Report;
}

#undef LOCTEXT_NAMESPACE
