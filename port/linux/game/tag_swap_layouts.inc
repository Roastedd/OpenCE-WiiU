/*
TAG_SWAP_LAYOUTS.INC

Made by tools/wiiu/tag_swap_tables.py from tools/wiiu/map_schema (the big-endian
build's tag schemas and its structures' scalar layouts, as map_convert.py converts
the Xbox maps with them): run it again when they change, rather than editing this.
*/

/* each definition's 2-, 4- and 8-byte values, as runs of one width: offset,
width, count */
static struct tag_swap_run const tag_swap_runs[] =
{
	/* actor_definition (1272 bytes) */
	{ 0x0000, 4, 5 },
	{ 0x0014, 2, 2 },
	{ 0x0018, 4, 148 },
	{ 0x0268, 2, 4 },
	{ 0x0270, 4, 2 },
	{ 0x0278, 2, 2 },
	{ 0x027C, 4, 10 },
	{ 0x02A4, 2, 2 },
	{ 0x02A8, 4, 20 },
	{ 0x02F8, 2, 2 },
	{ 0x02FC, 4, 22 },
	{ 0x0354, 2, 2 },
	{ 0x0358, 4, 104 },
	/* actor_palette (16 bytes) */
	{ 0x0000, 4, 4 },
	/* actor_starting_location (28 bytes) */
	{ 0x0000, 4, 4 },
	{ 0x0010, 2, 1 },
	{ 0x0014, 2, 4 },
	/* actor_variant_change_colors (32 bytes) */
	{ 0x0000, 4, 8 },
	/* actor_variant_definition (568 bytes) */
	{ 0x0000, 4, 19 },
	{ 0x004C, 2, 2 },
	{ 0x0050, 4, 65 },
	{ 0x0154, 2, 2 },
	{ 0x0158, 4, 10 },
	{ 0x0180, 2, 4 },
	{ 0x0188, 4, 18 },
	{ 0x01D0, 2, 2 },
	{ 0x01D4, 4, 3 },
	{ 0x01E0, 2, 2 },
	{ 0x01E4, 4, 10 },
	{ 0x020C, 2, 2 },
	{ 0x0210, 4, 10 },
	/* ai_animation_reference (60 bytes) */
	{ 0x0020, 4, 7 },
	/* ai_command (32 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 2 },
	{ 0x000C, 2, 8 },
	{ 0x001C, 4, 1 },
	/* ai_command_list (96 bytes) */
	{ 0x0020, 4, 3 },
	{ 0x002C, 2, 2 },
	{ 0x0030, 4, 12 },
	/* ai_command_point (20 bytes) */
	{ 0x0000, 4, 5 },
	/* ai_recording_reference (40 bytes) */
	{ 0x0020, 4, 2 },
	/* ai_script_reference (40 bytes) */
	{ 0x0020, 4, 2 },
	/* animation (180 bytes) */
	{ 0x0020, 2, 4 },
	{ 0x0028, 4, 1 },
	{ 0x002C, 2, 2 },
	{ 0x0030, 4, 1 },
	{ 0x0034, 2, 6 },
	{ 0x0042, 2, 1 },
	{ 0x0044, 4, 28 },
	/* animation_graph (128 bytes) */
	{ 0x0000, 4, 25 },
	{ 0x0064, 2, 2 },
	{ 0x0068, 4, 6 },
	/* animation_graph_device_animations (96 bytes) */
	{ 0x0000, 4, 24 },
	/* animation_graph_first_person_weapon_animations (28 bytes) */
	{ 0x0000, 4, 7 },
	/* animation_graph_node (64 bytes) */
	{ 0x0020, 2, 4 },
	{ 0x0028, 4, 6 },
	/* animation_graph_object_overlay (20 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 3 },
	/* animation_graph_sound_reference (20 bytes) */
	{ 0x0000, 4, 5 },
	/* animation_graph_unit_seat (100 bytes) */
	{ 0x0020, 4, 2 },
	{ 0x0028, 2, 2 },
	{ 0x002C, 4, 2 },
	{ 0x0034, 2, 2 },
	{ 0x0038, 4, 11 },
	/* animation_graph_weapon_animations (28 bytes) */
	{ 0x0000, 4, 7 },
	/* animation_graph_weapon_class (188 bytes) */
	{ 0x0060, 4, 2 },
	{ 0x0068, 2, 2 },
	{ 0x006C, 4, 2 },
	{ 0x0074, 2, 2 },
	{ 0x0078, 4, 17 },
	/* animation_graph_weapon_type (60 bytes) */
	{ 0x0020, 4, 7 },
	/* animation_index (2 bytes) */
	{ 0x0000, 2, 1 },
	/* antenna (208 bytes) */
	{ 0x0020, 4, 44 },
	/* antenna_vertex (128 bytes) */
	{ 0x0000, 4, 10 },
	{ 0x0028, 2, 2 },
	{ 0x002C, 4, 21 },
	/* auxilary_meter (324 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 4 },
	{ 0x0014, 2, 2 },
	{ 0x0018, 4, 2 },
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 13 },
	{ 0x0058, 2, 2 },
	{ 0x005C, 4, 2 },
	{ 0x0064, 2, 4 },
	{ 0x006C, 4, 4 },
	{ 0x007C, 2, 2 },
	{ 0x0080, 4, 2 },
	{ 0x0088, 2, 2 },
	{ 0x008C, 4, 13 },
	{ 0x00C2, 2, 1 },
	{ 0x00C6, 2, 1 },
	{ 0x00C8, 4, 31 },
	/* auxilary_overlay (132 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 2 },
	{ 0x000C, 2, 2 },
	{ 0x0010, 4, 13 },
	{ 0x0044, 2, 2 },
	{ 0x0048, 4, 2 },
	{ 0x0050, 2, 4 },
	{ 0x0058, 4, 4 },
	{ 0x0068, 2, 2 },
	{ 0x006C, 4, 6 },
	/* biped (120 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 6 },
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 5 },
	{ 0x0040, 4, 4 },
	/* biped (516 bytes) */
	{ 0x0000, 4, 7 },
	{ 0x001C, 2, 4 },
	{ 0x0024, 4, 116 },
	{ 0x01F4, 2, 2 },
	{ 0x01F8, 4, 3 },
	/* biped_definition (1268 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 65 },
	{ 0x0108, 2, 4 },
	{ 0x0110, 4, 11 },
	{ 0x013C, 2, 2 },
	{ 0x0140, 4, 16 },
	{ 0x0180, 2, 2 },
	{ 0x0184, 4, 5 },
	{ 0x0198, 2, 4 },
	{ 0x01A0, 4, 2 },
	{ 0x01E8, 4, 29 },
	{ 0x025C, 2, 2 },
	{ 0x0260, 4, 14 },
	{ 0x0298, 2, 2 },
	{ 0x029C, 4, 10 },
	{ 0x02C4, 2, 4 },
	{ 0x02CC, 4, 16 },
	{ 0x030C, 2, 4 },
	{ 0x0314, 4, 116 },
	{ 0x04E4, 2, 2 },
	{ 0x04E8, 4, 3 },
	/* biped_palette (48 bytes) */
	{ 0x0000, 4, 12 },
	/* bitmap_data (48 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 10 },
	{ 0x0018, 4, 6 },
	/* bitmap_group (108 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 3 },
	{ 0x0014, 2, 4 },
	{ 0x001C, 4, 12 },
	{ 0x004C, 2, 4 },
	{ 0x0054, 4, 6 },
	/* bitmap_sequence (64 bytes) */
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 7 },
	/* bitmap_sprite (32 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 7 },
	/* breakable_surface_particle_effect (128 bytes) */
	{ 0x0000, 4, 32 },
	/* bsp2d_node (20 bytes) */
	{ 0x0000, 4, 5 },
	/* bsp2d_reference (8 bytes) */
	{ 0x0000, 4, 2 },
	/* bsp3d_node (12 bytes) */
	{ 0x0000, 4, 3 },
	/* bsp_switch_trigger_volume (8 bytes) */
	{ 0x0000, 2, 4 },
	/* camera_track (48 bytes) */
	{ 0x0000, 4, 12 },
	/* camera_track_control_point (60 bytes) */
	{ 0x0000, 4, 15 },
	/* collision_bsp (96 bytes) */
	{ 0x0000, 4, 24 },
	/* collision_edge (24 bytes) */
	{ 0x0000, 4, 6 },
	/* collision_leaf (8 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 1 },
	/* collision_model (664 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 2 },
	{ 0x0008, 4, 50 },
	{ 0x00D0, 2, 2 },
	{ 0x00D4, 4, 6 },
	{ 0x00EC, 2, 2 },
	{ 0x00F0, 4, 106 },
	/* collision_node (64 bytes) */
	{ 0x0020, 2, 4 },
	{ 0x0028, 4, 2 },
	{ 0x0030, 2, 2 },
	{ 0x0034, 4, 3 },
	/* collision_surface (12 bytes) */
	{ 0x0000, 4, 2 },
	{ 0x000A, 2, 1 },
	/* collision_vertex (16 bytes) */
	{ 0x0000, 4, 4 },
	/* color_table (12 bytes) */
	{ 0x0000, 4, 3 },
	/* color_table_color (48 bytes) */
	{ 0x0020, 4, 4 },
	/* continuous_damage_effect (512 bytes) */
	{ 0x0000, 4, 22 },
	{ 0x0058, 2, 2 },
	{ 0x005C, 4, 50 },
	{ 0x0124, 2, 2 },
	{ 0x0128, 4, 54 },
	/* contrail (324 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 5 },
	{ 0x0018, 2, 2 },
	{ 0x001C, 4, 9 },
	{ 0x0040, 2, 2 },
	{ 0x0044, 4, 16 },
	{ 0x0084, 2, 2 },
	{ 0x0088, 4, 7 },
	{ 0x00A4, 2, 4 },
	{ 0x00B0, 2, 1 },
	{ 0x0138, 4, 3 },
	/* contrail_point_state (104 bytes) */
	{ 0x0000, 4, 26 },
	/* control (64 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 6 },
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 1 },
	{ 0x0028, 2, 2 },
	{ 0x002C, 4, 1 },
	{ 0x0030, 2, 3 },
	/* control (136 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 1 },
	{ 0x0058, 4, 12 },
	/* control_definition (792 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 65 },
	{ 0x0108, 2, 4 },
	{ 0x0110, 4, 11 },
	{ 0x013C, 2, 2 },
	{ 0x0140, 4, 22 },
	{ 0x0198, 2, 4 },
	{ 0x01A0, 4, 60 },
	{ 0x0290, 2, 2 },
	{ 0x0294, 4, 1 },
	{ 0x02E8, 4, 12 },
	/* control_palette (48 bytes) */
	{ 0x0000, 4, 12 },
	/* conversation (116 bytes) */
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 2 },
	{ 0x0050, 4, 9 },
	/* conversation_line (124 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 2 },
	{ 0x001C, 4, 24 },
	/* conversation_participant (84 bytes) */
	{ 0x0000, 2, 6 },
	{ 0x0018, 2, 6 },
	{ 0x0044, 4, 1 },
	/* cutscene_camera_point (104 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0024, 4, 17 },
	/* cutscene_flag (92 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0024, 4, 5 },
	/* cutscene_title (96 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0024, 4, 1 },
	{ 0x0028, 2, 8 },
	{ 0x0038, 4, 6 },
	/* damage (220 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 54 },
	/* damage_effect (672 bytes) */
	{ 0x0000, 4, 9 },
	{ 0x0024, 2, 2 },
	{ 0x0028, 4, 4 },
	{ 0x0038, 2, 2 },
	{ 0x003C, 4, 10 },
	{ 0x0064, 2, 2 },
	{ 0x0068, 4, 4 },
	{ 0x0078, 2, 2 },
	{ 0x007C, 4, 8 },
	{ 0x009C, 2, 2 },
	{ 0x00A0, 4, 12 },
	{ 0x00D0, 2, 2 },
	{ 0x00D4, 4, 5 },
	{ 0x00E8, 2, 2 },
	{ 0x00EC, 4, 54 },
	{ 0x01C4, 2, 2 },
	{ 0x01C8, 4, 54 },
	/* damage_region (84 bytes) */
	{ 0x0020, 4, 13 },
	/* damage_resistance (600 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 2 },
	{ 0x0008, 4, 50 },
	{ 0x00D0, 2, 2 },
	{ 0x00D4, 4, 6 },
	{ 0x00EC, 2, 2 },
	{ 0x00F0, 4, 90 },
	/* damage_resistance_material (72 bytes) */
	{ 0x0020, 4, 1 },
	{ 0x0024, 2, 2 },
	{ 0x0028, 4, 8 },
	/* decal (268 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 20 },
	{ 0x0058, 2, 2 },
	{ 0x005C, 4, 14 },
	{ 0x0094, 2, 2 },
	{ 0x0098, 4, 7 },
	{ 0x00B4, 2, 8 },
	{ 0x00C4, 4, 15 },
	{ 0x0100, 2, 2 },
	{ 0x0104, 4, 2 },
	/* decal_palette (16 bytes) */
	{ 0x0000, 4, 4 },
	/* detail_object (6 bytes) */
	{ 0x0004, 2, 1 },
	/* detail_object_cell (32 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 6 },
	/* detail_object_collection (128 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 31 },
	/* detail_object_collection_palette (48 bytes) */
	{ 0x0000, 4, 4 },
	/* detail_object_count (2 bytes) */
	{ 0x0000, 2, 1 },
	/* detail_object_type (96 bytes) */
	{ 0x0024, 4, 7 },
	/* detail_object_z_reference_vector (16 bytes) */
	{ 0x0000, 4, 4 },
	/* device (276 bytes) */
	{ 0x0000, 4, 7 },
	{ 0x001C, 2, 4 },
	{ 0x0024, 4, 60 },
	/* device_definition (656 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 65 },
	{ 0x0108, 2, 4 },
	{ 0x0110, 4, 11 },
	{ 0x013C, 2, 2 },
	{ 0x0140, 4, 22 },
	{ 0x0198, 2, 4 },
	{ 0x01A0, 4, 60 },
	/* device_group (52 bytes) */
	{ 0x0020, 4, 5 },
	/* dialogue_definition (4112 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 1027 },
	/* effect (64 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 2 },
	{ 0x0008, 4, 14 },
	/* effect_event (68 bytes) */
	{ 0x0000, 4, 17 },
	/* effect_part (104 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 24 },
	/* effect_particles (232 bytes) */
	{ 0x0000, 2, 6 },
	{ 0x000C, 4, 23 },
	{ 0x0068, 2, 4 },
	{ 0x0070, 4, 30 },
	/* encounter (176 bytes) */
	{ 0x0020, 4, 1 },
	{ 0x0024, 2, 4 },
	{ 0x002C, 4, 20 },
	{ 0x007C, 2, 2 },
	{ 0x0080, 4, 12 },
	/* encounter_player_starting_location (52 bytes) */
	{ 0x0000, 4, 4 },
	{ 0x0010, 2, 6 },
	/* equipment (24 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 5 },
	/* equipment (40 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 6 },
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 1 },
	/* equipment_definition (800 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 65 },
	{ 0x0108, 2, 4 },
	{ 0x0110, 4, 11 },
	{ 0x013C, 2, 2 },
	{ 0x0140, 4, 16 },
	{ 0x0180, 2, 2 },
	{ 0x0184, 4, 1 },
	{ 0x0188, 2, 2 },
	{ 0x018C, 4, 4 },
	{ 0x019C, 2, 4 },
	{ 0x01A4, 4, 89 },
	{ 0x0308, 2, 2 },
	{ 0x030C, 4, 5 },
	/* equipment_palette (48 bytes) */
	{ 0x0000, 4, 12 },
	/* firing_position (24 bytes) */
	{ 0x0000, 4, 3 },
	{ 0x000C, 2, 2 },
	{ 0x0014, 4, 1 },
	/* flag (96 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 6 },
	{ 0x0010, 4, 20 },
	/* flag_attachment_point (52 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 4 },
	/* fog (396 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 1 },
	{ 0x0058, 4, 1 },
	{ 0x0060, 4, 1 },
	{ 0x0068, 4, 1 },
	{ 0x0074, 4, 4 },
	{ 0x0084, 2, 2 },
	{ 0x0088, 4, 5 },
	{ 0x00A0, 4, 4 },
	{ 0x00B8, 4, 13 },
	{ 0x00F4, 4, 8 },
	/* font (156 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 4 },
	{ 0x000C, 4, 36 },
	/* font_character (20 bytes) */
	{ 0x0000, 2, 8 },
	{ 0x0010, 4, 1 },
	/* font_character_index (2 bytes) */
	{ 0x0000, 2, 1 },
	/* font_character_table (12 bytes) */
	{ 0x0000, 4, 3 },
	/* game_globals (428 bytes) */
	{ 0x0000, 4, 107 },
	/* game_globals_difficulty_information (644 bytes) */
	{ 0x0000, 4, 161 },
	/* game_globals_falling_damage (152 bytes) */
	{ 0x0000, 4, 38 },
	/* game_globals_first_person_interface (192 bytes) */
	{ 0x0000, 4, 12 },
	{ 0x0030, 2, 2 },
	{ 0x0034, 4, 4 },
	{ 0x0044, 2, 2 },
	{ 0x0048, 4, 30 },
	/* game_globals_grenade (68 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 16 },
	/* game_globals_interface_tag_references (304 bytes) */
	{ 0x0000, 4, 76 },
	/* game_globals_multiplayer_information (160 bytes) */
	{ 0x0000, 4, 26 },
	/* game_globals_player_control (128 bytes) */
	{ 0x0000, 4, 27 },
	{ 0x006C, 2, 2 },
	{ 0x0070, 4, 4 },
	/* game_globals_player_information (244 bytes) */
	{ 0x0000, 4, 61 },
	/* game_globals_rasterizer_data (428 bytes) */
	{ 0x0000, 4, 92 },
	{ 0x0170, 2, 2 },
	{ 0x0174, 4, 14 },
	/* game_globals_vehicle (16 bytes) */
	{ 0x0000, 4, 4 },
	/* garbage_definition (776 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 65 },
	{ 0x0108, 2, 4 },
	{ 0x0110, 4, 11 },
	{ 0x013C, 2, 2 },
	{ 0x0140, 4, 16 },
	{ 0x0180, 2, 2 },
	{ 0x0184, 4, 1 },
	{ 0x0188, 2, 2 },
	{ 0x018C, 4, 4 },
	{ 0x019C, 2, 4 },
	{ 0x01A4, 4, 89 },
	/* gbxmodel (232 bytes) */
	{ 0x0000, 4, 7 },
	{ 0x001C, 2, 6 },
	{ 0x0028, 4, 48 },
	/* gbxmodel_geometry (48 bytes) */
	{ 0x0024, 4, 3 },
	/* gbxmodel_geometry_part (132 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 1 },
	{ 0x0008, 2, 2 },
	{ 0x000C, 4, 14 },
	{ 0x0044, 2, 2 },
	{ 0x0048, 4, 3 },
	{ 0x0054, 2, 2 },
	{ 0x0058, 4, 4 },
	{ 0x0082, 2, 1 },
	/* glow (340 bytes) */
	{ 0x0020, 2, 4 },
	{ 0x0028, 4, 8 },
	{ 0x0048, 2, 2 },
	{ 0x004C, 4, 1 },
	{ 0x0050, 2, 2 },
	{ 0x0054, 4, 3 },
	{ 0x0060, 2, 2 },
	{ 0x0064, 4, 3 },
	{ 0x0070, 2, 2 },
	{ 0x0074, 4, 3 },
	{ 0x0080, 2, 2 },
	{ 0x0084, 4, 6 },
	{ 0x009C, 2, 2 },
	{ 0x00A0, 4, 4 },
	{ 0x00B0, 2, 2 },
	{ 0x00B4, 4, 40 },
	/* grenade_count_panel (192 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 2 },
	{ 0x000C, 2, 2 },
	{ 0x0010, 4, 13 },
	{ 0x0044, 2, 2 },
	{ 0x0048, 4, 2 },
	{ 0x0050, 2, 4 },
	{ 0x0058, 4, 4 },
	{ 0x0068, 2, 2 },
	{ 0x006C, 4, 2 },
	{ 0x0074, 2, 2 },
	{ 0x0078, 4, 9 },
	{ 0x009C, 2, 2 },
	{ 0x00A0, 4, 2 },
	{ 0x00A8, 2, 2 },
	{ 0x00B0, 4, 3 },
	{ 0x00BC, 2, 2 },
	/* grenade_hud_interface (504 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 8 },
	{ 0x0024, 2, 2 },
	{ 0x0028, 4, 2 },
	{ 0x0030, 2, 2 },
	{ 0x0034, 4, 13 },
	{ 0x0068, 2, 2 },
	{ 0x006C, 4, 2 },
	{ 0x0074, 2, 4 },
	{ 0x007C, 4, 4 },
	{ 0x008C, 2, 2 },
	{ 0x0090, 4, 2 },
	{ 0x0098, 2, 2 },
	{ 0x009C, 4, 13 },
	{ 0x00D0, 2, 2 },
	{ 0x00D4, 4, 2 },
	{ 0x00DC, 2, 4 },
	{ 0x00E4, 4, 4 },
	{ 0x00F4, 2, 2 },
	{ 0x00F8, 4, 2 },
	{ 0x0100, 2, 2 },
	{ 0x0104, 4, 9 },
	{ 0x0128, 2, 2 },
	{ 0x012C, 4, 2 },
	{ 0x0134, 2, 2 },
	{ 0x013C, 4, 3 },
	{ 0x0148, 2, 2 },
	{ 0x014C, 4, 43 },
	/* hs_global (92 bytes) */
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 2 },
	/* hs_reference (40 bytes) */
	{ 0x0018, 4, 4 },
	/* hs_script (92 bytes) */
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 1 },
	/* hs_source_file (52 bytes) */
	{ 0x0020, 4, 5 },
	/* hud_absolute_placement (36 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 8 },
	/* hud_globals (1104 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 8 },
	{ 0x0024, 2, 2 },
	{ 0x0028, 4, 2 },
	{ 0x0030, 2, 2 },
	{ 0x0034, 4, 43 },
	{ 0x00E0, 2, 2 },
	{ 0x00E4, 4, 2 },
	{ 0x00EC, 2, 2 },
	{ 0x00F0, 4, 8 },
	{ 0x0110, 2, 2 },
	{ 0x0114, 4, 2 },
	{ 0x011C, 2, 2 },
	{ 0x0120, 4, 111 },
	{ 0x02DC, 2, 4 },
	{ 0x02E4, 4, 11 },
	{ 0x0310, 2, 4 },
	{ 0x0318, 4, 12 },
	{ 0x0348, 2, 2 },
	{ 0x034C, 4, 9 },
	{ 0x0370, 2, 2 },
	{ 0x0374, 4, 2 },
	{ 0x037C, 2, 2 },
	{ 0x0380, 4, 4 },
	{ 0x0390, 2, 2 },
	{ 0x0394, 4, 2 },
	{ 0x039C, 2, 2 },
	{ 0x03A0, 4, 14 },
	{ 0x03D8, 2, 4 },
	{ 0x03E0, 4, 28 },
	/* hud_message_text (128 bytes) */
	{ 0x0000, 4, 32 },
	/* hud_number (100 bytes) */
	{ 0x0000, 4, 4 },
	{ 0x0016, 2, 1 },
	{ 0x0018, 4, 19 },
	/* hud_screen_effect (184 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 2 },
	{ 0x0008, 4, 14 },
	{ 0x0040, 2, 2 },
	{ 0x0044, 4, 10 },
	{ 0x006C, 2, 2 },
	{ 0x0070, 4, 7 },
	{ 0x008C, 2, 2 },
	{ 0x0090, 4, 10 },
	/* hud_sound (56 bytes) */
	{ 0x0000, 4, 6 },
	/* hud_state_message (64 bytes) */
	{ 0x0020, 2, 2 },
	{ 0x0028, 4, 6 },
	/* hud_waypoint_arrow (104 bytes) */
	{ 0x0020, 4, 5 },
	{ 0x0034, 2, 4 },
	{ 0x003C, 4, 11 },
	/* icon_hud_element (16 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 1 },
	{ 0x000E, 2, 1 },
	/* inertial_matrix (36 bytes) */
	{ 0x0000, 4, 9 },
	/* item (396 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 2 },
	{ 0x0008, 4, 1 },
	{ 0x000C, 2, 2 },
	{ 0x0010, 4, 4 },
	{ 0x0020, 2, 4 },
	{ 0x0028, 4, 89 },
	/* item_collection_definition (92 bytes) */
	{ 0x0000, 4, 3 },
	{ 0x000C, 2, 2 },
	{ 0x0010, 4, 19 },
	/* item_definition (776 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 65 },
	{ 0x0108, 2, 4 },
	{ 0x0110, 4, 11 },
	{ 0x013C, 2, 2 },
	{ 0x0140, 4, 16 },
	{ 0x0180, 2, 2 },
	{ 0x0184, 4, 1 },
	{ 0x0188, 2, 2 },
	{ 0x018C, 4, 4 },
	{ 0x019C, 2, 4 },
	{ 0x01A4, 4, 89 },
	/* item_permutation (84 bytes) */
	{ 0x0000, 4, 21 },
	/* leaf_map (28 bytes) */
	{ 0x0000, 4, 7 },
	/* leaf_portal (24 bytes) */
	{ 0x0000, 4, 6 },
	/* lens_flare (240 bytes) */
	{ 0x0000, 4, 5 },
	{ 0x0014, 2, 2 },
	{ 0x0018, 4, 6 },
	{ 0x0030, 2, 2 },
	{ 0x0080, 2, 2 },
	{ 0x0084, 4, 1 },
	{ 0x00A0, 4, 2 },
	{ 0x00C4, 4, 3 },
	/* lens_flare_reflection (128 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x001C, 4, 2 },
	{ 0x0028, 4, 2 },
	{ 0x0030, 2, 2 },
	{ 0x0034, 4, 2 },
	{ 0x003C, 2, 2 },
	{ 0x0040, 4, 12 },
	{ 0x0070, 2, 2 },
	{ 0x0074, 4, 2 },
	/* light (352 bytes) */
	{ 0x0000, 4, 11 },
	{ 0x0034, 4, 9 },
	{ 0x0064, 4, 4 },
	{ 0x0074, 2, 2 },
	{ 0x0078, 4, 5 },
	{ 0x008C, 2, 2 },
	{ 0x0090, 4, 1 },
	{ 0x0094, 2, 2 },
	{ 0x0098, 4, 1 },
	{ 0x009C, 2, 2 },
	{ 0x00A0, 4, 1 },
	{ 0x00AC, 4, 4 },
	{ 0x00F4, 4, 1 },
	{ 0x00F8, 2, 2 },
	/* light_fixture (88 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 6 },
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 1 },
	{ 0x0028, 2, 2 },
	{ 0x002C, 4, 7 },
	/* light_fixture_definition (656 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 65 },
	{ 0x0108, 2, 4 },
	{ 0x0110, 4, 11 },
	{ 0x013C, 2, 2 },
	{ 0x0140, 4, 22 },
	{ 0x0198, 2, 4 },
	{ 0x01A0, 4, 60 },
	/* light_fixture_palette (48 bytes) */
	{ 0x0000, 4, 12 },
	/* light_volume (332 bytes) */
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 8 },
	{ 0x0044, 2, 2 },
	{ 0x0048, 4, 9 },
	{ 0x006C, 2, 2 },
	{ 0x0070, 4, 18 },
	{ 0x00B8, 2, 2 },
	{ 0x00BC, 4, 36 },
	/* light_volume_frame (176 bytes) */
	{ 0x0000, 4, 44 },
	/* lightning (264 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0014, 4, 2 },
	{ 0x002C, 2, 4 },
	{ 0x0034, 4, 4 },
	{ 0x0098, 4, 6 },
	/* lightning_marker (228 bytes) */
	{ 0x0020, 2, 4 },
	{ 0x0074, 4, 9 },
	/* lightning_shader (180 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 7 },
	{ 0x0020, 2, 8 },
	{ 0x004C, 4, 4 },
	{ 0x005C, 2, 4 },
	{ 0x0064, 4, 3 },
	{ 0x0070, 2, 2 },
	{ 0x0074, 4, 3 },
	{ 0x0080, 2, 2 },
	{ 0x0084, 4, 7 },
	/* long (4 bytes) */
	{ 0x0000, 4, 1 },
	/* look_function_value (4 bytes) */
	{ 0x0000, 4, 1 },
	/* looping_sound (84 bytes) */
	{ 0x0000, 4, 21 },
	/* looping_sound_detail (104 bytes) */
	{ 0x0000, 4, 26 },
	/* looping_sound_track (160 bytes) */
	{ 0x0000, 4, 40 },
	/* machine (64 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 6 },
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 1 },
	{ 0x0028, 2, 2 },
	{ 0x002C, 4, 1 },
	/* machine (148 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 21 },
	{ 0x0058, 2, 2 },
	{ 0x005C, 4, 14 },
	/* machine_definition (804 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 65 },
	{ 0x0108, 2, 4 },
	{ 0x0110, 4, 11 },
	{ 0x013C, 2, 2 },
	{ 0x0140, 4, 22 },
	{ 0x0198, 2, 4 },
	{ 0x01A0, 4, 60 },
	{ 0x0290, 2, 2 },
	{ 0x0294, 4, 21 },
	{ 0x02E8, 2, 2 },
	{ 0x02EC, 4, 14 },
	/* machine_palette (48 bytes) */
	{ 0x0000, 4, 12 },
	/* map_leaf (24 bytes) */
	{ 0x0000, 4, 6 },
	/* map_leaf_face (16 bytes) */
	{ 0x0000, 4, 4 },
	/* mass_point (128 bytes) */
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 14 },
	{ 0x005C, 2, 2 },
	{ 0x0060, 4, 3 },
	/* material (884 bytes) */
	{ 0x0000, 4, 221 },
	/* material_effect (28 bytes) */
	{ 0x0000, 4, 3 },
	/* material_effect_material (48 bytes) */
	{ 0x0000, 4, 8 },
	/* material_effects (12 bytes) */
	{ 0x0000, 4, 3 },
	/* meter_hud_element (104 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 2 },
	{ 0x000C, 2, 2 },
	{ 0x0010, 4, 13 },
	{ 0x0046, 2, 1 },
	{ 0x004A, 2, 1 },
	{ 0x004C, 4, 7 },
	/* metered_panel (240 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 2 },
	{ 0x000C, 2, 2 },
	{ 0x0010, 4, 13 },
	{ 0x0044, 2, 2 },
	{ 0x0048, 4, 2 },
	{ 0x0050, 2, 4 },
	{ 0x0058, 4, 4 },
	{ 0x0068, 2, 2 },
	{ 0x006C, 4, 2 },
	{ 0x0074, 2, 2 },
	{ 0x0078, 4, 13 },
	{ 0x00AE, 2, 1 },
	{ 0x00B2, 2, 1 },
	{ 0x00B4, 4, 15 },
	/* model (232 bytes) */
	{ 0x0000, 4, 7 },
	{ 0x001C, 2, 6 },
	{ 0x0028, 4, 48 },
	/* model_geometry (48 bytes) */
	{ 0x0024, 4, 3 },
	/* model_geometry_part (104 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 1 },
	{ 0x0008, 2, 2 },
	{ 0x000C, 4, 14 },
	{ 0x0044, 2, 2 },
	{ 0x0048, 4, 3 },
	{ 0x0054, 2, 2 },
	{ 0x0058, 4, 4 },
	/* model_marker (64 bytes) */
	{ 0x0020, 4, 8 },
	/* model_marker_instance (32 bytes) */
	{ 0x0004, 4, 7 },
	/* model_node (156 bytes) */
	{ 0x0020, 2, 4 },
	{ 0x0028, 4, 29 },
	/* model_region (76 bytes) */
	{ 0x0020, 4, 11 },
	/* model_region_permutation (88 bytes) */
	{ 0x0020, 4, 1 },
	{ 0x0024, 2, 2 },
	{ 0x0028, 4, 6 },
	{ 0x0040, 2, 6 },
	{ 0x004C, 4, 3 },
	/* model_region_permutation_marker (80 bytes) */
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 7 },
	/* model_shader_reference (32 bytes) */
	{ 0x0000, 4, 4 },
	{ 0x0010, 2, 2 },
	{ 0x0014, 4, 3 },
	/* model_triangle (6 bytes) */
	{ 0x0000, 2, 3 },
	/* motion_sensor_panel (240 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 2 },
	{ 0x000C, 2, 2 },
	{ 0x0010, 4, 13 },
	{ 0x0044, 2, 2 },
	{ 0x0048, 4, 2 },
	{ 0x0050, 2, 4 },
	{ 0x0058, 4, 4 },
	{ 0x0068, 2, 2 },
	{ 0x006C, 4, 2 },
	{ 0x0074, 2, 2 },
	{ 0x0078, 4, 13 },
	{ 0x00AC, 2, 2 },
	{ 0x00B0, 4, 2 },
	{ 0x00B8, 2, 4 },
	{ 0x00C0, 4, 12 },
	/* move_position (80 bytes) */
	{ 0x0000, 4, 7 },
	{ 0x001C, 2, 1 },
	{ 0x0020, 4, 2 },
	{ 0x0028, 2, 2 },
	{ 0x002C, 4, 9 },
	/* multiplayer_scenario_description (68 bytes) */
	{ 0x0000, 4, 8 },
	{ 0x0040, 4, 1 },
	/* multiplayer_scenario_descriptions (12 bytes) */
	{ 0x0000, 4, 3 },
	/* multitexture_overlay (480 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 8 },
	{ 0x0028, 2, 6 },
	{ 0x0034, 4, 24 },
	{ 0x0094, 2, 4 },
	{ 0x009C, 4, 81 },
	/* multitexture_overlay_effector (220 bytes) */
	{ 0x0000, 4, 16 },
	{ 0x0040, 2, 4 },
	{ 0x0048, 4, 26 },
	{ 0x00B0, 2, 2 },
	{ 0x00B4, 4, 10 },
	/* netgame_equipment (144 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 6 },
	{ 0x0010, 4, 32 },
	/* netgame_flag (148 bytes) */
	{ 0x0000, 4, 4 },
	{ 0x0010, 2, 2 },
	{ 0x0014, 4, 32 },
	/* object (380 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 65 },
	{ 0x0108, 2, 4 },
	{ 0x0110, 4, 11 },
	{ 0x013C, 2, 2 },
	{ 0x0140, 4, 15 },
	/* object_attachment (72 bytes) */
	{ 0x0000, 4, 4 },
	{ 0x0030, 2, 4 },
	{ 0x0038, 4, 4 },
	/* object_change_color (44 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 10 },
	/* object_change_color_permutation (28 bytes) */
	{ 0x0000, 4, 7 },
	/* object_definition (380 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 65 },
	{ 0x0108, 2, 4 },
	{ 0x0110, 4, 11 },
	{ 0x013C, 2, 2 },
	{ 0x0140, 4, 15 },
	/* object_function (360 bytes) */
	{ 0x0000, 4, 2 },
	{ 0x0008, 2, 4 },
	{ 0x0010, 4, 3 },
	{ 0x001C, 2, 6 },
	{ 0x0028, 4, 3 },
	{ 0x0034, 2, 2 },
	{ 0x0038, 4, 68 },
	/* object_name (36 bytes) */
	{ 0x0020, 2, 2 },
	/* object_widget (32 bytes) */
	{ 0x0000, 4, 8 },
	/* optional_meter_hud_element (104 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 2 },
	{ 0x000C, 2, 2 },
	{ 0x0010, 4, 13 },
	{ 0x0046, 2, 1 },
	{ 0x004A, 2, 1 },
	{ 0x004C, 4, 7 },
	/* optional_static_hud_element (104 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 2 },
	{ 0x000C, 2, 2 },
	{ 0x0010, 4, 13 },
	{ 0x0044, 2, 2 },
	{ 0x0048, 4, 2 },
	{ 0x0050, 2, 4 },
	{ 0x0058, 4, 4 },
	/* particle (356 bytes) */
	{ 0x0000, 4, 38 },
	{ 0x0098, 2, 4 },
	{ 0x00A0, 4, 3 },
	{ 0x00AC, 2, 4 },
	{ 0x00B4, 4, 7 },
	{ 0x00D0, 2, 8 },
	{ 0x00FC, 4, 4 },
	{ 0x010C, 2, 4 },
	{ 0x0114, 4, 3 },
	{ 0x0120, 2, 2 },
	{ 0x0124, 4, 3 },
	{ 0x0130, 2, 2 },
	{ 0x0134, 4, 7 },
	/* particle_system (104 bytes) */
	{ 0x0038, 4, 4 },
	{ 0x0048, 2, 2 },
	{ 0x004C, 4, 7 },
	/* particle_system_particle_state (376 bytes) */
	{ 0x0020, 4, 8 },
	{ 0x0040, 2, 2 },
	{ 0x0044, 4, 20 },
	{ 0x00B8, 2, 2 },
	{ 0x00BC, 4, 7 },
	{ 0x00D8, 2, 8 },
	{ 0x0104, 4, 4 },
	{ 0x0114, 2, 4 },
	{ 0x011C, 4, 3 },
	{ 0x0128, 2, 2 },
	{ 0x012C, 4, 3 },
	{ 0x0138, 2, 2 },
	{ 0x013C, 4, 7 },
	/* particle_system_physics_constant (4 bytes) */
	{ 0x0000, 4, 1 },
	/* particle_system_type (128 bytes) */
	{ 0x0020, 4, 1 },
	{ 0x0024, 2, 4 },
	{ 0x002C, 4, 1 },
	{ 0x0054, 2, 2 },
	{ 0x0058, 4, 10 },
	/* particle_system_type_state (192 bytes) */
	{ 0x0020, 4, 36 },
	{ 0x00B0, 2, 2 },
	{ 0x00B4, 4, 3 },
	/* pathfinding_sphere (32 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 7 },
	/* physics (128 bytes) */
	{ 0x0000, 4, 32 },
	/* placeholder_definition (380 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 65 },
	{ 0x0108, 2, 4 },
	{ 0x0110, 4, 11 },
	{ 0x013C, 2, 2 },
	{ 0x0140, 4, 15 },
	/* platoon (172 bytes) */
	{ 0x0020, 4, 4 },
	{ 0x0030, 2, 2 },
	{ 0x0034, 4, 2 },
	{ 0x003C, 2, 2 },
	{ 0x0040, 4, 1 },
	/* platoon_rule (8 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 1 },
	/* player_starting_location (52 bytes) */
	{ 0x0000, 4, 4 },
	{ 0x0010, 2, 6 },
	/* point_physics (64 bytes) */
	{ 0x0000, 4, 16 },
	/* powered_mass_point (128 bytes) */
	{ 0x0020, 4, 24 },
	/* predicted_resource (8 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 1 },
	/* projectile (208 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 6 },
	{ 0x0010, 4, 25 },
	{ 0x0074, 2, 2 },
	{ 0x0078, 4, 22 },
	/* projectile_definition (588 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 65 },
	{ 0x0108, 2, 4 },
	{ 0x0110, 4, 11 },
	{ 0x013C, 2, 2 },
	{ 0x0140, 4, 16 },
	{ 0x0180, 2, 6 },
	{ 0x018C, 4, 25 },
	{ 0x01F0, 2, 2 },
	{ 0x01F4, 4, 22 },
	/* projectile_material_response (160 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 4 },
	{ 0x0024, 2, 2 },
	{ 0x0028, 4, 9 },
	{ 0x005C, 2, 2 },
	{ 0x0060, 4, 6 },
	{ 0x0090, 4, 4 },
	/* real_plane3d (16 bytes) */
	{ 0x0000, 4, 4 },
	/* real_point2d (8 bytes) */
	{ 0x0000, 4, 2 },
	/* real_point3d (12 bytes) */
	{ 0x0000, 4, 3 },
	/* recorded_animation (64 bytes) */
	{ 0x0024, 2, 2 },
	{ 0x0028, 4, 6 },
	/* render_lighting (116 bytes) */
	{ 0x0000, 4, 3 },
	{ 0x000C, 2, 2 },
	{ 0x0010, 4, 12 },
	{ 0x0040, 2, 2 },
	{ 0x0044, 4, 12 },
	/* scenario (1456 bytes) */
	{ 0x0000, 4, 15 },
	{ 0x003C, 2, 2 },
	{ 0x0040, 4, 348 },
	/* scenery (72 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 6 },
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 5 },
	{ 0x0040, 4, 2 },
	/* scenery_definition (380 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 65 },
	{ 0x0108, 2, 4 },
	{ 0x0110, 4, 11 },
	{ 0x013C, 2, 2 },
	{ 0x0140, 4, 15 },
	/* scenery_palette (48 bytes) */
	{ 0x0000, 4, 12 },
	/* shader (40 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 7 },
	{ 0x0020, 2, 4 },
	/* shader_definition (40 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 7 },
	{ 0x0020, 2, 4 },
	/* shader_effect (180 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 7 },
	{ 0x0020, 2, 8 },
	{ 0x004C, 4, 4 },
	{ 0x005C, 2, 4 },
	{ 0x0064, 4, 3 },
	{ 0x0070, 2, 2 },
	{ 0x0074, 4, 3 },
	{ 0x0080, 2, 2 },
	{ 0x0084, 4, 7 },
	/* shader_environment (836 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 7 },
	{ 0x0020, 2, 6 },
	{ 0x002C, 4, 16 },
	{ 0x006C, 2, 2 },
	{ 0x0088, 4, 4 },
	{ 0x00B0, 2, 2 },
	{ 0x00B4, 4, 10 },
	{ 0x00F4, 2, 2 },
	{ 0x00F8, 4, 8 },
	{ 0x0124, 4, 7 },
	{ 0x0150, 2, 2 },
	{ 0x0154, 4, 2 },
	{ 0x015C, 2, 2 },
	{ 0x0160, 4, 2 },
	{ 0x0180, 2, 2 },
	{ 0x019C, 4, 6 },
	{ 0x01B4, 2, 2 },
	{ 0x01B8, 4, 2 },
	{ 0x01D8, 4, 6 },
	{ 0x01F0, 2, 2 },
	{ 0x01F4, 4, 2 },
	{ 0x0214, 4, 6 },
	{ 0x022C, 2, 2 },
	{ 0x0230, 4, 2 },
	{ 0x0250, 4, 5 },
	{ 0x027C, 2, 2 },
	{ 0x0280, 4, 20 },
	{ 0x02D0, 2, 2 },
	{ 0x02D4, 4, 28 },
	/* shader_layer (16 bytes) */
	{ 0x0000, 4, 4 },
	/* shader_model (440 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 7 },
	{ 0x0020, 2, 6 },
	{ 0x0038, 4, 1 },
	{ 0x004C, 2, 1 },
	{ 0x006C, 2, 4 },
	{ 0x0074, 4, 7 },
	{ 0x009C, 4, 6 },
	{ 0x00BC, 4, 4 },
	{ 0x00D4, 2, 2 },
	{ 0x00D8, 4, 6 },
	{ 0x00FC, 2, 2 },
	{ 0x0100, 4, 3 },
	{ 0x010C, 2, 2 },
	{ 0x0110, 4, 3 },
	{ 0x011C, 2, 2 },
	{ 0x0120, 4, 5 },
	{ 0x013C, 4, 14 },
	/* shader_texture_animation (56 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 3 },
	{ 0x0010, 2, 2 },
	{ 0x0014, 4, 3 },
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 5 },
	/* shader_transparent (56 bytes) */
	{ 0x0002, 2, 5 },
	{ 0x000C, 4, 11 },
	/* shader_transparent_chicago (108 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 7 },
	{ 0x0020, 2, 4 },
	{ 0x002A, 2, 5 },
	{ 0x0034, 4, 12 },
	/* shader_transparent_chicago_extended (120 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 7 },
	{ 0x0020, 2, 4 },
	{ 0x002A, 2, 5 },
	{ 0x0034, 4, 15 },
	/* shader_transparent_chicago_map (220 bytes) */
	{ 0x0000, 2, 1 },
	{ 0x002C, 2, 2 },
	{ 0x0054, 4, 10 },
	{ 0x00A4, 2, 2 },
	{ 0x00A8, 4, 3 },
	{ 0x00B4, 2, 2 },
	{ 0x00B8, 4, 3 },
	{ 0x00C4, 2, 2 },
	{ 0x00C8, 4, 5 },
	/* shader_transparent_generic (108 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 7 },
	{ 0x0020, 2, 4 },
	{ 0x002A, 2, 5 },
	{ 0x0034, 4, 14 },
	/* shader_transparent_generic_map (100 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 10 },
	{ 0x002C, 2, 2 },
	{ 0x0030, 4, 3 },
	{ 0x003C, 2, 2 },
	{ 0x0040, 4, 3 },
	{ 0x004C, 2, 2 },
	{ 0x0050, 4, 5 },
	/* shader_transparent_generic_stage (112 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 13 },
	{ 0x003C, 2, 26 },
	/* shader_transparent_glass (480 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 7 },
	{ 0x0020, 2, 6 },
	{ 0x0054, 4, 8 },
	{ 0x0088, 2, 2 },
	{ 0x008C, 4, 17 },
	{ 0x0150, 2, 2 },
	{ 0x0154, 4, 10 },
	/* shader_transparent_meter (260 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 7 },
	{ 0x0020, 2, 6 },
	{ 0x004C, 4, 4 },
	{ 0x007C, 4, 17 },
	{ 0x00D8, 2, 6 },
	/* shader_transparent_plasma (332 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 7 },
	{ 0x0020, 2, 4 },
	{ 0x002C, 2, 2 },
	{ 0x0030, 4, 1 },
	{ 0x0034, 2, 2 },
	{ 0x0038, 4, 2 },
	{ 0x0060, 4, 8 },
	{ 0x0080, 2, 1 },
	{ 0x00C0, 4, 9 },
	{ 0x0108, 4, 9 },
	/* shader_transparent_water (320 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 7 },
	{ 0x0020, 2, 6 },
	{ 0x004C, 4, 4 },
	{ 0x006C, 4, 8 },
	{ 0x009C, 4, 4 },
	{ 0x00BC, 4, 7 },
	{ 0x00D8, 2, 2 },
	{ 0x00DC, 4, 2 },
	{ 0x0124, 4, 3 },
	/* sky (208 bytes) */
	{ 0x0000, 4, 8 },
	{ 0x0058, 4, 3 },
	{ 0x006C, 4, 6 },
	{ 0x008C, 4, 17 },
	/* sky_animation (36 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 1 },
	/* sky_light (116 bytes) */
	{ 0x0000, 4, 4 },
	{ 0x0068, 4, 2 },
	/* sky_reference (16 bytes) */
	{ 0x0000, 4, 4 },
	/* sky_shader_function (36 bytes) */
	{ 0x0000, 4, 1 },
	/* sound (164 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 2 },
	{ 0x0008, 4, 10 },
	{ 0x003C, 4, 3 },
	{ 0x0054, 4, 3 },
	{ 0x006C, 2, 2 },
	{ 0x0070, 4, 4 },
	{ 0x0080, 2, 2 },
	{ 0x0084, 4, 8 },
	/* sound_environment (72 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 2 },
	{ 0x0008, 4, 12 },
	/* sound_permutation (124 bytes) */
	{ 0x0020, 4, 2 },
	{ 0x0028, 2, 2 },
	{ 0x002C, 4, 20 },
	/* sound_pitch_range (72 bytes) */
	{ 0x0020, 4, 3 },
	{ 0x002C, 2, 2 },
	{ 0x0030, 4, 2 },
	{ 0x0038, 2, 2 },
	{ 0x003C, 4, 3 },
	/* sound_scenery (40 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 6 },
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 1 },
	/* sound_scenery_definition (380 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 65 },
	{ 0x0108, 2, 4 },
	{ 0x0110, 4, 11 },
	{ 0x013C, 2, 2 },
	{ 0x0140, 4, 15 },
	/* sound_scenery_palette (48 bytes) */
	{ 0x0000, 4, 12 },
	/* squad (232 bytes) */
	{ 0x0020, 2, 4 },
	{ 0x0028, 4, 1 },
	{ 0x002C, 2, 2 },
	{ 0x0030, 4, 7 },
	{ 0x004C, 2, 2 },
	{ 0x0050, 4, 11 },
	{ 0x007C, 2, 8 },
	{ 0x008C, 4, 23 },
	/* starting_equipment (204 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 4 },
	{ 0x000C, 4, 48 },
	/* starting_profile (104 bytes) */
	{ 0x0020, 4, 6 },
	{ 0x0038, 2, 2 },
	{ 0x003C, 4, 4 },
	{ 0x004C, 2, 2 },
	/* static_hud_element (104 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 2 },
	{ 0x000C, 2, 2 },
	{ 0x0010, 4, 13 },
	{ 0x0044, 2, 2 },
	{ 0x0048, 4, 2 },
	{ 0x0050, 2, 4 },
	{ 0x0058, 4, 4 },
	/* string_list (12 bytes) */
	{ 0x0000, 4, 3 },
	/* string_list_entry (20 bytes) */
	{ 0x0000, 4, 5 },
	/* structure_background_sound_palette_entry (116 bytes) */
	{ 0x0020, 4, 4 },
	/* structure_breakable_surface (48 bytes) */
	{ 0x0000, 4, 12 },
	/* structure_bsp (648 bytes) */
	{ 0x0000, 4, 14 },
	{ 0x0038, 2, 2 },
	{ 0x003C, 4, 12 },
	{ 0x006C, 2, 2 },
	{ 0x0070, 4, 134 },
	/* structure_bsp_reference (32 bytes) */
	{ 0x0000, 4, 3 },
	{ 0x0010, 4, 4 },
	/* structure_cluster (104 bytes) */
	{ 0x0000, 2, 8 },
	{ 0x0010, 4, 12 },
	{ 0x0040, 2, 2 },
	{ 0x0044, 4, 9 },
	/* structure_cluster_portal (64 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 15 },
	/* structure_collision_material (20 bytes) */
	{ 0x0000, 4, 4 },
	{ 0x0010, 2, 2 },
	/* structure_detail_object_data (64 bytes) */
	{ 0x0000, 4, 12 },
	{ 0x0034, 4, 3 },
	/* structure_fog_palette_entry (136 bytes) */
	{ 0x0020, 4, 4 },
	/* structure_fog_plane (32 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 7 },
	/* structure_fog_region (40 bytes) */
	{ 0x0024, 2, 2 },
	/* structure_leaf (16 bytes) */
	{ 0x0006, 2, 3 },
	{ 0x000C, 4, 1 },
	/* structure_lens_flare (16 bytes) */
	{ 0x0000, 4, 4 },
	/* structure_lens_flare_marker (16 bytes) */
	{ 0x0000, 4, 3 },
	/* structure_lightmap (32 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 7 },
	/* structure_material (256 bytes) */
	{ 0x0000, 4, 4 },
	{ 0x0010, 2, 2 },
	{ 0x0014, 4, 8 },
	{ 0x0034, 2, 2 },
	{ 0x0038, 4, 12 },
	{ 0x0068, 2, 2 },
	{ 0x006C, 4, 16 },
	{ 0x00AC, 2, 4 },
	{ 0x00B4, 4, 4 },
	{ 0x00C4, 2, 2 },
	{ 0x00C8, 4, 14 },
	/* structure_mirror (64 bytes) */
	{ 0x0000, 4, 16 },
	/* structure_portal_index (2 bytes) */
	{ 0x0000, 2, 1 },
	/* structure_runtime_decal (16 bytes) */
	{ 0x0000, 4, 3 },
	/* structure_sound_environment_palette_entry (80 bytes) */
	{ 0x0020, 4, 4 },
	/* structure_subcluster (36 bytes) */
	{ 0x0000, 4, 9 },
	/* structure_surface (6 bytes) */
	{ 0x0000, 2, 3 },
	/* structure_surface_reference (8 bytes) */
	{ 0x0000, 4, 2 },
	/* structure_weather_palette_entry (240 bytes) */
	{ 0x0020, 4, 4 },
	{ 0x0030, 2, 2 },
	{ 0x0054, 4, 19 },
	{ 0x00A0, 2, 2 },
	{ 0x00C4, 4, 11 },
	/* structure_weather_polyhedron (32 bytes) */
	{ 0x0000, 4, 8 },
	/* tag_reference_camera_track (16 bytes) */
	{ 0x0000, 4, 4 },
	/* tag_reference_item (16 bytes) */
	{ 0x0000, 4, 4 },
	/* tag_reference_object (16 bytes) */
	{ 0x0000, 4, 4 },
	/* tag_reference_sound (16 bytes) */
	{ 0x0000, 4, 4 },
	/* trigger_volume (96 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0030, 4, 12 },
	/* ui_widget (1004 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0024, 2, 4 },
	{ 0x002C, 4, 16 },
	{ 0x00EC, 4, 12 },
	{ 0x011C, 2, 2 },
	{ 0x012E, 2, 3 },
	{ 0x0150, 4, 9 },
	{ 0x0174, 2, 8 },
	{ 0x01A4, 4, 4 },
	{ 0x02D4, 4, 3 },
	{ 0x03E0, 4, 3 },
	/* ui_widget_child (80 bytes) */
	{ 0x0000, 4, 4 },
	{ 0x0030, 4, 1 },
	{ 0x0034, 2, 3 },
	/* ui_widget_collection (12 bytes) */
	{ 0x0000, 4, 3 },
	/* ui_widget_collection_widget (16 bytes) */
	{ 0x0000, 4, 4 },
	/* ui_widget_conditional (80 bytes) */
	{ 0x0000, 4, 4 },
	{ 0x0030, 4, 1 },
	{ 0x0034, 2, 1 },
	/* ui_widget_event_handler (72 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 2 },
	{ 0x0008, 4, 8 },
	/* ui_widget_game_data_input (36 bytes) */
	{ 0x0000, 2, 1 },
	/* ui_widget_search_and_replace (34 bytes) */
	{ 0x0020, 2, 1 },
	/* unicode_string_list (12 bytes) */
	{ 0x0000, 4, 3 },
	/* unicode_string_list_entry (20 bytes) */
	{ 0x0000, 4, 5 },
	/* unit (372 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 2 },
	{ 0x0008, 4, 5 },
	{ 0x001C, 2, 4 },
	{ 0x0024, 4, 2 },
	{ 0x006C, 4, 29 },
	{ 0x00E0, 2, 2 },
	{ 0x00E4, 4, 14 },
	{ 0x011C, 2, 2 },
	{ 0x0120, 4, 10 },
	{ 0x0148, 2, 4 },
	{ 0x0150, 4, 9 },
	/* unit_camera (88 bytes) */
	{ 0x0040, 4, 6 },
	/* unit_camera_track (28 bytes) */
	{ 0x0000, 4, 7 },
	/* unit_definition (752 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 65 },
	{ 0x0108, 2, 4 },
	{ 0x0110, 4, 11 },
	{ 0x013C, 2, 2 },
	{ 0x0140, 4, 16 },
	{ 0x0180, 2, 2 },
	{ 0x0184, 4, 5 },
	{ 0x0198, 2, 4 },
	{ 0x01A0, 4, 2 },
	{ 0x01E8, 4, 29 },
	{ 0x025C, 2, 2 },
	{ 0x0260, 4, 14 },
	{ 0x0298, 2, 2 },
	{ 0x029C, 4, 10 },
	{ 0x02C4, 2, 4 },
	{ 0x02CC, 4, 9 },
	/* unit_dialogue_variant (24 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 5 },
	/* unit_hud (48 bytes) */
	{ 0x0000, 4, 12 },
	/* unit_hud_interface (1388 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 8 },
	{ 0x0024, 2, 2 },
	{ 0x0028, 4, 2 },
	{ 0x0030, 2, 2 },
	{ 0x0034, 4, 13 },
	{ 0x0068, 2, 2 },
	{ 0x006C, 4, 2 },
	{ 0x0074, 2, 4 },
	{ 0x007C, 4, 4 },
	{ 0x008C, 2, 2 },
	{ 0x0090, 4, 2 },
	{ 0x0098, 2, 2 },
	{ 0x009C, 4, 13 },
	{ 0x00D0, 2, 2 },
	{ 0x00D4, 4, 2 },
	{ 0x00DC, 2, 4 },
	{ 0x00E4, 4, 4 },
	{ 0x00F4, 2, 2 },
	{ 0x00F8, 4, 2 },
	{ 0x0100, 2, 2 },
	{ 0x0104, 4, 13 },
	{ 0x013A, 2, 1 },
	{ 0x013E, 2, 1 },
	{ 0x0140, 4, 15 },
	{ 0x017C, 2, 2 },
	{ 0x0180, 4, 2 },
	{ 0x0188, 2, 2 },
	{ 0x018C, 4, 13 },
	{ 0x01C0, 2, 2 },
	{ 0x01C4, 4, 2 },
	{ 0x01CC, 2, 4 },
	{ 0x01D4, 4, 4 },
	{ 0x01E4, 2, 2 },
	{ 0x01E8, 4, 2 },
	{ 0x01F0, 2, 2 },
	{ 0x01F4, 4, 13 },
	{ 0x022A, 2, 1 },
	{ 0x022E, 2, 1 },
	{ 0x0230, 4, 15 },
	{ 0x026C, 2, 2 },
	{ 0x0270, 4, 2 },
	{ 0x0278, 2, 2 },
	{ 0x027C, 4, 13 },
	{ 0x02B0, 2, 2 },
	{ 0x02B4, 4, 2 },
	{ 0x02BC, 2, 4 },
	{ 0x02C4, 4, 4 },
	{ 0x02D4, 2, 2 },
	{ 0x02D8, 4, 2 },
	{ 0x02E0, 2, 2 },
	{ 0x02E4, 4, 13 },
	{ 0x0318, 2, 2 },
	{ 0x031C, 4, 2 },
	{ 0x0324, 2, 4 },
	{ 0x032C, 4, 12 },
	{ 0x035C, 2, 2 },
	{ 0x0360, 4, 2 },
	{ 0x0368, 2, 2 },
	{ 0x036C, 4, 5 },
	{ 0x0380, 2, 2 },
	{ 0x0384, 4, 122 },
	/* unit_initial_weapon (36 bytes) */
	{ 0x0000, 4, 9 },
	/* unit_powered_seat (68 bytes) */
	{ 0x0000, 4, 17 },
	/* unit_seat (284 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0044, 4, 16 },
	{ 0x00C4, 4, 10 },
	{ 0x00EC, 2, 2 },
	{ 0x00F0, 4, 11 },
	/* vehicle (120 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 6 },
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 5 },
	{ 0x0040, 4, 4 },
	/* vehicle_animation (116 bytes) */
	{ 0x0000, 4, 2 },
	{ 0x0008, 2, 2 },
	{ 0x000C, 4, 2 },
	{ 0x0014, 2, 2 },
	{ 0x0018, 4, 23 },
	/* vehicle_definition (1008 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 65 },
	{ 0x0108, 2, 4 },
	{ 0x0110, 4, 11 },
	{ 0x013C, 2, 2 },
	{ 0x0140, 4, 16 },
	{ 0x0180, 2, 2 },
	{ 0x0184, 4, 5 },
	{ 0x0198, 2, 4 },
	{ 0x01A0, 4, 2 },
	{ 0x01E8, 4, 29 },
	{ 0x025C, 2, 2 },
	{ 0x0260, 4, 14 },
	{ 0x0298, 2, 2 },
	{ 0x029C, 4, 10 },
	{ 0x02C4, 2, 4 },
	{ 0x02CC, 4, 10 },
	{ 0x02F4, 2, 2 },
	{ 0x02F8, 4, 9 },
	{ 0x031C, 2, 4 },
	{ 0x0330, 4, 2 },
	{ 0x0340, 4, 2 },
	{ 0x0364, 4, 1 },
	{ 0x0380, 4, 4 },
	{ 0x0398, 4, 4 },
	{ 0x03B0, 4, 16 },
	/* vehicle_palette (48 bytes) */
	{ 0x0000, 4, 12 },
	/* vehicle_suspension (20 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 2 },
	/* vibrate_frequency (20 bytes) */
	{ 0x0000, 4, 2 },
	{ 0x0008, 2, 2 },
	{ 0x000C, 4, 2 },
	/* virtual_keyboard (60 bytes) */
	{ 0x0000, 4, 15 },
	/* virtual_keyboard_key (80 bytes) */
	{ 0x0000, 2, 8 },
	{ 0x0010, 4, 16 },
	/* water_ripple (76 bytes) */
	{ 0x0004, 4, 1 },
	{ 0x0028, 4, 4 },
	{ 0x0038, 2, 2 },
	/* weapon (92 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 6 },
	{ 0x0020, 2, 2 },
	{ 0x0024, 4, 5 },
	{ 0x0040, 4, 2 },
	{ 0x0048, 2, 4 },
	{ 0x0050, 4, 3 },
	/* weapon (512 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0024, 2, 6 },
	{ 0x0030, 4, 40 },
	{ 0x00D0, 2, 2 },
	{ 0x00D4, 4, 8 },
	{ 0x00F4, 2, 2 },
	{ 0x00F8, 4, 56 },
	{ 0x01D8, 2, 2 },
	{ 0x01DC, 4, 9 },
	/* weapon_ammunition_object (28 bytes) */
	{ 0x0000, 2, 6 },
	{ 0x000C, 4, 4 },
	/* weapon_definition (1288 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 65 },
	{ 0x0108, 2, 4 },
	{ 0x0110, 4, 11 },
	{ 0x013C, 2, 2 },
	{ 0x0140, 4, 16 },
	{ 0x0180, 2, 2 },
	{ 0x0184, 4, 1 },
	{ 0x0188, 2, 2 },
	{ 0x018C, 4, 4 },
	{ 0x019C, 2, 4 },
	{ 0x01A4, 4, 90 },
	{ 0x032C, 2, 6 },
	{ 0x0338, 4, 40 },
	{ 0x03D8, 2, 2 },
	{ 0x03DC, 4, 8 },
	{ 0x03FC, 2, 2 },
	{ 0x0400, 4, 56 },
	{ 0x04E0, 2, 2 },
	{ 0x04E4, 4, 9 },
	/* weapon_firing_effect (132 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 32 },
	/* weapon_hud_crosshair_item (108 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 2 },
	{ 0x000C, 2, 2 },
	{ 0x0010, 4, 9 },
	{ 0x0034, 2, 2 },
	{ 0x0038, 4, 2 },
	{ 0x0040, 2, 4 },
	{ 0x0048, 4, 9 },
	/* weapon_hud_crosshairs_element (104 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 24 },
	/* weapon_hud_element_header (36 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 7 },
	/* weapon_hud_interface (380 bytes) */
	{ 0x0000, 4, 4 },
	{ 0x0010, 2, 6 },
	{ 0x001C, 4, 8 },
	{ 0x003C, 2, 2 },
	{ 0x0040, 4, 63 },
	{ 0x013C, 2, 4 },
	{ 0x0144, 4, 1 },
	{ 0x014A, 2, 1 },
	{ 0x014C, 4, 12 },
	/* weapon_hud_meter_element (180 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 7 },
	{ 0x0024, 2, 2 },
	{ 0x0028, 4, 2 },
	{ 0x0030, 2, 2 },
	{ 0x0034, 4, 13 },
	{ 0x006A, 2, 1 },
	{ 0x006E, 2, 1 },
	{ 0x0070, 4, 17 },
	/* weapon_hud_number_element (160 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 7 },
	{ 0x0024, 2, 2 },
	{ 0x0028, 4, 2 },
	{ 0x0030, 2, 2 },
	{ 0x0034, 4, 9 },
	{ 0x0058, 2, 2 },
	{ 0x005C, 4, 2 },
	{ 0x0064, 2, 2 },
	{ 0x006C, 4, 3 },
	{ 0x0078, 2, 2 },
	{ 0x007C, 4, 9 },
	/* weapon_hud_overlay (28 bytes) */
	{ 0x0000, 4, 7 },
	/* weapon_hud_overlay_item (136 bytes) */
	{ 0x0000, 2, 2 },
	{ 0x0004, 4, 2 },
	{ 0x000C, 2, 2 },
	{ 0x0010, 4, 9 },
	{ 0x0034, 2, 2 },
	{ 0x0038, 4, 2 },
	{ 0x0040, 2, 6 },
	{ 0x004C, 4, 15 },
	/* weapon_hud_overlays_element (104 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 24 },
	/* weapon_hud_static_element (180 bytes) */
	{ 0x0000, 2, 4 },
	{ 0x0008, 4, 7 },
	{ 0x0024, 2, 2 },
	{ 0x0028, 4, 2 },
	{ 0x0030, 2, 2 },
	{ 0x0034, 4, 13 },
	{ 0x0068, 2, 2 },
	{ 0x006C, 4, 2 },
	{ 0x0074, 2, 4 },
	{ 0x007C, 4, 14 },
	/* weapon_magazine (112 bytes) */
	{ 0x0000, 4, 1 },
	{ 0x0004, 2, 8 },
	{ 0x0014, 4, 1 },
	{ 0x0018, 2, 2 },
	{ 0x001C, 4, 3 },
	{ 0x0028, 2, 8 },
	{ 0x0038, 4, 14 },
	/* weapon_palette (48 bytes) */
	{ 0x0000, 4, 12 },
	/* weapon_trigger (276 bytes) */
	{ 0x0000, 4, 8 },
	{ 0x0020, 2, 8 },
	{ 0x0030, 4, 8 },
	{ 0x0050, 2, 2 },
	{ 0x0054, 4, 6 },
	{ 0x006C, 2, 2 },
	{ 0x0070, 4, 41 },
	/* weather_particle_system (48 bytes) */
	{ 0x0000, 4, 12 },
	/* weather_particle_type (604 bytes) */
	{ 0x0020, 4, 97 },
	{ 0x01A4, 2, 4 },
	{ 0x01AC, 4, 7 },
	{ 0x01C8, 2, 8 },
	{ 0x01F4, 4, 4 },
	{ 0x0204, 2, 4 },
	{ 0x020C, 4, 3 },
	{ 0x0218, 2, 2 },
	{ 0x021C, 4, 3 },
	{ 0x0228, 2, 2 },
	{ 0x022C, 4, 7 },
	/* wind (64 bytes) */
	{ 0x0000, 4, 16 },
};

/* each definition's block, data and structure fields as the schemas have them:
kind, offset, count, stride (the game's schema must be the same) */
static struct tag_swap_field const tag_swap_fields[] =
{
	{ _tag_swap_field_block, 0x022C, 1, 12 }, /* actor_variant_definition.change_colors */
	{ _tag_swap_field_block, 0x0030, 1, 12 }, /* ai_command_list.commands */
	{ _tag_swap_field_block, 0x003C, 1, 12 }, /* ai_command_list.points */
	{ _tag_swap_field_data, 0x0048, 1, 20 }, /* animation.frame_info */
	{ _tag_swap_field_data, 0x008C, 1, 20 }, /* animation.default_data */
	{ _tag_swap_field_data, 0x00A0, 1, 20 }, /* animation.data */
	{ _tag_swap_field_block, 0x0000, 1, 12 }, /* animation_graph.object_overlays */
	{ _tag_swap_field_block, 0x000C, 1, 12 }, /* animation_graph.unit_seats */
	{ _tag_swap_field_block, 0x0018, 1, 12 }, /* animation_graph.weapon_animations */
	{ _tag_swap_field_block, 0x0024, 1, 12 }, /* animation_graph.vehicle_animations */
	{ _tag_swap_field_block, 0x0030, 1, 12 }, /* animation_graph.device_animations */
	{ _tag_swap_field_block, 0x003C, 1, 12 }, /* animation_graph.unit_damage_animations */
	{ _tag_swap_field_block, 0x0048, 1, 12 }, /* animation_graph.first_person_weapon_animations */
	{ _tag_swap_field_block, 0x0054, 1, 12 }, /* animation_graph.sound_references */
	{ _tag_swap_field_block, 0x0068, 1, 12 }, /* animation_graph.nodes */
	{ _tag_swap_field_block, 0x0074, 1, 12 }, /* animation_graph.animations */
	{ _tag_swap_field_block, 0x0054, 1, 12 }, /* animation_graph_device_animations.animations */
	{ _tag_swap_field_block, 0x0010, 1, 12 }, /* animation_graph_first_person_weapon_animations.animations */
	{ _tag_swap_field_block, 0x0040, 1, 12 }, /* animation_graph_unit_seat.animations */
	{ _tag_swap_field_block, 0x004C, 1, 12 }, /* animation_graph_unit_seat.ik_points */
	{ _tag_swap_field_block, 0x0058, 1, 12 }, /* animation_graph_unit_seat.weapon_classes */
	{ _tag_swap_field_block, 0x0010, 1, 12 }, /* animation_graph_weapon_animations.animations */
	{ _tag_swap_field_block, 0x0098, 1, 12 }, /* animation_graph_weapon_class.animations */
	{ _tag_swap_field_block, 0x00A4, 1, 12 }, /* animation_graph_weapon_class.ik_points */
	{ _tag_swap_field_block, 0x00B0, 1, 12 }, /* animation_graph_weapon_class.weapon_types */
	{ _tag_swap_field_block, 0x0030, 1, 12 }, /* animation_graph_weapon_type.animations */
	{ _tag_swap_field_block, 0x00C4, 1, 12 }, /* antenna.vertices */
	{ _tag_swap_field_struct, 0x0014, 1, 240 }, /* auxilary_meter.panel */
	{ _tag_swap_field_struct, 0x0000, 1, 104 }, /* auxilary_overlay.static_element */
	{ _tag_swap_field_block, 0x01F8, 1, 12 }, /* biped.contact_points */
	{ _tag_swap_field_struct, 0x0000, 1, 380 }, /* biped_definition.object */
	{ _tag_swap_field_struct, 0x017C, 1, 372 }, /* biped_definition.unit */
	{ _tag_swap_field_struct, 0x02F0, 1, 516 }, /* biped_definition.biped */
	{ _tag_swap_field_block, 0x0054, 1, 12 }, /* bitmap_group.sequences */
	{ _tag_swap_field_block, 0x0060, 1, 12 }, /* bitmap_group.bitmaps */
	{ _tag_swap_field_block, 0x0034, 1, 12 }, /* bitmap_sequence.sprites */
	{ _tag_swap_field_block, 0x0004, 1, 12 }, /* camera_track.control_points */
	{ _tag_swap_field_block, 0x000C, 1, 12 }, /* collision_bsp.bsp3d.planes */
	{ _tag_swap_field_block, 0x0000, 1, 12 }, /* collision_bsp.bsp3d.nodes */
	{ _tag_swap_field_block, 0x0018, 1, 12 }, /* collision_bsp.leaves */
	{ _tag_swap_field_block, 0x0024, 1, 12 }, /* collision_bsp.bsp2d_references */
	{ _tag_swap_field_block, 0x0030, 1, 12 }, /* collision_bsp.bsp2d.nodes */
	{ _tag_swap_field_block, 0x0054, 1, 12 }, /* collision_bsp.vertices */
	{ _tag_swap_field_block, 0x0048, 1, 12 }, /* collision_bsp.edges */
	{ _tag_swap_field_block, 0x003C, 1, 12 }, /* collision_bsp.surfaces */
	{ _tag_swap_field_struct, 0x0000, 1, 600 }, /* collision_model.resistance */
	{ _tag_swap_field_block, 0x0280, 1, 12 }, /* collision_model.pathfinding_spheres */
	{ _tag_swap_field_block, 0x028C, 1, 12 }, /* collision_model.nodes */
	{ _tag_swap_field_block, 0x0034, 1, 12 }, /* collision_node.bsps */
	{ _tag_swap_field_block, 0x0000, 1, 12 }, /* color_table.colors */
	{ _tag_swap_field_struct, 0x0124, 1, 220 }, /* continuous_damage_effect.damage */
	{ _tag_swap_field_struct, 0x0084, 1, 180 }, /* contrail.shader */
	{ _tag_swap_field_block, 0x0138, 1, 12 }, /* contrail.states */
	{ _tag_swap_field_struct, 0x0000, 1, 380 }, /* control_definition.object */
	{ _tag_swap_field_struct, 0x017C, 1, 276 }, /* control_definition.device */
	{ _tag_swap_field_struct, 0x0290, 1, 148 }, /* control_definition.control */
	{ _tag_swap_field_block, 0x0050, 1, 12 }, /* conversation.participants */
	{ _tag_swap_field_block, 0x005C, 1, 12 }, /* conversation.lines */
	{ _tag_swap_field_struct, 0x005C, 2, 20 }, /* damage_effect.vibrate.vibrate_frequencies */
	{ _tag_swap_field_struct, 0x01C4, 1, 220 }, /* damage_effect.damage */
	{ _tag_swap_field_block, 0x0048, 1, 12 }, /* damage_region.permutations */
	{ _tag_swap_field_block, 0x0234, 1, 12 }, /* damage_resistance.materials */
	{ _tag_swap_field_block, 0x0240, 1, 12 }, /* damage_resistance.regions */
	{ _tag_swap_field_block, 0x0044, 1, 12 }, /* detail_object_collection.type_definitions */
	{ _tag_swap_field_struct, 0x0000, 1, 380 }, /* device_definition.object */
	{ _tag_swap_field_struct, 0x017C, 1, 276 }, /* device_definition.device */
	{ _tag_swap_field_block, 0x0028, 1, 12 }, /* effect.locations */
	{ _tag_swap_field_block, 0x0034, 1, 12 }, /* effect.events */
	{ _tag_swap_field_block, 0x002C, 1, 12 }, /* effect_event.parts */
	{ _tag_swap_field_block, 0x0038, 1, 12 }, /* effect_event.particles */
	{ _tag_swap_field_block, 0x0080, 1, 12 }, /* encounter.squads */
	{ _tag_swap_field_block, 0x008C, 1, 12 }, /* encounter.platoons */
	{ _tag_swap_field_block, 0x0098, 1, 12 }, /* encounter.firing_positions */
	{ _tag_swap_field_block, 0x00A4, 1, 12 }, /* encounter.player_starting_locations */
	{ _tag_swap_field_struct, 0x0000, 1, 380 }, /* equipment_definition.object */
	{ _tag_swap_field_struct, 0x017C, 1, 396 }, /* equipment_definition.item */
	{ _tag_swap_field_struct, 0x0308, 1, 24 }, /* equipment_definition.equipment */
	{ _tag_swap_field_block, 0x0054, 1, 12 }, /* flag.attachment_points */
	{ _tag_swap_field_block, 0x0030, 1, 12 }, /* font.character_tables */
	{ _tag_swap_field_block, 0x007C, 1, 12 }, /* font.characters */
	{ _tag_swap_field_data, 0x0088, 1, 20 }, /* font.pixels */
	{ _tag_swap_field_block, 0x0000, 1, 12 }, /* font_character_table.character_indices */
	{ _tag_swap_field_block, 0x00F8, 1, 12 }, /* game_globals.sounds */
	{ _tag_swap_field_block, 0x0104, 1, 12 }, /* game_globals.camera */
	{ _tag_swap_field_block, 0x0110, 1, 12 }, /* game_globals.player_control */
	{ _tag_swap_field_block, 0x011C, 1, 12 }, /* game_globals.difficulty_information */
	{ _tag_swap_field_block, 0x0128, 1, 12 }, /* game_globals.grenades */
	{ _tag_swap_field_block, 0x0134, 1, 12 }, /* game_globals.rasterizer_data */
	{ _tag_swap_field_block, 0x0140, 1, 12 }, /* game_globals.interface_tag_references */
	{ _tag_swap_field_block, 0x014C, 1, 12 }, /* game_globals.weapon_list */
	{ _tag_swap_field_block, 0x0158, 1, 12 }, /* game_globals.cheat_powerups */
	{ _tag_swap_field_block, 0x0164, 1, 12 }, /* game_globals.multiplayer_information */
	{ _tag_swap_field_block, 0x0170, 1, 12 }, /* game_globals.player_information */
	{ _tag_swap_field_block, 0x017C, 1, 12 }, /* game_globals.first_person_interface */
	{ _tag_swap_field_block, 0x0188, 1, 12 }, /* game_globals.falling_damage */
	{ _tag_swap_field_block, 0x0194, 1, 12 }, /* game_globals.materials */
	{ _tag_swap_field_block, 0x0020, 1, 12 }, /* game_globals_multiplayer_information.vehicles */
	{ _tag_swap_field_block, 0x005C, 1, 12 }, /* game_globals_multiplayer_information.sounds */
	{ _tag_swap_field_block, 0x0074, 1, 12 }, /* game_globals_player_control.look_function */
	{ _tag_swap_field_struct, 0x0000, 1, 380 }, /* garbage_definition.object */
	{ _tag_swap_field_struct, 0x017C, 1, 396 }, /* garbage_definition.item */
	{ _tag_swap_field_block, 0x00AC, 1, 12 }, /* gbxmodel.markers */
	{ _tag_swap_field_block, 0x00B8, 1, 12 }, /* gbxmodel.nodes */
	{ _tag_swap_field_block, 0x00C4, 1, 12 }, /* gbxmodel.regions */
	{ _tag_swap_field_block, 0x00D0, 1, 12 }, /* gbxmodel.geometries */
	{ _tag_swap_field_block, 0x00DC, 1, 12 }, /* gbxmodel.shaders */
	{ _tag_swap_field_block, 0x0024, 1, 12 }, /* gbxmodel_geometry.parts */
	{ _tag_swap_field_block, 0x0020, 1, 12 }, /* gbxmodel_geometry_part.uncompressed_vertices */
	{ _tag_swap_field_block, 0x002C, 1, 12 }, /* gbxmodel_geometry_part.compressed_vertices */
	{ _tag_swap_field_block, 0x0038, 1, 12 }, /* gbxmodel_geometry_part.triangles */
	{ _tag_swap_field_struct, 0x0000, 1, 104 }, /* grenade_count_panel.background */
	{ _tag_swap_field_struct, 0x0000, 1, 36 }, /* grenade_hud_interface.absolute_placement */
	{ _tag_swap_field_struct, 0x0024, 1, 104 }, /* grenade_hud_interface.background */
	{ _tag_swap_field_struct, 0x008C, 1, 192 }, /* grenade_hud_interface.grenade_count_panel */
	{ _tag_swap_field_struct, 0x014C, 1, 28 }, /* grenade_hud_interface.overlays */
	{ _tag_swap_field_data, 0x0020, 1, 20 }, /* hs_source_file.source */
	{ _tag_swap_field_struct, 0x0000, 1, 36 }, /* hud_globals.messaging.absolute_placement */
	{ _tag_swap_field_block, 0x00C4, 1, 12 }, /* hud_globals.messaging.button_icons */
	{ _tag_swap_field_block, 0x0160, 1, 12 }, /* hud_globals.waypoint.arrows */
	{ _tag_swap_field_data, 0x0000, 1, 20 }, /* hud_message_text.text_data */
	{ _tag_swap_field_block, 0x0014, 1, 12 }, /* hud_message_text.elements */
	{ _tag_swap_field_block, 0x0020, 1, 12 }, /* hud_message_text.messages */
	{ _tag_swap_field_block, 0x0000, 1, 12 }, /* item_collection_definition.permutations */
	{ _tag_swap_field_struct, 0x0000, 1, 380 }, /* item_definition.object */
	{ _tag_swap_field_struct, 0x017C, 1, 396 }, /* item_definition.item */
	{ _tag_swap_field_block, 0x0004, 1, 12 }, /* leaf_map.leaves */
	{ _tag_swap_field_block, 0x0010, 1, 12 }, /* leaf_map.portals */
	{ _tag_swap_field_block, 0x000C, 1, 12 }, /* leaf_portal.vertices */
	{ _tag_swap_field_block, 0x00C4, 1, 12 }, /* lens_flare.reflections */
	{ _tag_swap_field_struct, 0x0000, 1, 380 }, /* light_fixture_definition.object */
	{ _tag_swap_field_struct, 0x017C, 1, 276 }, /* light_fixture_definition.device */
	{ _tag_swap_field_block, 0x0120, 1, 12 }, /* light_volume.frames */
	{ _tag_swap_field_block, 0x0098, 1, 12 }, /* lightning.markers */
	{ _tag_swap_field_block, 0x00A4, 1, 12 }, /* lightning.shaders */
	{ _tag_swap_field_struct, 0x0000, 1, 180 }, /* lightning_shader.shader */
	{ _tag_swap_field_block, 0x003C, 1, 12 }, /* looping_sound.tracks */
	{ _tag_swap_field_block, 0x0048, 1, 12 }, /* looping_sound.details */
	{ _tag_swap_field_struct, 0x0000, 1, 380 }, /* machine_definition.object */
	{ _tag_swap_field_struct, 0x017C, 1, 276 }, /* machine_definition.device */
	{ _tag_swap_field_struct, 0x0290, 1, 148 }, /* machine_definition.machine */
	{ _tag_swap_field_block, 0x0000, 1, 12 }, /* map_leaf.faces */
	{ _tag_swap_field_block, 0x000C, 1, 12 }, /* map_leaf.portal_designators */
	{ _tag_swap_field_block, 0x0004, 1, 12 }, /* map_leaf_face.vertices */
	{ _tag_swap_field_block, 0x031C, 1, 12 }, /* material.breakable_surface.particle_effects */
	{ _tag_swap_field_block, 0x0000, 1, 12 }, /* material_effect.materials */
	{ _tag_swap_field_block, 0x0000, 1, 12 }, /* material_effects.effects */
	{ _tag_swap_field_block, 0x0058, 1, 12 }, /* meter_hud_element.multitexture_overlays */
	{ _tag_swap_field_struct, 0x0000, 1, 104 }, /* metered_panel.background */
	{ _tag_swap_field_struct, 0x0068, 1, 104 }, /* metered_panel.meter */
	{ _tag_swap_field_block, 0x00AC, 1, 12 }, /* model.markers */
	{ _tag_swap_field_block, 0x00B8, 1, 12 }, /* model.nodes */
	{ _tag_swap_field_block, 0x00C4, 1, 12 }, /* model.regions */
	{ _tag_swap_field_block, 0x00D0, 1, 12 }, /* model.geometries */
	{ _tag_swap_field_block, 0x00DC, 1, 12 }, /* model.shaders */
	{ _tag_swap_field_block, 0x0024, 1, 12 }, /* model_geometry.parts */
	{ _tag_swap_field_block, 0x0020, 1, 12 }, /* model_geometry_part.uncompressed_vertices */
	{ _tag_swap_field_block, 0x002C, 1, 12 }, /* model_geometry_part.compressed_vertices */
	{ _tag_swap_field_block, 0x0038, 1, 12 }, /* model_geometry_part.triangles */
	{ _tag_swap_field_block, 0x0034, 1, 12 }, /* model_marker.instances */
	{ _tag_swap_field_block, 0x0040, 1, 12 }, /* model_region.permutations */
	{ _tag_swap_field_block, 0x004C, 1, 12 }, /* model_region_permutation.markers */
	{ _tag_swap_field_struct, 0x0000, 1, 104 }, /* motion_sensor_panel.background */
	{ _tag_swap_field_struct, 0x0068, 1, 104 }, /* motion_sensor_panel.foreground */
	{ _tag_swap_field_block, 0x0000, 1, 12 }, /* multiplayer_scenario_descriptions.scenarios */
	{ _tag_swap_field_block, 0x0154, 1, 12 }, /* multitexture_overlay.functions */
	{ _tag_swap_field_block, 0x0140, 1, 12 }, /* object.attachments */
	{ _tag_swap_field_block, 0x014C, 1, 12 }, /* object.widgets */
	{ _tag_swap_field_block, 0x0158, 1, 12 }, /* object.functions */
	{ _tag_swap_field_block, 0x0164, 1, 12 }, /* object.change_colors */
	{ _tag_swap_field_block, 0x0170, 1, 12 }, /* object.predicted_resources */
	{ _tag_swap_field_block, 0x0020, 1, 12 }, /* object_change_color.permutations */
	{ _tag_swap_field_struct, 0x0000, 1, 380 }, /* object_definition.object */
	{ _tag_swap_field_struct, 0x0000, 1, 104 }, /* optional_meter_hud_element.element */
	{ _tag_swap_field_struct, 0x0000, 1, 104 }, /* optional_static_hud_element.element */
	{ _tag_swap_field_struct, 0x00B0, 1, 180 }, /* particle.shader */
	{ _tag_swap_field_block, 0x005C, 1, 12 }, /* particle_system.types */
	{ _tag_swap_field_struct, 0x00B8, 1, 180 }, /* particle_system_particle_state.shader */
	{ _tag_swap_field_block, 0x005C, 1, 12 }, /* particle_system_type.physics_constants */
	{ _tag_swap_field_block, 0x0068, 1, 12 }, /* particle_system_type.type_states */
	{ _tag_swap_field_block, 0x0074, 1, 12 }, /* particle_system_type.particle_states */
	{ _tag_swap_field_block, 0x00B4, 1, 12 }, /* particle_system_type_state.physics_constants */
	{ _tag_swap_field_block, 0x005C, 1, 12 }, /* physics.inertial_matrix */
	{ _tag_swap_field_block, 0x0068, 1, 12 }, /* physics.powered_mass_points */
	{ _tag_swap_field_block, 0x0074, 1, 12 }, /* physics.mass_points */
	{ _tag_swap_field_struct, 0x0000, 1, 380 }, /* placeholder_definition.object */
	{ _tag_swap_field_struct, 0x0030, 1, 8 }, /* platoon.attacking_defending_rule */
	{ _tag_swap_field_struct, 0x003C, 1, 8 }, /* platoon.maneuvering_rule */
	{ _tag_swap_field_block, 0x00C4, 1, 12 }, /* projectile.material_responses */
	{ _tag_swap_field_struct, 0x0000, 1, 380 }, /* projectile_definition.object */
	{ _tag_swap_field_struct, 0x017C, 1, 208 }, /* projectile_definition.projectile */
	{ _tag_swap_field_data, 0x002C, 1, 20 }, /* recorded_animation.event_stream */
	{ _tag_swap_field_block, 0x0030, 1, 12 }, /* scenario.sky_references */
	{ _tag_swap_field_block, 0x00EC, 1, 12 }, /* scenario.predicted_ui_resources */
	{ _tag_swap_field_block, 0x0204, 1, 12 }, /* scenario.object_names */
	{ _tag_swap_field_block, 0x0210, 1, 12 }, /* scenario.scenery */
	{ _tag_swap_field_block, 0x021C, 1, 12 }, /* scenario.scenery_palette */
	{ _tag_swap_field_block, 0x0228, 1, 12 }, /* scenario.bipeds */
	{ _tag_swap_field_block, 0x0234, 1, 12 }, /* scenario.biped_palette */
	{ _tag_swap_field_block, 0x0240, 1, 12 }, /* scenario.vehicles */
	{ _tag_swap_field_block, 0x024C, 1, 12 }, /* scenario.vehicle_palette */
	{ _tag_swap_field_block, 0x0258, 1, 12 }, /* scenario.equipment */
	{ _tag_swap_field_block, 0x0264, 1, 12 }, /* scenario.equipment_palette */
	{ _tag_swap_field_block, 0x0270, 1, 12 }, /* scenario.weapons */
	{ _tag_swap_field_block, 0x027C, 1, 12 }, /* scenario.weapon_palette */
	{ _tag_swap_field_block, 0x0288, 1, 12 }, /* scenario.device_groups */
	{ _tag_swap_field_block, 0x0294, 1, 12 }, /* scenario.machines */
	{ _tag_swap_field_block, 0x02A0, 1, 12 }, /* scenario.machine_palette */
	{ _tag_swap_field_block, 0x02AC, 1, 12 }, /* scenario.controls */
	{ _tag_swap_field_block, 0x02B8, 1, 12 }, /* scenario.control_palette */
	{ _tag_swap_field_block, 0x02C4, 1, 12 }, /* scenario.light_fixtures */
	{ _tag_swap_field_block, 0x02D0, 1, 12 }, /* scenario.light_fixtures_palette */
	{ _tag_swap_field_block, 0x02DC, 1, 12 }, /* scenario.sound_scenery */
	{ _tag_swap_field_block, 0x02E8, 1, 12 }, /* scenario.sound_scenery_palette */
	{ _tag_swap_field_block, 0x0348, 1, 12 }, /* scenario.starting_profiles */
	{ _tag_swap_field_block, 0x0354, 1, 12 }, /* scenario.players */
	{ _tag_swap_field_block, 0x0360, 1, 12 }, /* scenario.trigger_volumes */
	{ _tag_swap_field_block, 0x036C, 1, 12 }, /* scenario.recorded_animations */
	{ _tag_swap_field_block, 0x0378, 1, 12 }, /* scenario.netgame_flags */
	{ _tag_swap_field_block, 0x0384, 1, 12 }, /* scenario.netgame_equipment */
	{ _tag_swap_field_block, 0x0390, 1, 12 }, /* scenario.scenario_starting_equipment */
	{ _tag_swap_field_block, 0x039C, 1, 12 }, /* scenario.bsp_switch_trigger_volumes */
	{ _tag_swap_field_block, 0x03B4, 1, 12 }, /* scenario.decal_palette */
	{ _tag_swap_field_block, 0x03C0, 1, 12 }, /* scenario.detail_object_collection_palette */
	{ _tag_swap_field_block, 0x0420, 1, 12 }, /* scenario.ai_actor_palette */
	{ _tag_swap_field_block, 0x042C, 1, 12 }, /* scenario.ai_encounters */
	{ _tag_swap_field_block, 0x0438, 1, 12 }, /* scenario.ai_command_lists */
	{ _tag_swap_field_block, 0x0444, 1, 12 }, /* scenario.ai_animation_references */
	{ _tag_swap_field_block, 0x0450, 1, 12 }, /* scenario.ai_script_references */
	{ _tag_swap_field_block, 0x045C, 1, 12 }, /* scenario.ai_recording_references */
	{ _tag_swap_field_block, 0x0468, 1, 12 }, /* scenario.ai_conversations */
	{ _tag_swap_field_data, 0x0474, 1, 20 }, /* scenario.hs_syntax_data */
	{ _tag_swap_field_data, 0x0488, 1, 20 }, /* scenario.hs_string_constants */
	{ _tag_swap_field_block, 0x049C, 1, 12 }, /* scenario.hs_scripts */
	{ _tag_swap_field_block, 0x04A8, 1, 12 }, /* scenario.hs_globals */
	{ _tag_swap_field_block, 0x04B4, 1, 12 }, /* scenario.hs_references */
	{ _tag_swap_field_block, 0x04C0, 1, 12 }, /* scenario.hs_source_files */
	{ _tag_swap_field_block, 0x04E4, 1, 12 }, /* scenario.cutscene_flags */
	{ _tag_swap_field_block, 0x04F0, 1, 12 }, /* scenario.cutscene_camera_points */
	{ _tag_swap_field_block, 0x04FC, 1, 12 }, /* scenario.cutscene_chapter_titles */
	{ _tag_swap_field_block, 0x05A4, 1, 12 }, /* scenario.structure_bsp_references */
	{ _tag_swap_field_struct, 0x0000, 1, 380 }, /* scenery_definition.object */
	{ _tag_swap_field_struct, 0x0000, 1, 40 }, /* shader_effect.shader */
	{ _tag_swap_field_struct, 0x0060, 1, 56 }, /* shader_effect.secondary_map_animation */
	{ _tag_swap_field_struct, 0x0000, 1, 40 }, /* shader_environment.shader */
	{ _tag_swap_field_struct, 0x0000, 1, 40 }, /* shader_model.shader */
	{ _tag_swap_field_struct, 0x00FC, 1, 56 }, /* shader_model.texture_animation */
	{ _tag_swap_field_struct, 0x0000, 1, 40 }, /* shader_transparent_chicago.shader */
	{ _tag_swap_field_struct, 0x0028, 1, 56 }, /* shader_transparent_chicago.transparent */
	{ _tag_swap_field_block, 0x0048, 1, 12 }, /* shader_transparent_chicago.transparent.extra_layers */
	{ _tag_swap_field_block, 0x0054, 1, 12 }, /* shader_transparent_chicago.transparent.maps */
	{ _tag_swap_field_struct, 0x0000, 1, 40 }, /* shader_transparent_chicago_extended.shader */
	{ _tag_swap_field_struct, 0x0028, 1, 56 }, /* shader_transparent_chicago_extended.transparent */
	{ _tag_swap_field_block, 0x0048, 1, 12 }, /* shader_transparent_chicago_extended.transparent.extra_layers */
	{ _tag_swap_field_block, 0x0054, 1, 12 }, /* shader_transparent_chicago_extended.transparent.maps */
	{ _tag_swap_field_block, 0x0060, 1, 12 }, /* shader_transparent_chicago_extended.two_stage_maps */
	{ _tag_swap_field_struct, 0x00A4, 1, 56 }, /* shader_transparent_chicago_map.map_animation */
	{ _tag_swap_field_struct, 0x0000, 1, 40 }, /* shader_transparent_generic.shader */
	{ _tag_swap_field_struct, 0x0028, 1, 56 }, /* shader_transparent_generic.transparent */
	{ _tag_swap_field_block, 0x0048, 1, 12 }, /* shader_transparent_generic.transparent.extra_layers */
	{ _tag_swap_field_block, 0x0054, 1, 12 }, /* shader_transparent_generic.transparent.maps */
	{ _tag_swap_field_block, 0x0060, 1, 12 }, /* shader_transparent_generic.stages */
	{ _tag_swap_field_struct, 0x002C, 1, 56 }, /* shader_transparent_generic_map.map_animation */
	{ _tag_swap_field_struct, 0x0000, 1, 40 }, /* shader_transparent_glass.shader */
	{ _tag_swap_field_struct, 0x0000, 1, 40 }, /* shader_transparent_meter.shader */
	{ _tag_swap_field_struct, 0x0000, 1, 40 }, /* shader_transparent_plasma.shader */
	{ _tag_swap_field_struct, 0x0000, 1, 40 }, /* shader_transparent_water.shader */
	{ _tag_swap_field_block, 0x0124, 1, 12 }, /* shader_transparent_water.ripples */
	{ _tag_swap_field_block, 0x00AC, 1, 12 }, /* sky.shader_functions */
	{ _tag_swap_field_block, 0x00B8, 1, 12 }, /* sky.animations */
	{ _tag_swap_field_block, 0x00C4, 1, 12 }, /* sky.lights */
	{ _tag_swap_field_block, 0x0098, 1, 12 }, /* sound.pitch_ranges */
	{ _tag_swap_field_file_data, 0x0040, 1, 20 }, /* sound_permutation.samples */
	{ _tag_swap_field_data, 0x0054, 1, 20 }, /* sound_permutation.mouth_data */
	{ _tag_swap_field_data, 0x0068, 1, 20 }, /* sound_permutation.subtitle_data */
	{ _tag_swap_field_block, 0x003C, 1, 12 }, /* sound_pitch_range.permutations */
	{ _tag_swap_field_struct, 0x0000, 1, 380 }, /* sound_scenery_definition.object */
	{ _tag_swap_field_block, 0x00C4, 1, 12 }, /* squad.move_positions */
	{ _tag_swap_field_block, 0x00D0, 1, 12 }, /* squad.starting_locations */
	{ _tag_swap_field_block, 0x0058, 1, 12 }, /* static_hud_element.multitexture_overlays */
	{ _tag_swap_field_block, 0x0000, 1, 12 }, /* string_list.strings */
	{ _tag_swap_field_data, 0x0000, 1, 20 }, /* string_list_entry.string */
	{ _tag_swap_field_struct, 0x002C, 1, 116 }, /* structure_bsp.default_lighting */
	{ _tag_swap_field_block, 0x00A4, 1, 12 }, /* structure_bsp.collision_materials */
	{ _tag_swap_field_block, 0x00B0, 1, 12 }, /* structure_bsp.collision_bsp */
	{ _tag_swap_field_block, 0x00BC, 1, 12 }, /* structure_bsp.nodes */
	{ _tag_swap_field_block, 0x00E0, 1, 12 }, /* structure_bsp.leaves */
	{ _tag_swap_field_block, 0x00EC, 1, 12 }, /* structure_bsp.surface_references */
	{ _tag_swap_field_block, 0x00F8, 1, 12 }, /* structure_bsp.surfaces */
	{ _tag_swap_field_block, 0x0104, 1, 12 }, /* structure_bsp.lightmaps */
	{ _tag_swap_field_block, 0x011C, 1, 12 }, /* structure_bsp.lens_flares */
	{ _tag_swap_field_block, 0x0128, 1, 12 }, /* structure_bsp.lens_flare_markers */
	{ _tag_swap_field_block, 0x0134, 1, 12 }, /* structure_bsp.clusters */
	{ _tag_swap_field_data, 0x0140, 1, 20 }, /* structure_bsp.cluster_data */
	{ _tag_swap_field_block, 0x0154, 1, 12 }, /* structure_bsp.cluster_portals */
	{ _tag_swap_field_block, 0x016C, 1, 12 }, /* structure_bsp.breakable_surfaces */
	{ _tag_swap_field_block, 0x0178, 1, 12 }, /* structure_bsp.fog_planes */
	{ _tag_swap_field_block, 0x0184, 1, 12 }, /* structure_bsp.fog_regions */
	{ _tag_swap_field_block, 0x0190, 1, 12 }, /* structure_bsp.fog_palette */
	{ _tag_swap_field_block, 0x01B4, 1, 12 }, /* structure_bsp.weather_palette */
	{ _tag_swap_field_block, 0x01C0, 1, 12 }, /* structure_bsp.weather_polyhedra */
	{ _tag_swap_field_block, 0x01E4, 1, 12 }, /* structure_bsp.pathfinding_surfaces */
	{ _tag_swap_field_block, 0x01F0, 1, 12 }, /* structure_bsp.pathfinding_edges */
	{ _tag_swap_field_block, 0x01FC, 1, 12 }, /* structure_bsp.background_sound_palette */
	{ _tag_swap_field_block, 0x0208, 1, 12 }, /* structure_bsp.sound_environment_palette */
	{ _tag_swap_field_data, 0x0214, 1, 20 }, /* structure_bsp.sound_cluster_data */
	{ _tag_swap_field_block, 0x024C, 1, 12 }, /* structure_bsp.detail_object_data */
	{ _tag_swap_field_block, 0x0258, 1, 12 }, /* structure_bsp.runtime_decals */
	{ _tag_swap_field_struct, 0x026C, 1, 28 }, /* structure_bsp.leaf_map */
	{ _tag_swap_field_block, 0x0028, 1, 12 }, /* structure_cluster.predicted_resources */
	{ _tag_swap_field_block, 0x0034, 1, 12 }, /* structure_cluster.subclusters */
	{ _tag_swap_field_block, 0x0044, 1, 12 }, /* structure_cluster.surface_indices */
	{ _tag_swap_field_block, 0x0050, 1, 12 }, /* structure_cluster.mirrors */
	{ _tag_swap_field_block, 0x005C, 1, 12 }, /* structure_cluster.portal_indices */
	{ _tag_swap_field_block, 0x0034, 1, 12 }, /* structure_cluster_portal.vertices */
	{ _tag_swap_field_block, 0x0000, 1, 12 }, /* structure_detail_object_data.cells */
	{ _tag_swap_field_block, 0x000C, 1, 12 }, /* structure_detail_object_data.detail_objects */
	{ _tag_swap_field_block, 0x0018, 1, 12 }, /* structure_detail_object_data.counts */
	{ _tag_swap_field_block, 0x0024, 1, 12 }, /* structure_detail_object_data.z_reference_vectors */
	{ _tag_swap_field_block, 0x0014, 1, 12 }, /* structure_fog_plane.vertices */
	{ _tag_swap_field_block, 0x0014, 1, 12 }, /* structure_lightmap.materials */
	{ _tag_swap_field_struct, 0x0028, 1, 116 }, /* structure_material.lighting */
	{ _tag_swap_field_data, 0x00D8, 1, 20 }, /* structure_material.uncompressed_vertex_data */
	{ _tag_swap_field_data, 0x00EC, 1, 20 }, /* structure_material.compressed_vertex_data */
	{ _tag_swap_field_block, 0x0034, 1, 12 }, /* structure_mirror.points */
	{ _tag_swap_field_block, 0x0018, 1, 12 }, /* structure_subcluster.surface_indices */
	{ _tag_swap_field_block, 0x0014, 1, 12 }, /* structure_weather_polyhedron.planes */
	{ _tag_swap_field_block, 0x0048, 1, 12 }, /* ui_widget.game_data_inputs */
	{ _tag_swap_field_block, 0x0054, 1, 12 }, /* ui_widget.event_handlers */
	{ _tag_swap_field_block, 0x0060, 1, 12 }, /* ui_widget.search_and_replace_functions */
	{ _tag_swap_field_block, 0x02D4, 1, 12 }, /* ui_widget.conditional_widgets */
	{ _tag_swap_field_block, 0x03E0, 1, 12 }, /* ui_widget.child_widgets */
	{ _tag_swap_field_block, 0x0000, 1, 12 }, /* ui_widget_collection.widgets */
	{ _tag_swap_field_block, 0x0000, 1, 12 }, /* unicode_string_list.strings */
	{ _tag_swap_field_data, 0x0000, 1, 20 }, /* unicode_string_list_entry.string */
	{ _tag_swap_field_struct, 0x002C, 1, 88 }, /* unit.camera */
	{ _tag_swap_field_block, 0x012C, 1, 12 }, /* unit.huds */
	{ _tag_swap_field_block, 0x0138, 1, 12 }, /* unit.dialogue_variants */
	{ _tag_swap_field_block, 0x0150, 1, 12 }, /* unit.powered_seats */
	{ _tag_swap_field_block, 0x015C, 1, 12 }, /* unit.initial_weapons */
	{ _tag_swap_field_block, 0x0168, 1, 12 }, /* unit.seats */
	{ _tag_swap_field_block, 0x004C, 1, 12 }, /* unit_camera.unit_camera_tracks */
	{ _tag_swap_field_struct, 0x0000, 1, 380 }, /* unit_definition.object */
	{ _tag_swap_field_struct, 0x017C, 1, 372 }, /* unit_definition.unit */
	{ _tag_swap_field_struct, 0x0000, 1, 36 }, /* unit_hud_interface.absolute_placement */
	{ _tag_swap_field_struct, 0x0024, 1, 104 }, /* unit_hud_interface.background */
	{ _tag_swap_field_struct, 0x008C, 1, 240 }, /* unit_hud_interface.shield_meter */
	{ _tag_swap_field_struct, 0x017C, 1, 240 }, /* unit_hud_interface.health_meter */
	{ _tag_swap_field_struct, 0x026C, 1, 240 }, /* unit_hud_interface.motion_sensor */
	{ _tag_swap_field_struct, 0x0380, 1, 36 }, /* unit_hud_interface.auxilary_panel.absolute_placement */
	{ _tag_swap_field_block, 0x03A4, 1, 12 }, /* unit_hud_interface.auxilary_panel.auxilary_overlays */
	{ _tag_swap_field_block, 0x03C0, 1, 12 }, /* unit_hud_interface.warning_sounds */
	{ _tag_swap_field_block, 0x03CC, 1, 12 }, /* unit_hud_interface.auxilary_meters */
	{ _tag_swap_field_struct, 0x0084, 1, 88 }, /* unit_seat.camera */
	{ _tag_swap_field_block, 0x00DC, 1, 12 }, /* unit_seat.seat_huds */
	{ _tag_swap_field_block, 0x005C, 1, 12 }, /* vehicle_animation.animations */
	{ _tag_swap_field_block, 0x0068, 1, 12 }, /* vehicle_animation.suspensions */
	{ _tag_swap_field_struct, 0x0000, 1, 380 }, /* vehicle_definition.unit.object */
	{ _tag_swap_field_struct, 0x017C, 1, 372 }, /* vehicle_definition.unit.unit */
	{ _tag_swap_field_block, 0x0030, 1, 12 }, /* virtual_keyboard.keys */
	{ _tag_swap_field_block, 0x01DC, 1, 12 }, /* weapon.predicted_resources */
	{ _tag_swap_field_block, 0x01E8, 1, 12 }, /* weapon.magazines */
	{ _tag_swap_field_block, 0x01F4, 1, 12 }, /* weapon.triggers */
	{ _tag_swap_field_struct, 0x0000, 1, 380 }, /* weapon_definition.object */
	{ _tag_swap_field_struct, 0x017C, 1, 396 }, /* weapon_definition.item */
	{ _tag_swap_field_struct, 0x0308, 1, 512 }, /* weapon_definition.weapon */
	{ _tag_swap_field_block, 0x0034, 1, 12 }, /* weapon_hud_crosshairs_element.crosshairs.items */
	{ _tag_swap_field_struct, 0x003C, 1, 36 }, /* weapon_hud_interface.absolute_placement */
	{ _tag_swap_field_block, 0x0060, 1, 12 }, /* weapon_hud_interface.statics */
	{ _tag_swap_field_block, 0x006C, 1, 12 }, /* weapon_hud_interface.meters */
	{ _tag_swap_field_block, 0x0078, 1, 12 }, /* weapon_hud_interface.numbers */
	{ _tag_swap_field_block, 0x0084, 1, 12 }, /* weapon_hud_interface.crosshairs */
	{ _tag_swap_field_block, 0x0090, 1, 12 }, /* weapon_hud_interface.overlays */
	{ _tag_swap_field_block, 0x00AC, 1, 12 }, /* weapon_hud_interface.screen_effects */
	{ _tag_swap_field_struct, 0x0000, 1, 36 }, /* weapon_hud_meter_element.header */
	{ _tag_swap_field_struct, 0x0024, 1, 104 }, /* weapon_hud_meter_element.meter_element */
	{ _tag_swap_field_struct, 0x0000, 1, 36 }, /* weapon_hud_number_element.header */
	{ _tag_swap_field_block, 0x0010, 1, 12 }, /* weapon_hud_overlay.items */
	{ _tag_swap_field_struct, 0x0024, 1, 28 }, /* weapon_hud_overlays_element.overlays */
	{ _tag_swap_field_struct, 0x0000, 1, 36 }, /* weapon_hud_static_element.header */
	{ _tag_swap_field_struct, 0x0024, 1, 104 }, /* weapon_hud_static_element.static_element */
	{ _tag_swap_field_block, 0x0064, 1, 12 }, /* weapon_magazine.ammunition_objects */
	{ _tag_swap_field_block, 0x0108, 1, 12 }, /* weapon_trigger.firing_effects */
	{ _tag_swap_field_block, 0x0024, 1, 12 }, /* weather_particle_system.particle_types */
	{ _tag_swap_field_struct, 0x01A8, 1, 180 }, /* weather_particle_type.shader */
};

/* what each definition's data fields hold, by their offset (map_convert.py's
DATA_HANDLERS; a data field not here is left as it is) */
static struct tag_swap_data_handler const tag_swap_data_handlers[] =
{
	{ 0x0048, _tag_swap_data_hook }, /* animation.frame_info */
	{ 0x008C, _tag_swap_data_hook }, /* animation.default_data */
	{ 0x00A0, _tag_swap_data_hook }, /* animation.data */
	{ 0x0088, _tag_swap_data_bytes }, /* font.pixels */
	{ 0x0020, _tag_swap_data_bytes }, /* hs_source_file.source */
	{ 0x0000, _tag_swap_data_shorts }, /* hud_message_text.text_data */
	{ 0x002C, _tag_swap_data_hook }, /* recorded_animation.event_stream */
	{ 0x0474, _tag_swap_data_hs_syntax_data }, /* scenario.hs_syntax_data */
	{ 0x0488, _tag_swap_data_bytes }, /* scenario.hs_string_constants */
	{ 0x0054, _tag_swap_data_bytes }, /* sound_permutation.mouth_data */
	{ 0x0068, _tag_swap_data_shorts }, /* sound_permutation.subtitle_data */
	{ 0x0000, _tag_swap_data_bytes }, /* string_list_entry.string */
	{ 0x0140, _tag_swap_data_words }, /* structure_bsp.cluster_data */
	{ 0x0214, _tag_swap_data_bytes }, /* structure_bsp.sound_cluster_data */
	{ 0x00D8, _tag_swap_data_words }, /* structure_material.uncompressed_vertex_data */
	{ 0x00EC, _tag_swap_data_structure_vertices }, /* structure_material.compressed_vertex_data */
	{ 0x0000, _tag_swap_data_shorts }, /* unicode_string_list_entry.string */
};

/* pointer fields from the same layouts as map_convert.py */
static unsigned short const tag_swap_pointer_offsets[] =
{
	0x0140, /* actor_definition */
	0x025C, /* actor_definition */
	0x04BC, /* actor_definition */
	0x0004, /* actor_palette */
	0x0008, /* actor_variant_definition */
	0x0018, /* actor_variant_definition */
	0x0028, /* actor_variant_definition */
	0x0068, /* actor_variant_definition */
	0x01C4, /* actor_variant_definition */
	0x0230, /* actor_variant_definition */
	0x0024, /* ai_animation_reference */
	0x0034, /* ai_command_list */
	0x0040, /* ai_command_list */
	0x004C, /* ai_command_list */
	0x0058, /* ai_command_list */
	0x0054, /* animation */
	0x0098, /* animation */
	0x00AC, /* animation */
	0x0004, /* animation_graph */
	0x0010, /* animation_graph */
	0x001C, /* animation_graph */
	0x0028, /* animation_graph */
	0x0034, /* animation_graph */
	0x0040, /* animation_graph */
	0x004C, /* animation_graph */
	0x0058, /* animation_graph */
	0x006C, /* animation_graph */
	0x0078, /* animation_graph */
	0x0058, /* animation_graph_device_animations */
	0x0014, /* animation_graph_first_person_weapon_animations */
	0x0004, /* animation_graph_sound_reference */
	0x0044, /* animation_graph_unit_seat */
	0x0050, /* animation_graph_unit_seat */
	0x005C, /* animation_graph_unit_seat */
	0x0014, /* animation_graph_weapon_animations */
	0x009C, /* animation_graph_weapon_class */
	0x00A8, /* animation_graph_weapon_class */
	0x00B4, /* animation_graph_weapon_class */
	0x0034, /* animation_graph_weapon_type */
	0x0024, /* antenna */
	0x0034, /* antenna */
	0x00C8, /* antenna */
	0x003C, /* auxilary_meter */
	0x0070, /* auxilary_meter */
	0x00A4, /* auxilary_meter */
	0x00D8, /* auxilary_meter */
	0x0028, /* auxilary_overlay */
	0x005C, /* auxilary_overlay */
	0x0028, /* biped */
	0x00A0, /* biped */
	0x01FC, /* biped */
	0x002C, /* biped_definition */
	0x003C, /* biped_definition */
	0x0074, /* biped_definition */
	0x0084, /* biped_definition */
	0x0094, /* biped_definition */
	0x00A4, /* biped_definition */
	0x0144, /* biped_definition */
	0x0150, /* biped_definition */
	0x015C, /* biped_definition */
	0x0168, /* biped_definition */
	0x0174, /* biped_definition */
	0x018C, /* biped_definition */
	0x01F8, /* biped_definition */
	0x0250, /* biped_definition */
	0x028C, /* biped_definition */
	0x02AC, /* biped_definition */
	0x02B8, /* biped_definition */
	0x02D0, /* biped_definition */
	0x02DC, /* biped_definition */
	0x02E8, /* biped_definition */
	0x0318, /* biped_definition */
	0x0390, /* biped_definition */
	0x04EC, /* biped_definition */
	0x0004, /* biped_palette */
	0x0028, /* bitmap_data */
	0x002C, /* bitmap_data */
	0x0028, /* bitmap_group */
	0x003C, /* bitmap_group */
	0x0058, /* bitmap_group */
	0x0064, /* bitmap_group */
	0x0038, /* bitmap_sequence */
	0x0004, /* breakable_surface_particle_effect */
	0x0008, /* camera_track */
	0x0004, /* collision_bsp */
	0x0010, /* collision_bsp */
	0x001C, /* collision_bsp */
	0x0028, /* collision_bsp */
	0x0034, /* collision_bsp */
	0x0040, /* collision_bsp */
	0x004C, /* collision_bsp */
	0x0058, /* collision_bsp */
	0x0074, /* collision_model */
	0x0088, /* collision_model */
	0x009C, /* collision_model */
	0x00AC, /* collision_model */
	0x00C0, /* collision_model */
	0x018C, /* collision_model */
	0x019C, /* collision_model */
	0x01AC, /* collision_model */
	0x0238, /* collision_model */
	0x0244, /* collision_model */
	0x0250, /* collision_model */
	0x0284, /* collision_model */
	0x0290, /* collision_model */
	0x0038, /* collision_node */
	0x0004, /* color_table */
	0x0034, /* contrail */
	0x013C, /* contrail */
	0x0014, /* contrail_point_state */
	0x005C, /* control */
	0x006C, /* control */
	0x007C, /* control */
	0x002C, /* control_definition */
	0x003C, /* control_definition */
	0x0074, /* control_definition */
	0x0084, /* control_definition */
	0x0094, /* control_definition */
	0x00A4, /* control_definition */
	0x0144, /* control_definition */
	0x0150, /* control_definition */
	0x015C, /* control_definition */
	0x0168, /* control_definition */
	0x0174, /* control_definition */
	0x01A4, /* control_definition */
	0x01B4, /* control_definition */
	0x01C4, /* control_definition */
	0x01D4, /* control_definition */
	0x01E4, /* control_definition */
	0x01F4, /* control_definition */
	0x0210, /* control_definition */
	0x02EC, /* control_definition */
	0x02FC, /* control_definition */
	0x030C, /* control_definition */
	0x0004, /* control_palette */
	0x0054, /* conversation */
	0x0060, /* conversation */
	0x006C, /* conversation */
	0x0020, /* conversation_line */
	0x0030, /* conversation_line */
	0x0040, /* conversation_line */
	0x0050, /* conversation_line */
	0x0060, /* conversation_line */
	0x0070, /* conversation_line */
	0x0118, /* damage_effect */
	0x003C, /* damage_region */
	0x004C, /* damage_region */
	0x0074, /* damage_resistance */
	0x0088, /* damage_resistance */
	0x009C, /* damage_resistance */
	0x00AC, /* damage_resistance */
	0x00C0, /* damage_resistance */
	0x018C, /* damage_resistance */
	0x019C, /* damage_resistance */
	0x01AC, /* damage_resistance */
	0x0238, /* damage_resistance */
	0x0244, /* damage_resistance */
	0x0250, /* damage_resistance */
	0x000C, /* decal */
	0x00DC, /* decal */
	0x0004, /* decal_palette */
	0x0038, /* detail_object_collection */
	0x0048, /* detail_object_collection */
	0x0004, /* detail_object_collection_palette */
	0x0028, /* device */
	0x0038, /* device */
	0x0048, /* device */
	0x0058, /* device */
	0x0068, /* device */
	0x0078, /* device */
	0x0094, /* device */
	0x002C, /* device_definition */
	0x003C, /* device_definition */
	0x0074, /* device_definition */
	0x0084, /* device_definition */
	0x0094, /* device_definition */
	0x00A4, /* device_definition */
	0x0144, /* device_definition */
	0x0150, /* device_definition */
	0x015C, /* device_definition */
	0x0168, /* device_definition */
	0x0174, /* device_definition */
	0x01A4, /* device_definition */
	0x01B4, /* device_definition */
	0x01C4, /* device_definition */
	0x01D4, /* device_definition */
	0x01E4, /* device_definition */
	0x01F4, /* device_definition */
	0x0210, /* device_definition */
	0x0014, /* dialogue_definition */
	0x0024, /* dialogue_definition */
	0x0034, /* dialogue_definition */
	0x0044, /* dialogue_definition */
	0x0054, /* dialogue_definition */
	0x0064, /* dialogue_definition */
	0x0074, /* dialogue_definition */
	0x0084, /* dialogue_definition */
	0x0094, /* dialogue_definition */
	0x00A4, /* dialogue_definition */
	0x00B4, /* dialogue_definition */
	0x00C4, /* dialogue_definition */
	0x00D4, /* dialogue_definition */
	0x00E4, /* dialogue_definition */
	0x00F4, /* dialogue_definition */
	0x0104, /* dialogue_definition */
	0x0114, /* dialogue_definition */
	0x0124, /* dialogue_definition */
	0x0134, /* dialogue_definition */
	0x0144, /* dialogue_definition */
	0x0154, /* dialogue_definition */
	0x0164, /* dialogue_definition */
	0x0174, /* dialogue_definition */
	0x0184, /* dialogue_definition */
	0x0194, /* dialogue_definition */
	0x01A4, /* dialogue_definition */
	0x01B4, /* dialogue_definition */
	0x01C4, /* dialogue_definition */
	0x01D4, /* dialogue_definition */
	0x01E4, /* dialogue_definition */
	0x01F4, /* dialogue_definition */
	0x0204, /* dialogue_definition */
	0x0214, /* dialogue_definition */
	0x0224, /* dialogue_definition */
	0x0234, /* dialogue_definition */
	0x0244, /* dialogue_definition */
	0x0254, /* dialogue_definition */
	0x0264, /* dialogue_definition */
	0x0274, /* dialogue_definition */
	0x0284, /* dialogue_definition */
	0x0294, /* dialogue_definition */
	0x02A4, /* dialogue_definition */
	0x02B4, /* dialogue_definition */
	0x02C4, /* dialogue_definition */
	0x02D4, /* dialogue_definition */
	0x02E4, /* dialogue_definition */
	0x02F4, /* dialogue_definition */
	0x0304, /* dialogue_definition */
	0x0314, /* dialogue_definition */
	0x0324, /* dialogue_definition */
	0x0334, /* dialogue_definition */
	0x0344, /* dialogue_definition */
	0x0354, /* dialogue_definition */
	0x0364, /* dialogue_definition */
	0x0374, /* dialogue_definition */
	0x0384, /* dialogue_definition */
	0x0394, /* dialogue_definition */
	0x03A4, /* dialogue_definition */
	0x03B4, /* dialogue_definition */
	0x03C4, /* dialogue_definition */
	0x03D4, /* dialogue_definition */
	0x03E4, /* dialogue_definition */
	0x03F4, /* dialogue_definition */
	0x0404, /* dialogue_definition */
	0x0414, /* dialogue_definition */
	0x0424, /* dialogue_definition */
	0x0434, /* dialogue_definition */
	0x0444, /* dialogue_definition */
	0x0454, /* dialogue_definition */
	0x0464, /* dialogue_definition */
	0x0474, /* dialogue_definition */
	0x0484, /* dialogue_definition */
	0x0494, /* dialogue_definition */
	0x04A4, /* dialogue_definition */
	0x04B4, /* dialogue_definition */
	0x04C4, /* dialogue_definition */
	0x04D4, /* dialogue_definition */
	0x04E4, /* dialogue_definition */
	0x04F4, /* dialogue_definition */
	0x0504, /* dialogue_definition */
	0x0514, /* dialogue_definition */
	0x0524, /* dialogue_definition */
	0x0534, /* dialogue_definition */
	0x0544, /* dialogue_definition */
	0x0554, /* dialogue_definition */
	0x0564, /* dialogue_definition */
	0x0574, /* dialogue_definition */
	0x0584, /* dialogue_definition */
	0x0594, /* dialogue_definition */
	0x05A4, /* dialogue_definition */
	0x05B4, /* dialogue_definition */
	0x05C4, /* dialogue_definition */
	0x05D4, /* dialogue_definition */
	0x05E4, /* dialogue_definition */
	0x05F4, /* dialogue_definition */
	0x0604, /* dialogue_definition */
	0x0614, /* dialogue_definition */
	0x0624, /* dialogue_definition */
	0x0634, /* dialogue_definition */
	0x0644, /* dialogue_definition */
	0x0654, /* dialogue_definition */
	0x0664, /* dialogue_definition */
	0x0674, /* dialogue_definition */
	0x0684, /* dialogue_definition */
	0x0694, /* dialogue_definition */
	0x06A4, /* dialogue_definition */
	0x06B4, /* dialogue_definition */
	0x06C4, /* dialogue_definition */
	0x06D4, /* dialogue_definition */
	0x06E4, /* dialogue_definition */
	0x06F4, /* dialogue_definition */
	0x0704, /* dialogue_definition */
	0x0714, /* dialogue_definition */
	0x0724, /* dialogue_definition */
	0x0734, /* dialogue_definition */
	0x0744, /* dialogue_definition */
	0x0754, /* dialogue_definition */
	0x0764, /* dialogue_definition */
	0x0774, /* dialogue_definition */
	0x0784, /* dialogue_definition */
	0x0794, /* dialogue_definition */
	0x07A4, /* dialogue_definition */
	0x07B4, /* dialogue_definition */
	0x07C4, /* dialogue_definition */
	0x07D4, /* dialogue_definition */
	0x07E4, /* dialogue_definition */
	0x07F4, /* dialogue_definition */
	0x0804, /* dialogue_definition */
	0x0814, /* dialogue_definition */
	0x0824, /* dialogue_definition */
	0x0834, /* dialogue_definition */
	0x0844, /* dialogue_definition */
	0x0854, /* dialogue_definition */
	0x0864, /* dialogue_definition */
	0x0874, /* dialogue_definition */
	0x0884, /* dialogue_definition */
	0x0894, /* dialogue_definition */
	0x08A4, /* dialogue_definition */
	0x08B4, /* dialogue_definition */
	0x08C4, /* dialogue_definition */
	0x08D4, /* dialogue_definition */
	0x08E4, /* dialogue_definition */
	0x08F4, /* dialogue_definition */
	0x0904, /* dialogue_definition */
	0x0914, /* dialogue_definition */
	0x0924, /* dialogue_definition */
	0x0934, /* dialogue_definition */
	0x0944, /* dialogue_definition */
	0x0954, /* dialogue_definition */
	0x0964, /* dialogue_definition */
	0x0974, /* dialogue_definition */
	0x0984, /* dialogue_definition */
	0x0994, /* dialogue_definition */
	0x09A4, /* dialogue_definition */
	0x09B4, /* dialogue_definition */
	0x09C4, /* dialogue_definition */
	0x09D4, /* dialogue_definition */
	0x09E4, /* dialogue_definition */
	0x09F4, /* dialogue_definition */
	0x0A04, /* dialogue_definition */
	0x0A14, /* dialogue_definition */
	0x0A24, /* dialogue_definition */
	0x0A34, /* dialogue_definition */
	0x0A44, /* dialogue_definition */
	0x0A54, /* dialogue_definition */
	0x0A64, /* dialogue_definition */
	0x0A74, /* dialogue_definition */
	0x0A84, /* dialogue_definition */
	0x0A94, /* dialogue_definition */
	0x0AA4, /* dialogue_definition */
	0x0AB4, /* dialogue_definition */
	0x0AC4, /* dialogue_definition */
	0x0AD4, /* dialogue_definition */
	0x0AE4, /* dialogue_definition */
	0x0AF4, /* dialogue_definition */
	0x0B04, /* dialogue_definition */
	0x0B14, /* dialogue_definition */
	0x0B24, /* dialogue_definition */
	0x0B34, /* dialogue_definition */
	0x0B44, /* dialogue_definition */
	0x0B54, /* dialogue_definition */
	0x0B64, /* dialogue_definition */
	0x0B74, /* dialogue_definition */
	0x0B84, /* dialogue_definition */
	0x0B94, /* dialogue_definition */
	0x0BA4, /* dialogue_definition */
	0x0BB4, /* dialogue_definition */
	0x0BC4, /* dialogue_definition */
	0x0BD4, /* dialogue_definition */
	0x0BE4, /* dialogue_definition */
	0x0BF4, /* dialogue_definition */
	0x0C04, /* dialogue_definition */
	0x0C14, /* dialogue_definition */
	0x0C24, /* dialogue_definition */
	0x0C34, /* dialogue_definition */
	0x0C44, /* dialogue_definition */
	0x0C54, /* dialogue_definition */
	0x0C64, /* dialogue_definition */
	0x0C74, /* dialogue_definition */
	0x0C84, /* dialogue_definition */
	0x0C94, /* dialogue_definition */
	0x0CA4, /* dialogue_definition */
	0x0CB4, /* dialogue_definition */
	0x0CC4, /* dialogue_definition */
	0x0CD4, /* dialogue_definition */
	0x0CE4, /* dialogue_definition */
	0x0CF4, /* dialogue_definition */
	0x0D04, /* dialogue_definition */
	0x0D14, /* dialogue_definition */
	0x0D24, /* dialogue_definition */
	0x0D34, /* dialogue_definition */
	0x0D44, /* dialogue_definition */
	0x0D54, /* dialogue_definition */
	0x0D64, /* dialogue_definition */
	0x0D74, /* dialogue_definition */
	0x0D84, /* dialogue_definition */
	0x0D94, /* dialogue_definition */
	0x0DA4, /* dialogue_definition */
	0x0DB4, /* dialogue_definition */
	0x0DC4, /* dialogue_definition */
	0x0DD4, /* dialogue_definition */
	0x0DE4, /* dialogue_definition */
	0x0DF4, /* dialogue_definition */
	0x0E04, /* dialogue_definition */
	0x0E14, /* dialogue_definition */
	0x0E24, /* dialogue_definition */
	0x0E34, /* dialogue_definition */
	0x0E44, /* dialogue_definition */
	0x0E54, /* dialogue_definition */
	0x0E64, /* dialogue_definition */
	0x0E74, /* dialogue_definition */
	0x0E84, /* dialogue_definition */
	0x0E94, /* dialogue_definition */
	0x0EA4, /* dialogue_definition */
	0x0EB4, /* dialogue_definition */
	0x0EC4, /* dialogue_definition */
	0x0ED4, /* dialogue_definition */
	0x0EE4, /* dialogue_definition */
	0x0EF4, /* dialogue_definition */
	0x0F04, /* dialogue_definition */
	0x0F14, /* dialogue_definition */
	0x0F24, /* dialogue_definition */
	0x0F34, /* dialogue_definition */
	0x0F44, /* dialogue_definition */
	0x0F54, /* dialogue_definition */
	0x0F64, /* dialogue_definition */
	0x0F74, /* dialogue_definition */
	0x0F84, /* dialogue_definition */
	0x0F94, /* dialogue_definition */
	0x0FA4, /* dialogue_definition */
	0x0FB4, /* dialogue_definition */
	0x0FC4, /* dialogue_definition */
	0x0FD4, /* dialogue_definition */
	0x0FE4, /* dialogue_definition */
	0x0FF4, /* dialogue_definition */
	0x1004, /* dialogue_definition */
	0x002C, /* effect */
	0x0038, /* effect */
	0x0030, /* effect_event */
	0x003C, /* effect_event */
	0x001C, /* effect_part */
	0x0058, /* effect_particles */
	0x0084, /* encounter */
	0x0090, /* encounter */
	0x009C, /* encounter */
	0x00A8, /* encounter */
	0x000C, /* equipment */
	0x002C, /* equipment_definition */
	0x003C, /* equipment_definition */
	0x0074, /* equipment_definition */
	0x0084, /* equipment_definition */
	0x0094, /* equipment_definition */
	0x00A4, /* equipment_definition */
	0x0144, /* equipment_definition */
	0x0150, /* equipment_definition */
	0x015C, /* equipment_definition */
	0x0168, /* equipment_definition */
	0x0174, /* equipment_definition */
	0x024C, /* equipment_definition */
	0x025C, /* equipment_definition */
	0x02EC, /* equipment_definition */
	0x02FC, /* equipment_definition */
	0x0314, /* equipment_definition */
	0x0004, /* equipment_palette */
	0x001C, /* flag */
	0x002C, /* flag */
	0x0048, /* flag */
	0x0058, /* flag */
	0x00C0, /* fog */
	0x00F8, /* fog */
	0x0108, /* fog */
	0x0034, /* font */
	0x0040, /* font */
	0x0050, /* font */
	0x0060, /* font */
	0x0070, /* font */
	0x0080, /* font */
	0x0094, /* font */
	0x0004, /* font_character_table */
	0x00FC, /* game_globals */
	0x0108, /* game_globals */
	0x0114, /* game_globals */
	0x0120, /* game_globals */
	0x012C, /* game_globals */
	0x0138, /* game_globals */
	0x0144, /* game_globals */
	0x0150, /* game_globals */
	0x015C, /* game_globals */
	0x0168, /* game_globals */
	0x0174, /* game_globals */
	0x0180, /* game_globals */
	0x018C, /* game_globals */
	0x0198, /* game_globals */
	0x01A4, /* game_globals */
	0x0014, /* game_globals_falling_damage */
	0x0030, /* game_globals_falling_damage */
	0x0040, /* game_globals_falling_damage */
	0x0050, /* game_globals_falling_damage */
	0x0060, /* game_globals_falling_damage */
	0x0070, /* game_globals_falling_damage */
	0x0004, /* game_globals_first_person_interface */
	0x0014, /* game_globals_first_person_interface */
	0x0024, /* game_globals_first_person_interface */
	0x0038, /* game_globals_first_person_interface */
	0x004C, /* game_globals_first_person_interface */
	0x005C, /* game_globals_first_person_interface */
	0x0008, /* game_globals_grenade */
	0x0018, /* game_globals_grenade */
	0x0028, /* game_globals_grenade */
	0x0038, /* game_globals_grenade */
	0x0004, /* game_globals_interface_tag_references */
	0x0014, /* game_globals_interface_tag_references */
	0x0024, /* game_globals_interface_tag_references */
	0x0034, /* game_globals_interface_tag_references */
	0x0044, /* game_globals_interface_tag_references */
	0x0054, /* game_globals_interface_tag_references */
	0x0064, /* game_globals_interface_tag_references */
	0x0074, /* game_globals_interface_tag_references */
	0x0084, /* game_globals_interface_tag_references */
	0x0094, /* game_globals_interface_tag_references */
	0x00A4, /* game_globals_interface_tag_references */
	0x00B4, /* game_globals_interface_tag_references */
	0x00C4, /* game_globals_interface_tag_references */
	0x00D4, /* game_globals_interface_tag_references */
	0x00E4, /* game_globals_interface_tag_references */
	0x00F4, /* game_globals_interface_tag_references */
	0x0004, /* game_globals_multiplayer_information */
	0x0014, /* game_globals_multiplayer_information */
	0x0024, /* game_globals_multiplayer_information */
	0x0030, /* game_globals_multiplayer_information */
	0x0040, /* game_globals_multiplayer_information */
	0x0050, /* game_globals_multiplayer_information */
	0x0060, /* game_globals_multiplayer_information */
	0x0078, /* game_globals_player_control */
	0x0004, /* game_globals_player_information */
	0x00BC, /* game_globals_player_information */
	0x0004, /* game_globals_rasterizer_data */
	0x0014, /* game_globals_rasterizer_data */
	0x0024, /* game_globals_rasterizer_data */
	0x0034, /* game_globals_rasterizer_data */
	0x0044, /* game_globals_rasterizer_data */
	0x0054, /* game_globals_rasterizer_data */
	0x0064, /* game_globals_rasterizer_data */
	0x00B0, /* game_globals_rasterizer_data */
	0x00C0, /* game_globals_rasterizer_data */
	0x00D0, /* game_globals_rasterizer_data */
	0x00E0, /* game_globals_rasterizer_data */
	0x00F0, /* game_globals_rasterizer_data */
	0x0100, /* game_globals_rasterizer_data */
	0x0110, /* game_globals_rasterizer_data */
	0x0120, /* game_globals_rasterizer_data */
	0x0130, /* game_globals_rasterizer_data */
	0x01A0, /* game_globals_rasterizer_data */
	0x0004, /* game_globals_vehicle */
	0x002C, /* garbage_definition */
	0x003C, /* garbage_definition */
	0x0074, /* garbage_definition */
	0x0084, /* garbage_definition */
	0x0094, /* garbage_definition */
	0x00A4, /* garbage_definition */
	0x0144, /* garbage_definition */
	0x0150, /* garbage_definition */
	0x015C, /* garbage_definition */
	0x0168, /* garbage_definition */
	0x0174, /* garbage_definition */
	0x024C, /* garbage_definition */
	0x025C, /* garbage_definition */
	0x02EC, /* garbage_definition */
	0x02FC, /* garbage_definition */
	0x00B0, /* gbxmodel */
	0x00BC, /* gbxmodel */
	0x00C8, /* gbxmodel */
	0x00D4, /* gbxmodel */
	0x00E0, /* gbxmodel */
	0x0028, /* gbxmodel_geometry */
	0x0024, /* gbxmodel_geometry_part */
	0x0030, /* gbxmodel_geometry_part */
	0x003C, /* gbxmodel_geometry_part */
	0x0148, /* glow */
	0x0028, /* grenade_count_panel */
	0x005C, /* grenade_count_panel */
	0x004C, /* grenade_hud_interface */
	0x0080, /* grenade_hud_interface */
	0x00B4, /* grenade_hud_interface */
	0x00E8, /* grenade_hud_interface */
	0x0150, /* grenade_hud_interface */
	0x0160, /* grenade_hud_interface */
	0x016C, /* grenade_hud_interface */
	0x01BC, /* grenade_hud_interface */
	0x001C, /* hs_reference */
	0x002C, /* hs_source_file */
	0x004C, /* hud_globals */
	0x005C, /* hud_globals */
	0x0098, /* hud_globals */
	0x00A8, /* hud_globals */
	0x00B8, /* hud_globals */
	0x00C8, /* hud_globals */
	0x00F4, /* hud_globals */
	0x0154, /* hud_globals */
	0x0164, /* hud_globals */
	0x02C4, /* hud_globals */
	0x033C, /* hud_globals */
	0x03CC, /* hud_globals */
	0x03E4, /* hud_globals */
	0x000C, /* hud_message_text */
	0x0018, /* hud_message_text */
	0x0024, /* hud_message_text */
	0x0004, /* hud_number */
	0x001C, /* hud_screen_effect */
	0x002C, /* hud_screen_effect */
	0x0004, /* hud_sound */
	0x00D0, /* item */
	0x00E0, /* item */
	0x0170, /* item */
	0x0180, /* item */
	0x0004, /* item_collection_definition */
	0x002C, /* item_definition */
	0x003C, /* item_definition */
	0x0074, /* item_definition */
	0x0084, /* item_definition */
	0x0094, /* item_definition */
	0x00A4, /* item_definition */
	0x0144, /* item_definition */
	0x0150, /* item_definition */
	0x015C, /* item_definition */
	0x0168, /* item_definition */
	0x0174, /* item_definition */
	0x024C, /* item_definition */
	0x025C, /* item_definition */
	0x02EC, /* item_definition */
	0x02FC, /* item_definition */
	0x0028, /* item_permutation */
	0x0008, /* leaf_map */
	0x0014, /* leaf_map */
	0x0010, /* leaf_portal */
	0x0024, /* lens_flare */
	0x00C8, /* lens_flare */
	0x0068, /* light */
	0x0080, /* light */
	0x00B0, /* light */
	0x002C, /* light_fixture_definition */
	0x003C, /* light_fixture_definition */
	0x0074, /* light_fixture_definition */
	0x0084, /* light_fixture_definition */
	0x0094, /* light_fixture_definition */
	0x00A4, /* light_fixture_definition */
	0x0144, /* light_fixture_definition */
	0x0150, /* light_fixture_definition */
	0x015C, /* light_fixture_definition */
	0x0168, /* light_fixture_definition */
	0x0174, /* light_fixture_definition */
	0x01A4, /* light_fixture_definition */
	0x01B4, /* light_fixture_definition */
	0x01C4, /* light_fixture_definition */
	0x01D4, /* light_fixture_definition */
	0x01E4, /* light_fixture_definition */
	0x01F4, /* light_fixture_definition */
	0x0210, /* light_fixture_definition */
	0x0004, /* light_fixture_palette */
	0x0060, /* light_volume */
	0x0124, /* light_volume */
	0x0038, /* lightning */
	0x009C, /* lightning */
	0x00A8, /* lightning */
	0x0050, /* lightning_shader */
	0x0030, /* looping_sound */
	0x0040, /* looping_sound */
	0x004C, /* looping_sound */
	0x0004, /* looping_sound_detail */
	0x0034, /* looping_sound_track */
	0x0044, /* looping_sound_track */
	0x0054, /* looping_sound_track */
	0x0084, /* looping_sound_track */
	0x0094, /* looping_sound_track */
	0x002C, /* machine_definition */
	0x003C, /* machine_definition */
	0x0074, /* machine_definition */
	0x0084, /* machine_definition */
	0x0094, /* machine_definition */
	0x00A4, /* machine_definition */
	0x0144, /* machine_definition */
	0x0150, /* machine_definition */
	0x015C, /* machine_definition */
	0x0168, /* machine_definition */
	0x0174, /* machine_definition */
	0x01A4, /* machine_definition */
	0x01B4, /* machine_definition */
	0x01C4, /* machine_definition */
	0x01D4, /* machine_definition */
	0x01E4, /* machine_definition */
	0x01F4, /* machine_definition */
	0x0210, /* machine_definition */
	0x0004, /* machine_palette */
	0x0004, /* map_leaf */
	0x0010, /* map_leaf */
	0x0008, /* map_leaf_face */
	0x02E8, /* material */
	0x02F8, /* material */
	0x0320, /* material */
	0x0368, /* material */
	0x0004, /* material_effect */
	0x0004, /* material_effect_material */
	0x0014, /* material_effect_material */
	0x0004, /* material_effects */
	0x0028, /* meter_hud_element */
	0x005C, /* meter_hud_element */
	0x0028, /* metered_panel */
	0x005C, /* metered_panel */
	0x0090, /* metered_panel */
	0x00C4, /* metered_panel */
	0x00B0, /* model */
	0x00BC, /* model */
	0x00C8, /* model */
	0x00D4, /* model */
	0x00E0, /* model */
	0x0028, /* model_geometry */
	0x0024, /* model_geometry_part */
	0x0030, /* model_geometry_part */
	0x003C, /* model_geometry_part */
	0x004C, /* model_geometry_part */
	0x0050, /* model_geometry_part */
	0x0060, /* model_geometry_part */
	0x0064, /* model_geometry_part */
	0x0038, /* model_marker */
	0x0044, /* model_region */
	0x0050, /* model_region_permutation */
	0x0004, /* model_shader_reference */
	0x0028, /* motion_sensor_panel */
	0x005C, /* motion_sensor_panel */
	0x0090, /* motion_sensor_panel */
	0x00C4, /* motion_sensor_panel */
	0x0004, /* multiplayer_scenario_description */
	0x0014, /* multiplayer_scenario_description */
	0x0004, /* multiplayer_scenario_descriptions */
	0x0068, /* multitexture_overlay */
	0x0078, /* multitexture_overlay */
	0x0088, /* multitexture_overlay */
	0x0158, /* multitexture_overlay */
	0x0054, /* netgame_equipment */
	0x002C, /* object */
	0x003C, /* object */
	0x0074, /* object */
	0x0084, /* object */
	0x0094, /* object */
	0x00A4, /* object */
	0x0144, /* object */
	0x0150, /* object */
	0x015C, /* object */
	0x0168, /* object */
	0x0174, /* object */
	0x0004, /* object_attachment */
	0x0024, /* object_change_color */
	0x002C, /* object_definition */
	0x003C, /* object_definition */
	0x0074, /* object_definition */
	0x0084, /* object_definition */
	0x0094, /* object_definition */
	0x00A4, /* object_definition */
	0x0144, /* object_definition */
	0x0150, /* object_definition */
	0x015C, /* object_definition */
	0x0168, /* object_definition */
	0x0174, /* object_definition */
	0x0004, /* object_widget */
	0x0028, /* optional_meter_hud_element */
	0x005C, /* optional_meter_hud_element */
	0x0028, /* optional_static_hud_element */
	0x005C, /* optional_static_hud_element */
	0x0008, /* particle */
	0x0018, /* particle */
	0x0028, /* particle */
	0x004C, /* particle */
	0x005C, /* particle */
	0x0100, /* particle */
	0x003C, /* particle_system */
	0x0054, /* particle_system */
	0x0060, /* particle_system */
	0x0034, /* particle_system_particle_state */
	0x0088, /* particle_system_particle_state */
	0x0108, /* particle_system_particle_state */
	0x0060, /* particle_system_type */
	0x006C, /* particle_system_type */
	0x0078, /* particle_system_type */
	0x00B8, /* particle_system_type_state */
	0x0060, /* physics */
	0x006C, /* physics */
	0x0078, /* physics */
	0x002C, /* placeholder_definition */
	0x003C, /* placeholder_definition */
	0x0074, /* placeholder_definition */
	0x0084, /* placeholder_definition */
	0x0094, /* placeholder_definition */
	0x00A4, /* placeholder_definition */
	0x0144, /* placeholder_definition */
	0x0150, /* placeholder_definition */
	0x015C, /* placeholder_definition */
	0x0168, /* placeholder_definition */
	0x0174, /* placeholder_definition */
	0x0014, /* projectile */
	0x0034, /* projectile */
	0x007C, /* projectile */
	0x008C, /* projectile */
	0x009C, /* projectile */
	0x00AC, /* projectile */
	0x00C8, /* projectile */
	0x002C, /* projectile_definition */
	0x003C, /* projectile_definition */
	0x0074, /* projectile_definition */
	0x0084, /* projectile_definition */
	0x0094, /* projectile_definition */
	0x00A4, /* projectile_definition */
	0x0144, /* projectile_definition */
	0x0150, /* projectile_definition */
	0x015C, /* projectile_definition */
	0x0168, /* projectile_definition */
	0x0174, /* projectile_definition */
	0x0190, /* projectile_definition */
	0x01B0, /* projectile_definition */
	0x01F8, /* projectile_definition */
	0x0208, /* projectile_definition */
	0x0218, /* projectile_definition */
	0x0228, /* projectile_definition */
	0x0244, /* projectile_definition */
	0x0008, /* projectile_material_response */
	0x0040, /* projectile_material_response */
	0x006C, /* projectile_material_response */
	0x0038, /* recorded_animation */
	0x0004, /* scenario */
	0x0014, /* scenario */
	0x0024, /* scenario */
	0x0034, /* scenario */
	0x0044, /* scenario */
	0x00F0, /* scenario */
	0x00FC, /* scenario */
	0x0110, /* scenario */
	0x011C, /* scenario */
	0x0208, /* scenario */
	0x0214, /* scenario */
	0x0220, /* scenario */
	0x022C, /* scenario */
	0x0238, /* scenario */
	0x0244, /* scenario */
	0x0250, /* scenario */
	0x025C, /* scenario */
	0x0268, /* scenario */
	0x0274, /* scenario */
	0x0280, /* scenario */
	0x028C, /* scenario */
	0x0298, /* scenario */
	0x02A4, /* scenario */
	0x02B0, /* scenario */
	0x02BC, /* scenario */
	0x02C8, /* scenario */
	0x02D4, /* scenario */
	0x02E0, /* scenario */
	0x02EC, /* scenario */
	0x02F8, /* scenario */
	0x0304, /* scenario */
	0x0310, /* scenario */
	0x031C, /* scenario */
	0x0328, /* scenario */
	0x0334, /* scenario */
	0x0340, /* scenario */
	0x034C, /* scenario */
	0x0358, /* scenario */
	0x0364, /* scenario */
	0x0370, /* scenario */
	0x037C, /* scenario */
	0x0388, /* scenario */
	0x0394, /* scenario */
	0x03A0, /* scenario */
	0x03AC, /* scenario */
	0x03B8, /* scenario */
	0x03C4, /* scenario */
	0x0424, /* scenario */
	0x0430, /* scenario */
	0x043C, /* scenario */
	0x0448, /* scenario */
	0x0454, /* scenario */
	0x0460, /* scenario */
	0x046C, /* scenario */
	0x0480, /* scenario */
	0x0494, /* scenario */
	0x04A0, /* scenario */
	0x04AC, /* scenario */
	0x04B8, /* scenario */
	0x04C4, /* scenario */
	0x04E8, /* scenario */
	0x04F4, /* scenario */
	0x0500, /* scenario */
	0x0578, /* scenario */
	0x0588, /* scenario */
	0x0598, /* scenario */
	0x05A8, /* scenario */
	0x002C, /* scenery_definition */
	0x003C, /* scenery_definition */
	0x0074, /* scenery_definition */
	0x0084, /* scenery_definition */
	0x0094, /* scenery_definition */
	0x00A4, /* scenery_definition */
	0x0144, /* scenery_definition */
	0x0150, /* scenery_definition */
	0x015C, /* scenery_definition */
	0x0168, /* scenery_definition */
	0x0174, /* scenery_definition */
	0x0004, /* scenery_palette */
	0x0050, /* shader_effect */
	0x0034, /* shader_environment */
	0x008C, /* shader_environment */
	0x00BC, /* shader_environment */
	0x00D0, /* shader_environment */
	0x0100, /* shader_environment */
	0x012C, /* shader_environment */
	0x0258, /* shader_environment */
	0x0328, /* shader_environment */
	0x0004, /* shader_layer */
	0x00A8, /* shader_model */
	0x00C0, /* shader_model */
	0x00E0, /* shader_model */
	0x0168, /* shader_model */
	0x0014, /* shader_transparent */
	0x0024, /* shader_transparent */
	0x0030, /* shader_transparent */
	0x003C, /* shader_transparent_chicago */
	0x004C, /* shader_transparent_chicago */
	0x0058, /* shader_transparent_chicago */
	0x003C, /* shader_transparent_chicago_extended */
	0x004C, /* shader_transparent_chicago_extended */
	0x0058, /* shader_transparent_chicago_extended */
	0x0064, /* shader_transparent_chicago_extended */
	0x0070, /* shader_transparent_chicago_map */
	0x003C, /* shader_transparent_generic */
	0x004C, /* shader_transparent_generic */
	0x0058, /* shader_transparent_generic */
	0x0064, /* shader_transparent_generic */
	0x0020, /* shader_transparent_generic_map */
	0x0068, /* shader_transparent_glass */
	0x00B0, /* shader_transparent_glass */
	0x00C4, /* shader_transparent_glass */
	0x015C, /* shader_transparent_glass */
	0x0170, /* shader_transparent_glass */
	0x0050, /* shader_transparent_meter */
	0x00D8, /* shader_transparent_plasma */
	0x0120, /* shader_transparent_plasma */
	0x0050, /* shader_transparent_water */
	0x00A0, /* shader_transparent_water */
	0x00CC, /* shader_transparent_water */
	0x0128, /* shader_transparent_water */
	0x0004, /* sky */
	0x0014, /* sky */
	0x009C, /* sky */
	0x00B0, /* sky */
	0x00BC, /* sky */
	0x00C8, /* sky */
	0x0004, /* sky_light */
	0x0004, /* sky_reference */
	0x0074, /* sound */
	0x009C, /* sound */
	0x004C, /* sound_permutation */
	0x0060, /* sound_permutation */
	0x0074, /* sound_permutation */
	0x0040, /* sound_pitch_range */
	0x002C, /* sound_scenery_definition */
	0x003C, /* sound_scenery_definition */
	0x0074, /* sound_scenery_definition */
	0x0084, /* sound_scenery_definition */
	0x0094, /* sound_scenery_definition */
	0x00A4, /* sound_scenery_definition */
	0x0144, /* sound_scenery_definition */
	0x0150, /* sound_scenery_definition */
	0x015C, /* sound_scenery_definition */
	0x0168, /* sound_scenery_definition */
	0x0174, /* sound_scenery_definition */
	0x0004, /* sound_scenery_palette */
	0x00C8, /* squad */
	0x00D4, /* squad */
	0x00E0, /* squad */
	0x0040, /* starting_equipment */
	0x0050, /* starting_equipment */
	0x0060, /* starting_equipment */
	0x0070, /* starting_equipment */
	0x0080, /* starting_equipment */
	0x0090, /* starting_equipment */
	0x002C, /* starting_profile */
	0x0040, /* starting_profile */
	0x0028, /* static_hud_element */
	0x005C, /* static_hud_element */
	0x0004, /* string_list */
	0x000C, /* string_list_entry */
	0x0024, /* structure_background_sound_palette_entry */
	0x0004, /* structure_bsp */
	0x00A8, /* structure_bsp */
	0x00B4, /* structure_bsp */
	0x00C0, /* structure_bsp */
	0x00E4, /* structure_bsp */
	0x00F0, /* structure_bsp */
	0x00FC, /* structure_bsp */
	0x0108, /* structure_bsp */
	0x0120, /* structure_bsp */
	0x012C, /* structure_bsp */
	0x0138, /* structure_bsp */
	0x014C, /* structure_bsp */
	0x0158, /* structure_bsp */
	0x0170, /* structure_bsp */
	0x017C, /* structure_bsp */
	0x0188, /* structure_bsp */
	0x0194, /* structure_bsp */
	0x01B8, /* structure_bsp */
	0x01C4, /* structure_bsp */
	0x01E8, /* structure_bsp */
	0x01F4, /* structure_bsp */
	0x0200, /* structure_bsp */
	0x020C, /* structure_bsp */
	0x0220, /* structure_bsp */
	0x0244, /* structure_bsp */
	0x0250, /* structure_bsp */
	0x025C, /* structure_bsp */
	0x0274, /* structure_bsp */
	0x0280, /* structure_bsp */
	0x0008, /* structure_bsp_reference */
	0x0014, /* structure_bsp_reference */
	0x002C, /* structure_cluster */
	0x0038, /* structure_cluster */
	0x0048, /* structure_cluster */
	0x0054, /* structure_cluster */
	0x0060, /* structure_cluster */
	0x0038, /* structure_cluster_portal */
	0x0004, /* structure_collision_material */
	0x0004, /* structure_detail_object_data */
	0x0010, /* structure_detail_object_data */
	0x001C, /* structure_detail_object_data */
	0x0028, /* structure_detail_object_data */
	0x0024, /* structure_fog_palette_entry */
	0x0018, /* structure_fog_plane */
	0x0004, /* structure_lens_flare */
	0x0018, /* structure_lightmap */
	0x0004, /* structure_material */
	0x00BC, /* structure_material */
	0x00C0, /* structure_material */
	0x00D0, /* structure_material */
	0x00D4, /* structure_material */
	0x00E4, /* structure_material */
	0x00F8, /* structure_material */
	0x0028, /* structure_mirror */
	0x0038, /* structure_mirror */
	0x0024, /* structure_sound_environment_palette_entry */
	0x001C, /* structure_subcluster */
	0x0024, /* structure_weather_palette_entry */
	0x0084, /* structure_weather_palette_entry */
	0x0018, /* structure_weather_polyhedron */
	0x0004, /* tag_reference_camera_track */
	0x0004, /* tag_reference_item */
	0x0004, /* tag_reference_object */
	0x0004, /* tag_reference_sound */
	0x003C, /* ui_widget */
	0x004C, /* ui_widget */
	0x0058, /* ui_widget */
	0x0064, /* ui_widget */
	0x00F0, /* ui_widget */
	0x0100, /* ui_widget */
	0x0158, /* ui_widget */
	0x0168, /* ui_widget */
	0x01A8, /* ui_widget */
	0x02D8, /* ui_widget */
	0x03E4, /* ui_widget */
	0x0004, /* ui_widget_child */
	0x0004, /* ui_widget_collection */
	0x0004, /* ui_widget_collection_widget */
	0x0004, /* ui_widget_conditional */
	0x000C, /* ui_widget_event_handler */
	0x001C, /* ui_widget_event_handler */
	0x0004, /* unicode_string_list */
	0x000C, /* unicode_string_list_entry */
	0x0010, /* unit */
	0x007C, /* unit */
	0x00D4, /* unit */
	0x0110, /* unit */
	0x0130, /* unit */
	0x013C, /* unit */
	0x0154, /* unit */
	0x0160, /* unit */
	0x016C, /* unit */
	0x0050, /* unit_camera */
	0x0004, /* unit_camera_track */
	0x002C, /* unit_definition */
	0x003C, /* unit_definition */
	0x0074, /* unit_definition */
	0x0084, /* unit_definition */
	0x0094, /* unit_definition */
	0x00A4, /* unit_definition */
	0x0144, /* unit_definition */
	0x0150, /* unit_definition */
	0x015C, /* unit_definition */
	0x0168, /* unit_definition */
	0x0174, /* unit_definition */
	0x018C, /* unit_definition */
	0x01F8, /* unit_definition */
	0x0250, /* unit_definition */
	0x028C, /* unit_definition */
	0x02AC, /* unit_definition */
	0x02B8, /* unit_definition */
	0x02D0, /* unit_definition */
	0x02DC, /* unit_definition */
	0x02E8, /* unit_definition */
	0x000C, /* unit_dialogue_variant */
	0x0004, /* unit_hud */
	0x004C, /* unit_hud_interface */
	0x0080, /* unit_hud_interface */
	0x00B4, /* unit_hud_interface */
	0x00E8, /* unit_hud_interface */
	0x011C, /* unit_hud_interface */
	0x0150, /* unit_hud_interface */
	0x01A4, /* unit_hud_interface */
	0x01D8, /* unit_hud_interface */
	0x020C, /* unit_hud_interface */
	0x0240, /* unit_hud_interface */
	0x0294, /* unit_hud_interface */
	0x02C8, /* unit_hud_interface */
	0x02FC, /* unit_hud_interface */
	0x0330, /* unit_hud_interface */
	0x03A8, /* unit_hud_interface */
	0x03C4, /* unit_hud_interface */
	0x03D0, /* unit_hud_interface */
	0x0004, /* unit_initial_weapon */
	0x00D4, /* unit_seat */
	0x00E0, /* unit_seat */
	0x00FC, /* unit_seat */
	0x0060, /* vehicle_animation */
	0x006C, /* vehicle_animation */
	0x002C, /* vehicle_definition */
	0x003C, /* vehicle_definition */
	0x0074, /* vehicle_definition */
	0x0084, /* vehicle_definition */
	0x0094, /* vehicle_definition */
	0x00A4, /* vehicle_definition */
	0x0144, /* vehicle_definition */
	0x0150, /* vehicle_definition */
	0x015C, /* vehicle_definition */
	0x0168, /* vehicle_definition */
	0x0174, /* vehicle_definition */
	0x018C, /* vehicle_definition */
	0x01F8, /* vehicle_definition */
	0x0250, /* vehicle_definition */
	0x028C, /* vehicle_definition */
	0x02AC, /* vehicle_definition */
	0x02B8, /* vehicle_definition */
	0x02D0, /* vehicle_definition */
	0x02DC, /* vehicle_definition */
	0x02E8, /* vehicle_definition */
	0x03B4, /* vehicle_definition */
	0x03C4, /* vehicle_definition */
	0x03D4, /* vehicle_definition */
	0x03E4, /* vehicle_definition */
	0x0004, /* vehicle_palette */
	0x0004, /* virtual_keyboard */
	0x0014, /* virtual_keyboard */
	0x0024, /* virtual_keyboard */
	0x0034, /* virtual_keyboard */
	0x0014, /* virtual_keyboard_key */
	0x0024, /* virtual_keyboard_key */
	0x0034, /* virtual_keyboard_key */
	0x0044, /* virtual_keyboard_key */
	0x0038, /* weapon */
	0x0070, /* weapon */
	0x0080, /* weapon */
	0x0090, /* weapon */
	0x00A0, /* weapon */
	0x00B8, /* weapon */
	0x011C, /* weapon */
	0x012C, /* weapon */
	0x0158, /* weapon */
	0x0168, /* weapon */
	0x017C, /* weapon */
	0x018C, /* weapon */
	0x019C, /* weapon */
	0x01AC, /* weapon */
	0x01BC, /* weapon */
	0x01E0, /* weapon */
	0x01EC, /* weapon */
	0x01F8, /* weapon */
	0x0010, /* weapon_ammunition_object */
	0x002C, /* weapon_definition */
	0x003C, /* weapon_definition */
	0x0074, /* weapon_definition */
	0x0084, /* weapon_definition */
	0x0094, /* weapon_definition */
	0x00A4, /* weapon_definition */
	0x0144, /* weapon_definition */
	0x0150, /* weapon_definition */
	0x015C, /* weapon_definition */
	0x0168, /* weapon_definition */
	0x0174, /* weapon_definition */
	0x024C, /* weapon_definition */
	0x025C, /* weapon_definition */
	0x02EC, /* weapon_definition */
	0x02FC, /* weapon_definition */
	0x0340, /* weapon_definition */
	0x0378, /* weapon_definition */
	0x0388, /* weapon_definition */
	0x0398, /* weapon_definition */
	0x03A8, /* weapon_definition */
	0x03C0, /* weapon_definition */
	0x0424, /* weapon_definition */
	0x0434, /* weapon_definition */
	0x0460, /* weapon_definition */
	0x0470, /* weapon_definition */
	0x0484, /* weapon_definition */
	0x0494, /* weapon_definition */
	0x04A4, /* weapon_definition */
	0x04B4, /* weapon_definition */
	0x04C4, /* weapon_definition */
	0x04E8, /* weapon_definition */
	0x04F4, /* weapon_definition */
	0x0500, /* weapon_definition */
	0x0028, /* weapon_firing_effect */
	0x0038, /* weapon_firing_effect */
	0x0048, /* weapon_firing_effect */
	0x0058, /* weapon_firing_effect */
	0x0068, /* weapon_firing_effect */
	0x0078, /* weapon_firing_effect */
	0x0028, /* weapon_hud_crosshairs_element */
	0x0038, /* weapon_hud_crosshairs_element */
	0x0004, /* weapon_hud_interface */
	0x0064, /* weapon_hud_interface */
	0x0070, /* weapon_hud_interface */
	0x007C, /* weapon_hud_interface */
	0x0088, /* weapon_hud_interface */
	0x0094, /* weapon_hud_interface */
	0x00A4, /* weapon_hud_interface */
	0x00B0, /* weapon_hud_interface */
	0x004C, /* weapon_hud_meter_element */
	0x0080, /* weapon_hud_meter_element */
	0x0004, /* weapon_hud_overlay */
	0x0014, /* weapon_hud_overlay */
	0x0028, /* weapon_hud_overlays_element */
	0x0038, /* weapon_hud_overlays_element */
	0x004C, /* weapon_hud_static_element */
	0x0080, /* weapon_hud_static_element */
	0x003C, /* weapon_magazine */
	0x004C, /* weapon_magazine */
	0x0068, /* weapon_magazine */
	0x0004, /* weapon_palette */
	0x0060, /* weapon_trigger */
	0x0098, /* weapon_trigger */
	0x010C, /* weapon_trigger */
	0x0028, /* weather_particle_system */
	0x00B0, /* weather_particle_type */
	0x0198, /* weather_particle_type */
	0x01F8, /* weather_particle_type */
};

/* the definitions, by name and then size: their runs, fields and data
handlers (first, count) and their hook */
static struct tag_swap_layout const tag_swap_layouts[] =
{
	{ "actor_definition", 0x4F8, 0, 13, 0, 0, 0, 0, 0, 3, _tag_swap_hook_none },
	{ "actor_palette", 0x10, 13, 1, 0, 0, 0, 0, 3, 1, _tag_swap_hook_none },
	{ "actor_starting_location", 0x1C, 14, 3, 0, 0, 0, 0, 4, 0, _tag_swap_hook_none },
	{ "actor_variant_change_colors", 0x20, 17, 1, 0, 0, 0, 0, 4, 0, _tag_swap_hook_none },
	{ "actor_variant_definition", 0x238, 18, 13, 0, 1, 0, 0, 4, 6, _tag_swap_hook_none },
	{ "ai_animation_reference", 0x3C, 31, 1, 1, 0, 0, 0, 10, 1, _tag_swap_hook_none },
	{ "ai_command", 0x20, 32, 4, 1, 0, 0, 0, 11, 0, _tag_swap_hook_none },
	{ "ai_command_list", 0x60, 36, 3, 1, 2, 0, 0, 11, 4, _tag_swap_hook_none },
	{ "ai_command_point", 0x14, 39, 1, 3, 0, 0, 0, 15, 0, _tag_swap_hook_none },
	{ "ai_recording_reference", 0x28, 40, 1, 3, 0, 0, 0, 15, 0, _tag_swap_hook_none },
	{ "ai_script_reference", 0x28, 41, 1, 3, 0, 0, 0, 15, 0, _tag_swap_hook_none },
	{ "animation", 0xB4, 42, 7, 3, 3, 0, 3, 15, 3, _tag_swap_hook_animation },
	{ "animation_graph", 0x80, 49, 3, 6, 10, 3, 0, 18, 10, _tag_swap_hook_none },
	{ "animation_graph_device_animations", 0x60, 52, 1, 16, 1, 3, 0, 28, 1, _tag_swap_hook_none },
	{ "animation_graph_first_person_weapon_animations", 0x1C, 53, 1, 17, 1, 3, 0, 29, 1, _tag_swap_hook_none },
	{ "animation_graph_ik_point", 0x40, 54, 0, 18, 0, 3, 0, 30, 0, _tag_swap_hook_none },
	{ "animation_graph_node", 0x40, 54, 2, 18, 0, 3, 0, 30, 0, _tag_swap_hook_none },
	{ "animation_graph_object_overlay", 0x14, 56, 2, 18, 0, 3, 0, 30, 0, _tag_swap_hook_none },
	{ "animation_graph_sound_reference", 0x14, 58, 1, 18, 0, 3, 0, 30, 1, _tag_swap_hook_none },
	{ "animation_graph_unit_seat", 0x64, 59, 5, 18, 3, 3, 0, 31, 3, _tag_swap_hook_none },
	{ "animation_graph_weapon_animations", 0x1C, 64, 1, 21, 1, 3, 0, 34, 1, _tag_swap_hook_none },
	{ "animation_graph_weapon_class", 0xBC, 65, 5, 22, 3, 3, 0, 35, 3, _tag_swap_hook_none },
	{ "animation_graph_weapon_type", 0x3C, 70, 1, 25, 1, 3, 0, 38, 1, _tag_swap_hook_none },
	{ "animation_index", 0x2, 71, 1, 26, 0, 3, 0, 39, 0, _tag_swap_hook_none },
	{ "antenna", 0xD0, 72, 1, 26, 1, 3, 0, 39, 3, _tag_swap_hook_none },
	{ "antenna_vertex", 0x80, 73, 3, 27, 0, 3, 0, 42, 0, _tag_swap_hook_none },
	{ "auxilary_meter", 0x144, 76, 17, 27, 1, 3, 0, 42, 4, _tag_swap_hook_none },
	{ "auxilary_overlay", 0x84, 93, 10, 28, 1, 3, 0, 46, 2, _tag_swap_hook_none },
	{ "biped", 0x78, 103, 5, 29, 0, 3, 0, 48, 0, _tag_swap_hook_none },
	{ "biped", 0x204, 108, 5, 29, 1, 3, 0, 48, 3, _tag_swap_hook_none },
	{ "biped_contact_point", 0x40, 113, 0, 30, 0, 3, 0, 51, 0, _tag_swap_hook_none },
	{ "biped_definition", 0x4F4, 113, 21, 30, 3, 3, 0, 51, 23, _tag_swap_hook_none },
	{ "biped_palette", 0x30, 134, 1, 33, 0, 3, 0, 74, 1, _tag_swap_hook_none },
	{ "bitmap_data", 0x30, 135, 3, 33, 0, 3, 0, 75, 2, _tag_swap_hook_none },
	{ "bitmap_group", 0x6C, 138, 6, 33, 2, 3, 0, 77, 4, _tag_swap_hook_none },
	{ "bitmap_sequence", 0x40, 144, 2, 35, 1, 3, 0, 81, 1, _tag_swap_hook_none },
	{ "bitmap_sprite", 0x20, 146, 2, 36, 0, 3, 0, 82, 0, _tag_swap_hook_none },
	{ "breakable_surface_particle_effect", 0x80, 148, 1, 36, 0, 3, 0, 82, 1, _tag_swap_hook_none },
	{ "bsp2d_node", 0x14, 149, 1, 36, 0, 3, 0, 83, 0, _tag_swap_hook_none },
	{ "bsp2d_reference", 0x8, 150, 1, 36, 0, 3, 0, 83, 0, _tag_swap_hook_none },
	{ "bsp3d_node", 0xC, 151, 1, 36, 0, 3, 0, 83, 0, _tag_swap_hook_none },
	{ "bsp_switch_trigger_volume", 0x8, 152, 1, 36, 0, 3, 0, 83, 0, _tag_swap_hook_none },
	{ "byte", 0x1, 153, 0, 36, 0, 3, 0, 83, 0, _tag_swap_hook_none },
	{ "camera_track", 0x30, 153, 1, 36, 1, 3, 0, 83, 1, _tag_swap_hook_none },
	{ "camera_track_control_point", 0x3C, 154, 1, 37, 0, 3, 0, 84, 0, _tag_swap_hook_none },
	{ "collision_bsp", 0x60, 155, 1, 37, 8, 3, 0, 84, 8, _tag_swap_hook_none },
	{ "collision_edge", 0x18, 156, 1, 45, 0, 3, 0, 92, 0, _tag_swap_hook_none },
	{ "collision_leaf", 0x8, 157, 2, 45, 0, 3, 0, 92, 0, _tag_swap_hook_none },
	{ "collision_model", 0x298, 159, 7, 45, 3, 3, 0, 92, 13, _tag_swap_hook_none },
	{ "collision_node", 0x40, 166, 4, 48, 1, 3, 0, 105, 1, _tag_swap_hook_none },
	{ "collision_surface", 0xC, 170, 2, 49, 0, 3, 0, 106, 0, _tag_swap_hook_none },
	{ "collision_vertex", 0x10, 172, 1, 49, 0, 3, 0, 106, 0, _tag_swap_hook_none },
	{ "color_table", 0xC, 173, 1, 49, 1, 3, 0, 106, 1, _tag_swap_hook_none },
	{ "color_table_color", 0x30, 174, 1, 50, 0, 3, 0, 107, 0, _tag_swap_hook_none },
	{ "continuous_damage_effect", 0x200, 175, 5, 50, 1, 3, 0, 107, 0, _tag_swap_hook_none },
	{ "contrail", 0x144, 180, 11, 51, 2, 3, 0, 107, 2, _tag_swap_hook_none },
	{ "contrail_point_state", 0x68, 191, 1, 53, 0, 3, 0, 109, 1, _tag_swap_hook_none },
	{ "control", 0x40, 192, 7, 53, 0, 3, 0, 110, 0, _tag_swap_hook_none },
	{ "control", 0x88, 199, 3, 53, 0, 3, 0, 110, 3, _tag_swap_hook_none },
	{ "control_definition", 0x318, 202, 11, 53, 3, 3, 0, 113, 21, _tag_swap_hook_none },
	{ "control_palette", 0x30, 213, 1, 56, 0, 3, 0, 134, 1, _tag_swap_hook_none },
	{ "conversation", 0x74, 214, 3, 56, 2, 3, 0, 135, 3, _tag_swap_hook_none },
	{ "conversation_line", 0x7C, 217, 3, 58, 0, 3, 0, 138, 6, _tag_swap_hook_none },
	{ "conversation_participant", 0x54, 220, 3, 58, 0, 3, 0, 144, 0, _tag_swap_hook_none },
	{ "cutscene_camera_point", 0x68, 223, 2, 58, 0, 3, 0, 144, 0, _tag_swap_hook_none },
	{ "cutscene_flag", 0x5C, 225, 2, 58, 0, 3, 0, 144, 0, _tag_swap_hook_none },
	{ "cutscene_title", 0x60, 227, 4, 58, 0, 3, 0, 144, 0, _tag_swap_hook_none },
	{ "damage", 0xDC, 231, 2, 58, 0, 3, 0, 144, 0, _tag_swap_hook_none },
	{ "damage_effect", 0x2A0, 233, 17, 58, 2, 3, 0, 144, 1, _tag_swap_hook_none },
	{ "damage_permutation", 0x20, 250, 0, 60, 0, 3, 0, 145, 0, _tag_swap_hook_none },
	{ "damage_region", 0x54, 250, 1, 60, 1, 3, 0, 145, 2, _tag_swap_hook_none },
	{ "damage_resistance", 0x258, 251, 7, 61, 2, 3, 0, 147, 11, _tag_swap_hook_none },
	{ "damage_resistance_material", 0x48, 258, 3, 63, 0, 3, 0, 158, 0, _tag_swap_hook_none },
	{ "decal", 0x10C, 261, 10, 63, 0, 3, 0, 158, 2, _tag_swap_hook_none },
	{ "decal_palette", 0x10, 271, 1, 63, 0, 3, 0, 160, 1, _tag_swap_hook_none },
	{ "detail_object", 0x6, 272, 1, 63, 0, 3, 0, 161, 0, _tag_swap_hook_none },
	{ "detail_object_cell", 0x20, 273, 2, 63, 0, 3, 0, 161, 0, _tag_swap_hook_none },
	{ "detail_object_collection", 0x80, 275, 2, 63, 1, 3, 0, 161, 2, _tag_swap_hook_none },
	{ "detail_object_collection_palette", 0x30, 277, 1, 64, 0, 3, 0, 163, 1, _tag_swap_hook_none },
	{ "detail_object_count", 0x2, 278, 1, 64, 0, 3, 0, 164, 0, _tag_swap_hook_none },
	{ "detail_object_type", 0x60, 279, 1, 64, 0, 3, 0, 164, 0, _tag_swap_hook_none },
	{ "detail_object_z_reference_vector", 0x10, 280, 1, 64, 0, 3, 0, 164, 0, _tag_swap_hook_none },
	{ "device", 0x114, 281, 3, 64, 0, 3, 0, 164, 7, _tag_swap_hook_none },
	{ "device_definition", 0x290, 284, 8, 64, 2, 3, 0, 171, 18, _tag_swap_hook_none },
	{ "device_group", 0x34, 292, 1, 66, 0, 3, 0, 189, 0, _tag_swap_hook_none },
	{ "dialogue_definition", 0x1010, 293, 2, 66, 0, 3, 0, 189, 256, _tag_swap_hook_none },
	{ "effect", 0x40, 295, 3, 66, 2, 3, 0, 445, 2, _tag_swap_hook_none },
	{ "effect_event", 0x44, 298, 1, 68, 2, 3, 0, 447, 2, _tag_swap_hook_none },
	{ "effect_location", 0x20, 299, 0, 70, 0, 3, 0, 449, 0, _tag_swap_hook_none },
	{ "effect_part", 0x68, 299, 2, 70, 0, 3, 0, 449, 1, _tag_swap_hook_none },
	{ "effect_particles", 0xE8, 301, 4, 70, 0, 3, 0, 450, 1, _tag_swap_hook_none },
	{ "encounter", 0xB0, 305, 5, 70, 4, 3, 0, 451, 4, _tag_swap_hook_none },
	{ "encounter_player_starting_location", 0x34, 310, 2, 74, 0, 3, 0, 455, 0, _tag_swap_hook_none },
	{ "equipment", 0x18, 312, 2, 74, 0, 3, 0, 455, 1, _tag_swap_hook_none },
	{ "equipment", 0x28, 314, 4, 74, 0, 3, 0, 456, 0, _tag_swap_hook_none },
	{ "equipment_definition", 0x320, 318, 14, 74, 3, 3, 0, 456, 16, _tag_swap_hook_none },
	{ "equipment_palette", 0x30, 332, 1, 77, 0, 3, 0, 472, 1, _tag_swap_hook_none },
	{ "firing_position", 0x18, 333, 3, 77, 0, 3, 0, 473, 0, _tag_swap_hook_none },
	{ "flag", 0x60, 336, 3, 77, 1, 3, 0, 473, 4, _tag_swap_hook_none },
	{ "flag_attachment_point", 0x34, 339, 2, 78, 0, 3, 0, 477, 0, _tag_swap_hook_none },
	{ "fog", 0x18C, 341, 11, 78, 0, 3, 0, 477, 3, _tag_swap_hook_none },
	{ "font", 0x9C, 352, 3, 78, 3, 3, 1, 480, 7, _tag_swap_hook_none },
	{ "font_character", 0x14, 355, 2, 81, 0, 4, 0, 487, 0, _tag_swap_hook_none },
	{ "font_character_index", 0x2, 357, 1, 81, 0, 4, 0, 487, 0, _tag_swap_hook_none },
	{ "font_character_table", 0xC, 358, 1, 81, 1, 4, 0, 487, 1, _tag_swap_hook_none },
	{ "game_globals", 0x1AC, 359, 1, 82, 14, 4, 0, 488, 15, _tag_swap_hook_none },
	{ "game_globals_difficulty_information", 0x284, 360, 1, 96, 0, 4, 0, 503, 0, _tag_swap_hook_none },
	{ "game_globals_falling_damage", 0x98, 361, 1, 96, 0, 4, 0, 503, 6, _tag_swap_hook_none },
	{ "game_globals_first_person_interface", 0xC0, 362, 5, 96, 0, 4, 0, 509, 6, _tag_swap_hook_none },
	{ "game_globals_grenade", 0x44, 367, 2, 96, 0, 4, 0, 515, 4, _tag_swap_hook_none },
	{ "game_globals_interface_tag_references", 0x130, 369, 1, 96, 0, 4, 0, 519, 16, _tag_swap_hook_none },
	{ "game_globals_multiplayer_information", 0xA0, 370, 1, 96, 2, 4, 0, 535, 7, _tag_swap_hook_none },
	{ "game_globals_player_control", 0x80, 371, 3, 98, 1, 4, 0, 542, 1, _tag_swap_hook_none },
	{ "game_globals_player_information", 0xF4, 374, 1, 99, 0, 4, 0, 543, 2, _tag_swap_hook_none },
	{ "game_globals_rasterizer_data", 0x1AC, 375, 3, 99, 0, 4, 0, 545, 17, _tag_swap_hook_none },
	{ "game_globals_vehicle", 0x10, 378, 1, 99, 0, 4, 0, 562, 1, _tag_swap_hook_none },
	{ "garbage_definition", 0x308, 379, 12, 99, 2, 4, 0, 563, 15, _tag_swap_hook_none },
	{ "gbxmodel", 0xE8, 391, 3, 101, 5, 4, 0, 578, 5, _tag_swap_hook_none },
	{ "gbxmodel_geometry", 0x30, 394, 1, 106, 1, 4, 0, 583, 1, _tag_swap_hook_none },
	{ "gbxmodel_geometry_part", 0x84, 395, 9, 107, 3, 4, 0, 584, 3, _tag_swap_hook_none },
	{ "glow", 0x154, 404, 16, 110, 0, 4, 0, 587, 1, _tag_swap_hook_none },
	{ "grenade_count_panel", 0xC0, 420, 17, 110, 1, 4, 0, 588, 2, _tag_swap_hook_none },
	{ "grenade_hud_interface", 0x1F8, 437, 28, 111, 4, 4, 0, 590, 8, _tag_swap_hook_none },
	{ "hs_global", 0x5C, 465, 2, 115, 0, 4, 0, 598, 0, _tag_swap_hook_none },
	{ "hs_reference", 0x28, 467, 1, 115, 0, 4, 0, 598, 1, _tag_swap_hook_none },
	{ "hs_script", 0x5C, 468, 2, 115, 0, 4, 0, 599, 0, _tag_swap_hook_none },
	{ "hs_source_file", 0x34, 470, 1, 115, 1, 4, 1, 599, 1, _tag_swap_hook_none },
	{ "hud_absolute_placement", 0x24, 471, 2, 116, 0, 5, 0, 600, 0, _tag_swap_hook_none },
	{ "hud_globals", 0x450, 473, 30, 116, 3, 5, 0, 600, 13, _tag_swap_hook_none },
	{ "hud_message_text", 0x80, 503, 1, 119, 3, 5, 1, 613, 3, _tag_swap_hook_none },
	{ "hud_number", 0x64, 504, 3, 122, 0, 6, 0, 616, 1, _tag_swap_hook_none },
	{ "hud_screen_effect", 0xB8, 507, 9, 122, 0, 6, 0, 617, 2, _tag_swap_hook_none },
	{ "hud_sound", 0x38, 516, 1, 122, 0, 6, 0, 619, 1, _tag_swap_hook_none },
	{ "hud_state_message", 0x40, 517, 2, 122, 0, 6, 0, 620, 0, _tag_swap_hook_none },
	{ "hud_state_message_element", 0x2, 519, 0, 122, 0, 6, 0, 620, 0, _tag_swap_hook_none },
	{ "hud_waypoint_arrow", 0x68, 519, 3, 122, 0, 6, 0, 620, 0, _tag_swap_hook_none },
	{ "icon_hud_element", 0x10, 522, 3, 122, 0, 6, 0, 620, 0, _tag_swap_hook_none },
	{ "inertial_matrix", 0x24, 525, 1, 122, 0, 6, 0, 620, 0, _tag_swap_hook_none },
	{ "item", 0x18C, 526, 7, 122, 0, 6, 0, 620, 4, _tag_swap_hook_none },
	{ "item_collection_definition", 0x5C, 533, 3, 122, 1, 6, 0, 624, 1, _tag_swap_hook_none },
	{ "item_definition", 0x308, 536, 12, 123, 2, 6, 0, 625, 15, _tag_swap_hook_none },
	{ "item_permutation", 0x54, 548, 1, 125, 0, 6, 0, 640, 1, _tag_swap_hook_none },
	{ "leaf_map", 0x1C, 549, 1, 125, 2, 6, 0, 641, 2, _tag_swap_hook_none },
	{ "leaf_portal", 0x18, 550, 1, 127, 1, 6, 0, 643, 1, _tag_swap_hook_none },
	{ "lens_flare", 0xF0, 551, 8, 128, 1, 6, 0, 644, 2, _tag_swap_hook_none },
	{ "lens_flare_reflection", 0x80, 559, 9, 129, 0, 6, 0, 646, 0, _tag_swap_hook_none },
	{ "light", 0x160, 568, 14, 129, 0, 6, 0, 646, 3, _tag_swap_hook_none },
	{ "light_fixture", 0x58, 582, 6, 129, 0, 6, 0, 649, 0, _tag_swap_hook_none },
	{ "light_fixture_definition", 0x290, 588, 8, 129, 2, 6, 0, 649, 18, _tag_swap_hook_none },
	{ "light_fixture_palette", 0x30, 596, 1, 131, 0, 6, 0, 667, 1, _tag_swap_hook_none },
	{ "light_volume", 0x14C, 597, 8, 131, 1, 6, 0, 668, 2, _tag_swap_hook_none },
	{ "light_volume_frame", 0xB0, 605, 1, 132, 0, 6, 0, 670, 0, _tag_swap_hook_none },
	{ "lightning", 0x108, 606, 5, 132, 2, 6, 0, 670, 3, _tag_swap_hook_none },
	{ "lightning_marker", 0xE4, 611, 2, 134, 0, 6, 0, 673, 0, _tag_swap_hook_none },
	{ "lightning_shader", 0xB4, 613, 10, 134, 1, 6, 0, 673, 1, _tag_swap_hook_none },
	{ "long", 0x4, 623, 1, 135, 0, 6, 0, 674, 0, _tag_swap_hook_none },
	{ "look_function_value", 0x4, 624, 1, 135, 0, 6, 0, 674, 0, _tag_swap_hook_none },
	{ "looping_sound", 0x54, 625, 1, 135, 2, 6, 0, 674, 3, _tag_swap_hook_none },
	{ "looping_sound_detail", 0x68, 626, 1, 137, 0, 6, 0, 677, 1, _tag_swap_hook_none },
	{ "looping_sound_track", 0xA0, 627, 1, 137, 0, 6, 0, 678, 5, _tag_swap_hook_none },
	{ "machine", 0x40, 628, 6, 137, 0, 6, 0, 683, 0, _tag_swap_hook_none },
	{ "machine", 0x94, 634, 4, 137, 0, 6, 0, 683, 0, _tag_swap_hook_none },
	{ "machine_definition", 0x324, 638, 12, 137, 3, 6, 0, 683, 18, _tag_swap_hook_none },
	{ "machine_palette", 0x30, 650, 1, 140, 0, 6, 0, 701, 1, _tag_swap_hook_none },
	{ "map_leaf", 0x18, 651, 1, 140, 2, 6, 0, 702, 2, _tag_swap_hook_none },
	{ "map_leaf_face", 0x10, 652, 1, 142, 1, 6, 0, 704, 1, _tag_swap_hook_none },
	{ "mass_point", 0x80, 653, 4, 143, 0, 6, 0, 705, 0, _tag_swap_hook_none },
	{ "material", 0x374, 657, 1, 143, 1, 6, 0, 705, 4, _tag_swap_hook_none },
	{ "material_effect", 0x1C, 658, 1, 144, 1, 6, 0, 709, 1, _tag_swap_hook_none },
	{ "material_effect_material", 0x30, 659, 1, 145, 0, 6, 0, 710, 2, _tag_swap_hook_none },
	{ "material_effects", 0xC, 660, 1, 145, 1, 6, 0, 712, 1, _tag_swap_hook_none },
	{ "meter_hud_element", 0x68, 661, 7, 146, 1, 6, 0, 713, 2, _tag_swap_hook_none },
	{ "metered_panel", 0xF0, 668, 15, 147, 2, 6, 0, 715, 4, _tag_swap_hook_none },
	{ "model", 0xE8, 683, 3, 149, 5, 6, 0, 719, 5, _tag_swap_hook_none },
	{ "model_geometry", 0x30, 686, 1, 154, 1, 6, 0, 724, 1, _tag_swap_hook_none },
	{ "model_geometry_part", 0x68, 687, 8, 155, 3, 6, 0, 725, 7, _tag_swap_hook_model_geometry_part },
	{ "model_marker", 0x40, 695, 1, 158, 1, 6, 0, 732, 1, _tag_swap_hook_none },
	{ "model_marker_instance", 0x20, 696, 1, 159, 0, 6, 0, 733, 0, _tag_swap_hook_none },
	{ "model_node", 0x9C, 697, 2, 159, 0, 6, 0, 733, 0, _tag_swap_hook_none },
	{ "model_region", 0x4C, 699, 1, 159, 1, 6, 0, 733, 1, _tag_swap_hook_none },
	{ "model_region_permutation", 0x58, 700, 5, 160, 1, 6, 0, 734, 1, _tag_swap_hook_none },
	{ "model_region_permutation_marker", 0x50, 705, 2, 161, 0, 6, 0, 735, 0, _tag_swap_hook_none },
	{ "model_shader_reference", 0x20, 707, 3, 161, 0, 6, 0, 735, 1, _tag_swap_hook_none },
	{ "model_triangle", 0x6, 710, 1, 161, 0, 6, 0, 736, 0, _tag_swap_hook_none },
	{ "model_vertex_compressed", 0x20, 711, 0, 161, 0, 6, 0, 736, 0, _tag_swap_hook_none },
	{ "model_vertex_uncompressed", 0x44, 711, 0, 161, 0, 6, 0, 736, 0, _tag_swap_hook_none },
	{ "motion_sensor_panel", 0xF0, 711, 16, 161, 2, 6, 0, 736, 4, _tag_swap_hook_none },
	{ "move_position", 0x50, 727, 5, 163, 0, 6, 0, 740, 0, _tag_swap_hook_none },
	{ "multiplayer_scenario_description", 0x44, 732, 2, 163, 0, 6, 0, 740, 2, _tag_swap_hook_none },
	{ "multiplayer_scenario_descriptions", 0xC, 734, 1, 163, 1, 6, 0, 742, 1, _tag_swap_hook_none },
	{ "multitexture_overlay", 0x1E0, 735, 6, 164, 1, 6, 0, 743, 4, _tag_swap_hook_none },
	{ "multitexture_overlay_effector", 0xDC, 741, 5, 165, 0, 6, 0, 747, 0, _tag_swap_hook_none },
	{ "netgame_equipment", 0x90, 746, 3, 165, 0, 6, 0, 747, 1, _tag_swap_hook_none },
	{ "netgame_flag", 0x94, 749, 3, 165, 0, 6, 0, 748, 0, _tag_swap_hook_none },
	{ "object", 0x17C, 752, 6, 165, 5, 6, 0, 748, 11, _tag_swap_hook_none },
	{ "object_attachment", 0x48, 758, 3, 170, 0, 6, 0, 759, 1, _tag_swap_hook_none },
	{ "object_change_color", 0x2C, 761, 2, 170, 1, 6, 0, 760, 1, _tag_swap_hook_none },
	{ "object_change_color_permutation", 0x1C, 763, 1, 171, 0, 6, 0, 761, 0, _tag_swap_hook_none },
	{ "object_definition", 0x17C, 764, 6, 171, 1, 6, 0, 761, 11, _tag_swap_hook_none },
	{ "object_function", 0x168, 770, 7, 172, 0, 6, 0, 772, 0, _tag_swap_hook_none },
	{ "object_name", 0x24, 777, 1, 172, 0, 6, 0, 772, 0, _tag_swap_hook_none },
	{ "object_widget", 0x20, 778, 1, 172, 0, 6, 0, 772, 1, _tag_swap_hook_none },
	{ "optional_meter_hud_element", 0x68, 779, 7, 172, 1, 6, 0, 773, 2, _tag_swap_hook_none },
	{ "optional_static_hud_element", 0x68, 786, 8, 173, 1, 6, 0, 775, 2, _tag_swap_hook_none },
	{ "particle", 0x164, 794, 13, 174, 1, 6, 0, 777, 6, _tag_swap_hook_none },
	{ "particle_system", 0x68, 807, 3, 175, 1, 6, 0, 783, 3, _tag_swap_hook_none },
	{ "particle_system_particle_state", 0x178, 810, 13, 176, 1, 6, 0, 786, 3, _tag_swap_hook_none },
	{ "particle_system_physics_constant", 0x4, 823, 1, 177, 0, 6, 0, 789, 0, _tag_swap_hook_none },
	{ "particle_system_type", 0x80, 824, 5, 177, 3, 6, 0, 789, 3, _tag_swap_hook_none },
	{ "particle_system_type_state", 0xC0, 829, 3, 180, 1, 6, 0, 792, 1, _tag_swap_hook_none },
	{ "pathfinding_sphere", 0x20, 832, 2, 181, 0, 6, 0, 793, 0, _tag_swap_hook_none },
	{ "physics", 0x80, 834, 1, 181, 3, 6, 0, 793, 3, _tag_swap_hook_none },
	{ "placeholder_definition", 0x17C, 835, 6, 184, 1, 6, 0, 796, 11, _tag_swap_hook_none },
	{ "platoon", 0xAC, 841, 5, 185, 2, 6, 0, 807, 0, _tag_swap_hook_none },
	{ "platoon_rule", 0x8, 846, 2, 187, 0, 6, 0, 807, 0, _tag_swap_hook_none },
	{ "player_starting_location", 0x34, 848, 2, 187, 0, 6, 0, 807, 0, _tag_swap_hook_none },
	{ "point_physics", 0x40, 850, 1, 187, 0, 6, 0, 807, 0, _tag_swap_hook_none },
	{ "powered_mass_point", 0x80, 851, 1, 187, 0, 6, 0, 807, 0, _tag_swap_hook_none },
	{ "predicted_resource", 0x8, 852, 2, 187, 0, 6, 0, 807, 0, _tag_swap_hook_none },
	{ "projectile", 0xD0, 854, 5, 187, 1, 6, 0, 807, 7, _tag_swap_hook_none },
	{ "projectile_definition", 0x24C, 859, 10, 188, 2, 6, 0, 814, 18, _tag_swap_hook_none },
	{ "projectile_material_response", 0xA0, 869, 7, 190, 0, 6, 0, 832, 3, _tag_swap_hook_none },
	{ "real_plane3d", 0x10, 876, 1, 190, 0, 6, 0, 835, 0, _tag_swap_hook_none },
	{ "real_point2d", 0x8, 877, 1, 190, 0, 6, 0, 835, 0, _tag_swap_hook_none },
	{ "real_point3d", 0xC, 878, 1, 190, 0, 6, 0, 835, 0, _tag_swap_hook_none },
	{ "recorded_animation", 0x40, 879, 2, 190, 1, 6, 1, 835, 1, _tag_swap_hook_recorded_animation },
	{ "render_lighting", 0x74, 881, 5, 191, 0, 7, 0, 836, 0, _tag_swap_hook_none },
	{ "scenario", 0x5B0, 886, 3, 191, 49, 7, 2, 836, 67, _tag_swap_hook_none },
	{ "scenery", 0x48, 889, 5, 240, 0, 9, 0, 903, 0, _tag_swap_hook_none },
	{ "scenery_definition", 0x17C, 894, 6, 240, 1, 9, 0, 903, 11, _tag_swap_hook_none },
	{ "scenery_palette", 0x30, 900, 1, 241, 0, 9, 0, 914, 1, _tag_swap_hook_none },
	{ "shader", 0x28, 901, 3, 241, 0, 9, 0, 915, 0, _tag_swap_hook_none },
	{ "shader_definition", 0x28, 904, 3, 241, 0, 9, 0, 915, 0, _tag_swap_hook_none },
	{ "shader_effect", 0xB4, 907, 10, 241, 2, 9, 0, 915, 1, _tag_swap_hook_none },
	{ "shader_environment", 0x344, 917, 30, 243, 1, 9, 0, 916, 8, _tag_swap_hook_none },
	{ "shader_layer", 0x10, 947, 1, 244, 0, 9, 0, 924, 1, _tag_swap_hook_none },
	{ "shader_model", 0x1B8, 948, 18, 244, 2, 9, 0, 925, 4, _tag_swap_hook_none },
	{ "shader_texture_animation", 0x38, 966, 6, 246, 0, 9, 0, 929, 0, _tag_swap_hook_none },
	{ "shader_transparent", 0x38, 972, 2, 246, 0, 9, 0, 929, 3, _tag_swap_hook_none },
	{ "shader_transparent_chicago", 0x6C, 974, 5, 246, 4, 9, 0, 932, 3, _tag_swap_hook_none },
	{ "shader_transparent_chicago_extended", 0x78, 979, 5, 250, 5, 9, 0, 935, 4, _tag_swap_hook_none },
	{ "shader_transparent_chicago_map", 0xDC, 984, 9, 255, 1, 9, 0, 939, 1, _tag_swap_hook_none },
	{ "shader_transparent_generic", 0x6C, 993, 5, 256, 5, 9, 0, 940, 4, _tag_swap_hook_none },
	{ "shader_transparent_generic_map", 0x64, 998, 8, 261, 1, 9, 0, 944, 1, _tag_swap_hook_none },
	{ "shader_transparent_generic_stage", 0x70, 1006, 3, 262, 0, 9, 0, 945, 0, _tag_swap_hook_none },
	{ "shader_transparent_glass", 0x1E0, 1009, 8, 262, 1, 9, 0, 945, 5, _tag_swap_hook_none },
	{ "shader_transparent_meter", 0x104, 1017, 6, 263, 1, 9, 0, 950, 1, _tag_swap_hook_none },
	{ "shader_transparent_plasma", 0x14C, 1023, 11, 264, 1, 9, 0, 951, 2, _tag_swap_hook_none },
	{ "shader_transparent_water", 0x140, 1034, 10, 265, 2, 9, 0, 953, 4, _tag_swap_hook_none },
	{ "sky", 0xD0, 1044, 4, 267, 3, 9, 0, 957, 6, _tag_swap_hook_none },
	{ "sky_animation", 0x24, 1048, 2, 270, 0, 9, 0, 963, 0, _tag_swap_hook_none },
	{ "sky_light", 0x74, 1050, 2, 270, 0, 9, 0, 963, 1, _tag_swap_hook_none },
	{ "sky_reference", 0x10, 1052, 1, 270, 0, 9, 0, 964, 1, _tag_swap_hook_none },
	{ "sky_shader_function", 0x24, 1053, 1, 270, 0, 9, 0, 965, 0, _tag_swap_hook_none },
	{ "sound", 0xA4, 1054, 9, 270, 1, 9, 0, 965, 2, _tag_swap_hook_none },
	{ "sound_environment", 0x48, 1063, 3, 271, 0, 9, 0, 967, 0, _tag_swap_hook_none },
	{ "sound_permutation", 0x7C, 1066, 3, 271, 3, 9, 2, 967, 3, _tag_swap_hook_none },
	{ "sound_pitch_range", 0x48, 1069, 5, 274, 1, 11, 0, 970, 1, _tag_swap_hook_none },
	{ "sound_scenery", 0x28, 1074, 4, 275, 0, 11, 0, 971, 0, _tag_swap_hook_none },
	{ "sound_scenery_definition", 0x17C, 1078, 6, 275, 1, 11, 0, 971, 11, _tag_swap_hook_none },
	{ "sound_scenery_palette", 0x30, 1084, 1, 276, 0, 11, 0, 982, 1, _tag_swap_hook_none },
	{ "squad", 0xE8, 1085, 8, 276, 2, 11, 0, 983, 3, _tag_swap_hook_none },
	{ "starting_equipment", 0xCC, 1093, 3, 278, 0, 11, 0, 986, 6, _tag_swap_hook_none },
	{ "starting_profile", 0x68, 1096, 4, 278, 0, 11, 0, 992, 2, _tag_swap_hook_none },
	{ "static_hud_element", 0x68, 1100, 8, 278, 1, 11, 0, 994, 2, _tag_swap_hook_none },
	{ "string_list", 0xC, 1108, 1, 279, 1, 11, 0, 996, 1, _tag_swap_hook_none },
	{ "string_list_entry", 0x14, 1109, 1, 280, 1, 11, 1, 997, 1, _tag_swap_hook_none },
	{ "structure_background_sound_palette_entry", 0x74, 1110, 1, 281, 0, 12, 0, 998, 1, _tag_swap_hook_none },
	{ "structure_breakable_surface", 0x30, 1111, 1, 281, 0, 12, 0, 999, 0, _tag_swap_hook_none },
	{ "structure_bsp", 0x288, 1112, 5, 281, 27, 12, 2, 999, 29, _tag_swap_hook_none },
	{ "structure_bsp_reference", 0x20, 1117, 2, 308, 0, 14, 0, 1028, 2, _tag_swap_hook_none },
	{ "structure_cluster", 0x68, 1119, 4, 308, 5, 14, 0, 1030, 5, _tag_swap_hook_none },
	{ "structure_cluster_portal", 0x40, 1123, 2, 313, 1, 14, 0, 1035, 1, _tag_swap_hook_none },
	{ "structure_collision_material", 0x14, 1125, 2, 314, 0, 14, 0, 1036, 1, _tag_swap_hook_none },
	{ "structure_detail_object_data", 0x40, 1127, 2, 314, 4, 14, 0, 1037, 4, _tag_swap_hook_none },
	{ "structure_fog_palette_entry", 0x88, 1129, 1, 318, 0, 14, 0, 1041, 1, _tag_swap_hook_none },
	{ "structure_fog_plane", 0x20, 1130, 2, 318, 1, 14, 0, 1042, 1, _tag_swap_hook_none },
	{ "structure_fog_region", 0x28, 1132, 1, 319, 0, 14, 0, 1043, 0, _tag_swap_hook_none },
	{ "structure_leaf", 0x10, 1133, 2, 319, 0, 14, 0, 1043, 0, _tag_swap_hook_none },
	{ "structure_lens_flare", 0x10, 1135, 1, 319, 0, 14, 0, 1043, 1, _tag_swap_hook_none },
	{ "structure_lens_flare_marker", 0x10, 1136, 1, 319, 0, 14, 0, 1044, 0, _tag_swap_hook_none },
	{ "structure_lightmap", 0x20, 1137, 2, 319, 1, 14, 0, 1044, 1, _tag_swap_hook_none },
	{ "structure_material", 0x100, 1139, 11, 320, 3, 14, 2, 1045, 7, _tag_swap_hook_none },
	{ "structure_mirror", 0x40, 1150, 1, 323, 1, 16, 0, 1052, 2, _tag_swap_hook_none },
	{ "structure_node", 0x6, 1151, 0, 324, 0, 16, 0, 1054, 0, _tag_swap_hook_none },
	{ "structure_portal_index", 0x2, 1151, 1, 324, 0, 16, 0, 1054, 0, _tag_swap_hook_none },
	{ "structure_runtime_decal", 0x10, 1152, 1, 324, 0, 16, 0, 1054, 0, _tag_swap_hook_none },
	{ "structure_sound_environment_palette_entry", 0x50, 1153, 1, 324, 0, 16, 0, 1054, 1, _tag_swap_hook_none },
	{ "structure_subcluster", 0x24, 1154, 1, 324, 1, 16, 0, 1055, 1, _tag_swap_hook_none },
	{ "structure_surface", 0x6, 1155, 1, 325, 0, 16, 0, 1056, 0, _tag_swap_hook_none },
	{ "structure_surface_reference", 0x8, 1156, 1, 325, 0, 16, 0, 1056, 0, _tag_swap_hook_none },
	{ "structure_weather_palette_entry", 0xF0, 1157, 5, 325, 0, 16, 0, 1056, 2, _tag_swap_hook_none },
	{ "structure_weather_polyhedron", 0x20, 1162, 1, 325, 1, 16, 0, 1058, 1, _tag_swap_hook_none },
	{ "tag_reference_camera_track", 0x10, 1163, 1, 326, 0, 16, 0, 1059, 1, _tag_swap_hook_none },
	{ "tag_reference_item", 0x10, 1164, 1, 326, 0, 16, 0, 1060, 1, _tag_swap_hook_none },
	{ "tag_reference_object", 0x10, 1165, 1, 326, 0, 16, 0, 1061, 1, _tag_swap_hook_none },
	{ "tag_reference_sound", 0x10, 1166, 1, 326, 0, 16, 0, 1062, 1, _tag_swap_hook_none },
	{ "trigger_volume", 0x60, 1167, 2, 326, 0, 16, 0, 1063, 0, _tag_swap_hook_none },
	{ "ui_widget", 0x3EC, 1169, 11, 326, 5, 16, 0, 1063, 11, _tag_swap_hook_none },
	{ "ui_widget_child", 0x50, 1180, 3, 331, 0, 16, 0, 1074, 1, _tag_swap_hook_none },
	{ "ui_widget_collection", 0xC, 1183, 1, 331, 1, 16, 0, 1075, 1, _tag_swap_hook_none },
	{ "ui_widget_collection_widget", 0x10, 1184, 1, 332, 0, 16, 0, 1076, 1, _tag_swap_hook_none },
	{ "ui_widget_conditional", 0x50, 1185, 3, 332, 0, 16, 0, 1077, 1, _tag_swap_hook_none },
	{ "ui_widget_event_handler", 0x48, 1188, 3, 332, 0, 16, 0, 1078, 2, _tag_swap_hook_none },
	{ "ui_widget_game_data_input", 0x24, 1191, 1, 332, 0, 16, 0, 1080, 0, _tag_swap_hook_none },
	{ "ui_widget_search_and_replace", 0x22, 1192, 1, 332, 0, 16, 0, 1080, 0, _tag_swap_hook_none },
	{ "unicode_string_list", 0xC, 1193, 1, 332, 1, 16, 0, 1080, 1, _tag_swap_hook_none },
	{ "unicode_string_list_entry", 0x14, 1194, 1, 333, 1, 16, 1, 1081, 1, _tag_swap_hook_none },
	{ "unit", 0x174, 1195, 12, 334, 6, 17, 0, 1082, 9, _tag_swap_hook_none },
	{ "unit_camera", 0x58, 1207, 1, 340, 1, 17, 0, 1091, 1, _tag_swap_hook_none },
	{ "unit_camera_track", 0x1C, 1208, 1, 341, 0, 17, 0, 1092, 1, _tag_swap_hook_none },
	{ "unit_definition", 0x2F0, 1209, 17, 341, 2, 17, 0, 1093, 20, _tag_swap_hook_none },
	{ "unit_dialogue_variant", 0x18, 1226, 2, 343, 0, 17, 0, 1113, 1, _tag_swap_hook_none },
	{ "unit_hud", 0x30, 1228, 1, 343, 0, 17, 0, 1114, 1, _tag_swap_hook_none },
	{ "unit_hud_interface", 0x56C, 1229, 62, 343, 9, 17, 0, 1115, 17, _tag_swap_hook_none },
	{ "unit_initial_weapon", 0x24, 1291, 1, 352, 0, 17, 0, 1132, 1, _tag_swap_hook_none },
	{ "unit_powered_seat", 0x44, 1292, 1, 352, 0, 17, 0, 1133, 0, _tag_swap_hook_none },
	{ "unit_seat", 0x11C, 1293, 5, 352, 2, 17, 0, 1133, 3, _tag_swap_hook_none },
	{ "vehicle", 0x78, 1298, 5, 354, 0, 17, 0, 1136, 0, _tag_swap_hook_none },
	{ "vehicle_animation", 0x74, 1303, 5, 354, 2, 17, 0, 1136, 2, _tag_swap_hook_none },
	{ "vehicle_definition", 0x3F0, 1308, 26, 356, 2, 17, 0, 1138, 24, _tag_swap_hook_none },
	{ "vehicle_palette", 0x30, 1334, 1, 358, 0, 17, 0, 1162, 1, _tag_swap_hook_none },
	{ "vehicle_suspension", 0x14, 1335, 2, 358, 0, 17, 0, 1163, 0, _tag_swap_hook_none },
	{ "vibrate_frequency", 0x14, 1337, 3, 358, 0, 17, 0, 1163, 0, _tag_swap_hook_none },
	{ "virtual_keyboard", 0x3C, 1340, 1, 358, 1, 17, 0, 1163, 4, _tag_swap_hook_none },
	{ "virtual_keyboard_key", 0x50, 1341, 2, 359, 0, 17, 0, 1167, 4, _tag_swap_hook_none },
	{ "water_ripple", 0x4C, 1343, 3, 359, 0, 17, 0, 1171, 0, _tag_swap_hook_none },
	{ "weapon", 0x5C, 1346, 7, 359, 0, 17, 0, 1171, 0, _tag_swap_hook_none },
	{ "weapon", 0x200, 1353, 9, 359, 3, 17, 0, 1171, 18, _tag_swap_hook_none },
	{ "weapon_ammunition_object", 0x1C, 1362, 2, 362, 0, 17, 0, 1189, 1, _tag_swap_hook_none },
	{ "weapon_definition", 0x508, 1364, 20, 362, 3, 17, 0, 1190, 33, _tag_swap_hook_none },
	{ "weapon_firing_effect", 0x84, 1384, 2, 365, 0, 17, 0, 1223, 6, _tag_swap_hook_none },
	{ "weapon_hud_crosshair_item", 0x6C, 1386, 8, 365, 0, 17, 0, 1229, 0, _tag_swap_hook_none },
	{ "weapon_hud_crosshairs_element", 0x68, 1394, 2, 365, 1, 17, 0, 1229, 2, _tag_swap_hook_none },
	{ "weapon_hud_element_header", 0x24, 1396, 2, 366, 0, 17, 0, 1231, 0, _tag_swap_hook_none },
	{ "weapon_hud_interface", 0x17C, 1398, 9, 366, 7, 17, 0, 1231, 8, _tag_swap_hook_none },
	{ "weapon_hud_meter_element", 0xB4, 1407, 9, 373, 2, 17, 0, 1239, 2, _tag_swap_hook_none },
	{ "weapon_hud_number_element", 0xA0, 1416, 12, 375, 1, 17, 0, 1241, 0, _tag_swap_hook_none },
	{ "weapon_hud_overlay", 0x1C, 1428, 1, 376, 1, 17, 0, 1241, 2, _tag_swap_hook_none },
	{ "weapon_hud_overlay_item", 0x88, 1429, 8, 377, 0, 17, 0, 1243, 0, _tag_swap_hook_none },
	{ "weapon_hud_overlays_element", 0x68, 1437, 2, 377, 1, 17, 0, 1243, 2, _tag_swap_hook_none },
	{ "weapon_hud_static_element", 0xB4, 1439, 10, 378, 2, 17, 0, 1245, 2, _tag_swap_hook_none },
	{ "weapon_magazine", 0x70, 1449, 7, 380, 1, 17, 0, 1247, 3, _tag_swap_hook_none },
	{ "weapon_palette", 0x30, 1456, 1, 381, 0, 17, 0, 1250, 1, _tag_swap_hook_none },
	{ "weapon_trigger", 0x114, 1457, 7, 381, 1, 17, 0, 1251, 3, _tag_swap_hook_none },
	{ "weather_particle_system", 0x30, 1464, 1, 382, 1, 17, 0, 1254, 1, _tag_swap_hook_none },
	{ "weather_particle_type", 0x25C, 1465, 11, 383, 1, 17, 0, 1255, 3, _tag_swap_hook_none },
	{ "wind", 0x40, 1476, 1, 384, 0, 17, 0, 1258, 0, _tag_swap_hook_none },
};

/* the fields the hooks and the model data's swap read, and their widths */
#define TAG_SWAP_ANIMATION_NODE_COUNT_OFFSET 0x2C
#define TAG_SWAP_ANIMATION_ROTATION_FLAGS_OFFSET 0x6C
#define TAG_SWAP_ANIMATION_TRANSLATION_FLAGS_OFFSET 0x5C
#define TAG_SWAP_ANIMATION_SCALE_FLAGS_OFFSET 0x7C
#define TAG_SWAP_ANIMATION_FRAME_COUNT_OFFSET 0x22
#define TAG_SWAP_ANIMATION_FRAME_SIZE_OFFSET 0x24
#define TAG_SWAP_ANIMATION_FLAGS_OFFSET 0x3A
#define TAG_SWAP_ANIMATION_COMPRESSED_DATA_OFFSET_OFFSET 0x88
#define TAG_SWAP_ANIMATION_FRAME_INFO_SIZE_OFFSET 0x48
#define TAG_SWAP_ANIMATION_FRAME_INFO_ADDRESS_OFFSET 0x54
#define TAG_SWAP_ANIMATION_DEFAULT_DATA_SIZE_OFFSET 0x8C
#define TAG_SWAP_ANIMATION_DEFAULT_DATA_ADDRESS_OFFSET 0x98
#define TAG_SWAP_ANIMATION_DATA_SIZE_OFFSET 0xA0
#define TAG_SWAP_ANIMATION_DATA_ADDRESS_OFFSET 0xAC
#define TAG_SWAP_RECORDED_ANIMATION_VERSION_OFFSET 0x22
#define TAG_SWAP_RECORDED_ANIMATION_EVENT_STREAM_SIZE_OFFSET 0x2C
#define TAG_SWAP_RECORDED_ANIMATION_EVENT_STREAM_ADDRESS_OFFSET 0x38
#define TAG_SWAP_MODEL_PART_TRIANGLE_BUFFER_TYPE_OFFSET 0x44
#define TAG_SWAP_MODEL_PART_TRIANGLE_BUFFER_COUNT_OFFSET 0x48
#define TAG_SWAP_MODEL_PART_TRIANGLE_BUFFER_HARDWARE_FORMAT_OFFSET 0x50
#define TAG_SWAP_STRUCTURE_MATERIAL_VERTEX_COUNT_OFFSET 0xB4
#define TAG_SWAP_STRUCTURE_MATERIAL_LIGHTMAP_VERTEX_COUNT_OFFSET 0xC8
#define TAG_SWAP_GBXMODEL_PART_STRIP_TRIANGLE_COUNT_OFFSET 0x48
#define TAG_SWAP_GBXMODEL_PART_STRIP_OFFSET_OFFSET 0x4C
#define TAG_SWAP_GBXMODEL_PART_VERTEX_COUNT_OFFSET 0x58
#define TAG_SWAP_GBXMODEL_PART_VERTEX_OFFSET_OFFSET 0x64
#define TAG_SWAP_GBXMODEL_BYTES 0xE8
#define TAG_SWAP_GBXMODEL_GEOMETRIES_OFFSET 0xD0
#define TAG_SWAP_GBXMODEL_GEOMETRIES_ELEMENT_BYTES 0x30
#define TAG_SWAP_GBXMODEL_GEOMETRY_PARTS_OFFSET 0x24
#define TAG_SWAP_GBXMODEL_GEOMETRY_PARTS_ELEMENT_BYTES 0x84

/* a data array kept in a tag's data (data.h's struct data_array, then its
elements): its header's values (one-byte ones too: they are marked, not turned)
and the scripts' syntax nodes' */
#define TAG_SWAP_DATA_ARRAY_BYTES 0x38
#define TAG_SWAP_DATA_ARRAY_MAXIMUM_COUNT_OFFSET 0x20
#define TAG_SWAP_DATA_ARRAY_ELEMENT_SIZE_OFFSET 0x22
#define TAG_SWAP_HS_SYNTAX_NODE_BYTES 0x14
static struct tag_swap_run const tag_swap_data_array_runs[] =
{
	{ 0x00, 1, 32 },
	{ 0x20, 2, 2 },
	{ 0x24, 1, 2 },
	{ 0x28, 4, 1 },
	{ 0x2C, 2, 4 },
	{ 0x34, 4, 1 },
};
static struct tag_swap_run const tag_swap_hs_syntax_node_runs[] =
{
	{ 0x00, 2, 4 },
	{ 0x08, 4, 3 },
};
