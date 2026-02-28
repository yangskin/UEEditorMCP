// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "../MCPContext.h"

/** Log category for all MCP actions and infrastructure */
DECLARE_LOG_CATEGORY_EXTERN(LogMCP, Log, All);

/**
 * FEditorAction
 *
 * Base class for all MCP editor actions. Provides a unified execution
 * pipeline with validation, crash protection, and auto-save.
 *
 * Subclasses override:
 * - Validate(): Check parameters and preconditions
 * - ExecuteInternal(): Perform the actual operation
 * - PostValidate(): Verify results (optional)
 * - GetActionName(): Return action identifier
 * - RequiresSave(): Whether to auto-save on success
 */
class UEEDITORMCP_API FEditorAction
{
public:
	virtual ~FEditorAction() = default;

	/**
	 * Execute the action with full pipeline.
	 * Handles validation, crash protection, and auto-save.
	 *
	 * @param Params Command parameters
	 * @param Context Current editor context
	 * @return JSON response with success/failure and result/error
	 */
	TSharedPtr<FJsonObject> Execute(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context);

	/**
	 * Execute the action (called after validation).
	 * Public for SEH wrapper access on Windows.
	 *
	 * @param Params Command parameters
	 * @param Context Current editor context
	 * @return JSON response
	 */
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) = 0;

protected:
	// =========================================================================
	// Override These in Subclasses
	// =========================================================================

	/**
	 * Validate parameters and preconditions before execution.
	 *
	 * @param Params Command parameters
	 * @param Context Current editor context
	 * @param OutError Error message if validation fails
	 * @return True if validation passes
	 */
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) = 0;

	/**
	 * Post-execution validation (optional).
	 * Called after successful execution to verify results.
	 *
	 * @param Context Current editor context
	 * @param OutError Error message if validation fails
	 * @return True if post-validation passes
	 */
	virtual bool PostValidate(FMCPEditorContext& Context, FString& OutError) { return true; }

	/**
	 * Get the action name for error messages.
	 */
	virtual FString GetActionName() const = 0;

	/**
	 * Whether this action should trigger auto-save on success.
	 */
	virtual bool RequiresSave() const { return true; }

	// =========================================================================
	// Helper Methods
	// =========================================================================

	/** Create a success response */
	TSharedPtr<FJsonObject> CreateSuccessResponse(const TSharedPtr<FJsonObject>& ResultData = nullptr) const;

	/** Create an error response */
	TSharedPtr<FJsonObject> CreateErrorResponse(const FString& ErrorMessage, const FString& ErrorType = TEXT("error")) const;

	/** Create a response indicating crash was prevented */
	TSharedPtr<FJsonObject> CreateCrashPreventedResponse() const;

	/** Get required string parameter, or set error */
	bool GetRequiredString(const TSharedPtr<FJsonObject>& Params, const FString& ParamName, FString& OutValue, FString& OutError) const;

	/** Get optional string parameter with default */
	FString GetOptionalString(const TSharedPtr<FJsonObject>& Params, const FString& ParamName, const FString& Default = TEXT("")) const;

	/** Get optional array parameter */
	const TArray<TSharedPtr<FJsonValue>>* GetOptionalArray(const TSharedPtr<FJsonObject>& Params, const FString& ParamName) const;

	/** Get optional number parameter with default */
	double GetOptionalNumber(const TSharedPtr<FJsonObject>& Params, const FString& ParamName, double Default = 0.0) const;

	/** Get optional bool parameter with default */
	bool GetOptionalBool(const TSharedPtr<FJsonObject>& Params, const FString& ParamName, bool Default = false) const;

	/** Find Blueprint by name */
	UBlueprint* FindBlueprint(const FString& BlueprintName, FString& OutError) const;

	/** Find graph in Blueprint */
	UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& GraphName, FString& OutError) const;

	/** Find node in graph by GUID */
	UEdGraphNode* FindNode(UEdGraph* Graph, const FGuid& NodeId, FString& OutError) const;

private:
	/**
	 * Execute with crash protection.
	 * Uses SEH on Windows, signal handlers on Unix.
	 */
	TSharedPtr<FJsonObject> ExecuteWithCrashProtection(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context);
};


/**
 * FBlueprintAction
 *
 * Base class for Blueprint-related actions.
 * Adds common Blueprint validation and utilities.
 */
class UEEDITORMCP_API FBlueprintAction : public FEditorAction
{
protected:
	/** Validate that Blueprint exists and is valid */
	bool ValidateBlueprint(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError);

	/** Get the Blueprint for this action (validates and caches) */
	UBlueprint* GetTargetBlueprint(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) const;

	/** Mark Blueprint as modified */
	void MarkBlueprintModified(UBlueprint* Blueprint, FMCPEditorContext& Context) const;

	/** Compile Blueprint and check for errors */
	bool CompileBlueprint(UBlueprint* Blueprint, FString& OutError) const;
};


/**
 * FBlueprintNodeAction
 *
 * Base class for Blueprint graph node actions.
 * Adds graph-specific validation and utilities.
 */
class UEEDITORMCP_API FBlueprintNodeAction : public FBlueprintAction
{
protected:
	/** Validate that graph exists */
	bool ValidateGraph(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError);

	/** Get the target graph for this action */
	UEdGraph* GetTargetGraph(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) const;

	/** Add node to graph and update context */
	void RegisterCreatedNode(UEdGraphNode* Node, FMCPEditorContext& Context) const;

	/** Parse node position from params */
	FVector2D GetNodePosition(const TSharedPtr<FJsonObject>& Params) const;
};
