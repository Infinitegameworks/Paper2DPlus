// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

/**
 * A bounded least-recently-used retention budget keyed by FSoftObjectPath.
 *
 * THE shared budget for virtualized card surfaces that stream art for the rows currently on screen
 * (the Catalog Roster's characters, the Combat browser's attacks). Each panel keeps its OWN load
 * graph — the Roster resolves a Character Profile and then its flipbook, the Combat browser resolves
 * one flipbook — because those genuinely differ. What does not differ, and what had drifted into two
 * implementations with different eviction cost, is "retain at most N, evict the least recently
 * touched": that is this class.
 *
 * PayloadType is whatever the owning panel needs to keep alive per key (a streamable handle, a small
 * state struct holding handles and keep-alives). This class never interprets it; it only decides who
 * is retained. Eviction destroys the payload, which is what releases the handles it holds.
 *
 * Touch() is O(n) in the retained set and eviction is O(1). With a budget in the tens — where it
 * belongs, since it exists to bound what is on screen — that is cheaper than the serial-scan
 * alternative it replaces, and it never allocates in the steady state.
 */
template <typename PayloadType>
class TProfileThumbnailBudget
{
public:
	/**
	 * InOnRelease runs for every payload leaving the budget, whether through eviction or Reset, and
	 * runs BEFORE the payload is destroyed. Releasing a streamable handle does not cancel an in-flight
	 * load, so a panel that streams art gives its cancel here — otherwise trimming an off-screen card
	 * would drop the retention while the load kept running.
	 */
	explicit TProfileThumbnailBudget(
		int32 InCapacity,
		TFunction<void(PayloadType&)> InOnRelease = nullptr)
		: OnRelease(MoveTemp(InOnRelease))
		, Capacity(FMath::Max(1, InCapacity))
	{
	}

	/**
	 * Mark Key as most recently used, inserting a default-constructed payload if it is new, then trim
	 * back to capacity. Returns the retained payload by reference.
	 *
	 * The reference is invalidated by any later Touch/Reset that evicts this key, so callers should
	 * finish with it before requesting another.
	 */
	PayloadType& Touch(const FSoftObjectPath& Key)
	{
		check(!Key.IsNull());
		Order.Remove(Key);
		Order.Add(Key);
		PayloadType& Payload = Payloads.FindOrAdd(Key);
		Trim();
		return Payloads.FindChecked(Key);
	}

	/**
	 * Install the release hook after construction. Prefer this when the payload is only
	 * forward-declared where the budget is DECLARED — a hook written at the member declaration would
	 * force the owning header to pull in the payload's full definition just to call into it.
	 */
	void SetOnRelease(TFunction<void(PayloadType&)> InOnRelease)
	{
		OnRelease = MoveTemp(InOnRelease);
	}

	PayloadType* Find(const FSoftObjectPath& Key) { return Payloads.Find(Key); }
	const PayloadType* Find(const FSoftObjectPath& Key) const { return Payloads.Find(Key); }
	bool Contains(const FSoftObjectPath& Key) const { return Payloads.Contains(Key); }
	int32 Num() const { return Payloads.Num(); }
	int32 GetCapacity() const { return Capacity; }

	void Reset()
	{
		if (OnRelease)
		{
			for (TPair<FSoftObjectPath, PayloadType>& Pair : Payloads)
			{
				OnRelease(Pair.Value);
			}
		}
		Payloads.Reset();
		Order.Reset();
	}

private:
	void Trim()
	{
		// Order[0] is the least recently touched, so eviction never scans.
		while (Order.Num() > Capacity)
		{
			const FSoftObjectPath Evicted = Order[0];
			Order.RemoveAt(0);
			if (OnRelease)
			{
				if (PayloadType* Payload = Payloads.Find(Evicted))
				{
					OnRelease(*Payload);
				}
			}
			Payloads.Remove(Evicted);
		}
	}

	TMap<FSoftObjectPath, PayloadType> Payloads;
	TArray<FSoftObjectPath> Order;
	TFunction<void(PayloadType&)> OnRelease;
	int32 Capacity = 1;
};
