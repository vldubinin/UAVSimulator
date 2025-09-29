// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class UAVSimulatorTarget : TargetRules
{
	public UAVSimulatorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        CppStandard = CppStandardVersion.Cpp20;
        ExtraModuleNames.AddRange( new string[] { "UAVSimulator" } );
	}
}
