// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterLayerBakeCoordinator.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "CharacterLayerBakeCore.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "TextureCompiler.h"
#include "FileHelpers.h"
#include "FrameCues/Paper2DPlusFrameCue.h"
#include "FrameEvents/Paper2DPlusFrameEventBase.h"
#include "Containers/StringConv.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusAppearanceResolver.h"
#include "Paper2DPlusLayerDraw.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "SpriteEditorOnlyTypes.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#else
#include <sys/stat.h>
#endif

bool CharacterLayerBakeCoordinator::IsPathLinkOrReparsePoint(const TCHAR* Path)
{
	if (!Path || Path[0] == TEXT('\0'))
	{
		return false;
	}

#if PLATFORM_WINDOWS
	const DWORD Attributes = ::GetFileAttributesW(Path);
	return Attributes != INVALID_FILE_ATTRIBUTES
		&& (Attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
	const FTCHARToUTF8 Utf8Path(Path);
	struct stat PathStat;
	return ::lstat(Utf8Path.Get(), &PathStat) == 0 && S_ISLNK(PathStat.st_mode);
#endif
}

namespace Paper2DPlusCharacterLayerBakeCoordinatorPrivate
{
	static const FName OwnerKey(TEXT("Paper2DPlus.LayerBake.Owner"));
	static const FName RevisionKey(TEXT("Paper2DPlus.LayerBake.Revision"));
	static const FName RoleKey(TEXT("Paper2DPlus.LayerBake.Role"));
	static const FName TargetKey(TEXT("Paper2DPlus.LayerBake.Target"));

	void FinishTextureCompilation(UTexture2D* Texture)
	{
		if (Texture)
		{
			FTextureCompilingManager::Get().FinishCompilation({ Texture });
		}
	}

	bool HasFutureBakeManifest(const UPaper2DPlusCharacterLayerAsset* Asset)
	{
		return Asset
			&& (Asset->BakeManifest.ManifestVersion > Paper2DPlusLayerBakeVersion::CurrentManifestVersion
				|| Asset->BakeManifest.DigestVersion > CharacterLayerBakeCore::CurrentDigestVersion);
	}

	FString FutureBakeManifestReport(const UPaper2DPlusCharacterLayerAsset& Asset)
	{
		if (Asset.BakeManifest.ManifestVersion > Paper2DPlusLayerBakeVersion::CurrentManifestVersion)
		{
			return FString::Printf(
				TEXT("This bake manifest uses schema version %u, newer than this Paper2DPlus build supports (%u). The bake set is read-only here; open it with a compatible newer build. No data was changed."),
				Asset.BakeManifest.ManifestVersion,
				Paper2DPlusLayerBakeVersion::CurrentManifestVersion);
		}
		return FString::Printf(
			TEXT("This bake manifest uses digest version %u, newer than this Paper2DPlus build supports (%u). The bake set is read-only here; open it with a compatible newer build. No data was changed."),
			Asset.BakeManifest.DigestVersion,
			CharacterLayerBakeCore::CurrentDigestVersion);
	}

	class FDigestBuilder
	{
	public:
		void AddBool(bool bValue) { Bytes.Add(bValue ? 1 : 0); }
		void AddUInt32(uint32 Value)
		{
			Bytes.Add(static_cast<uint8>(Value));
			Bytes.Add(static_cast<uint8>(Value >> 8));
			Bytes.Add(static_cast<uint8>(Value >> 16));
			Bytes.Add(static_cast<uint8>(Value >> 24));
		}
		void AddInt32(int32 Value) { AddUInt32(static_cast<uint32>(Value)); }
		void AddFloat(float Value)
		{
			uint32 Bits = 0;
			FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
			AddUInt32(Bits);
		}
		void AddDouble(double Value)
		{
			uint64 Bits = 0;
			FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
			AddUInt32(static_cast<uint32>(Bits));
			AddUInt32(static_cast<uint32>(Bits >> 32));
		}
		void AddVector2D(const FVector2D& Value) { AddDouble(Value.X); AddDouble(Value.Y); }
		void AddString(const FString& Value)
		{
			FTCHARToUTF8 Utf8(*Value);
			AddUInt32(static_cast<uint32>(Utf8.Length()));
			if (Utf8.Length() > 0)
			{
				Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			}
		}
		void AddBytes(const uint8* Data, int64 Count)
		{
			AddUInt32(static_cast<uint32>(FMath::Clamp<int64>(Count, 0, MAX_uint32)));
			if (Data && Count > 0 && Count <= MAX_int32)
			{
				Bytes.Append(Data, static_cast<int32>(Count));
			}
		}
		void AddHitbox(const FHitboxData& Box)
		{
			AddUInt32(static_cast<uint32>(Box.Type));
			AddInt32(Box.X); AddInt32(Box.Y); AddInt32(Box.Width); AddInt32(Box.Height);
			// Damage/Knockback float since TASK-146 (digest changes once; pre-existing bakes re-flag).
			AddInt32(Box.Z); AddInt32(Box.Depth); AddFloat(Box.Damage); AddFloat(Box.Knockback);
			AddString(Box.ClashCategory.ToString());
		}
		void AddSocket(const FSocketData& Socket)
		{
			AddString(Socket.Name); AddInt32(Socket.X); AddInt32(Socket.Y);
		}
		void AddFrame(const FFrameHitboxData& Frame)
		{
			AddString(Frame.FrameName); AddBool(Frame.bInvulnerable); AddString(Frame.DefenseClass.ToString());
			AddUInt32(static_cast<uint32>(Frame.Hitboxes.Num()));
			for (const FHitboxData& Box : Frame.Hitboxes) AddHitbox(Box);
			AddUInt32(static_cast<uint32>(Frame.Sockets.Num()));
			for (const FSocketData& Socket : Frame.Sockets) AddSocket(Socket);
		}
		FString Finalize() const
		{
			FMD5 Md5;
			if (!Bytes.IsEmpty()) Md5.Update(Bytes.GetData(), Bytes.Num());
			uint8 Digest[16];
			Md5.Final(Digest);
			return BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
		}

	private:
		TArray<uint8> Bytes;
	};

	FString HashBytes(const TArray<uint8>& Bytes)
	{
		FDigestBuilder Digest;
		Digest.AddString(TEXT("Paper2DPlus.LayerBake.StagePayload"));
		Digest.AddBytes(Bytes.GetData(), Bytes.Num());
		return Digest.Finalize();
	}

	FString SerializeObjectSemantic(const UObject* Object)
	{
		if (!Object) return TEXT("null");
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, true);
		FObjectAndNameAsStringProxyArchive Archive(Writer, false);
		Archive.ArNoDelta = true;
		Archive.ArIgnoreOuterRef = true;
		const_cast<UObject*>(Object)->Serialize(Archive);
		FDigestBuilder Digest;
		Digest.AddString(Object->GetClass()->GetPathName());
		Digest.AddBytes(Bytes.GetData(), Bytes.Num());
		return Digest.Finalize();
	}

	FString GuidText(const FGuid& Guid)
	{
		return Guid.ToString(EGuidFormats::DigitsWithHyphens).ToLower();
	}

	FString SanitizeToken(const FString& Value)
	{
		FString Result;
		bool bSeparator = false;
		for (TCHAR C : Value)
		{
			if (FChar::IsAlnum(C))
			{
				Result.AppendChar(C);
				bSeparator = false;
			}
			else if (!Result.IsEmpty() && !bSeparator)
			{
				Result.AppendChar(TEXT('_'));
				bSeparator = true;
			}
		}
		while (Result.EndsWith(TEXT("_"))) Result.LeftChopInline(1);
		if (Result.IsEmpty()) Result = TEXT("Item");
		return Result.Left(48);
	}

	FString ShortStableHash(const FString& Value)
	{
		FTCHARToUTF8 Utf8(*Value);
		return FMD5::HashBytes(
			reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()).Left(8).ToLower();
	}

	struct FManagedObjectPath
	{
		FString PackageName;
		FString ObjectName;
		FString ObjectPath;

		FSoftObjectPath ToSoftPath() const { return FSoftObjectPath(ObjectPath); }
	};

	FManagedObjectPath MakeObjectPath(const FString& PackageRoot, const FString& ObjectName)
	{
		FManagedObjectPath Result;
		Result.PackageName = PackageRoot / ObjectName;
		Result.ObjectName = ObjectName;
		Result.ObjectPath = Result.PackageName + TEXT(".") + ObjectName;
		return Result;
	}

	bool ParseObjectPath(const FSoftObjectPath& Path, FManagedObjectPath& Out)
	{
		if (Path.IsNull()) return false;
		const FString Text = Path.ToString();
		Out.PackageName = FPackageName::ObjectPathToPackageName(Text);
		Out.ObjectName = FPackageName::ObjectPathToObjectName(Text);
		Out.ObjectPath = Text;
		return !Out.PackageName.IsEmpty() && !Out.ObjectName.IsEmpty();
	}

	FString GeneratedRoot(const UPaper2DPlusCharacterLayerAsset& LayerAsset)
	{
		const FString AssetPackage = LayerAsset.GetOutermost()->GetName();
		const FString Parent = FPackageName::GetLongPackagePath(AssetPackage);
		return Parent / TEXT("_Generated") / SanitizeToken(LayerAsset.GetName());
	}

	FManagedObjectPath DeterministicTexturePath(
		const UPaper2DPlusCharacterLayerAsset& LayerAsset,
		const FCharacterLayerBakeAnimationPlan& Plan)
	{
		const FString Identity = Plan.TargetFlipbookPath.ToString().ToLower();
		const FString Name = FString::Printf(TEXT("T_%s_%s_%s"),
			*SanitizeToken(LayerAsset.GetName()), *SanitizeToken(Plan.AnimationName), *ShortStableHash(Identity));
		return MakeObjectPath(GeneratedRoot(LayerAsset), Name);
	}

	FManagedObjectPath DeterministicSpritePath(
		const UPaper2DPlusCharacterLayerAsset& LayerAsset,
		const FCharacterLayerBakeAnimationPlan& Plan,
		int32 FrameIndex)
	{
		const FString Identity = Plan.TargetFlipbookPath.ToString().ToLower();
		const FString Name = FString::Printf(TEXT("S_%s_%s_%s_F%04d"),
			*SanitizeToken(LayerAsset.GetName()), *SanitizeToken(Plan.AnimationName),
			*ShortStableHash(Identity), FrameIndex);
		return MakeObjectPath(GeneratedRoot(LayerAsset), Name);
	}

	const FString* FindMetadataValue(UPackage* Package, const UObject* Object, FName Key)
	{
		if (!Package || !Object) return nullptr;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
		return Package->GetMetaData().FindValue(Object, Key);
#else
		UMetaData* MetaData = Package->GetMetaData();
		return MetaData ? MetaData->FindValue(Object, Key) : nullptr;
#endif
	}

	void SetMetadataValue(UPackage* Package, const UObject* Object, FName Key, const FString& Value)
	{
		if (!Package || !Object) return;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
		Package->GetMetaData().SetValue(Object, Key, *Value);
#else
		if (UMetaData* MetaData = Package->GetMetaData()) MetaData->SetValue(Object, Key, *Value);
#endif
	}

	void RemoveMetadataValue(UPackage* Package, const UObject* Object, FName Key)
	{
		if (!Package || !Object) return;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
		Package->GetMetaData().RemoveValue(Object, Key);
#else
		if (UMetaData* MetaData = Package->GetMetaData()) MetaData->RemoveValue(Object, Key);
#endif
	}

	FString ReadMeta(const UObject* Object, FName Key, bool* bOutFound = nullptr)
	{
		if (bOutFound) *bOutFound = false;
		if (!Object || !Object->GetOutermost()) return FString();
		if (const FString* Value = FindMetadataValue(Object->GetOutermost(), Object, Key))
		{
			if (bOutFound) *bOutFound = true;
			return *Value;
		}
		return FString();
	}

	bool ValidateOwner(const UObject* Object, const FGuid& BakeSetId, FString& OutError)
	{
		bool bFound = false;
		const FString Owner = ReadMeta(Object, OwnerKey, &bFound);
		if (!bFound)
		{
			OutError = FString::Printf(TEXT("Managed object '%s' has no Paper2DPlus bake owner stamp."),
				Object ? *Object->GetPathName() : TEXT("<null>"));
			return false;
		}
		if (!Owner.Equals(GuidText(BakeSetId), ESearchCase::IgnoreCase))
		{
			OutError = FString::Printf(TEXT("Managed object '%s' belongs to competing bake set '%s'."),
				*Object->GetPathName(), *Owner);
			return false;
		}
		return true;
	}

	void StampObject(UObject* Object, const FGuid& Owner, uint32 Revision, const FString& Role, const FString& Target)
	{
		if (!Object || !Object->GetOutermost()) return;
		UPackage* Package = Object->GetOutermost();
		SetMetadataValue(Package, Object, OwnerKey, GuidText(Owner));
		SetMetadataValue(Package, Object, RevisionKey, LexToString(Revision));
		SetMetadataValue(Package, Object, RoleKey, Role);
		SetMetadataValue(Package, Object, TargetKey, Target);
	}

	UObject* ResolveAnyObject(const FManagedObjectPath& Path)
	{
		if (Path.ObjectPath.IsEmpty()) return nullptr;
		// Generated bake assets can live in unsaved packages. Resolve those directly before asking the
		// loader; StaticLoadObject logs a misleading "Failed to find object" warning for an in-memory,
		// unsaved package even when the object is already present in the global object table.
		if (UPackage* Package = FindPackage(nullptr, *Path.PackageName))
		{
			// The two-argument template maps to EFindObjectFlags on current engines and to the
			// legacy default-exactness overload on 5.0-5.3 without touching either deprecated bool API.
			if (UObject* Existing = FindObject<UObject>(Package, *Path.ObjectName))
			{
				return Existing;
			}
		}
		return StaticLoadObject(
			UObject::StaticClass(), nullptr, *Path.ObjectPath, nullptr, LOAD_NoWarn);
	}

	UObject* ResolveSoftObjectQuietly(const FSoftObjectPath& Path)
	{
		FManagedObjectPath ManagedPath;
		return ParseObjectPath(Path, ManagedPath) ? ResolveAnyObject(ManagedPath) : nullptr;
	}

	bool IsTempObject(const UObject* Object)
	{
		return Object && FPackageName::IsTempPackage(Object->GetOutermost()->GetName());
	}

	void RegisterCreatedAsset(UObject* Object)
	{
		if (Object && !IsTempObject(Object)) FAssetRegistryModule::AssetCreated(Object);
	}

	void UnregisterCreatedAsset(UObject* Object)
	{
		if (Object && !IsTempObject(Object)) FAssetRegistryModule::AssetDeleted(Object);
	}

	void SetDirty(UPackage* Package, bool bDirty)
	{
		if (Package) Package->SetDirtyFlag(bDirty);
	}

	const FFlipbookProfileEntry* FindProfileEntry(
		const UPaper2DPlusCharacterProfileAsset& Profile,
		const UPaperFlipbook* Flipbook,
		const FString& LegacyName)
	{
		const FSoftObjectPath Path(Flipbook ? Flipbook->GetPathName() : FString());
		const FFlipbookProfileEntry* Match = nullptr;
		for (const FFlipbookProfileEntry& Entry : Profile.Flipbooks)
		{
			if (Flipbook && (Entry.Identity.Flipbook.Get() == Flipbook
				|| Entry.Identity.Flipbook.ToSoftObjectPath() == Path))
			{
				if (Match) return nullptr;
				Match = &Entry;
			}
		}
		if (Match) return Match;
		for (const FFlipbookProfileEntry& Entry : Profile.Flipbooks)
		{
			if (!LegacyName.IsEmpty() && Entry.Identity.FlipbookName.Equals(LegacyName, ESearchCase::IgnoreCase))
			{
				if (Match) return nullptr;
				Match = &Entry;
			}
		}
		return Match;
	}

	FFlipbookProfileEntry* FindProfileEntryMutable(
		UPaper2DPlusCharacterProfileAsset& Profile,
		const UPaperFlipbook* Flipbook,
		const FString& LegacyName)
	{
		return const_cast<FFlipbookProfileEntry*>(FindProfileEntry(Profile, Flipbook, LegacyName));
	}

	const FCharacterLayerAnimationBakeRecord* FindRecord(
		const FCharacterLayerBakeManifest& Manifest,
		const UPaperFlipbook* Flipbook,
		const FString& LegacyName)
	{
		const FSoftObjectPath Path(Flipbook ? Flipbook->GetPathName() : FString());
		const FCharacterLayerAnimationBakeRecord* Match = nullptr;
		for (const FCharacterLayerAnimationBakeRecord& Record : Manifest.Animations)
		{
			if (Flipbook && (Record.Flipbook.Get() == Flipbook || Record.Flipbook.ToSoftObjectPath() == Path))
			{
				if (Match) return nullptr;
				Match = &Record;
			}
		}
		if (Match) return Match;
		for (const FCharacterLayerAnimationBakeRecord& Record : Manifest.Animations)
		{
			if (!LegacyName.IsEmpty() && Record.LegacyAnimationName.Equals(LegacyName, ESearchCase::IgnoreCase))
			{
				if (Match) return nullptr;
				Match = &Record;
			}
		}
		return Match;
	}

	FString StagingRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir()
			/ TEXT("Paper2DPlus") / TEXT("LayerBake"));
	}

	bool IsSafeStagingChild(const FString& Candidate)
	{
		FString Root = StagingRoot();
		FString Full = FPaths::ConvertRelativePathToFull(Candidate);
		FPaths::NormalizeDirectoryName(Root);
		FPaths::NormalizeDirectoryName(Full);
		return !Full.Equals(Root, ESearchCase::IgnoreCase)
			&& Full.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase);
	}

	bool HasStagingPathReparsePoint(const FString& Candidate)
	{
		if (!IsSafeStagingChild(Candidate)) return true;
		FString Root = StagingRoot();
		FString Cursor = FPaths::ConvertRelativePathToFull(Candidate);
		FPaths::NormalizeDirectoryName(Root);
		FPaths::NormalizeDirectoryName(Cursor);
		while (true)
		{
			if (IFileManager::Get().DirectoryExists(*Cursor)
				&& CharacterLayerBakeCoordinator::IsPathLinkOrReparsePoint(*Cursor))
			{
				return true; // Windows junctions are reported through the same API.
			}
			if (Cursor.Equals(Root, ESearchCase::IgnoreCase)) return false;
			const FString Parent = FPaths::GetPath(Cursor);
			if (Parent.IsEmpty() || Parent.Equals(Cursor, ESearchCase::IgnoreCase)
				|| (!Parent.Equals(Root, ESearchCase::IgnoreCase)
					&& !Parent.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase)))
			{
				return true;
			}
			Cursor = Parent;
		}
	}

	bool StagingTreeContainsReparsePoint(const FString& Directory)
	{
		if (CharacterLayerBakeCoordinator::IsPathLinkOrReparsePoint(*Directory)) return true;
		bool bFound = false;
		IFileManager::Get().IterateDirectory(*Directory,
			[&bFound](const TCHAR* Path, bool bIsDirectory)
			{
				if (CharacterLayerBakeCoordinator::IsPathLinkOrReparsePoint(Path)
					|| (bIsDirectory && StagingTreeContainsReparsePoint(Path)))
				{
					bFound = true;
					return false;
				}
				return true;
			});
		return bFound;
	}

	bool DeleteStagingTreeWithoutFollowingLinks(const FString& Directory)
	{
		if (CharacterLayerBakeCoordinator::IsPathLinkOrReparsePoint(*Directory)) return false;
		bool bDeleted = true;
		IFileManager::Get().IterateDirectory(*Directory,
			[&bDeleted](const TCHAR* Path, bool bIsDirectory)
			{
				if (CharacterLayerBakeCoordinator::IsPathLinkOrReparsePoint(Path))
				{
					bDeleted = false;
					return false;
				}
				bDeleted = bIsDirectory
					? DeleteStagingTreeWithoutFollowingLinks(Path)
					: IFileManager::Get().Delete(Path, false, true, true);
				return bDeleted;
			});
		return bDeleted
			&& !CharacterLayerBakeCoordinator::IsPathLinkOrReparsePoint(*Directory)
			&& IFileManager::Get().DeleteDirectory(*Directory, false, false);
	}

	bool TryDeleteSafeStagingTree(const FString& Candidate)
	{
		if (!IsSafeStagingChild(Candidate) || HasStagingPathReparsePoint(Candidate)
			|| StagingTreeContainsReparsePoint(Candidate))
		{
			return false;
		}
		return DeleteStagingTreeWithoutFollowingLinks(Candidate);
	}
}

FString CharacterLayerBakeCoordinator::GetStagingRoot()
{
	return Paper2DPlusCharacterLayerBakeCoordinatorPrivate::StagingRoot();
}

void CharacterLayerBakeCoordinator::CleanupAbandonedStaging()
{
	using namespace Paper2DPlusCharacterLayerBakeCoordinatorPrivate;
	const FString Root = StagingRoot();
	TArray<FString> Directories;
	IFileManager::Get().FindFiles(Directories, *(Root / TEXT("*")), false, true);
	for (const FString& Directory : Directories)
	{
		const FString Candidate = Root / Directory;
		TryDeleteSafeStagingTree(Candidate);
	}
}

bool CharacterLayerBakeCoordinator::CaptureRegistration(
	const UPaper2DPlusCharacterProfileAsset* CharacterProfile,
	TArray<FCharacterLayerAnimationRegistration>& OutRegistration,
	FString& OutError)
{
	OutRegistration.Reset();
	OutError.Reset();
	if (!CharacterProfile)
	{
		OutError = TEXT("A loaded Character Profile is required to capture registration.");
		return false;
	}

	TSet<FString> SeenFlipbooks;
	for (const FFlipbookProfileEntry& Entry : CharacterProfile->Flipbooks)
	{
		UPaperFlipbook* Flipbook = Entry.Identity.Flipbook.LoadSynchronous();
		if (!Flipbook)
		{
			OutError = FString::Printf(TEXT("Animation '%s' has no resolvable canonical flipbook."),
				*Entry.Identity.FlipbookName);
			OutRegistration.Reset();
			return false;
		}
		const FString Identity = Flipbook->GetPathName().ToLower();
		if (SeenFlipbooks.Contains(Identity))
		{
			OutError = FString::Printf(TEXT("Canonical flipbook '%s' appears more than once in the Profile."),
				*Flipbook->GetPathName());
			OutRegistration.Reset();
			return false;
		}
		SeenFlipbooks.Add(Identity);
		if (Flipbook->GetNumKeyFrames() <= 0)
		{
			OutError = FString::Printf(TEXT("Canonical flipbook '%s' has no key frames."), *Flipbook->GetPathName());
			OutRegistration.Reset();
			return false;
		}

		FCharacterLayerAnimationRegistration& Registration = OutRegistration.AddDefaulted_GetRef();
		Registration.Flipbook = Flipbook;
		Registration.LegacyAnimationName = Entry.Identity.FlipbookName;
		Registration.FramesPerSecond = Flipbook->GetFramesPerSecond();
		Registration.FrameRuns.Reserve(Flipbook->GetNumKeyFrames());
		Registration.Frames.SetNum(Flipbook->GetNumKeyFrames());
		bool bHasGeometry = false;
		for (int32 FrameIndex = 0; FrameIndex < Flipbook->GetNumKeyFrames(); ++FrameIndex)
		{
			const FPaperFlipbookKeyFrame& KeyFrame = Flipbook->GetKeyFrameChecked(FrameIndex);
			Registration.FrameRuns.Add(KeyFrame.FrameRun);
			FCharacterLayerRegisteredFrame& Frame = Registration.Frames[FrameIndex];
			UPaperSprite* Sprite = KeyFrame.Sprite;
			if (!Sprite) continue;

			Frame.SourceSprite = Sprite;
			Frame.SourceUV = Sprite->GetSourceUV();
			Frame.SourceDimension = Sprite->GetSourceSize();
			Frame.PivotLocal = Sprite->GetPivotPosition() - Sprite->GetSourceUV();
			Frame.PixelsPerUnrealUnit = Sprite->GetPixelsPerUnrealUnit();
			Frame.MaterialPath = Sprite->GetDefaultMaterial()
				? FSoftObjectPath(Sprite->GetDefaultMaterial()->GetPathName())
				: FSoftObjectPath();
			Frame.NativeBehaviorDigest = CharacterLayerBakeCore::ComputeNativeSpriteBehaviorDigest(Sprite);
#if WITH_EDITORONLY_DATA
			Frame.bUsesUnsupportedAtlasGroup = Sprite->GetAtlasGroup() != nullptr;
#endif
			FAdditionalSpriteTextureArray AdditionalTextures;
			Sprite->GetBakedAdditionalSourceTextures(AdditionalTextures);
			Frame.bUsesUnsupportedAdditionalTextures = !AdditionalTextures.IsEmpty();
			bHasGeometry |= Frame.SourceDimension.X > 0.0 && Frame.SourceDimension.Y > 0.0;
		}
		if (!bHasGeometry)
		{
			OutError = FString::Printf(TEXT("Canonical flipbook '%s' is all-null and has no explicit registration geometry."),
				*Flipbook->GetPathName());
			OutRegistration.Reset();
			return false;
		}
		Registration.RegistrationDigest = CharacterLayerBakeCore::ComputeAnimationRegistrationDigest(Registration);
	}
	if (OutRegistration.IsEmpty())
	{
		OutError = TEXT("The Character Profile contains no canonical animations.");
		return false;
	}
	return true;
}

FString CharacterLayerBakeCoordinator::ComputeCurrentOutputDigest(
	const UPaper2DPlusCharacterLayerAsset* LayerAsset,
	const UPaper2DPlusCharacterProfileAsset* CharacterProfile,
	const FCharacterLayerAnimationBakeRecord& Record,
	FString* OutError)
{
	using namespace Paper2DPlusCharacterLayerBakeCoordinatorPrivate;
	if (OutError) OutError->Reset();
	auto Fail = [OutError](const FString& Message) -> FString
	{
		if (OutError) *OutError = Message;
		return FString();
	};
	if (!LayerAsset || !CharacterProfile || !LayerAsset->BakeSetId.IsValid())
	{
		return Fail(TEXT("Layer Asset, Character Profile, and a valid bake identity are required."));
	}
	if (HasFutureBakeManifest(LayerAsset))
	{
		return Fail(FutureBakeManifestReport(*LayerAsset));
	}
	UPaperFlipbook* Flipbook = Record.Flipbook.LoadSynchronous();
	if (!Flipbook)
	{
		return Fail(FString::Printf(TEXT("Recorded flipbook '%s' is missing."),
			*Record.Flipbook.ToSoftObjectPath().ToString()));
	}
	const FFlipbookProfileEntry* ProfileEntry = FindProfileEntry(
		*CharacterProfile, Flipbook, Record.LegacyAnimationName);
	if (!ProfileEntry)
	{
		return Fail(TEXT("The recorded flipbook no longer resolves to one Profile animation row."));
	}
	UTexture2D* Texture = Cast<UTexture2D>(ResolveSoftObjectQuietly(Record.ManagedTexture));
	if (!Texture)
	{
		return Fail(FString::Printf(TEXT("Recorded managed texture '%s' is missing or has the wrong class."),
			*Record.ManagedTexture.ToString()));
	}
	// Texture Source uses mutable lock state while the async compiler reads it. UE 5.0 can re-enter its registered
	// texture set if a changed source finishes an older build, so every integrity read must observe a completed build.
	FinishTextureCompilation(Texture);
	FString OwnerError;
	if (!ValidateOwner(Texture, LayerAsset->BakeSetId, OwnerError)
		|| !ValidateOwner(Texture->GetOutermost(), LayerAsset->BakeSetId, OwnerError)
		|| !ValidateOwner(Flipbook, LayerAsset->BakeSetId, OwnerError))
	{
		return Fail(OwnerError);
	}

	FDigestBuilder Digest;
	const uint32 DigestVersion = Record.OutputRevision > LayerAsset->BakeManifest.BakeRevision
		? CharacterLayerBakeCore::CurrentDigestVersion
		: LayerAsset->BakeManifest.DigestVersion;
	Digest.AddString(TEXT("Paper2DPlus.CharacterLayerBake.ActualOutput"));
	Digest.AddUInt32(DigestVersion);
	Digest.AddString(GuidText(LayerAsset->BakeSetId));
	Digest.AddUInt32(Record.OutputRevision);
	Digest.AddString(Record.SourceDigest);
	Digest.AddString(Flipbook->GetPathName().ToLower());
	Digest.AddString(CharacterProfile->GetPathName().ToLower());
	Digest.AddString(GuidText(CharacterProfile->LayerBakeOwnerToken));
	Digest.AddString(ReadMeta(Flipbook, RevisionKey));

	Digest.AddString(Texture->GetPathName().ToLower());
	Digest.AddString(ReadMeta(Texture, RevisionKey));
	Digest.AddInt32(static_cast<int32>(Texture->Source.GetSizeX()));
	Digest.AddInt32(static_cast<int32>(Texture->Source.GetSizeY()));
	Digest.AddUInt32(static_cast<uint32>(Texture->Source.GetFormat()));
	const int64 TextureBytes = Texture->Source.GetSizeX()
		* Texture->Source.GetSizeY() * Texture->Source.GetBytesPerPixel();
	// Reject an obviously absent source payload before LockMipReadOnly asks older engine
	// versions to decompress it. UE 5.1-5.7 log that corrupt-input probe as an Error even
	// though the plugin correctly fails closed, which turns a recoverable asset diagnosis
	// into an editor/test failure. GetSizeOnDisk is available across UE 5.0-5.8 and does
	// not load or decode the payload.
	if (TextureBytes > 0 && Texture->Source.GetSizeOnDisk() <= 0)
	{
		return Fail(FString::Printf(
			TEXT("Recorded managed texture '%s' declares %lld source byte(s), but mip 0 could not be read."),
			*Texture->GetPathName(),
			static_cast<long long>(TextureBytes)));
	}
	const uint8* TextureData = TextureBytes > 0 ? Texture->Source.LockMipReadOnly(0) : nullptr;
	if (TextureBytes > 0 && !TextureData)
	{
		return Fail(FString::Printf(
			TEXT("Recorded managed texture '%s' declares %lld source byte(s), but mip 0 could not be read."),
			*Texture->GetPathName(),
			static_cast<long long>(TextureBytes)));
	}
	Digest.AddBytes(TextureData, TextureBytes);
	if (TextureData) Texture->Source.UnlockMip(0);
	Digest.AddUInt32(static_cast<uint32>(Texture->CompressionSettings));
	Digest.AddUInt32(static_cast<uint32>(Texture->Filter));
	Digest.AddUInt32(static_cast<uint32>(Texture->MipGenSettings));
	Digest.AddUInt32(static_cast<uint32>(Texture->LODGroup));
	Digest.AddBool(Texture->NeverStream);
	Digest.AddBool(Texture->SRGB);

	Digest.AddUInt32(static_cast<uint32>(Record.ManagedSprites.Num()));
	for (const FSoftObjectPath& SpritePath : Record.ManagedSprites)
	{
		Digest.AddString(SpritePath.ToString().ToLower());
		if (SpritePath.IsNull()) continue;
		UPaperSprite* Sprite = Cast<UPaperSprite>(ResolveSoftObjectQuietly(SpritePath));
		if (!Sprite)
		{
			return Fail(FString::Printf(TEXT("Recorded managed sprite '%s' is missing or has the wrong class."),
				*SpritePath.ToString()));
		}
		if (!ValidateOwner(Sprite, LayerAsset->BakeSetId, OwnerError)
			|| !ValidateOwner(Sprite->GetOutermost(), LayerAsset->BakeSetId, OwnerError))
		{
			return Fail(OwnerError);
		}
		Digest.AddString(ReadMeta(Sprite, RevisionKey));
		Digest.AddVector2D(Sprite->GetSourceUV());
		Digest.AddVector2D(Sprite->GetSourceSize());
		Digest.AddVector2D(Sprite->GetPivotPosition() - Sprite->GetSourceUV());
		Digest.AddFloat(Sprite->GetPixelsPerUnrealUnit());
		Digest.AddString(Sprite->GetDefaultMaterial()
			? Sprite->GetDefaultMaterial()->GetPathName().ToLower()
			: FString());
		Digest.AddString(CharacterLayerBakeCore::ComputeNativeSpriteBehaviorDigest(Sprite));
	}

	Digest.AddFloat(Flipbook->GetFramesPerSecond());
	Digest.AddUInt32(static_cast<uint32>(Flipbook->GetNumKeyFrames()));
	for (int32 FrameIndex = 0; FrameIndex < Flipbook->GetNumKeyFrames(); ++FrameIndex)
	{
		const FPaperFlipbookKeyFrame& KeyFrame = Flipbook->GetKeyFrameChecked(FrameIndex);
		Digest.AddInt32(KeyFrame.FrameRun);
		Digest.AddString(KeyFrame.Sprite ? KeyFrame.Sprite->GetPathName().ToLower() : FString());
	}

	Digest.AddUInt32(static_cast<uint32>(ProfileEntry->CombatData.Frames.Num()));
	for (const FFrameHitboxData& Frame : ProfileEntry->CombatData.Frames) Digest.AddFrame(Frame);
	Digest.AddUInt32(static_cast<uint32>(ProfileEntry->FrameEventData.FrameCues.Num()));
	for (const UPaper2DPlusCueBase* Cue : ProfileEntry->FrameEventData.FrameCues)
	{
		Digest.AddString(CharacterLayerBakeCore::ComputeCueSemanticDigest(
			Cue, DigestVersion));
	}
	Digest.AddUInt32(static_cast<uint32>(ProfileEntry->FrameEventData.FrameEvents.Num()));
	for (const UPaper2DPlusFrameEventBase* Event : ProfileEntry->FrameEventData.FrameEvents)
	{
		Digest.AddString(SerializeObjectSemantic(Event));
	}
	return Digest.Finalize();
}

FString CharacterLayerBakeCoordinator::GetManagedOutputRoot(
	const UPaper2DPlusCharacterLayerAsset* LayerAsset)
{
	return LayerAsset
		? Paper2DPlusCharacterLayerBakeCoordinatorPrivate::GeneratedRoot(*LayerAsset)
		: FString();
}

namespace Paper2DPlusCharacterLayerBakeCoordinatorPrivate
{
	struct FManagedAnimation
	{
		FCharacterLayerBakeAnimationPlan Plan;
		FString PixelFile;
		FString PixelHash;
		FManagedObjectPath TexturePath;
		TArray<FManagedObjectPath> SpritePaths;
		TObjectPtr<UTexture2D> Texture = nullptr;
		TArray<TObjectPtr<UPaperSprite>> Sprites;
		TArray<bool> bSpriteNeeded;
		bool bTextureWasNew = false;
		TArray<bool> bSpriteWasNew;
	};

	bool CheckSharedFlipbooks(
		const UPaper2DPlusCharacterLayerAsset& Asset,
		const TArray<FCharacterLayerAnimationRegistration>& Registration,
		FString& OutError)
	{
		TSet<FString> Targets;
		for (const FCharacterLayerAnimationRegistration& Row : Registration)
		{
			if (UPaperFlipbook* Flipbook = Row.Flipbook.LoadSynchronous())
				Targets.Add(Flipbook->GetPathName().ToLower());
		}
		for (TObjectIterator<UPaper2DPlusCharacterLayerAsset> It; It; ++It)
		{
			const UPaper2DPlusCharacterLayerAsset* Other = *It;
			if (!Other || Other == &Asset || Other->GetOutermost() == GetTransientPackage()) continue;
			if (!Other->BakeSetId.IsValid() || Other->BakeSetId == Asset.BakeSetId) continue;
			if (Other->BakeAttachmentState == ECharacterLayerBakeAttachmentState::Unclaimed
				|| Other->BakeAttachmentState == ECharacterLayerBakeAttachmentState::Detached) continue;
			for (const FCharacterLayerAnimationRegistration& Row : Other->AnimationRegistration)
			{
				UPaperFlipbook* OtherFlipbook = Row.Flipbook.Get();
				if (OtherFlipbook && Targets.Contains(OtherFlipbook->GetPathName().ToLower()))
				{
					OutError = FString::Printf(TEXT("Canonical flipbook '%s' is already registered to Layer Asset '%s'."),
						*OtherFlipbook->GetPathName(), *Other->GetPathName());
					return false;
				}
			}
		}
		return true;
	}

	bool PreflightManagedAnimation(
		const FCharacterLayerBakeRequest& Request,
		FManagedAnimation& Animation,
		FString& OutError)
	{
		UPaper2DPlusCharacterLayerAsset& Asset = *Request.LayerAsset;
		UPaperFlipbook* Flipbook = Animation.Plan.TargetFlipbook.Get();
		if (!Flipbook)
		{
			OutError = TEXT("A staged canonical flipbook was unloaded before managed-path preflight.");
			return false;
		}
		const FCharacterLayerAnimationBakeRecord* Existing = FindRecord(
			Asset.BakeManifest, Flipbook, Animation.Plan.AnimationName);
		if (Existing && Existing->OutputDigest.IsEmpty())
		{
			OutError = TEXT("An existing bake record has no output digest and cannot be trusted.");
			return false;
		}
		if (Existing)
		{
			FString DigestError;
			const FString Current = CharacterLayerBakeCoordinator::ComputeCurrentOutputDigest(
				&Asset, Request.CharacterProfile, *Existing, &DigestError);
			if (Current.IsEmpty())
			{
				OutError = DigestError;
				return false; // missing/owner-invalid backing is never silently recreated
			}
			if (Current != Existing->OutputDigest && !Request.bOverwriteManagedOutputConflict)
			{
				OutError = FString::Printf(TEXT("Managed output for '%s' differs from its manifest. Use Overwrite from Layer Source explicitly."),
					*Animation.Plan.AnimationName);
				return false;
			}
			if (!ParseObjectPath(Existing->ManagedTexture, Animation.TexturePath)
				|| (Existing->ManagedSprites.Num() != Animation.Plan.KeyFrameCount
					&& !Request.bOverwriteManagedOutputConflict))
			{
				OutError = TEXT("An existing bake record has incomplete managed paths.");
				return false;
			}
		}
		else
		{
			Animation.TexturePath = DeterministicTexturePath(Asset, Animation.Plan);
		}

		UObject* TextureObject = ResolveAnyObject(Animation.TexturePath);
		if (Existing)
		{
			Animation.Texture = Cast<UTexture2D>(TextureObject);
			if (!Animation.Texture || !ValidateOwner(Animation.Texture, Asset.BakeSetId, OutError)
				|| !ValidateOwner(Animation.Texture->GetOutermost(), Asset.BakeSetId, OutError))
				return false;
		}
		else if (TextureObject)
		{
			OutError = FString::Printf(TEXT("Deterministic generated path '%s' is already occupied."),
				*Animation.TexturePath.ObjectPath);
			return false;
		}

		Animation.SpritePaths.SetNum(Animation.Plan.KeyFrameCount);
		Animation.Sprites.SetNum(Animation.Plan.KeyFrameCount);
		Animation.bSpriteNeeded.SetNum(Animation.Plan.KeyFrameCount);
		Animation.bSpriteWasNew.Init(false, Animation.Plan.KeyFrameCount);
		for (int32 FrameIndex = 0; FrameIndex < Animation.Plan.KeyFrameCount; ++FrameIndex)
		{
			Animation.bSpriteNeeded[FrameIndex] = Animation.Plan.Composite.Frames.IsValidIndex(FrameIndex)
				&& Animation.Plan.Composite.Frames[FrameIndex].bHasVisiblePixels;
			FSoftObjectPath ExistingPath;
			if (Existing && Existing->ManagedSprites.IsValidIndex(FrameIndex))
			{
				ExistingPath = Existing->ManagedSprites[FrameIndex];
			}
			if (!ExistingPath.IsNull())
			{
				if (!ParseObjectPath(ExistingPath, Animation.SpritePaths[FrameIndex]))
				{
					OutError = TEXT("An existing managed sprite path is malformed.");
					return false;
				}
				Animation.Sprites[FrameIndex] = Cast<UPaperSprite>(ResolveAnyObject(Animation.SpritePaths[FrameIndex]));
				if (!Animation.Sprites[FrameIndex]
					|| !ValidateOwner(Animation.Sprites[FrameIndex], Asset.BakeSetId, OutError)
					|| !ValidateOwner(Animation.Sprites[FrameIndex]->GetOutermost(), Asset.BakeSetId, OutError))
					return false;
			}
			else if (Animation.bSpriteNeeded[FrameIndex])
			{
				Animation.SpritePaths[FrameIndex] = DeterministicSpritePath(Asset, Animation.Plan, FrameIndex);
				if (UObject* Occupant = ResolveAnyObject(Animation.SpritePaths[FrameIndex]))
				{
					Animation.Sprites[FrameIndex] = Cast<UPaperSprite>(Occupant);
					if (!Request.bOverwriteManagedOutputConflict
						|| !Animation.Sprites[FrameIndex]
						|| !ValidateOwner(Animation.Sprites[FrameIndex], Asset.BakeSetId, OutError)
						|| !ValidateOwner(Animation.Sprites[FrameIndex]->GetOutermost(), Asset.BakeSetId, OutError))
					{
						if (OutError.IsEmpty())
						{
							OutError = FString::Printf(TEXT("Deterministic generated path '%s' is already occupied."),
								*Animation.SpritePaths[FrameIndex].ObjectPath);
						}
						return false;
					}
				}
			}
		}
		return true;
	}

	struct FMetaEntrySnapshot
	{
		FName Key;
		bool bHadValue = false;
		FString Value;
	};

	struct FMetaSnapshot
	{
		TObjectPtr<UObject> Object = nullptr;
		TArray<FMetaEntrySnapshot> Entries;

		void Capture(UObject* InObject)
		{
			Object = InObject;
			for (FName Key : { OwnerKey, RevisionKey, RoleKey, TargetKey })
			{
				FMetaEntrySnapshot& Entry = Entries.AddDefaulted_GetRef();
				Entry.Key = Key;
				Entry.Value = ReadMeta(InObject, Key, &Entry.bHadValue);
			}
		}

		void Restore() const
		{
			if (!Object || !Object->GetOutermost()) return;
			for (const FMetaEntrySnapshot& Entry : Entries)
			{
				if (Entry.bHadValue) SetMetadataValue(Object->GetOutermost(), Object, Entry.Key, Entry.Value);
				else RemoveMetadataValue(Object->GetOutermost(), Object, Entry.Key);
			}
		}
	};

	struct FTextureSnapshot
	{
		TObjectPtr<UTexture2D> Texture = nullptr;
		int32 Width = 0;
		int32 Height = 0;
		ETextureSourceFormat Format = TSF_Invalid;
		TArray<uint8> Bytes;
		TextureCompressionSettings Compression = TC_Default;
		TextureFilter Filter = TF_Default;
		TextureMipGenSettings MipGen = TMGS_FromTextureGroup;
		TextureGroup LODGroup = TEXTUREGROUP_World;
		bool bNeverStream = false;
		bool bSRGB = true;

		bool Capture(UTexture2D* InTexture)
		{
			Texture = InTexture;
			if (!Texture) return false;
			FinishTextureCompilation(Texture);
			Width = static_cast<int32>(Texture->Source.GetSizeX());
			Height = static_cast<int32>(Texture->Source.GetSizeY());
			Format = Texture->Source.GetFormat();
			const int64 Count = static_cast<int64>(Width) * Height * Texture->Source.GetBytesPerPixel();
			if (Count < 0 || Count > MAX_int32) return false;
			Bytes.SetNumUninitialized(static_cast<int32>(Count));
			if (Count > 0)
			{
				const uint8* Source = Texture->Source.LockMipReadOnly(0);
				if (!Source) return false;
				FMemory::Memcpy(Bytes.GetData(), Source, Count);
				Texture->Source.UnlockMip(0);
			}
			Compression = Texture->CompressionSettings;
			Filter = Texture->Filter;
			MipGen = Texture->MipGenSettings;
			LODGroup = Texture->LODGroup;
			bNeverStream = Texture->NeverStream;
			bSRGB = Texture->SRGB;
			return true;
		}

		void Restore() const
		{
			if (!Texture) return;
			FinishTextureCompilation(Texture);
			Texture->Source.Init(Width, Height, 1, 1, Format);
			if (!Bytes.IsEmpty())
			{
				uint8* Destination = Texture->Source.LockMip(0);
				FMemory::Memcpy(Destination, Bytes.GetData(), Bytes.Num());
				Texture->Source.UnlockMip(0);
			}
			Texture->CompressionSettings = Compression;
			Texture->Filter = Filter;
			Texture->MipGenSettings = MipGen;
			Texture->LODGroup = LODGroup;
			Texture->NeverStream = bNeverStream;
			Texture->SRGB = bSRGB;
			Texture->UpdateResource();
			FinishTextureCompilation(Texture);
		}

		bool Verify() const
		{
			FinishTextureCompilation(Texture);
			if (!Texture || Width != Texture->Source.GetSizeX() || Height != Texture->Source.GetSizeY()
				|| Format != Texture->Source.GetFormat()
				|| Compression != Texture->CompressionSettings || Filter != Texture->Filter
				|| MipGen != Texture->MipGenSettings || LODGroup != Texture->LODGroup
				|| bNeverStream != Texture->NeverStream || bSRGB != Texture->SRGB)
				return false;
			const int64 Count = static_cast<int64>(Width) * Height * Texture->Source.GetBytesPerPixel();
			if (Count != Bytes.Num()) return false;
			if (Count == 0) return true;
			const uint8* Current = Texture->Source.LockMipReadOnly(0);
			const bool bEqual = Current && FMemory::Memcmp(Current, Bytes.GetData(), Bytes.Num()) == 0;
			if (Current) Texture->Source.UnlockMip(0);
			return bEqual;
		}
	};

	struct FSpriteSnapshot
	{
		TObjectPtr<UPaperSprite> Sprite = nullptr;
		TStrongObjectPtr<UPaperSprite> Copy;
		TObjectPtr<UBodySetup> OriginalBodySetup = nullptr;
		FString NativeDigest;

		bool Capture(UPaperSprite* InSprite)
		{
			Sprite = InSprite;
			if (!Sprite) return false;
			Copy.Reset(DuplicateObject<UPaperSprite>(Sprite, GetTransientPackage()));
			OriginalBodySetup = Sprite->BodySetup;
			NativeDigest = CharacterLayerBakeCore::ComputeNativeSpriteBehaviorDigest(Sprite);
			return Copy.IsValid();
		}

		void Restore() const
		{
			if (!Sprite || !Copy.IsValid()) return;
			UEngine::CopyPropertiesForUnrelatedObjects(Copy.Get(), Sprite);
			Sprite->BodySetup = OriginalBodySetup;
		}

		bool Verify() const
		{
			return Sprite && CharacterLayerBakeCore::ComputeNativeSpriteBehaviorDigest(Sprite) == NativeDigest;
		}
	};

	struct FFlipbookSnapshot
	{
		TObjectPtr<UPaperFlipbook> Flipbook = nullptr;
		float FramesPerSecond = 0.0f;
		TArray<FPaperFlipbookKeyFrame> KeyFrames;
		FString SemanticDigest;

		void Capture(UPaperFlipbook* InFlipbook)
		{
			Flipbook = InFlipbook;
			FramesPerSecond = Flipbook->GetFramesPerSecond();
			for (int32 Index = 0; Index < Flipbook->GetNumKeyFrames(); ++Index)
				KeyFrames.Add(Flipbook->GetKeyFrameChecked(Index));
			SemanticDigest = SerializeObjectSemantic(Flipbook);
		}

		void Restore() const
		{
			if (!Flipbook) return;
			FScopedFlipbookMutator Mutator(Flipbook);
			Mutator.FramesPerSecond = FramesPerSecond;
			Mutator.KeyFrames = KeyFrames;
		}
	};

	struct FLayerSnapshot
	{
		ECharacterLayerUsageMode UsageMode = ECharacterLayerUsageMode::RuntimeCustomizable;
		TArray<FCharacterLayer> Layers;
		FGuid BakeSetId;
		ECharacterLayerBakeAttachmentState Attachment = ECharacterLayerBakeAttachmentState::Unclaimed;
		TArray<FCharacterLayerAnimationRegistration> Registration;
		FCharacterLayerBakeManifest Manifest;
		FCharacterLayerBakeOperationRecord LastOperation;
		FString SemanticDigest;

		void Capture(const UPaper2DPlusCharacterLayerAsset& Asset)
		{
			UsageMode = Asset.UsageMode;
			Layers = Asset.Layers;
			BakeSetId = Asset.BakeSetId;
			Attachment = Asset.BakeAttachmentState;
			Registration = Asset.AnimationRegistration;
			Manifest = Asset.BakeManifest;
			LastOperation = Asset.LastBakeOperation;
			SemanticDigest = SerializeObjectSemantic(&Asset);
		}

		void Restore(UPaper2DPlusCharacterLayerAsset& Asset) const
		{
			Asset.UsageMode = UsageMode;
			Asset.Layers = Layers;
			Asset.BakeSetId = BakeSetId;
			Asset.BakeAttachmentState = Attachment;
			Asset.AnimationRegistration = Registration;
			Asset.BakeManifest = Manifest;
			Asset.LastBakeOperation = LastOperation;
			// CookedGameplayAnimations is a derived runtime projection. Rebuild after a rollback
			// rather than allowing an editor-only Cue track sidecar from a copied snapshot to linger.
			Asset.RebuildCookedGameplayData();
		}
	};

	struct FProfileSnapshot
	{
		TArray<FFlipbookProfileEntry> Flipbooks;
		FGuid Owner;
		FString OwnerHint;
		TArray<FPaper2DPlusCharacterBaselineAnimation> Baseline;
		FString SemanticDigest;

		void Capture(const UPaper2DPlusCharacterProfileAsset& Profile)
		{
			Flipbooks = Profile.Flipbooks;
			Owner = Profile.LayerBakeOwnerToken;
			OwnerHint = Profile.LayerBakeOwnerPathHint;
			Baseline = Profile.CharacterBaseline;
			SemanticDigest = SerializeObjectSemantic(&Profile);
		}

		void Restore(UPaper2DPlusCharacterProfileAsset& Profile) const
		{
			Profile.Flipbooks = Flipbooks;
			Profile.LayerBakeOwnerToken = Owner;
			Profile.LayerBakeOwnerPathHint = OwnerHint;
			Profile.CharacterBaseline = Baseline;
		}
	};

	struct FNewObjectRecord
	{
		TObjectPtr<UObject> Object = nullptr;
		TObjectPtr<UPackage> Package = nullptr;
	};

	struct FCommitSnapshot
	{
		FLayerSnapshot Layer;
		FProfileSnapshot Profile;
		TArray<FFlipbookSnapshot> Flipbooks;
		TArray<FTextureSnapshot> Textures;
		TArray<FSpriteSnapshot> Sprites;
		TArray<FMetaSnapshot> Metadata;
		TMap<UPackage*, bool> DirtyStates;

		void CapturePackage(UPackage* Package)
		{
			if (Package && !DirtyStates.Contains(Package)) DirtyStates.Add(Package, Package->IsDirty());
		}

		void CaptureMeta(UObject* Object)
		{
			if (!Object || Metadata.ContainsByPredicate([Object](const FMetaSnapshot& Row)
			{
				return Row.Object == Object;
			})) return;
			FMetaSnapshot& Row = Metadata.AddDefaulted_GetRef();
			Row.Capture(Object);
		}

		bool Capture(
			UPaper2DPlusCharacterLayerAsset& Asset,
			UPaper2DPlusCharacterProfileAsset& CharacterProfile,
			const TArray<FManagedAnimation>& Animations,
			FString& OutError)
		{
			Layer.Capture(Asset);
			Profile.Capture(CharacterProfile);
			CapturePackage(Asset.GetOutermost());
			CapturePackage(CharacterProfile.GetOutermost());
			TSet<UPaperFlipbook*> SeenFlipbooks;
			TSet<UTexture2D*> SeenTextures;
			TSet<UPaperSprite*> SeenSprites;
			for (const FManagedAnimation& Animation : Animations)
			{
				UPaperFlipbook* Flipbook = Animation.Plan.TargetFlipbook.Get();
				if (Flipbook && !SeenFlipbooks.Contains(Flipbook))
				{
					SeenFlipbooks.Add(Flipbook);
					FFlipbookSnapshot& Row = Flipbooks.AddDefaulted_GetRef();
					Row.Capture(Flipbook);
					CapturePackage(Flipbook->GetOutermost());
					CaptureMeta(Flipbook);
				}
				if (Animation.Texture && !SeenTextures.Contains(Animation.Texture))
				{
					SeenTextures.Add(Animation.Texture);
					FTextureSnapshot& Row = Textures.AddDefaulted_GetRef();
					if (!Row.Capture(Animation.Texture))
					{
						OutError = TEXT("Could not snapshot an existing managed texture.");
						return false;
					}
					CapturePackage(Animation.Texture->GetOutermost());
					CaptureMeta(Animation.Texture);
					CaptureMeta(Animation.Texture->GetOutermost());
				}
				for (UPaperSprite* Sprite : Animation.Sprites)
				{
					if (!Sprite || SeenSprites.Contains(Sprite)) continue;
					SeenSprites.Add(Sprite);
					FSpriteSnapshot& Row = Sprites.AddDefaulted_GetRef();
					if (!Row.Capture(Sprite))
					{
						OutError = TEXT("Could not snapshot an existing managed sprite.");
						return false;
					}
					CapturePackage(Sprite->GetOutermost());
					CaptureMeta(Sprite);
					CaptureMeta(Sprite->GetOutermost());
				}
			}
			return true;
		}
	};

	UObject* CreateManagedObject(
		UClass* ExpectedClass,
		const FManagedObjectPath& Path,
		bool& bOutCreated,
		FString& OutError)
	{
		bOutCreated = false;
		if (UObject* Existing = ResolveAnyObject(Path))
		{
			if (Existing->GetClass() != ExpectedClass)
			{
				OutError = FString::Printf(TEXT("Managed path '%s' contains %s instead of %s."),
					*Path.ObjectPath, *Existing->GetClass()->GetName(), *ExpectedClass->GetName());
				return nullptr;
			}
			return Existing;
		}
		UPackage* Package = CreatePackage(*Path.PackageName);
		if (!Package)
		{
			OutError = FString::Printf(TEXT("Could not create managed package '%s'."), *Path.PackageName);
			return nullptr;
		}
		UObject* Occupant = StaticFindObject(UObject::StaticClass(), Package, *Path.ObjectName);
		if (Occupant)
		{
			OutError = FString::Printf(TEXT("Managed package '%s' already contains a wrong-class occupant."),
				*Path.PackageName);
			return nullptr;
		}
		UObject* Created = NewObject<UObject>(
			Package, ExpectedClass, FName(*Path.ObjectName), RF_Public | RF_Standalone);
		if (!Created)
		{
			OutError = FString::Printf(TEXT("Could not create managed object '%s'."), *Path.ObjectPath);
			return nullptr;
		}
		bOutCreated = true;
		RegisterCreatedAsset(Created);
		return Created;
	}

	bool WriteTexture(UTexture2D& Texture, const FCharacterLayerBakeAnimationPlan& Plan, const TArray<uint8>& Bytes)
	{
		const int64 ExpectedBytes = static_cast<int64>(Plan.Composite.SheetSize.X)
			* Plan.Composite.SheetSize.Y * sizeof(FColor);
		if (ExpectedBytes <= 0 || ExpectedBytes != Bytes.Num()) return false;
		// UTexture::Modify did not fully unregister an outstanding texture-compiler task in UE 5.0. Fence before
		// changing Source, then return a stable committed texture so digesting and later designer edits cannot race it.
		FinishTextureCompilation(&Texture);
		Texture.Modify();
		Texture.Source.Init(Plan.Composite.SheetSize.X, Plan.Composite.SheetSize.Y, 1, 1, TSF_BGRA8);
		uint8* Destination = Texture.Source.LockMip(0);
		if (!Destination) return false;
		FMemory::Memcpy(Destination, Bytes.GetData(), Bytes.Num());
		Texture.Source.UnlockMip(0);
		Texture.CompressionSettings = TC_EditorIcon;
		Texture.Filter = TF_Nearest;
		Texture.MipGenSettings = TMGS_NoMipmaps;
		Texture.LODGroup = TEXTUREGROUP_Pixels2D;
		Texture.NeverStream = true;
		Texture.SRGB = true;
		Texture.UpdateResource();
		FinishTextureCompilation(&Texture);
		SetDirty(Texture.GetOutermost(), true);
		return true;
	}

	FSpriteGeometryCollection* GeometryProperty(UPaperSprite& Sprite, const TCHAR* Name)
	{
		FStructProperty* Property = FindFProperty<FStructProperty>(UPaperSprite::StaticClass(), FName(Name));
		return Property ? Property->ContainerPtrToValuePtr<FSpriteGeometryCollection>(&Sprite) : nullptr;
	}

	void TranslateGeometry(FSpriteGeometryCollection* Geometry, const FVector2D& Delta)
	{
		if (!Geometry) return;
		for (FSpriteGeometryShape& Shape : Geometry->Shapes) Shape.BoxPosition += Delta;
	}

	bool MaterializeSprite(
		UPaperSprite& Sprite,
		UTexture2D& Texture,
		const FCharacterLayerBakeAnimationPlan& Plan,
		int32 FrameIndex,
		FString& OutError)
	{
		if (!Plan.NativeFrameRecipes.IsValidIndex(FrameIndex)
			|| !Plan.Composite.Frames.IsValidIndex(FrameIndex))
		{
			OutError = TEXT("Managed sprite recipe is missing its frame topology.");
			return false;
		}
		const FCharacterLayerRegisteredFrame& Recipe = Plan.NativeFrameRecipes[FrameIndex];
		const FCharacterLayerExactFrameOutput& CompositeFrame = Plan.Composite.Frames[FrameIndex];
		UPaperSprite* NativeSource = Recipe.SourceSprite.LoadSynchronous();
		if (!NativeSource)
		{
			OutError = TEXT("Managed sprite native source no longer resolves.");
			return false;
		}

		Sprite.Modify();
		UEngine::CopyPropertiesForUnrelatedObjects(NativeSource, &Sprite);
		FSpriteAssetInitParameters Init;
		Init.Texture = &Texture;
		Init.Offset = CompositeFrame.SheetOrigin;
		Init.Dimension = Plan.Composite.CellSize;
		Init.SetPixelsPerUnrealUnit(Recipe.PixelsPerUnrealUnit);
		Sprite.InitializeSprite(Init, false);

		const FVector2D GeometryDelta = FVector2D(CompositeFrame.SheetOrigin)
			- FVector2D(Plan.Composite.CanonicalUnion.Min) - Recipe.SourceUV;
		FSpriteGeometryCollection* Collision = GeometryProperty(Sprite, TEXT("CollisionGeometry"));
		FSpriteGeometryCollection* Render = GeometryProperty(Sprite, TEXT("RenderGeometry"));
		if (!Render)
		{
			OutError = TEXT("Managed sprite render geometry is unavailable.");
			return false;
		}
		TranslateGeometry(Collision, GeometryDelta);
		const ESpritePolygonMode::Type CollisionMode = Collision
			? Collision->GeometryType.GetValue() : ESpritePolygonMode::FullyCustom;
		if (Collision && !Collision->Shapes.IsEmpty()) Collision->GeometryType = ESpritePolygonMode::FullyCustom;

		// Render geometry describes the generated composite, not the source sprite. Retaining a copied
		// FullyCustom/tight polygon clips pixels when an included layer expands the canonical union. Build
		// one source-bounds rectangle for the complete managed cell while leaving collision on its own
		// translated preservation path.
		Render->Shapes.Reset();
		Render->GeometryType = ESpritePolygonMode::SourceBoundingBox;

		Sprite.SetPivotMode(ESpritePivotMode::Custom,
			FVector2D(CompositeFrame.SheetOrigin) + CompositeFrame.ManagedPivotLocal, false);
		Sprite.RebuildData();
		if (Collision) Collision->GeometryType = CollisionMode;
		SetDirty(Sprite.GetOutermost(), true);
		return true;
	}

	bool StagePixelPayload(
		FManagedAnimation& Animation,
		const FString& OperationDirectory,
		int32 Ordinal,
		FString& OutError)
	{
		const int64 ByteCount = static_cast<int64>(Animation.Plan.Composite.SheetPixels.Num()) * sizeof(FColor);
		if (ByteCount <= 0 || ByteCount > MAX_int32)
		{
			OutError = TEXT("Staged pixel payload is empty or too large.");
			return false;
		}
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Animation.Plan.Composite.SheetPixels.GetData()),
			static_cast<int32>(ByteCount));
		Animation.PixelHash = HashBytes(Bytes);
		Animation.PixelFile = OperationDirectory / FString::Printf(TEXT("animation_%04d.rgba"), Ordinal);
		if (!FFileHelper::SaveArrayToFile(Bytes, *Animation.PixelFile))
		{
			OutError = FString::Printf(TEXT("Could not spill staged pixels to '%s'."), *Animation.PixelFile);
			return false;
		}
		Animation.Plan.Composite.SheetPixels.Reset();
		return true;
	}

	bool LoadPixelPayload(const FManagedAnimation& Animation, TArray<uint8>& OutBytes, FString& OutError)
	{
		OutBytes.Reset();
		if (!FFileHelper::LoadFileToArray(OutBytes, *Animation.PixelFile)
			|| HashBytes(OutBytes) != Animation.PixelHash)
		{
			OutError = FString::Printf(TEXT("Staged pixel payload for '%s' is missing or corrupt."),
				*Animation.Plan.AnimationName);
			return false;
		}
		return true;
	}

	void AddTouchedPackage(TArray<FSoftObjectPath>& Out, UObject* Object)
	{
		if (!Object || !Object->GetOutermost()) return;
		const FSoftObjectPath Path(Object->GetOutermost()->GetName());
		Out.AddUnique(Path);
	}

	void AddTouchedPackagePath(TArray<FSoftObjectPath>& Out, const FSoftObjectPath& ObjectPath)
	{
		if (ObjectPath.IsNull()) return;
		const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath.ToString());
		if (!PackageName.IsEmpty()) Out.AddUnique(FSoftObjectPath(PackageName));
	}

	void SortSoftPaths(TArray<FSoftObjectPath>& Paths);

	void BuildActiveManifestPackageSet(
		UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const FSoftObjectPath& CharacterProfilePath,
		const TArray<FCharacterLayerAnimationBakeRecord>& Records,
		TArray<FSoftObjectPath>& OutPackages)
	{
		OutPackages.Reset();
		AddTouchedPackage(OutPackages, LayerAsset);
		AddTouchedPackagePath(OutPackages, CharacterProfilePath);
		for (const FCharacterLayerAnimationBakeRecord& Record : Records)
		{
			AddTouchedPackagePath(OutPackages, Record.Flipbook.ToSoftObjectPath());
			AddTouchedPackagePath(OutPackages, Record.ManagedTexture);
			for (const FSoftObjectPath& SpritePath : Record.ManagedSprites)
			{
				AddTouchedPackagePath(OutPackages, SpritePath);
			}
		}
		SortSoftPaths(OutPackages);
	}

	void DuplicateBaselineIntoProfile(
		const UPaper2DPlusCharacterProfileAsset& StagingProfile,
		UPaper2DPlusCharacterProfileAsset& Destination)
	{
		Destination.CharacterBaseline.Reset();
		Destination.CharacterBaseline.Reserve(StagingProfile.CharacterBaseline.Num());
		for (const FPaper2DPlusCharacterBaselineAnimation& Source : StagingProfile.CharacterBaseline)
		{
			FPaper2DPlusCharacterBaselineAnimation& Target = Destination.CharacterBaseline.AddDefaulted_GetRef();
			Target.Flipbook = Source.Flipbook;
			Target.LegacyAnimationName = Source.LegacyAnimationName;
			Target.Frames = Source.Frames;
			FPaper2DPlusFrameCueReplacementMap CueReplacements;
			for (const UPaper2DPlusCueBase* Cue : Source.FrameCues)
			{
				UPaper2DPlusCueBase* Duplicate =
					Cue ? DuplicateObject<UPaper2DPlusCueBase>(Cue, &Destination) : nullptr;
				Target.FrameCues.Add(Duplicate);
				if (Cue && Duplicate) CueReplacements.Add(Cue, Duplicate);
			}
			Target.CueTrackLayout = Source.CueTrackLayout;
			Target.CueTrackLayout.RemapCueReferences(CueReplacements);
			TSet<const UPaper2DPlusCueBase*> CueDomain;
			for (const UPaper2DPlusCueBase* Cue : Target.FrameCues)
			{
				if (Cue) CueDomain.Add(Cue);
			}
			Target.CueTrackLayout.RetainCueAssignments(CueDomain);
			for (const UPaper2DPlusFrameEventBase* Event : Source.LegacyFrameEvents)
				Target.LegacyFrameEvents.Add(Event
					? DuplicateObject<UPaper2DPlusFrameEventBase>(Event, &Destination) : nullptr);
		}
	}

	FGuid MakeCompiledLayerCueTrackId(const FGuid& LayerId, const FGuid& SourceTrackId)
	{
		const FString Seed = FString::Printf(
			TEXT("Paper2DPlus.FrameCue.CompiledTrack|%s|%s"),
			*LayerId.ToString(EGuidFormats::Digits),
			*SourceTrackId.ToString(EGuidFormats::Digits));
		FTCHARToUTF8 Utf8(*Seed);
		FMD5 Md5;
		Md5.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		uint8 Digest[16];
		Md5.Final(Digest);
		auto ReadWord = [&Digest](int32 Offset) -> uint32
		{
			return (static_cast<uint32>(Digest[Offset]) << 24)
				| (static_cast<uint32>(Digest[Offset + 1]) << 16)
				| (static_cast<uint32>(Digest[Offset + 2]) << 8)
				| static_cast<uint32>(Digest[Offset + 3]);
		};
		FGuid Result(ReadWord(0), ReadWord(4), ReadWord(8), ReadWord(12));
		if (!Result.IsValid())
		{
			// Preserve deterministic identity even for the astronomically unlikely all-zero digest.
			Result.D = 1;
		}
		return Result;
	}

	const FCharacterLayerAuthoredAnimationData* FindAuthoredAnimationForBakePlan(
		const FCharacterLayer& Layer,
		const FCharacterLayerBakeAnimationPlan& Plan)
	{
		const FCharacterLayerAuthoredAnimationData* Match = nullptr;
		for (const FCharacterLayerAuthoredAnimationData& Candidate : Layer.AuthoredAnimations)
		{
			const FSoftObjectPath CandidatePath = Candidate.Flipbook.ToSoftObjectPath();
			const bool bMatches = Plan.TargetFlipbookPath.IsValid() && CandidatePath.IsValid()
				? CandidatePath == Plan.TargetFlipbookPath
				: !Plan.AnimationName.IsEmpty()
					&& Candidate.LegacyAnimationName.Equals(
						Plan.AnimationName, ESearchCase::IgnoreCase);
			if (!bMatches)
			{
				continue;
			}
			if (Match)
			{
				return nullptr;
			}
			Match = &Candidate;
		}
		return Match;
	}

	bool AddCompiledCueTrackDefinition(
		FPaper2DPlusFrameCueTrackLayout& Layout,
		const FGuid& TrackId,
		const FString& PreferredLabel,
		const FString& StableCollisionSuffix)
	{
		if (!TrackId.IsValid() || Layout.IsTrackIdValid(TrackId))
		{
			return false;
		}

		FString Label = FPaper2DPlusFrameCueTrackLayout::NormalizeTrackName(PreferredLabel);
		if (Label.IsEmpty())
		{
			return false;
		}
		if (!Layout.IsTrackNameAvailable(Label))
		{
			if (StableCollisionSuffix.IsEmpty())
			{
				return false;
			}
			const FString Base = FString::Printf(
				TEXT("%s [%s]"), *Label, *StableCollisionSuffix);
			Label = Base;
			for (int32 Number = 2; !Layout.IsTrackNameAvailable(Label); ++Number)
			{
				Label = FString::Printf(TEXT("%s-%d"), *Base, Number);
			}
		}

		FGuid AddedTrackId;
		return Layout.AddTrack(Label, TrackId, AddedTrackId)
			&& AddedTrackId == TrackId;
	}

	struct FCompiledLayerCueTrackSource
	{
		const FCharacterLayer* Layer = nullptr;
		const FCharacterLayerAuthoredAnimationData* Animation = nullptr;
	};

	void BuildCompiledCueTrackDefinitions(
		const UPaper2DPlusCharacterLayerAsset& SourceLayerAsset,
		const UPaper2DPlusCharacterProfileAsset& SourceProfile,
		const FCharacterLayerBakeAnimationPlan& Plan,
		FPaper2DPlusFrameCueTrackLayout& OutLayout,
		TMap<FGuid, FGuid>& OutBaselineTrackIds,
		TMap<FGuid, TMap<FGuid, FGuid>>& OutLayerTrackIds)
	{
		OutLayout = FPaper2DPlusFrameCueTrackLayout();
		OutBaselineTrackIds.Reset();
		OutLayerTrackIds.Reset();

		if (const FPaper2DPlusCharacterBaselineAnimation* Baseline =
			SourceProfile.FindCharacterBaseline(Plan.TargetFlipbookPath, Plan.AnimationName))
		{
			for (const FPaper2DPlusFrameCueTrackDefinition& Track :
				Baseline->CueTrackLayout.OptionalTracks)
			{
				if (Baseline->CueTrackLayout.IsTrackIdValid(Track.TrackId)
					&& Baseline->CueTrackLayout.IsTrackNameAvailable(
						Track.DisplayName, Track.TrackId)
					&& AddCompiledCueTrackDefinition(
						OutLayout, Track.TrackId, Track.DisplayName, FString()))
				{
					OutBaselineTrackIds.Add(Track.TrackId, Track.TrackId);
				}
			}
		}

		TArray<FCompiledLayerCueTrackSource> Sources;
		TMap<FString, int32> LayerNameCounts;
		TSet<FGuid> SeenLayerIds;
		for (const FGuid& LayerId : Plan.IncludedLayerIds)
		{
			if (!LayerId.IsValid() || SeenLayerIds.Contains(LayerId))
			{
				continue;
			}
			SeenLayerIds.Add(LayerId);
			const FCharacterLayer* Layer = SourceLayerAsset.GetLayerById(LayerId);
			const FCharacterLayerAuthoredAnimationData* Animation = Layer
				? FindAuthoredAnimationForBakePlan(*Layer, Plan)
				: nullptr;
			if (!Layer || !Animation)
			{
				continue;
			}
			FCompiledLayerCueTrackSource& Source = Sources.AddDefaulted_GetRef();
			Source.Layer = Layer;
			Source.Animation = Animation;
			++LayerNameCounts.FindOrAdd(Layer->LayerName.ToLower());
		}

		for (const FCompiledLayerCueTrackSource& Source : Sources)
		{
			check(Source.Layer && Source.Animation);
			FString LayerLabel = Source.Layer->LayerName;
			if (LayerNameCounts.FindRef(Source.Layer->LayerName.ToLower()) > 1)
			{
				LayerLabel += FString::Printf(
					TEXT(" [%s]"),
					*Source.Layer->LayerId.ToString(EGuidFormats::Digits).Left(8));
			}

			TMap<FGuid, FGuid>& CompiledIds =
				OutLayerTrackIds.FindOrAdd(Source.Layer->LayerId);
			for (const FPaper2DPlusFrameCueTrackDefinition& Track :
				Source.Animation->CueTrackLayout.OptionalTracks)
			{
				if (!Source.Animation->CueTrackLayout.IsTrackIdValid(Track.TrackId)
					|| !Source.Animation->CueTrackLayout.IsTrackNameAvailable(
						Track.DisplayName, Track.TrackId))
				{
					continue;
				}
				const FGuid CompiledId = MakeCompiledLayerCueTrackId(
					Source.Layer->LayerId, Track.TrackId);
				const FString Label = FString::Printf(
					TEXT("%s / %s"), *LayerLabel, *Track.DisplayName);
				if (AddCompiledCueTrackDefinition(
					OutLayout,
					CompiledId,
					Label,
					CompiledId.ToString(EGuidFormats::Digits).Left(8)))
				{
					CompiledIds.Add(Track.TrackId, CompiledId);
				}
			}
		}
	}

	void PreserveStashedCueTrackMembership(
		UPaper2DPlusCharacterProfileAsset& DestinationProfile,
		const FFlipbookProfileEntry& DestinationEntry,
		FPaper2DPlusFrameCueTrackLayout& InOutLayout)
	{
		const FPaper2DPlusFrameCueTrackLayout& ExistingLayout =
			DestinationEntry.FrameEventData.CueTrackLayout;
		TMap<const UPaper2DPlusCueBase*, int32> Occurrences;
		for (const UPaper2DPlusCueBase* Cue : DestinationEntry.FrameEventData.FrameCues)
		{
			if (Cue) ++Occurrences.FindOrAdd(Cue);
		}
		for (const FExcludedFlipbookFrameData& Excluded :
			DestinationEntry.CombatData.ExcludedFrames)
		{
			for (const UPaper2DPlusCueBase* Cue : Excluded.StashedFrameCues)
			{
				if (Cue) ++Occurrences.FindOrAdd(Cue);
			}
		}

		TArray<TPair<UPaper2DPlusCueBase*, FGuid>> Assignments;
		TSet<FGuid> ReferencedTrackIds;
		for (const FExcludedFlipbookFrameData& Excluded :
			DestinationEntry.CombatData.ExcludedFrames)
		{
			for (UPaper2DPlusCueBase* Cue : Excluded.StashedFrameCues)
			{
				if (!Cue
					|| Cue->GetOuter() != &DestinationProfile
					|| Occurrences.FindRef(Cue) != 1)
				{
					continue;
				}
				const FGuid TrackId = ExistingLayout.ResolveStoredTrackId(Cue);
				if (TrackId.IsValid())
				{
					Assignments.Emplace(Cue, TrackId);
					ReferencedTrackIds.Add(TrackId);
				}
			}
		}

		// A stashed placement may be the sole remaining consumer of a track from an earlier
		// compiled source. Keep that definition after current baseline/layer definitions.
		for (const FPaper2DPlusFrameCueTrackDefinition& Track : ExistingLayout.OptionalTracks)
		{
			if (!ReferencedTrackIds.Contains(Track.TrackId)
				|| InOutLayout.IsTrackIdValid(Track.TrackId)
				|| !ExistingLayout.IsTrackIdValid(Track.TrackId)
				|| !ExistingLayout.IsTrackNameAvailable(Track.DisplayName, Track.TrackId))
			{
				continue;
			}
			AddCompiledCueTrackDefinition(
				InOutLayout,
				Track.TrackId,
				Track.DisplayName,
				Track.TrackId.ToString(EGuidFormats::Digits).Left(8));
		}

		for (const TPair<UPaper2DPlusCueBase*, FGuid>& Assignment : Assignments)
		{
			if (InOutLayout.IsTrackIdValid(Assignment.Value))
			{
				InOutLayout.AssignCue(Assignment.Key, Assignment.Value);
			}
		}
	}

	int32 CountCueOccurrences(
		const TArray<TObjectPtr<UPaper2DPlusCueBase>>& Cues,
		const UPaper2DPlusCueBase* Wanted)
	{
		int32 Count = 0;
		for (const UPaper2DPlusCueBase* Cue : Cues)
		{
			Count += Cue == Wanted ? 1 : 0;
		}
		return Count;
	}

	bool CompileFrameCuesAndTrackLayout(
		const UPaper2DPlusCharacterLayerAsset& SourceLayerAsset,
		const UPaper2DPlusCharacterProfileAsset& SourceProfile,
		UPaper2DPlusCharacterProfileAsset& DestinationProfile,
		const FCharacterLayerBakeAnimationPlan& Plan,
		FFlipbookProfileEntry& DestinationEntry,
		FString& OutError)
	{
		FPaper2DPlusFrameCueTrackLayout CompiledLayout;
		TMap<FGuid, FGuid> BaselineTrackIds;
		TMap<FGuid, TMap<FGuid, FGuid>> LayerTrackIds;
		BuildCompiledCueTrackDefinitions(
			SourceLayerAsset,
			SourceProfile,
			Plan,
			CompiledLayout,
			BaselineTrackIds,
			LayerTrackIds);
		PreserveStashedCueTrackMembership(
			DestinationProfile, DestinationEntry, CompiledLayout);

		TArray<TObjectPtr<UPaper2DPlusCueBase>> CompiledCues;
		CompiledCues.Reserve(Plan.OutputFrameCues.Num());
		int32 ProvenanceIndex = 0;
		const FPaper2DPlusCharacterBaselineAnimation* Baseline =
			SourceProfile.FindCharacterBaseline(Plan.TargetFlipbookPath, Plan.AnimationName);
		for (const UPaper2DPlusCueBase* SourceCue : Plan.OutputFrameCues)
		{
			if (!SourceCue
				|| !Plan.CueSources.IsValidIndex(ProvenanceIndex))
			{
				OutError = TEXT("Frame Cue bake provenance is incomplete at the commit boundary.");
				return false;
			}
			const FCharacterLayerBakeCueSource& Provenance =
				Plan.CueSources[ProvenanceIndex];
			if (Provenance.Cue.Get() != SourceCue
				|| Provenance.SourceOrder != ProvenanceIndex)
			{
				OutError = TEXT("Frame Cue bake provenance no longer matches authoritative output order.");
				return false;
			}

			UPaper2DPlusCueBase* Duplicate =
				DuplicateObject<UPaper2DPlusCueBase>(SourceCue, &DestinationProfile);
			if (!Duplicate)
			{
				OutError = TEXT("A compiled Frame Cue placement could not be duplicated.");
				return false;
			}
			CompiledCues.Add(Duplicate);

			FGuid CompiledTrackId;
			if (Provenance.bCharacterBaseline)
			{
				if (Baseline
					&& SourceCue->GetOuter() == &SourceProfile
					&& CountCueOccurrences(Baseline->FrameCues, SourceCue) == 1)
				{
					const FGuid SourceTrackId =
						Baseline->CueTrackLayout.ResolveStoredTrackId(SourceCue);
					if (const FGuid* Mapped = BaselineTrackIds.Find(SourceTrackId))
					{
						CompiledTrackId = *Mapped;
					}
				}
			}
			else if (const FCharacterLayer* Layer =
				SourceLayerAsset.GetLayerById(Provenance.LayerId))
			{
				const FCharacterLayerAuthoredAnimationData* Animation =
					FindAuthoredAnimationForBakePlan(*Layer, Plan);
				if (Animation
					&& SourceCue->GetOuter() == &SourceLayerAsset
					&& CountCueOccurrences(Animation->FrameCues, SourceCue) == 1)
				{
					const FGuid SourceTrackId =
						Animation->CueTrackLayout.ResolveStoredTrackId(SourceCue);
					if (const TMap<FGuid, FGuid>* LayerIds =
						LayerTrackIds.Find(Provenance.LayerId))
					{
						if (const FGuid* Mapped = LayerIds->Find(SourceTrackId))
						{
							CompiledTrackId = *Mapped;
						}
					}
				}
			}
			if (CompiledTrackId.IsValid())
			{
				CompiledLayout.AssignCue(Duplicate, CompiledTrackId);
			}
			++ProvenanceIndex;
		}

		if (ProvenanceIndex != Plan.CueSources.Num())
		{
			OutError = TEXT("Frame Cue bake provenance contains rows without compiled output.");
			return false;
		}
		DestinationEntry.FrameEventData.FrameCues = MoveTemp(CompiledCues);
		DestinationEntry.FrameEventData.CueTrackLayout = MoveTemp(CompiledLayout);
		return true;
	}

	struct FOperationDirectoryGuard
	{
		FString Directory;
		~FOperationDirectoryGuard()
		{
			if (!Directory.IsEmpty()) TryDeleteSafeStagingTree(Directory);
		}
	};

	bool VerifyMetaSnapshot(const FMetaSnapshot& Snapshot)
	{
		if (!Snapshot.Object) return false;
		for (const FMetaEntrySnapshot& Entry : Snapshot.Entries)
		{
			bool bFound = false;
			const FString Value = ReadMeta(Snapshot.Object, Entry.Key, &bFound);
			if (bFound != Entry.bHadValue || (bFound && Value != Entry.Value)) return false;
		}
		return true;
	}

	bool RollbackCommit(
		UPaper2DPlusCharacterLayerAsset& Asset,
		UPaper2DPlusCharacterProfileAsset& Profile,
		const FCommitSnapshot& Snapshot,
		TArray<FNewObjectRecord>& NewObjects,
		ECharacterLayerBakeFailurePoint Injection)
	{
		Snapshot.Layer.Restore(Asset);
		Snapshot.Profile.Restore(Profile);
		for (int32 Index = Snapshot.Flipbooks.Num() - 1; Index >= 0; --Index)
			Snapshot.Flipbooks[Index].Restore();
		for (int32 Index = Snapshot.Sprites.Num() - 1; Index >= 0; --Index)
			Snapshot.Sprites[Index].Restore();
		for (int32 Index = Snapshot.Textures.Num() - 1; Index >= 0; --Index)
			Snapshot.Textures[Index].Restore();
		for (int32 Index = Snapshot.Metadata.Num() - 1; Index >= 0; --Index)
			Snapshot.Metadata[Index].Restore();

		bool bRemovedNewObjects = true;
		for (int32 Index = NewObjects.Num() - 1; Index >= 0; --Index)
		{
			UObject* Object = NewObjects[Index].Object;
			UPackage* Package = NewObjects[Index].Package;
			if (!Object || !Package) continue;
			for (FName Key : { OwnerKey, RevisionKey, RoleKey, TargetKey })
			{
				RemoveMetadataValue(Package, Object, Key);
				RemoveMetadataValue(Package, Package, Key);
			}
			UnregisterCreatedAsset(Object);
			Object->ClearFlags(RF_Public | RF_Standalone);
			const bool bRenamed = Object->Rename(
				nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			bRemovedNewObjects &= bRenamed;
			SetDirty(Package, false);
		}
		for (const TPair<UPackage*, bool>& Pair : Snapshot.DirtyStates)
			SetDirty(Pair.Key, Pair.Value);

		bool bVerified = bRemovedNewObjects
			&& SerializeObjectSemantic(&Asset) == Snapshot.Layer.SemanticDigest
			&& SerializeObjectSemantic(&Profile) == Snapshot.Profile.SemanticDigest;
		for (const FFlipbookSnapshot& Row : Snapshot.Flipbooks)
			bVerified &= Row.Flipbook && SerializeObjectSemantic(Row.Flipbook) == Row.SemanticDigest;
		for (const FTextureSnapshot& Row : Snapshot.Textures) bVerified &= Row.Verify();
		for (const FSpriteSnapshot& Row : Snapshot.Sprites) bVerified &= Row.Verify();
		for (const FMetaSnapshot& Row : Snapshot.Metadata) bVerified &= VerifyMetaSnapshot(Row);
		for (const TPair<UPackage*, bool>& Pair : Snapshot.DirtyStates)
			bVerified &= Pair.Key && Pair.Key->IsDirty() == Pair.Value;
		if (Injection == ECharacterLayerBakeFailurePoint::RollbackVerification) bVerified = false;
		return bVerified;
	}

	FCharacterLayerAnimationBakeRecord* FindRecordMutable(
		FCharacterLayerBakeManifest& Manifest,
		UPaperFlipbook* Flipbook,
		const FString& LegacyName)
	{
		return const_cast<FCharacterLayerAnimationBakeRecord*>(FindRecord(Manifest, Flipbook, LegacyName));
	}

	void SortSoftPaths(TArray<FSoftObjectPath>& Paths)
	{
		Paths.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B)
		{
			return A.ToString().Compare(B.ToString(), ESearchCase::IgnoreCase) < 0;
		});
	}

	ECharacterLayerBakeLifecycleOperation LifecycleOperationFor(
		ECharacterLayerBakeOperation Operation)
	{
		switch (Operation)
		{
		case ECharacterLayerBakeOperation::Current:
			return ECharacterLayerBakeLifecycleOperation::BakeCurrent;
		case ECharacterLayerBakeOperation::AdoptAndBakeAll:
			return ECharacterLayerBakeLifecycleOperation::AdoptAndBakeAll;
		default:
			return ECharacterLayerBakeLifecycleOperation::BakeAll;
		}
	}

	FCharacterLayerBakeOperationRecord MakeOperationRecord(
		ECharacterLayerBakeLifecycleOperation Operation,
		const FString& Report,
		bool bSuccess)
	{
		FCharacterLayerBakeOperationRecord Record;
		Record.Operation = Operation;
		Record.CompletedUtc = FDateTime::UtcNow();
		Record.bSuccess = bSuccess;
		Record.Report = Report;
		return Record;
	}

	bool CollectFreezeOutputObjects(
		const UPaper2DPlusCharacterLayerAsset& Asset,
		TArray<UObject*>& OutStampedObjects,
		TArray<FString>& OutErrors,
		bool& bOutCompetingOwnership)
	{
		OutStampedObjects.Reset();
		OutErrors.Reset();
		bOutCompetingOwnership = false;
		if (!Asset.BakeSetId.IsValid() || Asset.BakeManifest.IsEmpty())
		{
			OutErrors.Add(TEXT("No retained bake manifest exists to prove the canonical output set."));
			return false;
		}
		const FString ExpectedOwner = GuidText(Asset.BakeSetId);
		auto Inspect = [&](UObject* Object, const FString& Description) -> bool
		{
			if (!Object)
			{
				OutErrors.Add(FString::Printf(TEXT("%s is missing."), *Description));
				return false;
			}
			bool bFound = false;
			const FString Owner = ReadMeta(Object, OwnerKey, &bFound);
			if (bFound && !Owner.Equals(ExpectedOwner, ESearchCase::IgnoreCase))
			{
				bOutCompetingOwnership = true;
				OutErrors.Add(FString::Printf(TEXT("%s is owned by competing bake set '%s'."),
					*Description, *Owner));
				return false;
			}
			if (bFound) OutStampedObjects.AddUnique(Object);
			return true;
		};

		bool bValid = true;
		for (const FCharacterLayerAnimationBakeRecord& Record : Asset.BakeManifest.Animations)
		{
			const FString Animation = Record.LegacyAnimationName.IsEmpty()
				? Record.Flipbook.ToSoftObjectPath().ToString() : Record.LegacyAnimationName;
			UPaperFlipbook* Flipbook = Record.Flipbook.LoadSynchronous();
			UObject* Texture = ResolveSoftObjectQuietly(Record.ManagedTexture);
			bValid &= Inspect(Flipbook, FString::Printf(TEXT("Flipbook for '%s'"), *Animation));
			bValid &= Inspect(Texture, FString::Printf(TEXT("Managed texture for '%s'"), *Animation));
			if (Texture)
			{
				bValid &= Inspect(Texture->GetOutermost(), FString::Printf(
					TEXT("Managed texture package for '%s'"), *Animation));
			}
			for (int32 SpriteIndex = 0; SpriteIndex < Record.ManagedSprites.Num(); ++SpriteIndex)
			{
				const FSoftObjectPath& SpritePath = Record.ManagedSprites[SpriteIndex];
				if (SpritePath.IsNull()) continue;
				UObject* Sprite = ResolveSoftObjectQuietly(SpritePath);
				bValid &= Inspect(Sprite, FString::Printf(
					TEXT("Managed sprite %d for '%s'"), SpriteIndex, *Animation));
				if (Sprite)
				{
					bValid &= Inspect(Sprite->GetOutermost(), FString::Printf(
						TEXT("Managed sprite package %d for '%s'"), SpriteIndex, *Animation));
				}
			}
		}
		return bValid && !bOutCompetingOwnership;
	}
}

FCharacterLayerBakeResult CharacterLayerBakeCoordinator::Execute(const FCharacterLayerBakeRequest& Request)
{
	using namespace Paper2DPlusCharacterLayerBakeCoordinatorPrivate;
	FCharacterLayerBakeResult Result;
	// A future writer may have changed both digest meaning and manifest invariants. Reject before the audit
	// scope is constructed so even the presentation-only operation record and package dirty state stay intact.
	if (HasFutureBakeManifest(Request.LayerAsset))
	{
		Result.Report = FutureBakeManifestReport(*Request.LayerAsset);
		Result.Errors.Add(Result.Report);
		return Result;
	}
	struct FExecuteAuditScope
	{
		UPaper2DPlusCharacterLayerAsset* Asset = nullptr;
		ECharacterLayerBakeLifecycleOperation Operation = ECharacterLayerBakeLifecycleOperation::None;
		FCharacterLayerBakeResult* Result = nullptr;
		~FExecuteAuditScope()
		{
			if (!Asset || !Result) return;
			if (Result->bSuccess && Asset->LastBakeOperation.Operation == Operation) return;
			Asset->LastBakeOperation = MakeOperationRecord(Operation, Result->Report, Result->bSuccess);
			Asset->LastBakeOperation.bCancelled = Result->bCancelled;
			Asset->LastBakeOperation.bRolledBack = Result->bRolledBack;
			Asset->LastBakeOperation.bRecoveryRequired = Result->bRecoveryRequired;
			Asset->LastBakeOperation.TouchedPackages = Result->TouchedPackages;
			Asset->MarkPackageDirty();
		}
	} AuditScope { Request.LayerAsset, LifecycleOperationFor(Request.Operation), &Result };
	CleanupAbandonedStaging();
	auto FailBeforeMutation = [&Result](const FString& Message) -> FCharacterLayerBakeResult
	{
		Result.Errors.Add(Message);
		Result.Report = Message;
		return Result;
	};
	UPaper2DPlusCharacterLayerAsset* Asset = Request.LayerAsset;
	UPaper2DPlusCharacterProfileAsset* Profile = Request.CharacterProfile;
	if (!Asset || !Profile)
		return FailBeforeMutation(TEXT("A loaded Character Layer Asset and Character Profile are required."));
	if (Asset->BakeAttachmentState == ECharacterLayerBakeAttachmentState::RecoveryRequired)
		return FailBeforeMutation(TEXT("This Layer Asset is Recovery Required. Repair or detach it before another bake."));
	if (!Asset->BaseProfile.IsNull()
		&& Asset->BaseProfile.Get() != Profile
		&& Asset->BaseProfile.ToSoftObjectPath() != FSoftObjectPath(Profile->GetPathName()))
		return FailBeforeMutation(TEXT("The supplied Character Profile is not this Layer Asset's Base Profile."));

	const bool bFirstPublish = Asset->BakeAttachmentState == ECharacterLayerBakeAttachmentState::Unclaimed;
	FGuid BakeSetId = Asset->BakeSetId;
	if (!BakeSetId.IsValid()) BakeSetId = FGuid::NewGuid();
	TArray<FCharacterLayerAnimationRegistration> Registration;
	FString Error;

	TStrongObjectPtr<UPaper2DPlusCharacterLayerAsset> StagingLayer;
	TStrongObjectPtr<UPaper2DPlusCharacterProfileAsset> StagingProfile;
	UPaper2DPlusCharacterLayerAsset* PlanLayer = Asset;
	UPaper2DPlusCharacterProfileAsset* PlanProfile = Profile;

	if (bFirstPublish)
	{
		if (Request.Operation != ECharacterLayerBakeOperation::All || !Asset->BakeManifest.IsEmpty())
			return FailBeforeMutation(TEXT("A first fixed bake must publish the complete unclaimed layer set."));
		if (Profile->LayerBakeOwnerToken.IsValid())
			return FailBeforeMutation(TEXT("The Character Profile is already claimed by another Layer bake set."));
		if (!CaptureRegistration(Profile, Registration, Error)) return FailBeforeMutation(Error);

		StagingLayer.Reset(DuplicateObject<UPaper2DPlusCharacterLayerAsset>(Asset, GetTransientPackage()));
		StagingProfile.Reset(DuplicateObject<UPaper2DPlusCharacterProfileAsset>(Profile, GetTransientPackage()));
		if (!StagingLayer.IsValid() || !StagingProfile.IsValid())
			return FailBeforeMutation(TEXT("Could not create mutation-free first-publish staging objects."));
		PlanLayer = StagingLayer.Get();
		PlanProfile = StagingProfile.Get();
		PlanLayer->BakeSetId = BakeSetId;
		PlanLayer->BakeAttachmentState = ECharacterLayerBakeAttachmentState::Attached;
		PlanLayer->UsageMode = ECharacterLayerUsageMode::FixedBaked;
		PlanLayer->AnimationRegistration = Registration;
		PlanLayer->BaseProfile = PlanProfile;
		if (!PlanProfile->CaptureCharacterBaselineFromRuntime(BakeSetId, Asset->GetPathName(), true))
			return FailBeforeMutation(TEXT("Could not capture the Character Baseline for the first fixed bake."));
	}
	else
	{
		if (Asset->BakeAttachmentState != ECharacterLayerBakeAttachmentState::Attached
			|| Asset->UsageMode != ECharacterLayerUsageMode::FixedBaked
			|| !BakeSetId.IsValid() || Profile->LayerBakeOwnerToken != BakeSetId)
			return FailBeforeMutation(TEXT("Ordinary bake requires an attached, exclusively claimed fixed-baked asset."));
		if (Asset->BakeManifest.IsEmpty() || Asset->BakeManifest.BakeSetId != BakeSetId)
			return FailBeforeMutation(TEXT("Attached canonical output has no trustworthy manifest."));
		Registration = Asset->AnimationRegistration;
		if (Registration.IsEmpty()) return FailBeforeMutation(TEXT("Canonical registration is missing."));
	}
	if (Request.Operation == ECharacterLayerBakeOperation::Current
		&& Asset->BakeManifest.ManifestVersion < Paper2DPlusLayerBakeVersion::CurrentManifestVersion)
	{
		return FailBeforeMutation(FString::Printf(
			TEXT("Bake Current cannot upgrade bake-manifest schema version %u to %u while other animation records remain untouched. Run Bake All to upgrade the complete bake set together."),
			Asset->BakeManifest.ManifestVersion,
			Paper2DPlusLayerBakeVersion::CurrentManifestVersion));
	}
	if (Request.Operation == ECharacterLayerBakeOperation::Current
		&& Asset->BakeManifest.DigestVersion < CharacterLayerBakeCore::CurrentDigestVersion)
	{
		return FailBeforeMutation(FString::Printf(
			TEXT("Bake Current cannot upgrade digest version %u to %u while other animation records remain untouched. Run Bake All to upgrade the complete bake set together."),
			Asset->BakeManifest.DigestVersion,
			CharacterLayerBakeCore::CurrentDigestVersion));
	}

	if (!CheckSharedFlipbooks(*Asset, Registration, Error)) return FailBeforeMutation(Error);
	for (const FCharacterLayerAnimationRegistration& Row : Registration)
	{
		UPaperFlipbook* Flipbook = Row.Flipbook.LoadSynchronous();
		if (!Flipbook) return FailBeforeMutation(TEXT("A registered canonical flipbook is missing."));
		bool bHasOwner = false;
		const FString ExistingOwner = ReadMeta(Flipbook, OwnerKey, &bHasOwner);
		if (bFirstPublish)
		{
			if (bHasOwner)
				return FailBeforeMutation(FString::Printf(TEXT("Canonical flipbook '%s' is already owner-stamped by bake set '%s'."),
					*Flipbook->GetPathName(), *ExistingOwner));
		}
		else if (!ValidateOwner(Flipbook, BakeSetId, Error))
		{
			const bool bNewRebaseTarget = Request.bOverwriteManagedOutputConflict
				&& FindRecord(Asset->BakeManifest, Flipbook, Row.LegacyAnimationName) == nullptr;
			if (!bNewRebaseTarget || bHasOwner)
			{
				return FailBeforeMutation(Error);
			}
		}
	}

	TArray<int32> RegistrationIndices;
	if (Request.Operation == ECharacterLayerBakeOperation::Current)
	{
		if (!Registration.IsValidIndex(Request.CurrentRegistrationIndex))
			return FailBeforeMutation(TEXT("Bake Current has no valid selected canonical animation."));
		RegistrationIndices.Add(Request.CurrentRegistrationIndex);
	}
	else
	{
		for (int32 Index = 0; Index < Registration.Num(); ++Index) RegistrationIndices.Add(Index);
	}

	FOperationDirectoryGuard StagingGuard;
	StagingGuard.Directory = StagingRoot() / FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (!IFileManager::Get().MakeDirectory(*StagingGuard.Directory, true))
		return FailBeforeMutation(TEXT("Could not create operation-scoped Layer bake staging storage."));

	TArray<FManagedAnimation> Animations;
	Animations.Reserve(RegistrationIndices.Num());
	for (int32 Ordinal = 0; Ordinal < RegistrationIndices.Num(); ++Ordinal)
	{
		if (Request.IsCancellationRequested && Request.IsCancellationRequested())
		{
			Result.bCancelled = true;
			Result.Report = TEXT("Layer bake cancelled during mutation-free staging.");
			return Result;
		}
		FManagedAnimation& Animation = Animations.AddDefaulted_GetRef();
		Animation.Plan = CharacterLayerBakeCore::BuildAnimationPlan(
			PlanLayer, PlanProfile, RegistrationIndices[Ordinal]);
		if (!Animation.Plan.CanCommit())
		{
			for (const FCharacterLayerBakeDiagnostic& Diagnostic : Animation.Plan.Diagnostics)
				if (Diagnostic.Severity == ECharacterLayerBakeDiagnosticSeverity::Error)
					Result.Errors.Add(Diagnostic.Message);
			Result.Report = Result.Errors.IsEmpty()
				? TEXT("Canonical compiler rejected the staged animation.")
				: FString::Join(Result.Errors, TEXT(" "));
			return Result;
		}
		Animation.Plan.ExpectedTargetPreconditionDigest = CharacterLayerBakeCore::ComputeTargetPreconditionDigest(
			Profile, Animation.Plan.TargetFlipbook.Get());
		if (Animation.Plan.ExpectedTargetPreconditionDigest.IsEmpty())
			return FailBeforeMutation(TEXT("Could not capture a target mutation precondition."));
		if (!StagePixelPayload(Animation, StagingGuard.Directory, Ordinal, Error))
			return FailBeforeMutation(Error);
		if (!PreflightManagedAnimation(Request, Animation, Error))
			return FailBeforeMutation(Error);
	}

	if (Request.IsCancellationRequested && Request.IsCancellationRequested())
	{
		Result.bCancelled = true;
		Result.Report = TEXT("Layer bake cancelled before commit.");
		return Result;
	}

	// Last mutation-free pass. Source recipes, output recipes, target state, ownership, and payload hashes must
	// all still match. Cancellation is intentionally ignored after this boundary.
	for (int32 Ordinal = 0; Ordinal < Animations.Num(); ++Ordinal)
	{
		FCharacterLayerBakeAnimationPlan Fresh = CharacterLayerBakeCore::BuildAnimationPlan(
			PlanLayer, PlanProfile, RegistrationIndices[Ordinal]);
		const FManagedAnimation& Staged = Animations[Ordinal];
		if (!Fresh.CanCommit() || Fresh.SourceDigest != Staged.Plan.SourceDigest
			|| Fresh.OutputDigest != Staged.Plan.OutputDigest
			|| CharacterLayerBakeCore::ComputeTargetPreconditionDigest(
				Profile, Staged.Plan.TargetFlipbook.Get()) != Staged.Plan.ExpectedTargetPreconditionDigest)
		{
			return FailBeforeMutation(TEXT("Layer source or canonical target drifted during staging; no output was changed."));
		}
		TArray<uint8> VerifiedPayload;
		if (!LoadPixelPayload(Staged, VerifiedPayload, Error)) return FailBeforeMutation(Error);
		FManagedAnimation Recheck = Staged;
		if (!PreflightManagedAnimation(Request, Recheck, Error)) return FailBeforeMutation(Error);
	}

	FCommitSnapshot Snapshot;
	if (!Snapshot.Capture(*Asset, *Profile, Animations, Error)) return FailBeforeMutation(Error);
	TArray<FNewObjectRecord> NewObjects;
	TArray<TStrongObjectPtr<UObject>> StrongManagedReferences;
	const uint32 NextRevision = FMath::Max<uint32>(1, Asset->BakeManifest.BakeRevision + 1);
	auto RollbackFailure = [&](const FString& Message) -> FCharacterLayerBakeResult
	{
		Result.Errors.Add(Message);
		Result.bRolledBack = true;
		const bool bRollbackVerified = RollbackCommit(
			*Asset, *Profile, Snapshot, NewObjects, Request.InjectFailure);
		if (!bRollbackVerified)
		{
			Asset->BakeAttachmentState = ECharacterLayerBakeAttachmentState::RecoveryRequired;
			SetDirty(Asset->GetOutermost(), true);
			Result.bRecoveryRequired = true;
			Result.Report = Message + TEXT(" Rollback verification failed; the asset is now Recovery Required.");
		}
		else
		{
			Result.Report = Message + TEXT(" Every mutation was rolled back and verified.");
		}
		return Result;
	};
	auto Injected = [&Request](ECharacterLayerBakeFailurePoint Point)
	{
		return Request.InjectFailure == Point
			|| (Request.InjectFailure == ECharacterLayerBakeFailurePoint::RollbackVerification
				&& Point == ECharacterLayerBakeFailurePoint::AfterManagedObjects);
	};

	Asset->Modify();
	Profile->Modify();
	if (bFirstPublish)
	{
		Asset->AnimationRegistration = Registration;
	}
	if (Injected(ECharacterLayerBakeFailurePoint::AfterAdoption))
		return RollbackFailure(TEXT("Injected failure after adoption/source mutation."));

	for (FManagedAnimation& Animation : Animations)
	{
		if (!Animation.Texture)
		{
			bool bCreated = false;
			Animation.Texture = Cast<UTexture2D>(CreateManagedObject(
				UTexture2D::StaticClass(), Animation.TexturePath, bCreated, Error));
			Animation.bTextureWasNew = bCreated;
			if (!Animation.Texture) return RollbackFailure(Error);
			if (bCreated)
			{
				FNewObjectRecord& New = NewObjects.AddDefaulted_GetRef();
				New.Object = Animation.Texture;
				New.Package = Animation.Texture->GetOutermost();
				Result.CreatedAssets.Add(Animation.TexturePath.ToSoftPath());
			}
		}
		StrongManagedReferences.Emplace(Animation.Texture);
		StampObject(Animation.Texture, BakeSetId, NextRevision, TEXT("AnimationSheet"),
			Animation.Plan.TargetFlipbookPath.ToString());
		StampObject(Animation.Texture->GetOutermost(), BakeSetId, NextRevision, TEXT("GeneratedPackage"),
			Animation.Plan.TargetFlipbookPath.ToString());
		for (int32 FrameIndex = 0; FrameIndex < Animation.Plan.KeyFrameCount; ++FrameIndex)
		{
			if (!Animation.bSpriteNeeded[FrameIndex] || Animation.Sprites[FrameIndex]) continue;
			bool bCreated = false;
			Animation.Sprites[FrameIndex] = Cast<UPaperSprite>(CreateManagedObject(
				UPaperSprite::StaticClass(), Animation.SpritePaths[FrameIndex], bCreated, Error));
			Animation.bSpriteWasNew[FrameIndex] = bCreated;
			if (!Animation.Sprites[FrameIndex]) return RollbackFailure(Error);
			if (bCreated)
			{
				FNewObjectRecord& New = NewObjects.AddDefaulted_GetRef();
				New.Object = Animation.Sprites[FrameIndex];
				New.Package = Animation.Sprites[FrameIndex]->GetOutermost();
				Result.CreatedAssets.Add(Animation.SpritePaths[FrameIndex].ToSoftPath());
			}
		}
		for (UPaperSprite* Sprite : Animation.Sprites)
		{
			if (!Sprite) continue;
			StrongManagedReferences.Emplace(Sprite);
			StampObject(Sprite, BakeSetId, NextRevision, TEXT("FrameSprite"),
				Animation.Plan.TargetFlipbookPath.ToString());
			StampObject(Sprite->GetOutermost(), BakeSetId, NextRevision, TEXT("GeneratedPackage"),
				Animation.Plan.TargetFlipbookPath.ToString());
		}
	}
	if (Injected(ECharacterLayerBakeFailurePoint::AfterManagedObjects))
		return RollbackFailure(TEXT("Injected failure after managed object creation."));

	for (FManagedAnimation& Animation : Animations)
	{
		TArray<uint8> Pixels;
		if (!LoadPixelPayload(Animation, Pixels, Error)
			|| !WriteTexture(*Animation.Texture, Animation.Plan, Pixels))
			return RollbackFailure(Error.IsEmpty() ? TEXT("Could not materialize a managed texture.") : Error);
	}
	if (Injected(ECharacterLayerBakeFailurePoint::AfterTexture))
		return RollbackFailure(TEXT("Injected failure after managed texture mutation."));

	for (FManagedAnimation& Animation : Animations)
	{
		for (int32 FrameIndex = 0; FrameIndex < Animation.Plan.KeyFrameCount; ++FrameIndex)
		{
			if (!Animation.bSpriteNeeded[FrameIndex]) continue;
			if (!Animation.Sprites[FrameIndex]
				|| !MaterializeSprite(*Animation.Sprites[FrameIndex], *Animation.Texture,
					Animation.Plan, FrameIndex, Error))
				return RollbackFailure(Error.IsEmpty() ? TEXT("Could not materialize a managed sprite.") : Error);
		}
	}
	if (Injected(ECharacterLayerBakeFailurePoint::AfterSprites))
		return RollbackFailure(TEXT("Injected failure after managed sprite mutation."));

	for (FManagedAnimation& Animation : Animations)
	{
		UPaperFlipbook* Flipbook = Animation.Plan.TargetFlipbook.Get();
		if (!Flipbook || Flipbook->GetNumKeyFrames() != Animation.Plan.KeyFrameCount
			|| !FMath::IsNearlyEqual(Flipbook->GetFramesPerSecond(), Animation.Plan.FramesPerSecond))
			return RollbackFailure(TEXT("Canonical flipbook timing drifted at the commit boundary."));
		Flipbook->Modify();
		{
			FScopedFlipbookMutator Mutator(Flipbook);
			for (int32 FrameIndex = 0; FrameIndex < Mutator.KeyFrames.Num(); ++FrameIndex)
			{
				if (Mutator.KeyFrames[FrameIndex].FrameRun != Animation.Plan.FrameRuns[FrameIndex])
					return RollbackFailure(TEXT("Canonical FrameRun drifted at the commit boundary."));
				Mutator.KeyFrames[FrameIndex].Sprite = Animation.bSpriteNeeded[FrameIndex]
					? Animation.Sprites[FrameIndex] : nullptr;
			}
		}
		StampObject(Flipbook, BakeSetId, NextRevision, TEXT("CanonicalFlipbook"), Asset->GetPathName());
		SetDirty(Flipbook->GetOutermost(), true);
	}
	if (Injected(ECharacterLayerBakeFailurePoint::AfterFlipbook))
		return RollbackFailure(TEXT("Injected failure after canonical flipbook mutation."));

	if (bFirstPublish) DuplicateBaselineIntoProfile(*PlanProfile, *Profile);
	for (FManagedAnimation& Animation : Animations)
	{
		UPaperFlipbook* Flipbook = Animation.Plan.TargetFlipbook.Get();
		FFlipbookProfileEntry* Entry = FindProfileEntryMutable(*Profile, Flipbook, Animation.Plan.AnimationName);
		if (!Entry) return RollbackFailure(TEXT("Profile animation became ambiguous during commit."));
		Entry->CombatData.Frames = Animation.Plan.OutputGameplayFrames;
		if (!CompileFrameCuesAndTrackLayout(
			*PlanLayer, *PlanProfile, *Profile, Animation.Plan, *Entry, Error))
		{
			return RollbackFailure(Error.IsEmpty()
				? TEXT("Could not materialize compiled Frame Cue organization.")
				: Error);
		}
		Entry->FrameEventData.FrameEvents.Reset();
		for (const UPaper2DPlusFrameEventBase* Event : Animation.Plan.BaselineLegacyFrameEvents)
			Entry->FrameEventData.FrameEvents.Add(Event
				? DuplicateObject<UPaper2DPlusFrameEventBase>(Event, Profile) : nullptr);
	}
	SetDirty(Profile->GetOutermost(), true);
	if (Injected(ECharacterLayerBakeFailurePoint::AfterProfile))
		return RollbackFailure(TEXT("Injected failure after Character Profile output mutation."));

	Asset->BakeSetId = BakeSetId;
	Asset->BakeAttachmentState = ECharacterLayerBakeAttachmentState::Attached;
	Asset->UsageMode = ECharacterLayerUsageMode::FixedBaked;
	Profile->LayerBakeOwnerToken = BakeSetId;
	Profile->LayerBakeOwnerPathHint = Asset->GetPathName();
	if (Injected(ECharacterLayerBakeFailurePoint::AfterClaim))
		return RollbackFailure(TEXT("Injected failure after exclusive claim mutation."));

	FCharacterLayerBakeManifest NewManifest = Snapshot.Layer.Manifest;
	NewManifest.ManifestVersion = Paper2DPlusLayerBakeVersion::CurrentManifestVersion;
	NewManifest.DigestVersion = CharacterLayerBakeCore::CurrentDigestVersion;
	NewManifest.BakeSetId = BakeSetId;
	NewManifest.BakeRevision = NextRevision;
	NewManifest.CharacterProfilePathHint = Profile->GetPathName();
	for (FManagedAnimation& Animation : Animations)
	{
		UPaperFlipbook* Flipbook = Animation.Plan.TargetFlipbook.Get();
		FCharacterLayerAnimationBakeRecord* Record = FindRecordMutable(
			NewManifest, Flipbook, Animation.Plan.AnimationName);
		if (!Record)
		{
			Record = &NewManifest.Animations.AddDefaulted_GetRef();
			Record->Flipbook = Flipbook;
			Record->LegacyAnimationName = Animation.Plan.AnimationName;
		}
		Record->SourceDigest = Animation.Plan.SourceDigest;
		Record->OutputRevision = NextRevision;
		Record->ManagedTexture = Animation.TexturePath.ToSoftPath();
		Record->ManagedSprites.SetNum(Animation.Plan.KeyFrameCount);
		for (int32 FrameIndex = 0; FrameIndex < Animation.Plan.KeyFrameCount; ++FrameIndex)
		{
			if (!Animation.SpritePaths[FrameIndex].ObjectPath.IsEmpty())
				Record->ManagedSprites[FrameIndex] = Animation.SpritePaths[FrameIndex].ToSoftPath();
		}
		FString DigestError;
		Record->OutputDigest = ComputeCurrentOutputDigest(Asset, Profile, *Record, &DigestError);
		if (Record->OutputDigest.IsEmpty())
			return RollbackFailure(DigestError.IsEmpty()
				? TEXT("Could not verify materialized output.") : DigestError);
	}

	// Save Bake Set owns the complete current bake set, not just the most recently selected animation.
	// Rebuild active ownership from every record, then retain any prior pending paths. Rebase puts retired/
	// frozen packages in that prior set so their metadata removals cannot be lost by a rebake before Save.
	const TArray<FSoftObjectPath> PriorPendingPackages = NewManifest.TouchedPackages;
	BuildActiveManifestPackageSet(
		Asset,
		FSoftObjectPath(Profile),
		NewManifest.Animations,
		NewManifest.TouchedPackages);
	for (const FSoftObjectPath& PendingPackage : PriorPendingPackages)
	{
		NewManifest.TouchedPackages.AddUnique(PendingPackage);
	}
	SortSoftPaths(NewManifest.TouchedPackages);
	NewManifest.LastBakeUtc = FDateTime::UtcNow();
	NewManifest.LastReport = FString::Printf(TEXT("Baked %d animation(s) at revision %u."),
		Animations.Num(), NextRevision);
	Asset->LastBakeOperation = MakeOperationRecord(
		LifecycleOperationFor(Request.Operation), NewManifest.LastReport, true);
	Asset->LastBakeOperation.TouchedPackages = NewManifest.TouchedPackages;
	Asset->BakeManifest = NewManifest; // consistency record is deliberately last
	SetDirty(Asset->GetOutermost(), true);
	if (Injected(ECharacterLayerBakeFailurePoint::AfterManifest))
		return RollbackFailure(TEXT("Injected failure after manifest mutation."));

	Result.bSuccess = true;
	Result.Report = NewManifest.LastReport;
	Result.TouchedPackages = NewManifest.TouchedPackages;
	return Result;
}

FCharacterLayerBakeSaveResult CharacterLayerBakeCoordinator::SaveBakeSet(
	UPaper2DPlusCharacterLayerAsset* LayerAsset,
	const FCharacterLayerBakeSaveBackend* SaveBackend)
{
	using namespace Paper2DPlusCharacterLayerBakeCoordinatorPrivate;
	FCharacterLayerBakeSaveResult Result;
	if (HasFutureBakeManifest(LayerAsset))
	{
		Result.Report = FutureBakeManifestReport(*LayerAsset);
		return Result;
	}
	if (!LayerAsset
		|| (LayerAsset->BakeAttachmentState != ECharacterLayerBakeAttachmentState::Attached
			&& LayerAsset->BakeAttachmentState != ECharacterLayerBakeAttachmentState::Detached)
		|| LayerAsset->BakeManifest.IsEmpty())
	{
		Result.Report = TEXT("Save Bake Set requires a successfully baked attached or detached Layer Asset.");
		return Result;
	}

	const FString LayerPackageName = LayerAsset->GetOutermost()->GetName();
	const FSoftObjectPath LayerPackagePath(LayerPackageName);
	const TArray<FSoftObjectPath> SaveScope = LayerAsset->BakeManifest.TouchedPackages;
	TArray<UPackage*> OutputPackages;
	bool bLayerPackageIsInManifest = false;
	for (const FSoftObjectPath& PackagePath : LayerAsset->BakeManifest.TouchedPackages)
	{
		const FString Name = FPackageName::ObjectPathToPackageName(PackagePath.ToString());
		if (Name.Equals(LayerPackageName, ESearchCase::IgnoreCase))
		{
			bLayerPackageIsInManifest = true;
			continue; // operation checkpoint is written and saved last
		}
		UPackage* Package = FindPackage(nullptr, *Name);
		if (!Package) Package = LoadPackage(nullptr, *Name, LOAD_None);
		if (!Package)
		{
			Result.FailedPackages.Add(PackagePath);
			continue;
		}
		OutputPackages.AddUnique(Package);
	}
	if (!bLayerPackageIsInManifest)
	{
		Result.FailedPackages.AddUnique(LayerPackagePath);
	}

	auto SavePackages = [SaveBackend](
		const TArray<UPackage*>& Packages,
		TArray<UPackage*>& OutFailed) -> bool
	{
		if (Packages.IsEmpty()) return true;
		if (SaveBackend && SaveBackend->SavePackages)
		{
			return SaveBackend->SavePackages(Packages, OutFailed);
		}
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
		FEditorFileUtils::FPromptForCheckoutAndSaveParams Params;
		Params.bCheckDirty = false;
		Params.bPromptToSave = false;
		Params.bCanBeDeclined = false;
		Params.bIsExplicitSave = true;
		Params.OutFailedPackages = &OutFailed;
		return FEditorFileUtils::PromptForCheckoutAndSave(Packages, Params)
			== FEditorFileUtils::PR_Success;
#else
		return FEditorFileUtils::PromptForCheckoutAndSave(
			Packages, false, false, &OutFailed, false, false)
			== FEditorFileUtils::PR_Success;
#endif
	};

	TArray<UPackage*> FailedOutputs;
	const bool bOutputSaveCompleted = SavePackages(OutputPackages, FailedOutputs);
	for (UPackage* Package : OutputPackages)
	{
		const FSoftObjectPath Path(Package->GetName());
		// A global failure/cancel result does not prove that any package omitted from OutFailed reached disk.
		// Treat every unproven output as pending so the complete manifest scope remains eligible for retry.
		if (!bOutputSaveCompleted || FailedOutputs.Contains(Package))
		{
			Result.FailedPackages.AddUnique(Path);
		}
		else
		{
			Result.SavedPackages.AddUnique(Path);
		}
	}
	const bool bOutputsSucceeded = bOutputSaveCompleted
		&& Result.FailedPackages.IsEmpty();

	// Predict the final record before saving the Layer checkpoint. A checkpoint failure is the one unavoidable
	// case that cannot persist its own final failure state; the in-memory asset remains dirty and reports it.
	if (bLayerPackageIsInManifest)
	{
		Result.SavedPackages.AddUnique(LayerPackagePath);
	}
	Result.bSuccess = bOutputsSucceeded && bLayerPackageIsInManifest;
	Result.Report = Result.bSuccess
		? FString::Printf(TEXT("Saved all %d bake-set package(s)."), Result.SavedPackages.Num())
		: FString::Printf(TEXT("Saved %d bake-set package(s); %d failed or the save was cancelled."),
			Result.SavedPackages.Num(), Result.FailedPackages.Num());

	// Every package in SaveScope is now durable, including Rebase-retired/frozen outputs. Prune those
	// one-shot paths before the Layer checkpoint so later bakes/saves cannot accumulate missing history.
	// Old manifests without a profile hint fail safe by retaining their complete scope.
	if (Result.bSuccess && !LayerAsset->BakeManifest.CharacterProfilePathHint.IsEmpty())
	{
		BuildActiveManifestPackageSet(
			LayerAsset,
			FSoftObjectPath(LayerAsset->BakeManifest.CharacterProfilePathHint),
			LayerAsset->BakeManifest.Animations,
			LayerAsset->BakeManifest.TouchedPackages);
	}

	LayerAsset->LastBakeOperation = MakeOperationRecord(
		ECharacterLayerBakeLifecycleOperation::SaveBakeSet, Result.Report, Result.bSuccess);
	LayerAsset->LastBakeOperation.TouchedPackages = SaveScope;
	LayerAsset->LastBakeOperation.SavedPackages = Result.SavedPackages;
	LayerAsset->LastBakeOperation.FailedPackages = Result.FailedPackages;
	LayerAsset->MarkPackageDirty();

	if (bLayerPackageIsInManifest)
	{
		TArray<UPackage*> FailedCheckpoint;
		const TArray<UPackage*> CheckpointPackages = { LayerAsset->GetOutermost() };
		const bool bCheckpointSaveCompleted = SavePackages(
			CheckpointPackages, FailedCheckpoint);
		Result.bCheckpointSaved = bCheckpointSaveCompleted
			&& FailedCheckpoint.IsEmpty();
		if (!Result.bCheckpointSaved)
		{
			// The pruned active-only manifest was not made durable. Restore the exact pre-save scope in memory
			// so a retry cannot forget retired/frozen packages whose metadata was saved in this attempt.
			LayerAsset->BakeManifest.TouchedPackages = SaveScope;
			Result.bSuccess = false;
			Result.SavedPackages.Remove(LayerPackagePath);
			Result.FailedPackages.AddUnique(LayerPackagePath);
			Result.Report = FString::Printf(
				TEXT("Saved %d bake-set package(s), but the final Layer report checkpoint failed."),
				Result.SavedPackages.Num());
			LayerAsset->LastBakeOperation.bSuccess = false;
			LayerAsset->LastBakeOperation.Report = Result.Report;
			LayerAsset->LastBakeOperation.SavedPackages = Result.SavedPackages;
			LayerAsset->LastBakeOperation.FailedPackages = Result.FailedPackages;
			LayerAsset->MarkPackageDirty();
		}
	}
	return Result;
}

FCharacterLayerBakeStatusSnapshot CharacterLayerBakeCoordinator::EvaluateStatus(
	const UPaper2DPlusCharacterLayerAsset* LayerAsset,
	const UPaper2DPlusCharacterProfileAsset* CharacterProfile,
	int32 CurrentRegistrationIndex)
{
	using namespace Paper2DPlusCharacterLayerBakeCoordinatorPrivate;
	FCharacterLayerBakeStatusSnapshot Result;
	Result.CurrentRegistrationIndex = CurrentRegistrationIndex;
	if (!LayerAsset)
	{
		Result.bIntegrityBlocked = true;
		Result.Blockers.Add(TEXT("A loaded Character Layer Asset is required."));
		return Result;
	}

	Result.AttachmentState = LayerAsset->BakeAttachmentState;
	Result.LastOperation = LayerAsset->LastBakeOperation;
	Result.OverwriteScope = LayerAsset->BakeManifest.TouchedPackages;
	FPaper2DPlusAppearanceDescriptor DefaultAppearance;
	if (Paper2DPlusAppearanceResolver::BuildDefaultDescriptor(LayerAsset, DefaultAppearance))
	{
		Result.DefaultActiveLayerCount = DefaultAppearance.ActiveLayerIds.Num();
	}

	auto ScanManifestPackages = [&Result, LayerAsset]()
	{
		for (const FSoftObjectPath& Path : LayerAsset->BakeManifest.TouchedPackages)
		{
			const FString PackageName = FPackageName::ObjectPathToPackageName(Path.ToString());
			if (PackageName.IsEmpty())
			{
				Result.MissingPackages.AddUnique(Path);
				continue;
			}
			if (UPackage* Package = FindPackage(nullptr, *PackageName))
			{
				if (Package->IsDirty()) Result.DirtyPackages.AddUnique(Path);
			}
			else if (!FPackageName::DoesPackageExist(PackageName))
			{
				Result.MissingPackages.AddUnique(Path);
			}
		}
		Result.FailedSavePackages = LayerAsset->LastBakeOperation.FailedPackages;
		Result.bNeedsSave = !Result.DirtyPackages.IsEmpty();
		Result.bPartialSave = !Result.MissingPackages.IsEmpty()
			|| (LayerAsset->LastBakeOperation.Operation == ECharacterLayerBakeLifecycleOperation::SaveBakeSet
				&& !LayerAsset->LastBakeOperation.bSuccess
				&& !LayerAsset->LastBakeOperation.FailedPackages.IsEmpty());
	};

	if (HasFutureBakeManifest(LayerAsset))
	{
		Result.bIntegrityBlocked = true;
		Result.Status = LayerAsset->BakeAttachmentState == ECharacterLayerBakeAttachmentState::Detached
			? ECharacterLayerBakeStatus::Detached
			: (LayerAsset->BakeAttachmentState == ECharacterLayerBakeAttachmentState::Unclaimed
				? ECharacterLayerBakeStatus::NeverBaked
				: ECharacterLayerBakeStatus::RecoveryRequired);
		Result.Blockers.Add(FutureBakeManifestReport(*LayerAsset));
		// Every mutating action intentionally remains false. A future writer is the only authority that can
		// interpret or migrate this manifest; Repair and Detach are not safe escape hatches.
		return Result;
	}
	// Once this writer understands the manifest, keep missing/dirty/failed package identities visible
	// even when a malformed attached set must return Recovery Required before animation hashing can run.
	ScanManifestPackages();

	if (LayerAsset->BakeAttachmentState == ECharacterLayerBakeAttachmentState::Unclaimed)
	{
		Result.Status = ECharacterLayerBakeStatus::NeverBaked;
		if (!CharacterProfile) Result.Blockers.Add(TEXT("A loaded Character Profile is required for the first fixed bake."));
		if (!LayerAsset->BakeManifest.IsEmpty()) Result.Blockers.Add(TEXT("Unclaimed source contains prior bake evidence; repair the lifecycle before publishing."));
		if (CharacterProfile && CharacterProfile->LayerBakeOwnerToken.IsValid())
		{
			Result.Blockers.Add(TEXT("The Character Profile is already claimed by another Layer bake set."));
			Result.bHasCompetingOwnership = CharacterProfile->LayerBakeOwnerToken != LayerAsset->BakeSetId;
		}
		Result.bCanBakeAll = Result.Blockers.IsEmpty();
		return Result;
	}

	if (LayerAsset->BakeAttachmentState == ECharacterLayerBakeAttachmentState::Detached)
	{
		Result.Status = ECharacterLayerBakeStatus::Detached;
		Result.bCanSaveBakeSet = !LayerAsset->BakeManifest.IsEmpty()
			&& (Result.bNeedsSave || Result.bPartialSave);
		return Result;
	}

	if (LayerAsset->BakeAttachmentState == ECharacterLayerBakeAttachmentState::RecoveryRequired)
	{
		Result.Status = ECharacterLayerBakeStatus::RecoveryRequired;
		Result.bHasCompetingOwnership = CharacterProfile
			&& CharacterProfile->LayerBakeOwnerToken.IsValid()
			&& CharacterProfile->LayerBakeOwnerToken != LayerAsset->BakeSetId;
		Result.bCanRepair = CharacterProfile != nullptr && !Result.bHasCompetingOwnership;
		TArray<UObject*> FreezeObjects;
		TArray<FString> FreezeErrors;
		bool bFreezeCompeting = false;
		Result.bCanDetach = CharacterProfile != nullptr
			&& CollectFreezeOutputObjects(*LayerAsset, FreezeObjects, FreezeErrors, bFreezeCompeting)
			&& !Result.bHasCompetingOwnership;
		Result.Blockers.Add(TEXT("Repair must complete before any other publish lifecycle command."));
		Result.Warnings.Append(FreezeErrors);
		return Result;
	}

	Result.Status = ECharacterLayerBakeStatus::UpToDate;
	if (!CharacterProfile)
	{
		Result.Blockers.Add(TEXT("The attached Layer Asset has no loaded Character Profile."));
	}
	if (!LayerAsset->BakeSetId.IsValid())
	{
		Result.Blockers.Add(TEXT("The attached Layer Asset has no valid bake-set identity."));
	}
	if (LayerAsset->UsageMode != ECharacterLayerUsageMode::FixedBaked)
	{
		Result.Blockers.Add(TEXT("An attached bake set must use Fixed Baked delivery."));
	}
	if (LayerAsset->BakeManifest.IsEmpty()
		|| LayerAsset->BakeManifest.BakeSetId != LayerAsset->BakeSetId)
	{
		Result.Blockers.Add(TEXT("The attached bake manifest is missing or belongs to another bake set."));
	}
	if (LayerAsset->AnimationRegistration.IsEmpty())
	{
		Result.Blockers.Add(TEXT("The attached bake set has no canonical animation registration."));
	}
	if (CharacterProfile && CharacterProfile->LayerBakeOwnerToken != LayerAsset->BakeSetId)
	{
		Result.Blockers.Add(TEXT("The Character Profile owner token does not match this Layer bake set."));
		Result.bHasCompetingOwnership = CharacterProfile->LayerBakeOwnerToken.IsValid();
	}
	Result.bIntegrityBlocked = !Result.Blockers.IsEmpty();
	if (Result.bIntegrityBlocked)
	{
		Result.Status = ECharacterLayerBakeStatus::RecoveryRequired;
		Result.bCanRepair = CharacterProfile != nullptr && !Result.bHasCompetingOwnership;
		TArray<UObject*> FreezeObjects;
		TArray<FString> FreezeErrors;
		bool bFreezeCompeting = false;
		Result.bCanDetach = CharacterProfile != nullptr
			&& CollectFreezeOutputObjects(*LayerAsset, FreezeObjects, FreezeErrors, bFreezeCompeting)
			&& !Result.bHasCompetingOwnership;
		Result.Warnings.Append(FreezeErrors);
		return Result;
	}
	const bool bManifestUpgradeRequiresBakeAll =
		LayerAsset->BakeManifest.ManifestVersion < Paper2DPlusLayerBakeVersion::CurrentManifestVersion;
	if (bManifestUpgradeRequiresBakeAll)
	{
		Result.Warnings.Add(FString::Printf(
			TEXT("This bake set uses manifest schema version %u. Run Bake All once to upgrade the complete bake set to schema version %u."),
			LayerAsset->BakeManifest.ManifestVersion,
			Paper2DPlusLayerBakeVersion::CurrentManifestVersion));
	}
	const bool bDigestUpgradeRequiresBakeAll =
		LayerAsset->BakeManifest.DigestVersion < CharacterLayerBakeCore::CurrentDigestVersion;
	if (bDigestUpgradeRequiresBakeAll)
	{
		Result.Warnings.Add(FString::Printf(
			TEXT("This bake set uses digest version %u. Run Bake All once to upgrade every animation record together to version %u."),
			LayerAsset->BakeManifest.DigestVersion,
			CharacterLayerBakeCore::CurrentDigestVersion));
	}

	for (int32 RegistrationIndex = 0;
		RegistrationIndex < LayerAsset->AnimationRegistration.Num(); ++RegistrationIndex)
	{
		const FCharacterLayerAnimationRegistration& Registration =
			LayerAsset->AnimationRegistration[RegistrationIndex];
		FCharacterLayerBakeAnimationStatus& Animation = Result.Animations.AddDefaulted_GetRef();
		Animation.RegistrationIndex = RegistrationIndex;
		Animation.FlipbookPath = Registration.Flipbook.ToSoftObjectPath();
		Animation.AnimationName = Registration.LegacyAnimationName;

		UPaperFlipbook* Flipbook = Registration.Flipbook.LoadSynchronous();
		if (!Flipbook)
		{
			Animation.bSourceDrift = true;
			Animation.bOutputConflict = true;
			Animation.Diagnostics.Add(TEXT("The registered canonical flipbook is missing."));
			++Result.SourceDriftCount;
			++Result.OutputConflictCount;
			continue;
		}
		const FCharacterLayerAnimationBakeRecord* Record = FindRecord(
			LayerAsset->BakeManifest, Flipbook, Registration.LegacyAnimationName);
		Animation.bHasManifestRecord = Record != nullptr;
		if (!Record)
		{
			Animation.bSourceDrift = true;
			Animation.bOutputConflict = true;
			Animation.Diagnostics.Add(TEXT("The registered animation has no manifest record."));
			++Result.SourceDriftCount;
			++Result.OutputConflictCount;
			continue;
		}

		const FCharacterLayerBakeAnimationPlan Plan = CharacterLayerBakeCore::BuildAnimationPlan(
			LayerAsset, CharacterProfile, RegistrationIndex);
		if (!Plan.CanCommit())
		{
			Animation.bSourceDrift = true;
			for (const FCharacterLayerBakeDiagnostic& Diagnostic : Plan.Diagnostics)
			{
				if (Diagnostic.Severity == ECharacterLayerBakeDiagnosticSeverity::Error)
				{
					Animation.Diagnostics.Add(Diagnostic.Message);
				}
			}
			if (Animation.Diagnostics.IsEmpty())
			{
				Animation.Diagnostics.Add(TEXT("The current Layer source cannot produce a valid animation plan."));
			}
		}
		else
		{
			Animation.bSourceDrift = Plan.SourceDigest != Record->SourceDigest;
		}
		if (Animation.bSourceDrift) ++Result.SourceDriftCount;

		FString DigestError;
		const FString CurrentOutput = ComputeCurrentOutputDigest(
			LayerAsset, CharacterProfile, *Record, &DigestError);
		Animation.bOutputConflict = CurrentOutput.IsEmpty() || CurrentOutput != Record->OutputDigest;
		if (Animation.bOutputConflict)
		{
			++Result.OutputConflictCount;
			Animation.Diagnostics.Add(DigestError.IsEmpty()
				? TEXT("Managed output differs from the committed manifest.") : DigestError);
			if (DigestError.Contains(TEXT("competing bake set"), ESearchCase::IgnoreCase))
			{
				Result.bHasCompetingOwnership = true;
			}
		}
	}

	if (Result.OutputConflictCount > 0)
	{
		Result.Status = ECharacterLayerBakeStatus::OutputConflict;
	}
	else if (Result.SourceDriftCount > 0)
	{
		Result.Status = ECharacterLayerBakeStatus::NeedsBake;
	}
	else
	{
		Result.Status = ECharacterLayerBakeStatus::UpToDate;
	}

	const bool bCurrentValid = LayerAsset->AnimationRegistration.IsValidIndex(CurrentRegistrationIndex);
	const bool bOrdinaryBakeSafe = Result.OutputConflictCount == 0 && !Result.bIntegrityBlocked;
	Result.bCanBakeCurrent = bCurrentValid && bOrdinaryBakeSafe
		&& !bManifestUpgradeRequiresBakeAll && !bDigestUpgradeRequiresBakeAll;
	Result.bCanBakeAll = bOrdinaryBakeSafe;
	Result.bCanSaveBakeSet = !LayerAsset->BakeManifest.IsEmpty()
		&& (Result.bNeedsSave || Result.bPartialSave);
	Result.bCanOverwriteFromSource = Result.OutputConflictCount > 0
		&& !Result.bHasCompetingOwnership && !Result.bIntegrityBlocked;
	Result.bCanRebaseRegistration = !Result.bHasCompetingOwnership
		&& !LayerAsset->BakeManifest.IsEmpty()
		&& !LayerAsset->AnimationRegistration.IsEmpty();
	TArray<UObject*> FreezeObjects;
	TArray<FString> FreezeErrors;
	bool bFreezeCompeting = false;
	Result.bCanDetach = CollectFreezeOutputObjects(
		*LayerAsset, FreezeObjects, FreezeErrors, bFreezeCompeting)
		&& !Result.bHasCompetingOwnership;
	if (!Result.bCanDetach) Result.Warnings.Append(FreezeErrors);
	return Result;
}

FCharacterLayerBakeResult CharacterLayerBakeCoordinator::Repair(
	UPaper2DPlusCharacterLayerAsset* LayerAsset,
	UPaper2DPlusCharacterProfileAsset* CharacterProfile,
	const TArray<FCharacterLayerAdoptionDecision>& AdoptionDecisions)
{
	using namespace Paper2DPlusCharacterLayerBakeCoordinatorPrivate;
	FCharacterLayerBakeResult Result;
	if (HasFutureBakeManifest(LayerAsset))
	{
		Result.Report = FutureBakeManifestReport(*LayerAsset);
		Result.Errors.Add(Result.Report);
		return Result;
	}
	if (!LayerAsset || !CharacterProfile)
	{
		Result.Report = TEXT("Repair requires a loaded Layer Asset and Character Profile.");
		Result.Errors.Add(Result.Report);
		return Result;
	}
	const FCharacterLayerBakeStatusSnapshot InitialStatus = EvaluateStatus(
		LayerAsset, CharacterProfile);
	const bool bPersistedOrDerivedRecovery =
		LayerAsset->BakeAttachmentState == ECharacterLayerBakeAttachmentState::RecoveryRequired
		|| (LayerAsset->BakeAttachmentState == ECharacterLayerBakeAttachmentState::Attached
			&& InitialStatus.bIntegrityBlocked);
	if (!bPersistedOrDerivedRecovery)
	{
		Result.Report = TEXT("Repair is available only for persisted or verified derived Recovery Required state.");
		Result.Errors.Add(Result.Report);
		return Result;
	}
	if (CharacterProfile->LayerBakeOwnerToken.IsValid()
		&& CharacterProfile->LayerBakeOwnerToken != LayerAsset->BakeSetId)
	{
		Result.Report = TEXT("Repair refused to overwrite a Character Profile claimed by a competing bake set.");
		Result.Errors.Add(Result.Report);
		LayerAsset->LastBakeOperation = MakeOperationRecord(
			ECharacterLayerBakeLifecycleOperation::Repair, Result.Report, false);
		LayerAsset->LastBakeOperation.bRecoveryRequired = true;
		LayerAsset->MarkPackageDirty();
		return Result;
	}

	const ECharacterLayerUsageMode OriginalUsage = LayerAsset->UsageMode;
	const FGuid OriginalProfileOwner = CharacterProfile->LayerBakeOwnerToken;
	const FString OriginalProfileOwnerPath = CharacterProfile->LayerBakeOwnerPathHint;
	if (LayerAsset->BakeManifest.IsEmpty())
	{
		Result.Report = TEXT("Repair cannot recover a fixed bake without retained manifest evidence. Detach and publish Bake All again.");
		Result.Errors.Add(Result.Report);
		return Result;
	}

	FCharacterLayerBakeRequest Request;
	Request.LayerAsset = LayerAsset;
	Request.CharacterProfile = CharacterProfile;
	Request.bOverwriteManagedOutputConflict = true;
	if (LayerAsset->AnimationRegistration.IsEmpty())
	{
		Result.Report = TEXT("Repair cannot trust an attached manifest without fixed-bake registration evidence.");
		Result.Errors.Add(Result.Report);
		return Result;
	}
	LayerAsset->BakeAttachmentState = ECharacterLayerBakeAttachmentState::Attached;
	LayerAsset->UsageMode = ECharacterLayerUsageMode::FixedBaked;
	if (!CharacterProfile->LayerBakeOwnerToken.IsValid())
	{
		CharacterProfile->LayerBakeOwnerToken = LayerAsset->BakeSetId;
		CharacterProfile->LayerBakeOwnerPathHint = LayerAsset->GetPathName();
	}
	Request.Operation = ECharacterLayerBakeOperation::All;

	Result = Execute(Request);
	if (!Result.bSuccess)
	{
		LayerAsset->BakeAttachmentState = ECharacterLayerBakeAttachmentState::RecoveryRequired;
		LayerAsset->UsageMode = OriginalUsage;
		CharacterProfile->LayerBakeOwnerToken = OriginalProfileOwner;
		CharacterProfile->LayerBakeOwnerPathHint = OriginalProfileOwnerPath;
		Result.bRecoveryRequired = true;
		if (Result.Report.IsEmpty()) Result.Report = TEXT("Repair failed; the Layer Asset remains Recovery Required.");
		LayerAsset->LastBakeOperation = MakeOperationRecord(
			ECharacterLayerBakeLifecycleOperation::Repair, Result.Report, false);
		LayerAsset->LastBakeOperation.bCancelled = Result.bCancelled;
		LayerAsset->LastBakeOperation.bRolledBack = Result.bRolledBack;
		LayerAsset->LastBakeOperation.bRecoveryRequired = true;
		LayerAsset->MarkPackageDirty();
		CharacterProfile->MarkPackageDirty();
		return Result;
	}

	LayerAsset->BakeAttachmentState = ECharacterLayerBakeAttachmentState::Attached;
	LayerAsset->UsageMode = ECharacterLayerUsageMode::FixedBaked;
	Result.Report = TEXT("Repair rebuilt and verified the complete fixed-publishing bake set. Save Bake Set to persist it.");
	LayerAsset->LastBakeOperation = MakeOperationRecord(
		ECharacterLayerBakeLifecycleOperation::Repair, Result.Report, true);
	LayerAsset->LastBakeOperation.TouchedPackages = Result.TouchedPackages;
	LayerAsset->MarkPackageDirty();
	return Result;
}

FCharacterLayerBakeResult CharacterLayerBakeCoordinator::RebaseRegistration(
	UPaper2DPlusCharacterLayerAsset* LayerAsset,
	UPaper2DPlusCharacterProfileAsset* CharacterProfile)
{
	using namespace Paper2DPlusCharacterLayerBakeCoordinatorPrivate;
	FCharacterLayerBakeResult Result;
	if (HasFutureBakeManifest(LayerAsset))
	{
		Result.Report = FutureBakeManifestReport(*LayerAsset);
		Result.Errors.Add(Result.Report);
		return Result;
	}
	auto Fail = [&Result, LayerAsset](const FString& Message)
	{
		Result.Report = Message;
		Result.Errors.Add(Message);
		if (LayerAsset)
		{
			LayerAsset->LastBakeOperation = MakeOperationRecord(
				ECharacterLayerBakeLifecycleOperation::RebaseRegistration, Message, false);
			LayerAsset->MarkPackageDirty();
		}
		return Result;
	};
	if (!LayerAsset || !CharacterProfile)
	{
		return Fail(TEXT("Rebase Registration requires a loaded Layer Asset and Character Profile."));
	}
	if (LayerAsset->BakeAttachmentState != ECharacterLayerBakeAttachmentState::Attached
		|| LayerAsset->UsageMode != ECharacterLayerUsageMode::FixedBaked
		|| CharacterProfile->LayerBakeOwnerToken != LayerAsset->BakeSetId
		|| LayerAsset->BakeManifest.IsEmpty()
		|| LayerAsset->AnimationRegistration.IsEmpty()
		|| CharacterProfile->Flipbooks.IsEmpty())
	{
		return Fail(TEXT("Rebase Registration requires an attached, exclusively owned fixed-publishing bake set."));
	}

	TArray<FCharacterLayerAnimationRegistration> Rebased;
	Rebased.Reserve(CharacterProfile->Flipbooks.Num());
	TSet<int32> UsedOldRegistrations;
	TSet<FString> CurrentProfileFlipbooks;
	TArray<const FFlipbookProfileEntry*> NewProfileEntries;

	auto ResolveOldRegistrationIndex = [LayerAsset](
		UPaperFlipbook* Flipbook, const FString& LegacyName, bool& bOutAmbiguous) -> int32
	{
		bOutAmbiguous = false;
		const FSoftObjectPath Path(Flipbook ? Flipbook->GetPathName() : FString());
		int32 Match = INDEX_NONE;
		for (int32 Index = 0; Index < LayerAsset->AnimationRegistration.Num(); ++Index)
		{
			const FCharacterLayerAnimationRegistration& Candidate = LayerAsset->AnimationRegistration[Index];
			if (Flipbook && (Candidate.Flipbook.Get() == Flipbook
				|| Candidate.Flipbook.ToSoftObjectPath() == Path))
			{
				if (Match != INDEX_NONE) { bOutAmbiguous = true; return INDEX_NONE; }
				Match = Index;
			}
		}
		if (Match != INDEX_NONE) return Match;
		for (int32 Index = 0; Index < LayerAsset->AnimationRegistration.Num(); ++Index)
		{
			if (!LegacyName.IsEmpty()
				&& LayerAsset->AnimationRegistration[Index].LegacyAnimationName.Equals(
					LegacyName, ESearchCase::IgnoreCase))
			{
				if (Match != INDEX_NONE) { bOutAmbiguous = true; return INDEX_NONE; }
				Match = Index;
			}
		}
		return Match;
	};

	auto CaptureNonManagedFrame = [&Fail](
		UPaperSprite* Sprite,
		const FString& AnimationName,
		int32 FrameIndex,
		FCharacterLayerRegisteredFrame& OutFrame) -> bool
	{
		if (!Sprite)
		{
			Fail(FString::Printf(
				TEXT("Rebase rejected new null frame %d in '%s'; no original source can be proven."),
				FrameIndex, *AnimationName));
			return false;
		}
		bool bObjectOwnerFound = false;
		bool bPackageOwnerFound = false;
		const FString ObjectOwner = ReadMeta(Sprite, OwnerKey, &bObjectOwnerFound);
		const FString PackageOwner = ReadMeta(Sprite->GetOutermost(), OwnerKey, &bPackageOwnerFound);
		if (bObjectOwnerFound || bPackageOwnerFound)
		{
			Fail(FString::Printf(
				TEXT("Rebase rejected frame %d in '%s': sprite '%s' is managed output (%s%s)."),
				FrameIndex, *AnimationName, *Sprite->GetPathName(), *ObjectOwner, *PackageOwner));
			return false;
		}

		OutFrame.SourceSprite = Sprite;
		OutFrame.SourceUV = Sprite->GetSourceUV();
		OutFrame.SourceDimension = Sprite->GetSourceSize();
		OutFrame.PivotLocal = Sprite->GetPivotPosition() - Sprite->GetSourceUV();
		OutFrame.PixelsPerUnrealUnit = Sprite->GetPixelsPerUnrealUnit();
		OutFrame.MaterialPath = Sprite->GetDefaultMaterial()
			? FSoftObjectPath(Sprite->GetDefaultMaterial()->GetPathName())
			: FSoftObjectPath();
		OutFrame.NativeBehaviorDigest = CharacterLayerBakeCore::ComputeNativeSpriteBehaviorDigest(Sprite);
#if WITH_EDITORONLY_DATA
		OutFrame.bUsesUnsupportedAtlasGroup = Sprite->GetAtlasGroup() != nullptr;
#endif
		FAdditionalSpriteTextureArray AdditionalTextures;
		Sprite->GetBakedAdditionalSourceTextures(AdditionalTextures);
		OutFrame.bUsesUnsupportedAdditionalTextures = !AdditionalTextures.IsEmpty();
		return true;
	};

	for (const FFlipbookProfileEntry& ProfileEntry : CharacterProfile->Flipbooks)
	{
		UPaperFlipbook* Flipbook = ProfileEntry.Identity.Flipbook.LoadSynchronous();
		if (!Flipbook || Flipbook->GetNumKeyFrames() <= 0)
		{
			return Fail(FString::Printf(TEXT("Profile animation '%s' has no usable canonical flipbook."),
				*ProfileEntry.Identity.FlipbookName));
		}
		const FString Identity = Flipbook->GetPathName().ToLower();
		if (CurrentProfileFlipbooks.Contains(Identity))
		{
			return Fail(FString::Printf(TEXT("Profile animation '%s' shares an ambiguous canonical flipbook."),
				*ProfileEntry.Identity.FlipbookName));
		}
		CurrentProfileFlipbooks.Add(Identity);

		bool bAmbiguousOld = false;
		const int32 OldIndex = ResolveOldRegistrationIndex(
			Flipbook, ProfileEntry.Identity.FlipbookName, bAmbiguousOld);
		if (bAmbiguousOld)
		{
			return Fail(FString::Printf(TEXT("Rebase found ambiguous prior registration for '%s'."),
				*ProfileEntry.Identity.FlipbookName));
		}
		const FCharacterLayerAnimationRegistration* OldRegistration =
			LayerAsset->AnimationRegistration.IsValidIndex(OldIndex)
				? &LayerAsset->AnimationRegistration[OldIndex] : nullptr;
		const FCharacterLayerAnimationBakeRecord* Record = OldRegistration
			? FindRecord(LayerAsset->BakeManifest, Flipbook, OldRegistration->LegacyAnimationName)
			: nullptr;
		if (OldRegistration && !Record)
		{
			return Fail(FString::Printf(TEXT("Rebase cannot trust prior registration '%s' without its manifest record."),
				*OldRegistration->LegacyAnimationName));
		}
		if (OldRegistration) UsedOldRegistrations.Add(OldIndex);
		else NewProfileEntries.Add(&ProfileEntry);

		FCharacterLayerAnimationRegistration& NewRegistration = Rebased.AddDefaulted_GetRef();
		NewRegistration.Flipbook = Flipbook;
		NewRegistration.LegacyAnimationName = ProfileEntry.Identity.FlipbookName;
		NewRegistration.FramesPerSecond = Flipbook->GetFramesPerSecond();
		NewRegistration.FrameRuns.Reserve(Flipbook->GetNumKeyFrames());
		NewRegistration.Frames.SetNum(Flipbook->GetNumKeyFrames());
		bool bHasGeometry = false;

		for (int32 FrameIndex = 0; FrameIndex < Flipbook->GetNumKeyFrames(); ++FrameIndex)
		{
			const FPaperFlipbookKeyFrame& KeyFrame = Flipbook->GetKeyFrameChecked(FrameIndex);
			NewRegistration.FrameRuns.Add(KeyFrame.FrameRun);
			FCharacterLayerRegisteredFrame& NewFrame = NewRegistration.Frames[FrameIndex];
			UPaperSprite* CurrentSprite = KeyFrame.Sprite;

			int32 ProvenOldIndex = INDEX_NONE;
			if (CurrentSprite && Record)
			{
				const FSoftObjectPath CurrentPath(CurrentSprite->GetPathName());
				ProvenOldIndex = Record->ManagedSprites.IndexOfByPredicate(
					[&CurrentPath](const FSoftObjectPath& Path) { return Path == CurrentPath; });
			}
			else if (!CurrentSprite && Record && OldRegistration
				&& OldRegistration->Frames.IsValidIndex(FrameIndex)
				&& Record->ManagedSprites.IsValidIndex(FrameIndex)
				&& Record->ManagedSprites[FrameIndex].IsNull())
			{
				ProvenOldIndex = FrameIndex;
			}

			if (OldRegistration && OldRegistration->Frames.IsValidIndex(ProvenOldIndex))
			{
				NewFrame = OldRegistration->Frames[ProvenOldIndex];
			}
			else if (!CaptureNonManagedFrame(
				CurrentSprite, ProfileEntry.Identity.FlipbookName, FrameIndex, NewFrame))
			{
				return Result;
			}
			bHasGeometry |= NewFrame.SourceDimension.X > 0.0 && NewFrame.SourceDimension.Y > 0.0;
		}
		if (!bHasGeometry)
		{
			return Fail(FString::Printf(
				TEXT("Rebase rejected '%s' because no frame has provable source geometry."),
				*ProfileEntry.Identity.FlipbookName));
		}
		NewRegistration.RegistrationDigest =
			CharacterLayerBakeCore::ComputeAnimationRegistrationDigest(NewRegistration);
	}

	TArray<FCharacterLayerAnimationBakeRecord> RemovedRecords;
	TArray<UObject*> RemovedStampedObjects;
	TArray<FSoftObjectPath> RemovedTouchedPackages;
	for (int32 OldIndex = 0; OldIndex < LayerAsset->AnimationRegistration.Num(); ++OldIndex)
	{
		if (UsedOldRegistrations.Contains(OldIndex)) continue;
		const FCharacterLayerAnimationRegistration& RemovedRegistration =
			LayerAsset->AnimationRegistration[OldIndex];
		UPaperFlipbook* RemovedFlipbook = RemovedRegistration.Flipbook.LoadSynchronous();
		const FCharacterLayerAnimationBakeRecord* RemovedRecord = FindRecord(
			LayerAsset->BakeManifest, RemovedFlipbook, RemovedRegistration.LegacyAnimationName);
		if (!RemovedFlipbook || !RemovedRecord)
		{
			return Fail(TEXT("Rebase cannot prove one removed animation's registered output."));
		}
		const FSoftObjectPath RemovedPath(RemovedFlipbook->GetPathName());
		for (TObjectIterator<UPaper2DPlusCharacterProfileAsset> It; It; ++It)
		{
			const UPaper2DPlusCharacterProfileAsset* OtherProfile = *It;
			if (!OtherProfile || OtherProfile == CharacterProfile) continue;
			// Adoption preflight uses an owned transient duplicate. It can remain reachable until the next GC, but
			// it is not an independent consumer. Skip only that exact same-owner/path identity; every other loaded
			// transient Profile remains a real shared-reference blocker (covered by the workflow regression).
			const bool bSameBakeStagingProfile = OtherProfile->GetOutermost() == GetTransientPackage()
				&& OtherProfile->LayerBakeOwnerToken == LayerAsset->BakeSetId
				&& OtherProfile->LayerBakeOwnerPathHint.Equals(
					LayerAsset->GetPathName(), ESearchCase::IgnoreCase);
			if (bSameBakeStagingProfile) continue;
			if (OtherProfile->Flipbooks.ContainsByPredicate([&RemovedPath](const FFlipbookProfileEntry& Entry)
			{
				return Entry.Identity.Flipbook.ToSoftObjectPath() == RemovedPath;
			}))
			{
				return Fail(FString::Printf(
					TEXT("Rebase refused removal of '%s': another loaded Character Profile still references it."),
					*RemovedRegistration.LegacyAnimationName));
			}
		}
		for (TObjectIterator<UPaper2DPlusCharacterLayerAsset> It; It; ++It)
		{
			const UPaper2DPlusCharacterLayerAsset* OtherLayer = *It;
			if (!OtherLayer || OtherLayer == LayerAsset
				|| OtherLayer->GetOutermost() == GetTransientPackage()
				|| OtherLayer->BakeAttachmentState == ECharacterLayerBakeAttachmentState::Unclaimed
				|| OtherLayer->BakeAttachmentState == ECharacterLayerBakeAttachmentState::Detached) continue;
			if (OtherLayer->AnimationRegistration.ContainsByPredicate(
				[&RemovedPath](const FCharacterLayerAnimationRegistration& Row)
				{
					return Row.Flipbook.ToSoftObjectPath() == RemovedPath;
				}))
			{
				return Fail(FString::Printf(
					TEXT("Rebase refused removal of '%s': another active Layer bake set shares it."),
					*RemovedRegistration.LegacyAnimationName));
			}
		}

		auto RequireMatchingStamp = [LayerAsset, &RemovedStampedObjects, &RemovedTouchedPackages, &Fail](
			UObject* Object, const FString& Description) -> bool
		{
			if (!Object)
			{
				Fail(Description + TEXT(" is missing."));
				return false;
			}
			FString OwnerError;
			if (!ValidateOwner(Object, LayerAsset->BakeSetId, OwnerError))
			{
				Fail(OwnerError);
				return false;
			}
			RemovedStampedObjects.AddUnique(Object);
			AddTouchedPackage(RemovedTouchedPackages, Object);
			return true;
		};
		if (!RequireMatchingStamp(RemovedFlipbook, TEXT("Removed canonical flipbook"))) return Result;
		UObject* RemovedTexture = ResolveSoftObjectQuietly(RemovedRecord->ManagedTexture);
		if (!RequireMatchingStamp(RemovedTexture, TEXT("Removed managed texture"))
			|| !RequireMatchingStamp(RemovedTexture ? RemovedTexture->GetOutermost() : nullptr,
				TEXT("Removed managed texture package"))) return Result;
		for (const FSoftObjectPath& SpritePath : RemovedRecord->ManagedSprites)
		{
			if (SpritePath.IsNull()) continue;
			UObject* Sprite = ResolveSoftObjectQuietly(SpritePath);
			if (!RequireMatchingStamp(Sprite, TEXT("Removed managed sprite"))
				|| !RequireMatchingStamp(Sprite ? Sprite->GetOutermost() : nullptr,
					TEXT("Removed managed sprite package"))) return Result;
		}
		RemovedRecords.Add(*RemovedRecord);
	}

	const TArray<FCharacterLayerAnimationRegistration> OriginalRegistration = LayerAsset->AnimationRegistration;
	const FCharacterLayerBakeOperationRecord OriginalOperation = LayerAsset->LastBakeOperation;
	const TArray<FPaper2DPlusCharacterBaselineAnimation> OriginalBaseline = CharacterProfile->CharacterBaseline;
	const bool bProfileWasDirty = CharacterProfile->GetOutermost()->IsDirty();
	LayerAsset->AnimationRegistration = MoveTemp(Rebased);
	LayerAsset->MarkPackageDirty();
	CharacterProfile->Modify();
	for (const FFlipbookProfileEntry* NewEntry : NewProfileEntries)
	{
		if (!NewEntry || CharacterProfile->FindCharacterBaseline(
			NewEntry->Identity.Flipbook.ToSoftObjectPath(), NewEntry->Identity.FlipbookName)) continue;
		FPaper2DPlusCharacterBaselineAnimation& Baseline =
			CharacterProfile->CharacterBaseline.AddDefaulted_GetRef();
		Baseline.Flipbook = NewEntry->Identity.Flipbook;
		Baseline.LegacyAnimationName = NewEntry->Identity.FlipbookName;
		Baseline.Frames = NewEntry->CombatData.Frames;
		FPaper2DPlusFrameCueReplacementMap CueReplacements;
		for (const UPaper2DPlusCueBase* Cue : NewEntry->FrameEventData.FrameCues)
		{
			UPaper2DPlusCueBase* Duplicate =
				Cue ? DuplicateObject<UPaper2DPlusCueBase>(Cue, CharacterProfile) : nullptr;
			Baseline.FrameCues.Add(Duplicate);
			if (Cue && Duplicate) CueReplacements.Add(Cue, Duplicate);
		}
		Baseline.CueTrackLayout = NewEntry->FrameEventData.CueTrackLayout;
		Baseline.CueTrackLayout.RemapCueReferences(CueReplacements);
		TSet<const UPaper2DPlusCueBase*> CueDomain;
		for (const UPaper2DPlusCueBase* Cue : Baseline.FrameCues)
		{
			if (Cue) CueDomain.Add(Cue);
		}
		Baseline.CueTrackLayout.RetainCueAssignments(CueDomain);
		for (const UPaper2DPlusFrameEventBase* Event : NewEntry->FrameEventData.FrameEvents)
		{
			Baseline.LegacyFrameEvents.Add(Event
				? DuplicateObject<UPaper2DPlusFrameEventBase>(Event, CharacterProfile) : nullptr);
		}
	}
	CharacterProfile->MarkPackageDirty();

	FCharacterLayerBakeRequest Request;
	Request.LayerAsset = LayerAsset;
	Request.CharacterProfile = CharacterProfile;
	Request.Operation = ECharacterLayerBakeOperation::All;
	Request.bOverwriteManagedOutputConflict = true;
	Result = Execute(Request);
	if (!Result.bSuccess)
	{
		LayerAsset->AnimationRegistration = OriginalRegistration;
		LayerAsset->LastBakeOperation = OriginalOperation;
		CharacterProfile->CharacterBaseline = OriginalBaseline;
		CharacterProfile->GetOutermost()->SetDirtyFlag(bProfileWasDirty);
		if (!Result.bRecoveryRequired)
		{
			LayerAsset->BakeAttachmentState = ECharacterLayerBakeAttachmentState::Attached;
		}
		LayerAsset->LastBakeOperation = MakeOperationRecord(
			ECharacterLayerBakeLifecycleOperation::RebaseRegistration, Result.Report, false);
		LayerAsset->LastBakeOperation.bCancelled = Result.bCancelled;
		LayerAsset->LastBakeOperation.bRolledBack = Result.bRolledBack;
		LayerAsset->LastBakeOperation.bRecoveryRequired = Result.bRecoveryRequired;
		LayerAsset->MarkPackageDirty();
		return Result;
	}

	for (UObject* Object : RemovedStampedObjects)
	{
		if (!Object || !Object->GetOutermost()) continue;
		Object->Modify();
		for (FName Key : { OwnerKey, RevisionKey, RoleKey, TargetKey })
		{
			RemoveMetadataValue(Object->GetOutermost(), Object, Key);
		}
		Object->GetOutermost()->MarkPackageDirty();
	}
	LayerAsset->BakeManifest.Animations.RemoveAll([&RemovedRecords](
		const FCharacterLayerAnimationBakeRecord& Candidate)
	{
		return RemovedRecords.ContainsByPredicate([&Candidate](const FCharacterLayerAnimationBakeRecord& Removed)
		{
			return (!Removed.Flipbook.IsNull()
				&& Removed.Flipbook.ToSoftObjectPath() == Candidate.Flipbook.ToSoftObjectPath())
				|| (!Removed.LegacyAnimationName.IsEmpty()
					&& Removed.LegacyAnimationName.Equals(Candidate.LegacyAnimationName, ESearchCase::IgnoreCase));
		});
	});
	CharacterProfile->CharacterBaseline.RemoveAll([&CurrentProfileFlipbooks](
		const FPaper2DPlusCharacterBaselineAnimation& Baseline)
	{
		return !CurrentProfileFlipbooks.Contains(
			Baseline.Flipbook.ToSoftObjectPath().ToString().ToLower());
	});
	for (const FSoftObjectPath& RemovedPackage : RemovedTouchedPackages)
	{
		LayerAsset->BakeManifest.TouchedPackages.AddUnique(RemovedPackage);
	}
	SortSoftPaths(LayerAsset->BakeManifest.TouchedPackages);
	Result.TouchedPackages = LayerAsset->BakeManifest.TouchedPackages;
	Result.Report = FString::Printf(
		TEXT("Rebased canonical registration (%d added, %d removed) and rebuilt every current animation. Removed outputs were frozen in place. Save Bake Set to persist it."),
		NewProfileEntries.Num(), RemovedRecords.Num());
	LayerAsset->BakeManifest.LastReport = Result.Report;
	LayerAsset->LastBakeOperation = MakeOperationRecord(
		ECharacterLayerBakeLifecycleOperation::RebaseRegistration, Result.Report, true);
	LayerAsset->LastBakeOperation.TouchedPackages = Result.TouchedPackages;
	LayerAsset->MarkPackageDirty();
	return Result;
}

FCharacterLayerBakeResult CharacterLayerBakeCoordinator::Detach(
	UPaper2DPlusCharacterLayerAsset* LayerAsset,
	UPaper2DPlusCharacterProfileAsset* CharacterProfile)
{
	using namespace Paper2DPlusCharacterLayerBakeCoordinatorPrivate;
	FCharacterLayerBakeResult Result;
	if (HasFutureBakeManifest(LayerAsset))
	{
		Result.Report = FutureBakeManifestReport(*LayerAsset);
		Result.Errors.Add(Result.Report);
		return Result;
	}
	auto Fail = [&Result, LayerAsset](const FString& Message)
	{
		Result.Report = Message;
		Result.Errors.Add(Message);
		if (LayerAsset)
		{
			LayerAsset->LastBakeOperation = MakeOperationRecord(
				ECharacterLayerBakeLifecycleOperation::Detach, Message, false);
			LayerAsset->MarkPackageDirty();
		}
		return Result;
	};
	if (!LayerAsset || !CharacterProfile)
	{
		return Fail(TEXT("Detach requires a loaded Layer Asset and Character Profile."));
	}
	if ((LayerAsset->BakeAttachmentState != ECharacterLayerBakeAttachmentState::Attached
			&& LayerAsset->BakeAttachmentState != ECharacterLayerBakeAttachmentState::RecoveryRequired)
		|| LayerAsset->UsageMode != ECharacterLayerUsageMode::FixedBaked)
	{
		return Fail(TEXT("Detach requires attached or Recovery Required canonical output."));
	}
	if (CharacterProfile->LayerBakeOwnerToken.IsValid()
		&& CharacterProfile->LayerBakeOwnerToken != LayerAsset->BakeSetId)
	{
		return Fail(TEXT("Detach refused to release a Character Profile claimed by a competing bake set."));
	}

	TArray<UObject*> StampedObjects;
	TArray<FString> FreezeErrors;
	bool bCompetingOwnership = false;
	if (!CollectFreezeOutputObjects(
		*LayerAsset, StampedObjects, FreezeErrors, bCompetingOwnership))
	{
		return Fail(FreezeErrors.IsEmpty()
			? TEXT("Detach could not prove that every canonical output asset exists and is non-competing.")
			: FString::Join(FreezeErrors, TEXT(" ")));
	}

	TArray<FString> AcceptedMismatches;
	for (const FCharacterLayerAnimationBakeRecord& Record : LayerAsset->BakeManifest.Animations)
	{
		FString DigestError;
		const FString CurrentDigest = ComputeCurrentOutputDigest(
			LayerAsset, CharacterProfile, Record, &DigestError);
		if (CurrentDigest.IsEmpty() || CurrentDigest != Record.OutputDigest)
		{
			const FString Animation = Record.LegacyAnimationName.IsEmpty()
				? Record.Flipbook.ToSoftObjectPath().ToString() : Record.LegacyAnimationName;
			AcceptedMismatches.Add(FString::Printf(
				TEXT("%s [%s]"), *Animation,
				DigestError.IsEmpty()
					? *FString::Printf(TEXT("expected %s, accepted %s"),
						*Record.OutputDigest, *CurrentDigest)
					: *DigestError));
		}
	}

	LayerAsset->Modify();
	CharacterProfile->Modify();
	for (UObject* Object : StampedObjects)
	{
		if (!Object || !Object->GetOutermost()) continue;
		Object->Modify();
		for (FName Key : { OwnerKey, RevisionKey, RoleKey, TargetKey })
		{
			RemoveMetadataValue(Object->GetOutermost(), Object, Key);
		}
		Object->GetOutermost()->MarkPackageDirty();
	}
	if (CharacterProfile->LayerBakeOwnerToken == LayerAsset->BakeSetId)
	{
		CharacterProfile->LayerBakeOwnerToken.Invalidate();
		CharacterProfile->LayerBakeOwnerPathHint.Reset();
	}
	CharacterProfile->MarkPackageDirty();
	LayerAsset->BakeAttachmentState = ECharacterLayerBakeAttachmentState::Detached;
	LayerAsset->UsageMode = ECharacterLayerUsageMode::FixedBaked;
	Result.bSuccess = true;
	Result.Report = AcceptedMismatches.IsEmpty()
		? TEXT("Detached verified canonical output in place. No output was restored, deleted, or reverse-composed.")
		: FString::Printf(
			TEXT("Detached and froze current canonical output while explicitly accepting %d mismatch(es): %s. No output was restored, deleted, or reverse-composed."),
			AcceptedMismatches.Num(), *FString::Join(AcceptedMismatches, TEXT("; ")));
	Result.TouchedPackages = LayerAsset->BakeManifest.TouchedPackages;
	LayerAsset->LastBakeOperation = MakeOperationRecord(
		ECharacterLayerBakeLifecycleOperation::Detach, Result.Report, true);
	LayerAsset->LastBakeOperation.TouchedPackages = Result.TouchedPackages;
	LayerAsset->MarkPackageDirty();
	return Result;
}
