// MaterialLayoutUtils.h — Shared pin-aware layer sorting for material auto-layout
// Used by both the MCP action path (MaterialActions.cpp) and
// the editor UI toolbar path (UEEditorMCPModule.cpp).

#pragma once

#include "CoreMinimal.h"

class UMaterialExpression;
class UMaterialEditorOnlyData;

namespace MaterialLayoutUtils
{
	/**
	 * Build PinIndexMap from dependency graph.
	 * PinIndexMap[Consumer][Dependency] = the input pin index on Consumer where Dependency is connected.
	 * Only records the first (lowest-index) pin for each dependency.
	 */
	void BuildPinIndexMap(
		const TMap<UMaterialExpression*, TArray<UMaterialExpression*>>& Deps,
		TMap<UMaterialExpression*, TMap<UMaterialExpression*, int32>>& OutPinIndexMap);

	/**
	 * Build RootConnectedSet and RootPinOrder from material editor-only data.
	 * RootConnectedSet: expressions directly connected to material output pins.
	 * RootPinOrder: maps each root-connected expression to its material output pin index
	 *               (BaseColor=0, Metallic=1, Specular=2, Roughness=3, Normal=4, ...).
	 *
	 * @param FilterSet  If non-null, only expressions in this set are included.
	 */
	void BuildRootMaps(
		UMaterialEditorOnlyData* EditorData,
		const TSet<UMaterialExpression*>* FilterSet,
		TSet<UMaterialExpression*>& OutRootConnectedSet,
		TMap<UMaterialExpression*, int32>& OutRootPinOrder);

	/**
	 * Sort LayerGroups by pin order using iterative pin-weighted barycenter.
	 *
	 * Algorithm:
	 *   Step 1: Sort Layer 0 by material output pin order (RootPinOrder).
	 *   Step 2: 3 iterations of forward (L=1→MaxLayer) + backward (L=MaxLayer-1→0) sweeps.
	 *           Each sweep computes a pin-weighted barycenter for every node:
	 *             key(N) = avg(sortIndex(neighbor) + pinIndex/MAX_PINS) over connected neighbors in adjacent layer
	 *           This combines:
	 *             - Pin ordering (pin offsets are small fractions → siblings of same consumer stay in pin order)
	 *             - Multi-consumer centering (average → shared nodes find centroid between consumers)
	 *             - Iterative convergence (3 passes handle DAG/diamond patterns)
	 *           Backward sweeps preserve root pin ordering for Layer 0.
	 *
	 * @param LayerGroups         [in/out] Map from layer index to sorted array of expressions.
	 * @param MaxLayer            Maximum layer index.
	 * @param Deps                Deps[E] = upstream dependencies of E.
	 * @param Consumers           Consumers[E] = downstream consumers of E.
	 * @param PinIndexMap         PinIndexMap[Consumer][Dep] = input pin index.
	 * @param RootConnectedSet    Expressions directly wired to material output.
	 * @param RootPinOrder        Material output pin order per root-connected expression.
	 */
	void SortLayersByPinOrder(
		TMap<int32, TArray<UMaterialExpression*>>& LayerGroups,
		int32 MaxLayer,
		const TMap<UMaterialExpression*, TArray<UMaterialExpression*>>& Deps,
		const TMap<UMaterialExpression*, TArray<UMaterialExpression*>>& Consumers,
		const TMap<UMaterialExpression*, TMap<UMaterialExpression*, int32>>& PinIndexMap,
		const TSet<UMaterialExpression*>& RootConnectedSet,
		const TMap<UMaterialExpression*, int32>& RootPinOrder);

} // namespace MaterialLayoutUtils
