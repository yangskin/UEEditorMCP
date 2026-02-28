// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/EditorActions.h"
#include "MCPCommonUtils.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "Kismet/GameplayStatics.h"
#include "FileHelpers.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "AssetSelection.h"
#include "ObjectTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"
#include "ImageUtils.h"
#include "Misc/Base64.h"
#include "Misc/PackageName.h"
#include "MCPLogCapture.h"
#include "MCPBridge.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/UnrealType.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformMisc.h"
#include "Misc/DateTime.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "EngineUtils.h"
#include "Selection.h"


// Helper to find actor by name
static AActor* FindActorByName(UWorld* World, const FString& ActorName)
{
	if (!World) return nullptr;

	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);

	for (AActor* Actor : AllActors)
	{
		if (Actor && Actor->GetName() == ActorName)
		{
			return Actor;
		}
	}
	return nullptr;
}


// ============================================================================
// FGetActorsInLevelAction
// ============================================================================

TSharedPtr<FJsonObject> FGetActorsInLevelAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);

	TArray<TSharedPtr<FJsonValue>> ActorArray;
	for (AActor* Actor : AllActors)
	{
		if (Actor)
		{
			ActorArray.Add(FMCPCommonUtils::ActorToJsonValue(Actor));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("actors"), ActorArray);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// FFindActorsByNameAction
// ============================================================================

bool FFindActorsByNameAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Pattern;
	return GetRequiredString(Params, TEXT("pattern"), Pattern, OutError);
}

TSharedPtr<FJsonObject> FFindActorsByNameAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Pattern, Error;
	GetRequiredString(Params, TEXT("pattern"), Pattern, Error);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);

	TArray<TSharedPtr<FJsonValue>> MatchingActors;
	for (AActor* Actor : AllActors)
	{
		if (Actor && Actor->GetName().Contains(Pattern))
		{
			MatchingActors.Add(FMCPCommonUtils::ActorToJsonValue(Actor));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("actors"), MatchingActors);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// FSpawnActorAction
// ============================================================================

bool FSpawnActorAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name, Type;
	if (!GetRequiredString(Params, TEXT("name"), Name, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("type"), Type, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FSpawnActorAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ActorName, ActorType, Error;
	GetRequiredString(Params, TEXT("name"), ActorName, Error);
	GetRequiredString(Params, TEXT("type"), ActorType, Error);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	// Resolve actor class
	UClass* ActorClass = ResolveActorClass(ActorType);
	if (!ActorClass)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Unknown actor type: %s"), *ActorType),
			TEXT("invalid_type")
		);
	}

	// Delete existing actor with same name
	AActor* Existing = FindActorByName(World, ActorName);
	if (Existing)
	{
		World->DestroyActor(Existing);
	}

	// Parse transform
	FVector Location = FMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
	FRotator Rotation = FMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
	FVector Scale = Params->HasField(TEXT("scale")) ?
		FMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")) : FVector(1, 1, 1);

	// Spawn
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(*ActorName);
	SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;

	AActor* NewActor = World->SpawnActor<AActor>(ActorClass, Location, Rotation, SpawnParams);
	if (!NewActor)
	{
		return CreateErrorResponse(TEXT("Failed to spawn actor"), TEXT("spawn_failed"));
	}

	NewActor->SetActorScale3D(Scale);
	NewActor->SetActorLabel(*ActorName);
	Context.LastCreatedActorName = ActorName;

	// Mark level dirty so auto-save works
	Context.MarkPackageDirty(World->GetOutermost());

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Spawned actor '%s' of type '%s'"), *ActorName, *ActorType);

	return CreateSuccessResponse(FMCPCommonUtils::ActorToJsonObject(NewActor));
}

UClass* FSpawnActorAction::ResolveActorClass(const FString& TypeName) const
{
	if (TypeName == TEXT("StaticMeshActor")) return AStaticMeshActor::StaticClass();
	if (TypeName == TEXT("PointLight")) return APointLight::StaticClass();
	if (TypeName == TEXT("SpotLight")) return ASpotLight::StaticClass();
	if (TypeName == TEXT("DirectionalLight")) return ADirectionalLight::StaticClass();
	if (TypeName == TEXT("CameraActor")) return ACameraActor::StaticClass();
	if (TypeName == TEXT("Actor")) return AActor::StaticClass();
	return nullptr;
}


// ============================================================================
// FDeleteActorAction
// ============================================================================

bool FDeleteActorAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	return GetRequiredString(Params, TEXT("name"), Name, OutError);
}

TSharedPtr<FJsonObject> FDeleteActorAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ActorName, Error;
	GetRequiredString(Params, TEXT("name"), ActorName, Error);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	AActor* Actor = FindActorByName(World, ActorName);
	if (!Actor)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Actor not found: %s"), *ActorName),
			TEXT("not_found")
		);
	}

	// Store info before deletion
	TSharedPtr<FJsonObject> ActorInfo = FMCPCommonUtils::ActorToJsonObject(Actor);

	// Use editor-proper destruction which handles World Partition external actors
	bool bDestroyed = World->EditorDestroyActor(Actor, true);
	if (!bDestroyed)
	{
		// Fallback to regular destroy
		Actor->Destroy();
	}

	// Mark world dirty
	Context.MarkPackageDirty(World->GetOutermost());

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Deleted actor '%s'"), *ActorName);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetObjectField(TEXT("deleted_actor"), ActorInfo);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// FSetActorTransformAction
// ============================================================================

bool FSetActorTransformAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	return GetRequiredString(Params, TEXT("name"), Name, OutError);
}

TSharedPtr<FJsonObject> FSetActorTransformAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ActorName, Error;
	GetRequiredString(Params, TEXT("name"), ActorName, Error);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	AActor* Actor = FindActorByName(World, ActorName);
	if (!Actor)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Actor not found: %s"), *ActorName),
			TEXT("not_found")
		);
	}

	// Update transform
	FTransform Transform = Actor->GetTransform();

	if (Params->HasField(TEXT("location")))
	{
		Transform.SetLocation(FMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
	}
	if (Params->HasField(TEXT("rotation")))
	{
		Transform.SetRotation(FQuat(FMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"))));
	}
	if (Params->HasField(TEXT("scale")))
	{
		Transform.SetScale3D(FMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
	}

	Actor->SetActorTransform(Transform);

	// Mark level dirty so auto-save works
	Context.MarkPackageDirty(World->GetOutermost());

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Set transform on actor '%s'"), *ActorName);

	return CreateSuccessResponse(FMCPCommonUtils::ActorToJsonObject(Actor));
}


// ============================================================================
// FGetActorPropertiesAction
// ============================================================================

bool FGetActorPropertiesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	return GetRequiredString(Params, TEXT("name"), Name, OutError);
}

// --- Property value serialization helper ---
TSharedPtr<FJsonValue> FGetActorPropertiesAction::PropertyValueToJson(FProperty* Property, const void* ValuePtr)
{
	if (!Property || !ValuePtr)
	{
		return MakeShared<FJsonValueNull>();
	}

	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		return MakeShared<FJsonValueBoolean>(BoolProp->GetPropertyValue(ValuePtr));
	}
	else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
	{
		return MakeShared<FJsonValueNumber>(IntProp->GetPropertyValue(ValuePtr));
	}
	else if (FInt64Property* Int64Prop = CastField<FInt64Property>(Property))
	{
		return MakeShared<FJsonValueNumber>(static_cast<double>(Int64Prop->GetPropertyValue(ValuePtr)));
	}
	else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
	{
		return MakeShared<FJsonValueNumber>(FloatProp->GetPropertyValue(ValuePtr));
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
	{
		return MakeShared<FJsonValueNumber>(DoubleProp->GetPropertyValue(ValuePtr));
	}
	else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		uint8 Val = ByteProp->GetPropertyValue(ValuePtr);
		UEnum* EnumDef = ByteProp->Enum;
		if (EnumDef)
		{
			FString EnumName = EnumDef->GetNameStringByValue(Val);
			return MakeShared<FJsonValueString>(EnumName);
		}
		return MakeShared<FJsonValueNumber>(Val);
	}
	else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		UEnum* EnumDef = EnumProp->GetEnum();
		FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
		if (EnumDef && UnderlyingProp)
		{
			int64 Val = UnderlyingProp->GetSignedIntPropertyValue(ValuePtr);
			FString EnumName = EnumDef->GetNameStringByValue(Val);
			return MakeShared<FJsonValueString>(EnumName);
		}
		return MakeShared<FJsonValueNull>();
	}
	else if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		return MakeShared<FJsonValueString>(StrProp->GetPropertyValue(ValuePtr));
	}
	else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		return MakeShared<FJsonValueString>(NameProp->GetPropertyValue(ValuePtr).ToString());
	}
	else if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		return MakeShared<FJsonValueString>(TextProp->GetPropertyValue(ValuePtr).ToString());
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		if (StructProp->Struct == TBaseStructure<FVector>::Get())
		{
			const FVector& Vec = *(const FVector*)ValuePtr;
			TArray<TSharedPtr<FJsonValue>> Arr;
			Arr.Add(MakeShared<FJsonValueNumber>(Vec.X));
			Arr.Add(MakeShared<FJsonValueNumber>(Vec.Y));
			Arr.Add(MakeShared<FJsonValueNumber>(Vec.Z));
			return MakeShared<FJsonValueArray>(Arr);
		}
		else if (StructProp->Struct == TBaseStructure<FRotator>::Get())
		{
			const FRotator& Rot = *(const FRotator*)ValuePtr;
			TArray<TSharedPtr<FJsonValue>> Arr;
			Arr.Add(MakeShared<FJsonValueNumber>(Rot.Pitch));
			Arr.Add(MakeShared<FJsonValueNumber>(Rot.Yaw));
			Arr.Add(MakeShared<FJsonValueNumber>(Rot.Roll));
			return MakeShared<FJsonValueArray>(Arr);
		}
		else if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
		{
			const FLinearColor& Color = *(const FLinearColor*)ValuePtr;
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("r"), Color.R);
			Obj->SetNumberField(TEXT("g"), Color.G);
			Obj->SetNumberField(TEXT("b"), Color.B);
			Obj->SetNumberField(TEXT("a"), Color.A);
			return MakeShared<FJsonValueObject>(Obj);
		}
		else if (StructProp->Struct == TBaseStructure<FColor>::Get())
		{
			const FColor& Color = *(const FColor*)ValuePtr;
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("r"), Color.R);
			Obj->SetNumberField(TEXT("g"), Color.G);
			Obj->SetNumberField(TEXT("b"), Color.B);
			Obj->SetNumberField(TEXT("a"), Color.A);
			return MakeShared<FJsonValueObject>(Obj);
		}
		else if (StructProp->Struct == TBaseStructure<FTransform>::Get())
		{
			const FTransform& Transform = *(const FTransform*)ValuePtr;
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			FVector Loc = Transform.GetLocation();
			FRotator Rot = Transform.Rotator();
			FVector Scale = Transform.GetScale3D();

			TArray<TSharedPtr<FJsonValue>> LocArr, RotArr, ScaleArr;
			LocArr.Add(MakeShared<FJsonValueNumber>(Loc.X));
			LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Y));
			LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Z));
			RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Pitch));
			RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Yaw));
			RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Roll));
			ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.X));
			ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.Y));
			ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.Z));

			Obj->SetArrayField(TEXT("location"), LocArr);
			Obj->SetArrayField(TEXT("rotation"), RotArr);
			Obj->SetArrayField(TEXT("scale"), ScaleArr);
			return MakeShared<FJsonValueObject>(Obj);
		}
		else
		{
			// Generic struct: use ExportText
			FString ExportedValue;
			StructProp->ExportTextItem_Direct(ExportedValue, ValuePtr, nullptr, nullptr, PPF_None);
			return MakeShared<FJsonValueString>(ExportedValue);
		}
	}
	else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
	{
		UObject* ObjValue = ObjProp->GetObjectPropertyValue(ValuePtr);
		if (ObjValue)
		{
			return MakeShared<FJsonValueString>(ObjValue->GetPathName());
		}
		return MakeShared<FJsonValueString>(TEXT("None"));
	}
	else if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
	{
		UClass* ClassValue = Cast<UClass>(ClassProp->GetObjectPropertyValue(ValuePtr));
		if (ClassValue)
		{
			return MakeShared<FJsonValueString>(ClassValue->GetPathName());
		}
		return MakeShared<FJsonValueString>(TEXT("None"));
	}
	else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper ArrayHelper(ArrayProp, ValuePtr);
		TArray<TSharedPtr<FJsonValue>> Arr;
		int32 Count = FMath::Min(ArrayHelper.Num(), 100); // Cap at 100 elements
		for (int32 i = 0; i < Count; ++i)
		{
			Arr.Add(PropertyValueToJson(ArrayProp->Inner, ArrayHelper.GetRawPtr(i)));
		}
		return MakeShared<FJsonValueArray>(Arr);
	}

	// Fallback: use ExportText
	FString ExportedValue;
	Property->ExportTextItem_Direct(ExportedValue, ValuePtr, nullptr, nullptr, PPF_None);
	return MakeShared<FJsonValueString>(ExportedValue);
}

// --- Property type string helper ---
FString FGetActorPropertiesAction::GetPropertyTypeString(FProperty* Property)
{
	if (!Property) return TEXT("Unknown");

	if (CastField<FBoolProperty>(Property)) return TEXT("Boolean");
	if (CastField<FIntProperty>(Property)) return TEXT("Int32");
	if (CastField<FInt64Property>(Property)) return TEXT("Int64");
	if (CastField<FFloatProperty>(Property)) return TEXT("Float");
	if (CastField<FDoubleProperty>(Property)) return TEXT("Double");
	if (CastField<FStrProperty>(Property)) return TEXT("String");
	if (CastField<FNameProperty>(Property)) return TEXT("Name");
	if (CastField<FTextProperty>(Property)) return TEXT("Text");

	if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (ByteProp->Enum) return FString::Printf(TEXT("Enum(%s)"), *ByteProp->Enum->GetName());
		return TEXT("Byte");
	}
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		if (EnumProp->GetEnum()) return FString::Printf(TEXT("Enum(%s)"), *EnumProp->GetEnum()->GetName());
		return TEXT("Enum");
	}
	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		return FString::Printf(TEXT("Struct(%s)"), *StructProp->Struct->GetName());
	}
	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
	{
		return FString::Printf(TEXT("Object(%s)"), *ObjProp->PropertyClass->GetName());
	}
	if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
	{
		return FString::Printf(TEXT("Class(%s)"), *ClassProp->MetaClass->GetName());
	}
	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		return FString::Printf(TEXT("Array(%s)"), *GetPropertyTypeString(ArrayProp->Inner));
	}
	if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
	{
		return FString::Printf(TEXT("Set(%s)"), *GetPropertyTypeString(SetProp->ElementProp));
	}
	if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		return FString::Printf(TEXT("Map(%s, %s)"),
			*GetPropertyTypeString(MapProp->KeyProp),
			*GetPropertyTypeString(MapProp->ValueProp));
	}
	if (CastField<FSoftObjectProperty>(Property)) return TEXT("SoftObjectReference");
	if (CastField<FSoftClassProperty>(Property)) return TEXT("SoftClassReference");
	if (CastField<FWeakObjectProperty>(Property)) return TEXT("WeakObjectReference");
	if (CastField<FInterfaceProperty>(Property)) return TEXT("Interface");
	if (CastField<FDelegateProperty>(Property)) return TEXT("Delegate");
	if (CastField<FMulticastDelegateProperty>(Property)) return TEXT("MulticastDelegate");

	return Property->GetCPPType();
}

TSharedPtr<FJsonObject> FGetActorPropertiesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ActorName, Error;
	GetRequiredString(Params, TEXT("name"), ActorName, Error);

	const bool bDetailed = GetOptionalBool(Params, TEXT("detailed"), false);
	const bool bEditableOnly = GetOptionalBool(Params, TEXT("editable_only"), false);
	const FString CategoryFilter = GetOptionalString(Params, TEXT("category"));

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	AActor* Actor = FindActorByName(World, ActorName);
	if (!Actor)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Actor not found: %s"), *ActorName),
			TEXT("not_found")
		);
	}

	// Always include basic info (backward compatible)
	TSharedPtr<FJsonObject> Result = FMCPCommonUtils::ActorToJsonObject(Actor);

	if (bDetailed)
	{
		// Determine if this is a Blueprint-generated actor
		UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(Actor->GetClass());
		if (BPGC)
		{
			Result->SetBoolField(TEXT("is_blueprint_actor"), true);
			if (BPGC->ClassGeneratedBy)
			{
				Result->SetStringField(TEXT("blueprint_name"), BPGC->ClassGeneratedBy->GetName());
				Result->SetStringField(TEXT("blueprint_path"), BPGC->ClassGeneratedBy->GetPathName());
			}
		}
		else
		{
			Result->SetBoolField(TEXT("is_blueprint_actor"), false);
		}

		// Enumerate ALL FProperty fields on the actor's class
		TArray<TSharedPtr<FJsonValue>> PropertiesArray;
		UClass* ActorClass = Actor->GetClass();

		// Determine where native AActor properties end (to flag blueprint variables)
		UClass* NativeStopClass = AActor::StaticClass();

		for (TFieldIterator<FProperty> PropIt(ActorClass); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;
			if (!Property) continue;

			// Filter: editable_only
			const bool bIsEditable = Property->HasAnyPropertyFlags(CPF_Edit);
			if (bEditableOnly && !bIsEditable) continue;

			// Filter: category
			FString PropCategory = Property->GetMetaData(TEXT("Category"));
			if (!CategoryFilter.IsEmpty() && !PropCategory.Contains(CategoryFilter)) continue;

			// Skip deprecated and transient unless explicitly asked
			if (Property->HasAnyPropertyFlags(CPF_Deprecated)) continue;

			// Determine if this is a Blueprint variable (defined in Generated class, not in native)
			bool bIsBlueprintVar = false;
			UClass* OwnerClass = Property->GetOwnerClass();
			if (OwnerClass && BPGC)
			{
				bIsBlueprintVar = OwnerClass->IsChildOf(BPGC) || OwnerClass == BPGC;
				// Also check if owner is any BP generated class in the hierarchy
				if (!bIsBlueprintVar)
				{
					bIsBlueprintVar = (Cast<UBlueprintGeneratedClass>(OwnerClass) != nullptr);
				}
			}

			TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
			PropObj->SetStringField(TEXT("name"), Property->GetName());
			PropObj->SetStringField(TEXT("type"), GetPropertyTypeString(Property));
			PropObj->SetBoolField(TEXT("is_editable"), bIsEditable);
			PropObj->SetBoolField(TEXT("is_blueprint_variable"), bIsBlueprintVar);

			if (!PropCategory.IsEmpty())
			{
				PropObj->SetStringField(TEXT("category"), PropCategory);
			}

			// Property flags of interest
			PropObj->SetBoolField(TEXT("is_visible_in_defaults"), Property->HasAnyPropertyFlags(CPF_Edit | CPF_EditConst));
			PropObj->SetBoolField(TEXT("is_read_only"), Property->HasAnyPropertyFlags(CPF_EditConst));
			PropObj->SetBoolField(TEXT("is_blueprint_visible"), Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
			PropObj->SetBoolField(TEXT("is_expose_on_spawn"), Property->HasAnyPropertyFlags(CPF_ExposeOnSpawn));
			PropObj->SetBoolField(TEXT("is_replicated"), Property->HasAnyPropertyFlags(CPF_Net));

			if (OwnerClass)
			{
				PropObj->SetStringField(TEXT("owner_class"), OwnerClass->GetName());
			}

			// Serialize the value
			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Actor);
			PropObj->SetField(TEXT("value"), PropertyValueToJson(Property, ValuePtr));

			PropertiesArray.Add(MakeShared<FJsonValueObject>(PropObj));
		}

		Result->SetArrayField(TEXT("properties"), PropertiesArray);
		Result->SetNumberField(TEXT("property_count"), PropertiesArray.Num());
	}

	return CreateSuccessResponse(Result);
}


// ============================================================================
// FSetActorPropertyAction
// ============================================================================

bool FSetActorPropertyAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name, PropertyName;
	if (!GetRequiredString(Params, TEXT("name"), Name, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("property_name"), PropertyName, OutError)) return false;
	if (!Params->HasField(TEXT("property_value")))
	{
		OutError = TEXT("Missing 'property_value' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FSetActorPropertyAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ActorName, PropertyName, Error;
	GetRequiredString(Params, TEXT("name"), ActorName, Error);
	GetRequiredString(Params, TEXT("property_name"), PropertyName, Error);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	AActor* Actor = FindActorByName(World, ActorName);
	if (!Actor)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Actor not found: %s"), *ActorName),
			TEXT("not_found")
		);
	}

	// Look up the property - this works for BOTH native C++ and Blueprint-generated properties
	// because Blueprint variables are compiled into the UBlueprintGeneratedClass as FProperty
	FProperty* Property = Actor->GetClass()->FindPropertyByName(*PropertyName);
	if (!Property)
	{
		// Also try case-insensitive search
		for (TFieldIterator<FProperty> PropIt(Actor->GetClass()); PropIt; ++PropIt)
		{
			if (PropIt->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
			{
				Property = *PropIt;
				break;
			}
		}
	}

	if (!Property)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Property '%s' not found on actor '%s' (class: %s)"), *PropertyName, *ActorName, *Actor->GetClass()->GetName()),
			TEXT("property_not_found")
		);
	}

	TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));

	FString ErrorMessage;
	if (!FMCPCommonUtils::SetObjectProperty(Actor, Property->GetName(), JsonValue, ErrorMessage))
	{
		return CreateErrorResponse(ErrorMessage, TEXT("property_set_failed"));
	}

	// Mark level dirty so auto-save works
	Context.MarkPackageDirty(World->GetOutermost());

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Set property '%s' on actor '%s'"), *PropertyName, *ActorName);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("actor"), ActorName);
	Result->SetStringField(TEXT("property"), Property->GetName());
	Result->SetStringField(TEXT("property_type"), Property->GetCPPType());
	Result->SetBoolField(TEXT("success"), true);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// FFocusViewportAction
// ============================================================================

bool FFocusViewportAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	bool HasTarget = Params->HasField(TEXT("target"));
	bool HasLocation = Params->HasField(TEXT("location"));

	if (!HasTarget && !HasLocation)
	{
		OutError = TEXT("Either 'target' or 'location' must be provided");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FFocusViewportAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FLevelEditorViewportClient* ViewportClient = nullptr;
	if (GEditor && GEditor->GetActiveViewport())
	{
		ViewportClient = (FLevelEditorViewportClient*)GEditor->GetActiveViewport()->GetClient();
	}

	if (!ViewportClient)
	{
		return CreateErrorResponse(TEXT("Failed to get active viewport"), TEXT("no_viewport"));
	}

	float Distance = GetOptionalNumber(Params, TEXT("distance"), 1000.0f);
	FVector TargetLocation(0, 0, 0);

	if (Params->HasField(TEXT("target")))
	{
		FString TargetActorName = Params->GetStringField(TEXT("target"));
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		AActor* TargetActor = FindActorByName(World, TargetActorName);

		if (!TargetActor)
		{
			return CreateErrorResponse(
				FString::Printf(TEXT("Actor not found: %s"), *TargetActorName),
				TEXT("not_found")
			);
		}
		TargetLocation = TargetActor->GetActorLocation();
	}
	else
	{
		TargetLocation = FMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
	}

	ViewportClient->SetViewLocation(TargetLocation - FVector(Distance, 0, 0));

	if (Params->HasField(TEXT("orientation")))
	{
		ViewportClient->SetViewRotation(FMCPCommonUtils::GetRotatorFromJson(Params, TEXT("orientation")));
	}

	ViewportClient->Invalidate();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// FGetViewportTransformAction
// ============================================================================

TSharedPtr<FJsonObject> FGetViewportTransformAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FLevelEditorViewportClient* ViewportClient = nullptr;
	if (GEditor && GEditor->GetActiveViewport())
	{
		ViewportClient = (FLevelEditorViewportClient*)GEditor->GetActiveViewport()->GetClient();
	}

	if (!ViewportClient)
	{
		return CreateErrorResponse(TEXT("Failed to get active viewport"), TEXT("no_viewport"));
	}

	FVector Location = ViewportClient->GetViewLocation();
	FRotator Rotation = ViewportClient->GetViewRotation();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> LocationArray;
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.X));
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Y));
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Z));
	Result->SetArrayField(TEXT("location"), LocationArray);

	TArray<TSharedPtr<FJsonValue>> RotationArray;
	RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Pitch));
	RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Yaw));
	RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Roll));
	Result->SetArrayField(TEXT("rotation"), RotationArray);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// FSetViewportTransformAction
// ============================================================================

bool FSetViewportTransformAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("location")) && !Params->HasField(TEXT("rotation")))
	{
		OutError = TEXT("At least 'location' or 'rotation' must be provided");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FSetViewportTransformAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FLevelEditorViewportClient* ViewportClient = nullptr;
	if (GEditor && GEditor->GetActiveViewport())
	{
		ViewportClient = (FLevelEditorViewportClient*)GEditor->GetActiveViewport()->GetClient();
	}

	if (!ViewportClient)
	{
		return CreateErrorResponse(TEXT("Failed to get active viewport"), TEXT("no_viewport"));
	}

	if (Params->HasField(TEXT("location")))
	{
		ViewportClient->SetViewLocation(FMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
	}
	if (Params->HasField(TEXT("rotation")))
	{
		ViewportClient->SetViewRotation(FMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation")));
	}

	ViewportClient->Invalidate();

	// Return new state
	FVector Location = ViewportClient->GetViewLocation();
	FRotator Rotation = ViewportClient->GetViewRotation();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> LocationArray;
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.X));
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Y));
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Z));
	Result->SetArrayField(TEXT("location"), LocationArray);

	TArray<TSharedPtr<FJsonValue>> RotationArray;
	RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Pitch));
	RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Yaw));
	RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Roll));
	Result->SetArrayField(TEXT("rotation"), RotationArray);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// FSaveAllAction
// ============================================================================

TSharedPtr<FJsonObject> FSaveAllAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	bool bOnlyMaps = GetOptionalBool(Params, TEXT("only_maps"), false);

	int32 SavedCount = 0;
	TArray<FString> SavedPackages;

	if (bOnlyMaps)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (World)
		{
			UPackage* WorldPackage = World->GetOutermost();
			if (WorldPackage && WorldPackage->IsDirty())
			{
				FString PackageFilename;
				if (FPackageName::TryConvertLongPackageNameToFilename(
					WorldPackage->GetName(), PackageFilename, FPackageName::GetMapPackageExtension()))
				{
					FSavePackageArgs SaveArgs;
					SaveArgs.TopLevelFlags = RF_Standalone;
					if (UPackage::SavePackage(WorldPackage, World, *PackageFilename, SaveArgs))
					{
						SavedCount++;
						SavedPackages.Add(WorldPackage->GetName());
					}
				}
			}
		}
	}
	else
	{
		TArray<UPackage*> DirtyPackages;
		FEditorFileUtils::GetDirtyPackages(DirtyPackages);

		for (UPackage* Package : DirtyPackages)
		{
			if (!Package) continue;

			FString PackageFilename;
			FString PackageName = Package->GetName();
			bool bIsMap = Package->ContainsMap();
			FString Extension = bIsMap ?
				FPackageName::GetMapPackageExtension() :
				FPackageName::GetAssetPackageExtension();

			if (FPackageName::TryConvertLongPackageNameToFilename(PackageName, PackageFilename, Extension))
			{
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = RF_Standalone;

				UObject* AssetToSave = bIsMap ? Package->FindAssetInPackage() : nullptr;

				if (UPackage::SavePackage(Package, AssetToSave, *PackageFilename, SaveArgs))
				{
					SavedCount++;
					SavedPackages.Add(PackageName);
					UE_LOG(LogMCP, Log, TEXT("UEEditorMCP SaveAll: Saved %s"), *PackageName);
				}
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("saved_count"), SavedCount);

	TArray<TSharedPtr<FJsonValue>> PackagesArray;
	for (const FString& PkgName : SavedPackages)
	{
		PackagesArray.Add(MakeShared<FJsonValueString>(PkgName));
	}
	Result->SetArrayField(TEXT("saved_packages"), PackagesArray);

	return CreateSuccessResponse(Result);
}


// ========================================================================
// FListAssetsAction
// ========================================================================

bool FListAssetsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("path")))
	{
		OutError = TEXT("Missing required 'path' parameter (e.g. /Game/UI)");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FListAssetsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Path = Params->GetStringField(TEXT("path"));
	bool bRecursive = true;
	if (Params->HasField(TEXT("recursive")))
	{
		bRecursive = Params->GetBoolField(TEXT("recursive"));
	}
	FString ClassFilter;
	if (Params->HasField(TEXT("class_filter")))
	{
		ClassFilter = Params->GetStringField(TEXT("class_filter"));
	}
	FString NameContains;
	if (Params->HasField(TEXT("name_contains")))
	{
		NameContains = Params->GetStringField(TEXT("name_contains"));
	}
	int32 MaxResults = 500;
	if (Params->HasField(TEXT("max_results")))
	{
		MaxResults = static_cast<int32>(Params->GetNumberField(TEXT("max_results")));
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Path));
	Filter.bRecursivePaths = bRecursive;

	// Apply class filter if specified
	if (!ClassFilter.IsEmpty())
	{
		// Support common short names
		if (ClassFilter == TEXT("Blueprint") || ClassFilter == TEXT("UBlueprint"))
		{
			Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
			Filter.bRecursiveClasses = true;
		}
		else
		{
			// Try to find the class by name
			UClass* FilterClass = FindFirstObject<UClass>(*ClassFilter, EFindFirstObjectOptions::ExactClass, ELogVerbosity::Warning, TEXT("FListAssetsAction"));
			if (!FilterClass)
			{
				// Try with U prefix
				FilterClass = FindFirstObject<UClass>(*(FString(TEXT("U")) + ClassFilter), EFindFirstObjectOptions::ExactClass, ELogVerbosity::Warning, TEXT("FListAssetsAction"));
			}
			if (FilterClass)
			{
				Filter.ClassPaths.Add(FilterClass->GetClassPathName());
				Filter.bRecursiveClasses = true;
			}
		}
	}

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssets(Filter, AssetList);

	TArray<TSharedPtr<FJsonValue>> AssetsArray;
	int32 Count = 0;

	for (const FAssetData& AssetData : AssetList)
	{
		if (Count >= MaxResults) break;

		FString AssetName = AssetData.AssetName.ToString();

		// Name filter
		if (!NameContains.IsEmpty() && !AssetName.Contains(NameContains))
		{
			continue;
		}

		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("asset_name"), AssetName);
		AssetObj->SetStringField(TEXT("asset_path"), AssetData.GetObjectPathString());
		AssetObj->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
		AssetObj->SetStringField(TEXT("asset_class"), AssetData.AssetClassPath.GetAssetName().ToString());

		AssetsArray.Add(MakeShared<FJsonValueObject>(AssetObj));
		Count++;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), Count);
	Result->SetNumberField(TEXT("total_unfiltered"), AssetList.Num());
	Result->SetStringField(TEXT("path"), Path);
	Result->SetArrayField(TEXT("assets"), AssetsArray);

	return CreateSuccessResponse(Result);
}


// ========================================================================
// FRenameAssetsAction
// ========================================================================

bool FRenameAssetsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	const bool bHasBatch = Params->HasField(TEXT("items"));
	const bool bHasSingle = Params->HasField(TEXT("old_asset_path"));

	if (!bHasBatch && !bHasSingle)
	{
		OutError = TEXT("Provide either 'items' for batch rename or single fields 'old_asset_path', 'new_package_path', 'new_name'");
		return false;
	}

	if (bHasBatch)
	{
		const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
		if (!Params->TryGetArrayField(TEXT("items"), ItemsArray) || !ItemsArray || ItemsArray->Num() == 0)
		{
			OutError = TEXT("'items' must be a non-empty array");
			return false;
		}

		for (int32 Index = 0; Index < ItemsArray->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& ItemValue = (*ItemsArray)[Index];
			if (!ItemValue.IsValid() || ItemValue->Type != EJson::Object)
			{
				OutError = FString::Printf(TEXT("items[%d] must be an object"), Index);
				return false;
			}

			const TSharedPtr<FJsonObject> ItemObject = ItemValue->AsObject();
			if (!ItemObject.IsValid())
			{
				OutError = FString::Printf(TEXT("items[%d] must be an object"), Index);
				return false;
			}

			FString OldAssetPath;
			FString NewPackagePath;
			FString NewName;
			if (!ItemObject->TryGetStringField(TEXT("old_asset_path"), OldAssetPath) || OldAssetPath.IsEmpty())
			{
				OutError = FString::Printf(TEXT("items[%d].old_asset_path is required"), Index);
				return false;
			}
			if (!ItemObject->TryGetStringField(TEXT("new_package_path"), NewPackagePath) || NewPackagePath.IsEmpty())
			{
				OutError = FString::Printf(TEXT("items[%d].new_package_path is required"), Index);
				return false;
			}
			if (!ItemObject->TryGetStringField(TEXT("new_name"), NewName) || NewName.IsEmpty())
			{
				OutError = FString::Printf(TEXT("items[%d].new_name is required"), Index);
				return false;
			}
		}

		return true;
	}

	FString OldAssetPath;
	FString NewPackagePath;
	FString NewName;
	if (!GetRequiredString(Params, TEXT("old_asset_path"), OldAssetPath, OutError))
	{
		return false;
	}
	if (!GetRequiredString(Params, TEXT("new_package_path"), NewPackagePath, OutError))
	{
		return false;
	}
	if (!GetRequiredString(Params, TEXT("new_name"), NewName, OutError))
	{
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FRenameAssetsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	struct FRenameRequestItem
	{
		FString OldAssetPath;
		FString NewPackagePath;
		FString NewName;
	};

	TArray<FRenameRequestItem> RequestItems;

	if (Params->HasField(TEXT("items")))
	{
		const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
		Params->TryGetArrayField(TEXT("items"), ItemsArray);
		if (ItemsArray)
		{
			for (const TSharedPtr<FJsonValue>& ItemValue : *ItemsArray)
			{
				if (!ItemValue.IsValid() || ItemValue->Type != EJson::Object)
				{
					continue;
				}

				const TSharedPtr<FJsonObject> ItemObject = ItemValue->AsObject();
				if (!ItemObject.IsValid())
				{
					continue;
				}

				FRenameRequestItem RequestItem;
				if (!ItemObject->TryGetStringField(TEXT("old_asset_path"), RequestItem.OldAssetPath))
				{
					continue;
				}
				if (!ItemObject->TryGetStringField(TEXT("new_package_path"), RequestItem.NewPackagePath))
				{
					continue;
				}
				if (!ItemObject->TryGetStringField(TEXT("new_name"), RequestItem.NewName))
				{
					continue;
				}

				RequestItems.Add(RequestItem);
			}
		}
	}
	else
	{
		FRenameRequestItem RequestItem;
		FString Error;
		GetRequiredString(Params, TEXT("old_asset_path"), RequestItem.OldAssetPath, Error);
		GetRequiredString(Params, TEXT("new_package_path"), RequestItem.NewPackagePath, Error);
		GetRequiredString(Params, TEXT("new_name"), RequestItem.NewName, Error);
		RequestItems.Add(RequestItem);
	}

	if (RequestItems.Num() == 0)
	{
		return CreateErrorResponse(TEXT("No valid rename items were provided"), TEXT("invalid_params"));
	}

	const bool bAutoFixupRedirectors = GetOptionalBool(Params, TEXT("auto_fixup_redirectors"), true);
	const bool bAllowUIPrompts = GetOptionalBool(Params, TEXT("allow_ui_prompts"), false);
	const bool bRequestedCheckoutDialogPrompt = GetOptionalBool(Params, TEXT("checkout_dialog_prompt"), false);
	const FString FixupModeRaw = GetOptionalString(Params, TEXT("fixup_mode"), TEXT("delete")).ToLower();

	ERedirectFixupMode FixupMode = ERedirectFixupMode::DeleteFixedUpRedirectors;
	if (FixupModeRaw == TEXT("leave"))
	{
		FixupMode = ERedirectFixupMode::LeaveFixedUpRedirectors;
	}
	else if (FixupModeRaw == TEXT("prompt"))
	{
		FixupMode = ERedirectFixupMode::PromptForDeletingRedirectors;
	}

	if (!bAllowUIPrompts && FixupMode == ERedirectFixupMode::PromptForDeletingRedirectors)
	{
		UE_LOG(LogMCP, Warning, TEXT("rename_assets: fixup_mode='prompt' requested, but allow_ui_prompts=false; forcing fixup_mode='delete' for non-interactive execution"));
		FixupMode = ERedirectFixupMode::DeleteFixedUpRedirectors;
	}

	const bool bEffectiveCheckoutDialogPrompt = bAllowUIPrompts && bRequestedCheckoutDialogPrompt;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	TArray<FAssetRenameData> RenameDataList;
	RenameDataList.Reserve(RequestItems.Num());

	TArray<FString> OldObjectPaths;
	OldObjectPaths.Reserve(RequestItems.Num());

	for (const FRenameRequestItem& RequestItem : RequestItems)
	{
		if (!FPackageName::IsValidLongPackageName(RequestItem.NewPackagePath))
		{
			return CreateErrorResponse(
				FString::Printf(TEXT("Invalid new_package_path: %s"), *RequestItem.NewPackagePath),
				TEXT("invalid_package_path")
			);
		}

		UObject* Asset = LoadObject<UObject>(nullptr, *RequestItem.OldAssetPath);
		if (!Asset)
		{
			return CreateErrorResponse(
				FString::Printf(TEXT("Asset not found: %s"), *RequestItem.OldAssetPath),
				TEXT("asset_not_found")
			);
		}

		OldObjectPaths.Add(Asset->GetPathName());
		RenameDataList.Emplace(Asset, RequestItem.NewPackagePath, RequestItem.NewName);
	}

	const bool bRenameSucceeded = AssetTools.RenameAssets(RenameDataList);
	if (!bRenameSucceeded)
	{
		return CreateErrorResponse(TEXT("RenameAssets failed. Check destination path/name conflicts and source-control state."), TEXT("rename_failed"));
	}

	int32 FoundRedirectorCount = 0;
	int32 FixedRedirectorCount = 0;
	int32 SilentlyDeletedRedirectorCount = 0;
	int32 KeptRedirectorCount = 0;
	TArray<UObjectRedirector*> RedirectorsToFix;
	TArray<TSharedPtr<FJsonValue>> KeptRedirectorsArray;

	if (bAutoFixupRedirectors)
	{
		for (const FString& OldObjectPath : OldObjectPaths)
		{
			UObject* LoadedObject = LoadObject<UObject>(nullptr, *OldObjectPath);
			UObjectRedirector* Redirector = Cast<UObjectRedirector>(LoadedObject);
			if (Redirector)
			{
				RedirectorsToFix.Add(Redirector);
				FoundRedirectorCount++;
			}
		}

		if (RedirectorsToFix.Num() > 0)
		{
			if (bAllowUIPrompts)
			{
				AssetTools.FixupReferencers(RedirectorsToFix, bEffectiveCheckoutDialogPrompt, FixupMode);
				FixedRedirectorCount = RedirectorsToFix.Num();
			}
			else
			{
				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
				IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

				for (const FString& OldObjectPath : OldObjectPaths)
				{
					const FString OldPackagePath = FPackageName::ObjectPathToPackageName(OldObjectPath);
					if (OldPackagePath.IsEmpty())
					{
						continue;
					}

					TArray<FName> Referencers;
					AssetRegistry.GetReferencers(FName(*OldPackagePath), Referencers);

					int32 ExternalReferencerCount = 0;
					for (const FName& Referencer : Referencers)
					{
						if (Referencer.ToString() != OldPackagePath)
						{
							ExternalReferencerCount++;
						}
					}

					if (ExternalReferencerCount == 0)
					{
						if (UEditorAssetLibrary::DoesAssetExist(OldPackagePath) && UEditorAssetLibrary::DeleteAsset(OldPackagePath))
						{
							SilentlyDeletedRedirectorCount++;
						}
						else
						{
							KeptRedirectorCount++;
							TSharedPtr<FJsonObject> KeptObj = MakeShared<FJsonObject>();
							KeptObj->SetStringField(TEXT("redirector_package"), OldPackagePath);
							KeptObj->SetStringField(TEXT("reason"), TEXT("delete_failed"));
							KeptRedirectorsArray.Add(MakeShared<FJsonValueObject>(KeptObj));
						}
					}
					else
					{
						KeptRedirectorCount++;
						TSharedPtr<FJsonObject> KeptObj = MakeShared<FJsonObject>();
						KeptObj->SetStringField(TEXT("redirector_package"), OldPackagePath);
						KeptObj->SetStringField(TEXT("reason"), TEXT("still_referenced"));
						KeptObj->SetNumberField(TEXT("referencer_count"), ExternalReferencerCount);
						KeptRedirectorsArray.Add(MakeShared<FJsonValueObject>(KeptObj));
					}
				}
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> RenamedItemsArray;
	for (const FAssetRenameData& RenameData : RenameDataList)
	{
		TSharedPtr<FJsonObject> ItemObject = MakeShared<FJsonObject>();
		ItemObject->SetStringField(TEXT("old_asset_path"), RenameData.OldObjectPath.ToString());
		ItemObject->SetStringField(TEXT("new_asset_path"), RenameData.NewObjectPath.ToString());
		ItemObject->SetStringField(TEXT("new_package_path"), RenameData.NewPackagePath);
		ItemObject->SetStringField(TEXT("new_name"), RenameData.NewName);
		RenamedItemsArray.Add(MakeShared<FJsonValueObject>(ItemObject));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("renamed_count"), RenameDataList.Num());
	Result->SetBoolField(TEXT("auto_fixup_redirectors"), bAutoFixupRedirectors);
	Result->SetBoolField(TEXT("allow_ui_prompts"), bAllowUIPrompts);
	Result->SetBoolField(TEXT("checkout_dialog_prompt_effective"), bEffectiveCheckoutDialogPrompt);
	Result->SetStringField(
		TEXT("fixup_mode_effective"),
		FixupMode == ERedirectFixupMode::LeaveFixedUpRedirectors
			? TEXT("leave")
			: (FixupMode == ERedirectFixupMode::PromptForDeletingRedirectors ? TEXT("prompt") : TEXT("delete"))
	);
	Result->SetNumberField(TEXT("redirectors_found"), FoundRedirectorCount);
	Result->SetNumberField(TEXT("redirectors_fixup_attempted"), FixedRedirectorCount);
	Result->SetNumberField(TEXT("redirectors_deleted_silently"), SilentlyDeletedRedirectorCount);
	Result->SetNumberField(TEXT("redirectors_kept"), KeptRedirectorCount);
	Result->SetArrayField(TEXT("kept_redirectors"), KeptRedirectorsArray);
	Result->SetArrayField(TEXT("renamed_items"), RenamedItemsArray);

	return CreateSuccessResponse(Result);
}


// ========================================================================
// FGetSelectedAssetThumbnailAction
// ========================================================================

bool FGetSelectedAssetThumbnailAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (Params->HasField(TEXT("size")))
	{
		const double SizeValue = Params->GetNumberField(TEXT("size"));
		if (SizeValue < 1.0)
		{
			OutError = TEXT("Parameter 'size' must be greater than 0");
			return false;
		}
	}

	auto ValidateStringArrayField = [&Params, &OutError](const TCHAR* FieldName) -> bool
	{
		if (!Params->HasField(FieldName))
		{
			return true;
		}

		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Params->TryGetArrayField(FieldName, Values) || !Values)
		{
			OutError = FString::Printf(TEXT("'%s' must be an array of strings"), FieldName);
			return false;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString Parsed;
			if (!Value.IsValid() || !Value->TryGetString(Parsed) || Parsed.IsEmpty())
			{
				OutError = FString::Printf(TEXT("'%s' must contain only non-empty strings"), FieldName);
				return false;
			}
		}

		return true;
	};

	if (!ValidateStringArrayField(TEXT("asset_paths"))) return false;
	if (!ValidateStringArrayField(TEXT("asset_ids"))) return false;
	if (!ValidateStringArrayField(TEXT("ids"))) return false;

	return true;
}

TSharedPtr<FJsonObject> FGetSelectedAssetThumbnailAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	const int32 RequestedSize = Params->HasField(TEXT("size"))
		? static_cast<int32>(Params->GetNumberField(TEXT("size")))
		: 256;
	const int32 ThumbnailSize = FMath::Clamp(RequestedSize, 1, 256);

	TArray<FString> TargetAssetPaths;

	auto CollectStringArrayField = [&Params, &TargetAssetPaths](const TCHAR* FieldName)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Params->TryGetArrayField(FieldName, Values) || !Values)
		{
			return;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString Parsed;
			if (Value.IsValid() && Value->TryGetString(Parsed) && !Parsed.IsEmpty())
			{
				TargetAssetPaths.Add(Parsed);
			}
		}
	};

	if (Params->HasField(TEXT("asset_path")))
	{
		const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
		if (!AssetPath.IsEmpty())
		{
			TargetAssetPaths.Add(AssetPath);
		}
	}

	CollectStringArrayField(TEXT("asset_paths"));
	CollectStringArrayField(TEXT("asset_ids"));
	CollectStringArrayField(TEXT("ids"));

	TArray<FString> UniqueTargetAssetPaths;
	for (const FString& PathItem : TargetAssetPaths)
	{
		if (!PathItem.IsEmpty())
		{
			UniqueTargetAssetPaths.AddUnique(PathItem);
		}
	}
	TargetAssetPaths = MoveTemp(UniqueTargetAssetPaths);

	bool bFromSelection = false;
	int32 SelectedCount = 0;

	if (TargetAssetPaths.IsEmpty())
	{
		TArray<FAssetData> SelectedAssets;
		AssetSelectionUtils::GetSelectedAssets(SelectedAssets);
		SelectedCount = SelectedAssets.Num();

		if (SelectedAssets.IsEmpty())
		{
			return CreateErrorResponse(
				TEXT("No selected asset found in Content Browser. Select assets or provide asset_path/asset_paths/asset_ids/ids."),
				TEXT("no_selection")
			);
		}

		for (const FAssetData& SelectedAsset : SelectedAssets)
		{
			TargetAssetPaths.Add(SelectedAsset.GetObjectPathString());
		}
		bFromSelection = true;
	}

	TArray<TSharedPtr<FJsonValue>> ThumbnailItems;
	int32 SucceededCount = 0;
	int32 FailedCount = 0;

	for (const FString& AssetPath : TargetAssetPaths)
	{
		TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("requested_asset"), AssetPath);
		Item->SetStringField(TEXT("mime_type"), TEXT("image/png"));
		Item->SetStringField(TEXT("image_format"), TEXT("png"));

		UObject* TargetObject = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
		if (!TargetObject)
		{
			Item->SetBoolField(TEXT("success"), false);
			Item->SetStringField(TEXT("error"), TEXT("asset_not_found"));
			Item->SetStringField(TEXT("error_message"), FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
			ThumbnailItems.Add(MakeShared<FJsonValueObject>(Item));
			++FailedCount;
			continue;
		}

		FObjectThumbnail RenderedThumbnail;
		ThumbnailTools::RenderThumbnail(
			TargetObject,
			static_cast<uint32>(ThumbnailSize),
			static_cast<uint32>(ThumbnailSize),
			ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush,
			nullptr,
			&RenderedThumbnail
		);

		if (RenderedThumbnail.IsEmpty() || !RenderedThumbnail.HasValidImageData())
		{
			Item->SetBoolField(TEXT("success"), false);
			Item->SetStringField(TEXT("error"), TEXT("thumbnail_render_failed"));
			Item->SetStringField(TEXT("error_message"), FString::Printf(TEXT("Failed to render thumbnail: %s"), *TargetObject->GetPathName()));
			ThumbnailItems.Add(MakeShared<FJsonValueObject>(Item));
			++FailedCount;
			continue;
		}

		const int32 ImageWidth = RenderedThumbnail.GetImageWidth();
		const int32 ImageHeight = RenderedThumbnail.GetImageHeight();
		if (ImageWidth <= 0 || ImageHeight <= 0)
		{
			Item->SetBoolField(TEXT("success"), false);
			Item->SetStringField(TEXT("error"), TEXT("invalid_thumbnail"));
			Item->SetStringField(TEXT("error_message"), TEXT("Rendered thumbnail has invalid dimensions"));
			ThumbnailItems.Add(MakeShared<FJsonValueObject>(Item));
			++FailedCount;
			continue;
		}

		const TArray<uint8>& RawImageBytes = RenderedThumbnail.GetUncompressedImageData();
		const int64 PixelCount = static_cast<int64>(ImageWidth) * static_cast<int64>(ImageHeight);
		const int64 ExpectedBytes = PixelCount * static_cast<int64>(sizeof(FColor));
		if (RawImageBytes.Num() < ExpectedBytes)
		{
			Item->SetBoolField(TEXT("success"), false);
			Item->SetStringField(TEXT("error"), TEXT("invalid_thumbnail_buffer"));
			Item->SetStringField(TEXT("error_message"), TEXT("Rendered thumbnail buffer is smaller than expected"));
			ThumbnailItems.Add(MakeShared<FJsonValueObject>(Item));
			++FailedCount;
			continue;
		}

		TArray<FColor> ColorPixels;
		ColorPixels.SetNumUninitialized(static_cast<int32>(PixelCount));
		FMemory::Memcpy(ColorPixels.GetData(), RawImageBytes.GetData(), static_cast<SIZE_T>(ExpectedBytes));

		TArray<uint8> CompressedPngBytes;
		FImageUtils::ThumbnailCompressImageArray(ImageWidth, ImageHeight, ColorPixels, CompressedPngBytes);

		if (CompressedPngBytes.IsEmpty())
		{
			Item->SetBoolField(TEXT("success"), false);
			Item->SetStringField(TEXT("error"), TEXT("png_compress_failed"));
			Item->SetStringField(TEXT("error_message"), TEXT("Failed to compress thumbnail to PNG"));
			ThumbnailItems.Add(MakeShared<FJsonValueObject>(Item));
			++FailedCount;
			continue;
		}

		Item->SetBoolField(TEXT("success"), true);
		Item->SetStringField(TEXT("asset_name"), TargetObject->GetName());
		Item->SetStringField(TEXT("asset_path"), TargetObject->GetPathName());
		Item->SetStringField(TEXT("asset_class"), TargetObject->GetClass() ? TargetObject->GetClass()->GetName() : TEXT("Unknown"));
		Item->SetNumberField(TEXT("width"), ImageWidth);
		Item->SetNumberField(TEXT("height"), ImageHeight);
		Item->SetNumberField(TEXT("image_byte_size"), CompressedPngBytes.Num());
		Item->SetStringField(TEXT("image_base64"), FBase64::Encode(CompressedPngBytes));

		ThumbnailItems.Add(MakeShared<FJsonValueObject>(Item));
		++SucceededCount;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("from_selection"), bFromSelection);
	Result->SetNumberField(TEXT("selected_count"), SelectedCount);
	Result->SetNumberField(TEXT("requested_size"), RequestedSize);
	Result->SetNumberField(TEXT("applied_size"), ThumbnailSize);
	Result->SetNumberField(TEXT("requested_assets"), TargetAssetPaths.Num());
	Result->SetNumberField(TEXT("succeeded"), SucceededCount);
	Result->SetNumberField(TEXT("failed"), FailedCount);
	Result->SetArrayField(TEXT("thumbnails"), ThumbnailItems);

	if (ThumbnailItems.Num() > 0)
	{
		const TSharedPtr<FJsonObject>* FirstItem = nullptr;
		if (ThumbnailItems[0]->TryGetObject(FirstItem) && FirstItem && (*FirstItem)->GetBoolField(TEXT("success")))
		{
			// Backward compatibility fields for existing single-item consumers.
			Result->SetStringField(TEXT("asset_name"), (*FirstItem)->GetStringField(TEXT("asset_name")));
			Result->SetStringField(TEXT("asset_path"), (*FirstItem)->GetStringField(TEXT("asset_path")));
			Result->SetStringField(TEXT("asset_class"), (*FirstItem)->GetStringField(TEXT("asset_class")));
			Result->SetNumberField(TEXT("width"), (*FirstItem)->GetNumberField(TEXT("width")));
			Result->SetNumberField(TEXT("height"), (*FirstItem)->GetNumberField(TEXT("height")));
			Result->SetStringField(TEXT("image_format"), TEXT("png"));
			Result->SetStringField(TEXT("mime_type"), TEXT("image/png"));
			Result->SetNumberField(TEXT("image_byte_size"), (*FirstItem)->GetNumberField(TEXT("image_byte_size")));
			Result->SetStringField(TEXT("image_base64"), (*FirstItem)->GetStringField(TEXT("image_base64")));
		}
	}

	return CreateSuccessResponse(Result);
}


// ========================================================================
// FGetBlueprintSummaryAction
// ========================================================================

bool FGetBlueprintSummaryAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("blueprint_name")) && !Params->HasField(TEXT("asset_path")))
	{
		OutError = TEXT("Missing required 'blueprint_name' or 'asset_path' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FGetBlueprintSummaryAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = nullptr;

	// Load blueprint by name or path
	if (Params->HasField(TEXT("asset_path")))
	{
		FString AssetPath = Params->GetStringField(TEXT("asset_path"));
		Blueprint = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *AssetPath));
	}
	
	if (!Blueprint && Params->HasField(TEXT("blueprint_name")))
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint_name"));
		Blueprint = FMCPCommonUtils::FindBlueprint(BlueprintName);
	}

	if (!Blueprint)
	{
		return CreateErrorResponse(TEXT("Blueprint not found"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
	Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());

	// Parent class
	if (Blueprint->ParentClass)
	{
		Result->SetStringField(TEXT("parent_class"), Blueprint->ParentClass->GetName());
		Result->SetStringField(TEXT("parent_class_path"), Blueprint->ParentClass->GetPathName());
	}

	// Blueprint type
	FString TypeStr;
	switch (Blueprint->BlueprintType)
	{
		case BPTYPE_Normal: TypeStr = TEXT("Normal"); break;
		case BPTYPE_Const: TypeStr = TEXT("Const"); break;
		case BPTYPE_MacroLibrary: TypeStr = TEXT("MacroLibrary"); break;
		case BPTYPE_Interface: TypeStr = TEXT("Interface"); break;
		case BPTYPE_LevelScript: TypeStr = TEXT("LevelScript"); break;
		case BPTYPE_FunctionLibrary: TypeStr = TEXT("FunctionLibrary"); break;
		default: TypeStr = TEXT("Unknown"); break;
	}
	Result->SetStringField(TEXT("blueprint_type"), TypeStr);

	// Compile status
	FString CompileStatus;
	switch (Blueprint->Status)
	{
		case BS_UpToDate: CompileStatus = TEXT("UpToDate"); break;
		case BS_Dirty: CompileStatus = TEXT("Dirty"); break;
		case BS_Error: CompileStatus = TEXT("Error"); break;
		case BS_BeingCreated: CompileStatus = TEXT("BeingCreated"); break;
		default: CompileStatus = TEXT("Unknown"); break;
	}
	Result->SetStringField(TEXT("compile_status"), CompileStatus);

	// ---- Variables ----
	TArray<TSharedPtr<FJsonValue>> VarsArray;
	for (const FBPVariableDescription& VarDesc : Blueprint->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), VarDesc.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), VarDesc.VarType.PinCategory.ToString());

		if (VarDesc.VarType.PinSubCategoryObject.IsValid())
		{
			VarObj->SetStringField(TEXT("sub_type"), VarDesc.VarType.PinSubCategoryObject->GetName());
		}

		// Container type
		if (VarDesc.VarType.IsArray())
		{
			VarObj->SetStringField(TEXT("container"), TEXT("Array"));
		}
		else if (VarDesc.VarType.IsSet())
		{
			VarObj->SetStringField(TEXT("container"), TEXT("Set"));
		}
		else if (VarDesc.VarType.IsMap())
		{
			VarObj->SetStringField(TEXT("container"), TEXT("Map"));
		}

		VarObj->SetBoolField(TEXT("is_instance_editable"), VarDesc.PropertyFlags & CPF_Edit ? true : false);
		VarObj->SetBoolField(TEXT("is_blueprint_read_only"), VarDesc.PropertyFlags & CPF_BlueprintReadOnly ? true : false);

		if (!VarDesc.Category.IsEmpty())
		{
			VarObj->SetStringField(TEXT("category"), VarDesc.Category.ToString());
		}
		if (!VarDesc.DefaultValue.IsEmpty())
		{
			VarObj->SetStringField(TEXT("default_value"), VarDesc.DefaultValue);
		}

		VarsArray.Add(MakeShared<FJsonValueObject>(VarObj));
	}
	Result->SetArrayField(TEXT("variables"), VarsArray);

	// ---- Functions / Graphs ----
	TArray<TSharedPtr<FJsonValue>> FunctionsArray;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph) continue;
		TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
		FuncObj->SetStringField(TEXT("name"), Graph->GetName());
		FuncObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

		// Try to get access specifier and descriptions from function entry
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_FunctionEntry* FuncEntry = Cast<UK2Node_FunctionEntry>(Node))
			{
				// Collect parameter pins
				TArray<TSharedPtr<FJsonValue>> ParamsArray;
				for (UEdGraphPin* Pin : FuncEntry->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Output && Pin->PinName != UEdGraphSchema_K2::PN_Then)
					{
						TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
						PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
						PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
						if (Pin->PinType.PinSubCategoryObject.IsValid())
						{
							PinObj->SetStringField(TEXT("sub_type"), Pin->PinType.PinSubCategoryObject->GetName());
						}
						ParamsArray.Add(MakeShared<FJsonValueObject>(PinObj));
					}
				}
				FuncObj->SetArrayField(TEXT("parameters"), ParamsArray);
				break;
			}
		}

		FunctionsArray.Add(MakeShared<FJsonValueObject>(FuncObj));
	}
	Result->SetArrayField(TEXT("functions"), FunctionsArray);

	// ---- Macros ----
	TArray<TSharedPtr<FJsonValue>> MacrosArray;
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (!Graph) continue;
		TSharedPtr<FJsonObject> MacroObj = MakeShared<FJsonObject>();
		MacroObj->SetStringField(TEXT("name"), Graph->GetName());
		MacroObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
		MacrosArray.Add(MakeShared<FJsonValueObject>(MacroObj));
	}
	Result->SetArrayField(TEXT("macros"), MacrosArray);

	// ---- Event Graphs ----
	TArray<TSharedPtr<FJsonValue>> EventGraphsArray;
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (!Graph) continue;
		TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
		GraphObj->SetStringField(TEXT("name"), Graph->GetName());
		GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

		// Collect event nodes and key node types
		TArray<TSharedPtr<FJsonValue>> EventNodes;
		int32 FunctionCallCount = 0;
		int32 VarGetCount = 0;
		int32 VarSetCount = 0;
		int32 CustomEventCount = 0;

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
			{
				TSharedPtr<FJsonObject> EvObj = MakeShared<FJsonObject>();
				EvObj->SetStringField(TEXT("event_name"), EventNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
				EvObj->SetStringField(TEXT("node_id"), EventNode->NodeGuid.ToString());
				EventNodes.Add(MakeShared<FJsonValueObject>(EvObj));
			}
			else if (Cast<UK2Node_CustomEvent>(Node))
			{
				CustomEventCount++;
				TSharedPtr<FJsonObject> EvObj = MakeShared<FJsonObject>();
				EvObj->SetStringField(TEXT("event_name"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
				EvObj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
				EvObj->SetStringField(TEXT("type"), TEXT("CustomEvent"));
				EventNodes.Add(MakeShared<FJsonValueObject>(EvObj));
			}
			else if (Cast<UK2Node_CallFunction>(Node))
			{
				FunctionCallCount++;
			}
			else if (Cast<UK2Node_VariableGet>(Node))
			{
				VarGetCount++;
			}
			else if (Cast<UK2Node_VariableSet>(Node))
			{
				VarSetCount++;
			}
		}

		GraphObj->SetArrayField(TEXT("events"), EventNodes);

		TSharedPtr<FJsonObject> StatsObj = MakeShared<FJsonObject>();
		StatsObj->SetNumberField(TEXT("function_calls"), FunctionCallCount);
		StatsObj->SetNumberField(TEXT("variable_gets"), VarGetCount);
		StatsObj->SetNumberField(TEXT("variable_sets"), VarSetCount);
		StatsObj->SetNumberField(TEXT("custom_events"), CustomEventCount);
		GraphObj->SetObjectField(TEXT("stats"), StatsObj);

		EventGraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
	}
	Result->SetArrayField(TEXT("event_graphs"), EventGraphsArray);

	// ---- Components (from SCS) ----
	TArray<TSharedPtr<FJsonValue>> ComponentsArray;
	if (Blueprint->SimpleConstructionScript)
	{
		TArray<USCS_Node*> AllNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
		for (USCS_Node* SCSNode : AllNodes)
		{
			if (!SCSNode) continue;
			TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("name"), SCSNode->GetVariableName().ToString());
			if (SCSNode->ComponentClass)
			{
				CompObj->SetStringField(TEXT("class"), SCSNode->ComponentClass->GetName());
			}
			// Parent info via ParentComponentOrVariableName
			if (!SCSNode->ParentComponentOrVariableName.IsNone())
			{
				CompObj->SetStringField(TEXT("parent"), SCSNode->ParentComponentOrVariableName.ToString());
			}
			ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
		}
	}
	Result->SetArrayField(TEXT("components"), ComponentsArray);

	// ---- Interfaces ----
	TArray<TSharedPtr<FJsonValue>> InterfacesArray;
	for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
	{
		if (InterfaceDesc.Interface)
		{
			InterfacesArray.Add(MakeShared<FJsonValueString>(InterfaceDesc.Interface->GetName()));
		}
	}
	Result->SetArrayField(TEXT("implemented_interfaces"), InterfacesArray);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// P2: FGetEditorLogsAction
// ============================================================================

namespace
{
ELogVerbosity::Type ParseMinVerbosity(const FString& VerbosityStr)
{
	if (VerbosityStr.Equals(TEXT("Fatal"), ESearchCase::IgnoreCase)) return ELogVerbosity::Fatal;
	if (VerbosityStr.Equals(TEXT("Error"), ESearchCase::IgnoreCase)) return ELogVerbosity::Error;
	if (VerbosityStr.Equals(TEXT("Warning"), ESearchCase::IgnoreCase)) return ELogVerbosity::Warning;
	if (VerbosityStr.Equals(TEXT("Display"), ESearchCase::IgnoreCase)) return ELogVerbosity::Display;
	if (VerbosityStr.Equals(TEXT("Log"), ESearchCase::IgnoreCase)) return ELogVerbosity::Log;
	if (VerbosityStr.Equals(TEXT("Verbose"), ESearchCase::IgnoreCase)) return ELogVerbosity::Verbose;
	if (VerbosityStr.Equals(TEXT("VeryVerbose"), ESearchCase::IgnoreCase)) return ELogVerbosity::VeryVerbose;
	return ELogVerbosity::All;
}

FString VerbosityToString(ELogVerbosity::Type Verbosity)
{
	switch (Verbosity)
	{
	case ELogVerbosity::Fatal: return TEXT("Fatal");
	case ELogVerbosity::Error: return TEXT("Error");
	case ELogVerbosity::Warning: return TEXT("Warning");
	case ELogVerbosity::Display: return TEXT("Display");
	case ELogVerbosity::Log: return TEXT("Log");
	case ELogVerbosity::Verbose: return TEXT("Verbose");
	default: return TEXT("VeryVerbose");
	}
}

uint64 ParseLiveCursor(const FString& Cursor)
{
	if (!Cursor.StartsWith(TEXT("live:")))
	{
		return 0;
	}

	uint64 Seq = 0;
	LexFromString(Seq, *Cursor.RightChop(5));
	return Seq;
}
}

TSharedPtr<FJsonObject> FGetEditorLogsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	const int32 Count = static_cast<int32>(GetOptionalNumber(Params, TEXT("count"), 100.0));
	const FString CategoryFilter = GetOptionalString(Params, TEXT("category"));

	// Parse verbosity filter
	const ELogVerbosity::Type MinVerbosity = ParseMinVerbosity(GetOptionalString(Params, TEXT("min_verbosity")));

	TArray<FMCPLogCapture::FLogEntry> Entries = FMCPLogCapture::Get().GetRecent(Count, CategoryFilter, MinVerbosity);

	TArray<TSharedPtr<FJsonValue>> LinesArray;
	for (const FMCPLogCapture::FLogEntry& Entry : Entries)
	{
		TSharedPtr<FJsonObject> LineObj = MakeShared<FJsonObject>();
		LineObj->SetNumberField(TEXT("timestamp"), Entry.Timestamp);
		LineObj->SetStringField(TEXT("category"), Entry.Category.ToString());

		// Convert verbosity to string
		LineObj->SetStringField(TEXT("verbosity"), VerbosityToString(Entry.Verbosity));
		LineObj->SetStringField(TEXT("message"), Entry.Message);

		LinesArray.Add(MakeShared<FJsonValueObject>(LineObj));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetArrayField(TEXT("lines"), LinesArray);
	ResultData->SetNumberField(TEXT("total"), static_cast<double>(LinesArray.Num()));
	ResultData->SetNumberField(TEXT("total_captured"), static_cast<double>(FMCPLogCapture::Get().GetTotalCaptured()));
	ResultData->SetBoolField(TEXT("capturing"), FMCPLogCapture::Get().IsCapturing());
	return CreateSuccessResponse(ResultData);
}

TSharedPtr<FJsonObject> FGetUnrealLogsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FMCPLogCapture& Capture = FMCPLogCapture::Get();
	if (!Capture.IsCapturing())
	{
		return CreateErrorResponse(TEXT("Live log capture is not active"), TEXT("capture_inactive"));
	}

	const int32 TailLines = FMath::Clamp(static_cast<int32>(GetOptionalNumber(Params, TEXT("tail_lines"), 200.0)), 20, 2000);
	const int32 MaxBytes = FMath::Clamp(static_cast<int32>(GetOptionalNumber(Params, TEXT("max_bytes"), 65536.0)), 8192, 1024 * 1024);
	const bool bIncludeMeta = GetOptionalBool(Params, TEXT("include_meta"), true);
	const bool bRequireRecent = GetOptionalBool(Params, TEXT("require_recent"), false);
	const double RecentWindowSeconds = FMath::Max(0.0, GetOptionalNumber(Params, TEXT("recent_window_seconds"), 2.0));

	if (bRequireRecent && !Capture.HasRecentData(RecentWindowSeconds))
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("No live logs in the last %.2f seconds"), RecentWindowSeconds),
			TEXT("no_recent_live_data"));
	}

	const FString Cursor = GetOptionalString(Params, TEXT("cursor"));
	const uint64 AfterSeq = ParseLiveCursor(Cursor);

	TArray<FString> CategoryFilters;
	const FString CategoryFilterSingle = GetOptionalString(Params, TEXT("filter_category"));
	if (!CategoryFilterSingle.IsEmpty())
	{
		CategoryFilters.Add(CategoryFilterSingle);
	}

	const TArray<TSharedPtr<FJsonValue>>* CategoryFilterArray = GetOptionalArray(Params, TEXT("filter_categories"));
	if (CategoryFilterArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *CategoryFilterArray)
		{
			FString CategoryValue;
			if (Value.IsValid() && Value->TryGetString(CategoryValue) && !CategoryValue.IsEmpty())
			{
				CategoryFilters.Add(CategoryValue);
			}
		}
	}

	const ELogVerbosity::Type MinVerbosity = ParseMinVerbosity(GetOptionalString(Params, TEXT("filter_min_verbosity")));
	const FString ContainsFilter = GetOptionalString(Params, TEXT("filter_contains"));

	bool bTruncated = false;
	uint64 LastSeq = Capture.GetLatestSeq();
	TArray<FMCPLogCapture::FLogEntry> Entries = Capture.GetSince(
		AfterSeq,
		TailLines,
		MaxBytes,
		CategoryFilters,
		MinVerbosity,
		ContainsFilter,
		bTruncated,
		LastSeq);

	if (AfterSeq == 0)
	{
		if (Entries.Num() > TailLines)
		{
			const int32 Start = Entries.Num() - TailLines;
			Entries.RemoveAt(0, Start);
			bTruncated = true;
		}
	}

	FString Content;
	int32 BytesReturned = 0;
	for (const FMCPLogCapture::FLogEntry& Entry : Entries)
	{
		const FString Line = FString::Printf(
			TEXT("[%s][%s][%s] %s\n"),
			*Entry.TimestampUtc.ToIso8601(),
			*VerbosityToString(Entry.Verbosity),
			*Entry.Category.ToString(),
			*Entry.Message);

		const int32 LineBytes = FTCHARToUTF8(*Line).Length();
		if (BytesReturned + LineBytes > MaxBytes)
		{
			bTruncated = true;
			break;
		}

		Content.Append(Line);
		BytesReturned += LineBytes;
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("source"), TEXT("live"));
	ResultData->SetBoolField(TEXT("isLive"), true);
	ResultData->SetStringField(TEXT("filePath"), TEXT(""));
	ResultData->SetStringField(TEXT("projectLogDir"), FPaths::ProjectLogDir());
	ResultData->SetStringField(TEXT("cursor"), FString::Printf(TEXT("live:%llu"), LastSeq));
	ResultData->SetBoolField(TEXT("truncated"), bTruncated);
	ResultData->SetNumberField(TEXT("linesReturned"), Entries.Num());
	ResultData->SetNumberField(TEXT("bytesReturned"), BytesReturned);
	ResultData->SetStringField(TEXT("lastUpdateUtc"), Capture.GetLastReceivedUtc().ToIso8601());
	ResultData->SetStringField(TEXT("content"), Content);

	TArray<TSharedPtr<FJsonValue>> Notes;
	if (AfterSeq > 0)
	{
		Notes.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("incremental_from_seq=%llu"), AfterSeq)));
	}
	if (bTruncated)
	{
		Notes.Add(MakeShared<FJsonValueString>(TEXT("response_truncated_by_limits")));
	}
	if (bIncludeMeta)
	{
		ResultData->SetBoolField(TEXT("hasRecentLiveData"), Capture.HasRecentData(2.0));
		ResultData->SetNumberField(TEXT("totalCaptured"), static_cast<double>(Capture.GetTotalCaptured()));
	}
	ResultData->SetArrayField(TEXT("notes"), Notes);

	if (Entries.Num() == 0 && AfterSeq == 0)
	{
		return CreateErrorResponse(TEXT("Live log capture has no entries yet"), TEXT("live_buffer_empty"));
	}

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// P2: FBatchExecuteAction
// ============================================================================

bool FBatchExecuteAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* Commands = GetOptionalArray(Params, TEXT("commands"));
	if (!Commands || Commands->Num() == 0)
	{
		OutError = TEXT("Missing or empty 'commands' array");
		return false;
	}
	if (Commands->Num() > MaxBatchSize)
	{
		OutError = FString::Printf(TEXT("Batch too large: %d commands (max %d)"), Commands->Num(), MaxBatchSize);
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FBatchExecuteAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	const TArray<TSharedPtr<FJsonValue>>* Commands = GetOptionalArray(Params, TEXT("commands"));
	const bool bStopOnError = GetOptionalBool(Params, TEXT("stop_on_error"), true);

	// We need access to the Bridge to dispatch sub-commands
	UMCPBridge* Bridge = GEditor ? GEditor->GetEditorSubsystem<UMCPBridge>() : nullptr;
	if (!Bridge)
	{
		return CreateErrorResponse(TEXT("MCPBridge subsystem not available"));
	}

	const int32 Total = Commands->Num();
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 Succeeded = 0;
	int32 Failed = 0;
	int32 Executed = 0;

	for (int32 i = 0; i < Total; ++i)
	{
		const TSharedPtr<FJsonObject>* CmdObj = nullptr;
		if (!(*Commands)[i]->TryGetObject(CmdObj) || !CmdObj || !(*CmdObj).IsValid())
		{
			TSharedPtr<FJsonObject> ErrResult = MakeShared<FJsonObject>();
			ErrResult->SetNumberField(TEXT("index"), i);
			ErrResult->SetBoolField(TEXT("success"), false);
			ErrResult->SetStringField(TEXT("error"), TEXT("Invalid command object"));
			ResultsArray.Add(MakeShared<FJsonValueObject>(ErrResult));
			++Failed;
			++Executed;
			if (bStopOnError) break;
			continue;
		}

		FString CmdType;
		if (!(*CmdObj)->TryGetStringField(TEXT("type"), CmdType) || CmdType.IsEmpty())
		{
			TSharedPtr<FJsonObject> ErrResult = MakeShared<FJsonObject>();
			ErrResult->SetNumberField(TEXT("index"), i);
			ErrResult->SetBoolField(TEXT("success"), false);
			ErrResult->SetStringField(TEXT("error"), TEXT("Missing 'type' field"));
			ResultsArray.Add(MakeShared<FJsonValueObject>(ErrResult));
			++Failed;
			++Executed;
			if (bStopOnError) break;
			continue;
		}

		TSharedPtr<FJsonObject> CmdParams;
		const TSharedPtr<FJsonObject>* CmdParamsPtr = nullptr;
		if ((*CmdObj)->TryGetObjectField(TEXT("params"), CmdParamsPtr) && CmdParamsPtr)
		{
			CmdParams = *CmdParamsPtr;
		}
		else
		{
			CmdParams = MakeShared<FJsonObject>();
		}

		UE_LOG(LogMCP, Log, TEXT("Batch[%d/%d]: executing '%s'"), i + 1, Total, *CmdType);

		// Execute the sub-command via the Bridge (bypasses TCP, stays on GameThread)
		TSharedPtr<FJsonObject> SubResult = Bridge->ExecuteCommand(CmdType, CmdParams);

		// Wrap result with index
		if (SubResult.IsValid())
		{
			SubResult->SetNumberField(TEXT("index"), i);
			SubResult->SetStringField(TEXT("type"), CmdType);
		}

		bool bSubSuccess = false;
		if (SubResult.IsValid() && SubResult->TryGetBoolField(TEXT("success"), bSubSuccess) && bSubSuccess)
		{
			++Succeeded;
		}
		else
		{
			++Failed;
		}

		ResultsArray.Add(MakeShared<FJsonValueObject>(SubResult.IsValid() ? SubResult : MakeShared<FJsonObject>()));
		++Executed;

		if (!bSubSuccess && bStopOnError)
		{
			UE_LOG(LogMCP, Warning, TEXT("Batch stopped at command %d/%d ('%s') due to stop_on_error"), i + 1, Total, *CmdType);
			break;
		}
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetNumberField(TEXT("total"), Total);
	ResultData->SetNumberField(TEXT("executed"), Executed);
	ResultData->SetNumberField(TEXT("succeeded"), Succeeded);
	ResultData->SetNumberField(TEXT("failed"), Failed);
	ResultData->SetArrayField(TEXT("results"), ResultsArray);

	// The batch command itself always succeeds (it dispatched commands).
	// Partial sub-command failures are conveyed via failed > 0 and individual
	// result items' "success" fields — not by failing the batch itself.
	// This ensures the Python side receives structured data (results, total,
	// executed, etc.) regardless of sub-command outcomes.
	if (Failed > 0)
	{
		ResultData->SetBoolField(TEXT("has_failures"), true);
	}

	return CreateSuccessResponse(ResultData);
}


// ============================================================================
// FEditorIsReadyAction
// ============================================================================

TSharedPtr<FJsonObject> FEditorIsReadyAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// 1) GEditor exists and is valid
	bool bEditorValid = (GEditor != nullptr);
	Result->SetBoolField(TEXT("editor_valid"), bEditorValid);

	// 2) Editor world is available
	bool bWorldReady = false;
	if (bEditorValid)
	{
		UWorld* World = GEditor->GetEditorWorldContext(false).World();
		bWorldReady = (World != nullptr);
	}
	Result->SetBoolField(TEXT("world_ready"), bWorldReady);

	// 3) Asset registry has finished initial scan
	bool bAssetRegistryReady = false;
	if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		bAssetRegistryReady = !AssetRegistry.IsLoadingAssets();
	}
	Result->SetBoolField(TEXT("asset_registry_ready"), bAssetRegistryReady);

	// 4) Overall readiness: all critical subsystems ready
	bool bFullyReady = bEditorValid && bWorldReady && bAssetRegistryReady;
	Result->SetBoolField(TEXT("ready"), bFullyReady);

	// 5) Uptime info
	Result->SetNumberField(TEXT("engine_uptime_seconds"), FPlatformTime::Seconds());

	return CreateSuccessResponse(Result);
}


// ============================================================================
// FRequestEditorShutdownAction
// ============================================================================

TSharedPtr<FJsonObject> FRequestEditorShutdownAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	bool bForce = GetOptionalBool(Params, TEXT("force"), false);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("shutdown_requested"), true);
	Result->SetBoolField(TEXT("force"), bForce);

	// Schedule the exit on the next game-thread tick so we can send the response first
	AsyncTask(ENamedThreads::GameThread, [bForce]()
	{
		// Small delay to ensure the MCP response is sent before the process starts shutting down
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([bForce](float DeltaTime) -> bool
			{
				if (bForce)
				{
					// Force immediate exit - no save dialogs
					FPlatformMisc::RequestExitWithStatus(bForce, 0);
				}
				else
				{
					// Graceful shutdown - may show save dialog if -unattended is not set
					FPlatformMisc::RequestExit(false);
				}
				return false; // Don't tick again
			}),
			0.2f // 200ms delay to let TCP response flush
		);
	});

	return CreateSuccessResponse(Result);
}

// ============================================================================
// FDescribeFullAction — Single-call comprehensive Blueprint snapshot
// ============================================================================

bool FDescribeFullAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("blueprint_name")) && !Params->HasField(TEXT("asset_path")))
	{
		OutError = TEXT("Either 'blueprint_name' or 'asset_path' is required");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FDescribeFullAction::SerializePinCompact(const UEdGraphPin* Pin)
{
	if (!Pin || Pin->bHidden)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
	PinObj->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
	PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
	PinObj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());

	if (Pin->PinType.PinSubCategoryObject.IsValid())
	{
		PinObj->SetStringField(TEXT("sub_type"), Pin->PinType.PinSubCategoryObject->GetName());
	}

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

TSharedPtr<FJsonObject> FDescribeFullAction::SerializeGraph(UBlueprint* Blueprint, UEdGraph* Graph,
	bool bIncludePinDetails, bool bIncludeFunctionSignatures) const
{
	if (!Graph) return nullptr;

	TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
	GraphObj->SetStringField(TEXT("name"), Graph->GetName());
	GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	TArray<TSharedPtr<FJsonValue>> EdgesArray;
	TSet<FString> SeenEdges;

	for (const UEdGraphNode* Node : Graph->Nodes)
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

		// Function signature (optional, for function call nodes)
		if (bIncludeFunctionSignatures)
		{
			if (const UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(Node))
			{
				if (const UFunction* Function = FuncNode->GetTargetFunction())
				{
					TSharedPtr<FJsonObject> SigObj = MakeShared<FJsonObject>();
					SigObj->SetStringField(TEXT("function_name"), Function->GetName());
					SigObj->SetStringField(TEXT("owner_class"),
						Function->GetOwnerClass() ? Function->GetOwnerClass()->GetName() : TEXT("Unknown"));
					SigObj->SetBoolField(TEXT("is_static"), Function->HasAnyFunctionFlags(FUNC_Static));
					SigObj->SetBoolField(TEXT("is_pure"), Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
					NodeObj->SetObjectField(TEXT("function_signature"), SigObj);
				}
			}
		}

		// Pins — compact mode by default
		TArray<TSharedPtr<FJsonValue>> PinsArray;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->bHidden) continue;

			TSharedPtr<FJsonObject> PinObj = SerializePinCompact(Pin);
			if (PinObj.IsValid())
			{
				// Add full PinType details only if requested
				if (bIncludePinDetails)
				{
					TSharedPtr<FJsonObject> PinTypeObj = MakeShared<FJsonObject>();
					PinTypeObj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
					if (Pin->PinType.PinSubCategory != NAME_None)
						PinTypeObj->SetStringField(TEXT("sub_category"), Pin->PinType.PinSubCategory.ToString());
					if (Pin->PinType.PinSubCategoryObject.Get())
						PinTypeObj->SetStringField(TEXT("sub_category_object"), Pin->PinType.PinSubCategoryObject->GetPathName());
					PinTypeObj->SetBoolField(TEXT("is_array"), Pin->PinType.IsArray());
					PinTypeObj->SetBoolField(TEXT("is_reference"), Pin->PinType.bIsReference);
					PinObj->SetObjectField(TEXT("pin_type"), PinTypeObj);
				}
				PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
			}

			// Collect edges (output pins only, deduplicated)
			if (Pin->Direction == EGPD_Output)
			{
				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
					FString EdgeKey = FString::Printf(TEXT("%s:%s->%s:%s"),
						*Node->NodeGuid.ToString(), *Pin->PinName.ToString(),
						*LinkedPin->GetOwningNode()->NodeGuid.ToString(), *LinkedPin->PinName.ToString());
					if (!SeenEdges.Contains(EdgeKey))
					{
						SeenEdges.Add(EdgeKey);
						TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
						EdgeObj->SetStringField(TEXT("from_node"), Node->NodeGuid.ToString());
						EdgeObj->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
						EdgeObj->SetStringField(TEXT("to_node"), LinkedPin->GetOwningNode()->NodeGuid.ToString());
						EdgeObj->SetStringField(TEXT("to_pin"), LinkedPin->PinName.ToString());
						EdgesArray.Add(MakeShared<FJsonValueObject>(EdgeObj));
					}
				}
			}
		}
		NodeObj->SetArrayField(TEXT("pins"), PinsArray);
		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	GraphObj->SetArrayField(TEXT("nodes"), NodesArray);
	GraphObj->SetNumberField(TEXT("edge_count"), EdgesArray.Num());
	GraphObj->SetArrayField(TEXT("edges"), EdgesArray);

	return GraphObj;
}

TSharedPtr<FJsonObject> FDescribeFullAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = nullptr;

	if (Params->HasField(TEXT("asset_path")))
	{
		FString AssetPath = Params->GetStringField(TEXT("asset_path"));
		Blueprint = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *AssetPath));
	}
	if (!Blueprint && Params->HasField(TEXT("blueprint_name")))
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint_name"));
		Blueprint = FMCPCommonUtils::FindBlueprint(BlueprintName);
	}
	if (!Blueprint)
	{
		return CreateErrorResponse(TEXT("Blueprint not found"));
	}

	const bool bIncludePinDetails = GetOptionalBool(Params, TEXT("include_pin_details"), false);
	const bool bIncludeFunctionSignatures = GetOptionalBool(Params, TEXT("include_function_signatures"), false);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// ---- Basic Info ----
	Result->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
	Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());

	if (Blueprint->ParentClass)
	{
		Result->SetStringField(TEXT("parent_class"), Blueprint->ParentClass->GetName());
		Result->SetStringField(TEXT("parent_class_path"), Blueprint->ParentClass->GetPathName());
	}

	FString TypeStr;
	switch (Blueprint->BlueprintType)
	{
		case BPTYPE_Normal: TypeStr = TEXT("Normal"); break;
		case BPTYPE_Const: TypeStr = TEXT("Const"); break;
		case BPTYPE_MacroLibrary: TypeStr = TEXT("MacroLibrary"); break;
		case BPTYPE_Interface: TypeStr = TEXT("Interface"); break;
		case BPTYPE_LevelScript: TypeStr = TEXT("LevelScript"); break;
		case BPTYPE_FunctionLibrary: TypeStr = TEXT("FunctionLibrary"); break;
		default: TypeStr = TEXT("Unknown"); break;
	}
	Result->SetStringField(TEXT("blueprint_type"), TypeStr);

	FString CompileStatus;
	switch (Blueprint->Status)
	{
		case BS_UpToDate: CompileStatus = TEXT("UpToDate"); break;
		case BS_Dirty: CompileStatus = TEXT("Dirty"); break;
		case BS_Error: CompileStatus = TEXT("Error"); break;
		case BS_BeingCreated: CompileStatus = TEXT("BeingCreated"); break;
		default: CompileStatus = TEXT("Unknown"); break;
	}
	Result->SetStringField(TEXT("compile_status"), CompileStatus);

	// ---- Variables ----
	TArray<TSharedPtr<FJsonValue>> VarsArray;
	for (const FBPVariableDescription& VarDesc : Blueprint->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), VarDesc.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), VarDesc.VarType.PinCategory.ToString());
		if (VarDesc.VarType.PinSubCategoryObject.IsValid())
			VarObj->SetStringField(TEXT("sub_type"), VarDesc.VarType.PinSubCategoryObject->GetName());
		if (VarDesc.VarType.IsArray())
			VarObj->SetStringField(TEXT("container"), TEXT("Array"));
		VarObj->SetBoolField(TEXT("is_instance_editable"), (VarDesc.PropertyFlags & CPF_Edit) != 0);
		if (!VarDesc.DefaultValue.IsEmpty())
			VarObj->SetStringField(TEXT("default_value"), VarDesc.DefaultValue);
		VarsArray.Add(MakeShared<FJsonValueObject>(VarObj));
	}
	Result->SetArrayField(TEXT("variables"), VarsArray);

	// ---- Components (SCS) ----
	TArray<TSharedPtr<FJsonValue>> ComponentsArray;
	if (Blueprint->SimpleConstructionScript)
	{
		TArray<USCS_Node*> AllNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
		for (USCS_Node* SCSNode : AllNodes)
		{
			if (!SCSNode) continue;
			TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("name"), SCSNode->GetVariableName().ToString());
			if (SCSNode->ComponentClass)
				CompObj->SetStringField(TEXT("class"), SCSNode->ComponentClass->GetName());
			if (!SCSNode->ParentComponentOrVariableName.IsNone())
				CompObj->SetStringField(TEXT("parent"), SCSNode->ParentComponentOrVariableName.ToString());
			ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
		}
	}
	Result->SetArrayField(TEXT("components"), ComponentsArray);

	// ---- Interfaces ----
	TArray<TSharedPtr<FJsonValue>> InterfacesArray;
	for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
	{
		if (InterfaceDesc.Interface)
			InterfacesArray.Add(MakeShared<FJsonValueString>(InterfaceDesc.Interface->GetName()));
	}
	Result->SetArrayField(TEXT("implemented_interfaces"), InterfacesArray);

	// ---- All Graph Topologies ----
	TArray<TSharedPtr<FJsonValue>> AllGraphs;

	// Event Graphs (UbergraphPages)
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		TSharedPtr<FJsonObject> GraphObj = SerializeGraph(Blueprint, Graph, bIncludePinDetails, bIncludeFunctionSignatures);
		if (GraphObj.IsValid())
		{
			GraphObj->SetStringField(TEXT("graph_type"), TEXT("EventGraph"));
			AllGraphs.Add(MakeShared<FJsonValueObject>(GraphObj));
		}
	}

	// Function Graphs
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		TSharedPtr<FJsonObject> GraphObj = SerializeGraph(Blueprint, Graph, bIncludePinDetails, bIncludeFunctionSignatures);
		if (GraphObj.IsValid())
		{
			GraphObj->SetStringField(TEXT("graph_type"), TEXT("Function"));
			AllGraphs.Add(MakeShared<FJsonValueObject>(GraphObj));
		}
	}

	// Macro Graphs
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		TSharedPtr<FJsonObject> GraphObj = SerializeGraph(Blueprint, Graph, bIncludePinDetails, bIncludeFunctionSignatures);
		if (GraphObj.IsValid())
		{
			GraphObj->SetStringField(TEXT("graph_type"), TEXT("Macro"));
			AllGraphs.Add(MakeShared<FJsonValueObject>(GraphObj));
		}
	}

	Result->SetArrayField(TEXT("graphs"), AllGraphs);

	// Summary counts
	int32 TotalNodes = 0;
	int32 TotalEdges = 0;
	for (const auto& GraphVal : AllGraphs)
	{
		const TSharedPtr<FJsonObject>* GraphObjPtr;
		if (GraphVal->TryGetObject(GraphObjPtr) && GraphObjPtr)
		{
			TotalNodes += static_cast<int32>((*GraphObjPtr)->GetNumberField(TEXT("node_count")));
			TotalEdges += static_cast<int32>((*GraphObjPtr)->GetNumberField(TEXT("edge_count")));
		}
	}
	Result->SetNumberField(TEXT("total_graphs"), AllGraphs.Num());
	Result->SetNumberField(TEXT("total_nodes"), TotalNodes);
	Result->SetNumberField(TEXT("total_edges"), TotalEdges);

	return CreateSuccessResponse(Result);
}


// =========================================================================
// P6: PIE Control Actions
// =========================================================================

// ---- P6.1 FStartPIEAction ----

bool FStartPIEAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("GEditor is not available");
		return false;
	}
	if (GEditor->PlayWorld)
	{
		OutError = TEXT("A PIE session is already running. Stop it first with editor.stop_pie.");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FStartPIEAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ModeStr = GetOptionalString(Params, TEXT("mode"), TEXT("SelectedViewport"));

	FRequestPlaySessionParams SessionParams;
	SessionParams.SessionDestination = EPlaySessionDestinationType::InProcess;

	if (ModeStr.Equals(TEXT("Simulate"), ESearchCase::IgnoreCase))
	{
		SessionParams.WorldType = EPlaySessionWorldType::SimulateInEditor;
	}
	else
	{
		SessionParams.WorldType = EPlaySessionWorldType::PlayInEditor;
	}

	// Configure play mode via LevelEditorPlaySettings
	ULevelEditorPlaySettings* PlaySettings = GetMutableDefault<ULevelEditorPlaySettings>();
	if (PlaySettings)
	{
		if (ModeStr.Equals(TEXT("NewWindow"), ESearchCase::IgnoreCase))
		{
			PlaySettings->LastExecutedPlayModeType = PlayMode_InEditorFloating;
		}
		else if (ModeStr.Equals(TEXT("Simulate"), ESearchCase::IgnoreCase))
		{
			PlaySettings->LastExecutedPlayModeType = PlayMode_Simulate;
		}
		else // SelectedViewport (default)
		{
			PlaySettings->LastExecutedPlayModeType = PlayMode_InViewPort;
		}
	}

	GEditor->RequestPlaySession(SessionParams);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("mode"), ModeStr);
	Result->SetStringField(TEXT("message"), TEXT("PIE session requested"));
	Result->SetBoolField(TEXT("is_async"), true);

	UE_LOG(LogMCP, Log, TEXT("PIE start requested (mode=%s)"), *ModeStr);

	return CreateSuccessResponse(Result);
}

// ---- P6.2 FStopPIEAction ----

TSharedPtr<FJsonObject> FStopPIEAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor || !GEditor->PlayWorld)
	{
		Result->SetStringField(TEXT("state"), TEXT("already_stopped"));
		Result->SetStringField(TEXT("message"), TEXT("No PIE session is currently running"));
		return CreateSuccessResponse(Result);
	}

	GEditor->RequestEndPlayMap();

	Result->SetStringField(TEXT("state"), TEXT("stop_requested"));
	Result->SetStringField(TEXT("message"), TEXT("PIE stop requested"));

	UE_LOG(LogMCP, Log, TEXT("PIE stop requested"));

	return CreateSuccessResponse(Result);
}

// ---- P6.3 FGetPIEStateAction ----

TSharedPtr<FJsonObject> FGetPIEStateAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		Result->SetStringField(TEXT("state"), TEXT("Stopped"));
		return CreateSuccessResponse(Result);
	}

	if (GEditor->PlayWorld)
	{
		Result->SetStringField(TEXT("state"), TEXT("Running"));
		Result->SetStringField(TEXT("world_name"), GEditor->PlayWorld->GetName());
		Result->SetBoolField(TEXT("is_paused"), GEditor->PlayWorld->IsPaused());
		Result->SetBoolField(TEXT("is_simulating"), GEditor->IsSimulateInEditorInProgress());

		// Duration: use engine uptime difference (best available method)
		double CurrentTime = FPlatformTime::Seconds();
		Result->SetNumberField(TEXT("engine_time"), CurrentTime);

		// Check if a request to end is already queued
		Result->SetBoolField(TEXT("is_play_session_in_progress"), GEditor->IsPlaySessionInProgress());
	}
	else
	{
		Result->SetStringField(TEXT("state"), TEXT("Stopped"));
		Result->SetBoolField(TEXT("is_play_session_in_progress"), GEditor->IsPlaySessionInProgress());
	}

	return CreateSuccessResponse(Result);
}


// =========================================================================
// P6: Log Enhancement Actions
// =========================================================================

#include "MCPLogCapture.h"

// ---- P6.4 FClearLogsAction ----

TSharedPtr<FJsonObject> FClearLogsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FMCPLogCapture& LogCapture = FMCPLogCapture::Get();

	// Get pre-clear stats
	int64 PrevCount = LogCapture.GetTotalCaptured();
	uint64 PrevSeq = LogCapture.GetLatestSeq();

	// Optionally insert a session tag before clearing
	FString Tag = GetOptionalString(Params, TEXT("tag"), TEXT(""));
	if (!Tag.IsEmpty())
	{
		UE_LOG(LogMCP, Log, TEXT("[SESSION] %s"), *Tag);
	}

	// Clear the ring buffer
	LogCapture.Clear();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("cleared_total_captured"), static_cast<double>(PrevCount));
	Result->SetStringField(TEXT("previous_cursor"), FString::Printf(TEXT("live:%llu"), PrevSeq));
	Result->SetStringField(TEXT("new_cursor"), FString::Printf(TEXT("live:%llu"), LogCapture.GetLatestSeq()));
	if (!Tag.IsEmpty())
	{
		Result->SetStringField(TEXT("session_tag"), Tag);
	}
	Result->SetStringField(TEXT("message"), TEXT("Log buffer cleared"));

	UE_LOG(LogMCP, Log, TEXT("Log buffer cleared (prev total=%lld, prev seq=%llu)"), PrevCount, PrevSeq);

	return CreateSuccessResponse(Result);
}

// ---- P6.5 FAssertLogAction ----

bool FAssertLogAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* Assertions = GetOptionalArray(Params, TEXT("assertions"));
	if (!Assertions || Assertions->Num() == 0)
	{
		OutError = TEXT("'assertions' array is required and must not be empty");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAssertLogAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FMCPLogCapture& LogCapture = FMCPLogCapture::Get();

	// Parse optional cursor
	FString SinceCursor = GetOptionalString(Params, TEXT("since_cursor"), TEXT(""));
	uint64 AfterSeq = 0;
	if (SinceCursor.StartsWith(TEXT("live:")))
	{
		FString SeqStr = SinceCursor.Mid(5);
		AfterSeq = FCString::Strtoui64(*SeqStr, nullptr, 10);
	}

	// Gather log entries
	bool bTruncated = false;
	uint64 LastSeq = 0;
	TArray<FString> EmptyCategories;
	TArray<FMCPLogCapture::FLogEntry> LogEntries = LogCapture.GetSince(
		AfterSeq, 10000, 5 * 1024 * 1024,
		EmptyCategories, ELogVerbosity::All, TEXT(""),
		bTruncated, LastSeq);

	// Parse assertions and check
	const TArray<TSharedPtr<FJsonValue>>* Assertions = GetOptionalArray(Params, TEXT("assertions"));
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 PassedCount = 0;
	int32 FailedCount = 0;

	for (const TSharedPtr<FJsonValue>& AssertVal : *Assertions)
	{
		const TSharedPtr<FJsonObject>* AssertObjPtr;
		if (!AssertVal->TryGetObject(AssertObjPtr) || !AssertObjPtr)
		{
			continue;
		}

		const TSharedPtr<FJsonObject>& AssertObj = *AssertObjPtr;
		FString Keyword;
		if (!AssertObj->TryGetStringField(TEXT("keyword"), Keyword) || Keyword.IsEmpty())
		{
			continue;
		}

		int32 ExpectedCount = static_cast<int32>(AssertObj->GetNumberField(TEXT("expected_count")));
		FString Comparison = TEXT(">=");
		AssertObj->TryGetStringField(TEXT("comparison"), Comparison);
		FString CategoryFilter;
		AssertObj->TryGetStringField(TEXT("category"), CategoryFilter);

		// Count keyword occurrences
		int32 ActualCount = 0;
		for (const FMCPLogCapture::FLogEntry& Entry : LogEntries)
		{
			// Optional category filter
			if (!CategoryFilter.IsEmpty() && !Entry.Category.ToString().Contains(CategoryFilter))
			{
				continue;
			}
			if (Entry.Message.Contains(Keyword))
			{
				ActualCount++;
			}
		}

		// Evaluate comparison
		bool bPassed = false;
		if (Comparison == TEXT("=="))
		{
			bPassed = (ActualCount == ExpectedCount);
		}
		else if (Comparison == TEXT(">="))
		{
			bPassed = (ActualCount >= ExpectedCount);
		}
		else if (Comparison == TEXT("<="))
		{
			bPassed = (ActualCount <= ExpectedCount);
		}
		else if (Comparison == TEXT(">"))
		{
			bPassed = (ActualCount > ExpectedCount);
		}
		else if (Comparison == TEXT("<"))
		{
			bPassed = (ActualCount < ExpectedCount);
		}

		if (bPassed)
		{
			PassedCount++;
		}
		else
		{
			FailedCount++;
		}

		TSharedPtr<FJsonObject> AssertResult = MakeShared<FJsonObject>();
		AssertResult->SetStringField(TEXT("keyword"), Keyword);
		AssertResult->SetNumberField(TEXT("expected"), ExpectedCount);
		AssertResult->SetNumberField(TEXT("actual"), ActualCount);
		AssertResult->SetStringField(TEXT("comparison"), Comparison);
		AssertResult->SetBoolField(TEXT("passed"), bPassed);
		ResultsArray.Add(MakeShared<FJsonValueObject>(AssertResult));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("overall"), FailedCount == 0 ? TEXT("pass") : TEXT("fail"));
	Result->SetNumberField(TEXT("total_assertions"), ResultsArray.Num());
	Result->SetNumberField(TEXT("passed"), PassedCount);
	Result->SetNumberField(TEXT("failed"), FailedCount);
	Result->SetArrayField(TEXT("results"), ResultsArray);

	TSharedPtr<FJsonObject> LogRange = MakeShared<FJsonObject>();
	LogRange->SetNumberField(TEXT("from_seq"), static_cast<double>(AfterSeq));
	LogRange->SetNumberField(TEXT("to_seq"), static_cast<double>(LastSeq));
	LogRange->SetNumberField(TEXT("lines_scanned"), LogEntries.Num());
	Result->SetObjectField(TEXT("log_range"), LogRange);

	return CreateSuccessResponse(Result);
}


// =========================================================================
// P6: Outliner Management Actions
// =========================================================================

// ---- Shared helper: find actor by name or label ----

static AActor* FindActorInWorld(UWorld* World, const FString& ActorName)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		// Match by name (GetName)
		if (Actor->GetName().Equals(ActorName, ESearchCase::IgnoreCase))
		{
			return Actor;
		}

		// Match by label (display name in Outliner)
		if (Actor->GetActorLabel().Equals(ActorName, ESearchCase::IgnoreCase))
		{
			return Actor;
		}
	}

	return nullptr;
}

// ---- P6.6 FRenameActorLabelAction ----

bool FRenameActorLabelAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// Need either (actor_name + new_label) or items[]
	const TArray<TSharedPtr<FJsonValue>>* Items = GetOptionalArray(Params, TEXT("items"));
	if (Items && Items->Num() > 0)
	{
		return true;
	}

	FString ActorName = GetOptionalString(Params, TEXT("actor_name"));
	FString NewLabel = GetOptionalString(Params, TEXT("new_label"));
	if (ActorName.IsEmpty() || NewLabel.IsEmpty())
	{
		OutError = TEXT("Either 'items' array or both 'actor_name' and 'new_label' are required");
		return false;
	}
	return true;
}

AActor* FRenameActorLabelAction::FindActorByName(UWorld* World, const FString& ActorName) const
{
	return FindActorInWorld(World, ActorName);
}

TSharedPtr<FJsonObject> FRenameActorLabelAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"));
	}

	struct FRenameItem
	{
		FString ActorName;
		FString NewLabel;
	};

	TArray<FRenameItem> ItemsList;
	const TArray<TSharedPtr<FJsonValue>>* Items = GetOptionalArray(Params, TEXT("items"));
	if (Items && Items->Num() > 0)
	{
		for (const TSharedPtr<FJsonValue>& ItemVal : *Items)
		{
			const TSharedPtr<FJsonObject>* ItemObjPtr;
			if (ItemVal->TryGetObject(ItemObjPtr) && ItemObjPtr)
			{
				FRenameItem Item;
				(*ItemObjPtr)->TryGetStringField(TEXT("actor_name"), Item.ActorName);
				(*ItemObjPtr)->TryGetStringField(TEXT("new_label"), Item.NewLabel);
				if (!Item.ActorName.IsEmpty() && !Item.NewLabel.IsEmpty())
				{
					ItemsList.Add(MoveTemp(Item));
				}
			}
		}
	}
	else
	{
		FRenameItem Item;
		Item.ActorName = GetOptionalString(Params, TEXT("actor_name"));
		Item.NewLabel = GetOptionalString(Params, TEXT("new_label"));
		ItemsList.Add(MoveTemp(Item));
	}

	FScopedTransaction Transaction(FText::FromString(TEXT("MCP Rename Actor Labels")));

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 SuccessCount = 0;

	for (const FRenameItem& Item : ItemsList)
	{
		TSharedPtr<FJsonObject> ItemResult = MakeShared<FJsonObject>();
		ItemResult->SetStringField(TEXT("actor_name"), Item.ActorName);

		AActor* Actor = FindActorByName(World, Item.ActorName);
		if (!Actor)
		{
			ItemResult->SetBoolField(TEXT("success"), false);
			ItemResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor '%s' not found"), *Item.ActorName));
		}
		else
		{
			FString OldLabel = Actor->GetActorLabel();
			Actor->SetActorLabel(Item.NewLabel);
			ItemResult->SetBoolField(TEXT("success"), true);
			ItemResult->SetStringField(TEXT("old_label"), OldLabel);
			ItemResult->SetStringField(TEXT("new_label"), Item.NewLabel);
			SuccessCount++;
		}

		ResultsArray.Add(MakeShared<FJsonValueObject>(ItemResult));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("total"), ItemsList.Num());
	Result->SetNumberField(TEXT("succeeded"), SuccessCount);
	Result->SetArrayField(TEXT("results"), ResultsArray);

	return CreateSuccessResponse(Result);
}

// ---- P6.7 FSetActorFolderAction ----

bool FSetActorFolderAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* Items = GetOptionalArray(Params, TEXT("items"));
	if (Items && Items->Num() > 0)
	{
		return true;
	}

	FString ActorName = GetOptionalString(Params, TEXT("actor_name"));
	FString FolderPath = GetOptionalString(Params, TEXT("folder_path"));
	if (ActorName.IsEmpty())
	{
		OutError = TEXT("Either 'items' array or 'actor_name' is required");
		return false;
	}
	return true;
}

AActor* FSetActorFolderAction::FindActorByName(UWorld* World, const FString& ActorName) const
{
	return FindActorInWorld(World, ActorName);
}

TSharedPtr<FJsonObject> FSetActorFolderAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"));
	}

	struct FFolderItem
	{
		FString ActorName;
		FString FolderPath;
	};

	TArray<FFolderItem> ItemsList;
	const TArray<TSharedPtr<FJsonValue>>* Items = GetOptionalArray(Params, TEXT("items"));
	if (Items && Items->Num() > 0)
	{
		for (const TSharedPtr<FJsonValue>& ItemVal : *Items)
		{
			const TSharedPtr<FJsonObject>* ItemObjPtr;
			if (ItemVal->TryGetObject(ItemObjPtr) && ItemObjPtr)
			{
				FFolderItem Item;
				(*ItemObjPtr)->TryGetStringField(TEXT("actor_name"), Item.ActorName);
				(*ItemObjPtr)->TryGetStringField(TEXT("folder_path"), Item.FolderPath);
				if (!Item.ActorName.IsEmpty())
				{
					ItemsList.Add(MoveTemp(Item));
				}
			}
		}
	}
	else
	{
		FFolderItem Item;
		Item.ActorName = GetOptionalString(Params, TEXT("actor_name"));
		Item.FolderPath = GetOptionalString(Params, TEXT("folder_path"));
		ItemsList.Add(MoveTemp(Item));
	}

	FScopedTransaction Transaction(FText::FromString(TEXT("MCP Set Actor Folders")));

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 SuccessCount = 0;

	for (const FFolderItem& Item : ItemsList)
	{
		TSharedPtr<FJsonObject> ItemResult = MakeShared<FJsonObject>();
		ItemResult->SetStringField(TEXT("actor_name"), Item.ActorName);

		AActor* Actor = FindActorByName(World, Item.ActorName);
		if (!Actor)
		{
			ItemResult->SetBoolField(TEXT("success"), false);
			ItemResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor '%s' not found"), *Item.ActorName));
		}
		else
		{
			FString OldFolder = Actor->GetFolderPath().ToString();
			Actor->SetFolderPath(FName(*Item.FolderPath));
			ItemResult->SetBoolField(TEXT("success"), true);
			ItemResult->SetStringField(TEXT("old_folder"), OldFolder);
			ItemResult->SetStringField(TEXT("new_folder"), Item.FolderPath);
			SuccessCount++;
		}

		ResultsArray.Add(MakeShared<FJsonValueObject>(ItemResult));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("total"), ItemsList.Num());
	Result->SetNumberField(TEXT("succeeded"), SuccessCount);
	Result->SetArrayField(TEXT("results"), ResultsArray);

	return CreateSuccessResponse(Result);
}

// ---- P6.8 FSelectActorsAction ----

bool FSelectActorsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ActorNames = GetOptionalArray(Params, TEXT("actor_names"));
	if (!ActorNames || ActorNames->Num() == 0)
	{
		OutError = TEXT("'actor_names' array is required and must not be empty");
		return false;
	}
	return true;
}

AActor* FSelectActorsAction::FindActorByName(UWorld* World, const FString& ActorName) const
{
	return FindActorInWorld(World, ActorName);
}

TSharedPtr<FJsonObject> FSelectActorsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"));
	}

	FString Mode = GetOptionalString(Params, TEXT("mode"), TEXT("set"));
	const TArray<TSharedPtr<FJsonValue>>* ActorNames = GetOptionalArray(Params, TEXT("actor_names"));

	// If mode is "set", deselect all first
	if (Mode.Equals(TEXT("set"), ESearchCase::IgnoreCase))
	{
		GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true);
	}

	int32 FoundCount = 0;
	int32 NotFoundCount = 0;
	TArray<FString> NotFoundNames;

	for (const TSharedPtr<FJsonValue>& NameVal : *ActorNames)
	{
		FString ActorName;
		if (!NameVal->TryGetString(ActorName) || ActorName.IsEmpty())
		{
			continue;
		}

		AActor* Actor = FindActorByName(World, ActorName);
		if (!Actor)
		{
			NotFoundCount++;
			NotFoundNames.Add(ActorName);
			continue;
		}

		if (Mode.Equals(TEXT("set"), ESearchCase::IgnoreCase) || Mode.Equals(TEXT("add"), ESearchCase::IgnoreCase))
		{
			GEditor->SelectActor(Actor, /*bInSelected=*/true, /*bNotify=*/false);
		}
		else if (Mode.Equals(TEXT("remove"), ESearchCase::IgnoreCase))
		{
			GEditor->SelectActor(Actor, /*bInSelected=*/false, /*bNotify=*/false);
		}
		else if (Mode.Equals(TEXT("toggle"), ESearchCase::IgnoreCase))
		{
			bool bIsSelected = Actor->IsSelected();
			GEditor->SelectActor(Actor, /*bInSelected=*/!bIsSelected, /*bNotify=*/false);
		}

		FoundCount++;
	}

	// Notify after all selection changes
	GEditor->NoteSelectionChange();

	// Count total selected
	int32 SelectedCount = 0;
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		SelectedCount++;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("mode"), Mode);
	Result->SetNumberField(TEXT("requested"), ActorNames->Num());
	Result->SetNumberField(TEXT("found"), FoundCount);
	Result->SetNumberField(TEXT("not_found"), NotFoundCount);
	Result->SetNumberField(TEXT("selected_count"), SelectedCount);
	if (NotFoundNames.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> NotFoundArr;
		for (const FString& Name : NotFoundNames)
		{
			NotFoundArr.Add(MakeShared<FJsonValueString>(Name));
		}
		Result->SetArrayField(TEXT("not_found_names"), NotFoundArr);
	}

	return CreateSuccessResponse(Result);
}

// ---- P6.9 FGetOutlinerTreeAction ----

TSharedPtr<FJsonObject> FGetOutlinerTreeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"));
	}

	FString ClassFilter = GetOptionalString(Params, TEXT("class_filter"));
	FString FolderFilter = GetOptionalString(Params, TEXT("folder_filter"));

	// Organize actors by folder
	TMap<FString, TArray<TSharedPtr<FJsonValue>>> FolderMap;
	TArray<TSharedPtr<FJsonValue>> UnfolderedActors;
	int32 TotalActors = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsA(AWorldSettings::StaticClass()))
		{
			continue;
		}

		FString ClassName = Actor->GetClass()->GetName();

		// Class filter
		if (!ClassFilter.IsEmpty() && !ClassName.Contains(ClassFilter))
		{
			continue;
		}

		FString FolderPath = Actor->GetFolderPath().ToString();

		// Folder filter
		if (!FolderFilter.IsEmpty() && !FolderPath.StartsWith(FolderFilter))
		{
			// If folder doesn't match and actor is not unfoldered, skip
			if (!FolderPath.IsEmpty())
			{
				continue;
			}
		}

		TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
		ActorObj->SetStringField(TEXT("name"), Actor->GetName());
		ActorObj->SetStringField(TEXT("class"), ClassName);
		ActorObj->SetStringField(TEXT("label"), Actor->GetActorLabel());

		TotalActors++;

		if (FolderPath.IsEmpty())
		{
			UnfolderedActors.Add(MakeShared<FJsonValueObject>(ActorObj));
		}
		else
		{
			FolderMap.FindOrAdd(FolderPath).Add(MakeShared<FJsonValueObject>(ActorObj));
		}
	}

	// Build folders array
	TArray<TSharedPtr<FJsonValue>> FoldersArray;
	// Sort folder paths
	TArray<FString> FolderPaths;
	FolderMap.GetKeys(FolderPaths);
	FolderPaths.Sort();

	for (const FString& Path : FolderPaths)
	{
		TSharedPtr<FJsonObject> FolderObj = MakeShared<FJsonObject>();
		FolderObj->SetStringField(TEXT("path"), Path);
		FolderObj->SetArrayField(TEXT("actors"), FolderMap[Path]);
		FolderObj->SetNumberField(TEXT("actor_count"), FolderMap[Path].Num());
		FoldersArray.Add(MakeShared<FJsonValueObject>(FolderObj));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("total_actors"), TotalActors);
	Result->SetNumberField(TEXT("folder_count"), FoldersArray.Num());
	Result->SetArrayField(TEXT("folders"), FoldersArray);
	Result->SetArrayField(TEXT("unfoldered_actors"), UnfolderedActors);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// FOpenAssetEditorAction — Open an asset editor and optionally focus it
// ============================================================================

bool FOpenAssetEditorAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("asset_path")))
	{
		OutError = TEXT("'asset_path' is required (e.g. '/Game/Characters/BP_Hero')");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FOpenAssetEditorAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	bool bFocus = GetOptionalBool(Params, TEXT("focus"), true);

	// 1) Validate GEditor
	if (!GEditor)
	{
		return CreateErrorResponse(TEXT("GEditor is not available"), TEXT("editor_not_ready"));
	}

	// 2) Load the asset
	UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	if (!Asset)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath),
			TEXT("asset_not_found")
		);
	}

	// 3) Get AssetEditorSubsystem
	UAssetEditorSubsystem* AssetEditorSS = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSS)
	{
		return CreateErrorResponse(TEXT("AssetEditorSubsystem is not available"), TEXT("subsystem_error"));
	}

	// 4) Open the editor
	bool bOpened = AssetEditorSS->OpenEditorForAsset(Asset);
	if (!bOpened)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Failed to open editor for asset: %s"), *AssetPath),
			TEXT("open_failed")
		);
	}

	// 5) Optionally focus the editor window
	FString EditorName = TEXT("Unknown");
	if (bFocus)
	{
		IAssetEditorInstance* EditorInstance = AssetEditorSS->FindEditorForAsset(Asset, /*bFocusIfOpen=*/ true);
		if (EditorInstance)
		{
			EditorName = EditorInstance->GetEditorName().ToString();
		}
	}
	else
	{
		IAssetEditorInstance* EditorInstance = AssetEditorSS->FindEditorForAsset(Asset, false);
		if (EditorInstance)
		{
			EditorName = EditorInstance->GetEditorName().ToString();
		}
	}

	// 6) Build result
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_name"), Asset->GetName());
	Result->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
	Result->SetStringField(TEXT("editor_name"), EditorName);
	Result->SetBoolField(TEXT("focused"), bFocus);

	return CreateSuccessResponse(Result);
}
