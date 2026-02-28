// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

/**
 * FBlueprintAutoLayoutCommands
 *
 * Defines editor commands for auto-layout of Blueprint graph nodes.
 * Registered via TCommands and bound to keyboard shortcuts.
 */
class FBlueprintAutoLayoutCommands : public TCommands<FBlueprintAutoLayoutCommands>
{
public:
	FBlueprintAutoLayoutCommands()
		: TCommands<FBlueprintAutoLayoutCommands>(
			TEXT("BlueprintAutoLayout"),
			NSLOCTEXT("Contexts", "BlueprintAutoLayout", "Blueprint Auto Layout"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

	/** Auto Layout Selected: arrange currently selected nodes */
	TSharedPtr<FUICommandInfo> AutoLayoutSelected;

	/** Auto Layout Subtree: arrange exec subtree from selected root */
	TSharedPtr<FUICommandInfo> AutoLayoutSubtree;

	/** Auto Layout Graph: arrange all nodes in the current graph */
	TSharedPtr<FUICommandInfo> AutoLayoutGraph;
};
