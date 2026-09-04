// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "BulkFolderOrganizationUtils.h"

#include "GameplayTagsManager.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace Paper2DPlus::BulkFolderOrganization
{
	namespace
	{
		constexpr TCHAR AnimationTagPrefix[] = TEXT("Paper2DPlus.Animation.");

		void SortFolderNames(TArray<FString>& Names)
		{
			Names.Sort([](const FString& A, const FString& B)
			{
				return A.Compare(B, ESearchCase::IgnoreCase) < 0;
			});
		}

		/** Metadata keys carry annotations, never folders. "_items" is the one exception and is
		 *  consumed by the caller before this is asked. */
		bool BulkOrgJson_IsMetadataKey(const FString& Key)
		{
			return Key.StartsWith(TEXT("_"), ESearchCase::CaseSensitive);
		}

		void BulkOrgJson_ReadStringArray(
			const TSharedPtr<FJsonObject>& Object,
			const FString& Field,
			TArray<FString>& OutStrings)
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Object.IsValid() || !Object->TryGetArrayField(Field, Values) || Values == nullptr)
			{
				return;
			}
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				FString Entry;
				if (Value.IsValid() && Value->TryGetString(Entry) && !Entry.IsEmpty())
				{
					OutStrings.AddUnique(Entry);
				}
			}
		}

		TSharedRef<FJsonObject> BulkOrgJson_WriteFolder(const FBulkFolderRecord& Folder)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("name"), Folder.Name);

			TArray<TSharedPtr<FJsonValue>> Items;
			for (const FString& Item : Folder.Items)
			{
				Items.Add(MakeShared<FJsonValueString>(Item));
			}
			Object->SetArrayField(TEXT("items"), Items);

			TArray<TSharedPtr<FJsonValue>> Children;
			for (const TSharedPtr<FBulkFolderRecord>& Child : Folder.Children)
			{
				if (Child.IsValid())
				{
					Children.Add(MakeShared<FJsonValueObject>(BulkOrgJson_WriteFolder(*Child)));
				}
			}
			Object->SetArrayField(TEXT("children"), Children);
			return Object;
		}

		/** The exported ordered-array shape. */
		void BulkOrgJson_ReadFolderArray(
			const TArray<TSharedPtr<FJsonValue>>& Values,
			TArray<TSharedPtr<FBulkFolderRecord>>& OutFolders)
		{
			for (const TSharedPtr<FJsonValue>& Value : Values)
			{
				const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
				if (!Value.IsValid() || !Value->TryGetObject(ObjectPtr) || ObjectPtr == nullptr)
				{
					continue;
				}
				const TSharedPtr<FJsonObject>& Object = *ObjectPtr;

				TSharedRef<FBulkFolderRecord> Record = MakeShared<FBulkFolderRecord>();
				Object->TryGetStringField(TEXT("name"), Record->Name);
				if (Record->Name.IsEmpty())
				{
					continue;
				}
				BulkOrgJson_ReadStringArray(Object, TEXT("items"), Record->Items);

				const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
				if (Object->TryGetArrayField(TEXT("children"), Children) && Children != nullptr)
				{
					BulkOrgJson_ReadFolderArray(*Children, Record->Children);
				}
				OutFolders.Add(Record);
			}
		}

		/** The hand-captured object shape: every non-metadata OBJECT key is a folder, and "_items"
		 *  holds that folder's texture display names. Visited in name order so an object payload
		 *  (whose key order JSON does not define) still imports deterministically. */
		void BulkOrgJson_ReadFolderObject(
			const TSharedPtr<FJsonObject>& Object,
			TArray<TSharedPtr<FBulkFolderRecord>>& OutFolders)
		{
			if (!Object.IsValid())
			{
				return;
			}

			TArray<FString> Keys;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
			{
				if (!BulkOrgJson_IsMetadataKey(Pair.Key))
				{
					Keys.Add(Pair.Key);
				}
			}
			SortFolderNames(Keys);

			for (const FString& Key : Keys)
			{
				// Go through the field accessor rather than indexing Values directly: UE 5.8 rekeys
				// FJsonObject::Values to UE::TSharedString<TCHAR>, so a raw FString Find no longer
				// compiles. TryGetObjectField takes an FStringView on 5.8 and a const FString& on the
				// older engines, so one call site covers the whole supported 5.0-5.8 range — and it
				// already fails closed on a missing, null, or non-object field, which is exactly the
				// skip this loop wants.
				const TSharedPtr<FJsonObject>* ChildObject = nullptr;
				if (!Object->TryGetObjectField(Key, ChildObject) || ChildObject == nullptr)
				{
					continue;
				}

				TSharedRef<FBulkFolderRecord> Record = MakeShared<FBulkFolderRecord>();
				Record->Name = Key;
				BulkOrgJson_ReadStringArray(*ChildObject, TEXT("_items"), Record->Items);
				BulkOrgJson_ReadFolderObject(*ChildObject, Record->Children);
				OutFolders.Add(Record);
			}
		}

		void BulkOrgJson_Flatten(
			const TSharedPtr<FBulkFolderRecord>& Folder,
			const FString& ParentPath,
			TMap<FString, FString>& OutNameToFolderPath,
			TArray<FString>& OutDuplicates)
		{
			if (!Folder.IsValid() || Folder->Name.IsEmpty())
			{
				return;
			}
			const FString Path = ParentPath.IsEmpty() ? Folder->Name : (ParentPath / Folder->Name);
			for (const FString& Item : Folder->Items)
			{
				if (Item.IsEmpty())
				{
					continue;
				}
				if (OutNameToFolderPath.Contains(Item))
				{
					OutDuplicates.AddUnique(Item);
					continue;
				}
				OutNameToFolderPath.Add(Item, Path);
			}
			for (const TSharedPtr<FBulkFolderRecord>& Child : Folder->Children)
			{
				BulkOrgJson_Flatten(Child, Path, OutNameToFolderPath, OutDuplicates);
			}
		}
	}

	TArray<FString> BuildAnimationGroupFolderNames(const TArray<FString>& TagPaths)
	{
		TMap<FString, FString> UniqueNames;
		for (const FString& TagPath : TagPaths)
		{
			if (!TagPath.StartsWith(AnimationTagPrefix, ESearchCase::CaseSensitive))
			{
				continue;
			}

			const FString RelativePath = TagPath.RightChop(UE_ARRAY_COUNT(AnimationTagPrefix) - 1);
			if (RelativePath.IsEmpty() || RelativePath.Contains(TEXT(".")))
			{
				continue;
			}

			UniqueNames.FindOrAdd(RelativePath.ToLower(), RelativePath);
		}

		TArray<FString> Result;
		UniqueNames.GenerateValueArray(Result);
		SortFolderNames(Result);
		return Result;
	}

	TArray<FString> GetLiveAnimationGroupFolderNames()
	{
		TArray<FString> DirectChildTagPaths;
		const FGameplayTag Root = FGameplayTag::RequestGameplayTag(
			FName(TEXT("Paper2DPlus.Animation")), /*ErrorIfNotFound=*/false);
		if (!Root.IsValid())
		{
			return DirectChildTagPaths;
		}

		if (const TSharedPtr<FGameplayTagNode> RootNode = UGameplayTagsManager::Get().FindTagNode(Root))
		{
			for (const TSharedPtr<FGameplayTagNode>& ChildNode : RootNode->GetChildTagNodes())
			{
				if (ChildNode.IsValid() && ChildNode->GetCompleteTag().IsValid())
				{
					DirectChildTagPaths.Add(ChildNode->GetCompleteTag().ToString());
				}
			}
		}

		return BuildAnimationGroupFolderNames(DirectChildTagPaths);
	}

	TArray<FString> FindMissingRootFolderNames(
		const TArray<FString>& CandidateNames,
		const TArray<FString>& ExistingRootNames)
	{
		TSet<FString> OccupiedNames;
		for (const FString& ExistingName : ExistingRootNames)
		{
			OccupiedNames.Add(ExistingName.ToLower());
		}

		TArray<FString> MissingNames;
		for (const FString& CandidateName : CandidateNames)
		{
			const FString Key = CandidateName.ToLower();
			if (!CandidateName.IsEmpty() && !OccupiedNames.Contains(Key))
			{
				OccupiedNames.Add(Key);
				MissingNames.Add(CandidateName);
			}
		}
		return MissingNames;
	}

	FString MakeRowIdentityKey(const FString& TextureAssetPath, const FString& AseStoredPath)
	{
		// The asset path wins when both are somehow present: it is the identity the texture half of
		// the pipeline already round-trips, and changing that would re-key existing payloads.
		if (!TextureAssetPath.IsEmpty())
		{
			return TextureAssetPath;
		}
		return AseStoredPath;
	}

	FString SerializeOrganizationSnapshot(const FBulkFolderOrganizationSnapshot& Snapshot)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("version"), 1);

		TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
		Settings->SetStringField(TEXT("outputPath"), Snapshot.Naming.OutputPath);
		Settings->SetBoolField(TEXT("includePrefixInFolder"), Snapshot.Naming.bIncludePrefixInFolder);
		Settings->SetBoolField(TEXT("includeSuffixInFolder"), Snapshot.Naming.bIncludeSuffixInFolder);
		Settings->SetBoolField(TEXT("includeBatchSuffix"), Snapshot.Naming.bIncludeBatchSuffix);
		Settings->SetStringField(TEXT("folderPrefix"), Snapshot.Naming.FolderPrefix);
		Settings->SetStringField(TEXT("folderSuffix"), Snapshot.Naming.FolderSuffix);
		Settings->SetStringField(TEXT("find"), Snapshot.Naming.Find);
		Settings->SetStringField(TEXT("replace"), Snapshot.Naming.Replace);
		Settings->SetStringField(TEXT("removeText"), Snapshot.Naming.RemoveText);

		TArray<TSharedPtr<FJsonValue>> UnassignedValues;
		for (const FString& Name : Snapshot.Unassigned)
		{
			UnassignedValues.Add(MakeShared<FJsonValueString>(Name));
		}
		Settings->SetArrayField(TEXT("unassigned"), UnassignedValues);
		Root->SetObjectField(TEXT("settings"), Settings);

		TArray<TSharedPtr<FJsonValue>> FolderValues;
		for (const TSharedPtr<FBulkFolderRecord>& Folder : Snapshot.Folders)
		{
			if (Folder.IsValid())
			{
				FolderValues.Add(MakeShared<FJsonValueObject>(BulkOrgJson_WriteFolder(*Folder)));
			}
		}
		Root->SetArrayField(TEXT("folders"), FolderValues);

		TArray<TSharedPtr<FJsonValue>> TextureValues;
		for (const FBulkTextureRecord& Texture : Snapshot.Textures)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("displayName"), Texture.DisplayName);
			Object->SetStringField(TEXT("subfolderPath"), Texture.SubfolderPath);
			Object->SetStringField(TEXT("assetPath"), Texture.AssetPath);
			TextureValues.Add(MakeShared<FJsonValueObject>(Object));
		}
		Root->SetArrayField(TEXT("textures"), TextureValues);

		if (Snapshot.Renames.Num() > 0)
		{
			TSharedRef<FJsonObject> Renames = MakeShared<FJsonObject>();
			for (const TPair<FString, FString>& Pair : Snapshot.Renames)
			{
				Renames->SetStringField(Pair.Key, Pair.Value);
			}
			Root->SetObjectField(TEXT("_renamesSinceEarlierCapture"), Renames);
		}

		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Root, Writer);
		return Output;
	}

	bool DeserializeOrganizationSnapshot(
		const FString& JsonText,
		FBulkFolderOrganizationSnapshot& OutSnapshot,
		FString& OutError)
	{
		OutSnapshot = FBulkFolderOrganizationSnapshot();
		OutError.Reset();

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("The file is not a readable JSON document.");
			return false;
		}

		// Settings. Every field is optional; an absent one keeps its default, and the retired
		// "createSubfolders" flag is deliberately ignored rather than treated as an error.
		const TSharedPtr<FJsonObject>* SettingsPtr = nullptr;
		if (Root->TryGetObjectField(TEXT("settings"), SettingsPtr) && SettingsPtr != nullptr)
		{
			const TSharedPtr<FJsonObject>& Settings = *SettingsPtr;
			// Record PRESENCE, not just values: an absent block must leave the importer's naming
			// configuration alone rather than overwriting it with these struct defaults.
			OutSnapshot.Naming.bPresentInPayload = true;
			Settings->TryGetStringField(TEXT("outputPath"), OutSnapshot.Naming.OutputPath);
			Settings->TryGetBoolField(TEXT("includePrefixInFolder"), OutSnapshot.Naming.bIncludePrefixInFolder);
			Settings->TryGetBoolField(TEXT("includeSuffixInFolder"), OutSnapshot.Naming.bIncludeSuffixInFolder);
			Settings->TryGetBoolField(TEXT("includeBatchSuffix"), OutSnapshot.Naming.bIncludeBatchSuffix);
			Settings->TryGetStringField(TEXT("folderPrefix"), OutSnapshot.Naming.FolderPrefix);
			Settings->TryGetStringField(TEXT("folderSuffix"), OutSnapshot.Naming.FolderSuffix);
			Settings->TryGetStringField(TEXT("find"), OutSnapshot.Naming.Find);
			Settings->TryGetStringField(TEXT("replace"), OutSnapshot.Naming.Replace);
			Settings->TryGetStringField(TEXT("removeText"), OutSnapshot.Naming.RemoveText);
			BulkOrgJson_ReadStringArray(Settings, TEXT("unassigned"), OutSnapshot.Unassigned);
		}
		BulkOrgJson_ReadStringArray(Root, TEXT("unassigned"), OutSnapshot.Unassigned);

		// Folders: the exported ordered array wins; otherwise accept the captured object tree.
		const TArray<TSharedPtr<FJsonValue>>* FolderArray = nullptr;
		if (Root->TryGetArrayField(TEXT("folders"), FolderArray) && FolderArray != nullptr)
		{
			BulkOrgJson_ReadFolderArray(*FolderArray, OutSnapshot.Folders);
		}
		else
		{
			const TSharedPtr<FJsonObject>* TreePtr = nullptr;
			if (Root->TryGetObjectField(TEXT("tree"), TreePtr) && TreePtr != nullptr)
			{
				BulkOrgJson_ReadFolderObject(*TreePtr, OutSnapshot.Folders);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* TextureArray = nullptr;
		if (Root->TryGetArrayField(TEXT("textures"), TextureArray) && TextureArray != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& Value : *TextureArray)
			{
				const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
				if (!Value.IsValid() || !Value->TryGetObject(ObjectPtr) || ObjectPtr == nullptr)
				{
					continue;
				}
				FBulkTextureRecord Record;
				(*ObjectPtr)->TryGetStringField(TEXT("displayName"), Record.DisplayName);
				(*ObjectPtr)->TryGetStringField(TEXT("subfolderPath"), Record.SubfolderPath);
				(*ObjectPtr)->TryGetStringField(TEXT("assetPath"), Record.AssetPath);
				if (!Record.DisplayName.IsEmpty() || !Record.AssetPath.IsEmpty())
				{
					OutSnapshot.Textures.Add(MoveTemp(Record));
				}
			}
		}

		const TSharedPtr<FJsonObject>* RenamesPtr = nullptr;
		if (Root->TryGetObjectField(TEXT("_renamesSinceEarlierCapture"), RenamesPtr) && RenamesPtr != nullptr)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*RenamesPtr)->Values)
			{
				FString NewName;
				if (BulkOrgJson_IsMetadataKey(Pair.Key)
					|| !Pair.Value.IsValid()
					|| !Pair.Value->TryGetString(NewName)
					|| NewName.IsEmpty())
				{
					continue;
				}
				OutSnapshot.Renames.Add(Pair.Key, NewName);
			}
		}

		if (OutSnapshot.Folders.Num() == 0 && OutSnapshot.Textures.Num() == 0)
		{
			OutError = TEXT("The file carries no folder tree and no texture rows.");
			return false;
		}
		return true;
	}

	void FlattenSnapshotAssignments(
		const FBulkFolderOrganizationSnapshot& Snapshot,
		TMap<FString, FString>& OutNameToFolderPath,
		TArray<FString>& OutDuplicates)
	{
		OutNameToFolderPath.Reset();
		OutDuplicates.Reset();
		for (const TSharedPtr<FBulkFolderRecord>& Folder : Snapshot.Folders)
		{
			BulkOrgJson_Flatten(Folder, FString(), OutNameToFolderPath, OutDuplicates);
		}
	}
}
