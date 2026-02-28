// Copyright (c) 2025 zolnoor. All rights reserved.

#include "MCPCommonUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Self.h"
#include "K2Node_InputAction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "GameFramework/Actor.h"
#include "EditorAssetLibrary.h"

// =========================================================================
// JSON Parsing Utilities
// =========================================================================

FVector FMCPCommonUtils::GetVectorFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName)
{
	FVector Result(0.0f, 0.0f, 0.0f);

	if (!JsonObject->HasField(FieldName))
	{
		return Result;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (JsonObject->TryGetArrayField(FieldName, JsonArray) && JsonArray->Num() >= 3)
	{
		Result.X = (float)(*JsonArray)[0]->AsNumber();
		Result.Y = (float)(*JsonArray)[1]->AsNumber();
		Result.Z = (float)(*JsonArray)[2]->AsNumber();
	}

	return Result;
}

FRotator FMCPCommonUtils::GetRotatorFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName)
{
	FRotator Result(0.0f, 0.0f, 0.0f);

	if (!JsonObject->HasField(FieldName))
	{
		return Result;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (JsonObject->TryGetArrayField(FieldName, JsonArray) && JsonArray->Num() >= 3)
	{
		Result.Pitch = (float)(*JsonArray)[0]->AsNumber();
		Result.Yaw = (float)(*JsonArray)[1]->AsNumber();
		Result.Roll = (float)(*JsonArray)[2]->AsNumber();
	}

	return Result;
}

FVector2D FMCPCommonUtils::GetVector2DFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName)
{
	FVector2D Result(0.0f, 0.0f);

	if (!JsonObject->HasField(FieldName))
	{
		return Result;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (JsonObject->TryGetArrayField(FieldName, JsonArray) && JsonArray->Num() >= 2)
	{
		Result.X = (float)(*JsonArray)[0]->AsNumber();
		Result.Y = (float)(*JsonArray)[1]->AsNumber();
	}

	return Result;
}

// =========================================================================
// Blueprint Utilities
// =========================================================================

UBlueprint* FMCPCommonUtils::FindBlueprint(const FString& BlueprintName)
{
	if (BlueprintName.IsEmpty()) return nullptr;

	// Search asset registry for all blueprint types (including Widget Blueprints)
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.bRecursiveClasses = true;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssets(Filter, AssetList);

	for (const FAssetData& AssetData : AssetList)
	{
		if (AssetData.AssetName.ToString() == BlueprintName)
		{
			return Cast<UBlueprint>(AssetData.GetAsset());
		}
	}

	return nullptr;
}

UEdGraph* FMCPCommonUtils::FindOrCreateEventGraph(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	// Try to find the event graph
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph->GetName().Contains(TEXT("EventGraph")))
		{
			return Graph;
		}
	}

	// Create a new event graph if none exists
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, FName(TEXT("EventGraph")),
		UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddUbergraphPage(Blueprint, NewGraph);
	return NewGraph;
}

UEdGraph* FMCPCommonUtils::FindFunctionGraph(UBlueprint* Blueprint, const FString& FunctionName)
{
	if (!Blueprint || FunctionName.IsEmpty())
	{
		return nullptr;
	}

	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph->GetFName() == FName(*FunctionName))
		{
			return Graph;
		}
	}

	return nullptr;
}

UEdGraph* FMCPCommonUtils::FindGraphByName(UBlueprint* Blueprint, const FString& GraphName)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	// If no graph name specified, return the event graph (default behavior)
	if (GraphName.IsEmpty())
	{
		return FindOrCreateEventGraph(Blueprint);
	}

	// First check if it's a function graph
	UEdGraph* FunctionGraph = FindFunctionGraph(Blueprint, GraphName);
	if (FunctionGraph)
	{
		return FunctionGraph;
	}

	// Check ubergraph pages (event graphs can have different names)
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph->GetFName() == FName(*GraphName) || Graph->GetName() == GraphName)
		{
			return Graph;
		}
	}

	// Check macro graphs
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (Graph->GetFName() == FName(*GraphName) || Graph->GetName() == GraphName)
		{
			return Graph;
		}
	}

	return nullptr;
}

USCS_Node* FMCPCommonUtils::FindComponentNode(UBlueprint* Blueprint, const FString& ComponentName)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	// Traverse inheritance hierarchy to find component
	UBlueprint* SearchBP = Blueprint;
	while (SearchBP)
	{
		if (SearchBP->SimpleConstructionScript)
		{
			for (USCS_Node* Node : SearchBP->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && Node->GetVariableName().ToString() == ComponentName)
				{
					return Node;
				}
			}
		}

		// Walk up to parent Blueprint
		UClass* ParentClass = SearchBP->ParentClass;
		SearchBP = (ParentClass) ? Cast<UBlueprint>(ParentClass->ClassGeneratedBy) : nullptr;
	}

	return nullptr;
}

// =========================================================================
// Property Setting Utilities
// =========================================================================

bool FMCPCommonUtils::SetObjectProperty(UObject* Object, const FString& PropertyName,
	const TSharedPtr<FJsonValue>& Value, FString& OutErrorMessage)
{
	if (!Object)
	{
		OutErrorMessage = TEXT("Invalid object");
		return false;
	}

	FProperty* Property = Object->GetClass()->FindPropertyByName(*PropertyName);
	if (!Property)
	{
		OutErrorMessage = FString::Printf(TEXT("Property not found: %s"), *PropertyName);
		return false;
	}

	void* PropertyAddr = Property->ContainerPtrToValuePtr<void>(Object);

	// Handle different property types
	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		BoolProp->SetPropertyValue(PropertyAddr, Value->AsBool());
		return true;
	}
	else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
	{
		IntProp->SetPropertyValue_InContainer(Object, static_cast<int32>(Value->AsNumber()));
		return true;
	}
	else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
	{
		FloatProp->SetPropertyValue(PropertyAddr, Value->AsNumber());
		return true;
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
	{
		DoubleProp->SetPropertyValue(PropertyAddr, Value->AsNumber());
		return true;
	}
	else if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		StrProp->SetPropertyValue(PropertyAddr, Value->AsString());
		return true;
	}
	else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		// FByteProperty can be a raw byte OR a TEnumAsByte<> (which has an associated UEnum)
		UEnum* EnumDef = ByteProp->Enum;
		if (EnumDef)
		{
			// Enum-backed byte property (e.g., TEnumAsByte<EComponentMobility::Type>)
			if (Value->Type == EJson::Number)
			{
				ByteProp->SetPropertyValue(PropertyAddr, static_cast<uint8>(Value->AsNumber()));
				return true;
			}
			else if (Value->Type == EJson::String)
			{
				FString EnumValueName = Value->AsString();
				// Strip optional "EnumName::" prefix
				if (EnumValueName.Contains(TEXT("::")))
				{
					EnumValueName.Split(TEXT("::"), nullptr, &EnumValueName);
				}

				int64 EnumValue = EnumDef->GetValueByNameString(EnumValueName);
				if (EnumValue != INDEX_NONE)
				{
					ByteProp->SetPropertyValue(PropertyAddr, static_cast<uint8>(EnumValue));
					return true;
				}
				OutErrorMessage = FString::Printf(TEXT("Invalid enum value '%s' for enum %s"), *EnumValueName, *EnumDef->GetName());
				return false;
			}
		}
		else
		{
			// Raw byte property
			ByteProp->SetPropertyValue(PropertyAddr, static_cast<uint8>(Value->AsNumber()));
			return true;
		}
	}
	else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		UEnum* EnumDef = EnumProp->GetEnum();
		FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();

		if (EnumDef && UnderlyingProp)
		{
			if (Value->Type == EJson::Number)
			{
				UnderlyingProp->SetIntPropertyValue(PropertyAddr, static_cast<int64>(Value->AsNumber()));
				return true;
			}
			else if (Value->Type == EJson::String)
			{
				FString EnumValueName = Value->AsString();
				if (EnumValueName.Contains(TEXT("::")))
				{
					EnumValueName.Split(TEXT("::"), nullptr, &EnumValueName);
				}

				int64 EnumValue = EnumDef->GetValueByNameString(EnumValueName);
				if (EnumValue != INDEX_NONE)
				{
					UnderlyingProp->SetIntPropertyValue(PropertyAddr, EnumValue);
					return true;
				}
				OutErrorMessage = FString::Printf(TEXT("Invalid enum value: %s"), *EnumValueName);
				return false;
			}
		}
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		if (StructProp->Struct == TBaseStructure<FVector>::Get())
		{
			if (Value->Type == EJson::Array)
			{
				TArray<TSharedPtr<FJsonValue>> Arr = Value->AsArray();
				if (Arr.Num() >= 3)
				{
					FVector* VecPtr = (FVector*)PropertyAddr;
					VecPtr->X = Arr[0]->AsNumber();
					VecPtr->Y = Arr[1]->AsNumber();
					VecPtr->Z = Arr[2]->AsNumber();
					return true;
				}
			}
		}
		else if (StructProp->Struct == TBaseStructure<FRotator>::Get())
		{
			if (Value->Type == EJson::Array)
			{
				TArray<TSharedPtr<FJsonValue>> Arr = Value->AsArray();
				if (Arr.Num() >= 3)
				{
					FRotator* RotPtr = (FRotator*)PropertyAddr;
					RotPtr->Pitch = Arr[0]->AsNumber();
					RotPtr->Yaw = Arr[1]->AsNumber();
					RotPtr->Roll = Arr[2]->AsNumber();
					return true;
				}
			}
		}
	}
	else if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
	{
		FString ClassPath = Value->AsString();
		UClass* LoadedClass = LoadObject<UClass>(nullptr, *ClassPath);

		if (!LoadedClass)
		{
			// Try constructing path from Blueprint name
			FString BlueprintPath = FString::Printf(TEXT("/Game/Blueprints/%s.%s_C"), *ClassPath, *ClassPath);
			LoadedClass = LoadObject<UClass>(nullptr, *BlueprintPath);
		}

		if (LoadedClass)
		{
			ClassProp->SetPropertyValue(PropertyAddr, LoadedClass);
			return true;
		}

		OutErrorMessage = FString::Printf(TEXT("Could not load class: %s"), *ClassPath);
		return false;
	}

	OutErrorMessage = FString::Printf(TEXT("Unsupported property type for: %s"), *PropertyName);
	return false;
}

// =========================================================================
// Graph Node Utilities
// =========================================================================

UEdGraphPin* FMCPCommonUtils::FindPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return nullptr;
	}

	// Exact match first
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin->PinName.ToString() == PinName && (Direction == EGPD_MAX || Pin->Direction == Direction))
		{
			return Pin;
		}
	}

	// Case-insensitive match
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase) &&
			(Direction == EGPD_MAX || Pin->Direction == Direction))
		{
			return Pin;
		}
	}

	return nullptr;
}

UK2Node_Event* FMCPCommonUtils::FindExistingEventNode(UEdGraph* Graph, const FString& EventName)
{
	if (!Graph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
		if (EventNode && EventNode->EventReference.GetMemberName() == FName(*EventName))
		{
			return EventNode;
		}
	}

	return nullptr;
}

UK2Node_Event* FMCPCommonUtils::CreateEventNode(UEdGraph* Graph, const FString& EventName, FVector2D Position)
{
	if (!Graph)
	{
		return nullptr;
	}

	// Get the blueprint for context
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (!Blueprint)
	{
		return nullptr;
	}

	// Create the event node
	UK2Node_Event* EventNode = NewObject<UK2Node_Event>(Graph);
	if (!EventNode)
	{
		return nullptr;
	}

	// Find the event function in AActor or parent class
	UClass* OwnerClass = Blueprint->GeneratedClass ? Blueprint->GeneratedClass : Blueprint->ParentClass;
	if (OwnerClass)
	{
		UFunction* EventFunc = OwnerClass->FindFunctionByName(FName(*EventName), EIncludeSuperFlag::IncludeSuper);
		if (EventFunc)
		{
			EventNode->EventReference.SetFromField<UFunction>(EventFunc, false);
		}
		else
		{
			// Try with Receive prefix for implementable events
			FString ReceiveEventName = TEXT("Receive") + EventName.Replace(TEXT("Receive"), TEXT(""));
			EventFunc = OwnerClass->FindFunctionByName(FName(*ReceiveEventName), EIncludeSuperFlag::IncludeSuper);
			if (EventFunc)
			{
				EventNode->EventReference.SetFromField<UFunction>(EventFunc, false);
			}
		}
	}

	EventNode->NodePosX = Position.X;
	EventNode->NodePosY = Position.Y;

	Graph->AddNode(EventNode);
	EventNode->CreateNewGuid();
	EventNode->PostPlacedNewNode();
	EventNode->AllocateDefaultPins();

	return EventNode;
}

UK2Node_InputAction* FMCPCommonUtils::CreateInputActionNode(UEdGraph* Graph, const FString& ActionName, FVector2D Position)
{
	if (!Graph)
	{
		return nullptr;
	}

	UK2Node_InputAction* InputActionNode = NewObject<UK2Node_InputAction>(Graph);
	if (!InputActionNode)
	{
		return nullptr;
	}

	InputActionNode->InputActionName = FName(*ActionName);
	InputActionNode->NodePosX = Position.X;
	InputActionNode->NodePosY = Position.Y;

	Graph->AddNode(InputActionNode);
	InputActionNode->CreateNewGuid();
	InputActionNode->PostPlacedNewNode();
	InputActionNode->AllocateDefaultPins();

	return InputActionNode;
}

UK2Node_CallFunction* FMCPCommonUtils::CreateFunctionCallNode(UEdGraph* Graph, UFunction* Function, FVector2D Position)
{
	if (!Graph || !Function)
	{
		return nullptr;
	}

	// Use SpawnNode to ensure proper initialization order.
	// This fixes WorldContext auto-binding for GameplayStatics functions
	// (e.g. GetPlayerCharacter) where the hidden WorldContextObject pin
	// needs the node to be fully registered in the graph before pin allocation.
	UK2Node_CallFunction* FunctionNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CallFunction>(
		Graph, Position, EK2NewNodeFlags::None,
		[Function](UK2Node_CallFunction* Node)
		{
			Node->SetFromFunction(Function);
		}
	);

	return FunctionNode;
}

UK2Node_Self* FMCPCommonUtils::CreateSelfReferenceNode(UEdGraph* Graph, FVector2D Position)
{
	if (!Graph)
	{
		return nullptr;
	}

	UK2Node_Self* SelfNode = NewObject<UK2Node_Self>(Graph);
	if (!SelfNode)
	{
		return nullptr;
	}

	SelfNode->NodePosX = Position.X;
	SelfNode->NodePosY = Position.Y;

	Graph->AddNode(SelfNode);
	SelfNode->CreateNewGuid();
	SelfNode->PostPlacedNewNode();
	SelfNode->AllocateDefaultPins();

	return SelfNode;
}

TSharedPtr<FJsonObject> FMCPCommonUtils::CreateErrorResponse(const FString& ErrorMessage)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetBoolField(TEXT("success"), false);
	Response->SetStringField(TEXT("error"), ErrorMessage);
	return Response;
}

// =========================================================================
// Actor Utilities
// =========================================================================

TSharedPtr<FJsonObject> FMCPCommonUtils::ActorToJsonObject(AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> ActorObject = MakeShared<FJsonObject>();
	ActorObject->SetStringField(TEXT("name"), Actor->GetName());
	ActorObject->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

	FVector Location = Actor->GetActorLocation();
	TArray<TSharedPtr<FJsonValue>> LocationArray;
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.X));
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Y));
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Z));
	ActorObject->SetArrayField(TEXT("location"), LocationArray);

	FRotator Rotation = Actor->GetActorRotation();
	TArray<TSharedPtr<FJsonValue>> RotationArray;
	RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Pitch));
	RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Yaw));
	RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Roll));
	ActorObject->SetArrayField(TEXT("rotation"), RotationArray);

	FVector Scale = Actor->GetActorScale3D();
	TArray<TSharedPtr<FJsonValue>> ScaleArray;
	ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.X));
	ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Y));
	ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Z));
	ActorObject->SetArrayField(TEXT("scale"), ScaleArray);

	return ActorObject;
}

TSharedPtr<FJsonValue> FMCPCommonUtils::ActorToJsonValue(AActor* Actor)
{
	TSharedPtr<FJsonObject> Obj = ActorToJsonObject(Actor);
	if (Obj.IsValid())
	{
		return MakeShared<FJsonValueObject>(Obj);
	}
	return MakeShared<FJsonValueNull>();
}

// =========================================================================
// Pin Type Resolution
// =========================================================================

bool FMCPCommonUtils::ResolvePinTypeFromString(const FString& TypeName, FEdGraphPinType& OutPinType, FString& OutError)
{
	OutPinType = FEdGraphPinType();

	// --- Primitives ---
	if (TypeName.Equals(TEXT("Float"), ESearchCase::IgnoreCase) ||
		TypeName.Equals(TEXT("Double"), ESearchCase::IgnoreCase) ||
		TypeName.Equals(TEXT("Real"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		return true;
	}
	if (TypeName.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase) ||
		TypeName.Equals(TEXT("Bool"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		return true;
	}
	if (TypeName.Equals(TEXT("Integer"), ESearchCase::IgnoreCase) ||
		TypeName.Equals(TEXT("Int"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		return true;
	}
	if (TypeName.Equals(TEXT("Int64"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
		return true;
	}
	if (TypeName.Equals(TEXT("Byte"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		return true;
	}
	if (TypeName.Equals(TEXT("String"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
		return true;
	}
	if (TypeName.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		return true;
	}
	if (TypeName.Equals(TEXT("Text"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
		return true;
	}

	// --- Common Structs ---
	if (TypeName.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
		return true;
	}
	if (TypeName.Equals(TEXT("Vector2D"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FVector2D>::Get();
		return true;
	}
	if (TypeName.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
		return true;
	}
	if (TypeName.Equals(TEXT("Transform"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
		return true;
	}
	if (TypeName.Equals(TEXT("LinearColor"), ESearchCase::IgnoreCase) ||
		TypeName.Equals(TEXT("Color"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
		return true;
	}
	if (TypeName.Equals(TEXT("IntPoint"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FIntPoint>::Get();
		return true;
	}

	// --- Delegate ---
	if (TypeName.Equals(TEXT("EventDispatcher"), ESearchCase::IgnoreCase) ||
		TypeName.Equals(TEXT("MulticastDelegate"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
		return true;
	}

	// --- Generic Object (no subclass) ---
	if (TypeName.Equals(TEXT("Object"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		OutPinType.PinSubCategoryObject = UObject::StaticClass();
		return true;
	}

	// --- Try to resolve as UObject subclass ---
	{
		FString ObjectClassName = TypeName;
		// Prepend 'U' if not already present for standard UObject classes
		FString FullClassName = ObjectClassName;
		if (!FullClassName.StartsWith(TEXT("U")) && !FullClassName.StartsWith(TEXT("A")))
		{
			FullClassName = TEXT("U") + FullClassName;
		}

		UClass* FoundClass = FindFirstObject<UClass>(*FullClassName, EFindFirstObjectOptions::ExactClass);
		if (!FoundClass)
		{
			// Try without prefix
			FoundClass = FindFirstObject<UClass>(*ObjectClassName, EFindFirstObjectOptions::ExactClass);
		}
		if (!FoundClass)
		{
			// Try loading by path if it looks like a path
			if (ObjectClassName.Contains(TEXT("/")))
			{
				FoundClass = LoadClass<UObject>(nullptr, *ObjectClassName);
			}
		}

		if (FoundClass)
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			OutPinType.PinSubCategoryObject = FoundClass;
			return true;
		}
	}

	// --- Try to resolve as UScriptStruct (user-defined structs, etc.) ---
	{
		UScriptStruct* FoundStruct = FindFirstObject<UScriptStruct>(*TypeName, EFindFirstObjectOptions::ExactClass);
		if (!FoundStruct)
		{
			FoundStruct = FindFirstObject<UScriptStruct>(*(FString(TEXT("F")) + TypeName), EFindFirstObjectOptions::ExactClass);
		}
		if (FoundStruct)
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = FoundStruct;
			return true;
		}
	}

	OutError = FString::Printf(
		TEXT("Unsupported type: '%s'. Supported: Boolean, Int, Int64, Float/Double, String, Name, Text, Byte, "
			 "Vector, Vector2D, Rotator, Transform, LinearColor, IntPoint, Object, "
			 "or any UObject/AActor subclass name (e.g. PlayerStatusModel, StaticMeshComponent)."),
		*TypeName);
	return false;
}

// =========================================================================
// Blueprint Editor Window Resolution
// =========================================================================

#include "BlueprintEditor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Docking/SDockTab.h"
#include "Editor.h"
#include "Actions/EditorAction.h" // LogMCP

FBlueprintEditor* FMCPCommonUtils::GetActiveBlueprintEditor(const FString& BlueprintName)
{
	if (!GEditor)
	{
		return nullptr;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return nullptr;
	}

	TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();

	// Collect unique candidate editors (multiple assets may share one editor)
	TArray<FBlueprintEditor*> Candidates;

	for (UObject* Asset : EditedAssets)
	{
		UBlueprint* BP = Cast<UBlueprint>(Asset);
		if (!BP)
		{
			continue;
		}

		// If a BlueprintName filter is given, match by name or path
		if (!BlueprintName.IsEmpty())
		{
			bool bMatch = BP->GetName() == BlueprintName
				|| BP->GetPathName().Contains(BlueprintName);
			if (!bMatch)
			{
				continue;
			}
		}

		IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(BP, false);
		if (!EditorInstance)
		{
			continue;
		}

		FBlueprintEditor* BPEditor = static_cast<FBlueprintEditor*>(EditorInstance);
		if (!BPEditor)
		{
			continue;
		}

		Candidates.AddUnique(BPEditor);
	}

	if (Candidates.Num() == 0)
	{
		return nullptr;
	}
	if (Candidates.Num() == 1)
	{
		return Candidates[0];
	}

	// --- Multiple candidates: layered heuristics ---

	// Strategy 1: IsActive() — backed by FGlobalTabmanager::GetActiveTab(),
	// set synchronously when a tab is activated (click / BringToFront).
	// Most reliable immediately after a tab switch, even before Slate ticks.
	for (FBlueprintEditor* BPEditor : Candidates)
	{
		TSharedPtr<SDockTab> OwnerTab = BPEditor->GetTabManager()->GetOwnerTab();
		if (OwnerTab.IsValid() && OwnerTab->IsActive())
		{
			return BPEditor;
		}
	}

	// Strategy 2: IsForeground() — dock-area foreground tab.
	// Updated during Slate tick; reliable in steady state.
	for (FBlueprintEditor* BPEditor : Candidates)
	{
		TSharedPtr<SDockTab> OwnerTab = BPEditor->GetTabManager()->GetOwnerTab();
		if (OwnerTab.IsValid() && OwnerTab->IsForeground())
		{
			return BPEditor;
		}
	}

	// Strategy 3: HasFocusedDescendants() — keyboard focus propagation.
	// Focus is transferred synchronously during tab activation.
	for (FBlueprintEditor* BPEditor : Candidates)
	{
		TSharedPtr<SDockTab> OwnerTab = BPEditor->GetTabManager()->GetOwnerTab();
		if (OwnerTab.IsValid() && OwnerTab->HasFocusedDescendants())
		{
			return BPEditor;
		}
	}

	// Fallback: first candidate (log for diagnostics)
	UE_LOG(LogMCP, Warning,
		TEXT("GetActiveBlueprintEditor: Could not determine active editor among %d candidates; using first found."),
		Candidates.Num());
	return Candidates[0];
}
