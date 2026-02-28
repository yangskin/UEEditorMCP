// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/LayoutActions.h"
#include "MCPCommonUtils.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "BlueprintEditor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Engine/Blueprint.h"
#include "ScopedTransaction.h"
#include "SGraphNode.h"
#include "SGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Docking/SDockTab.h"

// ============================================================================
// Helpers
// ============================================================================

static void ShowNotification(const FString& Message, bool bSuccess = true)
{
	FNotificationInfo Info(FText::FromString(Message));
	Info.ExpireDuration = 3.0f;
	Info.bUseLargeFont = false;
	FSlateNotificationManager::Get().AddNotification(Info);
	if (bSuccess)
	{
		UE_LOG(LogMCP, Log, TEXT("UEEditorMCP AutoLayout: %s"), *Message);
	}
	else
	{
		UE_LOG(LogMCP, Warning, TEXT("UEEditorMCP AutoLayout: %s"), *Message);
	}
}


// GetActiveBlueprintEditor() moved to FMCPCommonUtils::GetActiveBlueprintEditor().
// All call sites in this file now use the shared utility.


/**
 * Find node in a graph by GUID string.
 */
static UEdGraphNode* FindNodeByGuid(UEdGraph* Graph, const FString& GuidStr)
{
	FGuid TargetGuid;
	FGuid::Parse(GuidStr, TargetGuid);

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->NodeGuid == TargetGuid)
		{
			return Node;
		}
	}
	return nullptr;
}


/**
 * Apply layout settings from JSON params.
 */
static FBlueprintLayoutSettings ParseLayoutSettings(const TSharedPtr<FJsonObject>& Params)
{
	FBlueprintLayoutSettings Settings;
	if (Params->HasField(TEXT("layer_spacing")))
	{
		Settings.LayerSpacing = static_cast<float>(Params->GetNumberField(TEXT("layer_spacing")));
	}
	if (Params->HasField(TEXT("row_spacing")))
	{
		Settings.RowSpacing = static_cast<float>(Params->GetNumberField(TEXT("row_spacing")));
	}
	if (Params->HasField(TEXT("pure_offset_x")))
	{
		Settings.PureOffsetX = static_cast<float>(Params->GetNumberField(TEXT("pure_offset_x")));
	}
	if (Params->HasField(TEXT("pure_offset_y")))
	{
		Settings.PureOffsetY = static_cast<float>(Params->GetNumberField(TEXT("pure_offset_y")));
	}
	if (Params->HasField(TEXT("crossing_passes")))
	{
		Settings.CrossingOptPasses = static_cast<int32>(Params->GetNumberField(TEXT("crossing_passes")));
	}
	if (Params->HasField(TEXT("avoid_surrounding")))
	{
		Settings.bAvoidSurrounding = Params->GetBoolField(TEXT("avoid_surrounding"));
	}
	if (Params->HasField(TEXT("surrounding_margin")))
	{
		Settings.SurroundingMargin = static_cast<float>(Params->GetNumberField(TEXT("surrounding_margin")));
	}
	if (Params->HasField(TEXT("pin_align_pure")))
	{
		Settings.bPinAlignPureNodes = Params->GetBoolField(TEXT("pin_align_pure"));
	}
	if (Params->HasField(TEXT("preserve_exec_rows")))
	{
		Settings.bPreserveExecRows = Params->GetBoolField(TEXT("preserve_exec_rows"));
	}
	if (Params->HasField(TEXT("preserve_comments")))
	{
		Settings.bPreserveComments = Params->GetBoolField(TEXT("preserve_comments"));
	}
	if (Params->HasField(TEXT("horizontal_gap")))
	{
		Settings.HorizontalGap = static_cast<float>(Params->GetNumberField(TEXT("horizontal_gap")));
	}
	if (Params->HasField(TEXT("vertical_gap")))
	{
		Settings.VerticalGap = static_cast<float>(Params->GetNumberField(TEXT("vertical_gap")));
	}
	return Settings;
}


/**
 * Resolve graph: use params or fall back to focused editor.
 * Also attempts to find the BPEditor for the resolved Blueprint so callers
 * can access editor state (e.g. selected nodes) even when blueprint_name is given.
 */
static UEdGraph* ResolveGraph(
	const TSharedPtr<FJsonObject>& Params,
	FBlueprintEditor*& OutBPEditor)
{
	OutBPEditor = nullptr;
	UEdGraph* Graph = nullptr;

	// If blueprint_name is given, find the blueprint and graph
	if (Params->HasField(TEXT("blueprint_name")))
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint_name"));
		UBlueprint* Blueprint = FMCPCommonUtils::FindBlueprint(BlueprintName);
		if (!Blueprint) return nullptr;

		FString GraphName;
		if (Params->HasField(TEXT("graph_name")))
		{
			GraphName = Params->GetStringField(TEXT("graph_name"));
		}

		if (GraphName.IsEmpty())
		{
			// Default to first uber graph (event graph)
			Graph = FMCPCommonUtils::FindOrCreateEventGraph(Blueprint);
		}
		else
		{
			Graph = FMCPCommonUtils::FindGraphByName(Blueprint, GraphName);
		}

		// Also try to find the BPEditor for this blueprint so callers can
		// access editor state (selected nodes, etc.)
		OutBPEditor = FMCPCommonUtils::GetActiveBlueprintEditor(BlueprintName);

		return Graph;
	}

	// Fall back to focused editor
	OutBPEditor = FMCPCommonUtils::GetActiveBlueprintEditor();
	if (OutBPEditor)
	{
		Graph = OutBPEditor->GetFocusedGraph();
	}
	return Graph;
}


// ============================================================================
// FBlueprintAutoLayout - Core Algorithm
// ============================================================================

bool FBlueprintAutoLayout::IsExecPin(const UEdGraphPin* Pin)
{
	return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
}

bool FBlueprintAutoLayout::HasExecPins(const UEdGraphNode* Node)
{
	if (!Node) return false;
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (IsExecPin(Pin)) return true;
	}
	return false;
}

FVector2D FBlueprintAutoLayout::GetNodeSize(const UEdGraphNode* Node, const FBlueprintLayoutSettings& Settings)
{
	if (!Node)
	{
		return FVector2D(Settings.FallbackNodeWidth, Settings.FallbackNodeHeight);
	}

	// ----------------------------------------------------------------
	// Pin-count-based minimum floor (immune to LOD / zoom)
	// ----------------------------------------------------------------
	// UE's SGraphNode uses LOD-based rendering: at low zoom levels,
	// pins and buttons are Collapsed, causing GetDesiredSize() to return
	// a smaller value than the actual 1:1 node size.
	//
	// Strategy:
	//   - Compute a TIGHT pin-count-based floor (no safety factor).
	//   - When Slate widget is available: Max(Slate, TightFloor).
	//     At 1:1 zoom Slate is accurate and >= floor → uses Slate.
	//     At low LOD Slate shrinks → floor catches underestimation.
	//   - When Slate is unavailable (MCP path): TightFloor * safety.
	// ----------------------------------------------------------------

	// --- Tight floor: Height ---
	// Engine actuals: TitleBar ~26-28px, Pin row ~24px (16px icon + 4+4 padding),
	// Bottom pad ~8px
	const float TitleBarH   = 28.f;
	const float PinRowH     = 24.f;
	const float BottomPad   = 8.f;

	int32 InputPinCount  = 0;
	int32 OutputPinCount = 0;
	float MaxInputNameW  = 0.f;
	float MaxOutputNameW = 0.f;

	// CJK-aware character width estimator: CJK ideographs are roughly
	// twice as wide as Latin characters in proportional fonts.
	auto EstimateStringWidth = [](const FString& Str, float NarrowCharW) -> float
	{
		float TotalW = 0.f;
		for (TCHAR Ch : Str)
		{
			// CJK Unified Ideographs (U+4E00..U+9FFF),
			// CJK Extension A (U+3400..U+4DBF),
			// Fullwidth forms (U+FF00..U+FFEF),
			// Hangul Syllables (U+AC00..U+D7AF),
			// Katakana/Hiragana (U+3040..U+30FF)
			if ((Ch >= 0x4E00 && Ch <= 0x9FFF)
				|| (Ch >= 0x3400 && Ch <= 0x4DBF)
				|| (Ch >= 0xFF00 && Ch <= 0xFFEF)
				|| (Ch >= 0xAC00 && Ch <= 0xD7AF)
				|| (Ch >= 0x3040 && Ch <= 0x30FF))
			{
				TotalW += NarrowCharW * 1.8f;
			}
			else
			{
				TotalW += NarrowCharW;
			}
		}
		return TotalW;
	};

	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->bHidden) continue;

		const float PinNameW = EstimateStringWidth(Pin->GetDisplayName().ToString(), 7.5f);

		if (Pin->Direction == EGPD_Input)
		{
			InputPinCount++;
			MaxInputNameW = FMath::Max(MaxInputNameW, PinNameW);
		}
		else
		{
			OutputPinCount++;
			MaxOutputNameW = FMath::Max(MaxOutputNameW, PinNameW);
		}
	}

	const int32 MaxPinRows = FMath::Max(InputPinCount, OutputPinCount);

	// ---- Multi-line title height (matches EstimatePinYOffset logic) ----
	// GetNodeTitle(FullTitle) returns multi-line text for function calls:
	//   Line 1: "Get Player Status Model"
	//   Line 2: "Target is Player Status Subsystem"
	// Each extra line adds ~16px; border/padding adds ~9px total.
	int32 TitleLines = 1;
	{
		FString TitleStr = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		for (TCHAR Ch : TitleStr)
		{
			if (Ch == TEXT('\n')) TitleLines++;
		}
	}
	const float TitleAreaH = TitleBarH + FMath::Max(0, TitleLines - 1) * 16.f + 6.f + 3.f;

	float FloorHeight = TitleAreaH + MaxPinRows * PinRowH + BottomPad;
	// Note: FallbackNodeHeight is NOT used as floor here — it would inflate
	// small nodes (Get/Set variable) and cascade into RowMaxHeight.

	// --- Tight floor: Width ---
	const float PinIndent   = 28.f;
	const float ColumnGap   = 40.f;
	const float TitleCharW  = 8.5f;

	float TitleW = EstimateStringWidth(Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString(), TitleCharW);
	float PinBodyW = PinIndent + MaxInputNameW + ColumnGap + MaxOutputNameW + PinIndent;
	float FloorWidth = FMath::Max(PinBodyW, TitleW + PinIndent * 2.f);
	// Note: FallbackNodeWidth is NOT used as floor here — it would inflate
	// narrow nodes (Get/Set variable ~180px) to 280px and cascade into
	// LayerMaxWidth, making every layer gap much wider.

	// ----------------------------------------------------------------
	// Slate widget size (most accurate at 1:1 zoom, may be LOD-degraded)
	// ----------------------------------------------------------------
	TSharedPtr<SGraphNode> NodeWidget = Node->DEPRECATED_NodeWidget.Pin();
	if (NodeWidget.IsValid())
	{
		// Force Slate to recompute sizes (needed after pin value changes,
		// zoom changes, or when widget content has been invalidated).
		NodeWidget->SlatePrepass();

		FVector2D SlateSize = NodeWidget->GetDesiredSize();
		if (SlateSize.X > 1.0 && SlateSize.Y > 1.0)
		{
			// At 1:1 zoom: Slate >= Floor → returns Slate (accurate).
			// At low LOD: Slate < Floor → Floor prevents underestimation.
			return FVector2D(
				FMath::Max(SlateSize.X, FloorWidth),
				FMath::Max(SlateSize.Y, FloorHeight));
		}
	}

	// ----------------------------------------------------------------
	// Slate unavailable (MCP path) — small safety factor + absolute
	// minimum from Fallback settings to compensate for heuristic gaps.
	// ----------------------------------------------------------------
	// Safety factors compensate for heuristic gaps (render-time borders,
	// icons, default-value widgets, etc.). Previous 1.08/1.05 was too
	// conservative — raised to 1.15/1.12 for more reliable wrapping.
	return FVector2D(
		FMath::Max(FloorWidth * 1.15f, Settings.FallbackNodeWidth),
		FMath::Max(FloorHeight * 1.12f, Settings.FallbackNodeHeight));
}

float FBlueprintAutoLayout::EstimatePinYOffset(
	const UEdGraphNode* Node,
	const UEdGraphPin* TargetPin,
	const FBlueprintLayoutSettings& Settings)
{
	if (!Node || !TargetPin) return 0.f;

	// Count visible pins before TargetPin on the same side
	int32 PinIndex = 0;
	const EEdGraphPinDirection Dir = TargetPin->Direction;

	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin == TargetPin) break;
		if (Pin && Pin->Direction == Dir && !Pin->bHidden)
			PinIndex++;
	}

	// ------------------------------------------------------------------
	// Title-aware Y offset estimation
	// Standard SGraphNode layout (SGraphNode::UpdateGraphNode):
	//   [Title Area]         variable height based on title text lines
	//   [Content Area]       SBorder Padding(0,3) -> LeftNodeBox/RightNodeBox
	//   Pin rows within content area, each ~24px
	//
	// Title height depends on number of visual lines in the title.
	// GetNodeTitle(FullTitle) returns lines separated by '\n'.
	// Line 1 = main title (e.g. "Create Widget"), Line 2 = subtitle (e.g. "Target is User Widget")
	// ------------------------------------------------------------------
	int32 TitleLines = 1;
	{
		FString TitleStr = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		for (TCHAR Ch : TitleStr)
		{
			if (Ch == TEXT('\n')) TitleLines++;
		}
	}

	// Title area: ~28px base + ~16px per extra line + ~6px border/padding
	const float TitleAreaH = 28.f + FMath::Max(0, TitleLines - 1) * 16.f + 6.f;

	// Content area top padding: 3px (from SGraphNode::CreateNodeContentArea Padding(0,3))
	const float ContentPadTop = 3.f;

	// Each pin row: ~24px, center at 12px
	const float PinRowH = 24.f;
	const float PinRowCenter = PinRowH * 0.5f;

	return TitleAreaH + ContentPadTop + PinIndex * PinRowH + PinRowCenter;
}

void FBlueprintAutoLayout::CollectPureDependencies(
	UEdGraphNode* ExecNode,
	const TSet<UEdGraphNode*>& NodeSet,
	TArray<UEdGraphNode*>& OutPureNodes,
	TSet<UEdGraphNode*>& Visited,
	int32 MaxDepth)
{
	if (MaxDepth <= 0 || !ExecNode) return;

	for (UEdGraphPin* Pin : ExecNode->Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Input || IsExecPin(Pin)) continue;

		for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (!LinkedPin) continue;
			UEdGraphNode* SourceNode = LinkedPin->GetOwningNode();
			if (!SourceNode || Visited.Contains(SourceNode)) continue;
			if (!NodeSet.Contains(SourceNode)) continue;

			// Only collect if it's a pure node (no exec pins)
			if (!HasExecPins(SourceNode))
			{
				Visited.Add(SourceNode);
				OutPureNodes.Add(SourceNode);
				// Recurse into pure node's inputs
				CollectPureDependencies(SourceNode, NodeSet, OutPureNodes, Visited, MaxDepth - 1);
			}
		}
	}
}

TArray<UEdGraphNode*> FBlueprintAutoLayout::CollectExecSubtree(
	UEdGraphNode* Root,
	int32 MaxPureDepth)
{
	TArray<UEdGraphNode*> Result;
	if (!Root) return Result;

	TSet<UEdGraphNode*> Visited;
	TArray<UEdGraphNode*> Queue;

	// BFS along exec output pins
	Queue.Add(Root);
	Visited.Add(Root);

	while (Queue.Num() > 0)
	{
		UEdGraphNode* Current = Queue[0];
		Queue.RemoveAt(0);
		Result.Add(Current);

		for (UEdGraphPin* Pin : Current->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output || !IsExecPin(Pin)) continue;

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin) continue;
				UEdGraphNode* NextNode = LinkedPin->GetOwningNode();
				if (NextNode && !Visited.Contains(NextNode))
				{
					Visited.Add(NextNode);
					Queue.Add(NextNode);
				}
			}
		}
	}

	// Collect pure dependencies for each exec node
	if (Root->GetGraph())
	{
		for (UEdGraphNode* ExecNode : TArray<UEdGraphNode*>(Result))
		{
			for (UEdGraphPin* Pin : ExecNode->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Input || IsExecPin(Pin)) continue;
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin) continue;
					UEdGraphNode* SourceNode = LinkedPin->GetOwningNode();
					if (!SourceNode || Visited.Contains(SourceNode)) continue;
					if (!HasExecPins(SourceNode))
					{
						Visited.Add(SourceNode);
						Result.Add(SourceNode);
					}
				}
			}
		}
	}

	return Result;
}


// ============================================================================
// LayoutNodes V2 - Enhanced Sugiyama-style layout
//
// Algorithm (12 phases):
//   1. Classify nodes (Exec / Pure / Comment)
//   2. Build Exec adjacency
//   3. Find roots (no exec predecessors), sort by original Y
//   4. Longest-path layer assignment (Exec nodes, X-axis)
//   5. Initial row ordering via DFS (preserves pin order)
//   6. Barycenter crossing optimization (N passes)
//   7. Width/height-aware coordinate calculation
//   8. Smart Pure node placement (pin-aligned, chain-aware)
//   9. Internal collision resolution
//  10. Surrounding node avoidance (optional)
//  11. Comment box adjustment (optional)
//  12. Apply positions with Undo support
// ============================================================================

int32 FBlueprintAutoLayout::LayoutNodes(
	UEdGraph* Graph,
	const TArray<UEdGraphNode*>& NodesToLayout,
	const FBlueprintLayoutSettings& Settings,
	const TArray<UEdGraphNode*>* SurroundingNodes)
{
	if (!Graph || NodesToLayout.Num() == 0)
	{
		return 0;
	}

	// Build node set for fast lookup
	TSet<UEdGraphNode*> NodeSet;
	for (UEdGraphNode* Node : NodesToLayout)
	{
		if (Node) NodeSet.Add(Node);
	}

	// =================================================================
	// Phase 1: Classify nodes - Exec / Pure / Comment
	// =================================================================
	TArray<UEdGraphNode*> ExecNodes;
	TArray<UEdGraphNode*> PureNodes;
	TArray<UEdGraphNode*> CommentNodes;

	for (UEdGraphNode* Node : NodesToLayout)
	{
		if (!Node) continue;
		if (Cast<UEdGraphNode_Comment>(Node))
		{
			CommentNodes.Add(Node);
		}
		else if (HasExecPins(Node))
		{
			ExecNodes.Add(Node);
		}
		else
		{
			PureNodes.Add(Node);
		}
	}

	UE_LOG(LogMCP, Log, TEXT("AutoLayout Phase 1: %d Exec, %d Pure, %d Comment"),
		ExecNodes.Num(), PureNodes.Num(), CommentNodes.Num());
	for (UEdGraphNode* Node : ExecNodes)
	{
		int32 ExecInCount = 0, ExecOutCount = 0, DataInCount = 0, DataOutCount = 0;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->bHidden) continue;
			if (IsExecPin(Pin))
			{
				Pin->Direction == EGPD_Input ? ExecInCount++ : ExecOutCount++;
			}
			else
			{
				Pin->Direction == EGPD_Input ? DataInCount++ : DataOutCount++;
			}
		}
		UE_LOG(LogMCP, Log, TEXT("  [Exec] '%s' (%s) ExecIn=%d ExecOut=%d DataIn=%d DataOut=%d"),
			*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
			*Node->GetClass()->GetName(),
			ExecInCount, ExecOutCount, DataInCount, DataOutCount);
	}
	for (UEdGraphNode* Node : PureNodes)
	{
		UE_LOG(LogMCP, Log, TEXT("  [Pure] '%s' (%s)"),
			*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
			*Node->GetClass()->GetName());
	}

	// =================================================================
	// Phase 2: Build Exec adjacency (successors in pin order)
	// =================================================================
	TMap<UEdGraphNode*, TArray<UEdGraphNode*>> Successors;
	TMap<UEdGraphNode*, TArray<UEdGraphNode*>> Predecessors;

	for (UEdGraphNode* Node : ExecNodes)
	{
		Successors.FindOrAdd(Node);
		Predecessors.FindOrAdd(Node);
	}

	for (UEdGraphNode* Node : ExecNodes)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output || !IsExecPin(Pin)) continue;
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin) continue;
				UEdGraphNode* Succ = LinkedPin->GetOwningNode();
				if (Succ && NodeSet.Contains(Succ) && HasExecPins(Succ) && Succ != Node)
				{
					Successors[Node].AddUnique(Succ);
					Predecessors[Succ].AddUnique(Node);
				}
			}
		}
	}

	// =================================================================
	// Phase 2.5: Find connected components among ALL non-comment nodes
	//   Uses ALL pin connections (exec + data) so that pure nodes are
	//   naturally grouped with their execution chains.
	//   Each component is an independent group of exec + pure nodes.
	//   They will be laid out independently to avoid cross-chain
	//   interference in layer widths, row heights, and pure placement.
	// =================================================================
	TArray<TArray<UEdGraphNode*>> NodeGroups;       // each: exec + pure nodes
	TArray<TArray<UEdGraphNode*>> ExecChains;       // parallel: exec subset per group
	TArray<TArray<UEdGraphNode*>> GroupPureNodes;    // parallel: pure subset per group
	{
		// All non-comment nodes eligible for grouping
		TSet<UEdGraphNode*> AllLayoutNodes;
		for (UEdGraphNode* Node : NodesToLayout)
		{
			if (Node && !Cast<UEdGraphNode_Comment>(Node))
			{
				AllLayoutNodes.Add(Node);
			}
		}

		TSet<UEdGraphNode*> CCVisited;
		for (UEdGraphNode* Node : NodesToLayout)
		{
			if (!Node || Cast<UEdGraphNode_Comment>(Node)) continue;
			if (CCVisited.Contains(Node)) continue;

			TArray<UEdGraphNode*> Component;
			TArray<UEdGraphNode*> CCQueue;
			CCQueue.Add(Node);
			CCVisited.Add(Node);

			while (CCQueue.Num() > 0)
			{
				UEdGraphNode* Current = CCQueue[0];
				CCQueue.RemoveAt(0);
				Component.Add(Current);

				// Follow ALL pin connections (exec + data, input + output)
				for (UEdGraphPin* Pin : Current->Pins)
				{
					if (!Pin) continue;
					for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						if (!LinkedPin) continue;
						UEdGraphNode* Neighbor = LinkedPin->GetOwningNode();
						if (Neighbor && AllLayoutNodes.Contains(Neighbor) && !CCVisited.Contains(Neighbor))
						{
							CCVisited.Add(Neighbor);
							CCQueue.Add(Neighbor);
						}
					}
				}
			}

			NodeGroups.Add(MoveTemp(Component));
		}

		// Sort groups by original topmost Y for deterministic vertical order
		NodeGroups.Sort([](const TArray<UEdGraphNode*>& A, const TArray<UEdGraphNode*>& B)
		{
			float MinYA = TNumericLimits<float>::Max();
			float MinYB = TNumericLimits<float>::Max();
			for (const UEdGraphNode* N : A) MinYA = FMath::Min(MinYA, (float)N->NodePosY);
			for (const UEdGraphNode* N : B) MinYB = FMath::Min(MinYB, (float)N->NodePosY);
			return MinYA < MinYB;
		});

		// Split each group into exec and pure subsets
		ExecChains.SetNum(NodeGroups.Num());
		GroupPureNodes.SetNum(NodeGroups.Num());
		for (int32 i = 0; i < NodeGroups.Num(); i++)
		{
			for (UEdGraphNode* Node : NodeGroups[i])
			{
				if (HasExecPins(Node))
				{
					ExecChains[i].Add(Node);
				}
				else
				{
					GroupPureNodes[i].Add(Node);
				}
			}
		}
	}

	// Phase 2.5 diagnostic: log each connected component
	UE_LOG(LogMCP, Log, TEXT("AutoLayout Phase 2.5: %d connected component(s)"), NodeGroups.Num());
	for (int32 GI = 0; GI < NodeGroups.Num(); GI++)
	{
		UE_LOG(LogMCP, Log, TEXT("  Group %d: %d exec + %d pure = %d total"),
			GI, ExecChains[GI].Num(), GroupPureNodes[GI].Num(), NodeGroups[GI].Num());
		for (UEdGraphNode* Node : ExecChains[GI])
		{
			UE_LOG(LogMCP, Log, TEXT("    [Exec] '%s'"),
				*Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		}
		for (UEdGraphNode* Node : GroupPureNodes[GI])
		{
			UE_LOG(LogMCP, Log, TEXT("    [Pure] '%s'"),
				*Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		}
	}

	// If no groups found (pure-only graph with no connections), treat all pure nodes as one group
	if (NodeGroups.Num() == 0 && PureNodes.Num() > 0)
	{
		NodeGroups.Add(TArray<UEdGraphNode*>(PureNodes));
		ExecChains.Add(TArray<UEdGraphNode*>());
		GroupPureNodes.Add(TArray<UEdGraphNode*>(PureNodes));
	}

	// Shared state across all chains
	TMap<UEdGraphNode*, FVector2D> FinalPositions;
	TMap<UEdGraphNode*, int32> NodeToBlockId;
	TMap<int32, TArray<UEdGraphNode*>> BlockToNodes;
	int32 NextBlockId = 0;
	TMap<UEdGraphNode*, UEdGraphNode*> PureToExecOwner; // shared so chains don't reclaim pures

	// Per-chain tracking for post-loop stacking
	struct FChainInfo
	{
		TArray<UEdGraphNode*> ChainExecNodes;
		TArray<UEdGraphNode*> AllChainNodes; // exec + pures claimed by this chain
	};
	TArray<FChainInfo> ChainInfos;
	ChainInfos.SetNum(ExecChains.Num());

	// =================================================================
	// Per-chain layout loop (Phases 3 – 8.5)
	// =================================================================
	for (int32 ChainIdx = 0; ChainIdx < ExecChains.Num(); ChainIdx++)
	{
	const TArray<UEdGraphNode*>& ChainExecNodes = ExecChains[ChainIdx];
	ChainInfos[ChainIdx].ChainExecNodes = ChainExecNodes;

	if (ChainExecNodes.Num() == 0) continue;

	// =================================================================
	// Phase 3: Find roots within this chain
	// =================================================================
	TArray<UEdGraphNode*> Roots;
	for (UEdGraphNode* Node : ChainExecNodes)
	{
		if (Predecessors[Node].Num() == 0)
		{
			Roots.Add(Node);
		}
	}

	if (Roots.Num() == 0 && ChainExecNodes.Num() > 0)
	{
		Roots.Add(ChainExecNodes[0]);
	}

	Roots.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
	{
		return A.NodePosY < B.NodePosY;
	});

	// =================================================================
	// Phase 4: Longest-path layer assignment (replaces simple DFS depth)
	//   Layer[node] = max(Layer[predecessor] + 1),  roots at layer 0
	//   Uses Kahn's topological sort for correct ordering
	// =================================================================
	TMap<UEdGraphNode*, int32> NodeLayer;
	for (UEdGraphNode* Node : ChainExecNodes)
	{
		NodeLayer.Add(Node, 0);
	}

	{
		TMap<UEdGraphNode*, int32> InDegree;
		for (UEdGraphNode* Node : ChainExecNodes)
		{
			InDegree.Add(Node, Predecessors[Node].Num());
		}

		TArray<UEdGraphNode*> Queue;
		for (UEdGraphNode* Node : ChainExecNodes)
		{
			if (InDegree[Node] == 0)
			{
				Queue.Add(Node);
			}
		}

		TSet<UEdGraphNode*> Processed;
		while (Queue.Num() > 0)
		{
			UEdGraphNode* Node = Queue[0];
			Queue.RemoveAt(0);
			Processed.Add(Node);

			for (UEdGraphNode* Succ : Successors[Node])
			{
				NodeLayer[Succ] = FMath::Max(NodeLayer[Succ], NodeLayer[Node] + 1);
				InDegree[Succ]--;
				if (InDegree[Succ] == 0)
				{
					Queue.Add(Succ);
				}
			}
		}

		// Handle cycles: assign remaining to incrementing layers beyond MaxLayer
		// so they don't pile up at Layer 0 and cause exec-exec overlap.
		{
			int32 CycleLayerCursor = 0;
			for (auto& Pair : NodeLayer)
			{
				CycleLayerCursor = FMath::Max(CycleLayerCursor, Pair.Value);
			}
			CycleLayerCursor++; // start beyond current max
			for (UEdGraphNode* Node : ChainExecNodes)
			{
				if (!Processed.Contains(Node))
				{
					NodeLayer[Node] = CycleLayerCursor++;
					UE_LOG(LogMCP, Warning, TEXT("  Phase 4: Cycle node '%s' assigned to Layer %d"),
						*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
						NodeLayer[Node]);
				}
			}
		}
	}

	int32 MaxLayer = 0;
	for (auto& Pair : NodeLayer)
	{
		MaxLayer = FMath::Max(MaxLayer, Pair.Value);
	}

	// Group nodes by layer
	TMap<int32, TArray<UEdGraphNode*>> LayerToNodes;
	for (auto& Pair : NodeLayer)
	{
		LayerToNodes.FindOrAdd(Pair.Value).Add(Pair.Key);
	}

	// =================================================================
	// Phase 5: Initial row ordering via DFS (preserving pin order)
	//   Primary exec output -> same row (horizontal line)
	//   Branch outputs -> new rows below
	// =================================================================
	TMap<UEdGraphNode*, int32> NodeRow;
	TSet<UEdGraphNode*> RowAssigned;
	int32 NextRow = 0;

	TFunction<void(UEdGraphNode*, int32)> AssignRowDFS;
	AssignRowDFS = [&](UEdGraphNode* Node, int32 Row)
	{
		if (RowAssigned.Contains(Node)) return;
		RowAssigned.Add(Node);
		NodeRow.Add(Node, Row);

		// Collect unvisited exec successors in pin order
		TArray<UEdGraphNode*> Children;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output || !IsExecPin(Pin)) continue;
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin) continue;
				UEdGraphNode* Succ = LinkedPin->GetOwningNode();
				if (Succ && NodeSet.Contains(Succ) && HasExecPins(Succ)
					&& !RowAssigned.Contains(Succ) && !Children.Contains(Succ))
				{
					Children.Add(Succ);
				}
			}
		}

		if (Children.Num() == 0)
		{
			NextRow = FMath::Max(NextRow, Row + 1);
			return;
		}

		// First child on same row (straight line), rest on new rows
		AssignRowDFS(Children[0], Row);
		for (int32 i = 1; i < Children.Num(); i++)
		{
			AssignRowDFS(Children[i], NextRow);
		}
	};

	for (UEdGraphNode* Root : Roots)
	{
		if (!RowAssigned.Contains(Root))
		{
			AssignRowDFS(Root, NextRow);
			NextRow++; // gap between event chains
		}
	}

	// Assign orphan exec nodes within this chain
	for (UEdGraphNode* Node : ChainExecNodes)
	{
		if (!RowAssigned.Contains(Node))
		{
			NodeRow.Add(Node, NextRow++);
			RowAssigned.Add(Node);
		}
	}

	// =================================================================
	// Phase 6: Barycenter crossing optimization
	//   Forward + backward sweeps to minimize edge crossings
	// =================================================================
	if (!Settings.bPreserveExecRows && Settings.CrossingOptPasses > 0 && MaxLayer > 0)
	{
		// Use float positions for smooth barycenter computation
		TMap<UEdGraphNode*, float> NodeRowF;
		for (auto& Pair : NodeRow)
		{
			NodeRowF.Add(Pair.Key, static_cast<float>(Pair.Value));
		}

		for (int32 Pass = 0; Pass < Settings.CrossingOptPasses; Pass++)
		{
			// Forward sweep (left to right): layers 1..MaxLayer
			for (int32 L = 1; L <= MaxLayer; L++)
			{
				if (!LayerToNodes.Contains(L)) continue;
				TArray<UEdGraphNode*>& Nodes = LayerToNodes[L];

				for (UEdGraphNode* Node : Nodes)
				{
					float Sum = 0.f;
					int32 Cnt = 0;
					for (UEdGraphNode* Pred : Predecessors[Node])
					{
						if (NodeRowF.Contains(Pred))
						{
							Sum += NodeRowF[Pred];
							Cnt++;
						}
					}
					if (Cnt > 0) NodeRowF[Node] = Sum / static_cast<float>(Cnt);
				}

				Nodes.Sort([&NodeRowF](const UEdGraphNode& A, const UEdGraphNode& B)
				{
					return NodeRowF[&A] < NodeRowF[&B];
				});

				// Reassign row indices preserving same-row groups (forward sweep)
				int32 RowCursorFwd = 0;
				for (int32 i = 0; i < Nodes.Num(); i++)
				{
					if (i > 0)
					{
						int32 PrevOrigRow = NodeRow.Contains(Nodes[i-1]) ? NodeRow[Nodes[i-1]] : -1;
						int32 CurrOrigRow = NodeRow.Contains(Nodes[i]) ? NodeRow[Nodes[i]] : -2;
						if (PrevOrigRow != CurrOrigRow)
						{
							RowCursorFwd++;
						}
					}
					NodeRowF[Nodes[i]] = static_cast<float>(RowCursorFwd);
				}
			}

			// Backward sweep (right to left): layers MaxLayer-1..0
			for (int32 L = MaxLayer - 1; L >= 0; L--)
			{
				if (!LayerToNodes.Contains(L)) continue;
				TArray<UEdGraphNode*>& Nodes = LayerToNodes[L];

				for (UEdGraphNode* Node : Nodes)
				{
					float Sum = 0.f;
					int32 Cnt = 0;
					for (UEdGraphNode* Succ : Successors[Node])
					{
						if (NodeRowF.Contains(Succ))
						{
							Sum += NodeRowF[Succ];
							Cnt++;
						}
					}
					if (Cnt > 0) NodeRowF[Node] = Sum / static_cast<float>(Cnt);
				}

				Nodes.Sort([&NodeRowF](const UEdGraphNode& A, const UEdGraphNode& B)
				{
					return NodeRowF[&A] < NodeRowF[&B];
				});

				// Reassign row indices preserving same-row groups (backward sweep)
				int32 RowCursorBwd = 0;
				for (int32 i = 0; i < Nodes.Num(); i++)
				{
					if (i > 0)
					{
						int32 PrevOrigRow = NodeRow.Contains(Nodes[i-1]) ? NodeRow[Nodes[i-1]] : -1;
						int32 CurrOrigRow = NodeRow.Contains(Nodes[i]) ? NodeRow[Nodes[i]] : -2;
						if (PrevOrigRow != CurrOrigRow)
						{
							RowCursorBwd++;
						}
					}
					NodeRowF[Nodes[i]] = static_cast<float>(RowCursorBwd);
				}
			}
		}

		// Commit optimized row positions
		for (auto& LayerPair : LayerToNodes)
		{
			TArray<UEdGraphNode*>& Nodes = LayerPair.Value;
			Nodes.Sort([&NodeRowF](const UEdGraphNode& A, const UEdGraphNode& B)
			{
				return NodeRowF[&A] < NodeRowF[&B];
			});
			for (int32 i = 0; i < Nodes.Num(); i++)
			{
				NodeRow[Nodes[i]] = i;
			}
		}
	}

	// =================================================================
	// Phase 7: Per-node width-driven X + row-height Y calculation
	//   X: Each node's X = max(predecessor.X + predecessor.Width + Gap)
	//        for all exec predecessors. Processed in topological order
	//        (ascending Layer) so all predecessors are resolved first.
	//   Y: Row-max-height system (unchanged).
	// =================================================================

	// --- Per-node X via topological (layer-ascending) relaxation ---
	// Cache each node's own width
	TMap<UEdGraphNode*, float> NodeWidth;
	for (UEdGraphNode* Node : ChainExecNodes)
	{
		FVector2D Size = GetNodeSize(Node, Settings);
		NodeWidth.Add(Node, Size.X);
	}

	// NodeRelX: X relative to BaseX (root = 0)
	TMap<UEdGraphNode*, float> NodeRelX;

	// Process nodes layer by layer (topological order)
	for (int32 L = 0; L <= MaxLayer; L++)
	{
		if (!LayerToNodes.Contains(L)) continue;
		for (UEdGraphNode* Node : LayerToNodes[L])
		{
			if (L == 0)
			{
				// Root layer: X = 0
				NodeRelX.Add(Node, 0.f);
			}
			else
			{
				// X = max over all exec predecessors of (pred.X + pred.Width + Gap)
				float MaxX = 0.f;
				bool bHasPred = false;
				for (UEdGraphNode* Pred : Predecessors[Node])
				{
					if (const float* PredX = NodeRelX.Find(Pred))
					{
						float PredW = NodeWidth.Contains(Pred) ? NodeWidth[Pred] : Settings.FallbackNodeWidth;
						float Gap = (Settings.LayerSpacing > 0.f) ? Settings.LayerSpacing : (PredW + Settings.HorizontalGap);
						float CandidateX;
						if (Settings.LayerSpacing > 0.f)
						{
							// Fixed spacing mode: layer * spacing
							CandidateX = *PredX + Settings.LayerSpacing;
						}
						else
						{
							// Variable spacing: pred.X + pred.Width + gap
							CandidateX = *PredX + PredW + Settings.HorizontalGap;
						}
						if (!bHasPred || CandidateX > MaxX)
						{
							MaxX = CandidateX;
							bHasPred = true;
						}
					}
				}
				NodeRelX.Add(Node, bHasPred ? MaxX : 0.f);
			}
		}
	}

	// --- Row Y with exec-pin alignment ---
	// Instead of aligning nodes by their top edge, align them so that
	// the first exec pin is at the same Y within each row.
	// NodeExecPinOffset[Node] = Y offset from node top to first exec pin center.
	int32 MaxRow = 0;
	for (auto& Pair : NodeRow)
	{
		MaxRow = FMath::Max(MaxRow, Pair.Value);
	}

	// Compute each node's exec pin Y offset (from node top to first exec pin)
	// Two-pass approach to prevent Slate/Heuristic mixing within a row:
	//   Pass 1: Compute offset for each node, track source (Slate vs Heuristic)
	//   Pass 2: If any node in a row used Heuristic, recompute ALL in that row
	//           with Heuristic to ensure consistent offset source per row.
	TMap<UEdGraphNode*, float> NodeExecPinOffset;
	TMap<UEdGraphNode*, bool> NodeUsedSlate;
	TMap<UEdGraphNode*, const UEdGraphPin*> NodeFirstExecPin;

	// --- Pass 1: Compute offsets with source tracking ---
	for (UEdGraphNode* Node : ChainExecNodes)
	{
		// Find first exec input pin (or first exec output if no input)
		const UEdGraphPin* FirstExecPin = nullptr;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && !Pin->bHidden && IsExecPin(Pin) && Pin->Direction == EGPD_Input)
			{
				FirstExecPin = Pin;
				break;
			}
		}
		if (!FirstExecPin)
		{
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && !Pin->bHidden && IsExecPin(Pin) && Pin->Direction == EGPD_Output)
				{
					FirstExecPin = Pin;
					break;
				}
			}
		}
		NodeFirstExecPin.Add(Node, FirstExecPin);

		float Offset = 0.f;
		bool bUsedSlate = false;
		if (FirstExecPin)
		{
			// Try Slate first (accurate, based on actual widget layout)
			TSharedPtr<SGraphNode> NodeWidget = Node->DEPRECATED_NodeWidget.Pin();
			if (NodeWidget.IsValid())
			{
				TSharedPtr<SGraphPin> PinWidget = NodeWidget->FindWidgetForPin(const_cast<UEdGraphPin*>(FirstExecPin));
				if (PinWidget.IsValid())
				{
					FVector2D PinOffset = PinWidget->GetNodeOffset();
					if (FMath::Abs(PinOffset.Y) > 0.1f)
					{
						Offset = static_cast<float>(PinOffset.Y);
						bUsedSlate = true;
					}
				}
			}
			// Fallback to heuristic
			if (!bUsedSlate)
			{
				Offset = EstimatePinYOffset(Node, FirstExecPin, Settings);
			}
		}
		NodeExecPinOffset.Add(Node, Offset);
		NodeUsedSlate.Add(Node, bUsedSlate);

		UE_LOG(LogMCP, Log, TEXT("  Phase 7 Pass1: '%s' Row=%d Offset=%.1f Source=%s"),
			*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
			NodeRow.Contains(Node) ? NodeRow[Node] : -1,
			Offset,
			bUsedSlate ? TEXT("Slate") : TEXT("Heuristic"));
	}

	// NOTE: Pass 2 (per-row "Force Heuristic" consistency enforcement) was REMOVED.
	// Root cause analysis (2026-02-16) showed that EstimatePinYOffset has large
	// errors for certain node types (VariableSet: 22px, CreateWidget: 4.2px),
	// making the rendered pin position 22px off from the intended alignment.
	// The Slate offset instability between runs (±1.8px) produces ≤0.4px
	// rendered PinY error after RoundToInt, which is sub-pixel and invisible.
	// Heuristic fallback is kept only for individual nodes whose Slate cache
	// is truly uninitialized (returns 0), without contaminating the whole row.

	// Row max height: use full node height for row spacing
	TMap<int32, float> RowMaxHeight;
	for (auto& Pair : NodeRow)
	{
		FVector2D Size = GetNodeSize(Pair.Key, Settings);
		float& MaxH = RowMaxHeight.FindOrAdd(Pair.Value);
		MaxH = FMath::Max(MaxH, Size.Y);
	}

	// Row max exec pin offset: within each row, find the largest exec pin offset
	// All nodes in the row will be shifted so their exec pins align at this Y.
	TMap<int32, float> RowMaxExecOffset;
	for (UEdGraphNode* Node : ChainExecNodes)
	{
		if (!NodeRow.Contains(Node)) continue;
		int32 Row = NodeRow[Node];
		float ExecOff = NodeExecPinOffset.Contains(Node) ? NodeExecPinOffset[Node] : 0.f;
		float& MaxOff = RowMaxExecOffset.FindOrAdd(Row);
		MaxOff = FMath::Max(MaxOff, ExecOff);
	}

	for (auto& RMPair : RowMaxExecOffset)
	{
		UE_LOG(LogMCP, Log, TEXT("  Phase 7: Row %d -> RowMaxExecOffset=%.1f RowMaxHeight=%.1f"),
			RMPair.Key, RMPair.Value,
			RowMaxHeight.Contains(RMPair.Key) ? RowMaxHeight[RMPair.Key] : 0.f);
	}

	TArray<float> RowY;
	RowY.SetNum(MaxRow + 2);
	RowY[0] = 0.f;
	for (int32 R = 1; R <= MaxRow + 1; R++)
	{
		float PrevHeight = RowMaxHeight.Contains(R - 1)
			? RowMaxHeight[R - 1] : Settings.FallbackNodeHeight;

		if (Settings.RowSpacing > 0.f)
		{
			RowY[R] = RowY[R - 1] + PrevHeight + Settings.RowSpacing;
		}
		else
		{
			RowY[R] = RowY[R - 1] + PrevHeight + Settings.VerticalGap;
		}
	}

	// Base position (first root's original position as anchor)
	float BaseX = 0.f;
	float BaseY = 0.f;

	if (Roots.Num() > 0)
	{
		BaseX = static_cast<float>(Roots[0]->NodePosX);
		BaseY = static_cast<float>(Roots[0]->NodePosY);
	}
	else if (ChainExecNodes.Num() > 0)
	{
		BaseX = static_cast<float>(ChainExecNodes[0]->NodePosX);
		BaseY = static_cast<float>(ChainExecNodes[0]->NodePosY);
	}

	// Build final pixel positions for this chain's Exec nodes
	// Y = RowY[Row] + (RowMaxExecOffset - NodeExecPinOffset)
	// This shifts each node down so that all exec pins in the same row
	// are at exactly the same Y coordinate.
	for (auto& Pair : NodeLayer)
	{
		UEdGraphNode* Node = Pair.Key;
		int32 Row = NodeRow[Node];
		float RelX = NodeRelX.Contains(Node) ? NodeRelX[Node] : 0.f;
		float X = BaseX + RelX;

		float ExecOff = NodeExecPinOffset.Contains(Node) ? NodeExecPinOffset[Node] : 0.f;
		float RowExecOff = RowMaxExecOffset.Contains(Row) ? RowMaxExecOffset[Row] : 0.f;
		float YShift = RowExecOff - ExecOff;
		float Y = BaseY + RowY[Row] + YShift;

		FinalPositions.Add(Node, FVector2D(X, Y));

		UE_LOG(LogMCP, Log, TEXT("  Phase 7 Pos: '%s' L=%d R=%d X=%.0f Y=%.0f (RowY=%.0f Off=%.1f MaxOff=%.1f Shift=%.1f PinY=%.1f)"),
			*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
			Pair.Value, Row, X, Y,
			RowY[Row], ExecOff, RowExecOff, YShift, Y + ExecOff);

		const int32 BlockId = NextBlockId++;
		NodeToBlockId.Add(Node, BlockId);
		BlockToNodes.FindOrAdd(BlockId).Add(Node);
	}

	// Phase 7 now handles exec pin Y alignment directly via
	// NodeExecPinOffset + RowMaxExecOffset compensation.
	// Phase 12.5 removed — it conflicted with Phase 7 by using
	// a different offset source (stale Slate cache vs heuristic).

	// =================================================================
	// Phase 8: Block-Centric Pure Node Placement
	//   Block = exec node + all upstream pure inputs (BFS from input pins)
	//   Each block occupies an exclusive Y band below the exec chain.
	//   Pure X = consumer exec X (or slightly left for deeper pures).
	//   Result: compact, readable layout where data flows left-to-right,
	//   pure nodes sit directly below the exec node they serve.
	// =================================================================
	{
		// --- Step 1: Build blocks by BFS from each exec node's input pins ---
		// For each exec node, trace input pins backward through pure chains.
		// Stop at other exec nodes (they form their own block).
		// A pure consumed by multiple exec nodes → assigned to the first
		// (leftmost) exec  that claims it.
		// PureToExecOwner is shared across chains (declared before loop)
		TMap<UEdGraphNode*, TArray<UEdGraphNode*>> ExecToPures; // exec → ordered pures
		TMap<UEdGraphNode*, int32> PureDepth; // depth from consumer (1 = direct)

		// Order exec nodes by layer (left to right) for deterministic claiming
		TArray<UEdGraphNode*> OrderedExecNodes;
		for (auto& Pair : NodeLayer)
		{
			OrderedExecNodes.Add(Pair.Key);
		}
		OrderedExecNodes.Sort([&NodeLayer](const UEdGraphNode& A, const UEdGraphNode& B)
		{
			return NodeLayer[&A] < NodeLayer[&B];
		});

		for (UEdGraphNode* ExecNode : OrderedExecNodes)
		{
			// BFS from ExecNode's input pins
			TArray<UEdGraphNode*> Queue;
			TSet<UEdGraphNode*> Visited;

			// Seed: direct pure inputs of this exec node
			for (UEdGraphPin* InPin : ExecNode->Pins)
			{
				if (!InPin || InPin->Direction != EGPD_Input) continue;
				// Skip exec pins — we only want data inputs
				if (InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;

				for (UEdGraphPin* Linked : InPin->LinkedTo)
				{
					if (!Linked) continue;
					UEdGraphNode* Source = Linked->GetOwningNode();
					if (!Source || !NodeSet.Contains(Source)) continue;
					if (HasExecPins(Source)) continue; // stop at exec boundary
					if (PureToExecOwner.Contains(Source)) continue; // already claimed
					if (Visited.Contains(Source)) continue;

					Visited.Add(Source);
					Queue.Add(Source);
					PureToExecOwner.Add(Source, ExecNode);
					PureDepth.Add(Source, 1);
					ExecToPures.FindOrAdd(ExecNode).Add(Source);
				}
			}

			// BFS deeper pure chains
			int32 QueueIdx = 0;
			while (QueueIdx < Queue.Num())
			{
				UEdGraphNode* Current = Queue[QueueIdx++];
				int32 CurrentDepth = PureDepth[Current];

				for (UEdGraphPin* InPin : Current->Pins)
				{
					if (!InPin || InPin->Direction != EGPD_Input) continue;
					for (UEdGraphPin* Linked : InPin->LinkedTo)
					{
						if (!Linked) continue;
						UEdGraphNode* Source = Linked->GetOwningNode();
						if (!Source || !NodeSet.Contains(Source)) continue;
						if (HasExecPins(Source)) continue;
						if (PureToExecOwner.Contains(Source)) continue;
						if (Visited.Contains(Source)) continue;

						Visited.Add(Source);
						Queue.Add(Source);
						PureToExecOwner.Add(Source, ExecNode);
						PureDepth.Add(Source, CurrentDepth + 1);
						ExecToPures.FindOrAdd(ExecNode).Add(Source);
					}
				}
			}
		}

		// Assign unclaimed pures within THIS group to nearest exec by X
		for (UEdGraphNode* PureNode : GroupPureNodes[ChainIdx])
		{
			if (PureToExecOwner.Contains(PureNode)) continue;

			// Find closest exec node by original position
			UEdGraphNode* ClosestExec = nullptr;
			float BestDist = TNumericLimits<float>::Max();
			for (UEdGraphNode* E : OrderedExecNodes)
			{
				float Dist = FMath::Abs((float)PureNode->NodePosX - (float)E->NodePosX)
					+ FMath::Abs((float)PureNode->NodePosY - (float)E->NodePosY);
				if (Dist < BestDist)
				{
					BestDist = Dist;
					ClosestExec = E;
				}
			}
			if (ClosestExec)
			{
				PureToExecOwner.Add(PureNode, ClosestExec);
				PureDepth.Add(PureNode, 1);
				ExecToPures.FindOrAdd(ClosestExec).Add(PureNode);
			}
		}

		// --- Step 2: Place pures directly below their exec consumer ---
		// All blocks start at the same Y (just below exec chain).
		// Since blocks occupy different X ranges (different exec layers),
		// they generally don't overlap. Phase 9 handles the rare case
		// where X ranges do overlap.

		// Per-block Y: each block's pures start just below their own exec node
		const float PureYGap = Settings.VerticalGap * 0.2f;

		for (UEdGraphNode* ExecNode : OrderedExecNodes)
		{
			TArray<UEdGraphNode*>* BlockPures = ExecToPures.Find(ExecNode);
			if (!BlockPures || BlockPures->Num() == 0)
			{
				continue;
			}

			// Compute this exec node's bottom Y
			float ExecBottomY = BaseY + 100.f;
			if (FinalPositions.Contains(ExecNode))
			{
				FVector2D EPos = FinalPositions[ExecNode];
				FVector2D ESize = GetNodeSize(ExecNode, Settings);
				ExecBottomY = EPos.Y + ESize.Y;
			}
			const float PureBandStartY = ExecBottomY + PureYGap;

			UE_LOG(LogMCP, Log, TEXT("  Phase 8: ExecOwner='%s' EPos=(%.0f,%.0f) ESize=(%.0f,%.0f) ExecBottomY=%.1f PureBandStartY=%.1f PureCount=%d"),
				*ExecNode->GetNodeTitle(ENodeTitleType::ListView).ToString(),
				FinalPositions.Contains(ExecNode) ? FinalPositions[ExecNode].X : 0.f,
				FinalPositions.Contains(ExecNode) ? FinalPositions[ExecNode].Y : 0.f,
				GetNodeSize(ExecNode, Settings).X, GetNodeSize(ExecNode, Settings).Y,
				ExecBottomY, PureBandStartY, BlockPures->Num());

			// Sort pures within block: depth ascending (direct inputs first),
			// then by original Y position for stability
			BlockPures->Sort([&PureDepth](const UEdGraphNode& A, const UEdGraphNode& B)
			{
				int32 DA = PureDepth.Contains(&A) ? PureDepth[&A] : 0;
				int32 DB = PureDepth.Contains(&B) ? PureDepth[&B] : 0;
				if (DA != DB) return DA < DB;
				return A.NodePosY < B.NodePosY;
			});

			// Get exec node position for X reference
			FVector2D ExecPos = FinalPositions[ExecNode];

			// --- Compute per-depth max width within this block ---
			int32 MaxDepthInBlock = 0;
			TMap<int32, float> DepthMaxWidth; // depth → max node width at that depth
			for (UEdGraphNode* PureNode : *BlockPures)
			{
				int32 Depth = PureDepth.Contains(PureNode) ? PureDepth[PureNode] : 1;
				FVector2D PureSize = GetNodeSize(PureNode, Settings);
				float& MaxW = DepthMaxWidth.FindOrAdd(Depth);
				MaxW = FMath::Max(MaxW, PureSize.X);
				MaxDepthInBlock = FMath::Max(MaxDepthInBlock, Depth);
			}

			// --- Compute X offset for each depth column ---
			// depth 1 → just left of exec, depth 2 → left of depth 1 column, etc.
			// Uses actual column widths instead of global LayerX grid
			const float PureGapX = Settings.HorizontalGap * 0.4f;
			TMap<int32, float> DepthColumnX; // depth → column X position
			{
				float CursorX = ExecPos.X; // start from exec node's left edge
				for (int32 D = 1; D <= MaxDepthInBlock; D++)
				{
					float ColWidth = DepthMaxWidth.Contains(D) ? DepthMaxWidth[D] : Settings.FallbackNodeWidth;
					CursorX -= (ColWidth + PureGapX);
					DepthColumnX.Add(D, CursorX);
				}
			}

			// --- Place pures: X from depth column, Y stacked per-depth ---
			TMap<int32, int32> DepthStackCount; // depth → count placed so far

			for (UEdGraphNode* PureNode : *BlockPures)
			{
				int32 Depth = PureDepth.Contains(PureNode) ? PureDepth[PureNode] : 1;
				FVector2D PureSize = GetNodeSize(PureNode, Settings);

				// X: from per-block depth column
				float X = DepthColumnX.Contains(Depth)
					? DepthColumnX[Depth]
					: (ExecPos.X - Settings.PureOffsetX);

				// Y: start from common band, stack per-depth
				// Gap must be >= CollisionTolerance to avoid Phase 9 re-pushing
				const float PureStackGap = FMath::Max(Settings.VerticalGap * 0.3f, Settings.CollisionTolerance + 2.f);
				int32& StackIdx = DepthStackCount.FindOrAdd(Depth);
				float Y = PureBandStartY
					+ StackIdx * (PureSize.Y + PureStackGap);
				StackIdx++;

				FinalPositions.Add(PureNode, FVector2D(X, Y));

				UE_LOG(LogMCP, Log, TEXT("  Phase 8 Pure: '%s' Depth=%d Owner='%s' Pos=(%.0f,%.0f) Size=(%.0f,%.0f)"),
					*PureNode->GetNodeTitle(ENodeTitleType::ListView).ToString(),
					Depth,
					*ExecNode->GetNodeTitle(ENodeTitleType::ListView).ToString(),
					X, Y, PureSize.X, PureSize.Y);
			}

			// Assign pures to same block as their exec consumer
			int32 ExecBlockId = NodeToBlockId[ExecNode];
			for (UEdGraphNode* PureNode : *BlockPures)
			{
				NodeToBlockId.Add(PureNode, ExecBlockId);
				BlockToNodes.FindOrAdd(ExecBlockId).AddUnique(PureNode);
			}
		}

		// Populate AllChainNodes from the full node group (exec + pure)
		// Since Phase 2.5 already grouped nodes by all-pin connectivity,
		// this is simply the entire group.
		ChainInfos[ChainIdx].AllChainNodes = NodeGroups[ChainIdx];
	}

	// =================================================================
	// Phase 8.5: Block X-space correction (layer-based)
	//   Compare adjacent LAYERS (not individual exec nodes) to prevent
	//   pure-node blocks from overlapping across layer boundaries.
	//   Same-layer exec nodes are separated vertically and never need
	//   horizontal clearance between each other.
	// =================================================================
	{
		const float BlockGapX = Settings.HorizontalGap * 0.35f;

		// Block X range helper (exec + its pure dependencies)
		auto ComputeBlockXRange = [&](UEdGraphNode* ExecNode, float& OutMinX, float& OutMaxX)
		{
			OutMinX = TNumericLimits<float>::Max();
			OutMaxX = TNumericLimits<float>::Lowest();

			if (FinalPositions.Contains(ExecNode))
			{
				FVector2D Pos = FinalPositions[ExecNode];
				FVector2D Size = GetNodeSize(ExecNode, Settings);
				OutMinX = FMath::Min(OutMinX, Pos.X);
				OutMaxX = FMath::Max(OutMaxX, Pos.X + Size.X);
			}

			if (const int32* BlockIdPtr = NodeToBlockId.Find(ExecNode))
			{
				if (const TArray<UEdGraphNode*>* BlockNodes = BlockToNodes.Find(*BlockIdPtr))
				{
					for (UEdGraphNode* Node : *BlockNodes)
					{
						if (Node == ExecNode) continue;
						if (!FinalPositions.Contains(Node)) continue;
						FVector2D Pos = FinalPositions[Node];
						FVector2D Size = GetNodeSize(Node, Settings);
						OutMinX = FMath::Min(OutMinX, Pos.X);
						OutMaxX = FMath::Max(OutMaxX, Pos.X + Size.X);
					}
				}
			}
		};

		// Sweep layers left-to-right: compare layer L-1 vs layer L
		for (int32 L = 1; L <= MaxLayer; L++)
		{
			if (!LayerToNodes.Contains(L) || !LayerToNodes.Contains(L - 1)) continue;

			// Max right edge across ALL blocks at previous layer
			float PrevMaxRightX = TNumericLimits<float>::Lowest();
			for (UEdGraphNode* Node : LayerToNodes[L - 1])
			{
				float MinX, MaxX;
				ComputeBlockXRange(Node, MinX, MaxX);
				PrevMaxRightX = FMath::Max(PrevMaxRightX, MaxX);
			}

			// Min left edge across ALL blocks at current layer
			float CurrMinLeftX = TNumericLimits<float>::Max();
			for (UEdGraphNode* Node : LayerToNodes[L])
			{
				float MinX, MaxX;
				ComputeBlockXRange(Node, MinX, MaxX);
				CurrMinLeftX = FMath::Min(CurrMinLeftX, MinX);
			}

			float RequiredX = PrevMaxRightX + BlockGapX;
			if (CurrMinLeftX < RequiredX)
			{
				float ShiftX = RequiredX - CurrMinLeftX;

				// Shift all nodes at layers [L .. MaxLayer]
				for (int32 ShiftL = L; ShiftL <= MaxLayer; ShiftL++)
				{
					if (!LayerToNodes.Contains(ShiftL)) continue;
					for (UEdGraphNode* ExecNode : LayerToNodes[ShiftL])
					{
						if (FinalPositions.Contains(ExecNode))
						{
							FinalPositions[ExecNode].X += ShiftX;
						}

						if (const int32* BlockIdPtr = NodeToBlockId.Find(ExecNode))
						{
							if (const TArray<UEdGraphNode*>* BlockNodes = BlockToNodes.Find(*BlockIdPtr))
							{
								for (UEdGraphNode* Node : *BlockNodes)
								{
									if (Node == ExecNode) continue;
									if (!FinalPositions.Contains(Node)) continue;
									FinalPositions[Node].X += ShiftX;
								}
							}
						}
					}
				}
			}
		}
	}

	} // END per-chain loop (Phases 3 – 8.5)

	// =================================================================
	// Phase 8.55: Root X left-alignment
	//   Align all chains so their leftmost positioned node shares the
	//   same X coordinate. This gives a clean left-aligned appearance
	//   when multiple independent chains are stacked vertically.
	// =================================================================
	if (ExecChains.Num() > 1)
	{
		// Find global minimum X across all chains' leftmost nodes
		float GlobalMinX = TNumericLimits<float>::Max();
		TArray<float> ChainMinX;
		ChainMinX.SetNum(ChainInfos.Num());

		for (int32 CI = 0; CI < ChainInfos.Num(); CI++)
		{
			float MinX = TNumericLimits<float>::Max();
			for (UEdGraphNode* Node : ChainInfos[CI].AllChainNodes)
			{
				if (FinalPositions.Contains(Node))
				{
					MinX = FMath::Min(MinX, FinalPositions[Node].X);
				}
			}
			ChainMinX[CI] = MinX;
			GlobalMinX = FMath::Min(GlobalMinX, MinX);
		}

		// Shift each chain so its leftmost node aligns with GlobalMinX
		for (int32 CI = 0; CI < ChainInfos.Num(); CI++)
		{
			float ShiftX = GlobalMinX - ChainMinX[CI];
			if (FMath::Abs(ShiftX) < 1.f) continue;

			for (UEdGraphNode* Node : ChainInfos[CI].AllChainNodes)
			{
				if (FinalPositions.Contains(Node))
				{
					FinalPositions[Node].X += ShiftX;
				}
			}
		}
	}

	// =================================================================
	// Phase 8.6: Vertical stacking of independent chains
	//   Each chain was laid out with its own BaseY. Now we re-stack
	//   them vertically so they don't overlap, using their actual
	//   bounding boxes from FinalPositions.
	// =================================================================
	if (ExecChains.Num() > 1)
	{
		const float ChainGapY = Settings.VerticalGap * 1.5f;

		// Compute per-chain AABB
		struct FChainAABB
		{
			int32 ChainIdx;
			float MinY, MaxY;
		};
		TArray<FChainAABB> ChainBounds;

		for (int32 CI = 0; CI < ChainInfos.Num(); CI++)
		{
			const FChainInfo& Info = ChainInfos[CI];
			if (Info.AllChainNodes.Num() == 0) continue;

			FChainAABB Bounds;
			Bounds.ChainIdx = CI;
			Bounds.MinY = TNumericLimits<float>::Max();
			Bounds.MaxY = TNumericLimits<float>::Lowest();

			for (UEdGraphNode* Node : Info.AllChainNodes)
			{
				if (!FinalPositions.Contains(Node)) continue;
				FVector2D Pos = FinalPositions[Node];
				FVector2D Size = GetNodeSize(Node, Settings);
				Bounds.MinY = FMath::Min(Bounds.MinY, Pos.Y);
				Bounds.MaxY = FMath::Max(Bounds.MaxY, Pos.Y + Size.Y);
			}

			ChainBounds.Add(Bounds);
		}

		if (ChainBounds.Num() > 1)
		{
			// First chain keeps its Y position; subsequent chains stack below
			float CursorY = ChainBounds[0].MinY;
			for (int32 BI = 0; BI < ChainBounds.Num(); BI++)
			{
				FChainAABB& Bounds = ChainBounds[BI];
				float ChainHeight = Bounds.MaxY - Bounds.MinY;
				float OffsetY = CursorY - Bounds.MinY;

				if (FMath::Abs(OffsetY) > 1.f)
				{
					const FChainInfo& Info = ChainInfos[Bounds.ChainIdx];
					for (UEdGraphNode* Node : Info.AllChainNodes)
					{
						if (FinalPositions.Contains(Node))
						{
							FinalPositions[Node].Y += OffsetY;
						}
					}
				}

				CursorY += ChainHeight + ChainGapY;
			}
		}
	}

	// =================================================================
	// Phase 8.7: Assign orphan pure nodes
	//   Pure nodes not claimed by any chain's exec BFS.
	// =================================================================
	{
		float OrphanBaseY = 0.f;
		float OrphanBaseX = 0.f;
		if (FinalPositions.Num() > 0)
		{
			for (auto& Pair : FinalPositions)
			{
				FVector2D Size = GetNodeSize(Pair.Key, Settings);
				OrphanBaseY = FMath::Max(OrphanBaseY, Pair.Value.Y + Size.Y);
			}
			OrphanBaseX = FinalPositions.begin()->Value.X;
			OrphanBaseY += Settings.VerticalGap;
		}

		int32 OrphanCount = 0;
		for (UEdGraphNode* PureNode : PureNodes)
		{
			if (FinalPositions.Contains(PureNode)) continue;

			FVector2D PureSize = GetNodeSize(PureNode, Settings);
			FinalPositions.Add(PureNode, FVector2D(OrphanBaseX, OrphanBaseY));

			UE_LOG(LogMCP, Log, TEXT("  Phase 8.7 Orphan: '%s' Pos=(%.0f,%.0f) Size=(%.0f,%.0f)"),
				*PureNode->GetNodeTitle(ENodeTitleType::ListView).ToString(),
				OrphanBaseX, OrphanBaseY, PureSize.X, PureSize.Y);

			OrphanBaseY += PureSize.Y + Settings.VerticalGap * 0.3f;
			OrphanCount++;

			int32 FallbackBlock = NextBlockId++;
			NodeToBlockId.Add(PureNode, FallbackBlock);
			BlockToNodes.FindOrAdd(FallbackBlock).Add(PureNode);
		}
		if (OrphanCount > 0)
		{
			UE_LOG(LogMCP, Log, TEXT("  Phase 8.7: %d orphan pure node(s) placed"), OrphanCount);
		}
	}

	// =================================================================
	// Phase 9: Global collision resolution (all positioned nodes)
	//   Checks ALL positioned nodes against each other for AABB overlap.
	//   Exec-exec pairs are never moved (stable backbone).
	//   If a pure overlaps with anything (exec or pure), the pure is pushed down.
	//   Handles cross-chain overlaps in multi-entry-point graphs (EventGraph).
	// =================================================================
	{
		const float Padding = Settings.CollisionTolerance;
		const int32 MaxIterations = 15;

		TArray<UEdGraphNode*> AllPositioned;
		for (auto& Pair : FinalPositions)
		{
			AllPositioned.Add(Pair.Key);
		}

		for (int32 Iter = 0; Iter < MaxIterations; Iter++)
		{
			bool bMoved = false;

			// Sort by Y for stable sweep
			AllPositioned.Sort([&FinalPositions](const UEdGraphNode& A, const UEdGraphNode& B)
			{
				return FinalPositions[&A].Y < FinalPositions[&B].Y;
			});

			for (int32 i = 0; i < AllPositioned.Num(); i++)
			{
				UEdGraphNode* NodeA = AllPositioned[i];
				FVector2D PosA = FinalPositions[NodeA];
				FVector2D SizeA = GetNodeSize(NodeA, Settings);

				for (int32 j = i + 1; j < AllPositioned.Num(); j++)
				{
					UEdGraphNode* NodeB = AllPositioned[j];
					FVector2D PosB = FinalPositions[NodeB];
					FVector2D SizeB = GetNodeSize(NodeB, Settings);

					bool bOverlapX = (PosA.X < PosB.X + SizeB.X + Padding)
						&& (PosA.X + SizeA.X + Padding > PosB.X);
					bool bOverlapY = (PosA.Y < PosB.Y + SizeB.Y + Padding)
						&& (PosA.Y + SizeA.Y + Padding > PosB.Y);

					// Early Y-cutoff: nodes sorted by Y, so if B's top is
					// already beyond A's bottom + padding, no further j can overlap.
					if (PosB.Y > PosA.Y + SizeA.Y + Padding)
					{
						break;
					}

					if (bOverlapX && bOverlapY)
					{
						bool bAIsExec = HasExecPins(NodeA);
						bool bBIsExec = HasExecPins(NodeB);

						// Determine which node to move and in which direction:
						// - Pure vs anything: push pure node
						// - Exec vs Exec: push the one with higher layer/row (NodeB)
						//   using minimum displacement direction (Y or X)
						UEdGraphNode* NodeToMove = nullptr;
						UEdGraphNode* Anchor = nullptr;

						if (bAIsExec && bBIsExec)
						{
							// Exec-Exec: push NodeB (higher Y due to sort)
							// Choose minimum displacement direction
							float PushDownY = PosA.Y + SizeA.Y + Padding - PosB.Y;
							float PushRightX = PosA.X + SizeA.X + Padding - PosB.X;

							if (PushDownY > 0 || PushRightX > 0)
							{
								// Prefer X push for exec nodes to maintain horizontal flow
								if (PushRightX > 0 && PushRightX <= PushDownY * 1.5f)
								{
									FinalPositions[NodeB].X = PosA.X + SizeA.X + Padding;
								}
								else if (PushDownY > 0)
								{
									FinalPositions[NodeB].Y = PosA.Y + SizeA.Y + Padding;
								}
								else
								{
									FinalPositions[NodeB].X = PosA.X + SizeA.X + Padding;
								}
								bMoved = true;
							}
						}
						else if (!bBIsExec)
						{
							// NodeB is pure — push it
							NodeToMove = NodeB;
							Anchor = NodeA;
						}
						else if (!bAIsExec)
						{
							// NodeA is pure, NodeB is exec — push NodeA
							NodeToMove = NodeA;
							Anchor = NodeB;
						}

						if (NodeToMove && Anchor)
						{
							FVector2D AnchorPos = FinalPositions[Anchor];
							FVector2D AnchorSize = GetNodeSize(Anchor, Settings);
							FVector2D& MovePos = FinalPositions[NodeToMove];
							FVector2D MoveSize = GetNodeSize(NodeToMove, Settings);

							// Choose minimum displacement direction
							float PushDownY = AnchorPos.Y + AnchorSize.Y + Padding - MovePos.Y;
							float PushRightX = AnchorPos.X + AnchorSize.X + Padding - MovePos.X;
							float PushLeftX = MovePos.X + MoveSize.X + Padding - AnchorPos.X;

							// Default: push Y (most common for pure nodes below exec)
							if (PushDownY > 0)
							{
								MovePos.Y = AnchorPos.Y + AnchorSize.Y + Padding;
								bMoved = true;
							}
						}
					}
				}
			}

			if (!bMoved) break;
		}
	}

	// =================================================================
	// Phase 10: Surrounding node avoidance (per-node)
	//   Each positioned node is individually checked against all
	//   surrounding (non-layout) nodes. On overlap, the positioned
	//   node is pushed below the obstacle. After all pushes,
	//   Phase 9-style global collision resolution is re-run to
	//   fix any internal overlaps caused by the avoidance shifts.
	// =================================================================
	if (Settings.bAvoidSurrounding && SurroundingNodes && SurroundingNodes->Num() > 0)
	{
		struct FNodeBBox
		{
			float MinX, MinY, MaxX, MaxY;
		};

		// Build obstacle list from surrounding nodes
		TArray<FNodeBBox> Obstacles;
		for (UEdGraphNode* SurrNode : *SurroundingNodes)
		{
			if (!SurrNode || NodeSet.Contains(SurrNode)) continue;
			FVector2D Size = GetNodeSize(SurrNode, Settings);
			float Margin = Settings.SurroundingMargin;
			FNodeBBox Box;
			Box.MinX = SurrNode->NodePosX - Margin;
			Box.MinY = SurrNode->NodePosY - Margin;
			Box.MaxX = SurrNode->NodePosX + Size.X + Margin;
			Box.MaxY = SurrNode->NodePosY + Size.Y + Margin;
			Obstacles.Add(Box);
		}

		if (Obstacles.Num() > 0)
		{
			const float Padding = Settings.CollisionTolerance;
			const int32 MaxPasses = 5;

			// Per-node avoidance: push each positioned node below obstacles
			for (int32 Pass = 0; Pass < MaxPasses; Pass++)
			{
				bool bAnyMoved = false;

				for (auto& Pair : FinalPositions)
				{
					UEdGraphNode* Node = Pair.Key;
					FVector2D& Pos = Pair.Value;
					FVector2D Size = GetNodeSize(Node, Settings);

					for (const FNodeBBox& Obs : Obstacles)
					{
						bool bOverlap = !(Pos.X + Size.X + Padding < Obs.MinX
							|| Pos.X - Padding > Obs.MaxX
							|| Pos.Y + Size.Y + Padding < Obs.MinY
							|| Pos.Y - Padding > Obs.MaxY);

						if (bOverlap)
						{
							// Push node below the obstacle
							float NewY = Obs.MaxY + Padding;
							if (Pos.Y < NewY)
							{
								Pos.Y = NewY;
								bAnyMoved = true;
							}
						}
					}
				}

				if (!bAnyMoved) break;
			}

			// Re-run global collision resolution after avoidance pushes
			{
				const int32 ResolveIterations = 12;

				TArray<UEdGraphNode*> AllPos;
				for (auto& Pair : FinalPositions)
				{
					AllPos.Add(Pair.Key);
				}

				for (int32 Iter = 0; Iter < ResolveIterations; Iter++)
				{
					bool bMoved = false;

					AllPos.Sort([&FinalPositions](const UEdGraphNode& A, const UEdGraphNode& B)
					{
						return FinalPositions[&A].Y < FinalPositions[&B].Y;
					});

					for (int32 i = 0; i < AllPos.Num(); i++)
					{
						UEdGraphNode* NodeA = AllPos[i];
						FVector2D PosA = FinalPositions[NodeA];
						FVector2D SizeA = GetNodeSize(NodeA, Settings);

						for (int32 j = i + 1; j < AllPos.Num(); j++)
						{
							UEdGraphNode* NodeB = AllPos[j];
							FVector2D PosB = FinalPositions[NodeB];
							FVector2D SizeB = GetNodeSize(NodeB, Settings);

							bool bOX = (PosA.X < PosB.X + SizeB.X + Padding)
								&& (PosA.X + SizeA.X + Padding > PosB.X);
							bool bOY = (PosA.Y < PosB.Y + SizeB.Y + Padding)
								&& (PosA.Y + SizeA.Y + Padding > PosB.Y);

							if (bOX && bOY)
							{
								bool bAExec = HasExecPins(NodeA);
								bool bBExec = HasExecPins(NodeB);
								if (bAExec && bBExec) continue;

								UEdGraphNode* ToMove = !bBExec ? NodeB : (!bAExec ? NodeA : nullptr);
								UEdGraphNode* Anchor = (ToMove == NodeB) ? NodeA : NodeB;
								if (ToMove && Anchor)
								{
									FVector2D APos = FinalPositions[Anchor];
									FVector2D ASize = GetNodeSize(Anchor, Settings);
									float NewY = APos.Y + ASize.Y + Padding;
									if (FinalPositions[ToMove].Y < NewY)
									{
										FinalPositions[ToMove].Y = NewY;
										bMoved = true;
									}
								}
							}
						}
					}

					if (!bMoved) break;
				}
			}
		}
	}

	// =================================================================
	// Phase 11: Comment box adjustment
	//   Resize comment nodes to fit their contained children
	// =================================================================
	if (Settings.bPreserveComments)
	{
		for (UEdGraphNode* CommentNode : CommentNodes)
		{
			UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(CommentNode);
			if (!Comment) continue;

			// Find which positioned nodes were originally inside this comment
			float CMinX = static_cast<float>(Comment->NodePosX);
			float CMinY = static_cast<float>(Comment->NodePosY);
			float CMaxX = CMinX + static_cast<float>(Comment->NodeWidth);
			float CMaxY = CMinY + static_cast<float>(Comment->NodeHeight);

			TArray<UEdGraphNode*> ContainedNodes;
			for (auto& Pair : FinalPositions)
			{
				if (Pair.Key == Comment) continue;
				float OrigX = static_cast<float>(Pair.Key->NodePosX);
				float OrigY = static_cast<float>(Pair.Key->NodePosY);
				if (OrigX >= CMinX && OrigX <= CMaxX && OrigY >= CMinY && OrigY <= CMaxY)
				{
					ContainedNodes.Add(Pair.Key);
				}
			}

			if (ContainedNodes.Num() > 0)
			{
				float NewMinX = TNumericLimits<float>::Max();
				float NewMinY = TNumericLimits<float>::Max();
				float NewMaxX = TNumericLimits<float>::Lowest();
				float NewMaxY = TNumericLimits<float>::Lowest();

				for (UEdGraphNode* Node : ContainedNodes)
				{
					FVector2D NodePos = FinalPositions[Node];
					FVector2D NodeSize = GetNodeSize(Node, Settings);
					NewMinX = FMath::Min(NewMinX, NodePos.X);
					NewMinY = FMath::Min(NewMinY, NodePos.Y);
					NewMaxX = FMath::Max(NewMaxX, NodePos.X + NodeSize.X);
					NewMaxY = FMath::Max(NewMaxY, NodePos.Y + NodeSize.Y);
				}

				float Padding = 40.f;
				float TitleHeight = 36.f;
				FVector2D CommentPos(NewMinX - Padding, NewMinY - Padding - TitleHeight);
				int32 NewWidth = FMath::RoundToInt(NewMaxX - NewMinX + Padding * 2.f);
				int32 NewHeight = FMath::RoundToInt(NewMaxY - NewMinY + Padding * 2.f + TitleHeight);

				FinalPositions.Add(Comment, CommentPos);
				Comment->Modify();
				Comment->NodeWidth = NewWidth;
				Comment->NodeHeight = NewHeight;
			}
		}
	}

	// =================================================================
	// Phase 12: Apply positions with Modify() for Undo support
	// =================================================================
	int32 MovedCount = 0;

	for (auto& Pair : FinalPositions)
	{
		UEdGraphNode* Node = Pair.Key;
		int32 NewX = FMath::RoundToInt(Pair.Value.X);
		int32 NewY = FMath::RoundToInt(Pair.Value.Y);

		if (Node->NodePosX != NewX || Node->NodePosY != NewY)
		{
			Node->Modify();
			Node->NodePosX = NewX;
			Node->NodePosY = NewY;
			MovedCount++;
		}
	}

	return MovedCount;
}


// ============================================================================
// FAutoLayoutSelectedAction
// ============================================================================

bool FAutoLayoutSelectedAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// mode is optional, defaults to "selected"
	return true;
}

TSharedPtr<FJsonObject> FAutoLayoutSelectedAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Mode = TEXT("selected");
	if (Params->HasField(TEXT("mode")))
	{
		Mode = Params->GetStringField(TEXT("mode"));
	}

	FBlueprintEditor* BPEditor = nullptr;
	UEdGraph* Graph = ResolveGraph(Params, BPEditor);

	if (!Graph)
	{
		return CreateErrorResponse(TEXT("Could not find graph. Specify blueprint_name/graph_name or focus a Blueprint editor."));
	}

	FBlueprintLayoutSettings Settings = ParseLayoutSettings(Params);

	// Collect surrounding nodes for avoidance if enabled
	TArray<UEdGraphNode*> SurroundingNodes;
	bool bIncludePureDeps = false;
	if (Params->HasField(TEXT("include_pure_deps")))
	{
		bIncludePureDeps = Params->GetBoolField(TEXT("include_pure_deps"));
	}

	TArray<UEdGraphNode*> NodesToLayout;

	if (Mode == TEXT("selected"))
	{
		// Try to get selected nodes from editor
		if (Params->HasField(TEXT("node_ids")))
		{
			// Explicit node IDs
			const TArray<TSharedPtr<FJsonValue>>& NodeIds = Params->GetArrayField(TEXT("node_ids"));
			for (const auto& IdVal : NodeIds)
			{
				FString GuidStr = IdVal->AsString();
				UEdGraphNode* Node = FindNodeByGuid(Graph, GuidStr);
				if (Node)
				{
					NodesToLayout.Add(Node);
				}
			}
		}
		else if (BPEditor)
		{
			// Get selected nodes from the focused editor
			FGraphPanelSelectionSet SelectedNodes = BPEditor->GetSelectedNodes();
			for (UObject* Obj : SelectedNodes)
			{
				UEdGraphNode* Node = Cast<UEdGraphNode>(Obj);
				if (Node && Graph->Nodes.Contains(Node))
				{
					NodesToLayout.Add(Node);
				}
			}
		}

		if (NodesToLayout.Num() == 0)
		{
			ShowNotification(TEXT("No nodes selected for layout."), false);
			return CreateErrorResponse(TEXT("No nodes selected. Select nodes in the Blueprint editor or provide node_ids."));
		}

		// Optionally include pure dependencies of selected nodes
		if (bIncludePureDeps)
		{
			TSet<UEdGraphNode*> NodeSet;
			for (UEdGraphNode* N : NodesToLayout) if (N) NodeSet.Add(N);
			TSet<UEdGraphNode*> PureVisited;
			TArray<UEdGraphNode*> ExtraPures;
			for (UEdGraphNode* N : TArray<UEdGraphNode*>(NodesToLayout))
			{
				if (!N) continue;
				TSet<UEdGraphNode*> GraphNodeSet;
				for (UEdGraphNode* GN : Graph->Nodes) if (GN) GraphNodeSet.Add(GN);
				FBlueprintAutoLayout::CollectPureDependencies(N, GraphNodeSet, ExtraPures, PureVisited, 5);
			}
			for (UEdGraphNode* Pure : ExtraPures)
			{
				if (Pure && !NodeSet.Contains(Pure))
				{
					NodesToLayout.Add(Pure);
					NodeSet.Add(Pure);
				}
			}
		}
	}
	else if (Mode == TEXT("graph"))
	{
		// Layout all nodes in current graph
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node) NodesToLayout.Add(Node);
		}
	}
	else if (Mode == TEXT("all"))
	{
		// Layout all graphs in Blueprint
		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
		if (!Blueprint)
		{
			return CreateErrorResponse(TEXT("Could not find Blueprint for graph."));
		}

		int32 TotalMoved = 0;
		FScopedTransaction Transaction(FText::FromString(TEXT("Auto Layout All Graphs")));

		auto LayoutGraphNodes = [&](UEdGraph* G)
		{
			TArray<UEdGraphNode*> AllNodes;
			for (UEdGraphNode* Node : G->Nodes)
			{
				if (Node) AllNodes.Add(Node);
			}
			TotalMoved += FBlueprintAutoLayout::LayoutNodes(G, AllNodes, Settings);
		};

		for (UEdGraph* G : Blueprint->UbergraphPages)
		{
			if (G) LayoutGraphNodes(G);
		}
		for (UEdGraph* G : Blueprint->FunctionGraphs)
		{
			if (G) LayoutGraphNodes(G);
		}
		for (UEdGraph* G : Blueprint->MacroGraphs)
		{
			if (G) LayoutGraphNodes(G);
		}

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("nodes_moved"), TotalMoved);
		Result->SetStringField(TEXT("mode"), TEXT("all"));
		ShowNotification(FString::Printf(TEXT("Auto Layout All: %d nodes moved"), TotalMoved));
		return CreateSuccessResponse(Result);
	}
	else
	{
		return CreateErrorResponse(FString::Printf(TEXT("Unknown mode '%s'. Use 'selected', 'graph', or 'all'."), *Mode));
	}

	// Execute layout with undo
	FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("Auto Layout %s"), *Mode)));

	// Collect surrounding nodes for avoidance
	if (Settings.bAvoidSurrounding)
	{
		TSet<UEdGraphNode*> LayoutSet;
		for (UEdGraphNode* N : NodesToLayout) if (N) LayoutSet.Add(N);
		for (UEdGraphNode* N : Graph->Nodes)
		{
			if (N && !LayoutSet.Contains(N))
			{
				SurroundingNodes.Add(N);
			}
		}
	}

	const TArray<UEdGraphNode*>* SurrPtr = SurroundingNodes.Num() > 0 ? &SurroundingNodes : nullptr;
	int32 MovedCount = FBlueprintAutoLayout::LayoutNodes(Graph, NodesToLayout, Settings, SurrPtr);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("nodes_moved"), MovedCount);
	Result->SetNumberField(TEXT("total_nodes"), NodesToLayout.Num());
	Result->SetStringField(TEXT("mode"), Mode);
	Result->SetStringField(TEXT("graph_name"), Graph->GetName());

	ShowNotification(FString::Printf(TEXT("Auto Layout: %d/%d nodes moved"), MovedCount, NodesToLayout.Num()));
	return CreateSuccessResponse(Result);
}


// ============================================================================
// FAutoLayoutSubtreeAction
// ============================================================================

bool FAutoLayoutSubtreeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// root_node_id is required unless we have a single selected node via editor
	return true;
}

TSharedPtr<FJsonObject> FAutoLayoutSubtreeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FBlueprintEditor* BPEditor = nullptr;
	UEdGraph* Graph = ResolveGraph(Params, BPEditor);

	if (!Graph)
	{
		return CreateErrorResponse(TEXT("Could not find graph. Specify blueprint_name/graph_name or focus a Blueprint editor."));
	}

	FBlueprintLayoutSettings Settings = ParseLayoutSettings(Params);

	int32 MaxPureDepth = 3;
	if (Params->HasField(TEXT("max_pure_depth")))
	{
		MaxPureDepth = static_cast<int32>(Params->GetNumberField(TEXT("max_pure_depth")));
	}

	// Find root node
	UEdGraphNode* RootNode = nullptr;

	if (Params->HasField(TEXT("root_node_id")))
	{
		FString RootGuid = Params->GetStringField(TEXT("root_node_id"));
		RootNode = FindNodeByGuid(Graph, RootGuid);
	}
	else if (BPEditor)
	{
		// Try to use single selected node as root
		RootNode = BPEditor->GetSingleSelectedNode();
	}

	if (!RootNode)
	{
		ShowNotification(TEXT("No root node specified or selected for subtree layout."), false);
		return CreateErrorResponse(TEXT("No root node found. Select a single node or provide root_node_id."));
	}

	// Collect subtree
	TArray<UEdGraphNode*> SubtreeNodes = FBlueprintAutoLayout::CollectExecSubtree(RootNode, MaxPureDepth);

	if (SubtreeNodes.Num() == 0)
	{
		return CreateErrorResponse(TEXT("No nodes found in subtree from root."));
	}

	// Execute layout with undo
	FScopedTransaction Transaction(FText::FromString(TEXT("Auto Layout Subtree")));

	// Collect surrounding nodes for avoidance
	TArray<UEdGraphNode*> SurroundingNodes;
	if (Settings.bAvoidSurrounding)
	{
		TSet<UEdGraphNode*> SubtreeSet;
		for (UEdGraphNode* N : SubtreeNodes) if (N) SubtreeSet.Add(N);
		for (UEdGraphNode* N : Graph->Nodes)
		{
			if (N && !SubtreeSet.Contains(N))
			{
				SurroundingNodes.Add(N);
			}
		}
	}

	const TArray<UEdGraphNode*>* SurrPtr = SurroundingNodes.Num() > 0 ? &SurroundingNodes : nullptr;
	int32 MovedCount = FBlueprintAutoLayout::LayoutNodes(Graph, SubtreeNodes, Settings, SurrPtr);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("nodes_moved"), MovedCount);
	Result->SetNumberField(TEXT("subtree_size"), SubtreeNodes.Num());
	Result->SetStringField(TEXT("root_node"), RootNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
	Result->SetStringField(TEXT("root_node_id"), RootNode->NodeGuid.ToString());
	Result->SetStringField(TEXT("graph_name"), Graph->GetName());

	ShowNotification(FString::Printf(TEXT("Auto Layout Subtree: %d/%d nodes moved"), MovedCount, SubtreeNodes.Num()));
	return CreateSuccessResponse(Result);
}


// ============================================================================
// FAutoLayoutBlueprintAction
// ============================================================================

bool FAutoLayoutBlueprintAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return true;
}

TSharedPtr<FJsonObject> FAutoLayoutBlueprintAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	// 1. Resolve the Blueprint
	UBlueprint* Blueprint = nullptr;

	if (Params->HasField(TEXT("blueprint_name")))
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint_name"));
		Blueprint = FMCPCommonUtils::FindBlueprint(BlueprintName);
		if (!Blueprint)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Blueprint '%s' not found."), *BlueprintName));
		}
	}
	else
	{
		// Try focused editor
		FBlueprintEditor* BPEditor = FMCPCommonUtils::GetActiveBlueprintEditor();
		if (BPEditor)
		{
			UEdGraph* FocusedGraph = BPEditor->GetFocusedGraph();
			if (FocusedGraph)
			{
				Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(FocusedGraph);
			}
		}

		if (!Blueprint)
		{
			return CreateErrorResponse(TEXT("No Blueprint specified and no focused Blueprint editor found. Provide 'blueprint_name'."));
		}
	}

	// 2. Parse layout settings
	FBlueprintLayoutSettings Settings = ParseLayoutSettings(Params);

	// 3. Collect all graphs
	TArray<UEdGraph*> AllGraphs;

	for (UEdGraph* G : Blueprint->UbergraphPages)
	{
		if (G) AllGraphs.Add(G);
	}
	for (UEdGraph* G : Blueprint->FunctionGraphs)
	{
		if (G) AllGraphs.Add(G);
	}
	for (UEdGraph* G : Blueprint->MacroGraphs)
	{
		if (G) AllGraphs.Add(G);
	}

	if (AllGraphs.Num() == 0)
	{
		return CreateErrorResponse(TEXT("No graphs found in Blueprint."));
	}

	// 4. Layout each graph
	FScopedTransaction Transaction(FText::FromString(TEXT("Auto Layout Blueprint (All Graphs)")));

	int32 TotalMoved = 0;
	int32 TotalNodes = 0;
	TArray<TSharedPtr<FJsonValue>> GraphResults;

	for (UEdGraph* G : AllGraphs)
	{
		TArray<UEdGraphNode*> AllNodes;
		for (UEdGraphNode* Node : G->Nodes)
		{
			if (Node) AllNodes.Add(Node);
		}

		if (AllNodes.Num() == 0) continue;

		int32 Moved = FBlueprintAutoLayout::LayoutNodes(G, AllNodes, Settings);

		TotalMoved += Moved;
		TotalNodes += AllNodes.Num();

		// Per-graph result entry
		TSharedPtr<FJsonObject> GraphEntry = MakeShared<FJsonObject>();
		GraphEntry->SetStringField(TEXT("graph_name"), G->GetName());
		GraphEntry->SetNumberField(TEXT("nodes_moved"), Moved);
		GraphEntry->SetNumberField(TEXT("total_nodes"), AllNodes.Num());
		GraphResults.Add(MakeShared<FJsonValueObject>(GraphEntry));
	}

	// 5. Return summary
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("blueprint"), Blueprint->GetName());
	Result->SetNumberField(TEXT("graphs_processed"), AllGraphs.Num());
	Result->SetNumberField(TEXT("total_nodes_moved"), TotalMoved);
	Result->SetNumberField(TEXT("total_nodes"), TotalNodes);
	Result->SetArrayField(TEXT("graphs"), GraphResults);

	ShowNotification(FString::Printf(TEXT("Auto Layout Blueprint: %d graphs, %d/%d nodes moved"),
		AllGraphs.Num(), TotalMoved, TotalNodes));

	return CreateSuccessResponse(Result);
}


// ============================================================================
// FLayoutAndCommentAction
// ============================================================================

bool FLayoutAndCommentAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* GroupsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("groups"), GroupsArray) || !GroupsArray || GroupsArray->Num() == 0)
	{
		OutError = TEXT("'groups' is required and must be a non-empty array.");
		return false;
	}

	for (int32 i = 0; i < GroupsArray->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* GroupObj = nullptr;
		if (!(*GroupsArray)[i]->TryGetObject(GroupObj) || !GroupObj || !GroupObj->IsValid())
		{
			OutError = FString::Printf(TEXT("groups[%d] must be a JSON object."), i);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* NodeIds = nullptr;
		if (!(*GroupObj)->TryGetArrayField(TEXT("node_ids"), NodeIds) || !NodeIds || NodeIds->Num() == 0)
		{
			OutError = FString::Printf(TEXT("groups[%d].node_ids is required and must be non-empty."), i);
			return false;
		}

		FString CommentText;
		if (!(*GroupObj)->TryGetStringField(TEXT("comment_text"), CommentText) || CommentText.IsEmpty())
		{
			OutError = FString::Printf(TEXT("groups[%d].comment_text is required."), i);
			return false;
		}
	}

	return true;
}

TSharedPtr<FJsonObject> FLayoutAndCommentAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	// ================================================================
	// 0. Resolve graph
	// ================================================================
	FBlueprintEditor* BPEditor = nullptr;
	UEdGraph* Graph = ResolveGraph(Params, BPEditor);
	if (!Graph)
	{
		return CreateErrorResponse(TEXT("Could not find graph. Specify blueprint_name/graph_name or focus a Blueprint editor."));
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);

	// Parse params
	bool bAutoLayout = GetOptionalBool(Params, TEXT("auto_layout"), true);
	bool bClearExisting = GetOptionalBool(Params, TEXT("clear_existing"), false);
	float GroupSpacing = static_cast<float>(GetOptionalNumber(Params, TEXT("group_spacing"), 80.0));
	float CommentPadding = static_cast<float>(GetOptionalNumber(Params, TEXT("padding"), 40.0));
	float TitleHeight = static_cast<float>(GetOptionalNumber(Params, TEXT("title_height"), 36.0));

	const TArray<TSharedPtr<FJsonValue>>& GroupsArray = Params->GetArrayField(TEXT("groups"));

	// ================================================================
	// 1. Parse groups and collect all participating nodes
	// ================================================================
	struct FCommentGroup
	{
		FString CommentText;
		FLinearColor Color;
		bool bHasColor;
		TArray<UEdGraphNode*> Nodes;
		TArray<FString> MissingIds;
		// AABB (computed after layout)
		float MinX, MinY, MaxX, MaxY;
	};
	TArray<FCommentGroup> Groups;
	Groups.SetNum(GroupsArray.Num());

	TArray<UEdGraphNode*> AllParticipatingNodes;
	TSet<UEdGraphNode*> ParticipatingSet;

	FBlueprintLayoutSettings LayoutSettings;

	for (int32 i = 0; i < GroupsArray.Num(); ++i)
	{
		const TSharedPtr<FJsonObject>& GroupObj = GroupsArray[i]->AsObject();
		FCommentGroup& Group = Groups[i];

		Group.CommentText = GroupObj->GetStringField(TEXT("comment_text"));
		Group.bHasColor = false;
		Group.MinX = Group.MinY = TNumericLimits<float>::Max();
		Group.MaxX = Group.MaxY = TNumericLimits<float>::Lowest();

		// Parse color
		const TArray<TSharedPtr<FJsonValue>>* ColorArray = nullptr;
		if (GroupObj->TryGetArrayField(TEXT("color"), ColorArray) && ColorArray->Num() >= 3)
		{
			float R = static_cast<float>((*ColorArray)[0]->AsNumber());
			float G = static_cast<float>((*ColorArray)[1]->AsNumber());
			float B = static_cast<float>((*ColorArray)[2]->AsNumber());
			float A = ColorArray->Num() >= 4 ? static_cast<float>((*ColorArray)[3]->AsNumber()) : 1.0f;
			Group.Color = FLinearColor(R, G, B, A);
			Group.bHasColor = true;
		}

		// Resolve node_ids
		const TArray<TSharedPtr<FJsonValue>>& NodeIds = GroupObj->GetArrayField(TEXT("node_ids"));
		for (const TSharedPtr<FJsonValue>& IdVal : NodeIds)
		{
			FString GuidStr = IdVal->AsString();
			UEdGraphNode* Node = FindNodeByGuid(Graph, GuidStr);
			if (Node && !Cast<UEdGraphNode_Comment>(Node))
			{
				Group.Nodes.Add(Node);
				if (!ParticipatingSet.Contains(Node))
				{
					AllParticipatingNodes.Add(Node);
					ParticipatingSet.Add(Node);
				}
			}
			else
			{
				Group.MissingIds.Add(GuidStr);
			}
		}
	}

	if (AllParticipatingNodes.Num() == 0)
	{
		return CreateErrorResponse(TEXT("No valid nodes found across all groups."));
	}

	// Diagnostic: log group membership summary
	{
		int32 TotalGraphNodes = 0;
		for (UEdGraphNode* N : Graph->Nodes)
		{
			if (N && !Cast<UEdGraphNode_Comment>(N))
				TotalGraphNodes++;
		}
		int32 NonParticipating = TotalGraphNodes - AllParticipatingNodes.Num();
		UE_LOG(LogMCP, Log, TEXT("LayoutAndComment[1]: %d graph nodes, %d participating in %d group(s), %d non-participating"),
			TotalGraphNodes, AllParticipatingNodes.Num(), Groups.Num(), NonParticipating);
		for (int32 i = 0; i < Groups.Num(); ++i)
		{
			UE_LOG(LogMCP, Log, TEXT("  Group %d '%s': %d node(s), %d missing"),
				i, *Groups[i].CommentText, Groups[i].Nodes.Num(), Groups[i].MissingIds.Num());
			for (UEdGraphNode* N : Groups[i].Nodes)
			{
				UE_LOG(LogMCP, Log, TEXT("    -> '%s' (%s) at (%d, %d) guid=%s"),
					*N->GetNodeTitle(ENodeTitleType::ListView).ToString(),
					*N->GetClass()->GetName(),
					N->NodePosX, N->NodePosY,
					*N->NodeGuid.ToString());
			}
		}
		if (NonParticipating > 0)
		{
			for (UEdGraphNode* N : Graph->Nodes)
			{
				if (N && !Cast<UEdGraphNode_Comment>(N) && !ParticipatingSet.Contains(N))
				{
					UE_LOG(LogMCP, Log, TEXT("  Non-participating: '%s' (%s) at (%d, %d)"),
						*N->GetNodeTitle(ENodeTitleType::ListView).ToString(),
						*N->GetClass()->GetName(),
						N->NodePosX, N->NodePosY);
				}
			}
		}
	}

	// ================================================================
	// 2. Optionally clear existing comments
	// ================================================================
	int32 CommentsRemoved = 0;
	if (bClearExisting)
	{
		FScopedTransaction ClearTx(FText::FromString(TEXT("Clear Existing Comments")));
		TArray<UEdGraphNode*> CommentsToRemove;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Cast<UEdGraphNode_Comment>(Node))
			{
				CommentsToRemove.Add(Node);
			}
		}
		for (UEdGraphNode* Comment : CommentsToRemove)
		{
			Graph->RemoveNode(Comment);
			CommentsRemoved++;
		}
	}

	// ================================================================
	// 3. Optionally run auto-layout on ALL participating nodes
	//    Global layout preserves topological relationships across
	//    groups. Cross-group spatial overlap is resolved later in
	//    Steps 5 and 5.5 by translating entire groups as rigid
	//    bodies (preserving internal relative positions).
	// ================================================================
	int32 NodesMoved = 0;
	if (bAutoLayout)
	{
		FBlueprintLayoutSettings Settings = ParseLayoutSettings(Params);

		FScopedTransaction LayoutTx(FText::FromString(TEXT("Layout And Comment - Auto Layout")));

		// Collect non-participating nodes as surrounding obstacles
		TArray<UEdGraphNode*> SurroundingNodes;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && !ParticipatingSet.Contains(Node) && !Cast<UEdGraphNode_Comment>(Node))
			{
				SurroundingNodes.Add(Node);
			}
		}

		Settings.bAvoidSurrounding = (SurroundingNodes.Num() > 0);
		const TArray<UEdGraphNode*>* SurrPtr = SurroundingNodes.Num() > 0 ? &SurroundingNodes : nullptr;

		NodesMoved = FBlueprintAutoLayout::LayoutNodes(Graph, AllParticipatingNodes, Settings, SurrPtr);
		LayoutSettings = Settings;
	}

	// ================================================================
	// 4. Compute per-group AABB (after layout)
	// ================================================================
	for (int32 gi = 0; gi < Groups.Num(); ++gi)
	{
		FCommentGroup& Group = Groups[gi];
		for (UEdGraphNode* Node : Group.Nodes)
		{
			float NodeX = static_cast<float>(Node->NodePosX);
			float NodeY = static_cast<float>(Node->NodePosY);
			FVector2D NodeSize = FBlueprintAutoLayout::GetNodeSize(Node, LayoutSettings);

			Group.MinX = FMath::Min(Group.MinX, NodeX);
			Group.MinY = FMath::Min(Group.MinY, NodeY);
			Group.MaxX = FMath::Max(Group.MaxX, NodeX + NodeSize.X);
			Group.MaxY = FMath::Max(Group.MaxY, NodeY + NodeSize.Y);
		}
		UE_LOG(LogMCP, Log, TEXT("LayoutAndComment[4]: Group %d '%s' AABB=[%.0f, %.0f, %.0f, %.0f] (%d nodes)"),
			gi, *Group.CommentText, Group.MinX, Group.MinY, Group.MaxX, Group.MaxY, Group.Nodes.Num());
	}

	// ================================================================
	// 5. Detect and resolve inter-group AABB overlaps
	//    Strategy: sort groups by MinY, then push overlapping groups
	//    downward (moving all their nodes) to ensure GroupSpacing gap.
	//    Iterate up to 10 times for cascading overlaps.
	// ================================================================
	{
		// Build sort indices
		TArray<int32> SortOrder;
		SortOrder.SetNum(Groups.Num());
		for (int32 i = 0; i < Groups.Num(); ++i)
		{
			SortOrder[i] = i;
		}

		for (int32 Iter = 0; Iter < 10; ++Iter)
		{
			// Sort by MinY each iteration (positions may have changed)
			SortOrder.Sort([&Groups](int32 A, int32 B)
			{
				return Groups[A].MinY < Groups[B].MinY;
			});

			bool bAnyPush = false;

			for (int32 si = 0; si < SortOrder.Num(); ++si)
			{
				int32 IdxA = SortOrder[si];
				FCommentGroup& GroupA = Groups[IdxA];

				if (GroupA.Nodes.Num() == 0)
				{
					continue;
				}

				for (int32 sj = si + 1; sj < SortOrder.Num(); ++sj)
				{
					int32 IdxB = SortOrder[sj];
					FCommentGroup& GroupB = Groups[IdxB];

					if (GroupB.Nodes.Num() == 0)
					{
						continue;
					}

					// Include comment padding + title in AABB for overlap check
					float ATop = GroupA.MinY - CommentPadding - TitleHeight;
					float ABottom = GroupA.MaxY + CommentPadding;
					float ALeft = GroupA.MinX - CommentPadding;
					float ARight = GroupA.MaxX + CommentPadding;

					float BTop = GroupB.MinY - CommentPadding - TitleHeight;
					float BBottom = GroupB.MaxY + CommentPadding;
					float BLeft = GroupB.MinX - CommentPadding;
					float BRight = GroupB.MaxX + CommentPadding;

					// Check X overlap
					bool bOverlapX = (ALeft < BRight) && (BLeft < ARight);
					if (!bOverlapX)
					{
						continue;
					}

					// Check Y overlap (with GroupSpacing gap)
					float RequiredBTop = ABottom + GroupSpacing;
					if (BTop < RequiredBTop)
					{
						// Push GroupB down
						float PushY = RequiredBTop - BTop;

						UE_LOG(LogMCP, Log, TEXT("LayoutAndComment: Pushing group '%s' down by %.0f px to avoid overlap with '%s'"),
							*GroupB.CommentText, PushY, *GroupA.CommentText);

						for (UEdGraphNode* Node : GroupB.Nodes)
						{
							Node->Modify();
							Node->NodePosY += FMath::RoundToInt(PushY);
						}

						// Update GroupB AABB
						GroupB.MinY += PushY;
						GroupB.MaxY += PushY;
						bAnyPush = true;
					}
				}
			}

			if (!bAnyPush)
			{
				UE_LOG(LogMCP, Log, TEXT("LayoutAndComment: Group separation converged after %d iteration(s)"), Iter + 1);
				break;
			}
		}
	}

	// ================================================================
	// 5.5 Cross-group containment check (rigid-body translation)
	//     After AABB separation, verify that no node from group B
	//     falls inside group A's padded comment rect. If it does,
	//     translate the ENTIRE intruding group (preserving internal
	//     layout) in the direction requiring minimal displacement.
	//     Re-compute AABB after each push. Iterate ≤10 times.
	// ================================================================
	{
		auto RecomputeGroupAABB = [&LayoutSettings](FCommentGroup& G)
		{
			G.MinX = G.MinY = TNumericLimits<float>::Max();
			G.MaxX = G.MaxY = TNumericLimits<float>::Lowest();
			for (UEdGraphNode* Node : G.Nodes)
			{
				float NX = static_cast<float>(Node->NodePosX);
				float NY = static_cast<float>(Node->NodePosY);
				FVector2D NS = FBlueprintAutoLayout::GetNodeSize(Node, LayoutSettings);
				G.MinX = FMath::Min(G.MinX, NX);
				G.MinY = FMath::Min(G.MinY, NY);
				G.MaxX = FMath::Max(G.MaxX, NX + NS.X);
				G.MaxY = FMath::Max(G.MaxY, NY + NS.Y);
			}
		};

		for (int32 Iter = 0; Iter < 10; ++Iter)
		{
			bool bAnyPush = false;

			for (int32 ai = 0; ai < Groups.Num(); ++ai)
			{
				FCommentGroup& GroupA = Groups[ai];
				if (GroupA.Nodes.Num() == 0) continue;

				// GroupA's padded comment rect
				float ALeft   = GroupA.MinX - CommentPadding;
				float ARight  = GroupA.MaxX + CommentPadding;
				float ATop    = GroupA.MinY - CommentPadding - TitleHeight;
				float ABottom = GroupA.MaxY + CommentPadding;

				for (int32 bi = 0; bi < Groups.Num(); ++bi)
				{
					if (ai == bi) continue;

					FCommentGroup& GroupB = Groups[bi];
					if (GroupB.Nodes.Num() == 0) continue;

					// Check if ANY node of GroupB intrudes GroupA's comment rect
					bool bHasIntrusion = false;
					for (UEdGraphNode* Node : GroupB.Nodes)
					{
						float NX = static_cast<float>(Node->NodePosX);
						float NY = static_cast<float>(Node->NodePosY);
						FVector2D NSize = FBlueprintAutoLayout::GetNodeSize(Node, LayoutSettings);

						bool bOverlapX = (NX < ARight) && (NX + NSize.X > ALeft);
						bool bOverlapY = (NY < ABottom) && (NY + NSize.Y > ATop);

						if (bOverlapX && bOverlapY)
						{
							bHasIntrusion = true;
							break;
						}
					}

					if (!bHasIntrusion) continue;

					// Determine the cheapest translation direction
					// GroupB's padded AABB
					float BLeft   = GroupB.MinX - CommentPadding;
					float BRight  = GroupB.MaxX + CommentPadding;
					float BTop    = GroupB.MinY - CommentPadding - TitleHeight;
					float BBottom = GroupB.MaxY + CommentPadding;

					// Distance to push B out of A's rect in each direction
					float PushDown  = (ABottom + GroupSpacing) - BTop;                    // ↓
					float PushUp    = ATop - (BBottom + GroupSpacing);                     // ↑ (negative = upward)
					float PushRight = (ARight + GroupSpacing) - BLeft;                     // →
					float PushLeft  = ALeft - (BRight + GroupSpacing);                     // ← (negative = leftward)

					// Pick the direction with minimal absolute displacement
					float AbsDown  = FMath::Abs(PushDown);
					float AbsUp    = FMath::Abs(PushUp);
					float AbsRight = FMath::Abs(PushRight);
					float AbsLeft  = FMath::Abs(PushLeft);

					float MinAbs = FMath::Min(FMath::Min(AbsDown, AbsUp), FMath::Min(AbsRight, AbsLeft));

					float DeltaX = 0.f;
					float DeltaY = 0.f;
					FString Dir;

					if (MinAbs == AbsDown && PushDown > 0.f)
					{
						DeltaY = PushDown;
						Dir = TEXT("down");
					}
					else if (MinAbs == AbsUp && PushUp < 0.f)
					{
						DeltaY = PushUp; // negative = upward
						Dir = TEXT("up");
					}
					else if (MinAbs == AbsRight && PushRight > 0.f)
					{
						DeltaX = PushRight;
						Dir = TEXT("right");
					}
					else if (MinAbs == AbsLeft && PushLeft < 0.f)
					{
						DeltaX = PushLeft; // negative = leftward
						Dir = TEXT("left");
					}
					else
					{
						// Fallback: push down
						DeltaY = FMath::Max(PushDown, GroupSpacing);
						Dir = TEXT("down(fallback)");
					}

					UE_LOG(LogMCP, Log,
						TEXT("LayoutAndComment[5.5]: Group '%s' intrudes '%s' comment. Pushing '%s' %s by (%.0f, %.0f)"),
						*GroupB.CommentText, *GroupA.CommentText, *GroupB.CommentText, *Dir, DeltaX, DeltaY);

					for (UEdGraphNode* BNode : GroupB.Nodes)
					{
						BNode->Modify();
						BNode->NodePosX += FMath::RoundToInt(DeltaX);
						BNode->NodePosY += FMath::RoundToInt(DeltaY);
					}

					// Re-compute GroupB AABB after translation
					RecomputeGroupAABB(GroupB);
					bAnyPush = true;
					// Break inner group loop — re-check all pairs in next iteration
					break;
				}

				if (bAnyPush) break; // restart outer loop for next iteration
			}

			if (!bAnyPush)
			{
				UE_LOG(LogMCP, Log, TEXT("LayoutAndComment[5.5]: Cross-containment resolved after %d iteration(s)"), Iter + 1);
				break;
			}
		}
	}

	// ================================================================
	// 5.6 Re-compute all group AABBs after Step 5.5 translations
	// ================================================================
	for (FCommentGroup& Group : Groups)
	{
		Group.MinX = Group.MinY = TNumericLimits<float>::Max();
		Group.MaxX = Group.MaxY = TNumericLimits<float>::Lowest();
		for (UEdGraphNode* Node : Group.Nodes)
		{
			float NX = static_cast<float>(Node->NodePosX);
			float NY = static_cast<float>(Node->NodePosY);
			FVector2D NS = FBlueprintAutoLayout::GetNodeSize(Node, LayoutSettings);
			Group.MinX = FMath::Min(Group.MinX, NX);
			Group.MinY = FMath::Min(Group.MinY, NY);
			Group.MaxX = FMath::Max(Group.MaxX, NX + NS.X);
			Group.MaxY = FMath::Max(Group.MaxY, NY + NS.Y);
		}
	}

	// ================================================================
	// 5.7 Final safety: push non-group nodes out of comment rects
	//     Covers two blind spots not handled by Step 5.5:
	//     (a) Non-participating nodes (not in any group's node_ids)
	//     (b) Title-width expansion in Step 6 may enlarge the comment
	//         rect beyond the AABB used by Step 5.5
	//     For nodes belonging to another group: rigid-body push.
	//     For non-participating nodes: push individual node.
	// ================================================================
	{
		// Build node-to-group mapping for O(1) lookup
		TMap<UEdGraphNode*, int32> NodeGroupMap;
		for (int32 gi = 0; gi < Groups.Num(); ++gi)
		{
			for (UEdGraphNode* N : Groups[gi].Nodes)
			{
				NodeGroupMap.Add(N, gi);
			}
		}

		// Helper: compute final comment rect including title-width expansion
		// (mirrors Step 6 logic exactly)
		auto ComputeFinalCommentRect = [&](const FCommentGroup& G,
			float& OutLeft, float& OutTop, float& OutRight, float& OutBottom)
		{
			OutLeft   = G.MinX - CommentPadding;
			OutTop    = G.MinY - CommentPadding - TitleHeight;
			OutRight  = G.MaxX + CommentPadding;
			OutBottom = G.MaxY + CommentPadding;

			// Title-width expansion
			const float CCharW = 10.f;
			float TitleW = 0.f;
			for (TCHAR Ch : G.CommentText)
			{
				if ((Ch >= 0x4E00 && Ch <= 0x9FFF)
					|| (Ch >= 0x3400 && Ch <= 0x4DBF)
					|| (Ch >= 0xFF00 && Ch <= 0xFFEF)
					|| (Ch >= 0xAC00 && Ch <= 0xD7AF)
					|| (Ch >= 0x3040 && Ch <= 0x30FF))
				{
					TitleW += CCharW * 1.8f;
				}
				else
				{
					TitleW += CCharW;
				}
			}
			const float TitleMargin = 40.f;
			float MinW = TitleW + TitleMargin;
			float CurW = OutRight - OutLeft;
			if (MinW > CurW)
			{
				float Exp = (MinW - CurW) * 0.5f;
				OutLeft  -= Exp;
				OutRight += Exp;
			}
		};

		// Helper: recompute a group's AABB
		auto RecomputeAABB57 = [&LayoutSettings](FCommentGroup& G)
		{
			G.MinX = G.MinY = TNumericLimits<float>::Max();
			G.MaxX = G.MaxY = TNumericLimits<float>::Lowest();
			for (UEdGraphNode* Node : G.Nodes)
			{
				float NX = static_cast<float>(Node->NodePosX);
				float NY = static_cast<float>(Node->NodePosY);
				FVector2D NS = FBlueprintAutoLayout::GetNodeSize(Node, LayoutSettings);
				G.MinX = FMath::Min(G.MinX, NX);
				G.MinY = FMath::Min(G.MinY, NY);
				G.MaxX = FMath::Max(G.MaxX, NX + NS.X);
				G.MaxY = FMath::Max(G.MaxY, NY + NS.Y);
			}
		};

		// Collect all non-comment graph nodes
		TArray<UEdGraphNode*> AllGraphNodes;
		for (UEdGraphNode* N : Graph->Nodes)
		{
			if (N && !Cast<UEdGraphNode_Comment>(N))
			{
				AllGraphNodes.Add(N);
			}
		}

		for (int32 Iter = 0; Iter < 10; ++Iter)
		{
			bool bAnyPush = false;

			for (int32 gi = 0; gi < Groups.Num() && !bAnyPush; ++gi)
			{
				FCommentGroup& G = Groups[gi];
				if (G.Nodes.Num() == 0) continue;

				float CLeft, CTop, CRight, CBottom;
				ComputeFinalCommentRect(G, CLeft, CTop, CRight, CBottom);

				for (UEdGraphNode* Node : AllGraphNodes)
				{
					// Skip nodes belonging to THIS group
					int32* OwnerIdx = NodeGroupMap.Find(Node);
					if (OwnerIdx && *OwnerIdx == gi) continue;

					float NX = static_cast<float>(Node->NodePosX);
					float NY = static_cast<float>(Node->NodePosY);
					FVector2D NSize = FBlueprintAutoLayout::GetNodeSize(Node, LayoutSettings);

					bool bOverlapX = (NX < CRight) && (NX + NSize.X > CLeft);
					bool bOverlapY = (NY < CBottom) && (NY + NSize.Y > CTop);

					if (!bOverlapX || !bOverlapY) continue;

					// Node intrudes this group's final comment rect.
					// Compute minimal displacement to push node out.
					float PushRight = CRight  + GroupSpacing - NX;
					float PushLeft  = CLeft   - GroupSpacing - NSize.X - NX;
					float PushDown  = CBottom + GroupSpacing - NY;
					float PushUp    = CTop    - GroupSpacing - NSize.Y - NY;

					float AbsR = FMath::Abs(PushRight);
					float AbsL = FMath::Abs(PushLeft);
					float AbsD = FMath::Abs(PushDown);
					float AbsU = FMath::Abs(PushUp);
					float MinAbs = FMath::Min(FMath::Min(AbsR, AbsL), FMath::Min(AbsD, AbsU));

					float DX = 0.f, DY = 0.f;
					FString Dir;

					if      (MinAbs == AbsR) { DX = PushRight; Dir = TEXT("right"); }
					else if (MinAbs == AbsL) { DX = PushLeft;  Dir = TEXT("left");  }
					else if (MinAbs == AbsD) { DY = PushDown;  Dir = TEXT("down");  }
					else                     { DY = PushUp;    Dir = TEXT("up");    }

					if (OwnerIdx)
					{
						// Node belongs to another group — push entire group (rigid body)
						FCommentGroup& OwnerGroup = Groups[*OwnerIdx];
						UE_LOG(LogMCP, Log,
							TEXT("LayoutAndComment[5.7]: Node '%s' (group '%s') intrudes comment '%s'. "
							     "Pushing group '%s' %s by (%.0f, %.0f)"),
							*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
							*OwnerGroup.CommentText, *G.CommentText,
							*OwnerGroup.CommentText, *Dir, DX, DY);

						for (UEdGraphNode* GN : OwnerGroup.Nodes)
						{
							GN->Modify();
							GN->NodePosX += FMath::RoundToInt(DX);
							GN->NodePosY += FMath::RoundToInt(DY);
						}
						RecomputeAABB57(OwnerGroup);
					}
					else
					{
						// Non-participating node — push just this node
						UE_LOG(LogMCP, Log,
							TEXT("LayoutAndComment[5.7]: Non-group node '%s' (%s) intrudes comment '%s'. "
							     "Pushing node %s by (%.0f, %.0f)"),
							*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
							*Node->GetClass()->GetName(),
							*G.CommentText, *Dir, DX, DY);

						Node->Modify();
						Node->NodePosX += FMath::RoundToInt(DX);
						Node->NodePosY += FMath::RoundToInt(DY);
					}

					bAnyPush = true;
					break; // Restart iteration after any push
				}
			}

			if (!bAnyPush)
			{
				UE_LOG(LogMCP, Log, TEXT("LayoutAndComment[5.7]: No intrusions found (iter %d)"), Iter + 1);
				break;
			}
		}

		// Final AABB recompute after Step 5.7
		for (FCommentGroup& Group : Groups)
		{
			RecomputeAABB57(Group);
		}
	}

	// ================================================================
	// 6. Create comment boxes for each group
	// ================================================================
	FScopedTransaction CommentTx(FText::FromString(TEXT("Layout And Comment - Add Comments")));

	TArray<TSharedPtr<FJsonValue>> GroupResults;

	for (int32 i = 0; i < Groups.Num(); ++i)
	{
		FCommentGroup& Group = Groups[i];

		if (Group.Nodes.Num() == 0)
		{
			TSharedPtr<FJsonObject> GR = MakeShared<FJsonObject>();
			GR->SetNumberField(TEXT("group_index"), i);
			GR->SetStringField(TEXT("comment_text"), Group.CommentText);
			GR->SetBoolField(TEXT("success"), false);
			GR->SetStringField(TEXT("error"), TEXT("No valid nodes in group."));
			GroupResults.Add(MakeShared<FJsonValueObject>(GR));
			continue;
		}

		// Comment position and size from AABB
		float CommentX = Group.MinX - CommentPadding;
		float CommentY = Group.MinY - CommentPadding - TitleHeight;
		float CommentRight = Group.MaxX + CommentPadding;
		float CommentBottom = Group.MaxY + CommentPadding;

		// Ensure minimum width for comment title text
		{
			const float CommentCharW = 10.f;
			float TitleTextWidth = 0.f;
			for (TCHAR Ch : Group.CommentText)
			{
				if ((Ch >= 0x4E00 && Ch <= 0x9FFF)
					|| (Ch >= 0x3400 && Ch <= 0x4DBF)
					|| (Ch >= 0xFF00 && Ch <= 0xFFEF)
					|| (Ch >= 0xAC00 && Ch <= 0xD7AF)
					|| (Ch >= 0x3040 && Ch <= 0x30FF))
				{
					TitleTextWidth += CommentCharW * 1.8f;
				}
				else
				{
					TitleTextWidth += CommentCharW;
				}
			}
			const float TitleMargin = 40.f;
			float MinCommentWidth = TitleTextWidth + TitleMargin;
			float CurrentWidth = CommentRight - CommentX;
			if (MinCommentWidth > CurrentWidth)
			{
				float Expand = (MinCommentWidth - CurrentWidth) * 0.5f;
				CommentX -= Expand;
				CommentRight += Expand;
			}
		}

		int32 CommentWidth = FMath::RoundToInt(CommentRight - CommentX);
		int32 CommentHeight = FMath::RoundToInt(CommentBottom - CommentY);

		// Create comment node
		UEdGraphNode_Comment* CommentNode = NewObject<UEdGraphNode_Comment>(Graph);
		CommentNode->CreateNewGuid();
		CommentNode->NodeComment = Group.CommentText;
		CommentNode->NodePosX = FMath::RoundToInt(CommentX);
		CommentNode->NodePosY = FMath::RoundToInt(CommentY);
		CommentNode->NodeWidth = CommentWidth;
		CommentNode->NodeHeight = CommentHeight;

		if (Group.bHasColor)
		{
			CommentNode->CommentColor = Group.Color;
		}

		Graph->AddNode(CommentNode, true, false);
		CommentNode->SetFlags(RF_Transactional);

		// Diagnostic: log comment rect and check which nodes fall inside
		UE_LOG(LogMCP, Log, TEXT("LayoutAndComment[6]: Comment '%s' Rect=[%.0f, %.0f, %.0f, %.0f] Size=(%d, %d)"),
			*Group.CommentText, CommentX, CommentY, CommentRight, CommentBottom,
			CommentWidth, CommentHeight);
		for (UEdGraphNode* GNode : Graph->Nodes)
		{
			if (!GNode || Cast<UEdGraphNode_Comment>(GNode)) continue;
			float NX = static_cast<float>(GNode->NodePosX);
			float NY = static_cast<float>(GNode->NodePosY);
			// Check if node top-left falls inside comment rect
			if (NX >= CommentX && NX <= CommentRight && NY >= CommentY && NY <= CommentBottom)
			{
				bool bIsGroupMember = false;
				for (UEdGraphNode* GN : Group.Nodes)
				{
					if (GN == GNode) { bIsGroupMember = true; break; }
				}
				if (!bIsGroupMember)
				{
					UE_LOG(LogMCP, Warning,
						TEXT("LayoutAndComment[6]: *** CAPTURED NON-MEMBER *** '%s' at (%d, %d) falls inside comment '%s'"),
						*GNode->GetNodeTitle(ENodeTitleType::ListView).ToString(),
						GNode->NodePosX, GNode->NodePosY,
						*Group.CommentText);
				}
			}
		}

		// Build group result
		TSharedPtr<FJsonObject> GR = MakeShared<FJsonObject>();
		GR->SetNumberField(TEXT("group_index"), i);
		GR->SetStringField(TEXT("comment_text"), Group.CommentText);
		GR->SetStringField(TEXT("node_id"), CommentNode->NodeGuid.ToString());
		GR->SetNumberField(TEXT("nodes_wrapped"), Group.Nodes.Num());
		GR->SetBoolField(TEXT("success"), true);

		TArray<TSharedPtr<FJsonValue>> PosArray;
		PosArray.Add(MakeShared<FJsonValueNumber>(CommentX));
		PosArray.Add(MakeShared<FJsonValueNumber>(CommentY));
		GR->SetArrayField(TEXT("position"), PosArray);

		TArray<TSharedPtr<FJsonValue>> SizeArray;
		SizeArray.Add(MakeShared<FJsonValueNumber>(CommentWidth));
		SizeArray.Add(MakeShared<FJsonValueNumber>(CommentHeight));
		GR->SetArrayField(TEXT("size"), SizeArray);

		if (Group.MissingIds.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> MissingArr;
			for (const FString& Id : Group.MissingIds)
			{
				MissingArr.Add(MakeShared<FJsonValueString>(Id));
			}
			GR->SetArrayField(TEXT("missing_node_ids"), MissingArr);
		}

		GroupResults.Add(MakeShared<FJsonValueObject>(GR));
	}

	// Mark modified
	if (Blueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("graph_name"), Graph->GetName());
	Result->SetNumberField(TEXT("groups_created"), GroupResults.Num());
	Result->SetNumberField(TEXT("nodes_moved"), NodesMoved);
	Result->SetNumberField(TEXT("total_participating_nodes"), AllParticipatingNodes.Num());
	Result->SetNumberField(TEXT("comments_removed"), CommentsRemoved);
	Result->SetBoolField(TEXT("auto_layout_applied"), bAutoLayout);
	Result->SetArrayField(TEXT("groups"), GroupResults);

	ShowNotification(FString::Printf(TEXT("Layout & Comment: %d groups, %d nodes moved"),
		Groups.Num(), NodesMoved));

	return CreateSuccessResponse(Result);
}
