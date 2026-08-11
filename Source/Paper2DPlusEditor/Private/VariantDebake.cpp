// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "VariantDebake.h"

#define LOCTEXT_NAMESPACE "VariantDebake"

namespace VariantDebakePrivate
{
	/** Overlay-emission / offset-search / back-vote-discard tolerance (per channel). */
	constexpr int32 EmitTolerance = 2;

	/** Reconstruction-mismatch report tolerance (per channel). */
	constexpr int32 ResidualTolerance = 8;

	/** Un-blended underlying alpha at or below this fraction = nothing meaningful underneath. */
	constexpr float UnderAlphaFloor = 0.02f;

	/** Alpha-0 pixels can carry arbitrary RGB; normalize them so voting/equality is well-defined. */
	FORCEINLINE FColor Norm(const FColor& C)
	{
		return C.A == 0 ? FColor(0, 0, 0, 0) : C;
	}

	FORCEINLINE bool DiffExceeds(const FColor& A, const FColor& B, int32 Tol)
	{
		return FMath::Abs((int32)A.R - (int32)B.R) > Tol
			|| FMath::Abs((int32)A.G - (int32)B.G) > Tol
			|| FMath::Abs((int32)A.B - (int32)B.B) > Tol
			|| FMath::Abs((int32)A.A - (int32)B.A) > Tol;
	}

	/** One VFX sheet mapped onto the main grid at a cell offset. Unset / out-of-range samples are transparent. */
	struct FVfxView
	{
		const TArray<FColor>* Buf = nullptr;
		int32 Width = 0;
		int32 CellCols = 0;
		int32 CellCount = 0;
		int32 Offset = 0;

		bool IsSet() const { return Buf != nullptr && Buf->Num() > 0; }

		FColor Sample(int32 X, int32 Y, int32 CellW, int32 CellH, int32 Columns) const
		{
			if (!IsSet()) { return FColor(0, 0, 0, 0); }
			const int32 MainCell = (Y / CellH) * Columns + (X / CellW);
			const int32 VfxCell = MainCell - Offset;
			if (VfxCell < 0 || VfxCell >= CellCount) { return FColor(0, 0, 0, 0); }
			const int32 VX = (VfxCell % CellCols) * CellW + (X % CellW);
			const int32 VY = (VfxCell / CellCols) * CellH + (Y % CellH);
			const int32 Idx = VY * Width + VX;
			// MakeVfxView's validation bounds this index for every current caller; guard anyway so a
			// future caller with an unvalidated view degrades to transparent instead of reading OOB.
			if (!Buf->IsValidIndex(Idx))
			{
				return FColor(0, 0, 0, 0);
			}
			return Norm((*Buf)[Idx]);
		}
	};

	struct FSheetViews
	{
		FVfxView Front;
		FVfxView Back;
	};

	/** Validate + wrap one optional VFX buffer. Absent (empty) buffers succeed with an unset view. */
	bool MakeVfxView(const TArray<FColor>& Buf, int32 BufWidth, int32 CellW, int32 CellH, int32 MaxCells,
		int32 SheetIndex, const TCHAR* What, FVfxView& Out, FText& OutError)
	{
		if (Buf.Num() == 0)
		{
			return true;
		}
		if (BufWidth <= 0 || Buf.Num() % BufWidth != 0)
		{
			OutError = FText::Format(LOCTEXT("VfxBadBuffer", "Sheet {0}: {1} VFX buffer size does not match its width."),
				FText::AsNumber(SheetIndex), FText::FromString(What));
			return false;
		}
		const int32 BufHeight = Buf.Num() / BufWidth;
		if (BufWidth % CellW != 0 || BufHeight % CellH != 0)
		{
			OutError = FText::Format(LOCTEXT("VfxBadCellSize", "Sheet {0}: {1} VFX dimensions are not a multiple of the cell size ({2}x{3})."),
				FText::AsNumber(SheetIndex), FText::FromString(What), FText::AsNumber(CellW), FText::AsNumber(CellH));
			return false;
		}
		Out.Buf = &Buf;
		Out.Width = BufWidth;
		Out.CellCols = BufWidth / CellW;
		Out.CellCount = Out.CellCols * (BufHeight / CellH);
		if (Out.CellCount > MaxCells)
		{
			OutError = FText::Format(LOCTEXT("VfxTooManyCells", "Sheet {0}: {1} VFX has more cells ({2}) than the main sheet ({3})."),
				FText::AsNumber(SheetIndex), FText::FromString(What), FText::AsNumber(Out.CellCount), FText::AsNumber(MaxCells));
			return false;
		}
		return true;
	}

	/**
	 * The consensus vote (steps 1/2/3/4/6 of the plan). With bWithMasks=false every sheet is clean
	 * everywhere — that is the pure provisional vote the offset search composites against. With masks,
	 * a sheet votes only where its front VFX alpha is 0 (and its main pixel is not the bare back-VFX
	 * value showing through base transparency); vote-exhausted pixels fall back to un-blending the
	 * first semi-transparently covered sheet in reference-priority order, and pixels opaquely covered
	 * in every sheet stay transparent + are reported (never invented).
	 */
	void ComputeBase(const TArray<FDebakeSheetInput>& Sheets, const TArray<FSheetViews>& Views, bool bWithMasks,
		int32 W, int32 H, int32 CellW, int32 CellH, int32 Columns, int32 RefIndex,
		TArray<FColor>& OutBase, TArray<FIntPoint>* OutUnrecoverable)
	{
		const int32 N = Sheets.Num();
		OutBase.SetNumUninitialized(W * H);

		TArray<FColor, TInlineAllocator<16>> Main; Main.SetNum(N);
		TArray<FColor, TInlineAllocator<16>> Front; Front.SetNum(N);
		TArray<bool, TInlineAllocator<16>> Clean; Clean.SetNum(N);

		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const int32 P = Y * W + X;
				bool bAnyBackDiscard = false;
				for (int32 i = 0; i < N; ++i)
				{
					Main[i] = Norm(Sheets[i].Pixels[P]);
					Front[i] = bWithMasks ? Views[i].Front.Sample(X, Y, CellW, CellH, Columns) : FColor(0, 0, 0, 0);
					bool bClean = (Front[i].A == 0);
					if (bClean && bWithMasks && Views[i].Back.IsSet() && Main[i].A > 0)
					{
						// Back VFX shows through base transparency: a main pixel SHOWING CONTENT that
						// equals the bare back-VFX value is the back VFX, not the base — discard that
						// vote. A base-opaque pixel differs from the bare back value and stays trusted;
						// a transparent main is a legitimate transparent vote even under a faint back
						// halo (the "main shows content" gate from the spec).
						const FColor BackC = Views[i].Back.Sample(X, Y, CellW, CellH, Columns);
						if (BackC.A > 0 && !DiffExceeds(Main[i], BackC, EmitTolerance))
						{
							bClean = false;
							bAnyBackDiscard = true;
						}
					}
					Clean[i] = bClean;
				}

				TArray<uint32, TInlineAllocator<16>> Vals;
				TArray<int32, TInlineAllocator<16>> Counts;
				for (int32 i = 0; i < N; ++i)
				{
					if (!Clean[i]) { continue; }
					const uint32 V = Main[i].DWColor();
					int32 Idx = INDEX_NONE;
					if (Vals.Find(V, Idx))
					{
						Counts[Idx]++;
					}
					else
					{
						Vals.Add(V);
						Counts.Add(1);
					}
				}

				FColor BasePixel(0, 0, 0, 0);
				if (Vals.Num() > 0)
				{
					int32 MaxCount = 0;
					for (const int32 C : Counts) { MaxCount = FMath::Max(MaxCount, C); }
					TArray<uint32, TInlineAllocator<16>> Winners;
					for (int32 vi = 0; vi < Vals.Num(); ++vi)
					{
						if (Counts[vi] == MaxCount) { Winners.Add(Vals[vi]); }
					}

					if (Winners.Num() == 1)
					{
						BasePixel = FColor(Winners[0]);
					}
					else
					{
						// Deterministic tie-break: the reference sheet owns ambiguous pixels so the
						// base stays one consistent value across frames instead of flickering.
						bool bResolved = false;
						if (Clean[RefIndex] && Winners.Contains(Main[RefIndex].DWColor()))
						{
							BasePixel = Main[RefIndex];
							bResolved = true;
						}
						else if (bWithMasks && Front[RefIndex].A > 0 && Front[RefIndex].A < 255)
						{
							// Reference covered semi-transparently: prefer its un-blended value.
							FColor Under;
							if (FVariantDebake::UnBlend(Main[RefIndex], Front[RefIndex], Under))
							{
								BasePixel = Under;
								bResolved = true;
							}
						}
						if (!bResolved)
						{
							// Winners always come from clean sheets, so this walk always resolves.
							for (int32 Step = 0; Step < N; ++Step)
							{
								const int32 i = (RefIndex + Step) % N;
								if (Clean[i] && Winners.Contains(Main[i].DWColor()))
								{
									BasePixel = Main[i];
									break;
								}
							}
						}
					}
				}
				else
				{
					// Every clean vote exhausted: un-blend the first semi-transparently front-covered
					// sheet (reference-priority order) that yields a value; if every semi window says
					// "nothing underneath", the base is genuinely transparent there.
					bool bRecovered = false;
					bool bSawSemiFront = false;
					for (int32 Step = 0; Step < N && !bRecovered; ++Step)
					{
						const int32 i = (RefIndex + Step) % N;
						if (Front[i].A > 0 && Front[i].A < 255)
						{
							bSawSemiFront = true;
							FColor Under;
							if (FVariantDebake::UnBlend(Main[i], Front[i], Under))
							{
								BasePixel = Under;
								bRecovered = true;
							}
						}
					}
					// Report ONLY the spec's unrecoverable case — hidden behind opaque front VFX in
					// every contaminated sheet, with no semi-transparent window and no back-through-
					// transparency evidence (a back-discarded pixel is KNOWN transparent, not lost).
					if (!bRecovered && !bSawSemiFront && !bAnyBackDiscard && OutUnrecoverable)
					{
						OutUnrecoverable->Add(FIntPoint(X, Y));
					}
				}
				OutBase[P] = BasePixel;
			}
		}
	}
}

FColor FVariantDebake::AlphaOver(const FColor& Front, const FColor& Back)
{
	const float FA = Front.A / 255.0f;
	const float BA = Back.A / 255.0f;
	const float OutA = FA + BA * (1.0f - FA);
	if (OutA <= KINDA_SMALL_NUMBER)
	{
		return FColor(0, 0, 0, 0);
	}
	const float R = (Front.R * FA + Back.R * BA * (1.0f - FA)) / OutA;
	const float G = (Front.G * FA + Back.G * BA * (1.0f - FA)) / OutA;
	const float B = (Front.B * FA + Back.B * BA * (1.0f - FA)) / OutA;
	return FColor(
		(uint8)FMath::Clamp((int32)(R + 0.5f), 0, 255),
		(uint8)FMath::Clamp((int32)(G + 0.5f), 0, 255),
		(uint8)FMath::Clamp((int32)(B + 0.5f), 0, 255),
		(uint8)FMath::Clamp((int32)(OutA * 255.0f + 0.5f), 0, 255));
}

bool FVariantDebake::UnBlend(const FColor& Main, const FColor& Vfx, FColor& OutUnder)
{
	OutUnder = FColor(0, 0, 0, 0);
	if (Vfx.A == 0 || Vfx.A == 255)
	{
		return false;
	}
	const float F = Vfx.A / 255.0f;
	const float M = Main.A / 255.0f;
	const float U = (M - F) / (1.0f - F);
	if (U <= VariantDebakePrivate::UnderAlphaFloor)
	{
		return false;
	}
	const float ClampedU = FMath::Min(U, 1.0f);
	const float Denom = ClampedU * (1.0f - F);
	const auto Solve = [&](uint8 MainC, uint8 VfxC) -> uint8
	{
		const float V = (MainC * M - VfxC * F) / Denom;
		return (uint8)FMath::Clamp((int32)(V + 0.5f), 0, 255);
	};
	OutUnder = FColor(
		Solve(Main.R, Vfx.R),
		Solve(Main.G, Vfx.G),
		Solve(Main.B, Vfx.B),
		(uint8)FMath::Clamp((int32)(ClampedU * 255.0f + 0.5f), 0, 255));
	return true;
}

int32 FVariantDebake::FindBestVfxOffset(const FDebakeSheetInput& Sheet, const TArray<FColor>& ProvisionalBase,
	int32 CellW, int32 CellH, int32 Columns, int32 Rows)
{
	using namespace VariantDebakePrivate;

	const int32 MainCells = Columns * Rows;
	FText Discard;
	FVfxView FrontView, BackView;
	if (!MakeVfxView(Sheet.VfxFront, Sheet.VfxFrontWidth, CellW, CellH, MainCells, 0, TEXT("front"), FrontView, Discard) ||
		!MakeVfxView(Sheet.VfxBack, Sheet.VfxBackWidth, CellW, CellH, MainCells, 0, TEXT("back"), BackView, Discard))
	{
		return INDEX_NONE;
	}
	if (!FrontView.IsSet() && !BackView.IsSet())
	{
		return INDEX_NONE;
	}

	const int32 W = Sheet.Width;
	const int32 H = Sheet.Height;
	// Front and back share one start offset but may differ in span — the longer one bounds the search.
	const int32 VfxCells = FMath::Max(FrontView.CellCount, BackView.CellCount);
	const int32 MaxOffset = MainCells - VfxCells;

	int32 BestOffset = 0;
	int64 BestMismatch = MAX_int64;
	for (int32 K = 0; K <= MaxOffset; ++K)
	{
		FrontView.Offset = K;
		BackView.Offset = K;
		int64 Mismatch = 0;
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const int32 P = Y * W + X;
				const FColor Pred = AlphaOver(
					FrontView.Sample(X, Y, CellW, CellH, Columns),
					AlphaOver(Norm(ProvisionalBase[P]), BackView.Sample(X, Y, CellW, CellH, Columns)));
				if (DiffExceeds(Pred, Norm(Sheet.Pixels[P]), EmitTolerance))
				{
					Mismatch++;
				}
			}
		}
		if (Mismatch < BestMismatch)
		{
			BestMismatch = Mismatch;
			BestOffset = K;
		}
	}
	return BestOffset;
}

bool FVariantDebake::Run(const TArray<FDebakeSheetInput>& Sheets, const FDebakeSettings& Settings,
	FDebakeResult& Out, FText& OutError)
{
	using namespace VariantDebakePrivate;

	Out = FDebakeResult();
	OutError = FText::GetEmpty();

	const int32 N = Sheets.Num();
	if (N < 2)
	{
		OutError = LOCTEXT("TooFewSheets", "De-bake needs at least 2 variant sheets.");
		return false;
	}
	if (Settings.Columns < 1 || Settings.Rows < 1)
	{
		OutError = LOCTEXT("BadGrid", "Columns and Rows must both be at least 1.");
		return false;
	}
	const int32 W = Sheets[0].Width;
	const int32 H = Sheets[0].Height;
	if (W <= 0 || H <= 0)
	{
		OutError = LOCTEXT("BadDims", "Sheet dimensions must be positive.");
		return false;
	}
	for (int32 i = 0; i < N; ++i)
	{
		if (Sheets[i].Width != W || Sheets[i].Height != H || Sheets[i].Pixels.Num() != W * H)
		{
			OutError = FText::Format(LOCTEXT("MismatchedDims", "Sheet {0} dimensions do not match sheet 0 ({1}x{2}). All variant sheets must be identical in size."),
				FText::AsNumber(i), FText::AsNumber(W), FText::AsNumber(H));
			return false;
		}
	}
	if (W % Settings.Columns != 0 || H % Settings.Rows != 0)
	{
		OutError = FText::Format(LOCTEXT("GridNotDivisible", "Sheet size {0}x{1} is not divisible by the {2}x{3} grid."),
			FText::AsNumber(W), FText::AsNumber(H), FText::AsNumber(Settings.Columns), FText::AsNumber(Settings.Rows));
		return false;
	}
	if (Settings.ReferenceSheetIndex < 0 || Settings.ReferenceSheetIndex >= N)
	{
		OutError = LOCTEXT("BadRefIndex", "Reference sheet index is out of range.");
		return false;
	}

	const int32 CellW = W / Settings.Columns;
	const int32 CellH = H / Settings.Rows;
	const int32 MainCells = Settings.Columns * Settings.Rows;

	TArray<FSheetViews> Views;
	Views.SetNum(N);
	bool bAnyVfx = false;
	for (int32 i = 0; i < N; ++i)
	{
		if (!MakeVfxView(Sheets[i].VfxFront, Sheets[i].VfxFrontWidth, CellW, CellH, MainCells, i, TEXT("front"), Views[i].Front, OutError) ||
			!MakeVfxView(Sheets[i].VfxBack, Sheets[i].VfxBackWidth, CellW, CellH, MainCells, i, TEXT("back"), Views[i].Back, OutError))
		{
			return false;
		}
		bAnyVfx |= Views[i].Front.IsSet() || Views[i].Back.IsSet();
	}

	// Pass 1 — pure provisional vote (masks ignored). It is the base the offset auto-search composites
	// against, and the FINAL base when no sheet brought VFX — skip it when an explicit offset override
	// makes both uses moot.
	TArray<FColor> Provisional;
	const bool bNeedProvisional = !bAnyVfx || Settings.VfxOffsetOverride == INDEX_NONE;
	if (bNeedProvisional)
	{
		ComputeBase(Sheets, Views, /*bWithMasks=*/false, W, H, CellW, CellH, Settings.Columns, Settings.ReferenceSheetIndex,
			Provisional, nullptr);
	}

	// Resolve per-sheet VFX offsets (override wins; else auto-search).
	Out.VfxOffsets.Init(INDEX_NONE, N);
	for (int32 i = 0; i < N; ++i)
	{
		if (!Views[i].Front.IsSet() && !Views[i].Back.IsSet())
		{
			continue;
		}
		// Front and back share one start offset but may differ in span — the longer one bounds the search.
		const int32 VfxCells = FMath::Max(Views[i].Front.CellCount, Views[i].Back.CellCount);
		const int32 MaxOffset = MainCells - VfxCells;
		if (Settings.VfxOffsetOverride != INDEX_NONE)
		{
			if (Settings.VfxOffsetOverride < 0 || Settings.VfxOffsetOverride > MaxOffset)
			{
				OutError = FText::Format(LOCTEXT("BadOffsetOverride", "VFX offset override {0} is out of range for sheet {1} (valid: 0..{2})."),
					FText::AsNumber(Settings.VfxOffsetOverride), FText::AsNumber(i), FText::AsNumber(MaxOffset));
				return false;
			}
			Out.VfxOffsets[i] = Settings.VfxOffsetOverride;
		}
		else
		{
			Out.VfxOffsets[i] = FindBestVfxOffset(Sheets[i], Provisional, CellW, CellH, Settings.Columns, Settings.Rows);
		}
		Views[i].Front.Offset = Out.VfxOffsets[i];
		Views[i].Back.Offset = Out.VfxOffsets[i];
	}

	// Pass 2 — masked + un-blended base (skipped when no sheet brought VFX: the pure vote IS the base).
	if (bAnyVfx)
	{
		ComputeBase(Sheets, Views, /*bWithMasks=*/true, W, H, CellW, CellH, Settings.Columns, Settings.ReferenceSheetIndex,
			Out.BasePixels, &Out.UnrecoverablePixels);
	}
	else
	{
		Out.BasePixels = MoveTemp(Provisional);
	}

	// Per-variant residual overlays + counts.
	Out.Overlays.SetNum(N);
	Out.OverlayPixelCounts.Init(0, N);
	Out.ResidualCounts.Init(0, N);
	Out.EraseUnfixableCounts.Init(0, N);
	for (int32 i = 0; i < N; ++i)
	{
		TArray<FColor>& Overlay = Out.Overlays[i];
		Overlay.Init(FColor(0, 0, 0, 0), W * H);
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const int32 P = Y * W + X;
				const FColor MainC = Norm(Sheets[i].Pixels[P]);
				const FColor FrontC = Views[i].Front.Sample(X, Y, CellW, CellH, Settings.Columns);
				const FColor BackC = Views[i].Back.Sample(X, Y, CellW, CellH, Settings.Columns);
				const FColor BaseC = Out.BasePixels[P];
				const FColor BaseOverBack = AlphaOver(BaseC, BackC);

				// Only emit where the plain reconstruction actually disagrees with the original —
				// emitting wherever main != base double-composites semi-transparent VFX pixels and
				// corrupts alpha (the reference-implementation regression).
				const FColor Pred = AlphaOver(FrontC, BaseOverBack);
				if (DiffExceeds(Pred, MainC, EmitTolerance))
				{
					if (FrontC.A == 0)
					{
						if (MainC.A > 0)
						{
							Overlay[P] = MainC;
							Out.OverlayPixelCounts[i]++;
						}
						else
						{
							// Base has content this variant lacks — a normal layer cannot erase.
							Out.EraseUnfixableCounts[i]++;
						}
					}
					else if (FrontC.A < 255)
					{
						FColor Under;
						if (UnBlend(MainC, FrontC, Under) && DiffExceeds(Under, BaseC, EmitTolerance))
						{
							Overlay[P] = Under;
							Out.OverlayPixelCounts[i]++;
						}
					}
					// FrontC.A == 255: hidden behind opaque VFX either way — skip.
				}

				const FColor Stack = Overlay[P].A > 0 ? AlphaOver(Overlay[P], BaseOverBack) : BaseOverBack;
				const FColor Recon = AlphaOver(FrontC, Stack);
				if (DiffExceeds(Recon, MainC, ResidualTolerance))
				{
					Out.ResidualCounts[i]++;
				}
			}
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
