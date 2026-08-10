// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CharacterProfileEditorModel.h"
#include "BlueprintEditorSettings.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphNode_Comment.h"
#include "Editor.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/ComponentDelegateBinding.h"
#include "Engine/TimelineTemplate.h"
#include "BlueprintEditorTabs.h"
#include "FrameCueDataProvider.h"
#include "FrameEventEditor.h"
#include "Paper2DPlusEditorTestFrameCueTypes.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameCues/Paper2DPlusFrameCueDispatch.h"
#include "FrameCues/Paper2DPlusFrameCueBlueprint.h"
#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "FrameCues/Paper2DPlusFrameCueTypeEditor.h"
#include "Framework/Commands/InputBindingManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IDetailsView.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_Event.h"
#include "K2Node_Timeline.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Layout/Geometry.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/App.h"
#include "Misc/Guid.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "ScopedTransaction.h"
#include "SKismetInspector.h"
#include "SMyBlueprint.h"
#include "Templates/UnrealTemplate.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "UObject/GarbageCollection.h"
#include "UObject/ObjectKey.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
#include "Subsystems/AssetEditorSubsystem.h"
#endif

namespace Paper2DPlusFrameCueTypeFeasibilityTest
{
	UPaper2DPlusFrameCueBlueprint* MakeCueBlueprint(UClass* ParentClass, const TCHAR* Stem)
	{
		const FName AssetName = MakeUniqueObjectName(
			GetTransientPackage(), UPaper2DPlusFrameCueBlueprint::StaticClass(), FName(Stem));
		return Cast<UPaper2DPlusFrameCueBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			ParentClass,
			GetTransientPackage(),
			AssetName,
			BPTYPE_Normal,
			UPaper2DPlusFrameCueBlueprint::StaticClass(),
			UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass(),
			NAME_None));
	}

	UTimelineTemplate* InjectUnsupportedTimeline(
		UBlueprint& Blueprint,
		const FName TimelineVariableName)
	{
		// Unreal's AddNewTimeline correctly rejects UObject-derived Cue Types. Inject the same
		// serialized shape directly so rollback and legacy-quarantine paths still exercise corrupt
		// or historical assets that contain a timeline despite that engine-level restriction.
		if (!Blueprint.GeneratedClass)
		{
			return nullptr;
		}
		const FName TemplateName(
			*UTimelineTemplate::TimelineVariableNameToTemplateName(TimelineVariableName));
		UTimelineTemplate* Timeline = NewObject<UTimelineTemplate>(
			Blueprint.GeneratedClass,
			TemplateName,
			RF_Transactional);
		if (Timeline)
		{
			Blueprint.Timelines.Add(Timeline);
		}
		return Timeline;
	}

	FBPVariableDescription* FindVariable(UBlueprint& Blueprint, FName VariableName)
	{
		return Blueprint.NewVariables.FindByPredicate(
			[VariableName](const FBPVariableDescription& Description)
			{
				return Description.VarName == VariableName;
			});
	}

	uint32 RotateRight(uint32 Value, uint32 BitCount)
	{
		return (Value >> BitCount) | (Value << (32 - BitCount));
	}

	FString HashBytesSha256(const uint8* Data, int32 DataSize)
	{
		static constexpr uint32 RoundConstants[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
			0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
			0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
			0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
			0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
			0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
			0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
			0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
			0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
		};
		uint32 State[8] = {
			0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
			0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
		};

		TArray<uint8> Message;
		Message.Reserve(DataSize + 72);
		if (DataSize > 0)
		{
			Message.Append(Data, DataSize);
		}
		Message.Add(0x80);
		while ((Message.Num() + 8) % 64 != 0)
		{
			Message.Add(0);
		}
		const uint64 BitLength = static_cast<uint64>(DataSize) * 8;
		for (int32 Shift = 56; Shift >= 0; Shift -= 8)
		{
			Message.Add(static_cast<uint8>(BitLength >> Shift));
		}

		for (int32 Offset = 0; Offset < Message.Num(); Offset += 64)
		{
			uint32 Schedule[64] = {};
			for (int32 Index = 0; Index < 16; ++Index)
			{
				const int32 ByteOffset = Offset + Index * 4;
				Schedule[Index] =
					(static_cast<uint32>(Message[ByteOffset]) << 24)
					| (static_cast<uint32>(Message[ByteOffset + 1]) << 16)
					| (static_cast<uint32>(Message[ByteOffset + 2]) << 8)
					| static_cast<uint32>(Message[ByteOffset + 3]);
			}
			for (int32 Index = 16; Index < 64; ++Index)
			{
				const uint32 Sigma0 =
					RotateRight(Schedule[Index - 15], 7)
					^ RotateRight(Schedule[Index - 15], 18)
					^ (Schedule[Index - 15] >> 3);
				const uint32 Sigma1 =
					RotateRight(Schedule[Index - 2], 17)
					^ RotateRight(Schedule[Index - 2], 19)
					^ (Schedule[Index - 2] >> 10);
				Schedule[Index] =
					Schedule[Index - 16] + Sigma0 + Schedule[Index - 7] + Sigma1;
			}

			uint32 A = State[0];
			uint32 B = State[1];
			uint32 C = State[2];
			uint32 D = State[3];
			uint32 E = State[4];
			uint32 F = State[5];
			uint32 G = State[6];
			uint32 H = State[7];
			for (int32 Index = 0; Index < 64; ++Index)
			{
				const uint32 Choice = (E & F) ^ (~E & G);
				const uint32 Majority = (A & B) ^ (A & C) ^ (B & C);
				const uint32 Sum0 =
					RotateRight(A, 2) ^ RotateRight(A, 13) ^ RotateRight(A, 22);
				const uint32 Sum1 =
					RotateRight(E, 6) ^ RotateRight(E, 11) ^ RotateRight(E, 25);
				const uint32 Temporary1 =
					H + Sum1 + Choice + RoundConstants[Index] + Schedule[Index];
				const uint32 Temporary2 = Sum0 + Majority;
				H = G;
				G = F;
				F = E;
				E = D + Temporary1;
				D = C;
				C = B;
				B = A;
				A = Temporary1 + Temporary2;
			}
			State[0] += A;
			State[1] += B;
			State[2] += C;
			State[3] += D;
			State[4] += E;
			State[5] += F;
			State[6] += G;
			State[7] += H;
		}

		FString Result;
		Result.Reserve(64);
		for (const uint32 Word : State)
		{
			for (int32 Shift = 24; Shift >= 0; Shift -= 8)
			{
				Result += FString::Printf(TEXT("%02x"), (Word >> Shift) & 0xff);
			}
		}
		return Result;
	}

	FString HashUtf8Sha256(const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		return HashBytesSha256(
			reinterpret_cast<const uint8*>(Utf8.Get()),
			Utf8.Length());
	}

	bool AddEditableIntPayload(
		UBlueprint& Blueprint,
		FName VariableName,
		const FString& DefaultValue,
		FGuid* OutGuid = nullptr)
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Int;
		if (!FBlueprintEditorUtils::AddMemberVariable(
			&Blueprint, VariableName, Type, DefaultValue))
		{
			return false;
		}
		FBPVariableDescription* Variable = FindVariable(Blueprint, VariableName);
		if (!Variable)
		{
			return false;
		}
		Variable->PropertyFlags &= ~CPF_DisableEditOnInstance;
		if (OutGuid)
		{
			*OutGuid = Variable->VarGuid;
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		return true;
	}

	bool AddEditableFloatPayload(
		UBlueprint& Blueprint,
		FName VariableName,
		const FString& DefaultValue)
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Real;
		Type.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		if (!FBlueprintEditorUtils::AddMemberVariable(
			&Blueprint, VariableName, Type, DefaultValue))
		{
			return false;
		}
		FBPVariableDescription* Variable = FindVariable(Blueprint, VariableName);
		if (!Variable)
		{
			return false;
		}
		Variable->PropertyFlags &= ~CPF_DisableEditOnInstance;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		return true;
	}

	bool AddEditableVectorArrayPayload(
		UBlueprint& Blueprint,
		FName VariableName,
		const FString& DefaultValue)
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Struct;
		Type.PinSubCategoryObject = TBaseStructure<FVector>::Get();
		Type.ContainerType = EPinContainerType::Array;
		if (!FBlueprintEditorUtils::AddMemberVariable(
			&Blueprint, VariableName, Type, DefaultValue))
		{
			return false;
		}
		FBPVariableDescription* Variable = FindVariable(Blueprint, VariableName);
		if (!Variable)
		{
			return false;
		}
		Variable->PropertyFlags &= ~CPF_DisableEditOnInstance;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		return true;
	}

	bool AddEditableSoftClassPayload(
		UBlueprint& Blueprint,
		FName VariableName,
		const FString& DefaultValue)
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
		Type.PinSubCategoryObject = AActor::StaticClass();
		if (!FBlueprintEditorUtils::AddMemberVariable(
			&Blueprint, VariableName, Type, DefaultValue))
		{
			return false;
		}
		FBPVariableDescription* Variable = FindVariable(Blueprint, VariableName);
		if (!Variable)
		{
			return false;
		}
		Variable->PropertyFlags &= ~CPF_DisableEditOnInstance;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		return true;
	}

	FArrayProperty* FindVectorArrayProperty(const UClass* Class, FName PropertyName)
	{
		FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(Class, PropertyName);
		const FStructProperty* InnerStruct = ArrayProperty
			? CastField<FStructProperty>(ArrayProperty->Inner)
			: nullptr;
		return InnerStruct && InnerStruct->Struct == TBaseStructure<FVector>::Get()
			? ArrayProperty
			: nullptr;
	}

	FSoftClassProperty* FindActorSoftClassProperty(const UClass* Class, FName PropertyName)
	{
		FSoftClassProperty* SoftClassProperty =
			FindFProperty<FSoftClassProperty>(Class, PropertyName);
		return SoftClassProperty && SoftClassProperty->MetaClass == AActor::StaticClass()
			? SoftClassProperty
			: nullptr;
	}

	bool VectorArrayEquals(
		const FArrayProperty& ArrayProperty,
		const void* Container,
		const TArray<FVector>& Expected)
	{
		FScriptArrayHelper Helper(
			&ArrayProperty,
			ArrayProperty.ContainerPtrToValuePtr<void>(Container));
		if (Helper.Num() != Expected.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			if (!reinterpret_cast<const FVector*>(Helper.GetRawPtr(Index))->Equals(
				Expected[Index]))
			{
				return false;
			}
		}
		return true;
	}

	void SetVectorArray(
		const FArrayProperty& ArrayProperty,
		void* Container,
		const TArray<FVector>& Values)
	{
		FScriptArrayHelper Helper(
			&ArrayProperty,
			ArrayProperty.ContainerPtrToValuePtr<void>(Container));
		Helper.Resize(Values.Num());
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			*reinterpret_cast<FVector*>(Helper.GetRawPtr(Index)) = Values[Index];
		}
	}

	FSoftObjectPath GetSoftClassPath(
		const FSoftClassProperty& SoftClassProperty,
		const void* Container)
	{
		const FSoftObjectPtr* Value =
			SoftClassProperty.ContainerPtrToValuePtr<FSoftObjectPtr>(Container);
		return Value ? Value->ToSoftObjectPath() : FSoftObjectPath();
	}

	void SetSoftClass(
		const FSoftClassProperty& SoftClassProperty,
		void* Container,
		UClass* Value)
	{
		FSoftObjectPtr* SoftValue =
			SoftClassProperty.ContainerPtrToValuePtr<FSoftObjectPtr>(Container);
		if (SoftValue)
		{
			*SoftValue = FSoftObjectPtr(Value);
		}
	}

	UPaper2DPlusFrameCueBlueprint* MakeCompiledCueBlueprint(
		UClass* ParentClass,
		const TCHAR* Stem,
		UObject* Outer = nullptr,
		FGuid* OutPowerGuid = nullptr)
	{
		Outer = Outer ? Outer : GetTransientPackage();
		const FName RequestedName(Stem);
		const FName AssetName = FindObject<UObject>(Outer, Stem)
			? MakeUniqueObjectName(
				Outer, UPaper2DPlusFrameCueBlueprint::StaticClass(), RequestedName)
			: RequestedName;
		UPaper2DPlusFrameCueBlueprint* Blueprint =
			Cast<UPaper2DPlusFrameCueBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				ParentClass,
				Outer,
				AssetName,
				BPTYPE_Normal,
				UPaper2DPlusFrameCueBlueprint::StaticClass(),
				UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass(),
				NAME_None));
		if (!Blueprint
			|| !FPaper2DPlusFrameCueTypeEditor::PrepareNewlyCreatedBlueprint(*Blueprint)
			|| !AddEditableIntPayload(*Blueprint, TEXT("Power"), TEXT("12"), OutPowerGuid))
		{
			return nullptr;
		}
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return Blueprint->Status == BS_Error ? nullptr : Blueprint;
	}

	UK2Node_CallFunction* InjectDeferredCall(
		UPaper2DPlusFrameCueBlueprint& Blueprint,
		const FName FunctionName)
	{
		UEdGraph* Graph =
			FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(Blueprint);
		if (!Graph)
		{
			return nullptr;
		}
		FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
		UK2Node_CallFunction* Node = Creator.CreateNode(false);
		Node->FunctionReference.SetExternalMember(
			FunctionName,
			UKismetSystemLibrary::StaticClass());
		Creator.Finalize();
		return Node;
	}

	struct FScopedRoot
	{
		explicit FScopedRoot(UObject* InObject)
			: Object(InObject)
		{
			if (Object)
			{
				Object->AddToRoot();
			}
		}

		~FScopedRoot()
		{
			if (Object && Object->IsRooted())
			{
				Object->RemoveFromRoot();
			}
		}

		UObject* Object = nullptr;
	};

	struct FScopedTransactionHistory
	{
		explicit FScopedTransactionHistory(const FText& InResetReason)
			: ResetReason(InResetReason)
		{
			GEditor->ResetTransaction(ResetReason);
		}

		~FScopedTransactionHistory()
		{
			GEditor->ResetTransaction(ResetReason);
		}

		FText ResetReason;
	};

	struct FScopedReplacementCapture
	{
		explicit FScopedReplacementCapture(UObject* Watched)
			: WatchedKey(Watched)
		{
			Handle = FCoreUObjectDelegates::OnObjectsReplaced.AddRaw(
				this, &FScopedReplacementCapture::HandleObjectsReplaced);
		}

		~FScopedReplacementCapture()
		{
			FCoreUObjectDelegates::OnObjectsReplaced.Remove(Handle);
		}

		void HandleObjectsReplaced(
			const FCoreUObjectDelegates::FReplacementObjectMap& Replacements)
		{
			for (const TPair<UObject*, UObject*>& Pair : Replacements)
			{
				if (FObjectKey(Pair.Key) == WatchedKey)
				{
					++HitCount;
					ReplacementKey = FObjectKey(Pair.Value);
				}
			}
		}

		FObjectKey WatchedKey;
		TOptional<FObjectKey> ReplacementKey;
		int32 HitCount = 0;
		FDelegateHandle Handle;
	};

	struct FScopedCueTypeFixturePackage
	{
		FString PackageName;
		FString FilePath;
		FString PreparationError;
		FAutomationTestBase& Test;
		bool bRetainAfterSuccess = false;
		bool bSucceeded = false;
		bool bPrepared = false;
		bool bOwnsFixturePath = true;

		FScopedCueTypeFixturePackage(
			FAutomationTestBase& InTest,
			const FString& Stem,
			bool bInRetainAfterSuccess,
			const FString& RetainedPackageName = FString(),
			bool bRequireUnusedExactPackageName = false)
			: Test(InTest)
			, bRetainAfterSuccess(bInRetainAfterSuccess)
		{
			if (bRequireUnusedExactPackageName)
			{
				PackageName = RetainedPackageName;
				bOwnsFixturePath = false;
			}
			else if (bRetainAfterSuccess)
			{
				PackageName = RetainedPackageName.IsEmpty()
					? TEXT("/Game/Paper2DPlusAutomation/P2DPCueTypeCookedRuntimeFixture")
					: RetainedPackageName;
			}
			else
			{
				const FGuid Guid = FGuid::NewGuid();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
				const FString GuidString = Guid.ToString(EGuidFormats::Digits).ToLower();
#else
				const FString GuidString = Guid.ToString(EGuidFormats::DigitsLower);
#endif
				PackageName = FString::Printf(
					TEXT("/Game/__AutomationTemp__/%s_%s"), *Stem, *GuidString);
			}
			FilePath = FPackageName::LongPackageNameToFilename(
				PackageName, FPackageName::GetAssetPackageExtension());

			if (bRequireUnusedExactPackageName)
			{
				bPrepared = !PackageName.IsEmpty()
					&& FindPackage(nullptr, *PackageName) == nullptr
					&& !AnyFileExists();
				if (!bPrepared)
				{
					PreparationError = FString::Printf(
						TEXT("Refusing to overwrite pre-existing package state at exact fixture path '%s'."),
						*FilePath);
				}
				else
				{
					IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
				}
				return;
			}

			FText UnloadError;
			const bool bUnloaded = Unload(UnloadError);
			const bool bDeleted = DeleteFiles();
			bPrepared = bUnloaded && bDeleted;
			if (!bPrepared)
			{
				PreparationError = FString::Printf(
					TEXT("Could not clean prior fixture state at '%s'. Unload: %s; files removed: %s."),
					*FilePath,
					*UnloadError.ToString(),
					bDeleted ? TEXT("true") : TEXT("false"));
			}
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
		}

		~FScopedCueTypeFixturePackage()
		{
			if (!bOwnsFixturePath)
			{
				return;
			}

			FText UnloadError;
			if (!Unload(UnloadError))
			{
				Test.AddError(FString::Printf(
					TEXT("Cue Type fixture cleanup could not unload '%s': %s"),
					*PackageName,
					*UnloadError.ToString()));
			}
			if (!bRetainAfterSuccess || !bSucceeded)
			{
				if (!DeleteFiles())
				{
					Test.AddError(FString::Printf(
						TEXT("Cue Type fixture cleanup left package files at '%s'."),
						*FilePath));
				}
			}
		}

		void ClaimUnusedExactPackagePath()
		{
			check(bPrepared && !bOwnsFixturePath);
			bOwnsFixturePath = true;
		}

		void MarkSucceeded()
		{
			bSucceeded = true;
		}

		bool IsPrepared() const
		{
			return bPrepared;
		}

		/** How many attempts a stubborn cleanup step gets before it is reported as a failure. */
		static constexpr int32 CleanupAttempts = 5;

		/** Releases whatever still holds the just-saved package: async loads first, then garbage. */
		static void FlushPackageHolders()
		{
			FlushAsyncLoading();
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}

		bool Unload(FText& OutError) const
		{
			// A package saved moments ago can still be held by an in-flight async load or by objects
			// awaiting collection. Both resolve on their own; retry across a flush rather than reporting
			// a cleanup failure the first time one of them is still in progress.
			for (int32 Attempt = 0; Attempt < CleanupAttempts; ++Attempt)
			{
				UPackage* Package = FindPackage(nullptr, *PackageName);
				if (!Package)
				{
					return true;
				}
				if (Package->IsRooted())
				{
					Package->RemoveFromRoot();
				}
				Package->SetDirtyFlag(false);
				TArray<UPackage*> PackagesToUnload = { Package };
				const bool bUnloaded = UPackageTools::UnloadPackages(
					PackagesToUnload, OutError, true);
				FlushPackageHolders();
				if (bUnloaded && FindPackage(nullptr, *PackageName) == nullptr)
				{
					return true;
				}
			}
			return FindPackage(nullptr, *PackageName) == nullptr;
		}

		bool AnyFileExists() const
		{
			IFileManager& FileManager = IFileManager::Get();
			const TArray<FString> Paths = {
				FilePath,
				FPaths::ChangeExtension(FilePath, TEXT("uexp")),
				FPaths::ChangeExtension(FilePath, TEXT("ubulk")),
				FPaths::ChangeExtension(FilePath, TEXT("uptnl"))
			};
			return Paths.ContainsByPredicate(
				[&FileManager](const FString& Path)
				{
					return FileManager.FileExists(*Path);
				});
		}

		bool DeleteFiles() const
		{
			IFileManager& FileManager = IFileManager::Get();
			const TArray<FString> Paths = {
				FilePath,
				FPaths::ChangeExtension(FilePath, TEXT("uexp")),
				FPaths::ChangeExtension(FilePath, TEXT("ubulk")),
				FPaths::ChangeExtension(FilePath, TEXT("uptnl"))
			};
			auto AnyFileRemains = [&FileManager, &Paths]()
			{
				return Paths.ContainsByPredicate(
					[&FileManager](const FString& Path)
					{
						return FileManager.FileExists(*Path);
					});
			};

			// Windows refuses to delete a file while any handle is still open on it, and the loader can
			// hold one for a short while after a save. Flush and retry a bounded number of times so a
			// transient lock is not reported as a leaked fixture.
			for (int32 Attempt = 0; Attempt < CleanupAttempts; ++Attempt)
			{
				if (Attempt > 0)
				{
					FlushPackageHolders();
					FPlatformProcess::Sleep(0.05f);
				}
				for (const FString& Path : Paths)
				{
					FileManager.Delete(*Path, false, true, true);
				}
				if (!AnyFileRemains())
				{
					FileManager.DeleteDirectory(*FPaths::GetPath(FilePath), false, false);
					return true;
				}
			}
			return false;
		}
	};

	struct FScopedJumpToNodeErrors
	{
		FScopedJumpToNodeErrors()
			: Settings(GetMutableDefault<UBlueprintEditorSettings>())
			, bPreviousValue(Settings ? Settings->bJumpToNodeErrors : false)
		{
			if (Settings)
			{
				Settings->bJumpToNodeErrors = true;
			}
		}

		~FScopedJumpToNodeErrors()
		{
			if (Settings)
			{
				Settings->bJumpToNodeErrors = bPreviousValue;
			}
		}

		UBlueprintEditorSettings* Settings = nullptr;
		bool bPreviousValue = false;
	};

	struct FScopedPersistentSaveGuardBypass
	{
		FScopedPersistentSaveGuardBypass()
		{
			UPaper2DPlusFrameCueBlueprint::SetPersistentSaveGuardBypassForAutomation(true);
		}

		~FScopedPersistentSaveGuardBypass()
		{
			UPaper2DPlusFrameCueBlueprint::SetPersistentSaveGuardBypassForAutomation(false);
		}

		FScopedPersistentSaveGuardBypass(const FScopedPersistentSaveGuardBypass&) = delete;
		FScopedPersistentSaveGuardBypass& operator=(
			const FScopedPersistentSaveGuardBypass&) = delete;
	};

	struct FScopedCueTypeEditorClose
	{
		explicit FScopedCueTypeEditorClose(
			const TSharedRef<FPaper2DPlusFrameCueTypeEditor>& InEditor)
			: Editor(InEditor)
		{
		}

		~FScopedCueTypeEditorClose()
		{
			Close();
		}

		void Close()
		{
			if (!Editor.IsValid())
			{
				return;
			}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
			Editor->CloseWindow(EAssetEditorCloseReason::AssetUnloadingOrInvalid);
#else
			Editor->CloseWindow();
#endif
			Editor.Reset();
		}

		TSharedPtr<FPaper2DPlusFrameCueTypeEditor> Editor;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeEnvelopeFeasibilityTest,
	"Paper2DPlus.FrameCues.CueType.Feasibility.PersistentEnvelope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeEnvelopeFeasibilityTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeFeasibilityTest;

	UPaper2DPlusFrameCueBlueprint* MomentBlueprint = MakeCueBlueprint(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_FeasibilityMoment"));
	if (!TestNotNull(TEXT("Specialized Cue Blueprint is created"), MomentBlueprint))
	{
		return false;
	}

	TestTrue(TEXT("Envelope supports payload variables"), MomentBlueprint->SupportsGlobalVariables());
	TestTrue(TEXT("Editor module installed the direct-compile validation bridge"),
		UPaper2DPlusFrameCueBlueprint::IsCompileValidationDelegateBound());
	TestTrue(TEXT("Editor module installed the save/cook schema-parity bridge"),
		UPaper2DPlusFrameCueBlueprint::IsCompiledSchemaValidationDelegateBound());
	TestFalse(TEXT("Envelope does not support local variables"), MomentBlueprint->SupportsLocalVariables());
	TestFalse(TEXT("Envelope does not support functions"), MomentBlueprint->SupportsFunctions());
	TestFalse(TEXT("Envelope does not support macros"), MomentBlueprint->SupportsMacros());
	TestFalse(TEXT("Envelope does not support delegates"), MomentBlueprint->SupportsDelegates());
	TestTrue(TEXT("Envelope supports the permitted Cue behavior event graph"),
		MomentBlueprint->SupportsEventGraphs());
#if !UE_VERSION_OLDER_THAN(5, 1, 0)
	const UFunction* DeclaredCueEvent =
		UPaper2DPlusCue::StaticClass()->FindFunctionByName(TEXT("OnCueTriggered"));
	TestNotNull(TEXT("Cue base declares On Cue Triggered"), DeclaredCueEvent);
	TestFalse(TEXT("Stock My Blueprint cannot implement even a declared Cue event"),
		MomentBlueprint->AllowFunctionOverride(DeclaredCueEvent));
	TestFalse(TEXT("Stock My Blueprint rejects a null override candidate"),
		MomentBlueprint->AllowFunctionOverride(nullptr));
#endif
	TestTrue(TEXT("Cue Type editor accepts the specialized Blueprint"),
		FPaper2DPlusFrameCueTypeEditor::IsSupportedBlueprint(MomentBlueprint));
	const uint64 ExpectedForbiddenPayloadFlags =
		CPF_Transient
		| CPF_Config
		| CPF_GlobalConfig
		| CPF_DuplicateTransient
		| CPF_Deprecated
		| CPF_NonTransactional
		| CPF_EditorOnly
		| CPF_TextExportTransient
		| CPF_NonPIEDuplicateTransient
		| CPF_SkipSerialization;
	TestEqual(TEXT("Cue payload authority publishes the exact persistence-forbidden flag mask"),
		UPaper2DPlusFrameCueBlueprint::GetForbiddenPayloadPropertyFlags(),
		ExpectedForbiddenPayloadFlags);

	const FName OrdinaryBlueprintName = MakeUniqueObjectName(
		GetTransientPackage(),
		UBlueprint::StaticClass(),
		FName(TEXT("BP_P2DP_OrdinaryCueBlueprint")));
	UBlueprint* OrdinaryCueBlueprint = FKismetEditorUtilities::CreateBlueprint(
		UPaper2DPlusCue::StaticClass(),
		GetTransientPackage(),
		OrdinaryBlueprintName,
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		NAME_None);
	if (TestNotNull(TEXT("Ordinary Cue-derived Blueprint rejection fixture is created"),
		OrdinaryCueBlueprint))
	{
		TestTrue(TEXT("Restricted Cue Type editor accepts an ordinary Cue Blueprint for quarantined legacy recovery"),
			FPaper2DPlusFrameCueTypeEditor::IsSupportedBlueprint(OrdinaryCueBlueprint));
		FText OrdinaryContractError;
		TestFalse(TEXT("Shared contract rejects an ordinary Cue-derived UBlueprint asset"),
			UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(
				*OrdinaryCueBlueprint,
				&OrdinaryContractError));
		TestTrue(TEXT("Ordinary Blueprint rejection identifies the specialized envelope"),
			OrdinaryContractError.ToString().Contains(
				TEXT("Paper2D+ restricted behavior-capable Blueprint envelope")));

		UEdGraph* OrdinaryBehaviorGraph = FBlueprintEditorUtils::CreateNewGraph(
			OrdinaryCueBlueprint,
			TEXT("HiddenBehavior"),
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddFunctionGraph(
			OrdinaryCueBlueprint,
			OrdinaryBehaviorGraph,
			true,
			static_cast<UFunction*>(nullptr));
		FKismetEditorUtilities::CompileBlueprint(OrdinaryCueBlueprint);
		UClass* OrdinaryGeneratedClass = OrdinaryCueBlueprint->GeneratedClass;
		if (TestNotNull(
			TEXT("Ordinary behavior parent fixture compiles a generated class"),
			OrdinaryGeneratedClass))
		{
			const EClassFlags SavedOrdinaryClassFlags =
				OrdinaryGeneratedClass->ClassFlags;
			OrdinaryGeneratedClass->ClassFlags = static_cast<EClassFlags>(
				OrdinaryGeneratedClass->ClassFlags & ~CLASS_CompiledFromBlueprint);
			UPaper2DPlusFrameCueBlueprint* ParentEscapeChild = MakeCueBlueprint(
				UPaper2DPlusCue::StaticClass(),
				TEXT("BP_P2DP_ParentFlagEscapeChild"));
			if (TestNotNull(
				TEXT("Parent flag escape child Cue Type fixture is created"),
				ParentEscapeChild))
			{
				ParentEscapeChild->ParentClass = OrdinaryGeneratedClass;
				FText ParentEscapeError;
				TestFalse(
					TEXT("Clearing an ordinary behavior parent's Blueprint flag cannot make it trusted"),
					UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(
						*ParentEscapeChild,
						&ParentEscapeError));
				TestTrue(
					TEXT("Tampered ordinary parent rejection identifies behavior-capable inheritance"),
					ParentEscapeError.ToString().Contains(
						TEXT("selected Blueprint parent is outside the restricted Cue Type contract")));
			}
			OrdinaryGeneratedClass->ClassFlags = SavedOrdinaryClassFlags;
		}
	}
	const TArray<FName> AuthoringTabs =
		FPaper2DPlusFrameCueTypeEditor::GetAuthoringTabIdsForTests();
	TestEqual(TEXT("Restricted editor registers exactly five allowlisted tabs"),
		AuthoringTabs.Num(), 5);
	TestTrue(TEXT("Restricted mode registers stock My Blueprint"),
		AuthoringTabs.Contains(FBlueprintEditorTabs::MyBlueprintID));
	TestTrue(TEXT("Restricted mode registers native Details"),
		AuthoringTabs.Contains(FBlueprintEditorTabs::DetailsID));
	TestTrue(TEXT("Restricted mode registers Class Defaults"),
		AuthoringTabs.Contains(FBlueprintEditorTabs::DefaultEditorID));
	TestTrue(TEXT("Restricted mode registers Compiler Results"),
		AuthoringTabs.Contains(FBlueprintEditorTabs::CompilerResultsID));
	TestTrue(TEXT("Restricted mode registers Find Results"),
		AuthoringTabs.Contains(FBlueprintEditorTabs::FindResultsID));
	TestFalse(TEXT("Restricted mode never exposes the graph palette"),
		AuthoringTabs.Contains(FBlueprintEditorTabs::PaletteID));
	TestEqual(TEXT("Restricted editor uses the v4 standard-toolbar layout key"),
		FPaper2DPlusFrameCueTypeEditor::StandardToolbarLayoutName,
		FName(TEXT("Paper2DPlusFrameCueTypeEditor_Layout_v4_StandardToolbar")));
	const TArray<FName> CueOverrideEvents =
		FPaper2DPlusFrameCueTypeEditor::GetCueOverrideEventNames(*MomentBlueprint);
	TestTrue(TEXT("Plugin Override control exposes exactly the declared Cue event"),
		CueOverrideEvents.Num() == 1
		&& CueOverrideEvents[0] == FName(TEXT("OnCueTriggered")));

	TestTrue(TEXT("New Cue Type is normalized to the runtime envelope"),
		FPaper2DPlusFrameCueTypeEditor::PrepareNewlyCreatedBlueprint(
			*MomentBlueprint));
	TestTrue(TEXT("Cue Type uses the cook-validating generated-class envelope"),
		MomentBlueprint->GeneratedClass
			&& MomentBlueprint->GeneratedClass->GetClass()->IsChildOf(
				UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass()));
	TestEqual(TEXT("New Cue Type has exactly one permitted behavior event graph"),
		MomentBlueprint->UbergraphPages.Num(), 1);
	TestNotNull(TEXT("New Cue Type pre-seeds its On Cue Triggered stub"),
		FPaper2DPlusFrameCueTypeEditor::FindCueBehaviorEventNode(
			*MomentBlueprint, TEXT("OnCueTriggered")));
	TestEqual(TEXT("New Cue Type has no function graphs"), MomentBlueprint->FunctionGraphs.Num(), 0);
	TestEqual(TEXT("New Cue Type has no macro graphs"), MomentBlueprint->MacroGraphs.Num(), 0);
	TestEqual(TEXT("New Cue Type has no delegate signature graphs"),
		MomentBlueprint->DelegateSignatureGraphs.Num(), 0);
	FText DataOnlyContractError;
	TestTrue(TEXT("New Cue Type satisfies the restricted behavior/payload contract"),
		UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(
			*MomentBlueprint,
			&DataOnlyContractError));
	TestTrue(TEXT("Valid Cue Type reports no structural contract error"),
		DataOnlyContractError.IsEmpty());

	UPaper2DPlusFrameCueBlueprint* ExistingBlueprint = MakeCueBlueprint(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_ExistingCueType"));
	UEdGraph* ExistingEmptyEventGraph =
		FPaper2DPlusFrameCueTypeAuthoring::EnsureBehaviorEventGraph(*ExistingBlueprint);
	ExistingBlueprint->bIsNewlyCreated = false;
	TestFalse(TEXT("Creation cleanup rejects an existing Cue Type asset"),
		FPaper2DPlusFrameCueTypeEditor::PrepareNewlyCreatedBlueprint(*ExistingBlueprint));
	TestTrue(TEXT("Creation cleanup preserves an existing asset's empty Event Graph"),
		ExistingBlueprint->UbergraphPages.Contains(ExistingEmptyEventGraph));
	TestTrue(TEXT("Structural contract accepts one permitted behavior event graph"),
		UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(
			*ExistingBlueprint,
			&DataOnlyContractError));

	// A second event graph is still behavior the one-graph envelope cannot bound.
	UEdGraph* SecondEventGraph = FBlueprintEditorUtils::CreateNewGraph(
		ExistingBlueprint,
		TEXT("SmuggledSecondEventGraph"),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddUbergraphPage(ExistingBlueprint, SecondEventGraph);
	TestFalse(TEXT("Structural contract rejects a second event graph"),
		UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(
			*ExistingBlueprint,
			&DataOnlyContractError));
	TestFalse(TEXT("Rejected extra event graph reports an actionable contract error"),
		DataOnlyContractError.IsEmpty());

	UPaper2DPlusFrameCueBlueprint* FunctionGraphBlueprint = MakeCueBlueprint(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_FunctionGraphCueType"));
	if (TestNotNull(TEXT("Function-graph rejection fixture is created"), FunctionGraphBlueprint))
	{
		UEdGraph* SmuggledFunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
			FunctionGraphBlueprint,
			TEXT("SmuggledFunction"),
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddFunctionGraph(
			FunctionGraphBlueprint,
			SmuggledFunctionGraph,
			true,
			static_cast<UFunction*>(nullptr));
		TestFalse(TEXT("Structural contract still rejects a function graph"),
			UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(
				*FunctionGraphBlueprint,
				&DataOnlyContractError));
	}

	UPaper2DPlusFrameCueBlueprint* RangeBlueprint = MakeCueBlueprint(
		UPaper2DPlusCueState::StaticClass(), TEXT("BP_P2DP_FeasibilityRange"));
	TestTrue(TEXT("Cue State Blueprint uses the same specialized envelope"),
		RangeBlueprint && RangeBlueprint->ParentClass == UPaper2DPlusCueState::StaticClass());
	if (RangeBlueprint
		&& TestTrue(TEXT("New Cue State Type normalizes"),
			FPaper2DPlusFrameCueTypeEditor::PrepareNewlyCreatedBlueprint(*RangeBlueprint)))
	{
		TestNotNull(TEXT("New Cue State Type pre-seeds its On Cue Begin stub"),
			FPaper2DPlusFrameCueTypeEditor::FindCueBehaviorEventNode(
				*RangeBlueprint, TEXT("OnCueBegin")));
		TestNotNull(TEXT("New Cue State Type pre-seeds its On Cue End stub"),
			FPaper2DPlusFrameCueTypeEditor::FindCueBehaviorEventNode(
				*RangeBlueprint, TEXT("OnCueEnd")));
		TestNull(TEXT("Opt-in On Cue Update is not pre-seeded"),
			FPaper2DPlusFrameCueTypeEditor::FindCueBehaviorEventNode(
				*RangeBlueprint, TEXT("OnCueUpdate")));
		FText BehaviorContractError;
		TestTrue(TEXT("Seeded Range behavior passes the compiled envelope"),
			UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
				*RangeBlueprint, &BehaviorContractError));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRestrictedEditorRegistersOnlyAllowlistedTabsTest,
	"Paper2DPlus.FrameCues.CueType.Feasibility.RestrictedEditorRegistersOnlyAllowlistedTabs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRestrictedEditorRegistersOnlyAllowlistedTabsTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeFeasibilityTest;

	UPaper2DPlusFrameCueBlueprint* Blueprint = MakeCueBlueprint(
		UPaper2DPlusCue::StaticClass(),
		TEXT("BP_P2DP_RegisteredCueTypeTabs"));
	if (!TestNotNull(TEXT("Registered-tab fixture Blueprint"), Blueprint)
		|| !TestTrue(
			TEXT("Registered-tab fixture normalizes"),
			FPaper2DPlusFrameCueTypeEditor::PrepareNewlyCreatedBlueprint(*Blueprint)))
	{
		return false;
	}
	FScopedRoot BlueprintRoot(Blueprint);
	const TSharedRef<FPaper2DPlusFrameCueTypeEditor> Editor =
		MakeShared<FPaper2DPlusFrameCueTypeEditor>();
	Editor->InitFrameCueTypeEditor(EToolkitMode::Standalone, nullptr, Blueprint);
	FScopedCueTypeEditorClose EditorClose(Editor);
	TestTrue(TEXT("Restricted Cue Type mode enables its safe scripting commands"),
		Editor->IsInAScriptingMode());

	const TArray<FName> DeclaredTabs =
		FPaper2DPlusFrameCueTypeEditor::GetAuthoringTabIdsForTests();
	const TArray<FName> RegisteredTabs =
		Editor->GetRegisteredAuthoringTabIdsForTests();
	TestEqual(TEXT("Restricted Cue Type editor declares exactly five tabs"),
		DeclaredTabs.Num(), 5);
	TestEqual(TEXT("Restricted Cue Type editor actually registers exactly five tabs"),
		RegisteredTabs.Num(), DeclaredTabs.Num());
	for (const FName DeclaredTab : DeclaredTabs)
	{
		TestTrue(
			*FString::Printf(
				TEXT("Declared tab %s is actually registered"),
				*DeclaredTab.ToString()),
			RegisteredTabs.Contains(DeclaredTab));
	}
	TestTrue(TEXT("My Blueprint is allowlisted"),
		RegisteredTabs.Contains(FBlueprintEditorTabs::MyBlueprintID));
	TestTrue(TEXT("Details is allowlisted"),
		RegisteredTabs.Contains(FBlueprintEditorTabs::DetailsID));
	TestTrue(TEXT("Class Defaults is allowlisted"),
		RegisteredTabs.Contains(FBlueprintEditorTabs::DefaultEditorID));
	TestTrue(TEXT("Compiler Results is allowlisted"),
		RegisteredTabs.Contains(FBlueprintEditorTabs::CompilerResultsID));
	TestTrue(TEXT("Find Results is allowlisted"),
		RegisteredTabs.Contains(FBlueprintEditorTabs::FindResultsID));
	TestTrue(TEXT("Find Results opens for the toolbar Find command"),
		Editor->GetTabManager()->TryInvokeTab(FBlueprintEditorTabs::FindResultsID).IsValid());
	TestFalse(TEXT("Palette remains excluded by default"),
		RegisteredTabs.Contains(FBlueprintEditorTabs::PaletteID));

	FName ParentToolbarName;
	const FName ToolbarName = Editor->GetToolMenuToolbarNameForMode(
		FPaper2DPlusFrameCueTypeEditor::CueTypeModeName,
		ParentToolbarName);
	UToolMenu* Toolbar = UToolMenus::Get()->FindMenu(ToolbarName);
	if (TestNotNull(TEXT("Restricted Cue Type mode registers its Blueprint toolbar"), Toolbar))
	{
		const TArray<TPair<FName, FName>> ExpectedSections{
			{TEXT("Compile"), TEXT("CompileCommands")},
			{TEXT("SourceControl"), TEXT("SourceControlCommands")},
			{TEXT("Script"), TEXT("ScriptCommands")},
			{TEXT("Settings"), TEXT("BlueprintGlobalOptions")},
			{TEXT("Debugging"), TEXT("DebuggingCommands")}
		};
		for (const TPair<FName, FName>& Expected : ExpectedSections)
		{
			FToolMenuSection* Section = Toolbar->FindSection(Expected.Key);
			if (TestNotNull(
				*FString::Printf(
					TEXT("Cue Type toolbar contains the standard %s section"),
					*Expected.Key.ToString()),
				Section))
			{
				TestNotNull(
					*FString::Printf(
						TEXT("Cue Type toolbar section %s contains its standard dynamic entry"),
						*Expected.Key.ToString()),
					Section->FindEntry(Expected.Value));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeOverrideOnCueParentSeedsParentCallTest,
	"Paper2DPlus.FrameCues.CueType.Feasibility.OverrideOnCueParentSeedsParentCall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeOverrideOnCueParentSeedsParentCallTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeFeasibilityTest;

	UPaper2DPlusFrameCueBlueprint* ParentBlueprint = MakeCueBlueprint(
		UPaper2DPlusCue::StaticClass(),
		TEXT("BP_P2DP_CueOverrideParent"));
	if (!TestNotNull(TEXT("Designer Cue parent fixture is created"), ParentBlueprint)
		|| !TestTrue(TEXT("Designer Cue parent is normalized"),
			FPaper2DPlusFrameCueTypeEditor::PrepareNewlyCreatedBlueprint(
				*ParentBlueprint)))
	{
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(ParentBlueprint);
	if (!TestNotNull(TEXT("Designer Cue parent compiles a class"),
		ParentBlueprint->GeneratedClass.Get()))
	{
		return false;
	}

	UPaper2DPlusFrameCueBlueprint* ChildBlueprint = MakeCueBlueprint(
		ParentBlueprint->GeneratedClass,
		TEXT("BP_P2DP_CueOverrideChild"));
	if (!TestNotNull(TEXT("Designer Cue child fixture is created"), ChildBlueprint))
	{
		return false;
	}

	TArray<UEdGraph*> InitialEventGraphs;
	for (UEdGraph* Graph : ChildBlueprint->UbergraphPages)
	{
		InitialEventGraphs.Add(Graph);
	}
	for (UEdGraph* Graph : InitialEventGraphs)
	{
		if (Graph)
		{
			FBlueprintEditorUtils::RemoveGraph(
				ChildBlueprint,
				Graph,
				EGraphRemoveFlags::MarkTransient);
		}
	}
	ChildBlueprint->LastEditedDocuments.Reset();
	TestEqual(TEXT("Child starts without an event graph"),
		ChildBlueprint->UbergraphPages.Num(), 0);
	TestTrue(TEXT("Plugin Override path implements the child Cue event"),
		FPaper2DPlusFrameCueTypeEditor::AddCueBehaviorEventNode(
			*ChildBlueprint,
			TEXT("OnCueTriggered")));
	TestEqual(TEXT("Override path creates only the permitted event graph"),
		ChildBlueprint->UbergraphPages.Num(), 1);
	TestEqual(TEXT("Override path never creates a function graph"),
		ChildBlueprint->FunctionGraphs.Num(), 0);

	UK2Node_Event* EventNode =
		FPaper2DPlusFrameCueTypeEditor::FindCueBehaviorEventNode(
			*ChildBlueprint,
			TEXT("OnCueTriggered"));
	TestNotNull(TEXT("Child Cue event node exists"), EventNode);
	TArray<UK2Node_CallParentFunction*> ParentCalls;
	FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_CallParentFunction>(
		ChildBlueprint,
		ParentCalls);
	UK2Node_CallParentFunction* ParentCall = nullptr;
	for (UK2Node_CallParentFunction* Candidate : ParentCalls)
	{
		if (Candidate
			&& Candidate->FunctionReference.GetMemberName()
				== FName(TEXT("OnCueTriggered")))
		{
			ParentCall = Candidate;
			break;
		}
	}
	TestNotNull(TEXT("Child override seeds a Parent: On Cue Triggered call"),
		ParentCall);
	if (EventNode && ParentCall)
	{
		UEdGraphPin* EventThen =
			EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
		UEdGraphPin* ParentExecute =
			ParentCall->FindPin(UEdGraphSchema_K2::PN_Execute);
		TestTrue(TEXT("Child Cue event is wired into its parent call"),
			EventThen
			&& ParentExecute
			&& EventThen->LinkedTo.Contains(ParentExecute));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeVariableFacadeFeasibilityTest,
	"Paper2DPlus.FrameCues.CueType.Feasibility.VariableFacade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeVariableFacadeFeasibilityTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeFeasibilityTest;

	UPaper2DPlusFrameCueBlueprint* Blueprint = MakeCueBlueprint(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_FeasibilityVariables"));
	if (!TestNotNull(TEXT("Variable fixture Blueprint"), Blueprint))
	{
		return false;
	}
	FPaper2DPlusFrameCueTypeEditor::PrepareNewlyCreatedBlueprint(*Blueprint);

	FEdGraphPinType IntegerType;
	IntegerType.PinCategory = UEdGraphSchema_K2::PC_Int;
	TestTrue(TEXT("Engine utility adds a payload variable"),
		FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Damage"), IntegerType, TEXT("12")));
	TestNotNull(TEXT("Added variable is present"), FindVariable(*Blueprint, TEXT("Damage")));

	FBlueprintEditorUtils::RenameMemberVariable(Blueprint, TEXT("Damage"), TEXT("Power"));
	TestNull(TEXT("Old variable name is gone"), FindVariable(*Blueprint, TEXT("Damage")));
	TestNotNull(TEXT("Renamed variable is present"), FindVariable(*Blueprint, TEXT("Power")));

	FEdGraphPinType FloatArrayType;
	FloatArrayType.PinCategory = UEdGraphSchema_K2::PC_Real;
	FloatArrayType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	FloatArrayType.ContainerType = EPinContainerType::Array;
	FBlueprintEditorUtils::ChangeMemberVariableType(Blueprint, TEXT("Power"), FloatArrayType);
	const FBPVariableDescription* ChangedVariable = FindVariable(*Blueprint, TEXT("Power"));
	TestTrue(TEXT("Engine utility changes the payload type and container"),
		ChangedVariable
		&& ChangedVariable->VarType.PinCategory == UEdGraphSchema_K2::PC_Real
		&& ChangedVariable->VarType.PinSubCategory == UEdGraphSchema_K2::PC_Float
		&& ChangedVariable->VarType.ContainerType == EPinContainerType::Array);

	TArray<FName> VisibleVariables;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		VisibleVariables.Add(Variable.VarName);
	}
	TestEqual(TEXT("Stock variable model exposes exactly the authored payload"), VisibleVariables.Num(), 1);
	if (VisibleVariables.Num() == 1)
	{
		TestEqual(TEXT("Stock variable model exposes the renamed payload"), VisibleVariables[0], FName(TEXT("Power")));
	}

	FEdGraphPinType IntegerTypeAgain;
	IntegerTypeAgain.PinCategory = UEdGraphSchema_K2::PC_Int;
	FBlueprintEditorUtils::ChangeMemberVariableType(Blueprint, TEXT("Power"), IntegerTypeAgain);
	FBPVariableDescription* CompileVariable = FindVariable(*Blueprint, TEXT("Power"));
	if (CompileVariable)
	{
		CompileVariable->DefaultValue = TEXT("12");
		const uint64 ForbiddenPayloadFlags =
			UPaper2DPlusFrameCueBlueprint::GetForbiddenPayloadPropertyFlags();
		const uint64 SafeBaseFlags = CompileVariable->PropertyFlags
			& ~ForbiddenPayloadFlags
			& ~static_cast<uint64>(CPF_Net | CPF_RepNotify);
		const TArray<uint64> IndividuallyForbiddenFlags = {
			CPF_Transient,
			CPF_Config,
			CPF_GlobalConfig,
			CPF_DuplicateTransient,
			CPF_Deprecated,
			CPF_NonTransactional,
			CPF_EditorOnly,
			CPF_TextExportTransient,
			CPF_NonPIEDuplicateTransient,
			CPF_SkipSerialization
		};
		for (const uint64 ForbiddenFlag : IndividuallyForbiddenFlags)
		{
			CompileVariable->PropertyFlags = SafeBaseFlags | ForbiddenFlag;
			FText ForbiddenFlagError;
			TestFalse(
				*FString::Printf(
					TEXT("Shared contract rejects forbidden payload flag 0x%llx"),
					static_cast<unsigned long long>(ForbiddenFlag)),
				UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(
					*Blueprint,
					&ForbiddenFlagError));
			TestTrue(
				*FString::Printf(
					TEXT("Forbidden payload flag 0x%llx reports a persistence diagnostic"),
					static_cast<unsigned long long>(ForbiddenFlag)),
				ForbiddenFlagError.ToString().Contains(TEXT("must preserve their authored value")));
		}
		CompileVariable->PropertyFlags = SafeBaseFlags | CPF_SaveGame;
		FText AllowedFlagError;
		TestTrue(TEXT("Shared contract allows persistent payload flags outside the forbidden mask"),
			UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(
				*Blueprint,
				&AllowedFlagError));
		TestTrue(TEXT("Allowed payload flags produce no contract diagnostic"),
			AllowedFlagError.IsEmpty());

		const FName BlueprintGetterMetadata(TEXT("BlueprintGetter"));
		const FName BlueprintSetterMetadata(TEXT("BlueprintSetter"));
		CompileVariable->SetMetaData(BlueprintGetterMetadata, TEXT("GetPower"));
		FText GetterMetadataError;
		TestFalse(TEXT("Shared contract rejects a behavior-calling Blueprint getter"),
			UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(
				*Blueprint,
				&GetterMetadataError));
		TestTrue(TEXT("Blueprint getter rejection identifies behavior access"),
			GetterMetadataError.ToString().Contains(TEXT("getter or setter")));
		CompileVariable->RemoveMetaData(BlueprintGetterMetadata);

		CompileVariable->SetMetaData(BlueprintSetterMetadata, TEXT("SetPower"));
		FText SetterMetadataError;
		TestFalse(TEXT("Shared contract rejects a behavior-calling Blueprint setter"),
			UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(
				*Blueprint,
				&SetterMetadataError));
		TestTrue(TEXT("Blueprint setter rejection identifies behavior access"),
			SetterMetadataError.ToString().Contains(TEXT("getter or setter")));
		CompileVariable->RemoveMetaData(BlueprintSetterMetadata);
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (!TestTrue(TEXT("Variable-only Cue Type compiles"),
		Blueprint->Status != BS_Error && Blueprint->GeneratedClass != nullptr))
	{
		return false;
	}

	FIntProperty* PowerProperty = FindFProperty<FIntProperty>(Blueprint->GeneratedClass, TEXT("Power"));
	if (TestNotNull(TEXT("Compiled class contains the reflected payload property"), PowerProperty))
	{
		const UObject* Defaults = Blueprint->GeneratedClass->GetDefaultObject();
		TestEqual(TEXT("Compiled class default comes from the variable definition"),
			PowerProperty->GetPropertyValue_InContainer(Defaults), 12);
		TestTrue(TEXT("Allowed SaveGame payload flag reaches the generated property"),
			PowerProperty->HasAnyPropertyFlags(CPF_SaveGame));
		PowerProperty->SetPropertyFlags(CPF_Transient);
		FText GeneratedTamperError;
		TestFalse(TEXT("Compiled contract rejects a generated-only forbidden property tamper"),
			UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
				*Blueprint,
				&GeneratedTamperError));
		TestTrue(TEXT("Generated-only forbidden property tamper reports its generated field"),
			GeneratedTamperError.ToString().Contains(TEXT("Generated field 'Power'"))
			&& GeneratedTamperError.ToString().Contains(TEXT("excluded from reliable save")));
		const FBPVariableDescription* SourcePowerAfterGeneratedTamper =
			FindVariable(*Blueprint, TEXT("Power"));
		TestTrue(TEXT("Generated-only tamper does not alter the authored source field"),
			SourcePowerAfterGeneratedTamper
			&& (SourcePowerAfterGeneratedTamper->PropertyFlags & CPF_Transient) == 0);
		PowerProperty->ClearPropertyFlags(CPF_Transient);
		FText RestoredGeneratedError;
		TestTrue(TEXT("Compiled contract passes after the generated-only tamper is restored"),
			UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
				*Blueprint,
				&RestoredGeneratedError));
		TestTrue(TEXT("Restored generated contract produces no diagnostic"),
			RestoredGeneratedError.IsEmpty());

		FBPVariableDescription* SourcePowerForTypeParity =
			FindVariable(*Blueprint, TEXT("Power"));
		if (TestNotNull(
			TEXT("Type-parity fixture retains the authored Power field"),
			SourcePowerForTypeParity))
		{
			const FEdGraphPinType SavedPowerType = SourcePowerForTypeParity->VarType;
			SourcePowerForTypeParity->VarType.PinCategory = UEdGraphSchema_K2::PC_Real;
			SourcePowerForTypeParity->VarType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
			Blueprint->Status = BS_UpToDate;
			FText GeneratedTypeMismatchError;
			TestFalse(
				TEXT("Save/cook contract rejects matching-name source/generated type drift"),
				UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
					*Blueprint,
					&GeneratedTypeMismatchError));
			TestTrue(
				TEXT("Type drift reports the exact authored field"),
				GeneratedTypeMismatchError.ToString().Contains(
					TEXT("Generated field 'Power' does not match the authored payload type")));
			SourcePowerForTypeParity->VarType = SavedPowerType;
			FText RestoredTypeParityError;
			TestTrue(
				TEXT("Save/cook contract passes after source/generated types are restored"),
				UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
					*Blueprint,
					&RestoredTypeParityError));
		}

		UPaper2DPlusFrameCueBlueprintGeneratedClass* GeneratedCueClass =
			Cast<UPaper2DPlusFrameCueBlueprintGeneratedClass>(
				Blueprint->GeneratedClass);
		if (TestNotNull(
			TEXT("Compiled Cue Type exposes the specialized generated-class envelope"),
			GeneratedCueClass))
		{
			UComponentDelegateBinding* InjectedDynamicBinding =
				NewObject<UComponentDelegateBinding>(
					GeneratedCueClass,
					NAME_None,
					RF_Transient);
			GeneratedCueClass->DynamicBindingObjects.Add(InjectedDynamicBinding);
			FText GeneratedBehaviorReservoirError;
			TestFalse(
				TEXT("Compiled contract rejects generated-only dynamic binding behavior"),
				UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
					*Blueprint,
					&GeneratedBehaviorReservoirError));
			TestTrue(
				TEXT("Generated-only dynamic binding rejection identifies runtime behavior"),
				GeneratedBehaviorReservoirError.ToString().Contains(
					TEXT("generated runtime behavior")));
			GeneratedCueClass->DynamicBindingObjects.RemoveSingle(
				InjectedDynamicBinding);
			FText RestoredBehaviorReservoirError;
			TestTrue(
				TEXT("Compiled contract passes after generated behavior is removed"),
				UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
					*Blueprint,
					&RestoredBehaviorReservoirError));
			TestTrue(
				TEXT("Restored generated behavior inventory produces no diagnostic"),
				RestoredBehaviorReservoirError.IsEmpty());

			const EClassFlags SavedGeneratedClassFlags =
				GeneratedCueClass->ClassFlags;
			GeneratedCueClass->ClassFlags = static_cast<EClassFlags>(
				GeneratedCueClass->ClassFlags & ~CLASS_CompiledFromBlueprint);
			FText MissingCompiledFlagError;
			TestFalse(
				TEXT("Compiled contract rejects a generated class with its authority flag removed"),
				UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
					*Blueprint,
					&MissingCompiledFlagError));
			TestTrue(
				TEXT("Missing generated-class authority reports a non-authoritative class"),
				MissingCompiledFlagError.ToString().Contains(
					TEXT("stale, skeleton, reinstancing, or uncompiled class")));
			GeneratedCueClass->ClassFlags = SavedGeneratedClassFlags;
			FText RestoredClassAuthorityError;
			TestTrue(
				TEXT("Compiled contract passes after generated-class authority is restored"),
				UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
					*Blueprint,
					&RestoredClassAuthorityError));
		}
	}

	// Duplication serializes through a persistent saving archive, but it is a source-copy
	// operation rather than a package write. A safe Cue Type may be dirty while the destination
	// is being created and compiled, so the package-save currency gate must not poison the copy.
	const FBPVariableDescription* SourcePowerBeforeDuplicate =
		FindVariable(*Blueprint, TEXT("Power"));
	const FGuid SourcePowerGuid = SourcePowerBeforeDuplicate
		? SourcePowerBeforeDuplicate->VarGuid
		: FGuid();
	const FString SourcePowerDefault = SourcePowerBeforeDuplicate
		? SourcePowerBeforeDuplicate->DefaultValue
		: FString();
	const uint64 SourcePowerFlags = SourcePowerBeforeDuplicate
		? SourcePowerBeforeDuplicate->PropertyFlags
		: 0;
	const EBlueprintStatus StatusBeforeDuplicate = Blueprint->Status;
	Blueprint->Status = BS_Dirty;
	UPaper2DPlusFrameCueBlueprint* DirtyDuplicate =
		DuplicateObject<UPaper2DPlusFrameCueBlueprint>(
			Blueprint,
			GetTransientPackage(),
			MakeUniqueObjectName(
				GetTransientPackage(),
				UPaper2DPlusFrameCueBlueprint::StaticClass(),
				TEXT("BP_P2DP_DirtyCueDuplicate")));
	Blueprint->Status = StatusBeforeDuplicate;
	TestNotNull(TEXT("A safe but dirty specialized Cue Type can be duplicated"), DirtyDuplicate);
	const FBPVariableDescription* DuplicatedPower = DirtyDuplicate
		? FindVariable(*DirtyDuplicate, TEXT("Power"))
		: nullptr;
	if (TestNotNull(TEXT("Dirty duplication preserves the authored payload field"),
		DuplicatedPower))
	{
		TestTrue(TEXT("Dirty duplication assigns a fresh valid payload field GUID"),
			DuplicatedPower->VarGuid.IsValid()
			&& DuplicatedPower->VarGuid != SourcePowerGuid);
		TestEqual(TEXT("Dirty duplication preserves the payload source default"),
			DuplicatedPower->DefaultValue,
			SourcePowerDefault);
		TestEqual(TEXT("Dirty duplication preserves the payload source flags"),
			DuplicatedPower->PropertyFlags,
			SourcePowerFlags);
		TestEqual(TEXT("Dirty duplication preserves the payload source type"),
			DuplicatedPower->VarType.PinCategory,
			IntegerTypeAgain.PinCategory);
	}

	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, TEXT("Power"));
	TestEqual(TEXT("Engine utility removes the payload variable"), Blueprint->NewVariables.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCueTypeRenameRedirectContractTest,
	"Paper2DPlus.FrameCues.CueType.Feasibility.RenameRedirectsAreDirectAndAcyclic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCueTypeRenameRedirectContractTest::RunTest(
	const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Paper2DPlus"));
	if (!TestTrue(TEXT("Paper2DPlus plugin is available"), Plugin.IsValid()))
	{
		return false;
	}

	const FString RedirectPath = FPaths::Combine(
		Plugin->GetBaseDir(), TEXT("Config/DefaultPaper2DPlus.ini"));
	FString RedirectText;
	if (!TestTrue(TEXT("Plugin-local redirect table is readable"),
		FFileHelper::LoadFileToString(RedirectText, *RedirectPath)))
	{
		return false;
	}
	TestFalse(TEXT("Shipped redirects use no MatchSubstring catchall"),
		RedirectText.Contains(TEXT("MatchSubstring"), ESearchCase::CaseSensitive));

	auto ExtractQuotedField = [](const FString& Line, const TCHAR* Field, FString& OutValue)
	{
		const FString Prefix = FString(Field) + TEXT("=\"");
		const int32 ValueStart = Line.Find(Prefix, ESearchCase::CaseSensitive);
		if (ValueStart == INDEX_NONE)
		{
			return false;
		}
		const int32 ContentStart = ValueStart + Prefix.Len();
		const int32 ValueEnd = Line.Find(
			TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ContentStart);
		if (ValueEnd == INDEX_NONE)
		{
			return false;
		}
		OutValue = Line.Mid(ContentStart, ValueEnd - ContentStart);
		return true;
	};

	TArray<FString> Lines;
	RedirectText.ParseIntoArrayLines(Lines, false);
	TMap<FString, FString> Redirects;
	TSet<FString> OldNames;
	TSet<FString> NewNames;
	int32 RedirectRowCount = 0;
	for (const FString& Line : Lines)
	{
		if (!Line.StartsWith(TEXT("+")) || !Line.Contains(TEXT("Redirects=(")))
		{
			continue;
		}
		++RedirectRowCount;
		FString OldName;
		FString NewName;
		if (!ExtractQuotedField(Line, TEXT("OldName"), OldName)
			|| !ExtractQuotedField(Line, TEXT("NewName"), NewName))
		{
			AddError(FString::Printf(
				TEXT("Redirect row does not expose direct OldName/NewName fields: %s"), *Line));
			continue;
		}
		Redirects.Add(OldName, NewName);
		OldNames.Add(OldName);
		NewNames.Add(NewName);
	}

	TestEqual(TEXT("TASK-170 adds exactly three rows to the 42-row plugin table"),
		RedirectRowCount, 45);
	TestEqual(
		TEXT("Retired Frame Cue base points directly to CueBase"),
		Redirects.FindRef(TEXT("/Script/Paper2DPlus.Paper2DPlusFrameCue")),
		FString(TEXT("/Script/Paper2DPlus.Paper2DPlusCueBase")));
	TestEqual(
		TEXT("Retired Cue timing class points directly to Cue"),
		Redirects.FindRef(TEXT("/Script/Paper2DPlus.Paper2DPlusMomentCue")),
		FString(TEXT("/Script/Paper2DPlus.Paper2DPlusCue")));
	TestEqual(
		TEXT("Retired Cue State timing class points directly to CueState"),
		Redirects.FindRef(TEXT("/Script/Paper2DPlus.Paper2DPlusRangeCue")),
		FString(TEXT("/Script/Paper2DPlus.Paper2DPlusCueState")));

	for (const FString& NewName : NewNames)
	{
		if (OldNames.Contains(NewName))
		{
			AddError(FString::Printf(
				TEXT("Redirect chain found: '%s' is both a NewName and an OldName."), *NewName));
		}
	}
	return !HasAnyErrors();
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypePreU18CompatibilityTest,
	"Paper2DPlus.FrameCues.CueType.Feasibility.PreU18AssetRemainsReadyWithoutRestaging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypePreU18CompatibilityTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeFeasibilityTest;

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Paper2DPlus"));
	if (!TestTrue(TEXT("Paper2DPlus plugin is available"), Plugin.IsValid()))
	{
		return false;
	}
	const FString EncodedFixturePath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Source/Paper2DPlusEditor/Private/Tests/Fixtures/"
			"P2DPCueTypePreU18.uasset.gz.b64"));
	FString EncodedFixture;
	if (!TestTrue(TEXT("Pre-U18 fixture source is readable"),
		FFileHelper::LoadFileToString(EncodedFixture, *EncodedFixturePath)))
	{
		return false;
	}
	EncodedFixture.ReplaceInline(TEXT("\r"), TEXT(""));
	EncodedFixture.ReplaceInline(TEXT("\n"), TEXT(""));
	EncodedFixture.ReplaceInline(TEXT("\t"), TEXT(""));
	EncodedFixture.ReplaceInline(TEXT(" "), TEXT(""));

	TArray<uint8> CompressedFixture;
	if (!TestTrue(TEXT("Pre-U18 fixture base64 decodes"),
		FBase64::Decode(EncodedFixture, CompressedFixture))
		|| !TestTrue(TEXT("Pre-U18 gzip fixture has a trailer"),
			CompressedFixture.Num() >= 4))
	{
		return false;
	}
	const int32 Trailer = CompressedFixture.Num() - 4;
	const uint32 RawSize =
		static_cast<uint32>(CompressedFixture[Trailer])
		| (static_cast<uint32>(CompressedFixture[Trailer + 1]) << 8)
		| (static_cast<uint32>(CompressedFixture[Trailer + 2]) << 16)
		| (static_cast<uint32>(CompressedFixture[Trailer + 3]) << 24);
	if (!TestEqual(
		TEXT("Pre-U18 fixture gzip trailer matches the reviewed raw size"),
		RawSize,
		static_cast<uint32>(49113)))
	{
		return false;
	}
	TArray<uint8> RawFixture;
	RawFixture.SetNumUninitialized(static_cast<int32>(RawSize));
	if (!TestTrue(TEXT("Pre-U18 fixture gzip decompresses"),
		FCompression::UncompressMemory(
			NAME_Gzip,
			RawFixture.GetData(),
			RawFixture.Num(),
			CompressedFixture.GetData(),
			CompressedFixture.Num())))
	{
		return false;
	}
	static constexpr ANSICHAR Sha256KnownVector[] = "abc";
	if (!TestEqual(
		TEXT("Portable SHA-256 helper matches the published abc vector"),
		HashBytesSha256(
			reinterpret_cast<const uint8*>(Sha256KnownVector),
			UE_ARRAY_COUNT(Sha256KnownVector) - 1),
		FString(TEXT("ba7816bf8f01cfea414140de5dae2223"
			"b00361a396177a9cb410ff61f20015ad"))))
	{
		return false;
	}
	const FString FixtureSignature =
		HashBytesSha256(RawFixture.GetData(), RawFixture.Num());
	if (!TestEqual(
		TEXT("Pre-U18 fixture bytes match the reviewed characterization artifact"),
		FixtureSignature,
		FString(TEXT("b5fb78f1af16bd4dcafb4ceb77cbeed2ccc125f5e27f8f6cbdab8a8e1e4b9639"))))
	{
		return false;
	}

	FScopedCueTypeFixturePackage Fixture(
		*this,
		TEXT("P2DPPreU18Compatibility"),
		/*bInRetainAfterSuccess=*/false,
		TEXT("/Game/Paper2DPlusAutomation/P2DPCueTypeCookedRuntimeFixture"),
		/*bRequireUnusedExactPackageName=*/true);
	if (!TestTrue(TEXT("Exact pre-U18 package path is clean"), Fixture.IsPrepared()))
	{
		AddError(Fixture.PreparationError);
		return false;
	}
	Fixture.ClaimUnusedExactPackagePath();
	if (!TestTrue(TEXT("Decoded pre-U18 package writes to the exact original path"),
		FFileHelper::SaveArrayToFile(RawFixture, *Fixture.FilePath)))
	{
		return false;
	}

	UPackage* LoadedPackage = LoadPackage(nullptr, *Fixture.PackageName, LOAD_None);
	UPaper2DPlusFrameCueBlueprint* MomentType = LoadedPackage
		? FindObject<UPaper2DPlusFrameCueBlueprint>(
			LoadedPackage, TEXT("BP_PersistentCueType"))
		: nullptr;
	UPaper2DPlusFrameCueBlueprint* RangeType = LoadedPackage
		? FindObject<UPaper2DPlusFrameCueBlueprint>(
			LoadedPackage, TEXT("BP_PersistentRangeCueType"))
		: nullptr;
	UPaper2DPlusCharacterProfileAsset* Profile = LoadedPackage
		? FindObject<UPaper2DPlusCharacterProfileAsset>(
			LoadedPackage, TEXT("PersistentCueProfile"))
		: nullptr;
	if (!TestNotNull(TEXT("Pre-U18 package loads"), LoadedPackage)
		|| !TestNotNull(TEXT("Pre-U18 Cue Type loads"), MomentType)
		|| !TestNotNull(TEXT("Pre-U18 Cue State Type loads"), RangeType)
		|| !TestNotNull(TEXT("Pre-U18 placement profile loads"), Profile))
	{
		return false;
	}
	TestTrue(TEXT("Redirected instant Blueprint parent is the renamed Cue class"),
		MomentType->ParentClass == UPaper2DPlusCue::StaticClass()
		&& MomentType->GeneratedClass
		&& MomentType->GeneratedClass->IsChildOf(UPaper2DPlusCue::StaticClass()));
	TestTrue(TEXT("Redirected state Blueprint parent is the renamed Cue State class"),
		RangeType->ParentClass == UPaper2DPlusCueState::StaticClass()
		&& RangeType->GeneratedClass
		&& RangeType->GeneratedClass->IsChildOf(UPaper2DPlusCueState::StaticClass()));

	const int32 MomentVersion = MomentType->DurableSchemaVersion;
	const FString MomentFingerprint = MomentType->DurableSchemaFingerprint;
	const FString MomentSnapshot = MomentType->DurableSchemaSnapshot;
	const int32 RangeVersion = RangeType->DurableSchemaVersion;
	const FString RangeFingerprint = RangeType->DurableSchemaFingerprint;
	const FString RangeSnapshot = RangeType->DurableSchemaSnapshot;
	// This immutable fixture was saved by the then-version 2 schema service before U18 renamed the
	// native classes. Its historical outer stamp is 2, while the behavior-free snapshot remains
	// byte-identical to schema v1 below.
	TestEqual(TEXT("Pre-U18 Cue Type keeps its saved durable-service stamp"),
		MomentVersion,
		2);
	TestEqual(TEXT("Pre-U18 Cue State Type keeps its saved durable-service stamp"),
		RangeVersion,
		2);
	TestEqual(TEXT("Pre-U18 Cue Type keeps its reviewed fingerprint"),
		MomentFingerprint,
		FString(TEXT("ED66BF05A48DCDD35867E2CE90C8371DA71F95C1")));
	TestEqual(TEXT("Pre-U18 Cue State Type keeps its reviewed fingerprint"),
		RangeFingerprint,
		FString(TEXT("9D232407D5048F45975D9DC9FE66844FC416242D")));
	TestEqual(TEXT("Pre-U18 Cue Type snapshot keeps its exact reviewed length"),
		MomentSnapshot.Len(),
		3548);
	TestEqual(TEXT("Pre-U18 Cue State Type snapshot keeps its exact reviewed length"),
		RangeSnapshot.Len(),
		2750);
	TestEqual(TEXT("Pre-U18 Cue Type snapshot bytes remain exact"),
		HashUtf8Sha256(MomentSnapshot),
		FString(TEXT("f5fed62728a1f1e7b80f236c8b46ca293f78430bc0bfcee1c51ce2541c3d6119")));
	TestEqual(TEXT("Pre-U18 Cue State Type snapshot bytes remain exact"),
		HashUtf8Sha256(RangeSnapshot),
		FString(TEXT("518e1583daa8184506c87602498bdba1f03089853f94edb249a074618dfc6da1")));
	TestTrue(TEXT("Pre-U18 snapshots retain their canonical legacy native class tokens"),
		MomentSnapshot.Contains(TEXT("/Script/Paper2DPlus.Paper2DPlusMomentCue"))
		&& MomentSnapshot.Contains(TEXT("/Script/Paper2DPlus.Paper2DPlusFrameCue"))
		&& RangeSnapshot.Contains(TEXT("/Script/Paper2DPlus.Paper2DPlusRangeCue"))
		&& RangeSnapshot.Contains(TEXT("/Script/Paper2DPlus.Paper2DPlusFrameCue")));
	TestFalse(TEXT("Pre-U18 snapshots never restage renamed native class tokens"),
		MomentSnapshot.Contains(TEXT("/Script/Paper2DPlus.Paper2DPlusCue"))
		|| RangeSnapshot.Contains(TEXT("/Script/Paper2DPlus.Paper2DPlusCue")));
	const FPaper2DPlusFrameCueTypeDescriptor MomentDescriptor =
		FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(MomentType->GeneratedClass);
	const FPaper2DPlusFrameCueTypeDescriptor RangeDescriptor =
		FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(RangeType->GeneratedClass);
	TestEqual(TEXT("Pre-U18 Cue Type remains Ready"),
		MomentDescriptor.Availability, EPaper2DPlusFrameCueTypeAvailability::Ready);
	TestEqual(TEXT("Pre-U18 Cue State Type remains Ready"),
		RangeDescriptor.Availability, EPaper2DPlusFrameCueTypeAvailability::Ready);

	const FPaper2DPlusFrameCueTypeDiscoveryResult Discovery =
		FPaper2DPlusFrameCueTypeAuthoring::DiscoverCueTypes();
	auto IsDiscoveredReady = [&Discovery](const UClass* CueClass)
	{
		return Discovery.Types.ContainsByPredicate(
			[CueClass](const FPaper2DPlusFrameCueTypeDescriptor& Candidate)
			{
				return Candidate.Class.Get() == CueClass
					&& Candidate.Availability
						== EPaper2DPlusFrameCueTypeAvailability::Ready;
			});
	};
	TestTrue(TEXT("Pre-U18 Cue Type remains in ready discovery"),
		IsDiscoveredReady(MomentType->GeneratedClass));
	TestTrue(TEXT("Pre-U18 Cue State Type remains in ready discovery"),
		IsDiscoveredReady(RangeType->GeneratedClass));
	TestTrue(TEXT("Load and readiness checks do not restage or dirty the pre-U18 package"),
		!LoadedPackage->IsDirty()
		&& MomentType->DurableSchemaVersion == MomentVersion
		&& MomentType->DurableSchemaFingerprint == MomentFingerprint
		&& RangeType->DurableSchemaVersion == RangeVersion
		&& RangeType->DurableSchemaFingerprint == RangeFingerprint);

	if (FApp::CanEverRender())
	{
		const TSharedRef<FPaper2DPlusFrameCueTypeEditor> MomentEditor =
			MakeShared<FPaper2DPlusFrameCueTypeEditor>();
		FScopedCueTypeEditorClose CloseMomentEditor(MomentEditor);
		MomentEditor->InitFrameCueTypeEditor(
			EToolkitMode::Standalone, nullptr, MomentType);
		TestTrue(TEXT("Redirected pre-U18 Cue Type opens with stock My Blueprint"),
			MomentEditor->IsUsingStockMyBlueprintForTests()
			&& MomentEditor->GetMyBlueprintWidget().IsValid());

		const TSharedRef<FPaper2DPlusFrameCueTypeEditor> RangeEditor =
			MakeShared<FPaper2DPlusFrameCueTypeEditor>();
		FScopedCueTypeEditorClose CloseRangeEditor(RangeEditor);
		RangeEditor->InitFrameCueTypeEditor(
			EToolkitMode::Standalone, nullptr, RangeType);
		TestTrue(TEXT("Redirected pre-U18 Cue State Type opens with stock My Blueprint"),
			RangeEditor->IsUsingStockMyBlueprintForTests()
			&& RangeEditor->GetMyBlueprintWidget().IsValid());
	}
	else
	{
		AddInfo(TEXT("Renderless run skips editor-window creation; load, compile, validation, and dispatch still run."));
	}

	FKismetEditorUtilities::CompileBlueprint(MomentType);
	FKismetEditorUtilities::CompileBlueprint(RangeType);
	TestTrue(TEXT("Redirected pre-U18 Cue Type recompiles against Cue"),
		MomentType->Status != BS_Error
		&& MomentType->ParentClass == UPaper2DPlusCue::StaticClass()
		&& MomentType->GeneratedClass
		&& MomentType->GeneratedClass->IsChildOf(UPaper2DPlusCue::StaticClass()));
	TestTrue(TEXT("Redirected pre-U18 Cue State Type recompiles against Cue State"),
		RangeType->Status != BS_Error
		&& RangeType->ParentClass == UPaper2DPlusCueState::StaticClass()
		&& RangeType->GeneratedClass
		&& RangeType->GeneratedClass->IsChildOf(UPaper2DPlusCueState::StaticClass()));

	FText ValidationError;
	if (!TestTrue(TEXT("Redirected pre-U18 Cue Type keeps its authored envelope"),
		UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(
			*MomentType, &ValidationError)))
	{
		AddError(ValidationError.ToString());
	}
	ValidationError = FText::GetEmpty();
	if (!TestTrue(TEXT("Redirected pre-U18 Cue Type keeps its compiled envelope"),
		UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
			*MomentType, &ValidationError, MomentType->GeneratedClass)))
	{
		AddError(ValidationError.ToString());
	}
	ValidationError = FText::GetEmpty();
	if (!TestTrue(TEXT("Redirected pre-U18 Cue State Type keeps its authored envelope"),
		UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(
			*RangeType, &ValidationError)))
	{
		AddError(ValidationError.ToString());
	}
	ValidationError = FText::GetEmpty();
	if (!TestTrue(TEXT("Redirected pre-U18 Cue State Type keeps its compiled envelope"),
		UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
			*RangeType, &ValidationError, RangeType->GeneratedClass)))
	{
		AddError(ValidationError.ToString());
	}
	TestTrue(TEXT("Recompile stays clean and never restages either durable schema"),
		!LoadedPackage->IsDirty()
		&& MomentType->DurableSchemaVersion == MomentVersion
		&& MomentType->DurableSchemaFingerprint == MomentFingerprint
		&& MomentType->DurableSchemaSnapshot == MomentSnapshot
		&& RangeType->DurableSchemaVersion == RangeVersion
		&& RangeType->DurableSchemaFingerprint == RangeFingerprint
		&& RangeType->DurableSchemaSnapshot == RangeSnapshot);

	if (!TestEqual(TEXT("Pre-U18 profile keeps one animation"), Profile->Flipbooks.Num(), 1)
		|| !TestEqual(TEXT("Pre-U18 profile keeps both placements"),
			Profile->Flipbooks[0].FrameEventData.FrameCues.Num(), 2))
	{
		return false;
	}
	FFlipbookProfileEntry& Entry = Profile->Flipbooks[0];
	UPaper2DPlusCue* CuePlacement =
		Cast<UPaper2DPlusCue>(Entry.FrameEventData.FrameCues[0]);
	UPaper2DPlusCueState* StatePlacement =
		Cast<UPaper2DPlusCueState>(Entry.FrameEventData.FrameCues[1]);
	if (!TestNotNull(TEXT("Old instant placement resolves as Cue"), CuePlacement)
		|| !TestNotNull(TEXT("Old state placement resolves as Cue State"), StatePlacement))
	{
		return false;
	}
	TestTrue(TEXT("Redirected placements retain their generated classes and Profile owner"),
		CuePlacement->GetClass() == MomentType->GeneratedClass
		&& StatePlacement->GetClass() == RangeType->GeneratedClass
		&& CuePlacement->GetOuter() == Profile
		&& StatePlacement->GetOuter() == Profile);
	TestEqual(TEXT("Redirected Cue keeps frame 3"), CuePlacement->TriggerFrame, 3);
	TestEqual(TEXT("Redirected Cue State keeps start frame 0"), StatePlacement->StartFrame, 0);
	TestEqual(TEXT("Redirected Cue State keeps its two-frame span"), StatePlacement->FrameCount, 2);
	TestTrue(TEXT("Redirected Cue State keeps Update enabled"), StatePlacement->bEmitUpdates);

	FIntProperty* CuePower = FindFProperty<FIntProperty>(
		MomentType->GeneratedClass, TEXT("Power"));
	FIntProperty* StatePower = FindFProperty<FIntProperty>(
		RangeType->GeneratedClass, TEXT("Power"));
	if (TestNotNull(TEXT("Redirected Cue class keeps the Power payload"), CuePower))
	{
		TestEqual(TEXT("Redirected Cue placement keeps Power 37"),
			CuePower->GetPropertyValue_InContainer(CuePlacement), 37);
	}
	if (TestNotNull(TEXT("Redirected Cue State class keeps the Power payload"), StatePower))
	{
		TestEqual(TEXT("Redirected Cue State placement keeps Power 83"),
			StatePower->GetPropertyValue_InContainer(StatePlacement), 83);
	}

	FPaper2DPlusFrameCueContext BaseContext;
	BaseContext.AnimationName = FName(*Entry.Identity.FlipbookName);
	TSet<TObjectPtr<UPaper2DPlusCueBase>> ActiveRanges;
	TArray<TObjectPtr<UPaper2DPlusCueBase>> DispatchedCues;
	TArray<FPaper2DPlusFrameCueContext> Notifications;
	const auto Dispatch = [
		&Entry,
		&BaseContext,
		&ActiveRanges,
		&DispatchedCues,
		&Notifications](const int32 PreviousFrame, const int32 CurrentFrame)
	{
		BaseContext.PreviousFrame = PreviousFrame;
		BaseContext.CurrentFrame = CurrentFrame;
		Paper2DPlusFrameCues::DispatchFrameTransition(
			Entry.FrameEventData.FrameCues,
			BaseContext,
			ActiveRanges,
			[](UPaper2DPlusCueBase&) { return true; },
			[&DispatchedCues, &Notifications](
				UPaper2DPlusCueBase& Cue,
				const FPaper2DPlusFrameCueContext& Notification)
			{
				Paper2DPlusFrameCueBehavior::ExecuteCueBehavior(Cue, Notification);
				DispatchedCues.Add(&Cue);
				Notifications.Add(Notification);
			});
	};

	Dispatch(-1, 0);
	TestEqual(TEXT("Redirected Cue State entry dispatches Begin and Update"),
		Notifications.Num(), 2);
	if (Notifications.Num() == 2 && DispatchedCues.Num() == 2)
	{
		TestTrue(TEXT("Redirected Cue State owns both entry notifications"),
			DispatchedCues[0] == StatePlacement && DispatchedCues[1] == StatePlacement);
		TestEqual(TEXT("Redirected Cue State begins"),
			Notifications[0].Phase, EPaper2DPlusFrameCuePhase::Begin);
		TestEqual(TEXT("Redirected Cue State updates on entry"),
			Notifications[1].Phase, EPaper2DPlusFrameCuePhase::Update);
	}
	Notifications.Reset();
	DispatchedCues.Reset();
	Dispatch(0, 1);
	TestEqual(TEXT("Redirected Cue State dispatches its interior Update"),
		Notifications.Num(), 1);
	if (Notifications.Num() == 1 && DispatchedCues.Num() == 1)
	{
		TestTrue(TEXT("Redirected Cue State owns its interior Update"),
			DispatchedCues[0] == StatePlacement);
		TestEqual(TEXT("Redirected Cue State uses the Update phase"),
			Notifications[0].Phase, EPaper2DPlusFrameCuePhase::Update);
	}
	Notifications.Reset();
	DispatchedCues.Reset();
	Dispatch(1, 2);
	TestEqual(TEXT("Redirected Cue State dispatches its paired End"),
		Notifications.Num(), 1);
	if (Notifications.Num() == 1 && DispatchedCues.Num() == 1)
	{
		TestTrue(TEXT("Redirected Cue State owns its End"),
			DispatchedCues[0] == StatePlacement);
		TestEqual(TEXT("Redirected Cue State ends"),
			Notifications[0].Phase, EPaper2DPlusFrameCuePhase::End);
	}
	TestEqual(TEXT("Redirected Cue State leaves no active lifecycle"), ActiveRanges.Num(), 0);
	Notifications.Reset();
	DispatchedCues.Reset();
	Dispatch(2, 3);
	TestEqual(TEXT("Redirected Cue dispatches one Trigger"), Notifications.Num(), 1);
	if (Notifications.Num() == 1 && DispatchedCues.Num() == 1)
	{
		TestTrue(TEXT("Redirected Cue owns the Trigger"), DispatchedCues[0] == CuePlacement);
		TestEqual(TEXT("Redirected Cue uses the Trigger phase"),
			Notifications[0].Phase, EPaper2DPlusFrameCuePhase::Trigger);
	}

	LoadedPackage = nullptr;
	MomentType = nullptr;
	RangeType = nullptr;
	Profile = nullptr;
	FText UnloadError;
	TestTrue(TEXT("Pre-U18 fixture unloads after compatibility checks"),
		Fixture.Unload(UnloadError));
	if (!UnloadError.IsEmpty())
	{
		AddError(UnloadError.ToString());
	}
	TestTrue(TEXT("Pre-U18 fixture files are removed after compatibility checks"),
		Fixture.DeleteFiles());
	TestFalse(TEXT("Pre-U18 fixture leaves no package artifact"),
		IFileManager::Get().FileExists(*Fixture.FilePath));
	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypePersistentRoundtripFeasibilityTest,
	"Paper2DPlus.FrameCues.CueType.Feasibility.PersistentAssetAndPlacementRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypePersistentRoundtripFeasibilityTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeFeasibilityTest;
	using namespace Paper2DPlusFrameCueEditorAuthoring;

	const bool bRetainCookFixture = FParse::Param(
		FCommandLine::Get(), TEXT("Paper2DPlusRetainCueTypeCookFixture"));
	FScopedCueTypeFixturePackage Fixture(
		*this, TEXT("P2DPFrameCueType"), bRetainCookFixture);
	FScopedCueTypeFixturePackage InvalidCookFixture(
		*this,
		TEXT("P2DPInvalidCookCueType"),
		bRetainCookFixture,
		TEXT("/Game/Paper2DPlusAutomation/P2DPCueTypeInvalidCookFixture"));
	FScopedCueTypeFixturePackage DeferredInvalidCookFixture(
		*this,
		TEXT("P2DPDeferredInvalidCookCueType"),
		bRetainCookFixture,
		TEXT("/Game/Paper2DPlusDeferredInvalidAutomation/P2DPCueTypeDeferredInvalidCookFixture"));
	FScopedCueTypeFixturePackage SaveGuardRejectFixture(
		*this, TEXT("P2DPSaveGuardReject"), false);
	if (!TestTrue(TEXT("Persistent Cue Type fixtures start from clean package paths"),
		Fixture.IsPrepared()
		&& InvalidCookFixture.IsPrepared()
		&& DeferredInvalidCookFixture.IsPrepared()
		&& SaveGuardRejectFixture.IsPrepared()))
	{
		if (!Fixture.IsPrepared())
		{
			AddError(Fixture.PreparationError);
		}
		if (!InvalidCookFixture.IsPrepared())
		{
			AddError(InvalidCookFixture.PreparationError);
		}
		if (!DeferredInvalidCookFixture.IsPrepared())
		{
			AddError(DeferredInvalidCookFixture.PreparationError);
		}
		if (!SaveGuardRejectFixture.IsPrepared())
		{
			AddError(SaveGuardRejectFixture.PreparationError);
		}
		return false;
	}
	const FName BlueprintName(TEXT("BP_PersistentCueType"));
	const FName RangeBlueprintName(TEXT("BP_PersistentRangeCueType"));
	const FName ProfileName(TEXT("PersistentCueProfile"));
	const TArray<FVector> DefaultImpactOffsets = {
		FVector(1.0, 2.0, 3.0),
		FVector(-4.0, 5.0, 6.0)
	};
	const TArray<FVector> PlacementImpactOffsets = {
		FVector(7.0, 8.0, 9.0),
		FVector(10.0, -11.0, 12.0),
		FVector(13.0, 14.0, -15.0)
	};
	const FSoftObjectPath DefaultHandlerClassPath(AActor::StaticClass());
	const FSoftObjectPath PlacementHandlerClassPath(APawn::StaticClass());
	FGuid PowerGuid;

	UPackage* Package = CreatePackage(*Fixture.PackageName);
	if (!TestNotNull(TEXT("Persistent Cue Type package is created"), Package))
	{
		return false;
	}
	Package->AddToRoot();

	UPaper2DPlusFrameCueBlueprint* Blueprint = MakeCompiledCueBlueprint(
		UPaper2DPlusCue::StaticClass(), *BlueprintName.ToString(), Package, &PowerGuid);
	if (!TestNotNull(TEXT("Persistent specialized Cue Type compiles"), Blueprint))
	{
		Package->RemoveFromRoot();
		return false;
	}
	if (!TestTrue(TEXT("Persistent Cue Type adds a real FVector array payload"),
		AddEditableVectorArrayPayload(
			*Blueprint,
			TEXT("ImpactOffsets"),
			TEXT("((X=1.000000,Y=2.000000,Z=3.000000),(X=-4.000000,Y=5.000000,Z=6.000000))")))
		|| !TestTrue(TEXT("Persistent Cue Type adds a real soft-class payload"),
			AddEditableSoftClassPayload(
				*Blueprint,
				TEXT("HandlerClass"),
				FString::Printf(TEXT("\"%s\""), *DefaultHandlerClassPath.ToString()))))
	{
		Package->RemoveFromRoot();
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (!TestTrue(TEXT("Persistent struct-array and soft-class schema compiles cleanly"),
		Blueprint->Status != BS_Error))
	{
		Package->RemoveFromRoot();
		return false;
	}
	FArrayProperty* ImpactOffsetsProperty = FindVectorArrayProperty(
		Blueprint->GeneratedClass, TEXT("ImpactOffsets"));
	FSoftClassProperty* HandlerClassProperty = FindActorSoftClassProperty(
		Blueprint->GeneratedClass, TEXT("HandlerClass"));
	const UObject* GeneratedDefaults = Blueprint->GeneratedClass->GetDefaultObject();
	if (!TestNotNull(TEXT("Generated Cue Type uses a FVector array property"),
		ImpactOffsetsProperty)
		|| !TestNotNull(TEXT("Generated Cue Type uses an Actor soft-class property"),
			HandlerClassProperty))
	{
		Package->RemoveFromRoot();
		return false;
	}
	TestTrue(TEXT("Generated FVector array retains its authored class default"),
		VectorArrayEquals(*ImpactOffsetsProperty, GeneratedDefaults, DefaultImpactOffsets));
	TestEqual(TEXT("Generated soft-class retains its authored class default"),
		GetSoftClassPath(*HandlerClassProperty, GeneratedDefaults),
		DefaultHandlerClassPath);
	const FName SavedBlueprintName = Blueprint->GetFName();
	if (bRetainCookFixture)
	{
		TestEqual(TEXT("Cook fixture Blueprint keeps its deterministic object name"),
			SavedBlueprintName, BlueprintName);
	}
	Blueprint->SetFlags(RF_Public | RF_Standalone | RF_Transactional);

	UPaper2DPlusFrameCueBlueprint* RangeBlueprint = MakeCompiledCueBlueprint(
		UPaper2DPlusCueState::StaticClass(),
		*RangeBlueprintName.ToString(),
		Package);
	if (!TestNotNull(TEXT("Persistent specialized Cue State Type compiles"), RangeBlueprint))
	{
		Package->RemoveFromRoot();
		return false;
	}
	RangeBlueprint->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
	UPaper2DPlusCueState* RangeDefaults = Cast<UPaper2DPlusCueState>(
		RangeBlueprint->GeneratedClass->GetDefaultObject());
	if (!TestNotNull(TEXT("Generated Cue State Type has Range defaults"), RangeDefaults))
	{
		Package->RemoveFromRoot();
		return false;
	}
	RangeDefaults->bEmitUpdates = true;

	UPaper2DPlusCharacterProfileAsset* Profile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(
			Package, ProfileName, RF_Public | RF_Standalone | RF_Transactional);
	FFlipbookProfileEntry& Entry = Profile->Flipbooks.AddDefaulted_GetRef();
	Entry.Identity.FlipbookName = TEXT("Attack");
	Entry.CombatData.Frames.SetNum(4);

	UPaper2DPlusCueBase* Placement = CreatePlacement(
		Profile, Blueprint->GeneratedClass, 3, 4);
	if (!TestNotNull(TEXT("Generated Cue placement is created before save"), Placement))
	{
		Package->RemoveFromRoot();
		return false;
	}
	FIntProperty* PowerProperty =
		FindFProperty<FIntProperty>(Placement->GetClass(), TEXT("Power"));
	if (!TestNotNull(TEXT("Generated placement exposes its payload before save"), PowerProperty))
	{
		Package->RemoveFromRoot();
		return false;
	}
	TestEqual(TEXT("New Moment placement inherits its Cue Type payload default"),
		PowerProperty->GetPropertyValue_InContainer(Placement), 12);
	PowerProperty->SetPropertyValue_InContainer(Placement, 37);
	SetVectorArray(*ImpactOffsetsProperty, Placement, PlacementImpactOffsets);
	SetSoftClass(*HandlerClassProperty, Placement, APawn::StaticClass());
	TestTrue(TEXT("Placement owns a real FVector array override before save"),
		VectorArrayEquals(*ImpactOffsetsProperty, Placement, PlacementImpactOffsets));
	TestEqual(TEXT("Placement owns a real soft-class override before save"),
		GetSoftClassPath(*HandlerClassProperty, Placement),
		PlacementHandlerClassPath);
	Entry.FrameEventData.FrameCues.Add(Placement);
	UPaper2DPlusCueState* RangePlacement = Cast<UPaper2DPlusCueState>(CreatePlacement(
		Profile, RangeBlueprint->GeneratedClass, 0, 4));
	FIntProperty* RangePowerProperty = RangePlacement
		? FindFProperty<FIntProperty>(RangePlacement->GetClass(), TEXT("Power"))
		: nullptr;
	if (!TestNotNull(TEXT("Generated Cue State placement is created before save"), RangePlacement)
		|| !TestNotNull(TEXT("Generated Range placement exposes its payload"), RangePowerProperty))
	{
		Package->RemoveFromRoot();
		return false;
	}
	TestEqual(TEXT("New Range placement inherits its Cue Type payload default"),
		RangePowerProperty->GetPropertyValue_InContainer(RangePlacement), 12);
	RangePlacement->FrameCount = 2;
	RangePowerProperty->SetPropertyValue_InContainer(RangePlacement, 83);
	TestTrue(TEXT("Range placement inherits Update lifecycle from its Cue Type default"),
		RangePlacement->bEmitUpdates);
	Entry.FrameEventData.FrameCues.Add(RangePlacement);
	Package->MarkPackageDirty();
	FPaper2DPlusFrameCueDurableSaveAttempt MomentSaveAttempt;
	FPaper2DPlusFrameCueDurableSaveAttempt RangeSaveAttempt;
	FText MomentPrepareError;
	FText RangePrepareError;
	const bool bMomentPrepared =
		FPaper2DPlusFrameCueTypeEditor::PrepareDurableSchemaSaveAttempt(
			*Blueprint,
			MomentSaveAttempt,
			&MomentPrepareError);
	const bool bRangePrepared =
		FPaper2DPlusFrameCueTypeEditor::PrepareDurableSchemaSaveAttempt(
			*RangeBlueprint,
			RangeSaveAttempt,
			&RangePrepareError);
	if (!TestTrue(TEXT("Cue Type stages its exact durable schema before save"), bMomentPrepared)
		|| !TestTrue(TEXT("Cue State Type stages its exact durable schema before save"), bRangePrepared))
	{
		if (!MomentPrepareError.IsEmpty())
		{
			AddError(MomentPrepareError.ToString());
		}
		if (!RangePrepareError.IsEmpty())
		{
			AddError(RangePrepareError.ToString());
		}
		Package->RemoveFromRoot();
		return false;
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const bool bSaved = UPackage::SavePackage(
		Package, Profile, *Fixture.FilePath, SaveArgs);
	FText MomentFinalizeError;
	FText RangeFinalizeError;
	const bool bMomentFinalized =
		FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(
			*Blueprint,
			MomentSaveAttempt,
			&MomentFinalizeError);
	const bool bRangeFinalized =
		FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(
			*RangeBlueprint,
			RangeSaveAttempt,
			&RangeFinalizeError);
	Package->RemoveFromRoot();
	if (!TestTrue(TEXT("Specialized Cue Types and placements save through the protected durable workflow"),
		bSaved && bMomentFinalized && bRangeFinalized))
	{
		if (!MomentFinalizeError.IsEmpty())
		{
			AddError(MomentFinalizeError.ToString());
		}
		if (!RangeFinalizeError.IsEmpty())
		{
			AddError(RangeFinalizeError.ToString());
		}
		return false;
	}

	Package = nullptr;
	Blueprint = nullptr;
	RangeBlueprint = nullptr;
	Profile = nullptr;
	Placement = nullptr;
	RangePlacement = nullptr;
	PowerProperty = nullptr;
	RangePowerProperty = nullptr;
	ImpactOffsetsProperty = nullptr;
	HandlerClassProperty = nullptr;
	FText UnloadError;
	if (!TestTrue(TEXT("Saved Cue Type package fully unloads"), Fixture.Unload(UnloadError)))
	{
		AddError(FString::Printf(TEXT("Cue Type package unload failed: %s"), *UnloadError.ToString()));
		return false;
	}

	UPackage* LoadedPackage = LoadPackage(nullptr, *Fixture.PackageName, LOAD_None);
	if (!TestNotNull(TEXT("Saved Cue Type package reloads"), LoadedPackage))
	{
		return false;
	}
	UBlueprint* LoadedBlueprintObject = FindObject<UBlueprint>(
		LoadedPackage, *SavedBlueprintName.ToString());
	UPaper2DPlusFrameCueBlueprint* LoadedBlueprint =
		Cast<UPaper2DPlusFrameCueBlueprint>(LoadedBlueprintObject);
	UPaper2DPlusFrameCueBlueprint* LoadedRangeBlueprint =
		FindObject<UPaper2DPlusFrameCueBlueprint>(
			LoadedPackage,
			*RangeBlueprintName.ToString());
	UPaper2DPlusCharacterProfileAsset* LoadedProfile =
		FindObject<UPaper2DPlusCharacterProfileAsset>(
			LoadedPackage, *ProfileName.ToString());
	if (!TestNotNull(TEXT("Reloaded package contains the saved Blueprint object"), LoadedBlueprintObject)
		|| !TestNotNull(TEXT("Reload uses the specialized runtime Blueprint envelope"), LoadedBlueprint)
		|| !TestNotNull(TEXT("Reload contains the specialized Cue State Type"), LoadedRangeBlueprint)
		|| !TestNotNull(TEXT("Reloaded package contains the Profile"), LoadedProfile))
	{
		return false;
	}

	TestTrue(TEXT("Reloaded Cue Type keeps the Moment parent"),
		LoadedBlueprint->ParentClass == UPaper2DPlusCue::StaticClass());
	if (!TestTrue(TEXT("Reloaded generated class remains a specialized Cue"),
		LoadedBlueprint->GeneratedClass
		&& LoadedBlueprint->GeneratedClass->IsChildOf(UPaper2DPlusCue::StaticClass())
		&& LoadedBlueprint->GeneratedClass->GetClass()->IsChildOf(
			UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass())))
	{
		return false;
	}
	TestTrue(TEXT("Reloaded Cue Type is compiled without errors"),
		LoadedBlueprint->Status != BS_Error);
	TestTrue(TEXT("Reloaded Cue State Type keeps the Range parent and specialized class"),
		LoadedRangeBlueprint->ParentClass == UPaper2DPlusCueState::StaticClass()
		&& LoadedRangeBlueprint->GeneratedClass
		&& LoadedRangeBlueprint->GeneratedClass->IsChildOf(
			UPaper2DPlusCueState::StaticClass())
		&& LoadedRangeBlueprint->GeneratedClass->GetClass()->IsChildOf(
			UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass()));
	const UPaper2DPlusCueState* LoadedRangeDefaults = Cast<UPaper2DPlusCueState>(
		LoadedRangeBlueprint->GeneratedClass->GetDefaultObject());
	TestTrue(TEXT("Reloaded Cue State Type preserves its Update lifecycle default"),
		LoadedRangeDefaults && LoadedRangeDefaults->bEmitUpdates);
	TestEqual(TEXT("Durable schema version survives the asset roundtrip"),
		LoadedBlueprint->DurableSchemaVersion, MomentSaveAttempt.CandidateVersion);
	TestEqual(TEXT("Durable schema fingerprint survives the asset roundtrip"),
		LoadedBlueprint->DurableSchemaFingerprint, MomentSaveAttempt.CandidateFingerprint);
	TestEqual(TEXT("Range durable schema version survives the asset roundtrip"),
		LoadedRangeBlueprint->DurableSchemaVersion, RangeSaveAttempt.CandidateVersion);
	TestEqual(TEXT("Range durable schema fingerprint survives the asset roundtrip"),
		LoadedRangeBlueprint->DurableSchemaFingerprint, RangeSaveAttempt.CandidateFingerprint);

	const FBPVariableDescription* LoadedPowerDescription =
		FindVariable(*LoadedBlueprint, TEXT("Power"));
	TestTrue(TEXT("Payload variable identity survives the asset roundtrip"),
		LoadedPowerDescription && LoadedPowerDescription->VarGuid == PowerGuid);
	FIntProperty* LoadedPowerProperty = FindFProperty<FIntProperty>(
		LoadedBlueprint->GeneratedClass, TEXT("Power"));
	if (TestNotNull(TEXT("Reloaded generated class contains Power"), LoadedPowerProperty))
	{
		TestEqual(TEXT("Reloaded class default remains authored"),
			LoadedPowerProperty->GetPropertyValue_InContainer(
				LoadedBlueprint->GeneratedClass->GetDefaultObject()), 12);
	}
	FArrayProperty* LoadedImpactOffsetsProperty = FindVectorArrayProperty(
		LoadedBlueprint->GeneratedClass, TEXT("ImpactOffsets"));
	FSoftClassProperty* LoadedHandlerClassProperty = FindActorSoftClassProperty(
		LoadedBlueprint->GeneratedClass, TEXT("HandlerClass"));
	if (!TestNotNull(TEXT("Reloaded generated class preserves the FVector array type"),
		LoadedImpactOffsetsProperty)
		|| !TestNotNull(TEXT("Reloaded generated class preserves the Actor soft-class type"),
			LoadedHandlerClassProperty))
	{
		return false;
	}
	const UObject* LoadedGeneratedDefaults =
		LoadedBlueprint->GeneratedClass->GetDefaultObject();
	TestTrue(TEXT("Reloaded generated class preserves the FVector array default"),
		VectorArrayEquals(
			*LoadedImpactOffsetsProperty,
			LoadedGeneratedDefaults,
			DefaultImpactOffsets));
	TestEqual(TEXT("Reloaded generated class preserves the soft-class default"),
		GetSoftClassPath(*LoadedHandlerClassProperty, LoadedGeneratedDefaults),
		DefaultHandlerClassPath);

	if (!TestTrue(TEXT("Reloaded Profile retains one animation and both Cue kinds"),
		LoadedProfile->Flipbooks.Num() == 1
		&& LoadedProfile->Flipbooks[0].FrameEventData.FrameCues.Num() == 2))
	{
		return false;
	}
	UPaper2DPlusCueBase* LoadedPlacement =
		LoadedProfile->Flipbooks[0].FrameEventData.FrameCues[0];
	if (!TestNotNull(TEXT("Reloaded generated placement is valid"), LoadedPlacement))
	{
		return false;
	}
	TestTrue(TEXT("Reloaded placement uses exactly the current generated class"),
		LoadedPlacement->GetClass() == LoadedBlueprint->GeneratedClass);
	TestTrue(TEXT("Reloaded placement class belongs to the specialized asset"),
		LoadedPlacement->GetClass()->ClassGeneratedBy == LoadedBlueprint);
	TestTrue(TEXT("Reloaded placement remains owned by the Profile"),
		LoadedPlacement->GetOuter() == LoadedProfile);
	TestEqual(TEXT("Reloaded placement anchor survives"),
		LoadedPlacement->GetPrimaryAnchorFrame(), 3);
	FIntProperty* LoadedPlacementPower = FindFProperty<FIntProperty>(
		LoadedPlacement->GetClass(), TEXT("Power"));
	if (TestNotNull(TEXT("Reloaded placement contains Power"), LoadedPlacementPower))
	{
		TestEqual(TEXT("Reloaded placement preserves its payload override"),
			LoadedPlacementPower->GetPropertyValue_InContainer(LoadedPlacement), 37);
	}
	TestTrue(TEXT("Reloaded placement preserves its FVector array override"),
		VectorArrayEquals(
			*LoadedImpactOffsetsProperty,
			LoadedPlacement,
			PlacementImpactOffsets));
	TestEqual(TEXT("Reloaded placement preserves its soft-class override"),
		GetSoftClassPath(*LoadedHandlerClassProperty, LoadedPlacement),
		PlacementHandlerClassPath);

	UPaper2DPlusCueState* LoadedRangePlacement = Cast<UPaper2DPlusCueState>(
		LoadedProfile->Flipbooks[0].FrameEventData.FrameCues[1]);
	const FIntProperty* LoadedRangePower = LoadedRangePlacement
		? FindFProperty<FIntProperty>(LoadedRangePlacement->GetClass(), TEXT("Power"))
		: nullptr;
	TestTrue(TEXT("Reloaded Range placement keeps class, owner, timing, lifecycle, and payload"),
		LoadedRangePlacement
		&& LoadedRangePlacement->GetClass() == LoadedRangeBlueprint->GeneratedClass
		&& LoadedRangePlacement->GetOuter() == LoadedProfile
		&& LoadedRangePlacement->StartFrame == 0
		&& LoadedRangePlacement->FrameCount == 2
		&& LoadedRangePlacement->bEmitUpdates
		&& LoadedRangePower
		&& LoadedRangePower->GetPropertyValue_InContainer(LoadedRangePlacement) == 83);

	TestEqual(TEXT("Reloaded Cue Type keeps exactly one permitted behavior event graph"),
		LoadedBlueprint->UbergraphPages.Num(), 1);
	TestEqual(TEXT("Reloaded Cue Type contains no function graphs"),
		LoadedBlueprint->FunctionGraphs.Num(), 0);
	TestEqual(TEXT("Reloaded Cue Type contains no macro graphs"),
		LoadedBlueprint->MacroGraphs.Num(), 0);
	TestEqual(TEXT("Reloaded Cue Type contains no delegate graphs"),
		LoadedBlueprint->DelegateSignatureGraphs.Num(), 0);

	LoadedPackage->SetDirtyFlag(false);
	LoadedPlacement = nullptr;
	LoadedRangePlacement = nullptr;
	LoadedProfile = nullptr;
	LoadedBlueprint = nullptr;
	LoadedRangeBlueprint = nullptr;
	LoadedPackage = nullptr;
	FText FinalUnloadError;
	if (!TestTrue(TEXT("Reloaded Cue Type package can be cleanly released"),
		Fixture.Unload(FinalUnloadError)))
	{
		AddError(FString::Printf(
			TEXT("Reloaded Cue Type package unload failed: %s"),
			*FinalUnloadError.ToString()));
		return false;
	}

	UPackage* SaveRejectPackage = CreatePackage(*SaveGuardRejectFixture.PackageName);
	if (!TestNotNull(TEXT("Persistent-save guard rejection package is created"),
		SaveRejectPackage))
	{
		return false;
	}
	SaveRejectPackage->AddToRoot();
	UPaper2DPlusFrameCueBlueprint* SaveRejectBlueprint = MakeCompiledCueBlueprint(
		UPaper2DPlusCue::StaticClass(),
		TEXT("BP_SaveGuardRejectCueType"),
		SaveRejectPackage);
	if (!TestNotNull(TEXT("Persistent-save guard fixture starts as a clean Cue Type"),
		SaveRejectBlueprint))
	{
		SaveRejectPackage->RemoveFromRoot();
		return false;
	}
	SaveRejectBlueprint->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
	FBPVariableDescription* SaveRejectPower =
		FindVariable(*SaveRejectBlueprint, TEXT("Power"));
	if (!TestNotNull(TEXT("Persistent-save guard fixture retains Power"), SaveRejectPower))
	{
		SaveRejectPackage->RemoveFromRoot();
		return false;
	}
	SaveRejectPower->PropertyFlags |= CPF_Transient;
	SaveRejectBlueprint->Status = BS_UpToDate;
	SaveRejectPackage->MarkPackageDirty();
	AddExpectedError(
		TEXT("Cannot persist Frame Cue Type asset"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	const bool bInvalidPersistentSaveRejected = !UPackage::SavePackage(
		SaveRejectPackage,
		SaveRejectBlueprint,
		*SaveGuardRejectFixture.FilePath,
		SaveArgs);
	SaveRejectPackage->RemoveFromRoot();
	TestTrue(TEXT("Persistent serialization refuses an invalid Cue Type without bypass"),
		bInvalidPersistentSaveRejected);
	SaveRejectPackage = nullptr;
	SaveRejectBlueprint = nullptr;
	SaveRejectPower = nullptr;
	FText SaveRejectUnloadError;
	if (!TestTrue(TEXT("Rejected persistent-save fixture fully unloads"),
		SaveGuardRejectFixture.Unload(SaveRejectUnloadError)))
	{
		AddError(FString::Printf(
			TEXT("Rejected persistent-save fixture unload failed: %s"),
			*SaveRejectUnloadError.ToString()));
		return false;
	}
	TestTrue(TEXT("Rejected persistent save leaves no usable package sidecar"),
		SaveGuardRejectFixture.DeleteFiles()
		&& !IFileManager::Get().FileExists(*SaveGuardRejectFixture.FilePath));

	// Retained packaging runs need one deliberately invalid serialized asset. It compiles cleanly
	// first, then receives a source replication mutation. Unreal necessarily compiles Blueprints in
	// PreSave, so the test-only bypass permits that one synchronization and the package write. This
	// proves that a real cook independently validates both serialized source and generated exports.
	const FName InvalidBlueprintName(TEXT("BP_InvalidCookCueType"));
	const FName InvalidProfileName(TEXT("InvalidCookCueProfile"));
	UPackage* InvalidPackage = CreatePackage(*InvalidCookFixture.PackageName);
	if (!TestNotNull(TEXT("Invalid-cook Cue Type package is created"), InvalidPackage))
	{
		return false;
	}
	InvalidPackage->AddToRoot();
	UPaper2DPlusFrameCueBlueprint* InvalidBlueprint = MakeCompiledCueBlueprint(
		UPaper2DPlusCue::StaticClass(),
		*InvalidBlueprintName.ToString(),
		InvalidPackage);
	UPaper2DPlusCharacterProfileAsset* InvalidProfile =
		NewObject<UPaper2DPlusCharacterProfileAsset>(
			InvalidPackage,
			InvalidProfileName,
			RF_Public | RF_Standalone | RF_Transactional);
	if (!TestNotNull(TEXT("Invalid-cook fixture first creates a clean specialized Cue Type"),
		InvalidBlueprint)
		|| !TestNotNull(TEXT("Invalid-cook fixture creates its Profile"), InvalidProfile))
	{
		InvalidPackage->RemoveFromRoot();
		return false;
	}
	InvalidBlueprint->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
	FText CleanInvalidFixtureError;
	if (!TestTrue(TEXT("Invalid-cook fixture satisfies the compiled contract before tampering"),
		UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
			*InvalidBlueprint,
			&CleanInvalidFixtureError)))
	{
		InvalidPackage->RemoveFromRoot();
		return false;
	}

	FFlipbookProfileEntry& InvalidEntry =
		InvalidProfile->Flipbooks.AddDefaulted_GetRef();
	InvalidEntry.Identity.FlipbookName = TEXT("InvalidCookAttack");
	InvalidEntry.CombatData.Frames.SetNum(4);
	UPaper2DPlusCueBase* InvalidPlacement = CreatePlacement(
		InvalidProfile,
		InvalidBlueprint->GeneratedClass,
		1,
		4);
	FIntProperty* InvalidPlacementPower = InvalidPlacement
		? FindFProperty<FIntProperty>(InvalidPlacement->GetClass(), TEXT("Power"))
		: nullptr;
	if (!TestNotNull(TEXT("Invalid-cook fixture creates a real Profile placement"),
		InvalidPlacement)
		|| !TestNotNull(TEXT("Invalid-cook fixture placement exposes Power"),
			InvalidPlacementPower))
	{
		InvalidPackage->RemoveFromRoot();
		return false;
	}
	InvalidPlacementPower->SetPropertyValue_InContainer(InvalidPlacement, 73);
	InvalidEntry.FrameEventData.FrameCues.Add(InvalidPlacement);

	FBPVariableDescription* InvalidPowerDescription =
		FindVariable(*InvalidBlueprint, TEXT("Power"));
	if (!TestNotNull(TEXT("Invalid-cook fixture retains its authored Power source field"),
		InvalidPowerDescription))
	{
		InvalidPackage->RemoveFromRoot();
		return false;
	}
	InvalidPowerDescription->PropertyFlags |= CPF_Net;
	InvalidPowerDescription->ReplicationCondition = COND_OwnerOnly;
	InvalidBlueprint->Status = BS_UpToDate;
	InvalidPackage->MarkPackageDirty();
	bool bInvalidFixtureSaved = false;
	{
		// This is the sole bypass in the suite: it persists the intentionally invalid asset so a
		// real UAT cook can prove that the generated-class cook boundary fails closed.
		FScopedPersistentSaveGuardBypass SaveGuardBypass;
		bInvalidFixtureSaved = UPackage::SavePackage(
			InvalidPackage,
			InvalidProfile,
			*InvalidCookFixture.FilePath,
			SaveArgs);
	}
	InvalidPackage->RemoveFromRoot();
	if (!TestTrue(TEXT("Invalid-cook fixture persists through Unreal's mandatory pre-save compile"),
		bInvalidFixtureSaved))
	{
		return false;
	}
	// Unloading an editor Blueprint regenerates its class once. The fixture is intentionally
	// invalid, so consume that one expected compiler refusal while still proving the package can
	// unload/reload and the later real UAT cook independently rejects it.
	AddExpectedError(
		TEXT("Frame Cue Type compilation refused: Field 'Power' uses replication or RepNotify."),
		EAutomationExpectedErrorFlags::Contains,
		1);

	InvalidPackage = nullptr;
	InvalidBlueprint = nullptr;
	InvalidProfile = nullptr;
	InvalidPlacement = nullptr;
	InvalidPlacementPower = nullptr;
	InvalidPowerDescription = nullptr;
	FText InvalidUnloadError;
	if (!TestTrue(TEXT("Pre-save-compiled invalid-cook fixture fully unloads"),
		InvalidCookFixture.Unload(InvalidUnloadError)))
	{
		AddError(FString::Printf(
			TEXT("Invalid-cook fixture unload failed: %s"),
			*InvalidUnloadError.ToString()));
		return false;
	}

	UPackage* LoadedInvalidPackage = LoadPackage(
		nullptr,
		*InvalidCookFixture.PackageName,
		LOAD_None);
	if (!TestNotNull(TEXT("Pre-save-compiled invalid-cook fixture reloads"), LoadedInvalidPackage))
	{
		return false;
	}
	UPaper2DPlusFrameCueBlueprint* LoadedInvalidBlueprint =
		FindObject<UPaper2DPlusFrameCueBlueprint>(
			LoadedInvalidPackage,
			*InvalidBlueprintName.ToString());
	UPaper2DPlusCharacterProfileAsset* LoadedInvalidProfile =
		FindObject<UPaper2DPlusCharacterProfileAsset>(
			LoadedInvalidPackage,
			*InvalidProfileName.ToString());
	if (!TestNotNull(TEXT("Reloaded invalid-cook fixture keeps the specialized Blueprint"),
		LoadedInvalidBlueprint)
		|| !TestNotNull(TEXT("Reloaded invalid-cook fixture keeps the Profile"),
			LoadedInvalidProfile))
	{
		return false;
	}
	const FBPVariableDescription* LoadedInvalidPowerDescription =
		FindVariable(*LoadedInvalidBlueprint, TEXT("Power"));
	TestTrue(TEXT("Save/reload preserves the source replicated-field tamper"),
		LoadedInvalidPowerDescription
		&& (LoadedInvalidPowerDescription->PropertyFlags & CPF_Net) != 0
		&& LoadedInvalidPowerDescription->ReplicationCondition == COND_OwnerOnly);
	if (!TestTrue(TEXT("Invalid-cook fixture retains the custom generated-class envelope"),
		LoadedInvalidBlueprint->GeneratedClass
		&& LoadedInvalidBlueprint->GeneratedClass->GetClass()->IsChildOf(
			UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass())
		&& LoadedInvalidBlueprint->GeneratedClass->ClassGeneratedBy
			== LoadedInvalidBlueprint))
	{
		return false;
	}
	TestEqual(TEXT("Invalid-cook fixture exposes the deterministic generated-class path"),
		LoadedInvalidBlueprint->GeneratedClass->GetPathName(),
		FString::Printf(
			TEXT("%s.%s_C"),
			*InvalidCookFixture.PackageName,
			*InvalidBlueprintName.ToString()));
	const FIntProperty* LoadedInvalidGeneratedPower = FindFProperty<FIntProperty>(
		LoadedInvalidBlueprint->GeneratedClass, TEXT("Power"));
	TestTrue(TEXT("Mandatory pre-save compile synchronizes the generated replication flags"),
		LoadedInvalidGeneratedPower
		&& LoadedInvalidGeneratedPower->HasAnyPropertyFlags(CPF_Net)
		&& LoadedInvalidGeneratedPower->GetBlueprintReplicationCondition()
			== COND_OwnerOnly);

	FText InvalidContractError;
	TestFalse(TEXT("Shared contract rejects the reloaded invalid-cook fixture"),
		UPaper2DPlusFrameCueBlueprint::ValidateDataOnlyContract(
			*LoadedInvalidBlueprint,
			&InvalidContractError));
	const FString ExpectedInvalidContractError =
		TEXT("Field 'Power' uses replication or RepNotify. Cue payload fields are serialized data and cannot own network callbacks.");
	TestEqual(TEXT("Invalid-cook fixture reports the exact replicated-field diagnostic"),
		InvalidContractError.ToString(),
		ExpectedInvalidContractError);
	FText InvalidCompiledContractError;
	TestFalse(TEXT("Compiled contract also rejects the serialized source tamper"),
		UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
			*LoadedInvalidBlueprint,
			&InvalidCompiledContractError));
	TestEqual(TEXT("Compiled contract preserves the exact replicated-field diagnostic"),
		InvalidCompiledContractError.ToString(),
		ExpectedInvalidContractError);

	if (!TestTrue(TEXT("Reloaded invalid-cook Profile retains its exact Cue placement"),
		LoadedInvalidProfile->Flipbooks.Num() == 1
		&& LoadedInvalidProfile->Flipbooks[0].FrameEventData.FrameCues.Num() == 1))
	{
		return false;
	}
	UPaper2DPlusCueBase* LoadedInvalidPlacement =
		LoadedInvalidProfile->Flipbooks[0].FrameEventData.FrameCues[0];
	const FIntProperty* LoadedInvalidPlacementPower = LoadedInvalidPlacement
		? FindFProperty<FIntProperty>(LoadedInvalidPlacement->GetClass(), TEXT("Power"))
		: nullptr;
	TestTrue(TEXT("Reloaded invalid-cook placement keeps class, owner, and payload"),
		LoadedInvalidPlacement
		&& LoadedInvalidPlacement->GetClass() == LoadedInvalidBlueprint->GeneratedClass
		&& LoadedInvalidPlacement->GetOuter() == LoadedInvalidProfile
		&& LoadedInvalidPlacementPower
		&& LoadedInvalidPlacementPower->GetPropertyValue_InContainer(
			LoadedInvalidPlacement) == 73);

	LoadedInvalidPackage->SetDirtyFlag(false);
	LoadedInvalidPlacement = nullptr;
	LoadedInvalidProfile = nullptr;
	LoadedInvalidBlueprint = nullptr;
	LoadedInvalidPackage = nullptr;
	FText FinalInvalidUnloadError;
	if (!TestTrue(TEXT("Reloaded invalid-cook fixture can be cleanly released"),
		InvalidCookFixture.Unload(FinalInvalidUnloadError)))
	{
		AddError(FString::Printf(
			TEXT("Reloaded invalid-cook fixture unload failed: %s"),
			*FinalInvalidUnloadError.ToString()));
		return false;
	}

	// The second retained failure fixture proves the behavioral half of the cook contract
	// independently from the replicated-payload fixture above. Keep it in a separate cook directory
	// so each UAT invocation can only encounter the defect that its evidence row names.
	const FName DeferredInvalidBlueprintName(TEXT("BP_DeferredInvalidCookCueType"));
	UPackage* DeferredInvalidPackage =
		CreatePackage(*DeferredInvalidCookFixture.PackageName);
	if (!TestNotNull(TEXT("Deferred-invalid Cue Type package is created"),
		DeferredInvalidPackage))
	{
		return false;
	}
	DeferredInvalidPackage->AddToRoot();
	UPaper2DPlusFrameCueBlueprint* DeferredInvalidBlueprint =
		MakeCompiledCueBlueprint(
			UPaper2DPlusCue::StaticClass(),
			*DeferredInvalidBlueprintName.ToString(),
			DeferredInvalidPackage);
	if (!TestNotNull(TEXT("Deferred-invalid fixture starts as a clean Cue Type"),
		DeferredInvalidBlueprint))
	{
		DeferredInvalidPackage->RemoveFromRoot();
		return false;
	}
	DeferredInvalidBlueprint->SetFlags(
		RF_Public | RF_Standalone | RF_Transactional);
	UK2Node_CallFunction* DeferredDelayNode = InjectDeferredCall(
		*DeferredInvalidBlueprint,
		GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Delay));
	if (!TestNotNull(TEXT("Deferred-invalid fixture injects a real Delay call"),
		DeferredDelayNode))
	{
		DeferredInvalidPackage->RemoveFromRoot();
		return false;
	}
	UEdGraph* DeferredBehaviorGraph =
		FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(
			*DeferredInvalidBlueprint);
	const UFunction* DeferredDelayFunction =
		DeferredDelayNode->GetTargetFunction();
	if (!TestNotNull(TEXT("Deferred-invalid fixture retains its behavior graph"),
		DeferredBehaviorGraph)
		|| !TestNotNull(TEXT("Deferred-invalid fixture resolves the latent function"),
			DeferredDelayFunction))
	{
		DeferredInvalidPackage->RemoveFromRoot();
		return false;
	}
	const FString ExpectedDeferredContractError = FString::Printf(
		TEXT("Frame Cue Type '%s' behavior event(s) 'OnCueTriggered' contains deferred node/call "
			"'%s', reached from authored Cue Type '%s' through graph '%s': "
			"the referenced function is latent and can resume after the scoped Cue invocation has ended. "
			"Frame Cue placements are shared, and their owner/component/context exists only for "
			"synchronous dispatch, so deferred work cannot safely resume on the placement."),
		*DeferredInvalidBlueprint->GetPathName(),
		*DeferredDelayFunction->GetPathName(),
		*DeferredInvalidBlueprint->GetPathName(),
		*DeferredBehaviorGraph->GetPathName());
	FText DeferredContractError;
	TestFalse(TEXT("Shared policy rejects the deferred-invalid source fixture"),
		FPaper2DPlusFrameCueTypeAuthoring::ValidateSynchronousBehavior(
			*DeferredInvalidBlueprint,
			&DeferredContractError));
	TestEqual(TEXT("Deferred-invalid fixture reports its deterministic exact diagnosis"),
		DeferredContractError.ToString(),
		ExpectedDeferredContractError);

	DeferredInvalidBlueprint->Status = BS_UpToDate;
	DeferredInvalidPackage->MarkPackageDirty();
	bool bDeferredInvalidFixtureSaved = false;
	{
		// The bypass exists solely to retain invalid automation fixtures. A real cook runs without
		// it, traverses the serialized source graph, and must reject this package before staging.
		// Keep the last valid generated class deliberately stale: recompiling the invalid source
		// would exercise the direct-compile rejection before there is a package for the real cook.
		FScopedPersistentSaveGuardBypass SaveGuardBypass;
		bDeferredInvalidFixtureSaved =
			DeferredInvalidBlueprint->Status != BS_Error
			&& UPackage::SavePackage(
				DeferredInvalidPackage,
				DeferredInvalidBlueprint,
				*DeferredInvalidCookFixture.FilePath,
				SaveArgs);
	}
	DeferredInvalidPackage->RemoveFromRoot();
	if (!TestTrue(TEXT("Deferred-invalid fixture persists only through the automation bypass"),
		bDeferredInvalidFixtureSaved))
	{
		return false;
	}
	AddExpectedError(
		FString::Printf(
			TEXT("Frame Cue Type compilation refused: Frame Cue Type '%s'"),
			*DeferredInvalidBlueprint->GetPathName()),
		EAutomationExpectedErrorFlags::Contains,
		1);

	DeferredInvalidPackage = nullptr;
	DeferredInvalidBlueprint = nullptr;
	DeferredDelayNode = nullptr;
	DeferredBehaviorGraph = nullptr;
	DeferredDelayFunction = nullptr;
	FText DeferredInvalidUnloadError;
	if (!TestTrue(TEXT("Deferred-invalid fixture fully unloads"),
		DeferredInvalidCookFixture.Unload(DeferredInvalidUnloadError)))
	{
		AddError(FString::Printf(
			TEXT("Deferred-invalid fixture unload failed: %s"),
			*DeferredInvalidUnloadError.ToString()));
		return false;
	}

	UPackage* LoadedDeferredInvalidPackage = LoadPackage(
		nullptr,
		*DeferredInvalidCookFixture.PackageName,
		LOAD_None);
	UPaper2DPlusFrameCueBlueprint* LoadedDeferredInvalidBlueprint =
		LoadedDeferredInvalidPackage
			? FindObject<UPaper2DPlusFrameCueBlueprint>(
				LoadedDeferredInvalidPackage,
				*DeferredInvalidBlueprintName.ToString())
			: nullptr;
	if (!TestNotNull(TEXT("Deferred-invalid fixture reloads"),
		LoadedDeferredInvalidPackage)
		|| !TestNotNull(TEXT("Reloaded deferred-invalid fixture keeps its Blueprint"),
			LoadedDeferredInvalidBlueprint)
		|| !TestNotNull(TEXT("Reloaded deferred-invalid fixture keeps its generated class"),
			LoadedDeferredInvalidBlueprint
				? LoadedDeferredInvalidBlueprint->GeneratedClass.Get()
				: nullptr))
	{
		return false;
	}
	TestEqual(TEXT("Deferred-invalid fixture exposes the deterministic generated-class path"),
		LoadedDeferredInvalidBlueprint->GeneratedClass->GetPathName(),
		FString::Printf(
			TEXT("%s.%s_C"),
			*DeferredInvalidCookFixture.PackageName,
			*DeferredInvalidBlueprintName.ToString()));
	FText LoadedDeferredContractError;
	TestFalse(TEXT("Reloaded deferred-invalid fixture remains policy-invalid"),
		FPaper2DPlusFrameCueTypeAuthoring::ValidateSynchronousBehavior(
			*LoadedDeferredInvalidBlueprint,
			&LoadedDeferredContractError));
	TestEqual(TEXT("Save/reload preserves the exact deferred-behavior diagnosis"),
		LoadedDeferredContractError.ToString(),
		ExpectedDeferredContractError);

	LoadedDeferredInvalidPackage->SetDirtyFlag(false);
	LoadedDeferredInvalidBlueprint = nullptr;
	LoadedDeferredInvalidPackage = nullptr;
	FText FinalDeferredInvalidUnloadError;
	if (!TestTrue(TEXT("Reloaded deferred-invalid fixture can be cleanly released"),
		DeferredInvalidCookFixture.Unload(FinalDeferredInvalidUnloadError)))
	{
		AddError(FString::Printf(
			TEXT("Reloaded deferred-invalid fixture unload failed: %s"),
			*FinalDeferredInvalidUnloadError.ToString()));
		return false;
	}
	if (bRetainCookFixture)
	{
		if (!TestTrue(TEXT("Valid and both independently invalid cook fixtures remain available for packaging"),
			IFileManager::Get().FileExists(*Fixture.FilePath)
			&& IFileManager::Get().FileExists(*InvalidCookFixture.FilePath)
			&& IFileManager::Get().FileExists(*DeferredInvalidCookFixture.FilePath))
			|| HasAnyErrors())
		{
			return false;
		}
		Fixture.MarkSucceeded();
		InvalidCookFixture.MarkSucceeded();
		DeferredInvalidCookFixture.MarkSucceeded();
		return true;
	}
	TestTrue(TEXT("Persistent Cue Type fixtures remove every package sidecar"),
		Fixture.DeleteFiles()
		&& InvalidCookFixture.DeleteFiles()
		&& DeferredInvalidCookFixture.DeleteFiles());
	TestTrue(TEXT("Persistent Cue Type fixtures leave no assets on disk"),
		!IFileManager::Get().FileExists(*Fixture.FilePath)
		&& !IFileManager::Get().FileExists(*InvalidCookFixture.FilePath)
		&& !IFileManager::Get().FileExists(*DeferredInvalidCookFixture.FilePath));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeProfileReinstanceFeasibilityTest,
	"Paper2DPlus.FrameCues.CueType.Feasibility.ProfilePlacementReinstanceUndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeProfileReinstanceFeasibilityTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeFeasibilityTest;
	using namespace Paper2DPlusFrameCueEditorAuthoring;
	if (!TestNotNull(TEXT("GEditor is available for Profile transaction proof"), GEditor))
	{
		return false;
	}

	FScopedCueTypeFixturePackage Fixture(
		*this, TEXT("P2DPProfileCueReinstance"), false);
	if (!TestTrue(TEXT("Profile reinstance fixture starts from a clean package path"),
		Fixture.IsPrepared()))
	{
		AddError(Fixture.PreparationError);
		return false;
	}

	const FName BlueprintName(TEXT("BP_ProfileReinstanceCue"));
	const FName SentinelBlueprintName(TEXT("BP_ProfileReinstanceSentinelCue"));
	const FName ProfileName(TEXT("ProfileReinstanceAsset"));
	{
		UPackage* Package = CreatePackage(*Fixture.PackageName);
		if (!TestNotNull(TEXT("Profile reinstance package is created"), Package))
		{
			return false;
		}
		FScopedRoot PackageRoot(Package);

		UPaper2DPlusFrameCueBlueprint* Blueprint = MakeCompiledCueBlueprint(
			UPaper2DPlusCue::StaticClass(),
			*BlueprintName.ToString(),
			Package);
		UPaper2DPlusCharacterProfileAsset* Profile =
			NewObject<UPaper2DPlusCharacterProfileAsset>(
				Package,
				ProfileName,
				RF_Public | RF_Standalone | RF_Transactional);
		if (!TestNotNull(TEXT("Saved Profile reinstance Cue Type compiles"), Blueprint)
			|| !TestNotNull(TEXT("Saved Profile reinstance asset is created"), Profile))
		{
			return false;
		}
		Blueprint->SetFlags(RF_Public | RF_Standalone | RF_Transactional);

		FFlipbookProfileEntry& Entry = Profile->Flipbooks.AddDefaulted_GetRef();
		Entry.Identity.FlipbookName = TEXT("slash");
		Entry.CombatData.Frames.SetNum(4);
		// The order sentinel has to survive a real package save/reload and must NOT be reinstanced
		// when the placement's Cue Type recompiles, so it needs a concrete Cue class distinct from
		// that type. A second saved Cue Type Blueprint is used rather than an editor-module test
		// class: serializing an editor-only class into a /Game fixture package is exactly the kind
		// of asset a cook would then have to drop.
		UPaper2DPlusFrameCueBlueprint* SentinelType = MakeCompiledCueBlueprint(
			UPaper2DPlusCue::StaticClass(),
			*SentinelBlueprintName.ToString(),
			Package);
		if (!TestNotNull(TEXT("Saved Profile order-sentinel Cue Type compiles"), SentinelType))
		{
			return false;
		}
		SentinelType->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
		UPaper2DPlusCue* Sentinel = Cast<UPaper2DPlusCue>(CreatePlacement(
			Profile, SentinelType->GeneratedClass, 0, 4));
		if (!TestNotNull(TEXT("Saved Profile order sentinel is created"), Sentinel))
		{
			return false;
		}
		Sentinel->DebugName = TEXT("OrderSentinel");
		Entry.FrameEventData.FrameCues.Add(Sentinel);

		UPaper2DPlusCueBase* Placement = CreatePlacement(
			Profile, Blueprint->GeneratedClass, 2, 4);
		FIntProperty* Power = Placement
			? FindFProperty<FIntProperty>(Placement->GetClass(), TEXT("Power"))
			: nullptr;
		if (!TestNotNull(TEXT("Saved Profile generated placement is created"), Placement)
			|| !TestNotNull(TEXT("Saved Profile placement exposes Power"), Power))
		{
			return false;
		}
		Power->SetPropertyValue_InContainer(Placement, 23);
		Entry.FrameEventData.FrameCues.Add(Placement);
		Package->MarkPackageDirty();
		FPaper2DPlusFrameCueDurableSaveAttempt CueSaveAttempt;
		FPaper2DPlusFrameCueDurableSaveAttempt SentinelSaveAttempt;
		FText CuePrepareError;
		FText SentinelPrepareError;
		const bool bCuePrepared =
			FPaper2DPlusFrameCueTypeEditor::PrepareDurableSchemaSaveAttempt(
				*Blueprint,
				CueSaveAttempt,
				&CuePrepareError);
		const bool bSentinelPrepared =
			FPaper2DPlusFrameCueTypeEditor::PrepareDurableSchemaSaveAttempt(
				*SentinelType,
				SentinelSaveAttempt,
				&SentinelPrepareError);
		if (!TestTrue(TEXT("Profile Cue Type stages its exact durable schema before save"),
				bCuePrepared)
			|| !TestTrue(TEXT("Profile order-sentinel Cue Type stages its exact durable schema before save"),
				bSentinelPrepared))
		{
			if (!CuePrepareError.IsEmpty())
			{
				AddError(CuePrepareError.ToString());
			}
			if (!SentinelPrepareError.IsEmpty())
			{
				AddError(SentinelPrepareError.ToString());
			}
			return false;
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Package, Profile, *Fixture.FilePath, SaveArgs);
		FText CueFinalizeError;
		FText SentinelFinalizeError;
		const bool bCueFinalized =
			FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(
				*Blueprint,
				CueSaveAttempt,
				&CueFinalizeError);
		const bool bSentinelFinalized =
			FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(
				*SentinelType,
				SentinelSaveAttempt,
				&SentinelFinalizeError);
		if (!TestTrue(TEXT("Profile Cue Type and placement save before reinstance proof"),
			bSaved && bCueFinalized && bSentinelFinalized))
		{
			if (!CueFinalizeError.IsEmpty())
			{
				AddError(CueFinalizeError.ToString());
			}
			if (!SentinelFinalizeError.IsEmpty())
			{
				AddError(SentinelFinalizeError.ToString());
			}
			return false;
		}
	}

	FText InitialUnloadError;
	if (!TestTrue(TEXT("Saved Profile reinstance package fully unloads"),
		Fixture.Unload(InitialUnloadError)))
	{
		AddError(FString::Printf(
			TEXT("Profile reinstance fixture unload failed: %s"),
			*InitialUnloadError.ToString()));
		return false;
	}

	UPackage* LoadedPackage = LoadPackage(nullptr, *Fixture.PackageName, LOAD_None);
	UPaper2DPlusFrameCueBlueprint* LoadedBlueprint = LoadedPackage
		? FindObject<UPaper2DPlusFrameCueBlueprint>(LoadedPackage, *BlueprintName.ToString())
		: nullptr;
	UPaper2DPlusCharacterProfileAsset* LoadedProfile = LoadedPackage
		? FindObject<UPaper2DPlusCharacterProfileAsset>(LoadedPackage, *ProfileName.ToString())
		: nullptr;
	if (!TestNotNull(TEXT("Profile reinstance package reloads"), LoadedPackage)
		|| !TestNotNull(TEXT("Reloaded Profile uses the specialized Cue Type envelope"), LoadedBlueprint)
		|| !TestNotNull(TEXT("Reloaded Profile asset is linker-backed"), LoadedProfile))
	{
		return false;
	}

	{
		FScopedRoot BlueprintRoot(LoadedBlueprint);
		FScopedRoot ProfileRoot(LoadedProfile);
		FScopedTransactionHistory TransactionHistory(NSLOCTEXT(
			"Paper2DPlusCueTypeFeasibility",
			"LoadedProfileReinstanceHistory",
			"Loaded Profile Cue Type Reinstance Test"));
		const auto GetCues = [LoadedProfile]() -> TArray<TObjectPtr<UPaper2DPlusCueBase>>&
		{
			return LoadedProfile->Flipbooks[0].FrameEventData.FrameCues;
		};

		if (!TestTrue(TEXT("Reloaded Profile retains its ordered Cue placements"),
			LoadedProfile->Flipbooks.Num() == 1 && GetCues().Num() == 2))
		{
			return false;
		}
		UPaper2DPlusFrameCueBlueprint* LoadedSentinelType =
			FindObject<UPaper2DPlusFrameCueBlueprint>(
				LoadedPackage, *SentinelBlueprintName.ToString());
		UPaper2DPlusCue* Sentinel = Cast<UPaper2DPlusCue>(GetCues()[0]);
		UPaper2DPlusCueBase* LoadedPlacement = GetCues()[1];
		FIntProperty* LoadedPower = LoadedPlacement
			? FindFProperty<FIntProperty>(LoadedPlacement->GetClass(), TEXT("Power"))
			: nullptr;
		if (!TestNotNull(TEXT("Reloaded Profile keeps its order sentinel"), Sentinel)
			|| !TestNotNull(TEXT("Reloaded Profile placement is valid"), LoadedPlacement)
			|| !TestNotNull(TEXT("Reloaded Profile placement exposes Power"), LoadedPower))
		{
			return false;
		}
		TestEqual(TEXT("Reloaded Profile sentinel identity survives"),
			Sentinel->DebugName, FName(TEXT("OrderSentinel")));
		TestTrue(TEXT("Reloaded Profile sentinel keeps its own distinct Cue Type"),
			LoadedSentinelType != nullptr
			&& Sentinel->GetClass() == LoadedSentinelType->GeneratedClass
			&& Sentinel->GetClass() != LoadedBlueprint->GeneratedClass);
		TestTrue(TEXT("Reloaded Profile placement uses the current generated class"),
			LoadedPlacement->GetClass() == LoadedBlueprint->GeneratedClass);
		TestTrue(TEXT("Reloaded Profile placement remains owned by the Profile"),
			LoadedPlacement->GetOuter() == LoadedProfile);
		TestEqual(TEXT("Reloaded Profile placement retains its persisted anchor"),
			LoadedPlacement->GetPrimaryAnchorFrame(), 2);
		TestEqual(TEXT("Reloaded Profile placement retains its persisted payload"),
			LoadedPower->GetPropertyValue_InContainer(LoadedPlacement), 23);

		{
			FScopedTransaction Transaction(NSLOCTEXT(
				"Paper2DPlusCueTypeFeasibility",
				"EditLoadedProfileCue",
				"Edit Loaded Profile Cue"));
			LoadedProfile->Modify();
			LoadedPlacement->Modify();
			LoadedPlacement->SetPrimaryAnchorFrame(3);
			LoadedPower->SetPropertyValue_InContainer(LoadedPlacement, 37);
		}
		TestTrue(TEXT("Loaded Profile placement edit applies before reinstance"),
			GetCues().Num() == 2 && GetCues()[0] == Sentinel
			&& GetCues()[1]->GetPrimaryAnchorFrame() == 3
			&& LoadedPower->GetPropertyValue_InContainer(GetCues()[1]) == 37);

		TestTrue(TEXT("Loaded Profile placement edit undoes before reinstance"),
			GEditor->UndoTransaction(true));
		TestTrue(TEXT("Pre-reinstance Profile undo targets the loaded placement"),
			GetCues().Num() == 2 && GetCues()[0] == Sentinel
			&& GetCues()[1]->GetPrimaryAnchorFrame() == 2
			&& LoadedPower->GetPropertyValue_InContainer(GetCues()[1]) == 23);
		TestTrue(TEXT("Loaded Profile placement edit redoes before reinstance"),
			GEditor->RedoTransaction());
		TestTrue(TEXT("Pre-reinstance Profile redo restores loaded values and order"),
			GetCues().Num() == 2 && GetCues()[0] == Sentinel
			&& GetCues()[1]->GetPrimaryAnchorFrame() == 3
			&& LoadedPower->GetPropertyValue_InContainer(GetCues()[1]) == 37);

		UPaper2DPlusCueBase* PlacementBeforeCompile = GetCues()[1];
		FScopedReplacementCapture ReplacementCapture(PlacementBeforeCompile);
		FBPVariableDescription* PowerDescription = FindVariable(*LoadedBlueprint, TEXT("Power"));
		if (!TestNotNull(TEXT("Loaded Cue Type exposes the durable Power definition"), PowerDescription))
		{
			return false;
		}
		PowerDescription->DefaultValue = TEXT("19");
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(LoadedBlueprint);
		if (!TestTrue(TEXT("Compatible Radius field is added to the loaded Profile Cue Type"),
			AddEditableFloatPayload(*LoadedBlueprint, TEXT("Radius"), TEXT("1.5"))))
		{
			return false;
		}
		FKismetEditorUtilities::CompileBlueprint(LoadedBlueprint);
		if (!TestTrue(TEXT("Loaded Profile Cue Type recompiles after compatible schema growth"),
			LoadedBlueprint->Status != BS_Error && LoadedBlueprint->GeneratedClass != nullptr))
		{
			return false;
		}

		FIntProperty* ClassPower = FindFProperty<FIntProperty>(
			LoadedBlueprint->GeneratedClass, TEXT("Power"));
		FFloatProperty* ClassRadius = FindFProperty<FFloatProperty>(
			LoadedBlueprint->GeneratedClass, TEXT("Radius"));
		if (TestNotNull(TEXT("Recompiled Profile Cue Type retains Power"), ClassPower))
		{
			TestEqual(TEXT("Recompiled Profile Cue Type applies the edited Power default"),
				ClassPower->GetPropertyValue_InContainer(
					LoadedBlueprint->GeneratedClass->GetDefaultObject()), 19);
		}
		if (TestNotNull(TEXT("Recompiled Profile Cue Type adds Radius"), ClassRadius))
		{
			TestEqual(TEXT("Recompiled Profile Cue Type applies the Radius default"),
				ClassRadius->GetPropertyValue_InContainer(
					LoadedBlueprint->GeneratedClass->GetDefaultObject()), 1.5f);
		}

		TArray<TObjectPtr<UPaper2DPlusCueBase>>& ReinstancedCues = GetCues();
		if (!TestTrue(TEXT("Loaded Profile keeps exactly two ordered Cues after reinstance"),
			ReinstancedCues.Num() == 2 && ReinstancedCues[0] == Sentinel))
		{
			return false;
		}
		UPaper2DPlusCueBase* ReinstancedPlacement = ReinstancedCues[1];
		FIntProperty* ReinstancedPower = ReinstancedPlacement
			? FindFProperty<FIntProperty>(ReinstancedPlacement->GetClass(), TEXT("Power"))
			: nullptr;
		FFloatProperty* ReinstancedRadius = ReinstancedPlacement
			? FindFProperty<FFloatProperty>(ReinstancedPlacement->GetClass(), TEXT("Radius"))
			: nullptr;
		if (!TestNotNull(TEXT("Loaded Profile placement is valid after reinstance"), ReinstancedPlacement)
			|| !TestNotNull(TEXT("Loaded Profile placement retains Power after reinstance"), ReinstancedPower)
			|| !TestNotNull(TEXT("Loaded Profile placement receives Radius after reinstance"), ReinstancedRadius))
		{
			return false;
		}
		TestTrue(TEXT("Loaded Profile placement uses exactly the current generated class"),
			ReinstancedPlacement->GetClass() == LoadedBlueprint->GeneratedClass
			&& ReinstancedPlacement->GetClass()->ClassGeneratedBy == LoadedBlueprint
			&& !ReinstancedPlacement->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists));
		TestTrue(TEXT("Loaded Profile placement preserves outer, anchor, and payload"),
			ReinstancedPlacement->GetOuter() == LoadedProfile
			&& ReinstancedPlacement->GetPrimaryAnchorFrame() == 3
			&& ReinstancedPower->GetPropertyValue_InContainer(ReinstancedPlacement) == 37
			&& FMath::IsNearlyEqual(
				ReinstancedRadius->GetPropertyValue_InContainer(ReinstancedPlacement), 1.5f));
		TestTrue(TEXT("Loaded Profile placement emitted an object-replacement mapping"),
			ReplacementCapture.HitCount > 0 && ReplacementCapture.ReplacementKey.IsSet());
		if (ReplacementCapture.ReplacementKey.IsSet())
		{
			TestTrue(TEXT("Loaded Profile replacement maps to the live placement"),
				ReplacementCapture.ReplacementKey.GetValue() == FObjectKey(ReinstancedPlacement));
		}

		TestTrue(TEXT("Loaded Profile edit transaction undoes after reinstance"),
			GEditor->UndoTransaction(true));
		UPaper2DPlusCueBase* UndonePlacement = GetCues().Num() == 2 ? GetCues()[1].Get() : nullptr;
		FIntProperty* UndonePower = UndonePlacement
			? FindFProperty<FIntProperty>(UndonePlacement->GetClass(), TEXT("Power"))
			: nullptr;
		FFloatProperty* UndoneRadius = UndonePlacement
			? FindFProperty<FFloatProperty>(UndonePlacement->GetClass(), TEXT("Radius"))
			: nullptr;
		TestTrue(TEXT("Post-reinstance Profile undo targets one current loaded placement"),
			UndonePlacement && UndonePower && UndoneRadius
			&& GetCues()[0] == Sentinel
			&& UndonePlacement->GetClass() == LoadedBlueprint->GeneratedClass
			&& UndonePlacement->GetOuter() == LoadedProfile
			&& UndonePlacement->GetPrimaryAnchorFrame() == 2
			&& UndonePower->GetPropertyValue_InContainer(UndonePlacement) == 23
			&& FMath::IsNearlyEqual(
				UndoneRadius->GetPropertyValue_InContainer(UndonePlacement), 1.5f));

		TestTrue(TEXT("Loaded Profile edit transaction redoes after reinstance"),
			GEditor->RedoTransaction());
		UPaper2DPlusCueBase* RedonePlacement = GetCues().Num() == 2 ? GetCues()[1].Get() : nullptr;
		FIntProperty* RedonePower = RedonePlacement
			? FindFProperty<FIntProperty>(RedonePlacement->GetClass(), TEXT("Power"))
			: nullptr;
		FFloatProperty* RedoneRadius = RedonePlacement
			? FindFProperty<FFloatProperty>(RedonePlacement->GetClass(), TEXT("Radius"))
			: nullptr;
		TestTrue(TEXT("Post-reinstance Profile redo preserves current object, order, and payload"),
			RedonePlacement && RedonePower && RedoneRadius
			&& GetCues()[0] == Sentinel
			&& RedonePlacement->GetClass() == LoadedBlueprint->GeneratedClass
			&& RedonePlacement->GetOuter() == LoadedProfile
			&& RedonePlacement->GetPrimaryAnchorFrame() == 3
			&& RedonePower->GetPropertyValue_InContainer(RedonePlacement) == 37
			&& FMath::IsNearlyEqual(
				RedoneRadius->GetPropertyValue_InContainer(RedonePlacement), 1.5f));
	}

	LoadedPackage->SetDirtyFlag(false);
	LoadedProfile = nullptr;
	LoadedBlueprint = nullptr;
	LoadedPackage = nullptr;
	FText FinalUnloadError;
	if (!TestTrue(TEXT("Reloaded Profile reinstance package fully unloads after proof"),
		Fixture.Unload(FinalUnloadError)))
	{
		AddError(FString::Printf(
			TEXT("Reloaded Profile reinstance fixture unload failed: %s"),
			*FinalUnloadError.ToString()));
		return false;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeLayerReinstanceFeasibilityTest,
	"Paper2DPlus.FrameCues.CueType.Feasibility.LayerPlacementReinstanceUndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeLayerReinstanceFeasibilityTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeFeasibilityTest;
	using namespace Paper2DPlusFrameCueEditorAuthoring;
	if (!TestNotNull(TEXT("GEditor is available for Layer transaction proof"), GEditor))
	{
		return false;
	}

	FScopedCueTypeFixturePackage Fixture(
		*this, TEXT("P2DPLayerCueReinstance"), false);
	if (!TestTrue(TEXT("Layer reinstance fixture starts from a clean package path"),
		Fixture.IsPrepared()))
	{
		AddError(Fixture.PreparationError);
		return false;
	}

	const FName BlueprintName(TEXT("BP_LayerReinstanceCue"));
	const FName SentinelBlueprintName(TEXT("BP_LayerReinstanceSentinelCue"));
	const FName ProfileName(TEXT("LayerReinstanceProfile"));
	const FName LayerName(TEXT("LayerReinstanceAsset"));
	{
		UPackage* Package = CreatePackage(*Fixture.PackageName);
		if (!TestNotNull(TEXT("Layer reinstance package is created"), Package))
		{
			return false;
		}
		FScopedRoot PackageRoot(Package);

		UPaper2DPlusFrameCueBlueprint* Blueprint = MakeCompiledCueBlueprint(
			UPaper2DPlusCue::StaticClass(),
			*BlueprintName.ToString(),
			Package);
		UPaper2DPlusCharacterProfileAsset* Profile =
			NewObject<UPaper2DPlusCharacterProfileAsset>(
				Package,
				ProfileName,
				RF_Public | RF_Standalone | RF_Transactional);
		UPaper2DPlusCharacterLayerAsset* Layer =
			NewObject<UPaper2DPlusCharacterLayerAsset>(
				Package,
				LayerName,
				RF_Public | RF_Standalone | RF_Transactional);
		if (!TestNotNull(TEXT("Saved Layer reinstance Cue Type compiles"), Blueprint)
			|| !TestNotNull(TEXT("Saved Layer base Profile is created"), Profile)
			|| !TestNotNull(TEXT("Saved Layer asset is created"), Layer))
		{
			return false;
		}
		Blueprint->SetFlags(RF_Public | RF_Standalone | RF_Transactional);

		FFlipbookProfileEntry& Entry = Profile->Flipbooks.AddDefaulted_GetRef();
		Entry.Identity.FlipbookName = TEXT("slash");
		Entry.CombatData.Frames.SetNum(4);
		Layer->BaseProfile = Profile;
		FCharacterLayer& SourceLayer = Layer->Layers.AddDefaulted_GetRef();
		SourceLayer.LayerId = FGuid::NewGuid();
		SourceLayer.LayerName = TEXT("Sword Blade");
		FCharacterLayerAuthoredAnimationData& Authored =
			SourceLayer.AuthoredAnimations.AddDefaulted_GetRef();
		Authored.LegacyAnimationName = TEXT("slash");

		// The order sentinel has to survive a real package save/reload and must NOT be reinstanced
		// when the placement's Cue Type recompiles, so it needs a concrete Cue class distinct from
		// that type. A second saved Cue Type Blueprint is used rather than an editor-module test
		// class: serializing an editor-only class into a /Game fixture package is exactly the kind
		// of asset a cook would then have to drop.
		UPaper2DPlusFrameCueBlueprint* SentinelType = MakeCompiledCueBlueprint(
			UPaper2DPlusCue::StaticClass(),
			*SentinelBlueprintName.ToString(),
			Package);
		if (!TestNotNull(TEXT("Saved Layer order-sentinel Cue Type compiles"), SentinelType))
		{
			return false;
		}
		SentinelType->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
		UPaper2DPlusCue* Sentinel = Cast<UPaper2DPlusCue>(CreatePlacement(
			Layer, SentinelType->GeneratedClass, 0, 4));
		if (!TestNotNull(TEXT("Saved Layer order sentinel is created"), Sentinel))
		{
			return false;
		}
		Sentinel->DebugName = TEXT("LayerOrderSentinel");
		Authored.FrameCues.Add(Sentinel);
		UPaper2DPlusCueBase* Placement = CreatePlacement(
			Layer, Blueprint->GeneratedClass, 2, 4);
		FIntProperty* Power = Placement
			? FindFProperty<FIntProperty>(Placement->GetClass(), TEXT("Power"))
			: nullptr;
		if (!TestNotNull(TEXT("Saved Layer generated placement is created"), Placement)
			|| !TestNotNull(TEXT("Saved Layer placement exposes Power"), Power))
		{
			return false;
		}
		Power->SetPropertyValue_InContainer(Placement, 23);
		Authored.FrameCues.Add(Placement);
		Package->MarkPackageDirty();
		FPaper2DPlusFrameCueDurableSaveAttempt CueSaveAttempt;
		FPaper2DPlusFrameCueDurableSaveAttempt SentinelSaveAttempt;
		FText CuePrepareError;
		FText SentinelPrepareError;
		const bool bCuePrepared =
			FPaper2DPlusFrameCueTypeEditor::PrepareDurableSchemaSaveAttempt(
				*Blueprint,
				CueSaveAttempt,
				&CuePrepareError);
		const bool bSentinelPrepared =
			FPaper2DPlusFrameCueTypeEditor::PrepareDurableSchemaSaveAttempt(
				*SentinelType,
				SentinelSaveAttempt,
				&SentinelPrepareError);
		if (!TestTrue(TEXT("Layer Cue Type stages its exact durable schema before save"),
				bCuePrepared)
			|| !TestTrue(TEXT("Layer order-sentinel Cue Type stages its exact durable schema before save"),
				bSentinelPrepared))
		{
			if (!CuePrepareError.IsEmpty())
			{
				AddError(CuePrepareError.ToString());
			}
			if (!SentinelPrepareError.IsEmpty())
			{
				AddError(SentinelPrepareError.ToString());
			}
			return false;
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Package, Layer, *Fixture.FilePath, SaveArgs);
		FText CueFinalizeError;
		FText SentinelFinalizeError;
		const bool bCueFinalized =
			FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(
				*Blueprint,
				CueSaveAttempt,
				&CueFinalizeError);
		const bool bSentinelFinalized =
			FPaper2DPlusFrameCueTypeEditor::FinalizeDurableSchemaSaveAttempt(
				*SentinelType,
				SentinelSaveAttempt,
				&SentinelFinalizeError);
		if (!TestTrue(TEXT("Layer Cue Type and placement save before reinstance proof"),
			bSaved && bCueFinalized && bSentinelFinalized))
		{
			if (!CueFinalizeError.IsEmpty())
			{
				AddError(CueFinalizeError.ToString());
			}
			if (!SentinelFinalizeError.IsEmpty())
			{
				AddError(SentinelFinalizeError.ToString());
			}
			return false;
		}
	}

	FText InitialUnloadError;
	if (!TestTrue(TEXT("Saved Layer reinstance package fully unloads"),
		Fixture.Unload(InitialUnloadError)))
	{
		AddError(FString::Printf(
			TEXT("Layer reinstance fixture unload failed: %s"),
			*InitialUnloadError.ToString()));
		return false;
	}

	UPackage* LoadedPackage = LoadPackage(nullptr, *Fixture.PackageName, LOAD_None);
	UPaper2DPlusFrameCueBlueprint* LoadedBlueprint = LoadedPackage
		? FindObject<UPaper2DPlusFrameCueBlueprint>(LoadedPackage, *BlueprintName.ToString())
		: nullptr;
	UPaper2DPlusCharacterProfileAsset* LoadedProfile = LoadedPackage
		? FindObject<UPaper2DPlusCharacterProfileAsset>(LoadedPackage, *ProfileName.ToString())
		: nullptr;
	UPaper2DPlusCharacterLayerAsset* LoadedLayer = LoadedPackage
		? FindObject<UPaper2DPlusCharacterLayerAsset>(LoadedPackage, *LayerName.ToString())
		: nullptr;
	if (!TestNotNull(TEXT("Layer reinstance package reloads"), LoadedPackage)
		|| !TestNotNull(TEXT("Reloaded Layer uses the specialized Cue Type envelope"), LoadedBlueprint)
		|| !TestNotNull(TEXT("Reloaded Layer base Profile is linker-backed"), LoadedProfile)
		|| !TestNotNull(TEXT("Reloaded Layer asset is linker-backed"), LoadedLayer))
	{
		return false;
	}

	{
		FScopedRoot BlueprintRoot(LoadedBlueprint);
		FScopedRoot ProfileRoot(LoadedProfile);
		FScopedRoot LayerRoot(LoadedLayer);
		FScopedTransactionHistory TransactionHistory(NSLOCTEXT(
			"Paper2DPlusCueTypeFeasibility",
			"LoadedLayerReinstanceHistory",
			"Loaded Layer Cue Type Reinstance Test"));
		if (!TestTrue(TEXT("Reloaded Layer fixture retains its Profile and source layer"),
			LoadedProfile->Flipbooks.Num() == 1 && LoadedLayer->Layers.Num() == 1))
		{
			return false;
		}

		TSharedPtr<FCharacterProfileEditorModel> Model =
			MakeShared<FCharacterProfileEditorModel>();
		Model->InitializeFromAsset(LoadedProfile);
		Model->SetSecondaryWatchedObject(LoadedLayer);
		Model->SetSelectedFlipbook(0);
		Model->SetSelectedFrame(2);
		Model->SetSelectedLayerById(LoadedLayer->Layers[0].LayerId);
		FLayerFrameCueDataProvider Provider(LoadedLayer, Model);
		const FProfileScopedAnimationIdentity Scope = Provider.GetScopedAnimationIdentity(0);
		if (!TestTrue(TEXT("Reloaded Layer scope resolves the selected animation"), Scope.IsValid()))
		{
			return false;
		}

		const TArray<TObjectPtr<UPaper2DPlusCueBase>>* LoadedCues = Provider.GetCues(Scope);
		if (!TestTrue(TEXT("Reloaded Layer retains its ordered authored Cue placements"),
			LoadedCues && LoadedCues->Num() == 2
			&& LoadedLayer->Layers.Num() == 1
			&& LoadedLayer->Layers[0].AuthoredAnimations.Num() == 1))
		{
			return false;
		}
		UPaper2DPlusFrameCueBlueprint* LoadedSentinelType =
			FindObject<UPaper2DPlusFrameCueBlueprint>(
				LoadedPackage, *SentinelBlueprintName.ToString());
		UPaper2DPlusCue* Sentinel = Cast<UPaper2DPlusCue>((*LoadedCues)[0]);
		UPaper2DPlusCueBase* LoadedPlacement = (*LoadedCues)[1];
		FIntProperty* LoadedPower = LoadedPlacement
			? FindFProperty<FIntProperty>(LoadedPlacement->GetClass(), TEXT("Power"))
			: nullptr;
		if (!TestNotNull(TEXT("Reloaded Layer keeps its order sentinel"), Sentinel)
			|| !TestNotNull(TEXT("Reloaded Layer placement is valid"), LoadedPlacement)
			|| !TestNotNull(TEXT("Reloaded Layer placement exposes Power"), LoadedPower))
		{
			return false;
		}
		TestEqual(TEXT("Reloaded Layer sentinel identity survives"),
			Sentinel->DebugName, FName(TEXT("LayerOrderSentinel")));
		TestTrue(TEXT("Reloaded Layer sentinel keeps its own distinct Cue Type"),
			LoadedSentinelType != nullptr
			&& Sentinel->GetClass() == LoadedSentinelType->GeneratedClass
			&& Sentinel->GetClass() != LoadedBlueprint->GeneratedClass);
		TestEqual(TEXT("Reloaded Layer authored row keeps the stable animation name"),
			LoadedLayer->Layers[0].AuthoredAnimations[0].LegacyAnimationName,
			FString(TEXT("slash")));
		TestEqual(TEXT("Reloaded Layer placement leaves Profile Cue storage empty"),
			LoadedProfile->Flipbooks[0].FrameEventData.FrameCues.Num(), 0);
		TestTrue(TEXT("Reloaded Layer placement uses the current generated class and outer"),
			LoadedPlacement->GetClass() == LoadedBlueprint->GeneratedClass
			&& LoadedPlacement->GetOuter() == LoadedLayer);
		TestTrue(TEXT("Reloaded Layer placement retains its persisted anchor and payload"),
			LoadedPlacement->GetPrimaryAnchorFrame() == 2
			&& LoadedPower->GetPropertyValue_InContainer(LoadedPlacement) == 23);

		{
			FScopedTransaction Transaction(NSLOCTEXT(
				"Paper2DPlusCueTypeFeasibility",
				"EditLoadedLayerCue",
				"Edit Loaded Layer Cue"));
			LoadedLayer->Modify();
			LoadedPlacement->Modify();
			LoadedPlacement->SetPrimaryAnchorFrame(3);
			LoadedPower->SetPropertyValue_InContainer(LoadedPlacement, 37);
		}
		LoadedCues = Provider.GetCues(Scope);
		TestTrue(TEXT("Loaded Layer placement edit applies before reinstance"),
			LoadedCues && LoadedCues->Num() == 2 && (*LoadedCues)[0] == Sentinel
			&& (*LoadedCues)[1]->GetPrimaryAnchorFrame() == 3
			&& LoadedPower->GetPropertyValue_InContainer((*LoadedCues)[1]) == 37);

		TestTrue(TEXT("Loaded Layer placement edit undoes before reinstance"),
			GEditor->UndoTransaction(true));
		LoadedCues = Provider.GetCues(Scope);
		TestTrue(TEXT("Pre-reinstance Layer undo targets the loaded placement"),
			LoadedCues && LoadedCues->Num() == 2 && (*LoadedCues)[0] == Sentinel
			&& (*LoadedCues)[1]->GetPrimaryAnchorFrame() == 2
			&& LoadedPower->GetPropertyValue_InContainer((*LoadedCues)[1]) == 23);
		TestTrue(TEXT("Loaded Layer placement edit redoes before reinstance"),
			GEditor->RedoTransaction());
		LoadedCues = Provider.GetCues(Scope);
		if (!TestTrue(TEXT("Pre-reinstance Layer redo restores loaded values and order"),
			LoadedCues && LoadedCues->Num() == 2 && (*LoadedCues)[0] == Sentinel
			&& (*LoadedCues)[1]->GetPrimaryAnchorFrame() == 3
			&& LoadedPower->GetPropertyValue_InContainer((*LoadedCues)[1]) == 37))
		{
			return false;
		}

		UPaper2DPlusCueBase* PlacementBeforeCompile = (*LoadedCues)[1];
		FScopedReplacementCapture ReplacementCapture(PlacementBeforeCompile);
		FBPVariableDescription* PowerDescription = FindVariable(*LoadedBlueprint, TEXT("Power"));
		if (!TestNotNull(TEXT("Loaded Layer Cue Type exposes the durable Power definition"),
			PowerDescription))
		{
			return false;
		}
		PowerDescription->DefaultValue = TEXT("19");
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(LoadedBlueprint);
		if (!TestTrue(TEXT("Compatible Radius field is added to the loaded Layer Cue Type"),
			AddEditableFloatPayload(*LoadedBlueprint, TEXT("Radius"), TEXT("1.5"))))
		{
			return false;
		}
		FKismetEditorUtilities::CompileBlueprint(LoadedBlueprint);
		if (!TestTrue(TEXT("Loaded Layer Cue Type recompiles after compatible schema growth"),
			LoadedBlueprint->Status != BS_Error && LoadedBlueprint->GeneratedClass != nullptr))
		{
			return false;
		}

		FIntProperty* ClassPower = FindFProperty<FIntProperty>(
			LoadedBlueprint->GeneratedClass, TEXT("Power"));
		FFloatProperty* ClassRadius = FindFProperty<FFloatProperty>(
			LoadedBlueprint->GeneratedClass, TEXT("Radius"));
		if (TestNotNull(TEXT("Recompiled Layer Cue Type retains Power"), ClassPower))
		{
			TestEqual(TEXT("Recompiled Layer Cue Type applies the edited Power default"),
				ClassPower->GetPropertyValue_InContainer(
					LoadedBlueprint->GeneratedClass->GetDefaultObject()), 19);
		}
		if (TestNotNull(TEXT("Recompiled Layer Cue Type adds Radius"), ClassRadius))
		{
			TestEqual(TEXT("Recompiled Layer Cue Type applies the Radius default"),
				ClassRadius->GetPropertyValue_InContainer(
					LoadedBlueprint->GeneratedClass->GetDefaultObject()), 1.5f);
		}

		const TArray<TObjectPtr<UPaper2DPlusCueBase>>* ReinstancedCues =
			Provider.GetCues(Scope);
		if (!TestTrue(TEXT("Loaded Layer keeps exactly two ordered Cues after reinstance"),
			ReinstancedCues && ReinstancedCues->Num() == 2
			&& (*ReinstancedCues)[0] == Sentinel
			&& LoadedLayer->Layers[0].AuthoredAnimations.Num() == 1))
		{
			return false;
		}
		UPaper2DPlusCueBase* ReinstancedPlacement = (*ReinstancedCues)[1];
		FIntProperty* ReinstancedPower = ReinstancedPlacement
			? FindFProperty<FIntProperty>(ReinstancedPlacement->GetClass(), TEXT("Power"))
			: nullptr;
		FFloatProperty* ReinstancedRadius = ReinstancedPlacement
			? FindFProperty<FFloatProperty>(ReinstancedPlacement->GetClass(), TEXT("Radius"))
			: nullptr;
		if (!TestNotNull(TEXT("Loaded Layer placement is valid after reinstance"), ReinstancedPlacement)
			|| !TestNotNull(TEXT("Loaded Layer placement retains Power after reinstance"), ReinstancedPower)
			|| !TestNotNull(TEXT("Loaded Layer placement receives Radius after reinstance"), ReinstancedRadius))
		{
			return false;
		}
		TestTrue(TEXT("Loaded Layer placement uses exactly the current generated class"),
			ReinstancedPlacement->GetClass() == LoadedBlueprint->GeneratedClass
			&& ReinstancedPlacement->GetClass()->ClassGeneratedBy == LoadedBlueprint
			&& !ReinstancedPlacement->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists));
		TestTrue(TEXT("Loaded Layer placement preserves outer, anchor, and payload"),
			ReinstancedPlacement->GetOuter() == LoadedLayer
			&& ReinstancedPlacement->GetPrimaryAnchorFrame() == 3
			&& ReinstancedPower->GetPropertyValue_InContainer(ReinstancedPlacement) == 37
			&& FMath::IsNearlyEqual(
				ReinstancedRadius->GetPropertyValue_InContainer(ReinstancedPlacement), 1.5f));
		TestEqual(TEXT("Layer reinstance still leaves Profile Cue storage empty"),
			LoadedProfile->Flipbooks[0].FrameEventData.FrameCues.Num(), 0);
		TestTrue(TEXT("Loaded Layer placement emitted an object-replacement mapping"),
			ReplacementCapture.HitCount > 0 && ReplacementCapture.ReplacementKey.IsSet());
		if (ReplacementCapture.ReplacementKey.IsSet())
		{
			TestTrue(TEXT("Loaded Layer replacement maps to the live placement"),
				ReplacementCapture.ReplacementKey.GetValue() == FObjectKey(ReinstancedPlacement));
		}

		TestTrue(TEXT("Loaded Layer edit transaction undoes after reinstance"),
			GEditor->UndoTransaction(true));
		const TArray<TObjectPtr<UPaper2DPlusCueBase>>* UndoneCues = Provider.GetCues(Scope);
		UPaper2DPlusCueBase* UndonePlacement =
			UndoneCues && UndoneCues->Num() == 2 ? (*UndoneCues)[1].Get() : nullptr;
		FIntProperty* UndonePower = UndonePlacement
			? FindFProperty<FIntProperty>(UndonePlacement->GetClass(), TEXT("Power"))
			: nullptr;
		FFloatProperty* UndoneRadius = UndonePlacement
			? FindFProperty<FFloatProperty>(UndonePlacement->GetClass(), TEXT("Radius"))
			: nullptr;
		TestTrue(TEXT("Post-reinstance Layer undo targets one current loaded placement"),
			UndonePlacement && UndonePower && UndoneRadius
			&& (*UndoneCues)[0] == Sentinel
			&& UndonePlacement->GetClass() == LoadedBlueprint->GeneratedClass
			&& UndonePlacement->GetOuter() == LoadedLayer
			&& UndonePlacement->GetPrimaryAnchorFrame() == 2
			&& UndonePower->GetPropertyValue_InContainer(UndonePlacement) == 23
			&& FMath::IsNearlyEqual(
				UndoneRadius->GetPropertyValue_InContainer(UndonePlacement), 1.5f));

		TestTrue(TEXT("Loaded Layer edit transaction redoes after reinstance"),
			GEditor->RedoTransaction());
		const TArray<TObjectPtr<UPaper2DPlusCueBase>>* RedoneCues = Provider.GetCues(Scope);
		UPaper2DPlusCueBase* RedonePlacement =
			RedoneCues && RedoneCues->Num() == 2 ? (*RedoneCues)[1].Get() : nullptr;
		FIntProperty* RedonePower = RedonePlacement
			? FindFProperty<FIntProperty>(RedonePlacement->GetClass(), TEXT("Power"))
			: nullptr;
		FFloatProperty* RedoneRadius = RedonePlacement
			? FindFProperty<FFloatProperty>(RedonePlacement->GetClass(), TEXT("Radius"))
			: nullptr;
		TestTrue(TEXT("Post-reinstance Layer redo preserves current object, order, and payload"),
			RedonePlacement && RedonePower && RedoneRadius
			&& (*RedoneCues)[0] == Sentinel
			&& RedonePlacement->GetClass() == LoadedBlueprint->GeneratedClass
			&& RedonePlacement->GetOuter() == LoadedLayer
			&& RedonePlacement->GetPrimaryAnchorFrame() == 3
			&& RedonePower->GetPropertyValue_InContainer(RedonePlacement) == 37
			&& FMath::IsNearlyEqual(
				RedoneRadius->GetPropertyValue_InContainer(RedonePlacement), 1.5f));
		TestEqual(TEXT("Redone loaded Layer placement still leaves Profile storage empty"),
			LoadedProfile->Flipbooks[0].FrameEventData.FrameCues.Num(), 0);
	}

	LoadedPackage->SetDirtyFlag(false);
	LoadedLayer = nullptr;
	LoadedProfile = nullptr;
	LoadedBlueprint = nullptr;
	LoadedPackage = nullptr;
	FText FinalUnloadError;
	if (!TestTrue(TEXT("Reloaded Layer reinstance package fully unloads after proof"),
		Fixture.Unload(FinalUnloadError)))
	{
		AddError(FString::Printf(
			TEXT("Reloaded Layer reinstance fixture unload failed: %s"),
			*FinalUnloadError.ToString()));
		return false;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeEditorInspectorFeasibilityTest,
	"Paper2DPlus.FrameCues.CueType.Feasibility.RestrictedEditorInspector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeEditorInspectorFeasibilityTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeFeasibilityTest;
	if (!TestNotNull(TEXT("GEditor is available for restricted editor transactions"), GEditor))
	{
		return false;
	}

	UPaper2DPlusFrameCueBlueprint* Blueprint = MakeCueBlueprint(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_FeasibilityInspector"));
	if (!TestNotNull(TEXT("Inspector fixture Blueprint"), Blueprint))
	{
		return false;
	}
	FPaper2DPlusFrameCueTypeEditor::PrepareNewlyCreatedBlueprint(*Blueprint);

	FEdGraphPinType FloatType;
	FloatType.PinCategory = UEdGraphSchema_K2::PC_Real;
	FloatType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	if (!TestTrue(TEXT("Inspector fixture variable is added"),
		FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Strength"), FloatType, TEXT("1.5"))))
	{
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (!TestNotNull(TEXT("Inspector fixture variable is reflected after compile"),
		FindFProperty<FProperty>(Blueprint->GeneratedClass, TEXT("Strength"))))
	{
		return false;
	}
	FScopedRoot BlueprintRoot(Blueprint);
	FScopedTransactionHistory TransactionHistory(NSLOCTEXT(
		"Paper2DPlusCueTypeFeasibility",
		"RestrictedEditorHistory",
		"Restricted Cue Type Editor Test"));

	const TSharedRef<FPaper2DPlusFrameCueTypeEditor> Editor =
		MakeShared<FPaper2DPlusFrameCueTypeEditor>();
	Editor->InitFrameCueTypeEditor(EToolkitMode::Standalone, nullptr, Blueprint);
	FScopedCueTypeEditorClose EditorClose(Editor);
	TestEqual(TEXT("Cue Type editor enters only its restricted application mode"),
		Editor->GetCurrentMode(), FPaper2DPlusFrameCueTypeEditor::CueTypeModeName);
	const TSharedPtr<SMyBlueprint> StockMyBlueprint = Editor->GetMyBlueprintWidget();
	TestTrue(TEXT("Modern Cue Type hosts stock My Blueprint"),
		Editor->IsUsingStockMyBlueprintForTests()
		&& StockMyBlueprint.IsValid());
	TestTrue(TEXT("Modern stock My Blueprint is visible and enabled"),
		StockMyBlueprint.IsValid()
		&& StockMyBlueprint->IsEnabled()
		&& StockMyBlueprint->GetVisibility() == EVisibility::Visible);
	TestTrue(TEXT("Stock My Blueprint permits new payload variables"),
		Editor->IsNewDocumentVisibleForTests(
			FBlueprintEditor::CGT_NewVariable));
	TestFalse(TEXT("Stock My Blueprint cannot create function graphs"),
		Editor->IsNewDocumentVisibleForTests(
			FBlueprintEditor::CGT_NewFunctionGraph));
	TestTrue(TEXT("Stock My Blueprint exposes the variable section"),
		Editor->IsMyBlueprintSectionVisibleForTests(NodeSectionID::VARIABLE));
	TestFalse(TEXT("Stock My Blueprint hides the event-graph section"),
		Editor->IsMyBlueprintSectionVisibleForTests(NodeSectionID::GRAPH));
	TestFalse(TEXT("Stock My Blueprint hides the unsafe Functions section"),
		Editor->IsMyBlueprintSectionVisibleForTests(NodeSectionID::FUNCTION));

	UEdGraph* LifecycleForbiddenGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		TEXT("LifecycleForbiddenFunction"),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph(
		Blueprint,
		LifecycleForbiddenGraph,
		false,
		static_cast<UFunction*>(nullptr));
	Editor->ReevaluateMyBlueprintExposureForTests();
	TestTrue(TEXT("A live forbidden graph transition quarantines stock My Blueprint"),
		!Editor->IsUsingStockMyBlueprintForTests()
		&& !StockMyBlueprint->IsEnabled()
		&& StockMyBlueprint->GetVisibility() == EVisibility::Collapsed);
	FBlueprintEditorUtils::RemoveGraph(
		Blueprint,
		LifecycleForbiddenGraph,
		EGraphRemoveFlags::MarkTransient);
	Editor->ReevaluateMyBlueprintExposureForTests();
	TestTrue(TEXT("Removing live forbidden content restores stock My Blueprint"),
		Editor->IsUsingStockMyBlueprintForTests()
		&& StockMyBlueprint->IsEnabled()
		&& StockMyBlueprint->GetVisibility() == EVisibility::Visible);

	const TSharedRef<FUICommandList> ToolkitCommands = Editor->GetToolkitCommands();
	const auto TestBlockedCommands =
		[this, &ToolkitCommands](FName ContextName, const TArray<FName>& CommandNames)
	{
		for (const FName CommandName : CommandNames)
		{
			const TSharedPtr<FUICommandInfo> Command =
				FInputBindingManager::Get().FindCommandInContext(ContextName, CommandName);
			const FString CommandLabel = FString::Printf(
				TEXT("%s.%s"), *ContextName.ToString(), *CommandName.ToString());
			if (!TestTrue(
				*FString::Printf(TEXT("Restricted editor resolves blocked command %s"), *CommandLabel),
				Command.IsValid()))
			{
				continue;
			}

			const TSharedRef<FUICommandInfo> CommandRef = Command.ToSharedRef();
			TestFalse(
				*FString::Printf(TEXT("Restricted editor cannot execute %s"), *CommandLabel),
				ToolkitCommands->CanExecuteAction(CommandRef));
			TestTrue(
				*FString::Printf(TEXT("Restricted editor hides %s"), *CommandLabel),
				ToolkitCommands->GetVisibility(CommandRef) != EVisibility::Visible);
			TestFalse(
				*FString::Printf(TEXT("Restricted editor rejects direct execution of %s"), *CommandLabel),
				ToolkitCommands->TryExecuteAction(CommandRef));
		}
	};
	const TArray<FName> BlockedBlueprintCommands =
		FPaper2DPlusFrameCueTypeEditor::GetBlockedBlueprintCommandNamesForTests();
	const TArray<FName> BlockedFullBlueprintCommands =
		FPaper2DPlusFrameCueTypeEditor::GetBlockedFullBlueprintCommandNamesForTests();
	TestTrue(TEXT("Safe scripting mode cannot rewrite quarantined legacy graphs"),
		BlockedBlueprintCommands.Contains(TEXT("RefreshAllNodes")));
	TestTrue(TEXT("Safe scripting mode cannot delete graph-unreferenced payload fields"),
		BlockedBlueprintCommands.Contains(TEXT("DeleteUnusedVariables")));
	TestTrue(TEXT("Cue Type toolbar cannot enable save-on-compile"),
		BlockedFullBlueprintCommands.Contains(TEXT("SaveOnCompile_Never"))
		&& BlockedFullBlueprintCommands.Contains(TEXT("SaveOnCompile_SuccessOnly"))
		&& BlockedFullBlueprintCommands.Contains(TEXT("SaveOnCompile_Always")));
	TestBlockedCommands(TEXT("BlueprintEditor"), BlockedBlueprintCommands);
	TestBlockedCommands(TEXT("FullBlueprintEditor"), BlockedFullBlueprintCommands);
	TestBlockedCommands(
		TEXT("MyBlueprint"),
		FPaper2DPlusFrameCueTypeEditor::GetBlockedMyBlueprintCommandNamesForTests());
	TestBlockedCommands(
		TEXT("GenericCommands"),
		FPaper2DPlusFrameCueTypeEditor::GetBlockedGenericCommandNamesForTests());

	TestTrue(TEXT("Native Details summoner assigns the Inspector owner tab"),
		Editor->GetInspector()->GetOwnerTab().IsValid());
	TestTrue(TEXT("Native Details summoner assigns the property view host manager"),
		Editor->GetInspector()->GetPropertyView()->GetHostTabManager().IsValid());

	const TSharedPtr<SDockTab> MyBlueprintTab =
		Editor->GetTabManager()->TryInvokeTab(FBlueprintEditorTabs::MyBlueprintID);
	TestTrue(TEXT("Restricted editor spawns its stock My Blueprint tab"),
		MyBlueprintTab.IsValid());
	const TArray<FName> ListedEvents =
		FPaper2DPlusFrameCueTypeEditor::GetCueOverrideEventNames(*Blueprint);
	TestTrue(TEXT("Plugin Override control is filtered to the Cue's declared event"),
		ListedEvents.Num() == 1
		&& ListedEvents[0] == FName(TEXT("OnCueTriggered")));
	TestTrue(TEXT("Restricted editor opens the behavior graph on a declared event"),
		Editor->ShowCueBehaviorEvent(TEXT("OnCueTriggered")));
	UEdGraph* const PermittedBehaviorGraph =
		FPaper2DPlusFrameCueTypeEditor::FindCueBehaviorGraph(*Blueprint);
	TestNotNull(TEXT("Restricted editor keeps one permitted behavior event graph"),
		PermittedBehaviorGraph);
	if (PermittedBehaviorGraph)
	{
		TArray<TSharedPtr<SDockTab>> OpenBehaviorTabs;
		TestTrue(TEXT("Behavior graph document tab spawns for the permitted event graph"),
			Editor->FindOpenTabsContainingDocument(
				PermittedBehaviorGraph, OpenBehaviorTabs));
		TestTrue(TEXT("Restricted editor accepts only the permitted behavior document"),
			Editor->IsPermittedBehaviorDocument(PermittedBehaviorGraph));
	}

	TestTrue(TEXT("Stock My Blueprint forwards selection to Unreal's variable inspector"),
		Editor->ShowPayloadVariableDetails(TEXT("Strength")));
	const TSharedPtr<SDockTab> DetailsTab =
		Editor->GetTabManager()->FindExistingLiveTab(FBlueprintEditorTabs::DetailsID);
	TestTrue(TEXT("Selecting a payload field foregrounds Variable Details"),
		DetailsTab.IsValid() && DetailsTab->IsForeground());
	Editor->GetInspector()->Tick(FGeometry(), 0.0, 0.0f);
	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects =
		Editor->GetInspector()->GetSelectedObjects();
	TestEqual(TEXT("Stock Kismet inspector owns exactly one selected variable wrapper"),
		SelectedObjects.Num(), 1);
	if (SelectedObjects.Num() == 1)
	{
		const UPropertyWrapper* SelectedPropertyWrapper =
			Cast<UPropertyWrapper>(SelectedObjects[0].Get());
		TestNotNull(TEXT("Inspector selection is a reflected property wrapper"),
			SelectedPropertyWrapper);
		if (SelectedPropertyWrapper)
		{
			const FProperty* SelectedProperty = SelectedPropertyWrapper->GetProperty();
			TestNotNull(TEXT("Selected wrapper owns a reflected property"), SelectedProperty);
			if (SelectedProperty)
			{
				TestEqual(TEXT("Inspector selected the Strength payload variable"),
					SelectedProperty->GetFName(), FName(TEXT("Strength")));
				TestTrue(TEXT("Inspector selected the float payload property"),
					SelectedProperty->IsA<FFloatProperty>());
			}
		}
	}
	const FBPVariableDescription* StrengthBeforeRename =
		FindVariable(*Blueprint, TEXT("Strength"));
	const FGuid StrengthGuid = StrengthBeforeRename
		? StrengthBeforeRename->VarGuid
		: FGuid();
	FBlueprintEditorUtils::RenameMemberVariable(Blueprint, TEXT("Strength"), TEXT("Impact"));
	Editor->RefreshMyBlueprint();
	const FBPVariableDescription* ImpactAfterRename =
		FindVariable(*Blueprint, TEXT("Impact"));
	TestTrue(TEXT("Stock variable rename preserves the field's stable GUID"),
		StrengthGuid.IsValid()
		&& ImpactAfterRename
		&& ImpactAfterRename->VarGuid == StrengthGuid
		&& !FindVariable(*Blueprint, TEXT("Strength")));

	TestTrue(TEXT("Stock variable flow adds a payload field"),
		FBlueprintEditorUtils::AddMemberVariable(
			Blueprint,
			TEXT("NewPayload"),
			FloatType,
			TEXT("2.5")));
	const FName DuplicatedPayload = FBlueprintEditorUtils::DuplicateMemberVariable(
		Blueprint,
		Blueprint,
		TEXT("Impact"));
	TestTrue(TEXT("Stock variable copy/duplicate path creates a new payload field"),
		!DuplicatedPayload.IsNone()
		&& FindVariable(*Blueprint, DuplicatedPayload));
	{
		UBlueprintEditorSettings* BlueprintEditorSettings =
			GetMutableDefault<UBlueprintEditorSettings>();
		TGuardValue<TEnumAsByte<ESaveOnCompile>> SaveOnCompileTestSetting(
			BlueprintEditorSettings->SaveOnCompile,
			TEnumAsByte<ESaveOnCompile>(SoC_Always));
		bool bObservedProtectedCompile = false;
		bool bCompileStayedSaveFree = true;
		const FDelegateHandle CompileObservation = Blueprint->OnCompiled().AddLambda(
			[&bObservedProtectedCompile,
				&bCompileStayedSaveFree,
				BlueprintEditorSettings](UBlueprint*)
			{
				bObservedProtectedCompile = true;
				bCompileStayedSaveFree &=
					BlueprintEditorSettings->SaveOnCompile == SoC_Never;
			});
		const bool bCompiledPayloadSchema = Editor->CompileDataOnlyBlueprint();
		Blueprint->OnCompiled().Remove(CompileObservation);
		TestTrue(TEXT("Restricted editor compiles the stock-authored payload schema"),
			bCompiledPayloadSchema);
		TestTrue(TEXT("Cue compilation broadcasts while automatic saving is suppressed"),
			bObservedProtectedCompile && bCompileStayedSaveFree);
		TestTrue(TEXT("Cue compilation restores the designer's global save-on-compile setting"),
			BlueprintEditorSettings->SaveOnCompile == SoC_Always);
	}
	TestNotNull(TEXT("Compiled Cue Type contains the added payload field"),
		FindFProperty<FFloatProperty>(Blueprint->GeneratedClass, TEXT("NewPayload")));
	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, TEXT("NewPayload"));
	TestNull(TEXT("Stock variable delete removes the payload field"),
		FindVariable(*Blueprint, TEXT("NewPayload")));
	if (!DuplicatedPayload.IsNone())
	{
		FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, DuplicatedPayload);
	}

	UTimelineTemplate* RejectedTimeline =
		InjectUnsupportedTimeline(*Blueprint, TEXT("RejectedTimeline"));
	UK2Node_Timeline* RejectedTimelineNode = nullptr;
	if (TestNotNull(TEXT("Unsupported timeline rollback fixture is injected"),
		RejectedTimeline)
		&& TestNotNull(TEXT("Timeline fixture uses the permitted event graph"),
			PermittedBehaviorGraph))
	{
		RejectedTimelineNode =
			NewObject<UK2Node_Timeline>(PermittedBehaviorGraph);
		RejectedTimelineNode->TimelineName =
			RejectedTimeline->GetVariableName();
		RejectedTimelineNode->CreateNewGuid();
		RejectedTimelineNode->SetFlags(RF_Transactional);
		RejectedTimelineNode->AllocateDefaultPins();
		PermittedBehaviorGraph->AddNode(RejectedTimelineNode);
	}
	TestTrue(TEXT("Cue Type authority rolls back a newly introduced timeline"),
		Editor->EnforceDataOnlyAuthoringInvariantForTests());
	TestFalse(TEXT("Newly introduced timeline template is removed"),
		Blueprint->Timelines.Contains(RejectedTimeline));
	TestFalse(TEXT("Newly introduced timeline node is removed with its template"),
		PermittedBehaviorGraph
		&& PermittedBehaviorGraph->Nodes.Contains(RejectedTimelineNode));

	UEdGraph* RejectedRepNotifyGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		TEXT("OnRep_Impact"),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph(
		Blueprint,
		RejectedRepNotifyGraph,
		false,
		static_cast<UFunction*>(nullptr));
	const FSoftObjectPath RejectedRepNotifyGraphPath(RejectedRepNotifyGraph);
	FBPVariableDescription* ImpactWithRepNotify = FindVariable(*Blueprint, TEXT("Impact"));
	if (TestNotNull(TEXT("RepNotify rollback fixture retains the selected payload field"),
		ImpactWithRepNotify))
	{
		ImpactWithRepNotify->PropertyFlags |=
			CPF_Net
			| CPF_RepNotify
			| CPF_Transient
			| CPF_Config
			| CPF_EditorOnly
			| CPF_SaveGame;
		ImpactWithRepNotify->RepNotifyFunc = TEXT("OnRep_Impact");
		ImpactWithRepNotify->ReplicationCondition = COND_OwnerOnly;
	}
	Blueprint->LastEditedDocuments.Add(FEditedDocumentInfo(RejectedRepNotifyGraph));
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	TestFalse(TEXT("Restricted editor cannot open a newly introduced behavior graph"),
		Editor->OpenDocument(
			RejectedRepNotifyGraph,
			FDocumentTracker::OpenNewDocument).IsValid());
	TestTrue(TEXT("Cue Type authority rejects the synthetic RepNotify mutation"),
		Editor->EnforceDataOnlyAuthoringInvariantForTests());
	TestFalse(TEXT("Rejected RepNotify function graph is removed"),
		Blueprint->FunctionGraphs.Contains(RejectedRepNotifyGraph));
	TestFalse(TEXT("Rejected RepNotify graph document metadata is removed"),
		Blueprint->LastEditedDocuments.ContainsByPredicate(
			[RejectedRepNotifyGraphPath](const FEditedDocumentInfo& Document)
			{
				return Document.EditedObjectPath == RejectedRepNotifyGraphPath;
			}));
	const FBPVariableDescription* ImpactAfterRepNotifyRollback =
		FindVariable(*Blueprint, TEXT("Impact"));
	TestTrue(TEXT("Forbidden-field rollback preserves payload data and clears behavior metadata"),
		ImpactAfterRepNotifyRollback
		&& (ImpactAfterRepNotifyRollback->PropertyFlags
			& (CPF_Net
				| CPF_RepNotify
				| CPF_Transient
				| CPF_Config
				| CPF_EditorOnly)) == 0
		&& (ImpactAfterRepNotifyRollback->PropertyFlags & CPF_SaveGame) != 0
		&& ImpactAfterRepNotifyRollback->RepNotifyFunc.IsNone()
		&& ImpactAfterRepNotifyRollback->ReplicationCondition == COND_None);
	TestTrue(TEXT("Forbidden-field rollback restores a compilable Cue Type"),
		Editor->CompileDataOnlyBlueprint());
	const FProperty* CompiledImpactAfterRollback = FindFProperty<FProperty>(
		Blueprint->GeneratedClass, TEXT("Impact"));
	TestTrue(TEXT("Allowed SaveGame flag survives rollback and reaches the generated field"),
		CompiledImpactAfterRollback
		&& CompiledImpactAfterRollback->HasAnyPropertyFlags(CPF_SaveGame)
		&& !CompiledImpactAfterRollback->HasAnyPropertyFlags(
			CPF_Transient | CPF_Config | CPF_EditorOnly | CPF_Net | CPF_RepNotify));

	EditorClose.Close();

	UPaper2DPlusFrameCueBlueprint* LegacyBlueprint = MakeCueBlueprint(
		UPaper2DPlusCue::StaticClass(), TEXT("BP_P2DP_LegacyCueTypeStartup"));
	if (!TestNotNull(TEXT("Legacy startup fixture Blueprint"), LegacyBlueprint))
	{
		return false;
	}
	FPaper2DPlusFrameCueTypeEditor::PrepareNewlyCreatedBlueprint(*LegacyBlueprint);
	FGuid LegacyPowerGuid;
	if (!TestTrue(TEXT("Legacy rejection fixture adds a durable payload before behavior injection"),
		AddEditableIntPayload(
			*LegacyBlueprint,
			TEXT("Power"),
			TEXT("12"),
			&LegacyPowerGuid)))
	{
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(LegacyBlueprint);
	FIntProperty* CleanLegacyPowerProperty = LegacyBlueprint->GeneratedClass
		? FindFProperty<FIntProperty>(LegacyBlueprint->GeneratedClass, TEXT("Power"))
		: nullptr;
	if (!TestTrue(TEXT("Legacy rejection fixture starts from a clean custom generated class"),
		LegacyBlueprint->Status != BS_Error
		&& LegacyBlueprint->GeneratedClass
		&& LegacyBlueprint->GeneratedClass->GetClass()->IsChildOf(
			UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass()))
		|| !TestNotNull(TEXT("Clean legacy rejection fixture reflects Power"),
			CleanLegacyPowerProperty))
	{
		return false;
	}
	TestEqual(TEXT("Clean legacy rejection fixture starts with the authored Power default"),
		CleanLegacyPowerProperty->GetPropertyValue_InContainer(
			LegacyBlueprint->GeneratedClass->GetDefaultObject()),
		12);
	UEdGraph* SavedLegacyGraph = FBlueprintEditorUtils::CreateNewGraph(
		LegacyBlueprint,
		TEXT("SavedLegacyFunction"),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph(
		LegacyBlueprint, SavedLegacyGraph, false, static_cast<UFunction*>(nullptr));
	UEdGraphNode_Comment* SavedErrorNode =
		NewObject<UEdGraphNode_Comment>(SavedLegacyGraph);
	SavedErrorNode->CreateNewGuid();
	SavedErrorNode->bHasCompilerMessage = true;
	SavedErrorNode->ErrorType = EMessageSeverity::Error;
	SavedErrorNode->ErrorMsg = TEXT("Preserved legacy compile error");
	SavedLegacyGraph->AddNode(SavedErrorNode, false, false);
	UTimelineTemplate* SavedLegacyTimeline =
		InjectUnsupportedTimeline(*LegacyBlueprint, TEXT("SavedLegacyTimeline"));
	UEdGraph* LegacyBehaviorGraph =
		FPaper2DPlusFrameCueTypeEditor::FindCueBehaviorGraph(*LegacyBlueprint);
	UK2Node_Timeline* SavedLegacyTimelineNode = nullptr;
	if (SavedLegacyTimeline && LegacyBehaviorGraph)
	{
		SavedLegacyTimelineNode =
			NewObject<UK2Node_Timeline>(LegacyBehaviorGraph);
		SavedLegacyTimelineNode->TimelineName =
			SavedLegacyTimeline->GetVariableName();
		SavedLegacyTimelineNode->CreateNewGuid();
		SavedLegacyTimelineNode->SetFlags(RF_Transactional);
		SavedLegacyTimelineNode->AllocateDefaultPins();
		LegacyBehaviorGraph->AddNode(SavedLegacyTimelineNode);
	}
	LegacyBlueprint->LastEditedDocuments.Add(FEditedDocumentInfo(SavedLegacyGraph));
	LegacyBlueprint->bIsNewlyCreated = false;
	LegacyBlueprint->Status = BS_Error;
	FScopedRoot LegacyBlueprintRoot(LegacyBlueprint);
	FScopedJumpToNodeErrors JumpToNodeErrors;
	TestTrue(TEXT("Legacy startup fixture enables Unreal's jump-to-error behavior"),
		JumpToNodeErrors.Settings && JumpToNodeErrors.Settings->bJumpToNodeErrors);

	const TSharedRef<FPaper2DPlusFrameCueTypeEditor> LegacyEditor =
		MakeShared<FPaper2DPlusFrameCueTypeEditor>();
	LegacyEditor->InitFrameCueTypeEditor(
		EToolkitMode::Standalone, nullptr, LegacyBlueprint);
	FScopedCueTypeEditorClose LegacyEditorClose(LegacyEditor);
	const TSharedPtr<SMyBlueprint> QuarantinedMyBlueprint =
		LegacyEditor->GetMyBlueprintWidget();
	TestTrue(TEXT("Quarantined Cue Type uses the local safe body"),
		!LegacyEditor->IsUsingStockMyBlueprintForTests()
		&& QuarantinedMyBlueprint.IsValid()
		&& !QuarantinedMyBlueprint->IsEnabled()
		&& QuarantinedMyBlueprint->GetVisibility() == EVisibility::Collapsed);
	TestTrue(TEXT("Quarantine preserves a pre-existing timeline template"),
		SavedLegacyTimeline
		&& LegacyBlueprint->Timelines.Contains(SavedLegacyTimeline));
	TestTrue(TEXT("Quarantine preserves the pre-existing timeline node"),
		LegacyBehaviorGraph
		&& LegacyBehaviorGraph->Nodes.Contains(SavedLegacyTimelineNode));
	TArray<TSharedPtr<SDockTab>> OpenLegacyGraphTabs;
	TestFalse(TEXT("Opening an errored legacy Cue Type does not restore its saved graph tab"),
		LegacyEditor->FindOpenTabsContainingDocument(
			SavedLegacyGraph, OpenLegacyGraphTabs));
	TestEqual(TEXT("No hidden legacy graph tab is left open"),
		OpenLegacyGraphTabs.Num(), 0);
	TestFalse(TEXT("Legacy graph navigation remains blocked after startup"),
		LegacyEditor->OpenDocument(
			SavedLegacyGraph, FDocumentTracker::OpenNewDocument).IsValid());
	TestTrue(TEXT("Restricted startup preserves legacy authored graph data"),
		LegacyBlueprint->FunctionGraphs.Contains(SavedLegacyGraph));
	TestTrue(TEXT("Restricted startup preserves legacy document recovery metadata"),
		LegacyBlueprint->LastEditedDocuments.ContainsByPredicate(
			[SavedLegacyGraph](const FEditedDocumentInfo& Document)
			{
				return Document.EditedObjectPath == FSoftObjectPath(SavedLegacyGraph);
			}));
	FText LegacyContractError;
	TestFalse(TEXT("Legacy behavior graph fails the restricted Cue Type contract"),
		FPaper2DPlusFrameCueTypeEditor::ValidateDataOnlyBlueprint(
			*LegacyBlueprint,
			&LegacyContractError));
	TestFalse(TEXT("Legacy behavior graph has an actionable contract diagnostic"),
		LegacyContractError.IsEmpty());
	TestFalse(TEXT("Legacy behavior graph disables every editor compile path"),
		LegacyEditor->IsCompilingEnabled());
	TestFalse(TEXT("Legacy behavior graph disables Save"),
		LegacyEditor->CanSaveAsset());
	TestFalse(TEXT("Legacy behavior graph disables Save As"),
		LegacyEditor->CanSaveAssetAs());

	// The persistent Blueprint extension is the fail-closed backstop for callers that bypass the
	// restricted editor entirely. It must report a compiler error without deleting recovery data.
	const int32 LegacyNodeCountBeforeProgrammaticCompile =
		SavedLegacyGraph->Nodes.Num();
	AddExpectedError(
		TEXT("Frame Cue Type compilation refused"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	FKismetEditorUtilities::CompileBlueprint(LegacyBlueprint);
	TestEqual(TEXT("Direct programmatic compilation fails the restricted Cue Type contract"),
		LegacyBlueprint->Status,
		BS_Error);
	TestTrue(TEXT("Programmatic compile failure preserves the exact legacy graph"),
		LegacyBlueprint->FunctionGraphs.Contains(SavedLegacyGraph));
	TestEqual(TEXT("Programmatic compile failure preserves legacy graph nodes"),
		SavedLegacyGraph->Nodes.Num(),
		LegacyNodeCountBeforeProgrammaticCompile);
	TestTrue(TEXT("Rejected direct compile retains the custom generated-class envelope"),
		LegacyBlueprint->GeneratedClass
		&& LegacyBlueprint->GeneratedClass->GetClass()->IsChildOf(
			UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass())
		&& LegacyBlueprint->GeneratedClass->ClassGeneratedBy == LegacyBlueprint);
	const UObject* RejectedCompileDefaults = LegacyBlueprint->GeneratedClass
		? LegacyBlueprint->GeneratedClass->GetDefaultObject(false)
		: nullptr;
	FIntProperty* RejectedCompilePower = LegacyBlueprint->GeneratedClass
		? FindFProperty<FIntProperty>(LegacyBlueprint->GeneratedClass, TEXT("Power"))
		: nullptr;
	TestTrue(TEXT("Rejected direct compile retains a valid current-class CDO"),
		RejectedCompileDefaults
		&& RejectedCompileDefaults->GetClass() == LegacyBlueprint->GeneratedClass);
	TestTrue(TEXT("Rejected direct compile retains the reflected field and authored default"),
		RejectedCompilePower
		&& RejectedCompileDefaults
		&& RejectedCompilePower->GetPropertyValue_InContainer(RejectedCompileDefaults) == 12);
	const FBPVariableDescription* RejectedCompilePowerSource =
		FindVariable(*LegacyBlueprint, TEXT("Power"));
	TestTrue(TEXT("Rejected direct compile retains the exact authored Power source identity"),
		RejectedCompilePowerSource
		&& RejectedCompilePowerSource->VarGuid == LegacyPowerGuid
		&& RejectedCompilePowerSource->VarType.PinCategory == UEdGraphSchema_K2::PC_Int);

	const TSharedRef<FUICommandList> LegacyCommands =
		LegacyEditor->GetToolkitCommands();
	const TArray<TPair<FName, FName>> GuardedCommands = {
		{ TEXT("FullBlueprintEditor"), TEXT("Compile") },
		{ TEXT("BlueprintEditor"), TEXT("CompileBlueprint") },
		{ TEXT("AssetEditor"), TEXT("SaveAsset") },
		{ TEXT("AssetEditor"), TEXT("SaveAssetAs") }
	};
	for (const TPair<FName, FName>& CommandIdentity : GuardedCommands)
	{
		const TSharedPtr<FUICommandInfo> Command =
			FInputBindingManager::Get().FindCommandInContext(
				CommandIdentity.Key,
				CommandIdentity.Value);
		const FString Label = FString::Printf(
			TEXT("%s.%s"),
			*CommandIdentity.Key.ToString(),
			*CommandIdentity.Value.ToString());
		if (!TestTrue(
			*FString::Printf(TEXT("Restricted editor resolves guarded command %s"), *Label),
			Command.IsValid()))
		{
			continue;
		}
		TestFalse(
			*FString::Printf(TEXT("Legacy graph disables command %s"), *Label),
			LegacyCommands->CanExecuteAction(Command.ToSharedRef()));
		TestFalse(
			*FString::Printf(TEXT("Legacy graph rejects direct command %s"), *Label),
			LegacyCommands->TryExecuteAction(Command.ToSharedRef()));
	}

	const int32 LegacyGraphCountBeforeRejectedActions =
		LegacyBlueprint->FunctionGraphs.Num();
	const int32 LegacyNodeCountBeforeRejectedActions = SavedLegacyGraph->Nodes.Num();
	TestFalse(TEXT("Direct Cue editor compile rejects preserved legacy behavior"),
		LegacyEditor->CompileDataOnlyBlueprint());
	LegacyEditor->SaveAsset_Execute();
	LegacyEditor->SaveAssetAs_Execute();
	TestEqual(TEXT("Rejected compile/save paths preserve legacy graph inventory"),
		LegacyBlueprint->FunctionGraphs.Num(),
		LegacyGraphCountBeforeRejectedActions);
	TestEqual(TEXT("Rejected compile/save paths preserve every legacy graph node"),
		SavedLegacyGraph->Nodes.Num(),
		LegacyNodeCountBeforeRejectedActions);
	TestTrue(TEXT("Rejected compile/save paths preserve the exact legacy graph"),
		LegacyBlueprint->FunctionGraphs.Contains(SavedLegacyGraph));
	TestTrue(TEXT("Rejected compile/save paths preserve legacy recovery metadata"),
		LegacyBlueprint->LastEditedDocuments.ContainsByPredicate(
			[SavedLegacyGraph](const FEditedDocumentInfo& Document)
			{
				return Document.EditedObjectPath == FSoftObjectPath(SavedLegacyGraph);
			}));

	const FSoftObjectPath RecoveredLegacyGraphPath(SavedLegacyGraph);
	FBlueprintEditorUtils::RemoveGraph(
		LegacyBlueprint,
		SavedLegacyGraph,
		EGraphRemoveFlags::MarkTransient);
	LegacyBlueprint->LastEditedDocuments.RemoveAll(
		[RecoveredLegacyGraphPath](const FEditedDocumentInfo& Document)
		{
			return Document.EditedObjectPath == RecoveredLegacyGraphPath;
		});
	if (SavedLegacyTimelineNode)
	{
		FBlueprintEditorUtils::RemoveNode(
			LegacyBlueprint,
			SavedLegacyTimelineNode,
			/*bDontRecompile=*/ true);
	}
	else if (SavedLegacyTimeline)
	{
		FBlueprintEditorUtils::RemoveTimeline(
			LegacyBlueprint,
			SavedLegacyTimeline,
			/*bDontRecompile=*/ true);
		SavedLegacyTimeline->Rename(
			nullptr,
			GetTransientPackage(),
			REN_None);
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(LegacyBlueprint);
	TestTrue(TEXT("Removing the injected behavior graph allows a clean recovery compile"),
		LegacyEditor->CompileDataOnlyBlueprint());
	const FBPVariableDescription* RecoveredPowerSource =
		FindVariable(*LegacyBlueprint, TEXT("Power"));
	FIntProperty* RecoveredPowerProperty = LegacyBlueprint->GeneratedClass
		? FindFProperty<FIntProperty>(LegacyBlueprint->GeneratedClass, TEXT("Power"))
		: nullptr;
	const UObject* RecoveredDefaults = LegacyBlueprint->GeneratedClass
		? LegacyBlueprint->GeneratedClass->GetDefaultObject(false)
		: nullptr;
	TestTrue(TEXT("Recovery compile restores a clean specialized generated class and CDO"),
		LegacyBlueprint->Status != BS_Error
		&& LegacyBlueprint->GeneratedClass
		&& LegacyBlueprint->GeneratedClass->GetClass()->IsChildOf(
			UPaper2DPlusFrameCueBlueprintGeneratedClass::StaticClass())
		&& LegacyBlueprint->GeneratedClass->ClassGeneratedBy == LegacyBlueprint
		&& RecoveredDefaults
		&& RecoveredDefaults->GetClass() == LegacyBlueprint->GeneratedClass);
	TestTrue(TEXT("Recovery compile preserves source identity and the authored Power default"),
		RecoveredPowerSource
		&& RecoveredPowerSource->VarGuid == LegacyPowerGuid
		&& RecoveredPowerSource->VarType.PinCategory == UEdGraphSchema_K2::PC_Int
		&& RecoveredPowerProperty
		&& RecoveredDefaults
		&& RecoveredPowerProperty->GetPropertyValue_InContainer(RecoveredDefaults) == 12);
	FText RecoveredContractError;
	TestTrue(TEXT("Recovered Cue Type satisfies the full compiled Cue Type contract"),
		UPaper2DPlusFrameCueBlueprint::ValidateCompiledDataOnlyContract(
			*LegacyBlueprint,
			&RecoveredContractError));

	LegacyEditorClose.Close();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueTypeDeferredCompileReadinessTest,
	"Paper2DPlus.FrameCues.CueType.Feasibility.DeferredBehaviorFailsCompileAndReadiness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueTypeDeferredCompileReadinessTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusFrameCueTypeFeasibilityTest;

	UPaper2DPlusFrameCueBlueprint* Blueprint = MakeCompiledCueBlueprint(
		UPaper2DPlusCue::StaticClass(),
		TEXT("BP_P2DP_DeferredCompileReadiness"));
	if (!TestNotNull(TEXT("Deferred compile/readiness fixture"), Blueprint))
	{
		return false;
	}
	const FString OriginalFingerprint = Blueprint->DurableSchemaFingerprint;
	UK2Node_CallFunction* DelayNode = InjectDeferredCall(
		*Blueprint,
		GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Delay));
	if (!TestNotNull(TEXT("Programmatically injected Delay node"), DelayNode))
	{
		return false;
	}

	FText PolicyError;
	TestFalse(TEXT("The shared synchronous policy rejects the source graph"),
		FPaper2DPlusFrameCueTypeAuthoring::ValidateSynchronousBehavior(
			*Blueprint,
			&PolicyError));
	TestTrue(TEXT("Policy diagnosis names the Cue Type"),
		PolicyError.ToString().Contains(Blueprint->GetPathName()));
	TestTrue(TEXT("Policy diagnosis names the behavior event"),
		PolicyError.ToString().Contains(TEXT("OnCueTriggered")));
	TestTrue(TEXT("Policy diagnosis names the offending latent call"),
		PolicyError.ToString().Contains(TEXT("Delay"))
		&& PolicyError.ToString().Contains(TEXT("latent")));

	const FPaper2DPlusFrameCueTypeDescriptor Descriptor =
		FPaper2DPlusFrameCueTypeAuthoring::DescribeCueType(Blueprint->GeneratedClass);
	const FPaper2DPlusFrameCueAuthoringDiagnostic* DeferredDiagnostic =
		Descriptor.Diagnostics.FindByPredicate(
			[](const FPaper2DPlusFrameCueAuthoringDiagnostic& Diagnostic)
			{
				return Diagnostic.Code
					== EPaper2DPlusFrameCueDiagnosticCode::DeferredBehavior;
			});
	TestTrue(TEXT("Placement readiness rejects deferred behavior"),
		Descriptor.Availability == EPaper2DPlusFrameCueTypeAvailability::Invalid);
	TestNotNull(TEXT("Readiness uses the deferred-behavior diagnostic code"),
		DeferredDiagnostic);
	if (DeferredDiagnostic)
	{
		TestEqual(TEXT("Readiness and direct policy return the same diagnosis"),
			DeferredDiagnostic->Message.ToString(),
			PolicyError.ToString());
	}

	const FPaper2DPlusFrameCueSchemaPreflight Preflight =
		FPaper2DPlusFrameCueTypeAuthoring::BuildSchemaPreflight(*Blueprint);
	TestFalse(TEXT("Authored schema preflight cannot compile deferred behavior"),
		Preflight.bCanCompile);
	TestFalse(TEXT("Authored schema preflight cannot persist deferred behavior"),
		Preflight.bCanPersist);
	TestTrue(TEXT("Authored schema preflight retains the same diagnosis"),
		Preflight.CurrentSchema.Diagnostics.ContainsByPredicate(
			[&PolicyError](const FPaper2DPlusFrameCueAuthoringDiagnostic& Diagnostic)
			{
				return Diagnostic.Code
						== EPaper2DPlusFrameCueDiagnosticCode::DeferredBehavior
					&& Diagnostic.Message.ToString() == PolicyError.ToString();
			}));

	AddExpectedError(
		TEXT("Frame Cue Type compilation refused: Frame Cue Type"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	TestEqual(TEXT("Direct/programmatic compile fails closed"), Blueprint->Status, BS_Error);
	UEdGraph* BehaviorGraph =
		FPaper2DPlusFrameCueTypeAuthoring::FindBehaviorEventGraph(*Blueprint);
	TestTrue(TEXT("Rejected compilation does not strip or rewrite the offending node"),
		BehaviorGraph && BehaviorGraph->Nodes.Contains(DelayNode));
	TestEqual(TEXT("Rejected validation does not advance the durable fingerprint"),
		Blueprint->DurableSchemaFingerprint,
		OriginalFingerprint);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
