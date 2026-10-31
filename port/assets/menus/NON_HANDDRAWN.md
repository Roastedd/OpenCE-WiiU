# Menu pictures that are not redraws

The menus' bitmaps are drawn from the high-res redraws (`svg/`, from ui-svg-handmade) where there is
one. The PC version's pictures that have none (3D renders, screenshots and logos) are not in this
repository.

## From the Xbox's map

These have the same pictures, in the same order, in the Xbox's `ui.map`, which the menus draw them
from (the player's own; nothing is shipped). `mp_map_grafix`: the Xbox's has the 13 Xbox maps' pictures
(the PC version's first 13) and its "?"; the PC version's other 8 are its own maps'.

- `ui\shell\bitmaps\colors_sm`
- `ui\shell\bitmaps\mp_map_grafix`
- `ui\shell\bitmaps\sp_levels`
- `ui\shell\main_menu\difficulty_select\difficulty_options`
- `ui\shell\main_menu\difficulty_select\difficulty_options_small`
- `ui\shell\main_menu\halo_logo`
- `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\color_edit\player_color_marine_large`

These frames are drawn from a frame of the Xbox map's bitmap of the same name, whose other frames differ:
the same picture, or for the profile settings the Xbox's picture for the same thing.

| The PC version's frame | The Xbox's frame |
| --- | --- |
| `shell/main_menu/multiplayer_type_select/mp_options__2.png` | `ui\shell\main_menu\multiplayer_type_select\mp_options` frame 3 |
| `shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__2.png` | `ui\shell\main_menu\settings_select\multiplayer_setup\playlist_edit\gametype_options` frame 2 |
| `shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__5.png` | `ui\shell\main_menu\settings_select\multiplayer_setup\playlist_edit\gametype_options` frame 4 |
| `shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__0.png` | `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\profile_options` frame 0 |
| `shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__2.png` | `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\profile_options` frame 1 |
| `shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__7.png` | `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\profile_options` frame 3 |

These frames are added after a bitmap's, drawn scaled from the Xbox map's (the profile settings'
picture of Gamepad Setup: the Xbox's Controller Setup's pictures of the button settings).

| Our bitmap | The Xbox's frame |
| --- | --- |
| `main_menu/settings_select/player_setup/player_profile_edit/profile_options` | `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\controller_edit\config_controller` frame 4 |
| `main_menu/settings_select/player_setup/player_profile_edit/profile_options` | `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\controller_edit\config_controller` frame 5 |
| `main_menu/settings_select/player_setup/player_profile_edit/profile_options` | `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\controller_edit\config_controller` frame 6 |
| `main_menu/settings_select/player_setup/player_profile_edit/profile_options` | `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\controller_edit\config_controller` frame 7 |
| `main_menu/settings_select/player_setup/player_profile_edit/profile_options` | `ui\shell\main_menu\settings_select\player_setup\player_profile_edit\controller_edit\config_controller` frame 8 |

## Drawn for this port

The Xbox's map has none of these (or other pictures under the name), and the PC version's are 3D
renders and screenshots, which are not shipped. Each frame is an original drawing of what its item
is for, in the menus' style: an SVG in `drawn/`, under the frame's path, which
`tools/render_menu_art.py` draws at twice the frame's size.

| File | Size | What it shows |
| --- | --- | --- |
| `ce/shell/main_menu/multiplayer_type_select/mp_options__0.png` | 512x256 | Join or create a game on the Internet: a globe, and the list of games found on it |
| `ce/shell/main_menu/multiplayer_type_select/mp_options__1.png` | 512x256 | Join or create a game on the LAN: three screens linked by cables to one switch |
| `ce/shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__0.png` | 512x256 | Change Name: a name tag being written on, its gametype's emblem, the letters so far and the cursor |
| `ce/shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__3.png` | 512x256 | Item Options: a rifle, and a grenade, rounds and a health pack in their places |
| `ce/shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__4.png` | 512x256 | Vehicle Options: an off-road buggy, its roll cage open, on the pad it spawns on |
| `ce/shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__6.png` | 512x256 | Teamplay Options: two teams of three facing each other, the blue and the red, each under its emblem |
| `ce/shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__7.png` | 512x256 | Save Changes: the gametype going onto a memory card, and a tick |
| `ce/shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__1.png` | 512x256 | Controls Setup: a keyboard's keys and a mouse, those that move and jump, and the mouse's button, lit |
| `ce/shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__3.png` | 512x256 | Mouse Setup: a mouse, the aim it moves, and a sensitivity slider |
| `ce/shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__4.png` | 512x256 | Audio Setup: a speaker, its sound, and faders for the music and everything else |
| `ce/shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__5.png` | 512x256 | Video Setup: a TV showing a picture, and a tablet controller showing it too |
| `ce/shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__6.png` | 512x256 | Network Setup: a globe wrapped in a network of linked nodes |
| `ce/shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__8.png` | 512x256 | About: a ring world in space, a planet and its sun beyond it |

13 of 183 frames are drawn for this port; none is a placeholder.

The PC version's `ui\gamespy` and `ui\ticker` fonts are drawn with `ui\small_ui`, which
the Xbox's map has.
