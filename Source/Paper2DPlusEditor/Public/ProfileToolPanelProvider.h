// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Templates/Function.h"
#include "UObject/SoftObjectPath.h"

class SWidget;

/**
 * Stable identity for one animation row in a Character Profile.
 *
 * Editor widgets must not retain a Flipbooks[] index across a menu, drag, transaction, undo, reimport,
 * or external refresh. The soft flipbook path is the primary key (and works while the object is unloaded);
 * the authored animation name is the fallback for name-only/PaperZD-only entries. Providers resolve this
 * identity against the LIVE profile at action time.
 */
struct PAPER2DPLUSEDITOR_API FProfileAnimationIdentity
{
	FSoftObjectPath FlipbookPath;
	FString FallbackName;
	/** Session-stable owner guard. A delayed menu/drag captured for one Base Profile must expire when
	 *  the shared editor model is reinitialized to another profile with an identical animation name. */
	TWeakObjectPtr<const UPaper2DPlusCharacterProfileAsset> OwnerProfile;
	bool bHasOwnerProfile = false;

	FProfileAnimationIdentity() = default;
	FProfileAnimationIdentity(
		FSoftObjectPath InFlipbookPath,
		FString InFallbackName,
		const UPaper2DPlusCharacterProfileAsset* InOwnerProfile = nullptr)
		: FlipbookPath(MoveTemp(InFlipbookPath))
		, FallbackName(MoveTemp(InFallbackName))
		, OwnerProfile(InOwnerProfile)
		, bHasOwnerProfile(InOwnerProfile != nullptr)
	{
	}

	bool IsValid() const
	{
		return FlipbookPath.IsValid() || !FallbackName.IsEmpty();
	}

	bool HasObjectPath() const { return FlipbookPath.IsValid(); }

	FString ToDebugString() const
	{
		return FlipbookPath.IsValid() ? FlipbookPath.ToString() : FallbackName;
	}

	friend bool operator==(const FProfileAnimationIdentity& A, const FProfileAnimationIdentity& B)
	{
		if (A.bHasOwnerProfile != B.bHasOwnerProfile
			|| (A.bHasOwnerProfile && A.OwnerProfile != B.OwnerProfile))
		{
			return false;
		}
		if (A.FlipbookPath.IsValid() || B.FlipbookPath.IsValid())
		{
			return A.FlipbookPath.IsValid()
				&& B.FlipbookPath.IsValid()
				&& A.FlipbookPath == B.FlipbookPath;
		}
		return A.FallbackName.Equals(B.FallbackName, ESearchCase::IgnoreCase);
	}

	friend bool operator!=(const FProfileAnimationIdentity& A, const FProfileAnimationIdentity& B)
	{
		return !(A == B);
	}

	friend uint32 GetTypeHash(const FProfileAnimationIdentity& Identity)
	{
		const uint32 AnimationHash = Identity.FlipbookPath.IsValid()
			? GetTypeHash(Identity.FlipbookPath)
			: GetTypeHash(Identity.FallbackName.ToLower());
		return Identity.bHasOwnerProfile
			? HashCombine(AnimationHash, GetTypeHash(Identity.OwnerProfile))
			: AnimationHash;
	}
};

/** Stable selected Layer scope used to reject a delayed action after the designer changes source owner. */
struct PAPER2DPLUSEDITOR_API FProfileLayerScopeIdentity
{
	bool bLayerScoped = false;
	/** Stable source owner for Layer-scoped tools. */
	FGuid LayerId;

	static FProfileLayerScopeIdentity Profile()
	{
		return {};
	}

	static FProfileLayerScopeIdentity Layer(const FGuid& InLayerId)
	{
		FProfileLayerScopeIdentity Result;
		Result.bLayerScoped = true;
		Result.LayerId = InLayerId;
		return Result;
	}

	bool IsResolved() const
	{
		return !bLayerScoped || LayerId.IsValid();
	}

	friend bool operator==(const FProfileLayerScopeIdentity& A, const FProfileLayerScopeIdentity& B)
	{
		return A.bLayerScoped == B.bLayerScoped && A.LayerId == B.LayerId;
	}

	friend bool operator!=(const FProfileLayerScopeIdentity& A, const FProfileLayerScopeIdentity& B)
	{
		return !(A == B);
	}
};

/** Composite identity for a delayed tool action: stable animation plus the Profile/Layer edit scope. */
struct PAPER2DPLUSEDITOR_API FProfileScopedAnimationIdentity
{
	FProfileAnimationIdentity Animation;
	FProfileLayerScopeIdentity LayerScope;

	bool IsValid() const
	{
		return Animation.IsValid() && LayerScope.IsResolved();
	}
};

/** One canonical identity factory/resolver shared by every Profile tool provider. */
namespace Paper2DPlusProfileToolProvider
{
	inline FProfileAnimationIdentity MakeAnimationIdentity(
		const UPaper2DPlusCharacterProfileAsset* Profile,
		int32 FlipbookIndex)
	{
		if (!Profile || !Profile->Flipbooks.IsValidIndex(FlipbookIndex))
		{
			return {};
		}
		const FFlipbookIdentity& Identity = Profile->Flipbooks[FlipbookIndex].Identity;
		return FProfileAnimationIdentity(
			Identity.Flipbook.ToSoftObjectPath(), Identity.FlipbookName, Profile);
	}

	inline int32 ResolveAnimationIndex(
		const UPaper2DPlusCharacterProfileAsset* Profile,
		const FProfileAnimationIdentity& Identity)
	{
		if (!Profile || !Identity.IsValid())
		{
			return INDEX_NONE;
		}
		if (Identity.bHasOwnerProfile && Identity.OwnerProfile.Get() != Profile)
		{
			return INDEX_NONE;
		}

		if (Identity.FlipbookPath.IsValid())
		{
			int32 PathCandidate = INDEX_NONE;
			int32 PathMatchCount = 0;
			for (int32 Index = 0; Index < Profile->Flipbooks.Num(); ++Index)
			{
				if (Profile->Flipbooks[Index].Identity.Flipbook.ToSoftObjectPath() == Identity.FlipbookPath)
				{
					PathCandidate = Index;
					++PathMatchCount;
				}
			}
			if (PathMatchCount == 1)
			{
				return PathCandidate;
			}
			if (PathMatchCount > 1 && !Identity.FallbackName.IsEmpty())
			{
				int32 ExactCandidate = INDEX_NONE;
				int32 ExactMatchCount = 0;
				for (int32 Index = 0; Index < Profile->Flipbooks.Num(); ++Index)
				{
					const FFlipbookIdentity& Candidate = Profile->Flipbooks[Index].Identity;
					if (Candidate.Flipbook.ToSoftObjectPath() == Identity.FlipbookPath
						&& Candidate.FlipbookName.Equals(
							Identity.FallbackName, ESearchCase::IgnoreCase))
					{
						ExactCandidate = Index;
						++ExactMatchCount;
					}
				}
				return ExactMatchCount == 1 ? ExactCandidate : INDEX_NONE;
			}
			if (PathMatchCount > 1)
			{
				return INDEX_NONE;
			}
		}

		if (!Identity.FallbackName.IsEmpty())
		{
			int32 NameCandidate = INDEX_NONE;
			int32 NameMatchCount = 0;
			for (int32 Index = 0; Index < Profile->Flipbooks.Num(); ++Index)
			{
				if (Profile->Flipbooks[Index].Identity.FlipbookName.Equals(
					Identity.FallbackName, ESearchCase::IgnoreCase))
				{
					NameCandidate = Index;
					++NameMatchCount;
				}
			}
			return NameMatchCount == 1 ? NameCandidate : INDEX_NONE;
		}
		return INDEX_NONE;
	}
}

/**
 * A tool panel has exactly one navigation/layout owner. Embedded mode retains the legacy self-contained
 * layout; External mode participates in a workspace whose shared navigator and Details host live outside the
 * tool. An enum, rather than two booleans, makes the invalid "both embedded and external" state
 * unrepresentable.
 */
enum class EProfileToolPanelHostMode : uint8
{
	Embedded,
	External
};

struct PAPER2DPLUSEDITOR_API FProfileToolPanelHostContract
{
	static FProfileToolPanelHostContract Embedded()
	{
		return { EProfileToolPanelHostMode::Embedded };
	}

	static FProfileToolPanelHostContract External()
	{
		return { EProfileToolPanelHostMode::External };
	}

	bool OwnsEmbeddedNavigation() const { return Mode == EProfileToolPanelHostMode::Embedded; }
	bool UsesExternalNavigation() const { return Mode == EProfileToolPanelHostMode::External; }
	bool IsValid() const { return OwnsEmbeddedNavigation() != UsesExternalNavigation(); }

	EProfileToolPanelHostMode Mode = EProfileToolPanelHostMode::Embedded;
};

/**
 * Domain-neutral description of one contextual panel supplied by an active Profile tool.
 *
 * The one factory is host-agnostic: embedded and external hosts consume the same controller/view builder.
 * CapabilityId is a stable diagnostic/feature key (empty means unconditional); IsAvailable is evaluated at
 * construction time so a Layer/Profile scope change can hide a capability without rebuilding the descriptor.
 */
struct PAPER2DPLUSEDITOR_API FProfileToolPanelDescriptor
{
	FName PanelId;
	FText Label;
	/** AppStyle brush key, intentionally kept as data so this seam does not own a style set. */
	FName IconName;
	FText ToolTip;
	FName CapabilityId;
	TFunction<bool()> IsAvailable;
	TFunction<TSharedRef<SWidget>()> WidgetFactory;

	bool IsValid() const
	{
		return !PanelId.IsNone() && !Label.IsEmpty() && static_cast<bool>(WidgetFactory);
	}

	bool IsAvailableNow() const
	{
		return IsValid() && (!IsAvailable || IsAvailable());
	}

	/** Null when invalid/unavailable. Calls the sole factory exactly once otherwise. */
	TSharedPtr<SWidget> TryCreateWidget() const
	{
		if (!IsAvailableNow())
		{
			return nullptr;
		}
		return WidgetFactory();
	}
};

/**
 * Small provider interface consumed by the workspace host. It declares panel descriptors and owns one
 * embedded-or-external contract; Details-category presentation, persistence, ordering policy, and tool-specific
 * behavior live outside this seam. Concrete Hitbox/Frame Cue builders are added by their dedicated rehost units.
 */
class PAPER2DPLUSEDITOR_API IProfileToolPanelProvider
{
public:
	virtual ~IProfileToolPanelProvider() = default;
	virtual FProfileToolPanelHostContract GetHostContract() const = 0;
	virtual void GetContextualPanels(TArray<FProfileToolPanelDescriptor>& OutPanels) const = 0;
};
