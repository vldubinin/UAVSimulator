// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "ZeroMQ.h"

#define LOCTEXT_NAMESPACE "FZeroMQModule"

void FZeroMQModule::StartupModule()
{
	// Цей код виконається після завантаження модуля в пам'ять; точний момент задається в .uplugin для кожного модуля окремо
}

void FZeroMQModule::ShutdownModule()
{
	// Цю функцію може бути викликано під час завершення роботи для очищення модуля. Для модулів, що підтримують
	// динамічне перезавантаження, ця функція викликається перед вивантаженням модуля.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FZeroMQModule, ZeroMQ)