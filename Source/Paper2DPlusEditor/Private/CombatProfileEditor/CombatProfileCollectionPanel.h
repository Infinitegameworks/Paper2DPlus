// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FCombatProfileEditorSession;
class IStructureDetailsView;
class SBox;
class STableViewBase;
struct FPropertyChangedEvent;
class FStructOnScope;
template <typename ItemType> class SListView;

enum class ECombatProfileCollectionKind : uint8
{
	Variables,
	TagDefaults,
	ScoringProfiles,
	ScenarioPresets
};

struct FCombatProfileCollectionRow
{
	FString Identity;
	FText Label;
	FText Summary;
};

/** Guided master-detail editor for Combat setup collections; array indices never escape the panel. */
class SCombatProfileCollectionPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCombatProfileCollectionPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FCombatProfileEditorSession>, Session)
		SLATE_ARGUMENT(ECombatProfileCollectionKind, Collection)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SCombatProfileCollectionPanel() override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	void RefreshFromAsset();
	const FString& GetSelectedIdentityForTests() const { return SelectedIdentity; }
	int32 ResolveSelectedIndexForTests() const;
#if WITH_DEV_AUTOMATION_TESTS
	FString GetIdentityAtIndexForTests(int32 Index) const { return MakeIdentity(Index); }
	int32 AddRowForTests();
	bool SelectIdentityForTests(const FString& Identity);
	bool ApplyRowForTests(const void* RowMemory);
	bool MoveSelectedForTests(int32 Direction);
	bool RemoveSelectedForTests();
#endif

private:
	using FRowPtr = TSharedPtr<FCombatProfileCollectionRow>;

	FText GetTitle() const;
	FText GetEmptyText() const;
	UScriptStruct* GetRowStruct() const;
	int32 GetNumRows() const;
	void* GetMutableRowMemory(int32 Index) const;
	const void* GetRowMemory(int32 Index) const;
	FString MakeIdentity(int32 Index) const;
	FText MakeLabel(int32 Index) const;
	FText MakeSummary(int32 Index) const;
	int32 ResolveIdentity(const FString& Identity) const;
	void RebuildRows();
	void RebuildDetails();
	void ApplyStructEdit(const FPropertyChangedEvent& Event);
	void NotifyExplicitMutation();

	TSharedRef<ITableRow> GenerateRow(FRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleSelectionChanged(FRowPtr Row, ESelectInfo::Type SelectInfo);
	FReply AddRow();
	FReply RemoveSelectedRow();
	FReply MoveSelectedRow(int32 Direction);

	TSharedPtr<FCombatProfileEditorSession> Session;
	ECombatProfileCollectionKind Collection = ECombatProfileCollectionKind::Variables;
	TArray<FRowPtr> Rows;
	TSharedPtr<SListView<FRowPtr>> ListView;
	TSharedPtr<IStructureDetailsView> StructureView;
	TSharedPtr<FStructOnScope> StructureScope;
	TSharedPtr<SBox> DetailsHost;
	FString SelectedIdentity;
	FDelegateHandle SessionChangedHandle;
	bool bApplyingEdit = false;
};
