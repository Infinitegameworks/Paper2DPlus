// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AssetRegistry/AssetData.h"
#include "CharacterLayerAssetEditorToolkit.h"
#include "EdGraphSchema_K2.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "FrameCues/Paper2DPlusFrameCueTypeEditor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Paper2DPlusMoveTransition.h"
#include "ProfileValidationAdapter.h"
#include "ProfileValidationPanel.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusSettings.h"
#include "PaperFlipbook.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 1, 0)
#include "Misc/ITransaction.h"
#else
#include "Misc/TransactionObjectEvent.h"
#endif
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace
{
	FPaper2DPlusValidationIssue MakeTestIssue(
		EPaper2DPlusValidationSeverity Severity,
		const TCHAR* Code,
		const TCHAR* Item,
		const TCHAR* Field,
		const TCHAR* Message,
		bool bActionable = false)
	{
		FPaper2DPlusValidationIssue Issue;
		Issue.Severity = Severity;
		Issue.Code = Code;
		Issue.AssetPath = FSoftObjectPath(TEXT("/Game/Validation/TestProfile.TestProfile"));
		Issue.AssetType = TEXT("TestProfile");
		Issue.Scope = TEXT("TestScope");
		Issue.ItemIdentity = Item;
		Issue.Field = Field;
		Issue.Message = FText::FromString(Message);
		Issue.Remediation = FText::FromString(TEXT("Correct the test fixture."));
		if (bActionable)
		{
			FPaper2DPlusValidationToolTarget Target;
			Target.ToolId = TEXT("TestTool");
			Target.TabId = TEXT("TestTab");
			Target.ItemIdentity = Item;
			Target.Field = Field;
			Issue.ToolTarget = Target;
		}
		return Issue;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusValidationAdapterProjectionTest,
	"Paper2DPlus.Validation.SharedProjection.DomainAdapters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusValidationAdapterProjectionTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Character = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaper2DPlusCharacterLayerAsset* Layer = NewObject<UPaper2DPlusCharacterLayerAsset>();
	UPaper2DPlusEffectProfileAsset* Effect = NewObject<UPaper2DPlusEffectProfileAsset>();
	UPaper2DPlusCombatProfileAsset* Combat = NewObject<UPaper2DPlusCombatProfileAsset>();
	UPaperFlipbook* DustFlipbook = NewObject<UPaperFlipbook>(Effect, TEXT("Dust"));
	FPaper2DPlusEffectProfileEntry& DustEntry = Effect->Effects.AddDefaulted_GetRef();
	DustEntry.EffectFlipbook = DustFlipbook;
	DustEntry.EffectName = TEXT("Dust"); // compatibility identity emitted by the native fixture
	const FString DustPath = FSoftObjectPath(DustFlipbook).ToString();

	FCharacterProfileValidationIssue CharacterSource;
	CharacterSource.Severity = ECharacterProfileValidationSeverity::Error;
	CharacterSource.Context = TEXT("Run");
	CharacterSource.Message = TEXT("Character original message");

	FCharacterLayerValidationIssue LayerSource;
	LayerSource.Severity = ECharacterLayerValidationSeverity::Warning;
	LayerSource.LayerName = TEXT("Hat");
	LayerSource.Message = TEXT("Layer original message");
	FCharacterLayerValidationIssue SecondLayerSource = LayerSource;
	SecondLayerSource.Message = TEXT("A second distinct Hat violation");

	FPaper2DPlusEffectProfileValidationIssue EffectSource;
	EffectSource.Severity = EPaper2DPlusEffectProfileValidationSeverity::Info;
	EffectSource.EffectName = TEXT("Dust");
	EffectSource.Field = TEXT("EffectFlipbook");
	EffectSource.Message = FText::FromString(TEXT("Effect original message"));

	FPaper2DPlusCombatValidationIssue CombatSource;
	CombatSource.Severity = EPaper2DPlusCombatValidationSeverity::Error;
	CombatSource.MoveName = TEXT("HeavyAttack");
	CombatSource.Field = TEXT("PreferredRange");
	CombatSource.Message = FText::FromString(TEXT("Combat original message"));

	TArray<FPaper2DPlusValidationIssue> Projected;
	Paper2DPlusProfileValidationAdapter::ProjectCharacterIssues(*Character, {CharacterSource}, Projected);
	Paper2DPlusProfileValidationAdapter::ProjectLayerIssues(*Layer, {LayerSource, SecondLayerSource}, Projected);
	Paper2DPlusProfileValidationAdapter::ProjectEffectIssues(*Effect, {EffectSource}, Projected);
	Paper2DPlusProfileValidationAdapter::ProjectCombatIssues(*Combat, {CombatSource}, Projected);
	FPaper2DPlusValidationService::NormalizeAndSort(Projected);

	TestEqual(TEXT("same-item native violations remain distinct alongside the other issue kinds"), Projected.Num(), 5);
	TSet<FString> HatIssueKeys;
	for (const FPaper2DPlusValidationIssue& Issue : Projected)
	{
		if (Issue.Scope == TEXT("Layer") && Issue.ItemIdentity == TEXT("Hat"))
		{
			HatIssueKeys.Add(Issue.StableKey);
		}
	}
	TestEqual(TEXT("two same-layer native messages receive two machine-stable discriminators"),
		HatIssueKeys.Num(), 2);
	TestTrue(TEXT("Character severity/context/message survive projection"), Projected.ContainsByPredicate(
		[](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Scope == TEXT("CharacterProfile")
				&& Issue.Severity == EPaper2DPlusValidationSeverity::Error
				&& Issue.ItemIdentity == TEXT("Run")
				&& Issue.Message.ToString() == TEXT("Character original message");
		}));
	TestTrue(TEXT("Layer severity/context/message survive projection"), Projected.ContainsByPredicate(
		[](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Scope == TEXT("Layer")
				&& Issue.Severity == EPaper2DPlusValidationSeverity::Warning
				&& Issue.ItemIdentity == TEXT("Hat")
				&& Issue.Message.ToString() == TEXT("Layer original message");
		}));
	TestTrue(TEXT("Effect context and original message survive projection"), Projected.ContainsByPredicate(
		[&DustPath](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Scope == TEXT("Effect")
				&& Issue.Field == TEXT("EffectFlipbook")
				&& Issue.ItemIdentity == DustPath
				&& Issue.Message.ToString() == TEXT("Effect original message");
		}));
	TestTrue(TEXT("Combat field and original message survive projection"), Projected.ContainsByPredicate(
		[](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Scope == TEXT("Combat")
				&& Issue.Field == TEXT("PreferredRange")
				&& Issue.ItemIdentity == TEXT("HeavyAttack")
				&& Issue.Message.ToString() == TEXT("Combat original message");
		}));
	TestTrue(TEXT("Every projected issue has a stable code, soft path, and key"), Projected.ContainsByPredicate(
		[](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Code.IsNone() || !Issue.AssetPath.IsValid() || Issue.StableKey.IsEmpty();
		}) == false);
	TestTrue(TEXT("Effect projection supplies a path-stable Details/item/field target"), Projected.ContainsByPredicate(
		[&DustPath](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Scope == TEXT("Effect")
				&& Issue.CanActivate()
				&& Issue.ToolTarget->TabId == TEXT("EffectProfileEditor_Details")
				&& Issue.ToolTarget->ItemIdentity == DustPath
				&& Issue.Field == TEXT("EffectFlipbook")
				&& Issue.ToolTarget->Field == TEXT("EffectFlipbook");
		}));
	TestTrue(TEXT("Combat projection supplies a truthful Setup/item target without over-promising field focus"), Projected.ContainsByPredicate(
		[](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Scope == TEXT("Combat")
				&& Issue.CanActivate()
				&& Issue.ToolTarget->TabId == TEXT("CombatProfileEditor_Setup")
				&& Issue.ToolTarget->ItemIdentity == TEXT("HeavyAttack")
				&& Issue.Field == TEXT("PreferredRange")
				&& Issue.ToolTarget->Field.IsNone();
		}));

	FPaper2DPlusValidationService& Service = FPaper2DPlusValidationService::Get();
	const ECharacterLayerBakeAttachmentState PreviousAttachmentState = Layer->BakeAttachmentState;
	Layer->BakeAttachmentState = ECharacterLayerBakeAttachmentState::Detached;
	TArray<FPaper2DPlusValidationIssue> LayerNavigationIssues;
	TestTrue(TEXT("Built-in Layer adapter projects detached bake status"),
		Service.ValidateObject(Layer, LayerNavigationIssues));
	const FPaper2DPlusValidationIssue* DetachedBakeIssue = LayerNavigationIssues.FindByPredicate(
		[](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Code == TEXT("Paper2DPlus.Layer.Bake.Detached");
		});
	TestNotNull(TEXT("Detached bake issue is projected"), DetachedBakeIssue);
	if (DetachedBakeIssue)
	{
		TestTrue(TEXT("Detached bake issue is actionable"), DetachedBakeIssue->CanActivate());
		if (DetachedBakeIssue->ToolTarget.IsSet())
		{
			TestEqual(
				TEXT("Layer bake activation targets the compact Completion stack"),
				DetachedBakeIssue->ToolTarget->TabId,
				FCharacterLayerAssetEditorToolkit::CompletionTabId);
		}
	}
	const FPaper2DPlusValidationIssue* AppearanceIssue = LayerNavigationIssues.FindByPredicate(
		[](const FPaper2DPlusValidationIssue& Issue)
		{
			return Issue.Code == TEXT("Paper2DPlus.Layer.Appearance.Invalid");
		});
	TestNotNull(TEXT("Invalid generic appearance issue is projected"), AppearanceIssue);
	if (AppearanceIssue && AppearanceIssue->ToolTarget.IsSet())
	{
		TestEqual(
			TEXT("Generic appearance activation targets the Appearance tool tab"),
			AppearanceIssue->ToolTarget->TabId,
			FCharacterLayerAssetEditorToolkit::AppearanceTabId);
	}
	Layer->BakeAttachmentState = PreviousAttachmentState;

	for (UObject* NativeAsset : {static_cast<UObject*>(Character), static_cast<UObject*>(Layer), static_cast<UObject*>(Effect), static_cast<UObject*>(Combat)})
	{
		UPackage* Package = NativeAsset->GetOutermost();
		const bool bWasDirty = Package->IsDirty();
		TArray<FPaper2DPlusValidationIssue> NativeIssues;
		TestTrue(FString::Printf(TEXT("Built-in adapter exists for %s"), *NativeAsset->GetClass()->GetName()), Service.ValidateObject(NativeAsset, NativeIssues));
		TestEqual(FString::Printf(TEXT("Built-in validation does not dirty %s"), *NativeAsset->GetClass()->GetName()), Package->IsDirty(), bWasDirty);
	}

	// A future Catalog can register by class path and normalized value contract without a Catalog header.
	const FName FakeAdapterId(TEXT("Paper2DPlus.Test.FutureCatalog"));
	int32 AdapterCalls = 0;
	const bool bRegistered = Service.RegisterAdapter(
		FakeAdapterId,
		FName(*UPaperFlipbook::StaticClass()->GetPathName()),
		FPaper2DPlusValidationAdapterDelegate::CreateLambda(
			[&AdapterCalls](const UObject&, TArray<FPaper2DPlusValidationIssue>& OutIssues)
			{
				++AdapterCalls;
				FPaper2DPlusValidationIssue Issue = MakeTestIssue(
					EPaper2DPlusValidationSeverity::Warning,
					TEXT("Paper2DPlus.Catalog.Relationship"),
					TEXT("Hero"),
					TEXT("LayerProfile"),
					TEXT("Future Catalog issue"));
				Issue.AssetPath = FSoftObjectPath(); // Service supplies the actual owning asset path.
				Issue.AssetType = NAME_None;
				Issue.Scope = TEXT("Catalog");
				OutIssues.Add(MoveTemp(Issue));
			}));
	TestTrue(TEXT("Late Catalog-like adapter registers without a Catalog dependency"), bRegistered);

	UPackage* FuturePackage = CreatePackage(TEXT("/Engine/Transient/Paper2DPlusValidationFutureCatalog"));
	UPaperFlipbook* FutureCatalog = NewObject<UPaperFlipbook>(FuturePackage, TEXT("FutureCatalog"));
	const bool bDirtyBefore = FuturePackage->IsDirty();
	TArray<FPaper2DPlusValidationIssue> FutureIssues;
	TestTrue(TEXT("Future adapter validates through the shared service"), Service.ValidateObject(FutureCatalog, FutureIssues));
	TestEqual(TEXT("Future adapter runs exactly once"), AdapterCalls, 1);
	TestEqual(TEXT("Future adapter produces one normalized issue"), FutureIssues.Num(), 1);
	if (FutureIssues.Num() == 1)
	{
		TestEqual(TEXT("Future severity survives"), FutureIssues[0].Severity, EPaper2DPlusValidationSeverity::Warning);
		TestEqual(TEXT("Future scope survives"), FutureIssues[0].Scope, FName(TEXT("Catalog")));
		TestEqual(TEXT("Future item survives"), FutureIssues[0].ItemIdentity, FString(TEXT("Hero")));
		TestEqual(TEXT("Future field survives"), FutureIssues[0].Field, FName(TEXT("LayerProfile")));
		TestEqual(TEXT("Future message survives"), FutureIssues[0].Message.ToString(), FString(TEXT("Future Catalog issue")));
		TestEqual(TEXT("Owning path is supplied"), FutureIssues[0].AssetPath.ToString(), FSoftObjectPath(FutureCatalog->GetPathName()).ToString());
		TestFalse(TEXT("Stable key is populated"), FutureIssues[0].StableKey.IsEmpty());
	}
	TestEqual(TEXT("Validation projection does not dirty the package"), FuturePackage->IsDirty(), bDirtyBefore);
	TestTrue(TEXT("Late adapter can be removed cleanly"), Service.UnregisterAdapter(FakeAdapterId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusValidationPanelModelTest,
	"Paper2DPlus.Validation.SharedProjection.FilterSelectionAndSummary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusValidationPanelModelTest::RunTest(const FString& Parameters)
{
	TArray<FPaper2DPlusValidationIssue> Issues;
	Issues.Add(MakeTestIssue(EPaper2DPlusValidationSeverity::Info, TEXT("P2DP.Info"), TEXT("Idle"), TEXT("Tags"), TEXT("Informational tag note")));
	Issues.Add(MakeTestIssue(EPaper2DPlusValidationSeverity::Error, TEXT("P2DP.Error"), TEXT("Run"), TEXT("Flipbook"), TEXT("Run has no flipbook"), true));
	Issues.Add(MakeTestIssue(EPaper2DPlusValidationSeverity::Warning, TEXT("P2DP.Warning"), TEXT("Jump"), TEXT("Frames"), TEXT("Jump frame count differs")));

	FPaper2DPlusValidationPanelModel Model;
	Model.SetIssues(Issues);
	TestEqual(TEXT("All severities start visible"), Model.GetFilteredIssues().Num(), 3);
	TestEqual(TEXT("Errors sort first deterministically"), Model.GetFilteredIssues()[0].Severity, EPaper2DPlusValidationSeverity::Error);

	const FString SelectedKey = Model.GetFilteredIssues()[0].StableKey;
	Model.SelectIssue(SelectedKey);
	TestEqual(TEXT("Stable issue can be selected"), Model.GetSelectedStableKey(), SelectedKey);

	Model.SetSeverityVisible(EPaper2DPlusValidationSeverity::Info, false);
	TestEqual(TEXT("Severity filter removes only Info"), Model.GetFilteredIssues().Num(), 2);
	Model.SetSearchText(TEXT("jump frames"));
	TestEqual(TEXT("Text filter spans item and field"), Model.GetFilteredIssues().Num(), 1);
	TestEqual(TEXT("Hidden selection is cleared rather than retained stale"), Model.GetSelectedStableKey(), FString());
	TestEqual(TEXT("Filter identifies warning deterministically"), Model.GetFilteredIssues()[0].Severity, EPaper2DPlusValidationSeverity::Warning);

	const FPaper2DPlusValidationSummary Summary = Model.GetSummary();
	TestEqual(TEXT("Summary keeps total error count"), Summary.NumErrors, 1);
	TestEqual(TEXT("Summary keeps total warning count"), Summary.NumWarnings, 1);
	TestEqual(TEXT("Summary keeps total info count"), Summary.NumInfo, 1);
	TestTrue(TEXT("Error status exposes text semantics"), Summary.GetAccessibleText().ToString().Contains(TEXT("errors")));
	TestFalse(TEXT("Error status exposes an icon semantic"), Summary.GetStatusIconName().IsNone());

	Model.SetSearchText(FString());
	Model.SetSeverityVisible(EPaper2DPlusValidationSeverity::Info, true);
	Model.SelectIssue(Model.GetFilteredIssues()[0].StableKey);
	TArray<FPaper2DPlusValidationIssue> Replacement;
	Replacement.Add(Issues[2]);
	Model.SetIssues(Replacement);
	TestTrue(TEXT("A validation refresh that removes the selected issue clears stale selection"), Model.GetSelectedStableKey().IsEmpty());

	FPaper2DPlusValidationSummary Validating;
	Validating.bValidating = true;
	TestTrue(TEXT("Validating state is exposed as text"), Validating.GetAccessibleText().ToString().Contains(TEXT("Validating")));
	TestEqual(TEXT("Validating state exposes refresh icon"), Validating.GetStatusIconName(), FName(TEXT("Icons.Refresh")));
	FPaper2DPlusValidationSummary WarningOnly;
	WarningOnly.NumWarnings = 1;
	TestTrue(TEXT("Warning state is exposed as text"), WarningOnly.GetAccessibleText().ToString().Contains(TEXT("warnings")));
	TestEqual(TEXT("Warning state exposes warning icon"), WarningOnly.GetStatusIconName(), FName(TEXT("Icons.Warning")));
	const FPaper2DPlusValidationSummary Clean;
	TestTrue(TEXT("Clean state is exposed as text"), Clean.GetAccessibleText().ToString().Contains(TEXT("No validation issues")));
	TestEqual(TEXT("Clean state exposes success icon"), Clean.GetStatusIconName(), FName(TEXT("Icons.Check")));

	TArray<FPaper2DPlusValidationIssue> Repeat = Issues;
	FPaper2DPlusValidationService::NormalizeAndSort(Repeat);
	TArray<FPaper2DPlusValidationIssue> RepeatAgain = Issues;
	FPaper2DPlusValidationService::NormalizeAndSort(RepeatAgain);
	TestEqual(TEXT("Identical input produces identical row count"), Repeat.Num(), RepeatAgain.Num());
	for (int32 Index = 0; Index < Repeat.Num() && Index < RepeatAgain.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("Stable row key %d"), Index), Repeat[Index].StableKey, RepeatAgain[Index].StableKey);
		TestEqual(FString::Printf(TEXT("Stable row code %d"), Index), Repeat[Index].Code, RepeatAgain[Index].Code);
		TestEqual(FString::Printf(TEXT("Stable row severity %d"), Index), Repeat[Index].Severity, RepeatAgain[Index].Severity);
		TestEqual(FString::Printf(TEXT("Stable row asset path %d"), Index), Repeat[Index].AssetPath.ToString(), RepeatAgain[Index].AssetPath.ToString());
		TestEqual(FString::Printf(TEXT("Stable row asset type %d"), Index), Repeat[Index].AssetType, RepeatAgain[Index].AssetType);
		TestEqual(FString::Printf(TEXT("Stable row scope %d"), Index), Repeat[Index].Scope, RepeatAgain[Index].Scope);
		TestEqual(FString::Printf(TEXT("Stable row item %d"), Index), Repeat[Index].ItemIdentity, RepeatAgain[Index].ItemIdentity);
		TestEqual(FString::Printf(TEXT("Stable row field %d"), Index), Repeat[Index].Field, RepeatAgain[Index].Field);
		TestEqual(FString::Printf(TEXT("Stable row message %d"), Index), Repeat[Index].Message.ToString(), RepeatAgain[Index].Message.ToString());
		TestEqual(FString::Printf(TEXT("Stable row remediation %d"), Index), Repeat[Index].Remediation.ToString(), RepeatAgain[Index].Remediation.ToString());
		TestEqual(FString::Printf(TEXT("Stable row target presence %d"), Index), Repeat[Index].ToolTarget.IsSet(), RepeatAgain[Index].ToolTarget.IsSet());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusValidationPanelWidgetTest,
	"Paper2DPlus.Validation.SharedProjection.PanelRefreshAndActivation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusValidationPanelWidgetTest::RunTest(const FString& Parameters)
{
	FPaper2DPlusValidationService& Service = FPaper2DPlusValidationService::Get();
	const FName AdapterId(TEXT("Paper2DPlus.Test.ValidationPanel"));
	int32 AdapterCalls = 0;
	Service.RegisterAdapter(
		AdapterId,
		FName(*UPaperFlipbook::StaticClass()->GetPathName()),
		FPaper2DPlusValidationAdapterDelegate::CreateLambda(
			[&AdapterCalls](const UObject&, TArray<FPaper2DPlusValidationIssue>& OutIssues)
			{
				++AdapterCalls;
				OutIssues.Add(MakeTestIssue(
					EPaper2DPlusValidationSeverity::Error,
					TEXT("P2DP.Actionable"),
					TEXT("Run"),
					TEXT("Flipbook"),
					TEXT("Actionable issue"),
					true));
				OutIssues.Add(MakeTestIssue(
					EPaper2DPlusValidationSeverity::Warning,
					TEXT("P2DP.ReadOnly"),
					TEXT("Jump"),
					TEXT("Frames"),
					TEXT("Readable issue"),
					false));
			}));

	UPaperFlipbook* Asset = NewObject<UPaperFlipbook>();
	int32 ActivationCalls = 0;
	FString ActivatedStableKey;
	FSoftObjectPath ActivatedAssetPath;
	FName ActivatedTool;
	FString ActivatedItem;
	FName ActivatedField;
	TSharedRef<SProfileValidationPanel> Panel = SNew(SProfileValidationPanel)
		.Asset(Asset)
		.OnIssueActivated(FOnPaper2DPlusValidationIssueActivated::CreateLambda(
			[&ActivationCalls, &ActivatedStableKey, &ActivatedAssetPath, &ActivatedTool, &ActivatedItem, &ActivatedField](const FPaper2DPlusValidationIssue& Issue)
			{
				++ActivationCalls;
				ActivatedStableKey = Issue.StableKey;
				ActivatedAssetPath = Issue.AssetPath;
				ActivatedTool = Issue.ToolTarget->ToolId;
				ActivatedItem = Issue.ToolTarget->ItemIdentity;
				ActivatedField = Issue.ToolTarget->Field;
			}));

	TestEqual(TEXT("Construction performs one explicit validation"), Panel->GetValidationRunCountForTests(), 1);
	TestEqual(TEXT("Adapter ran once during construction"), AdapterCalls, 1);
	TestEqual(TEXT("Panel exposes both issues"), Panel->GetModel().GetIssues().Num(), 2);

	FPropertyChangedEvent ExternalChange(nullptr, EPropertyChangeType::ValueSet);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Asset, ExternalChange);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Asset, ExternalChange);
	FCoreUObjectDelegates::OnObjectModified.Broadcast(Asset);
	const FTransactionObjectEvent UndoLikeChange{};
	FCoreUObjectDelegates::OnObjectTransacted.Broadcast(Asset, UndoLikeChange);
	Panel->FlushPendingRefreshForTests();
	TestEqual(TEXT("External-property and undo notifications coalesce to one refresh"), Panel->GetValidationRunCountForTests(), 2);
	TestEqual(TEXT("Coalesced refresh runs the adapter once"), AdapterCalls, 2);

	UPaperFlipbook* UnrelatedAsset = NewObject<UPaperFlipbook>();
	FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(UnrelatedAsset, ExternalChange);
	Panel->FlushPendingRefreshForTests();
	TestEqual(TEXT("Unrelated external changes do not refresh this panel"), Panel->GetValidationRunCountForTests(), 2);

	const FPaper2DPlusValidationIssue* Actionable = Panel->GetModel().GetIssues().FindByPredicate(
		[](const FPaper2DPlusValidationIssue& Issue) { return Issue.Code == TEXT("P2DP.Actionable"); });
	const FPaper2DPlusValidationIssue* ReadOnly = Panel->GetModel().GetIssues().FindByPredicate(
		[](const FPaper2DPlusValidationIssue& Issue) { return Issue.Code == TEXT("P2DP.ReadOnly"); });
	TestNotNull(TEXT("Actionable issue is present"), Actionable);
	TestNotNull(TEXT("Read-only issue is present"), ReadOnly);
	if (Actionable && ReadOnly)
	{
		TestTrue(TEXT("Truthful target activates once"), Panel->ActivateIssueForTests(*Actionable));
		TestEqual(TEXT("Activation delegate fires once"), ActivationCalls, 1);
		TestEqual(TEXT("Activation routes the exact stable issue"), ActivatedStableKey, Actionable->StableKey);
		TestEqual(TEXT("Activation routes the exact owning asset"), ActivatedAssetPath.ToString(), Actionable->AssetPath.ToString());
		TestEqual(TEXT("Activation routes the exact tool"), ActivatedTool, FName(TEXT("TestTool")));
		TestEqual(TEXT("Activation routes the exact item"), ActivatedItem, FString(TEXT("Run")));
		TestEqual(TEXT("Activation routes the exact field"), ActivatedField, FName(TEXT("Flipbook")));
		TestFalse(TEXT("Issue without a target cannot imply navigation"), Panel->ActivateIssueForTests(*ReadOnly));
		TestEqual(TEXT("Read-only activation does not call host"), ActivationCalls, 1);
	}

	UPaperFlipbook* DeferredAsset = NewObject<UPaperFlipbook>();
	TSharedRef<SProfileValidationPanel> DeferredPanel = SNew(SProfileValidationPanel)
		.Asset(DeferredAsset)
		.RunActionText(FText::FromString(TEXT("Check Again")))
		.RunInitially(false)
		.RefreshOnObservedChanges(false);
	TestEqual(TEXT("A docked warnings host can defer its expensive first check"),
		DeferredPanel->GetValidationRunCountForTests(), 0);
	TestEqual(TEXT("A deferred warnings host does not imply a clean result before checking"),
		DeferredPanel->GetModel().GetIssues().Num(), 0);
	TestTrue(TEXT("The deferred empty state names the explicit warning action"),
		DeferredPanel->GetEmptyStateTextForTests().ToString().Contains(TEXT("Check Again")));
	TestFalse(TEXT("The deferred empty state does not claim validation succeeded"),
		DeferredPanel->GetEmptyStateTextForTests().ToString().Contains(TEXT("No validation issues")));
	DeferredPanel->RunValidationNow();
	TestEqual(TEXT("The first explicit warning check runs exactly once"),
		DeferredPanel->GetValidationRunCountForTests(), 1);
	TestEqual(TEXT("The deferred warning check invokes the adapter once"), AdapterCalls, 3);
	FCoreUObjectDelegates::OnObjectModified.Broadcast(DeferredAsset);
	DeferredPanel->FlushPendingRefreshForTests();
	TestEqual(TEXT("A manual warnings host does not rerun after observed asset changes"),
		DeferredPanel->GetValidationRunCountForTests(), 1);

	TestTrue(TEXT("Test adapter unregisters"), Service.UnregisterAdapter(AdapterId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusValidationPanelDependencyRefreshTest,
	"Paper2DPlus.Validation.SharedProjection.ExternalDependencyRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusValidationPanelDependencyRefreshTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Character = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaper2DPlusEffectProfileAsset* UnrelatedEffect = NewObject<UPaper2DPlusEffectProfileAsset>();
	UPaper2DPlusCharacterCatalogAsset* Catalog = NewObject<UPaper2DPlusCharacterCatalogAsset>();
	FPaper2DPlusCharacterCatalogEntry& CatalogEntry = Catalog->Entries.AddDefaulted_GetRef();
	CatalogEntry.CharacterProfile = Character;
	UPaperFlipbook* Unrelated = NewObject<UPaperFlipbook>();
	UPaper2DPlusSettings* Settings = GetMutableDefault<UPaper2DPlusSettings>();
	const TSoftObjectPtr<UPaper2DPlusCharacterCatalogAsset> OriginalDefaultCatalog =
		Settings->DefaultCharacterCatalog;
	Settings->DefaultCharacterCatalog = Catalog;

	{
		TSharedRef<SProfileValidationPanel> Panel = SNew(SProfileValidationPanel).Asset(Character);
		const int32 InitialRuns = Panel->GetValidationRunCountForTests();
		FPropertyChangedEvent Change(nullptr, EPropertyChangeType::ValueSet);

		FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Unrelated, Change);
		Panel->FlushPendingRefreshForTests();
		TestEqual(TEXT("an unrelated object change still does not refresh the Character panel"),
			Panel->GetValidationRunCountForTests(), InitialRuns);

		// Registry events use the same adapter dependency graph instead of invalidating this panel for every
		// asset imported anywhere in the project.
		TestFalse(TEXT("an unrelated flipbook registry event is filtered"),
			Panel->NotifyAssetRegistryChangedForTests(FAssetData(Unrelated)));
		TestFalse(TEXT("an unassigned Effect Profile registry event is filtered"),
			Panel->NotifyAssetRegistryChangedForTests(FAssetData(UnrelatedEffect)));
		Panel->FlushPendingRefreshForTests();
		TestEqual(TEXT("filtered registry events schedule no validation"),
			Panel->GetValidationRunCountForTests(), InitialRuns);

		TestTrue(TEXT("the configured Catalog path is a Character validation dependency"),
			Panel->NotifyAssetRegistryChangedForTests(FAssetData(Catalog)));
		Panel->FlushPendingRefreshForTests();
		TestEqual(TEXT("the relevant Catalog registry event refreshes once"),
			Panel->GetValidationRunCountForTests(), InitialRuns + 1);

		FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Settings, Change);
		Panel->FlushPendingRefreshForTests();
		TestEqual(TEXT("the configured Catalog authority setting invalidates Character validation"),
			Panel->GetValidationRunCountForTests(), InitialRuns + 2);
	}

	UPaper2DPlusEffectProfileAsset* ObservedEffect =
		NewObject<UPaper2DPlusEffectProfileAsset>();
	FPaper2DPlusEffectProfileEntry& ObservedEntry =
		ObservedEffect->Effects.AddDefaulted_GetRef();
	ObservedEntry.EffectFlipbook = Unrelated;
	{
		TSharedRef<SProfileValidationPanel> Panel =
			SNew(SProfileValidationPanel).Asset(ObservedEffect);
		const int32 InitialRuns = Panel->GetValidationRunCountForTests();
		TestFalse(TEXT("an unrelated Effect asset event does not refresh an Effect Profile panel"),
			Panel->NotifyAssetRegistryChangedForTests(FAssetData(UnrelatedEffect)));
		TestTrue(TEXT("a referenced Flipbook registry event refreshes its Effect Profile panel"),
			Panel->NotifyAssetRegistryChangedForTests(FAssetData(Unrelated)));
		Panel->FlushPendingRefreshForTests();
		TestEqual(TEXT("the referenced Flipbook event runs validation once"),
			Panel->GetValidationRunCountForTests(), InitialRuns + 1);

		TestTrue(TEXT("a rename away from the referenced Flipbook path also refreshes"),
			Panel->NotifyAssetRegistryChangedForTests(
				FAssetData(UnrelatedEffect), FSoftObjectPath(Unrelated).ToString()));
		Panel->NotifyAssetRegistryFilesLoadedForTests();
		Panel->FlushPendingRefreshForTests();
		TestEqual(TEXT("registry discovery completion and rename coalesce into one revalidation"),
			Panel->GetValidationRunCountForTests(), InitialRuns + 2);
	}

	Settings->DefaultCharacterCatalog = OriginalDefaultCatalog;
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
