// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusFrameCuePreviewBehaviorTestTypes.h"

APaper2DPlusPreviewBehaviorVisibleActorTest::
	APaper2DPlusPreviewBehaviorVisibleActorTest()
{
	ArrowComponent = CreateDefaultSubobject<UArrowComponent>(TEXT("VisiblePreviewArrow"));
	SetRootComponent(ArrowComponent);
	ArrowComponent->ArrowColor = FColor::Yellow;
	ArrowComponent->ArrowSize = 4.0f;
	ArrowComponent->SetHiddenInGame(false);
	ArrowComponent->SetVisibility(true);
}

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CharacterProfileEditorModel.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EngineUtils.h"
// Explicit, not transitive: Created.CueType->GeneratedClass needs the complete type, and unity-blob
// grouping shifts whenever module .cpp files are added or removed.
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueDispatch.h"
#include "FrameCues/Paper2DPlusFrameCueEditorSettings.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewBehavior.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewContext.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewHost.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "FrameCues/Paper2DPlusFrameCueTypeEditor.h"
#include "FrameCues/Paper2DPlusFrameCueMigrationService.h"
#include "FrameEventEditor.h"
#include "HAL/FileManager.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_SpawnActorFromClass.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "PaperFlipbook.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusFrameCueProjectAdapterTestTypes.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Sound/SoundWave.h"
#include "SFrameCueTimeline.h"
#include "Types/SlateAttributeMetaData.h"
#include "UObject/CoreRedirects.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace P2DPPreviewBehaviorLog = Paper2DPlusPreviewBehaviorTestLog;

namespace Paper2DPlusPreviewBehaviorTest
{
	bool PreviewBehavior_HasVisibleTextContaining(
		const TSharedRef<SWidget>& Widget,
		const bool bAncestorsVisible,
		const TCHAR* Substring)
	{
		FSlateAttributeMetaData::UpdateAllAttributes(
			Widget.Get(),
			FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidationIfConstructed);

		const bool bVisible = bAncestorsVisible && Widget->GetVisibility().IsVisible();
		if (bVisible && Widget->GetType() == FName(TEXT("STextBlock"))
			&& StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString().Contains(Substring))
		{
			return true;
		}
		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 Index = 0; Index < Children->Num(); ++Index)
			{
				if (PreviewBehavior_HasVisibleTextContaining(
					Children->GetChildAt(Index), bVisible, Substring))
				{
					return true;
				}
			}
		}
		return false;
	}

	/** One profile with one animation of the requested key-frame count. */
	struct FPreviewBehavior_Fixture
	{
		explicit FPreviewBehavior_Fixture(int32 FrameCount, FName AssetName = NAME_None)
		{
			Asset = NewObject<UPaper2DPlusCharacterProfileAsset>(
				GetTransientPackage(), AssetName, RF_Transactional);
			Asset->AddToRoot();
			FFlipbookProfileEntry& Entry = Asset->Flipbooks.AddDefaulted_GetRef();
			Entry.Identity.FlipbookName = TEXT("PreviewBehavior");
			UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Asset);
			{
				FScopedFlipbookMutator Mutator(Flipbook);
				Mutator.KeyFrames.AddDefaulted(FMath::Max(0, FrameCount));
			}
			Entry.Identity.Flipbook = Flipbook;
			Entry.CombatData.Frames.SetNum(FMath::Max(0, FrameCount));
		}

		explicit FPreviewBehavior_Fixture(UPaper2DPlusCharacterProfileAsset* ExistingAsset)
			: Asset(ExistingAsset)
		{
			check(Asset);
			Asset->AddToRoot();
		}

		~FPreviewBehavior_Fixture()
		{
			Editor.Reset();
			Model.Reset();
			if (Asset && Asset->IsRooted())
			{
				Asset->RemoveFromRoot();
			}
		}

		TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues() const
		{
			return Asset->Flipbooks[0].FrameEventData.FrameCues;
		}

		/** Builds the panel only after every placement exists, exactly as opening the tab would. */
		void OpenEditor()
		{
			Model = MakeShared<FCharacterProfileEditorModel>();
			Model->InitializeFromAsset(Asset);
			Model->SetSelectedFlipbook(0);
			Editor = SNew(SFrameEventEditor).Model(Model);
		}

		UPaper2DPlusCharacterProfileAsset* Asset = nullptr;
		TSharedPtr<FCharacterProfileEditorModel> Model;
		TSharedPtr<SFrameEventEditor> Editor;
	};

	/**
	 * Real on-disk profile used to prove the editor consumes the linker's deleted-native-class
	 * representation, rather than a hand-authored approximation of it.
	 */
	struct FPreviewBehavior_OrphanPackageFixture
	{
		FString Directory;
		FString PackageName;
		FString FilePath;

		FPreviewBehavior_OrphanPackageFixture()
		{
			const FString GuidString = FGuid::NewGuid().ToString(EGuidFormats::Digits);
			Directory = FString::Printf(
				TEXT("/Game/__AutomationTemp__/P2DPEditorOrphanNativeCue_%s"),
				*GuidString);
			PackageName = Directory / TEXT("OrphanProfile");
			FilePath = FPackageName::LongPackageNameToFilename(
				PackageName, FPackageName::GetAssetPackageExtension());
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
		}

		~FPreviewBehavior_OrphanPackageFixture()
		{
			PurgeFromMemory();

			const FString SafeRoot = FPaths::ConvertRelativePathToFull(
				FPaths::ProjectContentDir() / TEXT("__AutomationTemp__"));
			const FString FixtureDirectory =
				FPaths::ConvertRelativePathToFull(FPaths::GetPath(FilePath));
			if (!Directory.StartsWith(
					TEXT("/Game/__AutomationTemp__/P2DPEditorOrphanNativeCue_"))
				|| !FPaths::IsUnderDirectory(FixtureDirectory, SafeRoot))
			{
				return;
			}

			IFileManager& FileManager = IFileManager::Get();
			for (const FString& PackageFile : {
				FilePath,
				FPaths::ChangeExtension(FilePath, TEXT("uexp")),
				FPaths::ChangeExtension(FilePath, TEXT("ubulk")) })
			{
				if (FileManager.FileExists(*PackageFile))
				{
					FileManager.Delete(
						*PackageFile,
						/*RequireExists=*/false,
						/*EvenReadOnly=*/true,
						/*Quiet=*/true);
				}
			}
			FileManager.DeleteDirectory(
				*FixtureDirectory,
				/*RequireExists=*/false,
				/*Tree=*/true);
		}

		FPreviewBehavior_OrphanPackageFixture(
			const FPreviewBehavior_OrphanPackageFixture&) = delete;
		FPreviewBehavior_OrphanPackageFixture& operator=(
			const FPreviewBehavior_OrphanPackageFixture&) = delete;

		void PurgeFromMemory() const
		{
			if (UPackage* Resident = FindPackage(nullptr, *PackageName))
			{
				ForEachObjectWithOuter(
					Resident,
					[](UObject* Object)
					{
						Object->ClearFlags(RF_Public | RF_Standalone);
					},
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
					EGetObjectsFlags::IncludeNestedObjects);
#else
					/*bIncludeNestedObjects=*/true);
#endif
				Resident->ClearFlags(RF_Public | RF_Standalone);
				Resident->SetDirtyFlag(false);
				if (Resident->IsRooted())
				{
					Resident->RemoveFromRoot();
				}
				ResetLoaders(Resident);
				const FName TrashName = MakeUniqueObjectName(
					nullptr,
					UPackage::StaticClass(),
					TEXT("P2DPEditorOrphanNativeCueTrash"));
				Resident->Rename(
					*TrashName.ToString(),
					nullptr,
					REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
	};

	UPaper2DPlusPreviewBehaviorMomentCueTest* PreviewBehavior_AddMoment(
		FPreviewBehavior_Fixture& Fixture,
		int32 Frame,
		int32 Payload = 0)
	{
		UPaper2DPlusPreviewBehaviorMomentCueTest* Cue =
			NewObject<UPaper2DPlusPreviewBehaviorMomentCueTest>(
				Fixture.Asset, NAME_None, RF_Transactional);
		Cue->TriggerFrame = Frame;
		Cue->Payload = Payload;
		Fixture.Cues().Add(Cue);
		return Cue;
	}

	UPaper2DPlusPreviewBehaviorRangeCueTest* PreviewBehavior_AddRange(
		FPreviewBehavior_Fixture& Fixture,
		int32 StartFrame,
		int32 FrameCount,
		int32 Payload = 0)
	{
		UPaper2DPlusPreviewBehaviorRangeCueTest* Cue =
			NewObject<UPaper2DPlusPreviewBehaviorRangeCueTest>(
				Fixture.Asset, NAME_None, RF_Transactional);
		Cue->StartFrame = StartFrame;
		Cue->FrameCount = FrameCount;
		Cue->Payload = Payload;
		Fixture.Cues().Add(Cue);
		return Cue;
	}

	/**
	 * Creates the designer-authored shape that the native fixtures cannot cover: a generated Cue
	 * Blueprint whose declared event runs Unreal's stock Spawn Actor from Class node.
	 */
	UPaper2DPlusFrameCueBlueprint* PreviewBehavior_CreateBlueprintSpawnCue()
	{
		UObject* Outer = GetTransientPackage();
		UPaper2DPlusFrameCueBlueprint* Blueprint =
			Cast<UPaper2DPlusFrameCueBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UPaper2DPlusCue::StaticClass(),
				Outer,
				MakeUniqueObjectName(
					Outer,
					UPaper2DPlusFrameCueBlueprint::StaticClass(),
					TEXT("BP_P2DP_DesignerSpawnCue")),
				BPTYPE_Normal,
				UPaper2DPlusFrameCueBlueprint::StaticClass(),
				UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass(),
				NAME_None));
		if (!Blueprint
			|| !FPaper2DPlusFrameCueTypeEditor::PrepareNewlyCreatedBlueprint(*Blueprint))
		{
			return nullptr;
		}

		UK2Node_Event* EventNode =
			FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventNode(
				*Blueprint,
				TEXT("OnCueTriggered"));
		UEdGraph* BehaviorGraph = EventNode ? EventNode->GetGraph() : nullptr;
		if (!EventNode || !BehaviorGraph)
		{
			return nullptr;
		}
		for (const TObjectPtr<UEdGraphNode>& Node : BehaviorGraph->Nodes)
		{
			if (Node && Node->IsAutomaticallyPlacedGhostNode())
			{
				Node->SetEnabledState(ENodeEnabledState::Enabled, true);
			}
		}

		FGraphNodeCreator<UK2Node_SpawnActorFromClass> SpawnCreator(*BehaviorGraph);
		UK2Node_SpawnActorFromClass* SpawnNode = SpawnCreator.CreateNode(false);
		SpawnNode->NodePosX = EventNode->NodePosX + 320;
		SpawnNode->NodePosY = EventNode->NodePosY;
		// This node's PostPlacedNewNode reads the scale-method pin. The generic creator calls
		// PostPlacedNewNode before allocating pins, so seed them first just as the palette
		// template does for an interactively placed Spawn Actor node.
		SpawnNode->AllocateDefaultPins();
		SpawnCreator.Finalize();

		const UEdGraphSchema_K2* Schema =
			Cast<UEdGraphSchema_K2>(BehaviorGraph->GetSchema());
		UEdGraphPin* ClassPin = SpawnNode->GetClassPin();
		if (!Schema || !ClassPin)
		{
			return nullptr;
		}
		Schema->TrySetDefaultObject(
			*ClassPin,
			APaper2DPlusPreviewBehaviorVisibleActorTest::StaticClass());
		SpawnNode->PinDefaultValueChanged(ClassPin);
		ClassPin = SpawnNode->GetClassPin();

		// Use an ordinary Make Transform connection. The expanded Spawn Actor functions take the
		// transform by reference, so a hand-built literal does not reproduce the palette graph.
		FGraphNodeCreator<UK2Node_CallFunction> TransformCreator(*BehaviorGraph);
		UK2Node_CallFunction* TransformNode = TransformCreator.CreateNode(false);
		TransformNode->FunctionReference.SetExternalMember(
			GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, MakeTransform),
			UKismetMathLibrary::StaticClass());
		TransformNode->NodePosX = SpawnNode->NodePosX - 280;
		TransformNode->NodePosY = SpawnNode->NodePosY + 180;
		TransformCreator.Finalize();

		UEdGraphPin* TransformPin =
			SpawnNode->FindPin(TEXT("SpawnTransform"), EGPD_Input);
		UEdGraphPin* TransformValue = TransformNode->GetReturnValuePin();
		UEdGraphPin* EventThen = EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
		UEdGraphPin* SpawnExec = SpawnNode->GetExecPin();
		if (!ClassPin
			|| ClassPin->DefaultObject
				!= APaper2DPlusPreviewBehaviorVisibleActorTest::StaticClass()
			|| !TransformPin
			|| !TransformValue
			|| !EventThen
			|| !SpawnExec
			|| !Schema->TryCreateConnection(TransformValue, TransformPin)
			|| !Schema->TryCreateConnection(EventThen, SpawnExec))
		{
			return nullptr;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return Blueprint->Status == BS_Error ? nullptr : Blueprint;
	}

	/** Clears the shared log and the session error state a test starts and ends with. */
	struct FPreviewBehavior_ScopedSession
	{
		FPreviewBehavior_ScopedSession()
		{
			P2DPPreviewBehaviorLog::Reset();
			Paper2DPlusFrameCuePreviewBehavior::ResetSessionForTests();
			// Idempotent: the editor module already bound this at startup. Calling it here keeps the
			// test independent of module load order without installing a second handler.
			Paper2DPlusFrameCuePreviewBehavior::RegisterPreviewSoundHandler();
		}

		~FPreviewBehavior_ScopedSession()
		{
			P2DPPreviewBehaviorLog::Reset();
			Paper2DPlusFrameCuePreviewBehavior::ResetSessionForTests();
		}
	};

	/** In-memory only: proves automatic behavior preview has no project-adapter prerequisite. */
	struct FPreviewBehavior_ScopedEmptyAdapterRegistry
	{
		explicit FPreviewBehavior_ScopedEmptyAdapterRegistry(
			UClass* AdapterClass = nullptr)
		{
			UPaper2DPlusFrameCueEditorSettings* Settings =
				GetMutableDefault<UPaper2DPlusFrameCueEditorSettings>();
			SavedAdapters = Settings->PreviewAdapters;
			Settings->PreviewAdapters.Reset();
			if (AdapterClass)
			{
				Settings->PreviewAdapters.Add(
					TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>(AdapterClass));
			}
			Settings->NotifyPreviewAdaptersChanged();
		}

		~FPreviewBehavior_ScopedEmptyAdapterRegistry()
		{
			UPaper2DPlusFrameCueEditorSettings* Settings =
				GetMutableDefault<UPaper2DPlusFrameCueEditorSettings>();
			Settings->PreviewAdapters = SavedAdapters;
			Settings->NotifyPreviewAdaptersChanged();
		}

		TArray<TSoftClassPtr<UPaper2DPlusFrameCuePreviewAdapter>> SavedAdapters;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPreviewBehaviorScrubTest,
	"Paper2DPlus.FrameCues.Editor.Preview.Behavior.ScrubAndPlaybackRunBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPreviewBehaviorScrubTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusPreviewBehaviorTest;

	FPreviewBehavior_ScopedSession Session;
	FPreviewBehavior_Fixture Fixture(6);
	UPaper2DPlusPreviewBehaviorMomentCueTest* Moment = PreviewBehavior_AddMoment(Fixture, 2, 7);
	Fixture.OpenEditor();
	if (!TestNotNull(TEXT("Frame Cues panel"), Fixture.Editor.Get()))
	{
		return false;
	}

	// Scrubbing onto the cue's frame is one crossing; each later crossing runs the behavior again.
	Fixture.Editor->SeekFrameForTests(2);
	TestEqual(TEXT("Scrubbing onto a Cue runs its behavior once"),
		P2DPPreviewBehaviorLog::CountPhaseForCue(Moment, EPaper2DPlusFrameCuePhase::Trigger), 1);
	Fixture.Editor->SeekFrameForTests(4);
	TestEqual(TEXT("Scrubbing away from a Cue does not run it again"),
		P2DPPreviewBehaviorLog::CountPhaseForCue(Moment, EPaper2DPlusFrameCuePhase::Trigger), 1);
	Fixture.Editor->SeekFrameForTests(2);
	TestEqual(TEXT("Scrubbing back across a Cue runs its behavior again"),
		P2DPPreviewBehaviorLog::CountPhaseForCue(Moment, EPaper2DPlusFrameCuePhase::Trigger), 2);

	// Context identity is what a Blueprint branches on, so it is pinned on every record.
	for (const P2DPPreviewBehaviorLog::FRecord& Record : P2DPPreviewBehaviorLog::Records())
	{
		TestTrue(TEXT("Preview behavior always reports an editor-preview context"),
			Record.bIsEditorPreview);
		TestEqual(TEXT("Behavior reads its own placement's payload"), Record.Payload, 7);
		TestTrue(TEXT("Preview behavior receives a live isolated world through Cue self"),
			Record.bHadEditorPreviewWorld);
		TestTrue(TEXT("Preview behavior receives its subject actor and Profile Component"),
			Record.bHadPreviewSubject);
		TestTrue(TEXT("Preview behavior receives current animation identity"),
			Record.Flipbook.IsValid() && Record.AnimationName == TEXT("PreviewBehavior"));
	}
	TestNull(TEXT("The shared placement is worldless again after synchronous behavior"),
		Moment->GetWorld());

	// One playback pass across the whole animation runs the cue exactly once.
	P2DPPreviewBehaviorLog::Reset();
	Fixture.Editor->StopPlayback();
	for (int32 Frame = 0; Frame < 6; ++Frame)
	{
		Fixture.Editor->AdvancePlaybackFrameForTests(Frame);
	}
	TestEqual(TEXT("One playback pass runs a Cue's behavior exactly once"),
		P2DPPreviewBehaviorLog::CountPhaseForCue(Moment, EPaper2DPlusFrameCuePhase::Trigger), 1);

	// A looping second pass wraps back to frame zero and runs it exactly once again.
	P2DPPreviewBehaviorLog::Reset();
	for (int32 Frame = 0; Frame < 6; ++Frame)
	{
		Fixture.Editor->AdvancePlaybackFrameForTests(Frame);
	}
	TestEqual(TEXT("A second playback pass runs it exactly once again"),
		P2DPPreviewBehaviorLog::CountPhaseForCue(Moment, EPaper2DPlusFrameCuePhase::Trigger), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPreviewBehaviorAutomaticWorldTest,
	"Paper2DPlus.FrameCues.Editor.Preview.Behavior.SelectedCueUsesAutomaticIsolatedWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPreviewBehaviorAutomaticWorldTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusPreviewBehaviorTest;

	FPreviewBehavior_ScopedSession Session;
	FPreviewBehavior_ScopedEmptyAdapterRegistry EmptyRegistry;

	{
		FPreviewBehavior_Fixture Fixture(4);
		UPaper2DPlusPreviewBehaviorMomentCueTest* Moment =
			PreviewBehavior_AddMoment(Fixture, 1, 17);
		Moment->bSpawnActorInBehavior = true;
		Fixture.OpenEditor();
		P2DPPreviewBehaviorLog::Reset();
		Fixture.Editor->SelectCueForTests(0);

		const FPaper2DPlusFrameCuePreviewHost* Host = Fixture.Editor->GetPreviewHostForTests();
		if (!TestNotNull(TEXT("Automatic preview host"), Host))
		{
			return false;
		}
		TestEqual(TEXT("No registered adapter is needed"), Host->GetAdapterCount(), 0);
		TestEqual(TEXT("Preview Selected renders the Cue's anchor rather than a stale playhead"),
			Fixture.Editor->GetPreviewDisplayFrameIndexForTests(), 1);
		TestEqual(TEXT("Preview Selected executes Moment behavior"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(
				Moment, EPaper2DPlusFrameCuePhase::Trigger),
			1);
		if (P2DPPreviewBehaviorLog::Records().Num() == 1)
		{
			const P2DPPreviewBehaviorLog::FRecord& Record =
				P2DPPreviewBehaviorLog::Records()[0];
			TestTrue(TEXT("Cue self resolves the host's EditorPreview world during behavior"),
				Record.CueWorld.IsValid()
					&& Record.CueWorld.Get() == Host->GetPreviewWorld()
					&& Record.CueWorld->WorldType == EWorldType::EditorPreview);
			TestTrue(TEXT("Context identifies the isolated preview actor"),
				Record.OwningActor.Get() == Host->GetPreviewActor());
			TestTrue(TEXT("Context identifies the inert preview Profile Component"),
				Record.ProfileComponent.Get() == Host->GetProfileComponent());
			TestTrue(TEXT("Context carries the selected flipbook and animation name"),
				Record.Flipbook.Get() == Fixture.Asset->Flipbooks[0].Identity.Flipbook.Get()
					&& Record.AnimationName == TEXT("PreviewBehavior"));
		}
		TestTrue(TEXT("Ordinary SpawnActor behavior creates a visible preview-world resource"),
			P2DPPreviewBehaviorLog::SpawnedActors().Num() == 1
				&& P2DPPreviewBehaviorLog::SpawnedActors()[0].IsValid()
				&& Fixture.Editor->GetPreviewResourceCountForTests() > 0);
		const TWeakObjectPtr<AActor> Spawned =
			P2DPPreviewBehaviorLog::SpawnedActors().Num() == 1
				? P2DPPreviewBehaviorLog::SpawnedActors()[0]
				: TWeakObjectPtr<AActor>();
		TestNull(TEXT("Cue self is worldless again outside the callback"), Moment->GetWorld());
		Fixture.Editor->HandleHostDeactivated();
		TestFalse(TEXT("Tool teardown destroys behavior-spawned preview actors"), Spawned.IsValid());
		TestEqual(TEXT("Tool teardown leaves no preview resources"),
			Fixture.Editor->GetPreviewResourceCountForTests(), 0);
	}

	{
		// Native behavior alone can hide a broken generated-event path. Exercise the exact designer
		// shape: a Cue Type Blueprint, its declared event, and Unreal's stock Spawn Actor node.
		UPaper2DPlusFrameCueBlueprint* CueType =
			PreviewBehavior_CreateBlueprintSpawnCue();
		if (!TestNotNull(TEXT("Designer-authored spawning Cue Type compiles"), CueType))
		{
			return false;
		}
		if (!TestNotNull(
				TEXT("Designer-authored spawning Cue Type has a generated class"),
				CueType->GeneratedClass.Get()))
		{
			return false;
		}
		CueType->AddToRoot();
		ON_SCOPE_EXIT
		{
			if (CueType->IsRooted())
			{
				CueType->RemoveFromRoot();
			}
		};

		TestNotNull(
			TEXT("Generated Cue Type implements its declared behavior event"),
			CueType->GeneratedClass->FindFunctionByName(
				TEXT("OnCueTriggered"),
				EIncludeSuperFlag::ExcludeSuper));

		FPreviewBehavior_Fixture Fixture(4);
		UPaper2DPlusCue* Placement = NewObject<UPaper2DPlusCue>(
			Fixture.Asset,
			CueType->GeneratedClass,
			NAME_None,
			RF_Transactional);
		if (!TestNotNull(TEXT("Designer Cue Type placement"), Placement))
		{
			return false;
		}
		Placement->TriggerFrame = 1;
		Fixture.Cues().Add(Placement);
		Fixture.OpenEditor();
		Fixture.Editor->SelectCueForTests(0);

		const FPaper2DPlusFrameCuePreviewHost* Host =
			Fixture.Editor->GetPreviewHostForTests();
		if (!TestNotNull(TEXT("Designer Cue uses the automatic preview host"), Host)
			|| !TestNotNull(TEXT("Designer Cue uses an EditorPreview world"),
				Host ? Host->GetPreviewWorld() : nullptr))
		{
			return false;
		}

		// First match read directly off the iterator: a loop whose body ends in break leaves the
		// ++It increment unreachable, which UE 5.5's shared build env promotes to error C4702.
		TActorIterator<APaper2DPlusPreviewBehaviorVisibleActorTest> SpawnedIt(
			Host->GetPreviewWorld());
		APaper2DPlusPreviewBehaviorVisibleActorTest* SpawnedActor =
			SpawnedIt ? *SpawnedIt : nullptr;
		if (!TestNotNull(
				TEXT("the generated Cue graph actually spawns its Actor in the editor"),
				SpawnedActor))
		{
			return false;
		}
		UArrowComponent* VisibleArrow =
			SpawnedActor->FindComponentByClass<UArrowComponent>();
		TestTrue(
			TEXT("the designer-spawned Actor registers visible scene content"),
			VisibleArrow
				&& VisibleArrow->IsRegistered()
				&& VisibleArrow->IsVisible()
				&& VisibleArrow->ShouldRender());
		TestTrue(
			TEXT("the automatic host owns designer-spawned output without an adapter"),
			Host->GetAdapterCount() == 0
				&& Fixture.Editor->GetPreviewResourceCountForTests() > 0);

		const TWeakObjectPtr<AActor> SpawnedActorWeak = SpawnedActor;
		Fixture.Editor->HandleHostDeactivated();
		TestFalse(
			TEXT("designer-spawned output is destroyed when the Frame Cues tool deactivates"),
			SpawnedActorWeak.IsValid());
		TestEqual(
			TEXT("designer Cue teardown leaves no preview resources"),
			Fixture.Editor->GetPreviewResourceCountForTests(),
			0);
	}

	{
		P2DPPreviewBehaviorLog::Reset();
		FPreviewBehavior_Fixture Fixture(4);
		UPaper2DPlusPreviewBehaviorRangeCueTest* Range =
			PreviewBehavior_AddRange(Fixture, 1, 2, 21);
		Fixture.OpenEditor();
		P2DPPreviewBehaviorLog::Reset();
		Fixture.Editor->SelectCueForTests(0);
		TestEqual(TEXT("Cue State inspection renders its Begin anchor"),
			Fixture.Editor->GetPreviewDisplayFrameIndexForTests(), 1);
		TestEqual(TEXT("Preview Selected executes Cue State Begin"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(
				Range, EPaper2DPlusFrameCuePhase::Begin),
			1);
		TestEqual(TEXT("Selected Cue State joins the paired active ledger"),
			Fixture.Editor->GetActiveRangeCountForTests(), 1);
		TestEqual(TEXT("Preview Selected uses the selection evaluation mode"),
			Fixture.Editor->GetPreviewEvaluationModeForTests(),
			EPaper2DPlusFrameCueEvaluationMode::EditorSelectionPreview);
		Fixture.Editor->StopPlayback();
		TestEqual(TEXT("Clear/stop pairs selected Cue State Begin with End"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(
				Range, EPaper2DPlusFrameCuePhase::End),
			1);
		TestEqual(TEXT("Clearing a selection preview is an editor reset"),
			Fixture.Editor->GetLastPreviewEndReasonForTests(),
			EPaper2DPlusFrameCueEndReason::EditorReset);
		TestEqual(TEXT("Selection cleanup retains the outgoing evaluation mode"),
			Fixture.Editor->GetLastPreviewEndEvaluationModeForTests(),
			EPaper2DPlusFrameCueEvaluationMode::EditorSelectionPreview);
		TestEqual(TEXT("Selection cleanup retains the anchor frame"),
			Fixture.Editor->GetLastPreviewEndFrameForTests(), 1);
		TestEqual(TEXT("Clear/stop empties the selected Cue State ledger"),
			Fixture.Editor->GetActiveRangeCountForTests(), 0);
	}

	{
		// A backward scrub is two transactions: tear down the outgoing world, then dispatch the target.
		// The target's automatic output must survive that reset instead of being erased by a delayed
		// cleanup from the frame we left.
		P2DPPreviewBehaviorLog::Reset();
		FPreviewBehavior_Fixture Fixture(6);
		UPaper2DPlusPreviewBehaviorMomentCueTest* Target =
			PreviewBehavior_AddMoment(Fixture, 0, 33);
		Target->bSpawnActorInBehavior = true;
		Fixture.OpenEditor();
		Fixture.Editor->SeekFrameForTests(3);
		P2DPPreviewBehaviorLog::Reset();
		Fixture.Editor->SeekFrameForTests(0);
		TestEqual(TEXT("Backward seek dispatches the target Cue exactly once"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(
				Target, EPaper2DPlusFrameCuePhase::Trigger),
			1);
		TestTrue(TEXT("Backward seek preserves the target Cue's spawned actor"),
			P2DPPreviewBehaviorLog::SpawnedActors().Num() == 1
				&& P2DPPreviewBehaviorLog::SpawnedActors()[0].IsValid()
				&& Fixture.Editor->GetPreviewResourceCountForTests() > 0);
		Fixture.Editor->StopPlayback();
		TestEqual(TEXT("Stopping after the backward seek clears target output"),
			Fixture.Editor->GetPreviewResourceCountForTests(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPreviewBehaviorTeardownTest,
	"Paper2DPlus.FrameCues.Editor.Preview.Behavior.EveryTeardownDeliversEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPreviewBehaviorTeardownTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusPreviewBehaviorTest;

	FPreviewBehavior_ScopedSession Session;
	FPreviewBehavior_ScopedEmptyAdapterRegistry EmptyRegistry;

	// Scrub backward out of an active range.
	{
		FPreviewBehavior_Fixture Fixture(6);
		UPaper2DPlusPreviewBehaviorRangeCueTest* Range = PreviewBehavior_AddRange(Fixture, 1, 4);
		Fixture.OpenEditor();
		Fixture.Editor->SeekFrameForTests(2);
		TestEqual(TEXT("Entering a Cue State runs Begin"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(Range, EPaper2DPlusFrameCuePhase::Begin), 1);
		TestEqual(TEXT("The range is active before the scrub back"),
			Fixture.Editor->GetActiveRangeCountForTests(), 1);
		TestEqual(TEXT("Manual frame selection uses scrub/seek evaluation"),
			Fixture.Editor->GetPreviewEvaluationModeForTests(),
			EPaper2DPlusFrameCueEvaluationMode::EditorScrubSeek);
		Fixture.Editor->SeekFrameForTests(0);
		TestEqual(TEXT("Scrubbing back out of a Cue State delivers End to behavior"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(Range, EPaper2DPlusFrameCuePhase::End), 1);
		TestEqual(TEXT("Backward scrub keeps EditorReset semantics"),
			Fixture.Editor->GetLastPreviewEndReasonForTests(),
			EPaper2DPlusFrameCueEndReason::EditorReset);
		TestEqual(TEXT("Backward scrub End retains the outgoing scrub mode"),
			Fixture.Editor->GetLastPreviewEndEvaluationModeForTests(),
			EPaper2DPlusFrameCueEvaluationMode::EditorScrubSeek);
		TestEqual(TEXT("Backward scrub End retains its outgoing source frame"),
			Fixture.Editor->GetLastPreviewEndFrameForTests(), 2);
		TestEqual(TEXT("Scrubbing back leaves no active range"),
			Fixture.Editor->GetActiveRangeCountForTests(), 0);
	}

	// Stop playback while inside a range.
	{
		P2DPPreviewBehaviorLog::Reset();
		FPreviewBehavior_Fixture Fixture(6);
		UPaper2DPlusPreviewBehaviorRangeCueTest* Range = PreviewBehavior_AddRange(Fixture, 0, 6);
		Fixture.OpenEditor();
		Fixture.Editor->AdvancePlaybackFrameForTests(1);
		TestEqual(TEXT("Playback into a Cue State runs Begin"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(Range, EPaper2DPlusFrameCuePhase::Begin), 1);
		TestEqual(TEXT("Playback transition uses playback evaluation"),
			Fixture.Editor->GetPreviewEvaluationModeForTests(),
			EPaper2DPlusFrameCueEvaluationMode::EditorPlayback);
		Fixture.Editor->StopPlayback();
		TestEqual(TEXT("Stopping playback delivers End to behavior"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(Range, EPaper2DPlusFrameCuePhase::End), 1);
		TestEqual(TEXT("Stopping playback reports PlaybackStopped"),
			Fixture.Editor->GetLastPreviewEndReasonForTests(),
			EPaper2DPlusFrameCueEndReason::PlaybackStopped);
		TestEqual(TEXT("Playback stop End retains playback evaluation"),
			Fixture.Editor->GetLastPreviewEndEvaluationModeForTests(),
			EPaper2DPlusFrameCueEvaluationMode::EditorPlayback);
		TestEqual(TEXT("Playback stop End retains the stopped source frame"),
			Fixture.Editor->GetLastPreviewEndFrameForTests(), 1);
		Fixture.Editor->StopPlayback();
		TestEqual(TEXT("Repeated Stop cannot emit a second End"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(
				Range, EPaper2DPlusFrameCuePhase::End),
			1);
		TestEqual(TEXT("Stopping playback leaves no active range"),
			Fixture.Editor->GetActiveRangeCountForTests(), 0);
	}

	// Boundary arrow navigation on a one-frame animation is still a playback boundary. There is no
	// destination frame to click, so the key handler itself must pair the active Cue State.
	for (const FKey& BoundaryKey : {EKeys::Left, EKeys::Right})
	{
		P2DPPreviewBehaviorLog::Reset();
		FPreviewBehavior_Fixture Fixture(1);
		UPaper2DPlusPreviewBehaviorRangeCueTest* Range =
			PreviewBehavior_AddRange(Fixture, 0, 1);
		Fixture.OpenEditor();
		Fixture.Editor->SelectCueForTests(0);
		TestEqual(TEXT("One-frame selection preview begins its Cue State"),
			Fixture.Editor->GetActiveRangeCountForTests(), 1);

		const FKeyEvent KeyEvent(
			BoundaryKey,
			FModifierKeysState(),
			/*UserIndex=*/0,
			/*bInIsRepeat=*/false,
			/*InCharacterCode=*/0,
			/*InKeyCode=*/0);
		TestTrue(TEXT("One-frame boundary arrow remains handled"),
			Fixture.Editor->OnKeyDown(FGeometry(), KeyEvent).IsEventHandled());
		TestEqual(TEXT("One-frame boundary arrow pairs the active Cue State"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(
				Range, EPaper2DPlusFrameCuePhase::End),
			1);
		TestEqual(TEXT("One-frame boundary arrow reports PlaybackStopped"),
			Fixture.Editor->GetLastPreviewEndReasonForTests(),
			EPaper2DPlusFrameCueEndReason::PlaybackStopped);
		TestEqual(TEXT("One-frame boundary arrow retains selection evaluation"),
			Fixture.Editor->GetLastPreviewEndEvaluationModeForTests(),
			EPaper2DPlusFrameCueEvaluationMode::EditorSelectionPreview);
		TestEqual(TEXT("One-frame boundary arrow clears the active range"),
			Fixture.Editor->GetActiveRangeCountForTests(), 0);
	}

	// Switch away from the tool (the same path the tab close takes) while inside a range.
	{
		P2DPPreviewBehaviorLog::Reset();
		FPreviewBehavior_Fixture Fixture(6);
		UPaper2DPlusPreviewBehaviorRangeCueTest* Range = PreviewBehavior_AddRange(Fixture, 0, 6);
		Fixture.OpenEditor();
		Fixture.Editor->SeekFrameForTests(3);
		TestEqual(TEXT("Seeking into a Cue State runs Begin"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(Range, EPaper2DPlusFrameCuePhase::Begin), 1);
		Fixture.Editor->HandleHostDeactivated();
		TestEqual(TEXT("Switching tools delivers End to behavior"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(Range, EPaper2DPlusFrameCuePhase::End), 1);
		TestEqual(TEXT("Tool teardown retains EditorReset"),
			Fixture.Editor->GetLastPreviewEndReasonForTests(),
			EPaper2DPlusFrameCueEndReason::EditorReset);
		TestEqual(TEXT("Tool teardown retains the outgoing scrub mode"),
			Fixture.Editor->GetLastPreviewEndEvaluationModeForTests(),
			EPaper2DPlusFrameCueEvaluationMode::EditorScrubSeek);
		TestEqual(TEXT("Switching tools leaves no active range"),
			Fixture.Editor->GetActiveRangeCountForTests(), 0);
		TestEqual(TEXT("Every teardown reason ends with a paired lifecycle"),
			P2DPPreviewBehaviorLog::CountPhase(EPaper2DPlusFrameCuePhase::Begin),
			P2DPPreviewBehaviorLog::CountPhase(EPaper2DPlusFrameCuePhase::End));
	}

	// Switching the selected animation is distinct from stopping playback or replacing the Profile.
	{
		P2DPPreviewBehaviorLog::Reset();
		FPreviewBehavior_Fixture Fixture(6, TEXT("PreviewAnimationSwitch"));
		UPaper2DPlusPreviewBehaviorRangeCueTest* Range =
			PreviewBehavior_AddRange(Fixture, 0, 6);
		FFlipbookProfileEntry& Destination =
			Fixture.Asset->Flipbooks.AddDefaulted_GetRef();
		Destination.Identity.FlipbookName = TEXT("DestinationAnimation");
		UPaperFlipbook* DestinationFlipbook =
			NewObject<UPaperFlipbook>(Fixture.Asset);
		{
			FScopedFlipbookMutator Mutator(DestinationFlipbook);
			Mutator.KeyFrames.AddDefaulted(6);
		}
		Destination.Identity.Flipbook = DestinationFlipbook;
		Destination.CombatData.Frames.SetNum(6);
		Fixture.OpenEditor();
		Fixture.Editor->SeekFrameForTests(2);
		Fixture.Model->SetSelectedFlipbook(1);
		TestEqual(TEXT("Animation selection change pairs the outgoing Cue State"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(
				Range, EPaper2DPlusFrameCuePhase::End),
			1);
		TestEqual(TEXT("Animation selection change reports AnimationChanged"),
			Fixture.Editor->GetLastPreviewEndReasonForTests(),
			EPaper2DPlusFrameCueEndReason::AnimationChanged);
		TestEqual(TEXT("Animation selection change retains outgoing scrub evaluation"),
			Fixture.Editor->GetLastPreviewEndEvaluationModeForTests(),
			EPaper2DPlusFrameCueEvaluationMode::EditorScrubSeek);
		TestEqual(TEXT("Animation selection change retains outgoing source frame"),
			Fixture.Editor->GetLastPreviewEndFrameForTests(), 2);
	}

	// A model Profile swap is published after the model has adopted its destination asset. The
	// outgoing End must still carry the source animation identity that received Begin.
	{
		P2DPPreviewBehaviorLog::Reset();
		FPreviewBehavior_Fixture Destination(6, TEXT("PreviewBehaviorDestination"));
		Destination.Asset->Flipbooks[0].Identity.FlipbookName = TEXT("DestinationAnimation");
		FPreviewBehavior_Fixture Fixture(6, TEXT("PreviewBehaviorSource"));
		UPaper2DPlusPreviewBehaviorRangeCueTest* Range =
			PreviewBehavior_AddRange(Fixture, 0, 6);
		UPaperFlipbook* SourceFlipbook =
			Fixture.Asset->Flipbooks[0].Identity.Flipbook.Get();
		Fixture.OpenEditor();
		Fixture.Editor->SeekFrameForTests(2);
		Fixture.Model->InitializeFromAsset(Destination.Asset);
		TestEqual(TEXT("Profile swap delivers one outgoing End"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(
				Range, EPaper2DPlusFrameCuePhase::End),
			1);
		TestEqual(TEXT("Profile replacement is a source-removal boundary"),
			Fixture.Editor->GetLastPreviewEndReasonForTests(),
			EPaper2DPlusFrameCueEndReason::SourceRemoved);
		TestEqual(TEXT("Profile replacement retains the outgoing scrub mode"),
			Fixture.Editor->GetLastPreviewEndEvaluationModeForTests(),
			EPaper2DPlusFrameCueEvaluationMode::EditorScrubSeek);
		TestEqual(TEXT("Profile replacement retains the outgoing source frame"),
			Fixture.Editor->GetLastPreviewEndFrameForTests(), 2);
		const P2DPPreviewBehaviorLog::FRecord* EndRecord =
			P2DPPreviewBehaviorLog::Records().FindByPredicate(
				[Range](const P2DPPreviewBehaviorLog::FRecord& Record)
				{
					return Record.Cue == Range
						&& Record.Phase == EPaper2DPlusFrameCuePhase::End;
				});
		if (TestNotNull(TEXT("Profile swap End record"), EndRecord))
		{
			TestTrue(TEXT("Profile swap End preserves the source flipbook and animation name"),
				EndRecord->Flipbook.Get() == SourceFlipbook
					&& EndRecord->AnimationName == TEXT("PreviewBehavior"));
		}
	}

	// Blueprint compile and adapter-registry replacement share the hard-reset hook. Both must pair
	// the live Cue State before replacing the isolated world, and preview must re-enter cleanly.
	{
		P2DPPreviewBehaviorLog::Reset();
		FPreviewBehavior_Fixture Fixture(6);
		UPaper2DPlusPreviewBehaviorRangeCueTest* Range =
			PreviewBehavior_AddRange(Fixture, 0, 6);
		Fixture.OpenEditor();
		Fixture.Editor->SelectCueForTests(0);
		const FPaper2DPlusFrameCuePreviewHost* Host =
			Fixture.Editor->GetPreviewHostForTests();
		const TWeakObjectPtr<UWorld> WorldBeforeCompile =
			Host ? Host->GetPreviewWorld() : nullptr;
		if (!TestNotNull(TEXT("Editor exists for compile lifecycle"), GEditor))
		{
			return false;
		}
		UBlueprint* CompileFixture =
			NewObject<UBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
		// The classless fixture is unresolvable, which the scoped compile reset treats as relevant,
		// so the hard-reset lifecycle below still fires under the narrowed compile contract.
		GEditor->OnBlueprintPreCompile().Broadcast(CompileFixture);
		TestEqual(TEXT("Blueprint precompile pairs the active Cue State exactly once"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(
				Range, EPaper2DPlusFrameCuePhase::End),
			1);
		TestEqual(TEXT("Blueprint compile remains an EditorReset boundary"),
			Fixture.Editor->GetLastPreviewEndReasonForTests(),
			EPaper2DPlusFrameCueEndReason::EditorReset);
		TestEqual(TEXT("Blueprint compile End retains selection evaluation"),
			Fixture.Editor->GetLastPreviewEndEvaluationModeForTests(),
			EPaper2DPlusFrameCueEvaluationMode::EditorSelectionPreview);
		TestEqual(TEXT("Blueprint precompile clears the active range ledger"),
			Fixture.Editor->GetActiveRangeCountForTests(), 0);
		TestFalse(TEXT("Blueprint precompile retires the dirty preview world"),
			WorldBeforeCompile.IsValid());
		GEditor->OnBlueprintCompiled().Broadcast();

		Fixture.Editor->SelectCueForTests(0);
		TestEqual(TEXT("Preview re-enters once after Blueprint compile"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(
				Range, EPaper2DPlusFrameCuePhase::Begin),
			2);
		GetMutableDefault<UPaper2DPlusFrameCueEditorSettings>()
			->NotifyPreviewAdaptersChanged();
		TestEqual(TEXT("Registry replacement pairs the re-entered Cue State once"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(
				Range, EPaper2DPlusFrameCuePhase::End),
			2);
		TestEqual(TEXT("Registry replacement clears the active range ledger"),
			Fixture.Editor->GetActiveRangeCountForTests(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPreviewBehaviorReentrantResetTest,
	"Paper2DPlus.FrameCues.Editor.Preview.Behavior.ReentrantResetPairsActiveState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPreviewBehaviorReentrantResetTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusPreviewBehaviorTest;

	FPreviewBehavior_ScopedSession Session;
	UPaper2DPlusFrameCueRegistryReentryAdapterTest::ResetFixture();
	ON_SCOPE_EXIT
	{
		UPaper2DPlusFrameCueRegistryReentryAdapterTest::ResetFixture();
	};
	FPreviewBehavior_ScopedEmptyAdapterRegistry Registry(
		UPaper2DPlusFrameCueRegistryReentryAdapterTest::StaticClass());
	FPreviewBehavior_Fixture Fixture(6);
	UPaper2DPlusEditorTestRangeCue* Range =
		NewObject<UPaper2DPlusEditorTestRangeCue>(
			Fixture.Asset, NAME_None, RF_Transactional);
	Range->StartFrame = 0;
	Range->FrameCount = 6;
	Fixture.Cues().Add(Range);
	Fixture.OpenEditor();
	UPaper2DPlusFrameCueRegistryReentryAdapterTest::TrackedContext =
		Fixture.Editor->GetPreviewContextForTests();
	UPaper2DPlusFrameCueRegistryReentryAdapterTest::bBroadcastOnPreview = true;

	Fixture.Editor->SelectCueForTests(0);
	TestTrue(TEXT("Reentrant adapter executes the selection preview"),
		UPaper2DPlusFrameCueRegistryReentryAdapterTest::PreviewCallCount >= 1);
	TestEqual(TEXT("Reentrant registry reset drains the active range ledger"),
		Fixture.Editor->GetActiveRangeCountForTests(), 0);
	TestEqual(TEXT("Reentrant registry reset leaves no owned preview resources"),
		Fixture.Editor->GetPreviewResourceCountForTests(), 0);
	TestEqual(TEXT("Reentrant registry reset retains EditorReset"),
		Fixture.Editor->GetLastPreviewEndReasonForTests(),
		EPaper2DPlusFrameCueEndReason::EditorReset);
	TestEqual(TEXT("Reentrant registry reset retains selection evaluation"),
		Fixture.Editor->GetLastPreviewEndEvaluationModeForTests(),
		EPaper2DPlusFrameCueEvaluationMode::EditorSelectionPreview);
	TestEqual(TEXT("Reentrant registry reset retains the selection anchor frame"),
		Fixture.Editor->GetLastPreviewEndFrameForTests(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPreviewBehaviorSkipFlagTest,
	"Paper2DPlus.FrameCues.Editor.Preview.Behavior.SkipInEditorPreviewSuppressesOnlyPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPreviewBehaviorSkipFlagTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusPreviewBehaviorTest;

	FPreviewBehavior_ScopedSession Session;
	FPreviewBehavior_Fixture Fixture(6);
	UPaper2DPlusPreviewBehaviorMomentCueTest* Skipped = PreviewBehavior_AddMoment(Fixture, 2, 1);
	UPaper2DPlusPreviewBehaviorMomentCueTest* Dispatched = PreviewBehavior_AddMoment(Fixture, 2, 2);
#if WITH_EDITORONLY_DATA
	Skipped->bSkipInEditorPreview = true;
#endif
	Fixture.OpenEditor();

	Fixture.Editor->SeekFrameForTests(2);
	TestEqual(TEXT("A placement opted out of preview does not run its behavior"),
		P2DPPreviewBehaviorLog::CountPhaseForCue(Skipped, EPaper2DPlusFrameCuePhase::Trigger), 0);
	TestEqual(TEXT("Its neighbour on the same frame still runs"),
		P2DPPreviewBehaviorLog::CountPhaseForCue(Dispatched, EPaper2DPlusFrameCuePhase::Trigger), 1);

	// The same placements through the shared game dispatch core: the opt-out is preview-only.
	P2DPPreviewBehaviorLog::Reset();
	const TArray<TObjectPtr<UPaper2DPlusCueBase>> Placements = Fixture.Cues();
	TSet<TObjectPtr<UPaper2DPlusCueBase>> ActiveRanges;
	FPaper2DPlusFrameCueContext GameContext;
	GameContext.CurrentFrame = 2;
	GameContext.PreviousFrame = 1;
	Paper2DPlusFrameCues::DispatchFrameTransition(
		Placements,
		GameContext,
		ActiveRanges,
		[](UPaper2DPlusCueBase&) { return true; },
		[](UPaper2DPlusCueBase& Cue, const FPaper2DPlusFrameCueContext& Context)
		{
			Paper2DPlusFrameCueBehavior::ExecuteCueBehavior(Cue, Context);
		});
	TestEqual(TEXT("Game dispatch still runs the behavior of a preview-skipped placement"),
		P2DPPreviewBehaviorLog::CountPhaseForCue(Skipped, EPaper2DPlusFrameCuePhase::Trigger), 1);
	TestEqual(TEXT("Game dispatch runs both placements on the frame"),
		P2DPPreviewBehaviorLog::Records().Num(), 2);
	for (const P2DPPreviewBehaviorLog::FRecord& Record : P2DPPreviewBehaviorLog::Records())
	{
		TestFalse(TEXT("Game dispatch reports a game context to behavior"), Record.bIsEditorPreview);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPreviewBehaviorErrorContainmentTest,
	"Paper2DPlus.FrameCues.Editor.Preview.Behavior.ErrorLogsOnceBadgesAndKeepsRunning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPreviewBehaviorErrorContainmentTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusPreviewBehaviorTest;

	FPreviewBehavior_ScopedSession Session;

	// The containment report is the observable contract: exactly one for the session, however many
	// times the behavior fails. The Blueprint VM's own warning for each failure is expected too, but
	// deliberately uncounted.
	AddExpectedError(
		TEXT("Frame Cue behavior failed in editor preview"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	AddExpectedError(
		TEXT("Paper2DPlus preview behavior fixture failure"),
		EAutomationExpectedErrorFlags::Contains,
		0);

	FPreviewBehavior_Fixture Fixture(6);
	UPaper2DPlusPreviewBehaviorMomentCueTest* Failing = PreviewBehavior_AddMoment(Fixture, 2, 5);
	Failing->bRaiseBehaviorError = true;
	UPaper2DPlusPreviewBehaviorMomentCueTest* Healthy = PreviewBehavior_AddMoment(Fixture, 4, 6);
	Fixture.OpenEditor();

	Fixture.Editor->SeekFrameForTests(2);
	TestTrue(TEXT("A failed behavior badges its own placement"),
		Paper2DPlusFrameCuePreviewBehavior::GetPlacementBadge(Failing).bHasError);
	TestFalse(TEXT("A healthy placement is not badged"),
		Paper2DPlusFrameCuePreviewBehavior::GetPlacementBadge(Healthy).bHasError);
	TestFalse(TEXT("The badge carries a reason for the designer"),
		Paper2DPlusFrameCuePreviewBehavior::GetPlacementBadge(Failing).ToolTip.IsEmpty());

	// Failing again is neither re-reported nor quarantined: the behavior still runs every crossing.
	Fixture.Editor->SeekFrameForTests(0);
	Fixture.Editor->SeekFrameForTests(2);
	Fixture.Editor->SeekFrameForTests(0);
	Fixture.Editor->SeekFrameForTests(2);
	TestEqual(TEXT("A failing behavior keeps executing on later crossings"),
		P2DPPreviewBehaviorLog::CountPhaseForCue(Failing, EPaper2DPlusFrameCuePhase::Trigger), 3);
	TestEqual(TEXT("Only the failing placement is badged"),
		Paper2DPlusFrameCuePreviewBehavior::GetBadgedPlacementCountForTests(), 1);

	// Preview dispatch keeps working for everything else after the failure.
	Fixture.Editor->SeekFrameForTests(4);
	TestEqual(TEXT("Preview dispatch keeps working after a behavior failure"),
		P2DPPreviewBehaviorLog::CountPhaseForCue(Healthy, EPaper2DPlusFrameCuePhase::Trigger), 1);

	// Most preview failures are DATA problems on the placement — a payload variable left at zero —
	// which the designer fixes without recompiling the Cue Type, so the placement keeps its identity.
	// The badge has to retire on the next clean run, or it advertises a failure that no longer happens
	// until the editor is restarted (the only clear path used to be a visual-tour-only helper).
	Failing->bRaiseBehaviorError = false;
	Fixture.Editor->SeekFrameForTests(0);
	Fixture.Editor->SeekFrameForTests(2);
	TestFalse(TEXT("A clean re-dispatch retires the placement's error badge"),
		Paper2DPlusFrameCuePreviewBehavior::GetPlacementBadge(Failing).bHasError);
	TestTrue(TEXT("Retiring the badge carries its stale message away with it"),
		Paper2DPlusFrameCuePreviewBehavior::GetPlacementBadge(Failing).ToolTip.IsEmpty());
	TestEqual(TEXT("No badges remain once every placement runs clean"),
		Paper2DPlusFrameCuePreviewBehavior::GetBadgedPlacementCountForTests(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPreviewBehaviorSoundTest,
	"Paper2DPlus.FrameCues.Editor.Preview.Behavior.ContextAwareSoundUsesPreviewPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPreviewBehaviorSoundTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusPreviewBehaviorTest;

	FPreviewBehavior_ScopedSession Session;
	TestTrue(TEXT("The editor binds the runtime's preview-sound seam"),
		Paper2DPlusFrameCueBehavior::IsPreviewSoundHandlerBound());

	FPreviewBehavior_Fixture Fixture(6);
	UPaper2DPlusPreviewBehaviorMomentCueTest* Cue = PreviewBehavior_AddMoment(Fixture, 3, 1);
	USoundWave* Sound = NewObject<USoundWave>(Fixture.Asset);
	Cue->BehaviorSound = Sound;
	Cue->SoundVolume = 0.5f;
	Cue->SoundPitch = 1.25f;
	Fixture.OpenEditor();

	Fixture.Editor->SeekFrameForTests(3);
	TestEqual(TEXT("A sound cue's behavior routes exactly one preview-sound request"),
		Paper2DPlusFrameCuePreviewBehavior::GetPreviewSoundRequestCountForTests(), 1);
	const Paper2DPlusFrameCuePreviewBehavior::FPreviewSoundRequestRecord Request =
		Paper2DPlusFrameCuePreviewBehavior::GetLastPreviewSoundRequestForTests();
	TestTrue(TEXT("The request carries the placement that asked for it"), Request.Cue == Cue);
	TestTrue(TEXT("The request carries the authored sound"), Request.Sound == Sound);
	TestEqual(TEXT("The request carries the authored volume"), Request.VolumeMultiplier, 0.5f);
	TestEqual(TEXT("The request carries the authored pitch"), Request.PitchMultiplier, 1.25f);
	// Ledger ownership requires a real world-owned audio component, which the engine refuses
	// to create while all audio is disabled (-nosound / audio-less CI hosts). The request
	// routing above is audio-independent and stays asserted everywhere.
	if (GEngine != nullptr && GEngine->UseSound())
	{
		TestTrue(TEXT("The preview sound is owned by the tab's preview resource ledger"),
			Request.bRoutedToPreviewLedger);
	}
	else
	{
		AddInfo(TEXT(
			"Audio is disabled on this host (-nosound): the ledger-ownership assertion needs "
			"a real audio component and was skipped."));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDeletedNativeClassLoadedSlotEditorOpenTest,
	"Paper2DPlus.FrameCues.Editor.Orphan.DeletedNativeClassLoadedSlotOpensSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDeletedNativeClassLoadedSlotEditorOpenTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusPreviewBehaviorTest;

	FPreviewBehavior_ScopedSession Session;
	FPreviewBehavior_OrphanPackageFixture PackageFixture;

	// Save two different native test Cue classes into a real Character Profile package.
	{
		UPackage* Package = CreatePackage(*PackageFixture.PackageName);
		if (!TestNotNull(TEXT("The editor orphan fixture package can be created"), Package))
		{
			return false;
		}
		Package->AddToRoot();
		ON_SCOPE_EXIT{ Package->RemoveFromRoot(); };

		UPaper2DPlusCharacterProfileAsset* Profile =
			NewObject<UPaper2DPlusCharacterProfileAsset>(
				Package,
				TEXT("OrphanProfile"),
				RF_Public | RF_Standalone);
		UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(
			Package,
			TEXT("OrphanFlipbook"),
			RF_Public | RF_Standalone);
		{
			FScopedFlipbookMutator Mutator(Flipbook);
			Mutator.KeyFrames.AddDefaulted(6);
		}

		FFlipbookProfileEntry& Entry = Profile->Flipbooks.AddDefaulted_GetRef();
		Entry.Identity.FlipbookName = TEXT("DeletedNativeClassAttackFixture");
		Entry.Identity.Flipbook = Flipbook;
		Entry.CombatData.Frames.SetNum(6);

		auto AddDoomed = [&Entry, Profile](const int32 StartFrame, const int32 Payload)
		{
			UPaper2DPlusPreviewBehaviorRangeCueTest* Doomed =
				NewObject<UPaper2DPlusPreviewBehaviorRangeCueTest>(Profile);
			Doomed->StartFrame = StartFrame;
			Doomed->FrameCount = 1;
			Doomed->Payload = Payload;
			Entry.FrameEventData.FrameCues.Add(Doomed);
		};
		auto AddSibling = [&Entry, Profile](const int32 TriggerFrame, const int32 Payload)
		{
			UPaper2DPlusPreviewBehaviorMomentCueTest* Sibling =
				NewObject<UPaper2DPlusPreviewBehaviorMomentCueTest>(Profile);
			Sibling->TriggerFrame = TriggerFrame;
			Sibling->Payload = Payload;
			Entry.FrameEventData.FrameCues.Add(Sibling);
		};
		AddDoomed(0, 5);
		AddSibling(2, 91);
		AddDoomed(3, 6);
		AddSibling(4, 37);
		AddDoomed(5, 7);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!TestTrue(
				TEXT("The editor orphan fixture package is written to disk"),
				UPackage::SavePackage(
					Package,
					Profile,
					*PackageFixture.FilePath,
					SaveArgs)))
		{
			return false;
		}
	}

	PackageFixture.PurgeFromMemory();
	if (!TestNull(
			TEXT("The saved editor orphan fixture is evicted before reload"),
			FindPackage(nullptr, *PackageFixture.PackageName)))
	{
		return false;
	}

	// Test-local only: make the saved Range fixture's /Script import resolve to a class that is
	// absent from the loaded editor module. Shipping config still contains no redirect.
	const TArray<FCoreRedirect> AbsentClassRedirect = {
		FCoreRedirect(
			ECoreRedirectFlags::Type_Class,
			TEXT(
				"/Script/Paper2DPlusEditor."
				"Paper2DPlusPreviewBehaviorRangeCueTest"),
			TEXT(
				"/Script/Paper2DPlusEditor."
				"Paper2DPlusAbsentNativeCueForEditorOrphanTest"))
	};
	const FString RedirectSource(TEXT("Paper2DPlusEditorOrphanNativeCueTest"));
	UPackage* Reloaded = nullptr;
	{
		FCoreRedirects::AddRedirectList(AbsentClassRedirect, RedirectSource);
		ON_SCOPE_EXIT
		{
			FCoreRedirects::RemoveRedirectList(AbsentClassRedirect, RedirectSource);
		};
		Reloaded = LoadPackage(nullptr, *PackageFixture.PackageName, LOAD_None);
	}
	if (!TestNotNull(
			TEXT("The Character Profile package loads with an absent native Cue class"),
			Reloaded))
	{
		return false;
	}
	Reloaded->AddToRoot();
	ON_SCOPE_EXIT
	{
		if (Reloaded && Reloaded->IsRooted())
		{
			Reloaded->RemoveFromRoot();
		}
	};

	UPaper2DPlusCharacterProfileAsset* ReloadedProfile =
		FindObject<UPaper2DPlusCharacterProfileAsset>(Reloaded, TEXT("OrphanProfile"));
	if (!TestNotNull(TEXT("The reloaded Character Profile survives"), ReloadedProfile))
	{
		return false;
	}
	if (!TestEqual(
			TEXT("The reloaded Character Profile keeps one animation"),
			ReloadedProfile->Flipbooks.Num(),
			1))
	{
		return false;
	}

	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Placements =
		ReloadedProfile->Flipbooks[0].FrameEventData.FrameCues;
	if (!TestEqual(
			TEXT("The reloaded animation preserves all five placement slots"),
			Placements.Num(),
			5))
	{
		return false;
	}
	TestNull(TEXT("the first absent Cue reloads as null"), Placements[0].Get());
	TestNull(TEXT("the second absent Cue reloads as null"), Placements[2].Get());
	TestNull(TEXT("the third absent Cue reloads as null"), Placements[4].Get());
	UPaper2DPlusPreviewBehaviorMomentCueTest* SiblingA =
		Cast<UPaper2DPlusPreviewBehaviorMomentCueTest>(Placements[1].Get());
	UPaper2DPlusPreviewBehaviorMomentCueTest* SiblingB =
		Cast<UPaper2DPlusPreviewBehaviorMomentCueTest>(Placements[3].Get());
	if (!TestNotNull(
			TEXT("the first healthy sibling reloads between missing placements"),
			SiblingA)
		|| !TestNotNull(
			TEXT("the second healthy sibling reloads between missing placements"),
			SiblingB))
	{
		return false;
	}
	TestEqual(TEXT("the first healthy sibling keeps its timing"), SiblingA->TriggerFrame, 2);
	TestEqual(TEXT("the first healthy sibling keeps its payload"), SiblingA->Payload, 91);
	TestEqual(TEXT("the second healthy sibling keeps its timing"), SiblingB->TriggerFrame, 4);
	TestEqual(TEXT("the second healthy sibling keeps its payload"), SiblingB->Payload, 37);
	const FName SiblingAObjectName = SiblingA->GetFName();
	const FName SiblingBObjectName = SiblingB->GetFName();

	auto CountMigrationNullCues = [](const UPaper2DPlusCharacterProfileAsset& Profile)
	{
		const FPaper2DPlusFrameCueMigrationReport Report =
			FPaper2DPlusFrameCueMigrationService::AnalyzeCharacterProfile(Profile);
		return Report.Issues.FilterByPredicate(
			[](const FPaper2DPlusFrameCueMigrationIssue& Issue)
			{
				return Issue.Kind == EPaper2DPlusFrameCueMigrationIssueKind::NullCue;
			}).Num();
	};

	if (!TestNotNull(TEXT("GEditor is available for the UI recovery transaction"), GEditor))
	{
		return false;
	}
	{
		// Open the real Frame Cues panel against the asset that just came through the linker.
		FPreviewBehavior_Fixture Fixture(ReloadedProfile);
		Fixture.OpenEditor();
		if (!TestNotNull(TEXT("The Frame Cues panel opens with the loaded deleted-native slot"),
			Fixture.Editor.Get()))
		{
			return false;
		}
		Fixture.Editor->RefreshAll();
		TestTrue(
			TEXT("The Frame Cues panel visibly reports all three missing placements"),
			PreviewBehavior_HasVisibleTextContaining(
				Fixture.Editor.ToSharedRef(),
				/*bAncestorsVisible=*/true,
				TEXT("3 missing Frame Cue placements")));
		TestTrue(
			TEXT("The visible recovery action carries the same count"),
			PreviewBehavior_HasVisibleTextContaining(
				Fixture.Editor.ToSharedRef(),
				/*bAncestorsVisible=*/true,
				TEXT("Remove all (3)")));
		Fixture.Editor->SeekFrameForTests(2);
		TestEqual(TEXT("The first healthy sibling previews exactly once beside the null slots"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(
				SiblingA, EPaper2DPlusFrameCuePhase::Trigger),
			1);
		Fixture.Editor->SeekFrameForTests(4);
		TestEqual(TEXT("The second healthy sibling previews exactly once beside the null slots"),
			P2DPPreviewBehaviorLog::CountPhaseForCue(
				SiblingB, EPaper2DPlusFrameCuePhase::Trigger),
			1);

		TArray<FCharacterProfileValidationIssue> Issues;
		Fixture.Asset->ValidateCharacterProfileAsset(Issues);
		const TArray<FCharacterProfileValidationIssue> OrphanErrors =
			Issues.FilterByPredicate([](const FCharacterProfileValidationIssue& Issue)
			{
				return Issue.Severity == ECharacterProfileValidationSeverity::Error
					&& Issue.Message.Contains(TEXT("empty Frame Cue placement"));
			});
		TestEqual(TEXT("The editor-open fixture reports all three orphan-slot errors"),
			OrphanErrors.Num(), 3);
		for (const FCharacterProfileValidationIssue& OrphanError : OrphanErrors)
		{
			TestTrue(TEXT("The editor-open orphan error names the Character Profile"),
				OrphanError.Message.Contains(TEXT("OrphanProfile")));
			TestTrue(TEXT("The editor-open orphan error names the animation"),
				OrphanError.Message.Contains(TEXT("DeletedNativeClassAttackFixture")));
		}
		TestEqual(TEXT("The healthy siblings have no validation errors"),
			Issues.FilterByPredicate([](const FCharacterProfileValidationIssue& Issue)
			{
				return Issue.Severity == ECharacterProfileValidationSeverity::Error
					&& (Issue.Context.EndsWith(TEXT("FrameCue[1]"))
						|| Issue.Context.EndsWith(TEXT("FrameCue[3]")));
			}).Num(), 0);
		TestEqual(TEXT("migration analysis sees the same three null placements before repair"),
			CountMigrationNullCues(*Fixture.Asset), 3);

		TSharedPtr<SFrameCueTimeline> Timeline =
			Fixture.Editor->GetUnifiedTimelineForTests();
		if (!TestNotNull(TEXT("The real Frame Cues timeline is available"), Timeline.Get()))
		{
			return false;
		}
		TestEqual(TEXT("The recovery notice reports three missing placements"),
			Timeline->GetMissingCuePlacementCountForTests(), 3);

		GEditor->ResetTransaction(NSLOCTEXT(
			"Paper2DPlusPreviewBehaviorTest",
			"BeginMissingCueUIRecovery",
			"Begin Missing Cue UI Recovery Test"));
		TestTrue(TEXT("The recovery action removes all three missing placements"),
			Timeline->RemoveAllMissingCuePlacementsForTests());
		TestEqual(TEXT("The recovery notice collapses after removal"),
			Timeline->GetMissingCuePlacementCountForTests(), 0);
		TestFalse(
			TEXT("no missing-placement recovery text remains visible after removal"),
			PreviewBehavior_HasVisibleTextContaining(
				Fixture.Editor.ToSharedRef(),
				/*bAncestorsVisible=*/true,
				TEXT("missing Frame Cue placement")));
		TestTrue(TEXT("both healthy siblings remain in their original relative order"),
			Fixture.Cues().Num() == 2
			&& Fixture.Cues()[0] == SiblingA
			&& Fixture.Cues()[1] == SiblingB);
		TestEqual(TEXT("the first healthy sibling retains its timing"),
			SiblingA->TriggerFrame, 2);
		TestEqual(TEXT("the first healthy sibling retains its payload"),
			SiblingA->Payload, 91);
		TestEqual(TEXT("the second healthy sibling retains its timing"),
			SiblingB->TriggerFrame, 4);
		TestEqual(TEXT("the second healthy sibling retains its payload"),
			SiblingB->Payload, 37);

		Issues.Reset();
		Fixture.Asset->ValidateCharacterProfileAsset(Issues);
		TestEqual(TEXT("direct validation clears the empty-placement error"),
			Issues.FilterByPredicate([](const FCharacterProfileValidationIssue& Issue)
			{
				return Issue.Severity == ECharacterProfileValidationSeverity::Error
					&& Issue.Message.Contains(TEXT("empty Frame Cue placement"));
			}).Num(), 0);
		TestEqual(TEXT("migration validation clears its null-Cue issue"),
			CountMigrationNullCues(*Fixture.Asset), 0);

		TestTrue(TEXT("one Undo restores all three linker-created null slots"),
			GEditor->UndoTransaction(true));
		Fixture.Editor->RefreshAll();
		TestTrue(TEXT("Undo restores the exact interleaved five-slot shape"),
			Fixture.Cues().Num() == 5
			&& Fixture.Cues()[0] == nullptr
			&& Fixture.Cues()[1] == SiblingA
			&& Fixture.Cues()[2] == nullptr
			&& Fixture.Cues()[3] == SiblingB
			&& Fixture.Cues()[4] == nullptr);
		TestEqual(TEXT("Undo restores the recovery notice count"),
			Timeline->GetMissingCuePlacementCountForTests(), 3);
		TestTrue(
			TEXT("Undo makes the plural recovery notice visible again"),
			PreviewBehavior_HasVisibleTextContaining(
				Fixture.Editor.ToSharedRef(),
				/*bAncestorsVisible=*/true,
				TEXT("3 missing Frame Cue placements")));

		TestTrue(TEXT("one Redo removes all three null slots again"),
			GEditor->RedoTransaction());
		Fixture.Editor->RefreshAll();
		TestTrue(TEXT("Redo restores the repaired two-sibling shape"),
			Fixture.Cues().Num() == 2
			&& Fixture.Cues()[0] == SiblingA
			&& Fixture.Cues()[1] == SiblingB);
		TestEqual(TEXT("Redo clears the recovery notice count again"),
			Timeline->GetMissingCuePlacementCountForTests(), 0);
		TestFalse(
			TEXT("Redo collapses the recovery notice again"),
			PreviewBehavior_HasVisibleTextContaining(
				Fixture.Editor.ToSharedRef(),
				/*bAncestorsVisible=*/true,
				TEXT("missing Frame Cue placement")));

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		TestTrue(TEXT("the repaired Character Profile saves"),
			UPackage::SavePackage(
				Reloaded,
				Fixture.Asset,
				*PackageFixture.FilePath,
				SaveArgs));
		Fixture.Editor->HandleHostDeactivated();
	}

	// Release transaction/editor references, evict the package, and prove the repair survives a
	// normal load with no test redirect installed.
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusPreviewBehaviorTest",
		"EndMissingCueUIRecovery",
		"End Missing Cue UI Recovery Test"));
	if (Reloaded->IsRooted())
	{
		Reloaded->RemoveFromRoot();
	}
	PackageFixture.PurgeFromMemory();
	Reloaded = nullptr;
	TestNull(TEXT("the repaired package is evicted before its final load"),
		FindPackage(nullptr, *PackageFixture.PackageName));

	UPackage* RepairedPackage = LoadPackage(nullptr, *PackageFixture.PackageName, LOAD_None);
	if (!TestNotNull(TEXT("the repaired package reloads normally"), RepairedPackage))
	{
		return false;
	}
	UPaper2DPlusCharacterProfileAsset* RepairedProfile =
		FindObject<UPaper2DPlusCharacterProfileAsset>(RepairedPackage, TEXT("OrphanProfile"));
	if (!TestNotNull(TEXT("the repaired Character Profile reloads"), RepairedProfile))
	{
		return false;
	}
	if (!TestEqual(TEXT("the repaired Character Profile keeps its animation"),
		RepairedProfile->Flipbooks.Num(), 1))
	{
		return false;
	}
	const TArray<TObjectPtr<UPaper2DPlusCueBase>>& RepairedPlacements =
		RepairedProfile->Flipbooks[0].FrameEventData.FrameCues;
	TestEqual(TEXT("save/reload keeps both healthy placements"),
		RepairedPlacements.Num(), 2);
	UPaper2DPlusPreviewBehaviorMomentCueTest* RepairedSiblingA =
		RepairedPlacements.Num() == 2
			? Cast<UPaper2DPlusPreviewBehaviorMomentCueTest>(RepairedPlacements[0].Get())
			: nullptr;
	UPaper2DPlusPreviewBehaviorMomentCueTest* RepairedSiblingB =
		RepairedPlacements.Num() == 2
			? Cast<UPaper2DPlusPreviewBehaviorMomentCueTest>(RepairedPlacements[1].Get())
			: nullptr;
	if (TestNotNull(
			TEXT("the first healthy Cue type survives the repair round trip"),
			RepairedSiblingA))
	{
		TestEqual(TEXT("the first healthy Cue keeps its serialized object identity"),
			RepairedSiblingA->GetFName(), SiblingAObjectName);
		TestEqual(TEXT("the first healthy Cue keeps its saved timing"),
			RepairedSiblingA->TriggerFrame, 2);
		TestEqual(TEXT("the first healthy Cue keeps its saved payload"),
			RepairedSiblingA->Payload, 91);
	}
	if (TestNotNull(
			TEXT("the second healthy Cue type survives the repair round trip"),
			RepairedSiblingB))
	{
		TestEqual(TEXT("the second healthy Cue keeps its serialized object identity"),
			RepairedSiblingB->GetFName(), SiblingBObjectName);
		TestEqual(TEXT("the second healthy Cue keeps its saved timing"),
			RepairedSiblingB->TriggerFrame, 4);
		TestEqual(TEXT("the second healthy Cue keeps its saved payload"),
			RepairedSiblingB->Payload, 37);
	}
	TArray<FCharacterProfileValidationIssue> RepairedIssues;
	RepairedProfile->ValidateCharacterProfileAsset(RepairedIssues);
	TestEqual(TEXT("the saved repair has no empty-placement validation error"),
		RepairedIssues.FilterByPredicate([](const FCharacterProfileValidationIssue& Issue)
		{
			return Issue.Severity == ECharacterProfileValidationSeverity::Error
				&& Issue.Message.Contains(TEXT("empty Frame Cue placement"));
		}).Num(), 0);
	TestEqual(TEXT("the saved repair has no migration null-Cue issue"),
		CountMigrationNullCues(*RepairedProfile), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusPreviewBehaviorCueTypeDeleteTest,
	"Paper2DPlus.FrameCues.Editor.Preview.Behavior.CueTypeDeleteWhileEditorOpenIsSafe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusPreviewBehaviorCueTypeDeleteTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusPreviewBehaviorTest;
	using namespace Paper2DPlusFrameCueEditorAuthoring;

	FPreviewBehavior_ScopedSession Session;

	const FString PackageName = FString::Printf(
		TEXT("/Game/__AutomationTemp__/P2DPDeleteWhileOpen_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UPackage* Package = CreatePackage(*PackageName);
	if (!TestNotNull(TEXT("Cue Type fixture package"), Package))
	{
		return false;
	}
	// Deliberately not rooted: the delete under test unloads the emptied package itself, and a rooted
	// package would make that real cleanup path fail. The scope exit re-finds it by name and only
	// cleans up what the delete left behind.
	Package->SetDirtyFlag(false);
	ON_SCOPE_EXIT
	{
		if (UPackage* Remaining = FindPackage(nullptr, *PackageName))
		{
			Remaining->SetDirtyFlag(false);
			FText UnloadError;
			TArray<UPackage*> Packages = { Remaining };
			UPackageTools::UnloadPackages(Packages, UnloadError, true);
		}
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	};

	FPaper2DPlusFrameCueTypeCreateRequest Request;
	Request.Package = Package;
	Request.AssetName = TEXT("BP_DeletedWhileOpen");
	Request.Kind = EPaper2DPlusFrameCueTypeKind::Range;
	const FPaper2DPlusFrameCueTypeCreateResult Created =
		FPaper2DPlusFrameCueTypeAuthoring::CreateCueType(Request);
	if (!TestTrue(TEXT("A Cue State Type is created for the delete fixture"), Created.IsSuccess()))
	{
		AddError(Created.Error.ToString());
		return false;
	}
	UClass* CueTypeClass = Created.CueType->GeneratedClass;
	if (!TestNotNull(TEXT("The created Cue Type compiled a class"), CueTypeClass))
	{
		return false;
	}

	FPreviewBehavior_Fixture Fixture(6);
	UPaper2DPlusCueState* Placement = Cast<UPaper2DPlusCueState>(
		CreatePlacement(Fixture.Asset, CueTypeClass, 0, 6));
	if (!TestNotNull(TEXT("A placement of the Cue Type exists"), Placement))
	{
		return false;
	}
	Placement->FrameCount = 6;
	Fixture.Cues().Add(Placement);
	Fixture.OpenEditor();
	Fixture.Editor->SeekFrameForTests(2);
	TestEqual(TEXT("The placement's range is active before the delete"),
		Fixture.Editor->GetActiveRangeCountForTests(), 1);

	// The Content Browser's force delete: the asset goes even though a placement still uses it.
	const int32 DeletedCount = ObjectTools::ForceDeleteObjects(
		TArray<UObject*>{ Created.CueType }, /*ShowConfirmation*/ false);
	TestTrue(TEXT("Force delete removes the Cue Type asset"), DeletedCount > 0);

	UPaper2DPlusCueBase* SurvivingPlacement =
		Fixture.Cues().Num() == 1 ? Fixture.Cues()[0].Get() : nullptr;
	TestFalse(TEXT("An orphaned placement is no longer dispatchable"),
		Paper2DPlusFrameCueBehavior::IsPlacementResolvable(SurvivingPlacement));

	// The open editor keeps working: the next transitions close the orphan's lifecycle instead of
	// stranding it, and later scrubs, selection, and refresh stay safe.
	Fixture.Editor->SeekFrameForTests(4);
	TestEqual(TEXT("The orphaned range does not stay active after the delete"),
		Fixture.Editor->GetActiveRangeCountForTests(), 0);
	Fixture.Editor->SeekFrameForTests(1);
	Fixture.Editor->SeekFrameForTests(3);
	TestEqual(TEXT("An orphaned placement never re-enters the active set"),
		Fixture.Editor->GetActiveRangeCountForTests(), 0);
	Fixture.Editor->SelectCueForTests(0);
	Fixture.Editor->RefreshAll();
	TestEqual(TEXT("The placement survives the delete as authored data"),
		Fixture.Cues().Num(), 1);
	Fixture.Editor->HandleHostDeactivated();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
