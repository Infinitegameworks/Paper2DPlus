// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "FrameCues/Paper2DPlusFrameCueTypeAuthoring.h"
#include "Misc/Guid.h"
#include "UObject/SoftObjectPath.h"

namespace Paper2DPlusCueSchemaGoldenTest
{
	/**
	 * A fixed, hand-written version 1 schema.
	 *
	 * Every value here is literal on purpose: the golden text below has to be readable as the exact
	 * canonical grammar a pre-behavior build wrote, without depending on any class default, engine
	 * version, or reflection order that could drift underneath it.
	 */
	FPaper2DPlusFrameCueSchema SchemaGolden_MakeBehaviorFreeSchema()
	{
		FPaper2DPlusFrameCueSchema Schema;
		Schema.Version = FPaper2DPlusFrameCueTypeAuthoring::BehaviorFreeSchemaVersion;
		Schema.Kind = EPaper2DPlusFrameCueTypeKind::Moment;
		Schema.ParentClassPath =
			FSoftObjectPath(TEXT("/Script/Paper2DPlus.Paper2DPlusMomentCue"));

		FPaper2DPlusFrameCueSchemaField& Field = Schema.Fields.AddDefaulted_GetRef();
		Field.FieldId = FGuid(0x11111111, 0x22222222, 0x33333333, 0x44444444);
		Field.Name = TEXT("Power");
		Field.FriendlyName = TEXT("Power");
		Field.Category = TEXT("Payload");
		Field.TypeKey = TEXT("int");
		Field.Container = EPaper2DPlusFrameCueSchemaContainer::None;
		Field.PropertyFlags = 0;
		Field.DefaultValue = TEXT("12");
		FPaper2DPlusFrameCueSchemaMetadata& Metadata = Field.Metadata.AddDefaulted_GetRef();
		Metadata.Key = TEXT("ToolTip");
		Metadata.Value = TEXT("Damage dealt");

		FPaper2DPlusFrameCueInheritedDefault& Inherited =
			Schema.InheritedDefaults.AddDefaulted_GetRef();
		Inherited.OwnerClassPath =
			FSoftObjectPath(TEXT("/Script/Paper2DPlus.Paper2DPlusFrameCue"));
		Inherited.Name = TEXT("NetPolicy");
		Inherited.TypeKey = TEXT("byte");
		Inherited.DefaultValue = TEXT("LocalAlways");

		FPaper2DPlusFrameCueSchemaDependency& Dependency = Schema.Dependencies.AddDefaulted_GetRef();
		Dependency.Kind = EPaper2DPlusFrameCueSchemaDependencyKind::ParentCueType;
		Dependency.ObjectPath = FSoftObjectPath(TEXT("/Game/Cues/BP_ParentCue.BP_ParentCue"));
		Dependency.Fingerprint = TEXT("0123456789ABCDEF0123456789ABCDEF01234567");

		Schema.bValid = true;
		return Schema;
	}

	/** The canonical text a version 1 build produced for the schema above, byte for byte. */
	const TCHAR* SchemaGolden_CanonicalText =
		TEXT("schema=1;kind=1;parent=40:/Script/Paper2DPlus.Paper2DPlusMomentCue\n")
		TEXT("field;32:11111111222222223333333344444444;5:Power;5:Power;7:Payload;3:int;0;0;2:12\n")
		TEXT("metadata;7:ToolTip;12:Damage dealt\n")
		TEXT("inherited;39:/Script/Paper2DPlus.Paper2DPlusFrameCue;9:NetPolicy;4:byte;11:LocalAlways\n")
		TEXT("dependency;0;36:/Game/Cues/BP_ParentCue.BP_ParentCue;40:0123456789ABCDEF0123456789ABCDEF01234567\n");

	/** SHA-1 of the UTF-8 bytes of the text above, uppercase hex — the durable fingerprint. */
	const TCHAR* SchemaGolden_Fingerprint = TEXT("97E0E4E2D60D5B18448670AEAC1EAE3ACEB07196");

	/** Round-trips through the durable snapshot, which is where canonical text is rebuilt and hashed. */
	bool SchemaGolden_Roundtrip(
		FAutomationTestBase& Test,
		const FPaper2DPlusFrameCueSchema& Schema,
		FPaper2DPlusFrameCueSchema& OutSchema)
	{
		FString Snapshot;
		FText Error;
		if (!Test.TestTrue(
			TEXT("A valid schema serializes to a durable snapshot"),
			FPaper2DPlusFrameCueTypeAuthoring::SerializeSchemaSnapshot(Schema, Snapshot, &Error)))
		{
			Test.AddError(Error.ToString());
			return false;
		}
		if (!Test.TestTrue(
			TEXT("The durable snapshot deserializes"),
			FPaper2DPlusFrameCueTypeAuthoring::DeserializeSchemaSnapshot(
				Snapshot, OutSchema, &Error)))
		{
			Test.AddError(Error.ToString());
			return false;
		}
		return true;
	}
}

/**
 * The canonical schema text and fingerprint of a behavior-free Cue Type are a compatibility promise:
 * every Cue Type authored before behavior existed must keep classifying as Ready without a restaging
 * pass. Anything that changes the grammar, the escaping, the field order, or the hash breaks that
 * promise for every existing asset, so it has to break this test first.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusFrameCueSchemaGoldenTest,
	"Paper2DPlus.FrameCues.CueType.Schema.BehaviorFreeCanonicalTextIsPinned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusFrameCueSchemaGoldenTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusCueSchemaGoldenTest;

	const FPaper2DPlusFrameCueSchema Schema = SchemaGolden_MakeBehaviorFreeSchema();
	FPaper2DPlusFrameCueSchema Roundtripped;
	if (!SchemaGolden_Roundtrip(*this, Schema, Roundtripped))
	{
		return false;
	}

	TestEqual(TEXT("A behavior-free schema keeps the version 1 stamp"),
		Roundtripped.Version,
		FPaper2DPlusFrameCueTypeAuthoring::BehaviorFreeSchemaVersion);
	TestEqual(TEXT("A behavior-free schema produces the exact version 1 canonical text"),
		Roundtripped.CanonicalText,
		FString(SchemaGolden_CanonicalText));
	TestEqual(TEXT("A behavior-free schema produces the exact version 1 fingerprint"),
		Roundtripped.Fingerprint,
		FString(SchemaGolden_Fingerprint));
	TestTrue(TEXT("A behavior-free canonical text emits no behavior record"),
		!Roundtripped.CanonicalText.Contains(TEXT("behavior;")));
	TestTrue(TEXT("A round-tripped baseline validates against its own stored fingerprint"),
		Roundtripped.bValid);

	// The behavior digest is additive: it appears only when behavior exists, and when it does it must
	// change the fingerprint so a behavior edit cannot pass as an unchanged baseline.
	FPaper2DPlusFrameCueSchema WithBehavior = Schema;
	WithBehavior.BehaviorEvents.Add(TEXT("OnCueTriggered"));
	FPaper2DPlusFrameCueSchema RoundtrippedWithBehavior;
	if (SchemaGolden_Roundtrip(*this, WithBehavior, RoundtrippedWithBehavior))
	{
		TestTrue(TEXT("An implemented behavior event is recorded in the canonical text"),
			RoundtrippedWithBehavior.CanonicalText.Contains(TEXT("behavior;14:OnCueTriggered")));
		TestNotEqual(TEXT("Adding behavior changes the fingerprint"),
			RoundtrippedWithBehavior.Fingerprint,
			FString(SchemaGolden_Fingerprint));
		TestEqual(TEXT("Adding behavior changes nothing else in the canonical text"),
			RoundtrippedWithBehavior.CanonicalText.Replace(
				TEXT("behavior;14:OnCueTriggered\n"), TEXT("")),
			FString(SchemaGolden_CanonicalText));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
