// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Paper2DPlus::BulkFolderOrganization
{
	/**
	 * Return stable folder names for tag paths that are direct children of
	 * Paper2DPlus.Animation. The root itself, deeper descendants, and unrelated tags are ignored.
	 */
	TArray<FString> BuildAnimationGroupFolderNames(const TArray<FString>& TagPaths);

	/** Read the direct child groups from the live Paper2DPlus.Animation gameplay-tag tree. */
	TArray<FString> GetLiveAnimationGroupFolderNames();

	/** Return candidate folders that do not already exist at the organizer root (case-insensitive). */
	TArray<FString> FindMissingRootFolderNames(
		const TArray<FString>& CandidateNames,
		const TArray<FString>& ExistingRootNames);

	// ------------------------------------------------------------------
	// Export / Import of a whole bulk-extractor folder organization.
	//
	// Deliberately Slate-free and UObject-free: the window converts between its FBulkFolderNode
	// tree and these records, and everything below can be round-tripped headlessly.
	// ------------------------------------------------------------------

	/** One folder in an exported organizer tree. Children are shared pointers so the record can
	 *  nest itself without relying on TArray-of-incomplete-type behaviour. */
	struct FBulkFolderRecord
	{
		/** Single path segment (never a path). */
		FString Name;
		/** Texture DISPLAY NAMES filed directly in this folder (not in a sub-folder). */
		TArray<FString> Items;
		TArray<TSharedPtr<FBulkFolderRecord>> Children;
	};

	/** One texture row captured by an export. AssetPath is a matching aid only — the contract in
	 *  the payload is the display NAME, because a hand-authored capture carries nothing else. */
	struct FBulkTextureRecord
	{
		FString DisplayName;
		FString SubfolderPath;
		FString AssetPath;
	};

	/** The folder-naming half of the organizer's settings block.
	 *  bPresentInPayload distinguishes "the file said use these values" from "the file carried no
	 *  settings block at all, so these are struct defaults". The hand-captured `{"tree": {...}}`
	 *  shape has no settings object, and importing one must NOT silently reset the user's naming
	 *  configuration to defaults. Only apply these fields when the flag is true. */
	struct FBulkFolderNamingSettings
	{
		bool bPresentInPayload = false;
		FString OutputPath;
		bool bIncludePrefixInFolder = true;
		bool bIncludeSuffixInFolder = false;
		bool bIncludeBatchSuffix = false;
		FString FolderPrefix;
		FString FolderSuffix;
		FString Find;
		FString Replace;
		FString RemoveText;
	};

	/** A complete organization payload: settings + the nested folder tree + per-texture rows. */
	struct FBulkFolderOrganizationSnapshot
	{
		FBulkFolderNamingSettings Naming;
		TArray<TSharedPtr<FBulkFolderRecord>> Folders;
		/** Display names deliberately left outside every folder. */
		TArray<FString> Unassigned;
		TArray<FBulkTextureRecord> Textures;
		/** Older display name -> the name used inside this payload's tree. Import consults the
		 *  INVERSE of this map when a tree entry does not match any current row. */
		TMap<FString, FString> Renames;
	};

	/** Serialize to pretty JSON. Folders are written as an ORDERED ARRAY so a round-trip preserves
	 *  the tree order a JSON object cannot guarantee. */
	FString SerializeOrganizationSnapshot(const FBulkFolderOrganizationSnapshot& Snapshot);

	/** Parse either shape:
	 *   - the exported ordered array  ("folders": [ { "name", "items", "children" } ])
	 *   - the hand-captured object    ("tree": { "Locomotion": { "_items": [...], "Sub": {...} } })
	 *  Keys beginning with '_' are metadata and are ignored (so "_comment"/"_note"/"_UNVERIFIED"
	 *  annotations in a hand-authored capture never become folders), except "_items".
	 *  Object-shaped folders are visited in case-insensitive name order so an import is
	 *  deterministic even though JSON object key order is not.
	 *  Returns false with OutError set when the document is not readable JSON. */
	bool DeserializeOrganizationSnapshot(
		const FString& JsonText,
		FBulkFolderOrganizationSnapshot& OutSnapshot,
		FString& OutError);

	/** Flatten a snapshot's tree to "display name -> folder path" (no per-texture leaf segment).
	 *  A name appearing in more than one folder keeps its FIRST occurrence and is reported in
	 *  OutDuplicates, so an import can tell the user instead of silently picking one. */
	void FlattenSnapshotAssignments(
		const FBulkFolderOrganizationSnapshot& Snapshot,
		TMap<FString, FString>& OutNameToFolderPath,
		TArray<FString>& OutDuplicates);
}
