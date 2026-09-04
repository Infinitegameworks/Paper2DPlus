// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CharacterProfileAssetEditor.h"
#include "CharacterProfileEditorModel.h"
#include "Engine/Texture2D.h"
#include "FrameEventEditor.h"
#include "FrameTimingEditor.h"
#include "HAL/FileManager.h"
#include "HitboxEditorPanel.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "RootMotionEditor.h"
#include "SpriteEditorPanel.h"
#include "TextureCompiler.h"
#include "Tickable.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	/**
	 * The wrong-type fixture only has to be "not a UPaperFlipbook", but it is a real disk asset the editor
	 * loads and builds a render resource for. A UTexture2D saved with no Source has zero mips, and
	 * UTexture2D::CreateResource logs that at Error on UE 5.0-5.2 (Warning from 5.3 on), which fails the
	 * test on any render-capable lane (5.1/5.2 only pass under -nullrhi, which never creates the resource).
	 * One real 1x1 source mip keeps the fixture wrong-typed and engine-silent on every version.
	 */
	void DirectionalModel_InitOnePixelSource(UTexture2D* Texture)
	{
		Texture->Source.Init(1, 1, 1, 1, TSF_BGRA8);
		if (uint8* Pixels = Texture->Source.LockMip(0))
		{
			Pixels[0] = 255; // B
			Pixels[1] = 255; // G
			Pixels[2] = 255; // R
			Pixels[3] = 255; // A
			Texture->Source.UnlockMip(0);
		}
		Texture->CompressionSettings = TC_EditorIcon;
		Texture->Filter = TF_Nearest;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->LODGroup = TEXTUREGROUP_Pixels2D;
		Texture->NeverStream = true;
		Texture->UpdateResource();
		Texture->PostEditChange();
		FTextureCompilingManager::Get().FinishCompilation({ Texture });
	}

	UPaperFlipbook* DirectionalModel_MakeFlipbook(UObject* Outer, const TCHAR* Name)
	{
		return NewObject<UPaperFlipbook>(Outer, Name);
	}

	void DirectionalModel_SetFramesPerSecond(UPaperFlipbook* Flipbook, float FramesPerSecond)
	{
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.FramesPerSecond = FramesPerSecond;
	}

	int32 DirectionalModel_AddAnimation(
		UPaper2DPlusCharacterProfileAsset* Profile,
		const TCHAR* Name,
		UPaperFlipbook* BaseFlipbook)
	{
		FFlipbookProfileEntry Entry;
		Entry.Identity.FlipbookName = Name;
		Entry.Identity.Flipbook = BaseFlipbook;
		return Profile->Flipbooks.Add(MoveTemp(Entry));
	}

	void DirectionalModel_Configure(
		UPaper2DPlusCharacterProfileAsset* Profile,
		int32 AnimationIndex,
		int32 DirectionCount,
		float AngleOffsetDegrees,
		const TArray<TPair<int32, TSoftObjectPtr<UPaperFlipbook>>>& Slots)
	{
		FFlipbookProfileEntry& Entry = Profile->Flipbooks[AnimationIndex];
		Entry.DirectionalAnimationData.bHasDirectionalSet = true;
		Entry.DirectionalAnimationData.bOverrideProfileSettings = true;
		Entry.DirectionalAnimationData.DirectionCount = DirectionCount;
		Entry.DirectionalAnimationData.AngleOffsetDegrees = AngleOffsetDegrees;
		Entry.DirectionalAnimationData.Slots.Reset();
		for (const TPair<int32, TSoftObjectPtr<UPaperFlipbook>>& Pair : Slots)
		{
			FPaper2DPlusDirectionalAnimationSlot& Slot =
				Entry.DirectionalAnimationData.Slots.AddDefaulted_GetRef();
			Slot.SlotIndex = Pair.Key;
			Slot.Flipbook = Pair.Value;
		}
	}

	void DirectionalModel_PumpAsync()
	{
		for (int32 Pump = 0; Pump < 4; ++Pump)
		{
			FlushAsyncLoading();
			FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, 0.0f);
			FTSTicker::GetCoreTicker().Tick(0.0f);
		}
	}

	/** Disk-backed cold assets let the tests observe real Resolving and completion states. */
	struct FScopedDirectionalModelAssets
	{
		FString Directory;
		FString FlipbookPackageName;
		FString WrongTypePackageName;
		FString FlipbookFilePath;
		FString WrongTypeFilePath;
		FName FlipbookName = TEXT("ColdDirectionalFlipbook");
		FName WrongTypeName = TEXT("ColdDirectionalWrongType");

		FScopedDirectionalModelAssets()
		{
			Directory = FString::Printf(
				TEXT("/Game/__AutomationTemp__/P2DPDirectionalModel_%s"),
				*FGuid::NewGuid().ToString(EGuidFormats::Digits));
			FlipbookPackageName = Directory / TEXT("Flipbook");
			WrongTypePackageName = Directory / TEXT("WrongType");
			FlipbookFilePath = FPackageName::LongPackageNameToFilename(
				FlipbookPackageName, FPackageName::GetAssetPackageExtension());
			WrongTypeFilePath = FPackageName::LongPackageNameToFilename(
				WrongTypePackageName, FPackageName::GetAssetPackageExtension());
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(FlipbookFilePath), true);
		}

		~FScopedDirectionalModelAssets()
		{
			FText Ignored;
			Cleanup(Ignored);
		}

		FSoftObjectPath FlipbookPath() const
		{
			return FSoftObjectPath(FString::Printf(
				TEXT("%s.%s"), *FlipbookPackageName, *FlipbookName.ToString()));
		}

		FSoftObjectPath WrongTypePath() const
		{
			return FSoftObjectPath(FString::Printf(
				TEXT("%s.%s"), *WrongTypePackageName, *WrongTypeName.ToString()));
		}

		bool Unload(FText& OutError) const
		{
			TArray<UPackage*> Packages;
			for (const FString& PackageName : { FlipbookPackageName, WrongTypePackageName })
			{
				if (UPackage* Package = FindPackage(nullptr, *PackageName))
				{
					if (Package->IsRooted())
					{
						Package->RemoveFromRoot();
					}
					Package->SetDirtyFlag(false);
					Packages.Add(Package);
				}
			}
			const bool bUnloaded = Packages.IsEmpty()
				|| UPackageTools::UnloadPackages(Packages, OutError, true);
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			return bUnloaded;
		}

		bool BuildAndUnload(FText& OutError)
		{
			UPackage* FlipbookPackage = CreatePackage(*FlipbookPackageName);
			UPackage* WrongTypePackage = CreatePackage(*WrongTypePackageName);
			if (!FlipbookPackage || !WrongTypePackage)
			{
				OutError = FText::FromString(TEXT("Could not create directional model test packages."));
				return false;
			}
			FlipbookPackage->AddToRoot();
			WrongTypePackage->AddToRoot();
			UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(
				FlipbookPackage, FlipbookName, RF_Public | RF_Standalone);
			UTexture2D* WrongType = NewObject<UTexture2D>(
				WrongTypePackage, WrongTypeName, RF_Public | RF_Standalone);
			DirectionalModel_InitOnePixelSource(WrongType);
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			const bool bSaved = UPackage::SavePackage(
				FlipbookPackage, Flipbook, *FlipbookFilePath, SaveArgs)
				&& UPackage::SavePackage(
					WrongTypePackage, WrongType, *WrongTypeFilePath, SaveArgs);
			FlipbookPackage->RemoveFromRoot();
			WrongTypePackage->RemoveFromRoot();
			if (!bSaved)
			{
				OutError = FText::FromString(TEXT("Could not save directional model test packages."));
				return false;
			}
			return Unload(OutError);
		}

		bool Cleanup(FText& OutError) const
		{
			const bool bUnloaded = Unload(OutError);
			const FString SafeRoot = FPaths::ConvertRelativePathToFull(
				FPaths::ProjectContentDir() / TEXT("__AutomationTemp__"));
			const FString FixtureDirectory = FPaths::ConvertRelativePathToFull(
				FPaths::GetPath(FlipbookFilePath));
			const bool bSafe = Directory.StartsWith(
				TEXT("/Game/__AutomationTemp__/P2DPDirectionalModel_"))
				&& FPaths::IsUnderDirectory(FixtureDirectory, SafeRoot);
			if (!bSafe)
			{
				OutError = FText::FromString(TEXT("Refused unsafe directional model fixture cleanup."));
				return false;
			}

			IFileManager& Files = IFileManager::Get();
			bool bFilesGone = true;
			for (const FString& FilePath : { FlipbookFilePath, WrongTypeFilePath })
			{
				for (const FString& PackageFile : {
					FilePath,
					FPaths::ChangeExtension(FilePath, TEXT("uexp")),
					FPaths::ChangeExtension(FilePath, TEXT("ubulk")),
					FPaths::ChangeExtension(FilePath, TEXT("uptnl")) })
				{
					bFilesGone &= !Files.FileExists(*PackageFile)
						|| Files.Delete(*PackageFile, false, true, true);
				}
			}
			const bool bDirectoryGone = !Files.DirectoryExists(*FixtureDirectory)
				|| Files.DeleteDirectory(*FixtureDirectory, false, false);
			return bUnloaded && bFilesGone && bDirectoryGone;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalModelCapabilityTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Model.CapabilityDefaultsOffAndKeepsBase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalModelCapabilityTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Base = DirectionalModel_MakeFlipbook(Profile, TEXT("CapabilityBase"));
	UPaperFlipbook* Variant = DirectionalModel_MakeFlipbook(Profile, TEXT("CapabilityVariant"));
	const int32 Index = DirectionalModel_AddAnimation(Profile, TEXT("Idle"), Base);
	DirectionalModel_Configure(Profile, Index, 8, 0.0f, { { 0, Variant } });

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	const FCharacterProfileDirectionalPreview& Preview = Model->GetDirectionalPreview();
	TestFalse(TEXT("Directional preview capability is opt-in"), Model->IsDirectionalPreviewEnabled());
	TestEqual(TEXT("An opted-out host remains on base art"),
		Preview.State, ECharacterProfileDirectionalPreviewState::Base);
	TestEqual(TEXT("Base art remains the desired visual"), Preview.DesiredFlipbookPath, FSoftObjectPath(Base));
	TestEqual(TEXT("The stable mutation owner remains the base animation"),
		Preview.BaseAnimation.FallbackName, FString(TEXT("Idle")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalModelBearingTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Model.NormalizedBearingAndOffsetCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalModelBearingTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Base = DirectionalModel_MakeFlipbook(Profile, TEXT("BearingBase"));
	UPaperFlipbook* Variant = DirectionalModel_MakeFlipbook(Profile, TEXT("BearingVariant"));
	const int32 Index = DirectionalModel_AddAnimation(Profile, TEXT("Run"), Base);
	DirectionalModel_Configure(Profile, Index, 8, 15.0f, { { 2, Variant } });

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	Model->SetDirectionalPreviewEnabled(true);
	Model->SetCommittedDirectionalBearing(-45.0);
	TestEqual(TEXT("Negative physical bearings normalize into [0, 360)"),
		Model->GetCommittedDirectionalBearing(), 315.0);
	Model->SetCommittedDirectionalBearing(435.0);
	TestEqual(TEXT("Overflow physical bearings wrap into [0, 360)"),
		Model->GetCommittedDirectionalBearing(), 75.0);
	TestTrue(TEXT("A wheel slot can commit its offset-aware center"),
		Model->CommitDirectionalPreviewSlot(2));
	TestEqual(TEXT("Slot 2 center subtracts the authored 15-degree offset"),
		Model->GetCommittedDirectionalBearing(), 75.0);
	TestEqual(TEXT("The native topology seam resolves that center back to slot 2"),
		Model->GetDirectionalPreview().SlotIndex, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalModelCountReresolutionTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Model.BearingReresolvesAcrossThreeAndSixteenSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalModelCountReresolutionTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* ThreeBase = DirectionalModel_MakeFlipbook(Profile, TEXT("ThreeBase"));
	UPaperFlipbook* ThreeVariant = DirectionalModel_MakeFlipbook(Profile, TEXT("ThreeEast"));
	UPaperFlipbook* SixteenBase = DirectionalModel_MakeFlipbook(Profile, TEXT("SixteenBase"));
	UPaperFlipbook* SixteenVariant = DirectionalModel_MakeFlipbook(Profile, TEXT("SixteenEast"));
	const int32 ThreeIndex = DirectionalModel_AddAnimation(Profile, TEXT("Three"), ThreeBase);
	const int32 SixteenIndex = DirectionalModel_AddAnimation(Profile, TEXT("Sixteen"), SixteenBase);
	DirectionalModel_Configure(Profile, ThreeIndex, 3, 0.0f, { { 1, ThreeVariant } });
	DirectionalModel_Configure(Profile, SixteenIndex, 16, 0.0f, { { 4, SixteenVariant } });

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	Model->SetDirectionalPreviewEnabled(true);
	Model->SetCommittedDirectionalBearing(90.0);
	TestEqual(TEXT("East resolves to slot 1 with three directions"),
		Model->GetDirectionalPreview().SlotIndex, 1);
	Model->SetSelectedFlipbook(SixteenIndex);
	TestEqual(TEXT("The committed physical bearing survives animation selection"),
		Model->GetCommittedDirectionalBearing(), 90.0);
	TestEqual(TEXT("East re-resolves to slot 4 with sixteen directions"),
		Model->GetDirectionalPreview().SlotIndex, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalModelStatesTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Model.FivePreviewStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalModelStatesTest::RunTest(const FString& Parameters)
{
	FScopedDirectionalModelAssets ColdAssets;
	FText FixtureError;
	if (!TestTrue(TEXT("Cold preview fixtures save and unload"), ColdAssets.BuildAndUnload(FixtureError)))
	{
		AddError(FixtureError.ToString());
		return false;
	}
	TestNull(TEXT("Cold flipbook starts nonresident"), ColdAssets.FlipbookPath().ResolveObject());
	TestNull(TEXT("Cold wrong-type asset starts nonresident"), ColdAssets.WrongTypePath().ResolveObject());

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* LegacyBase = DirectionalModel_MakeFlipbook(Profile, TEXT("LegacyBase"));
	DirectionalModel_AddAnimation(Profile, TEXT("Legacy"), LegacyBase);
	UPaperFlipbook* Base = DirectionalModel_MakeFlipbook(Profile, TEXT("DirectionalBase"));
	UPaperFlipbook* Resident = DirectionalModel_MakeFlipbook(Profile, TEXT("ResidentNorth"));
	const int32 DirectionalIndex = DirectionalModel_AddAnimation(Profile, TEXT("Aim"), Base);
	DirectionalModel_Configure(Profile, DirectionalIndex, 8, 0.0f, {
		{ 0, Resident },
		{ 1, TSoftObjectPtr<UPaperFlipbook>(ColdAssets.FlipbookPath()) },
		{ 2, TSoftObjectPtr<UPaperFlipbook>(ColdAssets.WrongTypePath()) }
	});

	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	Model->SetDirectionalPreviewEnabled(true);
	TestEqual(TEXT("A non-directional animation previews Base"),
		Model->GetDirectionalPreview().State, ECharacterProfileDirectionalPreviewState::Base);
	TestTrue(TEXT("A configured-empty set can be enabled without adding occupancy"),
		Profile->EnableDirectionalSet(0));
	Model->NotifyAssetDataChanged();
	TestEqual(TEXT("A configured-empty set still previews Base"),
		Model->GetDirectionalPreview().State, ECharacterProfileDirectionalPreviewState::Base);

	Model->SetSelectedFlipbook(DirectionalIndex);
	TestEqual(TEXT("A resident occupied exact slot previews Occupied Variant"),
		Model->GetDirectionalPreview().State,
		ECharacterProfileDirectionalPreviewState::OccupiedVariant);
	Model->SetCommittedDirectionalBearing(135.0);
	TestEqual(TEXT("An exact active unoccupied slot previews Empty without fallback"),
		Model->GetDirectionalPreview().State, ECharacterProfileDirectionalPreviewState::Empty);

	Model->SetCommittedDirectionalBearing(45.0);
	TestEqual(TEXT("A nonresident occupied slot enters Resolving"),
		Model->GetDirectionalPreview().State, ECharacterProfileDirectionalPreviewState::Resolving);
	DirectionalModel_PumpAsync();
	TestEqual(TEXT("A successful async request publishes the occupied variant"),
		Model->GetDirectionalPreview().State,
		ECharacterProfileDirectionalPreviewState::OccupiedVariant);
	TestNotNull(TEXT("The loaded preview is retained as resident art"),
		Model->GetDirectionalPreview().ResidentFlipbook.Get());

	Model->SetCommittedDirectionalBearing(90.0);
	TestEqual(TEXT("A second cold occupied slot enters Resolving"),
		Model->GetDirectionalPreview().State, ECharacterProfileDirectionalPreviewState::Resolving);
	DirectionalModel_PumpAsync();
	TestEqual(TEXT("An asynchronously loaded non-flipbook publishes Unavailable"),
		Model->GetDirectionalPreview().State, ECharacterProfileDirectionalPreviewState::Unavailable);
	TestNull(TEXT("Unavailable never exposes misleading resident art"),
		Model->GetDirectionalPreview().ResidentFlipbook.Get());

	Model->InitializeFromAsset(nullptr);
	Model.Reset();
	DirectionalModel_PumpAsync();
	TestTrue(TEXT("Cold preview fixtures clean up after handle release"), ColdAssets.Cleanup(FixtureError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalCrossToolPreviewArtTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Model.EveryProfileToolDisplaysFivePreviewStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalCrossToolPreviewArtTest::RunTest(const FString& Parameters)
{
	FScopedDirectionalModelAssets ColdAssets;
	FText FixtureError;
	if (!TestTrue(TEXT("Cross-tool cold preview fixtures save and unload"),
		ColdAssets.BuildAndUnload(FixtureError)))
	{
		AddError(FixtureError.ToString());
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Base = DirectionalModel_MakeFlipbook(Profile, TEXT("ToolBase"));
	UPaperFlipbook* Resident =
		DirectionalModel_MakeFlipbook(Profile, TEXT("ToolResidentNorth"));
	const int32 AnimationIndex =
		DirectionalModel_AddAnimation(Profile, TEXT("Aim"), Base);
	DirectionalModel_Configure(Profile, AnimationIndex, 8, 0.0f, {
		{ 0, Resident },
		{ 1, TSoftObjectPtr<UPaperFlipbook>(ColdAssets.FlipbookPath()) },
		{ 2, TSoftObjectPtr<UPaperFlipbook>(ColdAssets.WrongTypePath()) }
	});

	TSharedPtr<FCharacterProfileEditorModel> Model =
		MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);

	TSharedPtr<SHitboxEditorPanel> Hitbox = SNew(SHitboxEditorPanel)
		.Model(Model)
		.HostContract(FProfileToolPanelHostContract::External());
	TSharedPtr<SSpriteEditorPanel> Sprite = SNew(SSpriteEditorPanel)
		.Model(Model)
		.HostContract(FProfileToolPanelHostContract::External());
	TSharedPtr<SFrameTimingEditor> FrameTiming = SNew(SFrameTimingEditor)
		.Asset(Profile)
		.Model(Model)
		.HostContract(FProfileToolPanelHostContract::External());
	TSharedPtr<SFrameEventEditor> FrameCues = SNew(SFrameEventEditor)
		.Model(Model)
		.HostContract(FProfileToolPanelHostContract::External());
	TSharedPtr<SRootMotionEditor> RootMotion = SNew(SRootMotionEditor)
		.Asset(Profile)
		.Model(Model)
		.HostContract(FProfileToolPanelHostContract::External());

	const auto AssertEveryToolDisplays = [
		this,
		&Model,
		&Hitbox,
		&Sprite,
		&FrameTiming,
		&FrameCues,
		&RootMotion](const TCHAR* StateLabel, UPaperFlipbook* Expected)
	{
		const auto AssertTool = [this, StateLabel, Expected](
			const TCHAR* ToolLabel,
			UPaperFlipbook* Actual)
		{
			TestEqual(
				FString::Printf(TEXT("%s displays %s art"), ToolLabel, StateLabel),
				Actual,
				Expected);
		};

		AssertTool(
			TEXT("Animations"),
			FCharacterProfileAssetEditorToolkit::ResolveDirectionalHeaderPreviewFlipbook(
				Model->GetDirectionalPreview()));
		AssertTool(TEXT("Hitbox"), Hitbox->GetPreviewFlipbook());
		AssertTool(TEXT("Sprite"), Sprite->GetPreviewFlipbook());
		AssertTool(TEXT("Frame Timing"), FrameTiming->GetPreviewFlipbook());
		AssertTool(TEXT("Frame Cues"), FrameCues->GetPreviewFlipbook());
		AssertTool(TEXT("Root Motion"), RootMotion->GetPreviewFlipbook());
	};

	TestEqual(TEXT("The cross-tool fixture starts in the Base preview state"),
		Model->GetDirectionalPreview().State,
		ECharacterProfileDirectionalPreviewState::Base);
	AssertEveryToolDisplays(TEXT("Base"), Base);

	Model->SetDirectionalPreviewEnabled(true);
	Model->SetSelectedFlipbook(AnimationIndex);
	AssertEveryToolDisplays(TEXT("Occupied Variant"), Resident);

	Model->SetCommittedDirectionalBearing(135.0);
	TestEqual(TEXT("The cross-tool empty fixture resolves the exact empty slot"),
		Model->GetDirectionalPreview().State,
		ECharacterProfileDirectionalPreviewState::Empty);
	AssertEveryToolDisplays(TEXT("Empty placeholder"), nullptr);

	Model->SetCommittedDirectionalBearing(45.0);
	TestEqual(TEXT("The cross-tool cold fixture enters Resolving"),
		Model->GetDirectionalPreview().State,
		ECharacterProfileDirectionalPreviewState::Resolving);
	AssertEveryToolDisplays(TEXT("Resolving placeholder"), nullptr);
	DirectionalModel_PumpAsync();
	TestEqual(TEXT("The cold flipbook finishes as an occupied variant"),
		Model->GetDirectionalPreview().State,
		ECharacterProfileDirectionalPreviewState::OccupiedVariant);
	AssertEveryToolDisplays(
		TEXT("loaded Occupied Variant"),
		Model->GetDirectionalPreview().ResidentFlipbook.Get());

	Model->SetCommittedDirectionalBearing(90.0);
	TestEqual(TEXT("The wrong-type fixture first enters Resolving"),
		Model->GetDirectionalPreview().State,
		ECharacterProfileDirectionalPreviewState::Resolving);
	DirectionalModel_PumpAsync();
	TestEqual(TEXT("The wrong-type fixture becomes Unavailable"),
		Model->GetDirectionalPreview().State,
		ECharacterProfileDirectionalPreviewState::Unavailable);
	AssertEveryToolDisplays(TEXT("Unavailable placeholder"), nullptr);

	Model->InitializeFromAsset(nullptr);
	Hitbox.Reset();
	Sprite.Reset();
	FrameTiming.Reset();
	FrameCues.Reset();
	RootMotion.Reset();
	Model.Reset();
	DirectionalModel_PumpAsync();
	TestTrue(TEXT("Cross-tool preview fixtures clean up after panel release"),
		ColdAssets.Cleanup(FixtureError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalModelStaleAsyncTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Model.StaleAsyncCannotReplaceNewestBearing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalModelStaleAsyncTest::RunTest(const FString& Parameters)
{
	FScopedDirectionalModelAssets ColdAssets;
	FText FixtureError;
	if (!TestTrue(TEXT("Cold stale-load fixture saves and unloads"),
		ColdAssets.BuildAndUnload(FixtureError)))
	{
		AddError(FixtureError.ToString());
		return false;
	}
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Base = DirectionalModel_MakeFlipbook(Profile, TEXT("StaleBase"));
	UPaperFlipbook* Newest = DirectionalModel_MakeFlipbook(Profile, TEXT("NewestVariant"));
	const int32 Index = DirectionalModel_AddAnimation(Profile, TEXT("Dash"), Base);
	DirectionalModel_Configure(Profile, Index, 8, 0.0f, {
		{ 0, TSoftObjectPtr<UPaperFlipbook>(ColdAssets.FlipbookPath()) },
		{ 1, Newest }
	});

	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	Model->SetDirectionalPreviewEnabled(true);
	TestEqual(TEXT("The first cold selection is resolving"),
		Model->GetDirectionalPreview().State, ECharacterProfileDirectionalPreviewState::Resolving);
	Model->SetCommittedDirectionalBearing(45.0);
	TestEqual(TEXT("The newer resident selection publishes immediately"),
		Model->GetDirectionalPreview().ResidentFlipbook.Get(), Newest);
	DirectionalModel_PumpAsync();
	TestEqual(TEXT("A stale completion cannot replace the newer state"),
		Model->GetDirectionalPreview().State,
		ECharacterProfileDirectionalPreviewState::OccupiedVariant);
	TestEqual(TEXT("A stale completion cannot replace the newer art"),
		Model->GetDirectionalPreview().ResidentFlipbook.Get(), Newest);
	TestEqual(TEXT("A stale completion cannot replace the newer desired path"),
		Model->GetDirectionalPreview().DesiredFlipbookPath, FSoftObjectPath(Newest));

	Model->InitializeFromAsset(nullptr);
	Model.Reset();
	DirectionalModel_PumpAsync();
	TestTrue(TEXT("Stale-load fixture cleans up after cancellation"), ColdAssets.Cleanup(FixtureError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalModelRefreshAndOwnershipTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Model.SelectionRefreshKeepsBaseGameplayCursor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalModelRefreshAndOwnershipTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* FirstBase = DirectionalModel_MakeFlipbook(Profile, TEXT("FirstBase"));
	UPaperFlipbook* FirstVariant = DirectionalModel_MakeFlipbook(Profile, TEXT("FirstVariant"));
	UPaperFlipbook* SecondBase = DirectionalModel_MakeFlipbook(Profile, TEXT("SecondBase"));
	UPaperFlipbook* SecondVariant = DirectionalModel_MakeFlipbook(Profile, TEXT("SecondVariant"));
	UPaperFlipbook* Replacement = DirectionalModel_MakeFlipbook(Profile, TEXT("ReplacementVariant"));
	const int32 FirstIndex = DirectionalModel_AddAnimation(Profile, TEXT("First"), FirstBase);
	const int32 SecondIndex = DirectionalModel_AddAnimation(Profile, TEXT("Second"), SecondBase);
	DirectionalModel_Configure(Profile, FirstIndex, 8, 0.0f, { { 0, FirstVariant } });
	DirectionalModel_Configure(Profile, SecondIndex, 8, 0.0f, { { 0, SecondVariant } });

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	Model->SetDirectionalPreviewEnabled(true);
	Model->SetSelectedFlipbook(SecondIndex);
	Model->SetSelectedFrame(7);
	TestEqual(TEXT("Selection recomputes the directional art"),
		Model->GetDirectionalPreview().ResidentFlipbook.Get(), SecondVariant);
	TestEqual(TEXT("Selection preserves the stable base mutation owner"),
		Model->GetDirectionalPreview().BaseAnimation.FallbackName, FString(TEXT("Second")));

	TestTrue(TEXT("Replacing the selected slot mutates the authored set"),
		Profile->SetDirectionalSlot(SecondIndex, 0, Replacement));
	Model->NotifyAssetDataChanged();
	TestEqual(TEXT("Asset-data refresh recomputes the visible variant"),
		Model->GetDirectionalPreview().ResidentFlipbook.Get(), Replacement);
	TestEqual(TEXT("Directional refresh never selects a variant row"),
		Model->GetSelectedFlipbookIndex(), SecondIndex);
	TestEqual(TEXT("Directional refresh never changes the base-owned frame cursor"),
		Model->GetSelectedFrameIndex(), 7);
	TestEqual(TEXT("Directional refresh never changes the canonical base reference"),
		Profile->Flipbooks[SecondIndex].Identity.Flipbook.Get(), SecondBase);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalModelResidentCompatibilityTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Model.ResidentIncompatibleVariantIsUnavailable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalModelResidentCompatibilityTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Base = DirectionalModel_MakeFlipbook(Profile, TEXT("CompatibleOwner"));
	UPaperFlipbook* Variant = DirectionalModel_MakeFlipbook(Profile, TEXT("WrongTimeline"));
	DirectionalModel_SetFramesPerSecond(Base, 30.0f);
	DirectionalModel_SetFramesPerSecond(Variant, 12.0f);
	const int32 Index = DirectionalModel_AddAnimation(Profile, TEXT("Aim"), Base);
	DirectionalModel_Configure(Profile, Index, 8, 0.0f, { { 0, Variant } });

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	Model->SetDirectionalPreviewEnabled(true);
	TestEqual(TEXT("Resident incompatible art fails closed"),
		Model->GetDirectionalPreview().State,
		ECharacterProfileDirectionalPreviewState::Unavailable);
	TestNull(TEXT("Resident incompatible art is never published"),
		Model->GetDirectionalPreview().ResidentFlipbook.Get());
	TestTrue(TEXT("The preview reports the discriminating timeline field"),
		Model->GetDirectionalPreview().Reason.Contains(TEXT("FramesPerSecond")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalModelResidentRetentionTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Model.ResidentVariantSurvivesGarbageCollection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalModelResidentRetentionTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UPaper2DPlusCharacterProfileAsset> Profile(
		NewObject<UPaper2DPlusCharacterProfileAsset>());
	TStrongObjectPtr<UPaperFlipbook> Base(
		DirectionalModel_MakeFlipbook(GetTransientPackage(), TEXT("RetainedBase")));
	UPaperFlipbook* Variant = DirectionalModel_MakeFlipbook(
		GetTransientPackage(), TEXT("RetainedVariant"));
	TWeakObjectPtr<UPaperFlipbook> VariantProbe(Variant);
	const int32 Index = DirectionalModel_AddAnimation(Profile.Get(), TEXT("Aim"), Base.Get());
	DirectionalModel_Configure(Profile.Get(), Index, 8, 0.0f, { { 0, Variant } });

	TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile.Get());
	Model->SetDirectionalPreviewEnabled(true);
	TestEqual(TEXT("The resident fast path publishes the occupied variant"),
		Model->GetDirectionalPreview().ResidentFlipbook.Get(), Variant);

	Variant = nullptr;
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestTrue(TEXT("The published visual source remains alive while the model displays it"),
		VariantProbe.IsValid());
	TestEqual(TEXT("Garbage collection cannot silently empty an occupied preview"),
		Model->GetDirectionalPreview().ResidentFlipbook.Get(), VariantProbe.Get());
	TestEqual(TEXT("The preview remains in its occupied terminal state"),
		Model->GetDirectionalPreview().State,
		ECharacterProfileDirectionalPreviewState::OccupiedVariant);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalModelColdCompatibilityTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Model.ColdIncompatibleVariantCompletesUnavailable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalModelColdCompatibilityTest::RunTest(const FString& Parameters)
{
	FScopedDirectionalModelAssets ColdAssets;
	FText FixtureError;
	if (!TestTrue(TEXT("Cold incompatible fixture saves and unloads"),
		ColdAssets.BuildAndUnload(FixtureError)))
	{
		AddError(FixtureError.ToString());
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Base = DirectionalModel_MakeFlipbook(Profile, TEXT("ColdOwner"));
	DirectionalModel_SetFramesPerSecond(Base, 30.0f);
	const int32 Index = DirectionalModel_AddAnimation(Profile, TEXT("Shoot"), Base);
	DirectionalModel_Configure(Profile, Index, 8, 0.0f, {
		{ 0, TSoftObjectPtr<UPaperFlipbook>(ColdAssets.FlipbookPath()) }
	});

	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	Model->SetDirectionalPreviewEnabled(true);
	TestEqual(TEXT("Cold art enters Resolving before publication"),
		Model->GetDirectionalPreview().State,
		ECharacterProfileDirectionalPreviewState::Resolving);
	DirectionalModel_PumpAsync();
	TestEqual(TEXT("Cold incompatible art completes as Unavailable"),
		Model->GetDirectionalPreview().State,
		ECharacterProfileDirectionalPreviewState::Unavailable);
	TestNull(TEXT("Cold incompatible art is never published"),
		Model->GetDirectionalPreview().ResidentFlipbook.Get());
	TestTrue(TEXT("The async completion reports the timeline mismatch"),
		Model->GetDirectionalPreview().Reason.Contains(TEXT("FramesPerSecond")));

	Model->InitializeFromAsset(nullptr);
	Model.Reset();
	DirectionalModel_PumpAsync();
	TestTrue(TEXT("Cold incompatible fixture cleans up"), ColdAssets.Cleanup(FixtureError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalModelDuplicateBaseCompletionTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Model.DuplicateBaseRowCannotRemainResolving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalModelDuplicateBaseCompletionTest::RunTest(const FString& Parameters)
{
	FScopedDirectionalModelAssets ColdAssets;
	FText FixtureError;
	if (!TestTrue(TEXT("Duplicate-base cold fixture saves and unloads"),
		ColdAssets.BuildAndUnload(FixtureError)))
	{
		AddError(FixtureError.ToString());
		return false;
	}

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* SharedBase = DirectionalModel_MakeFlipbook(Profile, TEXT("SharedLegacyBase"));
	DirectionalModel_AddAnimation(Profile, TEXT("LegacyFirst"), SharedBase);
	const int32 SelectedIndex = DirectionalModel_AddAnimation(
		Profile, TEXT("DirectionalSecond"), SharedBase);
	DirectionalModel_Configure(Profile, SelectedIndex, 8, 0.0f, {
		{ 0, TSoftObjectPtr<UPaperFlipbook>(ColdAssets.FlipbookPath()) }
	});

	TSharedPtr<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	Model->SetSelectedFlipbook(SelectedIndex);
	Model->SetDirectionalPreviewEnabled(true);
	TestEqual(TEXT("The selected later row begins a cold request"),
		Model->GetDirectionalPreview().State,
		ECharacterProfileDirectionalPreviewState::Resolving);
	DirectionalModel_PumpAsync();
	TestEqual(TEXT("Completion publishes the unique compatible occupied variant"),
		Model->GetDirectionalPreview().State,
		ECharacterProfileDirectionalPreviewState::OccupiedVariant);
	TestEqual(TEXT("Completion preserves the exact authored variant path"),
		Model->GetDirectionalPreview().DesiredFlipbookPath,
		ColdAssets.FlipbookPath());
	TestNotNull(TEXT("Completion retains the loaded directional Flipbook"),
		Model->GetDirectionalPreview().ResidentFlipbook.Get());
	TestEqual(TEXT("The retained object is the exact cold fixture asset"),
		FSoftObjectPath(Model->GetDirectionalPreview().ResidentFlipbook.Get()),
		ColdAssets.FlipbookPath());
	TestTrue(TEXT("Successful completion reports base-owned directional presentation"),
		Model->GetDirectionalPreview().Reason.Contains(TEXT("base-owned gameplay data")));
	TestEqual(TEXT("Completion preserves the selected duplicate row"),
		Model->GetSelectedFlipbookIndex(), SelectedIndex);
	TestEqual(TEXT("Completion preserves the authored owner name"),
		Model->GetDirectionalPreview().BaseAnimation.FallbackName,
		FString(TEXT("DirectionalSecond")));

	Model->InitializeFromAsset(nullptr);
	Model.Reset();
	DirectionalModel_PumpAsync();
	TestTrue(TEXT("Duplicate-base cold fixture cleans up"), ColdAssets.Cleanup(FixtureError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalPlaybackEmptyResumeTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Playback.EmptyPausesAndResumesWhenRenderable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalPlaybackEmptyResumeTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusEditor::DirectionalPreviewPlayback;

	const FDecision Empty = Resolve(
		/*bIsPlaying=*/true,
		/*bResumeAfterDirectionalPreviewResolves=*/false,
		EEvent::BecameEmpty);
	TestFalse(TEXT("An exact empty direction pauses visible playback"), Empty.bShouldBePlaying);
	TestTrue(TEXT("An exact empty direction remembers that playback was active"),
		Empty.bResumeAfterDirectionalPreviewResolves);

	const FDecision Occupied = Resolve(
		Empty.bShouldBePlaying,
		Empty.bResumeAfterDirectionalPreviewResolves,
		EEvent::BecameRenderable);
	TestTrue(TEXT("Selecting an occupied direction resumes the remembered playback"),
		Occupied.bShouldBePlaying);
	TestFalse(TEXT("The consumed resume request is cleared"),
		Occupied.bResumeAfterDirectionalPreviewResolves);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalPlaybackEmptyCancellationTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Playback.ToggleWhileEmptyCancelsPendingResume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalPlaybackEmptyCancellationTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusEditor::DirectionalPreviewPlayback;

	const FDecision Empty = Resolve(
		/*bIsPlaying=*/true,
		/*bResumeAfterDirectionalPreviewResolves=*/false,
		EEvent::BecameEmpty);
	const FDecision Cancelled = Resolve(
		Empty.bShouldBePlaying,
		Empty.bResumeAfterDirectionalPreviewResolves,
		EEvent::UserToggleWhilePaused);
	TestFalse(TEXT("A toggle cannot start invisible playback while the exact direction is empty"),
		Cancelled.bShouldBePlaying);
	TestFalse(TEXT("The toggle cancels the queued resume while the exact direction is empty"),
		Cancelled.bResumeAfterDirectionalPreviewResolves);

	const FDecision OccupiedAfterCancellation = Resolve(
		Cancelled.bShouldBePlaying,
		Cancelled.bResumeAfterDirectionalPreviewResolves,
		EEvent::BecameRenderable);
	TestFalse(TEXT("A later occupied direction stays stopped after that cancellation"),
		OccupiedAfterCancellation.bShouldBePlaying);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalPlaybackUnavailableClearsResumeTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Playback.UnavailableClearsPendingResume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalPlaybackUnavailableClearsResumeTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusEditor::DirectionalPreviewPlayback;

	const FDecision Resolving = Resolve(
		/*bIsPlaying=*/true,
		/*bResumeAfterDirectionalPreviewResolves=*/false,
		EEvent::BeginResolving);
	const FDecision Unavailable = Resolve(
		Resolving.bShouldBePlaying,
		Resolving.bResumeAfterDirectionalPreviewResolves,
		EEvent::BecameUnavailable);
	TestFalse(TEXT("An unavailable directional preview remains stopped"),
		Unavailable.bShouldBePlaying);
	TestFalse(TEXT("An unavailable directional preview discards the queued resume"),
		Unavailable.bResumeAfterDirectionalPreviewResolves);

	const FDecision LaterRenderable = Resolve(
		Unavailable.bShouldBePlaying,
		Unavailable.bResumeAfterDirectionalPreviewResolves,
		EEvent::BecameRenderable);
	TestFalse(TEXT("A later renderable preview does not revive playback after unavailability"),
		LaterRenderable.bShouldBePlaying);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalPlaybackPendingResumeCancellationTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Playback.ToggleWhileResolvingCancelsPendingResume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalPlaybackPendingResumeCancellationTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusEditor::DirectionalPreviewPlayback;

	const FDecision Resolving = Resolve(
		/*bIsPlaying=*/true,
		/*bResumeAfterDirectionalPreviewResolves=*/false,
		EEvent::BeginResolving);
	TestFalse(TEXT("Resolving stops visible playback in every playback-capable Profile tool"),
		Resolving.bShouldBePlaying);
	TestTrue(TEXT("Resolving queues the previously active preview to resume"),
		Resolving.bResumeAfterDirectionalPreviewResolves);

	const FDecision Cancelled = Resolve(
		Resolving.bShouldBePlaying,
		Resolving.bResumeAfterDirectionalPreviewResolves,
		EEvent::UserToggleWhilePaused);
	TestFalse(TEXT("The user's toggle keeps playback visibly stopped while resolution is pending"),
		Cancelled.bShouldBePlaying);
	TestFalse(TEXT("The user's toggle explicitly cancels the queued automatic resume"),
		Cancelled.bResumeAfterDirectionalPreviewResolves);

	const FDecision CompletedAfterCancellation = Resolve(
		Cancelled.bShouldBePlaying,
		Cancelled.bResumeAfterDirectionalPreviewResolves,
		EEvent::BecameRenderable);
	TestFalse(TEXT("Sprite, Frame Timing, Frame Cues, and Root Motion remain stopped after resolution"),
		CompletedAfterCancellation.bShouldBePlaying);

	const FDecision CompletedWithoutCancellation = Resolve(
		Resolving.bShouldBePlaying,
		Resolving.bResumeAfterDirectionalPreviewResolves,
		EEvent::BecameRenderable);
	TestTrue(TEXT("Without the intervening toggle, the same resolution still resumes playback"),
		CompletedWithoutCancellation.bShouldBePlaying);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusDirectionalCanvasExtentAnchorTest,
	"Paper2DPlus.DirectionalAnimation.Editor.Model.CanvasExtentAnchorsToBase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusDirectionalCanvasExtentAnchorTest::RunTest(const FString& Parameters)
{
	// The zoom/extent source must anchor to the canonical BASE flipbook. Before the fix, a null
	// directional preview (Empty/Resolving/Unavailable bearing) collapsed it to the 128x128
	// fallback, visibly rescaling base-owned hitboxes purely from moving the wheel. A resident
	// spriteless base computes (1,1) — distinct from the 128 fallback — so the anchor decision is
	// observable without authoring texture fixtures.
	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>();
	UPaperFlipbook* Base = NewObject<UPaperFlipbook>(Profile, TEXT("ExtentAnchorBase"));
	FFlipbookProfileEntry& Entry = Profile->Flipbooks.AddDefaulted_GetRef();
	Entry.Identity.FlipbookName = TEXT("ExtentAnchor");
	Entry.Identity.Flipbook = Base;

	UPaperFlipbook* PreviewValue = nullptr;
	const TSharedRef<SCharacterProfileEditorCanvas> Canvas =
		SNew(SCharacterProfileEditorCanvas)
		.Asset(Profile)
		.SelectedFlipbookIndex(0)
		.SelectedFrameIndex(0)
		.PreviewFlipbook_Lambda([&PreviewValue]() { return PreviewValue; });

	const FVector2D BaseDims = Canvas->GetLargestSpriteDimsForTests();
	TestEqual(TEXT("A resident base yields its own computed extent, not the 128 fallback"),
		BaseDims, FVector2D(1.0, 1.0));

	// Simulate an empty/resolving bearing: the preview attribute publishes null while the base
	// stays resident. The extent must not move.
	Canvas->ResetCachedGeometry();
	PreviewValue = nullptr;
	TestEqual(TEXT("A null directional preview leaves the extent anchored to base"),
		Canvas->GetLargestSpriteDimsForTests(), BaseDims);

	// A resident variant may not change the extent either: valid variants are geometry-identical
	// to base by the compatibility gate, so base remains the one authority.
	UPaperFlipbook* Variant = NewObject<UPaperFlipbook>(Profile, TEXT("ExtentAnchorVariant"));
	Canvas->ResetCachedGeometry();
	PreviewValue = Variant;
	TestEqual(TEXT("A resident variant preview leaves the extent anchored to base"),
		Canvas->GetLargestSpriteDimsForTests(), BaseDims);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
