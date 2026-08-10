// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FCombatProfileEditorSession;
class UPaper2DPlusCharacterProfileAsset;
struct FAssetData;

/** Compact Combat setup strip: relationship, status, navigation, and generate-missing. */
class SCombatProfileSetupPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCombatProfileSetupPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FCombatProfileEditorSession>, Session)
		SLATE_EVENT(FSimpleDelegate, OnValidate)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SCombatProfileSetupPanel() override;

	FText GetStatusText() const;
	void SetCharacterProfileForTests(UPaper2DPlusCharacterProfileAsset* CharacterProfile);
	int32 AdoptSparseVariableOverridesForTests();

private:
	FString GetCharacterProfilePath() const;
	void HandleCharacterProfileChanged(const FAssetData& AssetData);
	FReply OpenCharacterProfile();
	FReply GenerateMissing();
	FReply AdoptSparseVariableOverrides();
	FReply RunValidation();
	EVisibility GetLegacyVariableReviewVisibility() const;

	TSharedPtr<FCombatProfileEditorSession> Session;
	FSimpleDelegate OnValidate;
	FDelegateHandle SessionChangedHandle;
};
