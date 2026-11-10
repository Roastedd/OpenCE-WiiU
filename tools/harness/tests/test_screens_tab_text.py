"""The Friends screen's tabs and buttons show words on the widget tree the game builds from the screen's XML. The real console showed four tabs with no text in them (the first version
of menu_screens.inl asked the tab itself for a widget of its own name, found none, and set nothing); the host's pictures were made from the model and could not see it. This builds the
real tree (every widget of the screen's list, as the XML places it), runs the code that names the tabs, and fails for a text widget with a bitmap that has no words."""
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import ROOT, build, function, mutated, read, run

SCREEN = "port/assets/menus/ce/main_menu.multiplayer_type_select.screens.friends.xml"
LIST = "main_menu/multiplayer_type_select/screens/friends_list"


def tree():
    """the XML's widgets from the list down, one instance for each place a widget is a child, as C initialisers"""
    widgets = {element.get("name"): element for element in ElementTree.parse(ROOT / SCREEN).getroot() if element.tag == "widget"}
    instances = []

    def make(name):
        element = widgets[name]
        index = len(instances)
        instances.append(None)
        children = [make(child.get("widget")) for child in element.findall("child")]
        instances[index] = (name.rsplit("/", 1)[-1], element, children)
        return index

    make(LIST)
    lines = ["static struct widget_instance tree[%d];" % len(instances), "static void tree_make(void) __attribute__((constructor));", "static void tree_make(void)", "{"]
    for index, (short, element, children) in enumerate(instances):
        lines.append('\ttree[%d].name = "%s"; tree[%d].is_text = %d; tree[%d].has_bitmap = %d; tree[%d].has_static_text = %d;' % (
            index, short, index, element.get("type") == "text", index, bool(element.get("bitmap")), index, bool(element.get("text") or element.get("string_list"))))
        if children:
            lines.append("\ttree[%d].child = &tree[%d];" % (index, children[0]))
        for before, after in zip(children, children[1:]):
            lines.append("\ttree[%d].next = &tree[%d];" % (before, after))
    lines.append("}")
    return "\n".join(lines) + "\n"


def generated(fault=None):
    menus = read("port/linux/game/menu_functions.c")
    inl = read("port/linux/game/menu_screens.inl")
    code = "\n".join([function(menus, "descendant"), function(menus, "named"), function(menus, "text_set_length"), function(menus, "text_to_wide"), function(inl, "scr_text"),
                      function(inl, "scr_friends_tab_labels"), function(inl, "scr_friends_tab_log")])
    if fault:
        code = mutated(code, *fault)
    return (("halo_social.h", read("port/linux/include/halo_social.h")), ("screens.h", read("port/linux/game/screens.h")), ("screens_metrics.inc", read("port/linux/game/screens_metrics.inc")),
            ("text_safe.h", read("port/linux/game/text_safe.h")), ("text_safe_code.inc", read("port/linux/game/text_safe.c").replace('#include "text_safe.h"', "")),
            ("screens_code.inc", read("port/linux/game/screens.c").replace('#include "screens.h"', "")), ("screens_mock_code.inc", read("port/linux/game/screens_mock.c").replace('#include "screens.h"', "").replace('#include "text_safe.h"', "")),
            ("menu_text_code.inc", code), ("tree.inc", tree()))


def test_tabs_start_without_words():
    status, output = run(build("screens_tab_text", generated()), "before")
    assert status == 0, output


def test_tabs_get_their_words():
    status, output = run(build("screens_tab_text", generated()), "labels")
    assert status == 0, output


@pytest.mark.parametrize("fault", [
    # the first version: the tab asked for a widget of its own name inside itself
    ("scr_text(list, name, text);", "scr_text(named(list, name, 0), name, text);"),
    # a label for three of the four
    ("for (index = 0; index < SCREENS_TABS; index++)\n\t{\n\t\tsnprintf(name, sizeof(name), \"friends_tab_%d\", index);\n\t\tscreens_tab_label",
     "for (index = 0; index < SCREENS_TABS - 1; index++)\n\t{\n\t\tsnprintf(name, sizeof(name), \"friends_tab_%d\", index);\n\t\tscreens_tab_label"),
    # the words not put in
    ("scr_text(list, name, text);", "(void)text;"),
    # the log says what was meant, not what the widgets hold (it would read well with empty tabs)
    ("wide = tab ? tab->parameters.text_box.text : NULL;", "wide = L\"FRIENDS\"; (void)tab;"),
])
def test_negative_control(fault):
    status, output = run(build("screens_tab_text", generated(fault)), "labels")
    assert status != 0, "the fault was not caught"
