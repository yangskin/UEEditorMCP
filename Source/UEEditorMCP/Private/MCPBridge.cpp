// Copyright (c) 2025 zolnoor. All rights reserved.

#include "MCPBridge.h"
#include "MCPServer.h"
#include "Actions/EditorAction.h"
#include "Actions/BlueprintActions.h"
#include "Actions/EditorActions.h"
#include "Actions/NodeActions.h"
#include "Actions/GraphActions.h"
#include "Actions/ProjectActions.h"
#include "Actions/UMGActions.h"
#include "Actions/MaterialActions.h"
#include "Actions/LayoutActions.h"
#include "Actions/EditorDiffActions.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Blueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"

UMCPBridge::UMCPBridge()
	: Server(nullptr)
{
}

void UMCPBridge::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Bridge initializing"));

	// Register action handlers
	RegisterActions();

	// Start the TCP server
	Server = new FMCPServer(this, DefaultPort);
	if (Server->Start())
	{
		UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Server started on port %d"), DefaultPort);
	}
	else
	{
		UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Failed to start server"));
	}
}

void UMCPBridge::Deinitialize()
{
	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Bridge deinitializing"));

	// Stop the server
	if (Server)
	{
		Server->Stop();
		delete Server;
		Server = nullptr;
	}

	// Clear action handlers
	ActionHandlers.Empty();

	Super::Deinitialize();
}

void UMCPBridge::RegisterActions()
{
	// =========================================================================
	// Blueprint Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("create_blueprint"), MakeShared<FCreateBlueprintAction>());
	ActionHandlers.Add(TEXT("compile_blueprint"), MakeShared<FCompileBlueprintAction>());
	ActionHandlers.Add(TEXT("add_component_to_blueprint"), MakeShared<FAddComponentToBlueprintAction>());
	ActionHandlers.Add(TEXT("spawn_blueprint_actor"), MakeShared<FSpawnBlueprintActorAction>());
	ActionHandlers.Add(TEXT("set_component_property"), MakeShared<FSetComponentPropertyAction>());
	ActionHandlers.Add(TEXT("set_static_mesh_properties"), MakeShared<FSetStaticMeshPropertiesAction>());
	ActionHandlers.Add(TEXT("set_physics_properties"), MakeShared<FSetPhysicsPropertiesAction>());
	ActionHandlers.Add(TEXT("set_blueprint_property"), MakeShared<FSetBlueprintPropertyAction>());
	ActionHandlers.Add(TEXT("create_colored_material"), MakeShared<FCreateColoredMaterialAction>());
	ActionHandlers.Add(TEXT("set_blueprint_parent_class"), MakeShared<FSetBlueprintParentClassAction>());
	ActionHandlers.Add(TEXT("add_blueprint_interface"), MakeShared<FAddBlueprintInterfaceAction>());
	ActionHandlers.Add(TEXT("remove_blueprint_interface"), MakeShared<FRemoveBlueprintInterfaceAction>());

	// =========================================================================
	// Editor Actions (actors, viewport, save)
	// =========================================================================
	ActionHandlers.Add(TEXT("get_actors_in_level"), MakeShared<FGetActorsInLevelAction>());
	ActionHandlers.Add(TEXT("find_actors_by_name"), MakeShared<FFindActorsByNameAction>());
	ActionHandlers.Add(TEXT("spawn_actor"), MakeShared<FSpawnActorAction>());
	ActionHandlers.Add(TEXT("delete_actor"), MakeShared<FDeleteActorAction>());
	ActionHandlers.Add(TEXT("set_actor_transform"), MakeShared<FSetActorTransformAction>());
	ActionHandlers.Add(TEXT("get_actor_properties"), MakeShared<FGetActorPropertiesAction>());
	ActionHandlers.Add(TEXT("set_actor_property"), MakeShared<FSetActorPropertyAction>());
	ActionHandlers.Add(TEXT("focus_viewport"), MakeShared<FFocusViewportAction>());
	ActionHandlers.Add(TEXT("get_viewport_transform"), MakeShared<FGetViewportTransformAction>());
	ActionHandlers.Add(TEXT("set_viewport_transform"), MakeShared<FSetViewportTransformAction>());
	ActionHandlers.Add(TEXT("save_all"), MakeShared<FSaveAllAction>());
	ActionHandlers.Add(TEXT("list_assets"), MakeShared<FListAssetsAction>());
	ActionHandlers.Add(TEXT("rename_assets"), MakeShared<FRenameAssetsAction>());
	ActionHandlers.Add(TEXT("get_selected_asset_thumbnail"), MakeShared<FGetSelectedAssetThumbnailAction>());
	ActionHandlers.Add(TEXT("get_blueprint_summary"), MakeShared<FGetBlueprintSummaryAction>());
	ActionHandlers.Add(TEXT("describe_blueprint_full"), MakeShared<FDescribeFullAction>());
	ActionHandlers.Add(TEXT("get_editor_logs"), MakeShared<FGetEditorLogsAction>());
	ActionHandlers.Add(TEXT("get_unreal_logs"), MakeShared<FGetUnrealLogsAction>());
	ActionHandlers.Add(TEXT("batch_execute"), MakeShared<FBatchExecuteAction>());
	ActionHandlers.Add(TEXT("is_ready"), MakeShared<FEditorIsReadyAction>());
	ActionHandlers.Add(TEXT("request_shutdown"), MakeShared<FRequestEditorShutdownAction>());

	// =========================================================================
	// Layout Actions - Auto-arrange Blueprint graph nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("auto_layout_selected"), MakeShared<FAutoLayoutSelectedAction>());
	ActionHandlers.Add(TEXT("auto_layout_subtree"), MakeShared<FAutoLayoutSubtreeAction>());
	ActionHandlers.Add(TEXT("auto_layout_blueprint"), MakeShared<FAutoLayoutBlueprintAction>());
	ActionHandlers.Add(TEXT("layout_and_comment"), MakeShared<FLayoutAndCommentAction>());

	// =========================================================================
	// Node Actions - Graph Operations
	// =========================================================================
	ActionHandlers.Add(TEXT("connect_blueprint_nodes"), MakeShared<FConnectBlueprintNodesAction>());
	ActionHandlers.Add(TEXT("find_blueprint_nodes"), MakeShared<FFindBlueprintNodesAction>());
	ActionHandlers.Add(TEXT("delete_blueprint_node"), MakeShared<FDeleteBlueprintNodeAction>());
	ActionHandlers.Add(TEXT("get_node_pins"), MakeShared<FGetNodePinsAction>());
	ActionHandlers.Add(TEXT("describe_graph"), MakeShared<FDescribeGraphAction>());
	ActionHandlers.Add(TEXT("get_selected_nodes"), MakeShared<FGetSelectedNodesAction>());
	ActionHandlers.Add(TEXT("collapse_selection_to_function"), MakeShared<FCollapseSelectionToFunctionAction>());
	ActionHandlers.Add(TEXT("collapse_selection_to_macro"), MakeShared<FCollapseSelectionToMacroAction>());
	ActionHandlers.Add(TEXT("set_selected_nodes"), MakeShared<FSetSelectedNodesAction>());
	ActionHandlers.Add(TEXT("batch_select_and_act"), MakeShared<FBatchSelectAndActAction>());

	// =========================================================================
	// Node Actions - Event Nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("add_blueprint_event_node"), MakeShared<FAddBlueprintEventNodeAction>());
	ActionHandlers.Add(TEXT("add_blueprint_input_action_node"), MakeShared<FAddBlueprintInputActionNodeAction>());
	ActionHandlers.Add(TEXT("add_enhanced_input_action_node"), MakeShared<FAddEnhancedInputActionNodeAction>());
	ActionHandlers.Add(TEXT("add_blueprint_custom_event"), MakeShared<FAddBlueprintCustomEventAction>());
	ActionHandlers.Add(TEXT("add_custom_event_for_delegate"), MakeShared<FAddCustomEventForDelegateAction>());

	// =========================================================================
	// Node Actions - Variable Nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("add_blueprint_variable"), MakeShared<FAddBlueprintVariableAction>());
	ActionHandlers.Add(TEXT("add_blueprint_variable_get"), MakeShared<FAddBlueprintVariableGetAction>());
	ActionHandlers.Add(TEXT("add_blueprint_variable_set"), MakeShared<FAddBlueprintVariableSetAction>());
	ActionHandlers.Add(TEXT("set_node_pin_default"), MakeShared<FSetNodePinDefaultAction>());

	// =========================================================================
	// Node Actions - Function Nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("add_blueprint_function_node"), MakeShared<FAddBlueprintFunctionNodeAction>());
	ActionHandlers.Add(TEXT("add_blueprint_self_reference"), MakeShared<FAddBlueprintSelfReferenceAction>());
	ActionHandlers.Add(TEXT("add_blueprint_get_self_component_reference"), MakeShared<FAddBlueprintGetSelfComponentReferenceAction>());
	ActionHandlers.Add(TEXT("add_blueprint_branch_node"), MakeShared<FAddBlueprintBranchNodeAction>());
	ActionHandlers.Add(TEXT("add_blueprint_cast_node"), MakeShared<FAddBlueprintCastNodeAction>());
	ActionHandlers.Add(TEXT("add_blueprint_get_subsystem_node"), MakeShared<FAddBlueprintGetSubsystemNodeAction>());

	// =========================================================================
	// Node Actions - Blueprint Function Graph
	// =========================================================================
	ActionHandlers.Add(TEXT("create_blueprint_function"), MakeShared<FCreateBlueprintFunctionAction>());

	// =========================================================================
	// Node Actions - Event Dispatchers
	// =========================================================================
	ActionHandlers.Add(TEXT("add_event_dispatcher"), MakeShared<FAddEventDispatcherAction>());
	ActionHandlers.Add(TEXT("call_event_dispatcher"), MakeShared<FCallEventDispatcherAction>());
	ActionHandlers.Add(TEXT("bind_event_dispatcher"), MakeShared<FBindEventDispatcherAction>());
	ActionHandlers.Add(TEXT("create_event_delegate"), MakeShared<FCreateEventDelegateAction>());
	ActionHandlers.Add(TEXT("bind_component_event"), MakeShared<FBindComponentEventAction>());

	// =========================================================================
	// Node Actions - Spawn Actor Nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("add_spawn_actor_from_class_node"), MakeShared<FAddSpawnActorFromClassNodeAction>());
	ActionHandlers.Add(TEXT("call_blueprint_function"), MakeShared<FCallBlueprintFunctionAction>());

	// =========================================================================
	// Node Actions - External Object Property Nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("set_object_property"), MakeShared<FSetObjectPropertyAction>());

	// =========================================================================
	// Node Actions - Sequence Node
	// =========================================================================
	ActionHandlers.Add(TEXT("add_sequence_node"), MakeShared<FAddSequenceNodeAction>());

	// =========================================================================
	// Node Actions - Macro Instance Nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("add_macro_instance_node"), MakeShared<FAddMacroInstanceNodeAction>());

	// =========================================================================
	// Node Actions - Struct Nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("add_make_struct_node"), MakeShared<FAddMakeStructNodeAction>());
	ActionHandlers.Add(TEXT("add_break_struct_node"), MakeShared<FAddBreakStructNodeAction>());

	// =========================================================================
	// Node Actions - Switch Nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("add_switch_on_string_node"), MakeShared<FAddSwitchOnStringNodeAction>());
	ActionHandlers.Add(TEXT("add_switch_on_int_node"), MakeShared<FAddSwitchOnIntNodeAction>());
	ActionHandlers.Add(TEXT("add_function_local_variable"), MakeShared<FAddFunctionLocalVariableAction>());
	ActionHandlers.Add(TEXT("set_blueprint_variable_default"), MakeShared<FSetBlueprintVariableDefaultAction>());
	ActionHandlers.Add(TEXT("add_blueprint_comment"), MakeShared<FAddBlueprintCommentAction>());
	ActionHandlers.Add(TEXT("auto_comment"), MakeShared<FAutoCommentAction>());

	// =========================================================================
	// Node Actions - P1: Variable & Function Management
	// =========================================================================
	ActionHandlers.Add(TEXT("delete_blueprint_variable"), MakeShared<FDeleteBlueprintVariableAction>());
	ActionHandlers.Add(TEXT("rename_blueprint_variable"), MakeShared<FRenameBlueprintVariableAction>());
	ActionHandlers.Add(TEXT("set_variable_metadata"), MakeShared<FSetVariableMetadataAction>());
	ActionHandlers.Add(TEXT("delete_blueprint_function"), MakeShared<FDeleteBlueprintFunctionAction>());
	ActionHandlers.Add(TEXT("rename_blueprint_function"), MakeShared<FRenameBlueprintFunctionAction>());
	ActionHandlers.Add(TEXT("rename_blueprint_macro"), MakeShared<FRenameBlueprintMacroAction>());

	// =========================================================================
	// Node Actions - P2: Graph Operation Enhancements
	// =========================================================================
	ActionHandlers.Add(TEXT("disconnect_blueprint_pin"), MakeShared<FDisconnectBlueprintPinAction>());
	ActionHandlers.Add(TEXT("move_node"), MakeShared<FMoveNodeAction>());
	ActionHandlers.Add(TEXT("add_reroute_node"), MakeShared<FAddRerouteNodeAction>());

	// =========================================================================
	// P3: Graph Patch System
	// =========================================================================
	ActionHandlers.Add(TEXT("describe_graph_enhanced"), MakeShared<FGraphDescribeEnhancedAction>());
	ActionHandlers.Add(TEXT("apply_graph_patch"), MakeShared<FApplyPatchAction>());
	ActionHandlers.Add(TEXT("validate_graph_patch"), MakeShared<FValidatePatchAction>());

	// =========================================================================
	// P4: Cross-Graph Node Transfer
	// =========================================================================
	ActionHandlers.Add(TEXT("export_nodes_to_text"), MakeShared<FExportNodesToTextAction>());
	ActionHandlers.Add(TEXT("import_nodes_from_text"), MakeShared<FImportNodesFromTextAction>());

	// =========================================================================
	// Project Actions (Input Mappings, Enhanced Input)
	// =========================================================================
	ActionHandlers.Add(TEXT("create_input_mapping"), MakeShared<FCreateInputMappingAction>());
	ActionHandlers.Add(TEXT("create_input_action"), MakeShared<FCreateInputActionAction>());
	ActionHandlers.Add(TEXT("create_input_mapping_context"), MakeShared<FCreateInputMappingContextAction>());
	ActionHandlers.Add(TEXT("add_key_mapping_to_context"), MakeShared<FAddKeyMappingToContextAction>());

	// =========================================================================
	// UMG Actions (Widget Blueprints)
	// =========================================================================
	ActionHandlers.Add(TEXT("create_umg_widget_blueprint"), MakeShared<FCreateUMGWidgetBlueprintAction>());
	ActionHandlers.Add(TEXT("add_text_block_to_widget"), MakeShared<FAddTextBlockToWidgetAction>());
	ActionHandlers.Add(TEXT("add_button_to_widget"), MakeShared<FAddButtonToWidgetAction>());
	ActionHandlers.Add(TEXT("add_image_to_widget"), MakeShared<FAddImageToWidgetAction>());
	ActionHandlers.Add(TEXT("add_border_to_widget"), MakeShared<FAddBorderToWidgetAction>());
	ActionHandlers.Add(TEXT("add_overlay_to_widget"), MakeShared<FAddOverlayToWidgetAction>());
	ActionHandlers.Add(TEXT("add_horizontal_box_to_widget"), MakeShared<FAddHorizontalBoxToWidgetAction>());
	ActionHandlers.Add(TEXT("add_vertical_box_to_widget"), MakeShared<FAddVerticalBoxToWidgetAction>());
	ActionHandlers.Add(TEXT("add_slider_to_widget"), MakeShared<FAddSliderToWidgetAction>());
	ActionHandlers.Add(TEXT("add_progress_bar_to_widget"), MakeShared<FAddProgressBarToWidgetAction>());
	ActionHandlers.Add(TEXT("add_size_box_to_widget"), MakeShared<FAddSizeBoxToWidgetAction>());
	ActionHandlers.Add(TEXT("add_scale_box_to_widget"), MakeShared<FAddScaleBoxToWidgetAction>());
	ActionHandlers.Add(TEXT("add_canvas_panel_to_widget"), MakeShared<FAddCanvasPanelToWidgetAction>());
	ActionHandlers.Add(TEXT("add_combo_box_to_widget"), MakeShared<FAddComboBoxToWidgetAction>());
	ActionHandlers.Add(TEXT("add_check_box_to_widget"), MakeShared<FAddCheckBoxToWidgetAction>());
	ActionHandlers.Add(TEXT("add_spin_box_to_widget"), MakeShared<FAddSpinBoxToWidgetAction>());
	ActionHandlers.Add(TEXT("add_editable_text_box_to_widget"), MakeShared<FAddEditableTextBoxToWidgetAction>());
	ActionHandlers.Add(TEXT("bind_widget_event"), MakeShared<FBindWidgetEventAction>());
	ActionHandlers.Add(TEXT("add_widget_to_viewport"), MakeShared<FAddWidgetToViewportAction>());
	ActionHandlers.Add(TEXT("set_text_block_binding"), MakeShared<FSetTextBlockBindingAction>());
	ActionHandlers.Add(TEXT("list_widget_components"), MakeShared<FListWidgetComponentsAction>());
	ActionHandlers.Add(TEXT("reparent_widgets"), MakeShared<FReparentWidgetsAction>());
	ActionHandlers.Add(TEXT("set_widget_properties"), MakeShared<FSetWidgetPropertiesAction>());
	ActionHandlers.Add(TEXT("get_widget_tree"), MakeShared<FGetWidgetTreeAction>());
	ActionHandlers.Add(TEXT("delete_widget_from_blueprint"), MakeShared<FDeleteWidgetFromBlueprintAction>());
	ActionHandlers.Add(TEXT("rename_widget_in_blueprint"), MakeShared<FRenameWidgetInBlueprintAction>());
	ActionHandlers.Add(TEXT("add_widget_child"), MakeShared<FAddWidgetChildAction>());
	ActionHandlers.Add(TEXT("delete_umg_widget_blueprint"), MakeShared<FDeleteUMGWidgetBlueprintAction>());
	ActionHandlers.Add(TEXT("set_combo_box_options"), MakeShared<FSetComboBoxOptionsAction>());
	ActionHandlers.Add(TEXT("set_widget_text"), MakeShared<FSetWidgetTextAction>());
	ActionHandlers.Add(TEXT("set_slider_properties"), MakeShared<FSetSliderPropertiesAction>());
	ActionHandlers.Add(TEXT("add_generic_widget_to_widget"), MakeShared<FAddGenericWidgetAction>());

	// MVVM Actions
	ActionHandlers.Add(TEXT("mvvm_add_viewmodel"), MakeShared<FMVVMAddViewModelAction>());
	ActionHandlers.Add(TEXT("mvvm_add_binding"), MakeShared<FMVVMAddBindingAction>());
	ActionHandlers.Add(TEXT("mvvm_get_bindings"), MakeShared<FMVVMGetBindingsAction>());
	ActionHandlers.Add(TEXT("mvvm_remove_binding"), MakeShared<FMVVMRemoveBindingAction>());
	ActionHandlers.Add(TEXT("mvvm_remove_viewmodel"), MakeShared<FMVVMRemoveViewModelAction>());

	// =========================================================================
	// Material Actions (Materials, Shaders, Post-Process)
	// =========================================================================
	ActionHandlers.Add(TEXT("create_material"), MakeShared<FCreateMaterialAction>());
	ActionHandlers.Add(TEXT("set_material_property"), MakeShared<FSetMaterialPropertyAction>());
	ActionHandlers.Add(TEXT("add_material_expression"), MakeShared<FAddMaterialExpressionAction>());
	ActionHandlers.Add(TEXT("connect_material_expressions"), MakeShared<FConnectMaterialExpressionsAction>());
	ActionHandlers.Add(TEXT("connect_to_material_output"), MakeShared<FConnectToMaterialOutputAction>());
	ActionHandlers.Add(TEXT("set_material_expression_property"), MakeShared<FSetMaterialExpressionPropertyAction>());
	ActionHandlers.Add(TEXT("compile_material"), MakeShared<FCompileMaterialAction>());
	ActionHandlers.Add(TEXT("create_material_instance"), MakeShared<FCreateMaterialInstanceAction>());
	ActionHandlers.Add(TEXT("create_post_process_volume"), MakeShared<FCreatePostProcessVolumeAction>());
	// Phase 4 Material Actions
	ActionHandlers.Add(TEXT("get_material_summary"), MakeShared<FGetMaterialSummaryAction>());
	ActionHandlers.Add(TEXT("remove_material_expression"), MakeShared<FRemoveMaterialExpressionAction>());
	ActionHandlers.Add(TEXT("auto_layout_material"), MakeShared<FAutoLayoutMaterialAction>());
	ActionHandlers.Add(TEXT("auto_comment_material"), MakeShared<FAutoCommentMaterialAction>());
	ActionHandlers.Add(TEXT("get_material_selected_nodes"), MakeShared<FGetMaterialSelectedNodesAction>());
	// Phase 5 Material Actions
	ActionHandlers.Add(TEXT("apply_material_to_component"), MakeShared<FApplyMaterialToComponentAction>());
	ActionHandlers.Add(TEXT("apply_material_to_actor"), MakeShared<FApplyMaterialToActorAction>());
	ActionHandlers.Add(TEXT("refresh_material_editor"), MakeShared<FRefreshMaterialEditorAction>());

	// =========================================================================
	// Diff Actions (Source Control)
	// =========================================================================
	ActionHandlers.Add(TEXT("diff_against_depot"), MakeShared<FDiffAgainstDepotAction>());

	// =========================================================================
	// P6: PIE Control Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("start_pie"), MakeShared<FStartPIEAction>());
	ActionHandlers.Add(TEXT("stop_pie"), MakeShared<FStopPIEAction>());
	ActionHandlers.Add(TEXT("get_pie_state"), MakeShared<FGetPIEStateAction>());

	// =========================================================================
	// P6: Log Enhancement Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("clear_logs"), MakeShared<FClearLogsAction>());
	ActionHandlers.Add(TEXT("assert_log"), MakeShared<FAssertLogAction>());

	// =========================================================================
	// P6: Outliner Management Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("rename_actor_label"), MakeShared<FRenameActorLabelAction>());
	ActionHandlers.Add(TEXT("set_actor_folder"), MakeShared<FSetActorFolderAction>());
	ActionHandlers.Add(TEXT("select_actors"), MakeShared<FSelectActorsAction>());
	ActionHandlers.Add(TEXT("get_outliner_tree"), MakeShared<FGetOutlinerTreeAction>());

	// =========================================================================
	// P7: Asset Editor Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("open_asset_editor"), MakeShared<FOpenAssetEditorAction>());

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Registered %d action handlers"), ActionHandlers.Num());
}

TSharedRef<FEditorAction>* UMCPBridge::FindAction(const FString& CommandType)
{
	return ActionHandlers.Find(CommandType);
}

TSharedPtr<FJsonObject> UMCPBridge::ExecuteCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	// =========================================================================
	// Action Handlers (modular actions - check these first)
	// =========================================================================
	TSharedRef<FEditorAction>* ActionPtr = FindAction(CommandType);
	if (ActionPtr)
	{
		return (*ActionPtr)->Execute(Params, Context);
	}

	// =========================================================================
	// Unknown Command (all handlers should be registered as actions now)
	// =========================================================================
	return CreateErrorResponse(
		FString::Printf(TEXT("Unknown command type: %s"), *CommandType),
		TEXT("unknown_command")
	);
}

TSharedPtr<FJsonObject> UMCPBridge::ExecuteCommandSafe(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	// Phase 2: Top-level C++ exception guard around command execution.
	// SEH is handled per-action inside FEditorAction::ExecuteWithCrashProtection.
	try
	{
		return ExecuteCommandInternal(CommandType, Params);
	}
	catch (const std::exception& Ex)
	{
		UE_LOG(LogMCP, Error, TEXT("C++ exception in command '%s': %hs"), *CommandType, Ex.what());
		return CreateErrorResponse(
			FString::Printf(TEXT("C++ exception: %hs"), Ex.what()),
			TEXT("cpp_exception")
		);
	}
	catch (...)
	{
		UE_LOG(LogMCP, Error, TEXT("Unknown C++ exception in command '%s'"), *CommandType);
		return CreateErrorResponse(TEXT("Unknown C++ exception"), TEXT("cpp_exception"));
	}
}

TSharedPtr<FJsonObject> UMCPBridge::ExecuteCommandInternal(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	return ExecuteCommand(CommandType, Params);
}

TSharedPtr<FJsonObject> UMCPBridge::CreateSuccessResponse(const TSharedPtr<FJsonObject>& ResultData)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("status"), TEXT("success"));
	Response->SetBoolField(TEXT("success"), true);

	if (ResultData.IsValid())
	{
		Response->SetObjectField(TEXT("result"), ResultData);
	}
	else
	{
		Response->SetObjectField(TEXT("result"), MakeShared<FJsonObject>());
	}

	return Response;
}

TSharedPtr<FJsonObject> UMCPBridge::CreateErrorResponse(const FString& ErrorMessage, const FString& ErrorType)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("status"), TEXT("error"));
	Response->SetBoolField(TEXT("success"), false);
	Response->SetStringField(TEXT("error"), ErrorMessage);
	Response->SetStringField(TEXT("error_type"), ErrorType);

	return Response;
}
