// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Dom/JsonObject.h"
#include "MCPContext.h"
#include "MCPBridge.generated.h"

// Forward declarations
class FMCPServer;
class FEditorAction;

/**
 * UMCPBridge
 *
 * Editor subsystem that manages the MCP server and routes commands
 * to appropriate action handlers.
 */
UCLASS()
class UEEDITORMCP_API UMCPBridge : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UMCPBridge();

	// =========================================================================
	// UEditorSubsystem Interface
	// =========================================================================

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// =========================================================================
	// Command Execution
	// =========================================================================

	/**
	 * Execute a command received from the MCP server.
	 * Routes to appropriate action handler based on command type.
	 *
	 * @param CommandType The type of command (e.g., "create_blueprint")
	 * @param Params The command parameters
	 * @return JSON response with success/failure and result/error
	 */
	TSharedPtr<FJsonObject> ExecuteCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

	/**
	 * Execute a command with crash protection (SEH on Windows, signal handler on Unix).
	 * If execution crashes, returns an error response instead of crashing the editor.
	 */
	TSharedPtr<FJsonObject> ExecuteCommandSafe(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

	// =========================================================================
	// Context Access
	// =========================================================================

	/** Get the current editor context */
	FMCPEditorContext& GetContext() { return Context; }
	const FMCPEditorContext& GetContext() const { return Context; }

	// =========================================================================
	// Response Helpers
	// =========================================================================

	/** Create a success response */
	static TSharedPtr<FJsonObject> CreateSuccessResponse(const TSharedPtr<FJsonObject>& ResultData = nullptr);

	/** Create an error response */
	static TSharedPtr<FJsonObject> CreateErrorResponse(const FString& ErrorMessage, const FString& ErrorType = TEXT("error"));

private:
	/** Register all action handlers */
	void RegisterActions();

	/** Find action handler for a command type */
	TSharedRef<FEditorAction>* FindAction(const FString& CommandType);

	/** Execute internal command (called after validation) */
	TSharedPtr<FJsonObject> ExecuteCommandInternal(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

	/** The MCP TCP server (raw pointer - cleanup in Deinitialize) */
	FMCPServer* Server;

	/** Editor context (persists across commands) */
	FMCPEditorContext Context;

	/** Map of command types to action handlers */
	TMap<FString, TSharedRef<FEditorAction>> ActionHandlers;

	/** Port to listen on (55558 during development to avoid conflict with old plugin) */
	static constexpr int32 DefaultPort = 55558;
};
