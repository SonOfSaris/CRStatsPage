// archive_test.js - the Archives script (ui/statspage_archive.js) against a stand-in page and a stand-in game.
//   node archive_test.js <ui/statspage_archive.js> <page.json from the DLL>
// The stand-in game does what the real one does with its three tabs: a tab key moves by one and wraps, an event
// 'narrative_object_sub_tab_click' is applied at the start of the next frame, and the data models follow.
const fs = require('fs');
const source = fs.readFileSync(process.argv[2], 'utf8');
const pageText = fs.readFileSync(process.argv[3], 'utf8');
const page = JSON.parse(pageText);
let fails = 0;
function check(ok, what) { console.log((ok ? 'PASS' : 'FAIL') + '  ' + what); if (!ok) fails++; }

function makeWorld(opts) {
    opts = opts || {};
    // ---- a small document
    function Element(tag) {
        this.tagName = tag; this.className = ''; this.style = {}; this.childNodes = []; this.parentNode = null;
        this.textContent = ''; this.innerHTML = ''; this.listeners = {}; this.id = ''; this.scrollTop = 0;
    }
    Element.prototype.appendChild = function (c) { if (c.parentNode) c.parentNode.removeChild(c); this.childNodes.push(c); c.parentNode = this; return c; };
    let inserts = 0;
    Element.prototype.insertBefore = function (c, ref) {
        if (opts.breakInsertAt && ++inserts === opts.breakInsertAt) throw new Error('insertBefore broke');
        if (c.parentNode) c.parentNode.removeChild(c);
        const i = this.childNodes.indexOf(ref); if (i < 0) throw new Error('insertBefore: no such child');
        this.childNodes.splice(i, 0, c); c.parentNode = this; return c;
    };
    Element.prototype.removeChild = function (c) { const i = this.childNodes.indexOf(c); if (i < 0) throw new Error('removeChild: no such child'); this.childNodes.splice(i, 1); c.parentNode = null; return c; };
    Element.prototype.addEventListener = function (n, f) { (this.listeners[n] = this.listeners[n] || []).push(f); };
    Element.prototype.fire = function (n, e) { (this.listeners[n] || []).forEach(f => f(e || {})); };
    Element.prototype.has = function (c) { return (' ' + this.className + ' ').indexOf(' ' + c + ' ') >= 0; };
    Element.prototype.replaceChild = function (c, old) { const i = this.childNodes.indexOf(old); if (i < 0) throw new Error('replaceChild: no such child'); this.childNodes[i] = c; c.parentNode = this; old.parentNode = null; return old; };
    Object.defineProperty(Element.prototype, 'children', { get: function () { return this.childNodes.slice(); } });
    // the real engine has no use for these here; the script must do without them
    Object.defineProperty(Element.prototype, 'nodeType', { get: function () { throw new Error('nodeType is not to be used'); } });
    function mk(cls, id) { const e = new Element('div'); e.className = cls || ''; if (id) e.id = id; return e; }
    const body = mk('app');
    function walk(e, f) { if (f(e)) return e; for (const c of e.childNodes) { const r = walk(c, f); if (r) return r; } return null; }
    function all(e, f, out) { if (f(e)) out.push(e); for (const c of e.childNodes) all(c, f, out); return out; }
    const simple = sel => { if (!/^\.[\w-]+$/.test(sel)) throw new Error('selector not supported here: ' + sel); return sel.slice(1); };
    const document = {
        createElement: t => new Element(t),
        querySelector: sel => walk(body, e => e.has(simple(sel))),
        querySelectorAll: sel => all(body, e => e.has(simple(sel)), []),
        getElementById: id => walk(body, e => e.id === id),
    };
    // Another screen that uses the Archives page's class, earlier in the page (the Deluxe Edition menu).
    const decoy = mk('narrative-object-menu deluxe');
    { const d1 = mk('fullscreen-layout'), d2 = mk('fullscreen-layout__content'); d2.appendChild(decoy); d1.appendChild(d2); body.appendChild(d1); }
    const frame = mk('fullscreen-layout__content'), header = mk('menu-header-layout'), headerMain = mk('menu-header-layout__main');
    { const f1 = mk('fullscreen-layout fullscreen-layout__container'); f1.appendChild(frame); body.appendChild(f1); }
    const holder = mk('narrative-object-menu-sub-tabs'), bar = mk('tab-buttons');
    const leftKey = mk('action-key tab-buttons__element tab-buttons__key tab-buttons__key--left'), rightKey = mk('action-key tab-buttons__element tab-buttons__key tab-buttons__key--right');
    const originals = [0, 1, 2].map(() => mk('menu-button tab-buttons-button tab-buttons__element menu-button--small'));
    bar.appendChild(leftKey); if (!opts.noButtons) originals.forEach(o => bar.appendChild(o)); bar.appendChild(rightKey);
    holder.appendChild(bar); headerMain.appendChild(holder); header.appendChild(headerMain); frame.appendChild(header);
    // The game's Archives page: the page file's top element, put in the placeholder's place when the UI starts.
    const pageParent = mk('tabs__pages__page'), placeholder = mk('', 'NarrativeObjectMenuComponent'), slot = mk('narrative-object-menu');
    { const pages = mk('tabs__pages'), wrapper = mk('tabs__pages__page-wrapper'); wrapper.appendChild(pageParent); pages.appendChild(wrapper); frame.appendChild(pages); }
    pageParent.appendChild(opts.late ? placeholder : slot);

    // ---- the stand-in game
    const window = {};
    const model = (name, value) => { window[name] = { value }; };
    model('ui_narrative_object_menu_is_open', false);
    model('ui_stacks_game_states_narrative_object_menu_active', false);
    model('ui_stacks_game_states_narrative_object_inspect_current', false);
    model('ui_narrative_object_menu_sub_tabs_selected', -1);
    model('ui_input_active_controller_menu_mouse', false);
    model('ui_menu_navigation_narrative_object_menu_id', 'narrative_object_menu');
    model('ui_narrative_object_menu_cell_size', { x: 1, y: 1 });
    for (let i = 0; i < 150; i++) { model('ui_narrative_object_menu_categories_' + i + '_id', i < 5 ? i : -1); model('ui_narrative_object_menu_categories_' + i + '_unused', i >= 5); }
    ['Collectibles', 'Enemies', 'Tutorials'].forEach((t, i) => { window['ui_narrative_object_menu_sub_tabs_tabs_' + i + '_title'] = { translation: '', locID: '' }; model('ui_narrative_object_menu_sub_tabs_tabs_' + i + '_has_new', i === 1); });
    const game = { tab: 0, open: false, pending: [], events: [], listeners: {}, cells: [], navCurrent: 3, clock: 1000000, requests: [], raf: null, setCellThrows: false, changes: 0 };      // changes: how often its tab changed = how often its tab sound played
    const engine = {
        trigger: function (name, a) { game.events.push([name, a]); if (name === 'narrative_object_sub_tab_click') game.pending.push(a); },
        on: (name, f) => { (game.listeners[name] = game.listeners[name] || []).push(f); },
        translate: k => ({ MENU_NARRATIVE_OBJECT_COLLECTIBLES: 'Sammelobjekte', ZONE_DOWNTOWN: 'Innenstadt' }[k] || k),
    };
    const navigation = {
        setCell: a => { if (game.setCellThrows) throw new Error('setCell broke'); game.cells.push(a); },
        current: () => game.navCurrent, setCurrent: (c, id) => { game.navCurrent = id; game.setCurrentCalls = (game.setCurrentCalls || 0) + 1; },
    };
    function XMLHttpRequest() { }
    XMLHttpRequest.prototype.open = function (m, url) { this.url = url; };
    XMLHttpRequest.prototype.send = function () {
        game.requests.push(this.url);
        this.status = 200; this.responseText = JSON.stringify(Object.assign({}, page, { arch: Object.assign({ on: 1, hold: 1, trace: 1 }, opts.arch || {}) }));
        if (this.onload) this.onload();
    };
    const FakeDate = { now: () => game.clock };
    const run = new Function('window', 'document', 'engine', 'navigation', 'XMLHttpRequest', 'requestAnimationFrame', 'setTimeout', 'Date', source);
    run(window, document, engine, navigation, XMLHttpRequest, f => { game.raf = f; }, f => { game.raf = f; }, FakeDate);
    const api = window.__StatsPageArchive;

    // one frame of the game: clicks sent last frame are applied, the models follow, then the page's frame runs
    game.frame = function (n) {
        for (let i = 0; i < (n || 1); i++) {
            game.clock += 16;
            if (game.pending.length) { const v = game.pending.shift(); if (v !== game.tab) game.changes++; game.tab = v; }
            window.ui_narrative_object_menu_is_open.value = game.open;
            window.ui_stacks_game_states_narrative_object_menu_active.value = game.open;
            window.ui_narrative_object_menu_sub_tabs_selected.value = game.open ? game.tab : -1;
            const f = game.raf; game.raf = null; if (f) f();
        }
    };
    game.press = function (name) {          // a tab key: the game moves by one and tells the page's scripts
        if (name === 'OnNext') game.tab = (game.tab + 1) % 3; else if (name === 'OnPrev') game.tab = (game.tab + 2) % 3;
        game.changes++;
        (game.listeners[name] || []).forEach(f => f());
    };
    game.openOn = function (t) { game.open = true; game.tab = t; game.frame(30); };
    const buttons = () => bar.childNodes.filter(c => c.has('tab-buttons-button'));
    const lit = () => buttons().findIndex(b => b.has('menu-button--selected'));
    const myPage = () => pageParent.childNodes.find(c => c.has('statspage-archive'));
    const shownPage = () => { const p = myPage(); return !!p && p.style.display !== 'none'; };
    const shownGame = () => slot.style.display !== 'none';
    const rows = () => { const p = myPage(); const list = walk(p, e => e.has('category-list__content')); return list.childNodes.filter(r => r.style.display !== 'none'); };
    const rowText = r => walk(r, e => e.has('narrative-object-category-item__name')).textContent;
    const rowCounter = r => { const c = walk(r, e => e.has('narrative-object-category-item__counter')); return (c.innerHTML || c.textContent).replace(/<[^>]*>/g, ''); };
    const details = () => walk(myPage(), e => e.has('statspage-archive__details')).innerHTML;
    const aboutText = () => walk(myPage(), e => e.has('menu-context-text--gameplay')).textContent;      // the right panel's description
    const barShown = () => walk(myPage(), e => e.has('menu-context__heading')).parentNode.childNodes[1].style.display !== 'none';      // the right panel's bar
    game.loadPage = () => pageParent.replaceChild(slot, placeholder);
    return { window, document, engine, navigation, game, api, bar, slot, decoy, originals, buttons, lit, myPage, shownPage, shownGame, rows, rowText, rowCounter, details, aboutText, barShown, walk, rightKey, leftKey };
}

// ---------------------------------------------------------------- the tab bar and the pages
let w = makeWorld();
w.game.frame(3);
check(w.api.state().ready && w.buttons().length === 7 && w.originals.every(o => o.parentNode === null), 'start: seven buttons in the bar, the game\'s three taken out');
check(w.bar.childNodes[0] === w.leftKey && w.bar.childNodes[w.bar.childNodes.length - 1] === w.rightKey, 'the bar keeps its two key symbols at the ends');
const labels = () => w.buttons().map(b => w.walk(b, e => e.has('menu-button__label')).textContent);
check(labels().join(',') === 'Sammelobjekte,Enemies,Tutorials,Completion,Story,Zones,Dylan', 'labels: the game\'s three in its language, then ours (' + labels().join(', ') + ')');
check(w.myPage() && !w.shownPage() && w.shownGame() && w.myPage().parentNode === w.slot.parentNode, 'our page stands next to the game\'s Archives page, hidden');
check(w.decoy.style.display === undefined && w.decoy.parentNode.childNodes.length === 1, 'the other screen with the same class is left alone');
w.window.ui_narrative_object_menu_sub_tabs_tabs_1_title.translation = 'Gegner';
w.game.openOn(0);
check(w.lit() === 0 && w.shownGame() && !w.shownPage() && labels()[1] === 'Gegner', 'Archives open on Collectibles: its button is lit, the game\'s page shows, labels follow the game');
check(w.buttons()[1].childNodes[0].has('new-marker--visible') && !w.buttons()[0].childNodes[0].has('new-marker--visible'), 'the "new" marker of a game tab is carried over');
check(w.game.requests.some(u => /w=arch/.test(u) && /s=ready/.test(u)), 'the DLL hears that the tabs are in place');

// a click on Story
w.buttons()[4].fire('click');
w.game.frame(1);
check(w.api.state().tab === 1 && w.lit() === 4 && w.shownPage() && !w.shownGame(), 'click on Story: its button is lit, our page shows, the game\'s is hidden');
check(w.game.events.filter(e => e[0] === 'narrative_object_sub_tab_click').map(e => e[1]).join(',') === '1', 'and the game gets one tab change underneath, so the click sounds like one on its own tabs');
w.game.frame(10);
check(w.game.tab === 1 && w.api.state().tab === 1 && w.api.state().expect === 0 && w.game.changes === 1, 'the game moved once and is left there, our tab stays');
check(w.rows().map(w.rowText).join(',') === 'Main Story Missions,Side Stories,Mapping the New New York,Dylan\'s Sketchbook,Unsupervised Science,Freeze Frame,The Last Taxi,Every Dog Has Her Day,The Complete Set,Pope\'s Research' &&
      w.rowCounter(w.rows()[0]) === '26/26' && w.rowCounter(w.rows()[1]) === '12/12' && w.rowCounter(w.rows()[2]) === '7/7' && w.rowCounter(w.rows()[3]) === '9/11' && w.rowCounter(w.rows()[5]) === '15/15', 'Story lists its two rows and the eight side stories in parts, with counts (' + w.rows().map(r => w.rowText(r) + ' ' + w.rowCounter(r)).join('; ') + ')');
check(w.game.cells.length >= 5 && w.game.cells.slice(-5).every(c => c.m_disabled === true && c.m_contextId === 'narrative_object_menu' && c.__Type === 'NavigationSetCellArguments'), 'the game\'s five live category cells are switched off');

// events of the hidden page do not reach the game
const before = w.game.events.length;
w.engine.trigger('narrative_object_category_action', 3); w.engine.trigger('narrative_object_select', 1);
check(w.game.events.length === before, 'a select on the hidden page does not reach the game');
w.engine.trigger('some_other_event', 1);
check(w.game.events.length === before + 1, 'other events pass');

// tab keys through our tabs
function press(name, frames) { w.game.press(name); w.game.frame(frames || 12); }
press('OnNext'); check(w.api.state().tab === 2 && w.lit() === 5 && w.game.tab === 2 && w.game.changes === 2, 'next: Zones; the game moved by one and is left there (its tab sound once)');
check(w.rows().map(w.rowText).join(',') === 'Zone Levels,World Encounters,Hiss Vending Machines,World Feats,SH\u00dcM Scores,FBC Tasks', 'Zones lists its six rows in the order a player comes to them');
press('OnNext');
check(w.api.state().tab === 3 && w.lit() === 6 && w.rows().length === 6 && w.rowText(w.rows()[5]) === 'Promotional Outfits' && w.rowCounter(w.rows()[5]) === String(page.promo),
      'next: Dylan with five rows and the count of promotion pieces (' + w.rowCounter(w.rows()[5]) + ')');
check(/Still Missing/.test(w.details()) && />\?\?\?<\/div><div [^>]*>Blueprint sold by a vendor</.test(w.details()) && !/Fingerboard/.test(w.details()), 'Artifacts: what is missing reads ???, with where it comes from');
check(!/menu-context-stats|<span|<p /.test(w.details()) && (w.details().match(/display:flex;flex-direction:row/g) || []).length >= 9, 'the details are rows that lay themselves out: no inline text, no added colon');
(w.game.listeners.OnNavigateDown || []).forEach(f => f()); (w.game.listeners.OnNavigateDown || []).forEach(f => f()); (w.game.listeners.OnNavigateDown || []).forEach(f => f()); w.game.frame(1);
check(w.rowText(w.rows()[3]) === 'Talents' && />Unlocked<.*>45<.*>Remaining<.*>27</.test(w.details()) && w.barShown(), 'a row without a list (Talents) says how much is unlocked and how much remains');
(w.game.listeners.OnNavigateDown || []).forEach(f => f()); (w.game.listeners.OnNavigateDown || []).forEach(f => f()); w.game.frame(1);
check(w.api.state().sel === 5 && !w.barShown() && />Owned</.test(w.details()) && (w.details().match(/min-height:2\.7777777778vh/g) || []).length === page.promo && !/ \/ /.test(w.details()),
      'the promotion pieces: no bar, no total, the ' + page.promo + ' owned ones by name');
(w.game.listeners.OnNavigateUp || []).forEach(f => f()); (w.game.listeners.OnNavigateUp || []).forEach(f => f()); w.game.frame(1);
check(w.api.state().sel === 3 && w.barShown(), 'back on a counted row the bar is there again');
(w.game.listeners.OnNavigateUp || []).forEach(f => f()); (w.game.listeners.OnNavigateUp || []).forEach(f => f()); (w.game.listeners.OnNavigateUp || []).forEach(f => f()); w.game.frame(1);
let before1 = w.game.changes;
press('OnNext');
check(w.api.state().tab === -1 && w.game.tab === 0 && w.lit() === 0 && w.shownGame() && !w.shownPage(), 'next from our last tab: the game\'s Collectibles');
check(w.game.changes === before1 + 2, 'here the key took the game to Enemies, so it had to be moved once more (the one step with two changes)');
check(w.game.cells.slice(-5).every(c => c.m_disabled === false), 'and the game\'s cells are switched on again');
press('OnNext'); check(w.api.state().tab === -1 && w.game.tab === 1 && w.lit() === 1, 'next among the game\'s tabs is left alone');
press('OnPrev'); press('OnPrev');
check(w.api.state().tab === 3 && w.game.tab === 2 && w.lit() === 6 && w.shownPage(), 'previous from Collectibles: our last tab');
press('OnPrev'); press('OnPrev'); press('OnPrev');
check(w.api.state().tab === 0 && w.lit() === 3, 'previous three times: Completion');
check(w.rows().map(w.rowText).join(',') === 'Completion,Story,Zones,Dylan', 'Completion lists the whole and the three parts: ' + w.rows().map(r => w.rowText(r) + ' ' + w.rowCounter(r)).join('; '));
check(/>Story<.*>Zones<.*>Dylan</.test(w.details()) && /Played \d+ h/.test(w.details()) && !/Promotional|Collectibles|nobtainable|K9|earlier playthrough/.test(w.details()),
      'its details: the three parts and the save\'s lines; no row for collectibles, enemy research or the last 1%');
check(w.aboutText() === 'Overall game completion, including collectibles and enemy encounters.', 'its description: ' + w.aboutText());
check(w.rowCounter(w.rows()[0]) === page.shown.toFixed(1) + '%' && !/[Bb]onus/.test(w.details()) && !/[Bb]onus/.test(w.myPage().childNodes.length ? JSON.stringify(w.rows().map(w.rowText)) : ''),
      'the bar is one number (' + w.rowCounter(w.rows()[0]) + '), and nothing on the page says "bonus"');
const about = [];
for (let i = 0; i < 3; i++) { (w.game.listeners.OnNavigateDown || []).forEach(f => f()); w.game.frame(1); about.push(w.aboutText()); }
check(about.join(' | ') === 'Main story missions and side story missions. | Missions and encounters around Manhattan. | Dylan\'s stuff.', 'the parts\' descriptions: ' + about.join(' | '));
check(w.api.state().sel === 3 && w.rowText(w.rows()[3]) === 'Dylan' && />Artifacts<.*>Clothes<.*>Weapon Upgrades</.test(w.details()) && !/Promotional/.test(w.details()), 'a part shows the counts of its rows');
(w.game.listeners.OnNavigateDown || []).forEach(f => f()); w.game.frame(1);
check(w.api.state().sel === 3 && w.rows().length === 4, 'nothing under the three parts');
for (let i = 0; i < 5; i++) (w.game.listeners.OnNavigateUp || []).forEach(f => f());
w.game.frame(1);
press('OnPrev');
check(w.api.state().tab === -1 && w.game.tab === 2 && w.lit() === 2 && w.shownGame(), 'previous from Completion: the game\'s Tutorials');
before1 = w.game.changes;
press('OnNext');
check(w.api.state().tab === 0 && w.game.tab === 0 && w.shownPage() && w.game.changes === before1 + 1, 'next from Tutorials: Completion, with one change of the game\'s tab');

// the game's page never shows the wrong list while tabs change: checked frame by frame
w.game.press('OnNext');
let flashed = false;
for (let i = 0; i < 12; i++) { w.game.frame(1); if (w.shownGame()) flashed = true; }
check(!flashed && w.api.state().tab === 1, 'between two of our tabs the game\'s page stays hidden in every frame');
w.game.press('OnPrev'); w.game.frame(12); w.game.press('OnPrev');
flashed = false; let wrong = false;
for (let i = 0; i < 12; i++) { w.game.frame(1); if (w.shownGame() && w.game.tab !== 2) wrong = true; }
check(!wrong && w.shownGame() && w.game.tab === 2, 'going back to Tutorials the game\'s page appears only once the game is there');

// the mouse on a game tab while one of ours is open
press('OnNext'); press('OnNext');
check(w.api.state().tab === 1, 'on Story again');
w.buttons()[1].fire('click'); w.game.frame(12);
check(w.api.state().tab === -1 && w.game.tab === 1 && w.lit() === 1 && w.shownGame(), 'click on Enemies: there, with the game\'s page');
w.buttons()[5].fire('click'); w.game.frame(12);
check(w.api.state().tab === 2 && w.game.tab === 2, 'click on Zones from Enemies: Zones, one tab change of the game underneath');

// moving in the list
const sel = () => w.rows().findIndex(r => r.has('menu-button-box--selected'));
(w.game.listeners.OnNavigateDown || []).forEach(f => f()); w.game.frame(1);
check(w.api.state().sel === 1 && sel() === 1 && w.rows()[1].has('selection-item--selected'), 'down: the second row is selected and has the focus look');
check(/Innenstadt/.test(w.details()) && /\d+ \/ \d+/.test(w.details()), 'its details list the zones in the game\'s language');
for (let i = 0; i < 9; i++) (w.game.listeners.OnNavigateDown || []).forEach(f => f());
w.game.frame(1);
check(w.api.state().sel === 5, 'down stops at the last row');
w.window.ui_input_active_controller_menu_mouse.value = true;
w.rows()[2].fire('mouseenter'); w.game.frame(1);
check(w.api.state().sel === 2 && w.rows()[2].has('selection-item--hovered') && !w.rows()[2].has('selection-item--selected'), 'the mouse over a row selects it, with the hover look');
w.window.ui_input_active_controller_menu_mouse.value = false;

// closing and opening again
w.game.open = false; w.game.frame(3);
check(w.api.state().tab === -1 && w.shownGame() && !w.shownPage() && w.game.cells.slice(-5).every(c => c.m_disabled === false), 'Archives closed with our tab open: back to the game\'s page, its cells on');
w.game.openOn(2);
check(w.api.state().tab === -1 && w.lit() === 2 && w.shownGame(), 'opened again: the game\'s tab, nothing of ours');
w.game.open = false; w.game.frame(3); w.game.tab = 0; w.game.open = true; w.game.frame(2);
check(w.api.state().tab === -1, 'the game showing another tab when it opens is not taken for a key press');
w.game.frame(30);
const traces = w.game.requests.filter(u => /&t=/.test(u)).map(u => decodeURIComponent(u.split('&t=')[1])).join('|');
check(/game tab 2 -> 0 \(key next, ours -1\)/.test(traces) && /tab -1 -> 0/.test(traces), 'the DLL gets a trace of what happened (' + traces.split('|').length + ' lines)');

// ---------------------------------------------------------------- when things are not as expected
w = makeWorld({ late: true });
w.game.frame(40);
check(!w.api.state().ready && !w.api.state().failed && w.buttons().length === 3, 'the Archives page not loaded yet: the script waits and touches nothing');
w.game.loadPage(); w.game.frame(3);
check(w.api.state().ready && w.buttons().length === 7 && w.myPage() && w.myPage().parentNode === w.slot.parentNode, 'once the game has put its page in, the tabs follow');
w.game.openOn(0); w.buttons()[4].fire('click'); w.game.frame(5);
check(w.shownPage() && !w.shownGame() && w.decoy.style.display === undefined, 'and a click on Story hides the game\'s page, not the other screen\'s');
w = makeWorld({ noButtons: true });
w.game.frame(1900);
check(!w.api.state().ready && w.api.state().failed && /no tabs: the Archives tab bar has 0 buttons/.test(w.api.state().status) && w.bar.childNodes.length === 2, 'a tab bar without the three buttons: nothing is touched, the status says what was found');
w = makeWorld();
w.game.frame(3); w.game.openOn(0);
w.buttons()[3].fire('click'); w.game.frame(5);
w.game.setCellThrows = true;
w.game.frame(400);
check(w.api.state().failed && w.originals.every(o => o.parentNode === w.bar) && w.buttons().length === 3 && !w.myPage() && w.shownGame(), 'errors while running: the game\'s own three buttons and page are put back');
const n = w.game.events.length; w.engine.trigger('narrative_object_category_action', 1);
check(w.game.events.length === n + 1, 'and its events pass again');
w = makeWorld({ breakInsertAt: 5 });
w.game.frame(5);
check(w.api.state().failed && /no tabs/.test(w.api.state().status) && w.buttons().length === 3 && w.originals.every(o => o.parentNode === w.bar) && !w.myPage() &&
      w.bar.childNodes[0] === w.leftKey && w.bar.childNodes[4] === w.rightKey, 'the page refusing a button half way: ours are taken out again and the three of the game stand as before');
w = makeWorld({ arch: { hold: 0 } });
w.game.frame(3); w.game.openOn(0); w.buttons()[3].fire('click'); w.game.frame(20);
check(w.api.state().tab === 0 && w.game.cells.length === 0, 'with the list hold switched off the game\'s cells are left alone');
w = makeWorld({ arch: { on: 0 } });
w.game.frame(5);
check(w.buttons().length === 3 && w.originals.every(o => o.parentNode === w.bar), 'switched off in the settings: the game\'s bar is as it was');

// ---------------------------------------------------------------- one tab sound per key press
w = makeWorld();
w.game.frame(3); w.game.openOn(2);
let steps = [];
for (let i = 0; i < 7; i++) { const c = w.game.changes; press('OnNext'); steps.push(w.game.changes - c); }
check(w.api.state().tab === -1 && w.game.tab === 2 && w.lit() === 2 && steps.join('') === '1111211', 'seven times next, once around: every key press changes the game\'s tab once, except the step from our last tab to Collectibles (' + steps.join(' ') + ')');
steps = [];
for (let i = 0; i < 7; i++) { const c = w.game.changes; press('OnPrev'); steps.push(w.game.changes - c); }
check(w.api.state().tab === -1 && w.game.tab === 2 && steps.join('') === '1111112', 'seven times previous: the same, the second change is at the step from Completion back to Tutorials (' + steps.join(' ') + ')');
steps = [];
for (const k of ['OnNext', 'OnNext', 'OnNext', 'OnPrev', 'OnPrev', 'OnPrev']) { const c = w.game.changes; press(k); steps.push(w.game.changes - c); }
check(w.api.state().tab === -1 && w.game.tab === 2 && steps.join('') === '111111', 'into our tabs and back the same way: one change per key press throughout');
w.game.openOn(2); w.buttons()[3].fire('click'); w.game.frame(12);
let c0 = w.game.changes;
w.buttons()[5].fire('click'); w.game.frame(12);
check(w.api.state().tab === 2 && w.game.changes === c0 + 1, 'a click from one of our tabs to another: one change of the game\'s tab');
c0 = w.game.changes; w.buttons()[5].fire('click'); w.game.frame(12);
check(w.game.changes === c0, 'a click on the tab that is open already: none');

// ---------------------------------------------------------------- what the DLL holds back: a story total, a zone not visited yet
page.rows.find(r => r.k === 'main').v = '25 / ??';
page.rows.find(r => r.k === 'feats').d.groups[2] = ['', '???', 0, 0, 1];
w = makeWorld();
w.game.frame(3); w.game.openOn(0);
w.buttons()[4].fire('click'); w.game.frame(5);
check(w.rowText(w.rows()[0]) === 'Main Story Missions' && w.rowCounter(w.rows()[0]) === '25/??', 'a story total held back shows as the DLL sends it: ' + w.rowCounter(w.rows()[0]));
w.buttons()[5].fire('click'); w.game.frame(5);
for (let i = 0; i < 3; i++) { (w.game.listeners.OnNavigateDown || []).forEach(f => f()); w.game.frame(1); }
check(w.rowText(w.rows()[3]) === 'World Feats' && w.api.state().sel === 3 && /By Zone/.test(w.details()) && />\?\?\?<\/div><div [^>]*>\?\?<\/div>/.test(w.details()) && (w.details().match(/ \/ /g) || []).length === page.rows.find(r => r.k === 'feats').d.groups.length - 1,
      'a zone not visited yet is a row ??? with ?? for its count, between the zones that are named');

// ---------------------------------------------------------------- a SHÜM machine shows its place, not a count
(function () {
    var shum = page.rows.find(r => r.k === 'shum');
    shum.d.groups = [['ZONE_DOWNTOWN', 'Downtown', 3, 3, 0, '1st Place'], ['ZONE_FRONTLINE', 'Central', 1, 3, 0, '3rd Place'], ['ZONE_MOLD', 'The Park', 0, 3, 0, 'Need higher score'], ['', '???', 0, 0, 1]];
    w = makeWorld();
    w.game.frame(3); w.game.openOn(0);
    w.buttons()[5].fire('click'); w.game.frame(5);
    for (var n = 0; n < 4; n++) { (w.game.listeners.OnNavigateDown || []).forEach(f => f()); w.game.frame(1); }
    var det = w.details();
    check(w.rowText(w.rows()[4]) === 'SH\u00dcM Scores' && w.api.state().sel === 4 && /1st Place/.test(det) && /3rd Place/.test(det) && /Need higher score/.test(det) && !/3 \/ 3/.test(det) && !/1 \/ 3/.test(det) && />\?\?<\/div>/.test(det) &&
          /rgb\(11,232,232\)">1st Place/.test(det) && !/rgb\(11,232,232\)">3rd Place/.test(det),
          'a SHÜM machine shows its place (1st Place in the colour of a full count), a zone not visited stays ??');
})();

// ---------------------------------------------------------------- Main Story Missions lists what is completed; a long list gets lower rows
(function () {
    var main = page.rows.find(r => r.k === 'main');
    var keep = JSON.parse(JSON.stringify(main));
    var names = []; for (var i = 1; i <= 26; i++) names.push('Mission number ' + i);
    function open(d, v) {
        main.v = v; main.d = d;
        w = makeWorld();
        w.game.frame(3); w.game.openOn(0);
        w.buttons()[4].fire('click'); w.game.frame(5);
        return w.details();
    }
    var det = open({ done: names.slice(0, 16).map(n => ['', n, '']) }, '16 / ??');
    check(w.rowText(w.rows()[0]) === 'Main Story Missions' && det.indexOf('Completed') >= 0 && det.indexOf('Mission number 1<') < det.indexOf('Mission number 16<') &&
          !/What is missing|still to go|Still Missing/.test(det) && (det.match(/min-height:3\.7037037037vh/g) || []).length === 16,
          'Main Story Missions: 16 completed missions by name under "Completed", in rows of the usual height, no sentence about the rest');
    det = open({ done: names.map(n => ['', n, '']) }, '26 / 26');
    var hs = det.match(/min-height:([0-9.]+)vh/g) || [], h = hs.length ? parseFloat(hs[0].slice(11)) : 0;
    check(hs.length === 26 && hs.every(x => x === hs[0]) && h > 2.2 && h * 26 <= 60.01 && /font-size:1\.8\d+vh/.test(det) && det.indexOf('Mission number 26<') > det.indexOf('Mission number 25<'),
          'all 26: lower rows so the list fits the page (' + h + 'vh a row, ' + (h * 26).toFixed(1) + 'vh in all)');
    det = open({ done: names.slice(0, 20).map(n => ['', n, '']), missing: names.slice(20).map(n => ['', n, '']), limit: 14 }, '20 / ??');
    hs = det.match(/min-height:([0-9.]+)vh/g) || []; h = hs.length ? parseFloat(hs[0].slice(11)) : 0;
    check(hs.length === 26 && hs.every(x => x === hs[0]) && h * 26 + 5.1 <= 60.01 && det.indexOf('Still Missing') > det.indexOf('Mission number 20<') && det.indexOf('Still Missing') < det.indexOf('Mission number 21<'),
          'with names shown: Completed, then Still Missing, rows of one height that fit together (' + h + 'vh)');
    Object.keys(main).forEach(k => delete main[k]); Object.assign(main, keep);
})();

// ---------------------------------------------------------------- Side Stories lists what is completed, above what is missing or held back
(function () {
    var side = page.rows.find(r => r.k === 'side');
    var keep = JSON.parse(JSON.stringify(side));
    var sentence = 'Undiscovered side stories will be listed here after completing the main story.';
    side.v = '2 / ??'; side.d = { done: [['', 'Urban Survival', ''], ['', 'New Growth', '']], hidden: sentence };
    w = makeWorld();
    w.game.frame(3); w.game.openOn(0);
    w.buttons()[4].fire('click'); w.game.frame(5);
    (w.game.listeners.OnNavigateDown || []).forEach(f => f()); w.game.frame(1);
    var det = w.details();
    check(w.rowText(w.rows()[1]) === 'Side Stories' && det.indexOf('Completed') >= 0 && det.indexOf('Completed') < det.indexOf('Urban Survival') && det.indexOf('Urban Survival') < det.indexOf('New Growth') &&
          det.indexOf('New Growth') < det.indexOf(sentence) && !/Still Missing/.test(det), 'Side Stories: the completed ones by name under "Completed", then the sentence about the rest');
    side.v = '2 / 13'; side.d = { done: [['', 'Urban Survival', ''], ['', 'New Growth', '']], missing: [['', 'Zoe\'s Briefing', '']], limit: 14 };
    w = makeWorld();
    w.game.frame(3); w.game.openOn(0);
    w.buttons()[4].fire('click'); w.game.frame(5);
    (w.game.listeners.OnNavigateDown || []).forEach(f => f()); w.game.frame(1);
    det = w.details();
    check(det.indexOf('Completed') >= 0 && det.indexOf('New Growth') < det.indexOf('Still Missing') && det.indexOf('Still Missing') < det.indexOf('Zoe\'s Briefing'), 'with names shown: Completed, then Still Missing');
    Object.keys(side).forEach(k => delete side[k]); Object.assign(side, keep);
})();

// ---------------------------------------------------------------- the side stories in parts: one part of the bar together, ??? until found and then last
(function () {
    var pr = k => page.rows.find(r => r.k === k);
    var taxi = pr('taxi');
    taxi.v = '3 / 7'; taxi.f = 3 / 7; taxi.w = [3, 7];
    taxi.d.groups = [['ZONE_DOWNTOWN', 'Downtown', 0, 1, 0, 'Not found'], ['ZONE_FRONTLINE', 'Central', 1, 1, 0, 'Found'], ['ZONE_HISS', 'Evacuation Zone', 1, 1, 0, 'Found'],
                     ['ZONE_PATTERN', 'West Incursion Zone', 0, 1, 0, 'Not found'], ['ZONE_MOLD', 'The Park', 1, 1, 0, 'Found'], ['ZONE_UNDERCITY', 'Underpass', 0, 1, 0, 'Not found'], ['', '???', 0, 0, 1]];
    // two stories not found yet: the first of the row order and the last
    [['mapkits', 7], ['pope', 8]].forEach(function (x) { var r = pr(x[0]); r.l = '???'; r.v = '0 / ??'; r.f = 0; r.w = [0, x[1]]; r.about = 'A story you have not found yet.'; r.d = {}; });
    var pooled = ['mapkits', 'sketch', 'science', 'freeze', 'taxi', 'dog', 'speakers', 'pope'].map(pr);
    var want = (pr('main').f + pr('side').f + pooled.reduce((a, r) => a + r.w[0], 0) / pooled.reduce((a, r) => a + r.w[1], 0)) / 3;
    var text = (Math.floor(want * 1000) / 10).toFixed(1).replace(/\.0$/, '') + '%';
    w = makeWorld();
    w.game.frame(3); w.game.openOn(0);
    w.buttons()[3].fire('click'); w.game.frame(5);
    check(pooled.every(r => r.pool === 1) && w.rowText(w.rows()[1]) === 'Story' && w.rowCounter(w.rows()[1]) === text && want < 0.95 && want > 0.85,
          'Completion: the stories in parts are one of Story\'s three parts, so Story reads ' + w.rowCounter(w.rows()[1]) + ' (' + text + ' expected)');
    (w.game.listeners.OnNavigateDown || []).forEach(f => f()); w.game.frame(1);
    var det = w.details();
    check(det.indexOf('Side Stories') < det.indexOf('Unsupervised Science') && det.indexOf('Unsupervised Science') < det.indexOf('The Complete Set') && det.indexOf('The Complete Set') < det.indexOf('???') &&
          (det.match(/\?\?\?/g) || []).length === 2, 'Completion, Story selected: its rows in order, the two ??? ones last');
    w.buttons()[4].fire('click'); w.game.frame(5);
    check(w.rows().map(w.rowText).join(',') === 'Main Story Missions,Side Stories,Dylan\'s Sketchbook,Unsupervised Science,Freeze Frame,The Last Taxi,Every Dog Has Her Day,The Complete Set,???,???' &&
          w.rowCounter(w.rows()[8]) === '0/??' && w.rowCounter(w.rows()[9]) === '0/??' && w.rowCounter(w.rows()[5]) === '3/7',
          'Story: a story with nothing found is a row ??? with 0/??, and such rows are at the bottom of the list');
    for (var i = 0; i < 5; i++) { (w.game.listeners.OnNavigateDown || []).forEach(f => f()); w.game.frame(1); }
    det = w.details();
    check(/By Zone/.test(det) && /rgb\(11,232,232\)">Found/.test(det) && />Not found</.test(det) && !/rgb\(11,232,232\)">Not found/.test(det) && />\?\?\?<\/div><div [^>]*>\?\?<\/div>/.test(det) && !/1 \/ 1/.test(det),
          'The Last Taxi: a line per zone, Found in the colour of a full count, Not found plain, a zone not visited ???');
    for (i = 0; i < 4; i++) { (w.game.listeners.OnNavigateDown || []).forEach(f => f()); w.game.frame(1); }
    check(w.api.state().sel === 9 && !/By Zone|Found/.test(w.details()), 'the ??? row shows no lines');
})();

console.log(fails ? 'FAILED' : 'ALL PASSED');
process.exit(fails ? 1 : 0);
