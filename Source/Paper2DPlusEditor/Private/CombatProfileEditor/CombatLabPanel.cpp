// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CombatProfileEditor/CombatLabPanel.h"

#include "CombatProfileEditor/CombatLabCanvas.h"
#include "CombatProfileEditor/CombatProfileEditorSession.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusFrameGeometry.h"
#include "Paper2DPlusHitboxSubsystem.h"
#include "PaperFlipbook.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSlider.h"
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

#define LOCTEXT_NAMESPACE "CombatLabPanel"

namespace CombatLabPrivate
{
	const FCombatLabParticipantState EmptyParticipant;

	bool IsDamagePair(EHitboxType A, EHitboxType B)
	{
		return (A == EHitboxType::Attack && B == EHitboxType::Hurtbox)
			|| (A == EHitboxType::Hurtbox && B == EHitboxType::Attack);
	}
}

FCombatLabModel::FCombatLabModel(TSharedPtr<FCombatProfileEditorSession> InSession)
	: Session(MoveTemp(InSession))
{
	Participants[0].Position = FVector2D(-75.0f, 0.0f);
	Participants[1].Position = FVector2D(75.0f, 0.0f);
	Participants[1].bFacingLeft = true;
	if (Session.IsValid())
	{
		if (Session->GetCatalog().Num() > 0)
		{
			Participants[0].MoveName = Session->GetCatalog()[0].MoveName;
			Participants[1].MoveName = Session->GetCatalog().Num() > 1
				? Session->GetCatalog()[1].MoveName
				: Participants[0].MoveName;
			Session->SelectAttackByName(Participants[0].MoveName);
		}
		SessionDataChangedHandle = Session->OnDataChanged().AddRaw(
			this, &FCombatLabModel::HandleSessionDataChanged);
		SessionSelectionChangedHandle = Session->OnSelectionChanged().AddRaw(
			this, &FCombatLabModel::HandleSessionSelectionChanged);
	}
	Evaluate(true);
}

FCombatLabModel::~FCombatLabModel()
{
	if (Session.IsValid())
	{
		Session->OnDataChanged().Remove(SessionDataChangedHandle);
		Session->OnSelectionChanged().Remove(SessionSelectionChangedHandle);
	}
}

const FCombatLabParticipantState& FCombatLabModel::GetParticipant(int32 Index) const
{
	return Index == 0 || Index == 1 ? Participants[Index] : CombatLabPrivate::EmptyParticipant;
}

const FFlipbookProfileEntry* FCombatLabModel::GetEntry(int32 Index) const
{
	const UPaper2DPlusCombatProfileAsset* Combat = Session.IsValid() ? Session->GetAsset() : nullptr;
	const UPaper2DPlusCharacterProfileAsset* Character = Combat ? Combat->CharacterProfile : nullptr;
	if (!Character || (Index != 0 && Index != 1) || Participants[Index].MoveName.IsNone()) return nullptr;
	return Character->FindFlipbookDataPtr(Participants[Index].MoveName.ToString());
}

UPaperFlipbook* FCombatLabModel::GetFlipbook(int32 Index) const
{
	const FFlipbookProfileEntry* Entry = GetEntry(Index);
	return Entry ? Entry->Identity.Flipbook.Get() : nullptr;
}

int32 FCombatLabModel::GetFrameCount(int32 Index) const
{
	const UPaperFlipbook* Flipbook = GetFlipbook(Index);
	if (Flipbook && Flipbook->GetNumKeyFrames() > 0) return Flipbook->GetNumKeyFrames();
	const FFlipbookProfileEntry* Entry = GetEntry(Index);
	return Entry ? Entry->CombatData.Frames.Num() : 0;
}

void FCombatLabModel::SetMove(int32 Index, FName MoveName)
{
	if (Index != 0 && Index != 1) return;
	if (Participants[Index].MoveName == MoveName) return;
	Participants[Index].MoveName = MoveName;
	Participants[Index].FrameIndex = 0;
	Participants[Index].PlaybackSeconds = 0.0f;
	if (Index == 0 && Session.IsValid()) Session->SelectAttackByName(MoveName);
	Evaluate(true);
}

void FCombatLabModel::SetFrame(int32 Index, int32 FrameIndex)
{
	if (Index != 0 && Index != 1) return;
	const int32 Count = GetFrameCount(Index);
	const int32 Clamped = Count > 0 ? FMath::Clamp(FrameIndex, 0, Count - 1) : 0;
	if (Participants[Index].FrameIndex == Clamped) return;
	Participants[Index].FrameIndex = Clamped;
	Evaluate(false);
}

void FCombatLabModel::SetPosition(int32 Index, FVector2D Position)
{
	if (Index != 0 && Index != 1 || Participants[Index].Position.Equals(Position)) return;
	Participants[Index].Position = Position;
	Evaluate(true);
}

void FCombatLabModel::SetPlaying(int32 Index, bool bPlaying)
{
	if (Index != 0 && Index != 1 || Participants[Index].bPlaying == bPlaying) return;
	Participants[Index].bPlaying = bPlaying;
	Changed.Broadcast();
}

void FCombatLabModel::SetSelectedParticipant(int32 Index)
{
	if ((Index != 0 && Index != 1) || SelectedParticipant == Index) return;
	SelectedParticipant = Index;
	Changed.Broadcast();
}

void FCombatLabModel::HandleSessionSelectionChanged(const FProfileItemIdentity& Identity)
{
	if (!Identity.IsValid())
	{
		if (Participants[0].MoveName.IsNone()) return;
		Participants[0].MoveName = NAME_None;
		Participants[0].FrameIndex = 0;
		Participants[0].PlaybackSeconds = 0.0f;
		Evaluate(false);
		return;
	}
	const FName MoveName(*Identity.FallbackKey);
	if (MoveName.IsNone() || Participants[0].MoveName == MoveName) return;
	Participants[0].MoveName = MoveName;
	Participants[0].FrameIndex = 0;
	Participants[0].PlaybackSeconds = 0.0f;
	Evaluate(false);
}

void FCombatLabModel::HandleSessionDataChanged()
{
	if (!Session.IsValid()) return;

	const FName PreviousMove = Participants[0].MoveName;
	HandleSessionSelectionChanged(Session->GetSelectedAttack());
	// Selection changes already evaluate. A data edit on the same selected move must still rebuild
	// geometry and collision; the old early return left the Lab showing stale physical data.
	if (Participants[0].MoveName == PreviousMove)
	{
		Evaluate(true);
	}
}

void FCombatLabModel::Advance(float DeltaSeconds)
{
	bool bChanged = false;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FCombatLabParticipantState& Participant = Participants[Index];
		UPaperFlipbook* Flipbook = GetFlipbook(Index);
		if (!Participant.bPlaying || !Flipbook || Flipbook->GetTotalDuration() <= 0.0f) continue;
		Participant.PlaybackSeconds = FMath::Fmod(
			Participant.PlaybackSeconds + FMath::Max(0.0f, DeltaSeconds), Flipbook->GetTotalDuration());
		const int32 NewFrame = FMath::Clamp(
			Flipbook->GetKeyFrameIndexAtTime(Participant.PlaybackSeconds), 0, Flipbook->GetNumKeyFrames() - 1);
		bChanged |= NewFrame != Participant.FrameIndex;
		Participant.FrameIndex = NewFrame;
	}
	if (bChanged) Evaluate(false);
}

FVector2D FCombatLabModel::GetParticipantPreviewPosition(int32 Index) const
{
	if (Index != 0 && Index != 1) return FVector2D::ZeroVector;
	const FCombatLabParticipantState& Participant = Participants[Index];
	const FFlipbookProfileEntry* Entry = GetEntry(Index);
	if (!Entry || Entry->MotionData.RootMotion.Num() == 0)
	{
		return Participant.Position;
	}

	// Runtime treats a zero root-motion sample as authoring-blank and keeps the prior applied position.
	// Resolve the same cumulative pose for arbitrary worldless scrubbing by walking to the latest
	// non-zero sample at or before the selected key frame.
	FVector2D AuthoredPosition = FVector2D::ZeroVector;
	for (int32 Frame = FMath::Min(Participant.FrameIndex, Entry->MotionData.RootMotion.Num() - 1);
		Frame >= 0;
		--Frame)
	{
		const FVector2D Candidate = Entry->MotionData.RootMotion[Frame].Position;
		if (!Candidate.IsNearlyZero())
		{
			AuthoredPosition = Candidate;
			break;
		}
	}

	if (Participant.bFacingLeft)
	{
		AuthoredPosition.X = -AuthoredPosition.X;
	}
	// Root-motion authoring uses texture-down Y; runtime applies it to up-positive world Z.
	AuthoredPosition.Y = -AuthoredPosition.Y;
	return Participant.Position + AuthoredPosition;
}

void FCombatLabModel::Evaluate(bool bRefreshScores)
{
	Evaluation = FCombatLabEvaluation();
	Evaluation.Distance = FMath::Abs(
		GetParticipantPreviewPosition(1).X - GetParticipantPreviewPosition(0).X);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FFlipbookProfileEntry* Entry = GetEntry(Index);
		if (!Entry || !Entry->CombatData.Frames.IsValidIndex(Participants[Index].FrameIndex)) continue;
		FFrameHitboxData FrameData = Entry->CombatData.Frames[Participants[Index].FrameIndex];
		FVector2D PivotFraction = FVector2D::ZeroVector;
		UPaperFlipbook* Flipbook = GetFlipbook(Index);
		const UPaper2DPlusCombatProfileAsset* Combat = Session.IsValid() ? Session->GetAsset() : nullptr;
		const UPaper2DPlusCharacterProfileAsset* Character = Combat ? Combat->CharacterProfile : nullptr;
		FVector2D PivotLocal = FVector2D::ZeroVector;
		if (Character && Flipbook
			&& Character->GetFramePivotLocal(Flipbook, Participants[Index].FrameIndex, PivotLocal))
		{
			PivotFraction = Paper2DPlusFrameGeometry::ConvertFrameDataFromTopLeftToPivotSpace(
				FrameData,
				PivotLocal);
		}

		const FVector2D PreviewPosition = GetParticipantPreviewPosition(Index);
		const FVector WorldOrigin = Paper2DPlusFrameGeometry::ApplyPivotFractionToWorldOrigin(
			FVector(PreviewPosition.X, 0.0f, PreviewPosition.Y),
			PivotFraction,
			Participants[Index].bFacingLeft,
			1.0f,
			1.0f);
		TArray<FWorldHitbox>& Output = Index == 0 ? Evaluation.AttackerBoxes : Evaluation.DefenderBoxes;
		for (const FHitboxData& Box : FrameData.Hitboxes)
		{
			if (Box.Type == EHitboxType::Attack || Box.Type == EHitboxType::Hurtbox)
			{
				Output.Add(Paper2DPlusFrameGeometry::MakeWorldHitbox(
					Box,
					WorldOrigin,
					Participants[Index].bFacingLeft,
					1.0f,
					1.0f,
					Entry->CombatData.DefaultClashCategory,
					FrameData.DefenseClass));
			}
		}
	}
	for (const FWorldHitbox& A : Evaluation.AttackerBoxes)
	{
		for (const FWorldHitbox& B : Evaluation.DefenderBoxes)
		{
			if (CombatLabPrivate::IsDamagePair(A.Type, B.Type)
				&& UPaper2DPlusHitboxSubsystem::IntersectHitboxes2D(A, B))
			{
				++Evaluation.AttackHurtOverlapCount;
			}
		}
	}
	Evaluation.bAnyCollision = Evaluation.AttackHurtOverlapCount > 0;
	if (Session.IsValid())
	{
		Session->EditPreviewContext().DistanceToTarget = Evaluation.Distance;
		if (bRefreshScores) Session->RefreshScores();
	}
	Changed.Broadcast();
}

void FCombatLabModel::RefreshEvaluation()
{
	Evaluate(true);
}

void FCombatLabModel::CaptureIntoPreset(
	FPaper2DPlusCombatScenarioPreset& Preset) const
{
	if (Session.IsValid())
	{
		Preset.Context = Session->GetPreviewContext();
		Preset.ScoringProfileName = Session->GetScoringProfileName();
	}
	Preset.AttackerMove = Participants[0].MoveName;
	Preset.DefenderMove = Participants[1].MoveName;
	Preset.AttackerPosition = Participants[0].Position;
	Preset.DefenderPosition = Participants[1].Position;
	Preset.SelectedParticipant = SelectedParticipant;
}

void FCombatLabModel::ApplyPreset(const FPaper2DPlusCombatScenarioPreset& Preset)
{
	Participants[0].MoveName = Preset.AttackerMove;
	Participants[1].MoveName = Preset.DefenderMove;
	Participants[0].Position = Preset.AttackerPosition;
	Participants[1].Position = Preset.DefenderPosition;
	SelectedParticipant = FMath::Clamp(Preset.SelectedParticipant, 0, 1);
	for (FCombatLabParticipantState& Participant : Participants)
	{
		Participant.FrameIndex = 0;
		Participant.PlaybackSeconds = 0.0f;
		Participant.bPlaying = false;
	}
	if (Session.IsValid())
	{
		Session->EditPreviewContext() = Preset.Context;
		Session->EditPreviewContext().DistanceToTarget = FMath::Abs(
			Participants[1].Position.X - Participants[0].Position.X);
		const bool bProfileChanged = Session->GetScoringProfileName() != Preset.ScoringProfileName;
		if (bProfileChanged) Session->SetScoringProfileName(Preset.ScoringProfileName);
		if (Participants[0].MoveName.IsNone() || !Session->SelectAttackByName(Participants[0].MoveName))
		{
			Session->ClearSelection();
		}
		Evaluate(!bProfileChanged);
		return;
	}
	Evaluate(true);
}

void SCombatLabPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	RefreshMoveOptions();
	if (Model.IsValid())
	{
		ChangedHandle = Model->OnChanged().AddLambda([WeakThis = TWeakPtr<SCombatLabPanel>(SharedThis(this))]()
		{
			if (const TSharedPtr<SCombatLabPanel> Pinned = WeakThis.Pin()) Pinned->RefreshControls();
		});
		if (Model->GetSession().IsValid())
		{
			SessionDataHandle = Model->GetSession()->OnDataChanged().AddLambda(
				[WeakThis = TWeakPtr<SCombatLabPanel>(SharedThis(this))]()
				{
					if (const TSharedPtr<SCombatLabPanel> Pinned = WeakThis.Pin()) Pinned->RefreshMoveOptions();
				});
			SessionScoreHandle = Model->GetSession()->OnScoreChanged().AddLambda(
				[WeakThis = TWeakPtr<SCombatLabPanel>(SharedThis(this))]()
				{
					if (const TSharedPtr<SCombatLabPanel> Pinned = WeakThis.Pin()) Pinned->RefreshControls();
				});
		}
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 3.f, 0.f)[BuildParticipantControls(0)]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(3.f, 0.f, 0.f, 0.f)[BuildParticipantControls(1)]
		]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(4.f)
		[
			SAssignNew(Canvas, SCombatLabCanvas).Model(Model)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(6.f)
		[
			SAssignNew(StatusText, STextBlock)
			.Text(this, &SCombatLabPanel::GetStatusText)
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 6.f, 6.f)
		[
			SAssignNew(ScoreExplanationText, STextBlock)
			.Text(this, &SCombatLabPanel::GetScoreExplanationText)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
	];
	RefreshMoveOptions();
}

SCombatLabPanel::~SCombatLabPanel()
{
	if (Model.IsValid())
	{
		Model->OnChanged().Remove(ChangedHandle);
		if (Model->GetSession().IsValid())
		{
			Model->GetSession()->OnDataChanged().Remove(SessionDataHandle);
			Model->GetSession()->OnScoreChanged().Remove(SessionScoreHandle);
		}
	}
}

void SCombatLabPanel::Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (Model.IsValid()) Model->Advance(InDeltaTime);
}

void SCombatLabPanel::RefreshMoveOptions()
{
	FName PreviousMoves[2] = { NAME_None, NAME_None };
	if (Model.IsValid())
	{
		PreviousMoves[0] = Model->GetParticipant(0).MoveName;
		PreviousMoves[1] = Model->GetParticipant(1).MoveName;
	}
	MoveOptions.Reset();
	if (Model.IsValid() && Model->GetSession().IsValid())
	{
		for (const FPaper2DPlusCombatAttackDerivedData& Row : Model->GetSession()->GetCatalog())
		{
			MoveOptions.Add(MakeShared<FName>(Row.MoveName));
		}
	}
	for (int32 ParticipantIndex = 0; ParticipantIndex < 2; ++ParticipantIndex)
	{
		TSharedPtr<SComboBox<TSharedPtr<FName>>>& Combo = MoveCombos[ParticipantIndex];
		if (!Combo.IsValid()) continue;
		Combo->RefreshOptions();
		const TSharedPtr<FName>* Match = MoveOptions.FindByPredicate(
			[Desired = PreviousMoves[ParticipantIndex]](const TSharedPtr<FName>& Name)
			{
				return Name.IsValid() && *Name == Desired;
			});
		if (Match) Combo->SetSelectedItem(*Match);
		else Combo->ClearSelection();
	}
}

TSharedRef<SWidget> SCombatLabPanel::BuildParticipantControls(int32 ParticipantIndex)
{
	return SNew(SBorder)
		.Padding(5.f)
		.BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(STextBlock).Text_Lambda([WeakModel = TWeakPtr<FCombatLabModel>(Model), ParticipantIndex]()
				{
					const TSharedPtr<FCombatLabModel> Pinned = WeakModel.Pin();
					const bool bSelected = Pinned.IsValid() && Pinned->GetSelectedParticipant() == ParticipantIndex;
					if (ParticipantIndex == 0)
					{
						return bSelected ? LOCTEXT("AttackerSelected", "Attacker (selected)") : LOCTEXT("Attacker", "Attacker");
					}
					return bSelected ? LOCTEXT("DefenderSelected", "Defender (selected)") : LOCTEXT("Defender", "Defender");
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SAssignNew(MoveCombos[ParticipantIndex], SComboBox<TSharedPtr<FName>>)
				.OptionsSource(&MoveOptions)
				.OnGenerateWidget_Lambda([](TSharedPtr<FName> Name)
				{
					return SNew(STextBlock).Text(Name.IsValid() ? FText::FromName(*Name) : FText::GetEmpty());
				})
				.OnSelectionChanged_Lambda([WeakModel = TWeakPtr<FCombatLabModel>(Model), ParticipantIndex](TSharedPtr<FName> Name, ESelectInfo::Type SelectInfo)
				{
					if (Name.IsValid()) if (const TSharedPtr<FCombatLabModel> Pinned = WeakModel.Pin())
					{
						if (SelectInfo != ESelectInfo::Direct) Pinned->SetSelectedParticipant(ParticipantIndex);
						Pinned->SetMove(ParticipantIndex, *Name);
					}
				})
				[
					SNew(STextBlock).Text_Lambda([WeakModel = TWeakPtr<FCombatLabModel>(Model), ParticipantIndex]()
					{
						const TSharedPtr<FCombatLabModel> Pinned = WeakModel.Pin();
						return Pinned.IsValid() ? FText::FromName(Pinned->GetParticipant(ParticipantIndex).MoveName) : FText::GetEmpty();
					})
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Text(this, &SCombatLabPanel::GetPlayLabel, ParticipantIndex)
				.OnClicked(this, &SCombatLabPanel::TogglePlaying, ParticipantIndex)
			]
			+ SHorizontalBox::Slot().FillWidth(0.7f).Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SSlider)
				.Value_Lambda([WeakModel = TWeakPtr<FCombatLabModel>(Model), ParticipantIndex]()
				{
					const TSharedPtr<FCombatLabModel> Pinned = WeakModel.Pin();
					if (!Pinned.IsValid()) return 0.0f;
					const int32 Count = Pinned->GetFrameCount(ParticipantIndex);
					return Count > 1 ? static_cast<float>(Pinned->GetParticipant(ParticipantIndex).FrameIndex) / static_cast<float>(Count - 1) : 0.0f;
				})
				.OnValueChanged_Lambda([WeakModel = TWeakPtr<FCombatLabModel>(Model), ParticipantIndex](float Value)
				{
					if (const TSharedPtr<FCombatLabModel> Pinned = WeakModel.Pin())
					{
						Pinned->SetSelectedParticipant(ParticipantIndex);
						Pinned->SetPlaying(ParticipantIndex, false);
						Pinned->SetFrame(ParticipantIndex, FMath::RoundToInt(Value * FMath::Max(0, Pinned->GetFrameCount(ParticipantIndex) - 1)));
					}
				})
			]
		];
}

void SCombatLabPanel::RefreshControls()
{
	if (Canvas.IsValid()) Canvas->Invalidate(EInvalidateWidgetReason::Paint);
	if (StatusText.IsValid()) StatusText->Invalidate(EInvalidateWidgetReason::Paint);
	if (ScoreExplanationText.IsValid()) ScoreExplanationText->Invalidate(EInvalidateWidgetReason::Paint);
}

FReply SCombatLabPanel::TogglePlaying(int32 ParticipantIndex)
{
	if (Model.IsValid())
	{
		Model->SetSelectedParticipant(ParticipantIndex);
		Model->SetPlaying(ParticipantIndex, !Model->GetParticipant(ParticipantIndex).bPlaying);
	}
	return FReply::Handled();
}

FText SCombatLabPanel::GetPlayLabel(int32 ParticipantIndex) const
{
	return Model.IsValid() && Model->GetParticipant(ParticipantIndex).bPlaying
		? LOCTEXT("Pause", "Pause")
		: LOCTEXT("Play", "Play");
}

FText SCombatLabPanel::GetStatusText() const
{
	if (!Model.IsValid()) return LOCTEXT("NoModel", "Combat Lab is unavailable.");
	const FCombatLabEvaluation& Result = Model->GetEvaluation();
	return FText::Format(
		Result.bAnyCollision
			? LOCTEXT("CollisionStatus", "Distance {0}. Collision: {1} attack/hurt overlap(s). Drag, or select and use arrow keys, to reposition. Visual advisory only — the Lab executes no gameplay.")
			: LOCTEXT("ClearStatus", "Distance {0}. No attack/hurt overlap. Drag, or select and use arrow keys, to test range. Visual advisory only — the Lab executes no gameplay."),
		FText::AsNumber(Result.Distance), FText::AsNumber(Result.AttackHurtOverlapCount));
}

FText SCombatLabPanel::GetScoreExplanationText() const
{
	const TSharedPtr<FCombatProfileEditorSession> Session = Model.IsValid() ? Model->GetSession() : nullptr;
	if (!Session.IsValid()) return LOCTEXT("NoScoreSession", "Score explanation is unavailable.");
	const FName SelectedMove = Model->GetParticipant(0).MoveName;
	const FPaper2DPlusCombatRankedOption* Ranked = Session->GetRankedOptions().FindByPredicate(
		[SelectedMove](const FPaper2DPlusCombatRankedOption& Option)
		{
			return Option.Attack.MoveName == SelectedMove;
		});
	if (!Ranked)
	{
		return LOCTEXT("NoRankedAttacker", "The attacker has no ranked scoring result for the current context.");
	}
	return FText::Format(
		LOCTEXT("CompactScoreExplanation", "Scored move {0}: {1} from {2} term(s). Open Score Playground for the full breakdown."),
		FText::FromName(Ranked->Attack.MoveName),
		FText::AsNumber(Ranked->Score),
		FText::AsNumber(Ranked->Breakdown.Terms.Num()));
}

#undef LOCTEXT_NAMESPACE
