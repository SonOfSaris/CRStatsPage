/* StatsPage: the completion tabs of the Archives screen (in-game menu).
 * statscompletiontracker.dll appends this file to the game's globals script. It stands on its own: it uses only what the page
 * gives every script (the document, "engine", "navigation", the game's data models as globals), no game file is
 * changed for it, and if anything here fails the game's own three tabs are put back.
 *
 * The game knows three Archives tabs and counts them in its own code: next goes 0 -> 1 -> 2 -> 0. This script
 * adds more tabs behind the third one without telling the game:
 *  - the tab bar's buttons are replaced by plain copies this script draws itself, the game's three plus ours
 *    (Completion, Story, Zones, Dylan);
 *    our page goes next to the game's Archives page (the element that took the placeholder's place at start);
 *  - while one of our tabs is open the game's page is hidden and its list is switched off, and this script's
 *    page is shown in its place;
 *  - a tab key always moves the game by one; a change of the game's tab that no click asked for is therefore a
 *    key press, and its direction says where to go in the longer row of tabs. The game stays on the tab the key
 *    took it to: it plays its tab sound with every change of its tab, so moving it a second time would sound
 *    twice. Only when the row leaves our tabs for a game tab the game is not on, it is sent there.
 * The numbers come from the DLL: coui://base/__statspage__.json, the same answer the Options page uses. */
(function () {
    'use strict';
    if (window.__StatsPageArchive) return;
    var API = window.__StatsPageArchive = { version: 15 };

    var URL = 'coui://base/__statspage__.json';
    var GAME_TABS = 3;
    var GAME_KEYS = [['MENU_NARRATIVE_OBJECT_COLLECTIBLES', 'Collectibles'], ['MENU_NARRATIVE_OBJECT_ENEMIES', 'Enemies'], ['MENU_NARRATIVE_OBJECT_TUTORIALS', 'Tutorials']];
    var ROW_KEYS = ['total', 'main', 'side', 'mapkits', 'science', 'freeze', 'taxi', 'dog', 'speakers', 'pope',
                    'levels', 'encounters', 'vending', 'feats', 'shum', 'tasks', 'collectibles', 'research',
                    'artifacts', 'outfits', 'abilities', 'talents', 'weapons', 'promo'];
    // The parts the tabs show. Each has a tab of its own, and Completion lists them with their percentages.
    // Collectibles and enemy research count toward the bar too but have no row here: the game's own Collectibles
    // and Enemies tabs show them. Neither has the bar's last 1% (an item the game cannot hand out).
    // "apart" rows are listed in a tab but are no part of the bar (the promotion items: a count, no total).
    // Rows are in the order a player comes to them; a row that reads ??? goes to the bottom of its list (ordered).
    var SECTIONS = [
        { id: 'story', label: 'Story', about: 'Main story missions and side story missions.', rows: ['main', 'side', 'mapkits', 'science', 'freeze', 'taxi', 'dog', 'speakers', 'pope'] },
        { id: 'zones', label: 'Zones', about: 'Missions and encounters around Manhattan.', rows: ['levels', 'encounters', 'vending', 'feats', 'shum', 'tasks'] },
        { id: 'dylan', label: 'Dylan', about: 'Dylan\'s stuff.', rows: ['artifacts', 'outfits', 'abilities', 'talents', 'weapons'], apart: ['promo'] }
    ];
    var TABS = [{ id: 'completion', label: 'Completion' }].concat(SECTIONS);
    var N = TABS.length;
    // Events of the game's page that must not reach the game while one of our tabs is open.
    var QUIET = { narrative_object_category_focus: 1, narrative_object_category_action: 1, narrative_object_select: 1, narrative_object_action: 1,
                  narrative_object_focus: 1, narrative_object_leave: 1, narrative_object_close_inspect: 1 };
    var BUTTON = 'menu-button tab-buttons-button tab-buttons__element menu-button--small';
    var ROW = 'menu-button-box category-list-item narrative-object-category-item menu-button-box--invisible menu-button-box--selected-solid menu-button-box--locked-stripes';
    var MARKER = '<svg class="new-marker__outer" viewBox="0 0 32 28" xmlns="http://www.w3.org/2000/svg"><path d="M16 27L0.41 0L31.59 0Z"></path></svg>' +
                 '<svg class="new-marker__inner" viewBox="0 0 32 28" xmlns="http://www.w3.org/2000/svg"><path d="M16 15L10.8 6L21.2 6Z"></path></svg>';

    var cfg = { on: true, hold: true, trace: true };
    var status = 'started', failed = false, ready = false, tries = 0, errors = 0;
    var tab = -1, sel = 0, nativeSel = -1, wasOpen = false, openAt = 0, expectQ = [], clickAt = 0, clickArg = -1;
    var hoverBtn = -1, hoverRow = -1, held = false, holdPasses = 0, releasePasses = 0, frame = 0, heldCurrent, heldTab = -1;
    var data = null, requests = 0, lastAsk = 0, traceBuf = [], traceSent = 0, traceAt = 0, t0 = Date.now();
    var dom = null, entries = [], shown = { tab: -2, sel: -2, data: null, mouse: null };
    var origTrigger = null, moves = 0;

    function now() { return Date.now(); }
    function mv(name, fallback) { var m = window[name]; return m && m.value !== undefined ? m.value : fallback; }
    function trace(text) {
        if (!cfg.trace || traceSent + traceBuf.length >= 600) return;
        traceBuf.push(((now() - t0) / 1000).toFixed(2) + ' ' + text);
    }
    function esc(t) { return String(t == null ? '' : t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }
    // The game's own text for a name (in the player's language), or the English name the DLL sent.
    function tr(key, fallback) {
        try {
            if (key) { var t = engine.translate(key); if (typeof t === 'string' && t && t !== key) return t.replace(/<[^>]*>/g, '').replace(/\s+/g, ' ').trim() || fallback; }
        } catch (e) { }
        return fallback;
    }

    // ---------------------------------------------------------------- the DLL's numbers
    function ask(extra) {
        var x;
        try { x = new XMLHttpRequest(); } catch (e) { return; }
        var n = ++requests, settled = false;
        function done(text) {
            if (settled) return; settled = true;
            var r = null; try { r = text ? JSON.parse(text) : null; } catch (e) { r = null; }
            if (r && typeof r === 'object' && r.rows) {
                data = r;
                if (r.arch) { cfg.on = r.arch.on !== 0; cfg.hold = r.arch.hold !== 0; cfg.trace = r.arch.trace !== 0; }
                build();
            }
        }
        try {
            x.open('GET', URL + '?w=arch&n=' + n + '&s=' + encodeURIComponent(status) + (extra || ''), true);
            x.onload = function () { done(x.status === 200 || x.status === 0 ? x.responseText : ''); };
            x.onerror = function () { done(''); };
            x.send();
        } catch (e) { done(''); }
    }
    function refresh() { var t = now(); if (t - lastAsk < 1500) return; lastAsk = t; ask(''); }
    function flushTrace() {
        if (!traceBuf.length || now() - traceAt < 1000) return;
        traceAt = now();
        var out = '', count = 0;
        while (traceBuf.length && out.length < 1400) { out += (count ? '|' : '') + traceBuf.shift(); count++; }
        traceSent += count;
        ask('&t=' + encodeURIComponent(out));
    }
    function setStatus(s) { if (s !== status) { status = s; lastAsk = 0; refresh(); } }

    // ---------------------------------------------------------------- what the tabs list
    function rowOf(key) {
        if (!data || !data.rows) return null;
        var k;
        for (k = 0; k < data.rows.length; k++) if (data.rows[k] && data.rows[k].k === key) return data.rows[k];
        k = ROW_KEYS.indexOf(key);
        return k >= 0 && data.rows[k] ? data.rows[k] : null;
    }
    function share(r) { return r && typeof r.f === 'number' ? Math.max(0, Math.min(1, r.f)) : 0; }
    function percent(f) { var p = Math.floor(f * 1000) / 10; return (p === Math.floor(p) ? String(p) : p.toFixed(1)) + '%'; }
    // Rows that read ??? (a story not found yet) go to the bottom of a list, the others keep their order.
    function ordered(keys) {
        var a = [], b = [], k, r;
        for (k = 0; k < keys.length; k++) { r = rowOf(keys[k]); (r && r.l === '???' ? b : a).push(keys[k]); }
        return a.concat(b);
    }
    // Rows of one pool (the side stories in parts) are one part of the bar together: what they have of all they hold.
    function sectionShare(s) {
        var sum = 0, n = 0, k, pools = {}, p;
        for (k = 0; k < s.rows.length; k++) {
            var r = rowOf(s.rows[k]);
            if (!r) continue;
            if (r.pool && r.w) { p = pools[r.pool] || (pools[r.pool] = [0, 0]); p[0] += r.w[0]; p[1] += r.w[1]; continue; }
            sum += share(r); n++;
        }
        for (k in pools) if (pools.hasOwnProperty(k)) { sum += pools[k][1] ? pools[k][0] / pools[k][1] : 0; n++; }
        return n ? sum / n : 0;
    }
    // The rows of the left list, per tab. An entry: label, counter text, complete, and what the details show.
    function build() {
        entries = [];
        var t, k, r, list;
        for (t = 0; t < N; t++) {
            list = [];
            if (t === 0) {
                r = rowOf('total');
                list.push({ kind: 'total', label: r ? r.l : 'Completion', counter: r ? r.v : '', done: !!r && share(r) >= 1, row: r });
                for (k = 0; k < SECTIONS.length; k++) {
                    var f = sectionShare(SECTIONS[k]);
                    list.push({ kind: 'section', label: SECTIONS[k].label, counter: data ? percent(f) : '', done: f >= 1, section: SECTIONS[k], f: f });
                }
            } else {
                var s = TABS[t], keys = ordered(s.rows).concat(s.apart || []);
                for (k = 0; k < keys.length; k++) {
                    r = rowOf(keys[k]);
                    if (!r && k >= s.rows.length) continue;
                    list.push({ kind: 'cat', label: r ? r.l : keys[k], counter: r ? r.v : '', done: !!r && !r.count && share(r) >= 1, row: r });
                }
            }
            entries.push(list);
        }
        shown.data = null;          // draw again
    }

    // ---------------------------------------------------------------- small DOM helpers
    function el(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
    function setClass(e, cls) { if (e && e.className !== cls) e.className = cls; }
    function setText(e, text) { text = String(text == null ? '' : text); if (e && e.__text !== text) { e.__text = text; e.textContent = text; } }
    function setHtml(e, html) { if (e && e.__html !== html) { e.__html = html; e.innerHTML = html; } }
    function hasClass(e, name) { return (' ' + (e.className || '') + ' ').indexOf(' ' + name + ' ') >= 0; }
    function kids(e) { var out = [], c = e.children, k; for (k = 0; c && k < c.length; k++) if (c[k]) out.push(c[k]); return out; }
    function inside(e, ancestor) { for (var p = e; p; p = p.parentNode) if (p === ancestor) return true; return false; }
    // The Archives page as it stands in the menu. The page file's top element takes the place of the placeholder
    // <div id="NarrativeObjectMenuComponent"> when the UI starts, so there is no id to find it by; another screen
    // (the Deluxe Edition menu) uses the same class, so it must be the one in the menu that holds our tab bar.
    function gamePage(holder) {
        var frame = null, p, k, all = document.querySelectorAll('.narrative-object-menu'), first = null;
        for (p = holder; p; p = p.parentNode) if (hasClass(p, 'fullscreen-layout__content')) { frame = p; break; }
        for (k = 0; all && k < all.length; k++) {
            if (hasClass(all[k], 'statspage-archive') || !all[k].parentNode) continue;
            if (frame ? inside(all[k], frame) : true) { first = all[k]; break; }
        }
        return first;
    }
    function show(e, on, how) { var v = on ? (how || '') : 'none'; if (e && e.__display !== v) { e.__display = v; e.style.display = v; } }

    // ---------------------------------------------------------------- the tab bar
    function makeButton(index) {
        var b = el('div', BUTTON), marker = el('div', 'new-marker new-marker--top-center'), label = el('div', 'menu-button__label tab-buttons-button__label menu-button__label--small');
        marker.innerHTML = MARKER;
        b.appendChild(marker); b.appendChild(label);
        b.addEventListener('click', function () { guard(function () { clickButton(index); }); });
        b.addEventListener('mouseenter', function () {
            hoverBtn = index;
            if (index < GAME_TABS && tab < 0) { try { origTrigger.call(engine, 'narrative_object_sub_tab_focus', index); } catch (e) { } }
        });
        b.addEventListener('mouseleave', function () { if (hoverBtn === index) hoverBtn = -1; });
        return { root: b, marker: marker, label: label };
    }
    function setup() {
        var holder = document.querySelector('.narrative-object-menu-sub-tabs'), bar = null, key = null, originals = [], buttons = [], k, c;
        if (!holder) return 'the page has no Archives tab bar';
        c = kids(holder);
        for (k = 0; k < c.length; k++) if (hasClass(c[k], 'tab-buttons')) bar = c[k];
        if (!bar) return 'the Archives tab bar holds ' + c.length + ' elements, none of them the row of buttons';
        var slot = gamePage(holder);
        if (!slot) return document.getElementById('NarrativeObjectMenuComponent') ? 'the Archives page is not loaded yet' : 'the Archives page is not in the menu';
        c = kids(bar);
        for (k = 0; k < c.length; k++) {
            if (hasClass(c[k], 'tab-buttons-button')) originals.push(c[k]);
            else if (hasClass(c[k], 'tab-buttons__key--right')) key = c[k];
        }
        if (originals.length !== GAME_TABS || !key) return 'the Archives tab bar has ' + originals.length + ' buttons' + (key ? '' : ' and no key symbol at its end') + ' (' + c.length + ' elements)';
        for (k = 0; k < GAME_TABS + N; k++) buttons.push(makeButton(k));
        for (k = 0; k < GAME_TABS; k++) setText(buttons[k].label, tr(GAME_KEYS[k][0], GAME_KEYS[k][1]));
        for (k = 0; k < N; k++) setText(buttons[GAME_TABS + k].label, TABS[k].label);
        var page = makePage();
        dom = { bar: bar, slot: slot, key: key, originals: originals, buttons: buttons, page: page, swapped: false };
        // our buttons in, then the game's out (kept, for the way back)
        for (k = 0; k < buttons.length; k++) bar.insertBefore(buttons[k].root, key);
        for (k = 0; k < originals.length; k++) bar.removeChild(originals[k]);
        dom.swapped = true;
        slot.parentNode.appendChild(page.root);
        return '';
    }
    // Back to the game's own tab bar and page (after an error, or when switched off).
    function restore() {
        try { if (held) holdList(false); } catch (e) { }
        held = false;
        try {
            if (dom && dom.swapped) {
                var k;
                for (k = 0; k < dom.originals.length; k++) dom.bar.insertBefore(dom.originals[k], dom.key);
                for (k = 0; k < dom.buttons.length; k++) if (dom.buttons[k].root.parentNode) dom.buttons[k].root.parentNode.removeChild(dom.buttons[k].root);
                dom.swapped = false;
            }
        } catch (e) { }
        try {
            if (dom) {
                if (dom.page.root.parentNode) dom.page.root.parentNode.removeChild(dom.page.root);
                dom.slot.style.display = ''; dom.slot.__display = '';
            }
        } catch (e) { }
        tab = -1; ready = false; expectQ = [];
    }

    // ---------------------------------------------------------------- our page: list on the left, details, a panel on the right
    function makePage() {
        var root = el('div', 'narrative-object-menu statspage-archive');
        root.style.display = 'none';
        var layout = el('div', 'narrative-object-menu__items-layout narrative-object-menu__view');
        var cats = el('div', 'category-list narrative-object-menu__categories');
        // a little wider than the game's list (33.3 with a gap of 3.7), for titles like "Mapping the New New York"
        cats.style.width = '38.8888888889vh'; cats.style.marginRight = '1.8518518519vh';
        var list = el('div', 'category-list__content');
        list.style.marginTop = '0'; list.style.marginLeft = '0';
        cats.appendChild(list);
        var main = el('div', 'menu-with-context');
        main.style.alignItems = 'flex-start';
        var mid = el('div', 'menu-with-context__content narrative-object-menu__objects');
        mid.style.height = '72vh';
        var scroller = el('div', 'menu-context statspage-archive__details');
        scroller.style.width = '100%'; scroller.style.height = '100%'; scroller.style.overflow = 'hidden';      // the lists fit; no scroll bar of the engine's own
        mid.appendChild(scroller);
        scroller.addEventListener('wheel', function (e) { try { scroller.scrollTop += (e && e.deltaY) || 0; } catch (x) { } });
        var side = el('div', 'menu-with-context__context');
        var panel = el('div', 'menu-context');
        var section = el('div', 'menu-context__section menu-context__section--no-top-border');
        var heading = el('div', 'menu-context__heading');
        var bar = el('div'), fill = el('div');
        bar.style.height = '.7407407407vh'; bar.style.margin = '.9259259259vh 0'; bar.style.backgroundColor = 'rgba(232,232,232,.15)';
        fill.style.height = '100%'; fill.style.width = '0%'; fill.style.backgroundColor = '#e8e8e8';
        bar.appendChild(fill);
        var about = el('p', 'menu-context-text menu-context-text--gameplay');
        var note = el('p', 'menu-context-text menu-context-text--dimmed');
        section.appendChild(heading); section.appendChild(bar); section.appendChild(about); section.appendChild(note);
        panel.appendChild(section); side.appendChild(panel);
        main.appendChild(mid); main.appendChild(side);
        layout.appendChild(cats); layout.appendChild(main);
        root.appendChild(layout);
        return { root: root, list: list, rows: [], scroller: scroller, heading: heading, bar: bar, fill: fill, about: about, note: note };
    }
    function makeRow(index) {
        var r = el('div', ROW), name = el('span', 'narrative-object-category-item__name'), counter = el('div', 'narrative-object-category-item__counter narrative-object-category-item__counter--visible');
        var stripes = el('div', 'menu-button-box__stripes'), underline = el('div', 'menu-button-box__underline');
        var body = el('div', 'category-list-item__body'), content = el('div', 'category-list-item__body__content');
        content.appendChild(name); content.appendChild(counter); body.appendChild(content);
        r.appendChild(stripes); r.appendChild(underline); r.appendChild(body);
        r.addEventListener('mouseenter', function () { hoverRow = index; if (tab >= 0 && sel !== index) { sel = index; scrollTop(); } });
        r.addEventListener('mouseleave', function () { if (hoverRow === index) hoverRow = -1; });
        return { root: r, name: name, counter: counter, stripes: stripes, underline: underline };
    }
    function scrollTop() { try { if (dom) dom.page.scroller.scrollTop = 0; } catch (e) { } }

    // The middle column: rows of two cells with a thin line under each, the name on the left and the count on
    // the right. Every row is a flex row with a style of its own: the engine lays out no inline text and draws
    // an ::after text as a box of its own, so nothing here leans on either.
    var CYAN = 'rgb(11,232,232)', PAD = '.9259259259vh';
    function cell(label, value, look, tight) {
        // tight: true for a low row, or a height in vh of a list that has to fit the page (fitRows)
        var low = typeof tight === 'number' && tight > 0;
        var height = low ? tight.toFixed(4) + 'vh' : tight ? '2.7777777778vh' : '3.7037037037vh';
        if (low && tight < 2.7777) height += ';line-height:' + tight.toFixed(4) + 'vh;font-size:' + (tight * 0.8).toFixed(4) + 'vh';
        var right = look === 'note' ? 'color:rgba(232,232,232,.5)' : 'font-weight:700;color:' + (look === 'done' ? CYAN : look === 'dim' ? 'rgba(232,232,232,.35)' : '#e8e8e8');
        return '<div style="display:flex;flex-direction:row;align-items:center;justify-content:space-between;min-height:' + height +
               ';padding:0 ' + PAD + ';border-bottom:.0925925926vh solid rgba(232,232,232,.08)">' +
               '<div style="color:rgba(232,232,232,.8)">' + label + '</div><div style="margin-left:1.8518518519vh;' + right + '">' + value + '</div></div>';
    }
    function head(title, first) {
        return '<div style="padding:0 ' + PAD + ';margin-top:' + (first ? '0' : '1.8518518519vh') + ';margin-bottom:.462962963vh;font-weight:700;color:#e8e8e8">' + esc(title) + '</div>';
    }
    function words(text) { return '<div style="padding:.462962963vh ' + PAD + ';color:rgba(232,232,232,.5)">' + esc(text) + '</div>'; }
    // The height in vh of a row so that a list of <rows> rows fits the page, with <heads> headings beyond its first
    // and, if <more>, the line that says how many are left out: 0 (the usual row) where that fits, less for a long
    // list such as every main mission.
    function fitRows(rows, heads, more) { var h = (60 - 5.1 * heads - (more ? 3.8 : 0)) / rows; return h >= 3.7037037037 ? 0 : Math.max(h, 1.6); }
    function column(body) { return body ? '<div style="display:flex;flex-direction:column;width:51.8518518519vh;margin-top:.6481481481vh">' + body + '</div>' : ''; }
    function countLook(v) { var p = String(v).split(' / '); return (p.length === 2 && p[0] === p[1]) || v === '100%' ? 'done' : ''; }
    // For a row of the DLL's answer: counts per zone, what is missing, what is owned (a row without a total), the
    // save's lines.
    function rowBody(r) {
        if (!r) return '';
        var d = r.d || {}, out = '', k, n;
        if (d.groups && d.groups.length) {
            out += head('By Zone', !out);
            for (k = 0; k < d.groups.length; k++) {
                if (d.groups[k][4]) { out += cell(esc(d.groups[k][1]), '??', 'dim'); continue; }       // a zone not visited yet
                n = d.groups[k][2] + ' / ' + d.groups[k][3];
                // a sixth entry is a text for the count (a SHUM machine's place); the look stays the count's
                out += cell(esc(tr(d.groups[k][0], d.groups[k][1])), d.groups[k][5] ? esc(d.groups[k][5]) : n, countLook(n));
            }
        }
        // what is done by name (Main Story Missions, Side Stories), above what is missing
        var limit = d.limit || 12, nDone = d.done ? d.done.length : 0, nMiss = d.missing ? d.missing.length : 0, size = 0;
        if (nDone || nMiss) size = fitRows(nDone + Math.min(nMiss, limit), nDone && nMiss ? 1 : 0, nMiss > limit);
        if (nDone) {
            out += head('Completed', !out);
            for (k = 0; k < nDone; k++) out += cell(esc(tr(d.done[k][0], d.done[k][1])), '', 'note', size);
        }
        if (nMiss) {
            out += head(d.missingTitle || 'Still Missing', !out);
            for (k = 0; k < d.missing.length && k < limit; k++) out += cell(esc(tr(d.missing[k][0], d.missing[k][1])), esc(d.missing[k][2] || ''), 'note', size);
            if (d.missing.length > limit) out += words('and ' + (d.missing.length - limit) + ' more');
        } else if (d.hidden) out += (out ? '<div style="height:1.3888888889vh"></div>' : '') + words(d.hidden);
        if (d.owned) {
            if (d.owned.length) { out += head('Owned', !out); for (k = 0; k < d.owned.length; k++) out += cell(esc(tr(d.owned[k][0], d.owned[k][1])), '', '', true); }
            else out += words('None owned.');
        }
        if (d.lines && d.lines.length) for (k = 0; k < d.lines.length; k++) out += words(d.lines[k]);
        return out;
    }
    function detailsOf(e) {
        if (!e) return '';
        var k, r, f, s = '';
        if (e.kind === 'section') {
            var sk = ordered(e.section.rows);
            for (k = 0; k < sk.length; k++) { r = rowOf(sk[k]); if (r) s += cell(esc(r.l), esc(r.v), countLook(r.v)); }
            return column(s);
        }
        if (e.kind === 'total') {
            for (k = 0; k < SECTIONS.length; k++) { f = sectionShare(SECTIONS[k]); s += cell(esc(SECTIONS[k].label), percent(f), f >= 1 ? 'done' : ''); }
            return column(s + rowBody(e.row));
        }
        s = rowBody(e.row);
        if (!s && e.row) {                       // a row without a list (talents, weapon upgrades): say how much is left
            var p = String(e.row.v || '').split(' / '), have = parseInt(p[0], 10), total = parseInt(p[1], 10);
            if (p.length === 2 && have >= total) s = words('All unlocked.');
            else if (p.length === 2 && total > have) s = cell('Unlocked', String(have), '') + cell('Remaining', String(total - have), '');
        }
        return column(s);
    }
    function aboutOf(e) { return !e ? '' : e.kind === 'section' ? e.section.about : e.row && e.row.about ? e.row.about : ''; }
    function shareOf(e) { return !e ? 0 : e.kind === 'section' ? e.f : share(e.row); }

    // ---------------------------------------------------------------- drawing
    function target() { return expectQ.length ? expectQ[expectQ.length - 1].v : nativeSel; }
    function draw(open, inspect, mouse) {
        var k, b, on, hov, cls;
        var lit = tab >= 0 ? GAME_TABS + tab : target();
        for (k = 0; k < dom.buttons.length; k++) {
            b = dom.buttons[k]; on = k === lit; hov = mouse && hoverBtn === k;
            cls = BUTTON + (on ? ' menu-button--selected selection-item--selected' : '') + (hov ? ' menu-button--hovered selection-item--hovered' : '');
            setClass(b.root, cls);
            if (b.root.__hide !== inspect) { b.root.__hide = inspect; b.root.style.visibility = inspect ? 'hidden' : ''; }
            if (k < GAME_TABS) {
                var title = window['ui_narrative_object_menu_sub_tabs_tabs_' + k + '_title'];
                if (title && typeof title.translation === 'string' && title.translation) setText(b.label, title.translation);
                setClass(b.marker, 'new-marker new-marker--top-center' + (mv('ui_narrative_object_menu_sub_tabs_tabs_' + k + '_has_new', false) ? ' new-marker--visible' : ''));
            }
        }
        var ours = tab >= 0, game = !ours && !expectQ.length;
        show(dom.slot, game || !open, '');
        show(dom.page.root, ours && open, 'flex');
        if (!ours) { shown.tab = -1; return; }
        var list = entries[tab] || [];
        if (sel >= list.length) sel = Math.max(0, list.length - 1);
        if (shown.tab !== tab || shown.data !== data) {
            while (dom.page.rows.length < list.length) { var nr = makeRow(dom.page.rows.length); dom.page.rows.push(nr); dom.page.list.appendChild(nr.root); }
            for (k = 0; k < dom.page.rows.length; k++) {
                var row = dom.page.rows[k], e = list[k];
                show(row.root, !!e, 'flex');
                if (!e) continue;
                setText(row.name, e.label);
                var parts = e.kind === 'cat' && typeof e.counter === 'string' ? e.counter.split(' / ') : null;
                setHtml(row.counter, parts && parts.length === 2 ? '<div class="narrative-object-category-item__counter__collected">' + esc(parts[0]) + '</div>/<div>' + esc(parts[1]) + '</div>' : esc(e.counter));
            }
            shown.sel = -2;
        }
        if (shown.sel !== sel || shown.mouse !== mouse || shown.tab !== tab || shown.data !== data || shown.hover !== hoverRow) {
            for (k = 0; k < list.length; k++) {
                var rw = dom.page.rows[k], it = list[k], cur = k === sel, focus = cur && !mouse, hv = mouse && hoverRow === k;
                setClass(rw.root, ROW + (cur ? ' menu-button-box--selected category-list-item--selected' : '') +
                         (focus ? ' selection-item--selected' : '') + (hv ? ' selection-item--hovered' : ''));
                setClass(rw.stripes, 'menu-button-box__stripes' + (focus || hv ? ' menu-button-box__stripes--hidden' : ''));
                setClass(rw.underline, 'menu-button-box__underline' + (focus ? ' menu-button-box__underline--context-selected' : ''));
                setClass(rw.counter, 'narrative-object-category-item__counter narrative-object-category-item__counter--visible' +
                         (focus ? ' narrative-object-category-item__counter--selected' : '') + (hv ? ' narrative-object-category-item__counter--hovered' : '') +
                         (it.done && !focus && !hv ? ' narrative-object-category-item__counter--complete' : ''));
            }
            var en = list[sel];
            setText(dom.page.heading, en ? en.label + (en.counter ? '  ' + en.counter : '') : (data && data.note) || '');
            var w = (Math.round(shareOf(en) * 1000) / 10) + '%';
            if (dom.page.fill.__w !== w) { dom.page.fill.__w = w; dom.page.fill.style.width = w; }
            show(dom.page.bar, !(en && en.row && en.row.count), '');          // a count without a total has no bar
            setText(dom.page.about, aboutOf(en));
            setText(dom.page.note, !data ? 'Waiting for the numbers.' : data.note ? data.note : '');
            setHtml(dom.page.scroller, detailsOf(en));
        }
        shown.tab = tab; shown.sel = sel; shown.data = data; shown.mouse = mouse; shown.hover = hoverRow;
    }

    // ---------------------------------------------------------------- the game's list, switched off while our page is up
    // The game's category cells stay in its navigation, so the D-pad would walk its hidden list. They are marked
    // disabled for the time, with the values the game's own page gives them, and enabled again afterwards.
    function holdList(on) {
        if (typeof navigation === 'undefined' || !navigation || typeof navigation.setCell !== 'function') return;
        var ctx = mv('ui_menu_navigation_narrative_object_menu_id', 'narrative_object_menu'), size = mv('ui_narrative_object_menu_cell_size', null), k, n = 0;
        var w = size && size.x > 0 ? size.x : 1, h = size && size.y > 0 ? size.y : 1;
        // the list's own selection, to put back when the game returns to this tab of its own
        if (on && (!held || heldTab !== nativeSel)) { try { heldCurrent = navigation.current(ctx, 0); } catch (e) { heldCurrent = undefined; } heldTab = nativeSel; }
        for (k = 0; k < 150; k++) {
            var id = mv('ui_narrative_object_menu_categories_' + k + '_id', -1);
            if (typeof id !== 'number' || id < 0) continue;
            var unused = !!mv('ui_narrative_object_menu_categories_' + k + '_unused', true);
            if (on && unused) continue;
            navigation.setCell({ __Type: 'NavigationSetCellArguments', m_contextId: ctx, m_cellId: id, m_sortOrder: id, m_width: w, m_height: h, m_disabled: on ? true : unused, m_layout: 0 });
            n++;
        }
        if (on !== held) trace((on ? 'list held: ' : 'list released: ') + n + ' cells, current ' + heldCurrent);
        held = on;
    }

    // ---------------------------------------------------------------- which tab
    function sendTab(k) {                       // ask the game for its tab k; the change that follows is ours
        if (target() === k) return;
        expectQ.push({ v: k, at: now() });
        origTrigger.call(engine, 'narrative_object_sub_tab_click', k);
    }
    function setTab(t) {
        if (t === tab) return;
        trace('tab ' + tab + ' -> ' + t);
        tab = t;
        if (t >= 0) { sel = 0; scrollTop(); holdPasses = 6; refresh(); }
        else { releasePasses = 12; }
    }
    function clickButton(index) {
        var g = target();
        if (index >= GAME_TABS) {
            if (tab === index - GAME_TABS) return;
            setTab(index - GAME_TABS);
            // One change of the game's tab underneath, so the click sounds like a click on one of its own tabs.
            // Which of its tabs lies under ours does not matter.
            if (g >= 0) sendTab((g + 1) % GAME_TABS);
            return;
        }
        setTab(-1);
        sendTab(index);
    }
    // The game's tab changed from old to cur and nobody clicked: a tab key. dir = +1 next, -1 previous.
    // The game's three tabs and ours are one row; the key moves one step in it, around the ends.
    function key(dir, old) {
        var ring = GAME_TABS + N, pos = tab < 0 ? old : GAME_TABS + tab, np = (pos + dir + ring) % ring;
        if (np >= GAME_TABS) { setTab(np - GAME_TABS); return; }       // one of ours: the game stays where the key took it
        setTab(-1);
        sendTab(np);                                                   // does nothing when the game is there already
    }
    function nativeChanged(old, cur) {
        var t = now(), k;
        while (expectQ.length && t - expectQ[0].at > 700) expectQ.shift();
        if (cur < 0 || cur >= GAME_TABS) return;
        for (k = 0; k < expectQ.length; k++) if (expectQ[k].v === cur) { expectQ.splice(0, k + 1); return; }
        if (old < 0 || old >= GAME_TABS || t - openAt < 250) return;              // the screen is just opening
        if (clickArg === cur && t - clickAt < 600) { clickArg = -1; trace('game tab ' + old + ' -> ' + cur + ' (click)'); if (tab >= 0) setTab(-1); return; }
        var dir = cur === (old + 1) % GAME_TABS ? 1 : -1;
        trace('game tab ' + old + ' -> ' + cur + ' (key ' + (dir > 0 ? 'next' : 'previous') + ', ours ' + tab + ')');
        key(dir, old);
    }

    // ---------------------------------------------------------------- per frame
    function close() {
        if (tab >= 0) trace('closed with our tab ' + tab + ' open');
        tab = -1; expectQ = []; hoverBtn = -1; hoverRow = -1;
        if (held) holdList(false);
        releasePasses = 0; holdPasses = 0; heldTab = -1;
        if (dom) { show(dom.slot, true, ''); show(dom.page.root, false); }
    }
    function tick() {
        frame++;
        if (failed) return;
        if (!ready) {
            if (tries > 1800) return;
            tries++;
            var problem;
            try { problem = setup(); }
            catch (e) {                      // half way in: everything of ours out again, the game's buttons back
                if (dom) dom.swapped = true;
                restore(); failed = true; setStatus('no tabs: ' + e);
                return;
            }
            if (problem) { if (tries === 1800) { failed = true; setStatus('no tabs: ' + problem); } return; }
            ready = true; build(); setStatus('ready (script ' + API.version + ', after ' + tries + ' tries)');
        }
        if (!cfg.on) { if (dom.swapped) { restore(); ready = false; tries = 0; failed = true; setStatus('switched off'); } return; }
        var open = !!mv('ui_narrative_object_menu_is_open', false) && !!mv('ui_stacks_game_states_narrative_object_menu_active', false);
        if (!open) { if (wasOpen) { wasOpen = false; close(); trace('archives closed'); } flushTrace(); return; }
        var cur = mv('ui_narrative_object_menu_sub_tabs_selected', -1);
        if (!wasOpen) { wasOpen = true; openAt = now(); nativeSel = cur; trace('archives opened on tab ' + cur); refresh(); }
        if (cur !== nativeSel) { var old = nativeSel; nativeSel = cur; nativeChanged(old, cur); }
        while (expectQ.length && now() - expectQ[0].at > 700) { trace('the game did not go to tab ' + expectQ[0].v); expectQ.shift(); }
        if (cfg.hold) {
            if (tab >= 0) { if (holdPasses > 0 || frame % 30 === 0) { holdPasses--; holdList(true); } }
            else if (held || releasePasses > 0) { releasePasses--; holdList(false); if (releasePasses <= 0 && heldCurrent !== undefined && nativeSel === heldTab) restoreCurrent(); }
        } else if (held) holdList(false);
        var mouse = !!mv('ui_input_active_controller_menu_mouse', false);
        if (tab >= 0 && !mouse) stick();
        draw(true, !!mv('ui_stacks_game_states_narrative_object_inspect_current', false), mouse);
        flushTrace();
    }
    function restoreCurrent() {
        try {
            var ctx = mv('ui_menu_navigation_narrative_object_menu_id', 'narrative_object_menu');
            if (typeof heldCurrent === 'number' && heldCurrent >= 0 && navigation.current(ctx, 0) !== heldCurrent) navigation.setCurrent(ctx, heldCurrent, 0);
        } catch (e) { }
        heldCurrent = undefined;
    }
    // The right stick scrolls the details.
    function stick() {
        var v = mv('ui_input_gamepad_scroll_right_stick_vertical', 0);
        if (v && typeof v === 'object') v = typeof v.y === 'number' && v.y ? v.y : typeof v.x === 'number' ? v.x : 0;
        if (typeof v !== 'number' || !v) return;
        try { dom.page.scroller.scrollTop += v * 14; } catch (e) { }
    }
    function guard(f) {
        try { f(); }
        catch (e) {
            errors++; trace('error: ' + e);
            if (errors >= 5) { failed = true; restore(); setStatus('stopped after errors: ' + e); }
        }
    }
    function loop() { guard(tick); schedule(); }
    function schedule() {
        if (typeof requestAnimationFrame === 'function') requestAnimationFrame(loop); else setTimeout(loop, 16);
    }

    // ---------------------------------------------------------------- the game's events
    function hook() {
        origTrigger = engine.trigger;
        engine.trigger = function (name) {
            if (ready && !failed) {
                if (name === 'narrative_object_sub_tab_click') { clickArg = arguments[1]; clickAt = now(); }
                else if (tab >= 0 && QUIET[name] === 1) return undefined;
            }
            return origTrigger.apply(this, arguments);
        };
        if (typeof engine.on !== 'function') return;
        function move(d) {
            return function () {
                guard(function () {
                    if (tab < 0 || !wasOpen) return;
                    var list = entries[tab] || [], next = Math.max(0, Math.min(list.length - 1, sel + d));
                    if (next !== sel) { sel = next; scrollTop(); }
                    if (moves++ < 12) trace('list ' + (d > 0 ? 'down' : 'up') + ' -> row ' + sel);
                });
            };
        }
        engine.on('OnNavigateUp', move(-1));
        engine.on('OnNavigateDown', move(1));
    }

    API.state = function () { return { status: status, tab: tab, sel: sel, game: nativeSel, ready: ready, failed: failed, held: held, expect: expectQ.length, rows: (entries[tab] || []).length }; };
    API._tick = function () { guard(tick); };
    API.restore = restore;
    try { hook(); ask(''); schedule(); }
    catch (e) { failed = true; status = 'start failed: ' + e; try { ask(''); } catch (x) { } }
})();
