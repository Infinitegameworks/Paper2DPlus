// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "PaperZDSequenceAuthoring.h"

#include "Paper2DPlusCharacterProfileAsset.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "PaperFlipbook.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PaperZDSequenceAuthoring"

namespace Paper2DPlus::PaperZDSequenceAuthoring
{
	namespace
	{
		struct FCreateResult
		{
			bool bProfileModified = false;
			int32 CreatedCount = 0;
			int32 ReusedCount = 0;
			int32 LinkedFlipbookCount = 0;
			TArray<FString> Failures;
		};

		void ShowNotification(
			const FText& Message,
			const SNotificationItem::ECompletionState State =
				SNotificationItem::CS_Success)
		{
			FNotificationInfo Info(Message);
			Info.bFireAndForget = true;
			Info.ExpireDuration = 5.0f;
			if (const TSharedPtr<SNotificationItem> Item =
				FSlateNotificationManager::Get().AddNotification(Info))
			{
				Item->SetCompletionState(State);
			}
		}

		bool IsPaperZDPluginInstalledAndEnabled()
		{
			const TSharedPtr<IPlugin> PaperZDPlugin =
				IPluginManager::Get().FindPlugin(TEXT("PaperZD"));
			return PaperZDPlugin.IsValid() && PaperZDPlugin->IsEnabled();
		}

		struct FReflectedSequenceSchema
		{
			FObjectProperty* AnimSourceProperty = nullptr;
			FArrayProperty* AnimDataProperty = nullptr;
			FStructProperty* AnimDataElement = nullptr;
			FObjectProperty* AnimationProperty = nullptr;

			/**
			 * Where the flipbook pointer lives inside ONE element of the AnimData array.
			 *
			 * PaperZD changed this shape in v2.2, when composite animation layers arrived: a flat
			 * TArray<UPaperFlipbook*> cannot carry per-entry layers or a mirror mode, so the element
			 * became a struct. Both shapes are supported because the version installed is the user's
			 * choice, not ours -- an engine ships one PaperZD but a project can carry its own, so the
			 * boundary is NOT an engine version and must never be gated on one.
			 *
			 *   v2.2+      TArray<FPaperZDFlipbookAnimDataSource> AnimData   -> flipbook at ::Animation
			 *   pre-2.2    TArray<TObjectPtr<UPaperFlipbook>> AnimDataSource -> the element IS the flipbook
			 */
			void* GetFlipbookValuePtr(void* Element) const
			{
				if (!Element || !AnimationProperty)
				{
					return nullptr;
				}
				return AnimDataElement
					? AnimationProperty->ContainerPtrToValuePtr<void>(Element)
					: Element;
			}

			bool Accepts(
				const UClass* AnimationSourceClass,
				const UObject* AnimationSource) const
			{
				// AnimDataElement is deliberately NOT required: it is null on the legacy shape, where
				// the array element is the flipbook property itself.
				return AnimationSourceClass
					&& AnimSourceProperty
					&& AnimSourceProperty->PropertyClass
					&& AnimDataProperty
					&& AnimationProperty
					&& AnimationProperty->PropertyClass
					&& AnimationSourceClass->IsChildOf(
						AnimSourceProperty->PropertyClass)
					&& (!AnimationSource
						|| AnimationSource->IsA(
							AnimSourceProperty->PropertyClass))
					&& UPaperFlipbook::StaticClass()->IsChildOf(
						AnimationProperty->PropertyClass);
			}
		};

		FReflectedSequenceSchema ResolveSequenceSchema(
			UClass* SequenceClass)
		{
			FReflectedSequenceSchema Schema;
			if (!SequenceClass)
			{
				return Schema;
			}

			Schema.AnimSourceProperty =
				FindFProperty<FObjectProperty>(
					SequenceClass,
					TEXT("AnimSource"));
			// Current shape first (PaperZD v2.2+): AnimData is an array of structs and the flipbook
			// is the struct's Animation member.
			Schema.AnimDataProperty =
				FindFProperty<FArrayProperty>(
					SequenceClass,
					TEXT("AnimData"));
			Schema.AnimDataElement = Schema.AnimDataProperty
				? CastField<FStructProperty>(
					Schema.AnimDataProperty->Inner)
				: nullptr;
			Schema.AnimationProperty = Schema.AnimDataElement
				? FindFProperty<FObjectProperty>(
					Schema.AnimDataElement->Struct,
					TEXT("Animation"))
				: nullptr;
			if (Schema.AnimDataProperty && Schema.AnimationProperty)
			{
				return Schema;
			}

			// Legacy shape (pre-2.2, as bundled with UE 5.0 and 5.1): AnimDataSource is a flat array
			// of flipbook pointers, so the array's Inner IS the property that holds the flipbook.
			// Falling through rather than branching on a version keeps this honest if a project
			// installs its own PaperZD over the engine's — which this repository itself does.
			Schema.AnimDataElement = nullptr;
			Schema.AnimDataProperty =
				FindFProperty<FArrayProperty>(
					SequenceClass,
					TEXT("AnimDataSource"));
			Schema.AnimationProperty = Schema.AnimDataProperty
				? CastField<FObjectProperty>(
					Schema.AnimDataProperty->Inner)
				: nullptr;
			return Schema;
		}

		void GatherSequenceWorkInternal(
			UPaper2DPlusCharacterProfileAsset& Profile,
			const bool bDiscoverExistingSequences,
			const TSet<UPaperFlipbook*>* FlipbookScope,
			TMap<UPaperFlipbook*, UObject*>& OutResolvedSequences,
			TArray<TSharedPtr<FPendingSequence>>& OutPendingSequences,
			FSequenceWorkSummary* OutSummary = nullptr)
		{
			OutResolvedSequences.Reset();
			OutPendingSequences.Reset();
			if (OutSummary)
			{
				*OutSummary = FSequenceWorkSummary();
			}

			// Every in-scope, resolvable flipbook the creator could consider — INDEPENDENT of whether
			// it already has a sequence. This is what separates "nothing missing" from "nothing in
			// scope"; without it an empty Character Profile reports that all its (zero) flipbooks are
			// already covered, which is what made the pre-check disagree with Extract All.
			TSet<UPaperFlipbook*> CandidateFlipbooks;
			auto IsInScope = [FlipbookScope, &CandidateFlipbooks](UPaperFlipbook* Flipbook)
			{
				if (!Flipbook || (FlipbookScope && !FlipbookScope->Contains(Flipbook)))
				{
					return false;
				}
				CandidateFlipbooks.Add(Flipbook);
				return true;
			};
			auto ResolveFlipbook = [&Profile](const FString& FlipbookName)
				-> UPaperFlipbook*
			{
				const FFlipbookProfileEntry* Entry =
					Profile.FindFlipbookDataPtr(FlipbookName);
				return Entry
					? Entry->Identity.Flipbook.LoadSynchronous()
					: nullptr;
			};

			// Existing authored references are authoritative. Keep the first known sequence only for
			// filling empty siblings; no manual assignment is ever overwritten below.
			for (FFlipbookProfileEntry& Entry : Profile.Flipbooks)
			{
				UPaperFlipbook* Flipbook =
					Entry.Identity.Flipbook.LoadSynchronous();
				if (IsInScope(Flipbook) && Entry.Identity.PaperZDSequence)
				{
					OutResolvedSequences.FindOrAdd(
						Flipbook,
						Entry.Identity.PaperZDSequence.Get());
				}
			}
			for (TPair<FGameplayTag, FFlipbookTagMapping>& Pair :
				Profile.TagMappings)
			{
				for (FFlipbookTagMappingEntry& Entry : Pair.Value.Entries)
				{
					UPaperFlipbook* Flipbook =
						ResolveFlipbook(Entry.FlipbookName);
					if (IsInScope(Flipbook) && Entry.PaperZDSequence)
					{
						OutResolvedSequences.FindOrAdd(
							Flipbook,
							Entry.PaperZDSequence.Get());
					}
				}
			}

			TArray<TPair<UPaperFlipbook*, FString>> MissingFlipbooks;
			TSet<UPaperFlipbook*> SeenMissingFlipbooks;
			auto AddMissing =
				[&MissingFlipbooks, &SeenMissingFlipbooks, &IsInScope](
					UPaperFlipbook* Flipbook,
					const FString& FlipbookName)
			{
				if (IsInScope(Flipbook)
					&& !SeenMissingFlipbooks.Contains(Flipbook))
				{
					SeenMissingFlipbooks.Add(Flipbook);
					MissingFlipbooks.Emplace(Flipbook, FlipbookName);
				}
			};

			// Preserve Profile order for a predictable confirmation list. Tag mappings may introduce
			// a referenced Profile entry already assigned at identity level, so scan them second.
			for (FFlipbookProfileEntry& Entry : Profile.Flipbooks)
			{
				if (!Entry.Identity.PaperZDSequence)
				{
					AddMissing(
						Entry.Identity.Flipbook.LoadSynchronous(),
						Entry.Identity.FlipbookName);
				}
			}
			for (TPair<FGameplayTag, FFlipbookTagMapping>& Pair :
				Profile.TagMappings)
			{
				for (FFlipbookTagMappingEntry& Entry : Pair.Value.Entries)
				{
					if (!Entry.PaperZDSequence)
					{
						AddMissing(
							ResolveFlipbook(Entry.FlipbookName),
							Entry.FlipbookName);
					}
				}
			}

			for (const TPair<UPaperFlipbook*, FString>& Missing :
				MissingFlipbooks)
			{
				if (!OutResolvedSequences.Contains(Missing.Key)
					&& bDiscoverExistingSequences)
				{
					if (UObject* Existing =
						Profile.FindPaperZDSequenceForFlipbook(Missing.Key))
					{
						OutResolvedSequences.Add(Missing.Key, Existing);
					}
				}
				if (OutResolvedSequences.Contains(Missing.Key))
				{
					continue;
				}

				TSharedPtr<FPendingSequence> Pending =
					MakeShared<FPendingSequence>();
				Pending->FlipbookName = Missing.Value;
				Pending->SequenceName = BuildDefaultSequenceName(
					Profile.GetName(),
					Missing.Value,
					true);
				Pending->Flipbook = Missing.Key;
				OutPendingSequences.Add(MoveTemp(Pending));
			}

			for (auto It = OutResolvedSequences.CreateIterator(); It; ++It)
			{
				if (!SeenMissingFlipbooks.Contains(It.Key()))
				{
					It.RemoveCurrent();
				}
			}

			if (OutSummary)
			{
				OutSummary->CandidateFlipbooks = CandidateFlipbooks.Num();
				OutSummary->FlipbooksMissingSequences = SeenMissingFlipbooks.Num();
				// After the prune above, every surviving resolved entry is a MISSING flipbook that an
				// existing sequence can simply be linked to.
				OutSummary->FlipbooksResolvableFromExisting = OutResolvedSequences.Num();
			}
		}

		int32 AssignSequencesToMissingReferences(
			UPaper2DPlusCharacterProfileAsset& Profile,
			const TMap<UPaperFlipbook*, UObject*>& ResolvedSequences)
		{
			if (ResolvedSequences.Num() == 0)
			{
				return 0;
			}

			bool bModified = false;
			TSet<UPaperFlipbook*> ModifiedFlipbooks;
			auto AssignIfMissing =
				[&Profile,
				 &ResolvedSequences,
				 &ModifiedFlipbooks,
				 &bModified](
					TObjectPtr<UObject>& Target,
					UPaperFlipbook* Flipbook)
			{
				UObject* const* Sequence =
					Flipbook
						? ResolvedSequences.Find(Flipbook)
						: nullptr;
				if (Target || !Sequence || !*Sequence)
				{
					return;
				}
				if (!bModified)
				{
					Profile.SetFlags(RF_Transactional);
					Profile.Modify();
					bModified = true;
				}
				Target = *Sequence;
				ModifiedFlipbooks.Add(Flipbook);
			};

			for (FFlipbookProfileEntry& Entry : Profile.Flipbooks)
			{
				AssignIfMissing(
					Entry.Identity.PaperZDSequence,
					Entry.Identity.Flipbook.LoadSynchronous());
			}
			for (TPair<FGameplayTag, FFlipbookTagMapping>& Pair :
				Profile.TagMappings)
			{
				for (FFlipbookTagMappingEntry& Entry : Pair.Value.Entries)
				{
					const FFlipbookProfileEntry* ProfileEntry =
						Profile.FindFlipbookDataPtr(Entry.FlipbookName);
					AssignIfMissing(
						Entry.PaperZDSequence,
						ProfileEntry
							? ProfileEntry->Identity.Flipbook.LoadSynchronous()
							: nullptr);
				}
			}
			return ModifiedFlipbooks.Num();
		}
	}

	bool EvaluateOptionalAvailability(
		const bool bPluginInstalledAndEnabled,
		UClass* AnimationSourceClass,
		UClass* FlipbookSequenceClass)
	{
		return bPluginInstalledAndEnabled
			&& AnimationSourceClass
			&& FlipbookSequenceClass
			&& !FlipbookSequenceClass->HasAnyClassFlags(CLASS_Abstract)
			&& ResolveSequenceSchema(FlipbookSequenceClass).Accepts(
				AnimationSourceClass,
				nullptr);
	}

	UClass* ResolveAnimationSourceClass()
	{
		static const TCHAR* AnimationSourcePath =
			TEXT("/Script/PaperZD.PaperZDAnimationSource");
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		return FindObject<UClass>(nullptr, AnimationSourcePath);
#else
		return UClass::TryFindTypeSlow<UClass>(AnimationSourcePath);
#endif
	}

	UClass* ResolveFlipbookSequenceClass()
	{
		static const TCHAR* FlipbookSequencePath =
			TEXT("/Script/PaperZD.PaperZDAnimSequence_Flipbook");
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
		return FindObject<UClass>(nullptr, FlipbookSequencePath);
#else
		return UClass::TryFindTypeSlow<UClass>(FlipbookSequencePath);
#endif
	}

	bool IsOptionalAuthoringAvailable()
	{
		if (!IsPaperZDPluginInstalledAndEnabled())
		{
			return false;
		}
		return EvaluateOptionalAvailability(
			true,
			ResolveAnimationSourceClass(),
			ResolveFlipbookSequenceClass());
	}

	FString BuildDefaultSequenceName(
		const FString& ProfileName,
		const FString& FlipbookName,
		const bool bIncludeProfilePrefix)
	{
		if (!bIncludeProfilePrefix)
		{
			return FlipbookName;
		}

		const FString Prefix = ProfileName + TEXT("_");
		return FlipbookName.StartsWith(Prefix, ESearchCase::IgnoreCase)
			? FlipbookName
			: Prefix + FlipbookName;
	}

	FSequenceWorkSummary SummarizeSequenceWork(
		UPaper2DPlusCharacterProfileAsset& Profile,
		const bool bDiscoverExistingSequences,
		const TArray<UPaperFlipbook*>* FlipbookScope)
	{
		TSet<UPaperFlipbook*> Scope;
		if (FlipbookScope)
		{
			for (UPaperFlipbook* Flipbook : *FlipbookScope)
			{
				if (Flipbook)
				{
					Scope.Add(Flipbook);
				}
			}
		}

		FSequenceWorkSummary Summary;
		TMap<UPaperFlipbook*, UObject*> ResolvedSequences;
		TArray<TSharedPtr<FPendingSequence>> PendingSequences;
		GatherSequenceWorkInternal(
			Profile,
			bDiscoverExistingSequences,
			FlipbookScope ? &Scope : nullptr,
			ResolvedSequences,
			PendingSequences,
			&Summary);
		return Summary;
	}

	void GatherSequenceWork(
		UPaper2DPlusCharacterProfileAsset& Profile,
		const bool bDiscoverExistingSequences,
		const TArray<UPaperFlipbook*>& FlipbookScope,
		TMap<UPaperFlipbook*, UObject*>& OutResolvedSequences,
		TArray<TSharedPtr<FPendingSequence>>& OutPendingSequences)
	{
		TSet<UPaperFlipbook*> Scope;
		for (UPaperFlipbook* Flipbook : FlipbookScope)
		{
			if (Flipbook)
			{
				Scope.Add(Flipbook);
			}
		}
		GatherSequenceWorkInternal(
			Profile,
			bDiscoverExistingSequences,
			&Scope,
			OutResolvedSequences,
			OutPendingSequences);
	}

	void GatherAllSequenceWork(
		UPaper2DPlusCharacterProfileAsset& Profile,
		const bool bDiscoverExistingSequences,
		TMap<UPaperFlipbook*, UObject*>& OutResolvedSequences,
		TArray<TSharedPtr<FPendingSequence>>& OutPendingSequences)
	{
		GatherSequenceWorkInternal(
			Profile,
			bDiscoverExistingSequences,
			nullptr,
			OutResolvedSequences,
			OutPendingSequences);
	}

	bool AssignSequenceToMissingReferences(
		UPaper2DPlusCharacterProfileAsset& Profile,
		UPaperFlipbook* Flipbook,
		UObject* Sequence)
	{
		if (!Flipbook || !Sequence)
		{
			return false;
		}
		TMap<UPaperFlipbook*, UObject*> ResolvedSequence;
		ResolvedSequence.Add(Flipbook, Sequence);
		return AssignSequencesToMissingReferences(
			Profile,
			ResolvedSequence) > 0;
	}

	static FCreateResult CreateAndLinkMissingSequencesInternal(
		UPaper2DPlusCharacterProfileAsset& Profile,
		const TSharedRef<SWidget>& ParentWidget,
		const TSet<UPaperFlipbook*>* FlipbookScope)
	{
		FCreateResult Result;
		const bool bPluginInstalledAndEnabled =
			IsPaperZDPluginInstalledAndEnabled();
		UClass* AnimationSourceClass = bPluginInstalledAndEnabled
			? ResolveAnimationSourceClass()
			: nullptr;
		UClass* SequenceClass = bPluginInstalledAndEnabled
			? ResolveFlipbookSequenceClass()
			: nullptr;
		if (!EvaluateOptionalAvailability(
			bPluginInstalledAndEnabled,
			AnimationSourceClass,
			SequenceClass))
		{
			ShowNotification(
				LOCTEXT(
					"OptionalPaperZDUnavailable",
					"PaperZD authoring is unavailable. Verify that the optional PaperZD plugin is installed and enabled."),
				SNotificationItem::CS_Fail);
			Result.Failures.Add(TEXT("PaperZD is not installed, enabled, or reflection-compatible."));
			return Result;
		}
		if (Profile.PaperZDAnimSource.IsNull())
		{
			ShowNotification(
				LOCTEXT(
					"MissingSource",
					"Choose a PaperZD Anim Source before creating sequences."),
				SNotificationItem::CS_Fail);
			Result.Failures.Add(TEXT("No PaperZD Anim Source is selected."));
			return Result;
		}

		UObject* AnimSource = Profile.PaperZDAnimSource.LoadSynchronous();
		if (!AnimSource
			|| !AnimationSourceClass
			|| !AnimSource->IsA(AnimationSourceClass))
		{
			ShowNotification(
				LOCTEXT(
					"SourceLoadFailed",
					"The selected PaperZD Anim Source could not be loaded or is incompatible."),
				SNotificationItem::CS_Fail);
			Result.Failures.Add(TEXT("The selected PaperZD Anim Source is unavailable or incompatible."));
			return Result;
		}

		const FReflectedSequenceSchema Schema =
			ResolveSequenceSchema(SequenceClass);
		if (!Schema.Accepts(AnimationSourceClass, AnimSource))
		{
			ShowNotification(
				LOCTEXT(
					"SchemaChanged",
					"PaperZD's flipbook-sequence schema is not compatible with this creator. No assets were changed."),
				SNotificationItem::CS_Fail);
			Result.Failures.Add(TEXT("The reflected PaperZD flipbook sequence schema is incompatible."));
			return Result;
		}
		FObjectProperty* AnimSourceProperty =
			Schema.AnimSourceProperty;
		FArrayProperty* AnimDataProperty =
			Schema.AnimDataProperty;
		FObjectProperty* AnimationProperty =
			Schema.AnimationProperty;

		const FString ProfilePath = FPackageName::GetLongPackagePath(
			Profile.GetOutermost()->GetName());
		const FString SequenceFolderPath =
			ProfilePath / TEXT("Sequences");
		if (!SequenceFolderPath.StartsWith(
				TEXT("/Game/"),
				ESearchCase::CaseSensitive)
			|| !FPackageName::IsValidLongPackageName(SequenceFolderPath))
		{
			ShowNotification(
				LOCTEXT(
					"InvalidOutputRoot",
					"PaperZD sequences can only be created beside a saved Character Profile under /Game/."),
				SNotificationItem::CS_Fail);
			Result.Failures.Add(TEXT("The Character Profile is not saved under /Game/."));
			return Result;
		}

		TMap<UPaperFlipbook*, UObject*> ResolvedSequences;
		TArray<TSharedPtr<FPendingSequence>> PendingSequences;
		// The same summary the pre-check surfaces read, so the terminal message below cannot claim
		// "already covered" for a Profile that has no flipbooks in scope at all.
		FSequenceWorkSummary WorkSummary;
		GatherSequenceWorkInternal(
			Profile,
			true,
			FlipbookScope,
			ResolvedSequences,
			PendingSequences,
			&WorkSummary);

		if (PendingSequences.Num() > 0)
		{
			bool bConfirmed = false;
			TSharedRef<bool> bIncludeProfilePrefix = MakeShared<bool>(true);
			TSharedPtr<SVerticalBox> NameListBox;
			const FString ProfileName = Profile.GetName();

			auto RefreshDefaultNames =
				[PendingSequences, bIncludeProfilePrefix, ProfileName]()
			{
				for (const TSharedPtr<FPendingSequence>& Pending :
					PendingSequences)
				{
					if (Pending.IsValid())
					{
						Pending->SequenceName = BuildDefaultSequenceName(
							ProfileName,
							Pending->FlipbookName,
							*bIncludeProfilePrefix);
					}
				}
			};

			TSharedRef<SWindow> ConfirmWindow = SNew(SWindow)
				.Title(LOCTEXT("CreateTitle", "Create PaperZD Sequences"))
				.ClientSize(FVector2D(590.0f, 430.0f))
				.SupportsMinimize(false)
				.SupportsMaximize(false);
			const TWeakPtr<SWindow> WeakConfirmWindow = ConfirmWindow;

			ConfirmWindow->SetContent(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10, 10, 10, 4)
				[
					SNew(STextBlock)
					.Text(FText::Format(
						LOCTEXT(
							"CreateHeader",
							"Create {0} missing PaperZD sequence(s)?"),
						FText::AsNumber(PendingSequences.Num())))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10, 0, 10, 6)
				[
					SNew(STextBlock)
					.Text(FText::Format(
						LOCTEXT("OutputFolder", "Output: {0}"),
						FText::FromString(SequenceFolderPath)))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10, 0, 10, 6)
				[
					SNew(STextBlock)
					.Text(LOCTEXT(
						"CreationUndoBoundary",
						"Created assets remain on disk if profile links are later undone."))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10, 0, 10, 6)
				[
					SNew(SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					.OnCheckStateChanged_Lambda(
						[bIncludeProfilePrefix,
						 RefreshDefaultNames,
						 &NameListBox](ECheckBoxState State)
						{
							*bIncludeProfilePrefix =
								State == ECheckBoxState::Checked;
							RefreshDefaultNames();
							if (NameListBox.IsValid())
							{
								NameListBox->Invalidate(
									EInvalidateWidgetReason::Paint);
							}
						})
					[
						SNew(STextBlock)
						.Text(FText::Format(
							LOCTEXT(
								"IncludeProfilePrefix",
								"Include profile prefix \"{0}_\""),
							FText::FromString(ProfileName)))
					]
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(10, 0)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(NameListBox, SVerticalBox)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(10, 8)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4, 0)
					[
						SNew(SButton)
						.ButtonStyle(
							FAppStyle::Get(),
							"FlatButton.Default")
						.Text(LOCTEXT("CreateConfirm", "Create"))
						.OnClicked_Lambda(
							[&bConfirmed, WeakConfirmWindow]()
							{
								bConfirmed = true;
								if (const TSharedPtr<SWindow> Window =
									WeakConfirmWindow.Pin())
								{
									Window->RequestDestroyWindow();
								}
								return FReply::Handled();
							})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4, 0)
					[
						SNew(SButton)
						.ButtonStyle(
							FAppStyle::Get(),
							"FlatButton.Default")
						.Text(LOCTEXT("CreateCancel", "Cancel"))
						.OnClicked_Lambda([WeakConfirmWindow]()
						{
							if (const TSharedPtr<SWindow> Window =
								WeakConfirmWindow.Pin())
							{
								Window->RequestDestroyWindow();
							}
							return FReply::Handled();
						})
					]
				]);

			for (const TSharedPtr<FPendingSequence>& Pending :
				PendingSequences)
			{
				if (!Pending.IsValid())
				{
					continue;
				}
				NameListBox->AddSlot()
				.AutoHeight()
				.Padding(0, 1)
				[
					SNew(SBorder)
					.BorderImage(
						FAppStyle::Get().GetBrush(
							"ToolPanel.DarkGroupBorder"))
					.Padding(FMargin(6, 3))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(0.35f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(
								Pending->FlipbookName))
							.ColorAndOpacity(
								FSlateColor::UseSubduedForeground())
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(5, 0)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("\x2192")))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(0.65f)
						.VAlign(VAlign_Center)
						[
							SNew(SEditableTextBox)
							.Text_Lambda([Pending]()
							{
								return FText::FromString(
									Pending->SequenceName);
							})
							.OnTextChanged_Lambda(
								[Pending](const FText& Text)
								{
									Pending->SequenceName =
										Text.ToString().TrimStartAndEnd();
								})
						]
					]
				];
			}

			FSlateApplication::Get().AddModalWindow(
				ConfirmWindow,
				ParentWidget);
			if (!bConfirmed)
			{
				return Result;
			}
		}

		TSet<FName> RequestedNames;
		for (const TSharedPtr<FPendingSequence>& Pending : PendingSequences)
		{
			if (!Pending.IsValid() || !Pending->Flipbook.IsValid())
			{
				ShowNotification(
					LOCTEXT(
						"FlipbookUnavailable",
						"A source flipbook became unavailable. No sequences were created."),
					SNotificationItem::CS_Fail);
				Result.Failures.Add(TEXT("A source flipbook became unavailable."));
				return Result;
			}

			const FName SequenceName(*Pending->SequenceName);
			FText InvalidNameReason;
			if (SequenceName.IsNone()
				|| !SequenceName.IsValidXName(
					INVALID_OBJECTNAME_CHARACTERS,
					&InvalidNameReason))
			{
				ShowNotification(
					FText::Format(
						LOCTEXT(
							"InvalidName",
							"\"{0}\" is not a valid Unreal asset name: {1}"),
						FText::FromString(Pending->SequenceName),
						InvalidNameReason),
					SNotificationItem::CS_Fail);
				Result.Failures.Add(
					FString::Printf(
						TEXT("%s: invalid asset name."),
						*Pending->SequenceName));
				return Result;
			}
			if (RequestedNames.Contains(SequenceName))
			{
				ShowNotification(
					FText::Format(
						LOCTEXT(
							"DuplicateName",
							"More than one flipbook uses the sequence name \"{0}\". Choose unique names."),
						FText::FromName(SequenceName)),
					SNotificationItem::CS_Fail);
				Result.Failures.Add(
					FString::Printf(
						TEXT("%s: duplicate requested name."),
						*Pending->SequenceName));
				return Result;
			}
			RequestedNames.Add(SequenceName);
		}

		TUniquePtr<FScopedSlowTask> SlowTask;
		if (PendingSequences.Num() > 0)
		{
			SlowTask = MakeUnique<FScopedSlowTask>(
				PendingSequences.Num(),
				LOCTEXT(
					"CreatingProgress",
					"Creating PaperZD sequences..."));
			SlowTask->MakeDialogDelayed(0.5f, true);
		}

		auto ExistingSequenceMatches =
			[AnimSource,
			 AnimSourceProperty,
			 AnimDataProperty,
			 AnimationProperty,
			 &Schema,
			 SequenceClass](
				UObject* Sequence,
				UPaperFlipbook* Flipbook)
		{
			if (!Sequence || !Flipbook)
			{
				return false;
			}
			const bool bExpectedSequenceClass =
				Sequence->GetClass()->IsChildOf(SequenceClass);
			if (!bExpectedSequenceClass)
			{
				return false;
			}
			UObject* ExistingSource =
				AnimSourceProperty->GetObjectPropertyValue(
					AnimSourceProperty->ContainerPtrToValuePtr<void>(
						Sequence));
			FScriptArrayHelper AnimData(
				AnimDataProperty,
				AnimDataProperty->ContainerPtrToValuePtr<void>(Sequence));
			const bool bMatchingFlipbook = AnimData.Num() > 0
				&& AnimationProperty->GetObjectPropertyValue(
					Schema.GetFlipbookValuePtr(AnimData.GetRawPtr(0)))
					== Flipbook;
			return ExistingSource == AnimSource && bMatchingFlipbook;
		};

		for (const TSharedPtr<FPendingSequence>& Pending : PendingSequences)
		{
			if (SlowTask && SlowTask->ShouldCancel())
			{
				Result.Failures.Add(
					TEXT("Creation cancelled before all sequences were processed."));
				break;
			}
			if (SlowTask)
			{
				SlowTask->EnterProgressFrame(
					1.0f,
					FText::Format(
						LOCTEXT(
							"CreatingOneProgress",
							"Creating {0}"),
						FText::FromString(Pending->SequenceName)));
			}

			UPaperFlipbook* Flipbook = Pending->Flipbook.Get();
			const FString PackagePath =
				SequenceFolderPath / Pending->SequenceName;
			if (!FPackageName::IsValidLongPackageName(PackagePath))
			{
				Result.Failures.Add(FString::Printf(
					TEXT("%s: invalid package path."),
					*Pending->SequenceName));
				continue;
			}

			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package)
			{
				Result.Failures.Add(FString::Printf(
					TEXT("%s: package creation failed."),
					*Pending->SequenceName));
				continue;
			}
			// Load an on-disk occupant before deciding whether this is a compatible reuse. Without
			// this, a fresh editor session could shadow an unloaded same-name asset.
			Package->FullyLoad();

			UObject* Sequence = StaticFindObject(
				UObject::StaticClass(),
				Package,
				*Pending->SequenceName);
			if (Sequence)
			{
				if (!ExistingSequenceMatches(Sequence, Flipbook))
				{
					Result.Failures.Add(FString::Printf(
						TEXT("%s: an incompatible asset already exists."),
						*Pending->SequenceName));
					continue;
				}
				++Result.ReusedCount;
			}
			else
			{
				Sequence = NewObject<UObject>(
					Package,
					SequenceClass,
					*Pending->SequenceName,
					RF_Public | RF_Standalone | RF_Transactional);
				if (!Sequence)
				{
					Result.Failures.Add(FString::Printf(
						TEXT("%s: asset creation failed."),
						*Pending->SequenceName));
					continue;
				}

				AnimSourceProperty->SetObjectPropertyValue(
					AnimSourceProperty->ContainerPtrToValuePtr<void>(
						Sequence),
					AnimSource);
				FScriptArrayHelper AnimData(
					AnimDataProperty,
					AnimDataProperty->ContainerPtrToValuePtr<void>(
						Sequence));
				// PaperZD initializes non-CDO sequences with one default data slot. Preserve that
				// slot; only older or schema-compatible implementations that start empty need one.
				if (AnimData.Num() == 0)
				{
					AnimData.AddValue();
				}
				AnimationProperty->SetObjectPropertyValue(
					Schema.GetFlipbookValuePtr(AnimData.GetRawPtr(0)),
					Flipbook);
				Sequence->PostEditChange();
				Package->MarkPackageDirty();
				FAssetRegistryModule::AssetCreated(Sequence);
				++Result.CreatedCount;
			}

			ResolvedSequences.Add(Flipbook, Sequence);
		}

		if (ResolvedSequences.Num() > 0)
		{
			const FScopedTransaction Transaction(
				LOCTEXT(
					"CreateAndLinkTransaction",
					"Create and Link PaperZD Sequences"));
			Result.LinkedFlipbookCount =
				AssignSequencesToMissingReferences(Profile, ResolvedSequences);
			if (Result.LinkedFlipbookCount > 0)
			{
				Profile.MarkPackageDirty();
				Result.bProfileModified = true;
			}
		}

		for (const FString& Failure : Result.Failures)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("Paper2DPlus PaperZD sequence creator: %s"),
				*Failure);
		}

		if (PendingSequences.Num() == 0)
		{
			// Three-way, not two: an empty candidate set is "there is nothing to check yet", which is
			// a different statement from "everything is already covered".
			if (!WorkSummary.HasCandidates())
			{
				ShowNotification(
					FlipbookScope
						? LOCTEXT(
							"NoSequenceCandidatesInScope",
							"None of the selected flipbooks are on this Character Profile yet, so there is nothing to create sequences for.")
						: LOCTEXT(
							"NoSequenceCandidatesOnProfile",
							"This Character Profile has no flipbooks yet, so there is nothing to create sequences for. Sequences are offered automatically after Extract All succeeds."));
				return Result;
			}
			ShowNotification(
				Result.LinkedFlipbookCount > 0
					? FText::Format(
						LOCTEXT(
							"LinkedExisting",
							"Linked existing PaperZD sequences for {0} flipbook(s)."),
						FText::AsNumber(Result.LinkedFlipbookCount))
					: LOCTEXT(
						"NoSequencesNeeded",
						"All selected flipbooks already have PaperZD sequences."));
			return Result;
		}

		const int32 CreatedOrReused =
			Result.CreatedCount + Result.ReusedCount;
		if (CreatedOrReused == PendingSequences.Num())
		{
			ShowNotification(FText::Format(
				LOCTEXT(
					"CreateResult",
					"Created or reused {0} PaperZD sequence(s) and linked {1} flipbook(s)."),
				FText::AsNumber(CreatedOrReused),
				FText::AsNumber(Result.LinkedFlipbookCount)));
		}
		else
		{
			ShowNotification(
				FText::Format(
					LOCTEXT(
						"PartialResult",
						"Created or reused {0} of {1} PaperZD sequence(s). See Output Log for failures."),
					FText::AsNumber(CreatedOrReused),
					FText::AsNumber(PendingSequences.Num())),
				SNotificationItem::CS_Fail);
		}
		return Result;
	}

	bool CreateAndLinkMissingSequences(
		UPaper2DPlusCharacterProfileAsset& Profile,
		const TSharedRef<SWidget>& ParentWidget)
	{
		return CreateAndLinkMissingSequencesInternal(
			Profile,
			ParentWidget,
			nullptr).bProfileModified;
	}

	bool CreateAndLinkMissingSequencesForFlipbooks(
		UPaper2DPlusCharacterProfileAsset& Profile,
		const TSharedRef<SWidget>& ParentWidget,
		const TArray<UPaperFlipbook*>& FlipbookScope)
	{
		TSet<UPaperFlipbook*> Scope;
		for (UPaperFlipbook* Flipbook : FlipbookScope)
		{
			if (Flipbook)
			{
				Scope.Add(Flipbook);
			}
		}
		return CreateAndLinkMissingSequencesInternal(
			Profile,
			ParentWidget,
			&Scope).bProfileModified;
	}
}

#undef LOCTEXT_NAMESPACE
