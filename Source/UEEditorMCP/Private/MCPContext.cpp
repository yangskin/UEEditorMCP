// Copyright (c) 2025 zolnoor. All rights reserved.

#include "MCPContext.h"
#include "Actions/EditorAction.h"
#include "MCPCommonUtils.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"

FMCPEditorContext::FMCPEditorContext()
	: CurrentGraphName(NAME_None)
{
}

void FMCPEditorContext::SetCurrentBlueprint(UBlueprint* BP)
{
	CurrentBlueprint = BP;

	// Reset graph to event graph when changing blueprints
	CurrentGraphName = NAME_None;
}

void FMCPEditorContext::SetCurrentGraph(const FName& GraphName)
{
	CurrentGraphName = GraphName;
}

UEdGraph* FMCPEditorContext::GetCurrentGraph() const
{
	UBlueprint* BP = CurrentBlueprint.Get();
	if (!BP)
	{
		return nullptr;
	}

	// If a specific graph is set, find it
	if (CurrentGraphName != NAME_None)
	{
		for (UEdGraph* Graph : BP->FunctionGraphs)
		{
			if (Graph && Graph->GetFName() == CurrentGraphName)
			{
				return Graph;
			}
		}
		for (UEdGraph* Graph : BP->UbergraphPages)
		{
			if (Graph && Graph->GetFName() == CurrentGraphName)
			{
				return Graph;
			}
		}
		for (UEdGraph* Graph : BP->MacroGraphs)
		{
			if (Graph && Graph->GetFName() == CurrentGraphName)
			{
				return Graph;
			}
		}
	}

	// Default to event graph
	return GetEventGraph();
}

UEdGraph* FMCPEditorContext::GetEventGraph() const
{
	UBlueprint* BP = CurrentBlueprint.Get();
	if (!BP)
	{
		return nullptr;
	}

	// Find the main event graph
	for (UEdGraph* Graph : BP->UbergraphPages)
	{
		if (Graph && Graph->GetFName() == TEXT("EventGraph"))
		{
			return Graph;
		}
	}

	// Fallback to first ubergraph
	if (BP->UbergraphPages.Num() > 0)
	{
		return BP->UbergraphPages[0];
	}

	return nullptr;
}

void FMCPEditorContext::MarkPackageDirty(UPackage* Package)
{
	if (Package)
	{
		Package->MarkPackageDirty();
		DirtyPackages.Add(Package);
	}
}

void FMCPEditorContext::SaveDirtyPackages()
{
	// Use direct UPackage::SavePackage instead of FEditorFileUtils::SaveDirtyPackages.
	// FEditorFileUtils goes through InternalPromptForCheckoutAndSave which can create
	// UI dialogs (SVN checkout prompts, progress bars). These dialogs may spawn tiny
	// windows (8x8 px) that trigger D3D12 swap chain creation, which fails with
	// E_ACCESSDENIED (0x80070005) on some driver/GPU combos — causing a fatal crash.
	// Direct UPackage::SavePackage is headless and safe for MCP automation context.

	int32 SavedCount = 0;
	int32 FailedCount = 0;

	// Save only the packages that MCP actions have dirtied
	for (UPackage* Package : DirtyPackages)
	{
		if (!Package || !Package->IsDirty())
		{
			continue;
		}

		FString PackageName = Package->GetName();
		bool bIsMap = Package->ContainsMap();
		FString Extension = bIsMap
			? FPackageName::GetMapPackageExtension()
			: FPackageName::GetAssetPackageExtension();

		FString PackageFilename;
		if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, PackageFilename, Extension))
		{
			UE_LOG(LogMCP, Warning, TEXT("UEEditorMCP: SaveDirtyPackages - Could not resolve filename for '%s', skipping"), *PackageName);
			FailedCount++;
			continue;
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;

		UObject* AssetToSave = bIsMap ? Package->FindAssetInPackage() : nullptr;

		if (UPackage::SavePackage(Package, AssetToSave, *PackageFilename, SaveArgs))
		{
			SavedCount++;
		}
		else
		{
			UE_LOG(LogMCP, Warning, TEXT("UEEditorMCP: SaveDirtyPackages - Failed to save '%s'"), *PackageName);
			FailedCount++;
		}
	}

	// Verify no dirty packages remain
	TArray<UPackage*> StillDirty;
	for (UPackage* Package : DirtyPackages)
	{
		if (Package && Package->IsDirty())
		{
			StillDirty.Add(Package);
		}
	}

	if (StillDirty.Num() > 0)
	{
		UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: SaveDirtyPackages - %d packages still dirty after save:"), StillDirty.Num());
		for (UPackage* Package : StillDirty)
		{
			UE_LOG(LogMCP, Error, TEXT("  - %s"), *Package->GetName());
		}
	}

	if (SavedCount > 0 || FailedCount > 0)
	{
		UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: SaveDirtyPackages - Saved %d, Failed %d"), SavedCount, FailedCount);
	}

	DirtyPackages.Empty();
}

void FMCPEditorContext::Clear()
{
	CurrentBlueprint = nullptr;
	CurrentGraphName = NAME_None;
	CurrentWorld = nullptr;
	LastCreatedNodeId.Invalidate();
	LastCreatedActorName.Empty();
	LastCreatedWidgetName.Empty();
	DirtyPackages.Empty();

	// Clear material context
	CurrentMaterial = nullptr;
	MaterialNodeMap.Empty();
	LastCreatedMaterialNodeName.Empty();
}

// =========================================================================
// Material Context Methods
// =========================================================================

void FMCPEditorContext::SetCurrentMaterial(UMaterial* Mat)
{
	// If switching materials, clear the node map
	if (CurrentMaterial.Get() != Mat)
	{
		ClearMaterialNodes();
	}
	CurrentMaterial = Mat;
}

void FMCPEditorContext::RegisterMaterialNode(const FString& NodeName, UMaterialExpression* Expr)
{
	if (!NodeName.IsEmpty() && Expr)
	{
		MaterialNodeMap.Add(NodeName, Expr);
		LastCreatedMaterialNodeName = NodeName;
	}
}

UMaterialExpression* FMCPEditorContext::GetMaterialNode(const FString& NodeName) const
{
	// Handle special alias for last created node
	if (NodeName == TEXT("$last_node") || NodeName == TEXT("$last"))
	{
		if (!LastCreatedMaterialNodeName.IsEmpty())
		{
			if (const TWeakObjectPtr<UMaterialExpression>* Found = MaterialNodeMap.Find(LastCreatedMaterialNodeName))
			{
				return Found->Get();
			}
		}
		return nullptr;
	}

	// 1) Find by registered name in session map
	if (const TWeakObjectPtr<UMaterialExpression>* Found = MaterialNodeMap.Find(NodeName))
	{
		return Found->Get();
	}

	// 2) Fallback: search through current material's expressions
	UMaterial* Mat = CurrentMaterial.Get();
	if (!Mat)
	{
		return nullptr;
	}

	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Mat->GetExpressionCollection().Expressions;

	// 2a) Handle $expr_N pattern (matches auto-generated names from get_summary)
	if (NodeName.StartsWith(TEXT("$expr_")))
	{
		FString IndexStr = NodeName.Mid(6); // skip "$expr_"
		if (IndexStr.IsNumeric())
		{
			int32 TargetIndex = FCString::Atoi(*IndexStr);
			int32 CurrentIndex = 0;
			for (UMaterialExpression* Expr : Expressions)
			{
				if (!Expr || Expr->IsA<UMaterialExpressionComment>())
				{
					continue;
				}
				// Skip expressions already registered in the map (they have user-given names)
				bool bRegistered = false;
				for (const auto& Pair : MaterialNodeMap)
				{
					if (Pair.Value.IsValid() && Pair.Value.Get() == Expr)
					{
						bRegistered = true;
						break;
					}
				}
				if (bRegistered)
				{
					continue;
				}
				if (CurrentIndex == TargetIndex)
				{
					return Expr;
				}
				++CurrentIndex;
			}
		}
		return nullptr;
	}

	// 2b) Match by UObject GetName()
	for (UMaterialExpression* Expr : Expressions)
	{
		if (Expr && !Expr->IsA<UMaterialExpressionComment>() && Expr->GetName() == NodeName)
		{
			return Expr;
		}
	}

	// 2c) Match by expression Desc field (user-visible description)
	for (UMaterialExpression* Expr : Expressions)
	{
		if (Expr && !Expr->IsA<UMaterialExpressionComment>() && !Expr->Desc.IsEmpty() && Expr->Desc == NodeName)
		{
			return Expr;
		}
	}

	// 2d) Match by parameter name (ScalarParameter, VectorParameter, etc.)
	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr || Expr->IsA<UMaterialExpressionComment>())
		{
			continue;
		}
		if (UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expr))
		{
			if (Scalar->ParameterName.ToString() == NodeName)
			{
				return Expr;
			}
		}
		else if (UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Expr))
		{
			if (Vector->ParameterName.ToString() == NodeName)
			{
				return Expr;
			}
		}
	}

	return nullptr;
}

void FMCPEditorContext::ClearMaterialNodes()
{
	MaterialNodeMap.Empty();
	LastCreatedMaterialNodeName.Empty();
}

UMaterial* FMCPEditorContext::GetMaterialByNameOrCurrent(const FString& MaterialName) const
{
	// If name is empty, use current
	if (MaterialName.IsEmpty())
	{
		return CurrentMaterial.Get();
	}

	// Search for Material by name in asset registry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), AssetList);

	for (const FAssetData& AssetData : AssetList)
	{
		if (AssetData.AssetName.ToString() == MaterialName)
		{
			return Cast<UMaterial>(AssetData.GetAsset());
		}
	}

	return nullptr;
}

TSharedPtr<FJsonObject> FMCPEditorContext::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();

	// Current Blueprint
	if (UBlueprint* BP = CurrentBlueprint.Get())
	{
		JsonObj->SetStringField(TEXT("current_blueprint"), BP->GetName());
	}
	else
	{
		JsonObj->SetField(TEXT("current_blueprint"), MakeShared<FJsonValueNull>());
	}

	// Current Graph
	if (CurrentGraphName != NAME_None)
	{
		JsonObj->SetStringField(TEXT("current_graph"), CurrentGraphName.ToString());
	}
	else
	{
		JsonObj->SetStringField(TEXT("current_graph"), TEXT("EventGraph"));
	}

	// Last created objects
	if (LastCreatedNodeId.IsValid())
	{
		JsonObj->SetStringField(TEXT("last_node_id"), LastCreatedNodeId.ToString());
	}
	if (!LastCreatedActorName.IsEmpty())
	{
		JsonObj->SetStringField(TEXT("last_actor_name"), LastCreatedActorName);
	}
	if (!LastCreatedWidgetName.IsEmpty())
	{
		JsonObj->SetStringField(TEXT("last_widget_name"), LastCreatedWidgetName);
	}

	// Dirty packages count
	JsonObj->SetNumberField(TEXT("dirty_packages_count"), DirtyPackages.Num());

	// Material context
	if (UMaterial* Mat = CurrentMaterial.Get())
	{
		JsonObj->SetStringField(TEXT("current_material"), Mat->GetName());

		// List registered material nodes
		TArray<TSharedPtr<FJsonValue>> NodeNames;
		for (const auto& Pair : MaterialNodeMap)
		{
			if (Pair.Value.IsValid())
			{
				NodeNames.Add(MakeShared<FJsonValueString>(Pair.Key));
			}
		}
		JsonObj->SetArrayField(TEXT("material_nodes"), NodeNames);

		if (!LastCreatedMaterialNodeName.IsEmpty())
		{
			JsonObj->SetStringField(TEXT("last_material_node"), LastCreatedMaterialNodeName);
		}
	}

	return JsonObj;
}

UBlueprint* FMCPEditorContext::GetBlueprintByNameOrCurrent(const FString& BlueprintName) const
{
	if (BlueprintName.IsEmpty())
	{
		return CurrentBlueprint.Get();
	}
	return FMCPCommonUtils::FindBlueprint(BlueprintName);
}

UEdGraph* FMCPEditorContext::GetGraphByNameOrCurrent(const FString& GraphName) const
{
	UBlueprint* BP = CurrentBlueprint.Get();
	if (!BP)
	{
		return nullptr;
	}

	// If name is empty, use current graph
	if (GraphName.IsEmpty())
	{
		return GetCurrentGraph();
	}

	// Search function graphs
	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		if (Graph && Graph->GetFName().ToString() == GraphName)
		{
			return Graph;
		}
	}

	// Search ubergraph pages
	for (UEdGraph* Graph : BP->UbergraphPages)
	{
		if (Graph && Graph->GetFName().ToString() == GraphName)
		{
			return Graph;
		}
	}

	return nullptr;
}

FGuid FMCPEditorContext::ResolveNodeId(const FString& NodeIdOrAlias) const
{
	// Handle special alias
	if (NodeIdOrAlias == TEXT("$last_node") || NodeIdOrAlias == TEXT("$last"))
	{
		return LastCreatedNodeId;
	}

	// Try to parse as GUID
	FGuid NodeId;
	if (FGuid::Parse(NodeIdOrAlias, NodeId))
	{
		return NodeId;
	}

	// Invalid
	return FGuid();
}
