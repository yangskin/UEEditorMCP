// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actions/EditorAction.h"

class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UBlueprint;

// ============================================================================
// P3.1 — Enhanced Graph Description
// ============================================================================

/**
 * FGraphDescribeEnhancedAction
 *
 * Enhanced version of describe_graph (P2.1). In addition to the base topology,
 * provides:
 *   - Variable reference tracking (which nodes reference which variables)
 *   - Function signature inline expansion
 *   - Full FEdGraphPinType serialization (PinSubCategoryObject class path)
 *   - Node metadata (BreakpointEnabled, EnabledState, bCommentBubbleVisible)
 *
 * Command: describe_graph_enhanced
 * Action ID: graph.describe_enhanced
 */
class UEEDITORMCP_API FGraphDescribeEnhancedAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("describe_graph_enhanced"); }
	virtual bool RequiresSave() const override { return false; }

private:
	/** Serialize a pin with full FEdGraphPinType details */
	static TSharedPtr<FJsonObject> SerializePinEnhanced(const UEdGraphPin* Pin, bool bIncludeHidden, bool bIncludeOrphan);

	/** Compact pin serialization: category + direction + linked_to + default_value only (O6) */
	static TSharedPtr<FJsonObject> SerializePinCompact(const UEdGraphPin* Pin, bool bIncludeHidden, bool bIncludeOrphan);

	/** Serialize node metadata (breakpoints, enabled state, etc.) */
	static TSharedPtr<FJsonObject> SerializeNodeMetadata(const UEdGraphNode* Node);

	/** Collect variable references from graph nodes */
	static TArray<TSharedPtr<FJsonValue>> CollectVariableReferences(UBlueprint* Blueprint, UEdGraph* Graph);

	/** Serialize function signature details for function call nodes */
	static TSharedPtr<FJsonObject> SerializeFunctionSignature(const UEdGraphNode* Node);
};


// ============================================================================
// P3.2 — Patch Operations
// ============================================================================

/**
 * EPatchOpType
 * Enumeration of supported patch operation types.
 */
enum class EPatchOpType : uint8
{
	AddNode,
	RemoveNode,
	SetNodeProperty,
	Connect,
	Disconnect,
	AddVariable,
	SetVariableDefault,
	SetPinDefault,
	Invalid
};

/**
 * FPatchOp
 * A single operation within a patch document.
 * Parsed from JSON — each op has a type and type-specific parameters.
 */
struct UEEDITORMCP_API FPatchOp
{
	/** Operation type */
	EPatchOpType OpType = EPatchOpType::Invalid;

	/** Raw JSON parameters for this op */
	TSharedPtr<FJsonObject> Params;

	/** Index within the patch (for error reporting) */
	int32 OpIndex = -1;

	/** Parse op type from string */
	static EPatchOpType ParseOpType(const FString& OpStr);

	/** Get display name for an op type */
	static FString OpTypeToString(EPatchOpType Type);
};

/**
 * FPatchOpResult
 * Result of executing or validating a single patch op.
 */
struct UEEDITORMCP_API FPatchOpResult
{
	int32 OpIndex = -1;
	FString OpType;
	bool bSuccess = false;
	FString Message;
	FString NodeId; // For add_node ops — the created node's GUID

	TSharedPtr<FJsonObject> ToJson() const;
};


// ============================================================================
// P3.3 — Apply Patch
// ============================================================================

/**
 * FApplyPatchAction
 *
 * Applies a declarative patch document to a Blueprint graph.
 * The patch contains an ordered list of operations (add_node, connect,
 * set_pin_default, etc.) that describe desired graph state changes.
 *
 * Execution flow:
 *   Phase A: Validate all ops (read-only checks)
 *   Phase B: Execute ops sequentially
 *   Phase C: MarkBlueprintModified + CompileBlueprint
 *
 * Command: apply_graph_patch
 * Action ID: graph.apply_patch
 */
class UEEDITORMCP_API FApplyPatchAction : public FBlueprintNodeAction
{
	friend class FValidatePatchAction;

public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("apply_graph_patch"); }

private:
	/** Parse ops array from JSON */
	static bool ParseOps(const TArray<TSharedPtr<FJsonValue>>& OpsJson, TArray<FPatchOp>& OutOps, FString& OutError);

	/** Validate a single op before execution */
	FPatchOpResult ValidateOp(const FPatchOp& Op, UBlueprint* Blueprint, UEdGraph* Graph,
		const TMap<FString, FGuid>& TempIdMap) const;

	/** Execute a single op */
	FPatchOpResult ExecuteOp(const FPatchOp& Op, UBlueprint* Blueprint, UEdGraph* Graph,
		TMap<FString, FGuid>& TempIdMap, FMCPEditorContext& Context) const;

	// -- Individual op executors --
	FPatchOpResult ExecuteAddNode(const FPatchOp& Op, UBlueprint* Blueprint, UEdGraph* Graph,
		TMap<FString, FGuid>& TempIdMap, FMCPEditorContext& Context) const;
	FPatchOpResult ExecuteRemoveNode(const FPatchOp& Op, UBlueprint* Blueprint, UEdGraph* Graph,
		const TMap<FString, FGuid>& TempIdMap) const;
	FPatchOpResult ExecuteSetNodeProperty(const FPatchOp& Op, UEdGraph* Graph,
		const TMap<FString, FGuid>& TempIdMap) const;
	FPatchOpResult ExecuteConnect(const FPatchOp& Op, UEdGraph* Graph,
		const TMap<FString, FGuid>& TempIdMap) const;
	FPatchOpResult ExecuteDisconnect(const FPatchOp& Op, UEdGraph* Graph,
		const TMap<FString, FGuid>& TempIdMap) const;
	FPatchOpResult ExecuteAddVariable(const FPatchOp& Op, UBlueprint* Blueprint) const;
	FPatchOpResult ExecuteSetVariableDefault(const FPatchOp& Op, UBlueprint* Blueprint) const;
	FPatchOpResult ExecuteSetPinDefault(const FPatchOp& Op, UEdGraph* Graph,
		const TMap<FString, FGuid>& TempIdMap) const;

	/** Resolve node reference — supports temp IDs, $last_node, and real GUIDs */
	UEdGraphNode* ResolveNodeRef(const FString& NodeRef, UEdGraph* Graph,
		const TMap<FString, FGuid>& TempIdMap) const;

	/** Find pin on a node by name and direction */
	UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction) const;
};


// ============================================================================
// P3.4 — Validate Patch (dry-run)
// ============================================================================

/**
 * FValidatePatchAction
 *
 * Dry-run mode: validates a patch document without modifying the graph.
 * Returns per-op validation results (valid / warning / error).
 *
 * Command: validate_graph_patch
 * Action ID: graph.validate_patch
 */
class UEEDITORMCP_API FValidatePatchAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("validate_graph_patch"); }
	virtual bool RequiresSave() const override { return false; }
};


// ============================================================================
// P4 — Cross-Graph Node Transfer (Export / Import)
// ============================================================================

/**
 * FExportNodesToTextAction
 *
 * Serializes a set of Blueprint graph nodes to text using UE's native
 * FEdGraphUtilities::ExportNodesToText. The resulting text can later be
 * imported into any compatible graph via ImportNodesFromText.
 *
 * This enables cross-graph node transfer workflows:
 *   1. export_nodes_to_text  → capture nodes as text
 *   2. delete_blueprint_node → remove from source graph
 *   3. import_nodes_from_text → paste into target graph
 *
 * Command: export_nodes_to_text
 * Action ID: graph.export_nodes
 */
class UEEDITORMCP_API FExportNodesToTextAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("export_nodes_to_text"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FImportNodesFromTextAction
 *
 * Imports (pastes) nodes from text into a target Blueprint graph using UE's
 * native FEdGraphUtilities::ImportNodesFromText + PostProcessPastedNodes.
 * Supports importing into a different graph than the export source, enabling
 * cross-graph node migration.
 *
 * Command: import_nodes_from_text
 * Action ID: graph.import_nodes
 */
class UEEDITORMCP_API FImportNodesFromTextAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("import_nodes_from_text"); }
};
