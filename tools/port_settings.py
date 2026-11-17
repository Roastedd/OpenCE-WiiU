"""The port's settings screens, which tools/ce_menus.py writes in place of the
PC version's (port/assets/menus/ce): laid out as its are (rows of a label and
a spinner over its row backgrounds, a line of help for the row chosen,
Defaults, OK and Cancel), with what this port has to set instead of what the
PC version had (no connection speed or texture quality: a window or the full
screen, the frame rate, the keyboard's own controls).

A row's spinner sets one of config.toml's settings (setting=, values=), or of
the profile being edited ("profile.<field>": the controller's settings), by
port/linux/game/menu_functions.c: "port setting load" shows its value,
"port settings save" (OK) writes those changed and applies them, "port
settings defaults" shows the defaults. Controls Setup binds the keyboard and
mouse's controls, by the functions the PC version names for it.
"""

import re
import sys
from pathlib import Path
from xml.sax.saxutils import quoteattr

PE = "main_menu/settings_select/player_setup/player_profile_edit"
# the mods' screen, and its rows (_mods); the main menu's items
# (ce/main_menu.xml): MODS in Credits' place (Credits is left out:
# tools/ce_menus.py's LEFT_OUT), Quit a row below it (its CHILD_OFFSETS)
MODS = "main_menu/mods"
MOD_ROWS = 11
MODS_ITEM_PLACE = (192, 391)
QUIT_ITEM_PLACE = (192, 427)
YES_NO = [("YES", "true"), ("NO", "false")]
ON_OFF = [("ON", "true"), ("OFF", "false")]
# the multiplayer armor colors, in the profile's order (player_profile.c)
SHIELD_COLORS = ["white", "black", "red", "blue", "gray", "yellow", "green", "pink", "purple", "cyan", "cobalt",
                 "orange", "teal", "sage", "brown", "tan", "maroon", "salmon"]
XBOX_BUTTON_STYLES = [("XBOX CLASSIC", "classic"), ("XBOX GEMS", "gems"),
                     ("XBOX DARK GEMS", "menu_gems"), ("XBOX MODERN", "xbox_modern")]
PS_BUTTON_STYLES = [("PS CLASSIC", "ps_classic"), ("PS GEMS", "ps_gems"),
                   ("PS DARK GEMS", "ps_dark_gems"), ("PS MODERN", "ps_modern")]
SWITCH_BUTTON_STYLES = [("SWITCH CLASSIC", "switch_classic"), ("SWITCH GEMS", "switch_gems"),
                       ("SWITCH MODERN", "switch_modern")]
SENSITIVITIES = [(f"{value:g}", f"{value:g}") for value in (0.1, 0.15, 0.25, 0.4, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 2.5, 3, 4)]
VOLUMES = [(str(step), f"{step / 10:g}") for step in range(11)]
# an HDR display's paper white and peak in nits (display.hdr_paper_white,
# display.hdr_peak_brightness), in steps fine enough for HDR Calibration's
# patterns (and BT.2408's reference white, 203); Extra Effects' rows have the
# same, so they show what it chose
PAPER_WHITES = [("AUTO", "0")] + [(str(nits), str(nits)) for nits in sorted({*range(80, 200, 10), 200, 203, *range(220, 401, 20), 250})]
PEAK_BRIGHTNESSES = [("AUTO", "0")] + [(str(nits), str(nits)) for nits in
                                       [*range(250, 600, 25), *range(600, 1200, 50), *range(1200, 2001, 100),
                                        2500, 3000, 4000, 5000, 6000, 8000, 10000]]

# each screen: its folder below PE, its screen's widget (the name the profile
# menu opens), its header (widget, bitmap), the row spacing, and its rows:
# (label, setting, [(shown, value)], help, platform[, key]): platform is None
# for every platform, else the platforms the row is for, separated by spaces
# ("desktop", "android", "web", "wiiu": the menus' platform attribute,
# port/linux/src/menu_files.c); key names the row's widgets where two rows set
# one setting (one for each platform), else the setting's name does
SCREENS = {
    "video_settings": {
        "screen": "video_settings_screen",
        "header": ("header_profile_video_settings", f"{PE}/video_settings/header_profile_video_settings"),
        # a 28px step (the other screens' 30 would take the desktop's ten
        # rows into the help: Graphics' is 27). each platform packs its own
        # rows, so a row hidden on this machine does not leave a gap
        "spacing": 28,
        # (below the desktop's last row, 353: its two lines clear of the
        # buttons, 414)
        "help_top": 358, "help_height": 54,
        "platform_places": True,
        # (rows in the place of the row before them: Window Size in
        # Resolution's, port/linux/game/menu_functions.c showing the one the
        # display mode chosen uses; Android's anti-aliasing in the desktop's)
        "same_place": ["display.window_size", "anti_aliasing_android"],
        "rows": [
            ("DISPLAY MODE:", "display.mode",
             [("FULLSCREEN", "fullscreen"), ("BORDERLESS", "borderless"), ("WINDOWED", "windowed")],
             "Fullscreen takes the display; borderless covers it\nwith a window. F11 switches to the window.",
             "desktop"),
            # (port/linux/game/menu_tags.c adds the display's resolutions)
            ("RESOLUTION:", "display.resolution", [("NATIVE", "native")],
             "What fullscreen and borderless draw at. Fullscreen\nsets the display to it; borderless scales it.",
             "desktop"),
            # (port/linux/game/menu_tags.c puts the sizes that fit the desktop
            # in place of this one)
            ("WINDOW SIZE:", "display.window_size", [("1280 x 960", "1280x960")],
             "The window's size: 4:3, then 16:10, 16:9 and 21:9\n(its edges can also be dragged).", "desktop"),
            ("RESOLUTION SCALING:", "display.resolution_scaling",
             [("NATIVE", "native"), ("75%", "75%"), ("50%", "50%"), ("25%", "25%"), ("ORIGINAL", "original")],
             "Native draws at the resolution, 75% to 25% that\npart each way; Original, the Xbox's 480 lines.",
             "desktop android web"),
            ("V-SYNC:", "display.vsync", ON_OFF,
             "Wait for the display between frames, so that the\npicture never tears.", "desktop android web"),
            # (the Wii U's graphics modes: port/linux/game/graphics_quality.c, which
            # menu_functions.c's graphics_mode_settle writes the settings of)
            ("GRAPHICS MODE:", "display.graphics_mode",
             [("PERFORMANCE", "performance"), ("BALANCED", "balanced"), ("QUALITY", "quality"), ("CUSTOM", "custom")],
             "Performance: 60 frames, less detail. Balanced: 720\nlines at 60. Quality: up to 1080 lines at 30.\nCustom: the rows as set. Lines from the next start.",
             "wiiu"),
            # (the Wii U: port/wiiu/src/wiiu_gx2.c reads it when the game starts)
            ("RENDER RESOLUTION:", "display.render_lines",
             [("480", "480"), ("540", "540"), ("720", "720"), ("900", "900"), ("1080", "1080")],
             "The lines the game draws (TV and GamePad scale it).\n720 holds 60 most places. From the next start.",
             "wiiu"),
            # (the Wii U's dynamic resolution: port/wiiu/src/wiiu_drs.c, which
            # port/wiiu/src/wiiu_gx2.c runs at each frame; Quality turns it on)
            ("DYNAMIC RESOLUTION:", "display.dynamic_resolution", ON_OFF,
             "Draw fewer lines while the GPU falls behind,\nand all of them again when it catches up.",
             "wiiu"),
            ("LOWEST LINES:", "display.dynamic_resolution_floor",
             [("540", "540"), ("576", "576"), ("648", "648"), ("720", "720"), ("810", "810")],
             "The fewest lines dynamic resolution draws.\nOf 720 lines it goes to 540, of 1080 to 810.",
             "wiiu"),
            ("FRAME RATE LIMIT:", "display.max_fps",
             [("AUTO", "0"), ("30", "30"), ("60", "60"), ("120", "120"), ("144", "144"), ("165", "165"),
              ("240", "240"), ("NONE", "-1")],
             "With V-Sync off, the most frames a second. Auto:\ntwice the display's refresh rate.", "desktop web"),
            ("FRAME RATE LIMIT:", "display.max_fps",
             [("AUTO", "0"), ("30", "30"), ("60", "60"), ("120", "120"), ("NONE", "-1")],
             "With V-Sync off, the most frames a second. Auto:\ntwice the display's refresh rate.", "android",
             "max_fps_android"),
            ("SMOOTH MOTION:", "display.interpolation", ON_OFF,
             "Draw a frame for every display refresh, blending\nbetween the game's 30 ticks a second.", None),
            # (the Wii U's split screen with three or four views: port/linux/game/graphics_quality.c)
            ("SPLIT SCREEN DETAIL:", "display.split_screen_detail", [("REDUCED", "reduced"), ("SAME", "same")],
             "Three or four players: Reduced leaves out shadows,\nflares and shine, which each view draws again.",
             "wiiu"),
            ("SPLIT SCREEN FPS:", "display.split_screen_fps", [("AUTO", "auto"), ("30", "30"), ("60", "60")],
             "Auto: 30, evenly paced, for three or four players;\ntwo as Smooth Motion has them. 30: two or more.",
             "wiiu"),
            ("INSTANT AIM:", "display.direct_camera", ON_OFF,
             "In first person, turn the view the moment the\nmouse moves, not up to two ticks later.", "desktop web"),
            # (Graphics', with the HUD's rows: Video Setup's own has no room
            # left above its help on the desktop. The game's own counter, main.c's main_framerate_render, which
            # display.show_fps turns on: port/linux/game/frame_rate_display.c)
            ("SHOW FRAMES:", "display.show_fps", ON_OFF,
             "Draw the frame rate at the bottom right of a game,\ncounted over half a second.", None),
            ("HIGH-RES HUD:", "display.high_res_hud", ON_OFF,
             "Draw the HUD from the high-res redraws; off\ndraws the game's own pictures.", None),
            ("HIGH-RES TEXT:", "display.high_res_text", ON_OFF,
             "Draw text and titles with high-res fonts; off\ndraws the game's own.", None),
            ("ANTI-ALIASING:", "display.anti_aliasing",
             [("OFF", "off"), ("FXAA", "fxaa"), ("SMAA", "smaa"), ("SSAA 2X", "ssaa2x"), ("MSAA 2X", "msaa2x"),
              ("MSAA 4X", "msaa4x"), ("MSAA 8X", "msaa8x")],
             "Smooth jagged edges, which the Xbox did not. FXAA\nand SMAA are cheap; SSAA and MSAA are sharper.",
             "desktop"),
            ("ANTI-ALIASING:", "display.anti_aliasing",
             [("OFF", "off"), ("FXAA", "fxaa"), ("MSAA 2X", "msaa2x"), ("MSAA 4X", "msaa4x")],
             "Smooth jagged edges, which the Xbox did not. FXAA\nis cheap; MSAA is sharper.",
             "android web", "anti_aliasing_android"),
            ("TEXTURE FILTERING:", "display.anisotropic_filtering",
             [("OFF", "0"), ("2X", "2"), ("4X", "4"), ("8X", "8"), ("16X", "16")],
             "Keep floors and walls sharp at a grazing angle,\nwhich the Xbox did not. Off: as the Xbox.",
             "desktop android web"),
            ("SHADOW RESOLUTION:", "display.shadow_resolution",
             [("128", "128"), ("256", "256"), ("512", "512"), ("1024", "1024")],
             "The size objects' shadows are drawn at: 128 as on\nthe Xbox; larger for smoother, as soft, edges.",
             "desktop android web"),
            ("PER-PIXEL LIGHTING:", "display.per_pixel_lighting", ON_OFF,
             "Light models for each pixel, without the facets\nof the Xbox's lighting for each vertex.",
             "desktop android web"),
            ("OVERSHIELD:", "display.overshield_effect", ON_OFF,
             "The overshield rises up the body and stays lit,\nin its own color. Online: the host's choice.",
             "desktop android web"),
            ("MODEL DETAIL:", "display.model_detail", [("STOCK", "stock"), ("NATIVE", "native"), ("HIGHEST", "highest")],
             "Stock: original detail. Native: display resolution.\nHighest: most geometry; may reduce frame rate.",
             "desktop android web wiiu"),
            ("SHIELD GLOW:", "display.shield_glow", ON_OFF,
             "Shields light their surroundings as they flare.\nOnline: the host's choice.", "desktop android web"),
            ("GLOW INTENSITY:", "display.shield_glow_intensity", [("DEFAULT", "default"), ("LIGHT SHOW", "light_show")],
             "Light Show: three times as far, at full from a\nthird of the flare. Online: the host's choice.",
             "desktop android web"),
            ("HDR:", "display.hdr", ON_OFF,
             "Let explosions, plasma and the sun shine brighter\nthan white, then fit them to the screen.",
             "desktop android web"),
            ("HDR DISPLAY:", "display.hdr_output", ON_OFF,
             "On an HDR display (Windows HDR on), show that\nlight truly brighter. From the next start.", "desktop"),
            ("TONE CURVE:", "display.hdr_tone_mapping",
             [("GT7", "gt7"), ("NEUTRAL", "neutral"), ("ACES", "aces"), ("REINHARD", "reinhard")],
             "GT7: bright light turns white, as on film. Neutral:\nas drawn. ACES: more contrast. Reinhard: softer.",
             "desktop android web"),
            ("PAPER WHITE:", "display.hdr_paper_white", PAPER_WHITES,
             "With HDR Display, how bright white is, in nits.\nAuto: Windows' SDR content brightness.", "desktop"),
            ("PEAK BRIGHTNESS:", "display.hdr_peak_brightness", PEAK_BRIGHTNESSES,
             "With HDR Display, your display's brightest, in\nnits. Auto: Windows' HDR calibration.", "desktop"),
        ],
    },
    "mouse_settings": {
        "screen": "mouse_settings_screen",
        "header": ("header_profile_mouse_settings", f"{PE}/mouse_settings/header_profile_mouse_settings"),
        "spacing": 30,
        "rows": [
            ("HORIZONTAL SENSITIVITY:", "input.mouse_sensitivity", SENSITIVITIES,
             "How fast the view turns side to side for the\nmouse's movement.", None),
            ("VERTICAL SENSITIVITY:", "input.mouse_vertical_sensitivity", [("SAME", "0")] + SENSITIVITIES,
             "How fast the view turns up and down; Same\nturns it as fast as side to side.", None),
            ("INVERT VERTICAL AXIS:", "input.invert_mouse", YES_NO,
             "Moving the mouse forward looks down.", None),
            ("AIM ASSIST:", "input.mouse_aim_assist", ON_OFF,
             "Slow and drag the view along with a target while\naiming with the mouse, as with a controller.", None),
            # (port/linux/src/sdl_platform.c's menu_cursor_size; last, as the
            # other platforms have no row in its place)
            ("MENU CURSOR:", "display.menu_cursor",
             [("OFF", "off"), ("SMALL", "small"), ("MEDIUM", "medium"), ("LARGE", "large"),
              ("EXTRA LARGE", "extra_large")],
             "The menus' blue arrow for the mouse, and its size;\nOff shows the system's pointer.", "desktop"),
        ],
    },
    "audio_settings": {
        "screen": "audio_settings_screen",
        "header": ("header_profile_audio_settings", f"{PE}/audio_settings/header_profile_audio_settings"),
        "spacing": 30,
        # (the devices, desktop only, first: Android's rows from the top)
        "platform_places": True,
        "rows": [
            # (port/linux/game/menu_tags.c adds the devices SDL finds)
            ("OUTPUT DEVICE:", "audio.output_device", [("SYSTEM DEFAULT", "default")],
             "Where the game's sound and the voices play.", "desktop"),
            ("INPUT DEVICE:", "audio.input_device", [("SYSTEM DEFAULT", "default")],
             "The microphone voice chat listens to.", "desktop"),
            ("MASTER VOLUME:", "audio.volume", VOLUMES, "The volume of everything.", None),
            ("MUSIC VOLUME:", "audio.music_volume", VOLUMES, "The music's volume.", None),
            ("EFFECTS VOLUME:", "audio.effects_volume", VOLUMES,
             "The volume of every other sound: effects and\nspeech.", None),
            ("REVERB:", "audio.reverb", ON_OFF,
             "Echo sounds as the place you are in does, and\nmuffle those behind walls, as the Xbox did.", None),
            ("SOUND:", "audio.enabled", ON_OFF,
             "Play sound at all; from the next time the game\nstarts.", None),
            # voice chat: this machine's own (port/linux/game/network_voice.c)
            ("VOICE CHAT:", "audio.voice_chat",
             [("PUSH TO TALK", "push_to_talk"), ("OPEN MIC", "open_mic"), ("OFF", "off")],
             "Talk in network games: while PUSH TO TALK is held\n(Controls), whenever you speak, or never.",
             "desktop android web"),
            # (the Wii U's: push to talk is the GamePad's TV button, the one the game leaves free,
            # port/wiiu/src/wiiu_input.c; open mic is best with a headset in the GamePad, whose own
            # microphone hears its speaker)
            ("VOICE CHAT:", "audio.voice_chat",
             [("PUSH TO TALK", "push_to_talk"), ("OPEN MIC", "open_mic"), ("OFF", "off")],
             "Talk in network games: while the GamePad's TV\nbutton is held, whenever you speak (best with a\nheadset in the GamePad), or never.",
             "wiiu", "voice_chat_wiiu"),
            ("VOICE VOLUME:", "audio.voice_volume", VOLUMES, "The other players' voices.", "desktop android web wiiu"),
        ],
    },
    "network_setup": {
        "screen": "network_settings_screen",
        "header": ("header_profile_network_settings", f"{PE}/network_setup/header_profile_network_settings"),
        "spacing": 30,
        # (the router, the clipboard and the updates, which the web build
        # has not: the other rows from the top there)
        "platform_places": True,
        "rows": [
            ("INTERNET PLAY:", "network.online", ON_OFF,
             "Host and join games over the internet by invite\nlinks; off keeps to the local network.",
             "desktop android web"),
            ("UPNP PORT FORWARDING:", "network.allow_upnp", ON_OFF,
             "Let internet play ask the router to forward its\nport, for networks that stop connections.",
             "desktop android"),
            ("JOIN FROM CLIPBOARD:", "network.join_from_clipboard", ON_OFF,
             "Join the game of an invite link copied before\nswitching to the game.", "desktop android"),
            ("CHECK FOR UPDATES:", "update.auto", ON_OFF,
             "Look for a new version when the game starts.", "desktop android wiiu"),
            ("PLAYER NAMES:", "display.player_names",
             [("ALL", "all"), ("ALLIES", "allies"), ("ENEMIES", "enemies"), ("NONE", "none")],
             "In multiplayer, whose names are drawn above their\nheads.", None),
            ("PLAYER NAME SIZE:", "display.player_name_scale",
             [(f"{value:g}x", f"{value:g}") for value in (0.5, 0.75, 1, 1.25, 1.5, 2)],
             "How large the players' names are drawn.", None),
            ("SCOREBOARD LAYOUT:", "display.scoreboard_team_layout", [("TEAMS", "teams"), ("BY SCORE", "score")],
             "A team game's scoreboard: a column for each team,\nor every player in order of score.", None),
            ("SCOREBOARD PANEL:", "display.scoreboard_background", ON_OFF,
             "Draw a panel behind the scoreboard, for clearer\ntext.", None),
        ],
    },
    "gamepad_setup": {
        "screen": "gamepad_setup_screen",
        "header": ("header_profile_controller_setting", f"{PE}/controller_edit/header_profile_controller_setting"),
        "spacing": 27,
        "platform_places": True,
        "rows": [
            ("LOOK SENSITIVITY:", "profile.look_sensitivity", [(str(step), str(step)) for step in range(1, 11)],
             "How fast the right stick turns the view.", None),
            ("INVERT LOOK:", "profile.invert_look", YES_NO, "Pushing the right stick up looks down.", None),
            ("INVERT FLIGHT:", "profile.flight_inversion", YES_NO,
             "Flying vehicles pitch as aircraft do: pushing up\nflies down.", None),
            ("AUTO-CENTER LOOK:", "profile.autocenter", YES_NO,
             "Level the view while walking.", None),
            ("BUTTON LAYOUT:", "profile.button_preset",
             [("DEFAULT", "0"), ("SWAP TRIGGERS", "1"), ("SWAP A, L TRIGGER", "2"), ("SWAP B, L TRIGGER", "3"),
              ("SWAP B, R STICK", "4"), ("BUMPER JUMPER", "5"), ("FISHSTICKS", "6"), ("RECON", "7")],
             "Which of the controller's buttons does what (not\nthe keyboard's: Controls Setup sets those).", None),
            ("STICK LAYOUT:", "profile.joystick_preset",
             [("DEFAULT", "0"), ("SOUTHPAW", "1"), ("LEGACY", "2"), ("LEGACY SOUTHPAW", "3")],
             "Which stick moves and which looks.", None),
            ("VIBRATION:", "profile.vibration", ON_OFF, "Rumble the controller.", None),
            ("IN-GAME HELP:", "profile.ingame_help", ON_OFF, "Show the game's hints about its controls.", None),
        ],
        "categories": [("BUTTON REMAPPING", "controller_remapping",
                        "Custom button layouts for P1-P4.\nUse / Reload stays combined."),
                       ("BUTTON APPEARANCE", "controller_appearance",
                        "Controller artwork and bumper style.\nRequires High-Res HUD in Graphics.")],
    },
    "controller_appearance": {
        "screen": "controller_appearance_screen", "spacing": 30,
        "header": ("header_profile_controller_setting", f"{PE}/controller_edit/header_profile_controller_setting"),
        "same_place": ["hud_button_style_xbox", "hud_button_style_ps", "hud_button_style_switch"],
        "rows": [
            ("AUTO BUTTON DETECTION:", "input.auto_button_detection", ON_OFF,
             "Show styles for your controller's family.\nRequires High-Res HUD in Graphics.", None),
            ("BUTTON STYLE:", "display.hud_button_style", XBOX_BUTTON_STYLES + PS_BUTTON_STYLES + SWITCH_BUTTON_STYLES,
             "Manual gameplay button artwork, kept when auto\ndetection is on. Requires High-Res HUD.", None),
            ("BUTTON STYLE:", "display.hud_button_style_xbox", XBOX_BUTTON_STYLES,
             "Preferred Xbox artwork for automatic detection.\nRequires High-Res HUD in Graphics.", None),
            ("BUTTON STYLE:", "display.hud_button_style_ps", PS_BUTTON_STYLES,
             "Preferred PlayStation artwork for auto detection.\nRequires High-Res HUD in Graphics.", None),
            ("BUTTON STYLE:", "display.hud_button_style_switch", SWITCH_BUTTON_STYLES,
             "Preferred Switch artwork for automatic detection.\nRequires High-Res HUD in Graphics.", None),
            ("BUMPER STYLE:", "display.hud_bumper_style",
             [("ORIGINAL XBOX", "original"), ("MODERN XBOX", "modern")],
             "Original: black/white buttons. Modern: LB/RB.\nKept when automatic detection is on.", None),
        ],
    },
    "controller_remapping": {
        "screen": "controller_remapping_screen", "spacing": 30,
        "dynamic_help": True, "help_top": 320, "help_height": 90,
        "header": ("header_profile_controller_setting", f"{PE}/controller_edit/header_profile_controller_setting"),
        "rows": [],
    },
}

# Video Setup's categories open ordinary settings screens. Each has the same
# pending edits, Defaults, OK and Cancel as the other settings screens.
_video = SCREENS["video_settings"]
_graphics = {"display.show_fps", "display.high_res_hud",
             "display.high_res_text", "display.anti_aliasing",
             "display.anisotropic_filtering", "display.shadow_resolution", "display.per_pixel_lighting",
             "display.model_detail"}
# (the visual modifiers: a screen of their own, Extra Effects)
_extra_effects = {"display.shield_glow", "display.shield_glow_intensity",
                  "display.overshield_effect", "display.hdr", "display.hdr_output", "display.hdr_tone_mapping", "display.hdr_paper_white",
                  "display.hdr_peak_brightness"}
SCREENS["video_settings/graphics"] = {
    "screen": "graphics_settings_screen", "header": _video["header"], "spacing": 27, "platform_places": True,
    "same_place": ["anti_aliasing_android"],
    "rows": [row for row in _video["rows"] if row[1] in _graphics],
}
SCREENS["video_settings/extra_effects"] = {
    "screen": "extra_effects_settings_screen", "header": _video["header"], "spacing": 30,
    "rows": [row for row in _video["rows"] if row[1] in _extra_effects],
    # (shown only with an HDR display: menu_functions.c's
    # hdr_calibration_row_show)
    "categories": [
        ("HDR CALIBRATION:", "video_settings/hdr_calibration",
         "Set Peak Brightness and Paper White by eye,\nwith patterns brighter than white.", "desktop"),
    ],
}
# HDR Calibration: the peak, then paper white, each row's pattern drawn by
# the renderer between the rows and the help, at the values shown, which the
# display takes while the screen is open (menu_functions.c's "port hdr
# calibration", d3d8_gl.c's hdr_calibration_draw)
SCREENS["video_settings/hdr_calibration"] = {
    "screen": "hdr_calibration_settings_screen", "header": _video["header"], "spacing": 30,
    "inputs": ["port hdr calibration"],
    "rows": [
        ("PEAK BRIGHTNESS:", "display.hdr_peak_brightness", PEAK_BRIGHTNESSES,
         "Raise it until the ring just disappears into\nthe square: the brightest your display shows.", None),
        ("PAPER WHITE:", "display.hdr_paper_white", PAPER_WHITES,
         "How bright white is (the menus, the HUD, the\ngrey bars), in nits. Choose what looks right.", None),
    ],
}
_video["rows"] = [row for row in _video["rows"] if row[1] not in _graphics | _extra_effects]
_video["categories"] = [
    ("DETAIL:", "video_settings/detail", "What is drawn: grass, decals, particles,\nshadows, flares and the level's shine.", "wiiu"),
    ("GRAPHICS:", "video_settings/graphics", "The HUD, text, anti-aliasing, shadows,\nlighting and HDR."),
    ("FOV AND VIEWMODELS:", "video_settings/fov_viewmodels",
     "The field of view and the first-person\nweapon. Their defaults keep the stock view."),
    ("EXTRA EFFECTS:", "video_settings/extra_effects",
     "Visual modifiers the stock game does not\nhave, such as the shield glow.", "desktop android web"),
]
SCREENS["video_settings/fov_viewmodels"] = {
    "screen": "fov_viewmodel_settings_screen", "header": _video["header"], "spacing": 30,
    "rows": [
        ("FOV:", "display.fov", [("DEFAULT", "0")] + [(str(n), str(n)) for n in range(80, 151, 5)],
         "On foot, horizontal at 16:9, in degrees.\nDefault keeps the stock view.", None),
        ("VIEWMODEL FOV:", "display.viewmodel_fov", [("DEFAULT", "0")] + [(str(n), str(n)) for n in range(80, 151, 5)],
         "The weapon and hands, horizontal at 16:9.\nDefault keeps the weapon's stock view.", None),
        ("VIEWMODELS:", "display.viewmodel_visible", ON_OFF,
         "Draw first-person weapons, hands and attached\nvisuals. Gameplay and sound continue when off.", None),
        ("PASSENGER THIRD PERSON:", "display.passenger_third_person", ON_OFF,
         "Third-person Warthog and Scorpion passengers in\nthe campaign and co-op. Weapon zoom is disabled.", None),
        ("ARM SHIELDS:", "display.viewmodel_shield", ON_OFF,
         "The energy shield's flare on your first-person\narms too, as on your body.", None),
        ("LEGS:", "display.first_person_legs", ON_OFF,
         "Your legs in first person, seen looking down,\nmoving as you run. Your body as others see it.", None),
        ("SHIELD COLOR:", "display.shield_color", [("DEFAULT", "default")] + [(c.upper(), c) for c in SHIELD_COLORS],
         "Your shield's flare in an armor color, whatever\nyour armor. Only you see it. Default: its own.", None),
    ],
}

SCREENS["video_settings/fov_viewmodels"]["categories"] = [
    ("THIRD PERSON CAMERA:","video_settings/third_person","Shoulder framing and distance for on-foot play.","wiiu")]
SCREENS["video_settings/third_person"] = {
    "screen":"third_person_settings_screen","header":_video["header"],"spacing":30,
    "rows":[
        ("SHOULDER CAMERA:","display.wiiu_shoulder_camera",ON_OFF,"On-foot third-person aiming camera.\nOff restores the original follow track.","wiiu"),
        ("DISTANCE:","display.wiiu_camera_distance",[(str(x),str(x)) for x in (0.6,0.8,1.1,1.4,1.7,2.0)],"Follow distance; nearby walls retract the view.\nVehicles retain their own camera settings.","wiiu"),
        ("SHOULDER:","display.wiiu_camera_shoulder",[("LEFT","-0.25"),("CENTER","0"),("RIGHT","0.25")],"Choose which side of the character to look past.\nThe reticle follows the actual shot path.","wiiu"),
        ("HEIGHT:","display.wiiu_camera_height",[(str(x),str(x)) for x in (0.0,0.1,0.2,0.3,0.4)],"Raise the camera pivot above eye height.\nWeapon zoom uses the normal eye view.","wiiu"),
    ]}

# the Wii U's Detail screen, from Video Setup: what the graphics modes set besides the lines and the frame rate
# (port/linux/game/graphics_quality.c; changing a row makes the mode custom: menu_functions.c's graphics_mode_settle)
SCREENS["video_settings/detail"] = {
    "screen": "detail_settings_screen", "header": _video["header"], "spacing": 30,
    "rows": [
            # (the frame-time governor: port/linux/game/render_governor.c; Performance turns it on)
            ("AUTO DETAIL:", "display.dynamic_quality", ON_OFF,
             "Leaves out small and far things, a step at a time,\nwhile the frames take over 22 ms; puts them back.", None),
            ("GRASS:", "display.grass", ON_OFF,
             "The grass, pebbles and plants near you. Off\nsaves the game's time in fields of grass.", None),
            ("DECALS:", "display.decals", ON_OFF,
             "Bullet holes, scorch marks and blood left on\nwalls and the ground.", None),
            ("PARTICLES:", "display.particles", ON_OFF,
             "Sparks, smoke, dust, debris and the trails of\nshots. Off leaves the shots and blasts bare.", None),
            ("OBJECT SHADOWS:", "display.object_shadows", ON_OFF,
             "The shadows characters, vehicles and weapons\ncast on the ground.", None),
            ("LENS FLARES:", "display.lens_flares", ON_OFF,
             "The flares and glows of lights and the sun.", None),
            ("SPECULAR:", "display.specular", ON_OFF,
             "The shine of lights on the level's surfaces,\nand its reflections and mirrors.", None),
            ("BUMP MAPPING:", "display.bump_mapping", ON_OFF,
             "The relief of rock and metal in the level's\nlighting; off lights its surfaces flat.", None),
    ],
}

# the Wii U's GamePad and controllers, a screen of their own from Controller Setup (port/wiiu/src/xinput_wiiu.c)
# (after Controller Setup's own categories, whose remapping and button pictures are SDL's: port/linux/src/
# controller_remap_input.c, controller_buttons.c)
SCREENS["gamepad_setup"]["categories"] = [
    *[(*category[:3], "desktop android web") for category in SCREENS["gamepad_setup"]["categories"]],
    ("WII U GAMEPAD:", "gamepad_setup/wiiu", "The GamePad's gyro aim, its buttons, what its\nscreen shows and the Pro Controllers.", "wiiu"),
    ("WII REMOTE:", "gamepad_setup/wiimote", "MotionPlus aiming, the free-aim area and\ncamera turning speed.", "wiiu"),
]
SCREENS["gamepad_setup/wiimote"] = {
    "screen": "wiiu_remote_settings_screen", "header": SCREENS["gamepad_setup"]["header"], "spacing": 27,
    "rows": [
        ("LOOK SOURCE:", "input.wiiu_wiimote_look",
         [("AUTO", "auto"), ("MOTIONPLUS", "motion"), ("SENSOR BAR", "pointer"), ("OFF", "off")],
         "Auto prefers MotionPlus. Sensor Bar uses\nthe Remote's infrared pointer.", None),
        ("SENSOR FILTER:", "input.wiiu_wiimote_ir_samples",
         [(str(value), str(value)) for value in (1, 2, 3, 5, 8)],
         "Recent sensor-bar samples are blended.\nLower values respond faster; 1 is unfiltered.", None),
        ("POINTING RANGE:", "input.wiiu_wiimote_ir_scale",
         [(str(value), str(value)) for value in (0.5, 0.65, 0.75, 0.9, 1.0, 1.25, 1.5)],
         "How far the sensor-bar pointer moves the\naim point across the screen.", None),
        ("SHAKE MELEE:", "input.wiiu_wiimote_shake_melee", ON_OFF,
         "Swing either hand deliberately for melee.\nReturn to rest before the next gesture.", None),
        ("FREE AIM:", "input.wiiu_wiimote_free_aim", ON_OFF,
         "Move the weapon within the screen on foot.\nThe camera turns near the aim area's edge.", None),
        ("MOTION SENSITIVITY:", "input.wiiu_wiimote_sensitivity",
         [(str(value), str(value)) for value in (0.25, 0.4, 0.5, 0.65, 0.8, 1.0, 1.25, 1.5, 2.0)],
         "How far the weapon aims when you turn the\nRemote. Lower values give finer control.", None),
        ("HORIZONTAL AREA:", "input.wiiu_wiimote_free_aim_box",
         [(str(value), str(value)) for value in (4, 6, 8, 10, 12, 16, 20, 25)],
         "Horizontal half-width in degrees. A larger\narea gives more aim before camera turning.", None),
        ("VERTICAL AREA:", "input.wiiu_wiimote_free_aim_vertical_box",
         [(str(value), str(value)) for value in (2, 4, 6, 8, 10, 12, 16, 20, 25)],
         "Vertical half-height in degrees. Adjust it\nseparately from horizontal camera turning.", None),
        ("SCOPED AIM:", "input.wiiu_wiimote_zoom_sensitivity",
         [(str(value), str(value)) for value in (0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0)],
         "Extra sensitivity while scoped. Lower values\nmake small corrections easier.", None),
        ("TURN SPEED:", "input.wiiu_wiimote_free_aim_speed",
         [(str(value), str(value)) for value in (30, 45, 60, 75, 90, 120, 150, 180)],
         "Maximum camera speed at the aim area's\nedge, in degrees per second.", None),
    ],
}

# Keep tuning grouped by purpose instead of extending the flat settings page.
_remote_original = SCREENS["gamepad_setup/wiimote"]["rows"]
def _remote_page(name, rows):
    SCREENS["gamepad_setup/wiimote/" + name] = {
        "screen": "wiiu_remote_" + name + "_screen", "header": SCREENS["gamepad_setup"]["header"],
        "spacing": 27, "rows": rows,
    }
_remote_page("aim", [row for row in _remote_original if row[1] not in {
    "input.wiiu_wiimote_look", "input.wiiu_wiimote_shake_melee"}] + [
    ("FULL TURN AT:", "input.wiiu_wiimote_turn_outer", [(str(x),str(x)) for x in (0.5,0.65,0.75,0.85,1.0)],
     "Reach maximum turn speed before pointing\nout of sensor range. Lower means earlier.", None),
])
_profiles=[]
for _context in ("hip","scope","vehicle"):
    _profiles.append((_context.upper()+" TURN:", "input.wiiu_wiimote_"+_context+"_profile",
        [("STEADY","steady"),("BALANCED","balanced"),("FAST","fast")],
        "Steady/Balanced/Fast change turn speed only.\nSet the free-aim area on Aim and Turn.", None))
    for _axis in ("horizontal","vertical"):
        _profiles.append((_context.upper()+" "+("H" if _axis=="horizontal" else "V")+" SPEED:",
            "input.wiiu_wiimote_"+_context+"_"+_axis,[(str(x),str(x)) for x in (0.25,0.5,0.75,1.0,1.25,1.5,2.0)],
            "Turn-speed multiplier for this axis.\nDoes not change the other aiming modes.",None))
_profiles.append(("SCOPE EDGE SCAN:", "input.wiiu_wiimote_scope_scan_floor", [("OPTICAL","0"),("15%","0.15"),("25%","0.25"),("35%","0.35"),("50%","0.5")],
    "Faster scanning near the scope's outer edge.\nSmall central corrections stay precise.",None))
SCREENS["gamepad_setup/wiimote/aim"]["rows"].append(("GUNNER SPEED:","input.wiiu_wiimote_gunner_speed",
    [(str(x),str(x)) for x in (0.5,0.75,1.0,1.25,1.5,2.0)],"Sensor turn speed for gunner seats.\nDriving controls keep their own response.",None))
_remote_page("profiles",_profiles)
_remote_page("calibration",[
    ("AIM GUIDE:","input.wiiu_wiimote_aim_guide",ON_OFF,"Preview center and full-turn boundaries in play.\nSave with OK, then test your comfortable reach.",None),
    ("CENTER H:","input.wiiu_wiimote_center_x",[(str(x),str(x)) for x in (-0.3,-0.2,-0.1,0.0,0.1,0.2,0.3)],"Horizontal center of your comfortable pointing.\nAdjust with the sensor bar in its usual place.",None),
    ("CENTER V:","input.wiiu_wiimote_center_y",[(str(x),str(x)) for x in (-0.3,-0.2,-0.1,0.0,0.1,0.2,0.3)],"Vertical center of your comfortable pointing.\nZero uses the default sensor center.",None),
    ("REACH H:","input.wiiu_wiimote_range_x",[(str(x),str(x)) for x in (0.5,0.65,0.8,1.0,1.25,1.5)],"Horizontal hand travel for the full range.\nLower values need less hand movement.",None),
    ("REACH V:","input.wiiu_wiimote_range_y",[(str(x),str(x)) for x in (0.5,0.65,0.8,1.0,1.25,1.5)],"Vertical hand travel for the full range.\nLower values need less hand movement.",None),
])
_remote_page("gestures",[
    ("NUNCHUK MELEE:","input.wiiu_wiimote_shake_melee",ON_OFF,"Swing the Nunchuk, then return it to rest.\nThe aiming Remote does not trigger melee.",None),
    ("MELEE FORCE:","input.wiiu_nunchuk_melee_threshold",[("LIGHT","1.5"),("NORMAL","2.0"),("FIRM","2.5"),("STRONG","3.0")],"Force needed for the Nunchuk gesture.\nChoose firmer if normal movement triggers it.",None),
    ("HOLD TO STEADY:","input.wiiu_wiimote_clutch",ON_OFF,"Hold the chosen button to freeze aiming.\nReplaces that button's usual action in play.",None),
    ("STEADY BUTTON:","input.wiiu_wiimote_clutch_button",[("REMOTE 2","2"),("NUNCHUK C","c")],"2 replaces button melee; C replaces crouch.\nMenu controls are unchanged.",None),
])
SCREENS["gamepad_setup/wiimote"]["rows"]=[_remote_original[0],
    ("ADAPTIVE FILTER:","input.wiiu_wiimote_adaptive_filter",ON_OFF,"Steadier at rest, quicker during fast motion.\nOff uses Sensor Filter on the Aim page.",None)]
SCREENS["gamepad_setup/wiimote"]["categories"]=[
    ("AIM AND TURN:","gamepad_setup/wiimote/aim","Aim range, turning speed and scope response.","wiiu"),
    ("CONTEXT PROFILES:","gamepad_setup/wiimote/profiles","Separate hip, scoped and vehicle tuning.","wiiu"),
    ("CALIBRATION:","gamepad_setup/wiimote/calibration","Comfortable pointing center and axis reach.","wiiu"),
    ("GESTURES:","gamepad_setup/wiimote/gestures","Independent Nunchuk melee and hold-to-steady.","wiiu"),
]

SCREENS["gamepad_setup/wiiu"] = {
    "screen": "wiiu_gamepad_settings_screen", "header": SCREENS["gamepad_setup"]["header"], "spacing": 30,
    "rows": [
            # (the Wii U's own: port/wiiu/src/xinput_wiiu.c)
            ("GYRO AIM:", "input.wiiu_gyro_aim", [("OFF", "off"), ("ALWAYS", "always"), ("ZOOMED", "zoom")],
             "Turning the GamePad aims too: always, or only\nwhile zoomed in through a scope.", None),
            ("GYRO TURNS WITH:", "input.wiiu_gyro_space", [("ANY HOLD", "player"), ("YAW", "turn"), ("LEAN", "lean")],
             "Any hold: turn the GamePad left and right\nhowever you hold it. Lean: like a wheel.", None),
            ("GYRO PAUSE:", "input.wiiu_gyro_pause", [("TOUCH", "touch"), ("RIGHT STICK", "stick"), ("NONE", "none")],
             "Holds the view still while you bring the\nGamePad back: a thumb on its screen, or\nwhile the right stick is pushed.", None),
            ("GYRO SENSITIVITY:", "input.wiiu_gyro_sensitivity", SENSITIVITIES,
             "How far the view turns for the GamePad's\nturn: 1 as far as the pad, 2 twice as far.", None),
            ("INVERT GYRO:", "input.wiiu_gyro_invert_y", YES_NO,
             "Tilting the top of the GamePad away looks up.", None),
            ("FACE BUTTONS:", "input.wiiu_face_buttons", [("AUTO", "auto"), ("BY PLACE", "position"), ("BY LETTER", "label")],
             "Auto: by letter in the menus, by place in the\ngame (B jumps). From the next start.", None),
            ("CONTROLLERS:", "input.wiiu_controller_layout",
             [("AUTO", "auto"), ("SEPARATE", "separate"), ("MERGED", "merged")],
             "Auto: all play as player 1 until another joins\n(ADD PLAYER, co-op). Separate: each its own.\nMerged: the first is the GamePad's.", None),
            # (port/wiiu/src/d3d8_gx2.c's gamepad_view_make, wiiu_gx2.c's blit_to)
            ("GAMEPAD SHOWS:", "input.wiiu_gamepad_view", [("TV", "tv"), ("PLAYER 1", "player1")],
             "Player 1: in split screen, player 1's view alone\non the GamePad. The menus are shown whole.", None),
    ],
}

# Audio Setup's sound processing (port/linux/src/dsound_sdl.c): the reverb
# and the resampler, a screen of their own (a category, as Video Setup's);
# the devices and the volumes stay on Audio Setup
_audio = SCREENS["audio_settings"]
_processing = {"audio.reverb"}
SCREENS["audio_settings/sound_processing"] = {
    "screen": "sound_processing_settings_screen", "header": _audio["header"], "spacing": 30,
    "rows": [row for row in _audio["rows"] if row[1] in _processing] + [
        ("RESAMPLER:", "audio.resampler", [("SINC", "sinc"), ("LINEAR", "linear")],
         "How sounds are converted to the output's rate:\nsinc is clean; linear is brighter, with aliasing.",
         None),
    ],
}
_audio["rows"] = [row for row in _audio["rows"] if row[1] not in _processing]

# (voice chat stays on Audio Setup; the text chat's rows are the multiplayer
# HUD's, on Network Setup)
_audio["categories"] = [
    ("SOUND PROCESSING:", "audio_settings/sound_processing",
     "The reverb, and how sounds are converted to the\noutput's rate."),
]

# Network Setup's map sharing (port/linux/game/map_share.c): this machine's
# part in another's game, a screen of its own (a category, as Video Setup's)
SEED_UPLOADS = (64, 128, 256, 512, 1024, 2048, 4096, 8192, 10240)
# the bot settings (screens.h, port/linux/game/menu_screens.inl): the Players page's BOT SETTINGS, how many bots, how well they play and on which teams: the
# settings the build honours (bots.count, bots.skill, bots.teams), a row each, the settings screens' own grammar with a text header
SCREENS["bot_settings"] = {
    "screen": "bot_settings_screen", "header": ("header_bot_settings", f"{PE}/bot_settings/header_bot_settings"), "header_text": "BOT SETTINGS",
    # (the social screens' places: the rows from 76, 28 apart; the help where the detail line is, starting where the rows' text does: a 512 wide box from x 66)
    "spacing": 28, "top": 76, "help_top": 336, "help_left": 66, "help_width": 500, "help_height": 48,
    "rows": [
        ("BOTS:", "bots.count", [("OFF", "0")] + [(str(count), str(count)) for count in range(1, 16)],
         "Bots only play in a game on this machine for now:\nnot in a hosted or online game.", None),
        ("SKILL:", "bots.skill",
         [("RECRUIT", "recruit"), ("EASY", "easy"), ("NORMAL", "normal"), ("HEROIC", "heroic"), ("VETERAN", "veteran"), ("LEGENDARY", "legendary")],
         "How well the bots aim, react and throw grenades.", None),
        ("TEAMS:", "bots.teams", [("EVEN", "even"), ("AGAINST YOU", "against")],
         "EVEN splits the bots between the teams.\nAGAINST YOU puts them all on the other team.", None),
    ],
}
_network = SCREENS["network_setup"]
# (and the multiplayer HUD's rows, and streamer mode, screens of their own:
# Network Setup has room for nine places)
_hud_settings = ("display.player_names", "display.player_name_scale", "display.scoreboard_team_layout",
                 "display.scoreboard_background")
_hud_rows = [row for row in _network["rows"] if row[1] in _hud_settings]
_network["rows"] = [row for row in _network["rows"] if row[1] not in _hud_settings]
_network["categories"] = [
    ("MAP SHARING:", "network_setup/map_sharing",
     "Downloading Custom Edition maps you do not have,\nand helping send them to other players.",
     "desktop android"),
    ("MULTIPLAYER HUD:", "network_setup/multiplayer_hud",
     "Players' names above their heads, the\nscoreboard, the text chat and the match clock."),
    ("STREAMER MODE:", "network_setup/streamer_mode",
     "Hide offensive words in names and text from\nother players, maps and mods, as stars."),
    ("SERVER BROWSER:", "network_setup/server_browser",
     "What the Server Browser lists and its order: the\nsame settings as its Filters screen."),
    # (not the Wii U's: linking opens the master's page in a browser,
    # player_link.c's platform_open_url, which the console does not)
    ("DISCORD ACCOUNT:", "network_setup/discord_account",
     "Link a Discord account, which lets you join\nservers that block VPNs while you use one.",
     "desktop android web"),
]
SCREENS["network_setup/map_sharing"] = {
    "screen": "map_sharing_settings_screen", "header": _network["header"], "spacing": 30,
    "rows": [
        ("DOWNLOAD MAPS:", "network.map_download", ON_OFF,
         "Joining a game on a Custom Edition map you do not\nhave: download it from the players (if shared).",
         None),
        ("HELP SEND MAPS:", "network.map_seed", ON_OFF,
         "In another's game, send its map to players who\njoin without it, as a torrent's peers do.", None),
        ("UPLOAD LIMIT:", "network.map_seed_upload",
         [(f"{kb} KB/S" if kb < 1024 else f"{kb // 1024} MB/S", str(kb)) for kb in SEED_UPLOADS],
         "The most you send of maps to other players, so\nthat the game keeps the rest of your upload.", None),
    ],
}

# Network Setup's server browser (menu_functions.c's lobby_browser_update): where
# its games come from (network.listings, master_list.c), the Filters screen's
# settings and its order; the master servers that answered, shown, not set
# (a row with no values: its value's text, menu_functions.c's
# master_servers_show)
SCREENS["network_setup/server_browser"] = {
    "screen": "server_browser_settings_screen", "header": _network["header"], "spacing": 30,
    "rows": [
        ("LISTS:", "network.listings",
         [("ALL GAMES", "both"), ("DEDICATED", "masters"), ("P2P GAMES", "brokers")],
         "The master servers' dedicated servers, the games\nplayers host (through the brokers), or all of them.", None),
        ("SHOW EMPTY SERVERS:", "network.browser_show_empty", YES_NO, "List games no one is playing yet.", None),
        ("SHOW FULL SERVERS:", "network.browser_show_full", YES_NO, "List games with no room left.", None),
        ("SHOW MISSING MAPS:", "network.browser_maps",
         [("NO", "no"), ("YES", "yes"), ("IF DOWNLOADABLE", "downloadable")],
         "List games on maps you do not have: IF\nDOWNLOADABLE, those Map Sharing can download.", None),
        # (a column title chosen again writes the order reversed, "-players": the row shows its order's word,
        # menu_functions.c's setting_value_index, and writes it again only if another is chosen)
        ("SORT BY:", "network.browser_sort",
         [("DEDICATED FIRST", "servers"), ("PLAYERS", "players"), ("NAME", "name"), ("MAP", "map"),
          ("GAME TYPE", "type"), ("LOCKED FIRST", "locked")],
         "The list's order. Its column titles sort it too;\nchoosing one again reverses it.", None),
        ("MASTER SERVERS:", "network.masters_file", None,
         "The master servers (masters.txt) that answered\nwhen the Server Browser last asked.", None,
         "master_servers"),
    ],
}
# (the read-only row's words, its screen's words: menu_functions.c's
# master_servers_show)
SCREENS["network_setup/server_browser"]["words"] = ["OF", "NONE LISTED", "NOT ASKED YET", "OFF"]

# Network Setup's Discord account (port/linux/src/player_link.c): the account
# linked at the master servers (masters.txt's, up to 4), which a dedicated
# server that blocks VPNs lets in from one when it is listed by the master the
# account is linked with. A row a master: its state; LINK WITH MASTER n opens
# that master's page in the browser (on OK; the master a refusing server needs
# is chosen already), UNLINK ALL forgets the account (link.action is no
# setting of config.toml's: menu_functions.c's setting_write does it). The
# rows' text: menu_functions.c's discord_account_show, its words
SCREENS["network_setup/discord_account"] = {
    "screen": "discord_account_settings_screen", "header": _network["header"], "spacing": 30,
    "wide_values": ["link_master_1", "link_master_2", "link_master_3", "link_master_4"],
    "rows": [
        ("DISCORD ACCOUNT:", "network.link_share", None,
         "The Discord account linked to this game, and with\nhow many of the master servers.", None,
         "discord_account"),
        *[(f"MASTER {n}:", "network.masters_file", None,
           "A master server (masters.txt) and the account\nlinked with it: a server it lists lets it in.", None,
           f"link_master_{n}") for n in range(1, 5)],
        ("LINK:", "link.action",
         [("NO CHANGE", "none")] + [(f"WITH MASTER {n}", f"link_{n}") for n in range(1, 5)] +
         [("UNLINK ALL", "unlink")],
         "WITH MASTER n opens that master's page in the\nbrowser (on OK). UNLINK ALL forgets the account.", None,
         "link_action"),
        ("SHOW TO SERVERS:", "network.link_share",
         [("WHEN ASKED", "when_needed"), ("ALWAYS", "always"), ("NEVER", "never")],
         "Which servers see your Discord account: WHEN\nASKED, those that block VPNs, joined from one.", None),
    ],
    "words": ["NOT LINKED", "LINKED AS", "WAITING: CODE", "REVOKED", "EXPIRED:", "OF", "A SERVER NEEDS IT", "NONE"],
}

# Network Setup's multiplayer HUD: its rows, moved from Network Setup, and the
# text chat's (port/linux/game/network_chat.c)
SCREENS["network_setup/multiplayer_hud"] = {
    "screen": "multiplayer_hud_settings_screen", "header": _network["header"], "spacing": 30,
    "rows": _hud_rows + [
        ("TEXT CHAT:", "display.text_chat", [("SHOW", "true"), ("HIDE", "false")],
         "SHOW the text chat, or HIDE it: no lines, and\nits keys and D-pad Up open nothing.", None),
        ("CHAT TEXT:", "display.chat_style", [("COLORS", "colors"), ("WHITE", "white")],
         "The text chat's colors: COLORS, names in their\nteam's color; WHITE, as Custom Edition.", None),
        ("MATCH TIMER:", "display.match_timer", ON_OFF,
         "Draw a clock at the bottom right in multiplayer,\ncounting up from the start of the game.", None),
    ],
}

# Network Setup's streamer mode (port/linux/game/text_filter.c): words hidden
# in what other players, maps and mods show, each group from a severity of
# Surge AI's on (port/assets/text_filter)
STREAMER_LEVELS = [("OFF", "off"), ("SEVERE", "severe"), ("STRONG AND WORSE", "strong"), ("MILD AND WORSE", "mild")]
SCREENS["network_setup/streamer_mode"] = {
    "screen": "streamer_mode_settings_screen", "header": _network["header"], "spacing": 30,
    "rows": [
        ("STREAMER MODE:", "streamer.enabled", ON_OFF,
         "Hide offensive words in names and text from\nother players, maps and mods, as stars.", None),
        ("CENSOR SLURS:", "streamer.slurs", STREAMER_LEVELS,
         "Slurs about race, sexuality, gender and\ndisability.", None),
        ("CENSOR SWEARING:", "streamer.swearing", STREAMER_LEVELS,
         "Swearing and sexual words (STRONG AND WORSE\nleaves the mildest showing).", None),
        ("CENSOR INSULTS:", "streamer.insults", STREAMER_LEVELS,
         "Name-calling and insults that are not slurs.", None),
    ],
}

# Network Setup in a game (the pause menu's SETTINGS: port/linux/game/
# menu_tags.c's pause_network_patch): its pages alone, without the rows that
# would drop the game (internet play) or are for the main menu (the router,
# the clipboard, the updates)
SCREENS["network_setup/in_game"] = {
    "screen": "in_game_network_settings_screen", "header": _network["header"], "spacing": 30,
    "platform_places": True, "rows": [],
    "categories": [category for category in _network["categories"] if category[1] != "network_setup/server_browser"],
}

# Controls Setup: the keyboard and mouse's actions, in groups (the order of
# port/linux/game/menu_functions.c's table of them)
CONTROL_GROUPS = ["MOVEMENT", "WEAPONS", "ACTIONS", "COMMUNICATION"]
CONTROL_ROWS = 7

# the settings whose rows have the wider spinner (a device's name)
WIDE_SETTINGS = {"audio.output_device", "audio.input_device", "streamer.slurs", "streamer.swearing",
                 "streamer.insults", "network.listings", "network.browser_maps", "network.browser_sort",
                 # (the gamepad layouts' names, 'LEGACY SOUTHPAW' and 'SWAP B, L TRIGGER', are 175 wide in the large font: the 147 of the plain spinner cut them off at the arrows)
                 "profile.button_preset", "profile.joystick_preset"}

# Teamplay Options' rows of the port's (_teamplay_options_extras): the
# gametype's FORCE TEAM SPAWNS, then the host's voice chat and vote kicks;
# each row's key, its label, its choices' words (menu_functions.c's
# gametype_options give their values) and their helps
TEAMPLAY_EDIT = "main_menu/settings_select/multiplayer_setup/teamplay_options_edit"
VOICE_QUALITIES = (8, 12, 16, 24, 32, 48, 64)
# (the rows' step: closer than the PC's 30px, so that all of Server Setup's
# fit above the help text, which moves down to TEAMPLAY_HELP_TOP)
TEAMPLAY_STEP = 26
TEAMPLAY_HELP_TOP = 351
# (game_engine.c's game_engine_force_team_spawns: the gametype's own, so
# shown in Edit Gametypes' copy too, and not on Server Setup's VOICE OPTIONS)
TEAMPLAY_GAMETYPE_ROWS = [
    ("force_team_spawns", "FORCE TEAM SPAWNS:", ["OFF", "ON"], [
        "Players will spawn at any spawn point the\\ngametype allows.",
        "Players will only spawn at their own team's\\nCapture the Flag spawn points.",
    ]),
]
TEAMPLAY_ROWS = [
    ("voice_mode", "VOICE CHAT:", ["OFF", "TEAM NEAR", "ANYONE NEAR", "TEAM", "TEAM, ENEMIES NEAR"], [
        "No voice chat during the game.",
        "Players hear their teammates who are near them.",
        "Players hear everyone who is near them.",
        "Players hear all their teammates, wherever they\\nare.",
        "Players hear all their teammates, and the enemies\\nwho are near them.",
    ]),
    ("voice_lobby", "LOBBY VOICE CHAT:", ["ON", "OFF"], [
        "Everyone hears everyone in the lobby, before and\\nafter the game.",
        "No voice chat in the lobby.",
    ]),
    ("voice_kbps", "VOICE QUALITY:", [f"{kbps} KBPS" for kbps in VOICE_QUALITIES],
     [f"Voices at {kbps} kilobits a second, in the lobby\\nand in the game." for kbps in VOICE_QUALITIES]),
    ("voice_proximity", "VOICE NEAR DISTANCE:", ["15 M", "30 M", "45 M", "60 M", "90 M", "150 M"],
     [f"Players {metres} metres apart or less are near\\nfor voice chat." for metres in (15, 30, 45, 60, 90, 150)]),
    # (network_votekick.c)
    ("votekick", "VOTE KICK:", ["ON", "OFF"], [
        "Players can vote to kick a player, from the\\nscoreboard.",
        "Players cannot vote to kick anyone.",
    ]),
    # (network_chat.c: one row for the chat and its links, as Teamplay
    # Options has room for one more)
    ("text_chat", "TEXT CHAT:", ["ON", "WITH LINKS", "OFF"], [
        "Players can type to each other; lines with links\\nare refused. Off in a public internet game.",
        "Players can type to each other, links too. Off\\nin a public internet game.",
        "No text chat in the game.",
    ]),
]
# (Teamplay Options' rows of the port's, in order)
TEAMPLAY_EXTRA_ROWS = TEAMPLAY_GAMETYPE_ROWS + TEAMPLAY_ROWS

# the profile menu's words for what its items now open
STRING_OVERRIDES = {
    f"{PE}/profile_edit_descriptions": [
        "Rename this profile.\\n\\n\\nProfile:",
        "Choose the keys and mouse\\nbuttons for each action.\\n\\nProfile:",
        "Set this profile's controller\\nsensitivity and layouts.\\n\\nProfile:",
        "Adjust the mouse's sensitivity\\nand aiming.\\n\\nProfile:",
        "Adjust the volume of the music\\nand of everything else.\\n\\nProfile:",
        "The picture: its resolution,\\nframe rate and detail.\\n\\nProfile:",
        "Internet play, updates and the\\nmultiplayer HUD.\\n\\nProfile:",
        "Change the current profile's\\nfree-for-all multiplayer color.\\n\\nProfile:",
        "Halo: Combat Evolved, the Xbox\\ngame, rebuilt by OpenCE.\\n\\nProfile:",
    ],
}


def attributes(pairs: list) -> str:
    return "".join(f" {key}={quoteattr(str(value))}" for key, value in pairs if value is not None)


def _widget(name: str, pairs: list, inner: list) -> list:
    return [f"\t<widget{attributes([('name', name)] + pairs)}>", *[f"\t\t{line}" for line in inner], "\t</widget>"]


def _strings(name: str, texts: list) -> list:
    return [f"\t<strings{attributes([('name', name)])}>",
            *[f"\t\t<string{attributes([('text', text)])}/>" for text in texts], "\t</strings>"]


def _button(name: str, caption: int, handlers: list) -> list:
    pairs = [("type", "text"), ("width", 128), ("height", 24), ("bitmap", "bitmaps/text_button_background"),
             ("string_list", "strings/common_button_captions"), ("string_index", caption), ("font", "ui\\small_ui"),
             ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)]
    return _widget(name, pairs, handlers + ['<on event="left_mouse" run="mouse emit accept event"/>'])


def _screen(folder: str, spec: dict, rows: list, list_inputs: list, list_handlers: list, extra: list) -> list:
    """a screen: its header, the list of rows (with the help line as its
    description), and the buttons"""
    base = folder if folder.startswith("main_menu/") else f"{PE}/{folder}"
    header, header_bitmap = spec["header"]
    lines = []
    # (a text header's screen has the social screens' header band behind it: tools/port_settings.py's _social_band)
    band = [f'<child widget="{base}/header_band" y="{PANEL_LINE - SOCIAL_BAND_HEIGHT}"/>'] if spec.get("header_text") else []
    lines += _widget(f"{base}/{spec['screen']}",
                     [("width", 640), ("height", 480), ("flags", "pass_unhandled_to_focused_child pause_game"),
                      ("bitmap", "bitmaps/gradient")],
                     ['<on event="b" back="true"/>', '<on event="back" back="true"/>', *band,
                      f'<child{attributes([("widget", f"{base}/{header}")])}/>',
                      f'<child{attributes([("widget", f"{base}/options_menu")])}/>'])
    if spec.get("header_text"):
        # (a text in the large font, as the map vote's: no picture of 2048 x 256 for a page the pause menu reaches in a game. Placed as the social screens' titles: the
        # capitals' top at the safe margin, 24, at the rows' left edge)
        lines += _social_band(base)
        lines += _widget(f"{base}/{header}", [("type", "text"), ("controller", 1), ("left", 54), ("top", SOCIAL_TITLE_TOP), ("width", 560), ("height", 22),
                                              ("font", "ui\\large_ui"), ("color", "#FF2995FD"), ("text", spec["header_text"]), ("text_x", 12), ("text_y", 0),
                                              ("text_flags", "no_focus_test")], [])
    else:
        lines += _widget(f"{base}/{header}", [("controller", 1), ("left", 35), ("top", 11), ("width", 605), ("height", 59),
                                              ("bitmap", header_bitmap)], [])
    lines += _widget(f"{base}/help", [("type", "text"), ("controller", 1), ("left", spec.get("help_left", 68)),
                                      ("top", spec.get("help_top", 350)), ("width", spec.get("help_width", 482)),
                                      ("height", spec.get("help_height", 60)), ("string_list", None if spec.get("dynamic_help") else f"{base}/help_strings"),
                                      ("font", "ui\\large_ui"), ("color", "#FFFFFFFF")], [])
    children = [f'<data input="{name}"/>' for name in list_inputs] + list_handlers
    # (each row: its widget, platform, and the place it is in, else the next)
    for index, (row, platform, *place) in enumerate(rows):
        y = spec.get("top", 73) + (place[0] if place else index) * spec["spacing"]
        children.append(f'<child{attributes([("widget", row), ("x", 54), ("y", y), ("platform", platform)])}/>')
    children.append(f'<child{attributes([("widget", f"{base}/button_bar"), ("y", 414)])}/>')
    lines += _widget(f"{base}/options_menu",
                     [("type", "column_list"), ("width", 640), ("height", 480),
                      ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                      ("description", f"{base}/help")], children)
    lines += _widget(f"{base}/button_bar",
                     [("type", "column_list"), ("width", 640), ("height", 28),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     [f'<child{attributes([("widget", f"{base}/button_defaults"), ("y", 1)])}/>',
                      f'<child{attributes([("widget", f"{base}/button_ok"), ("x", 380), ("y", 1)])}/>',
                      '<child widget="common_button_cancel" x="510" y="1"/>'])
    lines += extra
    return lines


def _placed(row: str, platform, members: list, places: dict) -> list:
    """a row's children (row, platform, place): one for its platforms where
    they have it in the same place, else one for each place, with the
    platforms whose place it is"""
    groups = {}
    for name in members:
        groups.setdefault(places[name], []).append(name)
    if len(groups) == 1:
        return [(row, platform, places[members[0]])]
    return [(row, " ".join(names) if len(names) < len(places) else None, place) for place, names in groups.items()]


def _setting_screen(folder: str, spec: dict) -> list:
    base = f"{PE}/{folder}"
    rows, extra = [], []
    place = -1
    # (platform_places: each platform's rows in places of their own, with
    # no gap where the others' are; a row in one place for all its
    # platforms, else a child for each place)
    places = {"desktop": -1, "android": -1, "web": -1, "wiiu": -1}
    for index, (label, setting, choices, _, platform, *named) in enumerate(spec["rows"]):
        key = named[0] if named else setting.split(".", 1)[1]
        row = f"{base}/op_{key}"
        shares = setting in spec.get("same_place", ()) or key in spec.get("same_place", ())
        if spec.get("platform_places"):
            members = platform.split() if platform else list(places)
            for name in members:
                if not shares:
                    places[name] += 1
            rows += _placed(row, platform, members, places)
        else:
            if setting not in spec.get("same_place", ()) and key not in spec.get("same_place", ()):
                place += 1
            rows.append((row, platform, place))
        # (a row with no values shows its value, set by the game: its text; a
        # wide one, a short label and a long value in the small font)
        if choices is None:
            wide_value = key in spec.get("wide_values", ())
            label_width, value_x, value_width = (130, 130, 382) if wide_value else (300, 286, 212)
            extra += _widget(row, [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                                   ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF"), ("platform", platform)],
                             [f'<child{attributes([("widget", f"{base}/{key}_label")])}/>',
                              f'<child{attributes([("widget", f"{base}/{key}_value"), ("x", value_x), ("y", 1)])}/>'])
            extra += _widget(f"{base}/{key}_label",
                             [("type", "text"), ("controller", 1), ("width", label_width), ("height", 22),
                              ("string_list", f"{base}/labels"), ("string_index", index), ("font", "ui\\large_ui"),
                              ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
            extra += _widget(f"{base}/{key}_value",
                             [("type", "text"), ("controller", 1), ("width", value_width), ("height", 22),
                              ("font", "ui\\small_ui" if wide_value else "ui\\large_ui"), ("color", "#FF2896FF"),
                              ("align", "left" if wide_value else "center"), ("text_y", 5 if wide_value else 1),
                              ("text_flags", "no_focus_test")], [])
            continue
        # (a device's name wants the wider spinner Server Setup's co-op rows
        # have: WIDE_SETTINGS)
        wide = setting in WIDE_SETTINGS
        bumper_style = setting == "display.hud_bumper_style"
        controller_style = setting.startswith("display.hud_button_style")
        # Dynamic captions retain their help index without a string list that
        # would overwrite the player number during the engine's text update.
        extra += _widget(row, [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                               ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF"), ("platform", platform)],
                         [f'<child{attributes([("widget", f"{base}/{key}_label")])}/>',
                          f'<child{attributes([("widget", f"{base}/{key}_spinner"), ("x", 240 if controller_style or bumper_style else 286 if wide else 320), ("y", 1)])}/>'])
        extra += _widget(f"{base}/{key}_label",
                         [("type", "text"), ("controller", 1), ("width", 300), ("height", 22),
                          ("string_list", None if controller_style else f"{base}/labels"),
                          ("string_index", index), ("font", "ui\\large_ui"),
                          ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
        extra += _widget(f"{base}/{key}_spinner",
                         [("type", "spinner"), ("left", None if wide else 3), ("top", 2), ("width", 252 if controller_style or bumper_style else 206 if wide else 147),
                          ("height", 20), ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                          ("strings", "|".join(shown for shown, _ in choices)), ("setting", setting),
                          ("values", "|".join(value for _, value in choices)), ("font", "ui\\large_ui"),
                          ("color", "#FF2896FF"), ("align", "center"), ("text_y", 4 if wide else 1),
                          ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                          ("header_bounds", "7 -13 19 -7" if wide else "7 -6 19 0"),
                          ("footer_bounds", "7 254 19 260" if controller_style or bumper_style else "7 208 19 214" if wide else "7 150 19 156")],
                         ['<on event="created" run="port setting load"/>'])
    # (each category's row: its label, the screen it opens, its help, and the
    # platforms it is for, as a row's: every one's when it names none)
    for index, (label, category_folder, _, *category_platform) in enumerate(spec.get("categories", ())):
        key = category_folder.rsplit("/", 1)[-1]
        row = f"{base}/op_{key}"
        target = f"{PE}/{category_folder}/{SCREENS[category_folder]['screen']}"
        platform = category_platform[0] if category_platform else None
        if spec.get("platform_places"):
            members = platform.split() if platform else list(places)
            for name in members:
                places[name] += 1
            rows += _placed(row, platform, members, places)
        else:
            rows.append((row, platform, place + 1 + index))
        extra += _widget(row, [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                               ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF"), ("platform", platform)],
                         [f'<on event="a" open="{target}"/>', f'<on event="start" open="{target}"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          f'<child widget="{base}/{key}_label"/>'])
        extra += _widget(f"{base}/{key}_label",
                         [("type", "text"), ("controller", 1), ("width", 512), ("height", 22),
                          ("string_list", f"{base}/labels"), ("string_index", len(spec["rows"]) + index),
                          ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    extra += _button(f"{base}/button_defaults", 3, ['<on event="a" run="port settings defaults"/>',
                                                    '<on event="start" run="port settings defaults"/>'])
    extra += _button(f"{base}/button_ok", 1, ['<on event="a" run="port settings save" back="true"/>',
                                              '<on event="start" run="port settings save" back="true"/>'])
    extra += _strings(f"{base}/labels", [label for label, *_ in spec["rows"]] +
                      [label for label, *_ in spec.get("categories", ())])
    if spec.get("words"):
        extra += _strings(f"{base}/words", spec["words"])
    # (the help of the row whose label is string n is n + 1: the buttons' is 0)
    extra += _strings(f"{base}/help_strings",
                      [""] + [help_text.replace("\n", "\\n") for _, _, _, help_text, *_ in spec["rows"]] +
                      [help_text.replace("\n", "\\n") for _, _, help_text, *_ in spec.get("categories", ())])
    return _screen(folder, spec, rows, ["port settings help", *spec.get("inputs", ())], [], extra)


def _controls_screen() -> list:
    folder = "controls_setup"
    base = f"{PE}/{folder}"
    spec = {"screen": "controls_settings_screen", "spacing": 30,
            "header": ("header_profile_controls_settings", f"{base}/header_profile_controls_settings")}
    rows, extra = [(f"{base}/op_group", None)], []
    extra += _widget(f"{base}/op_group", [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                                          ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                     [f'<child{attributes([("widget", f"{base}/group_label")])}/>',
                      f'<child{attributes([("widget", f"{base}/group_spinner"), ("x", 200), ("y", 1)])}/>'])
    extra += _widget(f"{base}/group_label",
                     [("type", "text"), ("controller", 1), ("width", 200), ("height", 22), ("text", "SHOW:"),
                      ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    extra += _widget(f"{base}/group_spinner",
                     [("type", "spinner"), ("left", 3), ("top", 2), ("width", 297), ("height", 20),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                      ("strings", "|".join(CONTROL_GROUPS)), ("font", "ui\\large_ui"), ("color", "#FF2896FF"),
                      ("align", "center"), ("text_y", 1), ("header_bitmap", "bitmaps/arrow_sm_left"),
                      ("footer_bitmap", "bitmaps/arrow_sm_right"), ("header_bounds", "7 -6 19 0"),
                      ("footer_bounds", "7 300 19 306")], [])
    for index in range(CONTROL_ROWS):
        row = f"{base}/op_command_{index + 1}"
        rows.append((row, None))
        extra += _widget(row, [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                               ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         ['<on event="a" run="controls begin binding"/>',
                          '<on event="start" run="controls begin binding"/>',
                          '<on event="left right" run="controls binding slot"/>',
                          f'<child{attributes([("widget", f"{base}/command_label")])}/>',
                          f'<child{attributes([("widget", f"{base}/command_binding"), ("x", 200)])}/>',
                          f'<child{attributes([("widget", f"{base}/command_binding"), ("x", 356)])}/>'])
    extra += _widget(f"{base}/command_label",
                     [("type", "text"), ("controller", 1), ("width", 200), ("height", 22), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    extra += _widget(f"{base}/command_binding",
                     [("type", "text"), ("controller", 1), ("width", 152), ("height", 22), ("font", "ui\\small_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 5), ("text_flags", "no_focus_test")],
                     [])
    extra += _button(f"{base}/button_defaults", 3, ['<on event="a" run="controls screen defaults"/>',
                                                    '<on event="start" run="controls screen defaults"/>'])
    extra += _button(f"{base}/button_ok", 1, ['<on event="a" run="controls screen change set" back="true"/>',
                                              '<on event="start" run="controls screen change set" back="true"/>'])
    extra += _strings(f"{base}/help_strings", [
        "Choose what to show, with left and right.",
        "Enter sets the key chosen; left and right choose\\nwhich of two. Then press the new key or button.",
        "Press a key, a mouse button or turn the wheel...\\nEscape or a controller's B cancels, Delete clears it.",
    ])
    lines = _screen(folder, spec, rows, ["controls update menu"], ['<on event="created" run="controls screen init"/>'],
                    extra)
    # (B cancels a key's setting first)
    return [line.replace('<on event="b" back="true"/>', '<on event="b" run="controls back handler"/>')
            .replace('<on event="back" back="true"/>', '<on event="back" run="controls back handler"/>')
            for line in lines]


def _controller_screen() -> list:
    folder = "controller_remapping"
    base = f"{PE}/{folder}"
    rows, extra = [], []
    for key, label, choices in [("player", "PLAYER:", "P1|P2|P3|P4"),
                                ("group", "SHOW:", "MOVEMENT|COMBAT|GENERAL|COMMUNICATION|DEBUG")]:
        row = f"{base}/op_{key}"
        rows.append((row, None))
        extra += _widget(row, [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                               ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         [f'<child widget="{base}/{key}_label"/>', f'<child widget="{base}/pad_{key}" x="200" y="1"/>'])
        extra += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1), ("width", 200),
                         ("height", 22), ("text", label), ("font", "ui\\large_ui"),
                         ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
        extra += _widget(f"{base}/pad_{key}", [("type", "spinner"), ("left", 3), ("top", 2),
                         ("width", 297), ("height", 20), ("strings", choices),
                         ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                         ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("align", "center"), ("text_y", 1),
                         ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                         ("header_bounds", "7 -6 19 0"), ("footer_bounds", "7 300 19 306")], [])
    for index in range(6):
        row = f"{base}/op_pad_{index + 1}"
        rows.append((row, None))
        extra += _widget(row, [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                               ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         ['<on event="a start" run="port controller bind"/>',
                          '<on event="x" run="port controller clear"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          '<on event="left" run="port controller left"/>',
                          '<on event="right" run="port controller right"/>',
                          f'<child widget="{base}/pad_action"/>',
                          f'<child widget="{base}/pad_binding" x="220"/>'])
    extra += _widget(f"{base}/pad_action", [("type", "text"), ("controller", 1), ("width", 220),
                     ("height", 22), ("font", "ui\\small_ui"), ("color", "#FF2896FF"),
                     ("text_x", 13), ("text_y", 4)], [])
    extra += _widget(f"{base}/pad_binding", [("type", "text"), ("controller", 1), ("width", 280),
                     ("height", 22), ("font", "ui\\large_ui"), ("color", "#FF2896FF"),
                     ("align", "center"), ("text_y", 4), ("text_flags", "no_focus_test")], [])
    extra += _button(f"{base}/button_defaults", 3, ['<on event="a start" run="port controller defaults"/>'])
    extra += _button(f"{base}/button_ok", 1, ['<on event="a start" run="port controller save" back="true"/>'])
    lines = _screen(folder, SCREENS[folder], rows, ["port controller update"],
                   ['<on event="created" run="port controller init"/>',
                    '<on event="deleted" run="port controller close"/>'], extra)

    return [line.replace('<on event="b" back="true"/>', '<on event="b" run="port controller cancel" back="true"/>')
            .replace('<on event="back" back="true"/>', '<on event="back" run="port controller cancel" back="true"/>')
            for line in lines]


def settings_files() -> dict:
    """the files of the port's screens, by their names in ce/"""
    files = {}
    for folder, spec in SCREENS.items():
        files[f"{PE}/{folder}".replace("/", ".") + ".xml"] = (
            _controller_screen() if folder == "controller_remapping" else _setting_screen(folder, spec))
    files[f"{PE}/controls_setup".replace("/", ".") + ".xml"] = _controls_screen()
    return {name: ['<?xml version="1.0" encoding="UTF-8"?>',
                   "<!-- The port's settings screen, in the PC version's style (tools/port_settings.py) -->",
                   "<menus>", *lines, "</menus>", ""] for name, lines in files.items()}


def replaced(folder: str) -> bool:
    """whether the PC version's folder of widgets is replaced by ours (and
    those below it: the controls' advanced screen, the video test)"""
    return any(folder == path or folder.startswith(f"{path}/")
               for path in [*(f"{PE}/{name}" for name in [*SCREENS, "controls_setup"]), *REPLACED_FOLDERS])


# ---------- multiplayer (PLAN.md section 11)

MT = "main_menu/multiplayer_type_select"

# strings added to the PC version's lists: (at, strings) of each list. Slayer's
# kills to win goes up to 500, for big games (the Xbox editor's are
# source/interface/ui_widget.c's kills_to_win_extra_strings): the values,
# and their helps after the five of its own (its helps are the rows' values'
# in turn: menu_functions.c's gametype_option_help)
SLAYER_EDIT = "main_menu/settings_select/multiplayer_setup/playlist_edit/slayer_edit"
PLAYER_EDIT = "main_menu/settings_select/multiplayer_setup/player_options_edit"
# (its rows, the PC version's, in order: ce_menus.py moves them 27 apart)
PLAYER_OPTIONS_ROWS = ['number_of_lives', 'maximum_health', 'shields', 'respawn_time', 'respawn_time_growth', 'odd_man_out', 'invisible_players', 'suicide_penalty']
# the vehicle sets' PC set (game_engine.h's VARIANT_VEHICLE_SET_PC), between
# GUN TURRETS and CUSTOM (menu_functions.c's VEHICLE_PRESET_PC), and its help
# among the sets' (its rows' values' in turn: gametype_option_help)
VEHICLE_EDIT = "main_menu/settings_select/multiplayer_setup/vehicle_options_edit"
AIM_ASSIST_HELP = [
    "Players keep their current mouse aim assist setting.",
    "Disable mouse aim assist for mouse/keyboard players.\\nController and touch aim assist are unchanged.",
]
# the host's shield effects, for every player of a network game
# (network_shield_effects.c): their words, then their helps
SHIELD_EFFECTS = ['STOCK', 'GLOW', 'LIGHT SHOW', 'OVERSHIELD', 'GLOW+OVERSHIELD', 'SHOW+OVERSHIELD',
                  "PLAYERS' OWN"]
SHIELD_EFFECTS_HELP = [
    "Everyone sees the stock game's shields and\\novershield, whatever their own settings.",
    "Everyone's shields light their surroundings as\\nthey flare.",
    "Everyone's shields light their surroundings as\\nthey flare, three times as far (Light Show).",
    "For everyone, the overshield rises up the body\\nand stays lit, in its own color.",
    "For everyone, shields light their surroundings,\\nand the overshield rises and stays lit.",
    "For everyone, Light Show's shield glow, and the\\novershield rising and staying lit.",
    "Not synced: each player sees the shield effects\\nof their own Video Setup settings.",
]

# OpenCE's gametypes' options (port/linux/game/game_modes.c), a screen of the
# gametype editor's (menu_functions.c's "mp profile init opence options"): the
# mode and Hardcore, then the mode's own rows, which menu_functions.c's
# gametype_option_help shows (op_inf_ for Infection, op_gg_ for Gun Game)
# below them; a row's values are menu_functions.c's gametype_options'
PLAYLIST_EDIT = "main_menu/settings_select/multiplayer_setup/playlist_edit"
OPENCE_EDIT = "main_menu/settings_select/multiplayer_setup/opence_options_edit"
OPENCE_ROWS = [
    ("oce_mode", "MODE:", ["NONE", "SWAT", "INFECTION", "GUN GAME"], [
        "The game type's own rules.",
        "No shields, and a shot to the head kills. Slayer only.",
        "Humans killed join the zombies, who win when\\nno human is left. Team Slayer only.",
        "Each kill gives the next weapon; a kill with the\\nlast wins. Free for all Slayer only."]),
    ("oce_hardcore", "HARDCORE:", ["OFF", "ON"], [
        "The motion sensor and aim assist as the game\\ntype has them.",
        "No motion sensor, no aim assist, and a kill\\nrestores the killer's health and shields."]),
    ("inf_zombies", "FIRST ZOMBIES:", ["1", "2", "3", "4", "8", "16"],
     ["The zombies chosen when the round starts\\n(a human is always left)."] * 6),
    ("inf_speed", "ZOMBIE SPEED:", ["100%", "110%", "125%", "150%", "175%", "200%"],
     ["How fast zombies move, of a human's speed."] * 6),
    ("inf_damage", "SKULL DAMAGE:", ["100%", "150%", "200%", "250%"],
     ["The damage of a zombie's skull's melee."] * 4),
    ("inf_respawn", "ZOMBIE RESPAWN:", ["1 SECOND", "3 SECONDS", "5 SECONDS", "10 SECONDS", "15 SECONDS",
                                        "30 SECONDS"],
     ["How long a dead zombie waits to respawn."] * 6),
    ("inf_countdown", "COUNTDOWN:", ["NONE", "5 SECONDS", "10 SECONDS", "15 SECONDS", "30 SECONDS"],
     ["How long the round waits, once two players are\\nalive, before the first zombies are chosen."] * 5),
    ("inf_shields", "ZOMBIE SHIELDS:", ["OFF", "ON"],
     ["Zombies have no shields.", "Zombies have shields, as humans do."]),
    ("gg_order", "WEAPON ORDER:", ["STRONGEST FIRST", "WEAKEST FIRST", "RANDOM"], [
        "From the rocket launcher to the pistol.", "From the pistol to the rocket launcher.",
        "In an order the host draws for each game."]),
    ("gg_melee", "MELEE DEMOTES:", ["OFF", "ON"], [
        "A melee kill takes nothing from the victim.",
        "A melee kill takes the victim back a weapon."]),
]
OPENCE_ITEM = f"{PLAYLIST_EDIT}/playlist_edit_opence_item"

STRING_INSERTS = {
    # (Filters' SHOW MISSING MAPS' third value's help, after the PC version's:
    # menu_functions.c's filters_help)
    f"{MT}/join_game/filters/cap_filter_options": [(11, [
        "Show servers on maps you do not have if their\\nhost shares them: they download as you join."])],
    # (the gametype editor's OPENCE OPTIONS' words, after the PC version's:
    # menu_functions.c's playlist_edit_description)
    f"{PLAYLIST_EDIT}/playlist_edit_list_extended_dsc_text": [(8, [
        "Choose the rules of OpenCE's\\ngametypes: SWAT, Infection,\\nGun Game and Hardcore.\\nThis gametype:"])],
    f"{PLAYER_EDIT}/player_options_labels": [(8, ["AIM ASSIST:", "SHIELD EFFECTS:"])],
    f"{PLAYER_EDIT}/cap_player_options": [(28, AIM_ASSIST_HELP + SHIELD_EFFECTS_HELP)],
    # (Teamplay Options' voice chat and vote kick rows, after its own:
    # TEAMPLAY_ROWS)
    f"{TEAMPLAY_EDIT}/teamplay_options_labels": [(3, [label for _, label, *_ in TEAMPLAY_EXTRA_ROWS])],
    f"{TEAMPLAY_EDIT}/cap_teamplay_options": [(10, [text for *_, helps in TEAMPLAY_EXTRA_ROWS for text in helps])],
    f"{SLAYER_EDIT}/var_kills_to_win": [(5, ["75", "100", "150", "200", "250", "500"])],
    f"{VEHICLE_EDIT}/var_vehicle_set": [(8, ["PC"])],
    f"{VEHICLE_EDIT}/cap_vehicle_options": [(17, [
        "Every vehicle the map places will spawn, as in\\nHalo PC.",
    ])],
    f"{SLAYER_EDIT}/cap_slayer": [(11, [
        "Seventy-five kills to win. Settle in for a long\\nfight.",
        "A hundred kills to win. Made for big games.",
        "A hundred and fifty kills to win. Only a crowded\\nserver gets there.",
        "Two hundred kills to win. Bring friends. Lots of\\nthem.",
        "Two hundred and fifty kills to win.",
        "Five hundred kills to win. You'll be here a while.",
    ])],
}


# a custom loadout's weapons (game_engine.h's _loadout_weapon_*)
LOADOUT_WEAPONS = ["NONE", "RANDOM", "ASSAULT RIFLE", "PISTOL", "SHOTGUN", "SNIPER RIFLE", "ROCKET LAUNCHER",
                   "PLASMA PISTOL", "PLASMA RIFLE", "NEEDLER"]


def LOADOUT_HELPS(slot: str) -> list:
    """the helps of a custom loadout's weapon's values"""
    helps = [f"No {slot} weapon.", f"A random {slot} weapon, of those the map has,\\neach time you spawn."]
    for weapon in LOADOUT_WEAPONS[2:]:
        helps.append(f"Everyone spawns with a {weapon.lower()} as their {slot}\\nweapon (where the map has one).")
    return helps


STRING_OVERRIDES.update({
    f"{MT}/multiplayer_options": ["JOIN GAME", "CREATE GAME", "INTERNET", "LAN", "DIRECT LINK", "EDIT GAMETYPES",
                                  "SERVER BROWSER", "CO-OP CAMPAIGN"],
    f"{MT}/multiplayer_option_descriptions": [
        "Browse the games on the\\nInternet.",
        "Join a multiplayer game on\\nyour LAN.",
        "Join a game by its invite link,\\nor one a Discord invite\\nreached.",
        "Host a game on the Internet:\\nplayers join by its invite\\nlink or Discord.",
        "Host a game on your LAN\\nonly.",
        "Play the campaign with a\\nfriend in split screen:\\nplayer 2 on a gamepad.",
        "Set all the attributes for your \\nmultiplayer gametypes and\\nkeep them for future use.",
        # (the bar of buttons at the bottom: FRIENDS, then BACK, which the PC version's list has no description for: it showed <missing string>)
        "See your friends, requests and\\nparty. Add a friend by code,\\njoin a game or invite one.",
        "Go back to the main menu.",
    ],
    # (and the port's OPENCE: OpenCE's gametypes, port/linux/game/game_modes.c)
    "main_menu/gametype_select/var_gametype_banks": ["STANDARD", "OPENCE", "CUSTOM"],
    # (the PC's weapon sets, then the Xbox's NO GRENADES: menu_functions.c's
    # gametype_options maps them to the engine's)
    "main_menu/settings_select/multiplayer_setup/item_options_edit/item_options_labels": ["INFINITE GRENADES:", "WEAPON SET:", "VEHICLE SET:", "STARTING EQUIPMENT:",
                                  "VEHICLE RESPAWN TIME:", "LOADOUT:", "PRIMARY WEAPON:", "SECONDARY WEAPON:",
                                  "MAP WEAPONS:"],
    # (the helps of each option's values in turn: menu_functions.c's
    # gametype_option_help)
    "main_menu/settings_select/multiplayer_setup/item_options_edit/cap_item_options": [
        "Everyone has a limitless supply of grenades, which\\nmakes for loud, explosive and occasionally messy fun.",
        "Each player can only carry up to 4 grenades of each\\ntype at any given time.",
        "The map will contain whatever weapons the designers\\nplaced on it.",
        "All the weapons on the map will be replaced by\\npistols.",
        "All the weapons on the map will be replaced by\\nassault rifles and plasma rifles.",
        "All the weapons on the map will be replaced by\\nplasma weapons.",
        "The guns on the map will be replaced by weapons\\nwith sniper scopes.",
        "Sniper rifles and pistols will not appear on the map.",
        "All the weapons on the map will be replaced by\\nrocket launchers.",
        "All the weapons on the map will be replaced by\\nshotguns.",
        "All the weapons on the map are only effective at\\nshort ranges.",
        "There are no weapons on the map that were made in\\nCovenant sweat shops.",
        "Only weapons made by the Covenant will appear\\nin the map.",
        "The flamethrower and fuel rod gun will not appear\\nin the map.",
        "Only the most devastating and dangerous of weapons\\nwill appear in the map.",
        "The map's weapons, without any grenades.",
        "You will start the game with the weapon set that the \\nmap designers custom tailored for it.",
        "Use the generic starting weapons across all maps.",
        "The map's weapons appear where its designers\\nplaced them.",
        "No weapons appear on the map: with a loadout of\\nNONE and NONE, melee and grenades only.",
        "The weapons follow the weapon set above.",
        "Everyone starts with the primary and secondary\\nweapons below; the map's weapons are its own.",
        *LOADOUT_HELPS("primary"),
        *LOADOUT_HELPS("secondary"),
    ],
    "main_menu/settings_select/multiplayer_setup/item_options_edit/var_weapon_set": [
        "NORMAL", "PISTOLS", "RIFLES", "PLASMA WEAPONS", "SNIPER", "NO SNIPING", "ROCKET LAUNCHERS", "SHOTGUNS",
        "SHORT RANGE", "HUMAN", "COVENANT", "CLASSIC", "HEAVY WEAPONS", "NO GRENADES"],
})

# which of STRING_OVERRIDES' strings say what one of the PC version's own
# says: its index in the PC version's list, or None (the port's own). A map
# in another language than English has those in its language (map=,
# menu_tags.c's map_strings); the inserted strings (STRING_INSERTS) are the
# port's own, the rest of their lists the PC version's
OVERRIDE_ORIGINALS = {
    f"{PE}/profile_edit_descriptions": [0, None, None, None, None, None, None, 7, None],
    f"{MT}/multiplayer_options": [0, 1, 2, 3, None, 5, None, None],
    f"{MT}/multiplayer_option_descriptions": [0, 1, None, None, 4, None, 5, None, None],
    "main_menu/gametype_select/var_gametype_banks": [0, None, 2],
    "main_menu/settings_select/multiplayer_setup/item_options_edit/item_options_labels": [0, 1, 2, 3, 4] + [None] * 4,
    # (the first's note that infinite grenades work only with 4 players or
    # fewer is left out: not so here)
    "main_menu/settings_select/multiplayer_setup/item_options_edit/cap_item_options":
        [None] + list(range(1, 15)) + [None, 15, 16] + [None] * 24,
    "main_menu/settings_select/multiplayer_setup/item_options_edit/var_weapon_set": list(range(13)) + [None],
}

# the map lists' first row: SINGLEPLAYER, MULTIPLAYER, CUSTOM SINGLEPLAYER or
# CUSTOM MULTIPLAYER maps (_map_kind)
MAP_KIND_CHOOSER = "main_menu/new_select/list_item_0_map_kind"

# changes to the PC version's widgets (by our names): attributes set, all
# their handlers replaced, children added, game data inputs added
# the Wii U's profile menu: no keyboard or mouse rows (Controls Setup binds keys, Mouse Setup the mouse's), and the rows
# below them in their places, one or two rows' step (33) up (each row's description is its label's, not its place's:
# ui_widget_game_data_input_functions.c)
WIIU_PROFILE_ROWS = [("gamepads_profile_item", -33), ("audio_settings_profile_item", -66), ("video_settings_profile_item", -66),
                     ("network_settings_profile_item", -66), ("color_profile_item", -34), ("about_item", -34)]

WIDGET_PATCHES = {
    f"{PE}/profile_edit_select_list": {
        "child_platforms": {f"{PE}/{item}": "desktop android web" for item in
                            ["controller_setup_profile_item", "mouse_settings_profile_item"] + [item for item, _ in WIIU_PROFILE_ROWS]},
        "insert_before": {f"{PE}/profile_edit_button_bar": [f'<child widget="{PE}/{item}" y="{y}" platform="wiiu"/>'
                                                            for item, y in WIIU_PROFILE_ROWS]},
    },
    # The host returns here after a round with no menu history. Back must
    # reopen the main menu when it closes the last screen, as the lobby does.
    f"{MT}/connected/connected_map_select_wrapper": {"set": {
        "flags": "pass_unhandled_to_focused_child pause_game main_menu_if_no_history tag_controller_index force_handle_mouse",
    }},
    # (the main menu's MODS, before Quit: _mods)
    "main_menu/main_menu_select_list": {"insert_before": {"main_menu/main_menu_item_quit_game": [
        f'<child widget="main_menu/main_menu_item_mods" x="{MODS_ITEM_PLACE[0]}" y="{MODS_ITEM_PLACE[1]}"/>']}},
    f"{PLAYER_EDIT}/player_options_menu": {"insert_before": {
        f"{PLAYER_EDIT}/player_button_bar": [
            f'<child widget="{PLAYER_EDIT}/op_aim_assist" x="54" y="289"/>',
            f'<child widget="{PLAYER_EDIT}/op_shield_effects" x="54" y="316"/>']}},
    # (its rows 27 apart, not 30, so that ten fit above the help, which keeps
    # the three lines some have: ce_menus.py's CHILD_OFFSETS)
    f"{PLAYER_EDIT}/player_options_help": {"set": {"top": 345, "height": 60}},
    # (the gametype editor's OPENCE OPTIONS, after its own rows: _opence_options)
    f"{PLAYLIST_EDIT}/edit_playlist_select_list": {
        "insert_before": {f"{PLAYLIST_EDIT}/playlist_edit_button_bar": [f'<child widget="{OPENCE_ITEM}"/>']},
        "inputs": ["port playlist edit desc"]},
    # (the profile settings' picture: on Gamepad Setup's row, the profile's
    # button settings, BITMAP_FRAMES; menu_functions.c's
    # profile_gamepad_layout)
    f"{PE}/profile_edit_extended_desc_pic": {"inputs": ["port gamepad layout preview"]},
    # (straight to their screens: no "checking for updates" dialog, which
    # asked the PC version's servers)
    f"{MT}/multiplayer_type_join_internet_item": {"set": {"string_index": 6}, "handlers": [
        f'<on event="a" run="mp type set mode" open="{MT}/join_game/join_game_screen"/>',
        f'<on event="start" run="mp type set mode" open="{MT}/join_game/join_game_screen"/>',
        '<on event="left_mouse" run="mouse emit accept event"/>',
    ]},
    f"{MT}/multiplayer_type_create_internet_item": {"handlers": [
        '<on event="a" run="join controller to mp game"/>',
        f'<on event="a" run="mp type set mode" open="{MT}/connected/connected_map_select_wrapper"/>',
        '<on event="left_mouse" run="mouse emit accept event"/>',
    ]},
    f"{MT}/multiplayer_type_join_direct_item": {"handlers": [
        f'<on event="a" run="mp type set mode" open="{MT}/join_game/join_game_screen"/>',
        f'<on event="start" run="mp type set mode" open="{MT}/join_game/join_game_screen"/>',
        '<on event="left_mouse" run="mouse emit accept event"/>',
    ]},
    # (Co-op, after Create Game's: _coop)
    f"{MT}/multiplayer_type_select_list": {"insert_before": {
        f"{MT}/multiplayer_type_gametypes_item": [f'<child widget="{MT}/multiplayer_type_coop_item" y="309"/>'],
    }},
    # (the Map screen's and New Game's lists: SINGLEPLAYER or MULTIPLAYER
    # maps, the first row's chooser's (_map_kind); B on the Map screen goes
    # from a co-op level's difficulties back to the level, else out of the
    # screen: menu_functions.c's map list)
    f"{MT}/mp_map_select/mp_map_select_list_2": {"swap": {"main_menu/new_select/list_item_0": MAP_KIND_CHOOSER},
                                                 "handlers": [
        '<on event="created" run="mp level list initialize"/>',
        '<on event="deleted" run="mp level list dispose"/>',
        f'<on event="custom_activation" run="mp level select" open="{MT}/connected/gametype_select_screen_wrapper"/>',
        '<on event="b" run="port map list back"/>',
        '<on event="back" run="port map list back"/>',
    ]},
    "main_menu/solo_level_select/solo_level_select_list": {"swap": {"main_menu/new_select/list_item_0": MAP_KIND_CHOOSER}},
    # (each description shows the other kind's maps too: a campaign level's
    # picture, name and words on the Map screen, a multiplayer map's on New
    # Game's)
    f"{MT}/mp_map_select/mp_map_right_item": {"children": [
        '<child widget="main_menu/solo_level_select/replay_level_right_name"/>',
        '<child widget="main_menu/solo_level_select/replay_level_right_pic"/>',
        '<child widget="main_menu/solo_level_select/replay_level_right_data"/>',
    ]},
    "main_menu/solo_level_select/replay_level_right_item": {"children": [
        f'<child widget="{MT}/mp_map_select/mp_map_right_name"/>',
        f'<child widget="{MT}/mp_map_select/mp_map_right_pic"/>',
        f'<child widget="{MT}/mp_map_select/mp_map_right_data"/>',
    ]},
    f"{MT}/join_game/header_join_game": {"children": [
        f'<child widget="{MT}/join_game/header_server_browser"/>',
        f'<child widget="{MT}/join_game/header_direct_link"/>',
    ]},
    # (the small font's line does not fit below their 4 units of offset)
    f"{MT}/join_game/ticker_player_info": {"set": {"text_y": 1}},
    # (the server browser's top-left tab: which games it lists, not Internet
    # or LAN, which are menu items of their own here; wider for its words, in
    # the small font of the counts beside it, and moved left to stay centred
    # in its tab: ce_menus.py's CHILD_OFFSETS)
    # (the tab behind it without CE's rounded box: the screen's border
    # picture already outlines the tab, and the two outlines clashed)
    f"{MT}/join_game/op_browser_mode": {"set": {"bitmap": None}},
    f"{MT}/join_game/browser_mode_spinner": {"set": {
        "string_list": None, "strings": "ALL GAMES|DEDICATED|P2P GAMES", "setting": "network.browser_source",
        "values": "all|dedicated|players", "width": 117, "footer_bounds": "7 120 19 126", "font": "ui\\small_ui",
        "text_y": 3}},
    # (the server browser's error panel: its words in a box of their own in
    # the error box's place, under its title and button; menu_functions.c's
    # lobby_browser_error hides the error box)
    f"{MT}/join_game/gamespy_error_fullscreen": {"insert_before": {"error/error_modal": [
        f'<child widget="{MT}/join_game/error_words" x="192" y="176"/>']}},
    # (wider to the left, for "SERVERS: 8 (5 dedicated)": ce_menus.py's
    # CHILD_OFFSETS keeps its right edge)
    f"{MT}/join_game/server_count_label": {"set": {"width": 210}},
    # (the page, when there is more than one: centred in the counts' panel,
    # between the players and the servers, whatever their numbers' widths;
    # menu_functions.c's lobby_browser_update places it)
    f"{MT}/join_game/page_count_label": {"set": {"align": "center"}},
    # (Filters' rows set the network.browser_* settings, as Network Setup's
    # SERVER BROWSER does: menu_functions.c's filters_show and
    # "gamespy update filter settings". SHOW MISSING MAPS has a third value,
    # those a host shares: Map Sharing downloads them)
    f"{MT}/join_game/filters/empty_servers_spinner": {"set": {
        "string_list": None, "strings": "NO|YES", "setting": "network.browser_show_empty", "values": "false|true"}},
    f"{MT}/join_game/filters/full_servers_spinner": {"set": {
        "string_list": None, "strings": "NO|YES", "setting": "network.browser_show_full", "values": "false|true"}},
    f"{MT}/join_game/filters/game_type_spinner": {"set": {
        "string_list": None, "strings": "ANY|CTF|SLAYER|ODDBALL|KING|RACE", "setting": "network.browser_gametype",
        "values": "any|ctf|slayer|oddball|king|race"}},
    f"{MT}/join_game/filters/installed_maps_spinner": {"set": {
        "string_list": None, "strings": "NO|YES|IF DOWNLOADABLE", "setting": "network.browser_maps",
        "values": "no|yes|downloadable"}},
    f"{MT}/join_game/ticker_rules_info": {"set": {"text_y": 1}},
    # (Item Options' loadout rows, over its buttons)
    "main_menu/settings_select/multiplayer_setup/item_options_edit/item_options_menu": {"insert_before": {
        "main_menu/settings_select/multiplayer_setup/item_options_edit/item_button_bar": [
            f'<child widget="main_menu/settings_select/multiplayer_setup/item_options_edit/op_map_weapons" x="54" y="163"/>',
            f'<child widget="main_menu/settings_select/multiplayer_setup/item_options_edit/op_loadout" x="54" y="193"/>',
            f'<child widget="main_menu/settings_select/multiplayer_setup/item_options_edit/op_primary_weapon" x="54" y="223"/>',
            f'<child widget="main_menu/settings_select/multiplayer_setup/item_options_edit/op_secondary_weapon" x="54" y="253"/>',
        ]}},
    # (Teamplay Options' voice chat and vote kick rows, over its buttons:
    # Server Setup's only, _teamplay_options_extras)
    f"{TEAMPLAY_EDIT}/teamplay_options_menu": {"insert_before": {
        f"{TEAMPLAY_EDIT}/teamplay_button_bar": [
            f'<child widget="{TEAMPLAY_EDIT}/op_{key}" x="54" y="{163 + TEAMPLAY_STEP * index}"/>'
            for index, (key, *_) in enumerate(TEAMPLAY_EXTRA_ROWS)]}},
    f"{TEAMPLAY_EDIT}/teamplay_options_help": {"set": {"top": TEAMPLAY_HELP_TOP,
                                                       "height": 414 - TEAMPLAY_HELP_TOP}},
    # (the PC's Vehicles row's Start opened Item Options)
    "main_menu/settings_select/multiplayer_setup/playlist_edit/playlist_edit_vehicles_list_item": {"handlers": [
        '<on event="a" open="main_menu/settings_select/multiplayer_setup/vehicle_options_edit/vehicle_options_screen"/>',
        '<on event="start" open="main_menu/settings_select/multiplayer_setup/vehicle_options_edit/vehicle_options_screen"/>',
        '<on event="left_mouse" run="mouse emit accept event"/>',
    ]},
    # (Direct Link's clipboard button in the place of the PC version's
    # Refresh, and Refresh in Get List's: the Server Browser's only, which
    # asks the public games' hosts again; "swap" puts another widget in a
    # child's place)
    f"{MT}/join_game/join_game_button_bar": {"swap": {
        f"{MT}/join_game/join_game_button_refresh": f"{MT}/join_game/button_clipboard",
        f"{MT}/join_game/join_game_button_update": f"{MT}/join_game/join_game_button_refresh",
    }},
}

# frames added after a PC bitmap's (by our names): (the Xbox map's bitmap,
# its frame, the size drawn at, where in the widget). The profile settings'
# picture gets the Xbox's Controller Setup's pictures of its five button
# settings (its first four frames are the thumbstick settings'), which show
# 512 by 235 of their 512 by 512: drawn at 279 wide, in the middle of the
# picture's 279 by 202
XBOX_CONTROLLER_PICTURES = f"ui\\shell\\{PE.replace('/', chr(92))}\\controller_edit\\config_controller"
BITMAP_FRAMES = {
    f"{PE}/profile_options": [(XBOX_CONTROLLER_PICTURES, 4 + preset, 279, 279, 0, 34) for preset in range(5)],
}

# the titles this port has that the PC version has not, set as its headers
# are (port/assets/menus/port_svg): bitmap name, text
TITLES = {
    "main_menu/mods/header_mods": "MODS",
    f"{MT}/join_game/header_server_browser": "SERVER BROWSER",
    f"{MT}/join_game/header_direct_link": "DIRECT LINK",
    f"{MT}/join_game/header_password": "PASSWORD",
    f"{MT}/lobby/header_lobby": "GAME LOBBY",
    f"{MT}/coop/header_player_2": "PLAYER 2 PROFILE",
    f"{MT}/lobby/header_add_player": "ADD PLAYER",
}


def title_backdrop(width: float) -> str:
    """a header's dark backdrop, as the PC headers' redraws have it, for text
    width units wide (the text is set over it in OpenCE: ce_menus.py)"""
    return "\n".join([
        '<svg xmlns="http://www.w3.org/2000/svg" width="512" height="64" viewBox="0 0 512 64">',
        "  <defs>",
        '    <filter id="soft" filterUnits="userSpaceOnUse" x="-60" y="-60" width="632" height="184">',
        '      <feGaussianBlur stdDeviation="14.23 13.06"/>', "    </filter>",
        '    <mask id="cut" maskUnits="userSpaceOnUse" x="0" y="0" width="512" height="64">',
        '      <rect width="512" height="59" fill="#fff"/>', "    </mask>", "  </defs>",
        '  <g mask="url(#cut)">',
        f'    <rect x="29.08" y="28.05" width="{width + 28:.2f}" height="40.95" fill="#021931" fill-opacity="0.893" '
        'filter="url(#soft)"/>',
        "  </g>", "</svg>", ""])


# the in-game pause menu's box, taller for the buttons this port adds
# (menu_tags.c's pause_patch: SETTINGS, and the host's END GAME): the
# pausebox5 redraw's rounded box (ui-svg-handmade/shell/bitmaps), drawn to
# hold each number of buttons, one frame each (the Xbox's 2-button box is
# 159 units high, and each button 35 more)
PAUSE_BOX_BUTTONS = [3, 4]
# ... and the host's with the social screens' PLAYERS (five buttons), the frame after the campaign's two (menu_tags.c's PAUSE_BOX_FIVE_FRAME)
PAUSE_BOX_SOCIAL_BUTTONS = [5]
# the campaign pause box, tall enough for five rows (SETTINGS added) or six
# (network co-op's FRIENDLY FIRE too) at the PC version's pause menu's pitch
# (30) with the prompts still under the last row: the stock box is 159, and
# those rows need 36 and 66 more (menu_tags.c's pause_campaign_patch); the
# mission objectives' box beside it grows with it
PAUSE_BOX_EXTRA_HEIGHTS = [195, 225]


def pause_box_height(buttons: int) -> int:
    return 159 + (buttons - 2) * 35


def pause_box_canvas(height: int) -> int:
    """the height of the frame's picture: 256, or 512 for a box taller than that"""
    return 256 if height <= 256 else 512


def pause_box_svg(piece: str, height: int, header: bool = False) -> str:
    """a piece of the pause box (left cap, centre strip, right cap; 9-slice:
    the strip tiles across), height units high in its 256-high texture"""
    width = 4 if piece == "center" else 16
    # (the five buttons' box is taller than the 256 of the others' textures: its frame is 512 high)
    canvas = pause_box_canvas(height)
    lines = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{canvas}" viewBox="0 0 {width} {canvas}">']
    fill = "#0a2649"
    body_clip = ""
    if not header:
        # Keep the translucent footer clear of the body: blending two
        # 75%-opaque fills would make it nearly opaque and tint it blue.
        lines += [f'  <defs><clipPath id="body-area"><rect x="-16" y="0" width="48" height="{height - 28}"/></clipPath></defs>']
        body_clip = ' clip-path="url(#body-area)"'
    if height in PAUSE_BOX_EXTRA_HEIGHTS or height in map(pause_box_height, PAUSE_BOX_BUTTONS + PAUSE_BOX_SOCIAL_BUTTONS):
        # The stock campaign body fades from blue to dark navy at 75% opacity.
        # Scale it within the body, excluding the objective header or key footer.
        top, bottom = (29, height - 2) if header else (2, height - 28)
        lines += [f'  <defs><linearGradient id="body" gradientUnits="userSpaceOnUse" x1="0" y1="{top}" x2="0" y2="{bottom}">',
                  '    <stop offset="0" stop-color="#0a2649"/>',
                  '    <stop offset="1" stop-color="#050d1c"/>',
                  '  </linearGradient></defs>']
        fill = "url(#body)"
    if piece == "center":
        lines += [f'  <rect x="0" y="2" width="4" height="{height - 4}" fill="{fill}" fill-opacity="0.75"{body_clip}/>',
                  '  <rect x="0" y="0" width="4" height="2" fill="#2896ff"/>',
                  f'  <rect x="0" y="{height - 2}" width="4" height="2" fill="#2896ff"/>']
    else:
        bottom, corner = height - 1, height - 9.5
        cap = [f'    <path d="M9.5,1 H17 V{bottom} H9.5 A8.5,8.5 0 0 1 1,{corner} V9.5 A8.5,8.5 0 0 1 9.5,1 Z" '
               f'fill="{fill}" fill-opacity="0.75" fill-rule="evenodd"{body_clip}/>',
               f'    <path d="M17,1 H9.5 A8.5,8.5 0 0 0 1,9.5 V{corner} A8.5,8.5 0 0 0 9.5,{bottom} H17" fill="none" '
               'stroke="#2896ff" stroke-width="2"/>']
        lines += (['  <g transform="matrix(-1 0 0 1 16 0)">'] + cap + ['  </g>']) if piece == "right" else \
            [line[2:] for line in cap]
    # The stock objectives have a 28-unit header; the button key has a
    # 28-unit footer. Clip the black fill to the rounded panel interior.
    top = 2 if header else height - 28
    divider = 28 if header else height - 28
    if piece == "left":
        lines += [f'  <defs><clipPath id="strip"><rect x="2" y="2" width="{width + 16}" height="{height - 4}" rx="7.5"/></clipPath></defs>']
    if piece == "right":
        lines += [f'  <defs><clipPath id="strip"><rect x="-16" y="2" width="{width + 14}" height="{height - 4}" rx="7.5"/></clipPath></defs>']
    clip = ' clip-path="url(#strip)"' if piece != "center" else ""
    opacity = ' fill-opacity="0.75"' if not header else ""
    lines += [f'  <g{clip}>',
              f'    <rect x="0" y="{top}" width="{width}" height="26" fill="black"{opacity}/>',
              f'    <rect x="0" y="{divider}" width="{width}" height="0.75" fill="#2896ff"/>',
              '  </g>']
    lines += ["</svg>", ""]
    return "\n".join(lines)


# where the PC headers' text is (units of their 512x64), and its colour
TITLE_LEFT, TITLE_TOP, TITLE_CAP = 30.68, 33.5, 21.5
TITLE_COLOR = (0x29, 0x95, 0xFD, 255)


def _header(name: str, bitmap: str, platform=None) -> list:
    # (368, not 363: the title art of SERVER BROWSER, with its soft backdrop, is drawn to 367)
    return _widget(name, [("controller", 1), ("left", 35), ("top", 11), ("width", 368), ("height", 59),
                          ("bitmap", bitmap), ("platform", platform)], [])


# a text field's B and Back do nothing of their own (the engine would go
# back from the field): its screen's (SCREEN_BACK) cancel the edit, else
# leave
FIELD_BACK = ['<on event="b"/>', '<on event="back"/>']
SCREEN_BACK = ['<on event="b" run="gamespy back handler"/>', '<on event="back" run="gamespy back handler"/>']


def _value_row(base: str, key: str, label_index: int, run: str, value_color: str = "#FF2896FF") -> list:
    """a row of a label and a value the player sets (A edits it)"""
    lines = _widget(f"{base}/op_{key}", [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                                         ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                    [f'<on event="a" run="{run}"/>', f'<on event="start" run="{run}"/>', *FIELD_BACK,
                     '<on event="left_mouse" run="mouse emit accept event"/>',
                     f'<child widget="{base}/{key}_label"/>', f'<child widget="{base}/{key}_value" x="230" y="1"/>'])
    lines += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1), ("width", 230), ("height", 22),
                                             ("string_list", f"{base}/labels"), ("string_index", label_index or None),
                                             ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13),
                                             ("text_y", 4)], [])
    lines += _widget(f"{base}/{key}_value", [("type", "text"), ("controller", 1), ("width", 280), ("height", 22),
                                             ("font", "ui\\small_ui"), ("color", value_color), ("align", "center"),
                                             ("text_y", 5), ("text_flags", "no_focus_test")], [])
    return lines


# the server browser's words (menu_functions.c's lobby_browser_update, in
# this order: its _browser_word_*): the line below the rows, the rows' status,
# the counts, and the error panel's
BROWSER_WORDS = [
    # (the chosen game's line: "12 of 16 players", and its state)
    "of", "players", "player", "under way", "full or starting",
    # (where it was listed, and what it needs)
    "Dedicated server by", "listed on", "master server", "master servers", "Dedicated server",
    "Hosted by a player (anonymous listing)", "Password required",
    "Needs the newer build: Settings > Network Setup > Check for updates",
    "Runs an older build: it can be joined once it updates",
    # (a join, as it goes: p2p_join_progress)
    "Asking the brokers for the host...", "The host answered: connecting (UDP hole punching)...",
    "Connected: joining the game...",
    "The host did not answer (offline, or its listing is stale)",
    "No route to the host: a forwarded port on either side helps",
    "The host refused: full", "The host refused: closed", "The host refused: banned",
    "The host refused: wrong password", "The host refused: another version", "The host refused: its invite changed",
    "Strict NAT: turn on UPnP, or forward a port",
    # (the list's state)
    "Internet play is off (Settings)", "The server browser is off (network.public_lobby)",
    "Looking for public games...", "Games on a newer version (update the game to join them):",
    "No public games found", "No games match the filters",
    "No master server answered: showing player-hosted games only",
    # (the counts: "SERVERS: 8 (3 dedicated)"; the rows' status)
    "dedicated", "JOINING", "NEWER", "OLDER", "FAILED", "LIVE", "CLOSED",
    # (the error panel: nothing can be listed)
    "Internet play is off:\\nSettings > Network Setup",
    "Neither the master servers\\nnor the brokers answer:\\ncheck the connection",
    # (a refusal added later, last so that the others keep their places)
    "The host refused: your player name",
    # (and a dedicated server's VPN policy's: port/linux/game/network_link.c)
    "The host blocks VPNs: link Discord in Settings > Network Setup",
    "The host refused: it allows no VPNs",
    "The host refused your Discord link: link it again",
    "The host refused: your Discord account is too new",
    "The host refused: your Discord account is in the game",
    "The host blocks VPNs: link Discord with its master (Settings > Network Setup)",
    "The host refused: too many players from your address",
]


def _join_game_extras() -> list:
    """the browser's added titles, Direct Link's clipboard button, the
    password screen, and the browser's words"""
    base = f"{MT}/join_game"
    lines = _password_screen()
    lines += _strings(f"{base}/browser_words", BROWSER_WORDS)
    # (the error panel's words, in a box as the error box: its own text is
    # the game's error messages')
    lines += _widget(f"{base}/error_words", [("type", "text"), ("controller", 1), ("width", 280), ("height", 159),
                                             ("bitmap", "bitmaps/alert_bkd"), ("font", "ui\\large_ui"),
                                             ("color", "#FF2896FF"), ("text_x", 9),
                                             ("text_y", 35), ("text_flags", "no_focus_test")], [])
    lines += _header(f"{base}/header_server_browser", f"{base}/header_server_browser")
    lines += _header(f"{base}/header_direct_link", f"{base}/header_direct_link")
    lines += _widget(f"{base}/button_clipboard", [("type", "text"), ("width", 128), ("height", 24),
                                                  ("bitmap", "bitmaps/text_button_background"),
                                                  ("text", "PASTE LINK"), ("font", "ui\\small_ui"),
                                                  ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)],
                     ['<on event="a" run="direct ip connect go"/>', '<on event="start" run="direct ip connect go"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    return lines


def _password_screen() -> list:
    """the server browser's password screen: a game with a password chosen
    (menu_functions.c's lobby_browser_select), its password typed (shown as
    stars), JOIN GAME joins it if it is the game's; else its help says so"""
    base = f"{MT}/join_game/password"
    spec = {"screen": "password_screen", "spacing": 28, "help_top": 120,
            "header": ("header_password", f"{MT}/join_game/header_password")}
    extra = _value_row(base, "password", 0, "port password edit")
    extra += _button(f"{base}/button_defaults", 3, [])
    extra += _widget(f"{base}/button_ok", [("type", "text"), ("width", 128), ("height", 24),
                                           ("bitmap", "bitmaps/text_button_background"), ("text", "JOIN GAME"),
                                           ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"),
                                           ("text_y", 2)],
                     ['<on event="a" run="port password join"/>', '<on event="start" run="port password join"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    extra += _strings(f"{base}/labels", ["PASSWORD:"])
    # (by its state: menu_functions.c's password_screen_update)
    extra += _strings(f"{base}/help_strings", [
        "",
        "This game has a password. Type it, then press\\nEnter to join.",
        "That is not the game's password. Try again.",
        "Could not join the game.",
    ])
    lines = _screen(base, spec, [(f"{base}/op_password", None)], ["port password update"],
                    ['<on event="created" run="port password init"/>'], extra)
    # (no Defaults: the bar is JOIN GAME and CANCEL; B goes back at once,
    # even while typing, which the screen starts with)
    lines = [line.replace(f'<child widget="{base}/button_defaults" y="1"/>', "") for line in lines]
    lines = [line.replace('<on event="b" back="true"/>', '<on event="b" run="port password back"/>')
             .replace('<on event="back" back="true"/>', '<on event="back" run="port password back"/>')
             for line in lines]
    return lines


def _server_settings() -> list:
    """Create Game's server settings (in the PC version's place): the game's
    name, the most players (up to the port's 128), its invite link, and
    whether the server browser lists it (PUBLIC or PRIVATE: an internet
    game's)"""
    base = f"{MT}/server_settings"
    # (twelve rows: closer together, the help lower)
    spec = {"screen": "server_settings_screen", "spacing": 24, "help_top": 366,
            "header": ("header_server_settings", f"{base}/header_server_settings")}
    rows, extra = [], []
    extra += _value_row(base, "server_name", 0, "ss edit server name")
    rows.append((f"{base}/op_server_name", None))
    extra += _widget(f"{base}/op_max_players", [("width", 512), ("height", 28),
                                                ("flags", "pass_unhandled_to_focused_child"),
                                                ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                     [f'<child widget="{base}/max_players_label"/>',
                      f'<child widget="{base}/max_players_spinner" x="320" y="1"/>'])
    extra += _widget(f"{base}/max_players_label", [("type", "text"), ("controller", 1), ("width", 300),
                                                   ("height", 22), ("string_list", f"{base}/labels"),
                                                   ("string_index", 1), ("font", "ui\\large_ui"),
                                                   ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    extra += _widget(f"{base}/max_players_spinner",
                     [("type", "spinner"), ("left", 3), ("top", 2), ("width", 147), ("height", 20),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                      ("strings", "|".join(str(count) for count in MAXIMUM_PLAYERS)), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 1),
                      ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                      ("header_bounds", "7 -6 19 0"), ("footer_bounds", "7 150 19 156")], [])
    rows.append((f"{base}/op_max_players", None))
    extra += _value_row(base, "invite", 2, "ss copy invite")
    rows.append((f"{base}/op_invite", None))
    # (PUBLIC or PRIVATE: menu_functions.c's server_settings_update)
    extra += _widget(f"{base}/op_listing", [("width", 512), ("height", 28),
                                            ("flags", "pass_unhandled_to_focused_child"),
                                            ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                     [f'<child widget="{base}/listing_label"/>',
                      f'<child widget="{base}/listing_spinner" x="320" y="1"/>'])
    extra += _widget(f"{base}/listing_label", [("type", "text"), ("controller", 1), ("width", 300),
                                               ("height", 22), ("string_list", f"{base}/labels"),
                                               ("string_index", 9), ("font", "ui\\large_ui"),
                                               ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    extra += _widget(f"{base}/listing_spinner",
                     [("type", "spinner"), ("left", 3), ("top", 2), ("width", 147), ("height", 20),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                      ("strings", "PUBLIC|PRIVATE"), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 1),
                      ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                      ("header_bounds", "7 -6 19 0"), ("footer_bounds", "7 150 19 156")],
                     ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])
    rows.append((f"{base}/op_listing", None))
    # PASSWORD (a public internet game's: menu_functions.c's server_settings_update),
    # which players from the server browser must type
    extra += _value_row(base, "password", 10 + len(COOP_SETUP_SCREENS), "ss edit server password")
    rows.append((f"{base}/op_password", None))
    # MAP SHARING (map_share.c), a screen of its own as co-op's options are,
    # for any game: SETUP_OPTIONS_PLACE below it
    key, _, _, _ = MAP_SHARE_SETUP
    target = f"{base}/{key}/{key}_screen"
    extra += _widget(f"{base}/op_{key}", [("width", 512), ("height", 28),
                                         ("flags", "pass_unhandled_to_focused_child"),
                                         ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                     [f'<on event="a" open="{target}"/>', f'<on event="start" open="{target}"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>',
                      f'<child widget="{base}/{key}_label"/>'])
    extra += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1), ("width", 300), ("height", 22),
                                             ("string_list", f"{base}/labels"),
                                             ("string_index", 11 + len(COOP_SETUP_SCREENS)),
                                             ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13),
                                             ("text_y", 4)], [])
    rows.append((f"{base}/op_{key}", None, SETUP_OPTIONS_PLACE - 1))
    # co-op's options, in the place of the gametype's rows, which co-op hides
    # as multiplayer hides co-op's (menu_functions.c's server_settings_update):
    # rows opening their screens, as the gametype's options' do
    # (_setup_option_screen; COOP_SETUP_SCREENS)
    for index, (key, title, _, _) in enumerate(COOP_SETUP_SCREENS):
        target = f"{base}/{key}/{key}_screen"
        extra += _widget(f"{base}/op_{key}", [("width", 512), ("height", 28),
                                             ("flags", "pass_unhandled_to_focused_child"),
                                             ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         [f'<on event="a" open="{target}"/>', f'<on event="start" open="{target}"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          f'<child widget="{base}/{key}_label"/>'])
        extra += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1), ("width", 300), ("height", 22),
                                                 ("string_list", f"{base}/labels"), ("string_index", 10 + index),
                                                 ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13),
                                                 ("text_y", 4)], [])
        rows.append((f"{base}/op_{key}", None, SETUP_OPTIONS_PLACE + index))
    # the gametype's options for this game (the gametype editor's screens,
    # editing a copy of the gametype chosen: "port setup edit")
    for index, (key, screen) in enumerate(SETUP_OPTION_SCREENS):
        target = f"main_menu/settings_select/multiplayer_setup/{screen}"
        extra += _widget(f"{base}/op_{key}", [("width", 512), ("height", 28),
                                             ("flags", "pass_unhandled_to_focused_child"),
                                             ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         [f'<on event="a" run="port setup edit" open="{target}"/>',
                          f'<on event="start" run="port setup edit" open="{target}"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          f'<child widget="{base}/{key}_label"/>', f'<child widget="{base}/{key}_value" x="230" y="1"/>'])
        extra += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1), ("width", 230), ("height", 22),
                                                 ("string_list", f"{base}/labels"), ("string_index", 3 + index),
                                                 ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13),
                                                 ("text_y", 4)], [])
        extra += _widget(f"{base}/{key}_value", [("type", "text"), ("controller", 1), ("width", 280), ("height", 22),
                                                 ("font", "ui\\small_ui"), ("color", "#FF2896FF"), ("align", "center"),
                                                 ("text_y", 5), ("text_flags", "no_focus_test")], [])
        rows.append((f"{base}/op_{key}", None, SETUP_OPTIONS_PLACE + index))
    extra += _button(f"{base}/button_defaults", 3, [])
    extra += _widget(f"{base}/button_ok", [("type", "text"), ("width", 128), ("height", 24),
                                           ("bitmap", "bitmaps/text_button_background"), ("text", "START GAME"),
                                           ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"),
                                           ("text_y", 2)],
                     [f'<on event="a" run="ss start game" open="{MT}/lobby/lobby_screen"/>',
                      f'<on event="start" run="ss start game" open="{MT}/lobby/lobby_screen"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    extra += _strings(f"{base}/labels", ["GAME NAME:", "MAXIMUM PLAYERS:", "INVITE LINK:", "GAME TYPE:",
                                         "PLAYER OPTIONS:", "ITEM OPTIONS:", "VEHICLE OPTIONS:", "INDICATOR OPTIONS:",
                                         "TEAMPLAY OPTIONS:", "LISTING:",
                                         *(f"{title}:" for _, title, _, _ in COOP_SETUP_SCREENS), "PASSWORD:",
                                         f"{MAP_SHARE_SETUP[1]}:"])
    extra += _strings(f"{base}/help_strings", [
        "",
        "The name the game shows in the lists of games.\\nEnter changes it.",
        "The most players the game takes (up to 128).",
        "Players join this game by its invite link, or your\\nDiscord invite. Enter copies the link again.",
        "The gametype and its rules, for this game.",
        "Lives, health, shields and respawning, for this game.",
        "Weapons, grenades and starting equipment, for\\nthis game.",
        "Each team's vehicles and their respawn time, for\\nthis game.",
        "The motion tracker and nav points, for this game.",
        "Friendly fire and team balance, for this game.",
        # (LISTING's, by its choice; PUBLIC's with a publisher key is last)
        "Anyone can see and join your game: it is listed\\nfor everyone through the brokers.",
        "Only players with your invite link can join.",
        # (co-op's options' rows: COOP_SETUP_SCREENS)
        *(help_text for _, _, help_text, _ in COOP_SETUP_SCREENS),
        # (PASSWORD's)
        "Players from the Server Browser must type it to\\njoin (an invite link needs none). Enter sets it.",
        # (MAP SHARING's)
        MAP_SHARE_SETUP[2],
        # (LISTING's PUBLIC, on a machine with a publisher key: menu_functions.c's
        # server_settings_update)
        "Listed for everyone through the brokers, and on\\nthe master servers with your publisher key.",
    ])
    lines = _screen(base, spec, rows, ["server settings update"],
                    ['<on event="created" run="server settings init"/>'], extra)
    # (co-op's options' screens, and map sharing's)
    for key, title, _, screen_rows in [*COOP_SETUP_SCREENS, MAP_SHARE_SETUP]:
        lines += _setup_option_screen(f"{base}/{key}", key, screen_rows, fit=True)
    # (no Defaults: the bar is START and CANCEL; B cancels the name's edit
    # first)
    lines = [line.replace(f'<child widget="{base}/button_defaults" y="1"/>', "") for line in lines]
    lines = [line.replace('<on event="b" back="true"/>', SCREEN_BACK[0])
             .replace('<on event="back" back="true"/>', SCREEN_BACK[1]) for line in lines]
    return lines


MAXIMUM_PLAYERS = [2, 4, 8, 12, 16, 24, 32, 48, 64, 96, 128]
# CO-OP OPTIONS' amounts of PER PLAYER (network.coop_enemies, percentages)
# and STATIC MULTIPLIER (network.coop_enemies_multiplier), as menu_functions.c's
# gametype_options have their values
COOP_ENEMIES_PERCENTAGES = [25, 50, 100, 150, 200]
COOP_ENEMIES_MULTIPLIERS = [2, 4, 8, 16, 32]
# co-op's Server Setup options, in screens of their own as the gametype's
# are (its rows: _server_settings), each its key, its title, its row's help
# and its rows (as TEAMPLAY_ROWS: each spinner's key, label, words and the
# help of each of them; menu_functions.c's gametype_options give their
# settings' values)
COOP_SETUP_SCREENS = [
    ("coop_options", "CO-OP OPTIONS", "Team wipes, friendly fire, enemies and collisions,\\nfor this game.", [
        ("coop_friendly_fire", "FRIENDLY FIRE:", ["OFF", "ON", "SHIELD ONLY", "EXPLOSIVES ONLY"], [
            "Players can not be hurt by weapons and explosives\\nfired by the other players.",
            "Players can be hurt by weapons or explosives\\nfired by the other players.",
            "Damage from the other players will only reduce\\nshields. Health will be unaffected.",
            "Players can be hurt by damage from explosives\\nfired by the other players.",
        ]),
        ("coop_extra_enemies", "EXTRA ENEMIES:", ["PER PLAYER", "STATIC MULTIPLIER", "NONE"], [
            "Enemy squads grow with the players: by the value of\\n'Per Player' for each player past the first.",
            "Enemy squads use 'Multiplier', however many players\\nthere are.",
            "Enemy squads are as the campaign has them.",
        ]),
        ("coop_extra_pickups", "EXTRA PICKUPS:", ["PER PLAYER", "STATIC MULTIPLIER", "NONE"], [
            "Pickups grow with the players: by the value of\\n'Per Player' for each player past the first.",
            "Pickups use 'Multiplier', up to eight times as many,\\nhowever many players there are.",
            "Pickups are as the campaign has them.",
        ]),
        ("coop_extra_vehicles", "EXTRA VEHICLES:", ["PER PLAYER", "STATIC MULTIPLIER", "NONE"], [
            "Vehicles respawn after being driven a short distance\\nbased on the number of players: by two thirds the value of\\n'Per Player' for each player past the first.",
            "Vehicles respawn after being driven a short distance,\\nwith extra copies equal to ('Multiplier' - 1) * 2/3,\\nrounded down, however many players there are.",
            "Vehicles are as the campaign has them.",
        ]),
        ("coop_enemies_multiplier", "MULTIPLIER:", [f"{value}X" for value in COOP_ENEMIES_MULTIPLIERS],
         ["Static amounts for the options set to this mode.\\nPickups are capped at eight times as many."] * len(COOP_ENEMIES_MULTIPLIERS)),
        ("coop_enemies_per_player", "PER PLAYER:", [f"{value}%" for value in COOP_ENEMIES_PERCENTAGES],
         ["For each player past the first, options set to Per\\nPlayer get this much more (100%: as many again)."] *
         len(COOP_ENEMIES_PERCENTAGES)),
        ("coop_player_collisions", "PLAYER COLLISIONS:", ["ON", "OFF"], [
            "Players bump into each other, as in the campaign.",
            "Players walk through each other, so that no one\\nblocks a doorway. The AI's characters still block.",
        ]),
        # (network.coop_ally_friendly_fire: game_engine.c's
        # game_engine_friendly_damage)
        ("coop_ally_friendly_fire", "ALLY FRIENDLY FIRE:", ["ON", "OFF"], [
            "Players can hurt and kill their AI allies, as in\\nthe campaign.",
            "Players can not hurt their AI allies, so that no\\none can kill Keyes or the Marines a level needs.",
        ]),
        ("coop_team_wipe", "TEAM WIPE:", ["AUTO", "REVERT CHECKPOINT", "RESPAWN ONLY"], [
            "Private games revert the checkpoint; public games\\nonly respawn. A changed team or missing save\\nalways uses respawn only.",
            "Revert the checkpoint after everyone dies. A changed\\nteam or missing save uses respawn only.",
            "Respawn at checkpoint positions after everyone dies.\\nThe world keeps its progress.",
        ]),
    ]),
    ("voice_options", "VOICE AND VOTING", "Voice chat, text chat and votes to kick a player,\\nfor this game.",
     TEAMPLAY_ROWS),
]
# Server Setup's map sharing (port/linux/game/map_share.c), for any game,
# laid out as co-op's options are (its key, its title, its row's help and
# its rows: menu_functions.c's gametype_options give their settings' values)
MAP_SHARE_UPLOADS = (256, 512, 1024, 2048, 4096, 8192, 16384, 20480)
MAP_SHARE_SETUP = ("map_share_options", "MAP SHARING",
                   "Sending players who join without the game's\\nCustom Edition map the map, and how fast.", [
    ("map_share", "SHARE MAPS:", ["ON", "OFF"], [
        "Players who join without the map download it\\nfrom you and the other players.",
        "Players who join without the map cannot play.",
    ]),
    ("map_share_upload", "UPLOAD LIMIT:", [f"{kb} KB/S" if kb < 1024 else f"{kb // 1024} MB/S"
                                           for kb in MAP_SHARE_UPLOADS],
     ["The most you send of maps, so that the game\\nkeeps the rest of your upload."] * len(MAP_SHARE_UPLOADS)),
])
# (Server Setup's place of its options' rows, co-op's or the gametype's: after
# MAP SHARING's)
SETUP_OPTIONS_PLACE = 6
# (their screens' titles)
TITLES.update({f"{MT}/server_settings/{key}/header_{key}": title
               for key, title, _, _ in [*COOP_SETUP_SCREENS, MAP_SHARE_SETUP]})
# (the rows of a co-op options screen in one place, the one shown: PER
# PLAYER's and MULTIPLIER's, by EXTRA ENEMIES')
SETUP_OPTION_SAME_PLACE = set()


def _setup_option_screen(base: str, key: str, rows: list, fit: bool = False) -> list:
    """a screen of options laid out as the gametype's are (Teamplay
    Options'), its spinners setting config.toml's settings (menu_functions.c's
    "port setup options init" and "port setup options save", OK), each
    choice's help shown as the gametype's are ("game settings lists text
    update": the help strings, each spinner's choices' in turn)"""
    lines = _widget(f"{base}/{key}_screen", [("width", 640), ("height", 480),
                                            ("flags", "pass_unhandled_to_focused_child"),
                                            ("bitmap", "bitmaps/gradient")],
                    [f'<child widget="{base}/header_{key}"/>', f'<child widget="{base}/{key}_menu"/>'])
    lines += _header(f"{base}/header_{key}", f"{base}/header_{key}")
    lines += _widget(f"{base}/{key}_help", [("type", "text"), ("controller", 1), ("left", 68), ("top", 341),
                                           ("width", 482), ("height", 79), ("string_list", f"{base}/cap_{key}"),
                                           ("font", "ui\\large_ui"), ("color", "#FFFFFFFF")], [])
    children = ['<data input="game settings lists text update"/>',
                '<on event="created" run="port setup options init"/>']
    # (30 apart; with fit, closer for more rows than eight, the last 28 tall
    # clear of the help at 341: Server Setup's, whose rows are all shown, as
    # OpenCE Options' are not)
    places = len([row_key for row_key, *_ in rows if row_key not in SETUP_OPTION_SAME_PLACE])
    spacing = min(30, (341 - 1 - 73 - 28) // max(1, places - 1)) if fit else 30
    place = -1
    for row_key, *_ in rows:
        if row_key not in SETUP_OPTION_SAME_PLACE:
            place += 1
        children.append(f'<child widget="{base}/op_{row_key}" x="54" y="{73 + spacing * place}"/>')
    children.append(f'<child widget="{base}/{key}_button_bar" y="414"/>')
    lines += _widget(f"{base}/{key}_menu", [("type", "column_list"), ("width", 640), ("height", 480),
                                           ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                                           ("description", f"{base}/{key}_help")], children)
    lines += _widget(f"{base}/{key}_button_bar", [("type", "column_list"), ("width", 640), ("height", 28),
                                                 ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     ['<data input="common button bar update"/>', f'<child widget="{base}/{key}_button_ok" x="380" y="1"/>',
                      '<child widget="common_button_cancel" x="510" y="1"/>'])
    lines += _button(f"{base}/{key}_button_ok", 1, ['<on event="a" run="port setup options save" back="true"/>',
                                                     '<on event="start" run="port setup options save" back="true"/>'])
    for index, (row_key, label, strings, _) in enumerate(rows):
        lines += _widget(f"{base}/op_{row_key}", [("width", 512), ("height", 28),
                                                 ("flags", "pass_unhandled_to_focused_child"),
                                                 ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         [f'<child widget="{base}/{row_key}_label"/>',
                          f'<child widget="{base}/{row_key}_spinner" x="286" y="1"/>'])
        lines += _widget(f"{base}/{row_key}_label", [("type", "text"), ("controller", 1), ("width", 300),
                                                     ("height", 22), ("string_list", f"{base}/{key}_labels"),
                                                     ("string_index", index), ("font", "ui\\large_ui"),
                                                     ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
        lines += _widget(f"{base}/{row_key}_spinner",
                         [("type", "spinner"), ("top", 2), ("width", 206), ("height", 20),
                          ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                          ("string_list", f"{base}/var_{row_key}"), ("font", "ui\\large_ui"),
                          ("color", "#FF2896FF"), ("align", "center"), ("text_y", 4),
                          ("list_flags", "items_from_strings"),
                          ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                          ("header_bounds", "7 -13 19 -7"), ("footer_bounds", "7 208 19 214")],
                         ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])
        lines += _strings(f"{base}/var_{row_key}", strings)
    lines += _strings(f"{base}/{key}_labels", [label for _, label, *_ in rows])
    lines += _strings(f"{base}/cap_{key}", [text for *_, helps in rows for text in helps])
    return lines
# Server Setup's rows of the gametype's options: the gametype editor's
# screens
SETUP_OPTION_SCREENS = [
    ("game_type", "playlist_edit/game_type_select/gametype_select_screen"),
    ("player_options", "player_options_edit/player_options_screen"),
    ("item_options", "item_options_edit/item_options_screen"),
    ("vehicle_options", "vehicle_options_edit/vehicle_options_screen"),
    ("indicator_options", "indicator_options_edit/indicator_options_screen"),
    ("team_options", "teamplay_options_edit/teamplay_options_screen"),
]


def _lobby() -> list:
    """the lobby the host's and the joining players wait in (the Xbox's
    pregame's functions, which need no widget of theirs): up to the port's
    128 players, scrolling; the game's map and gametype; the countdown"""
    base = f"{MT}/lobby"
    # (split screen: another controller's START joins it, its B leaves alone:
    # menu_functions.c's lobby_join. After a game it is the only screen,
    # network_game_reset_to_pregame_ui opening it in place of the Xbox's
    # pregame: leaving it goes to the main menu, as the PC version's postgame
    # screens do)
    lines = _widget(f"{base}/lobby_screen", [("width", 640), ("height", 480),
                                             ("flags", "pass_unhandled_to_focused_child main_menu_if_no_history"),
                                             ("bitmap", "bitmaps/gradient")],
                    ['<on event="created" run="port lobby open"/>',
                     '<on event="created" run="net server accept conx"/>',
                     '<on event="created" run="net server allow start"/>',
                     '<on event="b" run="port lobby leave" back="true"/>',
                     '<on event="back" run="port lobby leave" back="true"/>',
                     # (another controller's START joins, its ADD PLAYER too: both
                     # ask player 1's Y, which opens the 4-way screen as player 1's)
                     '<on event="start" run="port lobby join"/>',
                     f'<on event="y" run="port lobby add player" open="{base}/join4_screen"/>',
                     # (the social screens' bots: L drops one and R adds one, the host's)
                     '<on event="white" run="port lobby bot drop"/>', '<on event="black" run="port lobby bot add"/>',
                     # (a dedicated server's map vote: its screen, opened as
                     # the vote begins by a posted X, and again with X while
                     # it runs: menu_functions.c's map_vote_lobby_check. Y is
                     # the 4-way join screen's)
                     f'<on event="x" run="port map vote open" open="{base}/map_vote/map_vote_screen" branch="true"/>',
                     '<child widget="main_menu/new_select/sel_list_desc_bkd"/>',
                     f'<child widget="{base}/lobby_list"/>',
                     f'<child widget="{base}/header_lobby"/>',
                     f'<child widget="{base}/lobby_join_help"/>'])
    lines += _header(f"{base}/header_lobby", f"{base}/header_lobby")
    # (under the bar's band, whose bottom is 444: the capitals' bottom at 454 and the slash's tail at 456, the safe margin; it was at 446, its capitals ending at 465)
    lines += _widget(f"{base}/lobby_join_help", [("type", "text"), ("controller", 1), ("left", 355), ("top", 436),
                                                 ("width", 275), ("height", 20), ("font", "ui\\small_ui"),
                                                 ("color", SOCIAL_HINT_COLOR), ("align", "right"), ("text_y", 4),
                                                 ("text_flags", "no_focus_test")], [])
    # (its rows: any controller's left and right switch its own player's team,
    # on the rows only, so that they move along the buttons)
    rows = [f'<child widget="{base}/list_item_{index}" x="20" y="{73 + 30 * index}"/>' for index in range(11)]
    lines += _widget(f"{base}/lobby_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                            ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                                            ("description", f"{base}/lobby_desc")],
                     ['<data input="net splitscreen prejoin players"/>', '<data input="port lobby update"/>',
                      *rows,
                      f'<child widget="{base}/lobby_button_bar" y="414"/>'])
    for index in range(11):
        # (the social screens' (screens.enable): A opens the player's actions, and the row's name and its tag of the team and a bot's skill replace the
        # centred text; hidden and empty with the setting off: port/linux/game/menu_screens.inl)
        lines += _widget(f"{base}/list_item_{index}",
                         [("width", 402 if index == 0 else 390), ("height", 28),
                          ("bitmap", "bitmaps/sel_list_item_bkd_top" if index == 0 else "bitmaps/sel_list_item_bkd"),
                          ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("align", "center"), ("text_y", 3)],
                         ['<on event="left right" run="swap player team"/>',
                          f'<on event="a" run="port lobby row" open="{SOCIAL}/actions_screen" branch="true"/>',
                          '<child widget="main_menu/new_select/list_item_text" x="25"/>',
                          f'<child widget="{base}/scr_lobby_name" x="{SOCIAL_LOBBY_COLUMNS[0][1]}"/>', f'<child widget="{base}/scr_lobby_tag" x="{SOCIAL_LOBBY_COLUMNS[1][1]}"/>',
                          '<child widget="main_menu/new_select/list_item_arrows"/>'])
    lines += _widget(f"{base}/scr_lobby_name", [("type", "text"), ("width", SOCIAL_LOBBY_COLUMNS[0][2]), ("height", 28), ("font", "ui\\large_ui"), ("color", "#FF2896FF"),
                                                ("text_y", 3), ("text_flags", "no_focus_test")], [])
    # (the tag's baseline is the name's: text_y 6 in a row whose name has 3)
    lines += _widget(f"{base}/scr_lobby_tag", [("type", "text"), ("width", SOCIAL_LOBBY_COLUMNS[1][2]), ("height", 28), ("font", "ui\\small_ui"), ("color", "#FF2896FF"),
                                               ("align", "right"), ("text_y", 6), ("text_flags", "no_focus_test")], [])
    lines += _widget(f"{base}/lobby_desc", [("width", 640), ("height", 480)],
                     ['<child widget="main_menu/current_profile_name"/>',
                      f'<child widget="{base}/lobby_right_item" x="22" y="2"/>'])
    lines += _widget(f"{base}/lobby_right_item", [("controller", 1), ("left", 406), ("top", 75), ("width", 162),
                                                  ("height", 326),
                                                  ("bitmap", "bitmaps/spinner_list_3_wide_item_background")],
                     # (a campaign level's picture and name in place of the
                     # map's, for network co-op: menu_functions.c's
                     # lobby_map_show)
                     [f'<child widget="{base}/lobby_map_pic"/>', f'<child widget="{base}/lobby_map_name"/>',
                      '<child widget="main_menu/solo_level_select/replay_level_right_pic"/>',
                      '<child widget="main_menu/solo_level_select/replay_level_right_name"/>',
                      f'<child widget="{base}/lobby_game_data"/>'])
    lines += _widget(f"{base}/lobby_map_pic", [("controller", 1), ("left", 419), ("top", 87), ("width", 140),
                                               ("height", 114), ("bitmap", "ui\\shell\\bitmaps\\mp_map_grafix")], [])
    lines += _widget(f"{base}/lobby_map_name", [("type", "text"), ("controller", 1), ("left", 417), ("top", 204),
                                                ("width", 146), ("height", 43), ("string_list", "main_menu/mp_map_list"),
                                                ("font", "ui\\large_ui"), ("color", "#FF2896FF")], [])
    lines += _widget(f"{base}/lobby_game_data", [("type", "text"), ("controller", 1), ("left", 417), ("top", 250),
                                                 ("width", 146), ("height", 144), ("font", "ui\\small_ui"),
                                                 ("color", "#FF2896FF")], [])
    # (its words: menu_functions.c's lobby_update. "<name> - dedicated": the
    # game's host is a dedicated server)
    lines += _strings(f"{base}/lobby_words", ["dedicated"])
    lines += _widget(f"{base}/lobby_button_bar", [("type", "column_list"), ("width", 640), ("height", 28),
                                                  ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     # (SWITCH TEAM first: hidden in a game without teams, which
                     # moves the focus to the first shown, START NOW; at the end
                     # it traps none)
                     [f'<child widget="{base}/lobby_button_team" x="120" y="1"/>',
                      f'<child widget="{base}/lobby_button_start" x="250" y="1"/>',
                      f'<child widget="{base}/lobby_button_add" x="380" y="1"/>',
                      f'<child widget="{base}/lobby_button_leave" x="510" y="1"/>'])
    for key, caption, handlers in (
        ("add", "ADD PLAYER", [f'<on event="a" run="port lobby add player" open="{base}/join4_screen"/>',
                               f'<on event="start" run="port lobby add player" open="{base}/join4_screen"/>']),
        ("team", "SWITCH TEAM", ['<on event="a" run="swap player team"/>', '<on event="start" run="swap player team"/>']),
        ("start", "START NOW", ['<on event="a" run="net game speed start"/>',
                                '<on event="start" run="net game speed start"/>']),
        ("leave", "LEAVE", ['<on event="a" run="mouse emit back event"/>',
                            '<on event="start" run="mouse emit back event"/>'])):
        lines += _widget(f"{base}/lobby_button_{key}", [("type", "text"), ("width", 128), ("height", 24),
                                                       ("bitmap", "bitmaps/text_button_background"),
                                                       ("text", caption), ("font", "ui\\small_ui"),
                                                       ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)],
                         handlers + ['<on event="left_mouse" run="mouse emit accept event"/>'])
    # a dedicated server's map vote (network_map_vote.c), as the lobby: its
    # choices a row each (A votes), the focused one's map at the right with the
    # time left; B (or the vote's end) goes back to the lobby
    vote = f"{base}/map_vote"
    lines += _widget(f"{vote}/map_vote_screen", [("width", 640), ("height", 480),
                                                 ("flags", "pass_unhandled_to_focused_child"),
                                                 ("bitmap", "bitmaps/gradient")],
                     ['<on event="b" back="true"/>', '<on event="back" back="true"/>',
                      '<child widget="main_menu/new_select/sel_list_desc_bkd"/>',
                      f'<child widget="{vote}/map_vote_list"/>',
                      f'<child widget="{vote}/map_vote_title"/>'])
    lines += _widget(f"{vote}/map_vote_title", [("type", "text"), ("controller", 1), ("left", 30), ("top", 30),
                                                ("width", 580), ("height", 36), ("text", "VOTE FOR THE NEXT GAME"),
                                                ("font", "ui\\large_ui"), ("color", "#FFFFFFFF"),
                                                ("text_flags", "no_focus_test")], [])
    vote_rows = [f'<child widget="{vote}/list_item_{index}" x="20" y="{73 + 30 * index}"/>' for index in range(8)]
    lines += _widget(f"{vote}/map_vote_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                               ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                                               ("description", f"{vote}/map_vote_desc")],
                     ['<data input="port map vote update"/>', *vote_rows,
                      f'<child widget="{vote}/map_vote_button_bar" y="414"/>'])
    for index in range(8):
        lines += _widget(f"{vote}/list_item_{index}",
                         [("width", 402 if index == 0 else 390), ("height", 28),
                          ("bitmap", "bitmaps/sel_list_item_bkd_top" if index == 0 else "bitmaps/sel_list_item_bkd"),
                          ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("align", "center"), ("text_y", 3)],
                         ['<on event="a" run="port map vote choose"/>',
                          '<on event="start" run="port map vote choose"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          '<child widget="main_menu/new_select/list_item_text" x="25"/>',
                          '<child widget="main_menu/new_select/list_item_arrows"/>'])
    lines += _widget(f"{vote}/map_vote_desc", [("width", 640), ("height", 480)],
                     ['<child widget="main_menu/current_profile_name"/>',
                      f'<child widget="{base}/lobby_right_item" x="22" y="2"/>'])
    lines += _widget(f"{vote}/map_vote_button_bar", [("type", "column_list"), ("width", 640), ("height", 28),
                                                     ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     [f'<child widget="{vote}/map_vote_button_lobby" x="510" y="1"/>'])
    lines += _widget(f"{vote}/map_vote_button_lobby", [("type", "text"), ("width", 128), ("height", 24),
                                                       ("bitmap", "bitmaps/text_button_background"),
                                                       ("text", "LOBBY"), ("font", "ui\\small_ui"),
                                                       ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)],
                     ['<on event="a" run="mouse emit back event"/>', '<on event="start" run="mouse emit back event"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    # a split screen player's profile, chosen as they join the lobby (any
    # controller's presses: Co-op's rows; the host's countdown waits)
    lines += _widget(f"{base}/player_profile_screen", [("width", 640), ("height", 480),
                                                       ("flags", "pass_unhandled_to_focused_child"),
                                                       ("bitmap", "bitmaps/gradient")],
                     ['<on event="created" run="net server defer start"/>',
                      '<child widget="main_menu/new_select/sel_list_desc_bkd"/>',
                      f'<child widget="{base}/player_profile_list"/>',
                      f'<child widget="{base}/header_add_player"/>',
                      f'<child widget="{base}/player_profile_help" x="20" y="416"/>'])
    lines += _header(f"{base}/header_add_player", f"{base}/header_add_player")
    lines += _widget(f"{base}/player_profile_list",
                     [("type", "column_list"), ("width", 640), ("height", 480),
                      ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                      ("description", "main_menu/profile_manager/player_profile_extended_desc")],
                     ['<data input="3wide player profile list update"/>',
                      '<on event="created" run="port lobby player list initialize"/>',
                      '<on event="deleted" run="player profile list dispose"/>',
                      '<on event="custom_activation" run="port lobby player choose" back="true" branch="true"/>',
                      *[f'<child widget="{MT}/coop/list_item_{index}" x="20" y="{73 + 30 * index}"/>'
                        for index in range(11)],
                      f'<child widget="{MT}/coop/player_2_button_bar" y="414"/>'])
    lines += _widget(f"{base}/player_profile_help", [("type", "text"), ("width", 350), ("height", 24),
                                                     ("text", "The new player's profile."),
                                                     ("font", "ui\\small_ui"), ("color", "#FF2896FF"), ("text_y", 5),
                                                     ("text_flags", "no_focus_test")], [])
    # a game under way's lobby, before joining it (the browser's rows of
    # games in progress): what its advertisement tells, JOIN GAME. Split
    # screen players join here, as in the lobby, before JOIN GAME: the game
    # starts at once for the machine, with the players it brings
    lines += _widget(f"{base}/preview_screen", [("width", 640), ("height", 480),
                                                ("flags", "pass_unhandled_to_focused_child"),
                                                ("bitmap", "bitmaps/gradient")],
                     ['<on event="created" run="port lobby open"/>',
                      '<on event="b" run="port lobby preview leave" back="true"/>',
                      '<on event="back" run="port lobby preview leave" back="true"/>',
                      '<on event="start" run="port lobby preview add"/>',
                      f'<on event="y" run="port lobby add player" open="{base}/join4_screen"/>',
                      '<child widget="main_menu/new_select/sel_list_desc_bkd"/>',
                      f'<child widget="{base}/preview_list"/>',
                      f'<child widget="{base}/header_lobby"/>',
                      f'<child widget="{base}/lobby_join_help"/>'])
    lines += _widget(f"{base}/preview_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                              ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                                              ("description", f"{base}/lobby_desc")],
                     ['<data input="port lobby preview update"/>',
                      f'<child widget="{base}/preview_status" x="30" y="75"/>',
                      f'<child widget="{base}/preview_button_bar" y="414"/>'])
    lines += _widget(f"{base}/preview_status", [("type", "text"), ("controller", 1), ("width", 360), ("height", 300),
                                                ("font", "ui\\large_ui"), ("color", "#FF2896FF")], [])
    lines += _widget(f"{base}/preview_button_bar", [("type", "column_list"), ("width", 640), ("height", 28),
                                                    ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     [f'<child widget="{base}/preview_button_join" x="250" y="1"/>',
                      f'<child widget="{base}/preview_button_add" x="380" y="1"/>',
                      f'<child widget="{base}/preview_button_back" x="510" y="1"/>'])
    for key, caption, run in (("join", "JOIN GAME", "port lobby preview join"),
                              ("add", "ADD PLAYER", "port lobby add player"),
                              ("back", "BACK", "mouse emit back event")):
        opened = f' open="{base}/join4_screen"' if key == "add" else ""
        lines += _widget(f"{base}/preview_button_{key}", [("type", "text"), ("width", 128), ("height", 24),
                                                         ("bitmap", "bitmaps/text_button_background"),
                                                         ("text", caption), ("font", "ui\\small_ui"),
                                                         ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)],
                         [f'<on event="a" run="{run}"{opened}/>', f'<on event="start" run="{run}"{opened}/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>'])
    lines += _join4(base)
    return lines


def _join4(base: str) -> list:
    """ADD PLAYER's screen: the Xbox's split screen profile select (its own
    widgets and bitmaps, ui\\shell\\...\\4way_profile_select), a quarter for
    each controller. A controller not playing presses A to join, chooses its
    profile on the Xbox's spinner ("player profile list initialize": one no
    other player has) and presses A to select it; B goes back a step. A
    selected or playing quarter's A (or Enter) brings the players who
    selected theirs into the lobby, player 1's B goes back without them. The
    keyboard's SPACE makes the keyboard player 1 alone, freeing the gamepad
    it shared to join (menu_functions.c's join4). Each quarter takes its own
    controller's presses (pc_menu_screen_routes_by_controller)"""
    xbox = "ui\\shell\\main_menu\\multiplayer_type_select\\split_screen\\4way_profile_select\\"
    connected = "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\4way_profile_select\\"
    quarters = [(70, 76), (328, 76), (70, 242), (328, 242)]
    lines = _widget(f"{base}/join4_screen",
                    [("width", 640), ("height", 480), ("bitmap", "ui\\shell\\bitmaps\\gradient"),
                     ("flags", "pass_unhandled_to_focused_child no_focused_child pass_unhandled_to_all_children "
                               "pass_handled_to_all_children")],
                    ['<on event="created" run="port join4 open"/>',
                     '<on event="created" run="net server defer start"/>',
                     # (player 1's B, Escape: back without them; a screen
                     # with no B of its own goes back at any controller's,
                     # before its quarters see it)
                     '<on event="b" run="port join4 back" back="true"/>',
                     '<on event="back" run="port join4 back" back="true"/>',
                     '<data input="port join4 update"/>',
                     f'<child widget="{xbox}header_select_profile"/>',
                     *[f'<child widget="{base}/join4_qtr_join" controller="{index + 1}" x="{x}" y="{y}"/>'
                       for index, (x, y) in enumerate(quarters)],
                     '<child widget="ui\\shell\\main_menu\\button_key"/>',
                     f'<child widget="{base}/join4_hint" x="30" y="419"/>'])
    # (a quarter for a controller not playing: "Press A to Join"; the screen
    # sends a player's quarter "black" (their profile) and the quarter of the
    # controller whose START opened it "white" (its spinner): join4_open)
    lines += _widget(f"{base}/join4_qtr_join",
                     [("width", 255), ("height", 165), ("bitmap", "ui\\shell\\bitmaps\\split_screen_bkd"),
                      ("flags", "pass_unhandled_to_focused_child")],
                     [f'<on event="black" run="port join4 playing" replace="{base}/join4_qtr_playing"/>',
                      f'<on event="white" run="port join4 opener" replace="{base}/join4_qtr_select"/>',
                      f'<on event="a" run="port join4 join" replace="{base}/join4_qtr_select"/>',
                      f'<on event="start" run="port join4 join" replace="{base}/join4_qtr_select"/>',
                      # (a widget with no B of its own goes back at it)
                      '<on event="b"/>', '<on event="back"/>',
                      f'<child widget="{connected}press_start_to_join" x="54" y="20"/>',
                      f'<child widget="{connected}press_start_to_join_controller"/>'])
    lines += _widget(f"{base}/join4_qtr_select",
                     [("width", 254), ("height", 164), ("bitmap", f"{xbox}4way_profile_border"),
                      ("flags", "pass_unhandled_to_focused_child")],
                     ['<on event="created" run="port join4 choosing"/>',
                      f'<on event="a" run="port join4 select" replace="{base}/join4_qtr_ready"/>',
                      f'<on event="start" run="port join4 select" replace="{base}/join4_qtr_ready"/>',
                      f'<on event="b" run="port join4 leave" replace="{base}/join4_qtr_join"/>',
                      '<on event="back"/>',
                      f'<child widget="{xbox}qtr_screen_profile_color_pic"/>',
                      f'<child widget="{xbox}qtr_screen_profile_desc"/>',
                      f'<child widget="{xbox}qtr_screen_profile_spinner"/>'])
    selected = [f'<child widget="{xbox}qtr_screen_profile_color_pic_selected"/>',
                f'<child widget="{xbox}qtr_screen_profile_selected_text"/>',
                f'<child widget="{xbox}qtr_screen_profile_selected_name"/>']
    lines += _widget(f"{base}/join4_qtr_ready",
                     [("width", 254), ("height", 164), ("bitmap", f"{xbox}4way_profile_border")],
                     ['<on event="created" run="port join4 ready"/>',
                      '<on event="a" run="port join4 done" back="true"/>',
                      '<on event="start" run="port join4 done" back="true"/>',
                      f'<on event="b" run="port join4 unready" replace="{base}/join4_qtr_select"/>',
                      '<on event="back"/>',
                      *selected])
    # (a player in the game already)
    lines += _widget(f"{base}/join4_qtr_playing",
                     [("width", 254), ("height", 164), ("bitmap", f"{xbox}4way_profile_border")],
                     ['<on event="a" run="port join4 done" back="true"/>',
                      '<on event="start" run="port join4 done" back="true"/>',
                      '<on event="b"/>', '<on event="back"/>',
                      *selected])
    lines += _widget(f"{base}/join4_hint", [("type", "text"), ("width", 345), ("height", 24),
                                            ("font", "ui\\small_ui"), ("color", "#FF2896FF"),
                                            ("text_flags", "no_focus_test")], [])
    return lines


def _coop() -> list:
    """Co-op, the campaign for two players on this machine in split screen
    (the Xbox's Cooperative Play): Multiplayer's CO-OP CAMPAIGN, then player
    2's profile, chosen with player 2's controller (its rows take any
    controller's presses, as the shared rows do only controller 1's), then
    New Game's levels and difficulty, which either player's controller uses
    (ui_widget.c's widget_takes_events_of_controller; menu_functions.c's
    coop_begin)"""
    base = f"{MT}/coop"
    lines = _widget(f"{MT}/multiplayer_type_coop_item",
                    [("type", "text"), ("left", 51), ("width", 232), ("height", 32), ("bitmap", "bitmaps/list_item_bkd"),
                     ("string_list", f"{MT}/multiplayer_options"), ("string_index", 7), ("font", "ui\\large_ui"),
                     ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 5)],
                    [f'<on event="a" run="port coop begin" open="{base}/player_2_profile_screen" branch="true"/>',
                     f'<on event="start" run="port coop begin" open="{base}/player_2_profile_screen" branch="true"/>',
                     '<on event="left_mouse" run="mouse emit accept event"/>'])
    lines += _widget(f"{base}/player_2_profile_screen", [("width", 640), ("height", 480),
                                                         ("flags", "pass_unhandled_to_focused_child"),
                                                         ("bitmap", "bitmaps/gradient")],
                     ['<child widget="main_menu/new_select/sel_list_desc_bkd"/>',
                      f'<child widget="{base}/player_2_profile_list"/>',
                      f'<child widget="{base}/header_player_2"/>',
                      f'<child widget="{base}/player_2_help" x="20" y="416"/>'])
    lines += _header(f"{base}/header_player_2", f"{base}/header_player_2")
    rows = [f'<child widget="{base}/list_item_{index}" x="20" y="{73 + 30 * index}"/>' for index in range(11)]
    lines += _widget(f"{base}/player_2_profile_list",
                     [("type", "column_list"), ("width", 640), ("height", 480),
                      ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                      ("description", "main_menu/profile_manager/player_profile_extended_desc")],
                     ['<data input="3wide player profile list update"/>',
                      '<on event="created" run="port coop player 2 list initialize"/>',
                      '<on event="deleted" run="player profile list dispose"/>',
                      '<on event="custom_activation" run="port coop player 2" '
                      'open="main_menu/solo_level_select/solo_level_select_screen" branch="true"/>',
                      *rows, f'<child widget="{base}/player_2_button_bar" y="414"/>'])
    for index in range(11):
        lines += _widget(f"{base}/list_item_{index}",
                         [("width", 402 if index == 0 else 390), ("height", 28),
                          ("bitmap", "bitmaps/sel_list_item_bkd_top" if index == 0 else "bitmaps/sel_list_item_bkd"),
                          ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("align", "center"), ("text_y", 3)],
                         ['<on event="a" run="single prev cl item activated"/>',
                          '<on event="start" run="single prev cl item activated"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          '<child widget="main_menu/new_select/list_item_text" x="25"/>',
                          '<child widget="main_menu/new_select/list_item_arrows"/>'])
    lines += _widget(f"{base}/player_2_help", [("type", "text"), ("width", 350), ("height", 24),
                                               ("text", "Player 2: choose with your controller."),
                                               ("font", "ui\\small_ui"), ("color", "#FF2896FF"), ("text_y", 5),
                                               ("text_flags", "no_focus_test")], [])
    lines += _widget(f"{base}/player_2_button_bar", [("type", "column_list"), ("width", 640), ("height", 28),
                                                     ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     ['<data input="common button bar update"/>',
                      '<child widget="main_menu/profile_manager/profile_manager_button_ok" x="380" y="1"/>',
                      '<child widget="common_button_back" x="510" y="1"/>'])
    return lines


def _item_options_extras() -> list:
    """Item Options' rows of the port's: the map's weapons (YES or NO), and
    the loadout, CATEGORY (the weapon set) or CUSTOM (each player's primary
    and secondary weapons)"""
    base = "main_menu/settings_select/multiplayer_setup/item_options_edit"
    lines = []
    for key, strings, label in (("map_weapons", "var_map_weapons", 8), ("loadout", "var_loadout", 5),
                                ("primary_weapon", "var_loadout_weapon", 6),
                                ("secondary_weapon", "var_loadout_weapon", 7)):
        lines += _widget(f"{base}/op_{key}", [("width", 512), ("height", 28),
                                               ("flags", "pass_unhandled_to_focused_child"),
                                               ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         [f'<child widget="{base}/{key}_label"/>',
                          f'<child widget="{base}/{key}_spinner" x="286" y="1"/>'])
        lines += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1), ("width", 300), ("height", 22),
                                                   ("string_list", f"{base}/item_options_labels"),
                                                   ("string_index", label), ("font", "ui\\large_ui"),
                                                   ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
        lines += _widget(f"{base}/{key}_spinner",
                         [("type", "spinner"), ("top", 2), ("width", 206), ("height", 20),
                          ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                          ("string_list", f"{base}/{strings}"), ("font", "ui\\large_ui"), ("color", "#FF2896FF"),
                          ("align", "center"), ("text_y", 4), ("list_flags", "items_from_strings"),
                          ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                          ("header_bounds", "7 -13 19 -7"), ("footer_bounds", "7 208 19 214")],
                         ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])
    lines += _strings(f"{base}/var_map_weapons", ["YES", "NO"])
    lines += _strings(f"{base}/var_loadout", ["CATEGORY", "CUSTOM"])
    lines += _strings(f"{base}/var_loadout_weapon", LOADOUT_WEAPONS)
    return lines


def _player_options_extras() -> list:
    """The host's aim-assist policy and shield effects, hidden while editing
    saved gametypes."""
    base = PLAYER_EDIT
    lines = []
    for key, label_index, words in (("aim_assist", 8, ["ALLOWED", "BLOCK M+KB"]),
                                    ("shield_effects", 9, SHIELD_EFFECTS)):
        lines += _widget(f"{base}/op_{key}", [("width", 512), ("height", 28),
            ("flags", "pass_unhandled_to_focused_child"), ("bitmap", "bitmaps/option_bkds"),
            ("color", "#FF2896FF")], [f'<child widget="{base}/{key}_label"/>',
            f'<child widget="{base}/{key}_spinner" x="286" y="1"/>'])
        lines += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1),
            ("width", 280), ("height", 22), ("string_list", f"{base}/player_options_labels"),
            ("string_index", label_index), ("font", "ui\\large_ui"), ("color", "#FF2896FF"),
            ("text_x", 13), ("text_y", 4)], [])
        lines += _widget(f"{base}/{key}_spinner", [("type", "spinner"), ("top", 2),
            ("width", 206), ("height", 20), ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
            ("string_list", f"{base}/var_{key}"), ("font", "ui\\large_ui"),
            ("color", "#FF2896FF"), ("align", "center"), ("text_y", 4),
            ("list_flags", "items_from_strings"), ("header_bitmap", "bitmaps/arrow_sm_left"),
            ("footer_bitmap", "bitmaps/arrow_sm_right"), ("header_bounds", "7 -13 19 -7"),
            ("footer_bounds", "7 208 19 214")], ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])
    return lines + _strings(f"{base}/var_aim_assist", ["ALLOWED", "BLOCK M+KB"]) + \
        _strings(f"{base}/var_shield_effects", SHIELD_EFFECTS)


def _opence_options() -> list:
    """the gametype editor's OPENCE OPTIONS: its row, after the editor's own
    (as they are laid out), and its screen, as the gametype's options' are
    (_setup_option_screen), its spinners the gametype's (menu_functions.c's
    gametype_options), its header its words (no picture of its own)"""
    lines = _setup_option_screen(OPENCE_EDIT, "opence", OPENCE_ROWS)
    lines = [line.replace("port setup options init", "mp profile init opence options")
             .replace("port setup options save", "mp profile set opence options")
             .replace(f'bitmap="{OPENCE_EDIT}/header_opence"',
                      'type="text" text="OPENCE OPTIONS" font="ui\\large_ui" color="#FFFFFFFF" text_x="13" text_y="16"')
             for line in lines]
    lines += _widget(OPENCE_ITEM, [("type", "text"), ("left", 51), ("top", 309), ("width", 232), ("height", 32),
                                   ("bitmap", "bitmaps/list_item_bkd"), ("text", "OPENCE OPTIONS"),
                                   ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 5)],
                     [f'<on event="a" open="{OPENCE_EDIT}/opence_screen"/>',
                      f'<on event="start" open="{OPENCE_EDIT}/opence_screen"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    return lines


def _teamplay_options_extras() -> list:
    """Teamplay Options' rows of the port's: the gametype's FORCE TEAM
    SPAWNS (game_engine.c), then the host's voice chat
    (port/linux/game/network_voice.c) and vote kicks (network_votekick.c),
    in Server Setup's copy only (menu_functions.c's gametype_options_init),
    kept in config.toml"""
    lines = []
    for index, (key, label, strings, _) in enumerate(TEAMPLAY_EXTRA_ROWS):
        lines += _widget(f"{TEAMPLAY_EDIT}/op_{key}", [("width", 512), ("height", 28),
                                                       ("flags", "pass_unhandled_to_focused_child"),
                                                       ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         [f'<child widget="{TEAMPLAY_EDIT}/{key}_label"/>',
                          f'<child widget="{TEAMPLAY_EDIT}/{key}_spinner" x="286" y="1"/>'])
        lines += _widget(f"{TEAMPLAY_EDIT}/{key}_label", [("type", "text"), ("controller", 1), ("width", 300),
                                                          ("height", 22),
                                                          ("string_list", f"{TEAMPLAY_EDIT}/teamplay_options_labels"),
                                                          ("string_index", 3 + index), ("font", "ui\\large_ui"),
                                                          ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
        lines += _widget(f"{TEAMPLAY_EDIT}/{key}_spinner",
                         [("type", "spinner"), ("top", 2), ("width", 206), ("height", 20),
                          ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                          ("string_list", f"{TEAMPLAY_EDIT}/var_{key}"), ("font", "ui\\large_ui"),
                          ("color", "#FF2896FF"), ("align", "center"), ("text_y", 4),
                          ("list_flags", "items_from_strings"),
                          ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                          ("header_bounds", "7 -13 19 -7"), ("footer_bounds", "7 208 19 214")],
                         ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])
        lines += _strings(f"{TEAMPLAY_EDIT}/var_{key}", strings)
    return lines


def _map_kind() -> list:
    """the map lists' first row (New Game's and the Map screen's), as the
    gametype list's chooser: a spinner of SINGLEPLAYER or MULTIPLAYER maps,
    or the Custom Edition maps of the custom_maps folder of either kind
    (menu_functions.c's map lists), for either controller (split screen
    co-op's New Game takes both)"""
    lines = _widget(MAP_KIND_CHOOSER, [("width", 256), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                                       ("bitmap", "bitmaps/option_bkds_sm"), ("font", "ui\\large_ui"),
                                       ("color", "#FF2896FF"), ("align", "center"), ("text_y", 3)],
                    [f'<child widget="{MAP_KIND_CHOOSER}_spinner" x="9" y="2"/>'])
    # (wider than the gametype chooser's, for CUSTOM SINGLEPLAYER, its
    # arrows at the row's ends)
    lines += _widget(f"{MAP_KIND_CHOOSER}_spinner",
                     [("type", "spinner"), ("width", 238), ("height", 22), ("flags", "left_right_tabs_items"),
                      ("string_list", "main_menu/new_select/var_map_kinds"), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 1), ("list_flags", "items_from_strings"),
                      ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                      ("header_bounds", "7 -6 19 0"), ("footer_bounds", "7 238 19 244")],
                     ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])
    lines += _strings("main_menu/new_select/var_map_kinds",
                      ["SINGLEPLAYER", "MULTIPLAYER", "CUSTOM SINGLEPLAYER", "CUSTOM MULTIPLAYER"])
    return lines


def multiplayer_files() -> dict:
    """the port's multiplayer widgets: the browser's additions, the server
    settings, the lobby"""
    head = ['<?xml version="1.0" encoding="UTF-8"?>',
            "<!-- The port's multiplayer screens, in the PC version's style (tools/port_settings.py) -->", "<menus>"]
    return {
        f"{MT}/join_game".replace("/", ".") + ".port.xml": head + _join_game_extras() + ["</menus>", ""],
        f"{MT}/server_settings".replace("/", ".") + ".xml": head + _server_settings() + ["</menus>", ""],
        f"{MT}/lobby".replace("/", ".") + ".xml": head + _lobby() + ["</menus>", ""],
        f"{MT}/coop".replace("/", ".") + ".xml": head + _coop() + ["</menus>", ""],
        "main_menu/new_select".replace("/", ".") + ".port.xml": head + _map_kind() + ["</menus>", ""],
        "main_menu/settings_select/multiplayer_setup/item_options_edit".replace("/", ".") + ".port.xml": head + _item_options_extras() + ["</menus>", ""],
        PLAYER_EDIT.replace("/", ".") + ".port.xml": head + _player_options_extras() + ["</menus>", ""],
        TEAMPLAY_EDIT.replace("/", ".") + ".port.xml": head + _teamplay_options_extras() + ["</menus>", ""],
        OPENCE_EDIT.replace("/", ".") + ".port.xml": head + _opence_options() + ["</menus>", ""],
        MODS.replace("/", ".") + ".xml": [head[0], "<!-- The port's Mods screen, in the PC version's style "
                                          "(tools/port_settings.py) -->", head[2]] + _mods() + _mod_store() + ["</menus>", ""],
    }


REPLACED_FOLDERS = [f"{MT}/server_settings"]


# ---------- the mods (port/linux/game/mods.c, docs/mods.md): the main menu's
# MODS, which opens the list of the mods folder's mods, in their load order,
# each turned on or off and moved up or down (menu_functions.c's "port mods"
# functions)

# the redraws' main menu items (svg/shell/main_menu/menu_*.svg): their
# letters' top and scale, and where their middle is
MENU_ITEM_TOP, MENU_ITEM_SCALE, MENU_ITEM_MIDDLE = 6.57, 0.9037, 130.0
# the space between two of the redraws' letters, in their units (their
# capitals are 21.5 high), for the pairs the port's items have: no redraw has
# these pairs, so each is the sum of a space after the first letter and one
# before the second, fitted (least squares) to the 53 pairs the redraws' items
# and headers have (D-I, of the main menu's Credits, gives D's right side)
MENU_ITEM_SPACES = {"MO": 3.17, "OD": 2.8, "DS": 2.3}
# the mods' status icons (port/assets/icons/lucide): a mod out of the order it
# asks for, and one that cannot be played (another mod says it cannot be
# played with it, or this build cannot play it): menu_functions.c's
# mods_update shows frame 1 or 2
MOD_STATUS_ICONS = [("triangle-alert", "#FFB020"), ("circle-x", "#FF4040")]


def redraw_letters(redraws: Path) -> dict:
    """the redraws' letters (their <path id="gA">s, one drawing of each
    letter in every item and header), by letter"""
    letters = {}
    for svg in sorted(redraws.rglob("*.svg")):
        if any(part.startswith("_") for part in svg.relative_to(redraws).parts):
            continue
        for letter, path in re.findall(r'<path id="g([A-Z])" d="([^"]+)"', svg.read_text()):
            if letters.setdefault(letter, path) != path:
                sys.exit(f"{svg}: the redraws draw {letter} two ways")
    return letters


def menu_item_frames(text: str, redraws: Path) -> list:
    """the frames (svg, width, height) of a main menu item of the port's: its
    text in the redraws' own letters (redraw_letters), spaced, placed and
    coloured as their items are, dim, then white with their blue glow"""
    from fontTools.pens.boundsPen import BoundsPen
    from fontTools.svgLib.path import parse_path

    letters = redraw_letters(redraws)
    widths = []
    for letter in text:
        pen = BoundsPen(None)
        parse_path(letters[letter], pen)
        widths.append(pen.bounds[2])
    spaces = [MENU_ITEM_SPACES[first + second] for first, second in zip(text, text[1:])]
    x = MENU_ITEM_MIDDLE - (sum(widths) + sum(spaces)) * MENU_ITEM_SCALE / 2
    uses = []
    for index, letter in enumerate(text):
        uses.append(f'<use href="#g{letter}" transform="translate({x:.2f},{MENU_ITEM_TOP}) scale({MENU_ITEM_SCALE})"/>')
        x += (widths[index] + (spaces[index] if index < len(spaces) else 0)) * MENU_ITEM_SCALE
    paths = [f'    <path id="g{letter}" d="{letters[letter]}"/>' for letter in sorted(set(text))]
    head = ['<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="256" '
            'height="64" viewBox="0 0 256 64">', "  <defs>", *paths]
    dim = [*head, "  </defs>", f'  <!-- "{text}" (normal state: dim blue) -->',
           '  <g id="text" fill="#2895ff" fill-opacity="0.467">', *[f"    {use}" for use in uses], "  </g>", "</svg>", ""]
    glow = [*head, '    <filter id="glow" filterUnits="userSpaceOnUse" x="0" y="0" width="256" height="64">',
            '      <feGaussianBlur stdDeviation="2.19"/>', "    </filter>", "  </defs>",
            f'  <!-- "{text}" (highlighted: white with blue glow) -->',
            '  <g fill="#2895ff" stroke="#2895ff" stroke-width="6.3" stroke-linejoin="round" opacity="0.650" '
            'filter="url(#glow)">', *[f"    {use}" for use in uses], "  </g>",
            '  <g id="text" fill="#fff">', *[f"    {use}" for use in uses], "  </g>", "</svg>", ""]
    return [("\n".join(dim), 256, 64), ("\n".join(glow), 256, 64)]


def mod_status_frames(icons: Path, size: int = 24) -> list:
    """the frames of the mods' status icon, `size` pixels square (the menus
    draw a bitmap at its size): none, out of order, incompatible"""
    blank = f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 24 24"/>\n'
    return [(blank, size, size)] + [
        ((icons / f"{icon}.svg").read_text().replace('stroke="currentColor"', f'stroke="{color}"')
         .replace('width="24"', f'width="{size}"').replace('height="24"', f'height="{size}"'), size, size)
        for icon, color in MOD_STATUS_ICONS]


def _text_button(name: str, text: str, handlers: list, width: int = 128) -> list:
    """a button of the button bar with its own words"""
    # (a button narrower than the art's 128 has the flag that scales the whole art to it: else the engine cuts the art's rounded right end off)
    pairs = [("type", "text"), ("width", width), ("height", 24), ("bitmap", "bitmaps/text_button_background"),
             ("flags", "fit_bitmap_width" if width < 128 else None), ("text", text), ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)]
    return _widget(name, pairs, handlers + ['<on event="left_mouse" run="mouse emit accept event"/>'])


def _mods() -> list:
    """the main menu's MODS and its screen, laid out as the lobby is: on the
    left two thirds the mods as a resource pack list has them (the one at the
    top playing over those below it: the load order upside down), a row each
    with its status icon, name and ON or OFF; on the right third, in the
    lobby's frame, the mod chosen: its name, what is wrong with it, whether it
    is on and its priority, and what its mod.toml says. A turns the mod chosen
    on or off, left and right (and MOVE UP and MOVE DOWN, for the mouse) move
    it; OK keeps it all (load_order.toml) and plays them, Cancel forgets it
    (menu_functions.c)"""
    base = MODS
    lines = _widget("main_menu/main_menu_item_mods",
                    [("type", "text"), ("width", 256), ("height", 33), ("bitmap", "main_menu/menu_mods")],
                    [f'<on event="a" open="{base}/mods_screen"/>', f'<on event="start" open="{base}/mods_screen"/>',
                     '<on event="left_mouse" run="mouse emit accept event"/>'])
    lines += _widget(f"{base}/mods_screen", [("width", 640), ("height", 480),
                                             ("flags", "pass_unhandled_to_focused_child pause_game"),
                                             ("bitmap", "bitmaps/gradient")],
                     ['<on event="b" back="true"/>', '<on event="back" back="true"/>',
                      f'<on event="y" run="port mods store" open="{STORE}/store_screen"/>',
                      '<child widget="main_menu/new_select/sel_list_desc_bkd"/>',
                      f'<child widget="{base}/mods_list"/>',
                      f'<child widget="{base}/header_mods"/>'])
    lines += _header(f"{base}/header_mods", f"{base}/header_mods")
    # (the lobby's rows, MOD_ROWS of them, scrolling over more mods)
    rows = [f'<child widget="{base}/op_mod_{index + 1}" x="20" y="{73 + 30 * index}"/>' for index in range(MOD_ROWS)]
    lines += _widget(f"{base}/mods_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                           ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                                           ("description", f"{base}/mod_details")],
                     ['<data input="port mods update"/>', '<on event="created" run="port mods init"/>',
                      '<on event="deleted" run="port mods close"/>', *rows,
                      f'<child widget="{base}/button_bar" y="414"/>'])
    for index in range(MOD_ROWS):
        lines += _widget(f"{base}/op_mod_{index + 1}",
                         [("width", 402 if index == 0 else 390), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                          ("bitmap", "bitmaps/sel_list_item_bkd_top" if index == 0 else "bitmaps/sel_list_item_bkd"),
                          ("color", "#FF2896FF")],
                         ['<on event="a" run="port mods toggle"/>', '<on event="start" run="port mods toggle"/>',
                          '<on event="left" run="port mods move up"/>', '<on event="right" run="port mods move down"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          f'<child widget="{base}/mod_status" x="8" y="2"/>',
                          f'<child widget="{base}/mod_name" x="34"/>',
                          f'<child widget="{base}/mod_state" x="318"/>'])
    lines += _widget(f"{base}/mod_status", [("controller", 1), ("width", 24), ("height", 24),
                                            ("bitmap", "mods/status")], [])
    lines += _widget(f"{base}/mod_name",
                     [("type", "text"), ("controller", 1), ("width", 284), ("height", 22), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("text_y", 4)], [])
    lines += _widget(f"{base}/mod_state",
                     [("type", "text"), ("controller", 1), ("width", 66), ("height", 22), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 4), ("text_flags", "no_focus_test")], [])
    # (in the lobby's frame, sel_list_desc_bkd's: the mod chosen's name, the
    # icon of what is wrong with it, and the rest)
    lines += _widget(f"{base}/mod_details", [("width", 640), ("height", 480)],
                     [f'<child widget="{base}/mod_details_name"/>', f'<child widget="{base}/mod_details_status"/>',
                      f'<child widget="{base}/mod_details_text"/>'])
    lines += _widget(f"{base}/mod_details_name",
                     [("type", "text"), ("controller", 1), ("left", 412), ("top", 84), ("width", 200), ("height", 44),
                      ("font", "ui\\large_ui"), ("color", "#FFFFFFFF")], [])
    lines += _widget(f"{base}/mod_details_status", [("controller", 1), ("left", 411), ("top", 132), ("width", 16),
                                                    ("height", 16), ("bitmap", "mods/status_small")], [])
    lines += _widget(f"{base}/mod_details_text",
                     [("type", "text"), ("controller", 1), ("left", 412), ("top", 130), ("width", 200),
                      ("height", 272), ("font", "ui\\small_ui"), ("color", "#FF2896FF")], [])
    lines += _widget(f"{base}/button_bar",
                     [("type", "column_list"), ("width", 640), ("height", 28),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     [f'<child widget="{base}/button_up" y="1"/>', f'<child widget="{base}/button_down" x="116" y="1"/>',
                      f'<child widget="{base}/button_store" x="232" y="1"/>',
                      f'<child widget="{base}/button_ok" x="380" y="1"/>',
                      '<child widget="common_button_cancel" x="510" y="1"/>'])
    # (three buttons of 112 on the left: MOVE UP, MOVE DOWN and STORE, which keeps the order and opens the mod store)
    lines += _text_button(f"{base}/button_up", "MOVE UP", ['<on event="a" run="port mods move up"/>',
                                                          '<on event="start" run="port mods move up"/>'], 112)
    lines += _text_button(f"{base}/button_down", "MOVE DOWN", ['<on event="a" run="port mods move down"/>',
                                                              '<on event="start" run="port mods move down"/>'], 112)
    lines += _text_button(f"{base}/button_store", "STORE",
                          [f'<on event="a" run="port mods store" open="{STORE}/store_screen"/>',
                           f'<on event="start" run="port mods store" open="{STORE}/store_screen"/>'], 112)
    lines += _button(f"{base}/button_ok", 1, ['<on event="a" run="port mods save" back="true"/>',
                                              '<on event="start" run="port mods save" back="true"/>'])
    return lines


# ---------- the mod store (port/linux/src/modstore.c, port/linux/game/menu_store.inl): the Mods page's STORE opens a list of the catalog's mods, on the social screens'
# spacing system (below: "THE SPACING SYSTEM"; the helpers _social_* build it): the title at the margin with the count of mods on its baseline, nine rows of 28 from y 76 with the
# mod's name, its author, its size and its state, the description of the row chosen (or what the last job said) under them, the footer (who made it; the scroll counter), the
# arrows at the list's edge, the words of a store that is off, loading, failed or empty in the list's middle, and the button bar DOWNLOAD / UPDATE / STOP, REMOVE, REFRESH, BACK
STORE = f"{MODS}/store"
STORE_ROWS = 9
# a column: its name, x in the row, width, font (the widths are screens.h's SCREENS_STORE_*_WIDTH: a test reads both). The text starts 16 in, the columns are 8 apart or more, and the
# last ends 16 before the row's end (496)
STORE_COLUMNS = [("name", 12, 164, "large"), ("author", 184, 94, "small"), ("size", 282, 58, "small"), ("status", 344, 152, "small")]


def _mod_store() -> list:
    """the mod store's screen: the catalog's mods (name, author, size, state), the row chosen's description (or what the last job said) under them, and
    the buttons. A on a row downloads or updates the mod, X removes it (the buttons too), Y asks for the catalog again, B goes back (and stops a
    download, which is kept to go on from)"""
    base = STORE
    arrows, arrow_lines = _social_arrows(base, "store")
    lines = _social_screen(f"{base}/store_screen",
                           ['<on event="created" run="port store open"/>', '<on event="deleted" run="port store close"/>',
                            '<on event="b" back="true"/>', '<on event="back" back="true"/>',
                            '<on event="x" run="port store remove"/>', '<on event="y" run="port store refresh"/>'],
                           [f'<child widget="{base}/store_list"/>', f'<child widget="{base}/store_title"/>', f'<child widget="{base}/store_head"/>',
                            f'<child widget="{base}/store_footer"/>', f'<child widget="{base}/store_count"/>', f'<child widget="{base}/store_detail"/>',
                            f'<child widget="{base}/store_empty"/>', *arrows])
    lines += _social_title(base, "store_title", "MOD STORE", 280)
    # (the count of mods at the right, on the title's baseline)
    lines += _social_text(f"{base}/store_head", 340, SOCIAL_TITLE_TOP, SOCIAL_TEXT_RIGHT - 340, 22, align="right", color="#FFFFFFFF", text_y=0)
    lines += _social_footer(base, "store_footer", "store_count")
    lines += _social_detail(base, "store_detail")
    lines += _social_empty(base, "store_empty")
    lines += arrow_lines
    rows = [f'<child widget="{base}/store_item_{index + 1}" x="{SOCIAL_ROWS_LEFT}" y="{SOCIAL_ROWS_TOP + index * SOCIAL_ROW_PITCH}"/>' for index in range(STORE_ROWS)]
    lines += _widget(f"{base}/store_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                            ("flags", "pass_unhandled_to_focused_child up_down_tabs_children")],
                     ['<data input="port store update"/>', *rows, f'<child widget="{base}/store_button_bar" y="414"/>'])
    for index in range(STORE_ROWS):
        lines += _social_row(f"{base}/store_item_{index + 1}", STORE_COLUMNS, "store_cell",
                             ['<on event="a" run="port store row"/>', '<on event="start" run="port store row"/>'], base=base)
    for column, _, width, font in STORE_COLUMNS:
        lines += _social_cell(f"{base}/store_cell_{column}", width, font, "right" if column == "size" else None)
    # (four slots of the bar, the social screens' own places: the first has DOWNLOAD, UPDATE or STOP, whichever applies to the row and the job, one at a time)
    buttons = [("store_button_get", "DOWNLOAD", "port store get", 120), ("store_button_update", "UPDATE", "port store get", 120),
               ("store_button_stop", "STOP", "port store stop", 120), ("store_button_remove", "REMOVE", "port store remove", 250),
               ("store_button_refresh", "REFRESH", "port store refresh", 380)]
    children, widgets = [], []
    for key, caption, run, x in buttons:
        children.append(f'<child widget="{base}/{key}" x="{x}" y="1"/>')
        widgets += _widget(f"{base}/{key}", [("type", "text"), ("width", 128), ("height", 24), ("bitmap", "bitmaps/text_button_background"),
                                            ("text", caption), ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)],
                           [f'<on event="a" run="{run}"/>', f'<on event="start" run="{run}"/>', '<on event="left_mouse" run="mouse emit accept event"/>'])
    children.append(f'<child widget="{base}/store_button_back" x="510" y="1"/>')
    widgets += _widget(f"{base}/store_button_back", [("type", "text"), ("width", 128), ("height", 24), ("bitmap", "bitmaps/text_button_background"),
                                                    ("text", "BACK"), ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)],
                       ['<on event="a" run="mouse emit back event"/>', '<on event="start" run="mouse emit back event"/>',
                        '<on event="left_mouse" run="mouse emit accept event"/>'])
    lines += _widget(f"{base}/store_button_bar", [("type", "column_list"), ("width", 640), ("height", 28),
                                                  ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")], children) + widgets
    return lines + _social_band(base)


# ---------- the social screens (port/linux/game/screens.h, menu_screens.c): the Players page (the pause menu's PLAYERS, and the lobby's rows), its
# actions, the Friends screen with its tabs, the code a friend is added by, and the bot settings. They are built from the settings screens' own
# grammar (rows of bitmaps/option_bkds at x 54, 28 apart, the button bar at y 414, text headers in the large font as the map vote's), with no new picture;
# their rows' texts and colours come from the provider's rows (screens.c's cells), so the numbers below are screens.h's (a test reads both)
#
# THE SPACING SYSTEM (2026-11-10; reports/research/2026-11-10/ux2-result.md has the drawing). Units of the menus' 640 x 480 screen: 2.2535 pixels each on the TV
# (1920 x 1080), 1.0023 on the GamePad (854 x 480), centred in a screen 852 units wide. The base unit is 4: every gap, size and place below is a multiple of it, or one
# of the game's own numbers that the art fixes (a row is 28 high, a button 22, the panel's top line is at 70, its bottom line at 409).
#
#   y   0 -  24   the margin: TVs may overscan it; no text, only the header band's backdrop
#      24 -  38   the title (large font, capitals 24.5 to 37.5) at the left, what belongs with it (a count, the service) at the right on its baseline
#      44 -  66   the tab row (Friends only), 6 under the title's capitals and 4 above the panel's top line, which is at 70
#      76 - 328   the list: 9 rows, 28 apart, the first 5 below the line; the focus pill is the row's 28 and the text is centred on it
#     336 - 384   the detail line: up to two lines of the large font, 8 below the last row
#     384 - 404   the footer: one small line at the left (a note, the code), the scroll counter at the right (capitals 388 to 398, 11 above the panel's bottom line)
#     414 - 438   the button bar (the game's: the buttons' own 22 high pills at y 415)
#
#   x  54 - 566   the list, the detail and the footer: the settings screens' column. Text sits 16 in from a row's ends (4 of them are the engine's own inset), columns are
#                 8 or more apart, a row's last text ends 16 before the row's end. Nothing but the header band and the panel reaches the block's edge.
#
# The engine puts a text's capitals' top 3.5 units (large) or 3.7 (small) below the widget's top plus text_y (measured on the console's pictures of 2026-11-09), and a few
# units in from the left: the numbers below are written for that, so that a text is centred on its pill and the title's capitals start at the margin.
SOCIAL = f"{MT}/screens"
UNIT = 4
SAFE_TOP, SAFE_BOTTOM = 24, 456
PANEL_LINE, PANEL_BOTTOM_LINE = 70, 409
SOCIAL_ROWS_LEFT, SOCIAL_ROWS_TOP, SOCIAL_ROW_PITCH, SOCIAL_ROW_WIDTH, SOCIAL_ROW_HEIGHT = 54, 76, 28, 512, 28
# (the tab's art, text_button_background, is 128 units across and the engine cuts a bitmap at its widget's edge, which took the tabs' right ends off: the tabs have the flag
# fit_bitmap_width, which draws the art's whole width scaled to the widget's 116 -- 0.91 of its width, the height as it is; tools/menus_clip_check.py checks every menu for the
# defect. The tabs, 4 of 116 and 4 apart, are centred on the rows' 54 to 566: 72 to 548, clear by 8 of the HUD's bars at the top corners of a game under the pause menu: they end at x 64 and begin at 556)
SOCIAL_TAB_LEFT, SOCIAL_TAB_TOP, SOCIAL_TAB_PITCH, SOCIAL_TAB_WIDTH, SOCIAL_TAB_HEIGHT = 72, 44, 120, 116, 26
# nine rows on the Players page too (it had ten: the footer and a two line detail need the room), the actions' eight
SOCIAL_PLAYER_ROWS, SOCIAL_FRIEND_ROWS, SOCIAL_ACTION_ROWS = 9, 9, 8
SOCIAL_GAP = 8
# the engine's offset of a text's capitals from the widget's top plus text_y, and their height, by font
SOCIAL_CAP_OFFSET = {"large": 3.5, "small": 3.7}
SOCIAL_CAP_HEIGHT = {"large": 13, "small": 10}
SOCIAL_TITLE_TOP = 21
SOCIAL_SERVICE_TOP = 24
SOCIAL_LIST_BOTTOM = SOCIAL_ROWS_TOP + 9 * SOCIAL_ROW_PITCH
SOCIAL_DETAIL_TOP, SOCIAL_DETAIL_HEIGHT = SOCIAL_LIST_BOTTOM + SOCIAL_GAP, 48
SOCIAL_FOOTER_TOP, SOCIAL_FOOTER_HEIGHT = 384, 20
SOCIAL_EMPTY_TOP, SOCIAL_EMPTY_HEIGHT = 176, 66
SOCIAL_BAND_HEIGHT = 240
# a column: its x in the row, its width, its font (large or small). A row's pill is 512 wide; the text starts 16 in (the column 12 or 13 in and the engine's inset)
# and the last column ends 16 before the end; the columns are 8 apart or more
SOCIAL_PLAYER_COLUMNS = [("name", 12, 200, "large"), ("tag", 220, 138, "small"), ("team", 366, 52, "small"), ("ping", 428, 68, "small")]
SOCIAL_FRIEND_COLUMNS = [("mark", 10, 24, "large"), ("name", 38, 196, "large"), ("status", 242, 254, "small")]
SOCIAL_LOBBY_COLUMNS = [("name", 22, 196, "large"), ("tag", 214, 164, "small")]
SOCIAL_TEXT_COLOR = "#FF2896FF"
SOCIAL_TITLE_COLOR = "#FF2995FD"
# (the footer's and the hints' colour: a lighter azure than the rows', for the small font on the panel's moving background; amber for a warning)
SOCIAL_HINT_COLOR = "#FF8CC8FF"
SOCIAL_WARN_COLOR = "#FFFFB020"
SOCIAL_FONTS = {"large": ("ui\\large_ui", 4), "small": ("ui\\small_ui", 6)}
# (the rows' text starts 16 in from their pills: a left aligned text outside a row is shifted to start there; one aligned to the right ends 16 before the pills' right end, so its
# widget ends at 554, 12 before it)
SOCIAL_TEXT_X = {"large": 12, "small": 13}
SOCIAL_TEXT_RIGHT = SOCIAL_ROWS_LEFT + SOCIAL_ROW_WIDTH - 12


def _social_text(name: str, left: int, top: int, width: int, height: int, font: str = "large", color: str = SOCIAL_TEXT_COLOR,
                 align: str = None, text: str = None, text_y: int = None, text_x: int = None) -> list:
    """a text the screens' code fills in (or says, with text). A left aligned text is shifted by text_x to start where the rows' text does (x 70: the 54 of the pills, 12 in and the
    engine's 4 of inset; the small font's inset is 1 less, so 13)"""
    if text_x is None and align in (None, "left"):
        text_x = SOCIAL_TEXT_X[font]
    pairs = [("type", "text"), ("controller", 1), ("left", left), ("top", top), ("width", width), ("height", height),
             ("font", SOCIAL_FONTS[font][0]), ("color", color), ("align", align),
             ("text", text), ("text_x", text_x), ("text_y", text_y if text_y is not None else SOCIAL_FONTS[font][1]),
             ("text_flags", "no_focus_test")]
    return _widget(name, pairs, [])


def _social_cell(name: str, width: int, font: str, align: str = None) -> list:
    """a row's cell: a text in the row, set and coloured by the code (pc_menu_text_color) by the cell's name"""
    # (as high as the row: a letter with a tail, g or y, is not cut at the bottom by a smaller box)
    pairs = [("type", "text"), ("width", width), ("height", SOCIAL_ROW_HEIGHT), ("font", SOCIAL_FONTS[font][0]), ("color", SOCIAL_TEXT_COLOR),
             ("align", align), ("text_y", SOCIAL_FONTS[font][1]), ("text_flags", "no_focus_test")]
    return _widget(name, pairs, [])


def _social_row(name: str, columns: list, prefix: str, handlers: list, width: int = SOCIAL_ROW_WIDTH, bitmap: str = "bitmaps/option_bkds", base: str = None) -> list:
    """a row of the list: the focus pill (the settings screens' option_bkds: its second frame has the focus), its cells in their places. The cells sit at the row's top: their
    text_y (large 4, small 6, the settings screens' own) puts the capitals on the pill's middle"""
    children = [f'<child widget="{base or SOCIAL}/{prefix}_{column}" x="{x}" y="0"/>' for column, x, _, _ in columns]
    return _widget(name, [("width", width), ("height", SOCIAL_ROW_HEIGHT), ("flags", "pass_unhandled_to_focused_child"), ("bitmap", bitmap),
                          ("color", SOCIAL_TEXT_COLOR)], handlers + ['<on event="left_mouse" run="mouse emit accept event"/>'] + children)


def _social_buttons(base: str, bar: str, buttons: list, five: bool = False) -> list:
    """the bar of buttons at the bottom: (key, caption, handlers); four at 120, 250, 380, 510 (128 wide), five at 4, 131, 258, 385, 512 (126 wide)"""
    lines, children = [], []
    for index, (key, caption, handlers) in enumerate(buttons):
        x = 4 + 127 * index if five else 510 - (len(buttons) - 1 - index) * 130
        children.append(f'<child widget="{base}/{key}" x="{x}" y="1"/>')
        lines += _widget(f"{base}/{key}", [("type", "text"), ("width", 126 if five else 128), ("height", 24), ("bitmap", "bitmaps/text_button_background"),
                                          ("flags", "fit_bitmap_width" if five else None), ("text", caption), ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)],
                         handlers + ['<on event="left_mouse" run="mouse emit accept event"/>'])
    return _widget(bar, [("type", "column_list"), ("width", 640), ("height", 28),
                         ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")], children) + lines


def _social_title(base: str, name: str, text: str = None, width: int = 360) -> list:
    """a screen's title: large, azure, its capitals' top at the safe margin (24.5: its round letters' tops overshoot to 24), starting where the rows' text does"""
    return _social_text(f"{base}/{name}", SOCIAL_ROWS_LEFT, SOCIAL_TITLE_TOP, width, 22, color=SOCIAL_TITLE_COLOR, text=text, text_y=0)


def _social_detail(base: str, name: str, top: int = SOCIAL_DETAIL_TOP, height: int = SOCIAL_DETAIL_HEIGHT) -> list:
    """the line (two at most) under the rows: what the row chosen is, or what a button said"""
    return _social_text(f"{base}/{name}", SOCIAL_ROWS_LEFT, top, SOCIAL_ROW_WIDTH, height, color="#FFFFFFFF", text_y=0)


def _social_footer(base: str, note: str, count: str, color: str = SOCIAL_HINT_COLOR) -> list:
    """the footer under the detail: a note at the left (small), and the scroll counter ("4 OF 14") at the right, on one baseline"""
    return (_social_text(f"{base}/{note}", SOCIAL_ROWS_LEFT, SOCIAL_FOOTER_TOP, 380, SOCIAL_FOOTER_HEIGHT, "small", color=color, text_y=0) +
            _social_text(f"{base}/{count}", SOCIAL_ROWS_LEFT + 384, SOCIAL_FOOTER_TOP, SOCIAL_TEXT_RIGHT - SOCIAL_ROWS_LEFT - 384, SOCIAL_FOOTER_HEIGHT, "small", color=SOCIAL_HINT_COLOR,
                         align="right", text_y=0))


def _social_empty(base: str, name: str) -> list:
    """the words of an empty list (no friends, no requests, no party, no service), in the middle of the list's place, centred, up to three lines"""
    return _social_text(f"{base}/{name}", SOCIAL_ROWS_LEFT + 36, SOCIAL_EMPTY_TOP, SOCIAL_ROW_WIDTH - 72, SOCIAL_EMPTY_HEIGHT, color="#FFFFFFFF", align="center", text_y=0)


def _social_arrows(base: str, prefix: str) -> tuple:
    """the two arrows at the list's right edge that say there are more rows above or below (shown by code), and their widgets"""
    left = SOCIAL_ROWS_LEFT + SOCIAL_ROW_WIDTH + SOCIAL_GAP
    lines = []
    for way, top in (("up", SOCIAL_ROWS_TOP + 6), ("down", SOCIAL_LIST_BOTTOM - 22)):
        lines += _widget(f"{base}/{prefix}_more_{way}", [("controller", 1), ("left", left), ("top", top), ("width", 16), ("height", 16), ("bitmap", f"bitmaps/arrow_sm_{way}")], [])
    return [f'<child widget="{base}/{prefix}_more_{way}"/>' for way in ("up", "down")], lines


def _social_band(base: str) -> list:
    """the backdrop of the header (above the panel's top line, where the game's own screens leave the picture bare): a dark band of the panel's own darkness, so that the HUD of
    a game under the pause menu (the shield and health bars at the top corners) does not run through the title and the tabs. It is the engine's full screen dim (a flat bitmap
    at most 16 texels wide, a widget from 0, 0 over 320 x 240 is drawn across the whole screen: ui_widget.c's widen_to_screen) placed 170 up, so that its lower 70 are on screen."""
    return _widget(f"{base}/header_band", [("controller", 1), ("left", 0), ("top", 0), ("width", 640), ("height", SOCIAL_BAND_HEIGHT), ("bitmap", "bitmaps/semi_transparent_grey")], [])


def _social_screen(name: str, handlers: list, children: list) -> list:
    base = name.rsplit("/", 1)[0]
    band = f'<child widget="{base}/header_band" y="{PANEL_LINE - SOCIAL_BAND_HEIGHT}"/>'
    return _widget(name, [("width", 640), ("height", 480), ("flags", "pass_unhandled_to_focused_child pause_game"), ("bitmap", "bitmaps/gradient")],
                   handlers + [band] + children)


def _social_players() -> list:
    """the Players page: one row for each player (name, BOT <skill> or YOU or HOST or PARTY or FRIEND, team, ping), the line of what the row chosen is under them, and the
    buttons: BOT SETTINGS, ADD BOT, DROP BOT (the host's), FRIENDS, BACK. A on a row opens its actions, L drops a bot and R adds one (the host's). The note that the game keeps
    running is in the footer, not under the HUD's bars at the top"""
    base = SOCIAL
    arrows, arrow_lines = _social_arrows(base, "players")
    lines = _social_screen(f"{base}/players_screen",
                           ['<on event="created" run="port players open"/>', '<on event="b" back="true"/>', '<on event="back" back="true"/>',
                            '<on event="white" run="port players drop bot"/>', '<on event="black" run="port players add bot"/>'],
                           [f'<child widget="{base}/players_list"/>', f'<child widget="{base}/players_title"/>', f'<child widget="{base}/players_head"/>',
                            f'<child widget="{base}/players_banner"/>', f'<child widget="{base}/players_count"/>', f'<child widget="{base}/players_detail"/>',
                            f'<child widget="{base}/players_empty"/>', *arrows])
    lines += _social_title(base, "players_title", "PLAYERS")
    # (the count of players at the right, on the title's baseline)
    lines += _social_text(f"{base}/players_head", 340, SOCIAL_TITLE_TOP, SOCIAL_TEXT_RIGHT - 340, 22, align="right", color="#FFFFFFFF", text_y=0)
    lines += _social_footer(base, "players_banner", "players_count", SOCIAL_WARN_COLOR)
    lines += _social_detail(base, "players_detail")
    lines += _social_empty(base, "players_empty")
    lines += arrow_lines
    rows = [f'<child widget="{base}/player_item_{index + 1}" x="{SOCIAL_ROWS_LEFT}" y="{SOCIAL_ROWS_TOP + index * SOCIAL_ROW_PITCH}"/>'
            for index in range(SOCIAL_PLAYER_ROWS)]
    lines += _widget(f"{base}/players_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                              ("flags", "pass_unhandled_to_focused_child up_down_tabs_children")],
                     ['<data input="port players update"/>', *rows, f'<child widget="{base}/players_button_bar" y="414"/>'])
    for index in range(SOCIAL_PLAYER_ROWS):
        lines += _social_row(f"{base}/player_item_{index + 1}", SOCIAL_PLAYER_COLUMNS, "scr_player",
                             [f'<on event="a" run="port players row" open="{base}/actions_screen" branch="true"/>',
                              f'<on event="start" run="port players row" open="{base}/actions_screen" branch="true"/>'])
    for column, _, width, font in SOCIAL_PLAYER_COLUMNS:
        lines += _social_cell(f"{base}/scr_player_{column}", width, font)
    lines += _social_buttons(base, f"{base}/players_button_bar", [
        ("players_button_bots", "BOT SETTINGS", [f'<on event="a" open="{PE}/bot_settings/bot_settings_screen"/>',
                                                 f'<on event="start" open="{PE}/bot_settings/bot_settings_screen"/>']),
        ("players_button_add", "ADD BOT", ['<on event="a" run="port players add bot"/>', '<on event="start" run="port players add bot"/>']),
        ("players_button_drop", "DROP BOT", ['<on event="a" run="port players drop bot"/>', '<on event="start" run="port players drop bot"/>']),
        ("players_button_friends", "FRIENDS", [f'<on event="a" open="{base}/friends_screen"/>', f'<on event="start" open="{base}/friends_screen"/>']),
        ("players_button_back", "BACK", ['<on event="a" run="mouse emit back event"/>', '<on event="start" run="mouse emit back event"/>']),
    ], five=True)
    return lines


def _social_actions() -> list:
    """the actions of the row chosen (a person's: mute, vote to kick, add as a friend...; a bot's: drop, skill, team; a friend's; a party member's): up to eight rows, the
    provider says which; A runs one, B goes back. A line under them says what the provider answered, in plain words"""
    base = SOCIAL
    lines = _social_screen(f"{base}/actions_screen",
                           ['<on event="created" run="port screens actions open"/>', '<on event="b" back="true"/>', '<on event="back" back="true"/>',
                            f'<on event="x" open="{PE}/bot_settings/bot_settings_screen"/>'],
                           [f'<child widget="{base}/actions_list"/>', f'<child widget="{base}/actions_title"/>', f'<child widget="{base}/actions_answer"/>'])
    lines += _social_title(base, "actions_title", width=SOCIAL_ROW_WIDTH)
    lines += _social_detail(base, "actions_answer", SOCIAL_ROWS_TOP + SOCIAL_ACTION_ROWS * SOCIAL_ROW_PITCH + SOCIAL_GAP)
    rows = [f'<child widget="{base}/action_item_{index + 1}" x="{SOCIAL_ROWS_LEFT}" y="{SOCIAL_ROWS_TOP + index * SOCIAL_ROW_PITCH}"/>'
            for index in range(SOCIAL_ACTION_ROWS)]
    lines += _widget(f"{base}/actions_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                              ("flags", "pass_unhandled_to_focused_child up_down_tabs_children")],
                     ['<data input="port screens actions update"/>', *rows])
    for index in range(SOCIAL_ACTION_ROWS):
        lines += _social_row(f"{base}/action_item_{index + 1}", [("text", 12, 480, "large")], "scr_action",
                             ['<on event="a" run="port screens action run"/>', '<on event="start" run="port screens action run"/>'])
    lines += _social_cell(f"{base}/scr_action_text", 480, "large")
    return lines


def _social_friends() -> list:
    """the Friends screen: four tabs (FRIENDS, REQUESTS, PARTY, BLOCKED: the shoulders turn them), a row for each, the line of what the row chosen is under them, the
    service's state at the top right, this machine's code and the scroll counter in the footer, and the buttons ADD FRIEND, JOIN, INVITE, BACK (JOIN and INVITE shown when they
    apply to the row)"""
    base = SOCIAL
    arrows, arrow_lines = _social_arrows(base, "friends")
    lines = _social_screen(f"{base}/friends_screen",
                           ['<on event="created" run="port friends open"/>', '<on event="b" back="true"/>', '<on event="back" back="true"/>',
                            '<on event="white" run="port friends tab previous"/>', '<on event="black" run="port friends tab next"/>',
                            f'<on event="y" open="{base}/add/friend_add_screen"/>'],
                           [f'<child widget="{base}/friends_list"/>', f'<child widget="{base}/friends_title"/>', f'<child widget="{base}/friends_service"/>',
                            f'<child widget="{base}/friends_code"/>', f'<child widget="{base}/friends_count"/>', f'<child widget="{base}/friends_detail"/>',
                            f'<child widget="{base}/friends_empty"/>', *arrows])
    lines += _social_title(base, "friends_title", "FRIENDS", 200)
    # (the service word at the right, on the title's baseline: the capitals of a small text end where the title's do, 37)
    lines += _social_text(f"{base}/friends_service", 330, SOCIAL_SERVICE_TOP, SOCIAL_TEXT_RIGHT - 330, 18, "small", color="#FFFFFFFF", align="right", text_y=0)
    lines += _social_footer(base, "friends_code", "friends_count")
    lines += _social_detail(base, "friends_detail")
    lines += _social_empty(base, "friends_empty")
    lines += arrow_lines
    rows = [f'<child widget="{base}/friend_item_{index + 1}" x="{SOCIAL_ROWS_LEFT}" y="{SOCIAL_ROWS_TOP + index * SOCIAL_ROW_PITCH}"/>'
            for index in range(SOCIAL_FRIEND_ROWS)]
    lines += _widget(f"{base}/friends_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                              ("flags", "pass_unhandled_to_focused_child up_down_tabs_children")],
                     ['<data input="port friends update"/>', f'<child widget="{base}/friends_tab_bar" y="{SOCIAL_TAB_TOP}"/>', *rows,
                      f'<child widget="{base}/friends_button_bar" y="414"/>'])
    tabs = [f'<child widget="{base}/friends_tab_{index}" x="{SOCIAL_TAB_LEFT + index * SOCIAL_TAB_PITCH}"/>' for index in range(4)]
    lines += _widget(f"{base}/friends_tab_bar", [("type", "column_list"), ("width", 640), ("height", SOCIAL_TAB_HEIGHT),
                                                 ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")], tabs)
    for index in range(4):
        # (a tab is a button's pill, 22 high: its text at the buttons' own text_y 2 is centred on it)
        lines += _widget(f"{base}/friends_tab_{index}",
                         [("type", "text"), ("width", SOCIAL_TAB_WIDTH), ("height", SOCIAL_TAB_HEIGHT), ("bitmap", "bitmaps/text_button_background"),
                          ("flags", "fit_bitmap_width"), ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)],
                         [f'<on event="a" run="port friends tab {index}"/>', f'<on event="start" run="port friends tab {index}"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>'])
    for index in range(SOCIAL_FRIEND_ROWS):
        lines += _social_row(f"{base}/friend_item_{index + 1}", SOCIAL_FRIEND_COLUMNS, "scr_friend",
                             [f'<on event="a" run="port friends row" open="{base}/actions_screen" branch="true"/>',
                              f'<on event="start" run="port friends row" open="{base}/actions_screen" branch="true"/>'])
    for column, _, width, font in SOCIAL_FRIEND_COLUMNS:
        lines += _social_cell(f"{base}/scr_friend_{column}", width, font, "center" if column == "mark" else None)
    lines += _social_buttons(base, f"{base}/friends_button_bar", [
        ("friends_button_add", "ADD FRIEND", [f'<on event="a" open="{base}/add/friend_add_screen"/>', f'<on event="start" open="{base}/add/friend_add_screen"/>']),
        ("friends_button_join", "JOIN", ['<on event="a" run="port friends join"/>', '<on event="start" run="port friends join"/>']),
        ("friends_button_invite", "INVITE", ['<on event="a" run="port friends invite"/>', '<on event="start" run="port friends invite"/>']),
        ("friends_button_back", "BACK", ['<on event="a" run="mouse emit back event"/>', '<on event="start" run="mouse emit back event"/>']),
    ])
    return lines


# ADD FRIEND's places (units): the label of this machine's code at the first row's place, the code 24 below its top, the field 24 below that, the answer 16 below the field
SOCIAL_ADD_LABEL_TOP, SOCIAL_ADD_CODE_TOP, SOCIAL_ADD_FIELD_TOP, SOCIAL_ADD_ANSWER_TOP = 76, 100, 148, 192


def _social_add() -> list:
    """ADD FRIEND: this machine's code, large, to read out; the code of the friend, typed (on the GamePad's keyboard); the check in words; SEND REQUEST and CANCEL"""
    base = f"{SOCIAL}/add"
    lines = _social_screen(f"{base}/friend_add_screen",
                           ['<on event="created" run="port friend add init"/>', '<on event="b" run="port friend add back"/>',
                            '<on event="back" run="port friend add back"/>'],
                           [f'<child widget="{base}/add_list"/>', f'<child widget="{base}/add_title"/>', f'<child widget="{base}/add_own_label"/>',
                            f'<child widget="{base}/add_own_code"/>', f'<child widget="{base}/add_answer"/>'])
    lines += _social_band(base)
    lines += _social_title(base, "add_title", "ADD FRIEND", SOCIAL_ROW_WIDTH)
    lines += _social_text(f"{base}/add_own_label", SOCIAL_ROWS_LEFT, SOCIAL_ADD_LABEL_TOP, SOCIAL_ROW_WIDTH, 24, color="#FFFFFFFF",
                          text="YOUR CODE (READ IT OUT TO YOUR FRIEND):", text_y=0)
    lines += _social_text(f"{base}/add_own_code", SOCIAL_ROWS_LEFT, SOCIAL_ADD_CODE_TOP, SOCIAL_ROW_WIDTH, 24, color="#FFFFFFFF", text_y=0)
    lines += _social_detail(base, "add_answer", SOCIAL_ADD_ANSWER_TOP)
    lines += _widget(f"{base}/add_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                          ("flags", "pass_unhandled_to_focused_child up_down_tabs_children")],
                     ['<data input="port friend add update"/>', f'<child widget="{base}/op_code" x="{SOCIAL_ROWS_LEFT}" y="{SOCIAL_ADD_FIELD_TOP}"/>',
                      f'<child widget="{base}/add_button_bar" y="414"/>'])
    # (the code being typed is white: it is what the player reads while keying it in)
    lines += _value_row(base, "code", 0, "port friend add edit", "#FFFFFFFF")
    lines += _social_buttons(base, f"{base}/add_button_bar", [
        ("add_button_send", "SEND REQUEST", ['<on event="a" run="port friend add send"/>', '<on event="start" run="port friend add send"/>']),
        ("add_button_cancel", "CANCEL", ['<on event="a" run="port friend add back"/>', '<on event="start" run="port friend add back"/>']),
    ])
    lines += _strings(f"{base}/labels", ["FRIEND'S CODE:"])
    return lines


def social_files() -> dict:
    """the files of the social screens, by their names in ce/"""
    head = ['<?xml version="1.0" encoding="UTF-8"?>', "<!-- The port's social screens (tools/port_settings.py, port/linux/game/screens.h) -->", "<menus>"]
    button = _widget(f"{SOCIAL}/menu_friends_button", [("type", "text"), ("width", 128), ("height", 24), ("bitmap", "bitmaps/text_button_background"),
                                                       ("text", "FRIENDS"), ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)],
                     [f'<on event="a" open="{SOCIAL}/friends_screen"/>', f'<on event="start" open="{SOCIAL}/friends_screen"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    return {f"{SOCIAL}/players".replace("/", ".") + ".xml": head + _social_players() + _social_band(SOCIAL) + button + ["</menus>", ""],
            f"{SOCIAL}/actions".replace("/", ".") + ".xml": head + _social_actions() + ["</menus>", ""],
            f"{SOCIAL}/friends".replace("/", ".") + ".xml": head + _social_friends() + _social_add() + ["</menus>", ""]}


# the Multiplayer screen's bar of buttons (the PC version's: BACK at the right), with a FRIENDS button before it that menu_screens.inl shows while screens.enable is on
# (the file is written from the PC version's tags by tools/ce_menus.py, which then calls this)
MULTIPLAYER_BAR_OLD = f'''<child widget="common_button_back" x="510" y="1"/>'''
MULTIPLAYER_FOLDER = "main_menu/multiplayer_type_select"


def multiplayer_bar_patched(text: str) -> str:
    """the Multiplayer screen's XML with the FRIENDS button in its bar of buttons (once)"""
    marker = f"{SOCIAL}/menu_friends_button"
    if marker in text:
        return text
    bar = f'<widget name="{MT}/multiplayer_type_button_bar"'
    start = text.index(bar)
    end = text.index("</widget>", start)
    block = text[start:end]
    if MULTIPLAYER_BAR_OLD not in block:
        raise ValueError("the Multiplayer screen's bar has changed")
    block = block.replace(MULTIPLAYER_BAR_OLD, f'<data input="port screens menu update"/>\n\t\t<child widget="{marker}" x="380" y="1"/>\n\t\t' + MULTIPLAYER_BAR_OLD)
    return text[:start] + block + text[end:]

SCREENS["video_settings/graphics"]["rows"].append(("STABLE VISIBILITY:", "display.wiiu_stable_visibility", ON_OFF,
    "Keep nearby obstacles and distant detail.\nCan cost frame rate in demanding scenes.", "wiiu"))

SCREENS["video_settings/graphics"]["rows"].append(("GUNNER FEEDBACK:", "display.wiiu_gunner_feedback", ON_OFF,
    "Preserve limited local firing effects and show\na firing cue when the turret actually shoots.", "wiiu"))

SCREENS["video_settings/graphics"]["rows"].append(("RETICLE STABILITY:", "display.wiiu_reticle_stabilization", ON_OFF,
    "Smooth tiny third-person and gunner jitters.\nLarge aim moves remain immediate.", "wiiu"))
