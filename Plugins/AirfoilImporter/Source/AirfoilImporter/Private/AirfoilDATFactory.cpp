// Copyright Epic Games, Inc. All Rights Reserved.

#include "AirfoilDATFactory.h"
#include "AirfoilData.h"
#include "Engine/DataTable.h"

UAirfoilDATFactory::UAirfoilDATFactory()
{
	bCreateNew = false;
	bText = true;
	
	bEditorImport = true;
	SupportedClass = UDataTable::StaticClass();

	Formats.Add(TEXT("dat;Airfoil Profile Data"));
}

bool UAirfoilDATFactory::FactoryCanImport(const FString& Filename)
{
	return FPaths::GetExtension(Filename).Equals(TEXT("dat"));
}

UObject* UAirfoilDATFactory::FactoryCreateText(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const TCHAR*& Buffer, const TCHAR* BufferEnd, FFeedbackContext* Warn)
{
	UDataTable* DataTable = NewObject<UDataTable>(InParent, InName, Flags);
	if (!DataTable)
	{
		return nullptr;
	}

	DataTable->RowStruct = FAirfoilPointData::StaticStruct();

	TArray<FString> Lines;
	FString(Buffer).ParseIntoArray(Lines, TEXT("\n"), true);

	bool bHasHeader = true;
	int32 StartIndex = bHasHeader ? 1 : 0;

	for (int32 i = StartIndex; i < Lines.Num(); ++i)
	{
		const FString& Line = Lines[i];
		if (Line.IsEmpty())
		{
			continue;
		}

		TArray<FString> Coords;
		Line.TrimStartAndEnd().ParseIntoArray(Coords, TEXT(" "), true);
		

		if (Coords.Num() >= 2)
		{
			FAirfoilPointData NewRow;
			NewRow.X = FCString::Atof(*Coords[0]);
			NewRow.Z = FCString::Atof(*Coords[1]);

			FName RowName = FName(*FString::Printf(TEXT("%d"), i - StartIndex));
			
			DataTable->AddRow(RowName, NewRow);
		}
	}


	DataTable->Modify();

	return DataTable;
}
