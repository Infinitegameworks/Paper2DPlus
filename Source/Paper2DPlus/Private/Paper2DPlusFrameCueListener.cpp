// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusFrameCueListener.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

UPaper2DPlusFrameCueListener* UPaper2DPlusFrameCueListener::ListenForFrameCue(
	UObject* WorldContextObject,
	UPaper2DPlusCharacterProfileComponent* ProfileComponent,
	TSubclassOf<UPaper2DPlusCueBase> CueClass,
	FGameplayTag CueTag,
	bool bExactClass,
	bool bExactTag)
{
	UPaper2DPlusFrameCueListener* Listener = NewObject<UPaper2DPlusFrameCueListener>();
	Listener->WorldContext = WorldContextObject;
	Listener->SourceComponent = ProfileComponent;
	Listener->FilterClass = CueClass;
	Listener->FilterTag = CueTag;
	Listener->bFilterExactClass = bExactClass;
	Listener->bFilterExactTag = bExactTag;
	// Registering an async action with a transient/worldless context makes the engine emit a
	// misleading "No world was found" warning (and buys us no lifetime management). Runtime callers
	// still register normally; worldless automation/native callers are owned by their regular UObject
	// references and can explicitly Cancel the listener.
	if (WorldContextObject && GEngine &&
		GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		Listener->RegisterWithGameInstance(WorldContextObject);
	}
	return Listener;
}

void UPaper2DPlusFrameCueListener::Activate()
{
	if (bListening)
	{
		return;
	}

	if (!IsValid(SourceComponent) || !FilterClass)
	{
		SetReadyToDestroy();
		return;
	}

	bListening = true;
	SourceComponent->OnFrameCue.AddDynamic(this, &UPaper2DPlusFrameCueListener::HandleCue);
	SourceEndedHandle = SourceComponent->OnFrameCueSourceEndedNative.AddUObject(
		this, &UPaper2DPlusFrameCueListener::HandleSourceEnded);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(
		this, &UPaper2DPlusFrameCueListener::HandleWorldCleanup);
}

void UPaper2DPlusFrameCueListener::Cancel()
{
	Unbind();
	SetReadyToDestroy();
}

void UPaper2DPlusFrameCueListener::BeginDestroy()
{
	Unbind();
	Super::BeginDestroy();
}

bool UPaper2DPlusFrameCueListener::PassesFilter(const UPaper2DPlusCueBase& Cue) const
{
	const UClass* DesiredClass = FilterClass.Get();
	if (!DesiredClass)
	{
		return false;
	}

	const bool bClassMatches = bFilterExactClass
		? Cue.GetClass() == DesiredClass
		: Cue.IsA(DesiredClass);
	if (!bClassMatches)
	{
		return false;
	}

	if (!FilterTag.IsValid())
	{
		return true;
	}
	return bFilterExactTag ? Cue.CueTag == FilterTag : Cue.CueTag.MatchesTag(FilterTag);
}

void UPaper2DPlusFrameCueListener::HandleCue(
	UPaper2DPlusCueBase* Cue,
	const FPaper2DPlusFrameCueContext& Context)
{
	if (!IsValid(Cue) || !PassesFilter(*Cue))
	{
		return;
	}

	switch (Context.Phase)
	{
	case EPaper2DPlusFrameCuePhase::Trigger:
		Triggered.Broadcast(Cue, Context);
		break;
	case EPaper2DPlusFrameCuePhase::Begin:
		Began.Broadcast(Cue, Context);
		break;
	case EPaper2DPlusFrameCuePhase::Update:
		Updated.Broadcast(Cue, Context);
		break;
	case EPaper2DPlusFrameCuePhase::End:
		Ended.Broadcast(Cue, Context);
		break;
	}
}

void UPaper2DPlusFrameCueListener::HandleSourceEnded()
{
	Cancel();
}

void UPaper2DPlusFrameCueListener::HandleWorldCleanup(
	UWorld* World,
	bool bSessionEnded,
	bool bCleanupResources)
{
	UWorld* SourceWorld = SourceComponent ? SourceComponent->GetWorld() : nullptr;
	UWorld* ContextWorld = GEngine && WorldContext
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (World && (World == SourceWorld || World == ContextWorld))
	{
		Cancel();
	}
}

void UPaper2DPlusFrameCueListener::Unbind()
{
	if (!bListening)
	{
		return;
	}

	bListening = false;
	if (SourceComponent)
	{
		SourceComponent->OnFrameCue.RemoveDynamic(this, &UPaper2DPlusFrameCueListener::HandleCue);
		SourceComponent->OnFrameCueSourceEndedNative.Remove(SourceEndedHandle);
	}
	SourceEndedHandle.Reset();
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	WorldCleanupHandle.Reset();
}
