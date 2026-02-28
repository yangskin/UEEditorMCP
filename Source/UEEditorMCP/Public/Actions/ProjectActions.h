// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actions/EditorAction.h"

/**
 * Create legacy input mapping (Action or Axis)
 */
class UEEDITORMCP_API FCreateInputMappingAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("CreateInputMapping"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Create Enhanced Input Action asset
 */
class UEEDITORMCP_API FCreateInputActionAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("CreateInputAction"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Create Enhanced Input Mapping Context asset
 */
class UEEDITORMCP_API FCreateInputMappingContextAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("CreateInputMappingContext"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add key mapping to Input Mapping Context with optional modifiers
 */
class UEEDITORMCP_API FAddKeyMappingToContextAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddKeyMappingToContext"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};
