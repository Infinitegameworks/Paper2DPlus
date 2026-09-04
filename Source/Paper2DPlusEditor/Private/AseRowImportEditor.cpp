// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AseRowImportEditor.h"

#include "Algo/Count.h"
#include "LayerImportPreviewCanvas.h"
#include "LayerStructureTree.h" // FLayerStructureController::DeriveSectionSuggestions
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "AseRowImportEditor"

void SAseRowImportEditor::Construct(const FArguments& InArgs)
{
	RowState = InArgs._RowState;
	Host = InArgs._Host;
	check(RowState.IsValid());

	// THE parse. The row carries only a summary, so the pixels the preview needs do not exist until
	// here — and stop existing again the moment this window closes.
	TSharedRef<FAsepriteParsedData> Parsed = MakeShared<FAsepriteParsedData>();
	if (FAsepriteImporter::ParseFile(RowState->AseFilePath, *Parsed, ParseError))
	{
		ParsedData = Parsed;
		PerLayerBuffers = MakeShared<FPerLayerBufferMap>(FAsepriteImporter::CompositePerLayer(*Parsed));
		for (int32 LayerIdx = 0; LayerIdx < Parsed->Layers.Num(); ++LayerIdx)
		{
			if (Parsed->Layers[LayerIdx].LayerType == 1)
			{
				GroupCollapsed.Add(LayerIdx, false);
			}
		}
	}
	else if (ParseError.IsEmpty())
	{
		ParseError = RowState->AseParseError;
	}

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

	Body->AddSlot().AutoHeight().Padding(8, 8, 8, 4)[ BuildSummary() ];

	if (ParsedData.IsValid())
	{
		// Preview above, the two lists below it in a splitter the designer can rebalance: which
		// half matters depends entirely on whether they are checking art or checking animations.
		Body->AddSlot().FillHeight(0.55f).Padding(8, 4)[ BuildPreview() ];
		Body->AddSlot().FillHeight(0.45f).Padding(8, 0, 8, 4)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			+ SSplitter::Slot().Value(0.55f)[ BuildLayers() ]
			+ SSplitter::Slot().Value(0.45f)[ BuildAnimations() ]
		];
	}
	else
	{
		Body->AddSlot().FillHeight(1.0f).Padding(8, 4)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.45f, 0.45f)))
			.Text(FText::Format(
				LOCTEXT("AseRowParseFailed", "This file could not be parsed, so there is nothing to configure.\n\n{0}"),
				FText::FromString(ParseError)))
		];
	}

	Body->AddSlot().AutoHeight().Padding(8, 0, 8, 8)[ BuildSections() ];

	ChildSlot[ Body ];

	if (ParsedData.IsValid())
	{
		RebuildLayerList();
	}
}

SAseRowImportEditor::~SAseRowImportEditor()
{
	// ORDER MATTERS. SLayerImportPreviewCanvas holds RAW pointers into ParsedData/PerLayerBuffers,
	// and ChildSlot lives on the BASE class — so it is destroyed AFTER this class's members, which
	// would leave the canvas pointing at freed buffers. Dropping the children here makes that
	// impossible rather than merely unlikely.
	ChildSlot.DetachWidget();
	PreviewCanvas.Reset();
	LayerListBox.Reset();
}

TSharedRef<SWidget> SAseRowImportEditor::BuildSummary()
{
	return SNew(STextBlock)
		.AutoWrapText(true)
		.Text_Lambda([this]()
		{
			if (!ParsedData.IsValid())
			{
				return FText::FromString(FPaths::GetCleanFilename(RowState->AseFilePath));
			}
			return FText::Format(
				LOCTEXT("AseRowEditorSummaryLive", "{0} of {1} layer(s), {2} of {3} animation(s), {4} frame(s) at {5}x{6}"),
				FText::AsNumber(CountEnabledLayers()), FText::AsNumber(RowState->AseLayerNames.Num()),
				FText::AsNumber(CountEnabledTags()), FText::AsNumber(RowState->AseTagNames.Num()),
				FText::AsNumber(RowState->AseFrameCount),
				FText::AsNumber(RowState->AseCanvasSize.X), FText::AsNumber(RowState->AseCanvasSize.Y));
		});
}

TSharedRef<SWidget> SAseRowImportEditor::BuildPreview()
{
	return SNew(SVerticalBox)

	+ SVerticalBox::Slot().FillHeight(1.0f)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(2)
		[
			SAssignNew(PreviewCanvas, SLayerImportPreviewCanvas)
			.ParsedData(ParsedData.Get())
			.PerLayerBuffers(PerLayerBuffers.Get())
		]
	]

	+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.Text(LOCTEXT("AsePrevFrame", "\x25C0"))
			.ToolTipText(LOCTEXT("AsePrevFrameTip", "Previous frame"))
			.OnClicked_Lambda([this]() { StepPreviewFrame(-1); return FReply::Handled(); })
		]

		+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.Text(LOCTEXT("AseNextFrame", "\x25B6"))
			.ToolTipText(LOCTEXT("AseNextFrameTip", "Next frame"))
			.OnClicked_Lambda([this]() { StepPreviewFrame(1); return FReply::Handled(); })
		]

		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(6, 0)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text_Lambda([this]()
			{
				int32 From = 0, To = 0;
				GetPreviewRange(From, To);
				const FText Scope = RowState->AseTagNames.IsValidIndex(PreviewTagIndex)
					? FText::FromString(RowState->AseTagNames[PreviewTagIndex])
					: LOCTEXT("AseScopeAll", "all frames");
				return FText::Format(LOCTEXT("AseFrameCounter", "Frame {0} of {1}  \x2014  {2}"),
					FText::AsNumber(PreviewFrame - From + 1), FText::AsNumber(To - From + 1), Scope);
			})
		]
	];
}

TSharedRef<SWidget> SAseRowImportEditor::BuildLayers()
{
	return SNew(SVerticalBox)

	+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("AseRowEditorLayers", "Layers"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		.ToolTipText(LOCTEXT("AseRowEditorLayersTip",
			"Tick a layer to import it. Click its NAME to hide it in the preview \x2014 that is a viewing aid only and never changes what imports."))
	]

	+ SVerticalBox::Slot().FillHeight(1.0f)
	[
		SNew(SScrollBox) + SScrollBox::Slot()[ SAssignNew(LayerListBox, SVerticalBox) ]
	];
}

TSharedRef<SWidget> SAseRowImportEditor::BuildAnimations()
{
	TSharedRef<SVerticalBox> TagRows = SNew(SVerticalBox);
	for (int32 TagIdx = 0; TagIdx < RowState->AseTagNames.Num(); ++TagIdx)
	{
		const FString TagName = RowState->AseTagNames[TagIdx];
		TSharedPtr<FBulkExtractorTextureState> State = RowState;
		TagRows->AddSlot().AutoHeight().Padding(4, 1)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([State, TagIdx]()
				{
					const bool* Enabled = State->AseTagImportEnabled.Find(TagIdx);
					return (!Enabled || *Enabled) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([State, TagIdx](ECheckBoxState NewState)
				{
					State->AseTagImportEnabled.Add(TagIdx, NewState == ECheckBoxState::Checked);
				})
			]

			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
				.Cursor(EMouseCursor::Hand)
				.ToolTipText(LOCTEXT("AseScopePreviewTip", "Scope the preview scrubber to this animation."))
				.OnMouseButtonDown_Lambda([this, TagIdx](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
				{
					if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
					{
						return FReply::Unhandled();
					}
					PreviewTagIndex = (PreviewTagIndex == TagIdx) ? INDEX_NONE : TagIdx;
					int32 From = 0, To = 0;
					GetPreviewRange(From, To);
					SetPreviewFrame(From);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(TagName))
					.ColorAndOpacity_Lambda([this, TagIdx]()
					{
						return PreviewTagIndex == TagIdx
							? FSlateColor(FLinearColor(0.35f, 0.72f, 1.0f))
							: FSlateColor::UseForeground();
					})
				]
			]
		];
	}

	if (RowState->AseTagNames.Num() == 0)
	{
		TagRows->AddSlot().AutoHeight().Padding(4, 2)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("AseNoTags", "This file has no animation tags, so it imports as a single animation."))
		];
	}

	return SNew(SVerticalBox)

	+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("AseRowEditorAnimations", "Animations"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
	]

	+ SVerticalBox::Slot().FillHeight(1.0f)
	[
		SNew(SScrollBox) + SScrollBox::Slot()[ TagRows ]
	];
}

TSharedRef<SWidget> SAseRowImportEditor::BuildSections()
{
	// Derived ONCE: the row's layer list cannot change while this window is open, and a lambda that
	// re-derived it every frame would be an O(n) scan per paint.
	const bool bHasDerivableSections =
		FLayerStructureController::DeriveSectionSuggestions(RowState->AseLayerNames, nullptr).Num() > 0;
	TSharedPtr<FBulkExtractorTextureState> State = RowState;

	return SNew(SVerticalBox)

	+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 2)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("AseRowEditorSections", "Sections"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
	]

	+ SVerticalBox::Slot().AutoHeight()
	[
		SNew(SButton)
		.HAlign(HAlign_Center)
		.Text(LOCTEXT("PreviewSectionSuggestionsBtn", "Preview Section Suggestions\x2026"))
		.ToolTipText_Lambda([this, bHasDerivableSections]()
		{
			if (Host.IsSeparateFlipbooksOnly && Host.IsSeparateFlipbooksOnly())
			{
				return LOCTEXT("PreviewSectionSuggestionsOffTip",
					"Sections live on a Layer Profile, and \"Separate flipbooks only\" does not create one.");
			}
			if (!bHasDerivableSections)
			{
				return LOCTEXT("PreviewSectionSuggestionsNoneTip",
					"Every layer in this file sits at the root, so there are no folders to derive Sections from.");
			}
			return LOCTEXT("PreviewSectionSuggestionsTip",
				"Propose one Section per top-level folder in this file. Nothing is written until you apply.");
		})
		.IsEnabled_Lambda([this, bHasDerivableSections]()
		{
			const bool bFlipbooksOnly = Host.IsSeparateFlipbooksOnly && Host.IsSeparateFlipbooksOnly();
			return !bFlipbooksOnly && bHasDerivableSections;
		})
		.OnClicked_Lambda([this]()
		{
			if (Host.OpenSectionSuggestions)
			{
				Host.OpenSectionSuggestions();
			}
			return FReply::Handled();
		})
	]

	+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
	[
		SNew(STextBlock)
		.AutoWrapText(true)
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		.Text_Lambda([State]()
		{
			const int32 Accepted = Algo::CountIf(State->AcceptedSectionSuggestions,
				[](const FLayerSectionSuggestion& Suggestion) { return Suggestion.bAccepted; });
			if (Accepted == 0)
			{
				return LOCTEXT("AseRowSectionsNoneAccepted", "No Sections accepted for this file.");
			}
			// Deliberately does NOT say "on import": the same accepted set may already have been
			// applied (a reimport target that holds its layers) and is re-applied afterwards anyway.
			return FText::Format(
				LOCTEXT("AseRowSectionsAccepted", "{0} Section(s) accepted for this file."),
				FText::AsNumber(Accepted));
		})
	];
}

void SAseRowImportEditor::GetPreviewRange(int32& OutFrom, int32& OutTo) const
{
	OutFrom = 0;
	OutTo = ParsedData.IsValid() ? FMath::Max(0, ParsedData->Frames.Num() - 1) : 0;
	if (ParsedData.IsValid() && ParsedData->Tags.IsValidIndex(PreviewTagIndex))
	{
		// NOT named `Tag`: SWidget has a member of that name, and a local that shadows it is a
		// C4458 — which is an ERROR under the -WarningsAsErrors merge gate.
		const FAsepriteTag& ScopedTag = ParsedData->Tags[PreviewTagIndex];
		OutFrom = FMath::Clamp(ScopedTag.FromFrame, 0, OutTo);
		OutTo = FMath::Clamp(ScopedTag.ToFrame, OutFrom, OutTo);
	}
}

void SAseRowImportEditor::SetPreviewFrame(int32 NewFrame)
{
	int32 From = 0, To = 0;
	GetPreviewRange(From, To);
	PreviewFrame = FMath::Clamp(NewFrame, From, To);
	if (PreviewCanvas.IsValid())
	{
		PreviewCanvas->SetFrameIndex(PreviewFrame);
	}
}

void SAseRowImportEditor::StepPreviewFrame(int32 Delta)
{
	int32 From = 0, To = 0;
	GetPreviewRange(From, To);
	const int32 Span = To - From + 1;
	if (Span <= 0)
	{
		return;
	}
	// Wrap inside the scoped range, so stepping off the end of an animation returns to its start
	// rather than silently leaving the animation the designer scoped to.
	const int32 Offset = ((PreviewFrame - From + Delta) % Span + Span) % Span;
	SetPreviewFrame(From + Offset);
}

void SAseRowImportEditor::RebuildLayerList()
{
	if (!LayerListBox.IsValid() || !ParsedData.IsValid())
	{
		return;
	}
	LayerListBox->ClearChildren();
	LayerRowCount = 0;

	TSharedPtr<FBulkExtractorTextureState> State = RowState;

	for (int32 LayerIdx = 0; LayerIdx < ParsedData->Layers.Num(); ++LayerIdx)
	{
		const FAsepriteLayer& Layer = ParsedData->Layers[LayerIdx];
		const float Indent = Layer.ChildLevel * 16.0f;

		// Hide anything inside a collapsed ancestor group.
		bool bInsideCollapsedGroup = false;
		for (int32 PrevIdx = LayerIdx - 1; PrevIdx >= 0 && Layer.ChildLevel > 0 && !bInsideCollapsedGroup; --PrevIdx)
		{
			const FAsepriteLayer& Prev = ParsedData->Layers[PrevIdx];
			if (Prev.LayerType == 1 && Prev.ChildLevel < Layer.ChildLevel)
			{
				const bool* bCollapsed = GroupCollapsed.Find(PrevIdx);
				bInsideCollapsedGroup = bCollapsed && *bCollapsed;
				if (Prev.ChildLevel == 0)
				{
					break;
				}
			}
		}
		if (bInsideCollapsedGroup)
		{
			continue;
		}

		const FAsepriteHitboxLayer* HitboxInfo = ParsedData->HitboxLayers.FindByPredicate(
			[LayerIdx](const FAsepriteHitboxLayer& HL) { return HL.LayerIndex == LayerIdx; });

		if (Layer.LayerType == 1)
		{
			// ---- Group row: collapses, imports nothing itself.
			LayerListBox->AddSlot().AutoHeight().Padding(Indent, 2, 4, 2)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(FMargin(4, 3))
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "NoBorder")
						.OnClicked_Lambda([this, LayerIdx]()
						{
							bool& bCollapsed = GroupCollapsed.FindOrAdd(LayerIdx);
							bCollapsed = !bCollapsed;
							RebuildLayerList();
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
							.Text_Lambda([this, LayerIdx]()
							{
								const bool* bCollapsed = GroupCollapsed.Find(LayerIdx);
								return (bCollapsed && *bCollapsed)
									? LOCTEXT("AseGroupExpand", "\x25B8") : LOCTEXT("AseGroupCollapse", "\x25BE");
							})
						]
					]

					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Layer.Name))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]
				]
			];
		}
		else if (HitboxInfo)
		{
			// ---- Data layer: reported, never importable as art.
			const FString TypeLabel = HitboxInfo->bIsSocket
				? FString::Printf(TEXT("[Socket: %s]"), *HitboxInfo->SocketName)
				: (HitboxInfo->HitboxType == EHitboxType::Attack ? TEXT("[Attack]") : TEXT("[Hurtbox]"));

			LayerListBox->AddSlot().AutoHeight().Padding(Indent, 1, 4, 1)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TypeLabel))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.5f, 0.2f)))
				]

				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Layer.Name))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			];
		}
		else
		{
			// ---- Visual layer: the import tick, plus a preview-only visibility toggle.
			// Selections are looked up by KEY inside the lambdas rather than captured as pointers
			// into the row's TMaps — a rehash would leave a captured pointer dangling.
			++LayerRowCount;
			LayerListBox->AddSlot().AutoHeight().Padding(Indent, 1, 4, 1)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor_Lambda([this, LayerIdx]() -> FSlateColor
				{
					return (PreviewCanvas.IsValid() && PreviewCanvas->IsLayerVisible(LayerIdx))
						? FSlateColor(FLinearColor(0.08f, 0.12f, 0.18f, 1.0f))
						: FSlateColor(FLinearColor(0.03f, 0.03f, 0.03f, 1.0f));
				})
				.Padding(FMargin(4, 2))
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
					[
						SNew(SCheckBox)
						.ToolTipText(LOCTEXT("AseLayerImportTip", "Import this layer."))
						.IsChecked_Lambda([State, LayerIdx]()
						{
							const bool* bEnabled = State->AseLayerImportEnabled.Find(LayerIdx);
							return (bEnabled && *bEnabled) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([State, LayerIdx](ECheckBoxState NewState)
						{
							State->AseLayerImportEnabled.Add(LayerIdx, NewState == ECheckBoxState::Checked);
						})
					]

					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
						.Cursor(EMouseCursor::Hand)
						.ToolTipText(LOCTEXT("AseLayerPreviewTip", "Show or hide this layer in the preview. Does not change what imports."))
						.OnMouseButtonDown_Lambda([this, LayerIdx](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
						{
							if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && PreviewCanvas.IsValid())
							{
								PreviewCanvas->SetLayerVisibility(LayerIdx, !PreviewCanvas->IsLayerVisible(LayerIdx));
								if (LayerListBox.IsValid())
								{
									LayerListBox->Invalidate(EInvalidateWidgetReason::Paint);
								}
								return FReply::Handled();
							}
							return FReply::Unhandled();
						})
						[
							SNew(STextBlock)
							.Text(FText::FromString(Layer.Name))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						]
					]

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.MinDesiredWidth(24)
						.Text_Lambda([State, LayerIdx]()
						{
							const int32* Order = State->AseLayerOrder.Find(LayerIdx);
							return Order
								? FText::Format(LOCTEXT("AseLayerOrderFmt", "order:{0}"), FText::AsNumber(*Order))
								: FText::GetEmpty();
						})
					]

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 0, 0)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.Text_Lambda([this, LayerIdx]()
						{
							return (PreviewCanvas.IsValid() && PreviewCanvas->IsLayerVisible(LayerIdx))
								? LOCTEXT("AseEyeOpen", "\x25C9") : LOCTEXT("AseEyeClosed", "\x25CB");
						})
						.ColorAndOpacity_Lambda([this, LayerIdx]() -> FSlateColor
						{
							return (PreviewCanvas.IsValid() && PreviewCanvas->IsLayerVisible(LayerIdx))
								? FSlateColor(FLinearColor(0.3f, 0.7f, 0.9f))
								: FSlateColor(FLinearColor(0.3f, 0.3f, 0.3f));
						})
					]
				]
			];
		}
	}

	// ---- Data-layer summary: name-convention hitbox/socket layers deliver gameplay data, not art,
	// and a designer who cannot see them counted has no way to tell a typo'd prefix from a miss.
	if (ParsedData->HitboxLayers.Num() > 0)
	{
		int32 AttackCount = 0, HurtboxCount = 0, SocketCount = 0;
		for (const FAsepriteHitboxLayer& HL : ParsedData->HitboxLayers)
		{
			if (HL.bIsSocket) { ++SocketCount; }
			else if (HL.HitboxType == EHitboxType::Attack) { ++AttackCount; }
			else { ++HurtboxCount; }
		}

		TArray<FString> SummaryParts;
		if (AttackCount > 0) { SummaryParts.Add(FString::Printf(TEXT("%d attack"), AttackCount)); }
		if (HurtboxCount > 0) { SummaryParts.Add(FString::Printf(TEXT("%d hurtbox"), HurtboxCount)); }
		if (SocketCount > 0) { SummaryParts.Add(FString::Printf(TEXT("%d socket"), SocketCount)); }

		LayerListBox->AddSlot().AutoHeight().Padding(0, 8, 0, 2)[ SNew(SSeparator) ];
		LayerListBox->AddSlot().AutoHeight().Padding(4, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AseDataLayersHeader", "Data Layers"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.5f, 0.2f)))
		];
		LayerListBox->AddSlot().AutoHeight().Padding(8, 1, 4, 4)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Join(SummaryParts, TEXT(", "))))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
}

int32 SAseRowImportEditor::CountEnabledLayers() const
{
	int32 Count = 0;
	for (const TPair<int32, bool>& Pair : RowState->AseLayerImportEnabled)
	{
		if (Pair.Value)
		{
			++Count;
		}
	}
	return Count;
}

int32 SAseRowImportEditor::CountEnabledTags() const
{
	int32 Count = 0;
	for (int32 TagIdx = 0; TagIdx < RowState->AseTagNames.Num(); ++TagIdx)
	{
		const bool* Enabled = RowState->AseTagImportEnabled.Find(TagIdx);
		if (!Enabled || *Enabled)
		{
			++Count;
		}
	}
	return Count;
}

#undef LOCTEXT_NAMESPACE
