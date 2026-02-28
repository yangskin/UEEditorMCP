// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actions/EditorAction.h"

/**
 * Create a UMG Widget Blueprint
 */
class UEEDITORMCP_API FCreateUMGWidgetBlueprintAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("CreateUMGWidgetBlueprint"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add a Text Block to a Widget Blueprint
 */
class UEEDITORMCP_API FAddTextBlockToWidgetAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddTextBlockToWidget"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add a Button to a Widget Blueprint
 */
class UEEDITORMCP_API FAddButtonToWidgetAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddButtonToWidget"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add an Image to a Widget Blueprint
 */
class UEEDITORMCP_API FAddImageToWidgetAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddImageToWidget"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add a Border to a Widget Blueprint
 */
class UEEDITORMCP_API FAddBorderToWidgetAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddBorderToWidget"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add an Overlay to a Widget Blueprint
 */
class UEEDITORMCP_API FAddOverlayToWidgetAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddOverlayToWidget"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add a Horizontal Box to a Widget Blueprint
 */
class UEEDITORMCP_API FAddHorizontalBoxToWidgetAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddHorizontalBoxToWidget"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add a Vertical Box to a Widget Blueprint
 */
class UEEDITORMCP_API FAddVerticalBoxToWidgetAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddVerticalBoxToWidget"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add a Slider to a Widget Blueprint
 */
class UEEDITORMCP_API FAddSliderToWidgetAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddSliderToWidget"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add a Progress Bar to a Widget Blueprint
 */
class UEEDITORMCP_API FAddProgressBarToWidgetAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddProgressBarToWidget"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add a Size Box to a Widget Blueprint
 */
class UEEDITORMCP_API FAddSizeBoxToWidgetAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddSizeBoxToWidget"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add a Scale Box to a Widget Blueprint
 */
class UEEDITORMCP_API FAddScaleBoxToWidgetAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddScaleBoxToWidget"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add a Canvas Panel to a Widget Blueprint
 */
class UEEDITORMCP_API FAddCanvasPanelToWidgetAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddCanvasPanelToWidget"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add a ComboBox (String) to a Widget Blueprint
 */
class UEEDITORMCP_API FAddComboBoxToWidgetAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddComboBoxToWidget"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add a CheckBox to a Widget Blueprint
 */
class UEEDITORMCP_API FAddCheckBoxToWidgetAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddCheckBoxToWidget"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add a SpinBox to a Widget Blueprint
 */
class UEEDITORMCP_API FAddSpinBoxToWidgetAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddSpinBoxToWidget"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add an EditableTextBox to a Widget Blueprint
 */
class UEEDITORMCP_API FAddEditableTextBoxToWidgetAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddEditableTextBoxToWidget"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Bind a widget event to a function
 */
class UEEDITORMCP_API FBindWidgetEventAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("BindWidgetEvent"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add widget to viewport (returns class path for Blueprint use)
 */
class UEEDITORMCP_API FAddWidgetToViewportAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddWidgetToViewport"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Set up text block binding to a variable
 */
class UEEDITORMCP_API FSetTextBlockBindingAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("SetTextBlockBinding"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * List components in a Widget Blueprint
 */
class UEEDITORMCP_API FListWidgetComponentsAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("ListWidgetComponents"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Reparent widgets: move specified widgets into a target container
 */
class UEEDITORMCP_API FReparentWidgetsAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("ReparentWidgets"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Set widget properties: position, size, padding, render transform (scale/rotation/shear), alignment, visibility
 */
class UEEDITORMCP_API FSetWidgetPropertiesAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("SetWidgetProperties"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Get the full widget tree with hierarchy, slot info, and render transform for each widget
 */
class UEEDITORMCP_API FGetWidgetTreeAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("GetWidgetTree"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Delete a widget component by name from a Widget Blueprint
 */
class UEEDITORMCP_API FDeleteWidgetFromBlueprintAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("DeleteWidgetFromBlueprint"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Rename a widget component in a Widget Blueprint
 */
class UEEDITORMCP_API FRenameWidgetInBlueprintAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("RenameWidgetInBlueprint"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Move an existing widget to become a child of a specified parent container
 */
class UEEDITORMCP_API FAddWidgetChildAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddWidgetChild"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Delete a UMG Widget Blueprint asset from the project
 */
class UEEDITORMCP_API FDeleteUMGWidgetBlueprintAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("DeleteUMGWidgetBlueprint"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Set/clear/add options on an existing ComboBoxString widget
 * Params: widget_name, target (ComboBox name), options (string array), selected_option, mode ("replace"|"add"|"remove")
 */
class UEEDITORMCP_API FSetComboBoxOptionsAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("SetComboBoxOptions"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Set text on a TextBlock or Button's text in a Widget Blueprint
 * Params: widget_name, target (widget name), text, font_size, color
 */
class UEEDITORMCP_API FSetWidgetTextAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("SetWidgetText"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Set Slider properties (value, min, max, step, etc.) on an existing Slider widget
 * Params: widget_name, target (Slider name), value, min_value, max_value, step_size, locked
 */
class UEEDITORMCP_API FSetSliderPropertiesAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("SetSliderProperties"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add a generic widget to a Widget Blueprint.
 * Handles: ScrollBox, WidgetSwitcher, BackgroundBlur, UniformGridPanel,
 * Spacer, RichTextBlock, WrapBox, CircularThrobber.
 * Uses component_class parameter to determine the widget type.
 */
class UEEDITORMCP_API FAddGenericWidgetAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("AddGenericWidgetToWidget"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;

private:
	UClass* ResolveWidgetClass(const FString& ClassName) const;
};

// =============================================================================
// MVVM Actions — ModelViewViewModel integration for Widget Blueprints
// =============================================================================

/**
 * Associate a ViewModel class with a Widget Blueprint via MVVM Extension.
 * Creates the MVVM BlueprintView if it doesn't exist, then adds the ViewModel context.
 * Params: widget_name, viewmodel_class, viewmodel_name, creation_type
 */
class UEEDITORMCP_API FMVVMAddViewModelAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("MVVMAddViewModel"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Add a property binding between a ViewModel property and a Widget property.
 * Params: widget_name, viewmodel_name, source_property, destination_widget,
 *         destination_property, binding_mode, execution_mode
 */
class UEEDITORMCP_API FMVVMAddBindingAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("MVVMAddBinding"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Read all MVVM ViewModels and Bindings configured on a Widget Blueprint.
 * Params: widget_name
 */
class UEEDITORMCP_API FMVVMGetBindingsAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("MVVMGetBindings"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Remove a MVVM binding from a Widget Blueprint by binding_id.
 * Params: widget_name, binding_id
 */
class UEEDITORMCP_API FMVVMRemoveBindingAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("MVVMRemoveBinding"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};

/**
 * Remove a MVVM ViewModel from a Widget Blueprint by viewmodel_name.
 * Params: widget_name, viewmodel_name
 */
class UEEDITORMCP_API FMVVMRemoveViewModelAction : public FEditorAction
{
public:
	virtual FString GetActionName() const override { return TEXT("MVVMRemoveViewModel"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
};
