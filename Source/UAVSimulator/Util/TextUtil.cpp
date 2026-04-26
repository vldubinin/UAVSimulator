// Fill out your copyright notice in the Description page of Project Settings.

#include "UAVSimulator/Util/TextUtil.h"


FString TextUtil::RemoveAfterSymbol(FString Text, TCHAR Symbol)
{
	int32 LastSlashIndex;
	// FindLastChar знаходить останнє входження символу; усікаємо рядок до цієї позиції (не включно)
	if (Text.FindLastChar(Symbol, LastSlashIndex))
	{
		Text = Text.Left(LastSlashIndex);
	}
	return Text;
}