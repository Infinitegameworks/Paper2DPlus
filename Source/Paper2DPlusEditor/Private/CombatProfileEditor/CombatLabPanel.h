// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Paper2DPlusCombatProfileTypes.h"
#include "Paper2DPlusTypes.h"
#include "Widgets/SCompoundWidget.h"

class FCombatProfileEditorSession;
class SCombatLabCanvas;
class STextBlock;
template <typename ItemType> class SComboBox;
class UPaperFlipbook;
struct FProfileItemIdentity;
struct FFlipbookProfileEntry;

struct FCombatLabParticipantState
{
	FName MoveName;
	int32 FrameIndex = 0;
	float PlaybackSeconds = 0.0f;
	FVector2D Position = FVector2D::ZeroVector;
	bool bFacingLeft = false;
	bool bPlaying = false;
};

struct FCombatLabEvaluation
{
	float Distance = 0.0f;
	int32 AttackHurtOverlapCount = 0;
	bool bAnyCollision = false;
	TArray<FWorldHitbox> AttackerBoxes;
	TArray<FWorldHitbox> DefenderBoxes;
};

DECLARE_MULTICAST_DELEGATE(FOnCombatLabChanged);

/** Worldless, two-participant Combat Lab state and collision/scoring bridge. */
class FCombatLabModel final : public TSharedFromThis<FCombatLabModel>
{
public:
	explicit FCombatLabModel(TSharedPtr<FCombatProfileEditorSession> InSession);
	~FCombatLabModel();

	const FCombatLabParticipantState& GetParticipant(int32 Index) const;
	int32 GetSelectedParticipant() const { return SelectedParticipant; }
	const FCombatLabEvaluation& GetEvaluation() const { return Evaluation; }
	TSharedPtr<FCombatProfileEditorSession> GetSession() const { return Session; }
	const FFlipbookProfileEntry* GetEntry(int32 Index) const;
	UPaperFlipbook* GetFlipbook(int32 Index) const;
	int32 GetFrameCount(int32 Index) const;
	/** Base drag position plus the authored cumulative root-motion offset at the selected frame. */
	FVector2D GetParticipantPreviewPosition(int32 Index) const;

	void SetMove(int32 Index, FName MoveName);
	void SetFrame(int32 Index, int32 FrameIndex);
	void SetPosition(int32 Index, FVector2D Position);
	void SetPlaying(int32 Index, bool bPlaying);
	void SetSelectedParticipant(int32 Index);
	void Advance(float DeltaSeconds);
	void RefreshEvaluation();

	void CaptureIntoPreset(FPaper2DPlusCombatScenarioPreset& Preset) const;
	void ApplyPreset(const FPaper2DPlusCombatScenarioPreset& Preset);
	FOnCombatLabChanged& OnChanged() { return Changed; }

private:
	void Evaluate(bool bRefreshScores);
	void HandleSessionDataChanged();
	void HandleSessionSelectionChanged(const FProfileItemIdentity& Identity);

	TSharedPtr<FCombatProfileEditorSession> Session;
	FCombatLabParticipantState Participants[2];
	int32 SelectedParticipant = 0;
	FCombatLabEvaluation Evaluation;
	FOnCombatLabChanged Changed;
	FDelegateHandle SessionDataChangedHandle;
	FDelegateHandle SessionSelectionChangedHandle;
};

/** Designer controls around the worldless Combat Lab canvas. */
class SCombatLabPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCombatLabPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FCombatLabModel>, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SCombatLabPanel() override;
	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;

	FText GetStatusText() const;
	FText GetScoreExplanationText() const;
	TSharedPtr<FCombatLabModel> GetModelForTests() const { return Model; }

private:
	TSharedRef<SWidget> BuildParticipantControls(int32 ParticipantIndex);
	void RefreshMoveOptions();
	void RefreshControls();
	FReply TogglePlaying(int32 ParticipantIndex);
	FText GetPlayLabel(int32 ParticipantIndex) const;

	TSharedPtr<FCombatLabModel> Model;
	TSharedPtr<SCombatLabCanvas> Canvas;
	TArray<TSharedPtr<FName>> MoveOptions;
	TSharedPtr<SComboBox<TSharedPtr<FName>>> MoveCombos[2];
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<STextBlock> ScoreExplanationText;
	FDelegateHandle ChangedHandle;
	FDelegateHandle SessionDataHandle;
	FDelegateHandle SessionScoreHandle;
};
