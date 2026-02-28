// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorAction.h"

// Forward declarations
class UBlueprint;
class UBlueprintFactory;

/**
 * FCreateBlueprintAction
 *
 * Creates a new Blueprint asset with specified parent class.
 * Handles existing Blueprint cleanup and parent class resolution.
 *
 * Parameters:
 *   - name (required): Name of the Blueprint to create
 *   - parent_class (optional): Parent class name (Actor, Pawn, etc.)
 *
 * Returns:
 *   - name: Created Blueprint name
 *   - path: Asset path
 */
class UEEDITORMCP_API FCreateBlueprintAction : public FBlueprintAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("create_blueprint"); }

private:
	/** Resolve parent class from name string */
	UClass* ResolveParentClass(const FString& ParentClassName) const;

	/** Clean up existing Blueprint with same name */
	void CleanupExistingBlueprint(const FString& BlueprintName, const FString& PackagePath) const;
};


/**
 * FCompileBlueprintAction
 *
 * Compiles a Blueprint and reports errors/warnings.
 *
 * Parameters:
 *   - blueprint_name (required): Name of the Blueprint to compile
 *
 * Returns:
 *   - name: Blueprint name
 *   - success: Whether compilation succeeded
 *   - status: Compilation status string
 *   - error_count: Number of errors
 *   - warning_count: Number of warnings
 *   - errors: Array of error details (if any)
 *   - warnings: Array of warning details (if any)
 */
class UEEDITORMCP_API FCompileBlueprintAction : public FBlueprintAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("compile_blueprint"); }
	virtual bool RequiresSave() const override { return false; } // We save explicitly on success

private:
	/** Collect compilation messages from nodes */
	void CollectCompilationMessages(UBlueprint* Blueprint,
		TArray<TSharedPtr<FJsonValue>>& OutErrors,
		TArray<TSharedPtr<FJsonValue>>& OutWarnings) const;
};


/**
 * FAddComponentToBlueprintAction
 *
 * Adds a component to a Blueprint's component hierarchy.
 *
 * Parameters:
 *   - blueprint_name (required): Name of the target Blueprint
 *   - component_type (required): Type of component (StaticMeshComponent, etc.)
 *   - component_name (required): Name for the new component
 *   - location (optional): [X, Y, Z] relative location
 *   - rotation (optional): [Pitch, Yaw, Roll] relative rotation
 *   - scale (optional): [X, Y, Z] relative scale
 *
 * Returns:
 *   - component_name: Created component name
 *   - component_type: Component type
 */
class UEEDITORMCP_API FAddComponentToBlueprintAction : public FBlueprintAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("add_component_to_blueprint"); }

private:
	/** Resolve component class from type name */
	UClass* ResolveComponentClass(const FString& ComponentTypeName) const;

	/** Parse vector from JSON params */
	FVector GetVectorFromParams(const TSharedPtr<FJsonObject>& Params, const FString& FieldName) const;

	/** Parse rotator from JSON params */
	FRotator GetRotatorFromParams(const TSharedPtr<FJsonObject>& Params, const FString& FieldName) const;
};


/**
 * FSpawnBlueprintActorAction
 *
 * Spawns an instance of a Blueprint in the level.
 *
 * Parameters:
 *   - blueprint_name (required): Name of the Blueprint to spawn
 *   - actor_name (required): Name for the spawned actor
 *   - location (optional): [X, Y, Z] world location
 *   - rotation (optional): [Pitch, Yaw, Roll] world rotation
 *
 * Returns:
 *   - name: Actor name
 *   - class: Actor class
 *   - location: [X, Y, Z]
 *   - rotation: [Pitch, Yaw, Roll]
 */
class UEEDITORMCP_API FSpawnBlueprintActorAction : public FBlueprintAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("spawn_blueprint_actor"); }

private:
	/** Parse vector from JSON params */
	FVector GetVectorFromParams(const TSharedPtr<FJsonObject>& Params, const FString& FieldName) const;

	/** Parse rotator from JSON params */
	FRotator GetRotatorFromParams(const TSharedPtr<FJsonObject>& Params, const FString& FieldName) const;

	/** Convert actor to JSON response */
	TSharedPtr<FJsonObject> ActorToJson(AActor* Actor) const;
};


/**
 * FSetComponentPropertyAction
 *
 * Sets a property on a component in a Blueprint.
 *
 * Parameters:
 *   - blueprint_name (required): Name of the Blueprint
 *   - component_name (required): Name of the component
 *   - property_name (required): Property to set
 *   - property_value (required): Value to set
 *
 * Returns:
 *   - component: Component name
 *   - property: Property name
 *   - success: Whether property was set
 */
class UEEDITORMCP_API FSetComponentPropertyAction : public FBlueprintAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("set_component_property"); }
};


/**
 * FSetStaticMeshPropertiesAction
 *
 * Sets mesh, material, and overlay material on a StaticMeshComponent.
 *
 * Parameters:
 *   - blueprint_name (required): Name of the Blueprint
 *   - component_name (required): Name of the StaticMeshComponent
 *   - static_mesh (optional): Path to static mesh asset
 *   - material (optional): Path to material asset
 *   - overlay_material (optional): Path to overlay material asset (for outline effects, etc.)
 *
 * Returns:
 *   - component: Component name
 */
class UEEDITORMCP_API FSetStaticMeshPropertiesAction : public FBlueprintAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("set_static_mesh_properties"); }
};


/**
 * FSetPhysicsPropertiesAction
 *
 * Sets physics properties on a primitive component.
 *
 * Parameters:
 *   - blueprint_name (required): Name of the Blueprint
 *   - component_name (required): Name of the component
 *   - simulate_physics (optional): Enable physics simulation
 *   - mass (optional): Mass in kg
 *   - linear_damping (optional): Linear damping
 *   - angular_damping (optional): Angular damping
 *
 * Returns:
 *   - component: Component name
 */
class UEEDITORMCP_API FSetPhysicsPropertiesAction : public FBlueprintAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("set_physics_properties"); }
};


/**
 * FSetBlueprintPropertyAction
 *
 * Sets a property on a Blueprint's class default object.
 *
 * Parameters:
 *   - blueprint_name (required): Name of the Blueprint
 *   - property_name (required): Property to set
 *   - property_value (required): Value to set
 *
 * Returns:
 *   - property: Property name
 *   - success: Whether property was set
 */
class UEEDITORMCP_API FSetBlueprintPropertyAction : public FBlueprintAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("set_blueprint_property"); }
};


/**
 * FCreateColoredMaterialAction
 *
 * Creates a simple colored material asset.
 *
 * Parameters:
 *   - material_name (required): Name for the material
 *   - color (optional): [R, G, B] color values (0.0-1.0)
 *
 * Returns:
 *   - name: Material name
 *   - path: Asset path
 *   - success: Whether material was created
 */
class UEEDITORMCP_API FCreateColoredMaterialAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("create_colored_material"); }
};


// ============================================================================
// Blueprint Inheritance & Interfaces
// ============================================================================

/**
 * FSetBlueprintParentClassAction
 *
 * Change the parent class of a Blueprint (reparent).
 *
 * Parameters:
 *   - blueprint_name (required): Name of the Blueprint
 *   - parent_class (required): New parent class name or path
 *
 * Returns:
 *   - blueprint_name, old_parent_class, new_parent_class
 */
class UEEDITORMCP_API FSetBlueprintParentClassAction : public FBlueprintAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("set_blueprint_parent_class"); }

private:
	UClass* ResolveClass(const FString& ClassName) const;
};


/**
 * FAddBlueprintInterfaceAction
 *
 * Add an interface implementation to a Blueprint.
 *
 * Parameters:
 *   - blueprint_name (required): Name of the Blueprint
 *   - interface_name (required): Interface name or asset path
 *
 * Returns:
 *   - blueprint_name, interface_name
 */
class UEEDITORMCP_API FAddBlueprintInterfaceAction : public FBlueprintAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("add_blueprint_interface"); }
};


/**
 * FRemoveBlueprintInterfaceAction
 *
 * Remove an interface implementation from a Blueprint.
 *
 * Parameters:
 *   - blueprint_name (required): Name of the Blueprint
 *   - interface_name (required): Interface name or asset path
 *
 * Returns:
 *   - blueprint_name, interface_name
 */
class UEEDITORMCP_API FRemoveBlueprintInterfaceAction : public FBlueprintAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("remove_blueprint_interface"); }
};
