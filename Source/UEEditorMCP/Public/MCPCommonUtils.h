// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphPin.h"

class FBlueprintEditor;

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class AActor;
class UK2Node_Event;
class UK2Node_CallFunction;
class UK2Node_VariableGet;
class UK2Node_VariableSet;
class UK2Node_Self;
class UK2Node_InputAction;
class USCS_Node;

/**
 * Common utility functions for MCP commands.
 * These are shared across all action handlers.
 */
class UEEDITORMCP_API FMCPCommonUtils
{
public:
	// =========================================================================
	// JSON Parsing Utilities
	// =========================================================================

	/** Parse FVector from JSON array field [X, Y, Z] */
	static FVector GetVectorFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName);

	/** Parse FRotator from JSON array field [Pitch, Yaw, Roll] */
	static FRotator GetRotatorFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName);

	/** Parse FVector2D from JSON array field [X, Y] */
	static FVector2D GetVector2DFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName);

	// =========================================================================
	// Blueprint Utilities
	// =========================================================================

	/** Find a Blueprint by name (searches /Game/Blueprints/) */
	static UBlueprint* FindBlueprint(const FString& BlueprintName);

	/** Find or create the event graph for a Blueprint */
	static UEdGraph* FindOrCreateEventGraph(UBlueprint* Blueprint);

	/** Find a function graph by name */
	static UEdGraph* FindFunctionGraph(UBlueprint* Blueprint, const FString& FunctionName);

	/** Find any graph by name (event graph, function graph, etc.) */
	static UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName);

	/** Find a component node in a Blueprint (traverses parent hierarchy) */
	static USCS_Node* FindComponentNode(UBlueprint* Blueprint, const FString& ComponentName);

	// =========================================================================
	// Property Setting Utilities
	// =========================================================================

	/** Set a property on an object from a JSON value */
	static bool SetObjectProperty(UObject* Object, const FString& PropertyName,
		const TSharedPtr<FJsonValue>& Value, FString& OutErrorMessage);

	// =========================================================================
	// Graph Node Utilities
	// =========================================================================

	/** Find a pin on a node by name and direction */
	static UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction);

	/** Find an existing event node in a graph */
	static UK2Node_Event* FindExistingEventNode(UEdGraph* Graph, const FString& EventName);

	/** Create an event node in a graph */
	static UK2Node_Event* CreateEventNode(UEdGraph* Graph, const FString& EventName, FVector2D Position);

	/** Create an input action node in a graph */
	static UK2Node_InputAction* CreateInputActionNode(UEdGraph* Graph, const FString& ActionName, FVector2D Position);

	/** Create a function call node in a graph */
	static UK2Node_CallFunction* CreateFunctionCallNode(UEdGraph* Graph, UFunction* Function, FVector2D Position);

	/** Create a self reference node in a graph */
	static UK2Node_Self* CreateSelfReferenceNode(UEdGraph* Graph, FVector2D Position);

	/** Create an error response JSON object */
	static TSharedPtr<FJsonObject> CreateErrorResponse(const FString& ErrorMessage);

	// =========================================================================
	// Actor Utilities
	// =========================================================================

	/** Convert an actor to a JSON object with location/rotation/scale */
	static TSharedPtr<FJsonObject> ActorToJsonObject(AActor* Actor);

	/** Convert an actor to a JSON value */
	static TSharedPtr<FJsonValue> ActorToJsonValue(AActor* Actor);

	// =========================================================================
	// Pin Type Resolution
	// =========================================================================

	/**
	 * Resolve a type name string (e.g. "Float", "Vector", "PlayerStatusModel")
	 * into an FEdGraphPinType. Supports all primitive types, common struct types,
	 * and UObject subclass resolution via FindFirstObject/LoadClass.
	 *
	 * @param TypeName   The type string to resolve.
	 * @param OutPinType The resolved pin type.
	 * @param OutError   Error message if resolution fails.
	 * @return true if successfully resolved, false otherwise.
	 */
	static bool ResolvePinTypeFromString(const FString& TypeName, FEdGraphPinType& OutPinType, FString& OutError);

	// =========================================================================
	// Blueprint Editor Window Resolution
	// =========================================================================

	/**
	 * Get the currently focused FBlueprintEditor instance.
	 * If BlueprintName is provided, only returns the editor for that specific Blueprint.
	 * Otherwise returns the most recently active Blueprint editor.
	 *
	 * Uses layered heuristics to correctly resolve the active editor when
	 * multiple Blueprint editor tabs are open:
	 *   1. SDockTab::IsActive()           — global active tab (synchronous)
	 *   2. SDockTab::IsForeground()       — dock-area foreground (Slate tick)
	 *   3. SWidget::HasFocusedDescendants()— keyboard focus (synchronous)
	 *   4. Fallback to first candidate with warning log
	 */
	static FBlueprintEditor* GetActiveBlueprintEditor(const FString& BlueprintName = TEXT(""));
};
