// MaterialLayoutUtils.cpp — Shared pin-aware layer sorting implementation

#include "MaterialLayoutUtils.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/Material.h"

void MaterialLayoutUtils::BuildPinIndexMap(
	const TMap<UMaterialExpression*, TArray<UMaterialExpression*>>& Deps,
	TMap<UMaterialExpression*, TMap<UMaterialExpression*, int32>>& OutPinIndexMap)
{
	OutPinIndexMap.Reset();

	for (const auto& Pair : Deps)
	{
		UMaterialExpression* Consumer = Pair.Key;
		for (int32 InputIdx = 0; ; ++InputIdx)
		{
			FExpressionInput* Input = Consumer->GetInput(InputIdx);
			if (!Input) break;
			if (Input->Expression && !Input->Expression->IsA<UMaterialExpressionComment>())
			{
				auto& DepMap = OutPinIndexMap.FindOrAdd(Consumer);
				if (!DepMap.Contains(Input->Expression))
				{
					DepMap.Add(Input->Expression, InputIdx);
				}
			}
		}
	}
}

void MaterialLayoutUtils::BuildRootMaps(
	UMaterialEditorOnlyData* EditorData,
	const TSet<UMaterialExpression*>* FilterSet,
	TSet<UMaterialExpression*>& OutRootConnectedSet,
	TMap<UMaterialExpression*, int32>& OutRootPinOrder)
{
	OutRootConnectedSet.Reset();
	OutRootPinOrder.Reset();

	if (!EditorData) return;

	// Collect root-connected expressions
	auto CollectRoot = [&](const FExpressionInput& Input)
	{
		if (Input.Expression && (!FilterSet || FilterSet->Contains(Input.Expression)))
		{
			OutRootConnectedSet.Add(Input.Expression);
		}
	};
	CollectRoot(EditorData->BaseColor);
	CollectRoot(EditorData->EmissiveColor);
	CollectRoot(EditorData->Metallic);
	CollectRoot(EditorData->Roughness);
	CollectRoot(EditorData->Specular);
	CollectRoot(EditorData->Normal);
	CollectRoot(EditorData->Opacity);
	CollectRoot(EditorData->OpacityMask);
	CollectRoot(EditorData->AmbientOcclusion);
	CollectRoot(EditorData->WorldPositionOffset);
	CollectRoot(EditorData->Refraction);
	CollectRoot(EditorData->SubsurfaceColor);

	// Assign pin order (matches material output pin visual order)
	auto AssignOrder = [&](const FExpressionInput& Input, int32 Order)
	{
		if (Input.Expression
			&& (!FilterSet || FilterSet->Contains(Input.Expression))
			&& !OutRootPinOrder.Contains(Input.Expression))
		{
			OutRootPinOrder.Add(Input.Expression, Order);
		}
	};
	AssignOrder(EditorData->BaseColor, 0);
	AssignOrder(EditorData->Metallic, 1);
	AssignOrder(EditorData->Specular, 2);
	AssignOrder(EditorData->Roughness, 3);
	AssignOrder(EditorData->Normal, 4);
	AssignOrder(EditorData->EmissiveColor, 5);
	AssignOrder(EditorData->Opacity, 6);
	AssignOrder(EditorData->OpacityMask, 7);
	AssignOrder(EditorData->AmbientOcclusion, 8);
	AssignOrder(EditorData->WorldPositionOffset, 9);
	AssignOrder(EditorData->Refraction, 10);
	AssignOrder(EditorData->SubsurfaceColor, 11);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace
{
	// Compute pin-weighted barycenter for a node relative to an adjacent layer.
	// For each connected neighbor in the adjacent layer, the contribution is:
	//   sortIndex(neighbor) + pinIndex / MAX_PINS
	// The final key is the average of all contributions.
	// Nodes with no neighbors in the adjacent layer get a large fallback value.
	//
	// bForward = true  → neighbor = consumer  (sorting upstream layer by downstream positions)
	// bForward = false → neighbor = dependency (sorting downstream layer by upstream positions)
	float ComputePinBarycenter(
		UMaterialExpression* Expr,
		const TMap<UMaterialExpression*, int32>& AdjacentPos,
		const TMap<UMaterialExpression*, TArray<UMaterialExpression*>>& NeighborMap,
		const TMap<UMaterialExpression*, TMap<UMaterialExpression*, int32>>& PinIndexMap,
		bool bForward)
	{
		const float MAX_PINS = 10000.f;
		float Sum = 0.f;
		int32 Count = 0;

		const TArray<UMaterialExpression*>* Neighbors = NeighborMap.Find(Expr);
		if (Neighbors)
		{
			for (UMaterialExpression* N : *Neighbors)
			{
				const int32* NPos = AdjacentPos.Find(N);
				if (!NPos) continue;

				float PinOffset = 0.f;
				if (bForward)
				{
					// Expr feeds consumer N; pin index is on N's input for Expr
					if (auto* PM = PinIndexMap.Find(N))
					{
						if (const int32* Idx = PM->Find(Expr))
						{
							PinOffset = (float)(*Idx) / MAX_PINS;
						}
					}
				}
				else
				{
					// Expr consumes dependency N; pin index is on Expr's input for N
					if (auto* PM = PinIndexMap.Find(Expr))
					{
						if (const int32* Idx = PM->Find(N))
						{
							PinOffset = (float)(*Idx) / MAX_PINS;
						}
					}
				}

				Sum += (float)(*NPos) + PinOffset;
				++Count;
			}
		}

		return Count > 0 ? Sum / (float)Count : 99999.f;
	}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Main sorting entry point
// ---------------------------------------------------------------------------

void MaterialLayoutUtils::SortLayersByPinOrder(
	TMap<int32, TArray<UMaterialExpression*>>& LayerGroups,
	int32 MaxLayer,
	const TMap<UMaterialExpression*, TArray<UMaterialExpression*>>& Deps,
	const TMap<UMaterialExpression*, TArray<UMaterialExpression*>>& Consumers,
	const TMap<UMaterialExpression*, TMap<UMaterialExpression*, int32>>& PinIndexMap,
	const TSet<UMaterialExpression*>& RootConnectedSet,
	const TMap<UMaterialExpression*, int32>& RootPinOrder)
{
	// ---- Step 1: Sort Layer 0 by material output pin order ----
	if (auto* L0 = LayerGroups.Find(0))
	{
		L0->Sort([&RootConnectedSet, &RootPinOrder](UMaterialExpression& A, UMaterialExpression& B)
		{
			bool AR = RootConnectedSet.Contains(&A);
			bool BR = RootConnectedSet.Contains(&B);
			if (AR != BR) return AR; // root-connected first
			if (AR && BR)
			{
				int32 OA = RootPinOrder.Contains(&A) ? RootPinOrder[&A] : 999;
				int32 OB = RootPinOrder.Contains(&B) ? RootPinOrder[&B] : 999;
				if (OA != OB) return OA < OB;
			}
			return A.MaterialExpressionEditorY < B.MaterialExpressionEditorY;
		});
	}

	// ---- Step 2: Iterative pin-weighted barycenter (3 forward+backward sweeps) ----
	// Each iteration refines the node ordering. For tree-shaped subgraphs, the first
	// pass already converges (pin offsets dominate). For DAG/diamond patterns with
	// shared dependencies, successive passes allow nodes to migrate toward their
	// centroid position across multiple consumers.
	const int32 NumPasses = 3;

	for (int32 Pass = 0; Pass < NumPasses; ++Pass)
	{
		// ---- Forward sweep: L=1 → MaxLayer ----
		// Each node's position is determined by its downstream consumers' positions
		// in the already-sorted previous (L-1) layer.
		for (int32 L = 1; L <= MaxLayer; ++L)
		{
			auto* Group = LayerGroups.Find(L);
			if (!Group || Group->Num() <= 1) continue;
			auto* PrevGroup = LayerGroups.Find(L - 1);
			if (!PrevGroup) continue;

			// Build sort-index map for previous (downstream) layer
			TMap<UMaterialExpression*, int32> PrevPos;
			for (int32 i = 0; i < PrevGroup->Num(); ++i)
			{
				PrevPos.Add((*PrevGroup)[i], i);
			}

			// Compute pin-weighted barycenter for each node
			TMap<UMaterialExpression*, float> SortKey;
			for (UMaterialExpression* Expr : *Group)
			{
				SortKey.Add(Expr, ComputePinBarycenter(
					Expr, PrevPos, Consumers, PinIndexMap, /*bForward=*/ true));
			}

			Group->Sort([&SortKey](UMaterialExpression& A, UMaterialExpression& B)
			{
				return SortKey[&A] < SortKey[&B];
			});
		}

		// ---- Backward sweep: L=MaxLayer-1 → 0 ----
		// Each node's position is determined by its upstream dependencies' positions
		// in the already-sorted next (L+1) layer.
		// Layer 0 special: root-connected nodes are pinned in RootPinOrder;
		// only non-root nodes may float based on upstream barycenter.
		for (int32 L = MaxLayer - 1; L >= 0; --L)
		{
			auto* Group = LayerGroups.Find(L);
			if (!Group || Group->Num() <= 1) continue;
			auto* NextGroup = LayerGroups.Find(L + 1);
			if (!NextGroup) continue;

			// Build sort-index map for next (upstream) layer
			TMap<UMaterialExpression*, int32> NextPos;
			for (int32 i = 0; i < NextGroup->Num(); ++i)
			{
				NextPos.Add((*NextGroup)[i], i);
			}

			TMap<UMaterialExpression*, float> SortKey;
			if (L == 0)
			{
				// Layer 0: preserve root pin ordering; float non-root nodes
				const float GROUP_SEPARATOR = 1000.f;
				for (UMaterialExpression* Expr : *Group)
				{
					if (RootConnectedSet.Contains(Expr))
					{
						// Fixed key based on root pin order — stays at top in original order
						int32 RPO = RootPinOrder.Contains(Expr) ? RootPinOrder[Expr] : 999;
						SortKey.Add(Expr, (float)RPO);
					}
					else
					{
						// Non-root: float based on upstream barycenter, offset after root group
						float Bary = ComputePinBarycenter(
							Expr, NextPos, Deps, PinIndexMap, /*bForward=*/ false);
						SortKey.Add(Expr, GROUP_SEPARATOR + Bary);
					}
				}
			}
			else
			{
				for (UMaterialExpression* Expr : *Group)
				{
					SortKey.Add(Expr, ComputePinBarycenter(
						Expr, NextPos, Deps, PinIndexMap, /*bForward=*/ false));
				}
			}

			Group->Sort([&SortKey](UMaterialExpression& A, UMaterialExpression& B)
			{
				return SortKey[&A] < SortKey[&B];
			});
		}
	}
}
