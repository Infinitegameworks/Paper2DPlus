// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FrameCues/Paper2DPlusFrameCueTypeEditor.h"

#include "Async/Async.h"
#include "BlueprintEditorContext.h"
#include "BlueprintEditorModes.h"
#include "BlueprintEditorModule.h"
#include "BlueprintEditorSettings.h"
#include "BlueprintEditorSharedTabFactories.h"
#include "BlueprintEditorTabs.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/TimelineTemplate.h"
#include "FileHelpers.h"
#include "FindInBlueprints.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCuePlacementAuthoring.h"
#include "FrameCues/Paper2DPlusFrameCueTabPresentation.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "HAL/FileManager.h"
#include "IDetailsView.h"
#include "K2Node_Event.h"
#include "K2Node_Timeline.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusEditorModule.h"
#include "ScopedTransaction.h"
#include "SBlueprintEditorToolbar.h"
#include "SMyBlueprint.h"
#include "SKismetInspector.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Templates/UnrealTemplate.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "Paper2DPlusFrameCueTypeEditor"

const FName FPaper2DPlusFrameCueTypeEditor::CueTypeModeName(TEXT("Paper2DPlusFrameCueType"));
const FName FPaper2DPlusFrameCueTypeEditor::ToolbarOwnerName(
	TEXT("Paper2DPlus.FrameCueTypeToolbar"));
const FName FPaper2DPlusFrameCueTypeEditor::StandardToolbarLayoutName(
	TEXT("Paper2DPlusFrameCueTypeEditor_Layout_v4_StandardToolbar"));

struct FPaper2DPlusFrameCueCompiledRecoveryState
{
	struct FPlacement
	{
		TWeakObjectPtr<UObject> Outer;
		FName ObjectName;
		TMap<FName, FString> PropertyValues;
	};

	TWeakObjectPtr<UPaper2DPlusFrameCueBlueprint> CueType;
	TArray<FBPVariableDescription> DurableVariables;
	TMap<FName, FString> DurableDefaultValues;
	/**
	 * The durable fingerprint this snapshot's Durable* fields were copied from. An ordinary save
	 * can now commit a confirmed schema change while this editor stays open, so a reused snapshot
	 * must prove its captured durable state still IS the asset's durable state — restoring a
	 * stale capture after a later failure would resurrect a baseline older than the saved asset.
	 */
	FString DurableFingerprintAtCapture;
	TArray<FBPVariableDescription> ProposedVariables;
	TMap<FName, FString> ProposedDefaultValues;
	TArray<FPlacement> Placements;
	TMap<TWeakObjectPtr<UPackage>, bool> PackageDirtyStates;
	FString ArtifactPath;
};

namespace Paper2DPlusFrameCueTypeEditorInternal
{
	void SetError(FText* OutError, const FText& Error);

#if WITH_DEV_AUTOMATION_TESTS
	TFunction<bool(const FPaper2DPlusFrameCueSchemaPreflight&)>
		DestructiveCompileConfirmationForTests;
	TFunction<bool(const FPaper2DPlusFrameCueSchemaPreflight&)>
		BaselineRecoveryCompileConfirmationForTests;
#endif

	bool ExportPropertyValue(
		const FProperty& Property,
		const UObject& Owner,
		FString& OutValue)
	{
		OutValue.Reset();
		const void* Value = Property.ContainerPtrToValuePtr<void>(&Owner);
#if UE_VERSION_OLDER_THAN(5, 5, 0)
		Property.ExportTextItem(OutValue, Value, nullptr, const_cast<UObject*>(&Owner), PPF_Copy);
#else
		Property.ExportTextItem_Direct(
			OutValue, Value, nullptr, const_cast<UObject*>(&Owner), PPF_Copy);
#endif
		return true;
	}

	bool ImportPropertyValue(
		const FProperty& Property,
		UObject& Owner,
		const FString& Value)
	{
		void* Destination = Property.ContainerPtrToValuePtr<void>(&Owner);
#if UE_VERSION_OLDER_THAN(5, 5, 0)
		return Property.ImportText(*Value, Destination, PPF_Copy, &Owner) != nullptr;
#else
		return Property.ImportText_Direct(*Value, Destination, &Owner, PPF_Copy) != nullptr;
#endif
	}

	bool WriteRecoveryArtifact(
		const UPaper2DPlusFrameCueBlueprint& Blueprint,
		const FPaper2DPlusFrameCueSchemaPreflight& Preflight,
		const TMap<FName, FString>& ProposedDefaults,
		FString& OutPath,
		FText* OutError)
	{
		FString ProposedSchemaSnapshot;
		if (!FPaper2DPlusFrameCueTypeAuthoring::SerializeSchemaSnapshot(
			Preflight.CurrentSchema,
			ProposedSchemaSnapshot,
			OutError))
		{
			return false;
		}

		const FString Directory = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("Paper2DPlus"),
			TEXT("FrameCueRecovery"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		const FString StableKey = FString::Printf(
			TEXT("%s_%08X.recovery.txt"),
			*FPaths::MakeValidFileName(Blueprint.GetName()),
			GetTypeHash(Blueprint.GetPathName()));
		OutPath = FPaths::Combine(Directory, StableKey);
		FString Contents;
		Contents += TEXT("Paper2DPlusFrameCueRecovery=1\n");
		Contents += TEXT("Asset=") + FBase64::Encode(Blueprint.GetPathName()) + TEXT("\n");
		Contents += TEXT("DurableSchema=")
			+ FBase64::Encode(Blueprint.DurableSchemaSnapshot) + TEXT("\n");
		Contents += TEXT("ProposedSchema=")
			+ FBase64::Encode(ProposedSchemaSnapshot) + TEXT("\n");
		for (const TPair<FName, FString>& Pair : ProposedDefaults)
		{
			Contents += TEXT("Default.") + Pair.Key.ToString() + TEXT("=")
				+ FBase64::Encode(Pair.Value) + TEXT("\n");
		}
		if (!FFileHelper::SaveStringToFile(
			Contents,
			*OutPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			SetError(OutError, FText::Format(
				LOCTEXT(
					"CueRecoveryArtifactWriteFailed",
					"The protected recovery artifact could not be written to '{0}', so compilation was blocked."),
				FText::FromString(OutPath)));
			return false;
		}
		SetError(OutError, FText::GetEmpty());
		return true;
	}

	void DeleteRecoveryArtifact(const FString& Path)
	{
		if (!Path.IsEmpty())
		{
			IFileManager::Get().Delete(*Path, false, true, true);
		}
	}

	void CopyDefaultsIntoVariableModel(
		TArray<FBPVariableDescription>& Variables,
		const TMap<FName, FString>& Defaults)
	{
		for (FBPVariableDescription& Variable : Variables)
		{
			if (const FString* DefaultValue = Defaults.Find(Variable.VarName))
			{
				// The Blueprint compiler consumes this field on the next full compile and then
				// clears it after copying the value to the new CDO. Keeping the proposal here
				// avoids mutating the restored durable CDO after a failed save.
				Variable.DefaultValue = *DefaultValue;
			}
		}
	}

	void MergeBufferedVariableDefaults(
		const TArray<FBPVariableDescription>& Variables,
		TMap<FName, FString>& InOutDefaults)
	{
		for (const FBPVariableDescription& Variable : Variables)
		{
			if (!Variable.DefaultValue.IsEmpty())
			{
				// An explicit compiler buffer is newer than the durable generated CDO. This is
				// especially important after rollback, when the CDO intentionally remains durable.
				InOutDefaults.Add(Variable.VarName, Variable.DefaultValue);
			}
		}
	}

	void MergePendingDefaultProposal(
		const UPaper2DPlusFrameCueBlueprint& Blueprint,
		TMap<FName, FString>& InOutDefaults)
	{
		if (!Blueprint.bHasPendingDefaultProposal
			|| !Blueprint.bPendingDefaultProposalNeedsCompile)
		{
			return;
		}
		for (const TPair<FName, FString>& Pair : Blueprint.PendingDefaultProposalValues)
		{
			// The pending map owns exact values while rollback deliberately keeps the CDO
			// durable. Key presence, rather than string non-emptiness, represents an empty value.
			InOutDefaults.Add(Pair.Key, Pair.Value);
		}
	}

	void StorePendingDefaultProposal(
		UPaper2DPlusFrameCueBlueprint& Blueprint,
		const TMap<FName, FString>& Defaults,
		bool bNeedsCompile)
	{
		Blueprint.PendingDefaultProposalValues = Defaults;
		Blueprint.bHasPendingDefaultProposal = true;
		Blueprint.bPendingDefaultProposalNeedsCompile = bNeedsCompile;
	}

	void ClearPendingDefaultProposal(UPaper2DPlusFrameCueBlueprint& Blueprint)
	{
		Blueprint.PendingDefaultProposalValues.Reset();
		Blueprint.bHasPendingDefaultProposal = false;
		Blueprint.bPendingDefaultProposalNeedsCompile = false;
	}

	bool DefaultMapsEqual(
		const TMap<FName, FString>& Left,
		const TMap<FName, FString>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (const TPair<FName, FString>& Pair : Left)
		{
			const FString* Other = Right.Find(Pair.Key);
			if (!Other || *Other != Pair.Value)
			{
				return false;
			}
		}
		return true;
	}

	FText GetModeDisplayName(const FName ModeName)
	{
		return LOCTEXT("ModeDisplayName", "Cue Type");
	}

	void CompileBlueprintWithoutAutomaticSave(
		UBlueprint& Blueprint,
		EBlueprintCompileOptions Options)
	{
		UBlueprintEditorSettings* Settings =
			GetMutableDefault<UBlueprintEditorSettings>();
		TGuardValue<TEnumAsByte<ESaveOnCompile>> SaveOnCompileGuard(
			Settings->SaveOnCompile,
			TEnumAsByte<ESaveOnCompile>(SoC_Never));
		FKismetEditorUtilities::CompileBlueprint(&Blueprint, Options);
	}

	class FWidgetTabFactory final : public FWorkflowTabFactory
	{
	public:
		FWidgetTabFactory(
			FName TabId,
			const TSharedPtr<FPaper2DPlusFrameCueTypeEditor>& Editor,
			FText InLabel,
			TFunction<TSharedRef<SWidget>()> InCreateBody)
			: FWorkflowTabFactory(TabId, Editor)
			, CreateBody(MoveTemp(InCreateBody))
		{
			TabLabel = MoveTemp(InLabel);
			ViewMenuDescription = TabLabel;
			ViewMenuTooltip = TabLabel;
			bIsSingleton = true;
		}

		virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override
		{
			return CreateBody();
		}

	private:
		TFunction<TSharedRef<SWidget>()> CreateBody;
	};

	void ReplaceStockCompileEntryWithProtectedCueCompile(UToolMenu& Toolbar)
	{
		FToolMenuSection* CompileSection = Toolbar.FindSection(TEXT("Compile"));
		if (!CompileSection)
		{
			return;
		}

		// Both supported stock shapes place their save-on-compile dropdown inside this dynamic
		// block (attached to Compile through UE 5.0, then a sibling combo button). Cue compilation
		// must finish its durable-schema transaction before any package save, so retain the native
		// Compile/status button and remove only those unsafe global options.
		UToolMenus::Get()->RemoveEntry(
			Toolbar.GetMenuName(),
			TEXT("Compile"),
			TEXT("CompileCommands"));
		CompileSection->AddDynamicEntry(
			TEXT("CompileCommands"),
			FNewToolMenuSectionDelegate::CreateLambda(
				[](FToolMenuSection& InSection)
				{
					const UBlueprintEditorToolMenuContext* Context =
						InSection.FindContext<UBlueprintEditorToolMenuContext>();
					if (!Context
						|| !Context->BlueprintEditor.IsValid()
						|| !Context->GetBlueprintObj())
					{
						return;
					}

					const TSharedPtr<FBlueprintEditorToolbar> ToolbarBuilder =
						Context->BlueprintEditor.Pin()->GetToolbarBuilder();
					if (!ToolbarBuilder.IsValid())
					{
						return;
					}

					const TSharedPtr<FUICommandInfo> CompileCommand =
						FInputBindingManager::Get().FindCommandInContext(
							TEXT("BlueprintEditor"),
							TEXT("CompileBlueprint"));
					if (!CompileCommand.IsValid())
					{
						return;
					}

					FToolMenuEntry CompileButton = FToolMenuEntry::InitToolBarButton(
						CompileCommand,
						TAttribute<FText>(),
						TAttribute<FText>(
							ToolbarBuilder.ToSharedRef(),
							&FBlueprintEditorToolbar::GetStatusTooltip),
						TAttribute<FSlateIcon>(
							ToolbarBuilder.ToSharedRef(),
							&FBlueprintEditorToolbar::GetStatusImage),
						FName(TEXT("CompileBlueprint")));
					CompileButton.StyleNameOverride = FName(TEXT("CalloutToolbar"));
					InSection.AddEntry(CompileButton);
				}));
	}

	class FApplicationMode final : public FBlueprintEditorApplicationMode
	{
	public:
		explicit FApplicationMode(const TSharedPtr<FPaper2DPlusFrameCueTypeEditor>& Editor)
			: FBlueprintEditorApplicationMode(
				Editor,
				FPaper2DPlusFrameCueTypeEditor::CueTypeModeName,
				&GetModeDisplayName,
				false,
				false)
		{
			// The stock Blueprint mode registers Palette, My Blueprint, Find, Bookmarks, and
			// document-oriented factories. Clear all of them and expose only the data surface.
			BlueprintEditorTabFactories.Clear();
			BlueprintEditorOnlyTabFactories.Clear();
			CoreTabFactories.Clear();

			const TWeakPtr<FPaper2DPlusFrameCueTypeEditor> WeakEditor = Editor;
			// The stock summoner is private in Kismet on every supported engine. Host the public
			// widget through this local factory and compose the one safe Cue Override gesture above it.
			BlueprintEditorTabFactories.RegisterFactory(MakeShared<FWidgetTabFactory>(
				FBlueprintEditorTabs::MyBlueprintID,
				Editor,
				LOCTEXT("MyBlueprintTab", "My Blueprint"),
				[WeakEditor]() -> TSharedRef<SWidget>
				{
					const TSharedPtr<FPaper2DPlusFrameCueTypeEditor> PinnedEditor = WeakEditor.Pin();
					if (PinnedEditor.IsValid())
					{
						return PinnedEditor->CreateMyBlueprintTabBody(WeakEditor);
					}
					return SNew(STextBlock)
						.Text(LOCTEXT(
							"UnavailableMyBlueprintTab",
							"The Cue Type editor is closing."));
				}));

			// The stock summoner sets the Inspector owner tab and Details host tab manager. The native
			// Blueprint variable customization depends on that lifecycle, so do not wrap it in a generic
			// widget factory.
			BlueprintEditorTabFactories.RegisterFactory(
				MakeShared<FSelectionDetailsSummoner>(Editor));

			BlueprintEditorTabFactories.RegisterFactory(MakeShared<FWidgetTabFactory>(
				FBlueprintEditorTabs::DefaultEditorID,
				Editor,
				LOCTEXT("ClassDefaultsTab", "Class Defaults"),
				[WeakEditor]() -> TSharedRef<SWidget>
				{
					const TSharedPtr<FPaper2DPlusFrameCueTypeEditor> PinnedEditor = WeakEditor.Pin();
					if (PinnedEditor.IsValid())
					{
						return PinnedEditor->GetDefaultEditor();
					}
					return SNew(STextBlock)
						.Text(LOCTEXT(
							"UnavailableClassDefaultsTab",
							"The Cue Type editor is closing."));
				}));

			BlueprintEditorTabFactories.RegisterFactory(
				MakeShared<FCompilerResultsSummoner>(Editor));

			BlueprintEditorTabFactories.RegisterFactory(MakeShared<FWidgetTabFactory>(
				FBlueprintEditorTabs::FindResultsID,
				Editor,
				LOCTEXT("FindResultsTab", "Find Results"),
				[WeakEditor]() -> TSharedRef<SWidget>
				{
					const TSharedPtr<FPaper2DPlusFrameCueTypeEditor> PinnedEditor =
						WeakEditor.Pin();
					if (PinnedEditor.IsValid())
					{
						return PinnedEditor->GetFindResults();
					}
					return SNew(STextBlock)
						.Text(LOCTEXT(
							"UnavailableFindResultsTab",
							"The Cue Type editor is closing."));
				}));

			// The layout key changes whenever the registered tab set changes; a stale saved
			// arrangement would otherwise restore an editor with unspawnable or missing tabs.
			TabLayout = FTabManager::NewLayout(
				FPaper2DPlusFrameCueTypeEditor::StandardToolbarLayoutName)
				->AddArea(
					FTabManager::NewPrimaryArea()
					->SetOrientation(Orient_Vertical)
					->Split(
						FTabManager::NewSplitter()
						->SetOrientation(Orient_Horizontal)
						->Split(
							FTabManager::NewSplitter()
							->SetOrientation(Orient_Vertical)
							->SetSizeCoefficient(0.25f)
							->Split(
								FTabManager::NewStack()
								->AddTab(FBlueprintEditorTabs::MyBlueprintID, ETabState::OpenedTab)))
						->Split(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.45f)
							->AddTab(TEXT("Document"), ETabState::ClosedTab))
						->Split(
							FTabManager::NewSplitter()
							->SetOrientation(Orient_Vertical)
							->SetSizeCoefficient(0.30f)
							->Split(
								FTabManager::NewStack()
								->SetSizeCoefficient(0.55f)
								->AddTab(FBlueprintEditorTabs::DetailsID, ETabState::OpenedTab)
								->AddTab(FBlueprintEditorTabs::DefaultEditorID, ETabState::OpenedTab)
								->SetForegroundTab(FBlueprintEditorTabs::DetailsID))
							->Split(
								FTabManager::NewStack()
								->SetSizeCoefficient(0.45f)
								->AddTab(FBlueprintEditorTabs::CompilerResultsID, ETabState::ClosedTab)
								->AddTab(FBlueprintEditorTabs::FindResultsID, ETabState::ClosedTab)))));

			FToolMenuOwnerScoped ToolbarOwner(
				FPaper2DPlusFrameCueTypeEditor::ToolbarOwnerName);
			Editor->RegisterModeToolbarIfUnregistered(GetModeName());
			FName ParentToolbarName;
			const FName ToolbarName = Editor->GetToolMenuToolbarNameForMode(
				GetModeName(),
				ParentToolbarName);
			if (UToolMenu* Toolbar = UToolMenus::Get()->FindMenu(ToolbarName))
			{
				const TSharedPtr<FBlueprintEditorToolbar> ToolbarBuilder =
					Editor->GetToolbarBuilder();
				if (ToolbarBuilder.IsValid())
				{
					ToolbarBuilder->AddCompileToolbar(Toolbar);
					ReplaceStockCompileEntryWithProtectedCueCompile(*Toolbar);
					ToolbarBuilder->AddScriptingToolbar(Toolbar);
					ToolbarBuilder->AddBlueprintGlobalOptionsToolbar(Toolbar);
					ToolbarBuilder->AddDebuggingToolbar(Toolbar);
				}
			}
		}

		TArray<FName> GetRegisteredTabIdsForTests()
		{
			TArray<FName> Registered;
			const auto AppendFactories = [&Registered](FWorkflowAllowedTabSet& Factories)
			{
				for (auto It = Factories.CreateIterator(); It; ++It)
				{
					Registered.Add(It.Key());
				}
			};
			AppendFactories(BlueprintEditorTabFactories);
			AppendFactories(BlueprintEditorOnlyTabFactories);
			AppendFactories(CoreTabFactories);
			return Registered;
		}

		virtual void PreDeactivateMode() override
		{
			// Do not save graph documents from a legacy Blueprint into this restricted mode.
			::FApplicationMode::PreDeactivateMode();
		}

		virtual void PostActivateMode() override
		{
			if (const TSharedPtr<FBlueprintEditor> Editor = MyBlueprintEditor.Pin())
			{
				Editor->RefreshMyBlueprint();
				Editor->RefreshInspector();
			}
			::FApplicationMode::PostActivateMode();
		}
	};

	void SetError(FText* OutError, const FText& Error)
	{
		if (OutError)
		{
			*OutError = Error;
		}
	}

}

TSharedRef<SWidget> FPaper2DPlusFrameCueTypeEditor::CreateMyBlueprintTabBody(
	const TWeakPtr<FPaper2DPlusFrameCueTypeEditor>& WeakEditor)
{
	const TSharedPtr<SMyBlueprint> MyBlueprint = GetMyBlueprintWidget();
	if (!MyBlueprint.IsValid())
	{
		return SNew(SBorder)
			.Padding(8.0f)
			[
				SNew(STextBlock)
					.AutoWrapText(true)
					.Text(LOCTEXT(
						"UnavailableMyBlueprintBody",
						"Unreal's My Blueprint panel is unavailable for this Cue Type."))
			];
	}

	UpdateMyBlueprintExposure();
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SVerticalBox)
				.Visibility_Lambda(
					[WeakEditor]()
					{
						const TSharedPtr<FPaper2DPlusFrameCueTypeEditor> Editor =
							WeakEditor.Pin();
						return Editor.IsValid() && Editor->bUsingStockMyBlueprint
							? EVisibility::Visible
							: EVisibility::Collapsed;
					})
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4.0f, 4.0f, 4.0f, 2.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
							.Text(LOCTEXT(
								"CueOverrideHint",
								"Payload variables use Unreal's standard Blueprint workflow."))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SComboButton)
							.ContentPadding(FMargin(8.0f, 2.0f))
							.ButtonContent()
							[
								SNew(STextBlock)
									.Text(LOCTEXT("CueOverrideButton", "Override"))
							]
							.OnGetMenuContent_Lambda(
								[WeakEditor]() -> TSharedRef<SWidget>
								{
									const TSharedPtr<FPaper2DPlusFrameCueTypeEditor> Editor =
										WeakEditor.Pin();
									if (Editor.IsValid())
									{
										return Editor->BuildCueOverrideMenu(WeakEditor);
									}
									return SNew(STextBlock)
										.Text(LOCTEXT(
											"UnavailableCueOverrideMenu",
											"The Cue Type editor is closing."));
								})
					]
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					MyBlueprint.ToSharedRef()
				]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			CreateLegacyQuarantineBody(WeakEditor)
		];
}

TSharedRef<SWidget> FPaper2DPlusFrameCueTypeEditor::BuildCueOverrideMenu(
	const TWeakPtr<FPaper2DPlusFrameCueTypeEditor>& WeakEditor)
{
	FMenuBuilder MenuBuilder(true, nullptr);
	const UBlueprint* Blueprint = GetBlueprintObj();
	for (const FName EventName : Blueprint
		? GetCueOverrideEventNames(*Blueprint)
		: TArray<FName>())
	{
		const bool bImplemented = Blueprint
			&& FindCueBehaviorEventNode(*Blueprint, EventName);
		MenuBuilder.AddMenuEntry(
			FText::FromString(FName::NameToDisplayString(EventName.ToString(), false)),
			bImplemented
				? LOCTEXT("OpenCueOverrideTooltip", "Open this Cue behavior override.")
				: LOCTEXT(
					"ImplementCueOverrideTooltip",
					"Implement this Cue behavior override in the permitted event graph."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda(
				[WeakEditor, EventName]()
				{
					if (const TSharedPtr<FPaper2DPlusFrameCueTypeEditor> Editor =
						WeakEditor.Pin())
					{
						Editor->ShowCueBehaviorEvent(EventName);
					}
				})));
	}
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> FPaper2DPlusFrameCueTypeEditor::CreateLegacyQuarantineBody(
	const TWeakPtr<FPaper2DPlusFrameCueTypeEditor>& WeakEditor)
{
	return SNew(SBorder)
		.Visibility_Lambda(
			[WeakEditor]()
			{
				const TSharedPtr<FPaper2DPlusFrameCueTypeEditor> Editor =
					WeakEditor.Pin();
				return Editor.IsValid() && !Editor->bUsingStockMyBlueprint
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
		.Padding(8.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
					.AutoWrapText(true)
					.Text_Lambda(
						[WeakEditor]()
						{
							const TSharedPtr<FPaper2DPlusFrameCueTypeEditor> Editor =
								WeakEditor.Pin();
							const UBlueprint* Blueprint =
								Editor.IsValid() ? Editor->GetBlueprintObj() : nullptr;
							return Blueprint
								? BuildLegacyContentWarningText(*Blueprint)
								: LOCTEXT(
									"MissingLegacyCueType",
									"This Cue Type is no longer available.");
						})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT(
					"OpenLegacyCueRecovery",
					"Open Advanced Recovery\u2026"))
				.Visibility_Lambda(
					[WeakEditor]()
					{
						const TSharedPtr<FPaper2DPlusFrameCueTypeEditor> Editor =
							WeakEditor.Pin();
						const UBlueprint* Blueprint =
							Editor.IsValid() ? Editor->GetBlueprintObj() : nullptr;
						return Blueprint && ShouldOfferLegacyBlueprintRecovery(*Blueprint)
							? EVisibility::Visible
							: EVisibility::Collapsed;
					})
				.OnClicked_Lambda(
					[WeakEditor]()
					{
						const TSharedPtr<FPaper2DPlusFrameCueTypeEditor> Editor =
							WeakEditor.Pin();
						if (Editor.IsValid())
						{
							Editor->OpenLegacyBlueprintRecovery();
							return FReply::Handled();
						}
						return FReply::Unhandled();
					})
			]
		];
}

UEdGraph* FPaper2DPlusFrameCueTypeEditor::FindCueBehaviorGraph(const UBlueprint& Blueprint)
{
	return FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(Blueprint);
}

UK2Node_Event* FPaper2DPlusFrameCueTypeEditor::FindCueBehaviorEventNode(
	const UBlueprint& Blueprint,
	const FName EventName)
{
	return FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventNode(Blueprint, EventName);
}

bool FPaper2DPlusFrameCueTypeEditor::AddCueBehaviorEventNode(
	UBlueprint& Blueprint,
	const FName EventName)
{
	return FPaper2DPlusFrameCueTypeAuthoring::EnsureBehaviorEventNode(Blueprint, EventName);
}

bool FPaper2DPlusFrameCueTypeEditor::IsPermittedBehaviorDocument(
	const UObject* DocumentID) const
{
	const UEdGraph* Graph = Cast<UEdGraph>(DocumentID);
	const UBlueprint* Blueprint = GetBlueprintObj();
	return Graph && Blueprint && Graph == FindCueBehaviorGraph(*Blueprint);
}

TArray<FEditedDocumentInfo> FPaper2DPlusFrameCueTypeEditor::CollectPermittedEditedDocuments(
	const UBlueprint& Blueprint)
{
	const UEdGraph* BehaviorGraph = FindCueBehaviorGraph(Blueprint);
	const FSoftObjectPath PermittedPath =
		BehaviorGraph ? FSoftObjectPath(BehaviorGraph) : FSoftObjectPath();
	TArray<FEditedDocumentInfo> Permitted;
	if (PermittedPath.IsNull())
	{
		// A behavior-free Cue Type has no document worth restoring at all.
		return Permitted;
	}
	for (const FEditedDocumentInfo& Document : Blueprint.LastEditedDocuments)
	{
		if (Document.EditedObjectPath == PermittedPath)
		{
			Permitted.Add(Document);
		}
	}
	return Permitted;
}

TSharedPtr<SDockTab> FPaper2DPlusFrameCueTypeEditor::OpenDocument(
	const UObject* DocumentID,
	FDocumentTracker::EOpenDocumentCause Cause)
{
	if (!IsPermittedBehaviorDocument(DocumentID))
	{
		return nullptr;
	}
	return FBlueprintEditor::OpenDocument(DocumentID, Cause);
}

void FPaper2DPlusFrameCueTypeEditor::JumpToHyperlink(
	const UObject* ObjectReference,
	const bool bRequestRename)
{
	const UEdGraph* TargetGraph = Cast<UEdGraph>(ObjectReference);
	if (!TargetGraph)
	{
		if (const UEdGraphNode* TargetNode = Cast<UEdGraphNode>(ObjectReference))
		{
			TargetGraph = TargetNode->GetGraph();
		}
	}
	if (TargetGraph && !IsPermittedBehaviorDocument(TargetGraph))
	{
		return;
	}
	FBlueprintEditor::JumpToHyperlink(ObjectReference, bRequestRename);
}

bool FPaper2DPlusFrameCueTypeEditor::ShowCueBehaviorEvent(const FName EventName)
{
	UBlueprint* Blueprint = GetBlueprintObj();
	if (!Blueprint)
	{
		return false;
	}
	if (!FindCueBehaviorEventNode(*Blueprint, EventName))
	{
		const FScopedTransaction Transaction(LOCTEXT(
			"ImplementCueBehaviorEvent",
			"Implement Cue Behavior Event"));
		Blueprint->Modify();
		if (!AddCueBehaviorEventNode(*Blueprint, EventName))
		{
			return false;
		}
		// The invariant guard removes graphs that appear outside the accepted baseline. The event
		// graph is authored here deliberately, so adopt only that graph. A broad baseline recapture
		// would accidentally bless a concurrently introduced function graph or timeline.
		AcceptPermittedBehaviorGraph();
	}

	UK2Node_Event* EventNode = FindCueBehaviorEventNode(*Blueprint, EventName);
	if (!EventNode)
	{
		return false;
	}
	if (UEdGraph* BehaviorGraph = FindCueBehaviorGraph(*Blueprint))
	{
		OpenDocument(BehaviorGraph, FDocumentTracker::OpenNewDocument);
	}
	JumpToHyperlink(EventNode, false);
	RefreshMyBlueprint();
	return true;
}

void FPaper2DPlusFrameCueTypeEditor::InitFrameCueTypeEditor(
	EToolkitMode::Type Mode,
	const TSharedPtr<IToolkitHost>& InitToolkitHost,
	UBlueprint* Blueprint,
	TSharedPtr<FPaper2DPlusFrameCuePendingPlacement> InPendingPlacement)
{
	check(IsSupportedBlueprint(Blueprint));
	PendingPlacement = MoveTemp(InPendingPlacement);
	// Common initialization restores the asset's remembered documents through the Blueprint editor's
	// own path, which does not pass this class's document gate. Hand that restore a filtered VIEW of
	// the list — the permitted behavior graph only — and put the asset's own list back the moment
	// initialization returns. Opening the restricted editor is an inspection, never an edit: a legacy
	// asset keeps every recovery entry it was saved with, and no save downstream can observe the
	// filtered form because nothing runs between the swap and the restore but initialization itself.
	TArray<FEditedDocumentInfo> PersistedEditedDocuments;
	if (Blueprint)
	{
		PersistedEditedDocuments = Blueprint->LastEditedDocuments;
		Blueprint->LastEditedDocuments = CollectPermittedEditedDocuments(*Blueprint);
	}
	TArray<UBlueprint*> Blueprints;
	Blueprints.Add(Blueprint);
	InitBlueprintEditor(Mode, InitToolkitHost, Blueprints, false);
	if (Blueprint)
	{
		Blueprint->LastEditedDocuments = MoveTemp(PersistedEditedDocuments);
	}
	if (const TSharedPtr<IDetailsView> DefaultPropertyView =
		GetDefaultEditor()->GetPropertyView())
	{
		// Cue Type DEFAULTS, not a placement: nothing here was ever imported from a Frame Event, so the
		// migration bookkeeping fields are filtered out with a null placement rather than rendering a
		// permanently-empty Migration category on every type a designer opens.
		DefaultPropertyView->SetIsPropertyVisibleDelegate(
			FIsPropertyVisible::CreateLambda(
				[](const FPropertyAndParent& PropertyAndParent)
				{
					return Paper2DPlusFrameCueTabPresentation::IsPlacementDetailsPropertyVisible(
						&PropertyAndParent.Property,
						/*Placement*/ nullptr);
				}));
	}
	// FBlueprintEditor unconditionally registers graph AND timeline document factories during common
	// initialization. Keep only the graph factory: Cue behavior lives in one permitted event graph,
	// while timelines stay rejected by the envelope and must have no authoring surface at all.
	{
		const TSharedPtr<FDocumentTabFactory> GraphFactory = GraphEditorTabFactoryPtr.Pin();
		DocumentManager->ClearDocumentFactories();
		if (GraphFactory.IsValid())
		{
			DocumentManager->RegisterDocumentFactory(GraphFactory);
		}
	}
	CaptureAuthoringBaseline();
	if (UEdGraph* BehaviorGraph = Blueprint ? FindCueBehaviorGraph(*Blueprint) : nullptr)
	{
		OpenDocument(BehaviorGraph, FDocumentTracker::OpenNewDocument);
	}
}

FPaper2DPlusFrameCueTypeEditor::FPaper2DPlusFrameCueTypeEditor() = default;

FPaper2DPlusFrameCueTypeEditor::~FPaper2DPlusFrameCueTypeEditor()
{
	if (PendingPlacement.IsValid())
	{
		PendingPlacement->ConsumeWithoutPlacement();
		PendingPlacement.Reset();
	}
}

bool FPaper2DPlusFrameCueTypeEditor::IsSupportedBlueprint(const UBlueprint* Blueprint)
{
	return Blueprint
		&& Blueprint->ParentClass
		&& (Blueprint->ParentClass->IsChildOf(UPaper2DPlusCue::StaticClass())
			|| Blueprint->ParentClass->IsChildOf(UPaper2DPlusCueState::StaticClass()));
}

bool FPaper2DPlusFrameCueTypeEditor::ShouldUseStockMyBlueprint(
	const UBlueprint& Blueprint)
{
	return Blueprint.IsA<UPaper2DPlusFrameCueBlueprint>()
		&& ValidateDataOnlyBlueprint(Blueprint);
}

bool FPaper2DPlusFrameCueTypeEditor::ShouldOfferLegacyBlueprintRecovery(
	const UBlueprint& Blueprint)
{
	if (!IsSupportedBlueprint(&Blueprint))
	{
		return false;
	}
	const FPaper2DPlusFrameCueLegacyGraphInventory Inventory =
		FPaper2DPlusFrameCueTypeAuthoring::InventoryLegacyGraphs(Blueprint);
	// A specialized Cue Type's own event graph is the supported behavior surface, so only graph
	// families the envelope still rejects mean quarantined content. A legacy Cue Blueprint has no
	// envelope at all, so any graph it owns keeps offering explicit recovery.
	return Blueprint.IsA<UPaper2DPlusFrameCueBlueprint>()
		? Inventory.CountUnsupportedGraphs() > 0
		: Inventory.HasBehaviorGraphs();
}

FText FPaper2DPlusFrameCueTypeEditor::BuildLegacyContentWarningText(
	const UBlueprint& Blueprint)
{
	const FPaper2DPlusFrameCueLegacyGraphInventory Inventory =
		FPaper2DPlusFrameCueTypeAuthoring::InventoryLegacyGraphs(Blueprint);
	int32 EventGraphs = 0;
	int32 FunctionGraphs = 0;
	int32 MacroGraphs = 0;
	int32 DelegateGraphs = 0;
	int32 InterfaceGraphs = 0;
	int32 Nodes = 0;
	for (const FPaper2DPlusFrameCueLegacyGraphEntry& Graph : Inventory.Graphs)
	{
		Nodes += Graph.NodeCount;
		if (Graph.GraphKind == TEXT("Event"))
		{
			++EventGraphs;
		}
		else if (Graph.GraphKind == TEXT("Function"))
		{
			++FunctionGraphs;
		}
		else if (Graph.GraphKind == TEXT("Macro"))
		{
			++MacroGraphs;
		}
		else if (Graph.GraphKind == TEXT("Delegate"))
		{
			++DelegateGraphs;
		}
		else if (Graph.GraphKind == TEXT("Interface"))
		{
			++InterfaceGraphs;
		}
	}

	return FText::Format(
		LOCTEXT(
			"LegacyContentWarning",
			"Legacy Blueprint behavior is preserved and restricted here: {0} graphs "
			"({1} Event, {2} Function, {3} Macro, {4} Delegate, {5} Interface), "
			"{6} nodes. This editor never opens, removes, or saves those graphs."),
		Inventory.Graphs.Num(),
		EventGraphs,
		FunctionGraphs,
		MacroGraphs,
		DelegateGraphs,
		InterfaceGraphs,
		Nodes);
}

bool FPaper2DPlusFrameCueTypeEditor::ExecuteLegacyBlueprintRecovery(
	UBlueprint& Blueprint,
	TFunctionRef<bool(UBlueprint&)> OpenCommand)
{
	return ShouldOfferLegacyBlueprintRecovery(Blueprint)
		&& OpenCommand(Blueprint);
}

bool FPaper2DPlusFrameCueTypeEditor::OpenLegacyBlueprintRecovery()
{
	UBlueprint* Blueprint = GetBlueprintObj();
	if (!Blueprint || !ShouldOfferLegacyBlueprintRecovery(*Blueprint))
	{
		return false;
	}

	const FText Warning = FText::Format(
		LOCTEXT(
			"OpenLegacyBlueprintRecoveryWarning",
			"{0}\n\nAdvanced recovery opens Unreal's standard Blueprint editor, where executable "
			"behavior can be changed. Paper2D+ will continue to quarantine this Cue Type until every "
			"behavior graph is deliberately removed. Open the standard editor?"),
		BuildLegacyContentWarningText(*Blueprint));
	if (FMessageDialog::Open(EAppMsgType::YesNo, Warning) != EAppReturnType::Yes)
	{
		return false;
	}

	return ExecuteLegacyBlueprintRecovery(
		*Blueprint,
		[](UBlueprint& LegacyBlueprint)
		{
			FBlueprintEditorModule& BlueprintEditorModule =
				FModuleManager::LoadModuleChecked<FBlueprintEditorModule>(TEXT("Kismet"));
			BlueprintEditorModule.CreateBlueprintEditor(
				EToolkitMode::Standalone,
				TSharedPtr<IToolkitHost>(),
				&LegacyBlueprint,
				false);
			return true;
		});
}

bool FPaper2DPlusFrameCueTypeEditor::ValidateDataOnlyBlueprint(
	const UBlueprint& Blueprint,
	FText* OutError)
{
	return UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(Blueprint, OutError);
}

bool FPaper2DPlusFrameCueTypeEditor::ValidateCompiledDataOnlyBlueprint(
	const UBlueprint& Blueprint,
	FText* OutError)
{
	return UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
		Blueprint,
		OutError);
}

bool FPaper2DPlusFrameCueTypeEditor::ValidateCompiledSchemaParity(
	const UBlueprint& Blueprint,
	FText* OutError)
{
	return FPaper2DPlusFrameCueTypeAuthoring::ValidateCompiledSchemaParity(
		Blueprint, OutError);
}

bool FPaper2DPlusFrameCueTypeEditor::PrepareDurableSchemaSaveAttempt(
	UPaper2DPlusFrameCueBlueprint& Blueprint,
	FPaper2DPlusFrameCueDurableSaveAttempt& OutAttempt,
	FText* OutError)
{
	using namespace Paper2DPlusFrameCueTypeEditorInternal;
	OutAttempt = FPaper2DPlusFrameCueDurableSaveAttempt();
	UPackage* Package = Blueprint.GetOutermost();
	if (!Package)
	{
		SetError(
			OutError,
			LOCTEXT("DurableAttemptMissingPackage", "The Cue Type has no package to save."));
		return false;
	}

	OutAttempt.CueType = &Blueprint;
	OutAttempt.Package = Package;
	OutAttempt.PreviousVersion = Blueprint.DurableSchemaVersion;
	OutAttempt.PreviousFingerprint = Blueprint.DurableSchemaFingerprint;
	OutAttempt.PreviousSchemaSnapshot = Blueprint.DurableSchemaSnapshot;
	OutAttempt.PreviousVariables = Blueprint.DurableVariables;
	OutAttempt.PreviousDefaultValues = Blueprint.DurableDefaultValues;
	OutAttempt.bPackageWasDirty = Package->IsDirty();
	const FPaper2DPlusFrameCueDurableSchemaCandidate Candidate =
		FPaper2DPlusFrameCueTypeAuthoring::PrepareDurableSchemaSaveCandidate(Blueprint);
	if (!Candidate.bSuccess)
	{
		RestoreDurableSchemaSaveAttempt(Blueprint, OutAttempt);
		SetError(OutError, Candidate.Error);
		return false;
	}

	OutAttempt.CandidateVersion = Candidate.Version;
	OutAttempt.CandidateFingerprint = Candidate.Fingerprint;
	OutAttempt.CandidateSchemaSnapshot = Candidate.SchemaSnapshot;
	OutAttempt.bPrepared = true;
	SetError(OutError, FText::GetEmpty());
	return true;
}

void FPaper2DPlusFrameCueTypeEditor::RestoreDurableSchemaSaveAttempt(
	UPaper2DPlusFrameCueBlueprint& Blueprint,
	const FPaper2DPlusFrameCueDurableSaveAttempt& Attempt)
{
	if (Attempt.CueType.Get() != &Blueprint)
	{
		return;
	}
	Blueprint.DurableSchemaVersion = Attempt.PreviousVersion;
	Blueprint.DurableSchemaFingerprint = Attempt.PreviousFingerprint;
	Blueprint.DurableSchemaSnapshot = Attempt.PreviousSchemaSnapshot;
	Blueprint.DurableVariables = Attempt.PreviousVariables;
	Blueprint.DurableDefaultValues = Attempt.PreviousDefaultValues;
	if (UPackage* Package = Blueprint.GetOutermost())
	{
		Package->SetDirtyFlag(Attempt.bPackageWasDirty);
	}
}

bool FPaper2DPlusFrameCueTypeEditor::IsDurablyReadyForPlacement(
	const UBlueprint& Blueprint)
{
	const UPaper2DPlusFrameCueBlueprint* CueType =
		Cast<UPaper2DPlusFrameCueBlueprint>(&Blueprint);
	if (!CueType || !Blueprint.GeneratedClass || !Blueprint.GetOutermost()
		|| Blueprint.GetOutermost()->IsDirty())
	{
		return false;
	}
	return FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(Blueprint.GeneratedClass)
		.Availability == EPaper2DPlusFrameCueTypeAvailability::Ready;
}

bool FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(
	UPaper2DPlusFrameCueBlueprint& Blueprint,
	const FPaper2DPlusFrameCueDurableSaveAttempt& Attempt,
	FText* OutError)
{
	using namespace Paper2DPlusFrameCueTypeEditorInternal;
	const bool bExactAttempt = Attempt.bPrepared
		&& Attempt.CueType.Get() == &Blueprint
		&& Attempt.Package.Get() == Blueprint.GetOutermost();
	const bool bExactCandidate = bExactAttempt
		&& Blueprint.DurableSchemaVersion == Attempt.CandidateVersion
		&& Blueprint.DurableSchemaFingerprint == Attempt.CandidateFingerprint
		&& Blueprint.DurableSchemaSnapshot == Attempt.CandidateSchemaSnapshot;
	if (bExactCandidate && IsDurablyReadyForPlacement(Blueprint))
	{
		SetError(OutError, FText::GetEmpty());
		return true;
	}

	if (bExactAttempt)
	{
		RestoreDurableSchemaSaveAttempt(Blueprint, Attempt);
	}
	SetError(
		OutError,
		LOCTEXT(
			"DurableAttemptNotSaved",
			"The Cue Type package did not complete a durable save. Its previous durable metadata "
			"was restored, authored edits remain available, and no placement was created."));
	return false;
}

bool FPaper2DPlusFrameCueTypeEditor::PrepareNewlyCreatedBlueprint(
	UBlueprint& Blueprint,
	FText* OutError)
{
	using namespace Paper2DPlusFrameCueTypeEditorInternal;

	if (!Blueprint.IsA<UPaper2DPlusFrameCueBlueprint>())
	{
		SetError(OutError, LOCTEXT("NotSpecializedBlueprint", "The asset is not a Paper2D+ Frame Cue Type."));
		return false;
	}
	if (!IsSupportedBlueprint(&Blueprint))
	{
		SetError(OutError, LOCTEXT(
			"UnsupportedCueParent",
			"The new asset is not derived from a Paper2D+ Cue or Cue State."));
		return false;
	}
	if (!Blueprint.bIsNewlyCreated)
	{
		SetError(OutError, LOCTEXT(
			"ExistingBlueprintCleanupDenied",
			"New-asset graph cleanup is allowed only during initial Cue Type creation."));
		return false;
	}

	if (Blueprint.FunctionGraphs.Num() > 0
		|| Blueprint.MacroGraphs.Num() > 0
		|| Blueprint.DelegateSignatureGraphs.Num() > 0
		|| Blueprint.UbergraphPages.Num() > 1)
	{
		SetError(OutError, LOCTEXT(
			"UnexpectedNewBlueprintGraphs",
			"The new Cue Type contains unexpected Blueprint behavior graphs and was not modified."));
		return false;
	}

	// Engine versions differ on whether CreateBlueprint already made the default Event Graph. Either
	// way the normalized shape is the same: exactly one empty event graph, ready to be seeded.
	if (Blueprint.UbergraphPages.Num() == 1)
	{
		const UEdGraph* EventGraph = Blueprint.UbergraphPages[0];
		if (!EventGraph || EventGraph->Nodes.Num() > 0)
		{
			SetError(OutError, LOCTEXT(
				"NonEmptyNewBlueprintGraph",
				"The new Cue Type's Event Graph is not empty and was preserved for safety."));
			return false;
		}
	}
	else
	{
		UEdGraph* EventGraph = FBlueprintEditorUtils::CreateNewGraph(
			&Blueprint,
			UEdGraphSchema_K2::GN_EventGraph,
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());
		if (!EventGraph)
		{
			SetError(OutError, LOCTEXT(
				"NewBlueprintEventGraphFailed",
				"The new Cue Type's behavior event graph could not be created."));
			return false;
		}
		FBlueprintEditorUtils::AddUbergraphPage(&Blueprint, EventGraph);
		Blueprint.LastEditedDocuments.Add(EventGraph);
	}

	const bool bHasOnlyPermittedGraphs = Blueprint.UbergraphPages.Num() == 1
		&& Blueprint.FunctionGraphs.Num() == 0
		&& Blueprint.MacroGraphs.Num() == 0
		&& Blueprint.DelegateSignatureGraphs.Num() == 0;
	if (!bHasOnlyPermittedGraphs)
	{
		SetError(OutError, LOCTEXT(
			"NewBlueprintStillHasGraphs",
			"The new Cue Type could not be normalized onto one permitted behavior event graph."));
		return false;
	}

	// FKismet's default UObject compiler creates UBlueprintGeneratedClass directly when no class
	// export already exists, even when CreateBlueprint was passed a more specialized generated
	// class. Preseed the new asset with our runtime envelope once, before it can have instances or
	// placements. Subsequent compiles preserve it, and GetBlueprintClass covers load regeneration.
	if (!Blueprint.GeneratedClass
		|| !Blueprint.GeneratedClass->GetClass()->IsChildOf(
			UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass())
		|| Blueprint.GeneratedClass->ClassGeneratedBy != &Blueprint
		|| Blueprint.GeneratedClass->GetSuperClass() != Blueprint.ParentClass)
	{
		FName GeneratedClassName;
		FName SkeletonClassName;
		Blueprint.GetBlueprintClassNames(GeneratedClassName, SkeletonClassName);
		FBlueprintEditorUtils::RemoveGeneratedClasses(&Blueprint);

		UPaper2DPlusFrameCueBlueprintGeneratedClass* GeneratedClass =
			NewObject<UPaper2DPlusFrameCueBlueprintGeneratedClass>(
				Blueprint.GetOutermost(),
				GeneratedClassName,
				RF_Public | RF_Transactional);
		if (!GeneratedClass)
		{
			SetError(OutError, LOCTEXT(
				"GeneratedClassEnvelopeFailed",
				"The Cue Type generated-class envelope could not be created."));
			return false;
		}

		Blueprint.GeneratedClass = GeneratedClass;
		GeneratedClass->ClassGeneratedBy = &Blueprint;
		GeneratedClass->SetSuperStruct(Blueprint.ParentClass);
		Paper2DPlusFrameCueTypeEditorInternal::CompileBlueprintWithoutAutomaticSave(
			Blueprint,
			EBlueprintCompileOptions::SkipGarbageCollection);
	}

	// Seed the behavior events the chosen Cue kind guarantees, so a fresh Cue Type opens onto an
	// implementable graph instead of an empty one the designer has to discover how to populate.
	if (FPaper2DPlusFrameCueTypeAuthoring::SeedDeclaredBehaviorEvents(Blueprint))
	{
		Paper2DPlusFrameCueTypeEditorInternal::CompileBlueprintWithoutAutomaticSave(
			Blueprint,
			EBlueprintCompileOptions::SkipGarbageCollection);
	}

	FText CompiledContractError;
	if (!ValidateCompiledDataOnlyBlueprint(Blueprint, &CompiledContractError))
	{
		SetError(OutError, FText::Format(
			LOCTEXT(
				"GeneratedClassEnvelopeCompileFailed",
				"The new Cue Type could not compile with its runtime class envelope: {0}"),
			CompiledContractError));
		return false;
	}

	return true;
}

TArray<FName> FPaper2DPlusFrameCueTypeEditor::GetAuthoringTabIdsForTests()
{
	return {
		FBlueprintEditorTabs::MyBlueprintID,
		FBlueprintEditorTabs::DetailsID,
		FBlueprintEditorTabs::DefaultEditorID,
		FBlueprintEditorTabs::CompilerResultsID,
		FBlueprintEditorTabs::FindResultsID
	};
}

#if WITH_DEV_AUTOMATION_TESTS
TArray<FName> FPaper2DPlusFrameCueTypeEditor::GetRegisteredAuthoringTabIdsForTests() const
{
	const TSharedPtr<FApplicationMode> CurrentMode = GetCurrentModePtr();
	const TSharedPtr<Paper2DPlusFrameCueTypeEditorInternal::FApplicationMode> CueTypeMode =
		StaticCastSharedPtr<Paper2DPlusFrameCueTypeEditorInternal::FApplicationMode>(
			CurrentMode);
	return CueTypeMode.IsValid()
		? CueTypeMode->GetRegisteredTabIdsForTests()
		: TArray<FName>();
}
#endif

TArray<FName> FPaper2DPlusFrameCueTypeEditor::GetCueOverrideEventNames(
	const UBlueprint& Blueprint)
{
	return Blueprint.ParentClass
		? Paper2DPlusFrameCueBehavior::GetDeclaredEventNamesForClass(
			Blueprint.ParentClass)
		: TArray<FName>();
}

TArray<FName> FPaper2DPlusFrameCueTypeEditor::GetBlockedBlueprintCommandNamesForTests()
{
	return {
		TEXT("ReparentBlueprint"),
		TEXT("BeginBlueprintMerge"),
		TEXT("AddNewLocalVariable"),
		TEXT("AddNewFunction"),
		TEXT("AddNewMacroDeclaration"),
		TEXT("AddNewAnimationLayer"),
		TEXT("AddNewEventGraph"),
		TEXT("AddNewDelegate"),
		TEXT("RefreshAllNodes"),
		TEXT("DeleteUnusedVariables")
	};
}

TArray<FName> FPaper2DPlusFrameCueTypeEditor::GetBlockedFullBlueprintCommandNamesForTests()
{
	return {
		TEXT("SwitchToScriptingMode"),
		TEXT("SwitchToBlueprintDefaultsMode"),
		TEXT("SwitchToComponentsMode"),
		TEXT("EditGlobalOptions"),
		TEXT("JumpToErrorNode"),
		TEXT("SaveOnCompile_Never"),
		TEXT("SaveOnCompile_SuccessOnly"),
		TEXT("SaveOnCompile_Always")
	};
}

TArray<FName> FPaper2DPlusFrameCueTypeEditor::GetBlockedMyBlueprintCommandNamesForTests()
{
	return {
		TEXT("OpenGraph"),
		TEXT("OpenGraphInNewTab"),
		TEXT("OpenExternalGraph"),
		TEXT("FocusNode"),
		TEXT("FocusNodeInNewTab"),
		TEXT("ImplementFunction"),
		TEXT("PasteLocalVariable"),
		TEXT("PasteFunction"),
		TEXT("PasteMacro")
	};
}

TArray<FName> FPaper2DPlusFrameCueTypeEditor::GetBlockedGenericCommandNamesForTests()
{
	// Stock My Blueprint owns ordinary variable cut/copy/paste, duplicate, delete, and rename.
	return {};
}

bool FPaper2DPlusFrameCueTypeEditor::ShowPayloadVariableDetails(FName VariableName)
{
	UBlueprint* Blueprint = GetBlueprintObj();
	if (!Blueprint || VariableName.IsNone()
		|| !Blueprint->NewVariables.ContainsByPredicate(
			[VariableName](const FBPVariableDescription& Variable)
			{
				return Variable.VarName == VariableName;
			}))
	{
		return false;
	}

	// Unreal's private Blueprint variable Details customization reads selection state from the
	// stock My Blueprint widget. Showing a property wrapper directly would
	// produce an Inspector object without the native type and metadata editing controls. Reusable
	// generated values belong to the separate Class Defaults surface.
	if (!GetMyBlueprintWidget().IsValid())
	{
		return false;
	}
	RefreshMyBlueprint();
	SelectGraphActionItemByName(
		VariableName, ESelectInfo::Direct, NodeSectionID::VARIABLE, false);
	if (const TSharedPtr<SDockTab> DetailsTab =
		GetTabManager()->TryInvokeTab(FBlueprintEditorTabs::DetailsID))
	{
		DetailsTab->ActivateInParent(ETabActivationCause::SetDirectly);
	}
	return true;
}

FName FPaper2DPlusFrameCueTypeEditor::GetToolkitFName() const
{
	return FName(TEXT("Paper2DPlusFrameCueTypeEditor"));
}

FText FPaper2DPlusFrameCueTypeEditor::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "Frame Cue Type");
}

bool FPaper2DPlusFrameCueTypeEditor::ValidateCurrentBlueprint(FText* OutError) const
{
	const UBlueprint* Blueprint = GetBlueprintObj();
	if (!Blueprint)
	{
		Paper2DPlusFrameCueTypeEditorInternal::SetError(
			OutError,
			LOCTEXT("MissingEditedBlueprint", "The Frame Cue Type is no longer available."));
		return false;
	}
	return ValidateDataOnlyBlueprint(*Blueprint, OutError);
}

void FPaper2DPlusFrameCueTypeEditor::ReportDataOnlyViolation(
	const FText& Action,
	const FText& Error) const
{
	const FText Message = FText::Format(
		LOCTEXT("DataOnlyActionBlocked", "{0} blocked: {1}"),
		Action,
		Error);
	UE_LOG(LogPaper2DPlusEditor, Warning, TEXT("%s"), *Message.ToString());

	FNotificationInfo Notification(Message);
	Notification.bFireAndForget = true;
	Notification.ExpireDuration = 6.0f;
	Notification.bUseLargeFont = false;
	Notification.bUseSuccessFailIcons = true;
	if (const TSharedPtr<SNotificationItem> Item =
		FSlateNotificationManager::Get().AddNotification(Notification))
	{
		Item->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void FPaper2DPlusFrameCueTypeEditor::SetDestructiveCompileConfirmationForTests(
	TFunction<bool(const FPaper2DPlusFrameCueSchemaPreflight&)> InConfirmation)
{
	Paper2DPlusFrameCueTypeEditorInternal::DestructiveCompileConfirmationForTests =
		MoveTemp(InConfirmation);
}

void FPaper2DPlusFrameCueTypeEditor::ClearDestructiveCompileConfirmationForTests()
{
	Paper2DPlusFrameCueTypeEditorInternal::DestructiveCompileConfirmationForTests = nullptr;
}

void FPaper2DPlusFrameCueTypeEditor::SetBaselineRecoveryCompileConfirmationForTests(
	TFunction<bool(const FPaper2DPlusFrameCueSchemaPreflight&)> InConfirmation)
{
	Paper2DPlusFrameCueTypeEditorInternal::BaselineRecoveryCompileConfirmationForTests =
		MoveTemp(InConfirmation);
}

void FPaper2DPlusFrameCueTypeEditor::ClearBaselineRecoveryCompileConfirmationForTests()
{
	Paper2DPlusFrameCueTypeEditorInternal::BaselineRecoveryCompileConfirmationForTests = nullptr;
}
#endif

bool FPaper2DPlusFrameCueTypeEditor::ConfirmSchemaPreflight(
	const FPaper2DPlusFrameCueSchemaPreflight& Preflight) const
{
	if (!Preflight.bRequiresDestructiveConfirmation)
	{
		return true;
	}
#if WITH_DEV_AUTOMATION_TESTS
	if (Paper2DPlusFrameCueTypeEditorInternal::DestructiveCompileConfirmationForTests)
	{
		return Paper2DPlusFrameCueTypeEditorInternal::DestructiveCompileConfirmationForTests(
			Preflight);
	}
#endif

	FString ChangeList;
	for (const FPaper2DPlusFrameCueSchemaChange& Change : Preflight.Diff.Changes)
	{
		if (!ChangeList.IsEmpty())
		{
			ChangeList += TEXT("\n");
		}
		ChangeList += TEXT("  • ");
		ChangeList += Change.Summary.ToString();
	}
	if (ChangeList.IsEmpty())
	{
		ChangeList = TEXT("  • A referenced payload dependency changed.");
	}
	const FText Prompt = FText::Format(
		LOCTEXT(
			"ConfirmDestructiveCueSchemaCompile",
			"This Cue Type change can discard or reset values on existing placements:\n\n{0}\n\nCompile it anyway? The durable schema and loaded placement values are retained until this Cue Type saves successfully; the next successful save (including Save All) commits the change."),
		FText::FromString(ChangeList));
	return FMessageDialog::Open(EAppMsgType::YesNo, Prompt) == EAppReturnType::Yes;
}

bool FPaper2DPlusFrameCueTypeEditor::ConfirmBaselineUnavailableCompile(
	const FPaper2DPlusFrameCueSchemaPreflight& Preflight) const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (Paper2DPlusFrameCueTypeEditorInternal::BaselineRecoveryCompileConfirmationForTests)
	{
		return Paper2DPlusFrameCueTypeEditorInternal::
			BaselineRecoveryCompileConfirmationForTests(Preflight);
	}
#endif

	const FText Prompt = FText::Format(
		LOCTEXT(
			"ConfirmBaselineUnavailableCueCompile",
			"This Cue Type's stored durable schema baseline cannot be used:\n\n  {0}\n\n"
			"Without that baseline this compile cannot measure the edit against saved placements, "
			"and no placement-value recovery snapshot can be captured.\n\n"
			"Compile anyway? The next successful save of this Cue Type re-establishes the durable "
			"baseline from the compiled schema."),
		Preflight.DurableBaselineError);
	return FMessageDialog::Open(EAppMsgType::YesNo, Prompt) == EAppReturnType::Yes;
}

bool FPaper2DPlusFrameCueTypeEditor::CaptureCompileRecoveryState(
	UPaper2DPlusFrameCueBlueprint& Blueprint,
	const FPaper2DPlusFrameCueSchemaPreflight& Preflight,
	FText* OutError)
{
	using namespace Paper2DPlusFrameCueTypeEditorInternal;
	if (Blueprint.DurableSchemaFingerprint.IsEmpty() || !Preflight.bHasDurableBaseline)
	{
		return true;
	}

	if (CompiledRecoveryState.IsValid()
		&& CompiledRecoveryState->CueType.Get() == &Blueprint
		&& CompiledRecoveryState->DurableFingerprintAtCapture
			== Blueprint.DurableSchemaFingerprint)
	{
		CompiledRecoveryState->ProposedVariables = Blueprint.NewVariables;
		if (!FPaper2DPlusFrameCueTypeAuthoring::CaptureDefaultValues(
			Blueprint,
			CompiledRecoveryState->ProposedDefaultValues,
			OutError))
		{
			return false;
		}
		MergePendingDefaultProposal(
			Blueprint,
			CompiledRecoveryState->ProposedDefaultValues);
		MergeBufferedVariableDefaults(
			CompiledRecoveryState->ProposedVariables,
			CompiledRecoveryState->ProposedDefaultValues);
		CopyDefaultsIntoVariableModel(
			CompiledRecoveryState->ProposedVariables,
			CompiledRecoveryState->ProposedDefaultValues);
		if (!WriteRecoveryArtifact(
			Blueprint,
			Preflight,
			CompiledRecoveryState->ProposedDefaultValues,
			CompiledRecoveryState->ArtifactPath,
			OutError))
		{
			return false;
		}
		StorePendingDefaultProposal(
			Blueprint,
			CompiledRecoveryState->ProposedDefaultValues,
			true);
		return true;
	}

	TUniquePtr<FPaper2DPlusFrameCueCompiledRecoveryState> Recovery =
		MakeUnique<FPaper2DPlusFrameCueCompiledRecoveryState>();
	Recovery->CueType = &Blueprint;
	Recovery->DurableVariables = Blueprint.DurableVariables;
	Recovery->DurableDefaultValues = Blueprint.DurableDefaultValues;
	Recovery->DurableFingerprintAtCapture = Blueprint.DurableSchemaFingerprint;
	Recovery->ProposedVariables = Blueprint.NewVariables;
	if (!FPaper2DPlusFrameCueTypeAuthoring::CaptureDefaultValues(
		Blueprint,
		Recovery->ProposedDefaultValues,
		OutError))
	{
		return false;
	}
	MergePendingDefaultProposal(
		Blueprint,
		Recovery->ProposedDefaultValues);
	MergeBufferedVariableDefaults(
		Recovery->ProposedVariables,
		Recovery->ProposedDefaultValues);
	CopyDefaultsIntoVariableModel(
		Recovery->ProposedVariables,
		Recovery->ProposedDefaultValues);
	if (!WriteRecoveryArtifact(
		Blueprint,
		Preflight,
		Recovery->ProposedDefaultValues,
		Recovery->ArtifactPath,
		OutError))
	{
		return false;
	}
	StorePendingDefaultProposal(
		Blueprint,
		Recovery->ProposedDefaultValues,
		true);
	if (UPackage* CuePackage = Blueprint.GetOutermost())
	{
		Recovery->PackageDirtyStates.Add(CuePackage, CuePackage->IsDirty());
	}

	UClass* AffectedClass = Blueprint.GeneratedClass;
	for (TObjectIterator<UPaper2DPlusCueBase> It; It; ++It)
	{
		UPaper2DPlusCueBase* Cue = *It;
		if (!Cue
			|| !AffectedClass
			|| !Cue->GetClass()->IsChildOf(AffectedClass)
			|| Cue->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_Transient)
			|| (!Cue->GetTypedOuter<UPaper2DPlusCharacterProfileAsset>()
				&& !Cue->GetTypedOuter<UPaper2DPlusCharacterLayerAsset>()))
		{
			continue;
		}

		FPaper2DPlusFrameCueCompiledRecoveryState::FPlacement& Placement =
			Recovery->Placements.AddDefaulted_GetRef();
		Placement.Outer = Cue->GetOuter();
		Placement.ObjectName = Cue->GetFName();
		const UObject* CurrentDefaults = Cue->GetClass()->GetDefaultObject(false);
		for (TFieldIterator<FProperty> PropertyIt(
			Cue->GetClass(), EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly))
			{
				continue;
			}

			FString Value;
			const FString* DurableDefault =
				Recovery->DurableDefaultValues.Find(Property->GetFName());
			if (CurrentDefaults
				&& DurableDefault
				&& Property->Identical_InContainer(Cue, CurrentDefaults))
			{
				Value = *DurableDefault;
			}
			else
			{
				ExportPropertyValue(*Property, *Cue, Value);
			}
			Placement.PropertyValues.Add(Property->GetFName(), MoveTemp(Value));
		}
		if (UPackage* OwnerPackage = Cue->GetOutermost())
		{
			Recovery->PackageDirtyStates.FindOrAdd(OwnerPackage) = OwnerPackage->IsDirty();
		}
	}

	CompiledRecoveryState = MoveTemp(Recovery);
	SetError(OutError, FText::GetEmpty());
	return true;
}

bool FPaper2DPlusFrameCueTypeEditor::RestoreDurableSchemaAfterFailure(
	UPaper2DPlusFrameCueBlueprint& Blueprint,
	FText* OutError)
{
	using namespace Paper2DPlusFrameCueTypeEditorInternal;
	if (!CompiledRecoveryState.IsValid()
		|| CompiledRecoveryState->CueType.Get() != &Blueprint)
	{
		SetError(OutError, FText::GetEmpty());
		return true;
	}

	FPaper2DPlusFrameCueCompiledRecoveryState& Recovery = *CompiledRecoveryState;
	const TArray<FBPVariableDescription> ProposedVariables = Recovery.ProposedVariables;
	auto WithRetainedArtifact = [&Recovery](const FText& Error)
	{
		if (Recovery.ArtifactPath.IsEmpty())
		{
			return Error;
		}
		return FText::Format(
			LOCTEXT(
				"CueRecoveryArtifactRetained",
				"{0}\n\nThe exact recovery proposal remains available at:\n{1}"),
			Error,
			FText::FromString(Recovery.ArtifactPath));
	};
	auto ReapplyProposalForRetry = [&]()
	{
		Blueprint.NewVariables = ProposedVariables;
		StorePendingDefaultProposal(
			Blueprint,
			Recovery.ProposedDefaultValues,
			true);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		Blueprint.MarkPackageDirty();
		RefreshMyBlueprint();
	};
	auto FailRecovery = [&](const FText& PrimaryError)
	{
		ReapplyProposalForRetry();
		SetError(OutError, WithRetainedArtifact(PrimaryError));
		return false;
	};

	Blueprint.NewVariables = Recovery.DurableVariables;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
	Paper2DPlusFrameCueTypeEditorInternal::CompileBlueprintWithoutAutomaticSave(
		Blueprint,
		EBlueprintCompileOptions::SkipGarbageCollection);
	FText RecoveryError;
	if (!ValidateCompiledDataOnlyBlueprint(Blueprint, &RecoveryError)
		|| !FPaper2DPlusFrameCueTypeAuthoring::ApplyDefaultValues(
			Blueprint,
			Recovery.DurableDefaultValues,
			&RecoveryError))
	{
		return FailRecovery(RecoveryError.IsEmpty()
			? LOCTEXT(
				"CueSchemaRecoveryCompileFailed",
				"The durable Cue Type schema could not be recompiled after the save failure. The asset remains blocked for manual recovery.")
			: RecoveryError);
	}

	for (const FPaper2DPlusFrameCueCompiledRecoveryState::FPlacement& Snapshot :
		Recovery.Placements)
	{
		UObject* Outer = Snapshot.Outer.Get();
		UPaper2DPlusCueBase* Placement = Outer
			? FindObject<UPaper2DPlusCueBase>(Outer, *Snapshot.ObjectName.ToString())
			: nullptr;
		if (!Placement)
		{
			return FailRecovery(FText::Format(
				LOCTEXT(
					"CuePlacementRecoveryMissing",
					"Loaded placement '{0}' could not be reacquired after Cue Type reinstancing."),
				FText::FromName(Snapshot.ObjectName)));
		}
		Placement->Modify();
		for (const TPair<FName, FString>& Pair : Snapshot.PropertyValues)
		{
			FProperty* Property = FindFProperty<FProperty>(Placement->GetClass(), Pair.Key);
			if (!Property || !ImportPropertyValue(*Property, *Placement, Pair.Value))
			{
				return FailRecovery(FText::Format(
					LOCTEXT(
						"CuePlacementRecoveryValueFailed",
						"Placement '{0}' field '{1}' could not be restored after the failed save."),
					FText::FromName(Snapshot.ObjectName),
					FText::FromName(Pair.Key)));
			}
		}
	}

	for (const TPair<TWeakObjectPtr<UPackage>, bool>& Pair : Recovery.PackageDirtyStates)
	{
		if (UPackage* Package = Pair.Key.Get())
		{
			Package->SetDirtyFlag(Pair.Value);
		}
	}

	// Put the author's proposal back into the variable/default editors without compiling it again.
	// The generated class and every loaded placement remain at the last durable state until retry.
	// Keep the stable recovery artifact until the proposal commits; it remains the fallback if the
	// transient asset/editor state is later unloaded or the process exits before retry.
	ReapplyProposalForRetry();
	SetError(OutError, FText::GetEmpty());
	return true;
}

void FPaper2DPlusFrameCueTypeEditor::CompleteDurableSchemaCommit()
{
	if (UPaper2DPlusFrameCueBlueprint* Blueprint =
		Cast<UPaper2DPlusFrameCueBlueprint>(GetBlueprintObj()))
	{
		Paper2DPlusFrameCueTypeEditorInternal::ClearPendingDefaultProposal(*Blueprint);
		Blueprint->ConfirmedDestructiveSchemaFingerprint.Reset();
	}
	if (CompiledRecoveryState.IsValid())
	{
		Paper2DPlusFrameCueTypeEditorInternal::DeleteRecoveryArtifact(
			CompiledRecoveryState->ArtifactPath);
	}
	CompiledRecoveryState.Reset();
}

bool FPaper2DPlusFrameCueTypeEditor::IsCompilingEnabled() const
{
	return FBlueprintEditor::IsCompilingEnabled() && ValidateCurrentBlueprint();
}

bool FPaper2DPlusFrameCueTypeEditor::IsInAScriptingMode() const
{
	return IsModeCurrent(CueTypeModeName);
}

bool FPaper2DPlusFrameCueTypeEditor::CompileDataOnlyBlueprint()
{
	EnforceDataOnlyAuthoringInvariant(true);

	UBlueprint* Blueprint = GetBlueprintObj();
	FText ContractError;
	if (!Blueprint
		|| !FBlueprintEditor::IsCompilingEnabled()
		|| !ValidateCurrentBlueprint(&ContractError)
		|| !UPaper2DPlusFrameCueBlueprint::ValidateGeneratedClassEnvelope(
			*Blueprint,
			&ContractError))
	{
		if (ContractError.IsEmpty())
		{
			ContractError = LOCTEXT(
				"CompileUnavailable",
				"The Frame Cue Type is not currently available for compilation.");
		}
		ReportDataOnlyViolation(LOCTEXT("CompileAction", "Compile"), ContractError);
		return false;
	}

	const FPaper2DPlusFrameCueSchemaPreflight Preflight =
		FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(*Blueprint);
	bool bAuthorConfirmedSchemaCommit = false;
	if (!Preflight.bCanCompile)
	{
		// Invalid compatibility folds two different causes; present the real one instead of
		// always blaming the baseline. The untrusted-baseline case gets an informed-consent
		// escape rather than a dead end: the old advice — "save the unchanged type once" — was
		// unreachable whenever the same broken baseline also made the asset refuse to save.
		if (!Preflight.CurrentSchemaError.IsEmpty())
		{
			ReportDataOnlyViolation(
				LOCTEXT("CompileAction", "Compile"),
				FText::Format(
					LOCTEXT(
						"CueAuthoredSchemaInvalid",
						"The authored payload schema is invalid: {0}"),
					Preflight.CurrentSchemaError));
			return false;
		}
		if (Preflight.DurableBaselineError.IsEmpty())
		{
			ReportDataOnlyViolation(
				LOCTEXT("CompileAction", "Compile"),
				LOCTEXT(
					"CueSchemaBaselineUnavailable",
					"This Cue Type does not have a trustworthy durable schema baseline for the proposed edit. Save the unchanged type once to establish the baseline, then retry the schema change."));
			return false;
		}
		if (!ConfirmBaselineUnavailableCompile(Preflight))
		{
			return false;
		}
		bAuthorConfirmedSchemaCommit = true;
	}
	else if (!ConfirmSchemaPreflight(Preflight))
	{
		return false;
	}
	else if (Preflight.bRequiresDestructiveConfirmation)
	{
		bAuthorConfirmedSchemaCommit = true;
	}
	if (Preflight.Diff.Compatibility != EPaper2DPlusFrameCueSchemaCompatibility::Identical)
	{
		UPaper2DPlusFrameCueBlueprint* Specialized =
			Cast<UPaper2DPlusFrameCueBlueprint>(Blueprint);
		if (!Specialized
			|| !CaptureCompileRecoveryState(*Specialized, Preflight, &ContractError))
		{
			ReportDataOnlyViolation(
				LOCTEXT("CompileAction", "Compile"),
				ContractError.IsEmpty()
					? LOCTEXT(
						"CueCompileRecoverySnapshotFailed",
						"The durable Cue schema and loaded placements could not be inventoried safely, so compilation was blocked.")
					: ContractError);
			return false;
		}
	}

	{
		UBlueprintEditorSettings* Settings =
			GetMutableDefault<UBlueprintEditorSettings>();
		TGuardValue<TEnumAsByte<ESaveOnCompile>> SaveOnCompileGuard(
			Settings->SaveOnCompile,
			TEnumAsByte<ESaveOnCompile>(SoC_Never));
		FBlueprintEditor::Compile();
	}
	Blueprint = GetBlueprintObj();
	bool bAppliedExactPendingDefaults = true;
	if (UPaper2DPlusFrameCueBlueprint* Specialized =
		Cast<UPaper2DPlusFrameCueBlueprint>(Blueprint))
	{
		if (Specialized->bHasPendingDefaultProposal)
		{
			bAppliedExactPendingDefaults =
				FPaper2DPlusFrameCueTypeAuthoring::ApplyDefaultValues(
					*Specialized,
					Specialized->PendingDefaultProposalValues,
					&ContractError,
					false);
			if (bAppliedExactPendingDefaults)
			{
				Specialized->bPendingDefaultProposalNeedsCompile = false;
			}
		}
	}
	if (!Blueprint
		|| !bAppliedExactPendingDefaults
		|| !ValidateCompiledDataOnlyBlueprint(*Blueprint, &ContractError))
	{
		if (ContractError.IsEmpty())
		{
			ContractError = LOCTEXT(
				"CompileDidNotProduceReadyCueType",
				"Compilation did not produce a current behavior-capable Cue class.");
		}
		// The compiled-contract text ("must finish a clean full compile before it can be saved or
		// cooked") describes save/cook callers; reported from the Compile action itself it points
		// the author at Save while the actual failure sits in the Compiler Results panel.
		if (Blueprint && Blueprint->Status == BS_Error)
		{
			ContractError = LOCTEXT(
				"CueCompileItselfFailed",
				"The compile failed. Fix the errors listed in the Compiler Results panel, then "
				"compile again. Nothing was saved and the durable schema was not changed.");
		}
		if (UPaper2DPlusFrameCueBlueprint* Specialized =
			Cast<UPaper2DPlusFrameCueBlueprint>(Blueprint))
		{
			FText RecoveryError;
			if (!RestoreDurableSchemaAfterFailure(*Specialized, &RecoveryError)
				&& !RecoveryError.IsEmpty())
			{
				ContractError = FText::Format(
					LOCTEXT(
						"CueCompileAndRecoveryFailed",
						"{0}\n\nAutomatic durable-state recovery also failed: {1}"),
					ContractError,
					RecoveryError);
			}
		}
		ReportDataOnlyViolation(LOCTEXT("CompileAction", "Compile"), ContractError);
		return false;
	}
	if (CompiledRecoveryState.IsValid()
		&& CompiledRecoveryState->CueType.Get() == Blueprint)
	{
		const FPaper2DPlusFrameCueSchemaPreflight CompiledProposalPreflight =
			FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(*Blueprint);
		CompiledRecoveryState->ProposedVariables = Blueprint->NewVariables;
		FText ProposalSnapshotError;
		const bool bCapturedProposalDefaults =
			FPaper2DPlusFrameCueTypeAuthoring::CaptureDefaultValues(
				*Blueprint,
				CompiledRecoveryState->ProposedDefaultValues,
				&ProposalSnapshotError);
		if (bCapturedProposalDefaults)
		{
			Paper2DPlusFrameCueTypeEditorInternal::MergeBufferedVariableDefaults(
				CompiledRecoveryState->ProposedVariables,
				CompiledRecoveryState->ProposedDefaultValues);
			Paper2DPlusFrameCueTypeEditorInternal::CopyDefaultsIntoVariableModel(
				CompiledRecoveryState->ProposedVariables,
				CompiledRecoveryState->ProposedDefaultValues);
		}
		if (!bCapturedProposalDefaults
			|| !Paper2DPlusFrameCueTypeEditorInternal::WriteRecoveryArtifact(
				*CastChecked<UPaper2DPlusFrameCueBlueprint>(Blueprint),
				CompiledProposalPreflight,
				CompiledRecoveryState->ProposedDefaultValues,
				CompiledRecoveryState->ArtifactPath,
				&ProposalSnapshotError))
		{
			FText RecoveryError;
			RestoreDurableSchemaAfterFailure(
				*CastChecked<UPaper2DPlusFrameCueBlueprint>(Blueprint),
				&RecoveryError);
			ContractError = ProposalSnapshotError.IsEmpty()
				? LOCTEXT(
					"CueCompiledProposalSnapshotFailed",
					"The compiled Cue Type proposal could not be recorded safely, so the durable schema was restored.")
				: ProposalSnapshotError;
			if (!RecoveryError.IsEmpty())
			{
				ContractError = FText::Format(
					LOCTEXT(
						"CueProposalSnapshotAndRecoveryFailed",
						"{0}\n\nAutomatic durable-state recovery also reported: {1}"),
					ContractError,
					RecoveryError);
			}
			ReportDataOnlyViolation(LOCTEXT("CompileAction", "Compile"), ContractError);
			return false;
		}
		Paper2DPlusFrameCueTypeEditorInternal::StorePendingDefaultProposal(
			*CastChecked<UPaper2DPlusFrameCueBlueprint>(Blueprint),
			CompiledRecoveryState->ProposedDefaultValues,
			false);
	}
	if (bAuthorConfirmedSchemaCommit && Preflight.CurrentSchema.bValid)
	{
		if (UPaper2DPlusFrameCueBlueprint* Specialized =
			Cast<UPaper2DPlusFrameCueBlueprint>(Blueprint))
		{
			// The author reviewed this exact schema's consequences on screen; ordinary saves
			// honor the confirmation while the authored schema still matches it.
			Specialized->ConfirmedDestructiveSchemaFingerprint =
				Preflight.CurrentSchema.Fingerprint;
		}
		FNotificationInfo Notification(LOCTEXT(
			"CueSchemaChangeCompiledTitle",
			"Schema change compiled"));
		Notification.SubText = LOCTEXT(
			"CueSchemaChangeCompiledSubText",
			"The next successful save of this Cue Type commits it (any save, including Save All).");
		Notification.bFireAndForget = true;
		Notification.ExpireDuration = 8.0f;
		Notification.bUseLargeFont = false;
		Notification.bUseSuccessFailIcons = true;
		if (const TSharedPtr<SNotificationItem> Item =
			FSlateNotificationManager::Get().AddNotification(Notification))
		{
			Item->SetCompletionState(SNotificationItem::CS_Success);
		}
	}
	return true;
}

bool FPaper2DPlusFrameCueTypeEditor::HasPendingPlacement() const
{
	if (!PendingPlacement.IsValid())
	{
		return false;
	}
	const EPaper2DPlusFrameCuePlacementRequestState State =
		PendingPlacement->GetState();
	return State != EPaper2DPlusFrameCuePlacementRequestState::PlacedConsumed
		&& State != EPaper2DPlusFrameCuePlacementRequestState::RetainedTypeConsumed;
}

bool FPaper2DPlusFrameCueTypeEditor::CanCreateAndPlacePendingCue() const
{
	if (!PendingPlacement.IsValid() || !GetBlueprintObj())
	{
		return false;
	}
	const EPaper2DPlusFrameCuePlacementRequestState State =
		PendingPlacement->GetState();
	return (State == EPaper2DPlusFrameCuePlacementRequestState::EditingType
			|| State == EPaper2DPlusFrameCuePlacementRequestState::ReadyToPlace
			|| State == EPaper2DPlusFrameCuePlacementRequestState::RetryableAfterRollback)
		&& IsCompilingEnabled();
}

bool FPaper2DPlusFrameCueTypeEditor::CreateAndPlacePendingCue()
{
	if (!CanCreateAndPlacePendingCue())
	{
		return false;
	}
	if (!CompileDataOnlyBlueprint())
	{
		return false;
	}

	FPaper2DPlusFrameCueDurableSaveAttempt SaveAttempt;
	FText PersistenceError;
	if (!PrepareCurrentBlueprintForPersistence(SaveAttempt, &PersistenceError))
	{
		ReportDataOnlyViolation(
			LOCTEXT("CreateAndPlaceAction", "Create and Place"),
			PersistenceError);
		return false;
	}

	// Saving is an independent commit domain. The service marks the candidate, while Unreal's
	// normal synchronous asset save owns the package write. Readiness remains false while dirty.
	FBlueprintEditor::SaveAsset_Execute();
	UPaper2DPlusFrameCueBlueprint* Blueprint =
		SaveAttempt.CueType.Get();
	const bool bDurableSaveSucceeded = Blueprint
		&& FinalizeDurableSchemaSaveAttempt(
			*Blueprint, SaveAttempt, &PersistenceError);
	if (!bDurableSaveSucceeded)
	{
		if (Blueprint)
		{
			FText RecoveryError;
			if (!RestoreDurableSchemaAfterFailure(*Blueprint, &RecoveryError)
				&& !RecoveryError.IsEmpty())
			{
				PersistenceError = FText::Format(
					LOCTEXT(
						"CreateAndPlaceSaveRecoveryFailed",
						"{0}\n\nAutomatic durable-state recovery failed: {1}"),
					PersistenceError,
					RecoveryError);
			}
		}
		ReportDataOnlyViolation(
			LOCTEXT("CreateAndPlaceAction", "Create and Place"),
			PersistenceError.IsEmpty()
				? LOCTEXT(
					"CreateAndPlaceSaveIncomplete",
					"The Cue Type was not durably saved. Fix the save problem and try Create and Place again; no placement was created.")
				: PersistenceError);
		return false;
	}
	CompleteDurableSchemaCommit();
	UClass* GeneratedClass = Blueprint->GeneratedClass;

	if (PendingPlacement->GetState()
		== EPaper2DPlusFrameCuePlacementRequestState::EditingType)
	{
		if (!PendingPlacement->MarkReadyToPlace())
		{
			return false;
		}
	}
	const FPaper2DPlusFrameCuePlacementResult Placement =
		PendingPlacement->Commit(GeneratedClass);
	if (Placement.Status != EPaper2DPlusFrameCuePlacementStatus::Success)
	{
		const FText Failure = Placement.Status
			== EPaper2DPlusFrameCuePlacementStatus::TargetUnavailable
			? LOCTEXT(
				"CreateAndPlaceTargetStale",
				"The Cue Type was saved, but the original animation, layer, or frame no longer exists. No placement was created.")
			: LOCTEXT(
				"CreateAndPlacePlacementFailed",
				"The Cue Type was saved, but placement failed and was rolled back. You can retry without recreating the type.");
		ReportDataOnlyViolation(
			LOCTEXT("CreateAndPlaceAction", "Create and Place"), Failure);
		return false;
	}

	FNotificationInfo Notification(LOCTEXT(
		"CreateAndPlaceSucceeded",
		"Frame Cue Type saved and placed on the captured animation frame."));
	Notification.bFireAndForget = true;
	Notification.ExpireDuration = 4.0f;
	Notification.bUseSuccessFailIcons = true;
	if (const TSharedPtr<SNotificationItem> Item =
		FSlateNotificationManager::Get().AddNotification(Notification))
	{
		Item->SetCompletionState(SNotificationItem::CS_Success);
	}
	return true;
}

void FPaper2DPlusFrameCueTypeEditor::Compile()
{
	CompileDataOnlyBlueprint();
}

bool FPaper2DPlusFrameCueTypeEditor::CanSaveAsset() const
{
	const UBlueprint* Blueprint = GetBlueprintObj();
	return FBlueprintEditor::CanSaveAsset()
		&& Blueprint
		&& ValidateCurrentBlueprint()
		&& UPaper2DPlusFrameCueBlueprint::ValidateGeneratedClassEnvelope(*Blueprint);
}

bool FPaper2DPlusFrameCueTypeEditor::PrepareCurrentBlueprintForPersistence(
	FPaper2DPlusFrameCueDurableSaveAttempt& OutAttempt,
	FText* OutError)
{
	OutAttempt = FPaper2DPlusFrameCueDurableSaveAttempt();
	EnforceDataOnlyAuthoringInvariant(true);
	UBlueprint* Blueprint = GetBlueprintObj();
	if (!Blueprint
		|| !ValidateCurrentBlueprint(OutError)
		|| !UPaper2DPlusFrameCueBlueprint::ValidateGeneratedClassEnvelope(
			*Blueprint,
			OutError))
	{
		return false;
	}

	FText CompiledError;
	const bool bHasBufferedProposalDefaults = Blueprint->NewVariables.ContainsByPredicate(
		[](const FBPVariableDescription& Variable)
		{
			return !Variable.DefaultValue.IsEmpty();
		});
	UPaper2DPlusFrameCueBlueprint* Specialized =
		Cast<UPaper2DPlusFrameCueBlueprint>(Blueprint);
	bool bPendingDefaultsDifferFromCompiledClass = false;
	if (Specialized
		&& Specialized->bHasPendingDefaultProposal
		&& !Specialized->bPendingDefaultProposalNeedsCompile)
	{
		TMap<FName, FString> CompiledDefaults;
		FText DefaultsError;
		bPendingDefaultsDifferFromCompiledClass =
			!FPaper2DPlusFrameCueTypeAuthoring::CaptureDefaultValues(
				*Specialized,
				CompiledDefaults,
				&DefaultsError)
			|| !Paper2DPlusFrameCueTypeEditorInternal::DefaultMapsEqual(
				Specialized->PendingDefaultProposalValues,
				CompiledDefaults);
	}
	const FPaper2DPlusFrameCueSchemaPreflight PersistencePreflight =
		FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(*Blueprint);
	const bool bProposalRequiresProtectedCompile = bHasBufferedProposalDefaults
		|| bPendingDefaultsDifferFromCompiledClass
		|| (Specialized && Specialized->bHasPendingDefaultProposal
			? Specialized->bPendingDefaultProposalNeedsCompile
			: PersistencePreflight.Diff.Compatibility
				!= EPaper2DPlusFrameCueSchemaCompatibility::Identical);
	if (bProposalRequiresProtectedCompile
		|| !ValidateCompiledDataOnlyBlueprint(*Blueprint, &CompiledError))
	{
		// Save owns the same protected compile boundary as the explicit Compile action. This captures
		// unsaved CDO edits and can rebuild an exact transient proposal after rollback, including an
		// empty string that FBPVariableDescription::DefaultValue cannot represent as a pending marker.
		if (!CompileDataOnlyBlueprint())
		{
			Paper2DPlusFrameCueTypeEditorInternal::SetError(
				OutError,
				CompiledError.IsEmpty()
					? LOCTEXT(
						"PersistenceCompileFailed",
						"The Frame Cue Type could not complete its required pre-save compile.")
					: CompiledError);
			return false;
		}
		Blueprint = GetBlueprintObj();
	}

	UPaper2DPlusFrameCueBlueprint* CueBlueprint =
		Cast<UPaper2DPlusFrameCueBlueprint>(Blueprint);
	if (!CueBlueprint
		|| !ValidateCompiledDataOnlyBlueprint(*CueBlueprint, OutError))
	{
		return false;
	}
	return PrepareDurableSchemaSaveAttempt(*CueBlueprint, OutAttempt, OutError);
}

void FPaper2DPlusFrameCueTypeEditor::SaveAsset_Execute()
{
	FPaper2DPlusFrameCueDurableSaveAttempt SaveAttempt;
	FText ContractError;
	if (!PrepareCurrentBlueprintForPersistence(SaveAttempt, &ContractError))
	{
		ReportDataOnlyViolation(LOCTEXT("SaveAction", "Save"), ContractError);
		return;
	}
	FBlueprintEditor::SaveAsset_Execute();
	UPaper2DPlusFrameCueBlueprint* Blueprint = SaveAttempt.CueType.Get();
	const bool bDurableSaveSucceeded = Blueprint
		&& FinalizeDurableSchemaSaveAttempt(*Blueprint, SaveAttempt, &ContractError);
	if (!bDurableSaveSucceeded)
	{
		if (Blueprint)
		{
			FText RecoveryError;
			if (!RestoreDurableSchemaAfterFailure(*Blueprint, &RecoveryError)
				&& !RecoveryError.IsEmpty())
			{
				ContractError = FText::Format(
					LOCTEXT(
						"SaveRecoveryFailed",
						"{0}\n\nAutomatic durable-state recovery failed: {1}"),
					ContractError,
					RecoveryError);
			}
		}
		ReportDataOnlyViolation(
			LOCTEXT("SaveAction", "Save"),
			ContractError.IsEmpty()
				? LOCTEXT("SaveDidNotBecomeDurable", "The Cue Type was not durably saved.")
				: ContractError);
		return;
	}
	CompleteDurableSchemaCommit();
}

bool FPaper2DPlusFrameCueTypeEditor::CanSaveAssetAs() const
{
	const UBlueprint* Blueprint = GetBlueprintObj();
	return FBlueprintEditor::CanSaveAssetAs()
		&& Blueprint
		&& ValidateCurrentBlueprint()
		&& UPaper2DPlusFrameCueBlueprint::ValidateGeneratedClassEnvelope(*Blueprint);
}

void FPaper2DPlusFrameCueTypeEditor::SaveAssetAs_Execute()
{
	FPaper2DPlusFrameCueDurableSaveAttempt SaveAttempt;
	FText ContractError;
	if (!PrepareCurrentBlueprintForPersistence(SaveAttempt, &ContractError))
	{
		ReportDataOnlyViolation(LOCTEXT("SaveAsAction", "Save As"), ContractError);
		return;
	}
	TSharedPtr<IToolkitHost> CurrentToolkitHost = ToolkitHost.Pin();
	TArray<UObject*> ObjectsToSave;
	GetSaveableObjects(ObjectsToSave);
	TArray<UObject*> SavedObjects;
	if (CurrentToolkitHost.IsValid() && ObjectsToSave.Num() > 0)
	{
		FEditorFileUtils::SaveAssetsAs(ObjectsToSave, SavedObjects);
	}

	bool bSaveAsSucceeded = false;
	bool bSavedOriginal = false;
	for (UObject* SavedObject : SavedObjects)
	{
		UPaper2DPlusFrameCueBlueprint* SavedCueType =
			Cast<UPaper2DPlusFrameCueBlueprint>(SavedObject);
		if (!SavedCueType
			|| SavedCueType->DurableSchemaVersion != SaveAttempt.CandidateVersion
			|| SavedCueType->DurableSchemaFingerprint != SaveAttempt.CandidateFingerprint
			|| SavedCueType->DurableSchemaSnapshot != SaveAttempt.CandidateSchemaSnapshot)
		{
			continue;
		}
		if (!IsDurablyReadyForPlacement(*SavedCueType))
		{
			// A duplicate can exist even when its readiness contract did not survive Save As. Do
			// not leave candidate metadata looking durable in memory; keep the copy dirty so the
			// author can repair and retry it explicitly.
			SavedCueType->DurableSchemaVersion = SaveAttempt.PreviousVersion;
			SavedCueType->DurableSchemaFingerprint = SaveAttempt.PreviousFingerprint;
			SavedCueType->DurableSchemaSnapshot = SaveAttempt.PreviousSchemaSnapshot;
			SavedCueType->DurableVariables = SaveAttempt.PreviousVariables;
			SavedCueType->DurableDefaultValues = SaveAttempt.PreviousDefaultValues;
			SavedCueType->MarkPackageDirty();
			continue;
		}
		bSaveAsSucceeded = true;
		bSavedOriginal |= SavedCueType == SaveAttempt.CueType.Get();
		Paper2DPlusFrameCueTypeEditorInternal::ClearPendingDefaultProposal(*SavedCueType);
	}
	UPaper2DPlusFrameCueBlueprint* Original = SaveAttempt.CueType.Get();
	if (Original && !bSavedOriginal)
	{
		// Save As writes a duplicate. The source asset was not the successful commit and must not
		// retain candidate metadata that only belongs to the saved copy.
		RestoreDurableSchemaSaveAttempt(*Original, SaveAttempt);
		FText RecoveryError;
		if (!RestoreDurableSchemaAfterFailure(*Original, &RecoveryError)
			&& !RecoveryError.IsEmpty())
		{
			ContractError = RecoveryError;
			bSaveAsSucceeded = false;
		}
	}
	else if (bSavedOriginal)
	{
		CompleteDurableSchemaCommit();
	}

	// Keep the stock Save As editor handoff while retaining SavedObjects so durability can be
	// validated on every supported engine. UE 5.0-5.2 expose no OnAssetsSavedAs hook.
	if (SavedObjects.Num() > 0 && CurrentToolkitHost.IsValid() && GEditor)
	{
		UAssetEditorSubsystem* AssetEditors =
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (AssetEditors)
		{
			const TArray<UObject*> EditingObjectsCopy = GetEditingObjects();
			TArray<UObject*> ObjectsToReopen;
			for (UObject* EditingObject : EditingObjectsCopy)
			{
				if (EditingObject && EditingObject->IsAsset()
					&& !ObjectsToSave.Contains(EditingObject))
				{
					ObjectsToReopen.AddUnique(EditingObject);
				}
			}
			for (UObject* SavedObject : SavedObjects)
			{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
				if (ShouldReopenEditorForSavedAsset(SavedObject))
#endif
				{
					ObjectsToReopen.AddUnique(SavedObject);
				}
			}
			for (UObject* EditingObject : EditingObjectsCopy)
			{
				if (EditingObject)
				{
					AssetEditors->CloseAllEditorsForAsset(EditingObject);
					AssetEditors->NotifyAssetClosed(EditingObject, this);
				}
			}
			AssetEditors->OpenEditorForAssets_Advanced(
				ObjectsToReopen, ToolkitMode, CurrentToolkitHost);
		}
	}

	if (!bSaveAsSucceeded)
	{
		ReportDataOnlyViolation(
			LOCTEXT("SaveAsAction", "Save As"),
			ContractError.IsEmpty()
				? LOCTEXT(
					"SaveAsDidNotBecomeDurable",
					"Save As was cancelled or did not create a durably ready Cue Type. The source asset's durable class and loaded placements were restored, and the proposed edits remain retryable.")
				: ContractError);
	}
}

void FPaper2DPlusFrameCueTypeEditor::CreateDefaultTabContents(
	const TArray<UBlueprint*>& InBlueprints)
{
	FBlueprintEditor::CreateDefaultTabContents(InBlueprints);

	// Healthy specialized Cue Types use stock My Blueprint for variables. Quarantined assets keep
	// the widget alive for Kismet internals but never attach or expose it.
	UpdateMyBlueprintExposure();
}

void FPaper2DPlusFrameCueTypeEditor::UpdateMyBlueprintExposure()
{
	const UBlueprint* Blueprint = GetBlueprintObj();
	bUsingStockMyBlueprint = Blueprint && ShouldUseStockMyBlueprint(*Blueprint);
	if (const TSharedPtr<SMyBlueprint> MyBlueprint = GetMyBlueprintWidget())
	{
		MyBlueprint->SetEnabled(bUsingStockMyBlueprint);
		MyBlueprint->SetVisibility(
			bUsingStockMyBlueprint ? EVisibility::Visible : EVisibility::Collapsed);
	}
}

void FPaper2DPlusFrameCueTypeEditor::CaptureAuthoringBaseline()
{
	AcceptedBehaviorGraphs.Reset();
	AcceptedTimelines.Reset();
	AcceptedPayloadFieldStates.Reset();

	if (const UBlueprint* Blueprint = GetBlueprintObj())
	{
		TArray<UEdGraph*> ExistingGraphs;
		Blueprint->GetAllGraphs(ExistingGraphs);
		for (UEdGraph* Graph : ExistingGraphs)
		{
			if (Graph)
			{
				AcceptedBehaviorGraphs.Add(Graph);
			}
		}
		for (UTimelineTemplate* Timeline : Blueprint->Timelines)
		{
			if (Timeline)
			{
				AcceptedTimelines.Add(Timeline);
			}
		}

		const uint64 ForbiddenFieldFlags =
			UPaper2DPlusFrameCueBlueprint::GetForbiddenPayloadPropertyFlags()
			| CPF_Net
			| CPF_RepNotify;
		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			if (!Variable.VarGuid.IsValid())
			{
				continue;
			}
			FAcceptedPayloadFieldState& State =
				AcceptedPayloadFieldStates.Add(Variable.VarGuid);
			State.PropertyFlags = Variable.PropertyFlags & ForbiddenFieldFlags;
			State.RepNotifyFunc = Variable.RepNotifyFunc;
			State.ReplicationCondition = Variable.ReplicationCondition;
		}
	}

	bAuthoringBaselineCaptured = true;
}

void FPaper2DPlusFrameCueTypeEditor::AcceptPermittedBehaviorGraph()
{
	UBlueprint* Blueprint = GetBlueprintObj();
	if (UEdGraph* BehaviorGraph =
		Blueprint ? FindCueBehaviorGraph(*Blueprint) : nullptr)
	{
		AcceptedBehaviorGraphs.Add(BehaviorGraph);
	}
}

void FPaper2DPlusFrameCueTypeEditor::ScheduleDataOnlyInvariantEnforcement()
{
	if (!bAuthoringBaselineCaptured
		|| bDataOnlyEnforcementPending
		|| bEnforcingDataOnlyInvariant)
	{
		return;
	}

	bDataOnlyEnforcementPending = true;
	const TWeakPtr<FPaper2DPlusFrameCueTypeEditor> WeakEditor = SharedThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakEditor]()
	{
		if (const TSharedPtr<FPaper2DPlusFrameCueTypeEditor> Editor = WeakEditor.Pin())
		{
			Editor->bDataOnlyEnforcementPending = false;
			Editor->EnforceDataOnlyAuthoringInvariant(true);
		}
	});
}

bool FPaper2DPlusFrameCueTypeEditor::EnforceDataOnlyAuthoringInvariant(
	bool bNotifyAuthor)
{
	bDataOnlyEnforcementPending = false;
	UBlueprint* Blueprint = GetBlueprintObj();
	if (!bAuthoringBaselineCaptured || !Blueprint || bEnforcingDataOnlyInvariant)
	{
		return false;
	}

	TGuardValue<bool> EnforcingGuard(bEnforcingDataOnlyInvariant, true);
	bool bRevertedForbiddenChange = false;

	TArray<UEdGraph*> CurrentGraphs;
	Blueprint->GetAllGraphs(CurrentGraphs);
	TArray<UEdGraph*> UnauthorizedGraphs;
	for (UEdGraph* Graph : CurrentGraphs)
	{
		if (Graph
			&& !AcceptedBehaviorGraphs.Contains(TWeakObjectPtr<UEdGraph>(Graph)))
		{
			UnauthorizedGraphs.AddUnique(Graph);
		}
	}

	for (UEdGraph* Graph : UnauthorizedGraphs)
	{
		const FSoftObjectPath RemovedGraphPath(Graph);
		FBlueprintEditorUtils::RemoveGraph(
			Blueprint,
			Graph,
			EGraphRemoveFlags::MarkTransient);
		Blueprint->LastEditedDocuments.RemoveAll(
			[&RemovedGraphPath](const FEditedDocumentInfo& Document)
			{
				return Document.EditedObjectPath == RemovedGraphPath;
			});
		bRevertedForbiddenChange = true;
	}

	TArray<UTimelineTemplate*> UnauthorizedTimelines;
	for (UTimelineTemplate* Timeline : Blueprint->Timelines)
	{
		if (Timeline
			&& !AcceptedTimelines.Contains(TWeakObjectPtr<UTimelineTemplate>(Timeline)))
		{
			UnauthorizedTimelines.Add(Timeline);
		}
	}
	for (UTimelineTemplate* Timeline : UnauthorizedTimelines)
	{
		if (UK2Node_Timeline* TimelineNode =
			FBlueprintEditorUtils::FindNodeForTimeline(Blueprint, Timeline))
		{
			// UK2Node_Timeline::DestroyNode owns removal and renaming of its template.
			FBlueprintEditorUtils::RemoveNode(
				Blueprint,
				TimelineNode,
				/*bDontRecompile=*/ true);
		}
		else
		{
			FBlueprintEditorUtils::RemoveTimeline(
				Blueprint,
				Timeline,
				/*bDontRecompile=*/ true);
			Timeline->Rename(nullptr, GetTransientPackage(), REN_None);
		}
		bRevertedForbiddenChange = true;
	}

	const uint64 ForbiddenFieldFlags =
		UPaper2DPlusFrameCueBlueprint::GetForbiddenPayloadPropertyFlags()
		| CPF_Net
		| CPF_RepNotify;
	for (FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		const bool bCurrentStateIsSafe =
			(Variable.PropertyFlags & ForbiddenFieldFlags) == 0
			&& Variable.RepNotifyFunc.IsNone()
			&& Variable.ReplicationCondition == COND_None;

		FAcceptedPayloadFieldState* AcceptedState = Variable.VarGuid.IsValid()
			? AcceptedPayloadFieldStates.Find(Variable.VarGuid)
			: nullptr;
		if (!AcceptedState)
		{
			FAcceptedPayloadFieldState NewState;
			if (bCurrentStateIsSafe)
			{
				NewState.PropertyFlags = Variable.PropertyFlags & ForbiddenFieldFlags;
				NewState.RepNotifyFunc = Variable.RepNotifyFunc;
				NewState.ReplicationCondition = Variable.ReplicationCondition;
			}
			if (Variable.VarGuid.IsValid())
			{
				AcceptedState = &AcceptedPayloadFieldStates.Add(
					Variable.VarGuid,
					NewState);
			}
			else
			{
				Variable.PropertyFlags &= ~ForbiddenFieldFlags;
				Variable.RepNotifyFunc = NAME_None;
				Variable.ReplicationCondition = COND_None;
				bRevertedForbiddenChange |= !bCurrentStateIsSafe;
				continue;
			}
		}

		const bool bAcceptedStateIsSafe =
			(AcceptedState->PropertyFlags & ForbiddenFieldFlags) == 0
			&& AcceptedState->RepNotifyFunc.IsNone()
			&& AcceptedState->ReplicationCondition == COND_None;
		if (bCurrentStateIsSafe)
		{
			// Clearing unsupported replication from a legacy field is a valid recovery step.
			if (!bAcceptedStateIsSafe)
			{
				AcceptedState->PropertyFlags = 0;
				AcceptedState->RepNotifyFunc = NAME_None;
				AcceptedState->ReplicationCondition = COND_None;
			}
			continue;
		}

		const uint64 CurrentForbiddenFlags =
			Variable.PropertyFlags & ForbiddenFieldFlags;
		if (CurrentForbiddenFlags != AcceptedState->PropertyFlags
			|| Variable.RepNotifyFunc != AcceptedState->RepNotifyFunc
			|| Variable.ReplicationCondition != AcceptedState->ReplicationCondition)
		{
			Variable.PropertyFlags =
				(Variable.PropertyFlags & ~ForbiddenFieldFlags)
				| AcceptedState->PropertyFlags;
			Variable.RepNotifyFunc = AcceptedState->RepNotifyFunc;
			Variable.ReplicationCondition = AcceptedState->ReplicationCondition;
			bRevertedForbiddenChange = true;
		}
	}

	if (bRevertedForbiddenChange)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		RefreshMyBlueprint();
		RefreshInspector();

		if (bNotifyAuthor)
		{
			ReportDataOnlyViolation(
				LOCTEXT("DataOnlyAuthoringAction", "Authoring change"),
				LOCTEXT(
					"DataOnlyAuthoringReverted",
					"Frame Cue Types cannot add behavior graphs, timelines, replication, RepNotify, or "
					"payload flags that break save, duplication, copy, cook, or undo. "
					"The attempted change was reverted; payload data was preserved."));
		}
	}

	return bRevertedForbiddenChange;
}

bool FPaper2DPlusFrameCueTypeEditor::EnforceDataOnlyAuthoringInvariantForTests()
{
	return EnforceDataOnlyAuthoringInvariant(false);
}

void FPaper2DPlusFrameCueTypeEditor::OnBlueprintChangedImpl(
	UBlueprint* InBlueprint,
	bool bIsJustBeingCompiled)
{
	FBlueprintEditor::OnBlueprintChangedImpl(InBlueprint, bIsJustBeingCompiled);
	if (InBlueprint == GetBlueprintObj())
	{
		UpdateMyBlueprintExposure();
		if (!bIsJustBeingCompiled && !bEnforcingDataOnlyInvariant)
		{
			ScheduleDataOnlyInvariantEnforcement();
		}
	}
}

void FPaper2DPlusFrameCueTypeEditor::CreateDefaultCommands()
{
	FBlueprintEditor::CreateDefaultCommands();

	const TSharedRef<FUICommandList> Commands = GetToolkitCommands();
	const auto BlockCommands = [&Commands](
		FName ContextName,
		const TArray<FName>& CommandNames)
	{
		for (const FName CommandName : CommandNames)
		{
			const TSharedPtr<FUICommandInfo> Command =
				FInputBindingManager::Get().FindCommandInContext(ContextName, CommandName);
			if (!Command.IsValid())
			{
				continue;
			}

			// FBlueprintEditor maps these before the Cue-specific application mode exists. A hidden
			// tab is insufficient: menu extenders or direct command execution could still reparent,
			// merge, switch into a behavior mode, or create behavior graphs. Shadow each command
			// locally with a fail-closed action.
			Commands->UnmapAction(Command);
			Commands->MapAction(
				Command,
				FUIAction(
					FExecuteAction(),
					FCanExecuteAction::CreateLambda([]() { return false; }),
					FIsActionChecked(),
					FIsActionButtonVisible::CreateLambda([]() { return false; })));
		}
	};

	BlockCommands(TEXT("BlueprintEditor"), GetBlockedBlueprintCommandNamesForTests());
	BlockCommands(TEXT("FullBlueprintEditor"), GetBlockedFullBlueprintCommandNamesForTests());
	BlockCommands(TEXT("MyBlueprint"), GetBlockedMyBlueprintCommandNamesForTests());
	BlockCommands(TEXT("GenericCommands"), GetBlockedGenericCommandNamesForTests());

	const auto MapValidatedCompileCommand = [this, &Commands](
		FName ContextName,
		FName CommandName)
	{
		const TSharedPtr<FUICommandInfo> Command =
			FInputBindingManager::Get().FindCommandInContext(ContextName, CommandName);
		if (!Command.IsValid())
		{
			return;
		}
		Commands->UnmapAction(Command);
		Commands->MapAction(
			Command,
			FExecuteAction::CreateSP(
				this,
				&FPaper2DPlusFrameCueTypeEditor::Compile),
			FCanExecuteAction::CreateSP(
				this,
				&FPaper2DPlusFrameCueTypeEditor::IsCompilingEnabled));
	};

	// Unreal exposes independent toolbar and menu/F7 compile commands. Route both through the same
	// restricted behavior/payload gate.
	MapValidatedCompileCommand(TEXT("FullBlueprintEditor"), TEXT("Compile"));
	MapValidatedCompileCommand(TEXT("BlueprintEditor"), TEXT("CompileBlueprint"));
}

void FPaper2DPlusFrameCueTypeEditor::RegisterApplicationModes(
	const TArray<UBlueprint*>& InBlueprints,
	bool bShouldOpenInDefaultsMode,
	bool bNewlyCreated)
{
	const TSharedPtr<FPaper2DPlusFrameCueTypeEditor> ThisEditor = SharedThis(this);
	AddApplicationMode(
		CueTypeModeName,
		MakeShared<Paper2DPlusFrameCueTypeEditorInternal::FApplicationMode>(ThisEditor));
	SetCurrentMode(CueTypeModeName);
}

bool FPaper2DPlusFrameCueTypeEditor::NewDocument_IsVisibleForType(
	ECreatedDocumentType GraphType) const
{
	// Exposing CGT_NewFunctionGraph also renders SMyBlueprint's private ImplementFunction path.
	// That path can create a forbidden function graph when no event graph exists, and no public
	// cross-version hook can replace it. The plugin-owned Override control is the sole safe route.
	return GraphType == CGT_NewVariable;
}

bool FPaper2DPlusFrameCueTypeEditor::IsSectionVisible(
	NodeSectionID::Type InSectionID) const
{
	return InSectionID == NodeSectionID::VARIABLE;
}

#undef LOCTEXT_NAMESPACE
