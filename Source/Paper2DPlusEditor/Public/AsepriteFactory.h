// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "AsepriteFactory.generated.h"

/**
 * Content Browser drop target for Aseprite (.ase/.aseprite) files. Creates nothing itself: it hands
 * the dropped file to the Bulk Sprite Extractor window, where the batch's Character Profile and
 * Layer Profile and the per-file options are chosen, and reports the drop as canceled so the
 * Content Browser does not create an asset.
 */
UCLASS()
class PAPER2DPLUSEDITOR_API UAsepriteFactory : public UFactory
{
	GENERATED_BODY()

public:
	UAsepriteFactory();

	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
		const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
	virtual bool FactoryCanImport(const FString& Filename) override;
	virtual bool DoesSupportClass(UClass* Class) override;
	virtual UClass* ResolveSupportedClass() override;
	virtual FText GetDisplayName() const override;

};
