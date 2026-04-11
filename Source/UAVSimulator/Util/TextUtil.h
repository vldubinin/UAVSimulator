// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"

/**
 *
 */
class TextUtil
{
public:
	/**
	 * Видаляє останній символ-роздільник та все, що йде після нього.
	 * Наприклад: RemoveAfterSymbol("/Game/Airplane/Wing/Data", '/') → "/Game/Airplane/Wing".
	 * Якщо символ не знайдено — рядок повертається без змін.
	 * @param Text   — вхідний рядок.
	 * @param Symbol — символ, після якого усікається рядок (включно з ним).
	 * @return Усічений рядок.
	 */
	static FString RemoveAfterSymbol(FString Text, TCHAR Symbol);
};