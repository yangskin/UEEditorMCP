// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorAction.h"

// Forward declarations
class UMaterial;
class UMaterialInstance;
class UMaterialExpression;
class UMaterialExpressionComment;
class UMaterialGraph;

/**
 * FMaterialAction
 *
 * Base class for Material-related actions.
 * Provides common utilities for material manipulation.
 */
class UEEDITORMCP_API FMaterialAction : public FEditorAction
{
protected:
	/** Find Material by name */
	UMaterial* FindMaterial(const FString& MaterialName, FString& OutError) const;

	/** Get Material by name, or use current from context if name is empty */
	UMaterial* GetMaterialByNameOrCurrent(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) const;

	/** Clean up existing material with same name */
	void CleanupExistingMaterial(const FString& MaterialName, const FString& PackagePath) const;

	/** Resolve expression class from friendly name */
	UClass* ResolveExpressionClass(const FString& ExpressionClassName) const;

	/** Mark material as modified and trigger recompilation */
	void MarkMaterialModified(UMaterial* Material, FMCPEditorContext& Context) const;
};


/**
 * FCreateMaterialAction
 *
 * Creates a new Material asset with specified domain and blend mode.
 *
 * Parameters:
 *   - material_name (required): Name of the Material to create
 *   - path (optional): Content path (default: /Game/Materials)
 *   - domain (optional): Material domain (Surface, PostProcess, DeferredDecal, LightFunction, UI)
 *   - blend_mode (optional): Blend mode (Opaque, Masked, Translucent, Additive, Modulate)
 *   - blendable_location (optional): For PostProcess materials (BeforeTonemapping, AfterTonemapping, BeforeTranslucency, ReplacingTonemapper)
 *
 * Returns:
 *   - name: Created material name
 *   - path: Asset path
 *   - domain: Material domain
 *   - blend_mode: Blend mode
 */
class UEEDITORMCP_API FCreateMaterialAction : public FMaterialAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("create_material"); }

private:
	/** Resolve domain enum from string */
	TOptional<EMaterialDomain> ResolveDomain(const FString& DomainString) const;

	/** Resolve blend mode enum from string */
	TOptional<EBlendMode> ResolveBlendMode(const FString& BlendModeString) const;

	/** Resolve blendable location enum from string */
	TOptional<EBlendableLocation> ResolveBlendableLocation(const FString& LocationString) const;
};


/**
 * FAddMaterialExpressionAction
 *
 * Adds an expression node to a Material's graph.
 *
 * Parameters:
 *   - material_name (required): Name of the target Material
 *   - expression_class (required): Type of expression (SceneTexture, Time, Noise, Add, Multiply, etc.)
 *   - node_name (required): Unique name for this node (for later connection/reference)
 *   - position (optional): [X, Y] editor position (default: [0, 0])
 *   - properties (optional): Object with property name/value pairs to set
 *
 * Returns:
 *   - node_name: Registered node name
 *   - expression_class: Expression type
 */
class UEEDITORMCP_API FAddMaterialExpressionAction : public FMaterialAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("add_material_expression"); }

private:
	/** Set properties on an expression from JSON object */
	void SetExpressionProperties(UMaterialExpression* Expression, const TSharedPtr<FJsonObject>& Properties) const;
};


/**
 * FConnectMaterialExpressionsAction
 *
 * Connects output of one expression to input of another.
 *
 * Parameters:
 *   - material_name (required): Name of the Material
 *   - source_node (required): Name of the source expression
 *   - source_output_index (optional): Output pin index (default: 0)
 *   - target_node (required): Name of the target expression
 *   - target_input (required): Input pin name (A, B, Alpha, etc.)
 *
 * Returns:
 *   - source_node: Source node name
 *   - target_node: Target node name
 *   - target_input: Input pin name
 */
class UEEDITORMCP_API FConnectMaterialExpressionsAction : public FMaterialAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("connect_material_expressions"); }

private:
	/** Connect to a named input on an expression (handles type-specific input property mapping) */
	bool ConnectToExpressionInput(UMaterialExpression* SourceExpr, int32 OutputIndex,
		UMaterialExpression* TargetExpr, const FString& InputName, FString& OutError) const;
};


/**
 * FConnectToMaterialOutputAction
 *
 * Connects an expression to a material's main output (BaseColor, EmissiveColor, etc.).
 *
 * Parameters:
 *   - material_name (required): Name of the Material
 *   - source_node (required): Name of the source expression
 *   - source_output_index (optional): Output pin index (default: 0)
 *   - material_property (required): Material property (BaseColor, EmissiveColor, Metallic, Roughness, Normal, Opacity, etc.)
 *
 * Returns:
 *   - source_node: Source node name
 *   - material_property: Connected property
 */
class UEEDITORMCP_API FConnectToMaterialOutputAction : public FMaterialAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("connect_to_material_output"); }

private:
	/** Connect expression to material property */
	bool ConnectToMaterialProperty(UMaterial* Material, UMaterialExpression* SourceExpr,
		int32 OutputIndex, const FString& PropertyName, FString& OutError) const;
};


/**
 * FSetMaterialExpressionPropertyAction
 *
 * Sets a property on an existing material expression.
 *
 * Parameters:
 *   - material_name (required): Name of the Material
 *   - node_name (required): Name of the expression node
 *   - property_name (required): Property to set
 *   - property_value (required): Value to set (as string, will be parsed)
 *
 * Returns:
 *   - node_name: Node name
 *   - property_name: Property that was set
 */
class UEEDITORMCP_API FSetMaterialExpressionPropertyAction : public FMaterialAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("set_material_expression_property"); }

private:
	/** Set a single property on an expression */
	bool SetExpressionProperty(UMaterialExpression* Expression, const FString& PropertyName,
		const FString& PropertyValue, FString& OutError) const;
};


/**
 * FCompileMaterialAction
 *
 * Compiles a material and reports errors.
 *
 * Parameters:
 *   - material_name (required): Name of the Material to compile
 *
 * Returns:
 *   - name: Material name
 *   - success: Whether compilation succeeded
 *   - error_count: Number of errors
 *   - warning_count: Number of warnings
 */
class UEEDITORMCP_API FCompileMaterialAction : public FMaterialAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("compile_material"); }
	virtual bool RequiresSave() const override { return true; } // Save after successful compilation
};


/**
 * FCreateMaterialInstanceAction
 *
 * Creates a Material Instance from a parent material with parameter overrides.
 *
 * Parameters:
 *   - instance_name (required): Name for the material instance
 *   - parent_material (required): Name of the parent material
 *   - path (optional): Content path (default: /Game/Materials)
 *   - scalar_parameters (optional): Object with scalar parameter overrides
 *   - vector_parameters (optional): Object with vector parameter overrides
 *
 * Returns:
 *   - name: Instance name
 *   - path: Asset path
 *   - parent: Parent material name
 */
class UEEDITORMCP_API FCreateMaterialInstanceAction : public FMaterialAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("create_material_instance"); }
};


/**
 * FSetMaterialPropertyAction
 *
 * Sets a property on a Material asset (not expression nodes).
 * Use this for material-level properties like ShadingModel, TwoSided, BlendMode, etc.
 *
 * Parameters:
 *   - material_name (required): Name of the Material
 *   - property_name (required): Property to set (ShadingModel, TwoSided, BlendMode, etc.)
 *   - property_value (required): Value to set (as string, will be parsed)
 *
 * Supported Properties:
 *   - ShadingModel: Unlit, DefaultLit, Subsurface, PreintegratedSkin, ClearCoat, SubsurfaceProfile, TwoSidedFoliage, Hair, Cloth, Eye
 *   - TwoSided: true/false
 *   - BlendMode: Opaque, Masked, Translucent, Additive, Modulate
 *   - DitheredLODTransition: true/false
 *   - AllowNegativeEmissiveColor: true/false
 *   - OpacityMaskClipValue: float (0.0-1.0)
 *
 * Returns:
 *   - material_name: Material name
 *   - property_name: Property that was set
 */
class UEEDITORMCP_API FSetMaterialPropertyAction : public FMaterialAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("set_material_property"); }

private:
	/** Resolve shading model enum from string */
	TOptional<EMaterialShadingModel> ResolveShadingModel(const FString& ShadingModelString) const;
};


/**
 * FCreatePostProcessVolumeAction
 *
 * Creates a Post Process Volume actor in the level.
 *
 * Parameters:
 *   - name (required): Name for the volume actor
 *   - location (optional): [X, Y, Z] world location
 *   - infinite_extent (optional): Whether to apply everywhere (default: true)
 *   - priority (optional): Priority value (default: 0.0)
 *   - post_process_materials (optional): Array of material names to add
 *
 * Returns:
 *   - name: Actor name
 *   - location: [X, Y, Z]
 *   - infinite_extent: Boolean
 *   - priority: Priority value
 */
class UEEDITORMCP_API FCreatePostProcessVolumeAction : public FMaterialAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("create_post_process_volume"); }

private:
	/** Parse location from params */
	FVector GetVectorFromParams(const TSharedPtr<FJsonObject>& Params, const FString& FieldName) const;
};


/**
 * FGetMaterialSummaryAction (P4.1)
 *
 * Returns full material graph structure: all expressions, connections,
 * and material-level properties (Domain, BlendMode, ShadingModel).
 *
 * Parameters:
 *   - material_name (required): Name of the Material
 *
 * Returns:
 *   - name, path, domain, blend_mode, shading_model, two_sided
 *   - expression_count, expressions (array of {node_name, class, pos_x, pos_y, properties})
 *   - connections (array of {source, source_output, target, target_input})
 */
class UEEDITORMCP_API FGetMaterialSummaryAction : public FMaterialAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("get_material_summary"); }
	virtual bool RequiresSave() const override { return false; }

private:
	/** Get friendly class name for an expression */
	FString GetExpressionClassName(UMaterialExpression* Expr) const;

	/** Get a JSON object with expression-type-specific properties */
	TSharedPtr<FJsonObject> GetExpressionProperties(UMaterialExpression* Expr) const;

	/** Build connections array from material's expression collection */
	TArray<TSharedPtr<FJsonValue>> BuildConnectionsArray(UMaterial* Material, const TMap<UMaterialExpression*, FString>& ExprToName) const;
};


/**
 * FRemoveMaterialExpressionAction (P4.6)
 *
 * Removes one or more expressions from a material by node_name.
 *
 * Parameters:
 *   - material_name (required): Name of the Material
 *   - node_name (optional): Single node to remove
 *   - node_names (optional): Array of nodes to remove
 *
 * Returns:
 *   - removed: Array of removed node names
 *   - not_found: Array of names that were not found
 */
class UEEDITORMCP_API FRemoveMaterialExpressionAction : public FMaterialAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("remove_material_expression"); }
};


/**
 * FAutoLayoutMaterialAction (P4.4)
 *
 * Auto-layout material graph nodes using data-flow topological sort.
 * Reuses size estimation and collision detection from FBlueprintAutoLayout.
 *
 * Parameters:
 *   - material_name (required): Name of the Material
 *   - layer_spacing (optional): float, >0=fixed px, 0=auto
 *   - row_spacing (optional): float, >0=fixed px, 0=auto
 *
 * Returns:
 *   - nodes_moved: Number of nodes repositioned
 *   - layer_count: Number of layers in the layout
 */
class UEEDITORMCP_API FAutoLayoutMaterialAction : public FMaterialAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("auto_layout_material"); }
	virtual bool RequiresSave() const override { return false; }

private:
	/** Build dependency graph (expression -> expressions it depends on via inputs) */
	void BuildDependencyGraph(UMaterial* Material,
		TMap<UMaterialExpression*, TArray<UMaterialExpression*>>& OutDeps,
		TMap<UMaterialExpression*, int32>& OutLayers) const;

	/** Assign layers via reverse BFS from root outputs */
	void AssignLayers(UMaterial* Material,
		const TMap<UMaterialExpression*, TArray<UMaterialExpression*>>& Deps,
		TMap<UMaterialExpression*, int32>& OutLayers) const;
};


/**
 * FAutoCommentMaterialAction (P4.5)
 *
 * Auto-add a comment node around specified (or all) material expressions.
 * Creates via UMaterialExpressionComment + Material->AddComment().
 *
 * Node resolution supports:
 *   - Session-registered names (from material.add_expression)
 *   - $expr_N auto-generated names (from material.get_summary)
 *   - UObject GetName() (e.g. "MaterialExpressionAdd_0")
 *   - Expression Desc field (user-visible description)
 *   - Parameter names (ScalarParameter, VectorParameter)
 *   - "$selected" keyword to wrap editor-selected nodes
 *
 * Parameters:
 *   - material_name (required): Name of the Material
 *   - comment_text (required): Comment text
 *   - node_names (optional): Array of node names to wrap; supports "$selected" (default: all)
 *   - use_selected (optional): Boolean, wrap currently selected nodes in material editor
 *   - color (optional): [R, G, B, A] (0-1)
 *   - padding (optional): Padding around nodes in pixels (default: 40)
 *
 * Returns:
 *   - comment_text, position, size, nodes_wrapped, missing_nodes (if any)
 */
class UEEDITORMCP_API FAutoCommentMaterialAction : public FMaterialAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("auto_comment_material"); }
};


/**
 * FGetMaterialSelectedNodesAction (P5.5)
 *
 * Returns the currently selected material expression nodes in the open material editor.
 * Automatically detects the active material editor — no material_name required.
 *
 * Parameters:
 *   - material_name (optional): Name of the Material (auto-detected if omitted)
 *
 * Returns:
 *   - material_name: Name of the material
 *   - selected_count: Number of selected nodes
 *   - nodes: Array of { node_name, expression_class, pos_x, pos_y, index }
 */
class UEEDITORMCP_API FGetMaterialSelectedNodesAction : public FMaterialAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("get_material_selected_nodes"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FApplyMaterialToComponentAction (P5.2)
 *
 * Applies a material to a specific component on a level actor.
 *
 * Parameters:
 *   - actor_name (required): Name of the Actor in the level
 *   - material_path (required): Asset path of the material (e.g. /Game/Materials/M_Example)
 *   - component_name (optional): Name of the target component (default: first PrimitiveComponent)
 *   - slot_index (optional): Material slot index (default: 0)
 *
 * Returns:
 *   - actor_name: Actor name
 *   - component_name: Component used
 *   - slot_index: Slot index
 *   - material_path: Applied material path
 *   - previous_material: Previously assigned material path (or "None")
 */
class UEEDITORMCP_API FApplyMaterialToComponentAction : public FMaterialAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("apply_material_to_component"); }
};


/**
 * FApplyMaterialToActorAction (P5.4)
 *
 * Applies a material to all PrimitiveComponents on a level actor.
 *
 * Parameters:
 *   - actor_name (required): Name of the Actor in the level
 *   - material_path (required): Asset path of the material
 *   - slot_index (optional): Material slot index to set on each component (default: 0)
 *
 * Returns:
 *   - actor_name: Actor name
 *   - material_path: Applied material path
 *   - components_updated: Number of components updated
 *   - component_names: Array of updated component names
 */
class UEEDITORMCP_API FApplyMaterialToActorAction : public FMaterialAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("apply_material_to_actor"); }
};


/**
 * FRefreshMaterialEditorAction
 *
 * Refreshes the Material Editor UI for a given material.
 * Call this after a batch of material graph modifications (add_expression,
 * connect_expressions, etc.) to make changes visible in the open editor
 * without closing and reopening it.
 *
 * Parameters:
 *   - material_name (required): Name of the Material to refresh
 *
 * Returns:
 *   - material_name: Name of the refreshed material
 *   - editor_found: Whether an open Material Editor was found
 *   - graph_rebuilt: Whether the material graph was rebuilt
 *   - previews_refreshed: Whether expression previews were force-refreshed
 */
class UEEDITORMCP_API FRefreshMaterialEditorAction : public FMaterialAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("refresh_material_editor"); }
	virtual bool RequiresSave() const override { return false; }
};
