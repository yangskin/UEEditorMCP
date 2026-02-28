// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/MaterialActions.h"
#include "MCPContext.h"

// Material system headers
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionSquareRoot.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionFloor.h"
#include "Materials/MaterialExpressionCeil.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionCosine.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionCrossProduct.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionSceneDepth.h"
#include "Materials/MaterialExpressionDDX.h"
#include "Materials/MaterialExpressionDDY.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionStep.h"
#include "Materials/MaterialExpressionSmoothStep.h"
#include "Materials/MaterialExpressionMin.h"
#include "Materials/MaterialExpressionMax.h"
#include "Materials/MaterialExpressionTextureSample.h"
// P4.3: TextureParameter
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
// P4.7: StaticSwitchParameter
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionStaticComponentMaskParameter.h"
// P4.8: MaterialFunction
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
// P4.5: Material comment
#include "Materials/MaterialExpressionComment.h"
// P4.4: Material graph
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "MaterialGraph/MaterialGraphNode_Root.h"
// P4.5+: Material editor graph selection
#include "GraphEditor.h"
// Layout utilities (P4.4 reuse)
#include "Actions/LayoutActions.h"
// Shared pin-aware layer sorting (P4.4 unified)
#include "MaterialLayoutUtils.h"

// Factory and editing
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "MaterialEditingLibrary.h"

// Editor and asset utilities
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "UObject/SavePackage.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Kismet/GameplayStatics.h"

// Post process volume
#include "Engine/PostProcessVolume.h"
#include "Components/PostProcessComponent.h"
#include "EngineUtils.h"  // For TActorIterator
#include "ComponentReregisterContext.h"  // For FGlobalComponentReregisterContext

// P5.1: Shader compilation manager (for material compile error retrieval)
#include "ShaderCompiler.h"
// P5.3: AssetEditorSubsystem (for finding preview material in editor)
#include "Subsystems/AssetEditorSubsystem.h"
// P5.3+: IMaterialEditor public API for selection queries across multiple open editors
#include "IMaterialEditor.h"
#include "MaterialEditorUtilities.h"
// P5.2/P5.4: Material apply to component/actor
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"

// =========================================================================
// Expression Class Mapping
// =========================================================================

static TMap<FString, UClass*> ExpressionClassMap;
static TMap<FString, EMaterialShadingModel> ShadingModelMap;
static TMap<FString, EBlendMode> BlendModeMap;

static bool IsInvalidMaterialParameterName(const FString& InName)
{
	const FString Trimmed = InName.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return true;
	}

	return FName(*Trimmed).IsNone();
}

static bool ValidateParameterOverrideKeys(const TSharedPtr<FJsonObject>& Params, const FString& FieldName, FString& OutError)
{
	if (!Params.IsValid() || !Params->HasField(FieldName))
	{
		return true;
	}

	const TSharedPtr<FJsonObject> ParamObject = Params->GetObjectField(FieldName);
	for (const auto& Pair : ParamObject->Values)
	{
		if (IsInvalidMaterialParameterName(Pair.Key))
		{
			OutError = FString::Printf(TEXT("Invalid parameter name '%s' in '%s'. Parameter names cannot be empty or None/NAME_None."), *Pair.Key, *FieldName);
			return false;
		}
	}

	return true;
}

static void InitShadingModelMap()
{
	if (ShadingModelMap.Num() > 0) return;

	ShadingModelMap.Add(TEXT("Unlit"), MSM_Unlit);
	ShadingModelMap.Add(TEXT("MSM_Unlit"), MSM_Unlit);
	ShadingModelMap.Add(TEXT("DefaultLit"), MSM_DefaultLit);
	ShadingModelMap.Add(TEXT("MSM_DefaultLit"), MSM_DefaultLit);
	ShadingModelMap.Add(TEXT("Lit"), MSM_DefaultLit);
	ShadingModelMap.Add(TEXT("Subsurface"), MSM_Subsurface);
	ShadingModelMap.Add(TEXT("MSM_Subsurface"), MSM_Subsurface);
	ShadingModelMap.Add(TEXT("PreintegratedSkin"), MSM_PreintegratedSkin);
	ShadingModelMap.Add(TEXT("MSM_PreintegratedSkin"), MSM_PreintegratedSkin);
	ShadingModelMap.Add(TEXT("ClearCoat"), MSM_ClearCoat);
	ShadingModelMap.Add(TEXT("MSM_ClearCoat"), MSM_ClearCoat);
	ShadingModelMap.Add(TEXT("SubsurfaceProfile"), MSM_SubsurfaceProfile);
	ShadingModelMap.Add(TEXT("MSM_SubsurfaceProfile"), MSM_SubsurfaceProfile);
	ShadingModelMap.Add(TEXT("TwoSidedFoliage"), MSM_TwoSidedFoliage);
	ShadingModelMap.Add(TEXT("MSM_TwoSidedFoliage"), MSM_TwoSidedFoliage);
	ShadingModelMap.Add(TEXT("Hair"), MSM_Hair);
	ShadingModelMap.Add(TEXT("MSM_Hair"), MSM_Hair);
	ShadingModelMap.Add(TEXT("Cloth"), MSM_Cloth);
	ShadingModelMap.Add(TEXT("MSM_Cloth"), MSM_Cloth);
	ShadingModelMap.Add(TEXT("Eye"), MSM_Eye);
	ShadingModelMap.Add(TEXT("MSM_Eye"), MSM_Eye);
}

static void InitBlendModeMap()
{
	if (BlendModeMap.Num() > 0) return;

	BlendModeMap.Add(TEXT("Opaque"), BLEND_Opaque);
	BlendModeMap.Add(TEXT("BLEND_Opaque"), BLEND_Opaque);
	BlendModeMap.Add(TEXT("Masked"), BLEND_Masked);
	BlendModeMap.Add(TEXT("BLEND_Masked"), BLEND_Masked);
	BlendModeMap.Add(TEXT("Translucent"), BLEND_Translucent);
	BlendModeMap.Add(TEXT("BLEND_Translucent"), BLEND_Translucent);
	BlendModeMap.Add(TEXT("Additive"), BLEND_Additive);
	BlendModeMap.Add(TEXT("BLEND_Additive"), BLEND_Additive);
	BlendModeMap.Add(TEXT("Modulate"), BLEND_Modulate);
	BlendModeMap.Add(TEXT("BLEND_Modulate"), BLEND_Modulate);
	BlendModeMap.Add(TEXT("AlphaComposite"), BLEND_AlphaComposite);
	BlendModeMap.Add(TEXT("BLEND_AlphaComposite"), BLEND_AlphaComposite);
	BlendModeMap.Add(TEXT("AlphaHoldout"), BLEND_AlphaHoldout);
	BlendModeMap.Add(TEXT("BLEND_AlphaHoldout"), BLEND_AlphaHoldout);
}

static void InitExpressionClassMap()
{
	if (ExpressionClassMap.Num() > 0) return; // Already initialized

	// Scene/Texture access
	ExpressionClassMap.Add(TEXT("SceneTexture"), UMaterialExpressionSceneTexture::StaticClass());
	ExpressionClassMap.Add(TEXT("SceneDepth"), UMaterialExpressionSceneDepth::StaticClass());
	ExpressionClassMap.Add(TEXT("ScreenPosition"), UMaterialExpressionScreenPosition::StaticClass());
	ExpressionClassMap.Add(TEXT("TextureCoordinate"), UMaterialExpressionTextureCoordinate::StaticClass());
	ExpressionClassMap.Add(TEXT("TextureSample"), UMaterialExpressionTextureSample::StaticClass());
	ExpressionClassMap.Add(TEXT("PixelDepth"), UMaterialExpressionPixelDepth::StaticClass());
	ExpressionClassMap.Add(TEXT("WorldPosition"), UMaterialExpressionWorldPosition::StaticClass());
	ExpressionClassMap.Add(TEXT("CameraPosition"), UMaterialExpressionCameraPositionWS::StaticClass());

	// Math operations
	ExpressionClassMap.Add(TEXT("Add"), UMaterialExpressionAdd::StaticClass());
	ExpressionClassMap.Add(TEXT("Subtract"), UMaterialExpressionSubtract::StaticClass());
	ExpressionClassMap.Add(TEXT("Multiply"), UMaterialExpressionMultiply::StaticClass());
	ExpressionClassMap.Add(TEXT("Divide"), UMaterialExpressionDivide::StaticClass());
	ExpressionClassMap.Add(TEXT("Power"), UMaterialExpressionPower::StaticClass());
	ExpressionClassMap.Add(TEXT("SquareRoot"), UMaterialExpressionSquareRoot::StaticClass());
	ExpressionClassMap.Add(TEXT("Abs"), UMaterialExpressionAbs::StaticClass());
	ExpressionClassMap.Add(TEXT("Min"), UMaterialExpressionMin::StaticClass());
	ExpressionClassMap.Add(TEXT("Max"), UMaterialExpressionMax::StaticClass());
	ExpressionClassMap.Add(TEXT("Clamp"), UMaterialExpressionClamp::StaticClass());
	ExpressionClassMap.Add(TEXT("Saturate"), UMaterialExpressionSaturate::StaticClass());
	ExpressionClassMap.Add(TEXT("Floor"), UMaterialExpressionFloor::StaticClass());
	ExpressionClassMap.Add(TEXT("Ceil"), UMaterialExpressionCeil::StaticClass());
	ExpressionClassMap.Add(TEXT("Frac"), UMaterialExpressionFrac::StaticClass());
	ExpressionClassMap.Add(TEXT("OneMinus"), UMaterialExpressionOneMinus::StaticClass());
	ExpressionClassMap.Add(TEXT("Step"), UMaterialExpressionStep::StaticClass());
	ExpressionClassMap.Add(TEXT("SmoothStep"), UMaterialExpressionSmoothStep::StaticClass());

	// Trigonometry
	ExpressionClassMap.Add(TEXT("Sin"), UMaterialExpressionSine::StaticClass());
	ExpressionClassMap.Add(TEXT("Cos"), UMaterialExpressionCosine::StaticClass());

	// Vector operations
	ExpressionClassMap.Add(TEXT("DotProduct"), UMaterialExpressionDotProduct::StaticClass());
	ExpressionClassMap.Add(TEXT("CrossProduct"), UMaterialExpressionCrossProduct::StaticClass());
	ExpressionClassMap.Add(TEXT("Normalize"), UMaterialExpressionNormalize::StaticClass());
	ExpressionClassMap.Add(TEXT("AppendVector"), UMaterialExpressionAppendVector::StaticClass());
	ExpressionClassMap.Add(TEXT("ComponentMask"), UMaterialExpressionComponentMask::StaticClass());

	// Constants
	ExpressionClassMap.Add(TEXT("Constant"), UMaterialExpressionConstant::StaticClass());
	ExpressionClassMap.Add(TEXT("Constant2Vector"), UMaterialExpressionConstant2Vector::StaticClass());
	ExpressionClassMap.Add(TEXT("Constant3Vector"), UMaterialExpressionConstant3Vector::StaticClass());
	ExpressionClassMap.Add(TEXT("Constant4Vector"), UMaterialExpressionConstant4Vector::StaticClass());

	// Parameters
	ExpressionClassMap.Add(TEXT("ScalarParameter"), UMaterialExpressionScalarParameter::StaticClass());
	ExpressionClassMap.Add(TEXT("VectorParameter"), UMaterialExpressionVectorParameter::StaticClass());

	// Procedural
	ExpressionClassMap.Add(TEXT("Noise"), UMaterialExpressionNoise::StaticClass());
	ExpressionClassMap.Add(TEXT("Time"), UMaterialExpressionTime::StaticClass());
	ExpressionClassMap.Add(TEXT("Panner"), UMaterialExpressionPanner::StaticClass());

	// Derivatives
	ExpressionClassMap.Add(TEXT("DDX"), UMaterialExpressionDDX::StaticClass());
	ExpressionClassMap.Add(TEXT("DDY"), UMaterialExpressionDDY::StaticClass());

	// Control
	ExpressionClassMap.Add(TEXT("If"), UMaterialExpressionIf::StaticClass());
	ExpressionClassMap.Add(TEXT("Lerp"), UMaterialExpressionLinearInterpolate::StaticClass());
	ExpressionClassMap.Add(TEXT("LinearInterpolate"), UMaterialExpressionLinearInterpolate::StaticClass());

	// Custom HLSL
	ExpressionClassMap.Add(TEXT("Custom"), UMaterialExpressionCustom::StaticClass());

	// P4.3: Texture parameters
	ExpressionClassMap.Add(TEXT("TextureParameter"), UMaterialExpressionTextureObjectParameter::StaticClass());
	ExpressionClassMap.Add(TEXT("TextureObjectParameter"), UMaterialExpressionTextureObjectParameter::StaticClass());
	ExpressionClassMap.Add(TEXT("TextureSampleParameter2D"), UMaterialExpressionTextureSampleParameter2D::StaticClass());

	// P4.7: Static switch parameter
	ExpressionClassMap.Add(TEXT("StaticSwitchParameter"), UMaterialExpressionStaticSwitchParameter::StaticClass());
	ExpressionClassMap.Add(TEXT("StaticComponentMaskParameter"), UMaterialExpressionStaticComponentMaskParameter::StaticClass());

	// P4.8: MaterialFunction references
	ExpressionClassMap.Add(TEXT("MaterialFunctionCall"), UMaterialExpressionMaterialFunctionCall::StaticClass());
}

// =========================================================================
// FMaterialAction - Base Class
// =========================================================================

UMaterial* FMaterialAction::FindMaterial(const FString& MaterialName, FString& OutError) const
{
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

	OutError = FString::Printf(TEXT("Material '%s' not found"), *MaterialName);
	return nullptr;
}

UMaterial* FMaterialAction::GetMaterialByNameOrCurrent(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) const
{
	FString MaterialName = GetOptionalString(Params, TEXT("material_name"));

	UMaterial* Result = nullptr;
	if (MaterialName.IsEmpty())
	{
		Result = Context.CurrentMaterial.Get();
		if (!Result)
		{
			OutError = TEXT("No current material set. Specify material_name or create a material first.");
		}
	}
	else
	{
		Result = FindMaterial(MaterialName, OutError);
	}

	// Always update the context so GetMaterialNode() fallback can resolve
	// $expr_N and other expression names against this material
	if (Result)
	{
		Context.SetCurrentMaterial(Result);
	}

	return Result;
}

void FMaterialAction::CleanupExistingMaterial(const FString& MaterialName, const FString& PackagePath) const
{
	UPackage* ExistingPackage = FindPackage(nullptr, *PackagePath);
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

	if (UEditorAssetLibrary::DoesAssetExist(PackagePath))
	{
		UEditorAssetLibrary::DeleteAsset(PackagePath);
	}
}

UClass* FMaterialAction::ResolveExpressionClass(const FString& ExpressionClassName) const
{
	InitExpressionClassMap();

	if (UClass** Found = ExpressionClassMap.Find(ExpressionClassName))
	{
		return *Found;
	}

	// Try direct class lookup
	FString ClassName = TEXT("MaterialExpression") + ExpressionClassName;
	UClass* FoundClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *ClassName));
	return FoundClass;
}

void FMaterialAction::MarkMaterialModified(UMaterial* Material, FMCPEditorContext& Context) const
{
	if (Material)
	{
		Material->PreEditChange(nullptr);
		Material->PostEditChange();

		// Reregister components to apply changes
		{
			FGlobalComponentReregisterContext RecreateComponents;
		}

		Material->MarkPackageDirty();
		Context.MarkPackageDirty(Material->GetOutermost());
	}
}

// =========================================================================
// FCreateMaterialAction
// =========================================================================

bool FCreateMaterialAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MaterialName;
	return GetRequiredString(Params, TEXT("material_name"), MaterialName, OutError);
}

TOptional<EMaterialDomain> FCreateMaterialAction::ResolveDomain(const FString& DomainString) const
{
	if (DomainString.IsEmpty() || DomainString.Equals(TEXT("Surface"), ESearchCase::IgnoreCase))
		return MD_Surface;
	if (DomainString.Equals(TEXT("PostProcess"), ESearchCase::IgnoreCase))
		return MD_PostProcess;
	if (DomainString.Equals(TEXT("DeferredDecal"), ESearchCase::IgnoreCase))
		return MD_DeferredDecal;
	if (DomainString.Equals(TEXT("LightFunction"), ESearchCase::IgnoreCase))
		return MD_LightFunction;
	if (DomainString.Equals(TEXT("UI"), ESearchCase::IgnoreCase))
		return MD_UI;
	if (DomainString.Equals(TEXT("Volume"), ESearchCase::IgnoreCase))
		return MD_Volume;

	return TOptional<EMaterialDomain>();
}

TOptional<EBlendMode> FCreateMaterialAction::ResolveBlendMode(const FString& BlendModeString) const
{
	if (BlendModeString.IsEmpty() || BlendModeString.Equals(TEXT("Opaque"), ESearchCase::IgnoreCase))
		return BLEND_Opaque;
	if (BlendModeString.Equals(TEXT("Masked"), ESearchCase::IgnoreCase))
		return BLEND_Masked;
	if (BlendModeString.Equals(TEXT("Translucent"), ESearchCase::IgnoreCase))
		return BLEND_Translucent;
	if (BlendModeString.Equals(TEXT("Additive"), ESearchCase::IgnoreCase))
		return BLEND_Additive;
	if (BlendModeString.Equals(TEXT("Modulate"), ESearchCase::IgnoreCase))
		return BLEND_Modulate;
	if (BlendModeString.Equals(TEXT("AlphaComposite"), ESearchCase::IgnoreCase))
		return BLEND_AlphaComposite;
	if (BlendModeString.Equals(TEXT("AlphaHoldout"), ESearchCase::IgnoreCase))
		return BLEND_AlphaHoldout;

	return TOptional<EBlendMode>();
}

TOptional<EBlendableLocation> FCreateMaterialAction::ResolveBlendableLocation(const FString& LocationString) const
{
	if (LocationString.IsEmpty() || LocationString.Equals(TEXT("AfterTonemapping"), ESearchCase::IgnoreCase) ||
		LocationString.Equals(TEXT("BL_AfterTonemapping"), ESearchCase::IgnoreCase) ||
		LocationString.Equals(TEXT("BL_SceneColorAfterTonemapping"), ESearchCase::IgnoreCase))
		return BL_SceneColorAfterTonemapping;
	if (LocationString.Equals(TEXT("BeforeTonemapping"), ESearchCase::IgnoreCase) ||
		LocationString.Equals(TEXT("BL_BeforeTonemapping"), ESearchCase::IgnoreCase) ||
		LocationString.Equals(TEXT("BL_SceneColorAfterDOF"), ESearchCase::IgnoreCase))
		return BL_SceneColorAfterDOF;
	if (LocationString.Equals(TEXT("BeforeTranslucency"), ESearchCase::IgnoreCase) ||
		LocationString.Equals(TEXT("BL_BeforeTranslucency"), ESearchCase::IgnoreCase) ||
		LocationString.Equals(TEXT("BL_SceneColorBeforeDOF"), ESearchCase::IgnoreCase))
		return BL_SceneColorBeforeDOF;
	if (LocationString.Equals(TEXT("ReplacingTonemapper"), ESearchCase::IgnoreCase) ||
		LocationString.Equals(TEXT("BL_ReplacingTonemapper"), ESearchCase::IgnoreCase))
		return BL_ReplacingTonemapper;
	if (LocationString.Equals(TEXT("SSRInput"), ESearchCase::IgnoreCase) ||
		LocationString.Equals(TEXT("BL_SSRInput"), ESearchCase::IgnoreCase))
		return BL_SSRInput;

	return TOptional<EBlendableLocation>();
}

TSharedPtr<FJsonObject> FCreateMaterialAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString MaterialName;
	GetRequiredString(Params, TEXT("material_name"), MaterialName, Error);

	FString Path = GetOptionalString(Params, TEXT("path"), TEXT("/Game/Materials"));
	FString DomainStr = GetOptionalString(Params, TEXT("domain"), TEXT("Surface"));
	FString BlendModeStr = GetOptionalString(Params, TEXT("blend_mode"), TEXT("Opaque"));
	FString BlendableLocationStr = GetOptionalString(Params, TEXT("blendable_location"), TEXT(""));

	// Resolve domain
	TOptional<EMaterialDomain> Domain = ResolveDomain(DomainStr);
	if (!Domain.IsSet())
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Invalid domain '%s'. Valid: Surface, PostProcess, DeferredDecal, LightFunction, UI, Volume"), *DomainStr),
			TEXT("invalid_domain"));
	}

	// Resolve blend mode
	TOptional<EBlendMode> BlendMode = ResolveBlendMode(BlendModeStr);
	if (!BlendMode.IsSet())
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Invalid blend_mode '%s'. Valid: Opaque, Masked, Translucent, Additive, Modulate"), *BlendModeStr),
			TEXT("invalid_blend_mode"));
	}

	// Build package path
	FString MaterialPackagePath = Path / MaterialName;

	// Clean up existing material
	CleanupExistingMaterial(MaterialName, MaterialPackagePath);

	// Create package
	UPackage* Package = CreatePackage(*MaterialPackagePath);
	if (!Package)
	{
		return CreateErrorResponse(TEXT("Failed to create package for material"), TEXT("package_creation_failed"));
	}
	Package->FullyLoad();

	// Create material using factory
	UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();
	UMaterial* NewMaterial = Cast<UMaterial>(MaterialFactory->FactoryCreateNew(
		UMaterial::StaticClass(), Package, *MaterialName,
		RF_Public | RF_Standalone, nullptr, GWarn));

	if (!NewMaterial)
	{
		return CreateErrorResponse(TEXT("Failed to create material"), TEXT("material_creation_failed"));
	}

	// Set domain and blend mode
	NewMaterial->MaterialDomain = Domain.GetValue();
	NewMaterial->BlendMode = BlendMode.GetValue();

	// Set blendable location for post-process materials
	if (!BlendableLocationStr.IsEmpty())
	{
		TOptional<EBlendableLocation> BlendableLocation = ResolveBlendableLocation(BlendableLocationStr);
		if (BlendableLocation.IsSet())
		{
			NewMaterial->BlendableLocation = BlendableLocation.GetValue();
		}
	}
	else if (Domain.GetValue() == MD_PostProcess)
	{
		// Default to SceneColorAfterDOF for post-process materials (needed for depth access)
		NewMaterial->BlendableLocation = BL_SceneColorAfterDOF;
	}

	// Trigger compilation
	NewMaterial->PreEditChange(nullptr);
	NewMaterial->PostEditChange();

	// Register and mark dirty
	Package->SetDirtyFlag(true);
	NewMaterial->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewMaterial);

	// Update context
	Context.SetCurrentMaterial(NewMaterial);
	Context.MarkPackageDirty(Package);

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), MaterialName);
	Result->SetStringField(TEXT("path"), MaterialPackagePath);
	Result->SetStringField(TEXT("domain"), DomainStr);
	Result->SetStringField(TEXT("blend_mode"), BlendModeStr);

	return CreateSuccessResponse(Result);
}

// =========================================================================
// FAddMaterialExpressionAction
// =========================================================================

bool FAddMaterialExpressionAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ExpressionClass, NodeName;
	if (!GetRequiredString(Params, TEXT("expression_class"), ExpressionClass, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("node_name"), NodeName, OutError)) return false;
	if (IsInvalidMaterialParameterName(NodeName))
	{
		OutError = FString::Printf(TEXT("Invalid node_name '%s'. node_name cannot be empty or None/NAME_None."), *NodeName);
		return false;
	}
	return true;
}

void FAddMaterialExpressionAction::SetExpressionProperties(UMaterialExpression* Expression, const TSharedPtr<FJsonObject>& Properties) const
{
	if (!Properties.IsValid() || !Expression) return;

	for (const auto& Pair : Properties->Values)
	{
		const FString& PropName = Pair.Key;
		const TSharedPtr<FJsonValue>& PropValue = Pair.Value;

		// Handle specific expression types and their properties
		// SceneTexture
		if (UMaterialExpressionSceneTexture* SceneTex = Cast<UMaterialExpressionSceneTexture>(Expression))
		{
			if (PropName == TEXT("SceneTextureId"))
			{
				FString IdStr = PropValue->AsString();
				if (IdStr == TEXT("PPI_SceneColor")) SceneTex->SceneTextureId = PPI_SceneColor;
				else if (IdStr == TEXT("PPI_SceneDepth")) SceneTex->SceneTextureId = PPI_SceneDepth;
				else if (IdStr == TEXT("PPI_WorldNormal")) SceneTex->SceneTextureId = PPI_WorldNormal;
				else if (IdStr == TEXT("PPI_PostProcessInput0")) SceneTex->SceneTextureId = PPI_PostProcessInput0;
			}
		}
		// Scalar Parameter
		else if (UMaterialExpressionScalarParameter* ScalarParam = Cast<UMaterialExpressionScalarParameter>(Expression))
		{
			if (PropName == TEXT("ParameterName")) ScalarParam->ParameterName = FName(*PropValue->AsString());
			else if (PropName == TEXT("DefaultValue")) ScalarParam->DefaultValue = PropValue->AsNumber();
		}
		// Vector Parameter
		else if (UMaterialExpressionVectorParameter* VectorParam = Cast<UMaterialExpressionVectorParameter>(Expression))
		{
			if (PropName == TEXT("ParameterName")) VectorParam->ParameterName = FName(*PropValue->AsString());
			else if (PropName == TEXT("DefaultValue"))
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr;
				if (PropValue->TryGetArray(Arr) && Arr->Num() >= 3)
				{
					VectorParam->DefaultValue = FLinearColor(
						(*Arr)[0]->AsNumber(),
						(*Arr)[1]->AsNumber(),
						(*Arr)[2]->AsNumber(),
						Arr->Num() > 3 ? (*Arr)[3]->AsNumber() : 1.0f);
				}
			}
		}
		// Constant
		else if (UMaterialExpressionConstant* Const = Cast<UMaterialExpressionConstant>(Expression))
		{
			if (PropName == TEXT("R") || PropName == TEXT("Value")) Const->R = PropValue->AsNumber();
		}
		// Constant3Vector
		else if (UMaterialExpressionConstant3Vector* Const3 = Cast<UMaterialExpressionConstant3Vector>(Expression))
		{
			if (PropName == TEXT("Constant"))
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr;
				if (PropValue->TryGetArray(Arr) && Arr->Num() >= 3)
				{
					Const3->Constant = FLinearColor(
						(*Arr)[0]->AsNumber(),
						(*Arr)[1]->AsNumber(),
						(*Arr)[2]->AsNumber());
				}
			}
		}
		// Custom HLSL
		else if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression))
		{
			if (PropName == TEXT("Code")) Custom->Code = PropValue->AsString();
			else if (PropName == TEXT("Description")) Custom->Description = PropValue->AsString();
			else if (PropName == TEXT("OutputType"))
			{
				FString TypeStr = PropValue->AsString();
				if (TypeStr == TEXT("CMOT_Float1")) Custom->OutputType = CMOT_Float1;
				else if (TypeStr == TEXT("CMOT_Float2")) Custom->OutputType = CMOT_Float2;
				else if (TypeStr == TEXT("CMOT_Float3")) Custom->OutputType = CMOT_Float3;
				else if (TypeStr == TEXT("CMOT_Float4")) Custom->OutputType = CMOT_Float4;
			}
		}
		// Noise
		else if (UMaterialExpressionNoise* Noise = Cast<UMaterialExpressionNoise>(Expression))
		{
			if (PropName == TEXT("NoiseFunction"))
			{
				FString FuncStr = PropValue->AsString();
				if (FuncStr == TEXT("NOISEFUNCTION_SimplexTex")) Noise->NoiseFunction = NOISEFUNCTION_SimplexTex;
				else if (FuncStr == TEXT("NOISEFUNCTION_GradientTex")) Noise->NoiseFunction = NOISEFUNCTION_GradientTex;
				else if (FuncStr == TEXT("NOISEFUNCTION_VoronoiALU")) Noise->NoiseFunction = NOISEFUNCTION_VoronoiALU;
			}
			else if (PropName == TEXT("Scale")) Noise->Scale = PropValue->AsNumber();
			else if (PropName == TEXT("Levels")) Noise->Levels = PropValue->AsNumber();
		}
		// ComponentMask
		else if (UMaterialExpressionComponentMask* Mask = Cast<UMaterialExpressionComponentMask>(Expression))
		{
			if (PropName == TEXT("R")) Mask->R = PropValue->AsBool();
			else if (PropName == TEXT("G")) Mask->G = PropValue->AsBool();
			else if (PropName == TEXT("B")) Mask->B = PropValue->AsBool();
			else if (PropName == TEXT("A")) Mask->A = PropValue->AsBool();
		}
		// Clamp
		else if (UMaterialExpressionClamp* Clamp = Cast<UMaterialExpressionClamp>(Expression))
		{
			if (PropName == TEXT("MinDefault")) Clamp->MinDefault = PropValue->AsNumber();
			else if (PropName == TEXT("MaxDefault")) Clamp->MaxDefault = PropValue->AsNumber();
		}

		// P4.3: Handle Texture property for texture-related expressions
		if (PropName == TEXT("Texture"))
		{
			FString TexturePath = PropValue->AsString();
			UTexture* Texture = LoadObject<UTexture>(nullptr, *TexturePath);
			if (!Texture)
			{
				// Try with /Game/ prefix
				Texture = LoadObject<UTexture>(nullptr, *(TEXT("/Game/") + TexturePath));
			}
			if (Texture)
			{
				// Use reflection to set the Texture property
				FProperty* TexProp = Expression->GetClass()->FindPropertyByName(FName(TEXT("Texture")));
				if (TexProp)
				{
					void* ValuePtr = TexProp->ContainerPtrToValuePtr<void>(Expression);
					if (FObjectProperty* ObjProp = CastField<FObjectProperty>(TexProp))
					{
						ObjProp->SetObjectPropertyValue(ValuePtr, Texture);
					}
				}
			}
		}

		// P4.8: Handle MaterialFunction property for MaterialFunctionCall
		if (PropName == TEXT("MaterialFunction") || PropName == TEXT("Function"))
		{
			if (UMaterialExpressionMaterialFunctionCall* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
			{
				FString FunctionPath = PropValue->AsString();
				UMaterialFunction* MatFunc = LoadObject<UMaterialFunction>(nullptr, *FunctionPath);
				if (!MatFunc)
				{
					MatFunc = LoadObject<UMaterialFunction>(nullptr, *(TEXT("/Game/") + FunctionPath));
				}
				if (MatFunc)
				{
					FuncCall->SetMaterialFunction(MatFunc);
				}
			}
		}
	}
}

TSharedPtr<FJsonObject> FAddMaterialExpressionAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;

	// Get material
	UMaterial* Material = GetMaterialByNameOrCurrent(Params, Context, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	// Get expression class
	FString ExpressionClassName;
	GetRequiredString(Params, TEXT("expression_class"), ExpressionClassName, Error);

	UClass* ExprClass = ResolveExpressionClass(ExpressionClassName);
	if (!ExprClass)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Unknown expression class '%s'. Common types: SceneTexture, Time, Noise, Add, Multiply, Lerp, Constant, ScalarParameter, VectorParameter, Custom"), *ExpressionClassName),
			TEXT("unknown_expression_class"));
	}

	// Get node name
	FString NodeName;
	GetRequiredString(Params, TEXT("node_name"), NodeName, Error);

	// Check for duplicate name
	if (Context.GetMaterialNode(NodeName))
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Node name '%s' already exists. Use a unique name."), *NodeName),
			TEXT("duplicate_node_name"));
	}

	// Get position
	int32 PosX = 0, PosY = 0;
	const TArray<TSharedPtr<FJsonValue>>* PosArray = GetOptionalArray(Params, TEXT("position"));
	if (PosArray && PosArray->Num() >= 2)
	{
		PosX = (*PosArray)[0]->AsNumber();
		PosY = (*PosArray)[1]->AsNumber();
	}

	// Create the expression
	UMaterialExpression* NewExpr = NewObject<UMaterialExpression>(Material, ExprClass);
	if (!NewExpr)
	{
		return CreateErrorResponse(TEXT("Failed to create material expression"), TEXT("creation_failed"));
	}

	// Set editor position
	NewExpr->MaterialExpressionEditorX = PosX;
	NewExpr->MaterialExpressionEditorY = PosY;

	// Add to material's expression collection
	Material->GetExpressionCollection().AddExpression(NewExpr);

	// Set properties if provided
	if (Params->HasField(TEXT("properties")))
	{
		TSharedPtr<FJsonObject> PropsObj = Params->GetObjectField(TEXT("properties"));
		SetExpressionProperties(NewExpr, PropsObj);
	}

	if (UMaterialExpressionScalarParameter* ScalarParam = Cast<UMaterialExpressionScalarParameter>(NewExpr))
	{
		if (ScalarParam->ParameterName.IsNone())
		{
			ScalarParam->ParameterName = FName(*NodeName);
		}
	}
	else if (UMaterialExpressionVectorParameter* VectorParam = Cast<UMaterialExpressionVectorParameter>(NewExpr))
	{
		if (VectorParam->ParameterName.IsNone())
		{
			VectorParam->ParameterName = FName(*NodeName);
		}
	}
	else if (UMaterialExpressionTextureObjectParameter* TextureObjectParam = Cast<UMaterialExpressionTextureObjectParameter>(NewExpr))
	{
		if (TextureObjectParam->ParameterName.IsNone())
		{
			TextureObjectParam->ParameterName = FName(*NodeName);
		}
	}
	else if (UMaterialExpressionTextureSampleParameter2D* TextureSampleParam2D = Cast<UMaterialExpressionTextureSampleParameter2D>(NewExpr))
	{
		if (TextureSampleParam2D->ParameterName.IsNone())
		{
			TextureSampleParam2D->ParameterName = FName(*NodeName);
		}
	}

	// Register in context
	Context.RegisterMaterialNode(NodeName, NewExpr);
	Context.SetCurrentMaterial(Material);

	// Mark modified
	MarkMaterialModified(Material, Context);

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("node_name"), NodeName);
	Result->SetStringField(TEXT("expression_class"), ExpressionClassName);
	Result->SetStringField(TEXT("material"), Material->GetName());

	return CreateSuccessResponse(Result);
}

// =========================================================================
// FConnectMaterialExpressionsAction
// =========================================================================

bool FConnectMaterialExpressionsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString SourceNode, TargetNode, TargetInput;
	if (!GetRequiredString(Params, TEXT("source_node"), SourceNode, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("target_node"), TargetNode, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("target_input"), TargetInput, OutError)) return false;
	return true;
}

bool FConnectMaterialExpressionsAction::ConnectToExpressionInput(UMaterialExpression* SourceExpr, int32 OutputIndex,
	UMaterialExpression* TargetExpr, const FString& InputName, FString& OutError) const
{
	// P4.2: Generic connection via GetInputsView() — works for ANY expression type
	// Special-case Custom expressions first (dynamic inputs)
	if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(TargetExpr))
	{
		// Try to find matching input by name in existing inputs
		for (FCustomInput& Input : Custom->Inputs)
		{
			if (Input.InputName.ToString().Equals(InputName, ESearchCase::IgnoreCase))
			{
				Input.Input.Expression = SourceExpr;
				Input.Input.OutputIndex = OutputIndex;
				return true;
			}
		}
		// Before adding a new input, try to reuse a default empty-named unconnected input
		// (UE creates Custom nodes with one default FCustomInput whose InputName is NAME_None)
		for (FCustomInput& Input : Custom->Inputs)
		{
			if ((Input.InputName == NAME_None || Input.InputName.ToString().IsEmpty())
				&& Input.Input.Expression == nullptr)
			{
				Input.InputName = FName(*InputName);
				Input.Input.Expression = SourceExpr;
				Input.Input.OutputIndex = OutputIndex;
				return true;
			}
		}
		// Add new input if no reusable slot found
		FCustomInput NewInput;
		NewInput.InputName = FName(*InputName);
		NewInput.Input.Expression = SourceExpr;
		NewInput.Input.OutputIndex = OutputIndex;
		Custom->Inputs.Add(NewInput);
		return true;
	}

	// Generic path: use GetInput(index) to find input by name
	for (int32 InputIndex = 0; ; ++InputIndex)
	{
		FExpressionInput* Input = TargetExpr->GetInput(InputIndex);
		if (!Input)
		{
			break;
		}

		FName CurInputName = TargetExpr->GetInputName(InputIndex);
		if (CurInputName.ToString().Equals(InputName, ESearchCase::IgnoreCase))
		{
			Input->Expression = SourceExpr;
			Input->OutputIndex = OutputIndex;
			return true;
		}
	}

	// Build helpful error with available input names
	TArray<FString> AvailableInputNames;
	for (int32 InputIndex = 0; ; ++InputIndex)
	{
		FExpressionInput* Input = TargetExpr->GetInput(InputIndex);
		if (!Input)
		{
			break;
		}
		AvailableInputNames.Add(TargetExpr->GetInputName(InputIndex).ToString());
	}

	OutError = FString::Printf(TEXT("Input '%s' not found on expression '%s'. Available inputs: %s"),
		*InputName, *TargetExpr->GetClass()->GetName(),
		*FString::Join(AvailableInputNames, TEXT(", ")));
	return false;
}

TSharedPtr<FJsonObject> FConnectMaterialExpressionsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;

	// Get material
	UMaterial* Material = GetMaterialByNameOrCurrent(Params, Context, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	// Get nodes
	FString SourceNodeName, TargetNodeName, TargetInput;
	GetRequiredString(Params, TEXT("source_node"), SourceNodeName, Error);
	GetRequiredString(Params, TEXT("target_node"), TargetNodeName, Error);
	GetRequiredString(Params, TEXT("target_input"), TargetInput, Error);

	int32 SourceOutputIndex = GetOptionalNumber(Params, TEXT("source_output_index"), 0);

	// Find source node
	UMaterialExpression* SourceExpr = Context.GetMaterialNode(SourceNodeName);
	if (!SourceExpr)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Source node '%s' not found. Make sure to use add_material_expression first."), *SourceNodeName),
			TEXT("source_not_found"));
	}

	// Find target node
	UMaterialExpression* TargetExpr = Context.GetMaterialNode(TargetNodeName);
	if (!TargetExpr)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Target node '%s' not found. Make sure to use add_material_expression first."), *TargetNodeName),
			TEXT("target_not_found"));
	}

	// Connect
	if (!ConnectToExpressionInput(SourceExpr, SourceOutputIndex, TargetExpr, TargetInput, Error))
	{
		return CreateErrorResponse(Error, TEXT("connection_failed"));
	}

	// Mark modified
	MarkMaterialModified(Material, Context);

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("source_node"), SourceNodeName);
	Result->SetStringField(TEXT("target_node"), TargetNodeName);
	Result->SetStringField(TEXT("target_input"), TargetInput);

	return CreateSuccessResponse(Result);
}

// =========================================================================
// FConnectToMaterialOutputAction
// =========================================================================

bool FConnectToMaterialOutputAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString SourceNode, MaterialProperty;
	if (!GetRequiredString(Params, TEXT("source_node"), SourceNode, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("material_property"), MaterialProperty, OutError)) return false;
	return true;
}

bool FConnectToMaterialOutputAction::ConnectToMaterialProperty(UMaterial* Material, UMaterialExpression* SourceExpr,
	int32 OutputIndex, const FString& PropertyName, FString& OutError) const
{
	// Get editor-only data for main material outputs
	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutError = TEXT("Could not access material editor data");
		return false;
	}

	// Map property name to material output
	if (PropertyName.Equals(TEXT("BaseColor"), ESearchCase::IgnoreCase))
	{
		EditorData->BaseColor.Expression = SourceExpr;
		EditorData->BaseColor.OutputIndex = OutputIndex;
		return true;
	}
	if (PropertyName.Equals(TEXT("EmissiveColor"), ESearchCase::IgnoreCase) ||
		PropertyName.Equals(TEXT("Emissive"), ESearchCase::IgnoreCase))
	{
		EditorData->EmissiveColor.Expression = SourceExpr;
		EditorData->EmissiveColor.OutputIndex = OutputIndex;
		return true;
	}
	if (PropertyName.Equals(TEXT("Metallic"), ESearchCase::IgnoreCase))
	{
		EditorData->Metallic.Expression = SourceExpr;
		EditorData->Metallic.OutputIndex = OutputIndex;
		return true;
	}
	if (PropertyName.Equals(TEXT("Roughness"), ESearchCase::IgnoreCase))
	{
		EditorData->Roughness.Expression = SourceExpr;
		EditorData->Roughness.OutputIndex = OutputIndex;
		return true;
	}
	if (PropertyName.Equals(TEXT("Specular"), ESearchCase::IgnoreCase))
	{
		EditorData->Specular.Expression = SourceExpr;
		EditorData->Specular.OutputIndex = OutputIndex;
		return true;
	}
	if (PropertyName.Equals(TEXT("Normal"), ESearchCase::IgnoreCase))
	{
		EditorData->Normal.Expression = SourceExpr;
		EditorData->Normal.OutputIndex = OutputIndex;
		return true;
	}
	if (PropertyName.Equals(TEXT("Opacity"), ESearchCase::IgnoreCase))
	{
		EditorData->Opacity.Expression = SourceExpr;
		EditorData->Opacity.OutputIndex = OutputIndex;
		return true;
	}
	if (PropertyName.Equals(TEXT("OpacityMask"), ESearchCase::IgnoreCase))
	{
		EditorData->OpacityMask.Expression = SourceExpr;
		EditorData->OpacityMask.OutputIndex = OutputIndex;
		return true;
	}
	if (PropertyName.Equals(TEXT("AmbientOcclusion"), ESearchCase::IgnoreCase) ||
		PropertyName.Equals(TEXT("AO"), ESearchCase::IgnoreCase))
	{
		EditorData->AmbientOcclusion.Expression = SourceExpr;
		EditorData->AmbientOcclusion.OutputIndex = OutputIndex;
		return true;
	}
	if (PropertyName.Equals(TEXT("WorldPositionOffset"), ESearchCase::IgnoreCase) ||
		PropertyName.Equals(TEXT("WPO"), ESearchCase::IgnoreCase))
	{
		EditorData->WorldPositionOffset.Expression = SourceExpr;
		EditorData->WorldPositionOffset.OutputIndex = OutputIndex;
		return true;
	}
	if (PropertyName.Equals(TEXT("Refraction"), ESearchCase::IgnoreCase))
	{
		EditorData->Refraction.Expression = SourceExpr;
		EditorData->Refraction.OutputIndex = OutputIndex;
		return true;
	}

	OutError = FString::Printf(TEXT("Unknown material property '%s'. Valid: BaseColor, EmissiveColor, Metallic, Roughness, Specular, Normal, Opacity, OpacityMask, AmbientOcclusion, WorldPositionOffset, Refraction"), *PropertyName);
	return false;
}

TSharedPtr<FJsonObject> FConnectToMaterialOutputAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;

	// Get material
	UMaterial* Material = GetMaterialByNameOrCurrent(Params, Context, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	// Get parameters
	FString SourceNodeName, MaterialProperty;
	GetRequiredString(Params, TEXT("source_node"), SourceNodeName, Error);
	GetRequiredString(Params, TEXT("material_property"), MaterialProperty, Error);

	int32 SourceOutputIndex = GetOptionalNumber(Params, TEXT("source_output_index"), 0);

	// Find source node
	UMaterialExpression* SourceExpr = Context.GetMaterialNode(SourceNodeName);
	if (!SourceExpr)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Source node '%s' not found"), *SourceNodeName),
			TEXT("source_not_found"));
	}

	// Connect to property
	if (!ConnectToMaterialProperty(Material, SourceExpr, SourceOutputIndex, MaterialProperty, Error))
	{
		return CreateErrorResponse(Error, TEXT("connection_failed"));
	}

	// Mark modified
	MarkMaterialModified(Material, Context);

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("source_node"), SourceNodeName);
	Result->SetStringField(TEXT("material_property"), MaterialProperty);

	return CreateSuccessResponse(Result);
}

// =========================================================================
// FSetMaterialExpressionPropertyAction
// =========================================================================

bool FSetMaterialExpressionPropertyAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString NodeName, PropertyName, PropertyValue;
	if (!GetRequiredString(Params, TEXT("node_name"), NodeName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("property_name"), PropertyName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("property_value"), PropertyValue, OutError)) return false;
	return true;
}

bool FSetMaterialExpressionPropertyAction::SetExpressionProperty(UMaterialExpression* Expression,
	const FString& PropertyName, const FString& PropertyValue, FString& OutError) const
{
	// Use reflection to set property
	FProperty* Prop = Expression->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (Prop)
	{
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Expression);

		// Handle different property types
		if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			FloatProp->SetPropertyValue(ValuePtr, FCString::Atof(*PropertyValue));
			return true;
		}
		if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			DoubleProp->SetPropertyValue(ValuePtr, FCString::Atod(*PropertyValue));
			return true;
		}
		if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			IntProp->SetPropertyValue(ValuePtr, FCString::Atoi(*PropertyValue));
			return true;
		}
		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			BoolProp->SetPropertyValue(ValuePtr, PropertyValue.ToBool());
			return true;
		}
		if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			StrProp->SetPropertyValue(ValuePtr, PropertyValue);
			return true;
		}
		if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			NameProp->SetPropertyValue(ValuePtr, FName(*PropertyValue));
			return true;
		}

		// P5: Handle UObject reference properties (Texture, MaterialFunction, etc.)
		if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			// Special handling for MaterialFunction on MaterialFunctionCall expressions
			if (PropertyName == TEXT("MaterialFunction") || PropertyName == TEXT("Function"))
			{
				if (UMaterialExpressionMaterialFunctionCall* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
				{
					UMaterialFunction* MatFunc = LoadObject<UMaterialFunction>(nullptr, *PropertyValue);
					if (!MatFunc)
					{
						MatFunc = LoadObject<UMaterialFunction>(nullptr, *(TEXT("/Game/") + PropertyValue));
					}
					if (MatFunc)
					{
						FuncCall->SetMaterialFunction(MatFunc);
						return true;
					}
					OutError = FString::Printf(TEXT("Could not load MaterialFunction '%s'"), *PropertyValue);
					return false;
				}
			}

			// Generic UObject loading (Texture, StaticMesh, etc.)
			UObject* LoadedObj = LoadObject<UObject>(nullptr, *PropertyValue);
			if (!LoadedObj)
			{
				// Try with /Game/ prefix
				LoadedObj = LoadObject<UObject>(nullptr, *(TEXT("/Game/") + PropertyValue));
			}
			if (!LoadedObj)
			{
				OutError = FString::Printf(TEXT("Could not load object '%s' for property '%s'"), *PropertyValue, *PropertyName);
				return false;
			}

			// Validate the loaded object is compatible with the property's expected class
			if (!LoadedObj->IsA(ObjProp->PropertyClass))
			{
				OutError = FString::Printf(TEXT("Loaded object '%s' (class %s) is not compatible with property '%s' (expected %s)"),
					*PropertyValue, *LoadedObj->GetClass()->GetName(), *PropertyName, *ObjProp->PropertyClass->GetName());
				return false;
			}

			ObjProp->SetObjectPropertyValue(ValuePtr, LoadedObj);
			return true;
		}
	}

	OutError = FString::Printf(TEXT("Property '%s' not found or unsupported type on expression"), *PropertyName);
	return false;
}

TSharedPtr<FJsonObject> FSetMaterialExpressionPropertyAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;

	// Get material
	UMaterial* Material = GetMaterialByNameOrCurrent(Params, Context, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	// Get parameters
	FString NodeName, PropertyName, PropertyValue;
	GetRequiredString(Params, TEXT("node_name"), NodeName, Error);
	GetRequiredString(Params, TEXT("property_name"), PropertyName, Error);
	GetRequiredString(Params, TEXT("property_value"), PropertyValue, Error);

	// Find node
	UMaterialExpression* Expr = Context.GetMaterialNode(NodeName);
	if (!Expr)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Node '%s' not found"), *NodeName),
			TEXT("node_not_found"));
	}

	// Set property
	if (!SetExpressionProperty(Expr, PropertyName, PropertyValue, Error))
	{
		return CreateErrorResponse(Error, TEXT("property_set_failed"));
	}

	// Mark modified
	MarkMaterialModified(Material, Context);

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("node_name"), NodeName);
	Result->SetStringField(TEXT("property_name"), PropertyName);

	return CreateSuccessResponse(Result);
}

// =========================================================================
// FCompileMaterialAction
// =========================================================================

bool FCompileMaterialAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// Allow compiling the current material if material_name is omitted.
	const FString MaterialName = GetOptionalString(Params, TEXT("material_name"));
	if (MaterialName.IsEmpty() && !Context.CurrentMaterial.IsValid())
	{
		OutError = TEXT("No current material set. Specify material_name or create/select a material first.");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FCompileMaterialAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	UMaterial* Material = GetMaterialByNameOrCurrent(Params, Context, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	const FString MaterialName = Material->GetName();

	// Force recompilation
	Material->PreEditChange(nullptr);
	Material->PostEditChange();

	// Force recompile for rendering (async shader compilation)
	Material->ForceRecompileForRendering();

	// P5.1: Wait for this material's shader compilation to finish, then read real errors
	FMaterialResource* MatResource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
	if (MatResource)
	{
		MatResource->FinishCompilation();
	}

	// Read compile errors from the material resource
	TArray<FString> CompileErrors;
	TArray<UMaterialExpression*> ErrorExpressions;
	if (MatResource)
	{
		CompileErrors = MatResource->GetCompileErrors();
		const TArray<UMaterialExpression*>& ErrExprs = MatResource->GetErrorExpressions();
		ErrorExpressions = ErrExprs;
	}
	else
	{
		CompileErrors.Add(TEXT("Material resource is null for current shader platform; shader compilation result is unavailable."));
	}

	int32 ErrorCount = CompileErrors.Num();
	const bool bCompiled = (MatResource != nullptr);
	const bool bSuccess = bCompiled && (ErrorCount == 0);

	// Reregister all components using this material
	FGlobalComponentReregisterContext RecreateComponents;

	// Save material
	Material->MarkPackageDirty();
	Context.MarkPackageDirty(Material->GetOutermost());

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	// Keep "name" for backward compatibility, but prefer material_name going forward.
	Result->SetStringField(TEXT("name"), MaterialName);
	Result->SetStringField(TEXT("material_name"), MaterialName);
	Result->SetBoolField(TEXT("compiled"), bCompiled);
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetNumberField(TEXT("error_count"), ErrorCount);
	Result->SetNumberField(TEXT("warning_count"), 0);

	// P5.1: Build errors array with associated expression info
	if (ErrorCount > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrorsArray;
		for (int32 i = 0; i < CompileErrors.Num(); ++i)
		{
			TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
			ErrorObj->SetStringField(TEXT("message"), CompileErrors[i]);

			if (ErrorExpressions.IsValidIndex(i) && ErrorExpressions[i])
			{
				UMaterialExpression* ErrExpr = ErrorExpressions[i];
				ErrorObj->SetStringField(TEXT("expression_name"), ErrExpr->GetName());
				ErrorObj->SetStringField(TEXT("expression_class"), ErrExpr->GetClass()->GetName());

				// Try to find the registered node_name from context
				FString NodeName;
				if (Context.MaterialNodeMap.Num() > 0)
				{
					for (const auto& Pair : Context.MaterialNodeMap)
					{
						if (Pair.Value == ErrExpr)
						{
							NodeName = Pair.Key;
							break;
						}
					}
				}
				if (!NodeName.IsEmpty())
				{
					ErrorObj->SetStringField(TEXT("node_name"), NodeName);
				}
			}

			ErrorsArray.Add(MakeShared<FJsonValueObject>(ErrorObj));
		}
		Result->SetArrayField(TEXT("errors"), ErrorsArray);
	}

	return CreateSuccessResponse(Result);
}

// =========================================================================
// FCreateMaterialInstanceAction
// =========================================================================

bool FCreateMaterialInstanceAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString InstanceName, ParentMaterial;
	if (!GetRequiredString(Params, TEXT("instance_name"), InstanceName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("parent_material"), ParentMaterial, OutError)) return false;
	if (IsInvalidMaterialParameterName(InstanceName))
	{
		OutError = FString::Printf(TEXT("Invalid instance_name '%s'. instance_name cannot be empty or None/NAME_None."), *InstanceName);
		return false;
	}
	if (!ValidateParameterOverrideKeys(Params, TEXT("scalar_parameters"), OutError)) return false;
	if (!ValidateParameterOverrideKeys(Params, TEXT("vector_parameters"), OutError)) return false;
	if (!ValidateParameterOverrideKeys(Params, TEXT("texture_parameters"), OutError)) return false;
	if (!ValidateParameterOverrideKeys(Params, TEXT("static_switch_parameters"), OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FCreateMaterialInstanceAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString InstanceName, ParentMaterialName;
	GetRequiredString(Params, TEXT("instance_name"), InstanceName, Error);
	GetRequiredString(Params, TEXT("parent_material"), ParentMaterialName, Error);

	FString Path = GetOptionalString(Params, TEXT("path"), TEXT("/Game/Materials"));

	// Find parent material
	UMaterial* ParentMaterial = FindMaterial(ParentMaterialName, Error);
	if (!ParentMaterial)
	{
		return CreateErrorResponse(Error, TEXT("parent_not_found"));
	}

	// Build package path
	FString InstancePackagePath = Path / InstanceName;

	// Clean up existing
	UPackage* ExistingPackage = FindPackage(nullptr, *InstancePackagePath);
	if (ExistingPackage)
	{
		UMaterialInstanceConstant* ExistingInstance = FindObject<UMaterialInstanceConstant>(ExistingPackage, *InstanceName);
		if (ExistingInstance)
		{
			FString TempName = FString::Printf(TEXT("%s_TEMP_%d"), *InstanceName, FMath::Rand());
			ExistingInstance->Rename(*TempName, GetTransientPackage(),
				REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
			ExistingInstance->MarkAsGarbage();
			ExistingPackage->MarkAsGarbage();
		}
	}

	if (UEditorAssetLibrary::DoesAssetExist(InstancePackagePath))
	{
		UEditorAssetLibrary::DeleteAsset(InstancePackagePath);
	}

	// Create package
	UPackage* Package = CreatePackage(*InstancePackagePath);
	if (!Package)
	{
		return CreateErrorResponse(TEXT("Failed to create package"), TEXT("package_creation_failed"));
	}
	Package->FullyLoad();

	// Create material instance using factory
	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
	Factory->InitialParent = ParentMaterial;

	UMaterialInstanceConstant* NewInstance = Cast<UMaterialInstanceConstant>(Factory->FactoryCreateNew(
		UMaterialInstanceConstant::StaticClass(), Package, *InstanceName,
		RF_Public | RF_Standalone, nullptr, GWarn));

	if (!NewInstance)
	{
		return CreateErrorResponse(TEXT("Failed to create material instance"), TEXT("creation_failed"));
	}

	// Set scalar parameters
	if (Params->HasField(TEXT("scalar_parameters")))
	{
		TSharedPtr<FJsonObject> ScalarParams = Params->GetObjectField(TEXT("scalar_parameters"));
		for (const auto& Pair : ScalarParams->Values)
		{
			NewInstance->SetScalarParameterValueEditorOnly(FName(*Pair.Key), Pair.Value->AsNumber());
		}
	}

	// Set vector parameters
	if (Params->HasField(TEXT("vector_parameters")))
	{
		TSharedPtr<FJsonObject> VectorParams = Params->GetObjectField(TEXT("vector_parameters"));
		for (const auto& Pair : VectorParams->Values)
		{
			const TArray<TSharedPtr<FJsonValue>>* Arr;
			if (Pair.Value->TryGetArray(Arr) && Arr->Num() >= 3)
			{
				FLinearColor Color(
					(*Arr)[0]->AsNumber(),
					(*Arr)[1]->AsNumber(),
					(*Arr)[2]->AsNumber(),
					Arr->Num() > 3 ? (*Arr)[3]->AsNumber() : 1.0f);
				NewInstance->SetVectorParameterValueEditorOnly(FName(*Pair.Key), Color);
			}
		}
	}

	// P4.3: Set texture parameters
	if (Params->HasField(TEXT("texture_parameters")))
	{
		TSharedPtr<FJsonObject> TextureParams = Params->GetObjectField(TEXT("texture_parameters"));
		for (const auto& Pair : TextureParams->Values)
		{
			FString TexturePath = Pair.Value->AsString();
			UTexture* Texture = LoadObject<UTexture>(nullptr, *TexturePath);
			if (!Texture)
			{
				Texture = LoadObject<UTexture>(nullptr, *(TEXT("/Game/") + TexturePath));
			}
			if (Texture)
			{
				NewInstance->SetTextureParameterValueEditorOnly(FName(*Pair.Key), Texture);
			}
		}
	}

	// P4.7: Set static switch parameters
	if (Params->HasField(TEXT("static_switch_parameters")))
	{
		TSharedPtr<FJsonObject> SwitchParams = Params->GetObjectField(TEXT("static_switch_parameters"));
		for (const auto& Pair : SwitchParams->Values)
		{
			bool bValue = Pair.Value->AsBool();
			NewInstance->SetStaticSwitchParameterValueEditorOnly(FName(*Pair.Key), bValue);
		}
	}

	// Register and mark dirty
	Package->SetDirtyFlag(true);
	NewInstance->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewInstance);
	Context.MarkPackageDirty(Package);

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), InstanceName);
	Result->SetStringField(TEXT("path"), InstancePackagePath);
	Result->SetStringField(TEXT("parent"), ParentMaterialName);

	return CreateSuccessResponse(Result);
}

// =========================================================================
// FSetMaterialPropertyAction
// =========================================================================

bool FSetMaterialPropertyAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MaterialName, PropertyName, PropertyValue;
	if (!GetRequiredString(Params, TEXT("material_name"), MaterialName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("property_name"), PropertyName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("property_value"), PropertyValue, OutError)) return false;
	return true;
}

TOptional<EMaterialShadingModel> FSetMaterialPropertyAction::ResolveShadingModel(const FString& ShadingModelString) const
{
	InitShadingModelMap();
	if (EMaterialShadingModel* Found = ShadingModelMap.Find(ShadingModelString))
	{
		return *Found;
	}
	return TOptional<EMaterialShadingModel>();
}

TSharedPtr<FJsonObject> FSetMaterialPropertyAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString MaterialName, PropertyName, PropertyValue;
	GetRequiredString(Params, TEXT("material_name"), MaterialName, Error);
	GetRequiredString(Params, TEXT("property_name"), PropertyName, Error);
	GetRequiredString(Params, TEXT("property_value"), PropertyValue, Error);

	// Find material
	UMaterial* Material = FindMaterial(MaterialName, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	// Property handlers map (property name -> handler lambda)
	using FPropertyHandler = TFunction<bool(UMaterial*, const FString&, FString&)>;
	static TMap<FString, FPropertyHandler> PropertyHandlers;

	if (PropertyHandlers.Num() == 0)
	{
		PropertyHandlers.Add(TEXT("ShadingModel"), [this](UMaterial* Mat, const FString& Value, FString& OutErr) -> bool
		{
			TOptional<EMaterialShadingModel> Model = ResolveShadingModel(Value);
			if (!Model.IsSet())
			{
				OutErr = FString::Printf(TEXT("Invalid ShadingModel '%s'. Valid: Unlit, DefaultLit, Subsurface, PreintegratedSkin, ClearCoat, SubsurfaceProfile, TwoSidedFoliage, Hair, Cloth, Eye"), *Value);
				return false;
			}
			Mat->SetShadingModel(Model.GetValue());
			return true;
		});

		PropertyHandlers.Add(TEXT("TwoSided"), [](UMaterial* Mat, const FString& Value, FString& OutErr) -> bool
		{
			Mat->TwoSided = Value.ToBool() || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
			return true;
		});

		PropertyHandlers.Add(TEXT("BlendMode"), [](UMaterial* Mat, const FString& Value, FString& OutErr) -> bool
		{
			InitBlendModeMap();
			if (EBlendMode* Found = BlendModeMap.Find(Value))
			{
				Mat->BlendMode = *Found;
				return true;
			}
			OutErr = FString::Printf(TEXT("Invalid BlendMode '%s'. Valid: Opaque, Masked, Translucent, Additive, Modulate, AlphaComposite, AlphaHoldout"), *Value);
			return false;
		});

		PropertyHandlers.Add(TEXT("DitheredLODTransition"), [](UMaterial* Mat, const FString& Value, FString& OutErr) -> bool
		{
			Mat->DitheredLODTransition = Value.ToBool() || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
			return true;
		});

		PropertyHandlers.Add(TEXT("AllowNegativeEmissiveColor"), [](UMaterial* Mat, const FString& Value, FString& OutErr) -> bool
		{
			Mat->bAllowNegativeEmissiveColor = Value.ToBool() || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
			return true;
		});

		PropertyHandlers.Add(TEXT("OpacityMaskClipValue"), [](UMaterial* Mat, const FString& Value, FString& OutErr) -> bool
		{
			Mat->OpacityMaskClipValue = FCString::Atof(*Value);
			return true;
		});

		PropertyHandlers.Add(TEXT("TangentSpaceNormal"), [](UMaterial* Mat, const FString& Value, FString& OutErr) -> bool
		{
			Mat->bTangentSpaceNormal = Value.ToBool() || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
			return true;
		});
	}

	// Find and execute the property handler
	FPropertyHandler* Handler = PropertyHandlers.Find(PropertyName);
	if (!Handler)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Unknown material property '%s'. Supported: ShadingModel, TwoSided, BlendMode, DitheredLODTransition, AllowNegativeEmissiveColor, OpacityMaskClipValue, TangentSpaceNormal"), *PropertyName),
			TEXT("unknown_property"));
	}

	FString HandlerError;
	if (!(*Handler)(Material, PropertyValue, HandlerError))
	{
		return CreateErrorResponse(HandlerError, TEXT("property_set_failed"));
	}

	// Mark material as modified and trigger recompilation
	MarkMaterialModified(Material, Context);

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("material_name"), MaterialName);
	Result->SetStringField(TEXT("property_name"), PropertyName);
	Result->SetStringField(TEXT("property_value"), PropertyValue);

	return CreateSuccessResponse(Result);
}


// =========================================================================
// FCreatePostProcessVolumeAction
// =========================================================================

bool FCreatePostProcessVolumeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	return GetRequiredString(Params, TEXT("name"), Name, OutError);
}

FVector FCreatePostProcessVolumeAction::GetVectorFromParams(const TSharedPtr<FJsonObject>& Params, const FString& FieldName) const
{
	FVector Result = FVector::ZeroVector;
	const TArray<TSharedPtr<FJsonValue>>* Arr = GetOptionalArray(Params, FieldName);
	if (Arr && Arr->Num() >= 3)
	{
		Result.X = (*Arr)[0]->AsNumber();
		Result.Y = (*Arr)[1]->AsNumber();
		Result.Z = (*Arr)[2]->AsNumber();
	}
	return Result;
}

TSharedPtr<FJsonObject> FCreatePostProcessVolumeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString ActorName;
	GetRequiredString(Params, TEXT("name"), ActorName, Error);

	FVector Location = GetVectorFromParams(Params, TEXT("location"));
	bool bInfiniteExtent = GetOptionalBool(Params, TEXT("infinite_extent"), true);
	float Priority = GetOptionalNumber(Params, TEXT("priority"), 0.0);

	// Get world
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No world available"), TEXT("no_world"));
	}

	// Find and delete existing actor with same name using safe method
	TArray<AActor*> AllPPVs;
	UGameplayStatics::GetAllActorsOfClass(World, APostProcessVolume::StaticClass(), AllPPVs);
	for (AActor* Actor : AllPPVs)
	{
		if (Actor && (Actor->GetActorLabel() == ActorName || Actor->GetName() == ActorName))
		{
			// Deselect before destroying to avoid editor issues
			GEditor->SelectNone(true, true);
			World->DestroyActor(Actor);
			break;
		}
	}

	// Spawn post process volume
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(*ActorName);
	SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	APostProcessVolume* Volume = World->SpawnActor<APostProcessVolume>(Location, FRotator::ZeroRotator, SpawnParams);
	if (!Volume)
	{
		return CreateErrorResponse(TEXT("Failed to spawn post process volume"), TEXT("spawn_failed"));
	}

	// Set properties
	Volume->bUnbound = bInfiniteExtent;
	Volume->Priority = Priority;
	Volume->SetActorLabel(ActorName);

	// Add materials
	const TArray<TSharedPtr<FJsonValue>>* MaterialsArray = GetOptionalArray(Params, TEXT("post_process_materials"));
	if (MaterialsArray)
	{
		for (const TSharedPtr<FJsonValue>& MatValue : *MaterialsArray)
		{
			FString MatName = MatValue->AsString();
			FString MatError;
			UMaterial* Mat = FindMaterial(MatName, MatError);
			if (Mat)
			{
				// Add as weighted blendable
				Volume->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, Mat));
			}
			else
			{
				UE_LOG(LogMCP, Warning, TEXT("FCreatePostProcessVolumeAction: Material '%s' not found"), *MatName);
			}
		}
	}

	// Update context
	Context.LastCreatedActorName = ActorName;

	// Mark level dirty
	World->MarkPackageDirty();

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), ActorName);

	TArray<TSharedPtr<FJsonValue>> LocationArray;
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.X));
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Y));
	LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Z));
	Result->SetArrayField(TEXT("location"), LocationArray);

	Result->SetBoolField(TEXT("infinite_extent"), bInfiniteExtent);
	Result->SetNumberField(TEXT("priority"), Priority);

	return CreateSuccessResponse(Result);
}


// =========================================================================
// FGetMaterialSummaryAction (P4.1)
// =========================================================================

bool FGetMaterialSummaryAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MaterialName;
	return GetRequiredString(Params, TEXT("material_name"), MaterialName, OutError);
}

FString FGetMaterialSummaryAction::GetExpressionClassName(UMaterialExpression* Expr) const
{
	if (!Expr)
	{
		return TEXT("Unknown");
	}

	// Reverse lookup from ExpressionClassMap
	InitExpressionClassMap();
	UClass* ExprClass = Expr->GetClass();
	for (const auto& Pair : ExpressionClassMap)
	{
		if (Pair.Value == ExprClass)
		{
			return Pair.Key;
		}
	}

	// Fallback: strip "MaterialExpression" prefix
	FString ClassName = ExprClass->GetName();
	ClassName.RemoveFromStart(TEXT("MaterialExpression"));
	return ClassName;
}

TSharedPtr<FJsonObject> FGetMaterialSummaryAction::GetExpressionProperties(UMaterialExpression* Expr) const
{
	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
	if (!Expr)
	{
		return Props;
	}

	// ScalarParameter
	if (UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expr))
	{
		Props->SetStringField(TEXT("ParameterName"), Scalar->ParameterName.ToString());
		Props->SetNumberField(TEXT("DefaultValue"), Scalar->DefaultValue);
	}
	// VectorParameter
	else if (UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Expr))
	{
		Props->SetStringField(TEXT("ParameterName"), Vector->ParameterName.ToString());
		TArray<TSharedPtr<FJsonValue>> ColorArr;
		ColorArr.Add(MakeShared<FJsonValueNumber>(Vector->DefaultValue.R));
		ColorArr.Add(MakeShared<FJsonValueNumber>(Vector->DefaultValue.G));
		ColorArr.Add(MakeShared<FJsonValueNumber>(Vector->DefaultValue.B));
		ColorArr.Add(MakeShared<FJsonValueNumber>(Vector->DefaultValue.A));
		Props->SetArrayField(TEXT("DefaultValue"), ColorArr);
	}
	// Constant
	else if (UMaterialExpressionConstant* Const = Cast<UMaterialExpressionConstant>(Expr))
	{
		Props->SetNumberField(TEXT("R"), Const->R);
	}
	// Constant3Vector
	else if (UMaterialExpressionConstant3Vector* Const3 = Cast<UMaterialExpressionConstant3Vector>(Expr))
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShared<FJsonValueNumber>(Const3->Constant.R));
		Arr.Add(MakeShared<FJsonValueNumber>(Const3->Constant.G));
		Arr.Add(MakeShared<FJsonValueNumber>(Const3->Constant.B));
		Props->SetArrayField(TEXT("Constant"), Arr);
	}
	// Custom HLSL
	else if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expr))
	{
		Props->SetStringField(TEXT("Code"), Custom->Code);
		Props->SetStringField(TEXT("Description"), Custom->Description);
		// Output Inputs array for diagnosis
		TArray<TSharedPtr<FJsonValue>> InputsArr;
		for (int32 i = 0; i < Custom->Inputs.Num(); ++i)
		{
			TSharedPtr<FJsonObject> InputObj = MakeShared<FJsonObject>();
			InputObj->SetNumberField(TEXT("index"), i);
			InputObj->SetStringField(TEXT("name"), Custom->Inputs[i].InputName.ToString());
			InputObj->SetBoolField(TEXT("connected"), Custom->Inputs[i].Input.Expression != nullptr);
			if (Custom->Inputs[i].Input.Expression)
			{
				InputObj->SetStringField(TEXT("source_class"), Custom->Inputs[i].Input.Expression->GetClass()->GetName());
			}
			InputsArr.Add(MakeShared<FJsonValueObject>(InputObj));
		}
		Props->SetArrayField(TEXT("Inputs"), InputsArr);
		// Output type
		Props->SetNumberField(TEXT("OutputType"), (int32)Custom->OutputType);
	}
	// Noise
	else if (UMaterialExpressionNoise* Noise = Cast<UMaterialExpressionNoise>(Expr))
	{
		Props->SetNumberField(TEXT("Scale"), Noise->Scale);
		Props->SetNumberField(TEXT("Levels"), Noise->Levels);
	}
	// SceneTexture
	else if (UMaterialExpressionSceneTexture* SceneTex = Cast<UMaterialExpressionSceneTexture>(Expr))
	{
		Props->SetNumberField(TEXT("SceneTextureId"), static_cast<int32>(SceneTex->SceneTextureId));
	}
	// TextureCoordinate
	else if (UMaterialExpressionTextureCoordinate* TexCoord = Cast<UMaterialExpressionTextureCoordinate>(Expr))
	{
		Props->SetNumberField(TEXT("CoordinateIndex"), TexCoord->CoordinateIndex);
		Props->SetNumberField(TEXT("UTiling"), TexCoord->UTiling);
		Props->SetNumberField(TEXT("VTiling"), TexCoord->VTiling);
	}

	return Props;
}

TArray<TSharedPtr<FJsonValue>> FGetMaterialSummaryAction::BuildConnectionsArray(
	UMaterial* Material, const TMap<UMaterialExpression*, FString>& ExprToName) const
{
	TArray<TSharedPtr<FJsonValue>> Connections;

	// Scan all expression inputs
	for (const auto& Pair : ExprToName)
	{
		UMaterialExpression* TargetExpr = Pair.Key;
		const FString& TargetName = Pair.Value;

		for (int32 InputIndex = 0; ; ++InputIndex)
		{
			FExpressionInput* Input = TargetExpr->GetInput(InputIndex);
			if (!Input)
			{
				break;
			}
			if (Input->Expression)
			{
				const FString* SourceName = ExprToName.Find(Input->Expression);
				if (SourceName)
				{
					TSharedPtr<FJsonObject> Conn = MakeShared<FJsonObject>();
					Conn->SetStringField(TEXT("source"), *SourceName);
					Conn->SetNumberField(TEXT("source_output"), Input->OutputIndex);
					Conn->SetStringField(TEXT("target"), TargetName);
					Conn->SetStringField(TEXT("target_input"), TargetExpr->GetInputName(InputIndex).ToString());
					Connections.Add(MakeShared<FJsonValueObject>(Conn));
				}
			}
		}
	}

	// Scan material output connections
	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (EditorData)
	{
		auto CheckOutput = [&](const FExpressionInput& Input, const FString& PropName)
		{
			if (Input.Expression)
			{
				const FString* SourceName = ExprToName.Find(Input.Expression);
				if (SourceName)
				{
					TSharedPtr<FJsonObject> Conn = MakeShared<FJsonObject>();
					Conn->SetStringField(TEXT("source"), *SourceName);
					Conn->SetNumberField(TEXT("source_output"), Input.OutputIndex);
					Conn->SetStringField(TEXT("target"), TEXT("$output"));
					Conn->SetStringField(TEXT("target_input"), PropName);
					Connections.Add(MakeShared<FJsonValueObject>(Conn));
				}
			}
		};

		CheckOutput(EditorData->BaseColor, TEXT("BaseColor"));
		CheckOutput(EditorData->EmissiveColor, TEXT("EmissiveColor"));
		CheckOutput(EditorData->Metallic, TEXT("Metallic"));
		CheckOutput(EditorData->Roughness, TEXT("Roughness"));
		CheckOutput(EditorData->Specular, TEXT("Specular"));
		CheckOutput(EditorData->Normal, TEXT("Normal"));
		CheckOutput(EditorData->Opacity, TEXT("Opacity"));
		CheckOutput(EditorData->OpacityMask, TEXT("OpacityMask"));
		CheckOutput(EditorData->AmbientOcclusion, TEXT("AmbientOcclusion"));
		CheckOutput(EditorData->WorldPositionOffset, TEXT("WorldPositionOffset"));
		CheckOutput(EditorData->Refraction, TEXT("Refraction"));
	}

	return Connections;
}

TSharedPtr<FJsonObject> FGetMaterialSummaryAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString MaterialName;
	GetRequiredString(Params, TEXT("material_name"), MaterialName, Error);

	UMaterial* Material = FindMaterial(MaterialName, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	// Build expression-to-name map (reverse lookup from Context + auto-naming)
	TMap<UMaterialExpression*, FString> ExprToName;

	// First, populate from Context's MaterialNodeMap
	for (const auto& CtxPair : Context.MaterialNodeMap)
	{
		if (CtxPair.Value.IsValid())
		{
			ExprToName.Add(CtxPair.Value.Get(), CtxPair.Key);
		}
	}

	// Then, assign names to unregistered expressions
	int32 UnnamedIndex = 0;
	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressionCollection().Expressions;
	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr || Expr->IsA<UMaterialExpressionComment>())
		{
			continue;
		}
		if (!ExprToName.Contains(Expr))
		{
			ExprToName.Add(Expr, FString::Printf(TEXT("$expr_%d"), UnnamedIndex++));
		}
	}

	// Build expressions array
	TArray<TSharedPtr<FJsonValue>> ExpressionsArray;
	for (const auto& Pair : ExprToName)
	{
		UMaterialExpression* Expr = Pair.Key;
		TSharedPtr<FJsonObject> ExprObj = MakeShared<FJsonObject>();
		ExprObj->SetStringField(TEXT("node_name"), Pair.Value);
		ExprObj->SetStringField(TEXT("class"), GetExpressionClassName(Expr));
		ExprObj->SetNumberField(TEXT("pos_x"), Expr->MaterialExpressionEditorX);
		ExprObj->SetNumberField(TEXT("pos_y"), Expr->MaterialExpressionEditorY);
		ExprObj->SetObjectField(TEXT("properties"), GetExpressionProperties(Expr));
		ExpressionsArray.Add(MakeShared<FJsonValueObject>(ExprObj));
	}

	// Build connections array
	TArray<TSharedPtr<FJsonValue>> Connections = BuildConnectionsArray(Material, ExprToName);

	// Build comments array
	TArray<TSharedPtr<FJsonValue>> CommentsArray;
	const TArray<TObjectPtr<UMaterialExpressionComment>>& Comments = Material->GetExpressionCollection().EditorComments;
	for (UMaterialExpressionComment* Comment : Comments)
	{
		if (!Comment)
		{
			continue;
		}
		TSharedPtr<FJsonObject> CommentObj = MakeShared<FJsonObject>();
		CommentObj->SetStringField(TEXT("text"), Comment->Text);
		CommentObj->SetNumberField(TEXT("pos_x"), Comment->MaterialExpressionEditorX);
		CommentObj->SetNumberField(TEXT("pos_y"), Comment->MaterialExpressionEditorY);
		CommentObj->SetNumberField(TEXT("size_x"), Comment->SizeX);
		CommentObj->SetNumberField(TEXT("size_y"), Comment->SizeY);
		CommentsArray.Add(MakeShared<FJsonValueObject>(CommentObj));
	}

	// Resolve domain/blend/shading strings
	auto DomainToString = [](EMaterialDomain D) -> FString
	{
		switch (D)
		{
		case MD_Surface: return TEXT("Surface");
		case MD_PostProcess: return TEXT("PostProcess");
		case MD_DeferredDecal: return TEXT("DeferredDecal");
		case MD_LightFunction: return TEXT("LightFunction");
		case MD_UI: return TEXT("UI");
		case MD_Volume: return TEXT("Volume");
		default: return TEXT("Unknown");
		}
	};
	auto BlendModeToString = [](EBlendMode B) -> FString
	{
		switch (B)
		{
		case BLEND_Opaque: return TEXT("Opaque");
		case BLEND_Masked: return TEXT("Masked");
		case BLEND_Translucent: return TEXT("Translucent");
		case BLEND_Additive: return TEXT("Additive");
		case BLEND_Modulate: return TEXT("Modulate");
		default: return TEXT("Unknown");
		}
	};
	auto ShadingModelToString = [](EMaterialShadingModel SM) -> FString
	{
		switch (SM)
		{
		case MSM_Unlit: return TEXT("Unlit");
		case MSM_DefaultLit: return TEXT("DefaultLit");
		case MSM_Subsurface: return TEXT("Subsurface");
		case MSM_ClearCoat: return TEXT("ClearCoat");
		case MSM_Hair: return TEXT("Hair");
		case MSM_Cloth: return TEXT("Cloth");
		case MSM_Eye: return TEXT("Eye");
		default: return TEXT("DefaultLit");
		}
	};

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), MaterialName);
	Result->SetStringField(TEXT("path"), Material->GetPathName());
	Result->SetStringField(TEXT("domain"), DomainToString(Material->MaterialDomain));
	Result->SetStringField(TEXT("blend_mode"), BlendModeToString(Material->BlendMode));
	Result->SetStringField(TEXT("shading_model"), ShadingModelToString(Material->GetShadingModels().GetFirstShadingModel()));
	Result->SetBoolField(TEXT("two_sided"), Material->IsTwoSided());
	Result->SetNumberField(TEXT("expression_count"), ExprToName.Num());
	Result->SetArrayField(TEXT("expressions"), ExpressionsArray);
	Result->SetArrayField(TEXT("connections"), Connections);
	Result->SetArrayField(TEXT("comments"), CommentsArray);

	return CreateSuccessResponse(Result);
}


// =========================================================================
// FRemoveMaterialExpressionAction (P4.6)
// =========================================================================

bool FRemoveMaterialExpressionAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// At least one of node_name or node_names must be provided
	FString NodeName = GetOptionalString(Params, TEXT("node_name"));
	const TArray<TSharedPtr<FJsonValue>>* NodeNames = GetOptionalArray(Params, TEXT("node_names"));
	if (NodeName.IsEmpty() && (!NodeNames || NodeNames->Num() == 0))
	{
		OutError = TEXT("Either 'node_name' or 'node_names' is required");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FRemoveMaterialExpressionAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;

	UMaterial* Material = GetMaterialByNameOrCurrent(Params, Context, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	// Collect node names to remove
	TArray<FString> NamesToRemove;
	FString SingleName = GetOptionalString(Params, TEXT("node_name"));
	if (!SingleName.IsEmpty())
	{
		NamesToRemove.Add(SingleName);
	}
	const TArray<TSharedPtr<FJsonValue>>* NodeNames = GetOptionalArray(Params, TEXT("node_names"));
	if (NodeNames)
	{
		for (const auto& Val : *NodeNames)
		{
			NamesToRemove.AddUnique(Val->AsString());
		}
	}

	TArray<TSharedPtr<FJsonValue>> Removed;
	TArray<TSharedPtr<FJsonValue>> NotFound;

	for (const FString& NodeName : NamesToRemove)
	{
		UMaterialExpression* Expr = Context.GetMaterialNode(NodeName);
		if (!Expr)
		{
			NotFound.Add(MakeShared<FJsonValueString>(NodeName));
			continue;
		}

		// Disconnect all inputs on this expression
		for (int32 InputIdx = 0; ; ++InputIdx)
		{
			FExpressionInput* Input = Expr->GetInput(InputIdx);
			if (!Input)
			{
				break;
			}
			Input->Expression = nullptr;
			Input->OutputIndex = 0;
		}

		// Disconnect all other expressions that reference this one
		for (UMaterialExpression* OtherExpr : Material->GetExpressionCollection().Expressions)
		{
			if (!OtherExpr || OtherExpr == Expr)
			{
				continue;
			}
			for (int32 InputIdx = 0; ; ++InputIdx)
			{
				FExpressionInput* OtherInput = OtherExpr->GetInput(InputIdx);
				if (!OtherInput)
				{
					break;
				}
				if (OtherInput->Expression == Expr)
				{
					OtherInput->Expression = nullptr;
					OtherInput->OutputIndex = 0;
				}
			}
		}

		// Disconnect from material outputs
		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData)
		{
			auto DisconnectOutput = [Expr](FExpressionInput& Input)
			{
				if (Input.Expression == Expr)
				{
					Input.Expression = nullptr;
					Input.OutputIndex = 0;
				}
			};
			DisconnectOutput(EditorData->BaseColor);
			DisconnectOutput(EditorData->EmissiveColor);
			DisconnectOutput(EditorData->Metallic);
			DisconnectOutput(EditorData->Roughness);
			DisconnectOutput(EditorData->Specular);
			DisconnectOutput(EditorData->Normal);
			DisconnectOutput(EditorData->Opacity);
			DisconnectOutput(EditorData->OpacityMask);
			DisconnectOutput(EditorData->AmbientOcclusion);
			DisconnectOutput(EditorData->WorldPositionOffset);
			DisconnectOutput(EditorData->Refraction);
		}

		// Remove from expression collection
		Material->GetExpressionCollection().RemoveExpression(Expr);

		// Remove from context
		Context.MaterialNodeMap.Remove(NodeName);

		Removed.Add(MakeShared<FJsonValueString>(NodeName));
	}

	// Mark modified
	MarkMaterialModified(Material, Context);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("removed"), Removed);
	Result->SetArrayField(TEXT("not_found"), NotFound);

	return CreateSuccessResponse(Result);
}


// =========================================================================
// FAutoLayoutMaterialAction (P4.4)
// =========================================================================

bool FAutoLayoutMaterialAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MaterialName;
	return GetRequiredString(Params, TEXT("material_name"), MaterialName, OutError);
}

void FAutoLayoutMaterialAction::BuildDependencyGraph(UMaterial* Material,
	TMap<UMaterialExpression*, TArray<UMaterialExpression*>>& OutDeps,
	TMap<UMaterialExpression*, int32>& OutLayers) const
{
	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressionCollection().Expressions;
	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr || Expr->IsA<UMaterialExpressionComment>())
		{
			continue;
		}

		TArray<UMaterialExpression*> Dependencies;
		for (int32 InputIdx = 0; ; ++InputIdx)
		{
			FExpressionInput* Input = Expr->GetInput(InputIdx);
			if (!Input)
			{
				break;
			}
			if (Input->Expression && !Input->Expression->IsA<UMaterialExpressionComment>())
			{
				Dependencies.AddUnique(Input->Expression);
			}
		}
		OutDeps.Add(Expr, Dependencies);
		OutLayers.Add(Expr, 0);
	}
}

void FAutoLayoutMaterialAction::AssignLayers(UMaterial* Material,
	const TMap<UMaterialExpression*, TArray<UMaterialExpression*>>& Deps,
	TMap<UMaterialExpression*, int32>& OutLayers) const
{
	// Find root-connected expressions (connected to material outputs)
	TSet<UMaterialExpression*> RootSet;
	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (EditorData)
	{
		auto CollectRoot = [&RootSet](const FExpressionInput& Input)
		{
			if (Input.Expression)
			{
				RootSet.Add(Input.Expression);
			}
		};
		CollectRoot(EditorData->BaseColor);
		CollectRoot(EditorData->EmissiveColor);
		CollectRoot(EditorData->Metallic);
		CollectRoot(EditorData->Roughness);
		CollectRoot(EditorData->Specular);
		CollectRoot(EditorData->Normal);
		CollectRoot(EditorData->Opacity);
		CollectRoot(EditorData->OpacityMask);
		CollectRoot(EditorData->AmbientOcclusion);
		CollectRoot(EditorData->WorldPositionOffset);
		CollectRoot(EditorData->Refraction);
	}

	// If no root connections found, treat all leaf nodes (no downstream consumers) as roots
	if (RootSet.Num() == 0)
	{
		TSet<UMaterialExpression*> HasConsumer;
		for (const auto& Pair : Deps)
		{
			for (UMaterialExpression* Dep : Pair.Value)
			{
				HasConsumer.Add(Dep);
			}
		}
		for (const auto& Pair : Deps)
		{
			if (!HasConsumer.Contains(Pair.Key))
			{
				RootSet.Add(Pair.Key);
			}
		}
	}

	// BFS from root: layer 0 = rightmost (closest to output), higher = leftward
	TQueue<UMaterialExpression*> Queue;
	for (UMaterialExpression* Root : RootSet)
	{
		OutLayers.FindOrAdd(Root) = 0;
		Queue.Enqueue(Root);
	}

	while (!Queue.IsEmpty())
	{
		UMaterialExpression* Current = nullptr;
		Queue.Dequeue(Current);

		int32 CurrentLayer = OutLayers.FindRef(Current);
		const TArray<UMaterialExpression*>* DepList = Deps.Find(Current);
		if (DepList)
		{
			for (UMaterialExpression* Dep : *DepList)
			{
				int32& DepLayer = OutLayers.FindOrAdd(Dep);
				if (DepLayer < CurrentLayer + 1)
				{
					DepLayer = CurrentLayer + 1;
					Queue.Enqueue(Dep);
				}
			}
		}
	}
}

// Helper: Estimate material expression node size (code-based for Custom nodes)
static FVector2D EstimateMaterialExprNodeSize(UMaterialExpression* Expr)
{
	if (!Expr)
	{
		return FVector2D(280.0, 100.0);
	}

	// Pin counts
	int32 InputCount = 0;
	for (int32 i = 0; ; ++i)
	{
		if (!Expr->GetInput(i)) break;
		++InputCount;
	}
	TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
	int32 OutputCount = Outputs.Num();
	int32 MaxPins = FMath::Max(InputCount, OutputCount);
	// Note: bCollapsed in material nodes hides preview/description, NOT pins.
	// All input/output pins are always rendered.

	const double TitleH = 32.0;
	const double PinRowH = 26.0;
	const double BottomPad = 8.0;
	double PinsH = FMath::Max(MaxPins, 1) * PinRowH;
	double Height = TitleH + PinsH + BottomPad;
	double Width = 280.0;

	// Custom node: always compute code-based size as minimum
	// (MCP can't reliably read ShowCode from preview material, so be conservative)
	const UMaterialExpressionCustom* CustomExpr = Cast<UMaterialExpressionCustom>(Expr);
	if (CustomExpr && !CustomExpr->Code.IsEmpty())
	{
		// Count code lines and find max line length
		int32 LineCount = 1;
		int32 MaxLineLen = 0;
		int32 CurLineLen = 0;
		for (TCHAR Ch : CustomExpr->Code)
		{
			if (Ch == TEXT('\n'))
			{
				++LineCount;
				MaxLineLen = FMath::Max(MaxLineLen, CurLineLen);
				CurLineLen = 0;
			}
			else if (Ch != TEXT('\r'))
			{
				++CurLineLen;
			}
		}
		MaxLineLen = FMath::Max(MaxLineLen, CurLineLen);

		int32 ExtraOutputs = CustomExpr->AdditionalOutputs.Num();
		int32 TotalPins = FMath::Max(InputCount, OutputCount + ExtraOutputs);

		// Code area (SMultiLineEditableTextBox below pins)
		const double CodeLineH = 16.0;
		const double CodePad = 36.0;
		double CodeH = FMath::Max(LineCount, 3) * CodeLineH + CodePad;
		double CodePinsH = TotalPins * PinRowH;
		Height = TitleH + CodePinsH + CodeH;

		// Width: code chars + margins + preview area
		const double CharW = 7.2;
		const double Margins = 140.0; // left/right pad + scrollbar + preview thumbnail
		Width = FMath::Max(MaxLineLen * CharW + Margins, 420.0);

		// Safety factor for heuristic (no Slate bounds available)
		Width *= 1.1;
		Height *= 1.1;
	}
	else if (CustomExpr)
	{
		Width = 400.0;
	}
	else
	{
		// Non-Custom nodes: many node types show a preview thumbnail below pins
		// (TextureSample, VectorParameter, ScalarParameter, etc.)
		// Add preview height for nodes that typically render one.
		const double PreviewH = 90.0;
		if (MaxPins <= 3)
		{
			// Low-pin nodes often have a dominant preview area
			Height = FMath::Max(Height, 100.0 + PreviewH);
		}
		// Safety factor for widget chrome
		Width *= 1.12;
		Height *= 1.15;
	}

	return FVector2D(Width, Height);
}

TSharedPtr<FJsonObject> FAutoLayoutMaterialAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString MaterialName;
	GetRequiredString(Params, TEXT("material_name"), MaterialName, Error);

	UMaterial* Material = FindMaterial(MaterialName, Error);
	if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	// Build dependency graph and assign layers
	TMap<UMaterialExpression*, TArray<UMaterialExpression*>> Deps;
	TMap<UMaterialExpression*, int32> Layers;
	BuildDependencyGraph(Material, Deps, Layers);
	AssignLayers(Material, Deps, Layers);

	if (Layers.Num() == 0)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("nodes_moved"), 0);
		Result->SetNumberField(TEXT("layer_count"), 0);
		return CreateSuccessResponse(Result);
	}

	// ---- Phase A: Group by layer and compute per-node sizes ----
	int32 MaxLayer = 0;
	TMap<int32, TArray<UMaterialExpression*>> LayerGroups;
	TMap<UMaterialExpression*, FVector2D> NodeSizes;

	for (const auto& Pair : Layers)
	{
		LayerGroups.FindOrAdd(Pair.Value).Add(Pair.Key);
		MaxLayer = FMath::Max(MaxLayer, Pair.Value);
		NodeSizes.Add(Pair.Key, EstimateMaterialExprNodeSize(Pair.Key));
	}

	// Build downstream consumer map for barycenter ordering
	TMap<UMaterialExpression*, TArray<UMaterialExpression*>> Consumers;
	for (const auto& Pair : Deps)
	{
		Consumers.FindOrAdd(Pair.Key);
		for (UMaterialExpression* Dep : Pair.Value)
		{
			Consumers.FindOrAdd(Dep).AddUnique(Pair.Key);
		}
	}

	// Build pin-index map, root-connected set, and root pin order using shared utility
	TMap<UMaterialExpression*, TMap<UMaterialExpression*, int32>> PinIndexMap;
	MaterialLayoutUtils::BuildPinIndexMap(Deps, PinIndexMap);

	TSet<UMaterialExpression*> RootConnectedSet;
	TMap<UMaterialExpression*, int32> RootPinOrder;
	MaterialLayoutUtils::BuildRootMaps(Material->GetEditorOnlyData(), nullptr, RootConnectedSet, RootPinOrder);

	// ---- Phase B: Pin-aware layer sorting (shared utility) ----
	MaterialLayoutUtils::SortLayersByPinOrder(
		LayerGroups, MaxLayer, Deps, Consumers, PinIndexMap, RootConnectedSet, RootPinOrder);

	// ---- Phase C: Compute per-layer max width ----
	const double HGap = 80.0;
	const double VGap = 40.0;

	TMap<int32, double> LayerMaxWidth;
	for (const auto& LP : LayerGroups)
	{
		double MaxW = 0.0;
		for (UMaterialExpression* Expr : LP.Value)
		{
			MaxW = FMath::Max(MaxW, NodeSizes[Expr].X);
		}
		LayerMaxWidth.Add(LP.Key, MaxW);
	}

	// ---- Phase D: X coordinates (right to left from root) ----
	TMap<int32, double> LayerX;
	double CurX = 0.0;
	for (int32 L = 0; L <= MaxLayer; ++L)
	{
		double W = LayerMaxWidth.FindRef(L);
		LayerX.Add(L, CurX - W);
		CurX -= (W + HGap);
	}

	// ---- Phase E: Y coordinates (per-node height stacking, center-aligned) ----
	TMap<UMaterialExpression*, FVector2D> Positions;
	TMap<int32, double> LayerTotalH;

	for (int32 L = 0; L <= MaxLayer; ++L)
	{
		auto* Group = LayerGroups.Find(L);
		if (!Group) continue;

		double Y = 0.0;
		for (UMaterialExpression* Expr : *Group)
		{
			Positions.Add(Expr, FVector2D(LayerX[L], Y));
			Y += NodeSizes[Expr].Y + VGap;
		}
		LayerTotalH.Add(L, Y - VGap);
	}

	// Center-align layers vertically to Layer 0 height
	double L0H = LayerTotalH.Contains(0) ? LayerTotalH[0] : 0.0;
	for (int32 L = 0; L <= MaxLayer; ++L)
	{
		double LH = LayerTotalH.Contains(L) ? LayerTotalH[L] : 0.0;
		double OffY = (L0H - LH) * 0.5;
		if (FMath::Abs(OffY) < 1.0) continue;
		if (auto* Group = LayerGroups.Find(L))
		{
			for (UMaterialExpression* Expr : *Group)
			{
				Positions[Expr].Y += OffY;
			}
		}
	}

	// Single-node layers: align to downstream consumer center
	for (int32 L = 1; L <= MaxLayer; ++L)
	{
		auto* Group = LayerGroups.Find(L);
		if (!Group || Group->Num() != 1) continue;
		UMaterialExpression* Expr = (*Group)[0];

		double SumCY = 0.0; int32 Cnt = 0;
		if (auto* CL = Consumers.Find(Expr))
		{
			for (UMaterialExpression* C : *CL)
			{
				if (FVector2D* CP = Positions.Find(C))
				{
					SumCY += CP->Y + NodeSizes[C].Y * 0.5;
					++Cnt;
				}
			}
		}
		if (Cnt > 0)
		{
			Positions[Expr].Y = SumCY / Cnt - NodeSizes[Expr].Y * 0.5;
		}
	}

	// ---- Phase F: Minimum-gap enforcement (8 iterations) ----
	// For X-overlapping nodes, enforce minimum VGap between bottom of A and top of B
	TArray<UMaterialExpression*> AllExprs;
	Positions.GetKeys(AllExprs);

	for (int32 Iter = 0; Iter < 8; ++Iter)
	{
		bool bCollision = false;
		AllExprs.Sort([&Positions](UMaterialExpression& A, UMaterialExpression& B)
		{
			return Positions[&A].Y < Positions[&B].Y;
		});

		for (int32 i = 0; i < AllExprs.Num(); ++i)
		{
			UMaterialExpression* EA = AllExprs[i];
			FVector2D PA = Positions[EA];
			FVector2D SA = NodeSizes[EA];

			for (int32 j = i + 1; j < AllExprs.Num(); ++j)
			{
				UMaterialExpression* EB = AllExprs[j];
				FVector2D PB = Positions[EB];
				FVector2D SB = NodeSizes[EB];

				if (PB.Y > PA.Y + SA.Y + VGap) break; // sorted, gap already sufficient

				// Check horizontal overlap
				const double Tol = 4.0;
				bool bOverX = (PA.X < PB.X + SB.X - Tol) && (PB.X < PA.X + SA.X - Tol);
				if (!bOverX) continue;

				// Enforce minimum gap: B's top must be at least VGap below A's bottom
				double GapY = PB.Y - (PA.Y + SA.Y);
				if (GapY < VGap)
				{
					double Push = VGap - GapY;
					Positions[EB].Y += Push;
					bCollision = true;
				}
			}
		}
		if (!bCollision) break;
	}

	// ---- Phase G: Apply positions ----
	int32 NodesMoved = 0;
	for (auto& Pair : Positions)
	{
		UMaterialExpression* Expr = Pair.Key;
		int32 NewX = FMath::RoundToInt(Pair.Value.X);
		int32 NewY = FMath::RoundToInt(Pair.Value.Y);

		if (Expr->MaterialExpressionEditorX != NewX || Expr->MaterialExpressionEditorY != NewY)
		{
			Expr->Modify();
			Expr->MaterialExpressionEditorX = NewX;
			Expr->MaterialExpressionEditorY = NewY;
			++NodesMoved;
		}
	}

	// Sync positions to graph nodes and rebuild
	if (Material->MaterialGraph)
	{
		// First: directly update UMaterialGraphNode positions to match expression positions
		// (RebuildGraph alone may not reliably refresh SGraphEditor widgets)
		for (UEdGraphNode* Node : Material->MaterialGraph->Nodes)
		{
			UMaterialGraphNode* MatNode = Cast<UMaterialGraphNode>(Node);
			if (MatNode && MatNode->MaterialExpression)
			{
				MatNode->NodePosX = MatNode->MaterialExpression->MaterialExpressionEditorX;
				MatNode->NodePosY = MatNode->MaterialExpression->MaterialExpressionEditorY;
			}
		}
		// Then rebuild and notify to ensure SGraphEditor picks up changes
		Material->MaterialGraph->RebuildGraph();
		Material->MaterialGraph->NotifyGraphChanged();
	}

	// Mark modified
	MarkMaterialModified(Material, Context);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("nodes_moved"), NodesMoved);
	Result->SetNumberField(TEXT("layer_count"), MaxLayer + 1);

	// Debug: emit layer sort order and pin index map
	TArray<TSharedPtr<FJsonValue>> DebugLayers;
	for (int32 L = 0; L <= MaxLayer; ++L)
	{
		auto* Group = LayerGroups.Find(L);
		if (!Group) continue;
		TSharedPtr<FJsonObject> LayerObj = MakeShared<FJsonObject>();
		LayerObj->SetNumberField(TEXT("layer"), L);
		TArray<TSharedPtr<FJsonValue>> NodesArr;
		for (int32 i = 0; i < Group->Num(); ++i)
		{
			UMaterialExpression* Expr = (*Group)[i];
			TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
			NodeObj->SetNumberField(TEXT("sort_index"), i);
			NodeObj->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
			NodeObj->SetNumberField(TEXT("final_x"), Expr->MaterialExpressionEditorX);
			NodeObj->SetNumberField(TEXT("final_y"), Expr->MaterialExpressionEditorY);
			// If we have PinIndexMap data for this node as consumer
			if (auto* PM = PinIndexMap.Find(Expr))
			{
				TSharedPtr<FJsonObject> PinMap = MakeShared<FJsonObject>();
				for (auto& PinPair : *PM)
				{
					PinMap->SetNumberField(PinPair.Key->GetClass()->GetName() + TEXT("_") + PinPair.Key->GetName(), PinPair.Value);
				}
				NodeObj->SetObjectField(TEXT("pin_index_map"), PinMap);
			}
			NodesArr.Add(MakeShared<FJsonValueObject>(NodeObj));
		}
		LayerObj->SetArrayField(TEXT("nodes"), NodesArr);
		DebugLayers.Add(MakeShared<FJsonValueObject>(LayerObj));
	}
	Result->SetArrayField(TEXT("debug_layers"), DebugLayers);

	return CreateSuccessResponse(Result);
}


// =========================================================================
// FAutoCommentMaterialAction (P4.5)
// =========================================================================

bool FAutoCommentMaterialAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString CommentText;
	return GetRequiredString(Params, TEXT("comment_text"), CommentText, OutError);
}

TSharedPtr<FJsonObject> FAutoCommentMaterialAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;

	// Check use_selected early so we can auto-detect material from editor
	bool bEarlyUseSelected = GetOptionalBool(Params, TEXT("use_selected"), false);
	if (!bEarlyUseSelected)
	{
		const TArray<TSharedPtr<FJsonValue>>* EarlyNodeNames = GetOptionalArray(Params, TEXT("node_names"));
		if (EarlyNodeNames)
		{
			for (const auto& Val : *EarlyNodeNames)
			{
				if (Val->AsString() == TEXT("$selected")) { bEarlyUseSelected = true; break; }
			}
		}
	}

	UMaterial* Material = GetMaterialByNameOrCurrent(Params, Context, Error);

	// If no material found and use_selected is requested, auto-detect from open material editor
	if (!Material && bEarlyUseSelected)
	{
		UAssetEditorSubsystem* DetectEditorSS = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (DetectEditorSS)
		{
			TArray<UObject*> EditedAssets = DetectEditorSS->GetAllEditedAssets();
			for (UObject* Asset : EditedAssets)
			{
				UMaterial* Mat = Cast<UMaterial>(Asset);
				if (Mat && Mat->GetOutermost() != GetTransientPackage())
				{
					Material = Mat;
					Context.SetCurrentMaterial(Material);
					UE_LOG(LogMCP, Log, TEXT("auto_comment: auto-detected material '%s' from editor"), *Material->GetName());
					break;
				}
			}
		}
		if (!Material)
		{
			return CreateErrorResponse(TEXT("No material editor is currently open. Open a material or specify material_name."), TEXT("material_not_found"));
		}
	}
	else if (!Material)
	{
		return CreateErrorResponse(Error, TEXT("material_not_found"));
	}

	FString CommentText;
	GetRequiredString(Params, TEXT("comment_text"), CommentText, Error);
	float Padding = GetOptionalNumber(Params, TEXT("padding"), 40.0);
	bool bOverwrite = GetOptionalBool(Params, TEXT("overwrite"), false);
	bool bClearAll = GetOptionalBool(Params, TEXT("clear_all"), false);

	// If clear_all, remove ALL existing comments first
	if (bClearAll)
	{
		TArray<TObjectPtr<UMaterialExpressionComment>>& Comments = Material->GetExpressionCollection().EditorComments;
		for (int32 i = Comments.Num() - 1; i >= 0; --i)
		{
			if (Comments[i])
			{
				if (Material->MaterialGraph)
				{
					UEdGraphNode* GraphNode = Comments[i]->GraphNode;
					if (GraphNode)
					{
						Material->MaterialGraph->RemoveNode(GraphNode);
					}
				}
			}
		}
		Comments.Empty();
	}
	// If overwrite, remove existing comments with the same text
	else if (bOverwrite)
	{
		TArray<TObjectPtr<UMaterialExpressionComment>>& Comments = Material->GetExpressionCollection().EditorComments;
		for (int32 i = Comments.Num() - 1; i >= 0; --i)
		{
			if (Comments[i] && Comments[i]->Text == CommentText)
			{
				if (Material->MaterialGraph)
				{
					// Remove graph node first
					UEdGraphNode* GraphNode = Comments[i]->GraphNode;
					if (GraphNode)
					{
						Material->MaterialGraph->RemoveNode(GraphNode);
					}
				}
				Comments.RemoveAt(i);
			}
		}
	}

	// Collect target expressions
	TArray<UMaterialExpression*> TargetExpressions;
	const TArray<TSharedPtr<FJsonValue>>* NodeNamesArray = GetOptionalArray(Params, TEXT("node_names"));
	TArray<TSharedPtr<FJsonValue>> MissingNodes;
	bool bUseSelected = false;

	// Check for "$selected" keyword in node_names
	if (NodeNamesArray && NodeNamesArray->Num() > 0)
	{
		for (const auto& Val : *NodeNamesArray)
		{
			if (Val->AsString() == TEXT("$selected"))
			{
				bUseSelected = true;
				break;
			}
		}
	}

	// Also support use_selected boolean param
	if (!bUseSelected)
	{
		bUseSelected = GetOptionalBool(Params, TEXT("use_selected"), false);
	}

	if (bUseSelected)
	{
		// ---- Robust material editor lookup ----
		// The material editor uses a preview material copy (UPreviewMaterial in TransientPackage).
		// The SGraphEditor and selected nodes reference the preview material's graph.
		//
		// IMPORTANT: Engine modules (UnrealEd, MaterialEditor) are compiled WITHOUT RTTI (/GR-),
		// while this plugin has bUseRTTI=true. Using dynamic_cast on engine types across DLL
		// boundaries is undefined behavior and will crash. We must use only:
		//   - FMaterialEditorUtilities::GetIMaterialEditorForObject → TSharedPtr<IMaterialEditor>
		//     (uses StaticCastSharedPtr internally, no RTTI needed)
		//   - UAssetEditorSubsystem for discovering edited assets (UObject Cast<> for UObjects)
		//   - IMaterialEditor inherits FAssetEditorToolkit, so GetObjectsCurrentlyBeingEdited()
		//     and other FAssetEditorToolkit methods are directly callable.

		TSharedPtr<IMaterialEditor> MatEditorPtr;
		UMaterial* PreviewMaterial = nullptr;

		// Method 1: Try original material's graph → FMaterialEditorUtilities
		if (Material->MaterialGraph)
		{
			MatEditorPtr = FMaterialEditorUtilities::GetIMaterialEditorForObject(Material->MaterialGraph);
			if (MatEditorPtr.IsValid())
			{
				UE_LOG(LogMCP, Log, TEXT("auto_comment: Found editor for '%s' via GetIMaterialEditorForObject (original graph)"), *Material->GetName());
			}
		}

		// Method 2: Find preview material through UAssetEditorSubsystem, then use its graph
		UAssetEditorSubsystem* EditorSS = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (!MatEditorPtr.IsValid() && EditorSS)
		{
			TArray<UObject*> EditedAssets = EditorSS->GetAllEditedAssets();
			for (UObject* Asset : EditedAssets)
			{
				UMaterial* Mat = Cast<UMaterial>(Asset);
				if (Mat && Mat != Material && Mat->MaterialGraph && Mat->GetOutermost() == GetTransientPackage())
				{
					// Candidate preview material — verify it's paired with our material
					TSharedPtr<IMaterialEditor> TestEditor = FMaterialEditorUtilities::GetIMaterialEditorForObject(Mat->MaterialGraph);
					if (TestEditor.IsValid())
					{
						const TArray<UObject*>* EditObjs = TestEditor->GetObjectsCurrentlyBeingEdited();
						if (EditObjs)
						{
							for (UObject* Obj : *EditObjs)
							{
								if (Obj == Material)
								{
									MatEditorPtr = TestEditor;
									PreviewMaterial = Mat;
									UE_LOG(LogMCP, Log, TEXT("auto_comment: Found editor for '%s' via preview material '%s'"),
										*Material->GetName(), *Mat->GetName());
									break;
								}
							}
						}
					}
					if (MatEditorPtr.IsValid()) break;
				}
			}
		}

		// Extract PreviewMaterial from editing objects if not already found
		if (MatEditorPtr.IsValid() && !PreviewMaterial)
		{
			const TArray<UObject*>* EditingObjs = MatEditorPtr->GetObjectsCurrentlyBeingEdited();
			if (EditingObjs && EditingObjs->Num() >= 2)
			{
				PreviewMaterial = Cast<UMaterial>((*EditingObjs)[1]);
			}
		}

		IMaterialEditor* MatEditorRaw = MatEditorPtr.IsValid() ? MatEditorPtr.Get() : nullptr;

		if (MatEditorRaw)
		{
			TSet<UObject*> SelectedNodes = MatEditorRaw->GetSelectedNodes();
			UE_LOG(LogMCP, Log, TEXT("auto_comment: IMaterialEditor::GetSelectedNodes count=%d for '%s'"),
				SelectedNodes.Num(), *Material->GetName());

			bool bHasRootOnly = true;
			for (UObject* SelObj : SelectedNodes)
			{
				UMaterialGraphNode* MatGraphNode = Cast<UMaterialGraphNode>(SelObj);
				if (!MatGraphNode)
				{
					if (Cast<UMaterialGraphNode_Root>(SelObj))
					{
						UE_LOG(LogMCP, Log, TEXT("auto_comment: Root node selected (skipped)"));
					}
					continue;
				}
				bHasRootOnly = false;
				if (MatGraphNode->MaterialExpression
					&& !MatGraphNode->MaterialExpression->IsA<UMaterialExpressionComment>())
				{
					// Map preview expression back to original material expression by index
					UMaterialExpression* PreviewExpr = MatGraphNode->MaterialExpression;
					if (PreviewMaterial && PreviewExpr)
					{
						const TArray<TObjectPtr<UMaterialExpression>>& PreviewExprs = PreviewMaterial->GetExpressionCollection().Expressions;
						int32 Idx = PreviewExprs.IndexOfByKey(PreviewExpr);
						if (Idx != INDEX_NONE)
						{
							const TArray<TObjectPtr<UMaterialExpression>>& OrigExprs = Material->GetExpressionCollection().Expressions;
							if (OrigExprs.IsValidIndex(Idx))
							{
								TargetExpressions.AddUnique(OrigExprs[Idx]);
								continue;
							}
						}
					}
					// Fallback: use the expression directly
					TargetExpressions.AddUnique(PreviewExpr);
				}
			}

			if (TargetExpressions.Num() == 0)
			{
				if (SelectedNodes.Num() > 0 && bHasRootOnly)
				{
					return CreateErrorResponse(TEXT("Only the material output (Root) node is selected. Select expression nodes to wrap."), TEXT("root_only"));
				}
				return CreateErrorResponse(TEXT("No material nodes are currently selected in the editor"), TEXT("no_selection"));
			}
		}
		else
		{
			// Fallback: Neither UAssetEditorSubsystem nor GetIMaterialEditorForObject found the editor.
			// Try SGraphEditor via preview material's graph from GetAllEditedAssets().
			UE_LOG(LogMCP, Log, TEXT("auto_comment: IMaterialEditor lookup failed for '%s', trying SGraphEditor fallback"), *Material->GetName());

			TSharedPtr<SGraphEditor> FallbackGraphEditor;

			// Scan ALL edited assets (including transient preview copies).
			// The SGraphEditor widget uses the preview material's MaterialGraph, not the original's.
			// We must NOT filter by name because the preview material has a different name (e.g. "PreviewMaterial_0").
			if (EditorSS)
			{
				TArray<UObject*> EditedAssets = EditorSS->GetAllEditedAssets();
				for (UObject* Asset : EditedAssets)
				{
					UMaterial* Mat = Cast<UMaterial>(Asset);
					if (Mat && Mat->MaterialGraph)
					{
						TSharedPtr<SGraphEditor> TestEditor = SGraphEditor::FindGraphEditorForGraph(Mat->MaterialGraph);
						if (TestEditor.IsValid())
						{
							// Verify this is the right editor: check if Mat is the preview copy of our Material,
							// or if Mat IS our Material, or if there's only one material editor open.
							bool bIsMatch = (Mat == Material);
							if (!bIsMatch && Mat->GetOutermost() == GetTransientPackage())
							{
								// This is a preview material. Check if the originating editor also has our Material
								// by looking for our material in the edited assets for the same editor.
								IAssetEditorInstance* TestEditorInst = EditorSS->FindEditorForAsset(Mat, false);
								if (TestEditorInst)
								{
									// Check if this same editor also edits our target material
									TArray<IAssetEditorInstance*> OurEditors = EditorSS->FindEditorsForAsset(Material);
									for (IAssetEditorInstance* OurEditor : OurEditors)
									{
										if (OurEditor == TestEditorInst)
										{
											bIsMatch = true;
											PreviewMaterial = Mat;
											break;
										}
									}
								}
							}

							if (bIsMatch)
							{
								FallbackGraphEditor = TestEditor;
								break;
							}
						}
					}
				}
			}

			// Last resort: try original material's graph directly
			if (!FallbackGraphEditor.IsValid() && Material->MaterialGraph)
			{
				FallbackGraphEditor = SGraphEditor::FindGraphEditorForGraph(Material->MaterialGraph);
			}

			if (FallbackGraphEditor.IsValid())
			{
				const FGraphPanelSelectionSet& SelectedNodes = FallbackGraphEditor->GetSelectedNodes();
				UE_LOG(LogMCP, Log, TEXT("auto_comment: SGraphEditor fallback found %d selected nodes for '%s'"),
					SelectedNodes.Num(), *Material->GetName());

				bool bHasRootOnly = true;
				for (UObject* SelObj : SelectedNodes)
				{
					UMaterialGraphNode* MatGraphNode = Cast<UMaterialGraphNode>(SelObj);
					if (!MatGraphNode)
					{
						if (Cast<UMaterialGraphNode_Root>(SelObj))
						{
							UE_LOG(LogMCP, Log, TEXT("auto_comment: Root node selected (skipped)"));
						}
						continue;
					}
					bHasRootOnly = false;
					if (MatGraphNode->MaterialExpression
						&& !MatGraphNode->MaterialExpression->IsA<UMaterialExpressionComment>())
					{
						UMaterialExpression* SelExpr = MatGraphNode->MaterialExpression;
						// Check if expression belongs to the original material directly
						const TArray<TObjectPtr<UMaterialExpression>>& OrigExprs = Material->GetExpressionCollection().Expressions;
						int32 DirectIdx = OrigExprs.IndexOfByKey(SelExpr);
						if (DirectIdx != INDEX_NONE)
						{
							TargetExpressions.AddUnique(SelExpr);
						}
						else
						{
							// Expression is from a preview/transient copy; map by index
							UMaterial* ExprOwner = Cast<UMaterial>(SelExpr->GetOuter());
							if (ExprOwner && ExprOwner != Material)
							{
								const TArray<TObjectPtr<UMaterialExpression>>& OwnerExprs = ExprOwner->GetExpressionCollection().Expressions;
								int32 OwnerIdx = OwnerExprs.IndexOfByKey(SelExpr);
								if (OwnerIdx != INDEX_NONE && OrigExprs.IsValidIndex(OwnerIdx))
								{
									TargetExpressions.AddUnique(OrigExprs[OwnerIdx]);
								}
								else
								{
									TargetExpressions.AddUnique(SelExpr);
								}
							}
							else
							{
								TargetExpressions.AddUnique(SelExpr);
							}
						}
					}
				}

				if (TargetExpressions.Num() == 0)
				{
					if (SelectedNodes.Num() > 0 && bHasRootOnly)
					{
						return CreateErrorResponse(TEXT("Only the material output (Root) node is selected. Select expression nodes to wrap."), TEXT("root_only"));
					}
					return CreateErrorResponse(TEXT("No material nodes are currently selected in the editor"), TEXT("no_selection"));
				}
			}
			else
			{
				UE_LOG(LogMCP, Warning, TEXT("auto_comment: Could not find IMaterialEditor or SGraphEditor for '%s'. Is the material editor open?"), *Material->GetName());
				return CreateErrorResponse(TEXT("Material editor not found. Open the material in the editor first."), TEXT("editor_not_found"));
			}
		}
	}
	else if (NodeNamesArray && NodeNamesArray->Num() > 0)
	{
		for (const auto& Val : *NodeNamesArray)
		{
			FString NodeName = Val->AsString();
			UMaterialExpression* Expr = Context.GetMaterialNode(NodeName);
			if (Expr)
			{
				TargetExpressions.AddUnique(Expr);
			}
			else
			{
				MissingNodes.Add(MakeShared<FJsonValueString>(NodeName));
			}
		}
	}
	else
	{
		// All non-comment expressions
		for (UMaterialExpression* Expr : Material->GetExpressionCollection().Expressions)
		{
			if (Expr && !Expr->IsA<UMaterialExpressionComment>())
			{
				TargetExpressions.Add(Expr);
			}
		}
	}

	if (TargetExpressions.Num() == 0)
	{
		return CreateErrorResponse(TEXT("No expressions found to wrap"), TEXT("no_expressions"));
	}

	// Calculate bounding box using actual node sizes when possible
	float MinX = TNumericLimits<float>::Max();
	float MinY = TNumericLimits<float>::Max();
	float MaxX = TNumericLimits<float>::Lowest();
	float MaxY = TNumericLimits<float>::Lowest();

	// Try to get material editor for accurate node bounds
	// The SGraphEditor displays the PreviewMaterial's MaterialGraph, not the original.
	// We must map original expressions → preview expressions by index to get Slate bounds.
	//
	// NOTE: No dynamic_cast — engine modules compiled without RTTI (/GR-).
	// Use FMaterialEditorUtilities::GetIMaterialEditorForObject (returns TSharedPtr via StaticCastSharedPtr).
	// IMaterialEditor inherits FAssetEditorToolkit, so GetObjectsCurrentlyBeingEdited() is accessible directly.
	TSharedPtr<IMaterialEditor> BoundsMatEditorPtr;
	IMaterialEditor* BoundsMatEditorRaw = nullptr;
	UMaterial* BoundsPreviewMaterial = nullptr;
	UMaterialGraph* BoundsGraph = nullptr;
	{
		// Method 1: Try original material's graph
		if (Material->MaterialGraph)
		{
			BoundsMatEditorPtr = FMaterialEditorUtilities::GetIMaterialEditorForObject(Material->MaterialGraph);
		}

		// Method 2: Find preview material's graph through UAssetEditorSubsystem
		if (!BoundsMatEditorPtr.IsValid())
		{
			UAssetEditorSubsystem* BoundsEditorSS = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
			if (BoundsEditorSS)
			{
				TArray<UObject*> EditedAssets = BoundsEditorSS->GetAllEditedAssets();
				for (UObject* Asset : EditedAssets)
				{
					UMaterial* Mat = Cast<UMaterial>(Asset);
					if (Mat && Mat != Material && Mat->MaterialGraph && Mat->GetOutermost() == GetTransientPackage())
					{
						TSharedPtr<IMaterialEditor> TestEditor = FMaterialEditorUtilities::GetIMaterialEditorForObject(Mat->MaterialGraph);
						if (TestEditor.IsValid())
						{
							const TArray<UObject*>* EditObjs = TestEditor->GetObjectsCurrentlyBeingEdited();
							if (EditObjs)
							{
								for (UObject* Obj : *EditObjs)
								{
									if (Obj == Material)
									{
										BoundsMatEditorPtr = TestEditor;
										BoundsPreviewMaterial = Mat;
										break;
									}
								}
							}
						}
						if (BoundsMatEditorPtr.IsValid()) break;
					}
				}
			}
		}

		if (BoundsMatEditorPtr.IsValid())
		{
			BoundsMatEditorRaw = BoundsMatEditorPtr.Get();

			// Extract PreviewMaterial and its graph
			if (!BoundsPreviewMaterial)
			{
				const TArray<UObject*>* EditObjs = BoundsMatEditorRaw->GetObjectsCurrentlyBeingEdited();
				if (EditObjs && EditObjs->Num() >= 2)
				{
					UMaterial* BoundsPreview = Cast<UMaterial>((*EditObjs)[1]);
					if (BoundsPreview && BoundsPreview->MaterialGraph)
					{
						BoundsPreviewMaterial = BoundsPreview;
					}
				}
			}

			if (BoundsPreviewMaterial && BoundsPreviewMaterial->MaterialGraph)
			{
				BoundsGraph = BoundsPreviewMaterial->MaterialGraph;
			}
			else if (Material->MaterialGraph)
			{
				BoundsGraph = Material->MaterialGraph;
			}

			UE_LOG(LogMCP, Log, TEXT("auto_comment: Bounds editor found for '%s' (PreviewMaterial=%s, Graph=%s)"),
				*Material->GetName(),
				BoundsPreviewMaterial ? *BoundsPreviewMaterial->GetName() : TEXT("none"),
				BoundsGraph ? TEXT("yes") : TEXT("no"));
		}
	}

	for (UMaterialExpression* Expr : TargetExpressions)
	{
		float NodeX = static_cast<float>(Expr->MaterialExpressionEditorX);
		float NodeY = static_cast<float>(Expr->MaterialExpressionEditorY);
		float NodeW = 280.f;
		float NodeH = 80.f;

		// Step 1: Try Slate actual bounds via IMaterialEditor + PreviewMaterial graph
		bool bGotBounds = false;
		if (BoundsMatEditorRaw)
		{
			// Expr is from the original material; the editor displays the PreviewMaterial's graph.
			// We must map original expression → preview expression by index to find the correct
			// graph node that has an active Slate widget in the SGraphEditor.
			UEdGraphNode* BoundsNode = nullptr;
			if (BoundsGraph)
			{
				// Step 1a: Direct pointer match (works when BoundsGraph == original graph)
				for (UEdGraphNode* GN : BoundsGraph->Nodes)
				{
					UMaterialGraphNode* MGN = Cast<UMaterialGraphNode>(GN);
					if (MGN && MGN->MaterialExpression == Expr)
					{
						BoundsNode = GN;
						break;
					}
				}

				// Step 1b: Index-based mapping when BoundsGraph is from PreviewMaterial
				// (Direct match fails because preview expressions are different UObject instances)
				if (!BoundsNode && BoundsPreviewMaterial && BoundsPreviewMaterial != Material)
				{
					const TArray<TObjectPtr<UMaterialExpression>>& OrigExprs = Material->GetExpressionCollection().Expressions;
					int32 ExprIdx = OrigExprs.IndexOfByKey(Expr);
					if (ExprIdx != INDEX_NONE)
					{
						const TArray<TObjectPtr<UMaterialExpression>>& PreviewExprs = BoundsPreviewMaterial->GetExpressionCollection().Expressions;
						if (PreviewExprs.IsValidIndex(ExprIdx))
						{
							UMaterialExpression* PreviewExpr = PreviewExprs[ExprIdx];
							if (PreviewExpr && PreviewExpr->GraphNode)
							{
								BoundsNode = PreviewExpr->GraphNode;
							}
						}
					}
				}
			}
			// Fallback: try Expr->GraphNode directly (may not have Slate widget if preview graph is active)
			if (!BoundsNode)
			{
				BoundsNode = Expr->GraphNode;
			}

			if (BoundsNode)
			{
				FSlateRect Rect;
				BoundsMatEditorRaw->GetBoundsForNode(BoundsNode, Rect, 0.f);
				float W = static_cast<float>(Rect.GetSize().X);
				float H = static_cast<float>(Rect.GetSize().Y);
				if (W > 10.f && H > 10.f)
				{
					NodeW = W;
					NodeH = H;
					bGotBounds = true;
				}
			}
		}

		// Step 2: Heuristic fallback using shared size estimator
		if (!bGotBounds)
		{
			FVector2D EstSize = EstimateMaterialExprNodeSize(Expr);
			NodeW = static_cast<float>(EstSize.X);
			NodeH = static_cast<float>(EstSize.Y);
			UE_LOG(LogMCP, Log, TEXT("auto_comment: node '%s' bounds via heuristic: %.0fx%.0f"),
				*Expr->GetName(), NodeW, NodeH);
		}
		else
		{
			UE_LOG(LogMCP, Log, TEXT("auto_comment: node '%s' bounds via Slate: %.0fx%.0f"),
				*Expr->GetName(), NodeW, NodeH);
		}

		MinX = FMath::Min(MinX, NodeX);
		MinY = FMath::Min(MinY, NodeY);
		MaxX = FMath::Max(MaxX, NodeX + NodeW);
		MaxY = FMath::Max(MaxY, NodeY + NodeH);
	}

	// Apply padding
	float CommentX = MinX - Padding;
	float CommentY = MinY - Padding - 40.f; // Extra space for comment title
	float CommentW = (MaxX - MinX) + 2.f * Padding;
	float CommentH = (MaxY - MinY) + 2.f * Padding + 40.f;

	// Minimum width based on text length
	float MinWidth = static_cast<float>(CommentText.Len()) * 8.f + 40.f;
	CommentW = FMath::Max(CommentW, MinWidth);

	// Note: Collision avoidance with existing comments was intentionally removed.
	// Material editor comments are designed to overlap/nest (e.g. a large comment wrapping
	// multiple smaller commented groups). Displacing comments breaks the "wrap target nodes"
	// contract and pushes comments far from their intended position.

	// Create UMaterialExpressionComment
	UMaterialExpressionComment* Comment = NewObject<UMaterialExpressionComment>(Material);
	Comment->Text = CommentText;
	Comment->MaterialExpressionEditorX = static_cast<int32>(CommentX);
	Comment->MaterialExpressionEditorY = static_cast<int32>(CommentY);
	Comment->SizeX = static_cast<int32>(CommentW);
	Comment->SizeY = static_cast<int32>(CommentH);
	Comment->MaterialExpressionGuid = FGuid::NewGuid();

	// Set color if provided
	const TArray<TSharedPtr<FJsonValue>>* ColorArray = GetOptionalArray(Params, TEXT("color"));
	if (ColorArray && ColorArray->Num() >= 3)
	{
		Comment->CommentColor = FLinearColor(
			(*ColorArray)[0]->AsNumber(),
			(*ColorArray)[1]->AsNumber(),
			(*ColorArray)[2]->AsNumber(),
			ColorArray->Num() > 3 ? (*ColorArray)[3]->AsNumber() : 1.0f);
	}

	// Add to material's EditorComments (NOT Expressions — Comments have a separate collection)
	Material->GetExpressionCollection().AddComment(Comment);

	// Add graph node if material graph is available (ensures comment appears in the editor)
	if (Material->MaterialGraph)
	{
		Material->MaterialGraph->AddComment(Comment);
	}

	// Mark modified
	MarkMaterialModified(Material, Context);

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("comment_text"), CommentText);

	TArray<TSharedPtr<FJsonValue>> PosArr;
	PosArr.Add(MakeShared<FJsonValueNumber>(CommentX));
	PosArr.Add(MakeShared<FJsonValueNumber>(CommentY));
	Result->SetArrayField(TEXT("position"), PosArr);

	TArray<TSharedPtr<FJsonValue>> SizeArr;
	SizeArr.Add(MakeShared<FJsonValueNumber>(CommentW));
	SizeArr.Add(MakeShared<FJsonValueNumber>(CommentH));
	Result->SetArrayField(TEXT("size"), SizeArr);

	Result->SetNumberField(TEXT("nodes_wrapped"), TargetExpressions.Num());
	if (MissingNodes.Num() > 0)
	{
		Result->SetArrayField(TEXT("missing_nodes"), MissingNodes);
	}

	return CreateSuccessResponse(Result);
}


// =========================================================================
// FGetMaterialSelectedNodesAction (P5.5)
// =========================================================================

bool FGetMaterialSelectedNodesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// No required params — material is auto-detected if not specified
	return true;
}

TSharedPtr<FJsonObject> FGetMaterialSelectedNodesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;

	// Try to get material by name, or auto-detect from open editor
	UMaterial* Material = GetMaterialByNameOrCurrent(Params, Context, Error);

	if (!Material)
	{
		// Auto-detect from any open material editor
		UAssetEditorSubsystem* AssetEditorSS = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (AssetEditorSS)
		{
			TArray<UObject*> EditedAssets = AssetEditorSS->GetAllEditedAssets();
			for (UObject* Asset : EditedAssets)
			{
				UMaterial* Mat = Cast<UMaterial>(Asset);
				if (Mat && Mat->GetOutermost() != GetTransientPackage())
				{
					Material = Mat;
					Context.SetCurrentMaterial(Material);
					break;
				}
			}
		}
		if (!Material)
		{
			return CreateErrorResponse(TEXT("No material editor is currently open. Open a material or specify material_name."), TEXT("material_not_found"));
		}
	}

	// Find the material graph and its SGraphEditor
	UMaterialGraph* MatGraph = Material->MaterialGraph;
	if (!MatGraph)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Material '%s' has no graph (not open in editor?)"), *Material->GetName()), TEXT("no_graph"));
	}

	TSharedPtr<SGraphEditor> GraphEditor = SGraphEditor::FindGraphEditorForGraph(MatGraph);
	if (!GraphEditor.IsValid())
	{
		// Fallback: scan all edited assets for any graph editor
		UAssetEditorSubsystem* AssetEditorSS = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (AssetEditorSS)
		{
			TArray<UObject*> EditedAssets = AssetEditorSS->GetAllEditedAssets();
			for (UObject* Asset : EditedAssets)
			{
				UMaterial* Mat = Cast<UMaterial>(Asset);
				if (Mat && Mat->MaterialGraph)
				{
					TSharedPtr<SGraphEditor> TestEditor = SGraphEditor::FindGraphEditorForGraph(Mat->MaterialGraph);
					if (TestEditor.IsValid())
					{
						GraphEditor = TestEditor;
						MatGraph = Mat->MaterialGraph;
						// Update material reference if different
						if (Mat != Material && Mat->GetOutermost() != GetTransientPackage())
						{
							Material = Mat;
							Context.SetCurrentMaterial(Material);
						}
						break;
					}
				}
			}
		}
	}

	if (!GraphEditor.IsValid())
	{
		return CreateErrorResponse(TEXT("Material editor SGraphEditor not found. Make sure the material is open and visible."), TEXT("no_editor"));
	}

	// Get selected nodes
	const FGraphPanelSelectionSet& SelectedNodes = GraphEditor->GetSelectedNodes();

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	bool bHasRootSelected = false;

	for (UObject* SelObj : SelectedNodes)
	{
		if (Cast<UMaterialGraphNode_Root>(SelObj))
		{
			bHasRootSelected = true;
			continue;
		}

		UMaterialGraphNode* MatGraphNode = Cast<UMaterialGraphNode>(SelObj);
		if (!MatGraphNode || !MatGraphNode->MaterialExpression) continue;
		if (MatGraphNode->MaterialExpression->IsA<UMaterialExpressionComment>()) continue;

		UMaterialExpression* Expr = MatGraphNode->MaterialExpression;

		// Determine the expression's index and name in the original material
		const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressionCollection().Expressions;
		int32 ExprIndex = Expressions.IndexOfByKey(Expr);

		// If expression is from a different material (e.g. transient preview), map by index
		if (ExprIndex == INDEX_NONE)
		{
			// Try to match by index if this is a preview copy
			UMaterial* PreviewOwner = Cast<UMaterial>(Expr->GetOuter());
			if (PreviewOwner && PreviewOwner != Material)
			{
				const TArray<TObjectPtr<UMaterialExpression>>& PreviewExprs = PreviewOwner->GetExpressionCollection().Expressions;
				int32 PreviewIdx = PreviewExprs.IndexOfByKey(Expr);
				if (PreviewIdx != INDEX_NONE && Expressions.IsValidIndex(PreviewIdx))
				{
					Expr = Expressions[PreviewIdx];
					ExprIndex = PreviewIdx;
				}
			}
		}

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();

		// Build node name: check session map, Desc, parameter name, then fallback
		FString NodeName;
		for (const auto& Pair : Context.MaterialNodeMap)
		{
			if (Pair.Value.IsValid() && Pair.Value.Get() == Expr)
			{
				NodeName = Pair.Key;
				break;
			}
		}

		if (NodeName.IsEmpty() && !Expr->Desc.IsEmpty())
		{
			NodeName = Expr->Desc;
		}
		if (NodeName.IsEmpty())
		{
			if (auto* ScalarParam = Cast<UMaterialExpressionScalarParameter>(Expr))
			{
				NodeName = ScalarParam->ParameterName.ToString();
			}
			else if (auto* VectorParam = Cast<UMaterialExpressionVectorParameter>(Expr))
			{
				NodeName = VectorParam->ParameterName.ToString();
			}
		}
		if (NodeName.IsEmpty())
		{
			NodeName = Expr->GetName();
		}

		NodeObj->SetStringField(TEXT("node_name"), NodeName);
		NodeObj->SetStringField(TEXT("expression_class"), Expr->GetClass()->GetName());
		NodeObj->SetNumberField(TEXT("pos_x"), Expr->MaterialExpressionEditorX);
		NodeObj->SetNumberField(TEXT("pos_y"), Expr->MaterialExpressionEditorY);
		if (ExprIndex != INDEX_NONE)
		{
			NodeObj->SetNumberField(TEXT("index"), ExprIndex);
		}

		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetStringField(TEXT("material_name"), Material->GetName());
	ResultData->SetNumberField(TEXT("selected_count"), NodesArray.Num());
	ResultData->SetArrayField(TEXT("nodes"), NodesArray);
	if (bHasRootSelected)
	{
		ResultData->SetBoolField(TEXT("root_selected"), true);
	}

	return CreateSuccessResponse(ResultData);
}


// =========================================================================
// FApplyMaterialToComponentAction (P5.2)
// =========================================================================

bool FApplyMaterialToComponentAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ActorName, MaterialPath;
	if (!GetRequiredString(Params, TEXT("actor_name"), ActorName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("material_path"), MaterialPath, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FApplyMaterialToComponentAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString ActorName, MaterialPath;
	GetRequiredString(Params, TEXT("actor_name"), ActorName, Error);
	GetRequiredString(Params, TEXT("material_path"), MaterialPath, Error);

	FString ComponentName = GetOptionalString(Params, TEXT("component_name"), TEXT(""));
	int32 SlotIndex = static_cast<int32>(GetOptionalNumber(Params, TEXT("slot_index"), 0.0));

	// Find actor in the editor world
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorName || It->GetName() == ActorName)
		{
			TargetActor = *It;
			break;
		}
	}

	if (!TargetActor)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found in level"), *ActorName), TEXT("actor_not_found"));
	}

	// Load material
	UMaterialInterface* MaterialToApply = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!MaterialToApply)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Material '%s' not found"), *MaterialPath), TEXT("material_not_found"));
	}

	// Find the target primitive component
	UPrimitiveComponent* TargetComponent = nullptr;

	if (!ComponentName.IsEmpty())
	{
		// Find by component name
		TArray<UActorComponent*> Components;
		TargetActor->GetComponents(Components);
		for (UActorComponent* Comp : Components)
		{
			if (Comp && (Comp->GetName() == ComponentName || Comp->GetFName().ToString() == ComponentName))
			{
				TargetComponent = Cast<UPrimitiveComponent>(Comp);
				if (TargetComponent)
				{
					break;
				}
			}
		}

		if (!TargetComponent)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Component '%s' not found or not a PrimitiveComponent on actor '%s'"), *ComponentName, *ActorName), TEXT("component_not_found"));
		}
	}
	else
	{
		// Find first PrimitiveComponent
		TArray<UPrimitiveComponent*> PrimitiveComponents;
		TargetActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
		if (PrimitiveComponents.Num() > 0)
		{
			TargetComponent = PrimitiveComponents[0];
		}

		if (!TargetComponent)
		{
			return CreateErrorResponse(FString::Printf(TEXT("No PrimitiveComponent found on actor '%s'"), *ActorName), TEXT("no_primitive_component"));
		}
	}

	// Get previous material for reporting
	FString PreviousMaterialPath = TEXT("None");
	UMaterialInterface* PreviousMaterial = TargetComponent->GetMaterial(SlotIndex);
	if (PreviousMaterial)
	{
		PreviousMaterialPath = PreviousMaterial->GetPathName();
	}

	// Apply material
	TargetComponent->SetMaterial(SlotIndex, MaterialToApply);

	// Mark modified
	TargetActor->MarkPackageDirty();

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("actor_name"), ActorName);
	Result->SetStringField(TEXT("component_name"), TargetComponent->GetName());
	Result->SetNumberField(TEXT("slot_index"), SlotIndex);
	Result->SetStringField(TEXT("material_path"), MaterialPath);
	Result->SetStringField(TEXT("previous_material"), PreviousMaterialPath);

	return CreateSuccessResponse(Result);
}


// =========================================================================
// FApplyMaterialToActorAction (P5.4)
// =========================================================================

bool FApplyMaterialToActorAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ActorName, MaterialPath;
	if (!GetRequiredString(Params, TEXT("actor_name"), ActorName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("material_path"), MaterialPath, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FApplyMaterialToActorAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Error;
	FString ActorName, MaterialPath;
	GetRequiredString(Params, TEXT("actor_name"), ActorName, Error);
	GetRequiredString(Params, TEXT("material_path"), MaterialPath, Error);

	int32 SlotIndex = static_cast<int32>(GetOptionalNumber(Params, TEXT("slot_index"), 0.0));

	// Find actor in the editor world
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"), TEXT("no_world"));
	}

	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorName || It->GetName() == ActorName)
		{
			TargetActor = *It;
			break;
		}
	}

	if (!TargetActor)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found in level"), *ActorName), TEXT("actor_not_found"));
	}

	// Load material
	UMaterialInterface* MaterialToApply = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!MaterialToApply)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Material '%s' not found"), *MaterialPath), TEXT("material_not_found"));
	}

	// Find all PrimitiveComponents on the actor
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	TargetActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	if (PrimitiveComponents.Num() == 0)
	{
		return CreateErrorResponse(FString::Printf(TEXT("No PrimitiveComponent found on actor '%s'"), *ActorName), TEXT("no_primitive_component"));
	}

	// Apply material to all components
	TArray<TSharedPtr<FJsonValue>> ComponentNames;
	int32 UpdatedCount = 0;

	for (UPrimitiveComponent* Comp : PrimitiveComponents)
	{
		if (!Comp)
		{
			continue;
		}

		int32 NumMaterials = Comp->GetNumMaterials();
		if (SlotIndex < NumMaterials)
		{
			Comp->SetMaterial(SlotIndex, MaterialToApply);
			ComponentNames.Add(MakeShared<FJsonValueString>(Comp->GetName()));
			++UpdatedCount;
		}
	}

	// Mark modified
	TargetActor->MarkPackageDirty();

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("actor_name"), ActorName);
	Result->SetStringField(TEXT("material_path"), MaterialPath);
	Result->SetNumberField(TEXT("slot_index"), SlotIndex);
	Result->SetNumberField(TEXT("components_updated"), UpdatedCount);
	Result->SetArrayField(TEXT("component_names"), ComponentNames);

	return CreateSuccessResponse(Result);
}

// =========================================================================
// FRefreshMaterialEditorAction
// =========================================================================

bool FRefreshMaterialEditorAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString MaterialName;
	return GetRequiredString(Params, TEXT("material_name"), MaterialName, OutError);
}

TSharedPtr<FJsonObject> FRefreshMaterialEditorAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString MaterialName = Params->GetStringField(TEXT("material_name"));
	FString FindError;
	UMaterial* Material = FindMaterial(MaterialName, FindError);
	if (!Material)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Material '%s' not found: %s"), *MaterialName, *FindError));
	}

	bool bGraphRebuilt = false;
	bool bEditorFound = false;
	bool bPreviewsRefreshed = false;

	// ---- Robust material editor lookup ----
	// The material editor uses a preview material copy (UPreviewMaterial in TransientPackage).
	// The SGraphEditor references the preview material's graph, not the original.
	// We must find the preview material to get a valid IMaterialEditor handle.
	//
	// IMPORTANT: Engine modules compiled WITHOUT RTTI (/GR-), so no dynamic_cast.
	// Use only FMaterialEditorUtilities::GetIMaterialEditorForObject (StaticCastSharedPtr).

	TSharedPtr<IMaterialEditor> MatEditorPtr;
	UMaterial* PreviewMaterial = nullptr;

	// Method 1: Try original material's graph → FMaterialEditorUtilities
	if (Material->MaterialGraph)
	{
		MatEditorPtr = FMaterialEditorUtilities::GetIMaterialEditorForObject(Material->MaterialGraph);
		if (MatEditorPtr.IsValid())
		{
			UE_LOG(LogMCP, Log, TEXT("refresh_material_editor: Found editor for '%s' via GetIMaterialEditorForObject (original graph)"), *MaterialName);
		}
	}

	// Method 2: Find preview material through UAssetEditorSubsystem, then use its graph
	UAssetEditorSubsystem* EditorSS = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	if (!MatEditorPtr.IsValid() && EditorSS)
	{
		TArray<UObject*> EditedAssets = EditorSS->GetAllEditedAssets();
		for (UObject* Asset : EditedAssets)
		{
			UMaterial* Mat = Cast<UMaterial>(Asset);
			if (Mat && Mat != Material && Mat->MaterialGraph && Mat->GetOutermost() == GetTransientPackage())
			{
				// Candidate preview material — verify it's paired with our material
				TSharedPtr<IMaterialEditor> TestEditor = FMaterialEditorUtilities::GetIMaterialEditorForObject(Mat->MaterialGraph);
				if (TestEditor.IsValid())
				{
					const TArray<UObject*>* EditObjs = TestEditor->GetObjectsCurrentlyBeingEdited();
					if (EditObjs)
					{
						for (UObject* Obj : *EditObjs)
						{
							if (Obj == Material)
							{
								MatEditorPtr = TestEditor;
								PreviewMaterial = Mat;
								UE_LOG(LogMCP, Log, TEXT("refresh_material_editor: Found editor for '%s' via preview material '%s'"),
									*MaterialName, *Mat->GetName());
								break;
							}
						}
					}
				}
				if (MatEditorPtr.IsValid()) break;
			}
		}
	}

	// Method 3: Also check non-transient edited assets that match the name directly
	if (!MatEditorPtr.IsValid() && EditorSS)
	{
		TArray<UObject*> EditedAssets = EditorSS->GetAllEditedAssets();
		for (UObject* Asset : EditedAssets)
		{
			UMaterial* Mat = Cast<UMaterial>(Asset);
			if (Mat && Mat->GetName() == MaterialName && Mat->MaterialGraph)
			{
				TSharedPtr<IMaterialEditor> TestEditor = FMaterialEditorUtilities::GetIMaterialEditorForObject(Mat->MaterialGraph);
				if (TestEditor.IsValid())
				{
					MatEditorPtr = TestEditor;
					UE_LOG(LogMCP, Log, TEXT("refresh_material_editor: Found editor for '%s' via name-matched edited asset"), *MaterialName);
					break;
				}
			}
		}
	}

	// Step 1: Rebuild the material graph(s)
	// Rebuild the original material's graph
	if (Material->MaterialGraph)
	{
		Material->MaterialGraph->RebuildGraph();
		Material->MaterialGraph->NotifyGraphChanged();
		bGraphRebuilt = true;
	}
	// Also rebuild the preview material's graph if found (this is what the editor displays)
	if (PreviewMaterial && PreviewMaterial->MaterialGraph)
	{
		PreviewMaterial->MaterialGraph->RebuildGraph();
		PreviewMaterial->MaterialGraph->NotifyGraphChanged();
	}

	// Step 2: Refresh the open Material Editor via IMaterialEditor
	if (MatEditorPtr.IsValid())
	{
		bEditorFound = true;
		MatEditorPtr->UpdateMaterialAfterGraphChange();
		MatEditorPtr->ForceRefreshExpressionPreviews();
		bPreviewsRefreshed = true;
		UE_LOG(LogMCP, Log, TEXT("refresh_material_editor: Refreshed editor UI for '%s'"), *MaterialName);
	}
	else
	{
		UE_LOG(LogMCP, Log, TEXT("refresh_material_editor: No open editor found for '%s' (graph was still rebuilt)"), *MaterialName);
	}

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("material_name"), MaterialName);
	Result->SetBoolField(TEXT("editor_found"), bEditorFound);
	Result->SetBoolField(TEXT("graph_rebuilt"), bGraphRebuilt);
	Result->SetBoolField(TEXT("previews_refreshed"), bPreviewsRefreshed);

	return CreateSuccessResponse(Result);
}
