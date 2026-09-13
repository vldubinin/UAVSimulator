// Copyright Epic Games, Inc. All Rights Reserved.

#include "AirfoilImporter.h"

#define LOCTEXT_NAMESPACE "FAirfoilImporterModule"

void FAirfoilImporterModule::StartupModule()
{
	// Цей код виконається після завантаження модуля в пам'ять; точний момент задається в .uplugin для кожного модуля окремо
}

void FAirfoilImporterModule::ShutdownModule()
{
	// Цю функцію може бути викликано під час завершення роботи для очищення модуля. Для модулів, що підтримують
	// динамічне перезавантаження, ця функція викликається перед вивантаженням модуля.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FAirfoilImporterModule, AirfoilImporter)
