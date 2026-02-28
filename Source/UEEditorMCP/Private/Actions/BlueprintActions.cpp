// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/BlueprintActions.h"
#include "MCPCommonUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Factories/BlueprintFactory.h"
#include "Factories/MaterialFactoryNew.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Camera/CameraComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "ComponentReregisterContext.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "FileHelpers.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "UObject/SavePackage.h"

// ============================================================================
// FCreateBlueprintAction
// ============================================================================

bool FCreateBlueprintAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString BlueprintName;
	if (!GetRequiredString(Params, TEXT("name"), BlueprintName, OutError))
	{
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FCreateBlueprintAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName;
	FString Error;
	GetRequiredString(Params, TEXT("name"), BlueprintName, Error);

	FString ParentClassName = GetOptionalString(Params, TEXT("parent_class"), TEXT("Actor"));
	FString PackagePath = GetOptionalString(Params, TEXT("path"), TEXT("/Game/Blueprints"));
	if (!PackagePath.EndsWith(TEXT("/")))
	{
		PackagePath += TEXT("/");
	}
	FString FullAssetPath = PackagePath + BlueprintName;

	// Clean up existing Blueprint if any
	CleanupExistingBlueprint(BlueprintName, PackagePath);

	// Resolve parent class
	UClass* ParentClass = ResolveParentClass(ParentClassName);
	if (!ParentClass)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Could not resolve parent class: %s"), *ParentClassName),
			TEXT("invalid_parent_class")
		);
	}

	// Create the Blueprint factory
	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = ParentClass;

	// Create the Blueprint
	UPackage* Package = CreatePackage(*(PackagePath + BlueprintName));
	UBlueprint* NewBlueprint = Cast<UBlueprint>(Factory->FactoryCreateNew(
		UBlueprint::StaticClass(),
		Package,
		*BlueprintName,
		RF_Standalone | RF_Public,
		nullptr,
		GWarn
	));

	if (!NewBlueprint)
	{
		return CreateErrorResponse(TEXT("Failed to create Blueprint"), TEXT("creation_failed"));
	}

	// Notify asset registry
	FAssetRegistryModule::AssetCreated(NewBlueprint);
	Package->MarkPackageDirty();

	// Update context
	Context.SetCurrentBlueprint(NewBlueprint);
	Context.MarkPackageDirty(Package);

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Created Blueprint '%s' with parent '%s'"),
		*BlueprintName, *ParentClass->GetName());

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), BlueprintName);
	Result->SetStringField(TEXT("path"), FullAssetPath);
	Result->SetStringField(TEXT("parent_class"), ParentClass->GetName());
	return CreateSuccessResponse(Result);
}

UClass* FCreateBlueprintAction::ResolveParentClass(const FString& ParentClassName) const
{
	// Direct StaticClass lookups for common classes
	if (ParentClassName == TEXT("Actor") || ParentClassName == TEXT("AActor"))
	{
		return AActor::StaticClass();
	}
	if (ParentClassName == TEXT("Pawn") || ParentClassName == TEXT("APawn"))
	{
		return APawn::StaticClass();
	}
	if (ParentClassName == TEXT("GameStateBase") || ParentClassName == TEXT("AGameStateBase"))
	{
		return AGameStateBase::StaticClass();
	}
	if (ParentClassName == TEXT("GameModeBase") || ParentClassName == TEXT("AGameModeBase"))
	{
		return AGameModeBase::StaticClass();
	}
	if (ParentClassName == TEXT("PlayerController") || ParentClassName == TEXT("APlayerController"))
	{
		return APlayerController::StaticClass();
	}

	// Build name variants
	FString ClassName = ParentClassName;
	if (!ClassName.StartsWith(TEXT("A")))
	{
		ClassName = TEXT("A") + ClassName;
	}

	// Try loading from common modules
	static const FString Modules[] = {
		TEXT("/Script/Engine"),
		TEXT("/Script/GameplayAbilities"),
		TEXT("/Script/AIModule"),
		TEXT("/Script/Game")
	};

	TArray<FString> NameVariants;
	NameVariants.Add(ClassName);
	NameVariants.Add(ParentClassName);

	for (const FString& Name : NameVariants)
	{
		for (const FString& Module : Modules)
		{
			FString Path = FString::Printf(TEXT("%s.%s"), *Module, *Name);
			UClass* FoundClass = LoadClass<AActor>(nullptr, *Path);
			if (FoundClass)
			{
				return FoundClass;
			}
		}
	}

	// Try as Blueprint parent
	FString BlueprintPath = FString::Printf(TEXT("/Game/Blueprints/%s.%s"), *ParentClassName, *ParentClassName);
	UBlueprint* ParentBlueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (ParentBlueprint && ParentBlueprint->GeneratedClass)
	{
		return ParentBlueprint->GeneratedClass;
	}

	// Fallback to Actor
	UE_LOG(LogMCP, Warning, TEXT("UEEditorMCP: Could not resolve parent class '%s', defaulting to AActor"),
		*ParentClassName);
	return AActor::StaticClass();
}

void FCreateBlueprintAction::CleanupExistingBlueprint(const FString& BlueprintName, const FString& PackagePath) const
{
	FString PackagePathName = PackagePath + BlueprintName;

	// Check in-memory first
	UPackage* ExistingPackage = FindPackage(nullptr, *PackagePathName);
	if (ExistingPackage)
	{
		UBlueprint* ExistingBlueprint = FindObject<UBlueprint>(ExistingPackage, *BlueprintName);
		if (ExistingBlueprint)
		{
			UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Blueprint '%s' exists in memory, cleaning up"), *BlueprintName);

			FString TempName = FString::Printf(TEXT("%s_TEMP_%d"), *BlueprintName, FMath::Rand());
			ExistingBlueprint->Rename(*TempName, GetTransientPackage(),
				REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
			ExistingBlueprint->MarkAsGarbage();
			ExistingPackage->MarkAsGarbage();
		}
	}

	// Delete from disk
	if (UEditorAssetLibrary::DoesAssetExist(PackagePathName))
	{
		UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Blueprint '%s' exists on disk, deleting"), *BlueprintName);
		UEditorAssetLibrary::DeleteAsset(PackagePathName);
	}
}


// ============================================================================
// FCompileBlueprintAction
// ============================================================================

bool FCompileBlueprintAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: compile_blueprint Validate called"));
	bool bResult = ValidateBlueprint(Params, Context, OutError);
	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: compile_blueprint Validate result: %s, Error: '%s'"),
		bResult ? TEXT("true") : TEXT("false"), *OutError);
	return bResult;
}

TSharedPtr<FJsonObject> FCompileBlueprintAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: compile_blueprint ExecuteInternal called"));

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	if (!Blueprint)
	{
		UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: compile_blueprint - Blueprint not found"));
		return CreateErrorResponse(TEXT("Blueprint not found"), TEXT("not_found"));
	}

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: compile_blueprint - Found blueprint '%s'"), *Blueprint->GetName());

	// Repair WidgetVariableNameToGuidMap before compile for Widget Blueprints.
	// MCP's ConstructWidget API never populates this map; WidgetBlueprintCompiler
	// fires ensure(WidgetBP->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
	// for every named widget that lacks a GUID entry.  Running this repair here
	// also fixes any pre-existing broken Widget Blueprint assets.
	//
	// NOTE: On editor startup, UE's ensure() fires but does NOT stop execution --
	// the UMG compiler continues and auto-patches the GUID map in memory.  So when
	// MCP compile runs, the memory map may already be complete even if the uasset
	// file on disk still lacks the entry.  We therefore check BOTH before compile
	// (pre-patch) and after (to catch the engine's own silent repair), and always
	// force-save a WidgetBlueprint that compiled successfully so that the fixed
	// map is persisted to disk and the ensure never fires again on next startup.
	bool bIsWidgetBlueprint = (Cast<UWidgetBlueprint>(Blueprint) != nullptr);

	if (UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Blueprint))
	{
		if (WBP->WidgetTree)
		{
			WBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
			{
				if (Widget && !WBP->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
				{
					WBP->WidgetVariableNameToGuidMap.Add(Widget->GetFName(), FGuid::NewGuid());
				}
			});
		}
	}

	// Compile
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	// Check status
	EBlueprintStatus Status = Blueprint->Status;
	bool bHasErrors = (Status == EBlueprintStatus::BS_Error);
	bool bSuccess = (Status == EBlueprintStatus::BS_UpToDate || Status == EBlueprintStatus::BS_UpToDateWithWarnings);

	// Collect messages
	TArray<TSharedPtr<FJsonValue>> Errors;
	TArray<TSharedPtr<FJsonValue>> Warnings;
	CollectCompilationMessages(Blueprint, Errors, Warnings);

	// Save if successful
	int32 SavedPackagesCount = 0;
	if (bSuccess)
	{
		// Helper: save a single package unconditionally.
		auto SaveOnePackage = [&](UPackage* Package) -> bool
		{
			if (!Package) return false;
			FString PackageFileName;
			FString PackageName = Package->GetName();
			bool bIsMap = Package->ContainsMap();
			FString Extension = bIsMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
			if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, PackageFileName, Extension))
				return false;
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UObject* AssetToSave = bIsMap ? Package->FindAssetInPackage() : nullptr;
			return UPackage::SavePackage(Package, AssetToSave, *PackageFileName, SaveArgs);
		};

		// If we patched WidgetVariableNameToGuidMap, force-save the blueprint
		// package now regardless of its dirty state -- the repair must persist so
		// the UMG compiler does not fire the ensure on the next editor startup.
		if (bIsWidgetBlueprint)
		{
			if (SaveOnePackage(Blueprint->GetOutermost()))
			{
				SavedPackagesCount++;
				UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Force-saved WBP GUID-patched package '%s'"), *Blueprint->GetName());
			}
		}

		// Also save any other dirty packages (e.g., generated class).
		TArray<UPackage*> DirtyPackages;
		FEditorFileUtils::GetDirtyPackages(DirtyPackages);
		for (UPackage* Package : DirtyPackages)
		{
			if (Package && Package != Blueprint->GetOutermost())
			{
				if (SaveOnePackage(Package))
					SavedPackagesCount++;
			}
		}
	}

	// Status string
	FString StatusStr;
	switch (Status)
	{
		case EBlueprintStatus::BS_Error: StatusStr = TEXT("Error"); break;
		case EBlueprintStatus::BS_UpToDate: StatusStr = TEXT("UpToDate"); break;
		case EBlueprintStatus::BS_UpToDateWithWarnings: StatusStr = TEXT("UpToDateWithWarnings"); break;
		case EBlueprintStatus::BS_Dirty: StatusStr = TEXT("Dirty"); break;
		default: StatusStr = TEXT("Unknown"); break;
	}

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Compiled Blueprint '%s' - Status: %s, Errors: %d, Warnings: %d"),
		*Blueprint->GetName(), *StatusStr, Errors.Num(), Warnings.Num());

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), Blueprint->GetName());
	Result->SetBoolField(TEXT("compiled"), bSuccess);  // Use "compiled" instead of "success" to avoid conflict
	Result->SetStringField(TEXT("status"), StatusStr);
	Result->SetNumberField(TEXT("error_count"), Errors.Num());
	Result->SetNumberField(TEXT("warning_count"), Warnings.Num());
	Result->SetNumberField(TEXT("saved_packages_count"), SavedPackagesCount);

	if (Errors.Num() > 0)
	{
		Result->SetArrayField(TEXT("errors"), Errors);
	}
	if (Warnings.Num() > 0)
	{
		Result->SetArrayField(TEXT("warnings"), Warnings);
	}

	// If compilation failed, return details along with the error flag
	if (!bSuccess)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint '%s' compilation failed with %d error(s)"),
			*Blueprint->GetName(), Errors.Num()));
		Result->SetStringField(TEXT("error_type"), TEXT("compilation_failed"));
		return Result;
	}

	return CreateSuccessResponse(Result);
}

void FCompileBlueprintAction::CollectCompilationMessages(UBlueprint* Blueprint,
	TArray<TSharedPtr<FJsonValue>>& OutErrors,
	TArray<TSharedPtr<FJsonValue>>& OutWarnings) const
{
	auto ProcessGraph = [&](UEdGraph* Graph)
	{
		if (!Graph) return;

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->bHasCompilerMessage)
			{
				TSharedPtr<FJsonObject> MsgObj = MakeShared<FJsonObject>();
				MsgObj->SetStringField(TEXT("node"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
				MsgObj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
				MsgObj->SetStringField(TEXT("message"), Node->ErrorMsg);

				if (Node->ErrorType == EMessageSeverity::Error)
				{
					OutErrors.Add(MakeShared<FJsonValueObject>(MsgObj));
				}
				else if (Node->ErrorType == EMessageSeverity::Warning)
				{
					OutWarnings.Add(MakeShared<FJsonValueObject>(MsgObj));
				}
			}
		}
	};

	// Check ubergraph pages (event graph)
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		ProcessGraph(Graph);
	}

	// Check function graphs
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		ProcessGraph(Graph);
	}
}


// ============================================================================
// FAddComponentToBlueprintAction
// ============================================================================

bool FAddComponentToBlueprintAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	// Support both "component_type" (canonical) and "component_class" (alias)
	FString ComponentType;
	if (!Params->TryGetStringField(TEXT("component_type"), ComponentType))
	{
		if (!Params->TryGetStringField(TEXT("component_class"), ComponentType))
		{
			OutError = TEXT("Missing required parameter: component_type (or component_class)");
			return false;
		}
	}

	FString ComponentName;
	if (!GetRequiredString(Params, TEXT("component_name"), ComponentName, OutError))
	{
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FAddComponentToBlueprintAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	FString Error;
	FString ComponentName;
	GetRequiredString(Params, TEXT("component_name"), ComponentName, Error);

	// Support both "component_type" (canonical) and "component_class" (alias)
	FString ComponentType;
	if (!Params->TryGetStringField(TEXT("component_type"), ComponentType))
	{
		Params->TryGetStringField(TEXT("component_class"), ComponentType);
	}

	// Resolve component class
	UClass* ComponentClass = ResolveComponentClass(ComponentType);
	if (!ComponentClass)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Unknown component type: %s"), *ComponentType),
			TEXT("invalid_component_type")
		);
	}

	// Ensure SCS exists
	if (!Blueprint->SimpleConstructionScript)
	{
		return CreateErrorResponse(TEXT("Blueprint has no SimpleConstructionScript"), TEXT("invalid_blueprint"));
	}

	// Create the component node
	USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, *ComponentName);
	if (!NewNode)
	{
		return CreateErrorResponse(TEXT("Failed to create component node"), TEXT("creation_failed"));
	}

	// Set transform if this is a scene component
	USceneComponent* SceneComponent = Cast<USceneComponent>(NewNode->ComponentTemplate);
	if (SceneComponent)
	{
		if (Params->HasField(TEXT("location")))
		{
			SceneComponent->SetRelativeLocation(GetVectorFromParams(Params, TEXT("location")));
		}
		if (Params->HasField(TEXT("rotation")))
		{
			SceneComponent->SetRelativeRotation(GetRotatorFromParams(Params, TEXT("rotation")));
		}
		if (Params->HasField(TEXT("scale")))
		{
			SceneComponent->SetRelativeScale3D(GetVectorFromParams(Params, TEXT("scale")));
		}
	}

	// Add to Blueprint
	Blueprint->SimpleConstructionScript->AddNode(NewNode);
	MarkBlueprintModified(Blueprint, Context);

	// Compile
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Added component '%s' (%s) to Blueprint '%s'"),
		*ComponentName, *ComponentType, *Blueprint->GetName());

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("component_name"), ComponentName);
	Result->SetStringField(TEXT("component_type"), ComponentClass->GetName());
	return CreateSuccessResponse(Result);
}

UClass* FAddComponentToBlueprintAction::ResolveComponentClass(const FString& ComponentTypeName) const
{
	// Build candidate names
	TArray<FString> Candidates;
	Candidates.Add(ComponentTypeName);

	if (!ComponentTypeName.EndsWith(TEXT("Component")))
	{
		Candidates.Add(ComponentTypeName + TEXT("Component"));
	}
	if (!ComponentTypeName.StartsWith(TEXT("U")))
	{
		Candidates.Add(TEXT("U") + ComponentTypeName);
		if (!ComponentTypeName.EndsWith(TEXT("Component")))
		{
			Candidates.Add(TEXT("U") + ComponentTypeName + TEXT("Component"));
		}
	}

	// Modules to search
	static const FString ModulePaths[] = {
		TEXT("/Script/Engine"),
		TEXT("/Script/UMG"),
		TEXT("/Script/AIModule"),
		TEXT("/Script/NavigationSystem")
	};

	for (const FString& Candidate : Candidates)
	{
		// Strip U prefix for path lookup
		FString PathName = Candidate;
		if (Candidate.StartsWith(TEXT("U")))
		{
			PathName = Candidate.Mid(1);
		}

		for (const FString& ModulePath : ModulePaths)
		{
			FString FullPath = FString::Printf(TEXT("%s.%s"), *ModulePath, *PathName);
			UClass* Found = LoadClass<UActorComponent>(nullptr, *FullPath);
			if (Found)
			{
				return Found;
			}
		}
	}

	return nullptr;
}

FVector FAddComponentToBlueprintAction::GetVectorFromParams(const TSharedPtr<FJsonObject>& Params, const FString& FieldName) const
{
	FVector Result(0, 0, 0);
	const TArray<TSharedPtr<FJsonValue>>* Arr = GetOptionalArray(Params, FieldName);
	if (Arr && Arr->Num() >= 3)
	{
		Result.X = (*Arr)[0]->AsNumber();
		Result.Y = (*Arr)[1]->AsNumber();
		Result.Z = (*Arr)[2]->AsNumber();
	}
	return Result;
}

FRotator FAddComponentToBlueprintAction::GetRotatorFromParams(const TSharedPtr<FJsonObject>& Params, const FString& FieldName) const
{
	FRotator Result(0, 0, 0);
	const TArray<TSharedPtr<FJsonValue>>* Arr = GetOptionalArray(Params, FieldName);
	if (Arr && Arr->Num() >= 3)
	{
		Result.Pitch = (*Arr)[0]->AsNumber();
		Result.Yaw = (*Arr)[1]->AsNumber();
		Result.Roll = (*Arr)[2]->AsNumber();
	}
	return Result;
}


// ============================================================================
// FSpawnBlueprintActorAction
// ============================================================================

bool FSpawnBlueprintActorAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString BlueprintName, ActorName;
	if (!GetRequiredString(Params, TEXT("blueprint_name"), BlueprintName, OutError))
	{
		return false;
	}
	if (!GetRequiredString(Params, TEXT("actor_name"), ActorName, OutError))
	{
		return false;
	}

	// Verify Blueprint exists
	UBlueprint* BP = FindBlueprint(BlueprintName, OutError);
	if (!BP)
	{
		return false;
	}

	// Verify it has a generated class
	if (!BP->GeneratedClass)
	{
		OutError = FString::Printf(TEXT("Blueprint '%s' has no generated class - compile it first"), *BlueprintName);
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FSpawnBlueprintActorAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BlueprintName, ActorName, Error;
	GetRequiredString(Params, TEXT("blueprint_name"), BlueprintName, Error);
	GetRequiredString(Params, TEXT("actor_name"), ActorName, Error);

	UBlueprint* Blueprint = FindBlueprint(BlueprintName, Error);
	if (!Blueprint)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	// Get world
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	// Parse transform
	FVector Location = GetVectorFromParams(Params, TEXT("location"));
	FRotator Rotation = GetRotatorFromParams(Params, TEXT("rotation"));

	FTransform SpawnTransform;
	SpawnTransform.SetLocation(Location);
	SpawnTransform.SetRotation(FQuat(Rotation));

	// Spawn
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(*ActorName);
	SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;

	AActor* NewActor = World->SpawnActor<AActor>(Blueprint->GeneratedClass, SpawnTransform, SpawnParams);
	if (!NewActor)
	{
		return CreateErrorResponse(TEXT("Failed to spawn actor"), TEXT("spawn_failed"));
	}

	NewActor->SetActorLabel(*ActorName);
	Context.LastCreatedActorName = ActorName;

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Spawned '%s' from Blueprint '%s' at (%f, %f, %f)"),
		*ActorName, *BlueprintName, Location.X, Location.Y, Location.Z);

	return CreateSuccessResponse(ActorToJson(NewActor));
}

FVector FSpawnBlueprintActorAction::GetVectorFromParams(const TSharedPtr<FJsonObject>& Params, const FString& FieldName) const
{
	FVector Result(0, 0, 0);
	const TArray<TSharedPtr<FJsonValue>>* Arr = GetOptionalArray(Params, FieldName);
	if (Arr && Arr->Num() >= 3)
	{
		Result.X = (*Arr)[0]->AsNumber();
		Result.Y = (*Arr)[1]->AsNumber();
		Result.Z = (*Arr)[2]->AsNumber();
	}
	return Result;
}

FRotator FSpawnBlueprintActorAction::GetRotatorFromParams(const TSharedPtr<FJsonObject>& Params, const FString& FieldName) const
{
	FRotator Result(0, 0, 0);
	const TArray<TSharedPtr<FJsonValue>>* Arr = GetOptionalArray(Params, FieldName);
	if (Arr && Arr->Num() >= 3)
	{
		Result.Pitch = (*Arr)[0]->AsNumber();
		Result.Yaw = (*Arr)[1]->AsNumber();
		Result.Roll = (*Arr)[2]->AsNumber();
	}
	return Result;
}

TSharedPtr<FJsonObject> FSpawnBlueprintActorAction::ActorToJson(AActor* Actor) const
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	if (Actor)
	{
		Obj->SetStringField(TEXT("name"), Actor->GetName());
		Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

		FVector Loc = Actor->GetActorLocation();
		TArray<TSharedPtr<FJsonValue>> LocArray;
		LocArray.Add(MakeShared<FJsonValueNumber>(Loc.X));
		LocArray.Add(MakeShared<FJsonValueNumber>(Loc.Y));
		LocArray.Add(MakeShared<FJsonValueNumber>(Loc.Z));
		Obj->SetArrayField(TEXT("location"), LocArray);

		FRotator Rot = Actor->GetActorRotation();
		TArray<TSharedPtr<FJsonValue>> RotArray;
		RotArray.Add(MakeShared<FJsonValueNumber>(Rot.Pitch));
		RotArray.Add(MakeShared<FJsonValueNumber>(Rot.Yaw));
		RotArray.Add(MakeShared<FJsonValueNumber>(Rot.Roll));
		Obj->SetArrayField(TEXT("rotation"), RotArray);

		FVector Scale = Actor->GetActorScale3D();
		TArray<TSharedPtr<FJsonValue>> ScaleArray;
		ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.X));
		ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Y));
		ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Z));
		Obj->SetArrayField(TEXT("scale"), ScaleArray);
	}
	return Obj;
}


// ============================================================================
// FSetComponentPropertyAction
// ============================================================================

bool FSetComponentPropertyAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString ComponentName, PropertyName;
	if (!GetRequiredString(Params, TEXT("component_name"), ComponentName, OutError))
	{
		return false;
	}
	if (!GetRequiredString(Params, TEXT("property_name"), PropertyName, OutError))
	{
		return false;
	}
	if (!Params->HasField(TEXT("property_value")))
	{
		OutError = TEXT("Missing 'property_value' parameter");
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FSetComponentPropertyAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	FString Error;
	FString ComponentName, PropertyName;
	GetRequiredString(Params, TEXT("component_name"), ComponentName, Error);
	GetRequiredString(Params, TEXT("property_name"), PropertyName, Error);

	// Find component node
	USCS_Node* ComponentNode = FMCPCommonUtils::FindComponentNode(Blueprint, ComponentName);
	if (!ComponentNode)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Component not found: %s"), *ComponentName),
			TEXT("component_not_found")
		);
	}

	UObject* ComponentTemplate = ComponentNode->ComponentTemplate;
	if (!ComponentTemplate)
	{
		return CreateErrorResponse(TEXT("Invalid component template"), TEXT("invalid_template"));
	}

	// Get the value
	TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));

	// Set the property
	FString ErrorMessage;
	if (!FMCPCommonUtils::SetObjectProperty(ComponentTemplate, PropertyName, JsonValue, ErrorMessage))
	{
		return CreateErrorResponse(ErrorMessage, TEXT("property_set_failed"));
	}

	MarkBlueprintModified(Blueprint, Context);

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Set property '%s' on component '%s' in Blueprint '%s'"),
		*PropertyName, *ComponentName, *Blueprint->GetName());

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("component"), ComponentName);
	Result->SetStringField(TEXT("property"), PropertyName);
	Result->SetBoolField(TEXT("success"), true);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// FSetStaticMeshPropertiesAction
// ============================================================================

bool FSetStaticMeshPropertiesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString ComponentName;
	if (!GetRequiredString(Params, TEXT("component_name"), ComponentName, OutError))
	{
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FSetStaticMeshPropertiesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	FString Error;
	FString ComponentName;
	GetRequiredString(Params, TEXT("component_name"), ComponentName, Error);

	// Find component node
	USCS_Node* ComponentNode = FMCPCommonUtils::FindComponentNode(Blueprint, ComponentName);
	if (!ComponentNode)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Component not found: %s"), *ComponentName),
			TEXT("component_not_found")
		);
	}

	UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(ComponentNode->ComponentTemplate);
	if (!MeshComponent)
	{
		return CreateErrorResponse(TEXT("Component is not a StaticMeshComponent"), TEXT("wrong_component_type"));
	}

	// Set static mesh
	FString MeshPath = GetOptionalString(Params, TEXT("static_mesh"), TEXT(""));
	if (!MeshPath.IsEmpty())
	{
		UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
		if (Mesh)
		{
			MeshComponent->SetStaticMesh(Mesh);
		}
	}

	// Set material
	FString MaterialPath = GetOptionalString(Params, TEXT("material"), TEXT(""));
	if (!MaterialPath.IsEmpty())
	{
		UMaterialInterface* Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
		if (Material)
		{
			MeshComponent->SetMaterial(0, Material);
		}
	}

	// Set overlay material
	FString OverlayMaterialPath = GetOptionalString(Params, TEXT("overlay_material"), TEXT(""));
	if (!OverlayMaterialPath.IsEmpty())
	{
		UMaterialInterface* OverlayMaterial = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(OverlayMaterialPath));
		if (OverlayMaterial)
		{
			MeshComponent->SetOverlayMaterial(OverlayMaterial);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	Context.MarkPackageDirty(Blueprint->GetOutermost());

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Set mesh properties on '%s' in Blueprint '%s'"),
		*ComponentName, *Blueprint->GetName());

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("component"), ComponentName);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// FSetPhysicsPropertiesAction
// ============================================================================

bool FSetPhysicsPropertiesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString ComponentName;
	if (!GetRequiredString(Params, TEXT("component_name"), ComponentName, OutError))
	{
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FSetPhysicsPropertiesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	FString Error;
	FString ComponentName;
	GetRequiredString(Params, TEXT("component_name"), ComponentName, Error);

	// Find component node
	USCS_Node* ComponentNode = FMCPCommonUtils::FindComponentNode(Blueprint, ComponentName);
	if (!ComponentNode)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Component not found: %s"), *ComponentName),
			TEXT("component_not_found")
		);
	}

	UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(ComponentNode->ComponentTemplate);
	if (!PrimComponent)
	{
		return CreateErrorResponse(TEXT("Component is not a PrimitiveComponent"), TEXT("wrong_component_type"));
	}

	// Set physics properties
	if (Params->HasField(TEXT("simulate_physics")))
	{
		PrimComponent->SetSimulatePhysics(Params->GetBoolField(TEXT("simulate_physics")));
	}

	if (Params->HasField(TEXT("mass")))
	{
		float Mass = Params->GetNumberField(TEXT("mass"));
		PrimComponent->SetMassOverrideInKg(NAME_None, Mass);
	}

	if (Params->HasField(TEXT("linear_damping")))
	{
		PrimComponent->SetLinearDamping(Params->GetNumberField(TEXT("linear_damping")));
	}

	if (Params->HasField(TEXT("angular_damping")))
	{
		PrimComponent->SetAngularDamping(Params->GetNumberField(TEXT("angular_damping")));
	}

	if (Params->HasField(TEXT("gravity_enabled")))
	{
		PrimComponent->SetEnableGravity(Params->GetBoolField(TEXT("gravity_enabled")));
	}

	MarkBlueprintModified(Blueprint, Context);

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Set physics properties on '%s' in Blueprint '%s'"),
		*ComponentName, *Blueprint->GetName());

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("component"), ComponentName);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// FSetBlueprintPropertyAction
// ============================================================================

bool FSetBlueprintPropertyAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString PropertyName;
	if (!GetRequiredString(Params, TEXT("property_name"), PropertyName, OutError))
	{
		return false;
	}
	if (!Params->HasField(TEXT("property_value")))
	{
		OutError = TEXT("Missing 'property_value' parameter");
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FSetBlueprintPropertyAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	FString Error;
	FString PropertyName;
	GetRequiredString(Params, TEXT("property_name"), PropertyName, Error);

	// Get the default object
	if (!Blueprint->GeneratedClass)
	{
		return CreateErrorResponse(TEXT("Blueprint has no generated class - compile it first"), TEXT("not_compiled"));
	}

	UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
	if (!DefaultObject)
	{
		return CreateErrorResponse(TEXT("Failed to get default object"), TEXT("no_default_object"));
	}

	// Get the value
	TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));

	// Set the property
	FString ErrorMessage;
	if (!FMCPCommonUtils::SetObjectProperty(DefaultObject, PropertyName, JsonValue, ErrorMessage))
	{
		return CreateErrorResponse(ErrorMessage, TEXT("property_set_failed"));
	}

	MarkBlueprintModified(Blueprint, Context);

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Set property '%s' on Blueprint '%s'"),
		*PropertyName, *Blueprint->GetName());

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("property"), PropertyName);
	Result->SetBoolField(TEXT("success"), true);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// FCreateColoredMaterialAction
// ============================================================================

bool FCreateColoredMaterialAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MaterialName;
	if (!GetRequiredString(Params, TEXT("material_name"), MaterialName, OutError))
	{
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FCreateColoredMaterialAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString MaterialName;
	GetRequiredString(Params, TEXT("material_name"), MaterialName, Error);

	// Get color (default to white)
	float R = 1.0f, G = 1.0f, B = 1.0f;
	const TArray<TSharedPtr<FJsonValue>>* ColorArray = GetOptionalArray(Params, TEXT("color"));
	if (ColorArray && ColorArray->Num() >= 3)
	{
		R = (*ColorArray)[0]->AsNumber();
		G = (*ColorArray)[1]->AsNumber();
		B = (*ColorArray)[2]->AsNumber();
	}

	// Clean up existing material
	FString MaterialPath = GetOptionalString(Params, TEXT("path"), TEXT("/Game/Materials"));
	if (!MaterialPath.EndsWith(TEXT("/")))
	{
		MaterialPath += TEXT("/");
	}
	FString MaterialPackagePath = MaterialPath + MaterialName;
	UPackage* ExistingPackage = FindPackage(nullptr, *MaterialPackagePath);
	if (ExistingPackage)
	{
		UMaterial* ExistingMaterial = FindObject<UMaterial>(ExistingPackage, *MaterialName);
		if (ExistingMaterial)
		{
			FString TempName = FString::Printf(TEXT("%s_TEMP_%d"), *MaterialName, FMath::Rand());
			ExistingMaterial->Rename(*TempName, GetTransientPackage(),
				REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
			ExistingMaterial->MarkAsGarbage();
			ExistingPackage->MarkAsGarbage();
		}
	}

	if (UEditorAssetLibrary::DoesAssetExist(MaterialPackagePath))
	{
		UEditorAssetLibrary::DeleteAsset(MaterialPackagePath);
	}

	// Create the package
	UPackage* Package = CreatePackage(*MaterialPackagePath);
	if (!Package)
	{
		return CreateErrorResponse(TEXT("Failed to create package for material"), TEXT("package_creation_failed"));
	}
	Package->FullyLoad();

	// Create the material
	UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();
	UMaterial* NewMaterial = (UMaterial*)MaterialFactory->FactoryCreateNew(
		UMaterial::StaticClass(), Package, *MaterialName,
		RF_Public | RF_Standalone, nullptr, GWarn);

	if (!NewMaterial)
	{
		return CreateErrorResponse(TEXT("Failed to create material"), TEXT("material_creation_failed"));
	}

	// Create color expression
	UMaterialExpressionConstant3Vector* ColorExpression = NewObject<UMaterialExpressionConstant3Vector>(NewMaterial);
	ColorExpression->Constant = FLinearColor(R, G, B);
	NewMaterial->GetExpressionCollection().AddExpression(ColorExpression);

	// Connect to BaseColor
	NewMaterial->GetEditorOnlyData()->BaseColor.Expression = ColorExpression;

	// Trigger shader compilation
	NewMaterial->PreEditChange(nullptr);
	NewMaterial->PostEditChange();

	// Reregister components
	{
		FGlobalComponentReregisterContext RecreateComponents;
	}

	// Mark and register
	Package->SetDirtyFlag(true);
	NewMaterial->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewMaterial);

	// Save
	FString PackageFileName = FPackageName::LongPackageNameToFilename(MaterialPackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, NewMaterial, *PackageFileName, SaveArgs);

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Created material '%s' with color (%.2f, %.2f, %.2f)"),
		*MaterialName, R, G, B);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), MaterialName);
	Result->SetStringField(TEXT("path"), MaterialPackagePath + TEXT(".") + MaterialName);
	Result->SetBoolField(TEXT("success"), true);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// Set Blueprint Parent Class
// ============================================================================

UClass* FSetBlueprintParentClassAction::ResolveClass(const FString& ClassName) const
{
	// Built-in name mapping
	static TMap<FString, FString> ClassMap = {
		{TEXT("Actor"), TEXT("/Script/Engine.Actor")},
		{TEXT("Pawn"), TEXT("/Script/Engine.Pawn")},
		{TEXT("Character"), TEXT("/Script/Engine.Character")},
		{TEXT("PlayerController"), TEXT("/Script/Engine.PlayerController")},
		{TEXT("GameModeBase"), TEXT("/Script/Engine.GameModeBase")},
		{TEXT("GameStateBase"), TEXT("/Script/Engine.GameStateBase")},
		{TEXT("HUD"), TEXT("/Script/Engine.HUD")},
		{TEXT("ActorComponent"), TEXT("/Script/Engine.ActorComponent")},
		{TEXT("SceneComponent"), TEXT("/Script/Engine.SceneComponent")},
		{TEXT("GameInstance"), TEXT("/Script/Engine.GameInstance")},
	};

	// Try built-in mapping first
	if (const FString* FullPath = ClassMap.Find(ClassName))
	{
		UClass* FoundClass = FindObject<UClass>(nullptr, **FullPath);
		if (FoundClass) return FoundClass;
	}

	// Try as full path
	UClass* FoundClass = FindObject<UClass>(nullptr, *ClassName);
	if (FoundClass) return FoundClass;

	// Try common prefixes
	FString WithPrefix = FString::Printf(TEXT("/Script/Engine.%s"), *ClassName);
	FoundClass = FindObject<UClass>(nullptr, *WithPrefix);
	if (FoundClass) return FoundClass;

	WithPrefix = FString::Printf(TEXT("/Script/GameplayAbilities.%s"), *ClassName);
	FoundClass = FindObject<UClass>(nullptr, *WithPrefix);

	return FoundClass;
}

bool FSetBlueprintParentClassAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ParentClass;
	if (!GetRequiredString(Params, TEXT("parent_class"), ParentClass, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FSetBlueprintParentClassAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ParentClassName = Params->GetStringField(TEXT("parent_class"));
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	UClass* NewParent = ResolveClass(ParentClassName);
	if (!NewParent)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Could not resolve parent class '%s'"), *ParentClassName));
	}

	// Cannot reparent to itself or its child
	if (Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(NewParent) && Blueprint->ParentClass == NewParent)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Blueprint already has parent class '%s'"), *ParentClassName));
	}

	Blueprint->ParentClass = NewParent;
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("parent_class"), NewParent->GetName());
	ResultData->SetStringField(TEXT("parent_class_path"), NewParent->GetPathName());
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Add Blueprint Interface
// ============================================================================

bool FAddBlueprintInterfaceAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString InterfaceName;
	if (!GetRequiredString(Params, TEXT("interface_name"), InterfaceName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FAddBlueprintInterfaceAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString InterfaceName = Params->GetStringField(TEXT("interface_name"));
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Find the interface class
	UClass* InterfaceClass = FindObject<UClass>(nullptr, *InterfaceName);
	if (!InterfaceClass)
	{
		// Try common prefix
		FString WithPrefix = FString::Printf(TEXT("/Script/Engine.%s"), *InterfaceName);
		InterfaceClass = FindObject<UClass>(nullptr, *WithPrefix);
	}
	if (!InterfaceClass)
	{
		// Try loading by asset path (for Blueprint Interfaces like /Game/...)
		UObject* LoadedObj = StaticLoadObject(UClass::StaticClass(), nullptr, *InterfaceName);
		InterfaceClass = Cast<UClass>(LoadedObj);
		if (!InterfaceClass)
		{
			// Try loading as a Blueprint interface
			UBlueprint* InterfaceBP = LoadObject<UBlueprint>(nullptr, *InterfaceName);
			if (InterfaceBP && InterfaceBP->GeneratedClass)
			{
				InterfaceClass = InterfaceBP->GeneratedClass;
			}
		}
	}
	if (!InterfaceClass || !InterfaceClass->IsChildOf(UInterface::StaticClass()))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Could not find interface '%s'"), *InterfaceName));
	}

	// Check if already implemented
	for (const FBPInterfaceDescription& Desc : Blueprint->ImplementedInterfaces)
	{
		if (Desc.Interface == InterfaceClass)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Interface '%s' is already implemented"), *InterfaceName));
		}
	}

	FBlueprintEditorUtils::ImplementNewInterface(Blueprint, FTopLevelAssetPath(InterfaceClass->GetPathName()));
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("interface_name"), InterfaceClass->GetName());
	ResultData->SetStringField(TEXT("interface_path"), InterfaceClass->GetPathName());
	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// Remove Blueprint Interface
// ============================================================================

bool FRemoveBlueprintInterfaceAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString InterfaceName;
	if (!GetRequiredString(Params, TEXT("interface_name"), InterfaceName, OutError)) return false;
	return ValidateBlueprint(Params, Context, OutError);
}

TSharedPtr<FJsonObject> FRemoveBlueprintInterfaceAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString InterfaceName = Params->GetStringField(TEXT("interface_name"));
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);

	// Find the interface in implemented list
	UClass* InterfaceClass = nullptr;
	for (const FBPInterfaceDescription& Desc : Blueprint->ImplementedInterfaces)
	{
		if (Desc.Interface && (Desc.Interface->GetName() == InterfaceName || Desc.Interface->GetPathName() == InterfaceName))
		{
			InterfaceClass = Desc.Interface;
			break;
		}
	}

	if (!InterfaceClass)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Interface '%s' is not implemented by this Blueprint"), *InterfaceName));
	}

	FBlueprintEditorUtils::RemoveInterface(Blueprint, FTopLevelAssetPath(InterfaceClass->GetPathName()));
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("removed_interface"), InterfaceClass->GetName());
	return CreateSuccessResponse(ResultData);
}