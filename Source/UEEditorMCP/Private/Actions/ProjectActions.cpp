// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/ProjectActions.h"
#include "MCPCommonUtils.h"
#include "GameFramework/InputSettings.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputActionValue.h"
#include "EnhancedActionKeyMapping.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "EditorAssetLibrary.h"

// =============================================================================
// FCreateInputMappingAction - Legacy input mapping
// =============================================================================

bool FCreateInputMappingAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("action_name")))
	{
		OutError = TEXT("Missing 'action_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("key")))
	{
		OutError = TEXT("Missing 'key' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FCreateInputMappingAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ActionName = Params->GetStringField(TEXT("action_name"));
	FString Key = Params->GetStringField(TEXT("key"));

	FString InputType = TEXT("Action");
	Params->TryGetStringField(TEXT("input_type"), InputType);

	UInputSettings* InputSettings = GetMutableDefault<UInputSettings>();
	if (!InputSettings)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get input settings"));
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (InputType == TEXT("Axis"))
	{
		FInputAxisKeyMapping AxisMapping;
		AxisMapping.AxisName = FName(*ActionName);
		AxisMapping.Key = FKey(*Key);
		AxisMapping.Scale = 1.0f;
		if (Params->HasField(TEXT("scale")))
		{
			AxisMapping.Scale = Params->GetNumberField(TEXT("scale"));
		}

		InputSettings->AddAxisMapping(AxisMapping);
		InputSettings->SaveConfig();
		InputSettings->ForceRebuildKeymaps();

		ResultObj->SetBoolField(TEXT("success"), true);
		ResultObj->SetStringField(TEXT("action_name"), ActionName);
		ResultObj->SetStringField(TEXT("key"), Key);
		ResultObj->SetStringField(TEXT("input_type"), TEXT("Axis"));
		ResultObj->SetNumberField(TEXT("scale"), AxisMapping.Scale);
	}
	else
	{
		FInputActionKeyMapping ActionMapping;
		ActionMapping.ActionName = FName(*ActionName);
		ActionMapping.Key = FKey(*Key);

		if (Params->HasField(TEXT("shift")))
		{
			ActionMapping.bShift = Params->GetBoolField(TEXT("shift"));
		}
		if (Params->HasField(TEXT("ctrl")))
		{
			ActionMapping.bCtrl = Params->GetBoolField(TEXT("ctrl"));
		}
		if (Params->HasField(TEXT("alt")))
		{
			ActionMapping.bAlt = Params->GetBoolField(TEXT("alt"));
		}
		if (Params->HasField(TEXT("cmd")))
		{
			ActionMapping.bCmd = Params->GetBoolField(TEXT("cmd"));
		}

		InputSettings->AddActionMapping(ActionMapping);
		InputSettings->SaveConfig();
		InputSettings->ForceRebuildKeymaps();

		ResultObj->SetBoolField(TEXT("success"), true);
		ResultObj->SetStringField(TEXT("action_name"), ActionName);
		ResultObj->SetStringField(TEXT("key"), Key);
		ResultObj->SetStringField(TEXT("input_type"), TEXT("Action"));
	}

	return ResultObj;
}

// =============================================================================
// FCreateInputActionAction - Enhanced Input Action asset
// =============================================================================

bool FCreateInputActionAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("name")))
	{
		OutError = TEXT("Missing 'name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FCreateInputActionAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Name = Params->GetStringField(TEXT("name"));

	FString ValueTypeStr = TEXT("Boolean");
	Params->TryGetStringField(TEXT("value_type"), ValueTypeStr);

	// Map string to enum
	EInputActionValueType ValueType = EInputActionValueType::Boolean;
	if (ValueTypeStr == TEXT("Axis1D") || ValueTypeStr == TEXT("Float"))
	{
		ValueType = EInputActionValueType::Axis1D;
	}
	else if (ValueTypeStr == TEXT("Axis2D") || ValueTypeStr == TEXT("Vector2D"))
	{
		ValueType = EInputActionValueType::Axis2D;
	}
	else if (ValueTypeStr == TEXT("Axis3D") || ValueTypeStr == TEXT("Vector"))
	{
		ValueType = EInputActionValueType::Axis3D;
	}

	// Create package path
	FString Path = TEXT("/Game/Input");
	Params->TryGetStringField(TEXT("path"), Path);
	FString PackagePath = Path / Name;

	// Check if asset already exists and clean up safely
	UPackage* ExistingPackage = FindPackage(nullptr, *PackagePath);
	if (ExistingPackage)
	{
		UInputAction* ExistingAction = FindObject<UInputAction>(ExistingPackage, *Name);
		if (ExistingAction)
		{
			UE_LOG(LogMCP, Log, TEXT("Input Action '%s' already exists, cleaning up for recreation"), *Name);
			FString TempName = FString::Printf(TEXT("%s_TEMP_%d"), *Name, FMath::Rand());
			ExistingAction->Rename(*TempName, GetTransientPackage(), REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
			ExistingAction->MarkAsGarbage();
			ExistingPackage->MarkAsGarbage();
		}
	}

	// Delete from disk if exists
	if (UEditorAssetLibrary::DoesAssetExist(PackagePath))
	{
		UE_LOG(LogMCP, Log, TEXT("Input Action '%s' exists on disk, deleting"), *Name);
		UEditorAssetLibrary::DeleteAsset(PackagePath);
	}

	// Create the package
	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();

	// Create the Input Action
	UInputAction* NewAction = NewObject<UInputAction>(Package, *Name, RF_Public | RF_Standalone);
	if (!NewAction)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Input Action"));
	}

	NewAction->ValueType = ValueType;

	// Register with asset registry and save
	FAssetRegistryModule::AssetCreated(NewAction);
	NewAction->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, NewAction, *PackageFilename, SaveArgs);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("name"), Name);
	ResultObj->SetStringField(TEXT("path"), PackagePath);
	ResultObj->SetStringField(TEXT("value_type"), ValueTypeStr);
	return ResultObj;
}

// =============================================================================
// FCreateInputMappingContextAction - Input Mapping Context asset
// =============================================================================

bool FCreateInputMappingContextAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("name")))
	{
		OutError = TEXT("Missing 'name' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FCreateInputMappingContextAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Name = Params->GetStringField(TEXT("name"));

	// Create package path
	FString Path = TEXT("/Game/Input");
	Params->TryGetStringField(TEXT("path"), Path);
	FString PackagePath = Path / Name;

	// Check if asset already exists and clean up safely
	UPackage* ExistingPackage = FindPackage(nullptr, *PackagePath);
	if (ExistingPackage)
	{
		UInputMappingContext* ExistingIMC = FindObject<UInputMappingContext>(ExistingPackage, *Name);
		if (ExistingIMC)
		{
			UE_LOG(LogMCP, Log, TEXT("Input Mapping Context '%s' already exists, cleaning up for recreation"), *Name);
			FString TempName = FString::Printf(TEXT("%s_TEMP_%d"), *Name, FMath::Rand());
			ExistingIMC->Rename(*TempName, GetTransientPackage(), REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
			ExistingIMC->MarkAsGarbage();
			ExistingPackage->MarkAsGarbage();
		}
	}

	// Delete from disk if exists
	if (UEditorAssetLibrary::DoesAssetExist(PackagePath))
	{
		UE_LOG(LogMCP, Log, TEXT("Input Mapping Context '%s' exists on disk, deleting"), *Name);
		UEditorAssetLibrary::DeleteAsset(PackagePath);
	}

	// Create the package
	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();

	// Create the Input Mapping Context
	UInputMappingContext* NewIMC = NewObject<UInputMappingContext>(Package, *Name, RF_Public | RF_Standalone);
	if (!NewIMC)
	{
		return FMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Input Mapping Context"));
	}

	// Register with asset registry and save
	FAssetRegistryModule::AssetCreated(NewIMC);
	NewIMC->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, NewIMC, *PackageFilename, SaveArgs);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("name"), Name);
	ResultObj->SetStringField(TEXT("path"), PackagePath);
	return ResultObj;
}

// =============================================================================
// FAddKeyMappingToContextAction - Add key to IMC with modifiers
// =============================================================================

bool FAddKeyMappingToContextAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!Params->HasField(TEXT("context_name")))
	{
		OutError = TEXT("Missing 'context_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("action_name")))
	{
		OutError = TEXT("Missing 'action_name' parameter");
		return false;
	}
	if (!Params->HasField(TEXT("key")))
	{
		OutError = TEXT("Missing 'key' parameter");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddKeyMappingToContextAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ContextName = Params->GetStringField(TEXT("context_name"));
	FString ActionName = Params->GetStringField(TEXT("action_name"));
	FString KeyStr = Params->GetStringField(TEXT("key"));

	// Find the IMC asset
	FString ContextPath = TEXT("/Game/Input");
	Params->TryGetStringField(TEXT("context_path"), ContextPath);
	FString FullContextPath = ContextPath / ContextName + TEXT(".") + ContextName;

	UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *FullContextPath);
	if (!IMC)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Input Mapping Context not found: %s"), *FullContextPath));
	}

	// Find the Input Action asset
	FString ActionPath = TEXT("/Game/Input");
	Params->TryGetStringField(TEXT("action_path"), ActionPath);
	FString FullActionPath = ActionPath / ActionName + TEXT(".") + ActionName;

	UInputAction* Action = LoadObject<UInputAction>(nullptr, *FullActionPath);
	if (!Action)
	{
		return FMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Input Action not found: %s"), *FullActionPath));
	}

	// Map the key to the action
	FKey MappedKey{FName{*KeyStr}};
	FEnhancedActionKeyMapping& Mapping = IMC->MapKey(Action, MappedKey);

	// Apply modifiers if specified
	if (Params->HasField(TEXT("modifiers")))
	{
		const TArray<TSharedPtr<FJsonValue>>& ModifiersArray = Params->GetArrayField(TEXT("modifiers"));
		for (const TSharedPtr<FJsonValue>& ModValue : ModifiersArray)
		{
			FString ModName = ModValue->AsString();

			if (ModName == TEXT("Negate"))
			{
				UInputModifierNegate* Mod = NewObject<UInputModifierNegate>(IMC);
				Mapping.Modifiers.Add(Mod);
			}
			else if (ModName == TEXT("SwizzleYXZ") || ModName == TEXT("Swizzle"))
			{
				UInputModifierSwizzleAxis* Mod = NewObject<UInputModifierSwizzleAxis>(IMC);
				Mod->Order = EInputAxisSwizzle::YXZ;
				Mapping.Modifiers.Add(Mod);
			}
			else if (ModName == TEXT("SwizzleZYX"))
			{
				UInputModifierSwizzleAxis* Mod = NewObject<UInputModifierSwizzleAxis>(IMC);
				Mod->Order = EInputAxisSwizzle::ZYX;
				Mapping.Modifiers.Add(Mod);
			}
			else if (ModName == TEXT("SwizzleXZY"))
			{
				UInputModifierSwizzleAxis* Mod = NewObject<UInputModifierSwizzleAxis>(IMC);
				Mod->Order = EInputAxisSwizzle::XZY;
				Mapping.Modifiers.Add(Mod);
			}
			else if (ModName == TEXT("SwizzleYZX"))
			{
				UInputModifierSwizzleAxis* Mod = NewObject<UInputModifierSwizzleAxis>(IMC);
				Mod->Order = EInputAxisSwizzle::YZX;
				Mapping.Modifiers.Add(Mod);
			}
			else if (ModName == TEXT("SwizzleZXY"))
			{
				UInputModifierSwizzleAxis* Mod = NewObject<UInputModifierSwizzleAxis>(IMC);
				Mod->Order = EInputAxisSwizzle::ZXY;
				Mapping.Modifiers.Add(Mod);
			}
		}
	}

	// Save the IMC package
	IMC->MarkPackageDirty();
	UPackage* Package = IMC->GetOutermost();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, IMC, *PackageFilename, SaveArgs);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("context"), ContextName);
	ResultObj->SetStringField(TEXT("action"), ActionName);
	ResultObj->SetStringField(TEXT("key"), KeyStr);
	return ResultObj;
}
