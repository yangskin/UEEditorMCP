// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/NodeActions.h"
#include "MCPCommonUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_InputAction.h"
#include "K2Node_Self.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_EnhancedInputAction.h"
#include "K2Node_GetSubsystem.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_ExecutionSequence.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/EngineSubsystem.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "GameFramework/GameUserSettings.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundClass.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_SwitchString.h"
#include "K2Node_SwitchInteger.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "EdGraphNode_Comment.h"
#include "K2Node_Knot.h"
#include "Actions/LayoutActions.h"
#include "GraphEditorActions.h"
#include "MCPBridge.h"
#include "GraphEditor.h"

// ============================================================================
// Graph Operations (connect, find, delete, inspect)
// ============================================================================

bool FConnectBlueprintNodesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString SourceNodeId, TargetNodeId, SourcePin, TargetPin;
	if (!GetRequiredString(Params, TEXT("source_node_id"), SourceNodeId, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("target_node_id"), TargetNodeId, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("source_pin"), SourcePin, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("target_pin"), TargetPin, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FConnectBlueprintNodesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SourceNodeId = Params->GetStringField(TEXT("source_node_id"));
	FString TargetNodeId = Params->GetStringField(TEXT("target_node_id"));
	FString SourcePinName = Params->GetStringField(TEXT("source_pin"));
	FString TargetPinName = Params->GetStringField(TEXT("target_pin"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the nodes
	UEdGraphNode* SourceNode = nullptr;
	UEdGraphNode* TargetNode = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node->NodeGuid.ToString() == SourceNodeId)
		{
			SourceNode = Node;
		}
		else if (Node->NodeGuid.ToString() == TargetNodeId)
		{
			TargetNode = Node;
		}
	}

	if (!SourceNode || !TargetNode)
	{
		return CreateErrorResponse(TEXT("Source or target node not found"));
	}

	// Find pins and provide detailed error messages
	UEdGraphPin* SourcePin = FMCPCommonUtils::FindPin(SourceNode, SourcePinName, EGPD_Output);
	UEdGraphPin* TargetPin = FMCPCommonUtils::FindPin(TargetNode, TargetPinName, EGPD_Input);

	auto GetAvailablePins = [](UEdGraphNode* Node, EEdGraphPinDirection Direction) -> FString
	{
		TArray<FString> PinNames;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == Direction && !Pin->bHidden)
			{
				PinNames.Add(FString::Printf(TEXT("'%s' (%s)"),
					*Pin->PinName.ToString(),
					*Pin->PinType.PinCategory.ToString()));
			}
		}
		return FString::Join(PinNames, TEXT(", "));
	};

	if (!SourcePin)
	{
		FString AvailablePins = GetAvailablePins(SourceNode, EGPD_Output);
		return CreateErrorResponse(FString::Printf(
			TEXT("Source pin '%s' not found on node. Available OUTPUT pins: [%s]"),
			*SourcePinName, *AvailablePins));
	}

	if (!TargetPin)
	{
		FString AvailablePins = GetAvailablePins(TargetNode, EGPD_Input);
		return CreateErrorResponse(FString::Printf(
			TEXT("Target pin '%s' not found on node. Available INPUT pins: [%s]"),
			*TargetPinName, *AvailablePins));
	}

	// Connect using the schema
	const UEdGraphSchema* Schema = TargetGraph->GetSchema();
	if (Schema)
	{
		bool bResult = Schema->TryCreateConnection(SourcePin, TargetPin);
		if (bResult)
		{
			SourceNode->PinConnectionListChanged(SourcePin);
			TargetNode->PinConnectionListChanged(TargetPin);
			MarkBlueprintModified(Blueprint, Context);

			TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
			ResultData->SetStringField(TEXT("source_node_id"), SourceNodeId);
			ResultData->SetStringField(TEXT("target_node_id"), TargetNodeId);
			return CreateSuccessResponse(ResultData);
		}
		else
		{
			return CreateErrorResponse(FString::Printf(
				TEXT("Schema refused connection: '%s' (%s) -> '%s' (%s). Types may be incompatible."),
				*SourcePin->PinName.ToString(), *SourcePin->PinType.PinCategory.ToString(),
				*TargetPin->PinName.ToString(), *TargetPin->PinType.PinCategory.ToString()));
		}
	}

	return CreateErrorResponse(TEXT("Failed to get graph schema"));
}


bool FFindBlueprintNodesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FFindBlueprintNodesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString NodeType = GetOptionalString(Params, TEXT("node_type"));
	FString EventName = GetOptionalString(Params, TEXT("event_name"));

	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	TArray<TSharedPtr<FJsonValue>> NodesArray;

	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (!Node) continue;

		bool bMatch = false;

		if (NodeType.IsEmpty())
		{
			// No filter - include all nodes
			bMatch = true;
		}
		else if (NodeType == TEXT("Event"))
		{
			UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
			if (EventNode)
			{
				if (EventName.IsEmpty())
				{
					bMatch = true;
				}
				else if (EventNode->EventReference.GetMemberName() == FName(*EventName))
				{
					bMatch = true;
				}
			}
			UK2Node_CustomEvent* CustomNode = Cast<UK2Node_CustomEvent>(Node);
			if (CustomNode)
			{
				if (EventName.IsEmpty())
				{
					bMatch = true;
				}
				else if (CustomNode->CustomFunctionName == EventName)
				{
					bMatch = true;
				}
			}
		}
		else if (NodeType == TEXT("Function"))
		{
			if (Cast<UK2Node_CallFunction>(Node))
			{
				bMatch = true;
			}
		}
		else if (NodeType == TEXT("Variable"))
		{
			if (Cast<UK2Node_VariableGet>(Node) || Cast<UK2Node_VariableSet>(Node))
			{
				bMatch = true;
			}
		}

		if (bMatch)
		{
			TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
			NodeObj->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());
			NodeObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
			NodeObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
		}
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetArrayField(TEXT("nodes"), NodesArray);
	ResultData->SetNumberField(TEXT("count"), NodesArray.Num());
	return CreateSuccessResponse(ResultData);
}


bool FDeleteBlueprintNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString NodeId;
	if (!GetRequiredString(Params, TEXT("node_id"), NodeId, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FDeleteBlueprintNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString NodeId = Params->GetStringField(TEXT("node_id"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the node
	UEdGraphNode* NodeToDelete = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node->NodeGuid.ToString() == NodeId)
		{
			NodeToDelete = Node;
			break;
		}
	}

	if (!NodeToDelete)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Node not found with ID: %s"), *NodeId));
	}

	FString NodeClass = NodeToDelete->GetClass()->GetName();
	FString NodeTitle = NodeToDelete->GetNodeTitle(ENodeTitleType::FullTitle).ToString();

	// Break all pin connections
	for (UEdGraphPin* Pin : NodeToDelete->Pins)
	{
		Pin->BreakAllPinLinks();
	}

	// Remove from graph
	TargetGraph->RemoveNode(NodeToDelete);
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("deleted_node_id"), NodeId);
	ResultData->SetStringField(TEXT("deleted_node_class"), NodeClass);
	ResultData->SetStringField(TEXT("deleted_node_title"), NodeTitle);
	return CreateSuccessResponse(ResultData);
}


bool FGetNodePinsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString NodeId;
	if (!GetRequiredString(Params, TEXT("node_id"), NodeId, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FGetNodePinsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString NodeId = Params->GetStringField(TEXT("node_id"));
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the node
	UEdGraphNode* FoundNode = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node->NodeGuid.ToString() == NodeId)
		{
			FoundNode = Node;
			break;
		}
	}

	if (!FoundNode)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Node not found with ID: %s"), *NodeId));
	}

	// Build array of pin info (including links)
	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (UEdGraphPin* Pin : FoundNode->Pins)
	{
		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
		PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
		PinObj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
		if (Pin->PinType.PinSubCategory != NAME_None)
		{
			PinObj->SetStringField(TEXT("sub_category"), Pin->PinType.PinSubCategory.ToString());
		}
		if (Pin->PinType.PinSubCategoryObject.Get())
		{
			PinObj->SetStringField(TEXT("sub_category_object"), Pin->PinType.PinSubCategoryObject->GetName());
		}
		PinObj->SetBoolField(TEXT("is_hidden"), Pin->bHidden);

		// Linked pins (node id + pin name)
		TArray<TSharedPtr<FJsonValue>> LinkedArray;
		for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (!LinkedPin) continue;
			UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
			if (!LinkedNode) continue;

			TSharedPtr<FJsonObject> LinkedObj = MakeShared<FJsonObject>();
			LinkedObj->SetStringField(TEXT("node_id"), LinkedNode->NodeGuid.ToString());
			LinkedObj->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());
			LinkedArray.Add(MakeShared<FJsonValueObject>(LinkedObj));
		}
		PinObj->SetArrayField(TEXT("linked_to"), LinkedArray);

		PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_class"), FoundNode->GetClass()->GetName());
	ResultData->SetArrayField(TEXT("pins"), PinsArray);
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Event Nodes
// ============================================================================

bool FAddBlueprintEventNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// Support both "event_name" (canonical) and "event_type" (alias)
	FString EventName;
	if (!Params->TryGetStringField(TEXT("event_name"), EventName))
	{
		if (!Params->TryGetStringField(TEXT("event_type"), EventName))
		{
			OutError = TEXT("Missing required parameter: event_name (or event_type)");
			return false;
		}
	}
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintEventNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	// Support both "event_name" (canonical) and "event_type" (alias)
	FString EventName;
	if (!Params->TryGetStringField(TEXT("event_name"), EventName))
	{
		Params->TryGetStringField(TEXT("event_type"), EventName);
	}
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* EventGraph = GetTargetGraph(Params, Context);

	// Reuse existing event nodes to avoid duplicate Tick/BeginPlay, etc.
	UK2Node_Event* ExistingEventNode = FMCPCommonUtils::FindExistingEventNode(EventGraph, EventName);
	if (!ExistingEventNode)
	{
		const FString NormalizedEventName = EventName.StartsWith(TEXT("Receive"))
			? EventName.Mid(7)
			: FString::Printf(TEXT("Receive%s"), *EventName);
		ExistingEventNode = FMCPCommonUtils::FindExistingEventNode(EventGraph, NormalizedEventName);
	}
	if (ExistingEventNode)
	{
		TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
		ResultData->SetStringField(TEXT("node_id"), ExistingEventNode->NodeGuid.ToString());
		ResultData->SetBoolField(TEXT("reused_existing"), true);
		return CreateSuccessResponse(ResultData);
	}

	UK2Node_Event* EventNode = FMCPCommonUtils::CreateEventNode(EventGraph, EventName, Position);
	if (!EventNode)
	{
		return CreateErrorResponse(TEXT("Failed to create event node"));
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(EventNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), EventNode->NodeGuid.ToString());
	ResultData->SetBoolField(TEXT("reused_existing"), false);
	return CreateSuccessResponse(ResultData);
}


bool FAddBlueprintInputActionNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ActionName;
	if (!GetRequiredString(Params, TEXT("action_name"), ActionName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintInputActionNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ActionName = Params->GetStringField(TEXT("action_name"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* EventGraph = FMCPCommonUtils::FindOrCreateEventGraph(Blueprint);

	UK2Node_InputAction* InputActionNode = FMCPCommonUtils::CreateInputActionNode(EventGraph, ActionName, Position);
	if (!InputActionNode)
	{
		return CreateErrorResponse(TEXT("Failed to create input action node"));
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(InputActionNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), InputActionNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FAddEnhancedInputActionNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ActionName;
	if (!GetRequiredString(Params, TEXT("action_name"), ActionName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddEnhancedInputActionNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ActionName = Params->GetStringField(TEXT("action_name"));
	FString ActionPath = GetOptionalString(Params, TEXT("action_path"), TEXT("/Game/Input"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* EventGraph = FMCPCommonUtils::FindOrCreateEventGraph(Blueprint);

	// Load the UInputAction asset
	FString AssetPath = FString::Printf(TEXT("%s/%s.%s"), *ActionPath, *ActionName, *ActionName);
	UInputAction* InputActionAsset = LoadObject<UInputAction>(nullptr, *AssetPath);
	if (!InputActionAsset)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Input Action asset not found: %s"), *AssetPath));
	}

	// Create the Enhanced Input Action node using editor's spawn API
	UK2Node_EnhancedInputAction* ActionNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_EnhancedInputAction>(
		EventGraph, Position, EK2NewNodeFlags::None,
		[InputActionAsset](UK2Node_EnhancedInputAction* Node) { Node->InputAction = InputActionAsset; }
	);

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(ActionNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), ActionNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FAddBlueprintCustomEventAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString EventName;
	if (!GetRequiredString(Params, TEXT("event_name"), EventName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintCustomEventAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString EventName = Params->GetStringField(TEXT("event_name"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* EventGraph = GetTargetGraph(Params, Context);

	// Create Custom Event node using editor's spawn API
	UK2Node_CustomEvent* CustomEventNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CustomEvent>(
		EventGraph, Position, EK2NewNodeFlags::None,
		[&EventName](UK2Node_CustomEvent* Node) { Node->CustomFunctionName = FName(*EventName); }
	);

	// Add parameters if provided
	const TArray<TSharedPtr<FJsonValue>>* ParametersArray = GetOptionalArray(Params, TEXT("parameters"));
	if (ParametersArray)
	{
		for (const TSharedPtr<FJsonValue>& ParamValue : *ParametersArray)
		{
			const TSharedPtr<FJsonObject>* ParamObj;
			if (ParamValue->TryGetObject(ParamObj) && ParamObj)
			{
				FString ParamName, ParamType;
				if ((*ParamObj)->TryGetStringField(TEXT("name"), ParamName) &&
					(*ParamObj)->TryGetStringField(TEXT("type"), ParamType))
				{
					FEdGraphPinType PinType;
					FString TypeResolveError;
					if (!FMCPCommonUtils::ResolvePinTypeFromString(ParamType, PinType, TypeResolveError))
					{
						UE_LOG(LogTemp, Warning, TEXT("CustomEvent param '%s': %s, defaulting to Float"), *ParamName, *TypeResolveError);
						PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
						PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
					}

					TSharedPtr<FUserPinInfo> NewPinInfo = MakeShared<FUserPinInfo>();
					NewPinInfo->PinName = FName(*ParamName);
					NewPinInfo->PinType = PinType;
					NewPinInfo->DesiredPinDirection = EGPD_Output;
					CustomEventNode->UserDefinedPins.Add(NewPinInfo);
				}
			}
		}
		CustomEventNode->ReconstructNode();
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(CustomEventNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), CustomEventNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("event_name"), EventName);
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Custom Event For Delegate (signature-matched)
// ============================================================================

UClass* FAddCustomEventForDelegateAction::ResolveClass(const FString& ClassName) const
{
	// Try as full path first
	UClass* FoundClass = FindObject<UClass>(nullptr, *ClassName);
	if (FoundClass) return FoundClass;

	// Strip U/A prefix for fallback search
	FString BaseName = ClassName;
	if ((BaseName.StartsWith(TEXT("U")) || BaseName.StartsWith(TEXT("A"))) && BaseName.Len() > 1)
	{
		BaseName = BaseName.Mid(1);
	}

	// Common module paths to try
	auto TryModule = [&](const TCHAR* Module) -> UClass*
	{
		FString Path = FString::Printf(TEXT("/Script/%s.%s"), Module, *ClassName);
		UClass* Result = FindObject<UClass>(nullptr, *Path);
		if (Result) return Result;
		Path = FString::Printf(TEXT("/Script/%s.%s"), Module, *BaseName);
		return FindObject<UClass>(nullptr, *Path);
	};

	static const TCHAR* Modules[] = {
		TEXT("Engine"), TEXT("UMG"), TEXT("EnhancedInput"), TEXT("AIModule"),
		TEXT("NavigationSystem"), TEXT("GameplayAbilities"), TEXT("Niagara"), TEXT("MediaAssets")
	};

	for (const TCHAR* Module : Modules)
	{
		FoundClass = TryModule(Module);
		if (FoundClass) return FoundClass;
	}

	return nullptr;
}

bool FAddCustomEventForDelegateAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString EventName;
	if (!GetRequiredString(Params, TEXT("event_name"), EventName, OutError)) return false;

	// Must have either (delegate_class + delegate_name) or source_node_id
	FString DelegateClass = GetOptionalString(Params, TEXT("delegate_class"));
	FString DelegateName = GetOptionalString(Params, TEXT("delegate_name"));
	FString SourceNodeId = GetOptionalString(Params, TEXT("source_node_id"));

	if (DelegateClass.IsEmpty() && DelegateName.IsEmpty() && SourceNodeId.IsEmpty())
	{
		OutError = TEXT("Must provide either (delegate_class + delegate_name) or source_node_id to resolve delegate signature.");
		return false;
	}

	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddCustomEventForDelegateAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString EventName = Params->GetStringField(TEXT("event_name"));
	FString DelegateClassName = GetOptionalString(Params, TEXT("delegate_class"));
	FString DelegateName = GetOptionalString(Params, TEXT("delegate_name"));
	FString SourceNodeId = GetOptionalString(Params, TEXT("source_node_id"));
	FString SourcePinName = GetOptionalString(Params, TEXT("source_pin_name"));
	bool bAutoConnect = GetOptionalBool(Params, TEXT("auto_connect"), true);
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	const UFunction* SignatureFunc = nullptr;
	UEdGraphPin* TargetDelegatePin = nullptr;  // Pin to connect to (if from node)
	FString ResolvedDelegateInfo;

	// ---- Mode A: Resolve from class + delegate property name ----
	if (!DelegateClassName.IsEmpty() && !DelegateName.IsEmpty())
	{
		UClass* DelegateClass = ResolveClass(DelegateClassName);
		if (!DelegateClass)
		{
			// Try from blueprint's own GeneratedClass
			if (Blueprint && Blueprint->GeneratedClass)
			{
				// Check if the delegate_class refers to a component type on this blueprint
				for (TFieldIterator<FObjectProperty> It(Blueprint->GeneratedClass); It; ++It)
				{
					if (It->PropertyClass && It->PropertyClass->GetName().Contains(DelegateClassName))
					{
						DelegateClass = It->PropertyClass;
						break;
					}
				}
			}

			if (!DelegateClass)
			{
				return CreateErrorResponse(FString::Printf(
					TEXT("Cannot resolve class '%s'. Try full path like '/Script/Engine.PrimitiveComponent' or short name like 'PrimitiveComponent'."),
					*DelegateClassName));
			}
		}

		// Find the multicast delegate property on the class
		FMulticastDelegateProperty* DelegateProp = nullptr;
		for (TFieldIterator<FMulticastDelegateProperty> It(DelegateClass); It; ++It)
		{
			if (It->GetFName() == FName(*DelegateName))
			{
				DelegateProp = *It;
				break;
			}
		}

		if (!DelegateProp)
		{
			TArray<FString> AvailableDelegates;
			for (TFieldIterator<FMulticastDelegateProperty> It(DelegateClass); It; ++It)
			{
				AvailableDelegates.Add(It->GetName());
			}
			FString DelegateList = AvailableDelegates.Num() > 0
				? FString::Join(AvailableDelegates, TEXT(", "))
				: TEXT("(none)");
			return CreateErrorResponse(FString::Printf(
				TEXT("Delegate '%s' not found on class '%s'. Available delegates: %s"),
				*DelegateName, *DelegateClass->GetName(), *DelegateList));
		}

		SignatureFunc = DelegateProp->SignatureFunction;
		ResolvedDelegateInfo = FString::Printf(TEXT("%s::%s"), *DelegateClass->GetName(), *DelegateName);
	}
	// ---- Mode B: Resolve from existing node's delegate pin ----
	else if (!SourceNodeId.IsEmpty())
	{
		FGuid SourceGuid;
		if (!FGuid::Parse(SourceNodeId, SourceGuid))
		{
			return CreateErrorResponse(FString::Printf(TEXT("Invalid source_node_id GUID: %s"), *SourceNodeId));
		}

		UEdGraphNode* SourceNode = nullptr;
		for (UEdGraphNode* Node : TargetGraph->Nodes)
		{
			if (Node && Node->NodeGuid == SourceGuid)
			{
				SourceNode = Node;
				break;
			}
		}

		if (!SourceNode)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Source node not found: %s"), *SourceNodeId));
		}

		// Find the delegate pin
		if (!SourcePinName.IsEmpty())
		{
			TargetDelegatePin = FMCPCommonUtils::FindPin(SourceNode, SourcePinName, EGPD_Input);
		}

		// Fallback: find first unconnected delegate input pin
		if (!TargetDelegatePin)
		{
			for (UEdGraphPin* Pin : SourceNode->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Input
					&& (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate
						|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate)
					&& Pin->LinkedTo.Num() == 0)
				{
					TargetDelegatePin = Pin;
					break;
				}
			}
		}

		if (!TargetDelegatePin)
		{
			// List available pins for diagnostic
			TArray<FString> PinNames;
			for (UEdGraphPin* Pin : SourceNode->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Input)
				{
					PinNames.Add(FString::Printf(TEXT("%s (%s)"),
						*Pin->PinName.ToString(), *Pin->PinType.PinCategory.ToString()));
				}
			}
			return CreateErrorResponse(FString::Printf(
				TEXT("No delegate input pin found on node %s. Available input pins: %s"),
				*SourceNodeId, *FString::Join(PinNames, TEXT(", "))));
		}

		// Resolve the signature from the delegate pin
		// Check if the owning node is a UK2Node_BaseMCDelegate (AddDelegate, etc.)
		if (const UK2Node_BaseMCDelegate* MCDelegateNode = Cast<UK2Node_BaseMCDelegate>(SourceNode))
		{
			SignatureFunc = MCDelegateNode->GetDelegateSignature();
		}
		else if (TargetDelegatePin->PinType.PinSubCategoryMemberReference.MemberName != NAME_None)
		{
			// Resolve from pin type's member reference
			SignatureFunc = FMemberReference::ResolveSimpleMemberReference<UFunction>(
				TargetDelegatePin->PinType.PinSubCategoryMemberReference);
		}

		if (!SignatureFunc)
		{
			return CreateErrorResponse(FString::Printf(
				TEXT("Cannot resolve delegate signature from pin '%s' on node %s. "
					 "Try using delegate_class + delegate_name mode instead."),
				*TargetDelegatePin->PinName.ToString(), *SourceNodeId));
		}

		ResolvedDelegateInfo = FString::Printf(TEXT("pin '%s' on node %s"),
			*TargetDelegatePin->PinName.ToString(), *SourceNodeId);
	}
	else if (!DelegateName.IsEmpty())
	{
		// delegate_name without delegate_class: try to find it on the blueprint's own class
		if (Blueprint && Blueprint->GeneratedClass)
		{
			FMulticastDelegateProperty* DelegateProp = FindFProperty<FMulticastDelegateProperty>(
				Blueprint->GeneratedClass, FName(*DelegateName));
			if (DelegateProp)
			{
				SignatureFunc = DelegateProp->SignatureFunction;
				ResolvedDelegateInfo = FString::Printf(TEXT("self::%s"), *DelegateName);
			}
		}

		if (!SignatureFunc)
		{
			return CreateErrorResponse(FString::Printf(
				TEXT("Delegate '%s' not found on Blueprint's own class. Specify delegate_class to search another class."),
				*DelegateName));
		}
	}

	if (!SignatureFunc)
	{
		return CreateErrorResponse(TEXT("Failed to resolve delegate signature. Provide (delegate_class + delegate_name) or a valid source_node_id."));
	}

	// Create the custom event node using CreateFromFunction
	UK2Node_CustomEvent* CustomEventNode = UK2Node_CustomEvent::CreateFromFunction(
		Position, TargetGraph, EventName, SignatureFunc, false /*bSelectNewNode*/);

	if (!CustomEventNode)
	{
		return CreateErrorResponse(TEXT("Failed to create custom event node."));
	}

	// If Mode B with auto_connect, connect the delegate output pin to the target
	bool bConnected = false;
	if (TargetDelegatePin && bAutoConnect)
	{
		UEdGraphPin* DelegateOutPin = CustomEventNode->FindPin(UK2Node_Event::DelegateOutputName);
		if (DelegateOutPin)
		{
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			bConnected = Schema->TryCreateConnection(DelegateOutPin, TargetDelegatePin);

			// If connected, reconstruct to lock pins to delegate signature
			if (bConnected)
			{
				CustomEventNode->ReconstructNode();
			}
		}
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(CustomEventNode, Context);

	// Build result
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), CustomEventNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("event_name"), EventName);
	ResultData->SetStringField(TEXT("delegate_source"), ResolvedDelegateInfo);
	ResultData->SetBoolField(TEXT("connected"), bConnected);

	// List output pins (delegate signature parameters)
	TArray<TSharedPtr<FJsonValue>> OutputPins;
	for (const UEdGraphPin* Pin : CustomEventNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output && Pin->PinName != UEdGraphSchema_K2::PN_Then)
		{
			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			if (!Pin->PinType.PinSubCategory.IsNone())
			{
				PinObj->SetStringField(TEXT("sub_type"), Pin->PinType.PinSubCategory.ToString());
			}
			OutputPins.Add(MakeShared<FJsonValueObject>(PinObj));
		}
	}
	ResultData->SetArrayField(TEXT("output_pins"), OutputPins);

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Variable Nodes
// ============================================================================

bool FAddBlueprintVariableAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString VariableName, VariableType;
	if (!GetRequiredString(Params, TEXT("variable_name"), VariableName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("variable_type"), VariableType, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintVariableAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString VariableName = Params->GetStringField(TEXT("variable_name"));
	FString VariableType = Params->GetStringField(TEXT("variable_type"));
	bool IsExposed = GetOptionalBool(Params, TEXT("is_exposed"), false);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Create variable based on type
	FEdGraphPinType PinType;
	FString TypeResolveError;
	if (!FMCPCommonUtils::ResolvePinTypeFromString(VariableType, PinType, TypeResolveError))
	{
		return CreateErrorResponse(TypeResolveError);
	}

	FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*VariableName), PinType);

	// Set variable properties
	for (FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName == FName(*VariableName))
		{
			if (IsExposed)
			{
				Variable.PropertyFlags |= CPF_Edit;
				Variable.PropertyFlags &= ~CPF_DisableEditOnInstance;
				// Use UE API to ensure structural modification notification
				FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, FName(*VariableName), false);
			}
			break;
		}
	}

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("variable_name"), VariableName);
	ResultData->SetStringField(TEXT("variable_type"), VariableType);
	return CreateSuccessResponse(ResultData);
}


bool FAddBlueprintVariableGetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString VariableName;
	if (!GetRequiredString(Params, TEXT("variable_name"), VariableName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintVariableGetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString VariableName = Params->GetStringField(TEXT("variable_name"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Check if this is a local variable in a function graph
	bool bIsLocalVar = false;
	FGuid LocalVarGuid;
	FString GraphNameStr = TargetGraph->GetName();
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
		{
			for (const FBPVariableDescription& LocalVar : EntryNode->LocalVariables)
			{
				if (LocalVar.VarName == FName(*VariableName))
				{
					bIsLocalVar = true;
					LocalVarGuid = LocalVar.VarGuid;
					break;
				}
			}
			break;
		}
	}

	UK2Node_VariableGet* VarGetNode = nullptr;
	if (bIsLocalVar)
	{
		VarGetNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableGet>(
			TargetGraph, Position, EK2NewNodeFlags::None,
			[&VariableName, &GraphNameStr, &LocalVarGuid](UK2Node_VariableGet* Node)
			{
				Node->VariableReference.SetLocalMember(FName(*VariableName), GraphNameStr, LocalVarGuid);
			}
		);
	}
	else
	{
		VarGetNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableGet>(
			TargetGraph, Position, EK2NewNodeFlags::None,
			[&VariableName](UK2Node_VariableGet* Node) { Node->VariableReference.SetSelfMember(FName(*VariableName)); }
		);
	}
	VarGetNode->ReconstructNode();

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(VarGetNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), VarGetNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FAddBlueprintVariableSetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString VariableName;
	if (!GetRequiredString(Params, TEXT("variable_name"), VariableName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintVariableSetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString VariableName = Params->GetStringField(TEXT("variable_name"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Check if this is a local variable in a function graph
	bool bIsLocalVar = false;
	FGuid LocalVarGuid;
	FString GraphNameStr = TargetGraph->GetName();
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
		{
			for (const FBPVariableDescription& LocalVar : EntryNode->LocalVariables)
			{
				if (LocalVar.VarName == FName(*VariableName))
				{
					bIsLocalVar = true;
					LocalVarGuid = LocalVar.VarGuid;
					break;
				}
			}
			break;
		}
	}

	UK2Node_VariableSet* VarSetNode = nullptr;
	if (bIsLocalVar)
	{
		VarSetNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableSet>(
			TargetGraph, Position, EK2NewNodeFlags::None,
			[&VariableName, &GraphNameStr, &LocalVarGuid](UK2Node_VariableSet* Node)
			{
				Node->VariableReference.SetLocalMember(FName(*VariableName), GraphNameStr, LocalVarGuid);
			}
		);
	}
	else
	{
		VarSetNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableSet>(
			TargetGraph, Position, EK2NewNodeFlags::None,
			[&VariableName](UK2Node_VariableSet* Node) { Node->VariableReference.SetSelfMember(FName(*VariableName)); }
		);
	}
	VarSetNode->ReconstructNode();

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(VarSetNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), VarSetNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FSetNodePinDefaultAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString NodeId, PinName, DefaultValue;
	if (!GetRequiredString(Params, TEXT("node_id"), NodeId, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("pin_name"), PinName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("default_value"), DefaultValue, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FSetNodePinDefaultAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString NodeId = Params->GetStringField(TEXT("node_id"));
	FString PinName = Params->GetStringField(TEXT("pin_name"));
	FString DefaultValue = Params->GetStringField(TEXT("default_value"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the node
	UEdGraphNode* TargetNode = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node->NodeGuid.ToString() == NodeId)
		{
			TargetNode = Node;
			break;
		}
	}

	if (!TargetNode)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Node not found: %s"), *NodeId));
	}

	// Find the pin
	UEdGraphPin* TargetPin = FMCPCommonUtils::FindPin(TargetNode, PinName, EGPD_Input);
	if (!TargetPin)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Pin not found: %s"), *PinName));
	}

	// Set the default value - handle object pins differently
	if (TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
		TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
		TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
		TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
	{
		UObject* LoadedObject = StaticLoadObject(UObject::StaticClass(), nullptr, *DefaultValue);
		if (LoadedObject)
		{
			TargetPin->DefaultObject = LoadedObject;
			TargetPin->DefaultValue.Empty();
		}
		else
		{
			return CreateErrorResponse(FString::Printf(TEXT("Failed to load object: %s"), *DefaultValue));
		}
	}
	else
	{
		TargetPin->DefaultValue = DefaultValue;
	}

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("pin_name"), PinName);
	ResultData->SetStringField(TEXT("default_value"), DefaultValue);
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Function Nodes
// ============================================================================

bool FAddBlueprintFunctionNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Target, FunctionName;
	if (!GetRequiredString(Params, TEXT("target"), Target, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("function_name"), FunctionName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintFunctionNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Target = Params->GetStringField(TEXT("target"));
	FString FunctionName = Params->GetStringField(TEXT("function_name"));
	FVector2D Position = GetNodePosition(Params);

	// Optional extra params payload (JSON string)
	TSharedPtr<FJsonObject> ExtraParams;
	if (Params->HasField(TEXT("params")))
	{
		const FString ParamsStr = Params->GetStringField(TEXT("params"));
		if (!ParamsStr.IsEmpty())
		{
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ParamsStr);
			TSharedPtr<FJsonObject> Parsed;
			if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
			{
				ExtraParams = Parsed;
			}
		}
	}

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the function
	UFunction* Function = nullptr;
	UClass* TargetClass = nullptr;

	// Direct mapping for known library classes
	FString TargetLower = Target.ToLower();
	if (TargetLower.Contains(TEXT("kismetmathlibrary")) || TargetLower.Contains(TEXT("math")))
	{
		TargetClass = UKismetMathLibrary::StaticClass();
	}
	else if (TargetLower.Contains(TEXT("kismetsystemlibrary")) || TargetLower.Contains(TEXT("systemlibrary")))
	{
		TargetClass = UKismetSystemLibrary::StaticClass();
	}
	else if (TargetLower.Contains(TEXT("gameplaystatics")))
	{
		TargetClass = UGameplayStatics::StaticClass();
	}
	else if (TargetLower.Contains(TEXT("enhancedinputlocalplayersubsystem")) || TargetLower.Contains(TEXT("inputsubsystem")))
	{
		TargetClass = UEnhancedInputLocalPlayerSubsystem::StaticClass();
	}
	else if (TargetLower.Equals(TEXT("kismetstringlibrary"), ESearchCase::IgnoreCase)
		|| TargetLower.Equals(TEXT("ukismetstringlibrary"), ESearchCase::IgnoreCase)
		|| TargetLower.Equals(TEXT("stringlibrary"), ESearchCase::IgnoreCase))
	{
		TargetClass = UKismetStringLibrary::StaticClass();
	}
	else if (TargetLower.Contains(TEXT("widgetblueprintlibrary")) || TargetLower.Contains(TEXT("widgetlibrary")))
	{
		TargetClass = UWidgetBlueprintLibrary::StaticClass();
	}
	else if (TargetLower.Contains(TEXT("gameusersettings")) || TargetLower.Contains(TEXT("usersettings")))
	{
		TargetClass = UGameUserSettings::StaticClass();
	}

	// If not a known class, try module paths
	if (!TargetClass)
	{
		// Fix: If target is already a full /Script/ path, try loading directly first
		if (Target.StartsWith(TEXT("/Script/")))
		{
			TargetClass = LoadClass<UObject>(nullptr, *Target);
		}

		// Try candidates with module path prefixes
		if (!TargetClass)
		{
			TArray<FString> CandidateNames;
			CandidateNames.Add(Target);
			if (!Target.StartsWith(TEXT("U")) && !Target.StartsWith(TEXT("/")))
			{
				CandidateNames.Add(TEXT("U") + Target);
			}

			static const FString ModulePaths[] = {
				TEXT("/Script/Engine"),
				TEXT("/Script/CoreUObject"),
				TEXT("/Script/UMG"),
			};

			for (const FString& Candidate : CandidateNames)
			{
				if (TargetClass) break;
				for (const FString& ModulePath : ModulePaths)
				{
					FString FullPath = FString::Printf(TEXT("%s.%s"), *ModulePath, *Candidate);
					TargetClass = LoadClass<UObject>(nullptr, *FullPath);
					if (TargetClass) break;
				}
			}
		}

		// Dynamic fallback: scan all loaded UClass objects for a matching name.
		// This handles project-specific classes (e.g., UPlayerStatusBlueprintLibrary)
		// without requiring the caller to know the full /Script/ path.
		if (!TargetClass)
		{
			FString ClassNameOnly = Target;
			// Strip module path prefix if present
			if (ClassNameOnly.Contains(TEXT(".")))
			{
				ClassNameOnly = ClassNameOnly.RightChop(ClassNameOnly.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd) + 1);
			}

			TArray<FString> NameVariants;
			NameVariants.Add(ClassNameOnly);
			if (!ClassNameOnly.StartsWith(TEXT("U")))
			{
				NameVariants.Add(TEXT("U") + ClassNameOnly);
			}

			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (TargetClass) break;
				for (const FString& Variant : NameVariants)
				{
					if (It->GetName() == Variant)
					{
						TargetClass = *It;
						break;
					}
				}
			}
		}
	}

	// Look for function in target class
	if (TargetClass)
	{
		Function = TargetClass->FindFunctionByName(*FunctionName);
		// Try case-insensitive match
		if (!Function)
		{
			for (TFieldIterator<UFunction> FuncIt(TargetClass); FuncIt; ++FuncIt)
			{
				if ((*FuncIt)->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
				{
					Function = *FuncIt;
					break;
				}
			}
		}
		// Try with K2_ prefix (UE wraps many AActor/UObject methods with K2_ prefix)
		if (!Function)
		{
			FString K2FunctionName = TEXT("K2_") + FunctionName;
			Function = TargetClass->FindFunctionByName(*K2FunctionName);
			if (!Function)
			{
				for (TFieldIterator<UFunction> FuncIt(TargetClass); FuncIt; ++FuncIt)
				{
					if ((*FuncIt)->GetName().Equals(K2FunctionName, ESearchCase::IgnoreCase))
					{
						Function = *FuncIt;
						break;
					}
				}
			}
		}
	}

	// Fallback to blueprint class
	if (!Function)
	{
		Function = Blueprint->GeneratedClass->FindFunctionByName(*FunctionName);
		// Also try K2_ prefix on blueprint class
		if (!Function)
		{
			FString K2FunctionName = TEXT("K2_") + FunctionName;
			Function = Blueprint->GeneratedClass->FindFunctionByName(*K2FunctionName);
		}
	}

	// Fallback: search common static function libraries (covers target="self" cases
	// where the function actually lives in a library class like KismetSystemLibrary)
	if (!Function)
	{
		static const TCHAR* FallbackLibraryPaths[] = {
			TEXT("/Script/Engine.KismetSystemLibrary"),
			TEXT("/Script/Engine.GameplayStatics"),
			TEXT("/Script/Engine.KismetMathLibrary"),
			TEXT("/Script/Engine.KismetStringLibrary"),
			TEXT("/Script/Engine.KismetTextLibrary"),
			TEXT("/Script/Engine.KismetArrayLibrary"),
		};
		for (const TCHAR* LibPath : FallbackLibraryPaths)
		{
			UClass* LibClass = FindObject<UClass>(nullptr, LibPath);
			if (LibClass)
			{
				Function = LibClass->FindFunctionByName(*FunctionName);
				if (!Function)
				{
					FString K2FunctionName = TEXT("K2_") + FunctionName;
					Function = LibClass->FindFunctionByName(*K2FunctionName);
				}
				if (Function)
				{
					TargetClass = LibClass;
					break;
				}
			}
		}
	}

	if (!Function)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Function not found: %s in target %s"), *FunctionName, *Target));
	}

	// Special-case: Create Widget node with explicit widget class
	// Use reflection-based K2Node_CreateWidget creation so we don't depend on private headers.
	if (TargetClass == UWidgetBlueprintLibrary::StaticClass())
	{
		const bool bIsCreateWidgetCall = FunctionName.Equals(TEXT("Create"), ESearchCase::IgnoreCase)
			|| FunctionName.Equals(TEXT("CreateWidget"), ESearchCase::IgnoreCase)
			|| Function->GetName().Equals(TEXT("Create"), ESearchCase::IgnoreCase)
			|| Function->GetName().Equals(TEXT("CreateWidget"), ESearchCase::IgnoreCase);

		if (bIsCreateWidgetCall && ExtraParams.IsValid()
			&& (ExtraParams->HasField(TEXT("widget_blueprint")) || ExtraParams->HasField(TEXT("widget_class_path"))))
		{
			UClass* WidgetClass = nullptr;

			if (ExtraParams->HasField(TEXT("widget_blueprint")))
			{
				const FString WidgetBPName = ExtraParams->GetStringField(TEXT("widget_blueprint"));
				UBlueprint* WidgetBP = FMCPCommonUtils::FindBlueprint(WidgetBPName);
				if (WidgetBP && WidgetBP->GeneratedClass)
				{
					WidgetClass = WidgetBP->GeneratedClass;
				}
			}

			if (!WidgetClass && ExtraParams->HasField(TEXT("widget_class_path")))
			{
				const FString ClassPath = ExtraParams->GetStringField(TEXT("widget_class_path"));
				WidgetClass = LoadClass<UObject>(nullptr, *ClassPath);
				if (!WidgetClass && !ClassPath.EndsWith(TEXT("_C")))
				{
					WidgetClass = LoadClass<UObject>(nullptr, *(ClassPath + TEXT("_C")));
				}
			}

			if (!WidgetClass)
			{
				return CreateErrorResponse(TEXT("Failed to resolve widget class for CreateWidget. Provide params.widget_blueprint or params.widget_class_path."));
			}

			UClass* CreateWidgetNodeClass = LoadClass<UK2Node>(nullptr, TEXT("/Script/UMGEditor.K2Node_CreateWidget"));
			if (!CreateWidgetNodeClass)
			{
				return CreateErrorResponse(TEXT("Failed to load /Script/UMGEditor.K2Node_CreateWidget"));
			}

			UK2Node* CreateWidgetNode = NewObject<UK2Node>(TargetGraph, CreateWidgetNodeClass);
			if (!CreateWidgetNode)
			{
				return CreateErrorResponse(TEXT("Failed to create K2Node_CreateWidget node"));
			}

			TargetGraph->AddNode(CreateWidgetNode, false, false);
			CreateWidgetNode->CreateNewGuid();
			CreateWidgetNode->NodePosX = FMath::RoundToInt(Position.X);
			CreateWidgetNode->NodePosY = FMath::RoundToInt(Position.Y);
			CreateWidgetNode->AllocateDefaultPins();

			if (UEdGraphPin* ClassPin = CreateWidgetNode->FindPin(TEXT("Class")))
			{
				ClassPin->DefaultObject = WidgetClass;
				ClassPin->DefaultValue.Empty();
				CreateWidgetNode->PinDefaultValueChanged(ClassPin);
			}

			CreateWidgetNode->ReconstructNode();

			MarkBlueprintModified(Blueprint, Context);
			RegisterCreatedNode(CreateWidgetNode, Context);

			TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
			ResultData->SetStringField(TEXT("node_id"), CreateWidgetNode->NodeGuid.ToString());
			return CreateSuccessResponse(ResultData);
		}
	}

	UK2Node_CallFunction* FunctionNode = FMCPCommonUtils::CreateFunctionCallNode(TargetGraph, Function, Position);
	if (!FunctionNode)
	{
		return CreateErrorResponse(TEXT("Failed to create function call node"));
	}

	// Optional: apply pin defaults from params JSON by pin name.
	// Example:
	// params: {"WidgetType":"/Game/UI/WBP_Options.WBP_Options_C", "ZOrder":0}
	if (ExtraParams.IsValid())
	{
		for (const auto& Pair : ExtraParams->Values)
		{
			const FString PinName = Pair.Key;
			UEdGraphPin* Pin = FMCPCommonUtils::FindPin(FunctionNode, PinName, EGPD_Input);
			if (!Pin) continue;

			const TSharedPtr<FJsonValue>& JsonVal = Pair.Value;
			if (!JsonVal.IsValid()) continue;

			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object
				|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class
				|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject
				|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
			{
				if (JsonVal->Type == EJson::String)
				{
					const FString ObjPath = JsonVal->AsString();
					UObject* LoadedObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjPath);
					if (LoadedObject)
					{
						Pin->DefaultObject = LoadedObject;
						Pin->DefaultValue.Empty();
					}
					else
					{
						Pin->DefaultValue = ObjPath;
					}
				}
			}
			else if (JsonVal->Type == EJson::String)
			{
				Pin->DefaultValue = JsonVal->AsString();
			}
			else if (JsonVal->Type == EJson::Number)
			{
				// Integer pins require a whole-number string; JSON has no
				// int vs float distinction so we must check pin category.
				const double NumVal = JsonVal->AsNumber();
				if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int
					|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
				{
					Pin->DefaultValue = FString::FromInt(static_cast<int32>(NumVal));
				}
				else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int64)
				{
					Pin->DefaultValue = FString::Printf(TEXT("%lld"), static_cast<int64>(NumVal));
				}
				else
				{
					Pin->DefaultValue = LexToString(NumVal);
				}
			}
			else if (JsonVal->Type == EJson::Boolean)
			{
				Pin->DefaultValue = JsonVal->AsBool() ? TEXT("true") : TEXT("false");
			}

			FunctionNode->PinDefaultValueChanged(Pin);
		}
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(FunctionNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), FunctionNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FAddBlueprintSelfReferenceAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintSelfReferenceAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	UK2Node_Self* SelfNode = FMCPCommonUtils::CreateSelfReferenceNode(TargetGraph, Position);
	if (!SelfNode)
	{
		return CreateErrorResponse(TEXT("Failed to create self node"));
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(SelfNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), SelfNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FAddBlueprintGetSelfComponentReferenceAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ComponentName;
	if (!GetRequiredString(Params, TEXT("component_name"), ComponentName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintGetSelfComponentReferenceAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ComponentName = Params->GetStringField(TEXT("component_name"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	UK2Node_VariableGet* GetComponentNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableGet>(
		TargetGraph, Position, EK2NewNodeFlags::None,
		[&ComponentName](UK2Node_VariableGet* Node) { Node->VariableReference.SetSelfMember(FName(*ComponentName)); }
	);
	GetComponentNode->ReconstructNode();

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(GetComponentNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), GetComponentNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FAddBlueprintBranchNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintBranchNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	UK2Node_IfThenElse* BranchNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_IfThenElse>(
		TargetGraph, Position, EK2NewNodeFlags::None
	);

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(BranchNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), BranchNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FAddBlueprintCastNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString TargetClass;
	if (!GetRequiredString(Params, TEXT("target_class"), TargetClass, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintCastNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString TargetClassName = Params->GetStringField(TEXT("target_class"));
	bool bPureCast = GetOptionalBool(Params, TEXT("pure_cast"), false);
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Fix: Respect graph_name parameter to avoid phantom node issue
	UEdGraph* EventGraph = GetTargetGraph(Params, Context);
	if (!EventGraph)
	{
		EventGraph = FMCPCommonUtils::FindOrCreateEventGraph(Blueprint);
	}

	// Find the target class
	UClass* TargetClass = nullptr;

	// Check if it's a content path
	if (TargetClassName.StartsWith(TEXT("/Game/")))
	{
		FString BPPath = TargetClassName;
		if (!BPPath.EndsWith(TEXT("_C")))
		{
			BPPath += TEXT("_C");
		}
		TargetClass = LoadClass<UObject>(nullptr, *BPPath);
		if (!TargetClass)
		{
			TargetClass = LoadClass<UObject>(nullptr, *TargetClassName);
		}
	}

	// Try to find as a blueprint name
	if (!TargetClass)
	{
		UBlueprint* TargetBP = FMCPCommonUtils::FindBlueprint(TargetClassName);
		if (TargetBP && TargetBP->GeneratedClass)
		{
			TargetClass = TargetBP->GeneratedClass;
		}
	}

	// Try engine classes
	if (!TargetClass)
	{
		static const FString ModulePaths[] = {
			TEXT("/Script/Engine"),
			TEXT("/Script/CoreUObject"),
		};
		for (const FString& ModulePath : ModulePaths)
		{
			FString FullPath = FString::Printf(TEXT("%s.%s"), *ModulePath, *TargetClassName);
			TargetClass = LoadClass<UObject>(nullptr, *FullPath);
			if (TargetClass) break;
		}
	}

	if (!TargetClass)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Target class not found: %s"), *TargetClassName));
	}

	UK2Node_DynamicCast* CastNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_DynamicCast>(
		EventGraph, Position, EK2NewNodeFlags::None,
		[TargetClass, bPureCast](UK2Node_DynamicCast* Node) {
			Node->TargetType = TargetClass;
			Node->SetPurity(bPureCast);
		}
	);

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(CastNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), CastNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("target_class"), TargetClass->GetName());
	ResultData->SetBoolField(TEXT("pure_cast"), bPureCast);
	return CreateSuccessResponse(ResultData);
}


// =============================================================================
// Subsystem Nodes
// =============================================================================

bool FAddBlueprintGetSubsystemNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString SubsystemClass;
	if (!GetRequiredString(Params, TEXT("subsystem_class"), SubsystemClass, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintGetSubsystemNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SubsystemClassName = Params->GetStringField(TEXT("subsystem_class"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Fix: Respect graph_name parameter instead of hardcoding EventGraph.
	// This prevents the "phantom node" issue where nodes are silently created
	// in EventGraph when the caller expects them in a function graph.
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);
	if (!TargetGraph)
	{
		TargetGraph = FMCPCommonUtils::FindOrCreateEventGraph(Blueprint);
	}

	// Find the subsystem class
	UClass* FoundClass = nullptr;

	// Try to load the class directly (for full paths like /Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem)
	if (SubsystemClassName.StartsWith(TEXT("/Script/")))
	{
		FoundClass = LoadClass<USubsystem>(nullptr, *SubsystemClassName);
	}

	// Try known module paths
	if (!FoundClass)
	{
		// Build candidate names: original + with U prefix
		TArray<FString> CandidateNames;
		CandidateNames.Add(SubsystemClassName);
		if (!SubsystemClassName.StartsWith(TEXT("U")) && !SubsystemClassName.StartsWith(TEXT("/")))
		{
			CandidateNames.Add(TEXT("U") + SubsystemClassName);
		}

		const FString ModulePaths[] = {
			TEXT("/Script/EnhancedInput"),
			TEXT("/Script/Engine"),
			TEXT("/Script/GameplayAbilities"),
		};

		for (const FString& Candidate : CandidateNames)
		{
			if (FoundClass) break;
			for (const FString& ModulePath : ModulePaths)
			{
				FString FullPath = FString::Printf(TEXT("%s.%s"), *ModulePath, *Candidate);
				FoundClass = LoadClass<USubsystem>(nullptr, *FullPath);
				if (FoundClass) break;
			}
		}
	}

	// Dynamic fallback: scan all loaded packages for any USubsystem subclass matching the name
	if (!FoundClass)
	{
		FString ClassNameOnly = SubsystemClassName;
		if (ClassNameOnly.Contains(TEXT(".")))
		{
			ClassNameOnly = ClassNameOnly.RightChop(ClassNameOnly.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd) + 1);
		}
		if (!ClassNameOnly.StartsWith(TEXT("U")))
		{
			ClassNameOnly = TEXT("U") + ClassNameOnly;
		}

		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->IsChildOf(USubsystem::StaticClass()) && It->GetName() == ClassNameOnly)
			{
				FoundClass = *It;
				break;
			}
		}
	}

	if (!FoundClass)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Subsystem class not found: %s. Try full path like /Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem or /Script/p110_2.UMySubsystem"), *SubsystemClassName));
	}

	// Fix: Select the correct K2Node type based on subsystem inheritance.
	// - UGameInstanceSubsystem  → UK2Node_GetSubsystem (base, uses WorldContext)
	// - ULocalPlayerSubsystem   → UK2Node_GetSubsystemFromPC (needs PlayerController)
	// - UEngineSubsystem         → UK2Node_GetEngineSubsystem
	// - UEditorSubsystem         → UK2Node_GetEditorSubsystem
	// Using the wrong node type causes type-mismatch on the output pin.
	UEdGraphNode* CreatedNode = nullptr;
	FString NodeTypeUsed;

	if (FoundClass->IsChildOf(ULocalPlayerSubsystem::StaticClass()))
	{
		UK2Node_GetSubsystemFromPC* SubsystemNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_GetSubsystemFromPC>(
			TargetGraph, Position, EK2NewNodeFlags::None,
			[FoundClass](UK2Node_GetSubsystemFromPC* Node) { Node->Initialize(FoundClass); }
		);
		CreatedNode = SubsystemNode;
		NodeTypeUsed = TEXT("GetSubsystemFromPC");
	}
	else if (FoundClass->IsChildOf(UEngineSubsystem::StaticClass()))
	{
		UK2Node_GetEngineSubsystem* SubsystemNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_GetEngineSubsystem>(
			TargetGraph, Position, EK2NewNodeFlags::None,
			[FoundClass](UK2Node_GetEngineSubsystem* Node) { Node->Initialize(FoundClass); }
		);
		CreatedNode = SubsystemNode;
		NodeTypeUsed = TEXT("GetEngineSubsystem");
	}
	else
	{
		// Default: UK2Node_GetSubsystem — works for UGameInstanceSubsystem
		// and any other USubsystem subclass
		UK2Node_GetSubsystem* SubsystemNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_GetSubsystem>(
			TargetGraph, Position, EK2NewNodeFlags::None,
			[FoundClass](UK2Node_GetSubsystem* Node) { Node->Initialize(FoundClass); }
		);
		CreatedNode = SubsystemNode;
		NodeTypeUsed = TEXT("GetSubsystem");
	}

	if (!CreatedNode)
	{
		return CreateErrorResponse(TEXT("Failed to spawn subsystem getter node"));
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(CreatedNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), CreatedNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("subsystem_class"), FoundClass->GetName());
	ResultData->SetStringField(TEXT("node_type"), NodeTypeUsed);
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Blueprint Function Graph
// ============================================================================

bool FCreateBlueprintFunctionAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString FunctionName;
	if (!GetRequiredString(Params, TEXT("function_name"), FunctionName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FCreateBlueprintFunctionAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString FunctionName = Params->GetStringField(TEXT("function_name"));
	bool bIsPure = GetOptionalBool(Params, TEXT("is_pure"), false);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Check if function already exists
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph->GetFName() == FName(*FunctionName))
		{
			// Find entry node
			FString EntryNodeId;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node);
				if (EntryNode)
				{
					EntryNodeId = EntryNode->NodeGuid.ToString();
					break;
				}
			}

			TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
			ResultData->SetBoolField(TEXT("already_exists"), true);
			ResultData->SetStringField(TEXT("function_name"), FunctionName);
			ResultData->SetStringField(TEXT("graph_name"), Graph->GetName());
			ResultData->SetStringField(TEXT("entry_node_id"), EntryNodeId);
			return CreateSuccessResponse(ResultData);
		}
	}

	// Create the function graph
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, FName(*FunctionName),
		UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

	if (!NewGraph)
	{
		return CreateErrorResponse(TEXT("Failed to create function graph"));
	}

	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, NewGraph, true, nullptr);

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	K2Schema->CreateDefaultNodesForGraph(*NewGraph);

	// Find entry and result nodes
	UK2Node_FunctionEntry* EntryNode = nullptr;
	UK2Node_FunctionResult* ResultNode = nullptr;
	for (UEdGraphNode* Node : NewGraph->Nodes)
	{
		if (!EntryNode) EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (!ResultNode) ResultNode = Cast<UK2Node_FunctionResult>(Node);
	}

	// Add input parameters
	const TArray<TSharedPtr<FJsonValue>>* InputsArray = GetOptionalArray(Params, TEXT("inputs"));
	if (InputsArray && EntryNode)
	{
		for (const TSharedPtr<FJsonValue>& InputValue : *InputsArray)
		{
			const TSharedPtr<FJsonObject>& InputObj = InputValue->AsObject();
			if (!InputObj) continue;

			FString ParamName, ParamType;
			if (!InputObj->TryGetStringField(TEXT("name"), ParamName)) continue;
			if (!InputObj->TryGetStringField(TEXT("type"), ParamType)) continue;

			FEdGraphPinType PinType;
			FString TypeResolveError;
			if (!FMCPCommonUtils::ResolvePinTypeFromString(ParamType, PinType, TypeResolveError))
			{
				return CreateErrorResponse(FString::Printf(TEXT("Function input '%s': %s"), *ParamName, *TypeResolveError));
			}

			EntryNode->CreateUserDefinedPin(FName(*ParamName), PinType, EGPD_Output);
		}
		EntryNode->ReconstructNode();
	}

	// Add output parameters
	const TArray<TSharedPtr<FJsonValue>>* OutputsArray = GetOptionalArray(Params, TEXT("outputs"));
	if (OutputsArray)
	{
		if (!ResultNode)
		{
			ResultNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_FunctionResult>(
				NewGraph, FVector2D(400, 0), EK2NewNodeFlags::None
			);
		}

		for (const TSharedPtr<FJsonValue>& OutputValue : *OutputsArray)
		{
			const TSharedPtr<FJsonObject>& OutputObj = OutputValue->AsObject();
			if (!OutputObj) continue;

			FString ParamName, ParamType;
			if (!OutputObj->TryGetStringField(TEXT("name"), ParamName)) continue;
			if (!OutputObj->TryGetStringField(TEXT("type"), ParamType)) continue;

			FEdGraphPinType PinType;
			FString TypeResolveError;
			if (!FMCPCommonUtils::ResolvePinTypeFromString(ParamType, PinType, TypeResolveError))
			{
				return CreateErrorResponse(FString::Printf(TEXT("Function output '%s': %s"), *ParamName, *TypeResolveError));
			}

			ResultNode->CreateUserDefinedPin(FName(*ParamName), PinType, EGPD_Input);
		}
		ResultNode->ReconstructNode();
	}

	if (bIsPure && EntryNode)
	{
		K2Schema->AddExtraFunctionFlags(NewGraph, FUNC_BlueprintPure);
	}

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("function_name"), FunctionName);
	ResultData->SetStringField(TEXT("graph_name"), NewGraph->GetName());
	if (EntryNode) ResultData->SetStringField(TEXT("entry_node_id"), EntryNode->NodeGuid.ToString());
	if (ResultNode) ResultData->SetStringField(TEXT("result_node_id"), ResultNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Event Dispatchers
// ============================================================================

bool FAddEventDispatcherAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString DispatcherName;
	if (!GetRequiredString(Params, TEXT("dispatcher_name"), DispatcherName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddEventDispatcherAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString DispatcherName = Params->GetStringField(TEXT("dispatcher_name"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Add the delegate variable
	FEdGraphPinType DelegateType;
	DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
	FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*DispatcherName), DelegateType);

	// Create the delegate signature graph
	FName GraphName = FName(*DispatcherName);
	UEdGraph* SignatureGraph = nullptr;

	for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
	{
		if (Graph->GetFName() == GraphName)
		{
			SignatureGraph = Graph;
			break;
		}
	}

	if (!SignatureGraph)
	{
		SignatureGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, GraphName,
			UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

		if (!SignatureGraph)
		{
			return CreateErrorResponse(TEXT("Failed to create delegate signature graph"));
		}

		SignatureGraph->bEditable = false;

		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		K2Schema->CreateDefaultNodesForGraph(*SignatureGraph);
		K2Schema->CreateFunctionGraphTerminators(*SignatureGraph, (UClass*)nullptr);
		K2Schema->AddExtraFunctionFlags(SignatureGraph, (FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public));
		K2Schema->MarkFunctionEntryAsEditable(SignatureGraph, true);

		Blueprint->DelegateSignatureGraphs.Add(SignatureGraph);
	}

	// Find entry node and add parameters
	UK2Node_FunctionEntry* EntryNode = nullptr;
	for (UEdGraphNode* Node : SignatureGraph->Nodes)
	{
		EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (EntryNode) break;
	}

	const TArray<TSharedPtr<FJsonValue>>* ParamsArray = GetOptionalArray(Params, TEXT("parameters"));
	if (EntryNode && ParamsArray)
	{
		for (const TSharedPtr<FJsonValue>& ParamValue : *ParamsArray)
		{
			const TSharedPtr<FJsonObject>& ParamObj = ParamValue->AsObject();
			if (!ParamObj) continue;

			FString ParamName, ParamType;
			if (!ParamObj->TryGetStringField(TEXT("name"), ParamName)) continue;
			if (!ParamObj->TryGetStringField(TEXT("type"), ParamType)) continue;

			FEdGraphPinType PinType;
			FString TypeResolveError;
			if (!FMCPCommonUtils::ResolvePinTypeFromString(ParamType, PinType, TypeResolveError))
			{
				UE_LOG(LogTemp, Warning, TEXT("EventDispatcher param '%s': %s, skipping"), *ParamName, *TypeResolveError);
				continue;
			}

			EntryNode->CreateUserDefinedPin(FName(*ParamName), PinType, EGPD_Output);
		}
		EntryNode->ReconstructNode();
	}

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("dispatcher_name"), DispatcherName);
	return CreateSuccessResponse(ResultData);
}


bool FCallEventDispatcherAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString DispatcherName;
	if (!GetRequiredString(Params, TEXT("dispatcher_name"), DispatcherName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FCallEventDispatcherAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString DispatcherName = Params->GetStringField(TEXT("dispatcher_name"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* EventGraph = GetTargetGraph(Params, Context);

	// Find the delegate property
	FMulticastDelegateProperty* DelegateProp = FindFProperty<FMulticastDelegateProperty>(
		Blueprint->GeneratedClass, FName(*DispatcherName));
	if (!DelegateProp)
	{
		return CreateErrorResponse(FString::Printf(
			TEXT("Delegate property '%s' not found. Compile the blueprint first."), *DispatcherName));
	}

	// Create CallDelegate node
	UClass* GenClass = Blueprint->GeneratedClass;
	UK2Node_CallDelegate* CallNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CallDelegate>(
		EventGraph, Position, EK2NewNodeFlags::None,
		[DelegateProp, GenClass](UK2Node_CallDelegate* Node) { Node->SetFromProperty(DelegateProp, false, GenClass); }
	);

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(CallNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), CallNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


bool FBindEventDispatcherAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString DispatcherName;
	if (!GetRequiredString(Params, TEXT("dispatcher_name"), DispatcherName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FBindEventDispatcherAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString DispatcherName = Params->GetStringField(TEXT("dispatcher_name"));
	FString TargetBlueprintName = GetOptionalString(Params, TEXT("target_blueprint"));
	FString BindingMode = GetOptionalString(Params, TEXT("binding_mode"));
	FString FunctionName = GetOptionalString(Params, TEXT("function_name"));
	const bool bBindToFunction = BindingMode.Equals(TEXT("function"), ESearchCase::IgnoreCase)
		|| GetOptionalBool(Params, TEXT("bind_to_function"), false);
	const bool bCreateFunctionIfMissing = GetOptionalBool(Params, TEXT("create_function_if_missing"), true);
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Get target blueprint (defaults to self)
	UBlueprint* TargetBlueprint = Blueprint;
	if (!TargetBlueprintName.IsEmpty())
	{
		TargetBlueprint = FMCPCommonUtils::FindBlueprint(TargetBlueprintName);
		if (!TargetBlueprint)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Target blueprint not found: %s"), *TargetBlueprintName));
		}
	}

	UEdGraph* EventGraph = GetTargetGraph(Params, Context);

	// Find the delegate property
	FMulticastDelegateProperty* DelegateProp = FindFProperty<FMulticastDelegateProperty>(
		TargetBlueprint->GeneratedClass, FName(*DispatcherName));
	if (!DelegateProp)
	{
		TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
		ResultData->SetStringField(TEXT("message"), TEXT("Dispatcher not found. Compile the target blueprint first."));
		return CreateErrorResponse(TEXT("Dispatcher not found in compiled class. Compile the target blueprint first."));
	}

	UFunction* SignatureFunc = DelegateProp->SignatureFunction;

	// Create UK2Node_AddDelegate
	UClass* TargetGenClass = TargetBlueprint->GeneratedClass;
	UK2Node_AddDelegate* BindNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_AddDelegate>(
		EventGraph, Position, EK2NewNodeFlags::None,
		[DelegateProp, TargetGenClass](UK2Node_AddDelegate* Node) { Node->SetFromProperty(DelegateProp, false, TargetGenClass); }
	);

	if (bBindToFunction)
	{
		if (FunctionName.IsEmpty())
		{
			FunctionName = FString::Printf(TEXT("On%s"), *DispatcherName);
		}

		UEdGraph* FunctionGraph = FMCPCommonUtils::FindFunctionGraph(Blueprint, FunctionName);
		bool bFunctionCreated = false;

		if (!FunctionGraph && bCreateFunctionIfMissing)
		{
			FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
				Blueprint, FName(*FunctionName),
				UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

			if (!FunctionGraph)
			{
				return CreateErrorResponse(FString::Printf(TEXT("Failed to create function graph '%s'"), *FunctionName));
			}

			FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, FunctionGraph, true, nullptr);
			const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
			K2Schema->CreateDefaultNodesForGraph(*FunctionGraph);

			UK2Node_FunctionEntry* EntryNode = nullptr;
			for (UEdGraphNode* Node : FunctionGraph->Nodes)
			{
				EntryNode = Cast<UK2Node_FunctionEntry>(Node);
				if (EntryNode)
				{
					break;
				}
			}

			if (EntryNode && SignatureFunc)
			{
				for (TFieldIterator<FProperty> PropIt(SignatureFunc); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
				{
					FProperty* Param = *PropIt;
					if (!(Param->PropertyFlags & CPF_ReturnParm))
					{
						FEdGraphPinType PinType;
						if (K2Schema->ConvertPropertyToPinType(Param, PinType))
						{
							EntryNode->CreateUserDefinedPin(Param->GetFName(), PinType, EGPD_Output);
						}
					}
				}

				EntryNode->ReconstructNode();
			}

			bFunctionCreated = true;
		}

		if (!FunctionGraph)
		{
			return CreateErrorResponse(FString::Printf(
				TEXT("Function '%s' not found. Set create_function_if_missing=true to auto-create it."),
				*FunctionName));
		}

		FVector2D DelegatePosition(Position.X + 320.f, Position.Y);
		UK2Node_CreateDelegate* CreateDelegateNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CreateDelegate>(
			EventGraph, DelegatePosition, EK2NewNodeFlags::None,
			[](UK2Node_CreateDelegate* Node) {}
		);

		if (!CreateDelegateNode)
		{
			return CreateErrorResponse(TEXT("Failed to create CreateDelegate node for function binding"));
		}

		UEdGraphPin* BindDelegatePin = BindNode->GetDelegatePin();
		UEdGraphPin* DelegateOutPin = CreateDelegateNode->GetDelegateOutPin();
		bool bConnected = false;

		if (BindDelegatePin && DelegateOutPin)
		{
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			bConnected = Schema->TryCreateConnection(DelegateOutPin, BindDelegatePin);
		}

		CreateDelegateNode->SetFunction(FName(*FunctionName));
		CreateDelegateNode->HandleAnyChange(true);

		const bool bFunctionResolved = (CreateDelegateNode->GetFunctionName() != NAME_None);

		MarkBlueprintModified(Blueprint, Context);

		TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
		ResultData->SetStringField(TEXT("bind_node_id"), BindNode->NodeGuid.ToString());
		ResultData->SetStringField(TEXT("delegate_node_id"), CreateDelegateNode->NodeGuid.ToString());
		ResultData->SetStringField(TEXT("function_name"), FunctionName);
		ResultData->SetBoolField(TEXT("function_created"), bFunctionCreated);
		ResultData->SetBoolField(TEXT("function_resolved"), bFunctionResolved);
		ResultData->SetBoolField(TEXT("delegate_connected"), bConnected);
		ResultData->SetStringField(TEXT("binding_type"), TEXT("function"));

		if (!bConnected || !bFunctionResolved)
		{
			ResultData->SetStringField(TEXT("warning"),
				TEXT("Function binding created but may need manual fix. Ensure dispatcher signature matches function parameters."));
		}

		return CreateSuccessResponse(ResultData);
	}

	// Create matching Custom Event
	FString EventName = FString::Printf(TEXT("On%s"), *DispatcherName);
	FVector2D EventPosition(Position.X + 300, Position.Y);
	UK2Node_CustomEvent* CustomEventNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CustomEvent>(
		EventGraph, EventPosition, EK2NewNodeFlags::None,
		[&EventName](UK2Node_CustomEvent* Node) { Node->CustomFunctionName = FName(*EventName); }
	);

	// Set delegate signature — this ensures the custom event's pins and type
	// exactly match the dispatcher, making it connectable to the delegate pin.
	if (SignatureFunc)
	{
		CustomEventNode->SetDelegateSignature(SignatureFunc);
	}

	// Connect custom event delegate output to bind node BEFORE ReconstructNode
	// so that ReconstructNode can resolve the delegate signature from the connection.
	UEdGraphPin* EventDelegatePin = CustomEventNode->FindPin(UK2Node_Event::DelegateOutputName);
	UEdGraphPin* BindDelegatePin = BindNode->GetDelegatePin();
	bool bConnected = false;
	if (EventDelegatePin && BindDelegatePin)
	{
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		bConnected = Schema->TryCreateConnection(EventDelegatePin, BindDelegatePin);
	}

	// Reconstruct after connecting to fully resolve delegate signature
	CustomEventNode->ReconstructNode();

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("bind_node_id"), BindNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("event_node_id"), CustomEventNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("event_name"), EventName);
	ResultData->SetStringField(TEXT("binding_type"), TEXT("custom_event"));
	ResultData->SetBoolField(TEXT("delegate_connected"), bConnected);
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Create Event Delegate (K2Node_CreateDelegate)
// ============================================================================

bool FCreateEventDelegateAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString FunctionName;
	if (!GetRequiredString(Params, TEXT("function_name"), FunctionName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FCreateEventDelegateAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString FunctionName = Params->GetStringField(TEXT("function_name"));
	FString ConnectToNodeId = GetOptionalString(Params, TEXT("connect_to_node_id"));
	FString ConnectToPin = GetOptionalString(Params, TEXT("connect_to_pin"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Create the K2Node_CreateDelegate node
	UK2Node_CreateDelegate* CreateDelegateNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CreateDelegate>(
		TargetGraph, Position, EK2NewNodeFlags::None,
		[](UK2Node_CreateDelegate* Node) {}
	);

	if (!CreateDelegateNode)
	{
		return CreateErrorResponse(TEXT("Failed to create CreateDelegate node"));
	}

	// If connect_to_node_id is specified, connect the delegate output pin first
	// (required before SetFunction so the node can resolve the delegate signature)
	bool bConnected = false;
	if (!ConnectToNodeId.IsEmpty())
	{
		FGuid TargetNodeGuid;
		if (FGuid::Parse(ConnectToNodeId, TargetNodeGuid))
		{
			UEdGraphNode* TargetNode = nullptr;
			for (UEdGraphNode* Node : TargetGraph->Nodes)
			{
				if (Node && Node->NodeGuid == TargetNodeGuid)
				{
					TargetNode = Node;
					break;
				}
			}

			if (TargetNode)
			{
				// Find the target delegate pin
				FString PinName = ConnectToPin.IsEmpty() ? TEXT("Event") : ConnectToPin;
				UEdGraphPin* TargetDelegatePin = FMCPCommonUtils::FindPin(TargetNode, PinName, EGPD_Input);

				// Fallback: look for any unconnected delegate input pin
				if (!TargetDelegatePin)
				{
					for (UEdGraphPin* Pin : TargetNode->Pins)
					{
						if (Pin && Pin->Direction == EGPD_Input
							&& Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate
							&& Pin->LinkedTo.Num() == 0)
						{
							TargetDelegatePin = Pin;
							break;
						}
					}
				}

				if (TargetDelegatePin)
				{
					UEdGraphPin* DelegateOutPin = CreateDelegateNode->GetDelegateOutPin();
					if (DelegateOutPin)
					{
						const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
						if (Schema->TryCreateConnection(DelegateOutPin, TargetDelegatePin))
						{
							bConnected = true;
						}
					}
				}
			}
		}
	}

	// Set the function name
	CreateDelegateNode->SetFunction(FName(*FunctionName));

	// Trigger validation and GUID resolution
	CreateDelegateNode->HandleAnyChange(true);

	MarkBlueprintModified(Blueprint, Context);

	// Build result
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), CreateDelegateNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("function_name"), FunctionName);
	ResultData->SetBoolField(TEXT("connected"), bConnected);

	// Check validity by inspecting whether the function was resolved
	FName ResolvedName = CreateDelegateNode->GetFunctionName();
	bool bFunctionResolved = (ResolvedName != NAME_None);
	ResultData->SetBoolField(TEXT("function_resolved"), bFunctionResolved);

	if (!bFunctionResolved || !bConnected)
	{
		ResultData->SetStringField(TEXT("warning"),
			TEXT("Node created but may need manual setup. Ensure: (1) delegate output is connected to a delegate pin, (2) function_name exists in scope, (3) function signature matches the delegate."));
	}

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Component Bound Event (bind component delegates like OnTTSEnvelope)
// ============================================================================

bool FBindComponentEventAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ComponentName, EventName;
	if (!GetRequiredString(Params, TEXT("component_name"), ComponentName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("event_name"), EventName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FBindComponentEventAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ComponentName = Params->GetStringField(TEXT("component_name"));
	FString EventName = Params->GetStringField(TEXT("event_name"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		return CreateErrorResponse(TEXT("Invalid Blueprint or GeneratedClass not available. Compile the Blueprint first."));
	}

	// 1. Find the component as FObjectProperty on the GeneratedClass
	FObjectProperty* ComponentProp = FindFProperty<FObjectProperty>(
		Blueprint->GeneratedClass, FName(*ComponentName));
	if (!ComponentProp)
	{
		// Collect available component properties for diagnostic
		TArray<FString> AvailableComponents;
		for (TFieldIterator<FObjectProperty> It(Blueprint->GeneratedClass); It; ++It)
		{
			if (It->PropertyClass && It->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
			{
				AvailableComponents.Add(It->GetName());
			}
		}
		FString CompList = AvailableComponents.Num() > 0
			? FString::Join(AvailableComponents, TEXT(", "))
			: TEXT("(none)");
		return CreateErrorResponse(FString::Printf(
			TEXT("Component '%s' not found as a property on GeneratedClass. "
				 "The component must be added via SCS or declared as UPROPERTY in C++ parent. "
				 "Available component properties: %s"),
			*ComponentName, *CompList));
	}

	// 2. Find the delegate property on the component class
	UClass* ComponentClass = ComponentProp->PropertyClass;
	if (!ComponentClass)
	{
		return CreateErrorResponse(FString::Printf(
			TEXT("Component property '%s' has no valid PropertyClass."), *ComponentName));
	}

	FMulticastDelegateProperty* DelegateProp = nullptr;
	for (TFieldIterator<FMulticastDelegateProperty> It(ComponentClass); It; ++It)
	{
		if (It->GetFName() == FName(*EventName))
		{
			DelegateProp = *It;
			break;
		}
	}

	if (!DelegateProp)
	{
		// Collect available delegates for diagnostic
		TArray<FString> AvailableDelegates;
		for (TFieldIterator<FMulticastDelegateProperty> It(ComponentClass); It; ++It)
		{
			AvailableDelegates.Add(It->GetName());
		}
		FString DelegateList = AvailableDelegates.Num() > 0
			? FString::Join(AvailableDelegates, TEXT(", "))
			: TEXT("(none)");
		return CreateErrorResponse(FString::Printf(
			TEXT("Delegate '%s' not found on component class '%s'. Available delegates: %s"),
			*EventName, *ComponentClass->GetName(), *DelegateList));
	}

	// 3. Get the event graph
	UEdGraph* EventGraph = GetTargetGraph(Params, Context);
	if (!EventGraph)
	{
		EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	}
	if (!EventGraph)
	{
		return CreateErrorResponse(TEXT("Failed to find event graph"));
	}

	// 4. Check if a ComponentBoundEvent node already exists for this combo
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		UK2Node_ComponentBoundEvent* ExistingEvent = Cast<UK2Node_ComponentBoundEvent>(Node);
		if (ExistingEvent &&
			ExistingEvent->ComponentPropertyName == FName(*ComponentName) &&
			ExistingEvent->DelegatePropertyName == DelegateProp->GetFName())
		{
			TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
			ResultData->SetBoolField(TEXT("already_exists"), true);
			ResultData->SetStringField(TEXT("component_name"), ComponentName);
			ResultData->SetStringField(TEXT("event_name"), EventName);
			ResultData->SetStringField(TEXT("node_id"), ExistingEvent->NodeGuid.ToString());
			return CreateSuccessResponse(ResultData);
		}
	}

	// 5. Create UK2Node_ComponentBoundEvent using the standard engine initializer
	UK2Node_ComponentBoundEvent* EventNode = NewObject<UK2Node_ComponentBoundEvent>(EventGraph);
	EventGraph->AddNode(EventNode, false, false);
	EventNode->CreateNewGuid();

	// Use the engine's standard initialization (sets ComponentPropertyName,
	// DelegatePropertyName, DelegateOwnerClass, EventReference,
	// CustomFunctionName, bOverrideFunction, bInternalEvent)
	EventNode->InitializeComponentBoundEventParams(ComponentProp, DelegateProp);

	EventNode->NodePosX = static_cast<int32>(Position.X);
	EventNode->NodePosY = static_cast<int32>(Position.Y);
	EventNode->AllocateDefaultPins();

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(EventNode, Context);

	// 6. Build result with output pin info
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), EventNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("component_name"), ComponentName);
	ResultData->SetStringField(TEXT("event_name"), EventName);
	ResultData->SetStringField(TEXT("component_class"), ComponentClass->GetName());

	// List output pins for the caller
	TArray<TSharedPtr<FJsonValue>> OutputPins;
	for (const UEdGraphPin* Pin : EventNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output)
		{
			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			OutputPins.Add(MakeShared<FJsonValueObject>(PinObj));
		}
	}
	ResultData->SetArrayField(TEXT("output_pins"), OutputPins);

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Spawn Actor Nodes
// ============================================================================

bool FAddSpawnActorFromClassNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ClassToSpawn;
	if (!GetRequiredString(Params, TEXT("class_to_spawn"), ClassToSpawn, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddSpawnActorFromClassNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ClassToSpawn = Params->GetStringField(TEXT("class_to_spawn"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the class to spawn
	UClass* SpawnClass = nullptr;

	// First, try to find as a blueprint
	UBlueprint* SpawnBP = FMCPCommonUtils::FindBlueprint(ClassToSpawn);
	if (SpawnBP && SpawnBP->GeneratedClass)
	{
		SpawnClass = SpawnBP->GeneratedClass;
	}

	// Try content path
	if (!SpawnClass && ClassToSpawn.StartsWith(TEXT("/Game/")))
	{
		FString BPPath = ClassToSpawn;
		if (!BPPath.EndsWith(TEXT("_C")))
		{
			BPPath += TEXT("_C");
		}
		SpawnClass = LoadClass<AActor>(nullptr, *BPPath);
		if (!SpawnClass)
		{
			SpawnClass = LoadClass<AActor>(nullptr, *ClassToSpawn);
		}
	}

	// Try engine classes
	if (!SpawnClass)
	{
		static const FString ModulePaths[] = {
			TEXT("/Script/Engine"),
			TEXT("/Script/CoreUObject")
		};
		for (const FString& ModulePath : ModulePaths)
		{
			FString FullPath = FString::Printf(TEXT("%s.%s"), *ModulePath, *ClassToSpawn);
			SpawnClass = LoadClass<AActor>(nullptr, *FullPath);
			if (SpawnClass) break;
		}
	}

	if (!SpawnClass)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Class to spawn not found: %s"), *ClassToSpawn));
	}

	UK2Node_SpawnActorFromClass* SpawnNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_SpawnActorFromClass>(
		TargetGraph, Position, EK2NewNodeFlags::None
	);

	// Set the class to spawn (must be done after pins are allocated)
	UEdGraphPin* ClassPin = SpawnNode->GetClassPin();
	if (ClassPin)
	{
		const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(TargetGraph->GetSchema());
		if (K2Schema)
		{
			K2Schema->TrySetDefaultObject(*ClassPin, SpawnClass);
		}
	}
	SpawnNode->ReconstructNode();

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(SpawnNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), SpawnNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("class_to_spawn"), SpawnClass->GetName());
	return CreateSuccessResponse(ResultData);
}


bool FCallBlueprintFunctionAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString TargetBlueprint, FunctionName;
	if (!GetRequiredString(Params, TEXT("target_blueprint"), TargetBlueprint, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("function_name"), FunctionName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FCallBlueprintFunctionAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString TargetBlueprintName = Params->GetStringField(TEXT("target_blueprint"));
	FString FunctionName = Params->GetStringField(TEXT("function_name"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the target blueprint
	UBlueprint* TargetBlueprint = FMCPCommonUtils::FindBlueprint(TargetBlueprintName);
	if (!TargetBlueprint)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Target blueprint not found: %s"), *TargetBlueprintName));
	}

	// Ensure target is compiled
	if (!TargetBlueprint->GeneratedClass)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Target blueprint not compiled: %s"), *TargetBlueprintName));
	}

	// Find the function
	UFunction* Function = TargetBlueprint->GeneratedClass->FindFunctionByName(*FunctionName);
	if (!Function)
	{
		// Check if graph exists
		bool bFoundGraph = false;
		for (UEdGraph* Graph : TargetBlueprint->FunctionGraphs)
		{
			if (Graph && Graph->GetFName() == FName(*FunctionName))
			{
				bFoundGraph = true;
				break;
			}
		}

		if (bFoundGraph)
		{
			return CreateErrorResponse(FString::Printf(
				TEXT("Function '%s' exists but is not compiled. Compile '%s' first."),
				*FunctionName, *TargetBlueprintName));
		}
		else
		{
			// List available functions
			TArray<FString> AvailableFunctions;
			for (TFieldIterator<UFunction> FuncIt(TargetBlueprint->GeneratedClass); FuncIt; ++FuncIt)
			{
				if ((*FuncIt)->HasAnyFunctionFlags(FUNC_BlueprintCallable))
				{
					AvailableFunctions.Add((*FuncIt)->GetName());
				}
			}
			FString AvailableStr = FString::Join(AvailableFunctions, TEXT(", "));
			return CreateErrorResponse(FString::Printf(
				TEXT("Function '%s' not found in '%s'. Available: %s"),
				*FunctionName, *TargetBlueprintName, *AvailableStr));
		}
	}

	UClass* TargetGenClass = TargetBlueprint->GeneratedClass;
	bool bIsSelfCall = (Blueprint == TargetBlueprint);
	UK2Node_CallFunction* FunctionNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CallFunction>(
		TargetGraph, Position, EK2NewNodeFlags::None,
		[Function, TargetGenClass, bIsSelfCall](UK2Node_CallFunction* Node) {
			if (bIsSelfCall)
			{
				Node->FunctionReference.SetSelfMember(Function->GetFName());
			}
			else
			{
				Node->FunctionReference.SetExternalMember(Function->GetFName(), TargetGenClass);
			}
		}
	);

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(FunctionNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), FunctionNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("function_name"), FunctionName);
	ResultData->SetStringField(TEXT("target_blueprint"), TargetBlueprintName);
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// External Object Property Nodes
// ============================================================================

bool FSetObjectPropertyAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString PropertyName, OwnerClass;
	if (!GetRequiredString(Params, TEXT("property_name"), PropertyName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("owner_class"), OwnerClass, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FSetObjectPropertyAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString PropertyName = Params->GetStringField(TEXT("property_name"));
	FString OwnerClassName = Params->GetStringField(TEXT("owner_class"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the owner class - try /Script/Engine first (most common), then blueprint
	UClass* OwnerClass = LoadClass<UObject>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *OwnerClassName));
	if (!OwnerClass)
	{
		UBlueprint* OwnerBP = FMCPCommonUtils::FindBlueprint(OwnerClassName);
		if (OwnerBP) OwnerClass = OwnerBP->GeneratedClass;
	}

	if (!OwnerClass)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Class not found: %s"), *OwnerClassName));
	}

	// Verify property exists
	FProperty* Property = OwnerClass->FindPropertyByName(FName(*PropertyName));
	if (!Property)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Property '%s' not found on '%s'"), *PropertyName, *OwnerClassName));
	}

	// Create Set node with external member reference
	UK2Node_VariableSet* VarSetNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableSet>(
		TargetGraph, Position, EK2NewNodeFlags::None,
		[&PropertyName, OwnerClass](UK2Node_VariableSet* Node) {
			Node->VariableReference.SetExternalMember(FName(*PropertyName), OwnerClass);
		}
	);
	VarSetNode->ReconstructNode();

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(VarSetNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), VarSetNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("property_name"), PropertyName);
	ResultData->SetStringField(TEXT("owner_class"), OwnerClass->GetName());
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Sequence Node
// ============================================================================

bool FAddSequenceNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddSequenceNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	UK2Node_ExecutionSequence* SeqNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_ExecutionSequence>(
		TargetGraph, Position, EK2NewNodeFlags::None
	);

	if (!SeqNode)
	{
		return CreateErrorResponse(TEXT("Failed to create Sequence node"));
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(SeqNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), SeqNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Macro Instance Nodes
// ============================================================================

UEdGraph* FAddMacroInstanceNodeAction::FindMacroGraph(const FString& MacroName) const
{
	UBlueprint* MacroBP = LoadObject<UBlueprint>(nullptr,
		TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
	if (!MacroBP) return nullptr;

	for (UEdGraph* Graph : MacroBP->MacroGraphs)
	{
		if (Graph && Graph->GetFName().ToString().Equals(MacroName, ESearchCase::IgnoreCase))
			return Graph;
	}
	return nullptr;
}

bool FAddMacroInstanceNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MacroName;
	if (!GetRequiredString(Params, TEXT("macro_name"), MacroName, OutError)) return false;
	if (!FindMacroGraph(MacroName))
	{
		OutError = FString::Printf(TEXT("Macro '%s' not found in StandardMacros"), *MacroName);
		return false;
	}
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddMacroInstanceNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString MacroName = Params->GetStringField(TEXT("macro_name"));
	UEdGraph* MacroGraph = FindMacroGraph(MacroName);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	UK2Node_MacroInstance* MacroNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_MacroInstance>(
		TargetGraph, GetNodePosition(Params), EK2NewNodeFlags::None,
		[MacroGraph](UK2Node_MacroInstance* Node) { Node->SetMacroGraph(MacroGraph); }
	);

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(MacroNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), MacroNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Struct Nodes
// ============================================================================

bool FAddMakeStructNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString StructType;
	if (!GetRequiredString(Params, TEXT("struct_type"), StructType, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddMakeStructNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString StructType = Params->GetStringField(TEXT("struct_type"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Resolve struct type - try common structs first
	UScriptStruct* ScriptStruct = nullptr;
	FString StructTypeLower = StructType.ToLower();

	// Common struct mappings
	if (StructTypeLower == TEXT("intpoint") || StructTypeLower == TEXT("fintpoint"))
	{
		ScriptStruct = TBaseStructure<FIntPoint>::Get();
	}
	else if (StructTypeLower == TEXT("vector") || StructTypeLower == TEXT("fvector"))
	{
		ScriptStruct = TBaseStructure<FVector>::Get();
	}
	else if (StructTypeLower == TEXT("vector2d") || StructTypeLower == TEXT("fvector2d"))
	{
		ScriptStruct = TBaseStructure<FVector2D>::Get();
	}
	else if (StructTypeLower == TEXT("rotator") || StructTypeLower == TEXT("frotator"))
	{
		ScriptStruct = TBaseStructure<FRotator>::Get();
	}
	else if (StructTypeLower == TEXT("transform") || StructTypeLower == TEXT("ftransform"))
	{
		ScriptStruct = TBaseStructure<FTransform>::Get();
	}
	else if (StructTypeLower == TEXT("linearcolor") || StructTypeLower == TEXT("flinearcolor"))
	{
		ScriptStruct = TBaseStructure<FLinearColor>::Get();
	}
	else if (StructTypeLower == TEXT("color") || StructTypeLower == TEXT("fcolor"))
	{
		ScriptStruct = TBaseStructure<FColor>::Get();
	}
	else
	{
		// Try to find by name
		FString FullStructName = StructType;
		if (!FullStructName.StartsWith(TEXT("F")))
		{
			FullStructName = TEXT("F") + FullStructName;
		}
		ScriptStruct = FindObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("/Script/CoreUObject.%s"), *FullStructName));
		if (!ScriptStruct)
		{
			ScriptStruct = FindObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *FullStructName));
		}
	}

	if (!ScriptStruct)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Struct type not found: %s"), *StructType));
	}

	UK2Node_MakeStruct* MakeStructNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_MakeStruct>(
		TargetGraph, Position, EK2NewNodeFlags::None,
		[ScriptStruct](UK2Node_MakeStruct* Node) { Node->StructType = ScriptStruct; }
	);

	if (!MakeStructNode)
	{
		return CreateErrorResponse(TEXT("Failed to create Make Struct node"));
	}

	// Set pin defaults if provided
	const TSharedPtr<FJsonObject>* PinDefaults = nullptr;
	if (Params->TryGetObjectField(TEXT("pin_defaults"), PinDefaults))
	{
		for (const auto& Pair : (*PinDefaults)->Values)
		{
			FString Value;
			if (Pair.Value->TryGetString(Value))
			{
				UEdGraphPin* Pin = FMCPCommonUtils::FindPin(MakeStructNode, Pair.Key, EGPD_Input);
				if (Pin)
				{
					Pin->DefaultValue = Value;
				}
			}
		}
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(MakeStructNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), MakeStructNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("struct_type"), ScriptStruct->GetName());
	return CreateSuccessResponse(ResultData);
}


bool FAddBreakStructNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString StructType;
	if (!GetRequiredString(Params, TEXT("struct_type"), StructType, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBreakStructNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString StructType = Params->GetStringField(TEXT("struct_type"));
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Resolve struct type - same logic as MakeStruct
	UScriptStruct* ScriptStruct = nullptr;
	FString StructTypeLower = StructType.ToLower();

	if (StructTypeLower == TEXT("intpoint") || StructTypeLower == TEXT("fintpoint"))
	{
		ScriptStruct = TBaseStructure<FIntPoint>::Get();
	}
	else if (StructTypeLower == TEXT("vector") || StructTypeLower == TEXT("fvector"))
	{
		ScriptStruct = TBaseStructure<FVector>::Get();
	}
	else if (StructTypeLower == TEXT("vector2d") || StructTypeLower == TEXT("fvector2d"))
	{
		ScriptStruct = TBaseStructure<FVector2D>::Get();
	}
	else if (StructTypeLower == TEXT("rotator") || StructTypeLower == TEXT("frotator"))
	{
		ScriptStruct = TBaseStructure<FRotator>::Get();
	}
	else if (StructTypeLower == TEXT("transform") || StructTypeLower == TEXT("ftransform"))
	{
		ScriptStruct = TBaseStructure<FTransform>::Get();
	}
	else if (StructTypeLower == TEXT("linearcolor") || StructTypeLower == TEXT("flinearcolor"))
	{
		ScriptStruct = TBaseStructure<FLinearColor>::Get();
	}
	else if (StructTypeLower == TEXT("color") || StructTypeLower == TEXT("fcolor"))
	{
		ScriptStruct = TBaseStructure<FColor>::Get();
	}
	else
	{
		FString FullStructName = StructType;
		if (!FullStructName.StartsWith(TEXT("F")))
		{
			FullStructName = TEXT("F") + FullStructName;
		}
		ScriptStruct = FindObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("/Script/CoreUObject.%s"), *FullStructName));
		if (!ScriptStruct)
		{
			ScriptStruct = FindObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *FullStructName));
		}
	}

	if (!ScriptStruct)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Struct type not found: %s"), *StructType));
	}

	UK2Node_BreakStruct* BreakStructNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_BreakStruct>(
		TargetGraph, Position, EK2NewNodeFlags::None,
		[ScriptStruct](UK2Node_BreakStruct* Node) { Node->StructType = ScriptStruct; }
	);

	if (!BreakStructNode)
	{
		return CreateErrorResponse(TEXT("Failed to create Break Struct node"));
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(BreakStructNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), BreakStructNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("struct_type"), ScriptStruct->GetName());
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Switch Nodes
// ============================================================================

bool FAddSwitchOnStringNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddSwitchOnStringNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	UK2Node_SwitchString* SwitchNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_SwitchString>(
		TargetGraph, Position, EK2NewNodeFlags::None,
		[](UK2Node_SwitchString* Node) {}
	);

	if (!SwitchNode)
	{
		return CreateErrorResponse(TEXT("Failed to create Switch on String node"));
	}

	// Add cases if provided
	const TArray<TSharedPtr<FJsonValue>>* CasesArray = nullptr;
	if (Params->TryGetArrayField(TEXT("cases"), CasesArray))
	{
		for (const TSharedPtr<FJsonValue>& CaseValue : *CasesArray)
		{
			FString CaseString;
			if (CaseValue->TryGetString(CaseString))
			{
				SwitchNode->PinNames.Add(FName(*CaseString));
			}
		}
		// Reconstruct node to add the pins
		SwitchNode->ReconstructNode();
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(SwitchNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), SwitchNode->NodeGuid.ToString());

	// Return available output pins
	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (UEdGraphPin* Pin : SwitchNode->Pins)
	{
		if (Pin->Direction == EGPD_Output && !Pin->bHidden)
		{
			PinsArray.Add(MakeShared<FJsonValueString>(Pin->PinName.ToString()));
		}
	}
	ResultData->SetArrayField(TEXT("output_pins"), PinsArray);

	return CreateSuccessResponse(ResultData);
}


bool FAddSwitchOnIntNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddSwitchOnIntNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	UK2Node_SwitchInteger* SwitchNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_SwitchInteger>(
		TargetGraph, Position, EK2NewNodeFlags::None,
		[](UK2Node_SwitchInteger* Node) {}
	);

	if (!SwitchNode)
	{
		return CreateErrorResponse(TEXT("Failed to create Switch on Int node"));
	}

	// Set start index if provided
	int32 StartIdx = 0;
	if (Params->TryGetNumberField(TEXT("start_index"), StartIdx))
	{
		SwitchNode->StartIndex = StartIdx;
	}

	// Add cases by calling AddPinToSwitchNode for each case
	const TArray<TSharedPtr<FJsonValue>>* CasesArray = nullptr;
	if (Params->TryGetArrayField(TEXT("cases"), CasesArray))
	{
		for (int32 i = 0; i < CasesArray->Num(); ++i)
		{
			SwitchNode->AddPinToSwitchNode();
		}
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(SwitchNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), SwitchNode->NodeGuid.ToString());

	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (UEdGraphPin* Pin : SwitchNode->Pins)
	{
		if (Pin->Direction == EGPD_Output && !Pin->bHidden)
		{
			PinsArray.Add(MakeShared<FJsonValueString>(Pin->PinName.ToString()));
		}
	}
	ResultData->SetArrayField(TEXT("output_pins"), PinsArray);

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Local Variables
// ============================================================================

bool FAddFunctionLocalVariableAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString FunctionName, VariableName, VariableType;
	if (!GetRequiredString(Params, TEXT("function_name"), FunctionName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("variable_name"), VariableName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("variable_type"), VariableType, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddFunctionLocalVariableAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString FunctionName = Params->GetStringField(TEXT("function_name"));
	FString VariableName = Params->GetStringField(TEXT("variable_name"));
	FString VariableType = Params->GetStringField(TEXT("variable_type"));
	FString DefaultValue = GetOptionalString(Params, TEXT("default_value"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Find the function graph
	UEdGraph* FunctionGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph->GetFName() == FName(*FunctionName))
		{
			FunctionGraph = Graph;
			break;
		}
	}
	if (!FunctionGraph)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Function '%s' not found in Blueprint"), *FunctionName));
	}

	// Find the FunctionEntry node
	UK2Node_FunctionEntry* EntryNode = nullptr;
	for (UEdGraphNode* Node : FunctionGraph->Nodes)
	{
		EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (EntryNode) break;
	}
	if (!EntryNode)
	{
		return CreateErrorResponse(TEXT("Function entry node not found"));
	}

	// Resolve pin type
	FEdGraphPinType PinType;
	FString TypeResolveError;
	if (!FMCPCommonUtils::ResolvePinTypeFromString(VariableType, PinType, TypeResolveError))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Function local variable '%s': %s"), *VariableName, *TypeResolveError));
	}

	// Add local variable via entry node's LocalVariables array
	FBPVariableDescription NewVar;
	NewVar.VarName = FName(*VariableName);
	NewVar.VarGuid = FGuid::NewGuid();
	NewVar.VarType = PinType;
	NewVar.PropertyFlags |= CPF_BlueprintVisible;
	NewVar.FriendlyName = FName::NameToDisplayString(VariableName, PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean);
	NewVar.Category = UEdGraphSchema_K2::VR_DefaultCategory;
	if (!DefaultValue.IsEmpty())
	{
		NewVar.DefaultValue = DefaultValue;
	}

	EntryNode->LocalVariables.Add(NewVar);
	EntryNode->ReconstructNode();
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("variable_name"), VariableName);
	ResultData->SetStringField(TEXT("variable_type"), VariableType);
	ResultData->SetStringField(TEXT("function_name"), FunctionName);
	if (!DefaultValue.IsEmpty())
	{
		ResultData->SetStringField(TEXT("default_value"), DefaultValue);
	}
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Variable Default Values
// ============================================================================

bool FSetBlueprintVariableDefaultAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString VariableName, DefaultValue;
	if (!GetRequiredString(Params, TEXT("variable_name"), VariableName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("default_value"), DefaultValue, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FSetBlueprintVariableDefaultAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString VariableName = Params->GetStringField(TEXT("variable_name"));
	FString DefaultValue = Params->GetStringField(TEXT("default_value"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	bool bFound = false;
	for (FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName == FName(*VariableName))
		{
			Variable.DefaultValue = DefaultValue;
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Variable '%s' not found in Blueprint"), *VariableName));
	}

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("variable_name"), VariableName);
	ResultData->SetStringField(TEXT("default_value"), DefaultValue);
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Comment Nodes
// ============================================================================

bool FAddBlueprintCommentAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString CommentText;
	if (!GetRequiredString(Params, TEXT("comment_text"), CommentText, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintCommentAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString CommentText = Params->GetStringField(TEXT("comment_text"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Get graph — empty string triggers FindGraph's default (EventGraph) fallback
	FString GraphName = GetOptionalString(Params, TEXT("graph_name"));
	FString GraphError;
	UEdGraph* Graph = FindGraph(Blueprint, GraphName, GraphError);
	if (!Graph)
	{
		return CreateErrorResponse(GraphError);
	}

	// Create the comment node
	UEdGraphNode_Comment* CommentNode = NewObject<UEdGraphNode_Comment>(Graph);
	CommentNode->CreateNewGuid();
	CommentNode->NodeComment = CommentText;

	// Position
	FVector2D NodePos = GetNodePosition(Params);
	CommentNode->NodePosX = NodePos.X;
	CommentNode->NodePosY = NodePos.Y;

	// Size (optional)
	const TArray<TSharedPtr<FJsonValue>>* SizeArray = nullptr;
	if (Params->TryGetArrayField(TEXT("size"), SizeArray) && SizeArray->Num() >= 2)
	{
		CommentNode->NodeWidth = static_cast<int32>((*SizeArray)[0]->AsNumber());
		CommentNode->NodeHeight = static_cast<int32>((*SizeArray)[1]->AsNumber());
	}
	else
	{
		CommentNode->NodeWidth = 400;
		CommentNode->NodeHeight = 200;
	}

	// Color (optional RGBA)
	const TArray<TSharedPtr<FJsonValue>>* ColorArray = nullptr;
	if (Params->TryGetArrayField(TEXT("color"), ColorArray) && ColorArray->Num() >= 3)
	{
		float R = static_cast<float>((*ColorArray)[0]->AsNumber());
		float G = static_cast<float>((*ColorArray)[1]->AsNumber());
		float B = static_cast<float>((*ColorArray)[2]->AsNumber());
		float A = ColorArray->Num() >= 4 ? static_cast<float>((*ColorArray)[3]->AsNumber()) : 1.0f;
		CommentNode->CommentColor = FLinearColor(R, G, B, A);
	}

	Graph->AddNode(CommentNode, true, false);
	CommentNode->SetFlags(RF_Transactional);

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(CommentNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), CommentNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("comment_text"), CommentText);
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Auto Comment — auto-sized comment wrapping specified nodes
// ============================================================================

bool FAutoCommentAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString CommentText;
	if (!GetRequiredString(Params, TEXT("comment_text"), CommentText, OutError)) return false;

	// node_ids is optional — when omitted, wraps all non-comment nodes in the graph

	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAutoCommentAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString CommentText = Params->GetStringField(TEXT("comment_text"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Get graph — empty string triggers FindGraph's default (EventGraph) fallback
	FString GraphName = GetOptionalString(Params, TEXT("graph_name"));
	FString GraphError;
	UEdGraph* Graph = FindGraph(Blueprint, GraphName, GraphError);
	if (!Graph)
	{
		return CreateErrorResponse(GraphError);
	}

	// Parse node_ids (optional — when omitted, wraps all non-comment nodes)
	const TArray<TSharedPtr<FJsonValue>>* NodeIdsArray = nullptr;
	bool bHasNodeIds = Params->TryGetArrayField(TEXT("node_ids"), NodeIdsArray) && NodeIdsArray && NodeIdsArray->Num() > 0;

	// Padding and title height
	float Padding = 40.f;
	if (Params->HasField(TEXT("padding")))
	{
		Padding = static_cast<float>(Params->GetNumberField(TEXT("padding")));
	}

	float TitleHeight = 36.f;
	if (Params->HasField(TEXT("title_height")))
	{
		TitleHeight = static_cast<float>(Params->GetNumberField(TEXT("title_height")));
	}

	// Layout settings for GetNodeSize fallback
	FBlueprintLayoutSettings LayoutSettings;

	// Build list of nodes to wrap
	TArray<UEdGraphNode*> NodesToWrap;
	TArray<FString> MissingNodes;

	if (bHasNodeIds)
	{
		// Explicit node_ids mode — find each specified node
		for (const TSharedPtr<FJsonValue>& NodeIdValue : *NodeIdsArray)
		{
			FString NodeIdStr = NodeIdValue->AsString();
			FGuid NodeGuid;
			if (!FGuid::Parse(NodeIdStr, NodeGuid))
			{
				MissingNodes.Add(NodeIdStr + TEXT(" (invalid GUID)"));
				continue;
			}

			UEdGraphNode* FoundNode = nullptr;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node && Node->NodeGuid == NodeGuid)
				{
					FoundNode = Node;
					break;
				}
			}

			if (!FoundNode)
			{
				MissingNodes.Add(NodeIdStr);
				continue;
			}

			// Skip comment nodes in bounding box calculation
			if (!Cast<UEdGraphNode_Comment>(FoundNode))
			{
				NodesToWrap.Add(FoundNode);
			}
		}
	}
	else
	{
		// wrap_all mode — include every non-comment node in the graph
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && !Cast<UEdGraphNode_Comment>(Node))
			{
				NodesToWrap.Add(Node);
			}
		}
	}

	if (NodesToWrap.Num() == 0)
	{
		FString ErrorMsg = TEXT("No valid nodes found to wrap.");
		if (MissingNodes.Num() > 0)
		{
			ErrorMsg += TEXT(" Missing: ") + FString::Join(MissingNodes, TEXT(", "));
		}
		return CreateErrorResponse(ErrorMsg);
	}

	// Calculate bounding box
	float MinX = TNumericLimits<float>::Max();
	float MinY = TNumericLimits<float>::Max();
	float MaxX = TNumericLimits<float>::Lowest();
	float MaxY = TNumericLimits<float>::Lowest();

	for (UEdGraphNode* WrapNode : NodesToWrap)
	{
		float NodeX = static_cast<float>(WrapNode->NodePosX);
		float NodeY = static_cast<float>(WrapNode->NodePosY);
		FVector2D NodeSize = FBlueprintAutoLayout::GetNodeSize(WrapNode, LayoutSettings);

		MinX = FMath::Min(MinX, NodeX);
		MinY = FMath::Min(MinY, NodeY);
		MaxX = FMath::Max(MaxX, NodeX + NodeSize.X);
		MaxY = FMath::Max(MaxY, NodeY + NodeSize.Y);
	}

	// Calculate comment position and size
	float CommentX = MinX - Padding;
	float CommentY = MinY - Padding - TitleHeight;
	float CommentBottom = MaxY + Padding;
	float CommentRight = MaxX + Padding;

	// ================================================================
	// Collision avoidance: push new comment's top edge above any
	// overlapping existing comment so title bars never overlap.
	// We keep the bottom edge fixed (must still cover target nodes)
	// and only grow upward.  Iterate up to 5 times for cascading.
	// ================================================================
	const float CommentGap = 60.f;  // min vertical gap between titles
	float OriginalCommentY = CommentY;  // track for collision_adjusted flag

	for (int32 Iter = 0; Iter < 5; ++Iter)
	{
		bool bAnyOverlap = false;
		for (UEdGraphNode* ExistingNode : Graph->Nodes)
		{
			UEdGraphNode_Comment* ExistingComment = Cast<UEdGraphNode_Comment>(ExistingNode);
			if (!ExistingComment) continue;

			float ExLeft   = static_cast<float>(ExistingComment->NodePosX);
			float ExTop    = static_cast<float>(ExistingComment->NodePosY);
			float ExRight  = ExLeft + static_cast<float>(ExistingComment->NodeWidth);
			float ExBottom = ExTop  + static_cast<float>(ExistingComment->NodeHeight);

			// Check AABB overlap (both axes must overlap)
			bool bOverlapX = (CommentX < ExRight) && (CommentRight > ExLeft);
			bool bOverlapY = (CommentY < ExBottom) && (CommentBottom > ExTop);

			if (bOverlapX && bOverlapY)
			{
				// Push our top edge above the existing comment's top - gap
				float NeededTop = ExTop - CommentGap;
				if (NeededTop < CommentY)
				{
					CommentY = NeededTop;
					bAnyOverlap = true;
				}
			}
		}
		if (!bAnyOverlap) break;
	}

	// ================================================================
	// Minimum width from comment title text
	// Ensure the comment box is wide enough to display its title.
	// Comment title uses ~10px/Latin char, CJK ~1.8x wider.
	// ================================================================
	{
		const float CommentCharW = 10.f;
		float TitleTextWidth = 0.f;
		for (TCHAR Ch : CommentText)
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
		// Add left/right margin for the title bar (bubble icon + padding)
		const float TitleMargin = 40.f;
		float MinCommentWidth = TitleTextWidth + TitleMargin;
		float CurrentWidth = CommentRight - CommentX;
		if (MinCommentWidth > CurrentWidth)
		{
			// Expand symmetrically from center
			float Expand = (MinCommentWidth - CurrentWidth) * 0.5f;
			CommentX -= Expand;
			CommentRight += Expand;
		}
	}

	int32 CommentWidth  = FMath::RoundToInt(CommentRight - CommentX);
	int32 CommentHeight = FMath::RoundToInt(CommentBottom - CommentY);

	// Create the comment node
	UEdGraphNode_Comment* CommentNode = NewObject<UEdGraphNode_Comment>(Graph);
	CommentNode->CreateNewGuid();
	CommentNode->NodeComment = CommentText;
	CommentNode->NodePosX = FMath::RoundToInt(CommentX);
	CommentNode->NodePosY = FMath::RoundToInt(CommentY);
	CommentNode->NodeWidth = CommentWidth;
	CommentNode->NodeHeight = CommentHeight;

	// Color (optional RGBA)
	const TArray<TSharedPtr<FJsonValue>>* ColorArray = nullptr;
	if (Params->TryGetArrayField(TEXT("color"), ColorArray) && ColorArray->Num() >= 3)
	{
		float R = static_cast<float>((*ColorArray)[0]->AsNumber());
		float G = static_cast<float>((*ColorArray)[1]->AsNumber());
		float B = static_cast<float>((*ColorArray)[2]->AsNumber());
		float A = ColorArray->Num() >= 4 ? static_cast<float>((*ColorArray)[3]->AsNumber()) : 1.0f;
		CommentNode->CommentColor = FLinearColor(R, G, B, A);
	}

	Graph->AddNode(CommentNode, true, false);
	CommentNode->SetFlags(RF_Transactional);

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(CommentNode, Context);

	// Build response
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), CommentNode->NodeGuid.ToString());
	ResultData->SetStringField(TEXT("comment_text"), CommentText);

	TArray<TSharedPtr<FJsonValue>> PosArray;
	PosArray.Add(MakeShared<FJsonValueNumber>(CommentX));
	PosArray.Add(MakeShared<FJsonValueNumber>(CommentY));
	ResultData->SetArrayField(TEXT("position"), PosArray);

	TArray<TSharedPtr<FJsonValue>> SizeArray;
	SizeArray.Add(MakeShared<FJsonValueNumber>(CommentWidth));
	SizeArray.Add(MakeShared<FJsonValueNumber>(CommentHeight));
	ResultData->SetArrayField(TEXT("size"), SizeArray);

	ResultData->SetNumberField(TEXT("nodes_wrapped"), NodesToWrap.Num());
	ResultData->SetBoolField(TEXT("wrap_all"), !bHasNodeIds);
	ResultData->SetBoolField(TEXT("collision_adjusted"), CommentY < OriginalCommentY);

	if (MissingNodes.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> MissingArray;
		for (const FString& Id : MissingNodes)
		{
			MissingArray.Add(MakeShared<FJsonValueString>(Id));
		}
		ResultData->SetArrayField(TEXT("missing_nodes"), MissingArray);
	}

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// P1 — Variable & Function Management
// ============================================================================

bool FDeleteBlueprintVariableAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString VariableName;
	if (!GetRequiredString(Params, TEXT("variable_name"), VariableName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FDeleteBlueprintVariableAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString VariableName = Params->GetStringField(TEXT("variable_name"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Check variable exists
	bool bFound = false;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName == FName(*VariableName))
		{
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Variable '%s' not found in Blueprint"), *VariableName));
	}

	// Remove the variable using the editor utility
	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, FName(*VariableName));
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("deleted_variable"), VariableName);
	return CreateSuccessResponse(ResultData);
}


bool FRenameBlueprintVariableAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString OldName, NewName;
	if (!GetRequiredString(Params, TEXT("old_name"), OldName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("new_name"), NewName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FRenameBlueprintVariableAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString OldName = Params->GetStringField(TEXT("old_name"));
	FString NewName = Params->GetStringField(TEXT("new_name"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Check old variable exists
	bool bFound = false;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName == FName(*OldName))
		{
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Variable '%s' not found in Blueprint"), *OldName));
	}

	// Check new name not already taken
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName == FName(*NewName))
		{
			return CreateErrorResponse(FString::Printf(TEXT("Variable '%s' already exists in Blueprint"), *NewName));
		}
	}

	FBlueprintEditorUtils::RenameMemberVariable(Blueprint, FName(*OldName), FName(*NewName));
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("old_name"), OldName);
	ResultData->SetStringField(TEXT("new_name"), NewName);
	return CreateSuccessResponse(ResultData);
}


bool FSetVariableMetadataAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString VariableName;
	if (!GetRequiredString(Params, TEXT("variable_name"), VariableName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FSetVariableMetadataAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString VariableName = Params->GetStringField(TEXT("variable_name"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Find the variable
	FBPVariableDescription* TargetVar = nullptr;
	for (FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName == FName(*VariableName))
		{
			TargetVar = &Variable;
			break;
		}
	}

	if (!TargetVar)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Variable '%s' not found in Blueprint"), *VariableName));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("variable_name"), VariableName);

	// Category
	FString Category = GetOptionalString(Params, TEXT("category"));
	if (!Category.IsEmpty())
	{
		FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, FName(*VariableName), nullptr, FText::FromString(Category));
		ResultData->SetStringField(TEXT("category"), Category);
	}

	// Tooltip
	FString Tooltip = GetOptionalString(Params, TEXT("tooltip"));
	if (!Tooltip.IsEmpty())
	{
		TargetVar->SetMetaData(FBlueprintMetadata::MD_Tooltip, *Tooltip);
		ResultData->SetStringField(TEXT("tooltip"), Tooltip);
	}

	// Instance Editable (expose to editor details)
	// Use UE official API: SetBlueprintOnlyEditableFlag handles CPF_DisableEditOnInstance
	// AND calls MarkBlueprintAsStructurallyModified internally, which is required
	// for the Blueprint editor UI (eye icon) to refresh.
	bool bDidChangeInstanceEditable = false;
	if (Params->HasField(TEXT("instance_editable")))
	{
		bool bEditable = Params->GetBoolField(TEXT("instance_editable"));
		if (bEditable)
		{
			TargetVar->PropertyFlags |= CPF_Edit;
		}
		else
		{
			TargetVar->PropertyFlags &= ~CPF_Edit;
		}
		// bNewBlueprintOnly = !bEditable: true means "blueprint only" (DisableEditOnInstance)
		FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, FName(*VariableName), !bEditable);
		bDidChangeInstanceEditable = true;
		ResultData->SetBoolField(TEXT("instance_editable"), bEditable);
	}

	// Blueprint Read Only
	if (Params->HasField(TEXT("blueprint_read_only")))
	{
		bool bReadOnly = Params->GetBoolField(TEXT("blueprint_read_only"));
		if (bReadOnly)
		{
			TargetVar->PropertyFlags |= CPF_BlueprintReadOnly;
			TargetVar->PropertyFlags &= ~CPF_BlueprintVisible;
		}
		else
		{
			TargetVar->PropertyFlags &= ~CPF_BlueprintReadOnly;
		}
		ResultData->SetBoolField(TEXT("blueprint_read_only"), bReadOnly);
	}

	// Expose on Spawn
	if (Params->HasField(TEXT("expose_on_spawn")))
	{
		bool bExposeOnSpawn = Params->GetBoolField(TEXT("expose_on_spawn"));
		if (bExposeOnSpawn)
		{
			TargetVar->PropertyFlags |= CPF_ExposeOnSpawn;
		}
		else
		{
			TargetVar->PropertyFlags &= ~CPF_ExposeOnSpawn;
		}
		ResultData->SetBoolField(TEXT("expose_on_spawn"), bExposeOnSpawn);
	}

	// Replicated
	if (Params->HasField(TEXT("replicated")))
	{
		bool bReplicated = Params->GetBoolField(TEXT("replicated"));
		if (bReplicated)
		{
			TargetVar->PropertyFlags |= CPF_Net;
			TargetVar->RepNotifyFunc = NAME_None;
		}
		else
		{
			TargetVar->PropertyFlags &= ~CPF_Net;
		}
		ResultData->SetBoolField(TEXT("replicated"), bReplicated);
	}

	// Private (visible only within this Blueprint)
	if (Params->HasField(TEXT("private")))
	{
		bool bPrivate = Params->GetBoolField(TEXT("private"));
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, FName(*VariableName), nullptr,
			FBlueprintMetadata::MD_Private, bPrivate ? TEXT("true") : TEXT("false"));
		ResultData->SetBoolField(TEXT("private"), bPrivate);
	}

	// If instance_editable was changed, SetBlueprintOnlyEditableFlag already
	// called MarkBlueprintAsStructurallyModified (skeleton rebuild + UI refresh).
	// For other flags, use the standard MarkBlueprintModified path.
	if (!bDidChangeInstanceEditable)
	{
		MarkBlueprintModified(Blueprint, Context);
	}
	else
	{
		// Still need to mark the package dirty for auto-save
		Context.MarkPackageDirty(Blueprint->GetOutermost());
	}

	// Force a full recompile so that flag changes
	// (CPF_Edit, CPF_ExposeOnSpawn, etc.) are reflected in the Generated Class.
	FString CompileError;
	bool bCompileOk = CompileBlueprint(Blueprint, CompileError);
	ResultData->SetBoolField(TEXT("compiled"), bCompileOk);
	if (!bCompileOk)
	{
		ResultData->SetStringField(TEXT("compile_error"), CompileError);
	}

	return CreateSuccessResponse(ResultData);
}


bool FDeleteBlueprintFunctionAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString FunctionName;
	if (!GetRequiredString(Params, TEXT("function_name"), FunctionName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FDeleteBlueprintFunctionAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString FunctionName = Params->GetStringField(TEXT("function_name"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Find the function graph
	UEdGraph* FunctionGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FName(*FunctionName))
		{
			FunctionGraph = Graph;
			break;
		}
	}

	if (!FunctionGraph)
	{
		// List available functions for helpful error
		TArray<FString> AvailableFunctions;
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph)
			{
				AvailableFunctions.Add(Graph->GetName());
			}
		}
		FString AvailableStr = FString::Join(AvailableFunctions, TEXT(", "));
		return CreateErrorResponse(FString::Printf(TEXT("Function '%s' not found. Available: %s"), *FunctionName, *AvailableStr));
	}

	FBlueprintEditorUtils::RemoveGraph(Blueprint, FunctionGraph, EGraphRemoveFlags::Default);
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("deleted_function"), FunctionName);
	return CreateSuccessResponse(ResultData);
}

bool FRenameBlueprintFunctionAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString FunctionName;
	FString NewName;
	if (!GetRequiredString(Params, TEXT("function_name"), FunctionName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("new_name"), NewName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FRenameBlueprintFunctionAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	const FString FunctionName = Params->GetStringField(TEXT("function_name"));
	const FString NewName = Params->GetStringField(TEXT("new_name"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	UEdGraph* FunctionGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FName(*FunctionName))
		{
			FunctionGraph = Graph;
			break;
		}
	}

	if (!FunctionGraph)
	{
		TArray<FString> AvailableFunctions;
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph)
			{
				AvailableFunctions.Add(Graph->GetName());
			}
		}
		const FString AvailableStr = FString::Join(AvailableFunctions, TEXT(", "));
		return CreateErrorResponse(FString::Printf(TEXT("Function '%s' not found. Available: %s"), *FunctionName, *AvailableStr));
	}

	if (FunctionName.Equals(NewName, ESearchCase::CaseSensitive))
	{
		TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
		ResultData->SetBoolField(TEXT("already_named"), true);
		ResultData->SetStringField(TEXT("function_name"), FunctionName);
		ResultData->SetStringField(TEXT("new_name"), NewName);
		return CreateSuccessResponse(ResultData);
	}

	const FName OldFName(*FunctionName);

	// Pre-validate: ensure new name won't collide with an existing graph
	UEdGraph* ExistingGraph = FindObject<UEdGraph>(FunctionGraph->GetOuter(), *NewName);
	if (ExistingGraph && ExistingGraph != FunctionGraph)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("A graph named '%s' already exists in this Blueprint."), *NewName),
			TEXT("name_collision"));
	}

	// Use the engine's own RenameGraph — it handles:
	// 1. UObject::Rename on the graph
	// 2. FunctionEntry/FunctionResult FunctionReference updates
	// 3. All K2Node_CallFunction call site updates (via TObjectIterator)
	// 4. Local variable scope updates
	// 5. ReplaceFunctionReferences for cross-BP refs
	// 6. NotifyGraphRenamed + MarkBlueprintAsStructurallyModified
	FBlueprintEditorUtils::RenameGraph(FunctionGraph, NewName);

	const FString RenamedTo = FunctionGraph->GetName();

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("old_name"), FunctionName);
	ResultData->SetStringField(TEXT("new_name"), RenamedTo);
	ResultData->SetBoolField(TEXT("exact_match"), RenamedTo.Equals(NewName, ESearchCase::CaseSensitive));
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// FRenameBlueprintMacroAction — macro.rename
// ============================================================================

bool FRenameBlueprintMacroAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MacroName;
	FString NewName;
	if (!GetRequiredString(Params, TEXT("macro_name"), MacroName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("new_name"), NewName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FRenameBlueprintMacroAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	const FString MacroName = Params->GetStringField(TEXT("macro_name"));
	const FString NewName = Params->GetStringField(TEXT("new_name"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	UEdGraph* MacroGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (Graph && Graph->GetFName() == FName(*MacroName))
		{
			MacroGraph = Graph;
			break;
		}
	}

	if (!MacroGraph)
	{
		TArray<FString> AvailableMacros;
		for (UEdGraph* Graph : Blueprint->MacroGraphs)
		{
			if (Graph)
			{
				AvailableMacros.Add(Graph->GetName());
			}
		}
		const FString AvailableStr = FString::Join(AvailableMacros, TEXT(", "));
		return CreateErrorResponse(FString::Printf(TEXT("Macro '%s' not found. Available: %s"), *MacroName, *AvailableStr));
	}

	if (MacroName.Equals(NewName, ESearchCase::CaseSensitive))
	{
		TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
		ResultData->SetBoolField(TEXT("already_named"), true);
		ResultData->SetStringField(TEXT("macro_name"), MacroName);
		ResultData->SetStringField(TEXT("new_name"), NewName);
		return CreateSuccessResponse(ResultData);
	}

	// Pre-validate: ensure new name won't collide with an existing graph
	UEdGraph* ExistingGraph = FindObject<UEdGraph>(MacroGraph->GetOuter(), *NewName);
	if (ExistingGraph && ExistingGraph != MacroGraph)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("A graph named '%s' already exists in this Blueprint."), *NewName),
			TEXT("name_collision"));
	}

	// Use the engine's own RenameGraph — it handles:
	// 1. UObject::Rename on the graph
	// 2. Macro instance node reference updates
	// 3. NotifyGraphRenamed + MarkBlueprintAsStructurallyModified
	FBlueprintEditorUtils::RenameGraph(MacroGraph, NewName);

	const FString RenamedTo = MacroGraph->GetName();

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("old_name"), MacroName);
	ResultData->SetStringField(TEXT("new_name"), RenamedTo);
	ResultData->SetBoolField(TEXT("exact_match"), RenamedTo.Equals(NewName, ESearchCase::CaseSensitive));
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// P2 — Graph Operation Enhancements
// ============================================================================

bool FDisconnectBlueprintPinAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString NodeId, PinName;
	if (!GetRequiredString(Params, TEXT("node_id"), NodeId, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("pin_name"), PinName, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FDisconnectBlueprintPinAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString NodeId = Params->GetStringField(TEXT("node_id"));
	FString PinName = Params->GetStringField(TEXT("pin_name"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the node
	UEdGraphNode* FoundNode = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node->NodeGuid.ToString() == NodeId)
		{
			FoundNode = Node;
			break;
		}
	}

	if (!FoundNode)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Node not found with ID: %s"), *NodeId));
	}

	// Find the pin (try both directions)
	UEdGraphPin* TargetPin = FMCPCommonUtils::FindPin(FoundNode, PinName, EGPD_Output);
	if (!TargetPin)
	{
		TargetPin = FMCPCommonUtils::FindPin(FoundNode, PinName, EGPD_Input);
	}

	if (!TargetPin)
	{
		// List available pins for helpful error
		TArray<FString> PinNames;
		for (UEdGraphPin* Pin : FoundNode->Pins)
		{
			if (!Pin->bHidden)
			{
				PinNames.Add(FString::Printf(TEXT("'%s' (%s)"), *Pin->PinName.ToString(),
					Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output")));
			}
		}
		FString AvailableStr = FString::Join(PinNames, TEXT(", "));
		return CreateErrorResponse(FString::Printf(TEXT("Pin '%s' not found. Available: [%s]"), *PinName, *AvailableStr));
	}

	int32 DisconnectedCount = TargetPin->LinkedTo.Num();
	TargetPin->BreakAllPinLinks();
	FoundNode->PinConnectionListChanged(TargetPin);
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), NodeId);
	ResultData->SetStringField(TEXT("pin_name"), PinName);
	ResultData->SetNumberField(TEXT("disconnected_count"), DisconnectedCount);
	return CreateSuccessResponse(ResultData);
}


bool FMoveNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString NodeId;
	if (!GetRequiredString(Params, TEXT("node_id"), NodeId, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FMoveNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString NodeId = Params->GetStringField(TEXT("node_id"));
	FVector2D NewPosition = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Find the node
	UEdGraphNode* FoundNode = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node->NodeGuid.ToString() == NodeId)
		{
			FoundNode = Node;
			break;
		}
	}

	if (!FoundNode)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Node not found with ID: %s"), *NodeId));
	}

	int32 OldX = FoundNode->NodePosX;
	int32 OldY = FoundNode->NodePosY;
	FoundNode->NodePosX = FMath::RoundToInt(NewPosition.X);
	FoundNode->NodePosY = FMath::RoundToInt(NewPosition.Y);

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), NodeId);
	TArray<TSharedPtr<FJsonValue>> OldPosArr;
	OldPosArr.Add(MakeShared<FJsonValueNumber>(OldX));
	OldPosArr.Add(MakeShared<FJsonValueNumber>(OldY));
	ResultData->SetArrayField(TEXT("old_position"), OldPosArr);
	TArray<TSharedPtr<FJsonValue>> NewPosArr;
	NewPosArr.Add(MakeShared<FJsonValueNumber>(FoundNode->NodePosX));
	NewPosArr.Add(MakeShared<FJsonValueNumber>(FoundNode->NodePosY));
	ResultData->SetArrayField(TEXT("new_position"), NewPosArr);
	return CreateSuccessResponse(ResultData);
}


bool FAddRerouteNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddRerouteNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FVector2D Position = GetNodePosition(Params);

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);

	// Create a Knot (reroute) node
	UK2Node_Knot* RerouteNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_Knot>(
		TargetGraph, Position, EK2NewNodeFlags::None
	);

	if (!RerouteNode)
	{
		return CreateErrorResponse(TEXT("Failed to create reroute node"));
	}

	MarkBlueprintModified(Blueprint, Context);
	RegisterCreatedNode(RerouteNode, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("node_id"), RerouteNode->NodeGuid.ToString());
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Graph Topology — FDescribeGraphAction (P2.1)
// ============================================================================

bool FDescribeGraphAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FDescribeGraphAction::SerializePinToJson(const UEdGraphPin* Pin, bool bIncludeHidden)
{
	if (!Pin)
	{
		return nullptr;
	}
	if (!bIncludeHidden && Pin->bHidden)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
	PinObj->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
	PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
	PinObj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());

	if (Pin->PinType.PinSubCategory != NAME_None)
	{
		PinObj->SetStringField(TEXT("sub_category"), Pin->PinType.PinSubCategory.ToString());
	}
	if (Pin->PinType.PinSubCategoryObject.Get())
	{
		PinObj->SetStringField(TEXT("sub_category_object"), Pin->PinType.PinSubCategoryObject->GetPathName());
	}

	PinObj->SetBoolField(TEXT("is_connected"), Pin->LinkedTo.Num() > 0);

	// Linked pins
	TArray<TSharedPtr<FJsonValue>> LinkedArray;
	for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
	{
		if (!LinkedPin) continue;
		const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
		if (!LinkedNode) continue;

		TSharedPtr<FJsonObject> LinkedObj = MakeShared<FJsonObject>();
		LinkedObj->SetStringField(TEXT("node_id"), LinkedNode->NodeGuid.ToString());
		LinkedObj->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());
		LinkedArray.Add(MakeShared<FJsonValueObject>(LinkedObj));
	}
	PinObj->SetArrayField(TEXT("linked_to"), LinkedArray);

	// Default value
	if (!Pin->DefaultValue.IsEmpty())
	{
		PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
	}
	else if (!Pin->AutogeneratedDefaultValue.IsEmpty())
	{
		PinObj->SetStringField(TEXT("default_value"), Pin->AutogeneratedDefaultValue);
	}

	return PinObj;
}

TSharedPtr<FJsonObject> FDescribeGraphAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);
	if (!TargetGraph)
	{
		return CreateErrorResponse(TEXT("Target graph not found"));
	}

	const bool bIncludeHidden = GetOptionalBool(Params, TEXT("include_hidden_pins"), false);

	// Build node array and collect edges (de-duplicated)
	TArray<TSharedPtr<FJsonValue>> NodesArray;
	TArray<TSharedPtr<FJsonValue>> EdgesArray;
	TSet<FString> SeenEdges; // "FromGuid:FromPin->ToGuid:ToPin"

	for (const UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (!Node) continue;

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
		NodeObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
		NodeObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
		NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);

		if (!Node->NodeComment.IsEmpty())
		{
			NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);
		}

		// Serialize pins
		TArray<TSharedPtr<FJsonValue>> PinsArray;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			TSharedPtr<FJsonObject> PinObj = SerializePinToJson(Pin, bIncludeHidden);
			if (PinObj.IsValid())
			{
				PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
			}

			// Collect edges (output pins only to avoid duplicates)
			if (Pin && Pin->Direction == EGPD_Output)
			{
				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin) continue;
					const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
					if (!LinkedNode) continue;

					// De-duplicate: create canonical edge key
					FString EdgeKey = FString::Printf(TEXT("%s:%s->%s:%s"),
						*Node->NodeGuid.ToString(), *Pin->PinName.ToString(),
						*LinkedNode->NodeGuid.ToString(), *LinkedPin->PinName.ToString());

					if (!SeenEdges.Contains(EdgeKey))
					{
						SeenEdges.Add(EdgeKey);

						TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
						EdgeObj->SetStringField(TEXT("from_node"), Node->NodeGuid.ToString());
						EdgeObj->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
						EdgeObj->SetStringField(TEXT("to_node"), LinkedNode->NodeGuid.ToString());
						EdgeObj->SetStringField(TEXT("to_pin"), LinkedPin->PinName.ToString());
						EdgesArray.Add(MakeShared<FJsonValueObject>(EdgeObj));
					}
				}
			}
		}
		NodeObj->SetArrayField(TEXT("pins"), PinsArray);

		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("graph_name"), TargetGraph->GetName());
	ResultData->SetNumberField(TEXT("node_count"), NodesArray.Num());
	ResultData->SetNumberField(TEXT("edge_count"), EdgesArray.Num());
	ResultData->SetArrayField(TEXT("nodes"), NodesArray);
	ResultData->SetArrayField(TEXT("edges"), EdgesArray);
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Graph Selection Read — FGetSelectedNodesAction
// ============================================================================

#define private public
#define protected public
#include "BlueprintEditor.h"
#undef protected
#undef private
#include "Subsystems/AssetEditorSubsystem.h"

// GetActiveBlueprintEditorForAction removed — use FMCPCommonUtils::GetActiveBlueprintEditor() instead.

bool FGetSelectedNodesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// No required params — works with editor selection or optional blueprint_name
	return true;
}

bool FCollapseSelectionToFunctionAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// No required params — action works on current selection in focused Blueprint editor.
	// Optional: blueprint_name to target a specific open Blueprint editor.
	return true;
}

TSharedPtr<FJsonObject> FCollapseSelectionToFunctionAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = GetOptionalString(Params, TEXT("blueprint_name"));

	FBlueprintEditor* BPEditor = FMCPCommonUtils::GetActiveBlueprintEditor(BlueprintName);
	if (!BPEditor)
	{
		return CreateErrorResponse(
			TEXT("No Blueprint editor is currently open. Open a Blueprint and select nodes first."),
			TEXT("no_editor"));
	}

	if (!BPEditor->GraphEditorCommands.IsValid())
	{
		return CreateErrorResponse(TEXT("Blueprint editor graph command list is not available."), TEXT("no_graph_commands"));
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(BPEditor->GetBlueprintObj());
	if (!Blueprint)
	{
		return CreateErrorResponse(TEXT("Failed to resolve Blueprint from active editor."), TEXT("blueprint_not_found"));
	}

	UEdGraph* FocusedGraph = BPEditor->GetFocusedGraph();
	if (!FocusedGraph)
	{
		return CreateErrorResponse(TEXT("Blueprint editor has no focused graph."), TEXT("no_graph"));
	}

	// ── Pre-flight: can the engine collapse the current selection? ──
	// NOTE: Delegate-binding nodes (AddDelegate, CreateDelegate, CallDelegate)
	// are fully supported by the engine's CollapseSelectionToFunction flow.
	// Only UK2Node_ComponentBoundEvent (an Event subclass) cannot be placed
	// in function graphs, but the engine's own CanPasteHere validation handles
	// that correctly — no need for MCP-side preemptive exclusion.
	const TSharedRef<const FUICommandInfo> CollapseToFunctionCommand = FGraphEditorCommands::Get().CollapseSelectionToFunction.ToSharedRef();
	if (!BPEditor->GraphEditorCommands->CanExecuteAction(CollapseToFunctionCommand))
	{
		return CreateErrorResponse(
			TEXT("Current selection cannot be collapsed to function. Ensure a valid node selection in a non-AnimGraph Blueprint graph."),
			TEXT("cannot_collapse_selection"));
	}

	// ── Snapshot existing function graphs ───────────────────────────
	TSet<FName> ExistingFunctionNames;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph)
		{
			ExistingFunctionNames.Add(Graph->GetFName());
		}
	}

	const int32 BeforeCount = ExistingFunctionNames.Num();
	if (!BPEditor->GraphEditorCommands->TryExecuteAction(CollapseToFunctionCommand))
	{
		return CreateErrorResponse(
			TEXT("Failed to execute Collapse Selection To Function command."),
			TEXT("collapse_command_failed"));
	}

	// ── Detect newly-created function graph(s) ──────────────────────
	TArray<TSharedPtr<FJsonValue>> NewFunctionNamesJson;
	FString NewestFunctionName;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		if (!ExistingFunctionNames.Contains(Graph->GetFName()))
		{
			const FString FunctionName = Graph->GetName();
			NewestFunctionName = FunctionName;
			NewFunctionNamesJson.Add(MakeShared<FJsonValueString>(FunctionName));
		}
	}

	if (NewFunctionNamesJson.Num() == 0)
	{
		return CreateErrorResponse(
			TEXT("Collapse to function did not create a new function graph. Check Blueprint Message Log for detailed validation errors."),
			TEXT("collapse_failed"));
	}

	Context.MarkPackageDirty(Blueprint->GetOutermost());

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
	ResultData->SetStringField(TEXT("source_graph"), FocusedGraph->GetName());
	ResultData->SetNumberField(TEXT("function_count_before"), BeforeCount);
	ResultData->SetNumberField(TEXT("function_count_after"), Blueprint->FunctionGraphs.Num());
	ResultData->SetArrayField(TEXT("created_functions"), NewFunctionNamesJson);
	ResultData->SetStringField(TEXT("created_function"), NewestFunctionName);

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// FCollapseSelectionToMacroAction
// ============================================================================

bool FCollapseSelectionToMacroAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// No required params — action works on current selection in focused Blueprint editor.
	// Optional: blueprint_name to target a specific open Blueprint editor.
	return true;
}

TSharedPtr<FJsonObject> FCollapseSelectionToMacroAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = GetOptionalString(Params, TEXT("blueprint_name"));

	FBlueprintEditor* BPEditor = FMCPCommonUtils::GetActiveBlueprintEditor(BlueprintName);
	if (!BPEditor)
	{
		return CreateErrorResponse(
			TEXT("No Blueprint editor is currently open. Open a Blueprint and select nodes first."),
			TEXT("no_editor"));
	}

	if (!BPEditor->GraphEditorCommands.IsValid())
	{
		return CreateErrorResponse(TEXT("Blueprint editor graph command list is not available."), TEXT("no_graph_commands"));
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(BPEditor->GetBlueprintObj());
	if (!Blueprint)
	{
		return CreateErrorResponse(TEXT("Failed to resolve Blueprint from active editor."), TEXT("blueprint_not_found"));
	}

	UEdGraph* FocusedGraph = BPEditor->GetFocusedGraph();
	if (!FocusedGraph)
	{
		return CreateErrorResponse(TEXT("Blueprint editor has no focused graph."), TEXT("no_graph"));
	}

	// ── Pre-flight: can the engine collapse the current selection to a macro? ──
	const TSharedRef<const FUICommandInfo> CollapseToMacroCommand = FGraphEditorCommands::Get().CollapseSelectionToMacro.ToSharedRef();
	if (!BPEditor->GraphEditorCommands->CanExecuteAction(CollapseToMacroCommand))
	{
		return CreateErrorResponse(
			TEXT("Current selection cannot be collapsed to macro. Ensure a valid node selection in a Blueprint graph (not AnimGraph)."),
			TEXT("cannot_collapse_selection"));
	}

	// ── Snapshot existing macro graphs ──────────────────────────────
	TSet<FName> ExistingMacroNames;
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (Graph)
		{
			ExistingMacroNames.Add(Graph->GetFName());
		}
	}

	const int32 BeforeCount = ExistingMacroNames.Num();
	if (!BPEditor->GraphEditorCommands->TryExecuteAction(CollapseToMacroCommand))
	{
		return CreateErrorResponse(
			TEXT("Failed to execute Collapse Selection To Macro command."),
			TEXT("collapse_command_failed"));
	}

	// ── Detect newly-created macro graph(s) ─────────────────────────
	TArray<TSharedPtr<FJsonValue>> NewMacroNamesJson;
	FString NewestMacroName;
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		if (!ExistingMacroNames.Contains(Graph->GetFName()))
		{
			const FString MacroName = Graph->GetName();
			NewestMacroName = MacroName;
			NewMacroNamesJson.Add(MakeShared<FJsonValueString>(MacroName));
		}
	}

	if (NewMacroNamesJson.Num() == 0)
	{
		return CreateErrorResponse(
			TEXT("Collapse to macro did not create a new macro graph. Check Blueprint Message Log for detailed validation errors."),
			TEXT("collapse_failed"));
	}

	Context.MarkPackageDirty(Blueprint->GetOutermost());

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
	ResultData->SetStringField(TEXT("source_graph"), FocusedGraph->GetName());
	ResultData->SetNumberField(TEXT("macro_count_before"), BeforeCount);
	ResultData->SetNumberField(TEXT("macro_count_after"), Blueprint->MacroGraphs.Num());
	ResultData->SetArrayField(TEXT("created_macros"), NewMacroNamesJson);
	ResultData->SetStringField(TEXT("created_macro"), NewestMacroName);

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Graph Selection Write — FSetSelectedNodesAction / FBatchSelectAndActAction
// ============================================================================

bool FSetSelectedNodesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* NodeIdsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("node_ids"), NodeIdsArray) || !NodeIdsArray || NodeIdsArray->Num() == 0)
	{
		OutError = TEXT("'node_ids' is required and must be a non-empty array of GUID strings.");
		return false;
	}
	return true;
}

/**
 * Helper: resolve nodes by GUID in a graph and set selection on the SGraphEditor.
 * Returns false + OutError on failure. On success, populates OutSelectedCount/OutMissingIds.
 */
static bool SetSelectionByNodeIds(
	FBlueprintEditor* BPEditor,
	UEdGraph* Graph,
	const TArray<TSharedPtr<FJsonValue>>& NodeIdsArray,
	bool bAppend,
	int32& OutSelectedCount,
	TArray<TSharedPtr<FJsonValue>>& OutSelectedIds,
	TArray<TSharedPtr<FJsonValue>>& OutMissingIds,
	FString& OutError)
{
	// Access the focused SGraphEditor via the private member (already exposed by the #define hack above)
	TSharedPtr<SGraphEditor> GraphEditor = BPEditor->FocusedGraphEdPtr.Pin();
	if (!GraphEditor.IsValid())
	{
		OutError = TEXT("No focused graph editor widget available. Ensure a graph tab is focused.");
		return false;
	}

	// Resolve node GUIDs
	TArray<UEdGraphNode*> NodesToSelect;

	for (const TSharedPtr<FJsonValue>& IdValue : NodeIdsArray)
	{
		FString NodeIdStr = IdValue->AsString();
		FGuid NodeGuid;
		if (!FGuid::Parse(NodeIdStr, NodeGuid))
		{
			OutMissingIds.Add(MakeShared<FJsonValueString>(NodeIdStr + TEXT(" (invalid GUID)")));
			continue;
		}

		UEdGraphNode* FoundNode = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == NodeGuid)
			{
				FoundNode = Node;
				break;
			}
		}

		if (FoundNode)
		{
			NodesToSelect.Add(FoundNode);
			OutSelectedIds.Add(MakeShared<FJsonValueString>(NodeIdStr));
		}
		else
		{
			OutMissingIds.Add(MakeShared<FJsonValueString>(NodeIdStr));
		}
	}

	// Set the selection
	if (!bAppend)
	{
		GraphEditor->ClearSelectionSet();
	}

	for (UEdGraphNode* Node : NodesToSelect)
	{
		GraphEditor->SetNodeSelection(Node, true);
	}

	OutSelectedCount = NodesToSelect.Num();
	return true;
}

TSharedPtr<FJsonObject> FSetSelectedNodesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = GetOptionalString(Params, TEXT("blueprint_name"));
	FBlueprintEditor* BPEditor = FMCPCommonUtils::GetActiveBlueprintEditor(BlueprintName);
	if (!BPEditor)
	{
		return CreateErrorResponse(
			TEXT("No Blueprint editor is currently open. Open a Blueprint and focus a graph first."),
			TEXT("no_editor"));
	}

	UEdGraph* FocusedGraph = BPEditor->GetFocusedGraph();
	if (!FocusedGraph)
	{
		return CreateErrorResponse(TEXT("Blueprint editor has no focused graph."), TEXT("no_graph"));
	}

	// Optional: find specific graph
	FString RequestedGraph = GetOptionalString(Params, TEXT("graph_name"));
	if (!RequestedGraph.IsEmpty() && FocusedGraph->GetName() != RequestedGraph)
	{
		UBlueprint* BP = Cast<UBlueprint>(BPEditor->GetBlueprintObj());
		if (BP)
		{
			FString FindError;
			UEdGraph* FoundGraph = FindGraph(BP, RequestedGraph, FindError);
			if (FoundGraph)
			{
				// Bring this graph to front so the SGraphEditor is valid for it
				BPEditor->OpenGraphAndBringToFront(FoundGraph);
				FocusedGraph = FoundGraph;
			}
			else
			{
				return CreateErrorResponse(
					FString::Printf(TEXT("Graph '%s' not found: %s"), *RequestedGraph, *FindError),
					TEXT("graph_not_found"));
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* NodeIdsArray = nullptr;
	Params->TryGetArrayField(TEXT("node_ids"), NodeIdsArray);
	bool bAppend = GetOptionalBool(Params, TEXT("append"), false);

	int32 SelectedCount = 0;
	TArray<TSharedPtr<FJsonValue>> SelectedIds;
	TArray<TSharedPtr<FJsonValue>> MissingIds;
	FString SelectError;

	if (!SetSelectionByNodeIds(BPEditor, FocusedGraph, *NodeIdsArray, bAppend, SelectedCount, SelectedIds, MissingIds, SelectError))
	{
		return CreateErrorResponse(SelectError, TEXT("selection_failed"));
	}

	UBlueprint* BP = Cast<UBlueprint>(BPEditor->GetBlueprintObj());
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("blueprint_name"), BP ? BP->GetName() : TEXT("Unknown"));
	ResultData->SetStringField(TEXT("graph_name"), FocusedGraph->GetName());
	ResultData->SetNumberField(TEXT("selected_count"), SelectedCount);
	ResultData->SetArrayField(TEXT("selected_ids"), SelectedIds);
	if (MissingIds.Num() > 0)
	{
		ResultData->SetArrayField(TEXT("missing_ids"), MissingIds);
	}
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// FBatchSelectAndActAction
// ============================================================================

bool FBatchSelectAndActAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* GroupsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("groups"), GroupsArray) || !GroupsArray || GroupsArray->Num() == 0)
	{
		OutError = TEXT("'groups' is required and must be a non-empty array of group objects.");
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

		const TArray<TSharedPtr<FJsonValue>>* InnerNodeIds = nullptr;
		if (!(*GroupObj)->TryGetArrayField(TEXT("node_ids"), InnerNodeIds) || !InnerNodeIds || InnerNodeIds->Num() == 0)
		{
			OutError = FString::Printf(TEXT("groups[%d].node_ids is required and must be a non-empty array."), i);
			return false;
		}

		FString ActionCmd;
		if (!(*GroupObj)->TryGetStringField(TEXT("action"), ActionCmd) || ActionCmd.IsEmpty())
		{
			OutError = FString::Printf(TEXT("groups[%d].action is required (e.g. 'collapse_selection_to_function', 'auto_comment')."), i);
			return false;
		}
	}

	return true;
}

TSharedPtr<FJsonObject> FBatchSelectAndActAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName = GetOptionalString(Params, TEXT("blueprint_name"));
	FBlueprintEditor* BPEditor = FMCPCommonUtils::GetActiveBlueprintEditor(BlueprintName);
	if (!BPEditor)
	{
		return CreateErrorResponse(
			TEXT("No Blueprint editor is currently open."),
			TEXT("no_editor"));
	}

	UEdGraph* FocusedGraph = BPEditor->GetFocusedGraph();
	if (!FocusedGraph)
	{
		return CreateErrorResponse(TEXT("Blueprint editor has no focused graph."), TEXT("no_graph"));
	}

	// Optional: find specific graph
	FString RequestedGraph = GetOptionalString(Params, TEXT("graph_name"));
	if (!RequestedGraph.IsEmpty() && FocusedGraph->GetName() != RequestedGraph)
	{
		UBlueprint* BP = Cast<UBlueprint>(BPEditor->GetBlueprintObj());
		if (BP)
		{
			FString FindError;
			UEdGraph* FoundGraph = FindGraph(BP, RequestedGraph, FindError);
			if (FoundGraph)
			{
				BPEditor->OpenGraphAndBringToFront(FoundGraph);
				FocusedGraph = FoundGraph;
			}
			else
			{
				return CreateErrorResponse(
					FString::Printf(TEXT("Graph '%s' not found: %s"), *RequestedGraph, *FindError),
					TEXT("graph_not_found"));
			}
		}
	}

	// Access SGraphEditor
	TSharedPtr<SGraphEditor> GraphEditor = BPEditor->FocusedGraphEdPtr.Pin();
	if (!GraphEditor.IsValid())
	{
		return CreateErrorResponse(
			TEXT("No focused graph editor widget available."),
			TEXT("no_graph_editor_widget"));
	}

	// Get the MCPBridge to dispatch sub-actions
	UMCPBridge* Bridge = nullptr;
	for (TObjectIterator<UMCPBridge> It; It; ++It)
	{
		Bridge = *It;
		break;
	}
	if (!Bridge)
	{
		return CreateErrorResponse(TEXT("MCPBridge not found — cannot dispatch sub-actions."), TEXT("no_bridge"));
	}

	const TArray<TSharedPtr<FJsonValue>>* GroupsArray = nullptr;
	Params->TryGetArrayField(TEXT("groups"), GroupsArray);

	TArray<TSharedPtr<FJsonValue>> ResultsArray;

	for (int32 GroupIdx = 0; GroupIdx < GroupsArray->Num(); ++GroupIdx)
	{
		TSharedPtr<FJsonObject> GroupResult = MakeShared<FJsonObject>();
		GroupResult->SetNumberField(TEXT("group_index"), GroupIdx);

		const TSharedPtr<FJsonObject>& GroupObj = (*GroupsArray)[GroupIdx]->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* NodeIds = nullptr;
		GroupObj->TryGetArrayField(TEXT("node_ids"), NodeIds);
		FString ActionCmd = GroupObj->GetStringField(TEXT("action"));

		// 1. Clear selection and select the group's nodes
		GraphEditor->ClearSelectionSet();

		TArray<FString> SelectedIds;
		TArray<FString> MissingIds;

		for (const TSharedPtr<FJsonValue>& IdValue : *NodeIds)
		{
			FString NodeIdStr = IdValue->AsString();
			FGuid NodeGuid;
			if (!FGuid::Parse(NodeIdStr, NodeGuid))
			{
				MissingIds.Add(NodeIdStr + TEXT(" (invalid GUID)"));
				continue;
			}

			UEdGraphNode* FoundNode = nullptr;
			for (UEdGraphNode* Node : FocusedGraph->Nodes)
			{
				if (Node && Node->NodeGuid == NodeGuid)
				{
					FoundNode = Node;
					break;
				}
			}

			if (FoundNode)
			{
				GraphEditor->SetNodeSelection(FoundNode, true);
				SelectedIds.Add(NodeIdStr);
			}
			else
			{
				MissingIds.Add(NodeIdStr);
			}
		}

		GroupResult->SetNumberField(TEXT("selected_count"), SelectedIds.Num());
		if (MissingIds.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> MissingJson;
			for (const FString& Id : MissingIds)
			{
				MissingJson.Add(MakeShared<FJsonValueString>(Id));
			}
			GroupResult->SetArrayField(TEXT("missing_ids"), MissingJson);
		}

		// 2. Build params for the sub-action (merge blueprint_name, graph_name, and action_params)
		TSharedPtr<FJsonObject> ActionParams = MakeShared<FJsonObject>();
		if (!BlueprintName.IsEmpty())
		{
			ActionParams->SetStringField(TEXT("blueprint_name"), BlueprintName);
		}
		if (!RequestedGraph.IsEmpty())
		{
			ActionParams->SetStringField(TEXT("graph_name"), RequestedGraph);
		}

		// Merge user-provided action_params
		const TSharedPtr<FJsonObject>* ExtraParams = nullptr;
		if (GroupObj->TryGetObjectField(TEXT("action_params"), ExtraParams) && ExtraParams && (*ExtraParams).IsValid())
		{
			for (const auto& Pair : (*ExtraParams)->Values)
			{
				ActionParams->SetField(Pair.Key, Pair.Value);
			}
		}

		// For auto_comment: inject node_ids so it wraps the correct nodes
		if (ActionCmd == TEXT("auto_comment"))
		{
			ActionParams->SetArrayField(TEXT("node_ids"), *const_cast<TArray<TSharedPtr<FJsonValue>>*>(NodeIds));
		}

		// 3. Execute the sub-action via MCPBridge::ExecuteCommand
		TSharedPtr<FJsonObject> ActionResult = Bridge->ExecuteCommand(ActionCmd, ActionParams);
		GroupResult->SetStringField(TEXT("action"), ActionCmd);
		GroupResult->SetObjectField(TEXT("action_result"), ActionResult);
		
		// Check if the sub-action succeeded
		bool bSubSuccess = false;
		if (ActionResult.IsValid())
		{
			ActionResult->TryGetBoolField(TEXT("success"), bSubSuccess);
		}
		GroupResult->SetBoolField(TEXT("success"), bSubSuccess);

		ResultsArray.Add(MakeShared<FJsonValueObject>(GroupResult));
	}

	// Restore clean selection state
	GraphEditor->ClearSelectionSet();

	UBlueprint* BP = Cast<UBlueprint>(BPEditor->GetBlueprintObj());
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("blueprint_name"), BP ? BP->GetName() : TEXT("Unknown"));
	ResultData->SetStringField(TEXT("graph_name"), FocusedGraph->GetName());
	ResultData->SetNumberField(TEXT("group_count"), GroupsArray->Num());
	ResultData->SetArrayField(TEXT("results"), ResultsArray);
	return CreateSuccessResponse(ResultData);
}


TSharedPtr<FJsonObject> FGetSelectedNodesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	// Resolve which editor to inspect
	FString BlueprintName = GetOptionalString(Params, TEXT("blueprint_name"));
	FBlueprintEditor* BPEditor = FMCPCommonUtils::GetActiveBlueprintEditor(BlueprintName);

	if (!BPEditor)
	{
		return CreateErrorResponse(
			TEXT("No Blueprint editor is currently open. Open a Blueprint in the editor first."),
			TEXT("no_editor"));
	}

	UEdGraph* FocusedGraph = BPEditor->GetFocusedGraph();
	if (!FocusedGraph)
	{
		return CreateErrorResponse(
			TEXT("Blueprint editor has no focused graph."),
			TEXT("no_graph"));
	}

	// Optional: filter by graph_name
	FString RequestedGraph = GetOptionalString(Params, TEXT("graph_name"));
	if (!RequestedGraph.IsEmpty() && FocusedGraph->GetName() != RequestedGraph)
	{
		// Try to find the requested graph in the same Blueprint
		UBlueprint* BP = Cast<UBlueprint>(BPEditor->GetBlueprintObj());
		if (BP)
		{
			FString FindError;
			UEdGraph* FoundGraph = FindGraph(BP, RequestedGraph, FindError);
			if (FoundGraph)
			{
				FocusedGraph = FoundGraph;
			}
			else
			{
				return CreateErrorResponse(
					FString::Printf(TEXT("Graph '%s' not found: %s"), *RequestedGraph, *FindError),
					TEXT("graph_not_found"));
			}
		}
	}

	// Get selected nodes
	FGraphPanelSelectionSet SelectedNodes = BPEditor->GetSelectedNodes();

	const bool bIncludeHidden = GetOptionalBool(Params, TEXT("include_hidden_pins"), false);

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	TArray<TSharedPtr<FJsonValue>> EdgesArray;
	TSet<FString> SeenEdges;

	for (UObject* Obj : SelectedNodes)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(Obj);
		if (!Node) continue;

		// Only include nodes that belong to the focused graph
		if (!FocusedGraph->Nodes.Contains(Node)) continue;

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
		NodeObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
		NodeObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
		NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);

		if (!Node->NodeComment.IsEmpty())
		{
			NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);
		}

		// Serialize pins (reuse FDescribeGraphAction pattern)
		TArray<TSharedPtr<FJsonValue>> PinsArray;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;
			if (!bIncludeHidden && Pin->bHidden) continue;

			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
			PinObj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());

			if (Pin->PinType.PinSubCategory != NAME_None)
			{
				PinObj->SetStringField(TEXT("sub_category"), Pin->PinType.PinSubCategory.ToString());
			}
			if (Pin->PinType.PinSubCategoryObject.Get())
			{
				PinObj->SetStringField(TEXT("sub_category_object"), Pin->PinType.PinSubCategoryObject->GetPathName());
			}

			PinObj->SetBoolField(TEXT("is_connected"), Pin->LinkedTo.Num() > 0);

			// Default value
			if (!Pin->DefaultValue.IsEmpty())
			{
				PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
			}
			else if (!Pin->AutogeneratedDefaultValue.IsEmpty())
			{
				PinObj->SetStringField(TEXT("default_value"), Pin->AutogeneratedDefaultValue);
			}

			// Linked pins
			TArray<TSharedPtr<FJsonValue>> LinkedArray;
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin) continue;
				const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
				if (!LinkedNode) continue;

				TSharedPtr<FJsonObject> LinkedObj = MakeShared<FJsonObject>();
				LinkedObj->SetStringField(TEXT("node_id"), LinkedNode->NodeGuid.ToString());
				LinkedObj->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());
				LinkedArray.Add(MakeShared<FJsonValueObject>(LinkedObj));
			}
			PinObj->SetArrayField(TEXT("linked_to"), LinkedArray);

			PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));

			// Collect edges (output only, de-duplicate)
			if (Pin->Direction == EGPD_Output)
			{
				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin) continue;
					const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
					if (!LinkedNode) continue;

					FString EdgeKey = FString::Printf(TEXT("%s:%s->%s:%s"),
						*Node->NodeGuid.ToString(), *Pin->PinName.ToString(),
						*LinkedNode->NodeGuid.ToString(), *LinkedPin->PinName.ToString());

					if (!SeenEdges.Contains(EdgeKey))
					{
						SeenEdges.Add(EdgeKey);
						TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
						EdgeObj->SetStringField(TEXT("from_node"), Node->NodeGuid.ToString());
						EdgeObj->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
						EdgeObj->SetStringField(TEXT("to_node"), LinkedNode->NodeGuid.ToString());
						EdgeObj->SetStringField(TEXT("to_pin"), LinkedPin->PinName.ToString());
						EdgesArray.Add(MakeShared<FJsonValueObject>(EdgeObj));
					}
				}
			}
		}
		NodeObj->SetArrayField(TEXT("pins"), PinsArray);
		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	// Build result
	UBlueprint* BP = Cast<UBlueprint>(BPEditor->GetBlueprintObj());
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("blueprint_name"), BP ? BP->GetName() : TEXT("Unknown"));
	ResultData->SetStringField(TEXT("graph_name"), FocusedGraph->GetName());
	ResultData->SetNumberField(TEXT("selected_count"), NodesArray.Num());
	ResultData->SetNumberField(TEXT("edge_count"), EdgesArray.Num());
	ResultData->SetArrayField(TEXT("nodes"), NodesArray);
	ResultData->SetArrayField(TEXT("edges"), EdgesArray);
	return CreateSuccessResponse(ResultData);
}