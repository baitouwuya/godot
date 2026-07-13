extends "res://tests/editor/mcp/data/mcp_node_base_script.gd"

@export_category("MCP Test")
@export_group("Values")
@export_range(0.0, 10.0, 0.5) var exported_speed: float = 2.5
@export_subgroup("References")
@export var exported_path: NodePath = ^"."
@export var exported_node: Node

@export_storage var stored_only: int = 9
var runtime_only: int = 7
