// Copyright (c) 2025 zolnoor. All rights reserved.

#include "BlueprintAutoLayoutCommands.h"

#define LOCTEXT_NAMESPACE "BlueprintAutoLayout"

void FBlueprintAutoLayoutCommands::RegisterCommands()
{
	UI_COMMAND(AutoLayoutSelected,
		"Auto Layout",
		"Automatically arrange Blueprint nodes (selected nodes if any, otherwise the whole graph)",
		EUserInterfaceActionType::Button,
		FInputChord(EModifierKey::Control | EModifierKey::Alt, EKeys::L));

	UI_COMMAND(AutoLayoutSubtree,
		"Auto Layout Subtree",
		"Arrange exec subtree from the selected root node",
		EUserInterfaceActionType::Button,
		FInputChord());

	UI_COMMAND(AutoLayoutGraph,
		"Auto Layout Graph",
		"Arrange all nodes in the current Blueprint graph",
		EUserInterfaceActionType::Button,
		FInputChord());
}

#undef LOCTEXT_NAMESPACE
