// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/GraphActions.h"
#include "MCPCommonUtils.h"
#include "MCPContext.h"

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_Self.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_Knot.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_ExecutionSequence.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphUtilities.h"
#include "UObject/UObjectIterator.h"


// ============================================================================
// Helpers
// ============================================================================

namespace PatchHelpers
{
	/** Resolve a node ref string to an actual graph node.
	 *  Supports:  temp IDs ("new_xxx") → lookup in TempIdMap
	 *             "$last_node"         → Context alias
	 *             GUID string          → direct lookup
	 */
	static UEdGraphNode* ResolveNodeRef(const FString& NodeRef, UEdGraph* Graph,
		const TMap<FString, FGuid>& TempIdMap)
	{
		if (!Graph || NodeRef.IsEmpty())
		{
			return nullptr;
		}

		FGuid TargetGuid;

		// 1) Check temp ID map first
		if (const FGuid* Found = TempIdMap.Find(NodeRef))
		{
			TargetGuid = *Found;
		}
		// 2) Try parse as raw GUID
		else if (FGuid::Parse(NodeRef, TargetGuid))
		{
			// Successfully parsed
		}
		else
		{
			return nullptr;
		}

		// Find node by GUID
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == TargetGuid)
			{
				return Node;
			}
		}
		return nullptr;
	}

	/** Find pin on node by name, trying both directions if needed */
	static UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName,
		EEdGraphPinDirection PreferredDirection = EGPD_MAX)
	{
		if (!Node)
		{
			return nullptr;
		}

		// If a direction is specified, try that first
		if (PreferredDirection != EGPD_MAX)
		{
			UEdGraphPin* Pin = FMCPCommonUtils::FindPin(Node, PinName, PreferredDirection);
			if (Pin)
			{
				return Pin;
			}
		}

		// Try both directions
		UEdGraphPin* Pin = FMCPCommonUtils::FindPin(Node, PinName, EGPD_Output);
		if (!Pin)
		{
			Pin = FMCPCommonUtils::FindPin(Node, PinName, EGPD_Input);
		}
		return Pin;
	}
}


// ============================================================================
// FPatchOp
// ============================================================================

EPatchOpType FPatchOp::ParseOpType(const FString& OpStr)
{
	if (OpStr == TEXT("add_node"))             return EPatchOpType::AddNode;
	if (OpStr == TEXT("remove_node"))          return EPatchOpType::RemoveNode;
	if (OpStr == TEXT("set_node_property"))    return EPatchOpType::SetNodeProperty;
	if (OpStr == TEXT("connect"))              return EPatchOpType::Connect;
	if (OpStr == TEXT("disconnect"))           return EPatchOpType::Disconnect;
	if (OpStr == TEXT("add_variable"))         return EPatchOpType::AddVariable;
	if (OpStr == TEXT("set_variable_default")) return EPatchOpType::SetVariableDefault;
	if (OpStr == TEXT("set_pin_default"))      return EPatchOpType::SetPinDefault;
	return EPatchOpType::Invalid;
}

FString FPatchOp::OpTypeToString(EPatchOpType Type)
{
	switch (Type)
	{
	case EPatchOpType::AddNode:            return TEXT("add_node");
	case EPatchOpType::RemoveNode:         return TEXT("remove_node");
	case EPatchOpType::SetNodeProperty:    return TEXT("set_node_property");
	case EPatchOpType::Connect:            return TEXT("connect");
	case EPatchOpType::Disconnect:         return TEXT("disconnect");
	case EPatchOpType::AddVariable:        return TEXT("add_variable");
	case EPatchOpType::SetVariableDefault: return TEXT("set_variable_default");
	case EPatchOpType::SetPinDefault:      return TEXT("set_pin_default");
	default:                               return TEXT("invalid");
	}
}


// ============================================================================
// FPatchOpResult
// ============================================================================

TSharedPtr<FJsonObject> FPatchOpResult::ToJson() const
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("op_index"), OpIndex);
	Obj->SetStringField(TEXT("op_type"), OpType);
	Obj->SetBoolField(TEXT("success"), bSuccess);
	Obj->SetStringField(TEXT("message"), Message);
	if (!NodeId.IsEmpty())
	{
		Obj->SetStringField(TEXT("node_id"), NodeId);
	}
	return Obj;
}


// ============================================================================
// P3.1 — FGraphDescribeEnhancedAction
// ============================================================================

bool FGraphDescribeEnhancedAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FGraphDescribeEnhancedAction::SerializePinEnhanced(
	const UEdGraphPin* Pin, bool bIncludeHidden, bool bIncludeOrphan)
{
	if (!Pin)
	{
		return nullptr;
	}
	if (!bIncludeHidden && Pin->bHidden)
	{
		return nullptr;
	}
	if (!bIncludeOrphan && Pin->bOrphanedPin)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
	PinObj->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
	PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));

	// Full FEdGraphPinType serialization
	TSharedPtr<FJsonObject> PinTypeObj = MakeShared<FJsonObject>();
	PinTypeObj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
	if (Pin->PinType.PinSubCategory != NAME_None)
	{
		PinTypeObj->SetStringField(TEXT("sub_category"), Pin->PinType.PinSubCategory.ToString());
	}
	if (Pin->PinType.PinSubCategoryObject.Get())
	{
		PinTypeObj->SetStringField(TEXT("sub_category_object"), Pin->PinType.PinSubCategoryObject->GetPathName());
	}
	PinTypeObj->SetBoolField(TEXT("is_array"), Pin->PinType.IsArray());
	PinTypeObj->SetBoolField(TEXT("is_set"), Pin->PinType.IsSet());
	PinTypeObj->SetBoolField(TEXT("is_map"), Pin->PinType.IsMap());
	PinTypeObj->SetBoolField(TEXT("is_reference"), Pin->PinType.bIsReference);
	PinTypeObj->SetBoolField(TEXT("is_const"), Pin->PinType.bIsConst);
	PinObj->SetObjectField(TEXT("pin_type"), PinTypeObj);

	PinObj->SetBoolField(TEXT("is_connected"), Pin->LinkedTo.Num() > 0);
	PinObj->SetBoolField(TEXT("is_hidden"), Pin->bHidden);
	PinObj->SetBoolField(TEXT("is_orphaned"), Pin->bOrphanedPin);

	// Linked pins
	TArray<TSharedPtr<FJsonValue>> LinkedArray;
	for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
	{
		if (!LinkedPin)
		{
			continue;
		}
		const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
		if (!LinkedNode)
		{
			continue;
		}

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
	if (Pin->DefaultObject)
	{
		PinObj->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
	}

	return PinObj;
}

TSharedPtr<FJsonObject> FGraphDescribeEnhancedAction::SerializeNodeMetadata(const UEdGraphNode* Node)
{
	TSharedPtr<FJsonObject> MetaObj = MakeShared<FJsonObject>();
	if (!Node)
	{
		return MetaObj;
	}

	MetaObj->SetBoolField(TEXT("breakpoint_enabled"), Node->bHasCompilerMessage);
	MetaObj->SetStringField(TEXT("enabled_state"),
		Node->GetDesiredEnabledState() == ENodeEnabledState::Enabled ? TEXT("Enabled")
		: Node->GetDesiredEnabledState() == ENodeEnabledState::Disabled ? TEXT("Disabled")
		: TEXT("DevelopmentOnly"));
	MetaObj->SetBoolField(TEXT("comment_bubble_visible"), Node->bCommentBubbleVisible);
	MetaObj->SetBoolField(TEXT("is_node_enabled"), Node->IsNodeEnabled());

	return MetaObj;
}

TArray<TSharedPtr<FJsonValue>> FGraphDescribeEnhancedAction::CollectVariableReferences(
	UBlueprint* Blueprint, UEdGraph* Graph)
{
	TArray<TSharedPtr<FJsonValue>> VarRefs;
	if (!Blueprint || !Graph)
	{
		return VarRefs;
	}

	// Map: variable name → array of node GUIDs that reference it
	TMap<FString, TArray<FString>> VarToNodes;

	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		// Check VariableGet nodes
		if (const UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(Node))
		{
			FString VarName = VarGet->GetVarName().ToString();
			VarToNodes.FindOrAdd(VarName).Add(Node->NodeGuid.ToString());
		}
		// Check VariableSet nodes
		else if (const UK2Node_VariableSet* VarSet = Cast<UK2Node_VariableSet>(Node))
		{
			FString VarName = VarSet->GetVarName().ToString();
			VarToNodes.FindOrAdd(VarName).Add(Node->NodeGuid.ToString());
		}
	}

	for (const auto& Pair : VarToNodes)
	{
		TSharedPtr<FJsonObject> RefObj = MakeShared<FJsonObject>();
		RefObj->SetStringField(TEXT("variable_name"), Pair.Key);

		TArray<TSharedPtr<FJsonValue>> NodeIds;
		for (const FString& NodeId : Pair.Value)
		{
			NodeIds.Add(MakeShared<FJsonValueString>(NodeId));
		}
		RefObj->SetArrayField(TEXT("referencing_nodes"), NodeIds);

		VarRefs.Add(MakeShared<FJsonValueObject>(RefObj));
	}

	return VarRefs;
}

TSharedPtr<FJsonObject> FGraphDescribeEnhancedAction::SerializeFunctionSignature(const UEdGraphNode* Node)
{
	const UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(Node);
	if (!FuncNode)
	{
		return nullptr;
	}

	const UFunction* Function = FuncNode->GetTargetFunction();
	if (!Function)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> SigObj = MakeShared<FJsonObject>();
	SigObj->SetStringField(TEXT("function_name"), Function->GetName());
	SigObj->SetStringField(TEXT("owner_class"), Function->GetOwnerClass() ? Function->GetOwnerClass()->GetName() : TEXT("Unknown"));
	SigObj->SetBoolField(TEXT("is_static"), Function->HasAnyFunctionFlags(FUNC_Static));
	SigObj->SetBoolField(TEXT("is_pure"), Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
	SigObj->SetBoolField(TEXT("is_const"), Function->HasAnyFunctionFlags(FUNC_Const));

	// Parameters
	TArray<TSharedPtr<FJsonValue>> ParamsArray;
	for (TFieldIterator<FProperty> PropIt(Function); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		if (!Prop)
		{
			continue;
		}

		TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
		ParamObj->SetStringField(TEXT("name"), Prop->GetName());
		ParamObj->SetStringField(TEXT("type"), Prop->GetCPPType());
		ParamObj->SetBoolField(TEXT("is_return"), Prop->HasAnyPropertyFlags(CPF_ReturnParm));
		ParamObj->SetBoolField(TEXT("is_out"), Prop->HasAnyPropertyFlags(CPF_OutParm) && !Prop->HasAnyPropertyFlags(CPF_ReturnParm));

		ParamsArray.Add(MakeShared<FJsonValueObject>(ParamObj));
	}
	SigObj->SetArrayField(TEXT("parameters"), ParamsArray);

	return SigObj;
}

// ---- O6: Compact pin serialization for graph.describe_enhanced ----
TSharedPtr<FJsonObject> FGraphDescribeEnhancedAction::SerializePinCompact(
	const UEdGraphPin* Pin, bool bIncludeHidden, bool bIncludeOrphan)
{
	if (!Pin) return nullptr;
	if (Pin->bHidden && !bIncludeHidden) return nullptr;
	if (Pin->bOrphanedPin && !bIncludeOrphan) return nullptr;

	TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
	PinObj->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
	PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
	PinObj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());

	if (Pin->PinType.PinSubCategoryObject.IsValid())
	{
		PinObj->SetStringField(TEXT("sub_type"), Pin->PinType.PinSubCategoryObject->GetName());
	}

	PinObj->SetBoolField(TEXT("is_connected"), Pin->LinkedTo.Num() > 0);

	if (Pin->LinkedTo.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> LinkedArray;
		for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
			TSharedPtr<FJsonObject> LinkedObj = MakeShared<FJsonObject>();
			LinkedObj->SetStringField(TEXT("node_id"), LinkedPin->GetOwningNode()->NodeGuid.ToString());
			LinkedObj->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());
			LinkedArray.Add(MakeShared<FJsonValueObject>(LinkedObj));
		}
		PinObj->SetArrayField(TEXT("linked_to"), LinkedArray);
	}

	if (!Pin->DefaultValue.IsEmpty())
	{
		PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
	}
	else if (!Pin->AutogeneratedDefaultValue.IsEmpty())
	{
		PinObj->SetStringField(TEXT("default_value"), Pin->AutogeneratedDefaultValue);
	}
	if (Pin->DefaultObject)
	{
		PinObj->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
	}

	return PinObj;
}

TSharedPtr<FJsonObject> FGraphDescribeEnhancedAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);
	if (!TargetGraph)
	{
		return CreateErrorResponse(TEXT("Target graph not found"));
	}

	const bool bIncludeHidden = GetOptionalBool(Params, TEXT("include_hidden_pins"), false);
	const bool bIncludeOrphan = GetOptionalBool(Params, TEXT("include_orphan_pins"), false);
	const bool bCompact = GetOptionalBool(Params, TEXT("compact"), false);

	// Build enhanced node array and edges
	TArray<TSharedPtr<FJsonValue>> NodesArray;
	TArray<TSharedPtr<FJsonValue>> EdgesArray;
	TSet<FString> SeenEdges;

	for (const UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

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

		// Node metadata — omitted in compact mode (O6)
		if (!bCompact)
		{
			NodeObj->SetObjectField(TEXT("metadata"), SerializeNodeMetadata(Node));
		}

		// Function signature — omitted in compact mode (O6)
		if (!bCompact)
		{
			TSharedPtr<FJsonObject> FuncSig = SerializeFunctionSignature(Node);
			if (FuncSig.IsValid())
			{
				NodeObj->SetObjectField(TEXT("function_signature"), FuncSig);
			}
		}

		// Pin serialization: compact or enhanced
		TArray<TSharedPtr<FJsonValue>> PinsArray;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			TSharedPtr<FJsonObject> PinObj = bCompact
				? SerializePinCompact(Pin, bIncludeHidden, bIncludeOrphan)
				: SerializePinEnhanced(Pin, bIncludeHidden, bIncludeOrphan);
			if (PinObj.IsValid())
			{
				PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
			}

			// Collect edges (output pins only)
			if (Pin && Pin->Direction == EGPD_Output)
			{
				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin)
					{
						continue;
					}
					const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
					if (!LinkedNode)
					{
						continue;
					}

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

	// Variable references — omitted in compact mode (O6)
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("graph_name"), TargetGraph->GetName());
	ResultData->SetBoolField(TEXT("compact"), bCompact);
	ResultData->SetNumberField(TEXT("node_count"), NodesArray.Num());
	ResultData->SetNumberField(TEXT("edge_count"), EdgesArray.Num());
	ResultData->SetArrayField(TEXT("nodes"), NodesArray);
	ResultData->SetArrayField(TEXT("edges"), EdgesArray);

	if (!bCompact)
	{
		TArray<TSharedPtr<FJsonValue>> VarRefs = CollectVariableReferences(Blueprint, TargetGraph);
		ResultData->SetArrayField(TEXT("variable_references"), VarRefs);
	}

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// P3.3 — FApplyPatchAction
// ============================================================================

bool FApplyPatchAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateGraph(Params, Context, OutError))
	{
		return false;
	}

	// Validate ops array exists
	const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("ops"), OpsArray) || !OpsArray || OpsArray->Num() == 0)
	{
		OutError = TEXT("'ops' array is required and must not be empty");
		return false;
	}

	if (OpsArray->Num() > 100)
	{
		OutError = FString::Printf(TEXT("Too many ops (%d). Maximum is 100."), OpsArray->Num());
		return false;
	}

	return true;
}

bool FApplyPatchAction::ParseOps(const TArray<TSharedPtr<FJsonValue>>& OpsJson,
	TArray<FPatchOp>& OutOps, FString& OutError)
{
	for (int32 Idx = 0; Idx < OpsJson.Num(); ++Idx)
	{
		const TSharedPtr<FJsonObject>* OpObj = nullptr;
		if (!OpsJson[Idx]->TryGetObject(OpObj) || !OpObj || !(*OpObj).IsValid())
		{
			OutError = FString::Printf(TEXT("ops[%d]: not a valid JSON object"), Idx);
			return false;
		}

		FString OpStr;
		if (!(*OpObj)->TryGetStringField(TEXT("op"), OpStr))
		{
			OutError = FString::Printf(TEXT("ops[%d]: missing 'op' field"), Idx);
			return false;
		}

		EPatchOpType OpType = FPatchOp::ParseOpType(OpStr);
		if (OpType == EPatchOpType::Invalid)
		{
			OutError = FString::Printf(TEXT("ops[%d]: unknown op type '%s'"), Idx, *OpStr);
			return false;
		}

		FPatchOp Op;
		Op.OpType = OpType;
		Op.Params = *OpObj;
		Op.OpIndex = Idx;
		OutOps.Add(MoveTemp(Op));
	}

	return true;
}

UEdGraphNode* FApplyPatchAction::ResolveNodeRef(const FString& NodeRef, UEdGraph* Graph,
	const TMap<FString, FGuid>& TempIdMap) const
{
	return PatchHelpers::ResolveNodeRef(NodeRef, Graph, TempIdMap);
}

UEdGraphPin* FApplyPatchAction::FindPinByName(UEdGraphNode* Node, const FString& PinName,
	EEdGraphPinDirection Direction) const
{
	return PatchHelpers::FindPinByName(Node, PinName, Direction);
}

FPatchOpResult FApplyPatchAction::ValidateOp(const FPatchOp& Op, UBlueprint* Blueprint,
	UEdGraph* Graph, const TMap<FString, FGuid>& TempIdMap) const
{
	FPatchOpResult Result;
	Result.OpIndex = Op.OpIndex;
	Result.OpType = FPatchOp::OpTypeToString(Op.OpType);
	Result.bSuccess = true;
	Result.Message = TEXT("OK");

	switch (Op.OpType)
	{
	case EPatchOpType::AddNode:
	{
		FString TempId, NodeType;
		if (!Op.Params->TryGetStringField(TEXT("id"), TempId) || TempId.IsEmpty())
		{
			Result.bSuccess = false;
			Result.Message = TEXT("add_node: 'id' is required");
		}
		else if (TempIdMap.Contains(TempId))
		{
			Result.bSuccess = false;
			Result.Message = FString::Printf(TEXT("add_node: duplicate temp id '%s'"), *TempId);
		}
		else if (!Op.Params->TryGetStringField(TEXT("node_type"), NodeType) || NodeType.IsEmpty())
		{
			Result.bSuccess = false;
			Result.Message = TEXT("add_node: 'node_type' is required");
		}
		break;
	}
	case EPatchOpType::RemoveNode:
	{
		FString NodeRef;
		if (!Op.Params->TryGetStringField(TEXT("node"), NodeRef) || NodeRef.IsEmpty())
		{
			Result.bSuccess = false;
			Result.Message = TEXT("remove_node: 'node' is required");
		}
		break;
	}
	case EPatchOpType::SetNodeProperty:
	{
		FString NodeRef, PropName;
		if (!Op.Params->TryGetStringField(TEXT("node"), NodeRef) || NodeRef.IsEmpty())
		{
			Result.bSuccess = false;
			Result.Message = TEXT("set_node_property: 'node' is required");
		}
		else if (!Op.Params->TryGetStringField(TEXT("property"), PropName) || PropName.IsEmpty())
		{
			Result.bSuccess = false;
			Result.Message = TEXT("set_node_property: 'property' is required");
		}
		break;
	}
	case EPatchOpType::Connect:
	{
		const TSharedPtr<FJsonObject>* FromObj = nullptr;
		const TSharedPtr<FJsonObject>* ToObj = nullptr;
		if (!Op.Params->TryGetObjectField(TEXT("from"), FromObj) || !FromObj)
		{
			Result.bSuccess = false;
			Result.Message = TEXT("connect: 'from' object is required");
		}
		else if (!Op.Params->TryGetObjectField(TEXT("to"), ToObj) || !ToObj)
		{
			Result.bSuccess = false;
			Result.Message = TEXT("connect: 'to' object is required");
		}
		else
		{
			FString FromNode, FromPin, ToNode, ToPin;
			if (!(*FromObj)->TryGetStringField(TEXT("node"), FromNode) || FromNode.IsEmpty())
			{
				Result.bSuccess = false;
				Result.Message = TEXT("connect: 'from.node' is required");
			}
			else if (!(*FromObj)->TryGetStringField(TEXT("pin"), FromPin) || FromPin.IsEmpty())
			{
				Result.bSuccess = false;
				Result.Message = TEXT("connect: 'from.pin' is required");
			}
			else if (!(*ToObj)->TryGetStringField(TEXT("node"), ToNode) || ToNode.IsEmpty())
			{
				Result.bSuccess = false;
				Result.Message = TEXT("connect: 'to.node' is required");
			}
			else if (!(*ToObj)->TryGetStringField(TEXT("pin"), ToPin) || ToPin.IsEmpty())
			{
				Result.bSuccess = false;
				Result.Message = TEXT("connect: 'to.pin' is required");
			}
		}
		break;
	}
	case EPatchOpType::Disconnect:
	{
		FString NodeRef, PinName;
		if (!Op.Params->TryGetStringField(TEXT("node"), NodeRef) || NodeRef.IsEmpty())
		{
			Result.bSuccess = false;
			Result.Message = TEXT("disconnect: 'node' is required");
		}
		else if (!Op.Params->TryGetStringField(TEXT("pin"), PinName) || PinName.IsEmpty())
		{
			Result.bSuccess = false;
			Result.Message = TEXT("disconnect: 'pin' is required");
		}
		break;
	}
	case EPatchOpType::AddVariable:
	{
		FString VarName, VarType;
		if (!Op.Params->TryGetStringField(TEXT("name"), VarName) || VarName.IsEmpty())
		{
			Result.bSuccess = false;
			Result.Message = TEXT("add_variable: 'name' is required");
		}
		else if (!Op.Params->TryGetStringField(TEXT("type"), VarType) || VarType.IsEmpty())
		{
			Result.bSuccess = false;
			Result.Message = TEXT("add_variable: 'type' is required");
		}
		break;
	}
	case EPatchOpType::SetVariableDefault:
	{
		FString VarName;
		if (!Op.Params->TryGetStringField(TEXT("name"), VarName) || VarName.IsEmpty())
		{
			Result.bSuccess = false;
			Result.Message = TEXT("set_variable_default: 'name' is required");
		}
		break;
	}
	case EPatchOpType::SetPinDefault:
	{
		FString NodeRef, PinName;
		if (!Op.Params->TryGetStringField(TEXT("node"), NodeRef) || NodeRef.IsEmpty())
		{
			Result.bSuccess = false;
			Result.Message = TEXT("set_pin_default: 'node' is required");
		}
		else if (!Op.Params->TryGetStringField(TEXT("pin"), PinName) || PinName.IsEmpty())
		{
			Result.bSuccess = false;
			Result.Message = TEXT("set_pin_default: 'pin' is required");
		}
		break;
	}
	default:
		Result.bSuccess = false;
		Result.Message = TEXT("Unknown op type");
		break;
	}

	return Result;
}

FPatchOpResult FApplyPatchAction::ExecuteOp(const FPatchOp& Op, UBlueprint* Blueprint,
	UEdGraph* Graph, TMap<FString, FGuid>& TempIdMap, FMCPEditorContext& Context) const
{
	switch (Op.OpType)
	{
	case EPatchOpType::AddNode:
		return ExecuteAddNode(Op, Blueprint, Graph, TempIdMap, Context);
	case EPatchOpType::RemoveNode:
		return ExecuteRemoveNode(Op, Blueprint, Graph, TempIdMap);
	case EPatchOpType::SetNodeProperty:
		return ExecuteSetNodeProperty(Op, Graph, TempIdMap);
	case EPatchOpType::Connect:
		return ExecuteConnect(Op, Graph, TempIdMap);
	case EPatchOpType::Disconnect:
		return ExecuteDisconnect(Op, Graph, TempIdMap);
	case EPatchOpType::AddVariable:
		return ExecuteAddVariable(Op, Blueprint);
	case EPatchOpType::SetVariableDefault:
		return ExecuteSetVariableDefault(Op, Blueprint);
	case EPatchOpType::SetPinDefault:
		return ExecuteSetPinDefault(Op, Graph, TempIdMap);
	default:
	{
		FPatchOpResult Result;
		Result.OpIndex = Op.OpIndex;
		Result.OpType = FPatchOp::OpTypeToString(Op.OpType);
		Result.bSuccess = false;
		Result.Message = TEXT("Unknown op type");
		return Result;
	}
	}
}

// -- Individual op executors --

FPatchOpResult FApplyPatchAction::ExecuteAddNode(const FPatchOp& Op, UBlueprint* Blueprint,
	UEdGraph* Graph, TMap<FString, FGuid>& TempIdMap, FMCPEditorContext& Context) const
{
	FPatchOpResult Result;
	Result.OpIndex = Op.OpIndex;
	Result.OpType = TEXT("add_node");

	FString TempId = Op.Params->GetStringField(TEXT("id"));
	FString NodeType = Op.Params->GetStringField(TEXT("node_type"));

	double PosX = 0.0;
	double PosY = 0.0;
	Op.Params->TryGetNumberField(TEXT("pos_x"), PosX);
	Op.Params->TryGetNumberField(TEXT("pos_y"), PosY);
	FVector2D Position(PosX, PosY);

	UEdGraphNode* CreatedNode = nullptr;

	// Route by node_type
	if (NodeType.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
	{
		FString EventName;
		if (!Op.Params->TryGetStringField(TEXT("event_name"), EventName))
		{
			Result.bSuccess = false;
			Result.Message = TEXT("add_node(Event): 'event_name' is required");
			return Result;
		}

		// Check if event already exists
		UK2Node_Event* ExistingEvent = FMCPCommonUtils::FindExistingEventNode(Graph, EventName);
		if (ExistingEvent)
		{
			// Reuse existing event node
			CreatedNode = ExistingEvent;
			Result.Message = FString::Printf(TEXT("Reused existing event node '%s'"), *EventName);
		}
		else
		{
			UK2Node_Event* EventNode = FMCPCommonUtils::CreateEventNode(Graph, EventName, Position);
			if (!EventNode)
			{
				Result.bSuccess = false;
				Result.Message = FString::Printf(TEXT("Failed to create event node '%s'"), *EventName);
				return Result;
			}
			CreatedNode = EventNode;
		}
	}
	else if (NodeType.Equals(TEXT("CustomEvent"), ESearchCase::IgnoreCase))
	{
		FString EventName;
		Op.Params->TryGetStringField(TEXT("event_name"), EventName);
		if (EventName.IsEmpty())
		{
			EventName = TEXT("NewCustomEvent");
		}

		UK2Node_CustomEvent* CustomEvent = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CustomEvent>(
			Graph, Position, EK2NewNodeFlags::None,
			[&EventName](UK2Node_CustomEvent* Node)
			{
				Node->CustomFunctionName = FName(*EventName);
			}
		);

		if (!CustomEvent)
		{
			Result.bSuccess = false;
			Result.Message = TEXT("Failed to create custom event node");
			return Result;
		}
		CreatedNode = CustomEvent;
	}
	else if (NodeType.Equals(TEXT("FunctionCall"), ESearchCase::IgnoreCase))
	{
		FString FunctionName;
		if (!Op.Params->TryGetStringField(TEXT("function_name"), FunctionName))
		{
			Result.bSuccess = false;
			Result.Message = TEXT("add_node(FunctionCall): 'function_name' is required");
			return Result;
		}

		FString TargetClass;
		Op.Params->TryGetStringField(TEXT("target_class"), TargetClass);

		// Resolve function
		UFunction* Function = nullptr;

		// Try to find the function on the target class
		if (!TargetClass.IsEmpty())
		{
			// Well-known class shortcuts
			UClass* OwnerClass = nullptr;
			if (TargetClass.Equals(TEXT("self"), ESearchCase::IgnoreCase))
			{
				OwnerClass = Blueprint->GeneratedClass;

				// For target="self", also search common static function libraries
				// as a fallback (e.g. PrintString lives in KismetSystemLibrary)
				if (OwnerClass)
				{
					Function = OwnerClass->FindFunctionByName(FName(*FunctionName));
					if (!Function)
					{
						Function = OwnerClass->FindFunctionByName(FName(*FString::Printf(TEXT("K2_%s"), *FunctionName)));
					}
				}

				if (!Function)
				{
					static const TCHAR* SelfFallbackLibs[] = {
						TEXT("/Script/Engine.KismetSystemLibrary"),
						TEXT("/Script/Engine.GameplayStatics"),
						TEXT("/Script/Engine.KismetMathLibrary"),
						TEXT("/Script/Engine.KismetStringLibrary"),
						TEXT("/Script/Engine.KismetTextLibrary"),
						TEXT("/Script/Engine.KismetArrayLibrary"),
					};
					for (const TCHAR* LibPath : SelfFallbackLibs)
					{
						UClass* LibClass = FindObject<UClass>(nullptr, LibPath);
						if (LibClass)
						{
							Function = LibClass->FindFunctionByName(FName(*FunctionName));
							if (!Function)
							{
								Function = LibClass->FindFunctionByName(FName(*FString::Printf(TEXT("K2_%s"), *FunctionName)));
							}
							if (Function) break;
						}
					}
				}
			}
			else if (TargetClass.Equals(TEXT("GameplayStatics"), ESearchCase::IgnoreCase))
			{
				OwnerClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.GameplayStatics"));
			}
			else if (TargetClass.Equals(TEXT("KismetMathLibrary"), ESearchCase::IgnoreCase) || TargetClass.Equals(TEXT("Math"), ESearchCase::IgnoreCase))
			{
				OwnerClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.KismetMathLibrary"));
			}
			else if (TargetClass.Equals(TEXT("KismetSystemLibrary"), ESearchCase::IgnoreCase) || TargetClass.Equals(TEXT("System"), ESearchCase::IgnoreCase))
			{
				OwnerClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.KismetSystemLibrary"));
			}
			else if (TargetClass.Equals(TEXT("KismetStringLibrary"), ESearchCase::IgnoreCase) || TargetClass.Equals(TEXT("String"), ESearchCase::IgnoreCase))
			{
				OwnerClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.KismetStringLibrary"));
			}
			else if (TargetClass.Equals(TEXT("KismetTextLibrary"), ESearchCase::IgnoreCase) || TargetClass.Equals(TEXT("Text"), ESearchCase::IgnoreCase))
			{
				OwnerClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.KismetTextLibrary"));
			}
			else if (TargetClass.Equals(TEXT("KismetArrayLibrary"), ESearchCase::IgnoreCase) || TargetClass.Equals(TEXT("Array"), ESearchCase::IgnoreCase))
			{
				OwnerClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.KismetArrayLibrary"));
			}
			else
			{
				// Also try with "U" prefix if not already present
				TArray<FString> ClassVariants;
				ClassVariants.Add(TargetClass);
				if (!TargetClass.StartsWith(TEXT("U")) && !TargetClass.StartsWith(TEXT("A")))
				{
					ClassVariants.Add(TEXT("U") + TargetClass);
				}

				// Try loading by module paths
				static const FString ModulePaths[] = {
					TEXT("/Script/Engine."),
					TEXT("/Script/CoreUObject."),
					TEXT("/Script/UMG."),
					TEXT("/Script/AIModule."),
				};

				for (const FString& Prefix : ModulePaths)
				{
					if (OwnerClass) break;
					for (const FString& Variant : ClassVariants)
					{
						FString FullPath = Prefix + Variant;
						OwnerClass = FindObject<UClass>(nullptr, *FullPath);
						if (OwnerClass) break;
					}
				}

				// Dynamic fallback: scan all loaded UClass objects for a matching name.
				// This handles project-specific classes (e.g. PlayerStatusSubsystem,
				// PlayerStatusModel) without requiring the caller to know the full
				// /Script/ module path.
				if (!OwnerClass)
				{
					for (TObjectIterator<UClass> It; It; ++It)
					{
						if (OwnerClass) break;
						for (const FString& Variant : ClassVariants)
						{
							if (It->GetName() == Variant)
							{
								OwnerClass = *It;
								break;
							}
						}
					}
				}
			}

			if (OwnerClass)
			{
				Function = OwnerClass->FindFunctionByName(FName(*FunctionName));
				// Try K2_ prefix
				if (!Function)
				{
					Function = OwnerClass->FindFunctionByName(FName(*FString::Printf(TEXT("K2_%s"), *FunctionName)));
				}
			}
		}
		else
		{
			// No target class — try self first, then common static libraries
			if (Blueprint->GeneratedClass)
			{
				Function = Blueprint->GeneratedClass->FindFunctionByName(FName(*FunctionName));
			}
			if (!Function)
			{
				// Try common static function libraries
				static const TCHAR* LibraryPaths[] = {
					TEXT("/Script/Engine.KismetSystemLibrary"),
					TEXT("/Script/Engine.GameplayStatics"),
					TEXT("/Script/Engine.KismetMathLibrary"),
					TEXT("/Script/Engine.KismetStringLibrary"),
					TEXT("/Script/Engine.KismetTextLibrary"),
					TEXT("/Script/Engine.KismetArrayLibrary"),
				};
				for (const TCHAR* LibPath : LibraryPaths)
				{
					UClass* LibClass = FindObject<UClass>(nullptr, LibPath);
					if (LibClass)
					{
						Function = LibClass->FindFunctionByName(FName(*FunctionName));
						if (!Function)
						{
							Function = LibClass->FindFunctionByName(FName(*FString::Printf(TEXT("K2_%s"), *FunctionName)));
						}
						if (Function)
						{
							break;
						}
					}
				}
			}
		}

		if (!Function)
		{
			Result.bSuccess = false;
			Result.Message = FString::Printf(TEXT("add_node(FunctionCall): function '%s' not found"), *FunctionName);
			return Result;
		}

		UK2Node_CallFunction* FuncNode = FMCPCommonUtils::CreateFunctionCallNode(Graph, Function, Position);
		if (!FuncNode)
		{
			Result.bSuccess = false;
			Result.Message = FString::Printf(TEXT("Failed to create function call node for '%s'"), *FunctionName);
			return Result;
		}

		// Apply defaults if provided
		const TSharedPtr<FJsonObject>* DefaultsObj = nullptr;
		if (Op.Params->TryGetObjectField(TEXT("defaults"), DefaultsObj) && DefaultsObj)
		{
			for (const auto& DefaultPair : (*DefaultsObj)->Values)
			{
				UEdGraphPin* Pin = FMCPCommonUtils::FindPin(FuncNode, DefaultPair.Key, EGPD_Input);
				if (Pin)
				{
					FString DefaultStr;
					if (DefaultPair.Value->TryGetString(DefaultStr))
					{
						Pin->DefaultValue = DefaultStr;
					}
				}
			}
		}

		CreatedNode = FuncNode;
	}
	else if (NodeType.Equals(TEXT("Branch"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("IfThenElse"), ESearchCase::IgnoreCase))
	{
		UK2Node_IfThenElse* BranchNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_IfThenElse>(
			Graph, Position, EK2NewNodeFlags::None,
			[](UK2Node_IfThenElse* Node) {}
		);
		if (!BranchNode)
		{
			Result.bSuccess = false;
			Result.Message = TEXT("Failed to create Branch node");
			return Result;
		}
		CreatedNode = BranchNode;
	}
	else if (NodeType.Equals(TEXT("VariableGet"), ESearchCase::IgnoreCase))
	{
		FString VarName;
		if (!Op.Params->TryGetStringField(TEXT("variable_name"), VarName))
		{
			Result.bSuccess = false;
			Result.Message = TEXT("add_node(VariableGet): 'variable_name' is required");
			return Result;
		}
		FName VariableFName(*VarName);

		UK2Node_VariableGet* VarGetNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableGet>(
			Graph, Position, EK2NewNodeFlags::None,
			[VariableFName](UK2Node_VariableGet* Node)
			{
				Node->VariableReference.SetSelfMember(VariableFName);
			}
		);
		if (!VarGetNode)
		{
			Result.bSuccess = false;
			Result.Message = FString::Printf(TEXT("Failed to create VariableGet node for '%s'"), *VarName);
			return Result;
		}
		CreatedNode = VarGetNode;
	}
	else if (NodeType.Equals(TEXT("VariableSet"), ESearchCase::IgnoreCase))
	{
		FString VarName;
		if (!Op.Params->TryGetStringField(TEXT("variable_name"), VarName))
		{
			Result.bSuccess = false;
			Result.Message = TEXT("add_node(VariableSet): 'variable_name' is required");
			return Result;
		}
		FName VariableFName(*VarName);

		UK2Node_VariableSet* VarSetNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableSet>(
			Graph, Position, EK2NewNodeFlags::None,
			[VariableFName](UK2Node_VariableSet* Node)
			{
				Node->VariableReference.SetSelfMember(VariableFName);
			}
		);
		if (!VarSetNode)
		{
			Result.bSuccess = false;
			Result.Message = FString::Printf(TEXT("Failed to create VariableSet node for '%s'"), *VarName);
			return Result;
		}
		CreatedNode = VarSetNode;
	}
	else if (NodeType.Equals(TEXT("Cast"), ESearchCase::IgnoreCase))
	{
		FString ClassName;
		if (!Op.Params->TryGetStringField(TEXT("class_name"), ClassName))
		{
			Result.bSuccess = false;
			Result.Message = TEXT("add_node(Cast): 'class_name' is required");
			return Result;
		}

		UClass* TargetUClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
		if (!TargetUClass)
		{
			TargetUClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *ClassName));
		}
		if (!TargetUClass)
		{
			Result.bSuccess = false;
			Result.Message = FString::Printf(TEXT("add_node(Cast): class '%s' not found"), *ClassName);
			return Result;
		}

		UK2Node_DynamicCast* CastNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_DynamicCast>(
			Graph, Position, EK2NewNodeFlags::None,
			[TargetUClass](UK2Node_DynamicCast* Node)
			{
				Node->TargetType = TargetUClass;
			}
		);
		if (!CastNode)
		{
			Result.bSuccess = false;
			Result.Message = TEXT("Failed to create Cast node");
			return Result;
		}
		CreatedNode = CastNode;
	}
	else if (NodeType.Equals(TEXT("Self"), ESearchCase::IgnoreCase))
	{
		UK2Node_Self* SelfNode = FMCPCommonUtils::CreateSelfReferenceNode(Graph, Position);
		if (!SelfNode)
		{
			Result.bSuccess = false;
			Result.Message = TEXT("Failed to create Self node");
			return Result;
		}
		CreatedNode = SelfNode;
	}
	else if (NodeType.Equals(TEXT("Reroute"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("Knot"), ESearchCase::IgnoreCase))
	{
		UK2Node_Knot* KnotNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_Knot>(
			Graph, Position, EK2NewNodeFlags::None,
			[](UK2Node_Knot* Node) {}
		);
		if (!KnotNode)
		{
			Result.bSuccess = false;
			Result.Message = TEXT("Failed to create Reroute node");
			return Result;
		}
		CreatedNode = KnotNode;
	}
	else if (NodeType.Equals(TEXT("MacroInstance"), ESearchCase::IgnoreCase))
	{
		FString MacroName;
		if (!Op.Params->TryGetStringField(TEXT("macro_name"), MacroName))
		{
			Result.bSuccess = false;
			Result.Message = TEXT("add_node(MacroInstance): 'macro_name' is required");
			return Result;
		}

		// Find macro graph
		UEdGraph* MacroGraph = nullptr;

		// Search common engine macros (ForEachLoop, ForLoop, WhileLoop, etc.)
		for (UBlueprint* MacroBP : TObjectRange<UBlueprint>())
		{
			if (!MacroBP || MacroBP->BlueprintType != BPTYPE_MacroLibrary)
			{
				continue;
			}
			for (UEdGraph* MacroG : MacroBP->MacroGraphs)
			{
				if (MacroG && MacroG->GetFName().ToString().Equals(MacroName, ESearchCase::IgnoreCase))
				{
					MacroGraph = MacroG;
					break;
				}
			}
			if (MacroGraph)
			{
				break;
			}
		}

		if (!MacroGraph)
		{
			Result.bSuccess = false;
			Result.Message = FString::Printf(TEXT("Macro '%s' not found"), *MacroName);
			return Result;
		}

		UK2Node_MacroInstance* MacroNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_MacroInstance>(
			Graph, Position, EK2NewNodeFlags::None,
			[MacroGraph](UK2Node_MacroInstance* Node)
			{
				Node->SetMacroGraph(MacroGraph);
			}
		);
		if (!MacroNode)
		{
			Result.bSuccess = false;
			Result.Message = TEXT("Failed to create MacroInstance node");
			return Result;
		}
		CreatedNode = MacroNode;
	}
	else if (NodeType.Equals(TEXT("Sequence"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("ExecutionSequence"), ESearchCase::IgnoreCase))
	{
		UK2Node_ExecutionSequence* SeqNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_ExecutionSequence>(
			Graph, Position, EK2NewNodeFlags::None,
			[](UK2Node_ExecutionSequence* Node) {}
		);
		if (!SeqNode)
		{
			Result.bSuccess = false;
			Result.Message = TEXT("Failed to create Sequence node");
			return Result;
		}
		CreatedNode = SeqNode;
	}
	else if (NodeType.Equals(TEXT("CreateDelegate"), ESearchCase::IgnoreCase))
	{
		FString FuncName;
		Op.Params->TryGetStringField(TEXT("function_name"), FuncName);

		UK2Node_CreateDelegate* DelegateNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CreateDelegate>(
			Graph, Position, EK2NewNodeFlags::None,
			[](UK2Node_CreateDelegate* Node) {}
		);
		if (!DelegateNode)
		{
			Result.bSuccess = false;
			Result.Message = TEXT("Failed to create CreateDelegate node");
			return Result;
		}

		// Set function name if provided (caller should connect delegate output first)
		if (!FuncName.IsEmpty())
		{
			DelegateNode->SetFunction(FName(*FuncName));
			DelegateNode->HandleAnyChange(true);
		}

		CreatedNode = DelegateNode;
	}
	else
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("add_node: unsupported node_type '%s'. Supported: Event, CustomEvent, FunctionCall, Branch, Sequence, VariableGet, VariableSet, Cast, Self, Reroute, MacroInstance, CreateDelegate"), *NodeType);
		return Result;
	}

	if (CreatedNode)
	{
		// Register temp ID mapping
		TempIdMap.Add(TempId, CreatedNode->NodeGuid);

		// Register in context for $last_node
		Context.LastCreatedNodeId = CreatedNode->NodeGuid;

		Result.bSuccess = true;
		Result.NodeId = CreatedNode->NodeGuid.ToString();
		if (Result.Message.IsEmpty())
		{
			Result.Message = FString::Printf(TEXT("Created %s node"), *NodeType);
		}
	}

	return Result;
}

FPatchOpResult FApplyPatchAction::ExecuteRemoveNode(const FPatchOp& Op, UBlueprint* Blueprint,
	UEdGraph* Graph, const TMap<FString, FGuid>& TempIdMap) const
{
	FPatchOpResult Result;
	Result.OpIndex = Op.OpIndex;
	Result.OpType = TEXT("remove_node");

	FString NodeRef = Op.Params->GetStringField(TEXT("node"));
	UEdGraphNode* NodeToRemove = ResolveNodeRef(NodeRef, Graph, TempIdMap);
	if (!NodeToRemove)
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("Node '%s' not found"), *NodeRef);
		return Result;
	}

	// Break all pin links first
	for (UEdGraphPin* Pin : NodeToRemove->Pins)
	{
		if (Pin)
		{
			Pin->BreakAllPinLinks();
		}
	}

	Graph->RemoveNode(NodeToRemove);

	Result.bSuccess = true;
	Result.Message = TEXT("Node removed");
	return Result;
}

FPatchOpResult FApplyPatchAction::ExecuteSetNodeProperty(const FPatchOp& Op, UEdGraph* Graph,
	const TMap<FString, FGuid>& TempIdMap) const
{
	FPatchOpResult Result;
	Result.OpIndex = Op.OpIndex;
	Result.OpType = TEXT("set_node_property");

	FString NodeRef = Op.Params->GetStringField(TEXT("node"));
	FString PropName = Op.Params->GetStringField(TEXT("property"));
	FString PropValue;
	Op.Params->TryGetStringField(TEXT("value"), PropValue);

	UEdGraphNode* TargetNode = ResolveNodeRef(NodeRef, Graph, TempIdMap);
	if (!TargetNode)
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("Node '%s' not found"), *NodeRef);
		return Result;
	}

	// Handle known properties
	if (PropName.Equals(TEXT("Comment"), ESearchCase::IgnoreCase) || PropName.Equals(TEXT("NodeComment"), ESearchCase::IgnoreCase))
	{
		TargetNode->NodeComment = PropValue;
		TargetNode->bCommentBubbleVisible = !PropValue.IsEmpty();
		Result.bSuccess = true;
		Result.Message = TEXT("Comment set");
	}
	else if (PropName.Equals(TEXT("EnabledState"), ESearchCase::IgnoreCase))
	{
		if (PropValue.Equals(TEXT("Enabled"), ESearchCase::IgnoreCase))
		{
			TargetNode->SetEnabledState(ENodeEnabledState::Enabled);
		}
		else if (PropValue.Equals(TEXT("Disabled"), ESearchCase::IgnoreCase))
		{
			TargetNode->SetEnabledState(ENodeEnabledState::Disabled);
		}
		else if (PropValue.Equals(TEXT("DevelopmentOnly"), ESearchCase::IgnoreCase))
		{
			TargetNode->SetEnabledState(ENodeEnabledState::DevelopmentOnly);
		}
		else
		{
			Result.bSuccess = false;
			Result.Message = FString::Printf(TEXT("Unknown EnabledState '%s'. Use: Enabled, Disabled, DevelopmentOnly"), *PropValue);
			return Result;
		}
		Result.bSuccess = true;
		Result.Message = TEXT("EnabledState set");
	}
	else
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("Unknown node property '%s'. Supported: Comment, EnabledState"), *PropName);
	}

	return Result;
}

FPatchOpResult FApplyPatchAction::ExecuteConnect(const FPatchOp& Op, UEdGraph* Graph,
	const TMap<FString, FGuid>& TempIdMap) const
{
	FPatchOpResult Result;
	Result.OpIndex = Op.OpIndex;
	Result.OpType = TEXT("connect");

	const TSharedPtr<FJsonObject>* FromObj = nullptr;
	const TSharedPtr<FJsonObject>* ToObj = nullptr;
	Op.Params->TryGetObjectField(TEXT("from"), FromObj);
	Op.Params->TryGetObjectField(TEXT("to"), ToObj);

	FString FromNodeRef = (*FromObj)->GetStringField(TEXT("node"));
	FString FromPinName = (*FromObj)->GetStringField(TEXT("pin"));
	FString ToNodeRef = (*ToObj)->GetStringField(TEXT("node"));
	FString ToPinName = (*ToObj)->GetStringField(TEXT("pin"));

	UEdGraphNode* FromNode = ResolveNodeRef(FromNodeRef, Graph, TempIdMap);
	if (!FromNode)
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("Source node '%s' not found"), *FromNodeRef);
		return Result;
	}

	UEdGraphNode* ToNode = ResolveNodeRef(ToNodeRef, Graph, TempIdMap);
	if (!ToNode)
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("Target node '%s' not found"), *ToNodeRef);
		return Result;
	}

	UEdGraphPin* FromPin = PatchHelpers::FindPinByName(FromNode, FromPinName, EGPD_Output);
	if (!FromPin)
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("Output pin '%s' not found on source node"), *FromPinName);
		return Result;
	}

	UEdGraphPin* ToPin = PatchHelpers::FindPinByName(ToNode, ToPinName, EGPD_Input);
	if (!ToPin)
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("Input pin '%s' not found on target node"), *ToPinName);
		return Result;
	}

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema)
	{
		Result.bSuccess = false;
		Result.Message = TEXT("Graph schema not found");
		return Result;
	}

	bool bConnected = Schema->TryCreateConnection(FromPin, ToPin);
	if (!bConnected)
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("Failed to connect %s.%s -> %s.%s"),
			*FromNodeRef, *FromPinName, *ToNodeRef, *ToPinName);
		return Result;
	}

	FromNode->PinConnectionListChanged(FromPin);
	ToNode->PinConnectionListChanged(ToPin);

	Result.bSuccess = true;
	Result.Message = TEXT("Connected");
	return Result;
}

FPatchOpResult FApplyPatchAction::ExecuteDisconnect(const FPatchOp& Op, UEdGraph* Graph,
	const TMap<FString, FGuid>& TempIdMap) const
{
	FPatchOpResult Result;
	Result.OpIndex = Op.OpIndex;
	Result.OpType = TEXT("disconnect");

	FString NodeRef = Op.Params->GetStringField(TEXT("node"));
	FString PinName = Op.Params->GetStringField(TEXT("pin"));

	UEdGraphNode* TargetNode = ResolveNodeRef(NodeRef, Graph, TempIdMap);
	if (!TargetNode)
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("Node '%s' not found"), *NodeRef);
		return Result;
	}

	UEdGraphPin* TargetPin = PatchHelpers::FindPinByName(TargetNode, PinName);
	if (!TargetPin)
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("Pin '%s' not found on node"), *PinName);
		return Result;
	}

	// Check for target-specific disconnect
	FString TargetNodeRef, TargetPinName;
	Op.Params->TryGetStringField(TEXT("target_node"), TargetNodeRef);
	Op.Params->TryGetStringField(TEXT("target_pin"), TargetPinName);

	if (!TargetNodeRef.IsEmpty() && !TargetPinName.IsEmpty())
	{
		UEdGraphNode* OtherNode = ResolveNodeRef(TargetNodeRef, Graph, TempIdMap);
		if (!OtherNode)
		{
			Result.bSuccess = false;
			Result.Message = FString::Printf(TEXT("Target node '%s' not found"), *TargetNodeRef);
			return Result;
		}

		UEdGraphPin* OtherPin = PatchHelpers::FindPinByName(OtherNode, TargetPinName);
		if (!OtherPin)
		{
			Result.bSuccess = false;
			Result.Message = FString::Printf(TEXT("Target pin '%s' not found"), *TargetPinName);
			return Result;
		}

		TargetPin->BreakLinkTo(OtherPin);
	}
	else
	{
		TargetPin->BreakAllPinLinks();
	}

	TargetNode->PinConnectionListChanged(TargetPin);

	Result.bSuccess = true;
	Result.Message = TEXT("Disconnected");
	return Result;
}

FPatchOpResult FApplyPatchAction::ExecuteAddVariable(const FPatchOp& Op, UBlueprint* Blueprint) const
{
	FPatchOpResult Result;
	Result.OpIndex = Op.OpIndex;
	Result.OpType = TEXT("add_variable");

	FString VarName = Op.Params->GetStringField(TEXT("name"));
	FString VarType = Op.Params->GetStringField(TEXT("type"));

	// Resolve type to FEdGraphPinType
	FEdGraphPinType PinType;
	FString TypeResolveError;
	if (!FMCPCommonUtils::ResolvePinTypeFromString(VarType, PinType, TypeResolveError))
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("Variable '%s': %s"), *VarName, *TypeResolveError);
		return Result;
	}

	FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*VarName), PinType);

	// Set optional properties
	bool bInstanceEditable = false;
	if (Op.Params->TryGetBoolField(TEXT("is_instance_editable"), bInstanceEditable) && bInstanceEditable)
	{
		for (FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			if (Var.VarName == FName(*VarName))
			{
				Var.PropertyFlags |= CPF_Edit;
				break;
			}
		}
	}

	FString Category;
	if (Op.Params->TryGetStringField(TEXT("category"), Category) && !Category.IsEmpty())
	{
		for (FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			if (Var.VarName == FName(*VarName))
			{
				Var.Category = FText::FromString(Category);
				break;
			}
		}
	}

	FString DefaultValue;
	if (Op.Params->TryGetStringField(TEXT("default_value"), DefaultValue))
	{
		for (FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			if (Var.VarName == FName(*VarName))
			{
				Var.DefaultValue = DefaultValue;
				break;
			}
		}
	}

	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Variable '%s' added"), *VarName);
	return Result;
}

FPatchOpResult FApplyPatchAction::ExecuteSetVariableDefault(const FPatchOp& Op, UBlueprint* Blueprint) const
{
	FPatchOpResult Result;
	Result.OpIndex = Op.OpIndex;
	Result.OpType = TEXT("set_variable_default");

	FString VarName = Op.Params->GetStringField(TEXT("name"));
	FString DefaultValue;
	Op.Params->TryGetStringField(TEXT("value"), DefaultValue);

	bool bFound = false;
	for (FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == FName(*VarName))
		{
			Var.DefaultValue = DefaultValue;
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("Variable '%s' not found"), *VarName);
		return Result;
	}

	Result.bSuccess = true;
	Result.Message = TEXT("Default value set");
	return Result;
}

FPatchOpResult FApplyPatchAction::ExecuteSetPinDefault(const FPatchOp& Op, UEdGraph* Graph,
	const TMap<FString, FGuid>& TempIdMap) const
{
	FPatchOpResult Result;
	Result.OpIndex = Op.OpIndex;
	Result.OpType = TEXT("set_pin_default");

	FString NodeRef = Op.Params->GetStringField(TEXT("node"));
	FString PinName = Op.Params->GetStringField(TEXT("pin"));
	FString DefaultValue;
	Op.Params->TryGetStringField(TEXT("value"), DefaultValue);

	UEdGraphNode* TargetNode = ResolveNodeRef(NodeRef, Graph, TempIdMap);
	if (!TargetNode)
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("Node '%s' not found"), *NodeRef);
		return Result;
	}

	UEdGraphPin* TargetPin = PatchHelpers::FindPinByName(TargetNode, PinName, EGPD_Input);
	if (!TargetPin)
	{
		Result.bSuccess = false;
		Result.Message = FString::Printf(TEXT("Pin '%s' not found on node"), *PinName);
		return Result;
	}

	// Handle object/class type pins
	const FName& Category = TargetPin->PinType.PinCategory;
	if (Category == UEdGraphSchema_K2::PC_Object ||
		Category == UEdGraphSchema_K2::PC_Class ||
		Category == UEdGraphSchema_K2::PC_SoftObject ||
		Category == UEdGraphSchema_K2::PC_SoftClass)
	{
		if (!DefaultValue.IsEmpty())
		{
			UObject* Obj = StaticLoadObject(UObject::StaticClass(), nullptr, *DefaultValue);
			if (Obj)
			{
				TargetPin->DefaultObject = Obj;
				TargetPin->DefaultValue.Empty();
			}
			else
			{
				// Fall back to string
				TargetPin->DefaultValue = DefaultValue;
			}
		}
		else
		{
			TargetPin->DefaultObject = nullptr;
			TargetPin->DefaultValue.Empty();
		}
	}
	else
	{
		TargetPin->DefaultValue = DefaultValue;
	}

	Result.bSuccess = true;
	Result.Message = TEXT("Pin default set");
	return Result;
}

TSharedPtr<FJsonObject> FApplyPatchAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);
	if (!Blueprint || !TargetGraph)
	{
		return CreateErrorResponse(TEXT("Blueprint or graph not found"));
	}

	const bool bContinueOnError = GetOptionalBool(Params, TEXT("continue_on_error"), false);

	// Parse ops
	const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
	Params->TryGetArrayField(TEXT("ops"), OpsArray);

	TArray<FPatchOp> Ops;
	FString ParseError;
	if (!ParseOps(*OpsArray, Ops, ParseError))
	{
		return CreateErrorResponse(ParseError);
	}

	// Phase A: Validate all ops
	TMap<FString, FGuid> ValidationTempIdMap;
	TArray<FPatchOpResult> ValidationResults;
	bool bAllValid = true;

	for (const FPatchOp& Op : Ops)
	{
		FPatchOpResult VResult = ValidateOp(Op, Blueprint, TargetGraph, ValidationTempIdMap);
		if (!VResult.bSuccess)
		{
			bAllValid = false;
			if (!bContinueOnError)
			{
				// Return early with validation failure
				TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
				ResultData->SetNumberField(TEXT("total"), Ops.Num());
				ResultData->SetNumberField(TEXT("failed_at"), Op.OpIndex);
				ResultData->SetStringField(TEXT("phase"), TEXT("validation"));

				TArray<TSharedPtr<FJsonValue>> ResultsJson;
				ValidationResults.Add(VResult);
				for (const FPatchOpResult& R : ValidationResults)
				{
					ResultsJson.Add(MakeShared<FJsonValueObject>(R.ToJson()));
				}
				ResultData->SetArrayField(TEXT("results"), ResultsJson);

				return CreateErrorResponse(
					FString::Printf(TEXT("Validation failed at op[%d]: %s"), Op.OpIndex, *VResult.Message));
			}
		}
		ValidationResults.Add(VResult);

		// Track temp IDs for validation of subsequent ops
		if (Op.OpType == EPatchOpType::AddNode)
		{
			FString TempId;
			if (Op.Params->TryGetStringField(TEXT("id"), TempId))
			{
				ValidationTempIdMap.Add(TempId, FGuid::NewGuid()); // placeholder GUID for validation
			}
		}
	}

	// Phase B: Execute ops
	TMap<FString, FGuid> TempIdMap;
	TArray<FPatchOpResult> ExecResults;
	int32 Succeeded = 0;
	int32 Failed = 0;

	for (const FPatchOp& Op : Ops)
	{
		FPatchOpResult ExecResult = ExecuteOp(Op, Blueprint, TargetGraph, TempIdMap, Context);
		ExecResults.Add(ExecResult);

		if (ExecResult.bSuccess)
		{
			Succeeded++;
		}
		else
		{
			Failed++;
			UE_LOG(LogMCP, Warning, TEXT("Patch op[%d] (%s) failed: %s"),
				Op.OpIndex, *FPatchOp::OpTypeToString(Op.OpType), *ExecResult.Message);

			if (!bContinueOnError)
			{
				break;
			}
		}
	}

	// Phase C: Post-execute
	MarkBlueprintModified(Blueprint, Context);

	FString CompileError;
	bool bCompileOK = CompileBlueprint(Blueprint, CompileError);

	// Build response
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetNumberField(TEXT("total"), Ops.Num());
	ResultData->SetNumberField(TEXT("executed"), ExecResults.Num());
	ResultData->SetNumberField(TEXT("succeeded"), Succeeded);
	ResultData->SetNumberField(TEXT("failed"), Failed);
	ResultData->SetBoolField(TEXT("compiled"), bCompileOK);
	if (!bCompileOK)
	{
		ResultData->SetStringField(TEXT("compile_error"), CompileError);
	}

	TArray<TSharedPtr<FJsonValue>> ResultsJson;
	for (const FPatchOpResult& R : ExecResults)
	{
		ResultsJson.Add(MakeShared<FJsonValueObject>(R.ToJson()));
	}
	ResultData->SetArrayField(TEXT("results"), ResultsJson);

	if (Failed > 0)
	{
		// Build a proper error response with detailed results attached.
		// Previously used CreateSuccessResponse(ResultData) with "success"=false inside
		// ResultData, but CreateSuccessResponse merges fields and the inner "success"=false
		// overwrites the outer "success"=true, resulting in no "error" field → Python shows
		// "Unknown error (no error message in response)".
		FString ErrorSummary;
		// Collect first failure message for the summary
		for (const FPatchOpResult& R : ExecResults)
		{
			if (!R.bSuccess)
			{
				ErrorSummary = FString::Printf(
					TEXT("Patch partially failed: %d/%d ops failed. First failure at op[%d]: %s"),
					Failed, Ops.Num(), R.OpIndex, *R.Message);
				break;
			}
		}
		TSharedPtr<FJsonObject> ErrorResponse = CreateErrorResponse(ErrorSummary, TEXT("partial_failure"));
		// Attach detailed results so callers can inspect per-op outcomes
		ErrorResponse->SetNumberField(TEXT("total"), Ops.Num());
		ErrorResponse->SetNumberField(TEXT("executed"), ExecResults.Num());
		ErrorResponse->SetNumberField(TEXT("succeeded"), Succeeded);
		ErrorResponse->SetNumberField(TEXT("failed"), Failed);
		ErrorResponse->SetBoolField(TEXT("compiled"), bCompileOK);
		if (!bCompileOK)
		{
			ErrorResponse->SetStringField(TEXT("compile_error"), CompileError);
		}
		ErrorResponse->SetArrayField(TEXT("results"), ResultsJson);
		return ErrorResponse;
	}

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// P3.4 — FValidatePatchAction
// ============================================================================

bool FValidatePatchAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateGraph(Params, Context, OutError))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("ops"), OpsArray) || !OpsArray || OpsArray->Num() == 0)
	{
		OutError = TEXT("'ops' array is required and must not be empty");
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FValidatePatchAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);
	if (!Blueprint || !TargetGraph)
	{
		return CreateErrorResponse(TEXT("Blueprint or graph not found"));
	}

	const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
	Params->TryGetArrayField(TEXT("ops"), OpsArray);

	TArray<FPatchOp> Ops;
	FString ParseError;
	if (!FApplyPatchAction::ParseOps(*OpsArray, Ops, ParseError))
	{
		return CreateErrorResponse(ParseError);
	}

	// Validate all ops without executing — dry run
	TMap<FString, FGuid> TempIdMap;
	TArray<TSharedPtr<FJsonValue>> ResultsJson;
	int32 ValidCount = 0;
	int32 ErrorCount = 0;

	// Create a temporary instance of FApplyPatchAction to reuse validation
	FApplyPatchAction PatchAction;

	for (const FPatchOp& Op : Ops)
	{
		FPatchOpResult VResult = PatchAction.ValidateOp(Op, Blueprint, TargetGraph, TempIdMap);
		ResultsJson.Add(MakeShared<FJsonValueObject>(VResult.ToJson()));

		if (VResult.bSuccess)
		{
			ValidCount++;
		}
		else
		{
			ErrorCount++;
		}

		// Track temp IDs for subsequent op validation
		if (Op.OpType == EPatchOpType::AddNode)
		{
			FString TempId;
			if (Op.Params->TryGetStringField(TEXT("id"), TempId))
			{
				TempIdMap.Add(TempId, FGuid::NewGuid());
			}
		}
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetNumberField(TEXT("total"), Ops.Num());
	ResultData->SetNumberField(TEXT("valid"), ValidCount);
	ResultData->SetNumberField(TEXT("errors"), ErrorCount);
	ResultData->SetBoolField(TEXT("all_valid"), ErrorCount == 0);
	ResultData->SetArrayField(TEXT("results"), ResultsJson);

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// P4 — Cross-Graph Node Transfer
// ============================================================================

// --- export_nodes_to_text ---

bool FExportNodesToTextAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// node_ids is required (array of GUID strings)
	const TArray<TSharedPtr<FJsonValue>>* NodeIdsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("node_ids"), NodeIdsArr) || !NodeIdsArr || NodeIdsArr->Num() == 0)
	{
		OutError = TEXT("'node_ids' (non-empty array of node GUID strings) is required");
		return false;
	}
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FExportNodesToTextAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);
	if (!TargetGraph)
	{
		return CreateErrorResponse(TEXT("Target graph not found"));
	}

	const TArray<TSharedPtr<FJsonValue>>& NodeIdsArr = Params->GetArrayField(TEXT("node_ids"));

	// Resolve node GUIDs → UEdGraphNode*
	TSet<UObject*> NodesToExport;
	TArray<FString> NotFoundIds;
	TArray<FString> ExportedIds;

	for (const TSharedPtr<FJsonValue>& IdVal : NodeIdsArr)
	{
		FString NodeIdStr;
		if (!IdVal->TryGetString(NodeIdStr))
		{
			continue;
		}

		FGuid NodeGuid;
		if (!FGuid::Parse(NodeIdStr, NodeGuid))
		{
			NotFoundIds.Add(NodeIdStr);
			continue;
		}

		bool bFound = false;
		for (UEdGraphNode* Node : TargetGraph->Nodes)
		{
			if (Node && Node->NodeGuid == NodeGuid)
			{
				NodesToExport.Add(Node);
				ExportedIds.Add(NodeIdStr);
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			NotFoundIds.Add(NodeIdStr);
		}
	}

	if (NodesToExport.Num() == 0)
	{
		return CreateErrorResponse(TEXT("No valid nodes found for the given node_ids"));
	}

	// Export using engine API
	FString ExportedText;
	FEdGraphUtilities::ExportNodesToText(NodesToExport, ExportedText);

	if (ExportedText.IsEmpty())
	{
		return CreateErrorResponse(TEXT("ExportNodesToText returned empty text"));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("exported_text"), ExportedText);
	ResultData->SetNumberField(TEXT("node_count"), NodesToExport.Num());

	TArray<TSharedPtr<FJsonValue>> ExportedArr;
	for (const FString& Id : ExportedIds)
	{
		ExportedArr.Add(MakeShared<FJsonValueString>(Id));
	}
	ResultData->SetArrayField(TEXT("exported_node_ids"), ExportedArr);

	if (NotFoundIds.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> MissingArr;
		for (const FString& Id : NotFoundIds)
		{
			MissingArr.Add(MakeShared<FJsonValueString>(Id));
		}
		ResultData->SetArrayField(TEXT("not_found_ids"), MissingArr);
	}

	return CreateSuccessResponse(ResultData);
}


// --- import_nodes_from_text ---

bool FImportNodesFromTextAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ExportedText;
	if (!GetRequiredString(Params, TEXT("exported_text"), ExportedText, OutError)) return false;
	return ValidateGraph(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FImportNodesFromTextAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ExportedText = Params->GetStringField(TEXT("exported_text"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UEdGraph* TargetGraph = GetTargetGraph(Params, Context);
	if (!TargetGraph)
	{
		return CreateErrorResponse(TEXT("Target graph not found"));
	}

	// Pre-check: can the text be imported into this graph?
	if (!FEdGraphUtilities::CanImportNodesFromText(TargetGraph, ExportedText))
	{
		return CreateErrorResponse(TEXT("Cannot import the given node text into this graph. The text may be invalid or incompatible with the target graph schema."));
	}

	// Import nodes
	TSet<UEdGraphNode*> ImportedNodes;
	FEdGraphUtilities::ImportNodesFromText(TargetGraph, ExportedText, ImportedNodes);

	if (ImportedNodes.Num() == 0)
	{
		return CreateErrorResponse(TEXT("ImportNodesFromText succeeded but produced zero nodes"));
	}

	// Post-process pasted nodes (fixup pin references etc.)
	FEdGraphUtilities::PostProcessPastedNodes(ImportedNodes);

	// Optional: apply position offset so pasted nodes don't overlap source positions
	double OffsetX = 0.0, OffsetY = 0.0;
	Params->TryGetNumberField(TEXT("offset_x"), OffsetX);
	Params->TryGetNumberField(TEXT("offset_y"), OffsetY);
	if (OffsetX != 0.0 || OffsetY != 0.0)
	{
		for (UEdGraphNode* Node : ImportedNodes)
		{
			if (Node)
			{
				Node->NodePosX += FMath::RoundToInt(OffsetX);
				Node->NodePosY += FMath::RoundToInt(OffsetY);
			}
		}
	}

	MarkBlueprintModified(Blueprint, Context);

	// Build result
	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetNumberField(TEXT("imported_count"), ImportedNodes.Num());

	TArray<TSharedPtr<FJsonValue>> ImportedArr;
	for (UEdGraphNode* Node : ImportedNodes)
	{
		if (!Node) continue;

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
		NodeObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
		NodeObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
		NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);
		ImportedArr.Add(MakeShared<FJsonValueObject>(NodeObj));

		// Register last created node for $last_node support
		Context.LastCreatedNodeId = Node->NodeGuid;
	}
	ResultData->SetArrayField(TEXT("imported_nodes"), ImportedArr);

	return CreateSuccessResponse(ResultData);
}
