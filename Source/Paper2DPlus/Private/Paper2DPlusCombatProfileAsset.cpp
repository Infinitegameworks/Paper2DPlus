// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusCombatProfileAsset.h"

#include "Paper2DPlusCombatScoring.h"
#include "GameplayTagsManager.h"

namespace
{
	const FName Paper2DPlusCombat_DefaultProfileName(TEXT("Default"));

	bool CombatProfile_MoveNamesEqual(FName Left, const FString& Right)
	{
		return Left.ToString().Equals(Right, ESearchCase::IgnoreCase);
	}

	FString CombatProfile_NormalizeFlipbookPath(const FSoftObjectPath& Path)
	{
		FString Normalized = Path.ToString();
		Normalized.TrimStartAndEndInline();
		Normalized.ToLowerInline();
		return Normalized;
	}

	FString CombatProfile_MakeMoveKey(const FFlipbookProfileEntry& Entry)
	{
		const FString FlipbookPath = CombatProfile_NormalizeFlipbookPath(Entry.Identity.Flipbook.ToSoftObjectPath());
		return FlipbookPath.IsEmpty()
			? FString(TEXT("name:")) + Entry.Identity.FlipbookName.ToLower()
			: FString(TEXT("path:")) + FlipbookPath;
	}

	FVector2D CombatProfile_MakeForwardRange(const FBox2D& Bounds)
	{
		if (!Bounds.bIsValid)
		{
			return FVector2D::ZeroVector;
		}

		const float MinX = FMath::Min(Bounds.Min.X, Bounds.Max.X);
		const float MaxX = FMath::Max(Bounds.Min.X, Bounds.Max.X);
		if (MaxX < 0.0f)
		{
			return FVector2D(FMath::Abs(MaxX), FMath::Abs(MinX));
		}

		return FVector2D(FMath::Max(0.0f, MinX), FMath::Max(0.0f, MaxX));
	}

	struct FCombatProfileAttackReachData
	{
		FBox2D LocalAttackBounds = FBox2D(ForceInit);
		FBox2D EffectiveAttackBounds = FBox2D(ForceInit);
		FVector2D HitboxForwardRange = FVector2D::ZeroVector;
		FVector2D EffectiveForwardRange = FVector2D::ZeroVector;
		FVector2D RootMotionAttackOffsetRange = FVector2D::ZeroVector;
	};

	void CombatProfile_AccumulateBox(FBox2D& Accumulator, const FBox2D& Box)
	{
		if (!Box.bIsValid)
		{
			return;
		}

		if (!Accumulator.bIsValid)
		{
			Accumulator = Box;
			return;
		}

		Accumulator.Min.X = FMath::Min(Accumulator.Min.X, Box.Min.X);
		Accumulator.Min.Y = FMath::Min(Accumulator.Min.Y, Box.Min.Y);
		Accumulator.Max.X = FMath::Max(Accumulator.Max.X, Box.Max.X);
		Accumulator.Max.Y = FMath::Max(Accumulator.Max.Y, Box.Max.Y);
	}

	FBox2D CombatProfile_OffsetBox(const FBox2D& Box, const FVector2D& Offset)
	{
		if (!Box.bIsValid)
		{
			return FBox2D(ForceInit);
		}

		FBox2D OffsetBox(ForceInit);
		OffsetBox += Box.Min + Offset;
		OffsetBox += Box.Max + Offset;
		return OffsetBox;
	}

	void CombatProfile_AccumulateRangeSample(FVector2D& Range, bool& bHasSample, float Value)
	{
		if (!bHasSample)
		{
			Range = FVector2D(Value, Value);
			bHasSample = true;
			return;
		}

		Range.X = FMath::Min(Range.X, Value);
		Range.Y = FMath::Max(Range.Y, Value);
	}

	FCombatProfileAttackReachData CombatProfile_ComputeAttackReachData(const FFlipbookProfileEntry& Entry)
	{
		FCombatProfileAttackReachData ReachData;
		bool bHasRootMotionOffsetSample = false;

		for (int32 FrameIndex = 0; FrameIndex < Entry.CombatData.Frames.Num(); ++FrameIndex)
		{
			const FFrameHitboxData& Frame = Entry.CombatData.Frames[FrameIndex];
			const FVector2D RootMotionOffset = Entry.MotionData.RootMotion.IsValidIndex(FrameIndex)
				? Entry.MotionData.RootMotion[FrameIndex].Position
				: FVector2D::ZeroVector;

			bool bFrameHasAttack = false;
			for (const FHitboxData& Hitbox : Frame.Hitboxes)
			{
				if (Hitbox.Type != EHitboxType::Attack)
				{
					continue;
				}

				bFrameHasAttack = true;
				const FBox2D HitboxBounds = Hitbox.GetBox2D();
				CombatProfile_AccumulateBox(ReachData.LocalAttackBounds, HitboxBounds);
				CombatProfile_AccumulateBox(ReachData.EffectiveAttackBounds, CombatProfile_OffsetBox(HitboxBounds, RootMotionOffset));
			}

			if (bFrameHasAttack)
			{
				CombatProfile_AccumulateRangeSample(ReachData.RootMotionAttackOffsetRange, bHasRootMotionOffsetSample, RootMotionOffset.X);
			}
		}

		if (!ReachData.EffectiveAttackBounds.bIsValid)
		{
			ReachData.EffectiveAttackBounds = ReachData.LocalAttackBounds;
		}

		ReachData.HitboxForwardRange = CombatProfile_MakeForwardRange(ReachData.LocalAttackBounds);
		ReachData.EffectiveForwardRange = CombatProfile_MakeForwardRange(ReachData.EffectiveAttackBounds);
		return ReachData;
	}

	bool CombatProfile_VariableValuesEqual(
		const FPaper2DPlusCombatVariableValue& A,
		const FPaper2DPlusCombatVariableValue& B)
	{
		return A.BoolValue == B.BoolValue
			&& A.IntValue == B.IntValue
			&& A.FloatValue == B.FloatValue
			&& A.NameValue == B.NameValue
			&& A.StringValue == B.StringValue
			&& A.TagValue == B.TagValue
			&& A.TagContainerValue == B.TagContainerValue
			&& A.Vector2DValue == B.Vector2DValue
			&& A.VectorValue == B.VectorValue;
	}
}

FPrimaryAssetId UPaper2DPlusCombatProfileAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("Paper2DPlusCombatProfile"), GetFName());
}

void UPaper2DPlusCombatProfileAsset::PostLoad()
{
	Super::PostLoad();
	MigrateVariableIdentity();
	RefreshAttackOptionMoveBindings();
	RebuildVariableBags();
}

void UPaper2DPlusCombatProfileAsset::PostInitProperties()
{
	Super::PostInitProperties();
	// Keep the native CDO at the legacy sentinel. Tagged package serialization delta-compares an
	// instance with its archetype: a current CDO would let schema 1 be omitted on save, then the
	// RF_NeedLoad sentinel below would make that current package reopen as legacy.
	VariableOverrideSchemaVersion = HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad)
		? 0
		: CurrentVariableOverrideSchemaVersion;
}

void UPaper2DPlusCombatProfileAsset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsTransacting())
	{
		// Schema 0 intentionally equals the CDO so pre-schema packages can omit it. Transactions also
		// delta-serialize against the CDO, so carry the byte explicitly or Undo would restore dense rows
		// while leaving the live object on schema 1.
		Ar << VariableOverrideSchemaVersion;
	}
}

void UPaper2DPlusCombatProfileAsset::MigrateVariableIdentity()
{
	// Pre-tag assets stored variable identity as a free-form FName. Best-effort fold each into a registered
	// Paper2DPlus.Combat.Var.<Name> tag. The feature shipped FName-keyed only briefly with no content
	// predating this, so an unresolved legacy name simply stays empty and is flagged by validation.
	auto ResolveLegacyTag = [](FName Legacy) -> FGameplayTag
	{
		if (Legacy.IsNone())
		{
			return FGameplayTag();
		}
		const FString Path = FString::Printf(TEXT("Paper2DPlus.Combat.Var.%s"), *Legacy.ToString());
		return UGameplayTagsManager::Get().RequestGameplayTag(FName(*Path), /*ErrorIfNotFound=*/false);
	};

	for (FPaper2DPlusCombatVariableDefinition& Definition : VariableDefinitions)
	{
		if (!Definition.VariableTag.IsValid() && !Definition.VariableName_DEPRECATED.IsNone())
		{
			Definition.VariableTag = ResolveLegacyTag(Definition.VariableName_DEPRECATED);
			if (Definition.VariableTag.IsValid())
			{
				// Clear the deprecated name only after a successful fold so it stops re-serializing; if the
				// legacy tag isn't registered yet, keep it for a later retry (migration hygiene — matches
				// MigrateLoadedFlipbookSubStructs's clear-after-move convention).
				Definition.VariableName_DEPRECATED = NAME_None;
			}
		}
	}

	auto MigrateConsiderations = [&ResolveLegacyTag](TArray<FPaper2DPlusCombatConsideration>& Considerations)
	{
		for (FPaper2DPlusCombatConsideration& Consideration : Considerations)
		{
			if (!Consideration.VariableTag.IsValid() && !Consideration.VariableName_DEPRECATED.IsNone())
			{
				Consideration.VariableTag = ResolveLegacyTag(Consideration.VariableName_DEPRECATED);
				if (Consideration.VariableTag.IsValid())
				{
					// Clear after a successful fold (migration hygiene, audit).
					Consideration.VariableName_DEPRECATED = NAME_None;
				}
			}
		}
	};
	for (FPaper2DPlusCombatTagDefaults& Defaults : TagDefaults) { MigrateConsiderations(Defaults.Considerations); }
	for (FPaper2DPlusCombatAttackOption& Option : AttackOptions) { MigrateConsiderations(Option.Considerations); }
	for (FPaper2DPlusCombatScoringProfile& Profile : ScoringProfiles) { MigrateConsiderations(Profile.GlobalConsiderations); }
}

void UPaper2DPlusCombatProfileAsset::RebuildVariableBag(
	TMap<FGameplayTag, FPaper2DPlusCombatVariableValue>& Variables,
	bool bEnsureDefinedEntries) const
{
	// Every scope prunes stale keys. Only the global bag is complete; tag, move, and scenario bags are sparse
	// override maps so a missing key can continue down the documented runtime inheritance chain.
	TSet<FGameplayTag> DefinedTags;
	DefinedTags.Reserve(VariableDefinitions.Num());
	for (const FPaper2DPlusCombatVariableDefinition& Definition : VariableDefinitions)
	{
		if (Definition.VariableTag.IsValid())
		{
			DefinedTags.Add(Definition.VariableTag);
		}
	}

	for (TMap<FGameplayTag, FPaper2DPlusCombatVariableValue>::TIterator It = Variables.CreateIterator(); It; ++It)
	{
		if (!DefinedTags.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}

	if (bEnsureDefinedEntries)
	{
		for (const FGameplayTag& Tag : DefinedTags)
		{
			Variables.FindOrAdd(Tag);
		}
	}
}

void UPaper2DPlusCombatProfileAsset::RebuildVariableBags()
{
	RebuildVariableBag(GlobalVariables);

	for (FPaper2DPlusCombatTagDefaults& Defaults : TagDefaults)
	{
		RebuildVariableBag(Defaults.Variables, /*bEnsureDefinedEntries=*/false);
	}

	for (FPaper2DPlusCombatAttackOption& Option : AttackOptions)
	{
		RebuildVariableBag(Option.Variables, /*bEnsureDefinedEntries=*/false);
	}

	for (FPaper2DPlusCombatScenarioPreset& Preset : ScenarioPresets)
	{
		RebuildVariableBag(Preset.Context.RuntimeVariables, /*bEnsureDefinedEntries=*/false);
	}
}

#if WITH_EDITOR
int32 UPaper2DPlusCombatProfileAsset::AdoptSparseVariableOverrides()
{
	int32 Removed = 0;
	auto RemoveNoOpOverrides = [&Removed](
		TMap<FGameplayTag, FPaper2DPlusCombatVariableValue>& Overrides,
		auto&& ResolveInherited)
	{
		for (TMap<FGameplayTag, FPaper2DPlusCombatVariableValue>::TIterator It = Overrides.CreateIterator();
			It;
			++It)
		{
			const FPaper2DPlusCombatVariableValue* Inherited = ResolveInherited(It.Key());
			if (Inherited && CombatProfile_VariableValuesEqual(It.Value(), *Inherited))
			{
				It.RemoveCurrent();
				++Removed;
			}
		}
	};

	auto ResolveGlobal = [this](FGameplayTag Tag) -> const FPaper2DPlusCombatVariableValue*
	{
		return GlobalVariables.Find(Tag);
	};
	for (FPaper2DPlusCombatTagDefaults& Defaults : TagDefaults)
	{
		RemoveNoOpOverrides(Defaults.Variables, ResolveGlobal);
	}
	for (FPaper2DPlusCombatAttackOption& Option : AttackOptions)
	{
		const FPaper2DPlusCombatTagDefaults* Defaults = FindTagDefaults(Option.AttackTag);
		RemoveNoOpOverrides(
			Option.Variables,
			[this, Defaults](FGameplayTag Tag) -> const FPaper2DPlusCombatVariableValue*
			{
				if (Defaults)
				{
					if (const FPaper2DPlusCombatVariableValue* Value = Defaults->Variables.Find(Tag))
					{
						return Value;
					}
				}
				return GlobalVariables.Find(Tag);
			});
	}
	for (FPaper2DPlusCombatScenarioPreset& Preset : ScenarioPresets)
	{
		const FPaper2DPlusCombatAttackOption* Option = FindAttackOption(Preset.AttackerMove);
		const FGameplayTag AttackTag = Option
			? Option->AttackTag
			: FindFirstAttackTagForMove(Preset.AttackerMove);
		const FPaper2DPlusCombatTagDefaults* Defaults = FindTagDefaults(AttackTag);
		RemoveNoOpOverrides(
			Preset.Context.RuntimeVariables,
			[this, Option, Defaults](FGameplayTag Tag) -> const FPaper2DPlusCombatVariableValue*
			{
				if (Option)
				{
					if (const FPaper2DPlusCombatVariableValue* Value = Option->Variables.Find(Tag))
					{
						return Value;
					}
				}
				if (Defaults)
				{
					if (const FPaper2DPlusCombatVariableValue* Value = Defaults->Variables.Find(Tag))
					{
						return Value;
					}
				}
				return GlobalVariables.Find(Tag);
			});
	}

	VariableOverrideSchemaVersion = CurrentVariableOverrideSchemaVersion;
	RebuildVariableBags();
	return Removed;
}

void UPaper2DPlusCombatProfileAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Editing a variable's identity/type — or adding/removing definitions — changes the value-bag schema.
	// Rebuild the bags automatically so authored values stay aligned (replaces the manual "Rebuild
	// Variables" step).
	// Use the public FProperty fields directly (GetMemberPropertyName() is 5.1+; the fields exist on all versions).
	const FName MemberName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (MemberName == GET_MEMBER_NAME_CHECKED(UPaper2DPlusCombatProfileAsset, VariableDefinitions)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatVariableDefinition, VariableTag)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatVariableDefinition, Type))
	{
		RebuildVariableBags();
	}

	if (MemberName == GET_MEMBER_NAME_CHECKED(UPaper2DPlusCombatProfileAsset, CharacterProfile)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UPaper2DPlusCombatProfileAsset, AttackOptions)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatAttackOption, MoveName)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FPaper2DPlusCombatAttackOption, MoveFlipbook))
	{
		RefreshAttackOptionMoveBindings();
	}
}

#if WITH_EDITOR
bool UPaper2DPlusCombatProfileAsset::RenameVariableTag(FGameplayTag OldTag, FGameplayTag NewTag)
{
	if (!OldTag.IsValid() || !NewTag.IsValid() || OldTag == NewTag)
	{
		return false;
	}

	auto RemapValues = [OldTag, NewTag](TMap<FGameplayTag, FPaper2DPlusCombatVariableValue>& Values)
	{
		if (const FPaper2DPlusCombatVariableValue* Existing = Values.Find(OldTag))
		{
			if (!Values.Contains(NewTag))
			{
				Values.Add(NewTag, *Existing);
			}
			Values.Remove(OldTag);
		}
	};
	auto RemapConsiderations = [OldTag, NewTag](TArray<FPaper2DPlusCombatConsideration>& Considerations)
	{
		for (FPaper2DPlusCombatConsideration& Consideration : Considerations)
		{
			if (Consideration.VariableTag == OldTag)
			{
				Consideration.VariableTag = NewTag;
			}
		}
	};

	RemapValues(GlobalVariables);
	for (FPaper2DPlusCombatTagDefaults& Defaults : TagDefaults)
	{
		RemapValues(Defaults.Variables);
		RemapConsiderations(Defaults.Considerations);
	}
	for (FPaper2DPlusCombatAttackOption& Option : AttackOptions)
	{
		RemapValues(Option.Variables);
		RemapConsiderations(Option.Considerations);
	}
	for (FPaper2DPlusCombatScoringProfile& Profile : ScoringProfiles)
	{
		RemapConsiderations(Profile.GlobalConsiderations);
	}
	for (FPaper2DPlusCombatScenarioPreset& Preset : ScenarioPresets)
	{
		RemapValues(Preset.Context.RuntimeVariables);
	}
	return true;
}
#endif
#endif

const FFlipbookProfileEntry* UPaper2DPlusCombatProfileAsset::FindMoveByFlipbookPath(
	const FSoftObjectPath& FlipbookPath) const
{
	if (!CharacterProfile)
	{
		return nullptr;
	}

	const FString WantedPath = CombatProfile_NormalizeFlipbookPath(FlipbookPath);
	if (WantedPath.IsEmpty())
	{
		return nullptr;
	}

	for (const FFlipbookProfileEntry& Entry : CharacterProfile->Flipbooks)
	{
		if (CombatProfile_NormalizeFlipbookPath(Entry.Identity.Flipbook.ToSoftObjectPath()) == WantedPath)
		{
			return &Entry;
		}
	}
	return nullptr;
}

const FFlipbookProfileEntry* UPaper2DPlusCombatProfileAsset::FindUniqueMoveByName(
	FName MoveName,
	bool* bOutAmbiguous) const
{
	if (bOutAmbiguous)
	{
		*bOutAmbiguous = false;
	}
	if (!CharacterProfile || MoveName.IsNone())
	{
		return nullptr;
	}

	const FFlipbookProfileEntry* Match = nullptr;
	for (const FFlipbookProfileEntry& Entry : CharacterProfile->Flipbooks)
	{
		if (!CombatProfile_MoveNamesEqual(MoveName, Entry.Identity.FlipbookName))
		{
			continue;
		}
		if (Match)
		{
			if (bOutAmbiguous)
			{
				*bOutAmbiguous = true;
			}
			return nullptr;
		}
		Match = &Entry;
	}
	return Match;
}

const FFlipbookProfileEntry* UPaper2DPlusCombatProfileAsset::ResolveAttackOptionMove(
	const FPaper2DPlusCombatAttackOption& Option,
	bool* bOutAmbiguousLegacyName) const
{
	if (bOutAmbiguousLegacyName)
	{
		*bOutAmbiguousLegacyName = false;
	}

	if (!Option.MoveFlipbook.IsNull())
	{
		// A canonical identity is authoritative. Never retarget a dangling object path by display name.
		return FindMoveByFlipbookPath(Option.MoveFlipbook.ToSoftObjectPath());
	}
	return FindUniqueMoveByName(Option.MoveName, bOutAmbiguousLegacyName);
}

int32 UPaper2DPlusCombatProfileAsset::RefreshAttackOptionMoveBindings()
{
	if (!CharacterProfile)
	{
		return 0;
	}

	int32 ChangedRows = 0;
	for (FPaper2DPlusCombatAttackOption& Option : AttackOptions)
	{
		bool bChanged = false;
		if (!Option.MoveFlipbook.IsNull())
		{
			if (const FFlipbookProfileEntry* Entry = FindMoveByFlipbookPath(Option.MoveFlipbook.ToSoftObjectPath()))
			{
				const FName CurrentName(*Entry->Identity.FlipbookName);
				if (!Option.MoveName.IsEqual(CurrentName, ENameCase::IgnoreCase))
				{
					Option.MoveName = CurrentName;
					bChanged = true;
				}
			}
		}
		else
		{
			if (const FFlipbookProfileEntry* Entry = FindUniqueMoveByName(Option.MoveName))
			{
				if (!Entry->Identity.Flipbook.IsNull())
				{
					Option.MoveFlipbook = Entry->Identity.Flipbook;
					bChanged = true;
				}
				const FName CurrentName(*Entry->Identity.FlipbookName);
				if (!Option.MoveName.IsEqual(CurrentName, ENameCase::IgnoreCase))
				{
					Option.MoveName = CurrentName;
					bChanged = true;
				}
			}
		}

		if (bChanged)
		{
			++ChangedRows;
		}
	}
	return ChangedRows;
}

int32 UPaper2DPlusCombatProfileAsset::GenerateAttackOptionsFromCharacterProfile(bool bOnlyMissing)
{
	if (!CharacterProfile)
	{
		return 0;
	}

	int32 Added = 0;
	for (const FFlipbookProfileEntry& Entry : CharacterProfile->Flipbooks)
	{
		if (Entry.Identity.FlipbookName.IsEmpty())
		{
			continue;
		}

		const FName MoveName(*Entry.Identity.FlipbookName);
		if (bOnlyMissing && FindAttackOption(Entry))
		{
			continue;
		}

		const FGameplayTag InheritedAttackTag = FindFirstAttackTagForMove(MoveName);
		if (!InheritedAttackTag.IsValid())
		{
			continue;
		}

		const FCombatProfileAttackReachData ReachData = CombatProfile_ComputeAttackReachData(Entry);
		if (!ReachData.LocalAttackBounds.bIsValid)
		{
			continue;
		}

		FPaper2DPlusCombatAttackOption Option;
		Option.MoveName = MoveName;
		Option.MoveFlipbook = Entry.Identity.Flipbook;

		const FPaper2DPlusCombatTagDefaults* Defaults = FindTagDefaults(InheritedAttackTag);
		if (Defaults)
		{
			Option.RoleTags.AppendTags(Defaults->RoleTags);
			Option.BaseWeight = Defaults->BaseWeight;
			Option.bOverridePreferredRange = Defaults->bOverridePreferredRange;
			Option.PreferredRangeLocal = Defaults->PreferredRangeLocal;
			// Tag considerations stay inherited. Copying them into the move row makes
			// scoring apply the same rule at tag and move scope, silently doubling it.
		}

		// New move rows begin with no variable overrides and inherit tag/global values dynamically.
		RebuildVariableBag(Option.Variables, /*bEnsureDefinedEntries=*/false);
		AttackOptions.Add(MoveTemp(Option));
		++Added;
	}

	return Added;
}

void UPaper2DPlusCombatProfileAsset::AddValidationIssue(
	TArray<FPaper2DPlusCombatValidationIssue>& OutIssues,
	EPaper2DPlusCombatValidationSeverity Severity,
	const FText& Message,
	FName Field,
	FName MoveName) const
{
	FPaper2DPlusCombatValidationIssue Issue;
	Issue.Severity = Severity;
	Issue.Message = Message;
	Issue.Field = Field;
	Issue.MoveName = MoveName;
	OutIssues.Add(MoveTemp(Issue));
}

bool UPaper2DPlusCombatProfileAsset::ValidateCombatProfileAsset(TArray<FPaper2DPlusCombatValidationIssue>& OutIssues) const
{
	OutIssues.Reset();
	if (HasLegacyDenseVariableOverrides())
	{
		AddValidationIssue(
			OutIssues,
			EPaper2DPlusCombatValidationSeverity::Info,
			NSLOCTEXT(
				"Paper2DPlus",
				"CombatProfileLegacyDenseVariableOverrides",
				"Legacy variable scope rows are preserved as explicit snapshots until inheritance is reviewed."),
			TEXT("VariableOverrideSchemaVersion"));
	}

	if (!CharacterProfile)
	{
		AddValidationIssue(
			OutIssues,
			EPaper2DPlusCombatValidationSeverity::Error,
			NSLOCTEXT("Paper2DPlus", "CombatProfileMissingCharacterProfile", "Combat Profile has no Character Profile."),
			TEXT("CharacterProfile"));
		return false;
	}

	TSet<FGameplayTag> VariableTags;
	for (const FPaper2DPlusCombatVariableDefinition& Definition : VariableDefinitions)
	{
		if (!Definition.VariableTag.IsValid())
		{
			AddValidationIssue(
				OutIssues,
				EPaper2DPlusCombatValidationSeverity::Error,
				NSLOCTEXT("Paper2DPlus", "CombatProfileEmptyVariableName", "A combat variable definition has an empty name."),
				TEXT("VariableDefinitions"));
			continue;
		}

		if (VariableTags.Contains(Definition.VariableTag))
		{
			AddValidationIssue(
				OutIssues,
				EPaper2DPlusCombatValidationSeverity::Error,
				FText::Format(NSLOCTEXT("Paper2DPlus", "CombatProfileDuplicateVariableName", "Combat variable '{0}' is defined more than once."), FText::FromName(Definition.VariableTag.GetTagName())),
				TEXT("VariableDefinitions"));
		}

		VariableTags.Add(Definition.VariableTag);
	}

	TSet<FString> OptionMoveIdentities;
	for (const FPaper2DPlusCombatAttackOption& Option : AttackOptions)
	{
		bool bAmbiguousLegacyName = false;
		const FFlipbookProfileEntry* ResolvedMove = ResolveAttackOptionMove(Option, &bAmbiguousLegacyName);
		const FName ResolvedMoveName = ResolvedMove
			? FName(*ResolvedMove->Identity.FlipbookName)
			: Option.MoveName;

		if (Option.MoveFlipbook.IsNull() && Option.MoveName.IsNone())
		{
			AddValidationIssue(
				OutIssues,
				EPaper2DPlusCombatValidationSeverity::Error,
				NSLOCTEXT("Paper2DPlus", "CombatProfileEmptyMoveIdentity", "An attack option has neither a Move Flipbook nor a legacy Move Name."),
				TEXT("AttackOptions"));
			continue;
		}

		FString OptionIdentity;
		if (ResolvedMove)
		{
			OptionIdentity = CombatProfile_MakeMoveKey(*ResolvedMove);
		}
		else if (!Option.MoveFlipbook.IsNull())
		{
			OptionIdentity = FString(TEXT("path:"))
				+ CombatProfile_NormalizeFlipbookPath(Option.MoveFlipbook.ToSoftObjectPath());
		}
		else
		{
			OptionIdentity = FString(TEXT("name:")) + Option.MoveName.ToString().ToLower();
		}

		if (OptionMoveIdentities.Contains(OptionIdentity))
		{
			AddValidationIssue(
				OutIssues,
				EPaper2DPlusCombatValidationSeverity::Error,
				FText::Format(NSLOCTEXT("Paper2DPlus", "CombatProfileDuplicateAttackOption", "Attack option '{0}' is defined more than once for the same move identity."), FText::FromName(ResolvedMoveName)),
				TEXT("AttackOptions"),
				ResolvedMoveName);
		}

		OptionMoveIdentities.Add(OptionIdentity);

		if (!ResolvedMove)
		{
			if (!Option.MoveFlipbook.IsNull())
			{
				AddValidationIssue(
					OutIssues,
					EPaper2DPlusCombatValidationSeverity::Error,
					FText::Format(
						NSLOCTEXT("Paper2DPlus", "CombatProfileDanglingMoveFlipbook", "Attack option '{0}' references Move Flipbook '{1}', which is not present on the Character Profile."),
						FText::FromName(Option.MoveName),
						FText::FromString(Option.MoveFlipbook.ToSoftObjectPath().ToString())),
					TEXT("MoveFlipbook"),
					Option.MoveName);
			}
			else if (bAmbiguousLegacyName)
			{
				AddValidationIssue(
					OutIssues,
					EPaper2DPlusCombatValidationSeverity::Error,
					FText::Format(NSLOCTEXT("Paper2DPlus", "CombatProfileAmbiguousMoveName", "Legacy attack option '{0}' matches more than one Character Profile move and cannot be bound automatically."), FText::FromName(Option.MoveName)),
					TEXT("MoveName"),
					Option.MoveName);
			}
			else
			{
				AddValidationIssue(
					OutIssues,
					EPaper2DPlusCombatValidationSeverity::Error,
					FText::Format(NSLOCTEXT("Paper2DPlus", "CombatProfileDanglingMove", "Attack option '{0}' does not match a move on the Character Profile."), FText::FromName(Option.MoveName)),
					TEXT("MoveName"),
					Option.MoveName);
			}
		}

		const FGameplayTag InheritedAttackTag = ResolvedMove
			? FindFirstAttackTagForMove(ResolvedMoveName)
			: FGameplayTag();
		if (!InheritedAttackTag.IsValid() && !Option.bIncludeWhenNotTagged)
		{
			AddValidationIssue(
				OutIssues,
				EPaper2DPlusCombatValidationSeverity::Info,
				FText::Format(NSLOCTEXT("Paper2DPlus", "CombatProfileInactiveTuningRow", "Attack option '{0}' is a tuning row only: the move is not mapped by a Character Profile tag, so it will not be included unless Include When Not Tagged is enabled."), FText::FromName(ResolvedMoveName)),
				TEXT("AttackOptions"),
				ResolvedMoveName);
		}

		if (Option.AttackTag.IsValid() && !Option.bIncludeWhenNotTagged && !CharacterProfile->TagMappings.Contains(Option.AttackTag))
		{
			AddValidationIssue(
				OutIssues,
				EPaper2DPlusCombatValidationSeverity::Warning,
				FText::Format(NSLOCTEXT("Paper2DPlus", "CombatProfileOptionTagUnmapped", "Attack option '{0}' uses tag '{1}', but the Character Profile has no mapping for that tag."), FText::FromName(Option.MoveName), FText::FromString(Option.AttackTag.ToString())),
				TEXT("AttackTag"),
				ResolvedMoveName);
		}
	}

	for (const FPaper2DPlusCombatTagDefaults& Defaults : TagDefaults)
	{
		if (!Defaults.AttackTag.IsValid())
		{
			AddValidationIssue(
				OutIssues,
				EPaper2DPlusCombatValidationSeverity::Warning,
				NSLOCTEXT("Paper2DPlus", "CombatProfileEmptyTagDefault", "A tag defaults row has no AttackTag."),
				TEXT("TagDefaults"));
			continue;
		}

		if (!CharacterProfile->TagMappings.Contains(Defaults.AttackTag))
		{
			AddValidationIssue(
				OutIssues,
				EPaper2DPlusCombatValidationSeverity::Warning,
				FText::Format(NSLOCTEXT("Paper2DPlus", "CombatProfileDefaultsTagUnmapped", "Tag defaults row '{0}' is not mapped by the Character Profile."), FText::FromString(Defaults.AttackTag.ToString())),
				TEXT("TagDefaults"));
		}
	}

	for (const FPaper2DPlusCombatScoringProfile& ScoringProfile : ScoringProfiles)
	{
		if (ScoringProfile.ProfileName.IsNone())
		{
			AddValidationIssue(
				OutIssues,
				EPaper2DPlusCombatValidationSeverity::Warning,
				NSLOCTEXT("Paper2DPlus", "CombatProfileEmptyScoringProfileName", "A scoring profile has an empty ProfileName."),
				TEXT("ScoringProfiles"));
		}
	}

	return !OutIssues.ContainsByPredicate([](const FPaper2DPlusCombatValidationIssue& Issue)
	{
		return Issue.Severity == EPaper2DPlusCombatValidationSeverity::Error;
	});
}

void UPaper2DPlusCombatProfileAsset::BuildAttackCatalog(TArray<FPaper2DPlusCombatAttackDerivedData>& OutCatalog) const
{
	// Base-only by contract: equip-aware = Paper2DPlusLayerCombat::ComposeCombatFrames (the component cached tier).
	OutCatalog.Reset();
	if (!CharacterProfile)
	{
		return;
	}

	TSet<FString> AddedMoveKeys;
	auto AddCatalogRow = [this, &OutCatalog, &AddedMoveKeys](
		const FFlipbookProfileEntry& Entry,
		FGameplayTag InheritedAttackTag,
		bool bInheritedFromCharacterProfile)
	{
		if (Entry.Identity.FlipbookName.IsEmpty())
		{
			return;
		}

		const FCombatProfileAttackReachData ReachData = CombatProfile_ComputeAttackReachData(Entry);
		if (!ReachData.LocalAttackBounds.bIsValid)
		{
			return;
		}

		const FName MoveName(*Entry.Identity.FlipbookName);
		const FString MoveKey = CombatProfile_MakeMoveKey(Entry);
		if (AddedMoveKeys.Contains(MoveKey))
		{
			return;
		}

		const FPaper2DPlusCombatAttackOption* Option = FindAttackOption(Entry);
		const FGameplayTag AttackTag = InheritedAttackTag.IsValid()
			? InheritedAttackTag
			: (Option && Option->AttackTag.IsValid() ? Option->AttackTag : FGameplayTag());
		const FPaper2DPlusCombatTagDefaults* Defaults = FindTagDefaults(AttackTag);

		FPaper2DPlusCombatAttackDerivedData Row;
		Row.MoveName = MoveName;
		Row.AttackTag = AttackTag;
		Row.RoleTags.Reset();
		Row.bInheritedFromCharacterProfile = bInheritedFromCharacterProfile;
		Row.bHasCombatProfileOption = Option != nullptr;
		Row.LocalAttackBounds = ReachData.LocalAttackBounds;
		Row.EffectiveAttackBoundsLocal = ReachData.EffectiveAttackBounds;
		Row.HitboxForwardRangeLocal = ReachData.HitboxForwardRange;
		Row.RootMotionAttackOffsetRangeLocal = ReachData.RootMotionAttackOffsetRange;
		Row.ForwardRangeLocal = ReachData.EffectiveForwardRange;
		Row.PreferredRangeLocal = Row.ForwardRangeLocal;
		Row.BaseWeight = 1.0f;
		Row.bHasAttack = true;

		// Catalog path deliberately uses the per-entry form, which skips the animation-tag stamp
		// (ComputeMoveFrameData runs the O(V+E) BuildAnimationTagMap batch once PER MOVE here;
		// nothing on the scoring/catalog path reads FrameData.AnimationTags).
		// Attack Catalogs can contain hundreds of soft moves. Keep derivation load-free so the editor's
		// virtualized card boundary decides which visible thumbnails may load; explicit frame-data/export
		// queries retain their default synchronous timing lookup.
		Row.FrameData = FPaper2DPlusFrameData::ComputeMoveFrameDataForEntry(
			CharacterProfile,
			Entry,
			false);

		if (Defaults)
		{
			Row.RoleTags.AppendTags(Defaults->RoleTags);
			Row.BaseWeight = Defaults->BaseWeight;
			if (Defaults->bOverridePreferredRange)
			{
				Row.PreferredRangeLocal = Defaults->PreferredRangeLocal;
			}
		}

		if (Option)
		{
			Row.RoleTags.AppendTags(Option->RoleTags);
			Row.BaseWeight = Option->BaseWeight;
			Row.CooldownSeconds = Option->CooldownSeconds;
			if (Option->bOverridePreferredRange)
			{
				Row.PreferredRangeLocal = Option->PreferredRangeLocal;
			}
		}

		AddedMoveKeys.Add(MoveKey);
		OutCatalog.Add(MoveTemp(Row));
	};

	for (const FFlipbookProfileEntry& Entry : CharacterProfile->Flipbooks)
	{
		if (Entry.Identity.FlipbookName.IsEmpty())
		{
			continue;
		}

		const FName MoveName(*Entry.Identity.FlipbookName);
		const FGameplayTag InheritedAttackTag = FindFirstAttackTagForMove(MoveName);
		if (!InheritedAttackTag.IsValid())
		{
			continue;
		}

		AddCatalogRow(Entry, InheritedAttackTag, true);
	}

	for (const FPaper2DPlusCombatAttackOption& Option : AttackOptions)
	{
		if (!Option.bIncludeWhenNotTagged)
		{
			continue;
		}

		const FFlipbookProfileEntry* ResolvedMove = ResolveAttackOptionMove(Option);
		if (!ResolvedMove || AddedMoveKeys.Contains(CombatProfile_MakeMoveKey(*ResolvedMove)))
		{
			continue;
		}

		AddCatalogRow(*ResolvedMove, FGameplayTag(), false);
	}
}

void UPaper2DPlusCombatProfileAsset::ScoreAttackOptions(
	const FPaper2DPlusCombatRuntimeContext& Context,
	TArray<FPaper2DPlusCombatRankedOption>& OutRankedOptions,
	FName ScoringProfileName) const
{
	FPaper2DPlusCombatScoring::ScoreAttackOptions(this, Context, OutRankedOptions, ScoringProfileName);
}

bool UPaper2DPlusCombatProfileAsset::GetCombatDecision(
	const FPaper2DPlusCombatRuntimeContext& Context,
	FPaper2DPlusCombatDecision& OutDecision,
	FName ScoringProfileName) const
{
	return FPaper2DPlusCombatScoring::BuildDecision(this, Context, OutDecision, ScoringProfileName);
}

bool UPaper2DPlusCombatProfileAsset::PickWeightedCombatAttack(
	const FPaper2DPlusCombatRuntimeContext& Context,
	FRandomStream& RandomStream,
	FPaper2DPlusCombatDecision& OutDecision,
	FName ScoringProfileName) const
{
	OutDecision = FPaper2DPlusCombatDecision();

	TArray<FPaper2DPlusCombatRankedOption> RankedOptions;
	ScoreAttackOptions(Context, RankedOptions, ScoringProfileName);
	OutDecision.RankedOptions = RankedOptions;
	OutDecision.CurrentDistance = Context.DistanceToTarget;

	FPaper2DPlusCombatRankedOption Picked;
	if (!FPaper2DPlusCombatScoring::PickWeightedAttack(RankedOptions, RandomStream, Picked))
	{
		return false;
	}

	const FPaper2DPlusCombatScoringProfile* Profile = FindScoringProfile(ScoringProfileName);
	const float MinimumScore = Profile ? Profile->MinimumViableScore : 0.01f;

	OutDecision.bHasGoodAttack = Picked.Score >= MinimumScore;
	OutDecision.BestMove = Picked.Attack.MoveName;
	OutDecision.BestAttackTag = Picked.Attack.AttackTag;
	OutDecision.DesiredRangeLocal = Picked.Attack.PreferredRangeLocal;
	OutDecision.BestScore = Picked.Score;
	OutDecision.BestBreakdown = Picked.Breakdown;
	return true;
}

const FPaper2DPlusCombatAttackOption* UPaper2DPlusCombatProfileAsset::FindAttackOption(FName MoveName) const
{
	bool bAmbiguous = false;
	if (const FFlipbookProfileEntry* MoveEntry = FindUniqueMoveByName(MoveName, &bAmbiguous))
	{
		return FindAttackOption(*MoveEntry);
	}
	if (bAmbiguous)
	{
		return nullptr;
	}

	// Character-less callers and dangling legacy rows retain the old name-only lookup behavior.
	for (const FPaper2DPlusCombatAttackOption& Option : AttackOptions)
	{
		if (Option.MoveName.IsEqual(MoveName, ENameCase::IgnoreCase))
		{
			return &Option;
		}
	}
	return nullptr;
}

const FPaper2DPlusCombatAttackOption* UPaper2DPlusCombatProfileAsset::FindAttackOption(
	const FFlipbookProfileEntry& MoveEntry) const
{
	const FString MovePath = CombatProfile_NormalizeFlipbookPath(MoveEntry.Identity.Flipbook.ToSoftObjectPath());
	if (!MovePath.IsEmpty())
	{
		for (const FPaper2DPlusCombatAttackOption& Option : AttackOptions)
		{
			if (!Option.MoveFlipbook.IsNull()
				&& CombatProfile_NormalizeFlipbookPath(Option.MoveFlipbook.ToSoftObjectPath()) == MovePath)
			{
				return &Option;
			}
		}
	}

	bool bAmbiguous = false;
	const FName MoveName(*MoveEntry.Identity.FlipbookName);
	const FFlipbookProfileEntry* UniqueNameMatch = FindUniqueMoveByName(MoveName, &bAmbiguous);
	if (bAmbiguous || !UniqueNameMatch
		|| CombatProfile_MakeMoveKey(*UniqueNameMatch) != CombatProfile_MakeMoveKey(MoveEntry))
	{
		return nullptr;
	}

	for (const FPaper2DPlusCombatAttackOption& Option : AttackOptions)
	{
		if (Option.MoveFlipbook.IsNull() && Option.MoveName.IsEqual(MoveName, ENameCase::IgnoreCase))
		{
			return &Option;
		}
	}
	return nullptr;
}

const FPaper2DPlusCombatTagDefaults* UPaper2DPlusCombatProfileAsset::FindTagDefaults(FGameplayTag AttackTag) const
{
	if (!AttackTag.IsValid())
	{
		return nullptr;
	}

	for (const FPaper2DPlusCombatTagDefaults& Defaults : TagDefaults)
	{
		if (Defaults.AttackTag == AttackTag)
		{
			return &Defaults;
		}
	}
	return nullptr;
}

const FPaper2DPlusCombatScoringProfile* UPaper2DPlusCombatProfileAsset::FindScoringProfile(FName ProfileName) const
{
	const FName DesiredName = ProfileName.IsNone() ? Paper2DPlusCombat_DefaultProfileName : ProfileName;
	for (const FPaper2DPlusCombatScoringProfile& Profile : ScoringProfiles)
	{
		if (Profile.ProfileName.IsEqual(DesiredName, ENameCase::IgnoreCase))
		{
			return &Profile;
		}
	}

	if (ScoringProfiles.Num() > 0)
	{
		return &ScoringProfiles[0];
	}

	return nullptr;
}

const FPaper2DPlusCombatVariableDefinition* UPaper2DPlusCombatProfileAsset::FindVariableDefinition(FGameplayTag VariableTag) const
{
	if (!VariableTag.IsValid())
	{
		return nullptr;
	}

	for (const FPaper2DPlusCombatVariableDefinition& Definition : VariableDefinitions)
	{
		if (Definition.VariableTag == VariableTag)
		{
			return &Definition;
		}
	}

	return nullptr;
}

bool UPaper2DPlusCombatProfileAsset::TryGetFloatVariable(const TMap<FGameplayTag, FPaper2DPlusCombatVariableValue>& Variables, FGameplayTag VariableTag, float& OutValue) const
{
	const FPaper2DPlusCombatVariableValue* Value = Variables.Find(VariableTag);
	if (!Value)
	{
		return false;
	}

	// Fail closed on a type mismatch — mirrors the old PropertyBag accessor (whose property type came from the
	// variable definition): a float read only resolves for a Float (or numerically-convertible Int32) variable,
	// so a CustomFloat consideration pointing at e.g. a Bool variable doesn't silently score the default field.
	const FPaper2DPlusCombatVariableDefinition* Definition = FindVariableDefinition(VariableTag);
	if (!Definition)
	{
		return false;
	}

	switch (Definition->Type)
	{
	case EPaper2DPlusCombatVariableType::Float:
		OutValue = Value->FloatValue;
		return true;
	case EPaper2DPlusCombatVariableType::Int32:
		OutValue = static_cast<float>(Value->IntValue);
		return true;
	default:
		return false;
	}
}

bool UPaper2DPlusCombatProfileAsset::TryGetBoolVariable(const TMap<FGameplayTag, FPaper2DPlusCombatVariableValue>& Variables, FGameplayTag VariableTag, bool& bOutValue) const
{
	const FPaper2DPlusCombatVariableValue* Value = Variables.Find(VariableTag);
	if (!Value)
	{
		return false;
	}

	// Fail closed unless the variable is actually Bool-typed (see TryGetFloatVariable).
	const FPaper2DPlusCombatVariableDefinition* Definition = FindVariableDefinition(VariableTag);
	if (!Definition || Definition->Type != EPaper2DPlusCombatVariableType::Bool)
	{
		return false;
	}

	bOutValue = Value->BoolValue;
	return true;
}

FGameplayTag UPaper2DPlusCombatProfileAsset::FindFirstAttackTagForMove(FName MoveName) const
{
	if (!CharacterProfile || MoveName.IsNone())
	{
		return FGameplayTag();
	}

	for (const TPair<FGameplayTag, FFlipbookTagMapping>& Pair : CharacterProfile->TagMappings)
	{
		for (const FFlipbookTagMappingEntry& Entry : Pair.Value.Entries)
		{
			if (CombatProfile_MoveNamesEqual(MoveName, Entry.FlipbookName))
			{
				return Pair.Key;
			}
		}
	}

	return FGameplayTag();
}
