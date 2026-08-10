// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Paper2DPlusTypes.h"
#include "Paper2DPlusFrameCurve.h"
#include "Paper2DPlusFrameData.h"
#include "Paper2DPlusMoveTransition.h"
#include "Paper2DPlusAnimationMapLibrary.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "GameFramework/Actor.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(
	ObjRef_AnimationMapGroupAttack,
	"Paper2DPlus.Test.ObjectRef.AnimationMap.Group.Attack")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	ObjRef_AnimationMapGroupSpecial,
	"Paper2DPlus.Test.ObjectRef.AnimationMap.Group.Special")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	ObjRef_AnimationMapPhaseActive,
	"Paper2DPlus.Test.ObjectRef.AnimationMap.Phase.Active")
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	ObjRef_AnimationMapPhaseRecovery,
	"Paper2DPlus.Test.ObjectRef.AnimationMap.Phase.Recovery")

/** Object-reference API tests (worldless). Verifies the UPaperFlipbook*-keyed variants added alongside
 *  the name-based API: the GetFlipbookByName <-> GetFlipbookName resolver pair + ContainsFlipbook; the
 *  asset accessors' *ByFlipbook siblings (frame/hitbox/socket/attack/phase); the BlueprintLibrary object
 *  variants (root motion, move frame data, curves); the component's GetCurrentMoveFlipbook; and the
 *  focused Animation Map resolver's object-reference inputs. The contract under test is
 *  parity: every object variant returns exactly what its name twin returns, and an object that isn't one
 *  of the asset's flipbooks resolves to "not found" (empty/null/false) — never a stray "None" lookup.
 *  Helpers are ObjRef_-prefixed per the unity-build file-unique-name rule. */

namespace
{
	/** Build a flipbook with NumFrames single-run key frames, owned by Owner (mirrors the transition test). */
	UPaperFlipbook* ObjRef_MakeFlipbook(UObject* Owner, int32 NumFrames)
	{
		UPaperFlipbook* FB = NewObject<UPaperFlipbook>(Owner);
		FScopedFlipbookMutator Mutator(FB);
		Mutator.FramesPerSecond = 10.0f;
		Mutator.KeyFrames.Empty();
		for (int32 i = 0; i < NumFrames; ++i)
		{
			FPaperFlipbookKeyFrame KF;
			KF.FrameRun = 1;
			KF.Sprite = NewObject<UPaperSprite>(FB);
			Mutator.KeyFrames.Add(KF);
		}
		return FB;
	}

	/** Add a flipbook entry named MoveName with a NumFrames-key flipbook; returns the live flipbook and
	 *  (optionally) the entry's array index. CombatData.Frames is sized to NumFrames so per-frame authoring
	 *  is valid. */
	UPaperFlipbook* ObjRef_AddMove(UPaper2DPlusCharacterProfileAsset* Asset, const FString& MoveName, int32 NumFrames, int32* OutIndex = nullptr)
	{
		FFlipbookProfileEntry Anim;
		Anim.Identity.FlipbookName = MoveName;
		UPaperFlipbook* FB = ObjRef_MakeFlipbook(Asset, NumFrames);
		Anim.Identity.Flipbook = FB;
		Anim.CombatData.Frames.SetNum(NumFrames);
		Anim.CombatData.FrameExtractionInfo.SetNum(NumFrames);
		const int32 Index = Asset->Flipbooks.Add(Anim);
		if (OutIndex)
		{
			*OutIndex = Index;
		}
		return FB;
	}
}

// ─── 1. Resolver pair + ContainsFlipbook + foreign/unknown handling ──────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusObjRefResolverPairTest,
	"Paper2DPlus.ObjectRef.ResolverPair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusObjRefResolverPairTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* IdleFB = ObjRef_AddMove(Asset, TEXT("Idle"), 3);
	UPaperFlipbook* RunFB = ObjRef_AddMove(Asset, TEXT("Run"), 5);
	UPaperFlipbook* ForeignFB = ObjRef_MakeFlipbook(Asset, 2); // never added to the asset

	// name -> object
	TestTrue(TEXT("GetFlipbookByName(Idle) returns the Idle flipbook"), Asset->GetFlipbookByName(TEXT("Idle")) == IdleFB);
	TestTrue(TEXT("GetFlipbookByName is case-insensitive"), Asset->GetFlipbookByName(TEXT("run")) == RunFB);
	TestNull(TEXT("GetFlipbookByName(unknown) is null"), Asset->GetFlipbookByName(TEXT("Nope")));

	// object -> name
	TestEqual(TEXT("GetFlipbookName(IdleFB) returns the authored name"), Asset->GetFlipbookName(IdleFB), FString(TEXT("Idle")));
	TestEqual(TEXT("GetFlipbookName(ForeignFB) is empty"), Asset->GetFlipbookName(ForeignFB), FString());
	TestEqual(TEXT("GetFlipbookName(null) is empty"), Asset->GetFlipbookName(nullptr), FString());

	// round-trip both ways
	TestTrue(TEXT("Round-trip name->object->name"), Asset->GetFlipbookName(Asset->GetFlipbookByName(TEXT("Run"))) == FString(TEXT("Run")));

	// ContainsFlipbook
	TestTrue(TEXT("ContainsFlipbook(IdleFB) is true"), Asset->ContainsFlipbook(IdleFB));
	TestFalse(TEXT("ContainsFlipbook(ForeignFB) is false"), Asset->ContainsFlipbook(ForeignFB));
	TestFalse(TEXT("ContainsFlipbook(null) is false"), Asset->ContainsFlipbook(nullptr));

	return true;
}

// ─── 2. Asset accessors: *ByFlipbook == name twin ────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusObjRefAssetAccessorsTest,
	"Paper2DPlus.ObjectRef.AssetAccessorsMatchNamePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusObjRefAssetAccessorsTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	int32 AttackIdx = INDEX_NONE;
	UPaperFlipbook* AttackFB = ObjRef_AddMove(Asset, TEXT("Attack"), 4, &AttackIdx);
	UPaperFlipbook* ForeignFB = ObjRef_MakeFlipbook(Asset, 2);

	// Author frame 0: one Attack hitbox, one Hurtbox, one socket "Hand".
	{
		FFrameHitboxData& Frame = Asset->Flipbooks[AttackIdx].CombatData.Frames[0];
		FHitboxData Atk; Atk.Type = EHitboxType::Attack; Atk.X = 10; Atk.Y = 0; Atk.Width = 20; Atk.Height = 8; Atk.Damage = 5;
		FHitboxData Hurt; Hurt.Type = EHitboxType::Hurtbox; Hurt.X = -4; Hurt.Y = -4; Hurt.Width = 12; Hurt.Height = 24;
		Frame.Hitboxes.Add(Atk);
		Frame.Hitboxes.Add(Hurt);
		FSocketData Socket; Socket.Name = TEXT("Hand");
		Frame.Sockets.Add(Socket);
	}
	// A profile-data mutation invalidates the cached lookups; refresh defensively (matches runtime use).
	Asset->InvalidateFlipbookLookupCache();

	const FString Name(TEXT("Attack"));

	// Frame count
	TestEqual(TEXT("GetFrameCountByFlipbook is 4"), Asset->GetFrameCountByFlipbook(AttackFB), 4);
	TestEqual(TEXT("GetFrameCountByFlipbook(foreign) is 0"), Asset->GetFrameCountByFlipbook(ForeignFB), 0);

	// GetFrame
	FFrameHitboxData FrameByObj;
	const bool bObjFrame = Asset->GetFrameByFlipbook(AttackFB, 0, FrameByObj);
	TestTrue(TEXT("GetFrameByFlipbook(0) succeeds"), bObjFrame);
	TestEqual(TEXT("GetFrameByFlipbook frame 0 has 2 hitboxes"), FrameByObj.Hitboxes.Num(), 2);
	TestFalse(TEXT("GetFrameByFlipbook(foreign) fails"), Asset->GetFrameByFlipbook(ForeignFB, 0, FrameByObj));

	// Hitboxes
	TestEqual(TEXT("GetHitboxesByFlipbook returns 2"), Asset->GetHitboxesByFlipbook(AttackFB, 0).Num(), 2);
	TestEqual(TEXT("GetHitboxesOfTypeByFlipbook(Attack) returns 1"), Asset->GetHitboxesOfTypeByFlipbook(AttackFB, 0, EHitboxType::Attack).Num(), 1);
	TestEqual(TEXT("GetHitboxesByFlipbook(foreign) is empty"), Asset->GetHitboxesByFlipbook(ForeignFB, 0).Num(), 0);

	// Sockets
	TestEqual(TEXT("GetSocketsByFlipbook returns 1"), Asset->GetSocketsByFlipbook(AttackFB, 0).Num(), 1);
	FSocketData SockObj;
	const bool bObjSock = Asset->FindSocketByFlipbook(AttackFB, 0, TEXT("Hand"), SockObj);
	TestTrue(TEXT("FindSocketByFlipbook(Hand) succeeds"), bObjSock);
	TestEqual(TEXT("FindSocketByFlipbook resolves the Hand socket"), SockObj.Name, FString(TEXT("Hand")));
	TestFalse(TEXT("FindSocketByFlipbook(missing) fails"), Asset->FindSocketByFlipbook(AttackFB, 0, TEXT("Nope"), SockObj));

	// Attack bounds / range
	TestTrue(TEXT("GetAttackRangeByFlipbook(Attack) is positive"), Asset->GetAttackRangeByFlipbook(AttackFB) > 0.0f);
	const FBox2D BoundsObj = Asset->GetAttackBoundsByFlipbook(AttackFB);
	const FBox2D BoundsName = Asset->GetAttackBoundsForFlipbook(Name);
	TestEqual(TEXT("GetAttackBoundsByFlipbook validity matches"), BoundsObj.bIsValid, BoundsName.bIsValid);
	TestTrue(TEXT("GetAttackBoundsByFlipbook bounds match"), BoundsObj.Min.Equals(BoundsName.Min) && BoundsObj.Max.Equals(BoundsName.Max));
	TestEqual(TEXT("GetAttackRangeByFlipbook(foreign) is 0"), Asset->GetAttackRangeByFlipbook(ForeignFB), 0.0f);

	return true;
}

// ─── 3. BlueprintLibrary object variants: root-motion / frame-data / curve parity ───────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusObjRefLibraryVariantsTest,
	"Paper2DPlus.ObjectRef.LibraryVariantsMatchNamePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusObjRefLibraryVariantsTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	int32 JabIdx = INDEX_NONE;
	UPaperFlipbook* JabFB = ObjRef_AddMove(Asset, TEXT("Jab"), 4, &JabIdx);
	UPaperFlipbook* ForeignFB = ObjRef_MakeFlipbook(Asset, 2);

	// Root motion on Jab frame 1.
	Asset->Flipbooks[JabIdx].MotionData.RootMotion.SetNum(4);
	Asset->Flipbooks[JabIdx].MotionData.RootMotion[1].Position = FVector2D(5.f, 7.f);

	// An aux curve "Power" on Jab.
	{
		FPaper2DPlusFrameCurve& Curve = Asset->Flipbooks[JabIdx].CurveData.Curves.FindOrAdd(TEXT("Power"));
		Curve.SetMode(EPaper2DPlusCurveInterp::Linear);
		Curve.SetKeyValue(0, 1.0f);
		Curve.SetKeyValue(2, 3.0f);
	}
	Asset->InvalidateFlipbookLookupCache();

	// Root motion
	TestTrue(TEXT("GetRootMotionAtFrameByFlipbook == GetRootMotionAtFrame"),
		UPaper2DPlusBlueprintLibrary::GetRootMotionAtFrameByFlipbook(Asset, JabFB, 1)
			.Equals(UPaper2DPlusBlueprintLibrary::GetRootMotionAtFrame(Asset, TEXT("Jab"), 1)));
	TestTrue(TEXT("GetRootMotionAtFrameByFlipbook returns (5,7)"),
		UPaper2DPlusBlueprintLibrary::GetRootMotionAtFrameByFlipbook(Asset, JabFB, 1).Equals(FVector2D(5.f, 7.f)));
	TestTrue(TEXT("GetRootMotionAtFrameByFlipbook(foreign) is zero"),
		UPaper2DPlusBlueprintLibrary::GetRootMotionAtFrameByFlipbook(Asset, ForeignFB, 1).IsNearlyZero());

	// Move frame data
	FPaper2DPlusMoveFrameData FDObj, FDName;
	const bool bObjFD = UPaper2DPlusBlueprintLibrary::GetMoveFrameDataByFlipbook(Asset, JabFB, FDObj);
	const bool bNameFD = UPaper2DPlusBlueprintLibrary::GetMoveFrameData(Asset, TEXT("Jab"), FDName);
	TestTrue(TEXT("GetMoveFrameDataByFlipbook succeeds"), bObjFD);
	TestEqual(TEXT("GetMoveFrameDataByFlipbook bool == name"), bObjFD, bNameFD);
	TestEqual(TEXT("GetMoveFrameDataByFlipbook TotalKeyFrames matches"), FDObj.TotalKeyFrames, FDName.TotalKeyFrames);
	TestFalse(TEXT("GetMoveFrameDataByFlipbook(foreign) fails"), UPaper2DPlusBlueprintLibrary::GetMoveFrameDataByFlipbook(Asset, ForeignFB, FDObj));

	// Curves (+ empty-name guard: foreign object must NOT round-trip into a "None" lookup)
	TestEqual(TEXT("GetMoveCurveValueAtFrameByFlipbook == name path (frame 0)"),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrameByFlipbook(Asset, JabFB, TEXT("Power"), 0, -99.f),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrame(Asset, TEXT("Jab"), TEXT("Power"), 0, -99.f));
	TestEqual(TEXT("GetMoveCurveValueAtFrameByFlipbook(0) is 1"),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFrameByFlipbook(Asset, JabFB, TEXT("Power"), 0, -99.f), 1.0f);
	TestEqual(TEXT("GetMoveCurveValueAtFramePositionByFlipbook(1.0) interpolates to 2"),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFramePositionByFlipbook(Asset, JabFB, TEXT("Power"), 1.0f, -99.f), 2.0f);
	TestEqual(TEXT("Curve(foreign) returns the default, not a None lookup"),
		UPaper2DPlusBlueprintLibrary::GetMoveCurveValueAtFramePositionByFlipbook(Asset, ForeignFB, TEXT("Power"), 0.f, -99.f), -99.0f);

	return true;
}

// ─── 4. Component object variants: GetCurrentMoveFlipbook ─────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusObjRefComponentVariantsTest,
	"Paper2DPlus.ObjectRef.ComponentVariants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusObjRefComponentVariantsTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* JabFB = ObjRef_AddMove(Asset, TEXT("Jab"), 4);
	UPaperFlipbook* ForeignFB = ObjRef_MakeFlipbook(Asset, 2); // never added to the asset
	// An attack hitbox on Jab frame 0 so the actor attack-range query has a non-trivial value to match on.
	{
		FHitboxData Atk; Atk.Type = EHitboxType::Attack; Atk.X = 10; Atk.Y = 0; Atk.Width = 20; Atk.Height = 8; Atk.Damage = 5;
		Asset->Flipbooks[0].CombatData.Frames[0].Hitboxes.Add(Atk);
	}
	Asset->InvalidateFlipbookLookupCache();

	AActor* Actor = NewObject<AActor>();
	UPaperFlipbookComponent* FBComp = NewObject<UPaperFlipbookComponent>(Actor);
	UPaper2DPlusCharacterProfileComponent* DataComp = NewObject<UPaper2DPlusCharacterProfileComponent>(Actor);
	FBComp->SetFlipbook(JabFB);
	DataComp->FlipbookComponent = FBComp;
	DataComp->CharacterProfile = Asset;

	// GetCurrentMoveFlipbook — object form of GetCurrentMoveName.
	TestTrue(TEXT("GetCurrentMoveFlipbook returns the live Jab flipbook"), DataComp->GetCurrentMoveFlipbook() == JabFB);
	TestEqual(TEXT("GetCurrentMoveName agrees (Jab)"), DataComp->GetCurrentMoveName(), FString(TEXT("Jab")));

	// Actor-based BlueprintLibrary *ByFlipbook overloads resolve the move off the actor's OWN profile
	// component (a distinct path from the asset-direct variants), so exercise them on this rig.
	FPaper2DPlusMoveFrameData FDObj, FDName;
	const bool bObjFD = UPaper2DPlusBlueprintLibrary::GetActorMoveFrameDataByFlipbook(Actor, JabFB, FDObj);
	const bool bNameFD = UPaper2DPlusBlueprintLibrary::GetActorMoveFrameData(Actor, TEXT("Jab"), FDName);
	TestTrue(TEXT("GetActorMoveFrameDataByFlipbook succeeds"), bObjFD);
	TestEqual(TEXT("GetActorMoveFrameDataByFlipbook bool == name path"), bObjFD, bNameFD);
	TestEqual(TEXT("GetActorMoveFrameDataByFlipbook TotalKeyFrames matches"), FDObj.TotalKeyFrames, FDName.TotalKeyFrames);
	TestFalse(TEXT("GetActorMoveFrameDataByFlipbook(foreign) fails"), UPaper2DPlusBlueprintLibrary::GetActorMoveFrameDataByFlipbook(Actor, ForeignFB, FDObj));
	TestFalse(TEXT("GetActorMoveFrameDataByFlipbook(null actor) fails"), UPaper2DPlusBlueprintLibrary::GetActorMoveFrameDataByFlipbook(nullptr, JabFB, FDObj));

	FVector2D RangeObj, RangeName;
	const bool bObjRange = UPaper2DPlusBlueprintLibrary::GetActorAttackRangeForMoveByFlipbook(Actor, JabFB, RangeObj);
	const bool bNameRange = UPaper2DPlusBlueprintLibrary::GetActorAttackRangeForMove(Actor, TEXT("Jab"), RangeName);
	TestEqual(TEXT("GetActorAttackRangeForMoveByFlipbook bool == name path"), bObjRange, bNameRange);
	TestTrue(TEXT("GetActorAttackRangeForMoveByFlipbook range == name path"), RangeObj.Equals(RangeName));
	FVector2D RangeForeign(1.f, 2.f); // seed non-zero to prove it gets cleared
	TestFalse(TEXT("GetActorAttackRangeForMoveByFlipbook(foreign) fails"), UPaper2DPlusBlueprintLibrary::GetActorAttackRangeForMoveByFlipbook(Actor, ForeignFB, RangeForeign));
	TestTrue(TEXT("GetActorAttackRangeForMoveByFlipbook(foreign) clears the out"), RangeForeign.IsZero());

	return true;
}

// ─── 5. Focused Animation Map resolver accepts exact object references ───────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusObjRefTransitionTargetTest,
	"Paper2DPlus.ObjectRef.AnimationMapTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusObjRefTransitionTargetTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* JabFB = ObjRef_AddMove(Asset, TEXT("Jab"), 4);
	UPaperFlipbook* Jab2FB = ObjRef_AddMove(Asset, TEXT("Jab2"), 4);
	UPaperFlipbook* ForeignFB = ObjRef_MakeFlipbook(Asset, 2);
	const FGameplayTag GroupTag = ObjRef_AnimationMapGroupAttack;
	const FGameplayTag Recovery = ObjRef_AnimationMapPhaseRecovery;

	Asset->Flipbooks[0].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Jab2")));
	Asset->Flipbooks[1].EditorMeta.PhaseTag = Recovery;
	FFlipbookTagMappingEntry RootEntry(TEXT("Jab"));
	RootEntry.bIsChainStart = true;
	Asset->TagMappings.FindOrAdd(GroupTag).Entries.Add(RootEntry);
	Asset->TagMappings.FindOrAdd(GroupTag).Entries.Emplace(TEXT("Jab2"));

	FPaper2DPlusAnimationSelectionCriteria Criteria;
	UPaperFlipbook* Resolved = nullptr;
	TestEqual(TEXT("Object-ref original resolves its Recovery transition"),
		UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
			Asset, JabFB, Recovery, Criteria, Resolved),
		EPaper2DPlusAnimationResolveResult::Success);
	TestTrue(TEXT("Resolved object is Jab2"), Resolved == Jab2FB);
	TestEqual(TEXT("A foreign object has no map entry"),
		UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
			Asset, ForeignFB, Recovery, Criteria, Resolved),
		EPaper2DPlusAnimationResolveResult::FlipbookNotInMap);
	TestNull(TEXT("Failure clears the object output"), Resolved);

	return true;
}

// ─── 6. Chain-start opener discovery and phase resolution ───────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusObjRefAnimationRootTest,
	"Paper2DPlus.ObjectRef.ComboOpener",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusObjRefAnimationRootTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	int32 A1Idx = INDEX_NONE, A2Idx = INDEX_NONE;
	UPaperFlipbook* A1 = ObjRef_AddMove(Asset, TEXT("Attack1"), 3, &A1Idx);
	UPaperFlipbook* A2 = ObjRef_AddMove(Asset, TEXT("Attack2"), 3, &A2Idx);
	const FGameplayTag GroupTag = ObjRef_AnimationMapGroupAttack;
	const FGameplayTag Active = ObjRef_AnimationMapPhaseActive;

	Asset->Flipbooks[A1Idx].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Attack2")));
	Asset->Flipbooks[A2Idx].EditorMeta.PhaseTag = Active;
	FFlipbookTagMappingEntry RootEntry(TEXT("Attack1"));
	RootEntry.bIsChainStart = true;
	Asset->TagMappings.FindOrAdd(GroupTag).Entries.Add(RootEntry);
	Asset->TagMappings.FindOrAdd(GroupTag).Entries.Emplace(TEXT("Attack2"));

	const TArray<UPaperFlipbook*> Openers =
		UPaper2DPlusAnimationMapLibrary::GetComboOpenerFlipbooks(Asset);
	TestEqual(TEXT("Dynamic discovery exposes the one flagged chain start"), Openers.Num(), 1);
	TestTrue(TEXT("Opener discovery returns the opener object directly"),
		Openers.Num() == 1 && Openers[0] == A1);

	FPaper2DPlusAnimationSelectionCriteria Criteria;
	UPaperFlipbook* Resolved = nullptr;
	TestEqual(TEXT("Phase resolution returns the direct object target"),
		UPaper2DPlusAnimationMapLibrary::ResolveAnimationTransition(
			Asset, A1, Active, Criteria, Resolved),
		EPaper2DPlusAnimationResolveResult::Success);
	TestTrue(TEXT("Resolved object target is Attack2"), Resolved == A2);

	return true;
}

// ─── 7. Chain starts are exact-group identities; multiple flags = multiple valid chains ────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusObjRefGroupScopedRootTest,
	"Paper2DPlus.ObjectRef.GroupScopedChainStarts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusObjRefGroupScopedRootTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Asset = NewObject<UPaper2DPlusCharacterProfileAsset>();
	int32 BCIdx = INDEX_NONE, A1Idx = INDEX_NONE;
	UPaperFlipbook* BC = ObjRef_AddMove(Asset, TEXT("BlockCounter"), 2, &BCIdx);
	UPaperFlipbook* A1 = ObjRef_AddMove(Asset, TEXT("Attack1"), 3, &A1Idx);
	UPaperFlipbook* A2 = ObjRef_AddMove(Asset, TEXT("Attack2"), 3);
	const FGameplayTag AttackGroup = ObjRef_AnimationMapGroupAttack;
	const FGameplayTag SpecialGroup = ObjRef_AnimationMapGroupSpecial;
	Asset->Flipbooks[BCIdx].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Attack1")));
	Asset->Flipbooks[A1Idx].TransitionData.Transitions.Add(FPaper2DPlusMoveTransition(TEXT("Attack2")));

	FFlipbookTagMappingEntry AttackRoot(TEXT("Attack1"));
	AttackRoot.bIsChainStart = true;
	Asset->TagMappings.FindOrAdd(AttackGroup).Entries.Add(AttackRoot);
	Asset->TagMappings.FindOrAdd(AttackGroup).Entries.Emplace(TEXT("Attack2"));

	TArray<UPaperFlipbook*> Openers = UPaper2DPlusAnimationMapLibrary::GetComboOpenerFlipbooks(Asset);
	TestEqual(TEXT("One flagged chain start discovered in the Attack group"), Openers.Num(), 1);
	TestTrue(TEXT("Incoming BlockCounter does not replace authored chain-start identity"),
		Openers.Num() == 1 && Openers[0] == A1);

	// A chain-start flag is independent in another exact group.
	FFlipbookTagMappingEntry SpecialRoot(TEXT("BlockCounter"));
	SpecialRoot.bIsChainStart = true;
	Asset->TagMappings.FindOrAdd(SpecialGroup).Entries.Add(SpecialRoot);
	Openers = UPaper2DPlusAnimationMapLibrary::GetComboOpenerFlipbooks(Asset);
	TestEqual(TEXT("Each group's flagged opener is discovered independently"), Openers.Num(), 2);
	TestTrue(TEXT("Group-tag-sorted discovery: the Attack opener precedes the Special opener"),
		Openers.Num() == 2 && Openers[0] == A1 && Openers[1] == BC);

	// Two flagged entries in ONE exact group are two VALID chains (the duplicate-number failure
	// mode died with numbered roots). Flag the EXISTING Attack2 entry — a second membership row
	// would be duplicated membership, which fails closed by design.
	Asset->TagMappings.FindOrAdd(AttackGroup).Entries[1].bIsChainStart = true;
	Openers = UPaper2DPlusAnimationMapLibrary::GetComboOpenerFlipbooks(Asset);
	TestEqual(TEXT("Two flags in one group author two valid chains"), Openers.Num(), 3);
	TestTrue(TEXT("Same-group openers keep authored entry order"),
		Openers.Num() == 3 && Openers[0] == A1 && Openers[1] == A2 && Openers[2] == BC);

	return true;
}

#endif // WITH_EDITOR
