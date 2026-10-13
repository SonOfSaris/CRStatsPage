# make_mock.py - a page for a normal browser that shows the Archives tabs with the game's own stylesheet.
#   python make_mock.py <page.json from the DLL> [out folder, default tests/out/mock]
# It takes the menu's header (top tabs, Archives tab bar) out of the game's ui.html, puts the Archives slot under
# it and runs ui/statspage_archive.js against stand-ins for the game: three tabs that a key moves by one and
# wraps. Keys in the browser: Q / E = previous / next tab, arrow up / down = move in the list, M = mouse mode on / off,
# O = close / open the Archives. The look is close to the game's, not equal: the game's fonts and images are missing.
import json, os, re, shutil, sys
here = os.path.dirname(os.path.abspath(__file__)).replace('\\', '/') + '/'
src = here + '../'
UI = src + '../extracted/ui_u1/uiresources__game__ui__ui.html'
CSS = src + '../extracted/u1/base-generic/data/uiresources/game/ui/ui.bundle.css'
out = (sys.argv[2] if len(sys.argv) > 2 else here + 'out/mock').replace('\\', '/') + '/'
os.makedirs(out, exist_ok=True)
u = open(UI, encoding='utf-8', errors='replace').read()

TAG = re.compile(r'<(/?)([a-zA-Z0-9-]+)((?:[^>"\']|"[^"]*"|\'[^\']*\')*)>')
VOID = {'br', 'img', 'input', 'meta', 'link', 'hr', 'path', 'circle', 'rect', 'line', 'polygon', 'use', 'stop', 'ellipse', 'polyline'}
def element(s, start):
    stack = []
    for m in TAG.finditer(s, start):
        tag = m.group(2).lower()
        if m.group(1):
            if tag not in stack: continue                 # a stray end tag
            while stack[-1] != tag: stack.pop()
            stack.pop()
            if not stack: return s[start:m.end()]
        elif not m.group(3).rstrip().endswith('/') and tag not in VOID: stack.append(tag)
    raise SystemExit('element at %d does not end' % start)
i = u.index('<div class="menu-header-layout">')
header = element(u, i)
shutil.copy(CSS, out + 'ui.bundle.css')
page = open(sys.argv[1], encoding='utf-8').read()
script = open(src + 'ui/statspage_archive.js', encoding='utf-8').read()

html = '''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>Archives tabs mock</title>
<link rel="stylesheet" href="ui.bundle.css">
<style>
 html, body { margin: 0; height: 100%; background: #171717; color: #e8e8e8; font-family: "Segoe UI", Arial, sans-serif; overflow: hidden; }
 #help { position: fixed; left: 1vh; bottom: 1vh; font-size: 1.6vh; opacity: .6; z-index: 99 }
 #gamepage { padding: 2vh; font-size: 2.2vh; opacity: .7 }
</style></head>
<body>
<div class="app"><div class="fullscreen-layout fullscreen-layout__container visible"><div class="fullscreen-layout__content fullscreen-layout__content--visible">
__HEADER__
<div class="tabs__pages"><div class="tabs__pages__page-wrapper visible"><div class="tabs__pages__page"><div class="narrative-object-menu"><div id="gamepage"></div></div></div></div></div>
</div></div></div>
<div id="help">Q / E tabs &middot; arrows list &middot; M mouse mode &middot; O open / close &middot; <span id="state"></span></div>
<script>
// ---- stand-ins for the game
var PAGE = __PAGE__;
var game = { tab: 0, open: true, pending: [], listeners: {} };
function model(name, value) { window[name] = { value: value }; }
model('ui_narrative_object_menu_is_open', true); model('ui_stacks_game_states_narrative_object_menu_active', true);
model('ui_stacks_game_states_narrative_object_inspect_current', false); model('ui_narrative_object_menu_sub_tabs_selected', 0);
model('ui_input_active_controller_menu_mouse', false); model('ui_menu_navigation_narrative_object_menu_id', 'narrative_object_menu');
model('ui_narrative_object_menu_cell_size', { x: 1, y: 1 });
['Collectibles', 'Enemies', 'Tutorials'].forEach(function (t, i) { window['ui_narrative_object_menu_sub_tabs_tabs_' + i + '_title'] = { translation: t }; model('ui_narrative_object_menu_sub_tabs_tabs_' + i + '_has_new', i === 1); });
var engine = {
    trigger: function (name, a) { if (name === 'narrative_object_sub_tab_click') game.pending.push(a); },
    on: function (name, f) { (game.listeners[name] = game.listeners[name] || []).push(f); },
    translate: function (k) { return k; }
};
var navigation = { setCell: function () { }, current: function () { return 0; }, setCurrent: function () { } };
window.XMLHttpRequest = function () { };
window.XMLHttpRequest.prototype.open = function (m, url) { this.url = url; };
window.XMLHttpRequest.prototype.send = function () { var x = this; setTimeout(function () { x.status = 200; x.responseText = JSON.stringify(PAGE); if (x.onload) x.onload(); }, 0); };
// what the game's binding would do: show what is bound visible, fill the labels
(function () {
    var all = document.querySelectorAll('*'), mains = document.querySelectorAll('.menu-header-layout__main'), k;
    for (k = 0; k < all.length; k++) if (all[k].hasAttribute('data-bind-rmd-visible') || all[k].hasAttribute('data-bind-rmd-visible-layout')) all[k].classList.add('visible');
    for (k = 1; k < mains.length; k++) mains[k].style.display = 'none';
    var top = document.querySelectorAll('.menu-header-layout__main .tab-buttons')[0].querySelectorAll('.tab-buttons-button__label');
    ['MAP', 'LOADOUT', 'INVENTORY', 'MISSIONS', 'ARCHIVES', 'SYSTEM'].forEach(function (t, i) { if (top[i]) top[i].textContent = t; });
    var topButtons = document.querySelectorAll('.menu-header-layout__main .tab-buttons')[0].querySelectorAll('.tab-buttons-button');
    if (topButtons[4]) topButtons[4].classList.add('menu-button--selected');
})();
function frame() {
    if (game.pending.length) game.tab = game.pending.shift();
    ui_narrative_object_menu_is_open.value = game.open; ui_stacks_game_states_narrative_object_menu_active.value = game.open;
    ui_narrative_object_menu_sub_tabs_selected.value = game.open ? game.tab : -1;
    document.getElementById('gamepage').textContent = "the game's page: tab " + game.tab + ' (' + ['Collectibles', 'Enemies', 'Tutorials'][game.tab] + ')';
    var s = window.__StatsPageArchive ? window.__StatsPageArchive.state() : {};
    document.getElementById('state').textContent = 'ours ' + s.tab + ', row ' + s.sel + ', game ' + game.tab + ', ' + s.status + (ui_input_active_controller_menu_mouse.value ? ', mouse' : ', controller');
    requestAnimationFrame(frame);
}
function fire(name) { (game.listeners[name] || []).forEach(function (f) { f(); }); }
document.addEventListener('keydown', function (e) {
    var k = e.key.toLowerCase();
    if (k === 'e') { game.tab = (game.tab + 1) % 3; fire('OnNext'); }
    else if (k === 'q') { game.tab = (game.tab + 2) % 3; fire('OnPrev'); }
    else if (k === 'arrowdown') fire('OnNavigateDown');
    else if (k === 'arrowup') fire('OnNavigateUp');
    else if (k === 'm') ui_input_active_controller_menu_mouse.value = !ui_input_active_controller_menu_mouse.value;
    else if (k === 'o') game.open = !game.open;
});
requestAnimationFrame(frame);
</script>
<script>
__SCRIPT__
</script>
</body></html>
'''
html = html.replace('__HEADER__', header).replace('__PAGE__', json.dumps(dict(json.loads(page), arch=dict(on=1, hold=1, trace=0)))).replace('__SCRIPT__', script)
open(out + 'index.html', 'w', encoding='utf-8', newline='\n').write(html)
print('mock written: %sindex.html (%d bytes, header %d bytes)' % (out, len(html), len(header)))
