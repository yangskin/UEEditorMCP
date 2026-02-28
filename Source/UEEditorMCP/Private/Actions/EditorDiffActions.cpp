// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/EditorDiffActions.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

// Source Control
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "ISourceControlRevision.h"
#include "SourceControlOperations.h"
#include "SourceControlHelpers.h"

// Diff
#include "DiffResults.h"
#include "DiffUtils.h"
#include "GraphDiffControl.h"

// Asset loading
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/PackageName.h"

bool FDiffAgainstDepotAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!GetRequiredString(Params, TEXT("asset_path"), AssetPath, OutError))
	{
		return false;
	}

	// Ensure source control is available
	ISourceControlModule& SCModule = ISourceControlModule::Get();
	if (!SCModule.IsEnabled())
	{
		OutError = TEXT("Source control is not enabled in this editor session.");
		return false;
	}

	ISourceControlProvider& Provider = SCModule.GetProvider();
	if (!Provider.IsAvailable())
	{
		OutError = TEXT("Source control provider is not available / not connected.");
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FDiffAgainstDepotAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	// ------------------------------------------------------------------
	// 1. Load the local asset
	// ------------------------------------------------------------------
	UObject* LocalObject = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	if (!LocalObject)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	const FString PackagePath = LocalObject->GetOutermost()->GetName();
	const FString PackageName = LocalObject->GetName();

	// ------------------------------------------------------------------
	// 2. Update source control history
	// ------------------------------------------------------------------
	ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();

	TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> UpdateOp = ISourceControlOperation::Create<FUpdateStatus>();
	UpdateOp->SetUpdateHistory(true);

	const FString DiskFilename = USourceControlHelpers::PackageFilename(PackagePath);
	ECommandResult::Type UpdateResult = Provider.Execute(UpdateOp, DiskFilename);

	if (UpdateResult != ECommandResult::Succeeded)
	{
		return CreateErrorResponse(FString::Printf(
			TEXT("Source control UpdateStatus failed for %s (result=%d)"), *AssetPath, static_cast<int32>(UpdateResult)));
	}

	// ------------------------------------------------------------------
	// 3. Get file state & history
	// ------------------------------------------------------------------
	FSourceControlStatePtr State = Provider.GetState(DiskFilename, EStateCacheUsage::Use);
	if (!State.IsValid())
	{
		return CreateErrorResponse(FString::Printf(TEXT("Cannot get source control state for %s"), *AssetPath));
	}

	if (!State->IsSourceControlled())
	{
		return CreateErrorResponse(FString::Printf(TEXT("Asset is not under source control: %s"), *AssetPath));
	}

	if (State->GetHistorySize() == 0)
	{
		return CreateErrorResponse(FString::Printf(TEXT("No source control history for %s"), *AssetPath));
	}

	// Determine which revision to use
	int32 RequestedRevision = static_cast<int32>(GetOptionalNumber(Params, TEXT("revision"), -1));
	TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Revision;

	if (RequestedRevision > 0)
	{
		Revision = State->FindHistoryRevision(RequestedRevision);
		if (!Revision.IsValid())
		{
			return CreateErrorResponse(FString::Printf(
				TEXT("Revision %d not found in history for %s"), RequestedRevision, *AssetPath));
		}
	}
	else
	{
		Revision = State->GetHistoryItem(0); // latest
	}

	if (!Revision.IsValid())
	{
		return CreateErrorResponse(FString::Printf(TEXT("Failed to get revision for %s"), *AssetPath));
	}

	// ------------------------------------------------------------------
	// 4. Load the depot version
	// ------------------------------------------------------------------
	UPackage* DepotPackage = DiffUtils::LoadPackageForDiff(Revision);
	if (!DepotPackage)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Failed to load depot version for %s"), *AssetPath));
	}

	UObject* DepotObject = FindObject<UObject>(DepotPackage, *PackageName);
	if (!DepotObject)
	{
		DepotObject = DepotPackage->FindAssetInPackage();
	}

	if (!DepotObject)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Failed to find asset in depot package for %s"), *AssetPath));
	}

	// ------------------------------------------------------------------
	// 5. Build revision info
	// ------------------------------------------------------------------
	TSharedPtr<FJsonObject> RevisionInfo = MakeShared<FJsonObject>();
	RevisionInfo->SetStringField(TEXT("revision"), Revision->GetRevision());
	RevisionInfo->SetStringField(TEXT("date"), Revision->GetDate().ToString());
	RevisionInfo->SetStringField(TEXT("userName"), Revision->GetUserName());
	RevisionInfo->SetStringField(TEXT("description"), Revision->GetDescription());
	RevisionInfo->SetNumberField(TEXT("changelistId"), Revision->GetCheckInIdentifier());

	// ------------------------------------------------------------------
	// 6. Compute diffs
	// ------------------------------------------------------------------
	TArray<FDiffSingleResult> AllDiffs;
	bool bIsBlueprintDiff = false;

	UBlueprint* LocalBP = Cast<UBlueprint>(LocalObject);
	UBlueprint* DepotBP = Cast<UBlueprint>(DepotObject);

	if (LocalBP && DepotBP)
	{
		bIsBlueprintDiff = true;

		// 6a. Graph-level diff: match graphs by name and diff each pair
		TMap<FString, UEdGraph*> LocalGraphMap;
		for (UEdGraph* Graph : LocalBP->UbergraphPages)
		{
			if (Graph)
			{
				LocalGraphMap.Add(Graph->GetName(), Graph);
			}
		}
		for (UEdGraph* Graph : LocalBP->FunctionGraphs)
		{
			if (Graph)
			{
				LocalGraphMap.Add(Graph->GetName(), Graph);
			}
		}
		for (UEdGraph* Graph : LocalBP->MacroGraphs)
		{
			if (Graph)
			{
				LocalGraphMap.Add(Graph->GetName(), Graph);
			}
		}

		TMap<FString, UEdGraph*> DepotGraphMap;
		for (UEdGraph* Graph : DepotBP->UbergraphPages)
		{
			if (Graph)
			{
				DepotGraphMap.Add(Graph->GetName(), Graph);
			}
		}
		for (UEdGraph* Graph : DepotBP->FunctionGraphs)
		{
			if (Graph)
			{
				DepotGraphMap.Add(Graph->GetName(), Graph);
			}
		}
		for (UEdGraph* Graph : DepotBP->MacroGraphs)
		{
			if (Graph)
			{
				DepotGraphMap.Add(Graph->GetName(), Graph);
			}
		}

		// Diff graphs that exist in both
		TSet<FString> ProcessedGraphs;
		for (auto& Pair : LocalGraphMap)
		{
			const FString& GraphName = Pair.Key;
			UEdGraph* LocalGraph = Pair.Value;
			ProcessedGraphs.Add(GraphName);

			UEdGraph** DepotGraphPtr = DepotGraphMap.Find(GraphName);
			if (DepotGraphPtr && *DepotGraphPtr)
			{
				TArray<FDiffSingleResult> GraphDiffs;
				FGraphDiffControl::DiffGraphs(*DepotGraphPtr, LocalGraph, GraphDiffs);

				for (FDiffSingleResult& Diff : GraphDiffs)
				{
					Diff.OwningObjectPath = GraphName;
					AllDiffs.Add(MoveTemp(Diff));
				}
			}
			else
			{
				// Graph added locally
				FDiffSingleResult AddedResult;
				AddedResult.Diff = EDiffType::OBJECT_ADDED;
				AddedResult.Category = EDiffType::ADDITION;
				AddedResult.DisplayString = FText::FromString(FString::Printf(TEXT("Graph added: %s"), *GraphName));
				AddedResult.OwningObjectPath = GraphName;
				AllDiffs.Add(AddedResult);
			}
		}

		// Graphs only in depot (removed locally)
		for (auto& Pair : DepotGraphMap)
		{
			if (!ProcessedGraphs.Contains(Pair.Key))
			{
				FDiffSingleResult RemovedResult;
				RemovedResult.Diff = EDiffType::OBJECT_REMOVED;
				RemovedResult.Category = EDiffType::SUBTRACTION;
				RemovedResult.DisplayString = FText::FromString(FString::Printf(TEXT("Graph removed: %s"), *Pair.Key));
				RemovedResult.OwningObjectPath = Pair.Key;
				AllDiffs.Add(RemovedResult);
			}
		}
	}
	else
	{
		// 6b. Generic object property diff
		TArray<FSingleObjectDiffEntry> PropertyDiffs;
		DiffUtils::CompareUnrelatedObjects(DepotObject, LocalObject, PropertyDiffs);

		for (const FSingleObjectDiffEntry& PropDiff : PropertyDiffs)
		{
			FDiffSingleResult Result;
			Result.Diff = EDiffType::OBJECT_PROPERTY;
			Result.Object1 = DepotObject;
			Result.Object2 = LocalObject;

			FString DiffTypeName;
			switch (PropDiff.DiffType)
			{
			case EPropertyDiffType::PropertyAddedToA:
				Result.Category = EDiffType::SUBTRACTION;
				DiffTypeName = TEXT("PropertyRemovedLocally");
				break;
			case EPropertyDiffType::PropertyAddedToB:
				Result.Category = EDiffType::ADDITION;
				DiffTypeName = TEXT("PropertyAddedLocally");
				break;
			case EPropertyDiffType::PropertyValueChanged:
				Result.Category = EDiffType::MODIFICATION;
				DiffTypeName = TEXT("PropertyValueChanged");
				break;
			default:
				Result.Category = EDiffType::MINOR;
				DiffTypeName = TEXT("Unknown");
				break;
			}

			Result.DisplayString = FText::FromString(FString::Printf(
				TEXT("%s: %s"), *DiffTypeName, *PropDiff.Identifier.ToDisplayName()));
			AllDiffs.Add(Result);
		}
	}

	// ------------------------------------------------------------------
	// 7. Serialize results to JSON
	// ------------------------------------------------------------------
	int32 AddedCount = 0, RemovedCount = 0, ModifiedCount = 0, MinorCount = 0;
	TArray<TSharedPtr<FJsonValue>> DiffsArray;

	for (const FDiffSingleResult& Diff : AllDiffs)
	{
		if (!Diff.IsRealDifference())
		{
			continue;
		}

		switch (Diff.Category)
		{
		case EDiffType::ADDITION:    ++AddedCount; break;
		case EDiffType::SUBTRACTION: ++RemovedCount; break;
		case EDiffType::MODIFICATION:++ModifiedCount; break;
		case EDiffType::MINOR:       ++MinorCount; break;
		default: break;
		}

		TSharedPtr<FJsonObject> DiffObj = MakeShared<FJsonObject>();
		DiffObj->SetStringField(TEXT("type"), DiffTypeToString(Diff.Diff));
		DiffObj->SetStringField(TEXT("category"), DiffCategoryToString(Diff.Category));
		DiffObj->SetStringField(TEXT("displayString"), Diff.DisplayString.ToString());

		if (!Diff.OwningObjectPath.IsEmpty())
		{
			DiffObj->SetStringField(TEXT("owningGraph"), Diff.OwningObjectPath);
		}

		if (!Diff.ToolTip.IsEmpty())
		{
			DiffObj->SetStringField(TEXT("tooltip"), Diff.ToolTip.ToString());
		}

		if (Diff.Node1)
		{
			DiffObj->SetStringField(TEXT("node1Name"), Diff.Node1->GetNodeTitle(ENodeTitleType::ListView).ToString());
			DiffObj->SetStringField(TEXT("node1Id"), Diff.Node1->NodeGuid.ToString());
		}
		if (Diff.Node2)
		{
			DiffObj->SetStringField(TEXT("node2Name"), Diff.Node2->GetNodeTitle(ENodeTitleType::ListView).ToString());
			DiffObj->SetStringField(TEXT("node2Id"), Diff.Node2->NodeGuid.ToString());
		}
		if (Diff.Pin1)
		{
			DiffObj->SetStringField(TEXT("pin1Name"), Diff.Pin1->GetName());
		}
		if (Diff.Pin2)
		{
			DiffObj->SetStringField(TEXT("pin2Name"), Diff.Pin2->GetName());
		}

		DiffsArray.Add(MakeShared<FJsonValueObject>(DiffObj));
	}

	// ------------------------------------------------------------------
	// 8. Build final response
	// ------------------------------------------------------------------
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("assetClass"), LocalObject->GetClass()->GetName());
	Result->SetBoolField(TEXT("isBlueprintDiff"), bIsBlueprintDiff);
	Result->SetObjectField(TEXT("revisionInfo"), RevisionInfo);
	Result->SetBoolField(TEXT("hasDifferences"), DiffsArray.Num() > 0);

	TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetNumberField(TEXT("totalDiffs"), DiffsArray.Num());
	Summary->SetNumberField(TEXT("added"), AddedCount);
	Summary->SetNumberField(TEXT("removed"), RemovedCount);
	Summary->SetNumberField(TEXT("modified"), ModifiedCount);
	Summary->SetNumberField(TEXT("minor"), MinorCount);
	Result->SetObjectField(TEXT("summary"), Summary);

	Result->SetArrayField(TEXT("diffs"), DiffsArray);

	return CreateSuccessResponse(Result);
}

FString FDiffAgainstDepotAction::DiffTypeToString(int32 DiffType)
{
	switch (static_cast<EDiffType::Type>(DiffType))
	{
	case EDiffType::NO_DIFFERENCE:             return TEXT("NO_DIFFERENCE");
	case EDiffType::OBJECT_REMOVED:            return TEXT("OBJECT_REMOVED");
	case EDiffType::OBJECT_ADDED:              return TEXT("OBJECT_ADDED");
	case EDiffType::OBJECT_PROPERTY:           return TEXT("OBJECT_PROPERTY");
	case EDiffType::OBJECT_REQUEST_DIFF:       return TEXT("OBJECT_REQUEST_DIFF");
	case EDiffType::NODE_REMOVED:              return TEXT("NODE_REMOVED");
	case EDiffType::NODE_ADDED:                return TEXT("NODE_ADDED");
	case EDiffType::PIN_LINKEDTO_NUM_DEC:      return TEXT("PIN_LINKEDTO_NUM_DEC");
	case EDiffType::PIN_LINKEDTO_NUM_INC:      return TEXT("PIN_LINKEDTO_NUM_INC");
	case EDiffType::PIN_DEFAULT_VALUE:         return TEXT("PIN_DEFAULT_VALUE");
	case EDiffType::PIN_TYPE_CATEGORY:         return TEXT("PIN_TYPE_CATEGORY");
	case EDiffType::PIN_TYPE_SUBCATEGORY:      return TEXT("PIN_TYPE_SUBCATEGORY");
	case EDiffType::PIN_TYPE_SUBCATEGORY_OBJECT: return TEXT("PIN_TYPE_SUBCATEGORY_OBJECT");
	case EDiffType::PIN_TYPE_IS_ARRAY:         return TEXT("PIN_TYPE_IS_ARRAY");
	case EDiffType::PIN_TYPE_IS_REF:           return TEXT("PIN_TYPE_IS_REF");
	case EDiffType::PIN_LINKEDTO_NODE:         return TEXT("PIN_LINKEDTO_NODE");
	case EDiffType::PIN_LINKEDTO_PIN:          return TEXT("PIN_LINKEDTO_PIN");
	case EDiffType::NODE_MOVED:                return TEXT("NODE_MOVED");
	case EDiffType::TIMELINE_LENGTH:           return TEXT("TIMELINE_LENGTH");
	case EDiffType::TIMELINE_AUTOPLAY:         return TEXT("TIMELINE_AUTOPLAY");
	case EDiffType::TIMELINE_LOOP:             return TEXT("TIMELINE_LOOP");
	case EDiffType::TIMELINE_IGNOREDILATION:   return TEXT("TIMELINE_IGNOREDILATION");
	case EDiffType::TIMELINE_NUM_TRACKS:       return TEXT("TIMELINE_NUM_TRACKS");
	case EDiffType::TIMELINE_TRACK_MODIFIED:   return TEXT("TIMELINE_TRACK_MODIFIED");
	case EDiffType::NODE_PIN_COUNT:            return TEXT("NODE_PIN_COUNT");
	case EDiffType::NODE_COMMENT:              return TEXT("NODE_COMMENT");
	case EDiffType::NODE_PROPERTY:             return TEXT("NODE_PROPERTY");
	case EDiffType::INFO_MESSAGE:              return TEXT("INFO_MESSAGE");
	case EDiffType::CUSTOM_OBJECT:             return TEXT("CUSTOM_OBJECT");
	default:                                    return TEXT("UNKNOWN");
	}
}

FString FDiffAgainstDepotAction::DiffCategoryToString(int32 Category)
{
	switch (static_cast<EDiffType::Category>(Category))
	{
	case EDiffType::ADDITION:     return TEXT("ADDITION");
	case EDiffType::SUBTRACTION:  return TEXT("SUBTRACTION");
	case EDiffType::MODIFICATION: return TEXT("MODIFICATION");
	case EDiffType::MINOR:        return TEXT("MINOR");
	case EDiffType::CONTROL:      return TEXT("CONTROL");
	default:                       return TEXT("UNKNOWN");
	}
}
