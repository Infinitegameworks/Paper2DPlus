// Copyright 2026 Infinite Gameworks. All Rights Reserved.
//
// U1 (layered-asset redesign) — editor trust fixes, worldless coverage of the model's new
// SECONDARY watched object (the Character Layer editor registers its layer asset so external
// Modify()s of it route through the SAME deferred OnAssetExternallyModified broadcast the
// profile asset uses):
//   - layer-asset Modify() fires the broadcast when the secondary is registered;
//   - a subobject of the secondary matches (the Object->IsIn(Secondary) arm);
//   - a profile-only model (no secondary — the Character Profile editor path) ignores an
//     unrelated object, and clearing the secondary restores that behavior;
//   - the secondary works with NO primary profile (layer asset without a BaseProfile);
//   - the secondary channel obeys the model-mutation pend discipline (BeginModelMutation
//     pends, EndModelMutation flushes — same as the primary channel).
//
// The model defers the broadcast via a 0-delay FTSTicker; tests pump the core ticker
// (FTSTicker::GetCoreTicker().Tick) — the proven worldless pump from Paper2DPlusHitStopTest.
// No asset editors are opened (nothing here needs a render context or a CanEverRender guard).
//
// Helpers are LayerTrust_-prefixed (unity-build FILE-UNIQUE test-helper name rule).

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Containers/Ticker.h"
#include "CharacterProfileEditorModel.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "PaperFlipbook.h"

namespace
{
// Drain the model's pending deferred external-modify ticker (registered with 0 delay, so one
// small tick fires it). Other registered core tickers may also fire — harmless in the suite.
void LayerTrust_PumpDeferredNotify()
{
	FTSTicker::GetCoreTicker().Tick(0.01f);
}
}

// =============================================================================
// Secondary watch: Modify() on the registered layer asset fires the model's
// external-modify broadcast; the primary (profile) channel keeps firing too;
// clearing the secondary stops the layer-asset channel.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerTrustSecondaryWatchFiresExternalModify,
	"Paper2DPlus.LayerEditor.Trust.SecondaryWatch.FiresExternalModify",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerTrustSecondaryWatchFiresExternalModify::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaper2DPlusCharacterLayerAsset* LayerAsset = NewObject<UPaper2DPlusCharacterLayerAsset>();

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	Model->SetSecondaryWatchedObject(LayerAsset);
	TestEqual(TEXT("GetSecondaryWatchedObject returns the registered layer asset"),
		Model->GetSecondaryWatchedObject(), static_cast<UObject*>(LayerAsset));

	int32 ExternalModifyCount = 0;
	Model->OnAssetExternallyModified.AddLambda([&ExternalModifyCount]() { ExternalModifyCount++; });

	// Simulated external modify of the LAYER asset (outside any panel transaction).
	LayerAsset->Modify();
	LayerTrust_PumpDeferredNotify();
	TestEqual(TEXT("Layer-asset Modify() fires the deferred external-modify broadcast"), ExternalModifyCount, 1);

	// The primary (profile) channel is untouched by the secondary registration.
	Profile->Modify();
	LayerTrust_PumpDeferredNotify();
	TestEqual(TEXT("Profile Modify() still fires through the same channel"), ExternalModifyCount, 2);

	// Clearing the secondary stops the layer-asset channel (profile-only filter restored).
	Model->SetSecondaryWatchedObject(nullptr);
	LayerAsset->Modify();
	LayerTrust_PumpDeferredNotify();
	TestEqual(TEXT("Cleared secondary: layer-asset Modify() no longer fires"), ExternalModifyCount, 2);

	return true;
}

// =============================================================================
// Secondary watch: a SUBOBJECT of the registered layer asset matches via the
// Object->IsIn(Secondary) arm (mirrors the primary filter's subobject handling).
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerTrustSecondaryWatchSubObjectMatches,
	"Paper2DPlus.LayerEditor.Trust.SecondaryWatch.SubObjectMatches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerTrustSecondaryWatchSubObjectMatches::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaper2DPlusCharacterLayerAsset* LayerAsset = NewObject<UPaper2DPlusCharacterLayerAsset>();
	// Any concrete UObject class outered to the layer asset works here (bare UObject is abstract and
	// trips the StaticAllocateObject abstract-class ensure under the automation worker).
	UObject* LayerInner = NewObject<UPaperFlipbook>(LayerAsset);

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	Model->SetSecondaryWatchedObject(LayerAsset);

	int32 ExternalModifyCount = 0;
	Model->OnAssetExternallyModified.AddLambda([&ExternalModifyCount]() { ExternalModifyCount++; });

	LayerInner->Modify();
	LayerTrust_PumpDeferredNotify();
	TestEqual(TEXT("Subobject of the secondary fires the external-modify broadcast"), ExternalModifyCount, 1);

	return true;
}

// =============================================================================
// Profile-only model (no secondary registered — the Character Profile editor
// path): an unrelated object's Modify() must NOT fire. Pins the byte-identical
// filter guarantee for the profile editor.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerTrustProfileOnlyIgnoresUnrelatedObject,
	"Paper2DPlus.LayerEditor.Trust.ProfileOnly.IgnoresUnrelatedObject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerTrustProfileOnlyIgnoresUnrelatedObject::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaper2DPlusCharacterLayerAsset* UnrelatedLayerAsset = NewObject<UPaper2DPlusCharacterLayerAsset>();

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	// Deliberately NO SetSecondaryWatchedObject — this is the profile editor's configuration.

	int32 ExternalModifyCount = 0;
	Model->OnAssetExternallyModified.AddLambda([&ExternalModifyCount]() { ExternalModifyCount++; });

	UnrelatedLayerAsset->Modify();
	LayerTrust_PumpDeferredNotify();
	TestEqual(TEXT("Unrelated object's Modify() does NOT fire for a profile-only model"), ExternalModifyCount, 0);

	Profile->Modify();
	LayerTrust_PumpDeferredNotify();
	TestEqual(TEXT("Profile Modify() fires (sanity: the channel itself works)"), ExternalModifyCount, 1);

	return true;
}

// =============================================================================
// Secondary with NO primary: a layer asset with no BaseProfile still gets its
// external-modify channel (the layer editor creates the model without
// InitializeFromAsset when BaseProfile is unset).
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerTrustSecondaryWatchWorksWithoutPrimary,
	"Paper2DPlus.LayerEditor.Trust.SecondaryWatch.WorksWithoutPrimary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerTrustSecondaryWatchWorksWithoutPrimary::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterLayerAsset* LayerAsset = NewObject<UPaper2DPlusCharacterLayerAsset>();

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	// NO InitializeFromAsset — the layer asset has no BaseProfile.
	Model->SetSecondaryWatchedObject(LayerAsset);

	int32 ExternalModifyCount = 0;
	Model->OnAssetExternallyModified.AddLambda([&ExternalModifyCount]() { ExternalModifyCount++; });

	LayerAsset->Modify();
	LayerTrust_PumpDeferredNotify();
	TestEqual(TEXT("Layer-asset Modify() fires with no primary profile set"), ExternalModifyCount, 1);

	return true;
}

// =============================================================================
// Pend discipline: a secondary-channel modify DURING a model mutation scope is
// pended (not dropped, not broadcast early) and flushes at EndModelMutation —
// the same discipline the primary channel uses.
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerTrustSecondaryWatchPendsDuringModelMutation,
	"Paper2DPlus.LayerEditor.Trust.SecondaryWatch.PendsDuringModelMutation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerTrustSecondaryWatchPendsDuringModelMutation::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaper2DPlusCharacterLayerAsset* LayerAsset = NewObject<UPaper2DPlusCharacterLayerAsset>();

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	Model->SetSecondaryWatchedObject(LayerAsset);

	int32 ExternalModifyCount = 0;
	Model->OnAssetExternallyModified.AddLambda([&ExternalModifyCount]() { ExternalModifyCount++; });

	Model->BeginModelMutation();
	LayerAsset->Modify();
	LayerTrust_PumpDeferredNotify();
	TestEqual(TEXT("Inside a mutation scope the broadcast is pended, not fired"), ExternalModifyCount, 0);

	Model->EndModelMutation();
	TestEqual(TEXT("EndModelMutation flushes the pended external-modify broadcast"), ExternalModifyCount, 1);

	return true;
}

#endif // WITH_EDITOR
