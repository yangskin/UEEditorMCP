// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FExtender;
class FUICommandList;

/**
 * UE Editor MCP Module
 *
 * Provides an MCP bridge for external tools (like AI assistants) to
 * manipulate Blueprints and the Unreal Editor via TCP commands.
 */
class FUEEditorMCPModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Check if module is loaded */
	static bool IsAvailable();

	/** Get the module instance */
	static FUEEditorMCPModule& Get();

private:
	/** Command list for auto-layout shortcuts */
	TSharedPtr<FUICommandList> AutoLayoutCommandList;

	/** Blueprint editor menu extender */
	TSharedPtr<FExtender> BlueprintMenuExtender;

	/** Material editor menu extender (P5.3) */
	TSharedPtr<FExtender> MaterialMenuExtender;

	/** Set up auto-layout commands and Blueprint editor integration */
	void RegisterAutoLayoutCommands();
	void UnregisterAutoLayoutCommands();
};
