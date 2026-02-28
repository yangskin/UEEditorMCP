// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorAction.h"

class AActor;

/**
 * FGetActorsInLevelAction
 * Returns all actors in the current level.
 */
class UEEDITORMCP_API FGetActorsInLevelAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override { return true; }
	virtual FString GetActionName() const override { return TEXT("get_actors_in_level"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FFindActorsByNameAction
 * Finds actors matching a name pattern.
 */
class UEEDITORMCP_API FFindActorsByNameAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("find_actors_by_name"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FSpawnActorAction
 * Spawns a basic actor type in the level.
 */
class UEEDITORMCP_API FSpawnActorAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("spawn_actor"); }

private:
	UClass* ResolveActorClass(const FString& TypeName) const;
};


/**
 * FDeleteActorAction
 * Deletes an actor from the level.
 */
class UEEDITORMCP_API FDeleteActorAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("delete_actor"); }
};


/**
 * FSetActorTransformAction
 * Sets the transform (location/rotation/scale) of an actor.
 */
class UEEDITORMCP_API FSetActorTransformAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("set_actor_transform"); }
};


/**
 * FGetActorPropertiesAction
 * Gets all properties of an actor.
 * Params: name (string), detailed (bool, default false), editable_only (bool, default false), category (string)
 * When detailed=true, enumerates ALL FProperty fields including Blueprint variables.
 */
class UEEDITORMCP_API FGetActorPropertiesAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("get_actor_properties"); }
	virtual bool RequiresSave() const override { return false; }

private:
	/** Serialize a single FProperty value to JSON representation */
	static TSharedPtr<FJsonValue> PropertyValueToJson(FProperty* Property, const void* ValuePtr);

	/** Get a human-readable type string for a property */
	static FString GetPropertyTypeString(FProperty* Property);
};


/**
 * FSetActorPropertyAction
 * Sets a property on an actor.
 */
class UEEDITORMCP_API FSetActorPropertyAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("set_actor_property"); }
};


/**
 * FFocusViewportAction
 * Focuses the viewport on an actor or location.
 */
class UEEDITORMCP_API FFocusViewportAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("focus_viewport"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FGetViewportTransformAction
 * Gets the current viewport camera location and rotation.
 */
class UEEDITORMCP_API FGetViewportTransformAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override { return true; }
	virtual FString GetActionName() const override { return TEXT("get_viewport_transform"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FSetViewportTransformAction
 * Sets the viewport camera location and/or rotation.
 */
class UEEDITORMCP_API FSetViewportTransformAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("set_viewport_transform"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FSaveAllAction
 * Saves all dirty packages (blueprints, levels, assets).
 */
class UEEDITORMCP_API FSaveAllAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override { return true; }
	virtual FString GetActionName() const override { return TEXT("save_all"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FListAssetsAction
 * Enumerate assets under a Content path with optional class/name filtering.
 * Params: path (e.g. "/Game/UI"), recursive (bool), class_filter (string),
 *         name_contains (string), max_results (int)
 */
class UEEDITORMCP_API FListAssetsAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("list_assets"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FRenameAssetsAction
 * Rename one or more assets and optionally fix redirectors automatically.
 *
 * Supported params:
 * 1) Single rename:
 *    old_asset_path, new_package_path, new_name
 * 2) Batch rename:
 *    items: [{old_asset_path, new_package_path, new_name}, ...]
 *
 * Optional params:
 *    auto_fixup_redirectors (bool, default true)
 *    allow_ui_prompts (bool, default false)
 *    fixup_mode (string: "delete" | "leave" | "prompt", default "delete")
 *    checkout_dialog_prompt (bool, default false; ignored when allow_ui_prompts=false)
 */
class UEEDITORMCP_API FRenameAssetsAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("rename_assets"); }
};


/**
 * FGetSelectedAssetThumbnailAction
 * Returns base64-encoded PNG thumbnails for selected Content Browser assets,
 * or explicit asset path/id lists if provided.
 * Params:
 *   asset_path (string, optional) - one full asset path
 *   asset_paths (string[], optional) - multiple full asset paths
 *   asset_ids (string[], optional) - alias of asset_paths
 *   ids (string[], optional) - alias of asset_paths
 *   size (int, optional, default 256, clamp 1..256)
 */
class UEEDITORMCP_API FGetSelectedAssetThumbnailAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("get_selected_asset_thumbnail"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FGetBlueprintSummaryAction
 * Get a comprehensive summary of a Blueprint's internal implementation:
 * variables, functions, event graphs, components, parent class, compile status, etc.
 * Params: blueprint_name (string) or asset_path (string)
 */
class UEEDITORMCP_API FGetBlueprintSummaryAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("get_blueprint_summary"); }
	virtual bool RequiresSave() const override { return false; }
};


// =========================================================================
// P2 Actions
// =========================================================================

/**
 * FGetEditorLogsAction
 * Returns recent editor log entries from the MCPLogCapture ring buffer.
 * Params: count (int, default 100), category (string), min_verbosity (string)
 */
class UEEDITORMCP_API FGetEditorLogsAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override { return true; }
	virtual FString GetActionName() const override { return TEXT("get_editor_logs"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FGetUnrealLogsAction
 * Returns live log lines from FMCPLogCapture using seq cursor + byte/line limits.
 * Params:
 *   cursor ("live:<seq>")
 *   tail_lines (int, default 200, clamp 20..2000)
 *   max_bytes (int, default 65536, clamp 8192..1048576)
 *   include_meta (bool, default true)
 *   require_recent (bool, default false)
 *   recent_window_seconds (double, default 2.0)
 *   filter_min_verbosity (string)
 *   filter_contains (string)
 *   filter_category (string) / filter_categories (string[])
 */
class UEEDITORMCP_API FGetUnrealLogsAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override { return true; }
	virtual FString GetActionName() const override { return TEXT("get_unreal_logs"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FBatchExecuteAction
 * Executes multiple commands in sequence within a single TCP request.
 * Params: commands (array of {type, params}), stop_on_error (bool, default true)
 */
class UEEDITORMCP_API FBatchExecuteAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("batch_execute"); }

private:
	/** Max commands per batch (C++ side limit) */
	static constexpr int32 MaxBatchSize = 50;
};


/**
 * FEditorIsReadyAction
 * Checks whether the editor has fully initialized and is ready for use.
 * Returns: ready (bool), details about startup state.
 * No params required.
 */
class UEEDITORMCP_API FEditorIsReadyAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override { return true; }
	virtual FString GetActionName() const override { return TEXT("is_ready"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FRequestEditorShutdownAction
 * Requests the editor to shut down gracefully via FGenericPlatformMisc::RequestExit.
 * Params: force (bool, default false) - if true, force-exits without save prompts.
 */
class UEEDITORMCP_API FRequestEditorShutdownAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override { return true; }
	virtual FString GetActionName() const override { return TEXT("request_shutdown"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FDescribeFullAction
 * Returns a comprehensive single-call snapshot of an entire Blueprint:
 * summary (parent class, variables, components, interfaces, compile status) +
 * all graph topologies (EventGraph + function graphs + macro graphs) with
 * compact node/edge serialization.
 *
 * Replaces the need for: 1x blueprint.get_summary + Nx graph.describe calls.
 *
 * Params:
 *   blueprint_name (string) or asset_path (string)
 *   include_pin_details (bool, default false) - if true, serialize full FEdGraphPinType
 *   include_function_signatures (bool, default false) - if true, inline function signatures
 *
 * Command: describe_blueprint_full
 * Action ID: blueprint.describe_full
 */
class UEEDITORMCP_API FDescribeFullAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("describe_blueprint_full"); }
	virtual bool RequiresSave() const override { return false; }

private:
	/** Serialize a single graph's topology in compact or detailed mode */
	TSharedPtr<FJsonObject> SerializeGraph(UBlueprint* Blueprint, UEdGraph* Graph,
		bool bIncludePinDetails, bool bIncludeFunctionSignatures) const;

	/** Serialize a pin in compact mode (category + direction + connected + default only) */
	static TSharedPtr<FJsonObject> SerializePinCompact(const UEdGraphPin* Pin);
};


// =========================================================================
// P6: PIE Control Actions
// =========================================================================

/**
 * FStartPIEAction
 * Starts a Play In Editor session.
 * Params: mode (string: SelectedViewport|NewWindow|Simulate, default SelectedViewport)
 * Action ID: editor.start_pie
 */
class UEEDITORMCP_API FStartPIEAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("start_pie"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FStopPIEAction
 * Stops the current PIE session without closing the editor.
 * Action ID: editor.stop_pie
 */
class UEEDITORMCP_API FStopPIEAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override { return true; }
	virtual FString GetActionName() const override { return TEXT("stop_pie"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FGetPIEStateAction
 * Queries the current PIE session state: Running/Stopped, world name, duration, paused.
 * Action ID: editor.get_pie_state
 */
class UEEDITORMCP_API FGetPIEStateAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override { return true; }
	virtual FString GetActionName() const override { return TEXT("get_pie_state"); }
	virtual bool RequiresSave() const override { return false; }
};


// =========================================================================
// P6: Log Enhancement Actions
// =========================================================================

/**
 * FClearLogsAction
 * Clears the MCPLogCapture ring buffer. Optionally inserts a session tag marker before clearing.
 * Params: tag (string, optional)
 * Action ID: editor.clear_logs
 */
class UEEDITORMCP_API FClearLogsAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override { return true; }
	virtual FString GetActionName() const override { return TEXT("clear_logs"); }
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FAssertLogAction
 * Assertion-based log validation. Checks log entries for keyword occurrences
 * and returns pass/fail for each assertion.
 * Params: assertions (array of {keyword, expected_count, comparison, category}),
 *         since_cursor (string, optional "live:<seq>")
 * Action ID: editor.assert_log
 */
class UEEDITORMCP_API FAssertLogAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("assert_log"); }
	virtual bool RequiresSave() const override { return false; }
};


// =========================================================================
// P6: Outliner Management Actions
// =========================================================================

/**
 * FRenameActorLabelAction
 * Renames an actor's display label in the World Outliner.
 * Params: actor_name (string), new_label (string) — or items[] for batch.
 * Action ID: editor.rename_actor_label
 */
class UEEDITORMCP_API FRenameActorLabelAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("rename_actor_label"); }

private:
	/** Find actor by name or label in the editor world */
	AActor* FindActorByName(UWorld* World, const FString& ActorName) const;
};


/**
 * FSetActorFolderAction
 * Moves actors into Outliner folders (creates folders automatically).
 * Params: actor_name (string), folder_path (string) — or items[] for batch.
 * Action ID: editor.set_actor_folder
 */
class UEEDITORMCP_API FSetActorFolderAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("set_actor_folder"); }

private:
	AActor* FindActorByName(UWorld* World, const FString& ActorName) const;
};


/**
 * FSelectActorsAction
 * Selects/deselects actors in the editor World Outliner.
 * Params: actor_names (string[]), mode (set|add|remove|toggle, default set)
 * Action ID: editor.select_actors
 */
class UEEDITORMCP_API FSelectActorsAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("select_actors"); }
	virtual bool RequiresSave() const override { return false; }

private:
	AActor* FindActorByName(UWorld* World, const FString& ActorName) const;
};


/**
 * FGetOutlinerTreeAction
 * Returns the actor hierarchy organized by Outliner folders.
 * Params: class_filter (string, optional), folder_filter (string, optional)
 * Action ID: editor.get_outliner_tree
 */
class UEEDITORMCP_API FGetOutlinerTreeAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override { return true; }
	virtual FString GetActionName() const override { return TEXT("get_outliner_tree"); }
	virtual bool RequiresSave() const override { return false; }
};


// =========================================================================
// P7: Asset Editor Actions
// =========================================================================

/**
 * FOpenAssetEditorAction
 * Opens the asset editor for a given asset and optionally brings it to focus.
 * Params:
 *   asset_path (string, required) - Full content path, e.g. "/Game/Characters/BP_Hero"
 *   focus (bool, default true) - Whether to focus the editor window after opening
 * Action ID: editor.open_asset_editor
 */
class UEEDITORMCP_API FOpenAssetEditorAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("open_asset_editor"); }
	virtual bool RequiresSave() const override { return false; }
};
