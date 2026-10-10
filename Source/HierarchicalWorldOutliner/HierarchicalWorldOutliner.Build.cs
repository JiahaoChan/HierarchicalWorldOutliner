// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

using UnrealBuildTool;

public class HierarchicalWorldOutliner : ModuleRules
{
	public HierarchicalWorldOutliner(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"SceneOutliner",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"LevelEditor",
			"ToolMenus",
			"TypedElementFramework",
			"TypedElementRuntime",
			"DataLayerEditor",
			"ApplicationCore",
			"InputCore",
			"Projects",
			"SourceControl",
			"UnsavedAssetsTracker",
			"WorkspaceMenuStructure",
		});
	}
}
