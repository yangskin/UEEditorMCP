// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actions/EditorAction.h"

class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class FBlueprintEditor;


// ============================================================================
// Blueprint Graph Auto-Layout Utility
// ============================================================================

/**
 * FBlueprintAutoLayout
 *
 * Core layout algorithm for Blueprint graph nodes. Implements a layered
 * (Sugiyama-style) layout for Exec-connected nodes, with Pure nodes
 * clustering near their consumer Exec nodes.
 *
 * Usage from either MCP action or editor command:
 *   FBlueprintAutoLayout::LayoutNodes(Graph, NodesToLayout, Settings);
 */
struct UEEDITORMCP_API FBlueprintLayoutSettings
{
	/** Horizontal spacing between layers. >0 = fixed px; <=0 = auto (width-aware) */
	float LayerSpacing = 0.f;

	/** Vertical spacing between rows. >0 = fixed px; <=0 = auto (height-aware) */
	float RowSpacing = 0.f;

	/** Horizontal gap added after each layer's max node width (auto mode) */
	float HorizontalGap = 36.f;

	/** Vertical gap added after each row's max node height (auto mode) */
	float VerticalGap = 24.f;

	/** Horizontal offset for pure nodes relative to their consumer (fallback) */
	float PureOffsetX = 160.f;

	/** Vertical offset for stacking pure nodes (fallback when pin-align fails) */
	float PureOffsetY = 60.f;

	/** Fallback node width when Slate widget unavailable (minimum for heuristic) */
	float FallbackNodeWidth = 280.f;

	/** Fallback node height when Slate widget unavailable (minimum for heuristic) */
	float FallbackNodeHeight = 100.f;

	/** Collision tolerance (pixels) */
	float CollisionTolerance = 10.f;

	/** Margin around surrounding (non-participating) nodes for avoidance */
	float SurroundingMargin = 20.f;

	/** Number of Barycenter crossing optimization passes (0 = disabled) */
	int32 CrossingOptPasses = 4;

	/** Align pure nodes with the Y position of the consumer's input pin (legacy mode) */
	bool bPinAlignPureNodes = false;

	/** Keep exec-chain rows stable/straight; skip row-reordering optimization passes */
	bool bPreserveExecRows = true;

	/** Avoid overlapping with surrounding (non-participating) nodes */
	bool bAvoidSurrounding = false;

	/** Resize comment nodes to fit their children after layout */
	bool bPreserveComments = true;
};


class UEEDITORMCP_API FBlueprintAutoLayout
{
public:
	/**
	 * Perform auto-layout on a set of nodes.
	 *
	 * @param Graph             The owning graph (used for NotifyGraphChanged)
	 * @param NodesToLayout     The nodes to arrange (can be subset of graph)
	 * @param Settings          Layout parameters
	 * @param SurroundingNodes  Optional non-participating nodes to avoid overlap with
	 * @return Number of nodes actually moved
	 */
	static int32 LayoutNodes(
		UEdGraph* Graph,
		const TArray<UEdGraphNode*>& NodesToLayout,
		const FBlueprintLayoutSettings& Settings = FBlueprintLayoutSettings(),
		const TArray<UEdGraphNode*>* SurroundingNodes = nullptr);

	/** Collect nodes reachable from Root via exec output pins (subtree mode) */
	static TArray<UEdGraphNode*> CollectExecSubtree(
		UEdGraphNode* Root,
		int32 MaxPureDepth = 3);

	/** Collect pure dependencies for a given exec node (reverse DFS on data pins) */
	static void CollectPureDependencies(
		UEdGraphNode* ExecNode,
		const TSet<UEdGraphNode*>& NodeSet,
		TArray<UEdGraphNode*>& OutPureNodes,
		TSet<UEdGraphNode*>& Visited,
		int32 MaxDepth);

	/** Get estimated node size (tries Slate widget, falls back to pin count estimation) */
	static FVector2D GetNodeSize(const UEdGraphNode* Node, const FBlueprintLayoutSettings& Settings);

private:
	/** Check if a pin is an Exec pin */
	static bool IsExecPin(const UEdGraphPin* Pin);

	/** Check if a node has any exec pins (input or output) */
	static bool HasExecPins(const UEdGraphNode* Node);

	/** Estimate the Y offset of a specific pin relative to its node's top-left */
	static float EstimatePinYOffset(const UEdGraphNode* Node, const UEdGraphPin* TargetPin, const FBlueprintLayoutSettings& Settings);
};


// ============================================================================
// MCP Actions
// ============================================================================

/**
 * FAutoLayoutSelectedAction
 *
 * Auto-layout currently selected nodes (or specified node IDs) in a
 * Blueprint graph. Supports granularity: whole graph, specific graph,
 * or specific node set.
 *
 * Params:
 *   blueprint_name       (optional) Blueprint name; defaults to focused editor
 *   graph_name           (optional) Graph name; defaults to focused graph
 *   node_ids             (optional) Array of node GUIDs to layout
 *   mode                 "selected" | "graph" | "all"
 *   layer_spacing        (optional) float, >0=fixed, 0=auto
 *   row_spacing          (optional) float, >0=fixed, 0=auto
 *   avoid_surrounding    (optional) bool, avoid non-participating nodes
 *   include_pure_deps    (optional) bool, auto-include pure dependencies
 *   crossing_passes      (optional) int, Barycenter optimization rounds
 *   surrounding_margin   (optional) float, obstacle margin px
 */
class UEEDITORMCP_API FAutoLayoutSelectedAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("auto_layout_selected"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FAutoLayoutSubtreeAction
 *
 * Auto-layout an exec subtree starting from a root node.
 *
 * Params:
 *   blueprint_name  (optional) Blueprint name; defaults to focused editor
 *   graph_name      (optional) Graph name; defaults to focused graph
 *   root_node_id    (required) GUID of root node
 *   layer_spacing   (optional) float
 *   row_spacing     (optional) float
 *   max_pure_depth  (optional) int, default 3
 */
class UEEDITORMCP_API FAutoLayoutSubtreeAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("auto_layout_subtree"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FAutoLayoutBlueprintAction
 *
 * Auto-layout ALL graphs in a Blueprint (EventGraph, Functions, Macros).
 * Designed for fully auto-generated Blueprints that need a one-shot
 * layout pass across every graph.
 *
 * Params:
 *   blueprint_name       (optional) Blueprint name; defaults to focused editor's Blueprint
 *   layer_spacing        (optional) float, >0=fixed, 0=auto (default: auto)
 *   row_spacing          (optional) float, >0=fixed, 0=auto (default: auto)
 *   horizontal_gap       (optional) float, default 250
 *   vertical_gap         (optional) float, default 100
 *   crossing_passes      (optional) int, Barycenter optimization rounds (default: 4)
 *   pin_align_pure       (optional) bool (default: true)
 *   preserve_comments    (optional) bool (default: true)
 */
class UEEDITORMCP_API FAutoLayoutBlueprintAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("auto_layout_blueprint"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FLayoutAndCommentAction
 *
 * Combined action: auto-layout nodes, then create non-overlapping
 * comment boxes for each functional group. Solves the comment-overlap
 * problem by inserting inter-group spacing BEFORE placing comments.
 *
 * Params:
 *   blueprint_name       (optional) Blueprint name; defaults to focused editor
 *   graph_name           (optional) Graph name; defaults to focused graph
 *   groups               (required) Array of group objects:
 *     - node_ids          (required) Array of node GUIDs belonging to this group
 *     - comment_text      (required) Comment text for the group
 *     - color             (optional) [R, G, B, A] floats 0..1
 *   group_spacing         (optional) float, min Y gap between group AABBs (default: 80)
 *   auto_layout           (optional) bool, run auto-layout before commenting (default: true)
 *   clear_existing        (optional) bool, remove all existing comments first (default: false)
 *   layer_spacing         (optional) float, forwarded to auto-layout
 *   row_spacing           (optional) float, forwarded to auto-layout
 *   crossing_passes       (optional) int, forwarded to auto-layout
 *   padding               (optional) float, comment box padding (default: 40)
 *   title_height          (optional) float, comment title height (default: 36)
 */
class UEEDITORMCP_API FLayoutAndCommentAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("layout_and_comment"); }
	virtual bool RequiresSave() const override { return false; }
};
