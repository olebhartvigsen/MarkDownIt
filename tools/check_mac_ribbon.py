#!/usr/bin/env python3
"""Check that the mac command strip mirrors the Windows ribbon markup.

The strip is the mac twin of the Windows ribbon: same tabs, same
groups, same commands in the same order, with the same labels, the same
toggle behavior and the same icons. The two live in different worlds
(src/ribbon.xml drives the Windows Ribbon Framework, ribbon_mac.mm
drives AppKit), so nothing but this check keeps them from drifting
apart.

Usage: python3 tools/check_mac_ribbon.py
"""

import os
import re
import sys
import xml.etree.ElementTree as ET

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RIBBON_XML = os.path.join(REPO, "src", "ribbon.xml")
RIBBON_MM = os.path.join(REPO, "src", "macos", "ribbon_mac.mm")
MAIN_MM = os.path.join(REPO, "src", "macos", "main_mac.mm")
CMAKE = os.path.join(REPO, "CMakeLists.txt")
ICONS = os.path.join(REPO, "src", "icons")

# The commands the mac build leaves out on purpose, with the reason.
OMITTED = {
    "cmdEditToggle": "the mac app has no view/edit split",
}

# The one command whose menu of the markup turns into a dropdown on the
# mac strip; its menu items follow as entries of their own.
DROP_DOWN = "cmdTableTools"


def tag_name(element):
    return element.tag.rsplit("}", 1)[-1]


def children(element, name):
    return [child for child in element if tag_name(child) == name]


def parse_markup():
    """The tabs, groups and commands of src/ribbon.xml."""
    root = ET.parse(RIBBON_XML).getroot()
    commands = {}
    for element in root.iter():
        if tag_name(element) != "Command" or not element.get("Name"):
            continue
        # The label sits in the elements, not in an attribute:
        # <Command.LabelTitle><String>Bold</String></Command.LabelTitle>.
        label = ""
        for child in element.iter():
            if tag_name(child) == "String":
                label = child.text or ""
                break
        commands[element.get("Name")] = label

    tabs = []
    for element in root.iter():
        if tag_name(element) != "Tab":
            continue
        groups = []
        for group in children(element, "Group"):
            entries = []
            for child in group:
                kind = tag_name(child)
                if kind in ("Button", "ToggleButton"):
                    entries.append((child.get("CommandName"), kind))
                elif kind == "DropDownButton":
                    entries.append((child.get("CommandName"), "Button"))
                    for menu_group in children(child, "MenuGroup"):
                        for item in menu_group:
                            item_kind = tag_name(item)
                            if item_kind in ("Button", "ToggleButton"):
                                entries.append((item.get("CommandName"),
                                                item_kind))
            groups.append((commands.get(group.get("CommandName"), ""), entries))
        tabs.append((commands.get(element.get("CommandName"), ""), groups))

    quick_access = []
    for element in root.iter():
        if tag_name(element).endswith("ApplicationDefaults"):
            for button in element.iter():
                if tag_name(button) in ("Button", "ToggleButton"):
                    quick_access.append(button.get("CommandName"))
    return commands, tabs, quick_access


def parse_strip():
    """The tabs, groups and commands the strip draws, from the table."""
    with open(RIBBON_MM, encoding="utf-8") as handle:
        code = handle.read()

    # The strings carry escapes (\"\u2318\" and friends), and the check
    # compares them as they are on disk, so they match the markup too.
    commands = {}
    for name, body in re.findall(
            r"const RibbonCommand (\w+)\[\] = \{(.*?)\n\};", code, re.S):
        rows = re.findall(
            r'\{"(cmd[^"]+)",\s+"((?:[^"\\]|\\.)*)",\s+"([A-Za-z0-9]+:)",\s+'
            r'"([a-z0-9-]+)",\s+(true|false),', body)
        commands[name] = [(row[0], row[1], row[2], row[3],
                           row[4] == "true") for row in rows]

    groups = {}
    for name, body in re.findall(
            r"const RibbonGroup (\w+)\[\] = \{(.*?)\n\};", code, re.S):
        groups[name] = re.findall(r'\{"([^"]+)", (\w+), (\d+)\}', body)

    tab_block = re.findall(r"const RibbonTab kTabs\[\] = \{(.*?)\n\};",
                           code, re.S)
    tab_rows = re.findall(r'\{"([^"]+)", (\w+), (\d+)\}',
                          tab_block[0]) if tab_block else []

    strip = []
    for tab_label, groups_name, _count in tab_rows:
        drawn_groups = []
        for group_label, array_name, _group_count in groups.get(groups_name, []):
            entries = list(commands.get(array_name, []))
            if DROP_DOWN in [entry[0] for entry in entries]:
                entries += commands.get("kTableToolsItems", [])
            drawn_groups.append((group_label, entries))
        strip.append((tab_label, drawn_groups))
    return commands, strip


def main() -> int:
    commands, tabs, quick_access = parse_markup()
    strip_commands, strip = parse_strip()

    problems = []

    if len(tabs) != len(strip):
        problems.append("the markup has %d tabs, the strip draws %d"
                        % (len(tabs), len(strip)))
    for markup_tab, strip_tab in zip(tabs, strip):
        markup_label, markup_groups = markup_tab
        strip_label, strip_groups = strip_tab
        if markup_label != strip_label:
            problems.append("tab %r reads %r on the strip"
                            % (markup_label, strip_label))
            continue
        if len(markup_groups) != len(strip_groups):
            problems.append("tab %r has %d groups in the markup, %d on the "
                            "strip" % (markup_label, len(markup_groups),
                                       len(strip_groups)))
        for markup_group, strip_group in zip(markup_groups, strip_groups):
            if markup_group[0] != strip_group[0]:
                problems.append("group %r reads %r on the strip"
                                % (markup_group[0], strip_group[0]))
                continue
            expected = [(command_id, kind)
                        for command_id, kind in markup_group[1]
                        if command_id not in OMITTED]
            drawn = strip_group[1]
            if [item[0] for item in expected] != [item[0] for item in drawn]:
                problems.append(
                    "group %r: the markup lists %s, the strip draws %s"
                    % (markup_group[0], [item[0] for item in expected],
                       [item[0] for item in drawn]))
                continue
            for (command_id, kind), entry in zip(expected, drawn):
                if command_id == DROP_DOWN:
                    continue
                if (kind == "ToggleButton") != entry[4]:
                    problems.append(
                        "%s is a %s in the markup and %s on the strip"
                        % (command_id, kind,
                           "a toggle" if entry[4] else "a plain button"))

    # The labels, command by command, over every array of the table.
    for array_name, entries in strip_commands.items():
        for entry in entries:
            expected = commands.get(entry[0])
            if expected is None:
                problems.append("%s is on the strip but not in the markup"
                                % entry[0])
            elif expected != entry[1]:
                problems.append("%s reads %r in the markup and %r on the "
                                "strip" % (entry[0], expected, entry[1]))

    # The Quick Access Toolbar.
    quick = strip_commands.get("kQuickAccess", [])
    if [entry[0] for entry in quick] != quick_access:
        problems.append("the quick access tools are %s in the markup and %s "
                        "on the strip" % (quick_access,
                                          [entry[0] for entry in quick]))

    # Every command is carried out: by the delegate, or by the strip
    # itself for the dropdown.
    with open(MAIN_MM, encoding="utf-8") as handle:
        window = handle.read()
    with open(RIBBON_MM, encoding="utf-8") as handle:
        ribbon = handle.read()
    for array_name, entries in strip_commands.items():
        for entry in entries:
            selector = entry[2][:-1]
            if selector == "showTableToolsMenu":
                if "- (void)showTableToolsMenu:(id)sender" not in ribbon:
                    problems.append("the strip does not answer "
                                    "showTableToolsMenu:")
                continue
            if not re.search(r"- \(void\)%s:\(id\)sender" % re.escape(selector),
                             window):
                problems.append("no -[MdAppDelegate %s:] carries %s out"
                                % (selector, entry[0]))

    # The icons the strip loads, file by file. The small buttons read
    # the 16px files, everything else the 32px ones.
    small = set()
    for array_name in ("kQuickAccess", "kTableToolsItems"):
        for entry in strip_commands.get(array_name, []):
            small.add(entry[3])
    for array_name, entries in strip_commands.items():
        for entry in entries:
            size = 16 if entry[3] in small else 32
            path = os.path.join(ICONS, "%s_%d.png" % (entry[3], size))
            if not os.path.exists(path):
                problems.append("the strip wants %s for %s, and it is not "
                                "there" % (os.path.basename(path), entry[0]))

    # The strip has to be built, wired and kept in step by the window.
    for needle, reason in (
            ("[MdRibbonView ribbonWithHost:self]",
             "the window never builds the strip"),
            ("SetSelectionChangedHandler",
             "the strip never follows the caret"),
            ("- (BOOL)ribbonCommandOn:", "the strip has no state to show"),
            ("- (BOOL)ribbonCommandAvailable:",
             "the strip cannot disable a command"),
            ("[container_ addArrangedSubview:ribbon_];",
             "the strip is built but never shown")):
        if needle not in window:
            problems.append(reason)

    with open(CMAKE, encoding="utf-8") as handle:
        cmake = handle.read()
    for needle in ("src/macos/ribbon_mac.mm", "Resources/icons"):
        if needle not in cmake:
            problems.append("CMakeLists.txt never mentions %r" % needle)

    count = sum(len(entries) for _, groups in strip for _, entries in groups)
    if problems:
        print("the mac ribbon check failed:")
        for problem in problems:
            print("  " + problem)
        return 1

    print("the mac strip mirrors the markup: %d tabs, %d groups, %d commands"
          % (len(strip), sum(len(groups) for _, groups in strip), count))
    return 0


if __name__ == "__main__":
    sys.exit(main())
