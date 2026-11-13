// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class tgjflying : ModuleRules
{
	public tgjflying(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"GameplayTags"
		});

		// FMOD: birds play their call sound from C++
		PrivateDependencyModuleNames.AddRange(new string[] { "FMODStudio" });

		PublicIncludePaths.AddRange(new string[] {
			"tgjflying",
			"tgjflying/Flocking",
			"tgjflying/player",
			"tgjflying/Flocking/Core",
			"tgjflying/Flocking/Behaviors",
			"tgjflying/Flocking/Agents",
			"tgjflying/Flocking/Spawner",
			"tgjflying/Flocking/Containment",
			"tgjflying/Flocking/Debug"
		});
	}
}
