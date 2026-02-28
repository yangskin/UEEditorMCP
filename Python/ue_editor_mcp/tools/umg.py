"""
UMG tools - Widget Blueprint creation and manipulation.
"""

import json
from typing import Any
from mcp.types import Tool, TextContent

from ..connection import get_connection


def _send_command(command_type: str, params: dict | None = None) -> list[TextContent]:
    """Helper to send command and format response."""
    conn = get_connection()
    if not conn.is_connected:
        conn.connect()
    result = conn.send_command(command_type, params)
    return [TextContent(type="text", text=json.dumps(result.to_dict(), indent=2))]


def get_tools() -> list[Tool]:
    """Get all UMG tools."""
    return [
        Tool(
            name="create_umg_widget_blueprint",
            description="Create a new UMG Widget Blueprint.",
            inputSchema={
                "type": "object",
                "properties": {
                    "widget_name": {"type": "string", "description": "Name of the widget blueprint"},
                    "parent_class": {"type": "string", "description": "Parent class (default: UserWidget)"},
                    "path": {"type": "string", "description": "Content browser path (default: /Game/UI)"}
                },
                "required": ["widget_name"]
            }
        ),
        Tool(
            name="add_widget_component",
            description="Add a widget component to a UMG Widget Blueprint. Supported component_type values: TextBlock, Button, Image, Border, Overlay, HorizontalBox, VerticalBox, Slider, ProgressBar, SizeBox, ScaleBox, CanvasPanel, ComboBox, CheckBox, SpinBox, EditableTextBox. Pass type-specific properties as needed (e.g. text for TextBlock/Button, options for ComboBox, value for Slider/SpinBox, etc.).",
            inputSchema={
                "type": "object",
                "properties": {
                    "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                    "component_type": {"type": "string", "enum": ["TextBlock", "Button", "Image", "Border", "Overlay", "HorizontalBox", "VerticalBox", "Slider", "ProgressBar", "SizeBox", "ScaleBox", "CanvasPanel", "ComboBox", "CheckBox", "SpinBox", "EditableTextBox", "ScrollBox", "WidgetSwitcher", "BackgroundBlur", "UniformGridPanel", "Spacer", "RichTextBlock", "WrapBox", "CircularThrobber"], "description": "Type of widget component to add"},
                    "component_name": {"type": "string", "description": "Name for the new component"},
                    "text": {"type": "string", "description": "Text content (TextBlock, Button, EditableTextBox, RichTextBlock)"},
                    "position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"},
                    "size": {"type": "array", "items": {"type": "number"}, "description": "[Width, Height]"},
                    "font_size": {"type": "integer", "description": "Font size (TextBlock, Button)"},
                    "color": {"type": "array", "items": {"type": "number"}, "description": "[R, G, B, A] values 0.0-1.0"},
                    "background_color": {"type": "array", "items": {"type": "number"}, "description": "[R, G, B, A] background (Button)"},
                    "texture_path": {"type": "string", "description": "Texture asset path (Image)"},
                    "z_order": {"type": "integer", "description": "Z-order (higher = on top)"},
                    "value": {"type": "number", "description": "Initial value (Slider 0-1, SpinBox)"},
                    "percent": {"type": "number", "description": "Initial percent 0-1 (ProgressBar)"},
                    "options": {"type": "array", "items": {"type": "string"}, "description": "Dropdown options (ComboBox)"},
                    "selected_option": {"type": "string", "description": "Default selected option (ComboBox)"},
                    "is_checked": {"type": "boolean", "description": "Initial checked state (CheckBox)"},
                    "label": {"type": "string", "description": "Text label (CheckBox)"},
                    "min_value": {"type": "number", "description": "Minimum value (SpinBox)"},
                    "max_value": {"type": "number", "description": "Maximum value (SpinBox)"},
                    "delta": {"type": "number", "description": "Step increment (SpinBox, default: 1)"},
                    "hint_text": {"type": "string", "description": "Placeholder text (EditableTextBox)"},
                    "is_read_only": {"type": "boolean", "description": "Read-only (EditableTextBox)"},
                    "blur_strength": {"type": "number", "description": "Blur intensity (BackgroundBlur, default: 10)"},
                    "orientation": {"type": "string", "enum": ["Vertical", "Horizontal"], "description": "Scroll orientation (ScrollBox, default: Vertical)"},
                    "active_index": {"type": "integer", "description": "Active child index (WidgetSwitcher, default: 0)"}
                },
                "required": ["widget_name", "component_type", "component_name"]
            }
        ),
        Tool(
            name="bind_widget_event",
            description="Bind an event on a widget component (e.g., button OnClicked). Creates a Component Bound Event node in the graph that fires when the event occurs.",
            inputSchema={
                "type": "object",
                "properties": {
                    "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                    "widget_component_name": {"type": "string", "description": "Name of the widget component (e.g., RestartButton)"},
                    "event_name": {"type": "string", "description": "Event to bind (OnClicked, OnPressed, OnReleased, etc.)"}
                },
                "required": ["widget_name", "widget_component_name", "event_name"]
            }
        ),
        Tool(
            name="add_widget_to_viewport",
            description="Add a Widget Blueprint instance to the viewport.",
            inputSchema={
                "type": "object",
                "properties": {
                    "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                    "z_order": {"type": "integer", "description": "Z-order (higher = on top)"}
                },
                "required": ["widget_name"]
            }
        ),
        Tool(
            name="set_text_block_binding",
            description="Set up a property binding for a Text Block widget.",
            inputSchema={
                "type": "object",
                "properties": {
                    "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                    "text_block_name": {"type": "string", "description": "Name of the Text Block"},
                    "binding_property": {"type": "string", "description": "Property to bind to"},
                    "binding_type": {"type": "string", "description": "Type of binding (Text, Visibility, etc.)"}
                },
                "required": ["widget_name", "text_block_name", "binding_property"]
            }
        ),
        Tool(
            name="list_widget_components",
            description="List components in a UMG Widget Blueprint.",
            inputSchema={
                "type": "object",
                "properties": {
                    "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"}
                },
                "required": ["widget_name"]
            }
        ),
        Tool(
            name="reparent_widgets",
            description="Move widgets into a target container (VerticalBox, HorizontalBox, Overlay, CanvasPanel, SizeBox, ScaleBox, Border). Use 'children' to specify widget names, 'filter_class' to filter by class (e.g. Button), or omit both to move all root-level widgets.",
            inputSchema={
                "type": "object",
                "properties": {
                    "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                    "target_container_name": {"type": "string", "description": "Name for the target container (creates if not found)"},
                    "container_type": {"type": "string", "description": "Container type: VerticalBox, HorizontalBox, Overlay, CanvasPanel, SizeBox, ScaleBox, Border (default: VerticalBox)"},
                    "children": {"type": "array", "items": {"type": "string"}, "description": "Explicit list of widget names to move (optional)"},
                    "filter_class": {"type": "string", "description": "Move only widgets of this class, e.g. Button, TextBlock, Image (optional)"},
                    "position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position for new container"},
                    "size": {"type": "array", "items": {"type": "number"}, "description": "[Width, Height] for new container"},
                    "z_order": {"type": "integer", "description": "Z-order for new container"}
                },
                "required": ["widget_name", "target_container_name"]
            }
        ),
        Tool(
            name="set_widget_properties",
            description="Set properties on a widget: slot properties (position, size, padding, alignment, anchors, z_order, size_rule, h_align, v_align), render transform (render_scale, render_angle, render_shear, render_translation, render_pivot), visibility, is_enabled. Type-specific: brush_texture/brush_size/color_and_opacity (Image), button_normal_color/button_hovered_color/button_pressed_color (Button), active_widget_index (WidgetSwitcher), blur_strength (BackgroundBlur). Slot properties depend on parent type (CanvasPanel→position/size/anchors; VerticalBox/HorizontalBox→padding/h_align/v_align/size_rule; Overlay→padding/h_align/v_align).",
            inputSchema={
                "type": "object",
                "properties": {
                    "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                    "target": {"type": "string", "description": "Name of the widget to modify"},
                    "position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position (CanvasPanel slot only)"},
                    "size": {"type": "array", "items": {"type": "number"}, "description": "[W, H] size (CanvasPanel slot only)"},
                    "auto_size": {"type": "boolean", "description": "Auto-size (CanvasPanel slot only)"},
                    "z_order": {"type": "integer", "description": "Z-order (CanvasPanel slot only)"},
                    "alignment": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] alignment 0-1 (CanvasPanel slot only)"},
                    "anchors": {"type": "array", "items": {"type": "number"}, "description": "[MinX, MinY, MaxX, MaxY] anchors 0-1 (CanvasPanel slot only)"},
                    "padding": {"type": "array", "items": {"type": "number"}, "description": "[Left, Top, Right, Bottom] padding (VBox/HBox/Overlay slot)"},
                    "h_align": {"type": "string", "description": "Horizontal alignment: Fill, Left, Center, Right (VBox/HBox/Overlay slot)"},
                    "v_align": {"type": "string", "description": "Vertical alignment: Fill, Top, Center, Bottom (VBox/HBox/Overlay slot)"},
                    "size_rule": {"type": "string", "description": "Size rule: Auto or Fill (VBox/HBox slot)"},
                    "render_scale": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] render scale"},
                    "render_angle": {"type": "number", "description": "Render rotation angle in degrees"},
                    "render_shear": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] render shear"},
                    "render_translation": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] render translation offset"},
                    "render_pivot": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] render transform pivot 0-1"},
                    "visibility": {"type": "string", "description": "Visible, Hidden, Collapsed, HitTestInvisible, SelfHitTestInvisible"},
                    "is_enabled": {"type": "boolean", "description": "Whether widget is enabled"},
                    "brush_texture": {"type": "string", "description": "Texture asset path to set on Image widget"},
                    "brush_size": {"type": "array", "items": {"type": "number"}, "description": "[W, H] brush image size (Image)"},
                    "color_and_opacity": {"type": "array", "items": {"type": "number"}, "description": "[R, G, B, A] color and opacity (Image)"},
                    "button_normal_color": {"type": "array", "items": {"type": "number"}, "description": "[R, G, B, A] Normal state tint (Button)"},
                    "button_hovered_color": {"type": "array", "items": {"type": "number"}, "description": "[R, G, B, A] Hovered state tint (Button)"},
                    "button_pressed_color": {"type": "array", "items": {"type": "number"}, "description": "[R, G, B, A] Pressed state tint (Button)"},
                    "active_widget_index": {"type": "integer", "description": "Active child index (WidgetSwitcher)"},
                    "blur_strength": {"type": "number", "description": "Blur intensity (BackgroundBlur)"}
                },
                "required": ["widget_name", "target"]
            }
        ),
        Tool(
            name="set_combo_box_options",
            description="Set/clear/add/remove options on an existing ComboBoxString widget.",
            inputSchema={
                "type": "object",
                "properties": {
                    "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                    "target": {"type": "string", "description": "Name of the ComboBoxString widget"},
                    "mode": {"type": "string", "enum": ["replace", "add", "remove", "clear"], "description": "How to apply options (default: replace)"},
                    "options": {"type": "array", "items": {"type": "string"}, "description": "Options to apply"},
                    "selected_option": {"type": "string", "description": "Option to select after update"}
                },
                "required": ["widget_name", "target"]
            }
        ),
        Tool(
            name="set_widget_text",
            description="Set text/style on an existing TextBlock, or update a Button's child TextBlock if present.",
            inputSchema={
                "type": "object",
                "properties": {
                    "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                    "target": {"type": "string", "description": "Name of the TextBlock or Button widget"},
                    "text": {"type": "string", "description": "Text to set"},
                    "font_size": {"type": "integer", "description": "Font size (TextBlock)"},
                    "color": {"type": "array", "items": {"type": "number"}, "description": "[R, G, B, A] 0-1 (TextBlock)"},
                    "justification": {"type": "string", "enum": ["Left", "Center", "Right"], "description": "Text justification (TextBlock)"},
                    "background_color": {"type": "array", "items": {"type": "number"}, "description": "[R, G, B, A] 0-1 (Button background)"}
                },
                "required": ["widget_name", "target"]
            }
        ),
        Tool(
            name="set_slider_properties",
            description="Set value/range/step/locked on an existing Slider widget.",
            inputSchema={
                "type": "object",
                "properties": {
                    "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                    "target": {"type": "string", "description": "Name of the Slider widget"},
                    "value": {"type": "number", "description": "Slider value (0-1 typically)"},
                    "min_value": {"type": "number", "description": "Min value"},
                    "max_value": {"type": "number", "description": "Max value"},
                    "step_size": {"type": "number", "description": "Step size"},
                    "locked": {"type": "boolean", "description": "Lock slider"}
                },
                "required": ["widget_name", "target"]
            }
        ),
        Tool(
            name="get_widget_tree",
            description="Get the full widget tree with hierarchy, class, slot info (position/size/padding/anchors/alignment), and render transform (translation/scale/shear/angle) for each widget.",
            inputSchema={
                "type": "object",
                "properties": {
                    "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"}
                },
                "required": ["widget_name"]
            }
        ),
        Tool(
            name="delete_widget_from_blueprint",
            description="Delete a widget component by name from a UMG Widget Blueprint. Removes the widget and all its children from the widget tree.",
            inputSchema={
                "type": "object",
                "properties": {
                    "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                    "target": {"type": "string", "description": "Name of the widget component to delete"}
                },
                "required": ["widget_name", "target"]
            }
        ),
        Tool(
            name="rename_widget_in_blueprint",
            description="Rename a widget component in a UMG Widget Blueprint.",
            inputSchema={
                "type": "object",
                "properties": {
                    "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                    "target": {"type": "string", "description": "Current name of the widget component"},
                    "new_name": {"type": "string", "description": "New name for the widget component"}
                },
                "required": ["widget_name", "target", "new_name"]
            }
        ),
        Tool(
            name="add_widget_child",
            description="Move an existing widget to become a child of a specified parent container widget. The widget is removed from its current parent and added to the target parent. Use this to build widget hierarchy after creating widgets.",
            inputSchema={
                "type": "object",
                "properties": {
                    "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                    "child": {"type": "string", "description": "Name of the widget to move (child)"},
                    "parent": {"type": "string", "description": "Name of the target parent container widget"}
                },
                "required": ["widget_name", "child", "parent"]
            }
        ),
        Tool(
            name="delete_umg_widget_blueprint",
            description="Delete a UMG Widget Blueprint asset from the project.",
            inputSchema={
                "type": "object",
                "properties": {
                    "widget_name": {"type": "string", "description": "Name of the Widget Blueprint to delete"}
                },
                "required": ["widget_name"]
            }
        ),
    ]


# Maps component_type -> (C++ command key, name parameter key)
_COMPONENT_TYPE_MAP = {
    "TextBlock": ("add_text_block_to_widget", "text_block_name", None),
    "Button": ("add_button_to_widget", "button_name", None),
    "Image": ("add_image_to_widget", "image_name", None),
    "Border": ("add_border_to_widget", "border_name", None),
    "Overlay": ("add_overlay_to_widget", "overlay_name", None),
    "HorizontalBox": ("add_horizontal_box_to_widget", "horizontal_box_name", None),
    "VerticalBox": ("add_vertical_box_to_widget", "vertical_box_name", None),
    "Slider": ("add_slider_to_widget", "slider_name", None),
    "ProgressBar": ("add_progress_bar_to_widget", "progress_bar_name", None),
    "SizeBox": ("add_size_box_to_widget", "size_box_name", None),
    "ScaleBox": ("add_scale_box_to_widget", "scale_box_name", None),
    "CanvasPanel": ("add_canvas_panel_to_widget", "canvas_panel_name", None),
    "ComboBox": ("add_combo_box_to_widget", "combo_box_name", None),
    "CheckBox": ("add_check_box_to_widget", "check_box_name", None),
    "SpinBox": ("add_spin_box_to_widget", "spin_box_name", None),
    "EditableTextBox": ("add_editable_text_box_to_widget", "editable_text_box_name", None),
    # New generic widget types
    "ScrollBox": ("add_generic_widget_to_widget", "component_name", "ScrollBox"),
    "WidgetSwitcher": ("add_generic_widget_to_widget", "component_name", "WidgetSwitcher"),
    "BackgroundBlur": ("add_generic_widget_to_widget", "component_name", "BackgroundBlur"),
    "UniformGridPanel": ("add_generic_widget_to_widget", "component_name", "UniformGridPanel"),
    "Spacer": ("add_generic_widget_to_widget", "component_name", "Spacer"),
    "RichTextBlock": ("add_generic_widget_to_widget", "component_name", "RichTextBlock"),
    "WrapBox": ("add_generic_widget_to_widget", "component_name", "WrapBox"),
    "CircularThrobber": ("add_generic_widget_to_widget", "component_name", "CircularThrobber"),
}

TOOL_HANDLERS = {
    "create_umg_widget_blueprint": "create_umg_widget_blueprint",
    "add_widget_component": "add_widget_component",
    "bind_widget_event": "bind_widget_event",
    "add_widget_to_viewport": "add_widget_to_viewport",
    "set_text_block_binding": "set_text_block_binding",
    "list_widget_components": "list_widget_components",
    "reparent_widgets": "reparent_widgets",
    "set_widget_properties": "set_widget_properties",
    "set_combo_box_options": "set_combo_box_options",
    "set_widget_text": "set_widget_text",
    "set_slider_properties": "set_slider_properties",
    "get_widget_tree": "get_widget_tree",
    "delete_widget_from_blueprint": "delete_widget_from_blueprint",
    "rename_widget_in_blueprint": "rename_widget_in_blueprint",
    "add_widget_child": "add_widget_child",
    "delete_umg_widget_blueprint": "delete_umg_widget_blueprint",
}


async def handle_tool(name: str, arguments: dict[str, Any]) -> list[TextContent]:
    """Handle a UMG tool call."""
    if name not in TOOL_HANDLERS:
        return [TextContent(type="text", text=f'{{"success": false, "error": "Unknown tool: {name}"}}')]

    # Route the merged add_widget_component tool to the correct C++ command
    if name == "add_widget_component":
        component_type = (arguments or {}).get("component_type", "")
        if component_type not in _COMPONENT_TYPE_MAP:
            return [TextContent(type="text", text=f'{{"success": false, "error": "Unknown component_type: {component_type}. Supported: {list(_COMPONENT_TYPE_MAP.keys())}"}}')]

        command_key, name_param, component_class = _COMPONENT_TYPE_MAP[component_type]

        # Build params: rename component_name -> type-specific name param, drop component_type
        params = dict(arguments)
        params[name_param] = params.pop("component_name")
        params.pop("component_type", None)
        if component_class:
            params["component_class"] = component_class
        return _send_command(command_key, params)

    # All other tools: direct passthrough
    command_type = TOOL_HANDLERS[name]
    return _send_command(command_type, arguments if arguments else None)
