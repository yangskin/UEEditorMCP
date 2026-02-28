// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/EditorAction.h"
#include "MCPCommonUtils.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"

#if PLATFORM_WINDOWS && defined(_MSC_VER)
#include "Windows/WindowsHWrapper.h"
#endif

DEFINE_LOG_CATEGORY(LogMCP);

// ============================================================================
// FEditorAction Implementation
// ============================================================================

TSharedPtr<FJsonObject> FEditorAction::Execute(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Action '%s' Execute started"), *GetActionName());

	// Step 1: Pre-validation
	if (!Validate(Params, Context, Error))
	{
		UE_LOG(LogMCP, Warning, TEXT("UEEditorMCP: Action '%s' validation failed: %s"), *GetActionName(), *Error);
		return CreateErrorResponse(Error, TEXT("validation_failed"));
	}

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Action '%s' validation passed"), *GetActionName());

	// Step 2: Execute with crash protection
	TSharedPtr<FJsonObject> Result = ExecuteWithCrashProtection(Params, Context);
	if (!Result)
	{
		UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Action '%s' returned nullptr!"), *GetActionName());
		return CreateCrashPreventedResponse();
	}

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Action '%s' ExecuteInternal returned (has success=%s, has error=%s)"),
		*GetActionName(),
		Result->HasField(TEXT("success")) ? TEXT("yes") : TEXT("no"),
		Result->HasField(TEXT("error")) ? TEXT("yes") : TEXT("no"));

	// Step 3: Post-validation
	if (!PostValidate(Context, Error))
	{
		UE_LOG(LogMCP, Warning, TEXT("UEEditorMCP: Action '%s' post-validation failed: %s"), *GetActionName(), *Error);
		return CreateErrorResponse(Error, TEXT("post_validation_failed"));
	}

	// Step 4: Auto-save on success
	if (RequiresSave() && Result->HasField(TEXT("success")))
	{
		bool bSuccess = false;
		if (Result->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess)
		{
			Context.SaveDirtyPackages();
		}
	}

	return Result;
}

#if PLATFORM_WINDOWS && defined(_MSC_VER)
// -- SEH crash protection infrastructure --
// MSVC C2712: __try cannot be in a function with objects requiring unwinding.
// Solution: the __try lives in a function with ZERO C++ objects. It calls a
// callback (which MAY use C++ objects) — that's allowed because the callback
// is a separate compilation unit function.

struct FSEHCallContext
{
	FEditorAction* Action;
	const TSharedPtr<FJsonObject>* Params;
	FMCPEditorContext* Context;
	TSharedPtr<FJsonObject>* OutResult;
};

static void SEH_InvokeInternal(void* RawCtx)
{
	FSEHCallContext* Ctx = static_cast<FSEHCallContext*>(RawCtx);
	*Ctx->OutResult = Ctx->Action->ExecuteInternal(*Ctx->Params, *Ctx->Context);
}

// This function has NO C++ objects with destructors — only POD/pointers.
#pragma warning(push)
#pragma warning(disable: 4611) // interaction between _setjmp and C++ destruction
static DWORD SEH_TryCall(void (*Func)(void*), void* UserData)
{
	__try
	{
		Func(UserData);
		return 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return GetExceptionCode();
	}
}
#pragma warning(pop)
#endif

TSharedPtr<FJsonObject> FEditorAction::ExecuteWithCrashProtection(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
#if PLATFORM_WINDOWS && defined(_MSC_VER)
	TSharedPtr<FJsonObject> OutResult;
	FSEHCallContext CallCtx = { this, &Params, &Context, &OutResult };

	DWORD ExCode = SEH_TryCall(&SEH_InvokeInternal, &CallCtx);
	if (ExCode != 0)
	{
		UE_LOG(LogMCP, Error, TEXT("SEH exception 0x%08X in action '%s'"), ExCode, *GetActionName());
		return CreateCrashPreventedResponse();
	}
	return OutResult;
#else
	return ExecuteInternal(Params, Context);
#endif
}

TSharedPtr<FJsonObject> FEditorAction::CreateSuccessResponse(const TSharedPtr<FJsonObject>& ResultData) const
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetBoolField(TEXT("success"), true);

	if (ResultData.IsValid())
	{
		// Merge result data into response
		for (const auto& Field : ResultData->Values)
		{
			Response->SetField(Field.Key, Field.Value);
		}
	}

	return Response;
}

TSharedPtr<FJsonObject> FEditorAction::CreateErrorResponse(const FString& ErrorMessage, const FString& ErrorType) const
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetBoolField(TEXT("success"), false);
	Response->SetStringField(TEXT("error"), ErrorMessage);
	Response->SetStringField(TEXT("error_type"), ErrorType);

	return Response;
}

TSharedPtr<FJsonObject> FEditorAction::CreateCrashPreventedResponse() const
{
	return CreateErrorResponse(
		FString::Printf(TEXT("CRASH PREVENTED: Access violation in '%s'. Operation aborted safely."), *GetActionName()),
		TEXT("crash_prevented")
	);
}

bool FEditorAction::GetRequiredString(const TSharedPtr<FJsonObject>& Params, const FString& ParamName, FString& OutValue, FString& OutError) const
{
	if (!Params.IsValid() || !Params->TryGetStringField(ParamName, OutValue) || OutValue.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Required parameter '%s' is missing or empty"), *ParamName);
		return false;
	}
	return true;
}

FString FEditorAction::GetOptionalString(const TSharedPtr<FJsonObject>& Params, const FString& ParamName, const FString& Default) const
{
	FString Value;
	if (Params.IsValid() && Params->TryGetStringField(ParamName, Value) && !Value.IsEmpty())
	{
		return Value;
	}
	return Default;
}

const TArray<TSharedPtr<FJsonValue>>* FEditorAction::GetOptionalArray(const TSharedPtr<FJsonObject>& Params, const FString& ParamName) const
{
	if (Params.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* OutArray = nullptr;
		if (Params->TryGetArrayField(ParamName, OutArray))
		{
			return OutArray;
		}
	}
	return nullptr;
}

double FEditorAction::GetOptionalNumber(const TSharedPtr<FJsonObject>& Params, const FString& ParamName, double Default) const
{
	double Value = Default;
	if (Params.IsValid() && Params->TryGetNumberField(ParamName, Value))
	{
		return Value;
	}
	return Default;
}

bool FEditorAction::GetOptionalBool(const TSharedPtr<FJsonObject>& Params, const FString& ParamName, bool Default) const
{
	bool Value = Default;
	if (Params.IsValid() && Params->TryGetBoolField(ParamName, Value))
	{
		return Value;
	}
	return Default;
}

UBlueprint* FEditorAction::FindBlueprint(const FString& BlueprintName, FString& OutError) const
{
	if (BlueprintName.IsEmpty())
	{
		OutError = TEXT("Blueprint name is empty");
		return nullptr;
	}

	UBlueprint* BP = FMCPCommonUtils::FindBlueprint(BlueprintName);
	if (!BP)
	{
		OutError = FString::Printf(TEXT("Blueprint '%s' not found"), *BlueprintName);
	}
	return BP;
}

UEdGraph* FEditorAction::FindGraph(UBlueprint* Blueprint, const FString& GraphName, FString& OutError) const
{
	if (!Blueprint)
	{
		OutError = TEXT("Blueprint is null");
		return nullptr;
	}

	// If no graph specified, return event graph
	if (GraphName.IsEmpty())
	{
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (Graph && Graph->GetFName() == TEXT("EventGraph"))
			{
				return Graph;
			}
		}
		// Fallback
		if (Blueprint->UbergraphPages.Num() > 0)
		{
			return Blueprint->UbergraphPages[0];
		}
	}

	// Search function graphs
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName().ToString() == GraphName)
		{
			return Graph;
		}
	}

	// Search ubergraph pages
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph && Graph->GetFName().ToString() == GraphName)
		{
			return Graph;
		}
	}

	// Search macro graphs
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (Graph && Graph->GetFName().ToString() == GraphName)
		{
			return Graph;
		}
	}

	OutError = FString::Printf(TEXT("Graph '%s' not found in Blueprint '%s'"), *GraphName, *Blueprint->GetName());
	return nullptr;
}

UEdGraphNode* FEditorAction::FindNode(UEdGraph* Graph, const FGuid& NodeId, FString& OutError) const
{
	if (!Graph)
	{
		OutError = TEXT("Graph is null");
		return nullptr;
	}

	if (!NodeId.IsValid())
	{
		OutError = TEXT("Node ID is invalid");
		return nullptr;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->NodeGuid == NodeId)
		{
			return Node;
		}
	}

	OutError = FString::Printf(TEXT("Node with ID '%s' not found"), *NodeId.ToString());
	return nullptr;
}

// ============================================================================
// FBlueprintAction Implementation
// ============================================================================

bool FBlueprintAction::ValidateBlueprint(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString BlueprintName = GetOptionalString(Params, TEXT("blueprint_name"));
	UBlueprint* BP = Context.GetBlueprintByNameOrCurrent(BlueprintName);

	if (!BP)
	{
		OutError = BlueprintName.IsEmpty()
			? TEXT("No current Blueprint set and no blueprint_name provided")
			: FString::Printf(TEXT("Blueprint '%s' not found"), *BlueprintName);
		return false;
	}

	if (!IsValid(BP))
	{
		OutError = FString::Printf(TEXT("Blueprint '%s' is invalid"), *BlueprintName);
		return false;
	}

	return true;
}

UBlueprint* FBlueprintAction::GetTargetBlueprint(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) const
{
	FString BlueprintName = GetOptionalString(Params, TEXT("blueprint_name"));
	return Context.GetBlueprintByNameOrCurrent(BlueprintName);
}

void FBlueprintAction::MarkBlueprintModified(UBlueprint* Blueprint, FMCPEditorContext& Context) const
{
	if (Blueprint)
	{
		// Notify ALL graphs so the Blueprint Editor UI refreshes connection wires, etc.
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (Graph)
			{
				Graph->NotifyGraphChanged();
			}
		}
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph)
			{
				Graph->NotifyGraphChanged();
			}
		}
		for (UEdGraph* Graph : Blueprint->MacroGraphs)
		{
			if (Graph)
			{
				Graph->NotifyGraphChanged();
			}
		}
		for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
		{
			if (Graph)
			{
				Graph->NotifyGraphChanged();
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		Context.MarkPackageDirty(Blueprint->GetOutermost());
	}
}

bool FBlueprintAction::CompileBlueprint(UBlueprint* Blueprint, FString& OutError) const
{
	if (!Blueprint)
	{
		OutError = TEXT("Blueprint is null");
		return false;
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	if (Blueprint->Status == BS_Error)
	{
		OutError = TEXT("Blueprint compilation failed with errors");
		return false;
	}

	return true;
}

// ============================================================================
// FBlueprintNodeAction Implementation
// ============================================================================

bool FBlueprintNodeAction::ValidateGraph(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// First validate Blueprint
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	// Then validate graph exists
	FString GraphName = GetOptionalString(Params, TEXT("graph_name"));
	UBlueprint* BP = GetTargetBlueprint(Params, Context);
	UEdGraph* Graph = FindGraph(BP, GraphName, OutError);

	if (!Graph)
	{
		return false;
	}

	return true;
}

UEdGraph* FBlueprintNodeAction::GetTargetGraph(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) const
{
	FString GraphName = GetOptionalString(Params, TEXT("graph_name"));
	UBlueprint* BP = GetTargetBlueprint(Params, Context);
	FString Error;
	return FindGraph(BP, GraphName, Error);
}

void FBlueprintNodeAction::RegisterCreatedNode(UEdGraphNode* Node, FMCPEditorContext& Context) const
{
	if (Node)
	{
		Context.LastCreatedNodeId = Node->NodeGuid;
	}
}

FVector2D FBlueprintNodeAction::GetNodePosition(const TSharedPtr<FJsonObject>& Params) const
{
	FVector2D Position(0, 0);

	// Try string format "[X, Y]"
	FString PosStr = GetOptionalString(Params, TEXT("node_position"));
	if (!PosStr.IsEmpty())
	{
		TArray<FString> Parts;
		PosStr.Replace(TEXT("["), TEXT("")).Replace(TEXT("]"), TEXT("")).ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() >= 2)
		{
			Position.X = FCString::Atof(*Parts[0].TrimStartAndEnd());
			Position.Y = FCString::Atof(*Parts[1].TrimStartAndEnd());
		}
	}

	// Try array format
	const TArray<TSharedPtr<FJsonValue>>* PosArray = GetOptionalArray(Params, TEXT("node_position"));
	if (PosArray && PosArray->Num() >= 2)
	{
		Position.X = (*PosArray)[0]->AsNumber();
		Position.Y = (*PosArray)[1]->AsNumber();
	}

	return Position;
}
