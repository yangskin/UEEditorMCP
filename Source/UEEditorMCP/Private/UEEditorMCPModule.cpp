// Copyright (c) 2025 zolnoor. All rights reserved.

#include "UEEditorMCPModule.h"
#include "BlueprintAutoLayoutCommands.h"
#include "Actions/LayoutActions.h"
#include "Modules/ModuleManager.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "ScopedTransaction.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "MCPBridge.h"
#include "MCPLogCapture.h"
#include "MCPCommonUtils.h"
#include "Widgets/Docking/SDockTab.h"
// P5.3: Material editor Auto Layout menu
#include "MaterialEditorModule.h"
#include "MaterialLayoutUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionReroute.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "MaterialGraph/MaterialGraphNode_Root.h"
#include "GraphEditor.h"

#define LOCTEXT_NAMESPACE "FUEEditorMCPModule"


// GetActiveBlueprintEditorForCommand removed — use FMCPCommonUtils::GetActiveBlueprintEditor() instead.


// -------------- Command Execution Helpers -------------------

static void ExecuteAutoLayoutSmart()
{
	FBlueprintEditor* BPEditor = FMCPCommonUtils::GetActiveBlueprintEditor();
	if (!BPEditor)
	{
		UE_LOG(LogMCP, Warning, TEXT("AutoLayout: No focused Blueprint editor."));
		return;
	}

	UEdGraph* Graph = BPEditor->GetFocusedGraph();
	if (!Graph)
	{
		return;
	}

	FGraphPanelSelectionSet SelectedNodes = BPEditor->GetSelectedNodes();
	TArray<UEdGraphNode*> NodesToLayout;
	for (UObject* Obj : SelectedNodes)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(Obj);
		if (Node && Graph->Nodes.Contains(Node))
		{
			NodesToLayout.Add(Node);
		}
	}

	FBlueprintLayoutSettings Settings;

	if (NodesToLayout.Num() > 0)
	{
		// Auto-expand: include pure dependencies of selected nodes
		TSet<UEdGraphNode*> LayoutSet;
		for (UEdGraphNode* N : NodesToLayout) { if (N) LayoutSet.Add(N); }

		{
			TSet<UEdGraphNode*> GraphNodeSet;
			for (UEdGraphNode* GN : Graph->Nodes) { if (GN) GraphNodeSet.Add(GN); }
			TSet<UEdGraphNode*> PureVisited;
			TArray<UEdGraphNode*> ExtraPures;
			for (UEdGraphNode* N : TArray<UEdGraphNode*>(NodesToLayout))
			{
				if (!N) continue;
				FBlueprintAutoLayout::CollectPureDependencies(N, GraphNodeSet, ExtraPures, PureVisited, 5);
			}
			for (UEdGraphNode* Pure : ExtraPures)
			{
				if (Pure && !LayoutSet.Contains(Pure))
				{
					NodesToLayout.Add(Pure);
					LayoutSet.Add(Pure);
				}
			}
		}

		// Collect surrounding nodes for avoidance (all non-selected graph nodes)
		TArray<UEdGraphNode*> SurroundingNodes;
		for (UEdGraphNode* N : Graph->Nodes)
		{
			if (N && !LayoutSet.Contains(N))
			{
				SurroundingNodes.Add(N);
			}
		}

		Settings.bAvoidSurrounding = true;
		const TArray<UEdGraphNode*>* SurrPtr = SurroundingNodes.Num() > 0 ? &SurroundingNodes : nullptr;

		FScopedTransaction Transaction(LOCTEXT("AutoLayoutSmartSelected", "Auto Layout Selected"));
		int32 Moved = FBlueprintAutoLayout::LayoutNodes(Graph, NodesToLayout, Settings, SurrPtr);
		UE_LOG(LogMCP, Log, TEXT("AutoLayout: selected mode, %d nodes (incl. pure deps) moved"), Moved);
		return;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			NodesToLayout.Add(Node);
		}
	}

	if (NodesToLayout.Num() == 0)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("AutoLayoutSmartGraph", "Auto Layout Graph"));
	int32 Moved = FBlueprintAutoLayout::LayoutNodes(Graph, NodesToLayout, Settings);
	UE_LOG(LogMCP, Log, TEXT("AutoLayout: graph mode, %d/%d nodes moved"), Moved, NodesToLayout.Num());
}

static void ExecuteAutoLayoutSelected()
{
	FBlueprintEditor* BPEditor = FMCPCommonUtils::GetActiveBlueprintEditor();
	if (!BPEditor)
	{
		UE_LOG(LogMCP, Warning, TEXT("AutoLayout: No focused Blueprint editor."));
		return;
	}

	UEdGraph* Graph = BPEditor->GetFocusedGraph();
	if (!Graph) return;

	FGraphPanelSelectionSet SelectedNodes = BPEditor->GetSelectedNodes();
	TArray<UEdGraphNode*> NodesToLayout;
	for (UObject* Obj : SelectedNodes)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(Obj);
		if (Node && Graph->Nodes.Contains(Node))
		{
			NodesToLayout.Add(Node);
		}
	}

	if (NodesToLayout.Num() == 0)
	{
		UE_LOG(LogMCP, Warning, TEXT("AutoLayout Selected: No nodes selected."));
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("AutoLayoutSelected", "Auto Layout Selected"));
	int32 Moved = FBlueprintAutoLayout::LayoutNodes(Graph, NodesToLayout);
	UE_LOG(LogMCP, Log, TEXT("AutoLayout Selected: %d nodes moved"), Moved);
}


static void ExecuteAutoLayoutSubtree()
{
	FBlueprintEditor* BPEditor = FMCPCommonUtils::GetActiveBlueprintEditor();
	if (!BPEditor)
	{
		UE_LOG(LogMCP, Warning, TEXT("AutoLayout: No focused Blueprint editor."));
		return;
	}

	UEdGraph* Graph = BPEditor->GetFocusedGraph();
	if (!Graph) return;

	UEdGraphNode* Root = BPEditor->GetSingleSelectedNode();
	if (!Root)
	{
		UE_LOG(LogMCP, Warning, TEXT("AutoLayout Subtree: Select a single root node."));
		return;
	}

	TArray<UEdGraphNode*> Subtree = FBlueprintAutoLayout::CollectExecSubtree(Root, 3);
	if (Subtree.Num() == 0) return;

	FScopedTransaction Transaction(LOCTEXT("AutoLayoutSubtree", "Auto Layout Subtree"));
	int32 Moved = FBlueprintAutoLayout::LayoutNodes(Graph, Subtree);
	UE_LOG(LogMCP, Log, TEXT("AutoLayout Subtree: %d/%d nodes moved"), Moved, Subtree.Num());
}


static void ExecuteAutoLayoutGraph()
{
	FBlueprintEditor* BPEditor = FMCPCommonUtils::GetActiveBlueprintEditor();
	if (!BPEditor)
	{
		UE_LOG(LogMCP, Warning, TEXT("AutoLayout: No focused Blueprint editor."));
		return;
	}

	UEdGraph* Graph = BPEditor->GetFocusedGraph();
	if (!Graph) return;

	TArray<UEdGraphNode*> AllNodes;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node) AllNodes.Add(Node);
	}

	if (AllNodes.Num() == 0) return;

	FScopedTransaction Transaction(LOCTEXT("AutoLayoutGraph", "Auto Layout Graph"));
	int32 Moved = FBlueprintAutoLayout::LayoutNodes(Graph, AllNodes);
	UE_LOG(LogMCP, Log, TEXT("AutoLayout Graph: %d/%d nodes moved"), Moved, AllNodes.Num());
}


// -------------- P5.3: Material Editor Auto Layout -------------------

/**
 * Get the actual rendered size of a material graph node via SGraphEditor.
 * Falls back to a pin-count + state-aware heuristic if not available.
 */
static FVector2D GetMaterialNodeSize(
	const UMaterialExpression* Expr,
	const TSharedPtr<SGraphEditor>& GraphEditor)
{
	if (!Expr)
	{
		return FVector2D(280.0, 100.0);
	}

	// ---- Step 1: For Custom nodes with ShowCode, always compute code-based minimum ----
	// SGraphNodeMaterialCustom renders code text and preview side by side (SHorizontalBox)
	// below the pin area (CreateBelowPinControls). GetBoundsForNode() may return the
	// compact/collapsed widget size, so we need code-based floor as safety net.
	const UMaterialExpressionCustom* CustomExpr = Cast<UMaterialExpressionCustom>(Expr);
	FVector2D CodeMinSize(0.0, 0.0);
	bool bHasCodeMin = false;

	if (CustomExpr && CustomExpr->ShowCode && !CustomExpr->Code.IsEmpty())
	{
		bHasCodeMin = true;

		// Count code lines and find the longest line
		int32 LineCount = 1;
		int32 MaxLineLen = 0;
		int32 CurLineLen = 0;
		for (TCHAR Ch : CustomExpr->Code)
		{
			if (Ch == TEXT('\n'))
			{
				++LineCount;
				MaxLineLen = FMath::Max(MaxLineLen, CurLineLen);
				CurLineLen = 0;
			}
			else if (Ch != TEXT('\r'))
			{
				++CurLineLen;
			}
		}
		MaxLineLen = FMath::Max(MaxLineLen, CurLineLen);

		// Pin counts
		int32 InputCount = 0;
		for (int32 i = 0; ; ++i)
		{
			if (!const_cast<UMaterialExpression*>(Expr)->GetInput(i)) break;
			++InputCount;
		}
		int32 OutputCount = 1 + CustomExpr->AdditionalOutputs.Num();
		int32 MaxPins = FMath::Max(InputCount, OutputCount);

		// Layout geometry (based on SGraphNodeMaterialCustom widget):
		//   Title bar:         ~32px
		//   Pin rows:          ~26px each
		//   Below-pins area:   [CodeTextBox | PreviewBox]  (horizontal)
		//   Code line height:  ~16px (SMultiLineEditableTextBox with SyntaxHighlight style)
		//   Code padding:      FMargin(10, 5, 10, 10) + NonPinNodeBodyPadding
		//   Preview box:       ~106px wide, ~86px tall (material thumbnail)
		const double TitleH = 32.0;
		const double PinRowH = 26.0;
		const double CodeLineH = 16.0;
		const double CodeTopPad = 20.0;   // gap between pins and code area
		const double CodeBottomPad = 16.0;
		const double PreviewW = 110.0;    // material preview thumbnail width
		const double PreviewH = 86.0;     // material preview thumbnail height
		const double CharW = 7.2;         // monospace char width at default zoom
		const double CodeMarginLR = 30.0; // left(10) + right(10) + extra(10)

		double PinsH = MaxPins * PinRowH;
		double CodeH = FMath::Max(LineCount, 3) * CodeLineH + CodeTopPad + CodeBottomPad;
		double BelowPinsH = FMath::Max(CodeH, PreviewH);
		double Height = TitleH + PinsH + BelowPinsH;

		// Width: code text + margins + preview area side by side
		double CodeTextW = MaxLineLen * CharW + CodeMarginLR;
		double Width = FMath::Max(CodeTextW + PreviewW, 420.0);

		CodeMinSize = FVector2D(Width, Height);

		UE_LOG(LogMCP, Log,
			TEXT("NodeSize[Custom] '%s': lines=%d maxLineLen=%d pins=%d → CodeMin=%.0fx%.0f"),
			*Expr->GetName(), LineCount, MaxLineLen, MaxPins,
			CodeMinSize.X, CodeMinSize.Y);
	}

	// ---- Step 2: Attempt Slate actual bounds ----
	if (GraphEditor.IsValid() && Expr->GraphNode)
	{
		FSlateRect Rect;
		if (GraphEditor->GetBoundsForNode(Expr->GraphNode, Rect, 0.f))
		{
			double W = Rect.GetSize().X;
			double H = Rect.GetSize().Y;
			if (W > 10.0 && H > 10.0)
			{
				if (bHasCodeMin)
				{
					// For Custom nodes showing code: take MAX of Slate and code-based estimate
					FVector2D Result(FMath::Max(W, CodeMinSize.X), FMath::Max(H, CodeMinSize.Y));
					UE_LOG(LogMCP, Log,
						TEXT("NodeSize[Custom] '%s': Slate=%.0fx%.0f → Final=%.0fx%.0f"),
						*Expr->GetName(), W, H, Result.X, Result.Y);
					return Result;
				}
				return FVector2D(W, H);
			}
		}
	}

	// ---- Step 3: Pure heuristic fallback ----
	if (bHasCodeMin)
	{
		// 15% safety factor when Slate bounds unavailable
		FVector2D Result = CodeMinSize * 1.15;
		UE_LOG(LogMCP, Log,
			TEXT("NodeSize[Custom/Heuristic] '%s': CodeMin=%.0fx%.0f → Final=%.0fx%.0f"),
			*Expr->GetName(), CodeMinSize.X, CodeMinSize.Y, Result.X, Result.Y);
		return Result;
	}

	// ---- Regular (non-Custom) node heuristic ----
	int32 InputCount = 0;
	for (int32 i = 0; ; ++i)
	{
		if (!const_cast<UMaterialExpression*>(Expr)->GetInput(i)) break;
		++InputCount;
	}
	TArray<FExpressionOutput>& Outputs = const_cast<UMaterialExpression*>(Expr)->GetOutputs();
	int32 OutputCount = Outputs.Num();
	int32 MaxPins = FMath::Max(InputCount, OutputCount);

	// Note: bCollapsed in material nodes hides preview/description, NOT pins.
	// All input/output pins are always rendered, so we do NOT reduce MaxPins.

	const double TitleH = 28.0;
	const double PinRowH = 24.0;
	const double BottomPad = 8.0;
	double FloorH = TitleH + FMath::Max(MaxPins, 1) * PinRowH + BottomPad;
	double Width = 280.0;

	// Custom node without ShowCode is still wider than standard
	if (CustomExpr)
	{
		Width = 360.0;
		int32 ExtraPins = CustomExpr->AdditionalOutputs.Num();
		FloorH = FMath::Max(FloorH, TitleH + FMath::Max(InputCount + 1, OutputCount + ExtraPins) * PinRowH + BottomPad);
	}

	return FVector2D(Width * 1.12, FloorH * 1.15);
}

static void ExecuteMaterialAutoLayout()
{
	UAssetEditorSubsystem* AssetEditorSS = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	if (!AssetEditorSS)
	{
		UE_LOG(LogMCP, Warning, TEXT("MaterialAutoLayout: No AssetEditorSubsystem available."));
		return;
	}

	TArray<UObject*> EditedAssets = AssetEditorSS->GetAllEditedAssets();
	UMaterial* PreviewMaterial = nullptr;
	UMaterial* OriginalMaterial = nullptr;
	for (UObject* Asset : EditedAssets)
	{
		UMaterial* Mat = Cast<UMaterial>(Asset);
		if (!Mat) continue;
		if (Mat->GetOutermost() == GetTransientPackage())
			PreviewMaterial = Mat;
		else
			OriginalMaterial = Mat;
	}

	UMaterial* TargetMaterial = PreviewMaterial ? PreviewMaterial : OriginalMaterial;
	if (!TargetMaterial)
	{
		UE_LOG(LogMCP, Warning, TEXT("MaterialAutoLayout: No material editor is currently open."));
		return;
	}

	UMaterialGraph* MatGraph = TargetMaterial->MaterialGraph;
	if (!MatGraph)
	{
		UE_LOG(LogMCP, Warning, TEXT("MaterialAutoLayout: Material '%s' has no graph."),
			*TargetMaterial->GetName());
		return;
	}

	TSharedPtr<SGraphEditor> GraphEditor = SGraphEditor::FindGraphEditorForGraph(MatGraph);

	// ---- Phase 0: Collect target nodes ----
	TSet<UMaterialExpression*> TargetExpressionSet;
	bool bSelectedOnly = false;
	bool bRootNodeSelected = false;

	if (GraphEditor.IsValid())
	{
		const FGraphPanelSelectionSet& SelectedNodes = GraphEditor->GetSelectedNodes();
		for (UObject* SelObj : SelectedNodes)
		{
			UMaterialGraphNode* MatGraphNode = Cast<UMaterialGraphNode>(SelObj);
			if (MatGraphNode && MatGraphNode->MaterialExpression
				&& !MatGraphNode->MaterialExpression->IsA<UMaterialExpressionComment>())
			{
				TargetExpressionSet.Add(MatGraphNode->MaterialExpression);
			}
			if (Cast<UMaterialGraphNode_Root>(SelObj))
			{
				bRootNodeSelected = true;
			}
		}
		if (TargetExpressionSet.Num() > 0 || bRootNodeSelected)
		{
			bSelectedOnly = true;
		}
	}

	const TArray<TObjectPtr<UMaterialExpression>>& AllExpressions =
		TargetMaterial->GetExpressionCollection().Expressions;
	if (!bSelectedOnly)
	{
		for (UMaterialExpression* Expr : AllExpressions)
		{
			if (Expr && !Expr->IsA<UMaterialExpressionComment>())
			{
				TargetExpressionSet.Add(Expr);
			}
		}
	}

	UMaterialGraphNode_Root* RootNode = MatGraph->RootNode;
	bool bHasRootNode = (RootNode != nullptr && (!bSelectedOnly || bRootNodeSelected));

	if (TargetExpressionSet.Num() == 0 && !bHasRootNode)
	{
		UE_LOG(LogMCP, Log, TEXT("MaterialAutoLayout: nothing to layout."));
		return;
	}

	// Root-only: reset to origin
	if (TargetExpressionSet.Num() == 0 && bHasRootNode)
	{
		if (TargetMaterial->EditorX != 0 || TargetMaterial->EditorY != 0)
		{
			TargetMaterial->EditorX = 0;
			TargetMaterial->EditorY = 0;
		}
		MatGraph->RebuildGraph();
		TargetMaterial->MarkPackageDirty();
		if (OriginalMaterial && OriginalMaterial != TargetMaterial)
		{
			OriginalMaterial->EditorX = 0;
			OriginalMaterial->EditorY = 0;
		}
		UE_LOG(LogMCP, Log, TEXT("MaterialAutoLayout: root reset to origin in '%s'"),
			*TargetMaterial->GetName());
		return;
	}

	UE_LOG(LogMCP, Log, TEXT("MaterialAutoLayout: target='%s', expressions=%d, selected=%s, root=%s"),
		*TargetMaterial->GetName(), TargetExpressionSet.Num(),
		bSelectedOnly ? TEXT("yes") : TEXT("no"),
		bHasRootNode ? TEXT("yes") : TEXT("no"));

	// ---- Phase 1: Get actual node sizes ----
	TMap<UMaterialExpression*, FVector2D> NodeSizes;
	for (UMaterialExpression* Expr : TargetExpressionSet)
	{
		NodeSizes.Add(Expr, GetMaterialNodeSize(Expr, GraphEditor));
	}

	FVector2D RootNodeSize(400.0, 600.0);
	if (bHasRootNode && GraphEditor.IsValid())
	{
		FSlateRect RootRect;
		if (GraphEditor->GetBoundsForNode(RootNode, RootRect, 0.f))
		{
			double RW = RootRect.GetSize().X;
			double RH = RootRect.GetSize().Y;
			if (RW > 10.0 && RH > 10.0)
			{
				RootNodeSize = FVector2D(RW, RH);
			}
		}
	}

	// ---- Phase 2: Build dependency graph ----
	// Deps: Expr -> its INPUT dependencies (upstream nodes).
	// Consumers: Expr -> nodes that USE it as input (downstream nodes).
	TMap<UMaterialExpression*, TArray<UMaterialExpression*>> Deps;     // upstream
	TMap<UMaterialExpression*, TArray<UMaterialExpression*>> Consumers; // downstream

	for (UMaterialExpression* Expr : TargetExpressionSet)
	{
		Deps.FindOrAdd(Expr);
		Consumers.FindOrAdd(Expr);
	}

	for (UMaterialExpression* Expr : TargetExpressionSet)
	{
		for (int32 InputIdx = 0; ; ++InputIdx)
		{
			FExpressionInput* Input = Expr->GetInput(InputIdx);
			if (!Input) break;
			if (Input->Expression && TargetExpressionSet.Contains(Input->Expression))
			{
				Deps[Expr].AddUnique(Input->Expression);
				Consumers[Input->Expression].AddUnique(Expr);
			}
		}
	}

	// Build pin-index map, root-connected set, and root pin order using shared utility
	TMap<UMaterialExpression*, TMap<UMaterialExpression*, int32>> PinIndexMap;
	MaterialLayoutUtils::BuildPinIndexMap(Deps, PinIndexMap);

	TSet<UMaterialExpression*> RootConnectedSet;
	TMap<UMaterialExpression*, int32> RootPinOrder;
	MaterialLayoutUtils::BuildRootMaps(TargetMaterial->GetEditorOnlyData(), &TargetExpressionSet, RootConnectedSet, RootPinOrder);

	// ---- Phase 3: Connected component discovery ----
	// BFS using ALL connections (both directions) to find independent sub-graphs
	TArray<TArray<UMaterialExpression*>> Components;
	TSet<UMaterialExpression*> Visited;

	for (UMaterialExpression* StartExpr : TargetExpressionSet)
	{
		if (Visited.Contains(StartExpr)) continue;

		TArray<UMaterialExpression*> Comp;
		TQueue<UMaterialExpression*> BFS;
		BFS.Enqueue(StartExpr);
		Visited.Add(StartExpr);

		while (!BFS.IsEmpty())
		{
			UMaterialExpression* Current = nullptr;
			BFS.Dequeue(Current);
			Comp.Add(Current);

			// Traverse both directions
			auto TraverseNeighbors = [&](const TArray<UMaterialExpression*>& Neighbors)
			{
				for (UMaterialExpression* N : Neighbors)
				{
					if (!Visited.Contains(N))
					{
						Visited.Add(N);
						BFS.Enqueue(N);
					}
				}
			};
			if (auto* DepList = Deps.Find(Current)) TraverseNeighbors(*DepList);
			if (auto* ConList = Consumers.Find(Current)) TraverseNeighbors(*ConList);
		}

		Components.Add(MoveTemp(Comp));
	}

	// Sort components: one containing root-connected nodes first
	Components.Sort([&RootConnectedSet](const TArray<UMaterialExpression*>& A, const TArray<UMaterialExpression*>& B)
	{
		bool AHasRoot = false, BHasRoot = false;
		for (auto* E : A) { if (RootConnectedSet.Contains(E)) { AHasRoot = true; break; } }
		for (auto* E : B) { if (RootConnectedSet.Contains(E)) { BHasRoot = true; break; } }
		if (AHasRoot != BHasRoot) return AHasRoot; // root-connected component first
		return A.Num() > B.Num(); // then by size
	});

	// ---- Per-component layout ----
	const double HGap = 80.0;
	const double VGap = 40.0;
	TMap<UMaterialExpression*, FVector2D> FinalPositions;
	double ComponentStackY = 0.0;
	double GlobalMinX = 0.0;

	for (int32 CompIdx = 0; CompIdx < Components.Num(); ++CompIdx)
	{
		const TArray<UMaterialExpression*>& Comp = Components[CompIdx];
		TSet<UMaterialExpression*> CompSet(Comp);

		// ---- Phase 4: Layer assignment (Kahn topological sort, longest path) ----
		// Layer 0 = closest to root output (rightmost), higher layers = further upstream (left)
		// "Roots" within component = nodes consumed by root output, or nodes with no downstream consumer in-set
		TMap<UMaterialExpression*, int32> LayerMap;
		TMap<UMaterialExpression*, int32> InDegree; // in-degree = number of downstream consumers (for Kahn from right side)

		for (UMaterialExpression* Expr : Comp)
		{
			LayerMap.Add(Expr, 0);
			int32 Deg = 0;
			if (auto* ConList = Consumers.Find(Expr))
			{
				for (UMaterialExpression* C : *ConList)
				{
					if (CompSet.Contains(C)) ++Deg;
				}
			}
			InDegree.Add(Expr, Deg);
		}

		// Seeds: nodes with no in-component downstream consumer, or root-connected
		TQueue<UMaterialExpression*> TopoQueue;
		for (UMaterialExpression* Expr : Comp)
		{
			if (InDegree[Expr] == 0)
			{
				TopoQueue.Enqueue(Expr);
			}
		}

		while (!TopoQueue.IsEmpty())
		{
			UMaterialExpression* Current = nullptr;
			TopoQueue.Dequeue(Current);

			int32 CurLayer = LayerMap[Current];
			if (auto* DepList = Deps.Find(Current))
			{
				for (UMaterialExpression* Dep : *DepList)
				{
					if (!CompSet.Contains(Dep)) continue;
					int32& DepLayer = LayerMap[Dep];
					if (DepLayer < CurLayer + 1)
					{
						DepLayer = CurLayer + 1;
					}
					int32& Deg = InDegree[Dep];
					// NOTE: for Kahn, decrement by counting unique traversals
					// Since deps can be shared, use direct decrement of consumer count
				}
			}
		}

		// Handle cycles: any node not yet fully resolved gets max+1
		// Re-do Kahn properly with actual in-degree tracking
		{
			TMap<UMaterialExpression*, int32> KahnInDeg;
			for (UMaterialExpression* Expr : Comp) KahnInDeg.Add(Expr, 0);
			for (UMaterialExpression* Expr : Comp)
			{
				if (auto* DepList = Deps.Find(Expr))
				{
					for (UMaterialExpression* Dep : *DepList)
					{
						if (CompSet.Contains(Dep))
							KahnInDeg[Dep]++;
						// Dep is upstream, Expr is downstream consumer
						// For layer assignment from right: consumer processes first (layer 0),
						// then deps propagate layer+1
					}
				}
			}

			// Reset layers
			for (UMaterialExpression* Expr : Comp) LayerMap[Expr] = 0;

			TQueue<UMaterialExpression*> Q;
			for (UMaterialExpression* Expr : Comp)
			{
				if (KahnInDeg[Expr] == 0) Q.Enqueue(Expr);
			}

			TArray<UMaterialExpression*> Sorted;
			while (!Q.IsEmpty())
			{
				UMaterialExpression* Cur = nullptr;
				Q.Dequeue(Cur);
				Sorted.Add(Cur);

				// Cur consumes its Deps; Deps are upstream
				if (auto* DepList = Deps.Find(Cur))
				{
					for (UMaterialExpression* Dep : *DepList)
					{
						if (!CompSet.Contains(Dep)) continue;
						int32& DepLayer = LayerMap[Dep];
						DepLayer = FMath::Max(DepLayer, LayerMap[Cur] + 1);
						KahnInDeg[Dep]--;
						if (KahnInDeg[Dep] == 0) Q.Enqueue(Dep);
					}
				}
			}

			// Cycle fallback
			int32 MaxL = 0;
			for (auto& P : LayerMap) MaxL = FMath::Max(MaxL, P.Value);
			for (UMaterialExpression* Expr : Comp)
			{
				if (!Sorted.Contains(Expr))
				{
					LayerMap[Expr] = ++MaxL;
				}
			}
		}

		// ---- Group by layer ----
		int32 MaxLayer = 0;
		TMap<int32, TArray<UMaterialExpression*>> LayerGroups;
		for (UMaterialExpression* Expr : Comp)
		{
			int32 L = LayerMap[Expr];
			LayerGroups.FindOrAdd(L).Add(Expr);
			MaxLayer = FMath::Max(MaxLayer, L);
		}

		// ---- Phase 5: Pin-aware layer sorting (shared utility) ----
		MaterialLayoutUtils::SortLayersByPinOrder(
			LayerGroups, MaxLayer, Deps, Consumers, PinIndexMap, RootConnectedSet, RootPinOrder);

		// ---- Phase 6: X coordinates (per-node width-driven, right to left) ----
		// Layer 0 at X = -(some offset from root), higher layers further left
		TMap<int32, float> LayerX;     // X position for each layer
		TMap<int32, float> LayerWidth; // max node width per layer

		for (int32 L = 0; L <= MaxLayer; ++L)
		{
			float MaxW = 0.f;
			if (auto* Group = LayerGroups.Find(L))
			{
				for (UMaterialExpression* Expr : *Group)
				{
					const FVector2D* SizePtr = NodeSizes.Find(Expr);
					MaxW = FMath::Max(MaxW, SizePtr ? (float)SizePtr->X : 280.f);
				}
			}
			LayerWidth.Add(L, MaxW);
		}

		// Root node at X=0; Layer 0 starts at -(RootWidth + HGap)
		float CurrentX = 0.f;
		if (bHasRootNode && CompIdx == 0)
		{
			CurrentX = -(RootNodeSize.X + HGap);
		}

		for (int32 L = 0; L <= MaxLayer; ++L)
		{
			float W = LayerWidth[L];
			LayerX.Add(L, CurrentX - W); // left edge of this layer
			CurrentX -= (W + HGap);
		}

		// ---- Phase 7: Y coordinates (connection-aware alignment) ----
		// First pass: assign Y based on row index with actual heights
		TMap<UMaterialExpression*, FVector2D> CompPositions;
		TMap<int32, float> LayerTotalHeight;

		for (int32 L = 0; L <= MaxLayer; ++L)
		{
			auto* Group = LayerGroups.Find(L);
			if (!Group) continue;

			float Y = 0.f;
			float TotalH = 0.f;
			for (UMaterialExpression* Expr : *Group)
			{
				const FVector2D* SizePtr = NodeSizes.Find(Expr);
				float H = SizePtr ? (float)SizePtr->Y : 100.f;
				CompPositions.Add(Expr, FVector2D(LayerX[L], Y));
				Y += H + VGap;
				TotalH = Y;
			}
			LayerTotalHeight.Add(L, TotalH - VGap);
		}

		// Second pass: Center-align each layer vertically to Layer 0's height
		float Layer0Height = LayerTotalHeight.Contains(0) ? LayerTotalHeight[0] : 0.f;
		for (int32 L = 0; L <= MaxLayer; ++L)
		{
			float LH = LayerTotalHeight.Contains(L) ? LayerTotalHeight[L] : 0.f;
			float OffsetY = (Layer0Height - LH) * 0.5f;
			if (FMath::Abs(OffsetY) < 1.0f) continue;

			if (auto* Group = LayerGroups.Find(L))
			{
				for (UMaterialExpression* Expr : *Group)
				{
					FVector2D& Pos = CompPositions[Expr];
					Pos.Y += OffsetY;
				}
			}
		}

		// Third pass: for single-node layers, align to connected downstream node's Y center
		for (int32 L = 1; L <= MaxLayer; ++L)
		{
			auto* Group = LayerGroups.Find(L);
			if (!Group || Group->Num() != 1) continue;

			UMaterialExpression* Expr = (*Group)[0];
			// Find downstream consumers in L-1
			TArray<FVector2D> ConsumerPositions;
			if (auto* ConList = Consumers.Find(Expr))
			{
				for (UMaterialExpression* C : *ConList)
				{
					if (FVector2D* CPos = CompPositions.Find(C))
					{
						const FVector2D* CSize = NodeSizes.Find(C);
						double CenterY = CPos->Y + (CSize ? CSize->Y * 0.5 : 50.0);
						ConsumerPositions.Add(FVector2D(CPos->X, CenterY));
					}
				}
			}

			if (ConsumerPositions.Num() > 0)
			{
				double AvgY = 0.0;
				for (const FVector2D& P : ConsumerPositions) AvgY += P.Y;
				AvgY /= ConsumerPositions.Num();

				const FVector2D* ExprSize = NodeSizes.Find(Expr);
				double ExprH = ExprSize ? ExprSize->Y : 100.0;
				CompPositions[Expr].Y = AvgY - ExprH * 0.5;
			}
		}

		// ---- Phase 8: Minimum-gap enforcement ----
		// For X-overlapping nodes, enforce minimum VGap between bottom of A and top of B
		TArray<UMaterialExpression*> AllInComp = Comp;
		for (int32 Iter = 0; Iter < 12; ++Iter)
		{
			bool bHadCollision = false;

			// Sort by Y for sweep
			AllInComp.Sort([&CompPositions](const UMaterialExpression& A, const UMaterialExpression& B)
			{
				return CompPositions[&A].Y < CompPositions[&B].Y;
			});

			for (int32 i = 0; i < AllInComp.Num(); ++i)
			{
				UMaterialExpression* ExprA = AllInComp[i];
				FVector2D PosA = CompPositions[ExprA];
				const FVector2D* SizeA = NodeSizes.Find(ExprA);
				double WA = SizeA ? SizeA->X : 280.0;
				double HA = SizeA ? SizeA->Y : 100.0;

				for (int32 j = i + 1; j < AllInComp.Num(); ++j)
				{
					UMaterialExpression* ExprB = AllInComp[j];
					FVector2D PosB = CompPositions[ExprB];
					const FVector2D* SizeB = NodeSizes.Find(ExprB);
					double WB = SizeB ? SizeB->X : 280.0;
					double HB = SizeB ? SizeB->Y : 100.0;

					// Early Y cutoff: B is far enough below A
					if (PosB.Y > PosA.Y + HA + VGap) break;

					// Check horizontal overlap
					const double Tol = 4.0;
					bool bOverlapX = (PosA.X < PosB.X + WB - Tol) && (PosB.X < PosA.X + WA - Tol);
					if (!bOverlapX) continue;

					// Enforce minimum gap: B's top must be at least VGap below A's bottom
					double GapY = PosB.Y - (PosA.Y + HA);
					if (GapY < VGap)
					{
						double PushY = VGap - GapY;
						CompPositions[ExprB].Y += PushY;
						bHadCollision = true;
					}
				}
			}

			if (!bHadCollision) break;
		}

		// ---- Phase 9: Stack this component below previous ones ----
		if (CompIdx > 0)
		{
			// Find min Y in this component
			double MinY = TNumericLimits<double>::Max();
			for (auto& Pair : CompPositions)
			{
				MinY = FMath::Min(MinY, Pair.Value.Y);
			}
			double ShiftY = ComponentStackY - MinY + VGap * 2.0;
			for (auto& Pair : CompPositions)
			{
				Pair.Value.Y += ShiftY;
			}
		}

		// Update ComponentStackY to bottom of this component
		double CompMaxY = -TNumericLimits<double>::Max();
		for (auto& Pair : CompPositions)
		{
			const FVector2D* Size = NodeSizes.Find(Pair.Key);
			double Bottom = Pair.Value.Y + (Size ? Size->Y : 100.0);
			CompMaxY = FMath::Max(CompMaxY, Bottom);
		}
		ComponentStackY = CompMaxY;

		// Track global min X
		for (auto& Pair : CompPositions)
		{
			GlobalMinX = FMath::Min(GlobalMinX, Pair.Value.X);
		}

		// Merge into final
		for (auto& Pair : CompPositions)
		{
			FinalPositions.Add(Pair.Key, Pair.Value);
		}
	}

	// ---- Phase 10: Apply positions ----
	int32 NodesMoved = 0;
	for (auto& Pair : FinalPositions)
	{
		UMaterialExpression* Expr = Pair.Key;
		int32 NewX = FMath::RoundToInt(Pair.Value.X);
		int32 NewY = FMath::RoundToInt(Pair.Value.Y);

		if (Expr->MaterialExpressionEditorX != NewX || Expr->MaterialExpressionEditorY != NewY)
		{
			Expr->Modify();
			Expr->MaterialExpressionEditorX = NewX;
			Expr->MaterialExpressionEditorY = NewY;
			++NodesMoved;
		}
	}

	// ---- Phase 11: Root node positioning ----
	if (bHasRootNode)
	{
		// Find the total height span of Layer 0 in the primary component
		double Layer0MinY = TNumericLimits<double>::Max();
		double Layer0MaxY = -TNumericLimits<double>::Max();

		for (auto& Pair : FinalPositions)
		{
			if (RootConnectedSet.Contains(Pair.Key) || (!bSelectedOnly && Consumers[Pair.Key].Num() == 0))
			{
				// This is a Layer 0 node in the primary component
				const FVector2D* Size = NodeSizes.Find(Pair.Key);
				double H = Size ? Size->Y : 100.0;
				Layer0MinY = FMath::Min(Layer0MinY, Pair.Value.Y);
				Layer0MaxY = FMath::Max(Layer0MaxY, Pair.Value.Y + H);
			}
		}

		if (Layer0MinY < TNumericLimits<double>::Max())
		{
			double RootY = (Layer0MinY + Layer0MaxY) * 0.5 - RootNodeSize.Y * 0.5;
			int32 NewRootX = 0;
			int32 NewRootY = FMath::RoundToInt(RootY);

			if (TargetMaterial->EditorX != NewRootX || TargetMaterial->EditorY != NewRootY)
			{
				TargetMaterial->EditorX = NewRootX;
				TargetMaterial->EditorY = NewRootY;
				++NodesMoved;
			}
		}
	}

	// ---- Phase 12: Rebuild and sync ----
	MatGraph->RebuildGraph();
	TargetMaterial->MarkPackageDirty();

	if (OriginalMaterial && OriginalMaterial != TargetMaterial)
	{
		OriginalMaterial->EditorX = TargetMaterial->EditorX;
		OriginalMaterial->EditorY = TargetMaterial->EditorY;

		UMCPBridge* Bridge = GEditor ? GEditor->GetEditorSubsystem<UMCPBridge>() : nullptr;
		if (Bridge)
		{
			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("material_name"), OriginalMaterial->GetName());
			Bridge->ExecuteCommandSafe(TEXT("auto_layout_material"), Params);
		}
	}

	UE_LOG(LogMCP, Log, TEXT("MaterialAutoLayout: moved %d nodes in '%s' (selected=%s, root=%s, components=%d)"),
		NodesMoved, *TargetMaterial->GetName(),
		bSelectedOnly ? TEXT("yes") : TEXT("no"),
		bHasRootNode ? TEXT("yes") : TEXT("no"),
		Components.Num());
}


// -------------- Module Implementation -------------------

void FUEEditorMCPModule::StartupModule()
{
	FMCPLogCapture::Get().Start();

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Module starting up"));

	// The Bridge is an EditorSubsystem and will be automatically created
	// when the editor starts. It handles server startup internally.

	RegisterAutoLayoutCommands();
}

void FUEEditorMCPModule::ShutdownModule()
{
	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Module shutting down"));

	UnregisterAutoLayoutCommands();

	FMCPLogCapture::Get().Stop();

	// The Bridge will be automatically destroyed as an EditorSubsystem.
}

bool FUEEditorMCPModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("UEEditorMCP");
}

FUEEditorMCPModule& FUEEditorMCPModule::Get()
{
	return FModuleManager::LoadModuleChecked<FUEEditorMCPModule>("UEEditorMCP");
}


void FUEEditorMCPModule::RegisterAutoLayoutCommands()
{
	// Register command infos (shortcuts)
	FBlueprintAutoLayoutCommands::Register();

	// Create command list and bind actions
	AutoLayoutCommandList = MakeShared<FUICommandList>();

	AutoLayoutCommandList->MapAction(
		FBlueprintAutoLayoutCommands::Get().AutoLayoutSelected,
		FExecuteAction::CreateStatic(&ExecuteAutoLayoutSmart));

	AutoLayoutCommandList->MapAction(
		FBlueprintAutoLayoutCommands::Get().AutoLayoutSubtree,
		FExecuteAction::CreateStatic(&ExecuteAutoLayoutSubtree));

	AutoLayoutCommandList->MapAction(
		FBlueprintAutoLayoutCommands::Get().AutoLayoutGraph,
		FExecuteAction::CreateStatic(&ExecuteAutoLayoutGraph));

	// Also bind into Blueprint editor shared commands so keyboard shortcuts
	// are processed inside Blueprint editor toolkits.
	if (FModuleManager::Get().IsModuleLoaded("Kismet"))
	{
		FBlueprintEditorModule& BPEditorModule = FModuleManager::GetModuleChecked<FBlueprintEditorModule>("Kismet");
		const TSharedRef<FUICommandList> SharedCommandsConst = BPEditorModule.GetsSharedBlueprintEditorCommands();
		TSharedRef<FUICommandList> SharedCommands = ConstCastSharedRef<FUICommandList>(SharedCommandsConst);

		SharedCommands->MapAction(
			FBlueprintAutoLayoutCommands::Get().AutoLayoutSelected,
			FExecuteAction::CreateStatic(&ExecuteAutoLayoutSmart));

		SharedCommands->MapAction(
			FBlueprintAutoLayoutCommands::Get().AutoLayoutSubtree,
			FExecuteAction::CreateStatic(&ExecuteAutoLayoutSubtree));

		SharedCommands->MapAction(
			FBlueprintAutoLayoutCommands::Get().AutoLayoutGraph,
			FExecuteAction::CreateStatic(&ExecuteAutoLayoutGraph));
	}

	// Create a shared extender with our menu entries
	BlueprintMenuExtender = MakeShared<FExtender>();
	BlueprintMenuExtender->AddMenuExtension(
		"EditSearch",
		EExtensionHook::After,
		AutoLayoutCommandList,
		FMenuExtensionDelegate::CreateLambda([](FMenuBuilder& MenuBuilder)
		{
			MenuBuilder.BeginSection("AutoLayout", LOCTEXT("AutoLayoutSection", "Auto Layout"));
			{
				MenuBuilder.AddMenuEntry(FBlueprintAutoLayoutCommands::Get().AutoLayoutSelected);
			}
			MenuBuilder.EndSection();
		}));

	// Defer Blueprint editor menu registration until Kismet module is available
	if (FModuleManager::Get().IsModuleLoaded("Kismet"))
	{
		FBlueprintEditorModule& BPEditorModule = FModuleManager::GetModuleChecked<FBlueprintEditorModule>("Kismet");
		TSharedPtr<FExtensibilityManager> MenuExtManager = BPEditorModule.GetMenuExtensibilityManager();
		if (MenuExtManager.IsValid())
		{
			MenuExtManager->AddExtender(BlueprintMenuExtender);
		}
	}
	else
	{
		// Kismet not loaded yet — register callback for when it becomes available
		FModuleManager::Get().OnModulesChanged().AddLambda(
			[this](FName ModuleName, EModuleChangeReason Reason)
			{
				if (ModuleName == TEXT("Kismet") && Reason == EModuleChangeReason::ModuleLoaded
					&& BlueprintMenuExtender.IsValid())
				{
					FBlueprintEditorModule& BPEditorModule = FModuleManager::GetModuleChecked<FBlueprintEditorModule>("Kismet");
					TSharedPtr<FExtensibilityManager> MenuExtManager = BPEditorModule.GetMenuExtensibilityManager();
					if (MenuExtManager.IsValid())
					{
						MenuExtManager->AddExtender(BlueprintMenuExtender);
					}
				}
			});
	}

	// ---- P5.3: Material editor Auto Layout menu registration ----
	MaterialMenuExtender = MakeShared<FExtender>();
	MaterialMenuExtender->AddMenuExtension(
		"EditHistory",
		EExtensionHook::After,
		AutoLayoutCommandList,
		FMenuExtensionDelegate::CreateLambda([](FMenuBuilder& MenuBuilder)
		{
			MenuBuilder.BeginSection("MaterialAutoLayout", LOCTEXT("MaterialAutoLayoutSection", "Auto Layout"));
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("MaterialAutoLayout", "Auto Layout"),
					LOCTEXT("MaterialAutoLayoutTooltip", "Auto-layout material graph nodes"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&ExecuteMaterialAutoLayout)));
			}
			MenuBuilder.EndSection();
		}));

	if (FModuleManager::Get().IsModuleLoaded("MaterialEditor"))
	{
		IMaterialEditorModule& MatEdModule = FModuleManager::GetModuleChecked<IMaterialEditorModule>("MaterialEditor");
		TSharedPtr<FExtensibilityManager> MatMenuExtManager = MatEdModule.GetMenuExtensibilityManager();
		if (MatMenuExtManager.IsValid())
		{
			MatMenuExtManager->AddExtender(MaterialMenuExtender);
		}
	}
	else
	{
		// MaterialEditor not loaded yet — register callback for deferred loading
		FModuleManager::Get().OnModulesChanged().AddLambda(
			[this](FName ModuleName, EModuleChangeReason Reason)
			{
				if (ModuleName == TEXT("MaterialEditor") && Reason == EModuleChangeReason::ModuleLoaded
					&& MaterialMenuExtender.IsValid())
				{
					IMaterialEditorModule& MatEdModule = FModuleManager::GetModuleChecked<IMaterialEditorModule>("MaterialEditor");
					TSharedPtr<FExtensibilityManager> MatMenuExtManager = MatEdModule.GetMenuExtensibilityManager();
					if (MatMenuExtManager.IsValid())
					{
						MatMenuExtManager->AddExtender(MaterialMenuExtender);
					}
				}
			});
	}
}


void FUEEditorMCPModule::UnregisterAutoLayoutCommands()
{
	// Remove Blueprint menu extender
	if (BlueprintMenuExtender.IsValid())
	{
		if (FModuleManager::Get().IsModuleLoaded("Kismet"))
		{
			FBlueprintEditorModule& BPEditorModule = FModuleManager::GetModuleChecked<FBlueprintEditorModule>("Kismet");
			TSharedPtr<FExtensibilityManager> MenuExtManager = BPEditorModule.GetMenuExtensibilityManager();
			if (MenuExtManager.IsValid())
			{
				MenuExtManager->RemoveExtender(BlueprintMenuExtender);
			}
		}
		BlueprintMenuExtender.Reset();
	}

	// P5.3: Remove Material editor menu extender
	if (MaterialMenuExtender.IsValid())
	{
		if (FModuleManager::Get().IsModuleLoaded("MaterialEditor"))
		{
			IMaterialEditorModule& MatEdModule = FModuleManager::GetModuleChecked<IMaterialEditorModule>("MaterialEditor");
			TSharedPtr<FExtensibilityManager> MatMenuExtManager = MatEdModule.GetMenuExtensibilityManager();
			if (MatMenuExtManager.IsValid())
			{
				MatMenuExtManager->RemoveExtender(MaterialMenuExtender);
			}
		}
		MaterialMenuExtender.Reset();
	}

	AutoLayoutCommandList.Reset();
	FBlueprintAutoLayoutCommands::Unregister();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUEEditorMCPModule, UEEditorMCP)
