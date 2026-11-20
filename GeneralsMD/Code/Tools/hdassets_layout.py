#!/usr/bin/env python3
"""Generate the native HD Assets modal from existing game widgets; no external artwork.

Run with no arguments to regenerate, or selfcheck to verify the tracked layout, geometry,
callback registration and English text. Mirrors have three visible rows and native page buttons.
"""
from pathlib import Path
import sys
import re
import wndlayout
from wndlayout import clone
from optionsmenu_layout import drop_prop, GROUP_HEADING_COLOR

BASE = Path(__file__).resolve().parents[1]
TARGET = BASE / 'Data/Window/Menus/HDAssetsMenu.wnd'


def native_draw_data(color, border):
    """The native parser consumes MAX_DRAW_DATA=9 complete entries unconditionally."""
    first = 'IMAGE: NoImage, COLOR: %s, BORDERCOLOR: %s' % (color, border)
    empty = 'IMAGE: NoImage, COLOR: 255 255 255 0, BORDERCOLOR: 255 255 255 0'
    return ', '.join([first] + [empty] * 8)


def validate_draw_data(layout):
    """Refuse truncated blocks before the native strtok-based parser can dereference NULL."""
    entry = (r'IMAGE:\s*[^,\s]+,\s*COLOR:\s*(?:\d+\s+){3}\d+,'
             r'\s*BORDERCOLOR:\s*(?:\d+\s+){3}\d+')
    nine_entries = r'\s*,\s*'.join([entry] * 9)
    for node in layout.root.walk():
        for raw in node.props:
            match = re.match(r'\s*(\w*DRAWDATA)\s*=', raw)
            if not match:
                continue
            key = match.group(1)
            statement = node.prop(key).partition('=')[2].strip()
            assert statement.endswith(';'), (node.name, key, 'missing terminator')
            body = statement[:-1].strip()
            assert re.fullmatch(nine_entries, body), (node.name, key, 'expected nine complete native draw entries')


def build():
    options = wndlayout.load(str(BASE / 'Data/Window/Menus/OptionsMenuClassic.wnd'))
    result = wndlayout.Layout()
    result.header = ['FILE_VERSION = 2;', 'STARTLAYOUTBLOCK', '  LAYOUTINIT = HDAssetsMenuInit;',
                     '  LAYOUTUPDATE = HDAssetsMenuUpdate;', '  LAYOUTSHUTDOWN = HDAssetsMenuShutdown;', 'ENDLAYOUTBLOCK']
    root = clone(options.root, 'HDAssetsMenu.wnd:Root')
    root.children = []
    root.set_prop('SYSTEMCALLBACK', '"HDAssetsMenuSystem"')
    root.set_prop('INPUTCALLBACK', '"HDAssetsMenuInput"')
    root.set_prop('ENABLEDDRAWDATA', native_draw_data('0 0 0 0', '0 0 0 0'))
    result.root = root
    panel = clone(options.find('OptionsMenuParentOld'), 'HDAssetsMenu.wnd:Panel')
    panel.children = []
    panel.place(136, 146, 528, 330)
    panel.set_prop('STATUS', 'ENABLED+BORDER')
    panel.set_prop('INPUTCALLBACK', '"HDAssetsMenuInput"')
    panel.set_prop('ENABLEDDRAWDATA', native_draw_data('0 0 0 255', '47 100 200 255'))
    root.children.append(panel)

    def widget(parent, template, name, rect, key=None):
        w = clone(options.find(template), 'HDAssetsMenu.wnd:' + name)
        w.children = []
        w.place(*rect)
        w.set_prop('STATUS', 'ENABLED' if template in ('DetailLabel', 'PageGraphics') else 'ENABLED+IMAGE')
        w.set_prop('SYSTEMCALLBACK', '"PassMessagesToParentSystem"' if template == 'PageGraphics' else '"[None]"')
        w.set_prop('INPUTCALLBACK', '"[None]"')
        drop_prop(w, 'TOOLTIPTEXT')
        if key:
            w.put_prop('TEXT', '"GUI:HDAssets%s"' % key)
        elif template == 'DetailLabel':
            w.put_prop('TEXT', '""')
        parent.children.append(w)
        return w

    def text(parent, name, rect, key=None, heading=False):
        w = widget(parent, 'DetailLabel', name, rect, key)
        w.set_prop('FONT', 'NAME: "Arial", SIZE: %d, BOLD: 0' % (12 if heading else 10))
        w.set_prop('HEADERTEMPLATE', '"[NONE]"')
        w.put_prop('STATICTEXTDATA', 'CENTERED: 0')
        if heading:
            w.set_prop('TEXTCOLOR', GROUP_HEADING_COLOR)
        return w

    def button(parent, name, rect, key):
        w = widget(parent, 'ButtonDefaults', name, rect, key)
        # Keep native three-slice button art and give compact modal rows a legible native face.
        w.set_prop('FONT', 'NAME: "Arial", SIZE: 10, BOLD: 0')
        w.set_prop('HEADERTEMPLATE', '"[NONE]"')
        return w

    text(panel, 'Title', (152, 151, 496, 30), 'Manage', True)
    rule = clone(options.find('Line'), 'HDAssetsMenu.wnd:TitleRule')
    rule.children = []
    rule.place(136, 185, 528, 1)
    panel.children.append(rule)
    # The first-launch invitation occupies the open left side of the main menu.
    # Keep its geometry independent so the pack/source dialogs retain their tested layout.
    invitation_panel = clone(panel, 'HDAssetsMenu.wnd:InvitationPanel')
    invitation_panel.children = []
    invitation_panel.place(52, 200, 444, 190)
    root.children.append(invitation_panel)
    text(invitation_panel, 'InvitationTitle', (68, 207, 412, 24), 'Welcome', True)
    invitation_rule = clone(rule, 'HDAssetsMenu.wnd:InvitationRule')
    invitation_rule.place(52, 236, 444, 1)
    invitation_panel.children.append(invitation_rule)
    for page in ['Packs', 'Sources', 'Invitation', 'Removal', 'Details']:
        parent = invitation_panel if page == 'Invitation' else panel
        rect = (60, 240, 428, 142) if page == 'Invitation' else (144, 188, 512, 280)
        p = widget(parent, 'PageGraphics', page, rect)
        p.set_prop('STATUS', 'ENABLED')
        p.set_prop('ENABLEDDRAWDATA', native_draw_data('0 0 0 0', '0 0 0 0'))
        p.set_prop('STYLE', 'USER')
        if page == 'Packs':
            for i in range(4):
                y = 198 + i * 43
                text(p, 'PackName%d' % i, (158, y, 274, 20))
                text(p, 'PackState%d' % i, (158, y + 20, 348, 18))
                text(p, 'PackSize%d' % i, (438, y, 68, 20))
                button(p, 'PackAction%d' % i, (516, y, 126, 28), 'Download')
                button(p, 'PackRemoveOwned%d' % i, (516, y + 22, 126, 20), 'RemoveDownloaded')
            text(p, 'Progress', (158, 373, 484, 20))
            progress_layout = wndlayout.load(str(BASE / 'Data/Window/Menus/WOLWelcomeMenu.wnd'))
            source = next(w for w in progress_layout.root.walk() if 'PROGRESSBAR' in (w.prop('WINDOWTYPE') or ''))
            bar = clone(source, 'HDAssetsMenu.wnd:ProgressBar')
            bar.children = []
            bar.rect = (158, 394, 642, 402, 800, 600)
            bar.set_prop('STATUS', 'ENABLED+BORDER')
            p.children.append(bar)
            text(p, 'Note', (158, 406, 360, 25), 'RestartNote')
            button(p, 'Back', (158, 435, 70, 28), 'Back')
            button(p, 'SourcesButton', (236, 435, 158, 28), 'Sources')
            button(p, 'FullSet', (402, 435, 240, 28), 'FullSet')
            # The live transfer controls occupy the same footer as Full Set.
            button(p, 'Pause', (402, 435, 116, 28), 'Pause')
            button(p, 'CancelDownload', (526, 435, 116, 28), 'Cancel')
            button(p, 'ErrorDetails', (526, 405, 116, 26), 'Details')
        elif page == 'Sources':
            text(p, 'OfficialLabel', (158, 198, 145, 25), 'Official')
            text(p, 'OfficialUrl', (306, 198, 195, 25), 'OfficialHost')
            button(p, 'Default', (510, 198, 132, 26), 'Default')
            for i in range(3):
                y = 236 + 33 * i
                text(p, 'Source%d' % i, (158, y, 340, 26))
                button(p, 'EditSource%d' % i, (510, y, 64, 26), 'Edit')
                button(p, 'RemoveSource%d' % i, (578, y, 64, 26), 'Remove')
            text(p, 'MirrorLabel', (158, 339, 484, 21), 'MirrorUrl')
            entry = widget(p, 'TextEntryHTTPProxy', 'MirrorUrl', (158, 365, 340, 26))
            entry.set_prop('TEXTENTRYDATA', 'MAXLEN: 2048, SECRETTEXT: 0, NUMERICALONLY: 0, ALPHANUMERICALONLY: 0, ASCIIONLY: 1')
            button(p, 'SaveSource', (510, 365, 132, 26), 'AddMirror')
            text(p, 'SourceNote', (158, 399, 484, 28), 'Fallback')
            button(p, 'PreviousSources', (158, 435, 64, 28), 'Previous')
            button(p, 'NextSources', (230, 435, 64, 28), 'Next')
            button(p, 'CancelEdit', (366, 435, 132, 28), 'Cancel')
            button(p, 'SourcesBack', (510, 435, 132, 28), 'Back')
        elif page == 'Invitation':
            text(p, 'InvitationText', (68, 248, 412, 90), 'InvitationText')
            button(p, 'InviteSkip', (68, 350, 80, 28), 'Skip')
            button(p, 'InviteChoose', (156, 350, 130, 28), 'ChoosePacks')
            button(p, 'InviteTextures', (294, 350, 186, 28), 'InviteTextures')
        elif page == 'Removal':
            text(p, 'RemovalText', (158, 205, 484, 170))
            button(p, 'RemovalCancel', (366, 435, 132, 28), 'Cancel')
            button(p, 'RemovalConfirm', (510, 435, 132, 28), 'Remove')
        else:
            text(p, 'DetailsText', (158, 205, 484, 210))
            button(p, 'DetailsBack', (510, 435, 132, 28), 'Back')
    return result


def selfcheck():
    expected = build().text().replace('\r\n', '\n')
    if TARGET.read_bytes() != expected.encode('utf-8'):
        raise SystemExit('HD Assets layout differs from generator')
    layout = wndlayout.load(str(TARGET))
    validate_draw_data(layout)
    keys = (BASE / 'Data/Patch.str').read_text()
    callback_source = (BASE / 'GameEngine/Source/Common/System/FunctionLexicon.cpp').read_text()
    names = set()
    for node in layout.root.walk():
        assert node.name not in names, node.name
        names.add(node.name)
        for key in re.findall(r'"(GUI:HDAssets\w+)"', node.prop('TEXT') or ''):
            assert re.search(r'^' + key + r'\s*$', keys, re.M), key
        for callback in re.findall(r'HDAssetsMenu\w+', ' '.join(node.props)):
            assert '"' + callback + '"' in callback_source, callback
        if node is not layout.root:
            l, t, r, b = node.rect[:4]
            assert 52 <= l < r <= 664 and 146 <= t < b <= 476, (node.name, node.rect)
    # Compact invitation leaves the native right-hand menu column visible.
    assert layout.find('InvitationPanel').rect[:4] == (52, 200, 496, 390)
    actions = [layout.find(n).rect for n in ('InviteSkip', 'InviteChoose', 'InviteTextures')]
    assert all(rect[1] == 350 and rect[3] == 378 for rect in actions)
    assert all(actions[i][2] + 8 == actions[i + 1][0] for i in range(2))
    # Three mirror rows, identical edit/remove hit targets in a single action column.
    for i in range(3):
        edit = layout.find('EditSource%d' % i).rect
        remove = layout.find('RemoveSource%d' % i).rect
        assert edit[2] - edit[0] == remove[2] - remove[0] == 64
        assert edit[3] - edit[1] == remove[3] - remove[1] == 26
        assert edit[0] == layout.find('Default').rect[0]
        assert remove[2] == layout.find('Default').rect[2]
    print('HD Assets native layout, text, callbacks and action-column geometry verified')


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'selfcheck':
        selfcheck()
    else:
        generated = build()
        validate_draw_data(generated)
        TARGET.write_bytes(generated.text().replace('\r\n', '\n').encode('utf-8'))
