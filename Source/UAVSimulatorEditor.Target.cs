// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class UAVSimulatorEditorTarget : TargetRules
{
	public UAVSimulatorEditorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        CppStandard = CppStandardVersion.Cpp20;
        ExtraModuleNames.AddRange( new string[] { "UAVSimulator" } );
	}
}
