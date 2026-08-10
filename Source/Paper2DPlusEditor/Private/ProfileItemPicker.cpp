// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfileItemPicker.h"

#include "ProfileNavigatorPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Crc.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "ProfileItemPicker"

namespace ProfileItemPickerPrivate
{
	const TCHAR* ConfigRoot = TEXT("Paper2DPlus.ProfilePicker");

	FString NormalizedFallback(const FString& Value)
	{
		return Value.TrimStartAndEnd().ToLower();
	}

	bool IdentityArrayContains(const TArray<FProfileItemIdentity>& Values, const FProfileItemIdentity& Candidate)
	{
		return Values.ContainsByPredicate([&Candidate](const FProfileItemIdentity& Value)
		{
			return Value.Matches(Candidate);
		});
	}
}

bool FProfileItemIdentity::IsValid() const
{
	return !SourceType.IsNone() && (!ObjectPath.IsNull() || !FallbackKey.TrimStartAndEnd().IsEmpty());
}

FString FProfileItemIdentity::ToStableString() const
{
	return FString::Printf(TEXT("%s\t%s\t%s"), *SourceType.ToString(), *ObjectPath.ToString(), *FallbackKey);
}

bool FProfileItemIdentity::FromStableString(const FString& Serialized, FProfileItemIdentity& OutIdentity)
{
	TArray<FString> Fields;
	Serialized.ParseIntoArray(Fields, TEXT("\t"), false);
	if (Fields.Num() != 3)
	{
		return false;
	}

	FProfileItemIdentity Parsed;
	Parsed.SourceType = FName(*Fields[0]);
	Parsed.ObjectPath = FSoftObjectPath(Fields[1]);
	Parsed.FallbackKey = Fields[2];
	if (!Parsed.IsValid())
	{
		return false;
	}
	OutIdentity = MoveTemp(Parsed);
	return true;
}

bool FProfileItemIdentity::Matches(const FProfileItemIdentity& Other) const
{
	if (SourceType != Other.SourceType)
	{
		return false;
	}
	if (!ObjectPath.IsNull() && !Other.ObjectPath.IsNull())
	{
		return ObjectPath == Other.ObjectPath;
	}
	return !FallbackKey.IsEmpty() && !Other.FallbackKey.IsEmpty()
		&& ProfileItemPickerPrivate::NormalizedFallback(FallbackKey)
			== ProfileItemPickerPrivate::NormalizedFallback(Other.FallbackKey);
}

FString FProfilePickerItem::BuildSearchDocument() const
{
	TArray<FString> Terms;
	Terms.Reserve(5 + Aliases.Num() + SearchTags.Num());
	Terms.Add(Label.ToString());
	Terms.Add(SecondaryText.ToString());
	Terms.Add(Group);
	Terms.Add(Identity.FallbackKey);
	Terms.Add(Identity.ObjectPath.IsNull() ? FString() : Identity.ObjectPath.ToString());
	Terms.Append(Aliases);
	for (const FGameplayTag& Tag : SearchTags)
	{
		const FString Full = Tag.ToString();
		Terms.Add(Full);
		FString Left;
		FString Leaf;
		if (Full.Split(TEXT("."), &Left, &Leaf, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			Terms.Add(Leaf);
		}
	}
	return FString::Join(Terms, TEXT(" ")).ToLower();
}

FProfilePickerResultModel::FProfilePickerResultModel(TSharedPtr<IProfileItemPickerSource> InSource)
	: Source(MoveTemp(InSource))
{
	Refresh();
}

void FProfilePickerResultModel::SetQuery(const FString& InQuery)
{
	const FString Trimmed = InQuery.TrimStartAndEnd();
	if (Query.Equals(Trimmed, ESearchCase::CaseSensitive))
	{
		return;
	}
	Query = Trimmed;
	Refresh();
}

bool FProfilePickerResultModel::MatchesQuery(const FProfilePickerItem& Item) const
{
	if (Query.IsEmpty())
	{
		return true;
	}
	const FString Document = Item.BuildSearchDocument();
	TArray<FString> Tokens;
	Query.ToLower().ParseIntoArrayWS(Tokens);
	for (const FString& Token : Tokens)
	{
		if (!Document.Contains(Token, ESearchCase::CaseSensitive))
		{
			return false;
		}
	}
	return true;
}

void FProfilePickerResultModel::AppendSection(const FText& Header,
	const TArray<TSharedPtr<FProfilePickerItem>>& Items, bool bPinned, bool bRecent,
	TSet<FString>& InOutAlreadyProjected)
{
	TArray<TSharedPtr<FProfilePickerItem>> Unique;
	for (const TSharedPtr<FProfilePickerItem>& Item : Items)
	{
		if (!Item.IsValid() || !MatchesQuery(*Item))
		{
			continue;
		}
		const FString Stable = Item->Identity.ToStableString().ToLower();
		if (!InOutAlreadyProjected.Contains(Stable))
		{
			InOutAlreadyProjected.Add(Stable);
			Unique.Add(Item);
		}
	}
	if (Unique.IsEmpty())
	{
		return;
	}

	TSharedPtr<FProfilePickerDisplayRow> HeaderRow = MakeShared<FProfilePickerDisplayRow>();
	HeaderRow->Kind = FProfilePickerDisplayRow::EKind::Header;
	HeaderRow->Header = Header;
	Rows.Add(HeaderRow);
	for (const TSharedPtr<FProfilePickerItem>& Item : Unique)
	{
		TSharedPtr<FProfilePickerDisplayRow> Row = MakeShared<FProfilePickerDisplayRow>();
		Row->Item = Item;
		Row->bPinnedProjection = bPinned;
		Row->bRecentProjection = bRecent;
		Rows.Add(Row);
	}
}

void FProfilePickerResultModel::Refresh()
{
	Rows.Reset();
	AllItems.Reset();
	MatchingItems.Reset();
	bHasAnySourceItems = false;
	if (!Source.IsValid())
	{
		return;
	}

	TArray<FProfilePickerItem> Projected;
	Source->GetItems(Projected);
	Projected.RemoveAll([](const FProfilePickerItem& Item) { return !Item.Identity.IsValid(); });
	Projected.StableSort([](const FProfilePickerItem& A, const FProfilePickerItem& B)
	{
		const int32 AO = A.CanonicalOrder == INDEX_NONE ? MAX_int32 : A.CanonicalOrder;
		const int32 BO = B.CanonicalOrder == INDEX_NONE ? MAX_int32 : B.CanonicalOrder;
		return AO < BO;
	});
	bHasAnySourceItems = !Projected.IsEmpty();

	FProfilePickerCatalogStore& Store = FProfilePickerCatalogStore::Get();
	const FString Scope = Source->GetLogicalCatalogScope();
	Store.Prune(Scope, Projected);
	for (FProfilePickerItem& Item : Projected)
	{
		TSharedPtr<FProfilePickerItem> Shared = MakeShared<FProfilePickerItem>(MoveTemp(Item));
		AllItems.Add(Shared);
		if (MatchesQuery(*Shared))
		{
			MatchingItems.Add(Shared);
		}
	}

	auto ResolveOrdered = [this](const TArray<FProfileItemIdentity>& Identities)
	{
		TArray<TSharedPtr<FProfilePickerItem>> Resolved;
		for (const FProfileItemIdentity& Identity : Identities)
		{
			if (TSharedPtr<FProfilePickerItem> Item = Resolve(Identity))
			{
				Resolved.Add(Item);
			}
		}
		return Resolved;
	};

	TSet<FString> AlreadyProjected;
	AppendSection(LOCTEXT("PinnedSection", "Pinned"), ResolveOrdered(Store.GetPins(Scope)), true, false, AlreadyProjected);
	AppendSection(LOCTEXT("RecentSection", "Recent"), ResolveOrdered(Store.GetRecents(Scope)), false, true, AlreadyProjected);

	TArray<FString> GroupOrder;
	TMap<FString, TArray<TSharedPtr<FProfilePickerItem>>> ByGroup;
	for (const TSharedPtr<FProfilePickerItem>& Item : MatchingItems)
	{
		const FString Group = Item->Group.IsEmpty() ? LOCTEXT("Ungrouped", "Ungrouped").ToString() : Item->Group;
		if (!ByGroup.Contains(Group))
		{
			GroupOrder.Add(Group);
		}
		ByGroup.FindOrAdd(Group).Add(Item);
	}
	for (const FString& Group : GroupOrder)
	{
		AppendSection(FText::FromString(Group), ByGroup.FindChecked(Group), false, false, AlreadyProjected);
	}
}

TSharedPtr<FProfilePickerItem> FProfilePickerResultModel::Resolve(const FProfileItemIdentity& Identity) const
{
	for (const TSharedPtr<FProfilePickerItem>& Item : AllItems)
	{
		if (Item.IsValid() && Item->Identity.Matches(Identity))
		{
			return Item;
		}
	}
	return nullptr;
}

FProfilePickerCatalogStore& FProfilePickerCatalogStore::Get()
{
	static FProfilePickerCatalogStore Instance;
	return Instance;
}

FString FProfilePickerCatalogStore::SectionForScope(const FString& Scope)
{
	return FString::Printf(TEXT("%s.%08X"), ProfileItemPickerPrivate::ConfigRoot, FCrc::StrCrc32(*Scope));
}

void FProfilePickerCatalogStore::Normalize(TArray<FProfileItemIdentity>& Identities, int32 MaxCount)
{
	TArray<FProfileItemIdentity> Normalized;
	Normalized.Reserve(FMath::Min(Identities.Num(), MaxCount));
	for (const FProfileItemIdentity& Identity : Identities)
	{
		if (Identity.IsValid() && !ProfileItemPickerPrivate::IdentityArrayContains(Normalized, Identity))
		{
			Normalized.Add(Identity);
			if (Normalized.Num() == MaxCount)
			{
				break;
			}
		}
	}
	Identities = MoveTemp(Normalized);
}

FProfilePickerCatalogStore::FCatalogState& FProfilePickerCatalogStore::FindOrLoad(const FString& Scope)
{
	FCatalogState& State = States.FindOrAdd(Scope);
	if (State.bLoaded)
	{
		return State;
	}
	State.bLoaded = true;
	if (!GConfig)
	{
		return State;
	}

	const FString Section = SectionForScope(Scope);
	FString StoredScope;
	if (GConfig->GetString(*Section, TEXT("Scope"), StoredScope, GEditorPerProjectIni)
		&& !StoredScope.Equals(Scope, ESearchCase::CaseSensitive))
	{
		// CRC collisions are rare but representable. Never leak another logical catalog's state.
		return State;
	}
	TArray<FString> SerializedPins;
	TArray<FString> SerializedRecents;
	GConfig->GetArray(*Section, TEXT("Pins"), SerializedPins, GEditorPerProjectIni);
	GConfig->GetArray(*Section, TEXT("Recents"), SerializedRecents, GEditorPerProjectIni);
	for (const FString& Value : SerializedPins)
	{
		FProfileItemIdentity Identity;
		if (FProfileItemIdentity::FromStableString(Value, Identity)) State.Pins.Add(MoveTemp(Identity));
	}
	for (const FString& Value : SerializedRecents)
	{
		FProfileItemIdentity Identity;
		if (FProfileItemIdentity::FromStableString(Value, Identity)) State.Recents.Add(MoveTemp(Identity));
	}
	Normalize(State.Pins, MaxPins);
	Normalize(State.Recents, MaxRecents);
	return State;
}

void FProfilePickerCatalogStore::Save(const FString& Scope, const FCatalogState& State)
{
	if (!GConfig)
	{
		return;
	}
	TArray<FString> Pins;
	TArray<FString> Recents;
	for (const FProfileItemIdentity& Identity : State.Pins) Pins.Add(Identity.ToStableString());
	for (const FProfileItemIdentity& Identity : State.Recents) Recents.Add(Identity.ToStableString());
	const FString Section = SectionForScope(Scope);
	GConfig->SetString(*Section, TEXT("Scope"), *Scope, GEditorPerProjectIni);
	GConfig->SetArray(*Section, TEXT("Pins"), Pins, GEditorPerProjectIni);
	GConfig->SetArray(*Section, TEXT("Recents"), Recents, GEditorPerProjectIni);
	// Intentionally no explicit Flush: the engine persists EditorPerProject config safely.
}

const TArray<FProfileItemIdentity>& FProfilePickerCatalogStore::GetPins(const FString& Scope)
{
	return FindOrLoad(Scope).Pins;
}

const TArray<FProfileItemIdentity>& FProfilePickerCatalogStore::GetRecents(const FString& Scope)
{
	return FindOrLoad(Scope).Recents;
}

bool FProfilePickerCatalogStore::IsPinned(const FString& Scope, const FProfileItemIdentity& Identity)
{
	return ProfileItemPickerPrivate::IdentityArrayContains(FindOrLoad(Scope).Pins, Identity);
}

FText FProfilePickerCatalogStore::GetPinActionLabel(const FString& Scope, const FProfileItemIdentity& Identity)
{
	return IsPinned(Scope, Identity) ? LOCTEXT("UnpinAction", "Unpin") : LOCTEXT("PinAction", "Pin");
}

void FProfilePickerCatalogStore::TogglePin(const FString& Scope, const FProfileItemIdentity& Identity)
{
	if (!Identity.IsValid()) return;
	FCatalogState& State = FindOrLoad(Scope);
	const int32 Existing = State.Pins.IndexOfByPredicate([&Identity](const FProfileItemIdentity& Value)
	{
		return Value.Matches(Identity);
	});
	if (Existing == INDEX_NONE) State.Pins.Insert(Identity, 0);
	else State.Pins.RemoveAt(Existing);
	Normalize(State.Pins, MaxPins);
	Save(Scope, State);
	StoreChanged.Broadcast();
}

void FProfilePickerCatalogStore::AddRecent(const FString& Scope, const FProfileItemIdentity& Identity)
{
	if (!Identity.IsValid()) return;
	FCatalogState& State = FindOrLoad(Scope);
	State.Recents.RemoveAll([&Identity](const FProfileItemIdentity& Value) { return Value.Matches(Identity); });
	State.Recents.Insert(Identity, 0);
	Normalize(State.Recents, MaxRecents);
	Save(Scope, State);
	StoreChanged.Broadcast();
}

void FProfilePickerCatalogStore::Prune(const FString& Scope, const TArray<FProfilePickerItem>& ValidItems)
{
	FCatalogState& State = FindOrLoad(Scope);
	auto IsValidIdentity = [&ValidItems](const FProfileItemIdentity& Identity)
	{
		return ValidItems.ContainsByPredicate([&Identity](const FProfilePickerItem& Item)
		{
			return Item.Identity.Matches(Identity);
		});
	};
	const int32 OldPins = State.Pins.Num();
	const int32 OldRecents = State.Recents.Num();
	State.Pins.RemoveAll([&IsValidIdentity](const FProfileItemIdentity& Identity) { return !IsValidIdentity(Identity); });
	State.Recents.RemoveAll([&IsValidIdentity](const FProfileItemIdentity& Identity) { return !IsValidIdentity(Identity); });
	Normalize(State.Pins, MaxPins);
	Normalize(State.Recents, MaxRecents);
	if (OldPins != State.Pins.Num() || OldRecents != State.Recents.Num())
	{
		Save(Scope, State);
		StoreChanged.Broadcast();
	}
}

void FProfilePickerCatalogStore::ResetScopeForTests(const FString& Scope)
{
	States.Remove(Scope);
	if (GConfig)
	{
		GConfig->EmptySection(*SectionForScope(Scope), GEditorPerProjectIni);
	}
	StoreChanged.Broadcast();
}

void SProfileItemPicker::Construct(const FArguments& InArgs)
{
	Source = InArgs._Source;
	OnGenerateItemPreview = InArgs._OnGenerateItemPreview;
	EmptySelectionText = InArgs._EmptySelectionText.IsSet()
		? InArgs._EmptySelectionText
		: TAttribute<FText>(LOCTEXT("NoSelection", "Select animation…"));
	if (Source.IsValid())
	{
		SourceChangedHandle = Source->OnSourceChanged().AddLambda([this]()
		{
			RefreshCurrentLabel();
			Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
		});
	}
	RefreshCurrentLabel();

	ChildSlot
	[
		SAssignNew(ComboButton, SComboButton)
		.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
		.ContentPadding(FMargin(6, 3))
		.HasDownArrow(true)
		.ToolTipText(LOCTEXT("CompactPickerTip", "Choose the current animation. Search does not alter the full Animations browser."))
		.AccessibleText_Lambda([this]()
		{
			return FText::Format(LOCTEXT("CompactPickerAccessible", "Current animation: {0}. Open animation picker."),
				GetCurrentLabel());
		})
		.OnGetMenuContent(this, &SProfileItemPicker::BuildMenuContent)
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text(this, &SProfileItemPicker::GetCurrentLabel)
		]
	];
}

SProfileItemPicker::~SProfileItemPicker()
{
	if (Source.IsValid()) Source->OnSourceChanged().Remove(SourceChangedHandle);
}

FText SProfileItemPicker::GetCurrentLabel() const
{
	return CachedCurrentLabel.IsEmpty() ? EmptySelectionText.Get() : CachedCurrentLabel;
}

void SProfileItemPicker::RefreshCurrentLabel()
{
	CachedCurrentLabel = FText::GetEmpty();
	if (Source.IsValid())
	{
		const FProfileItemIdentity Selected = Source->GetSelectedIdentity();
		TArray<FProfilePickerItem> Items;
		Source->GetItems(Items);
		for (const FProfilePickerItem& Item : Items)
		{
			if (Item.Identity.Matches(Selected))
			{
				CachedCurrentLabel = Item.Label;
				return;
			}
		}
	}
}

TSharedRef<SWidget> SProfileItemPicker::BuildMenuContent()
{
	TWeakPtr<SComboButton> WeakCombo = ComboButton;
	return SNew(SBox)
		.WidthOverride(420.0f)
		.HeightOverride(460.0f)
		[
			SNew(SProfileNavigatorPanel)
			.Source(Source)
			.Mode(EProfileNavigatorMode::Popup)
			.OnGenerateItemPreview(OnGenerateItemPreview)
			.OnDismissed(FOnProfileNavigatorDismissed::CreateLambda([WeakCombo]()
			{
				if (TSharedPtr<SComboButton> Combo = WeakCombo.Pin()) Combo->SetIsOpen(false);
			}))
		];
}

#undef LOCTEXT_NAMESPACE
