/* Browser-independent checks for setup controls and isolated backup requests. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

class Element {
    constructor() { this.value = ''; this.checked = false; this.disabled = false; this.files = []; this.children = []; this.handlers = {}; this.attributes = {}; }
    addEventListener(name, handler) { this.handlers[name] = handler; }
    append(...children) { this.children.push(...children); }
    replaceChildren(...children) { this.children = children; }
    setAttribute(name, value) { this.attributes[name] = value; }
    querySelectorAll() { return this.children; }
    focus() { this.focused = true; }
    click() { this.clicked = true; }
    remove() { this.removed = true; }
    async fire(name) { return this.handlers[name]?.({target: this, preventDefault() {}}); }
}

async function main() {
    const elements = Object.fromEntries(['setup', 'status', 'error', 'save', 'scan', 'scan_status', 'networks', 'ssid', 'password', 'open_network', 'username', 'interval', 'nonce', 'utc_offset', 'export', 'restore', 'restore_file', 'backup_status'].map(id => [id, new Element()]));
    const calls = [], timers = [], body = new Element();
    const nonce = '0123456789abcdef0123456789abcdef';
    const document = {body, querySelector: selector => elements[selector.slice(1)], getElementById: id => elements[id], createElement: () => new Element()};
    class FormData {
        constructor() {
            this.entries = ['ssid', 'password', 'username', 'interval', 'nonce', 'utc_offset']
                .filter(id => !elements[id].disabled).map(id => [id, elements[id].value]);
            if (elements.open_network.checked) this.entries.push(['open_network', '1']);
        }
        [Symbol.iterator]() { return this.entries[Symbol.iterator](); }
    }
    let restoreFail = false;
    const context = vm.createContext({document, FormData, URLSearchParams, Date,
        URL: {createObjectURL: () => 'blob:local-backup', revokeObjectURL() {}},
        setTimeout: (fn, ms) => { timers.push({fn, ms}); },
        fetch: async (url, options = {}) => {
            calls.push({url, options});
            if (url === '/api/state') return {ok: true, json: async () => ({ssid: 'Saved', username: 'octocat', interval: 6, nonce, open: false, pending: false, saved: false, message: 'Ready'})};
            if (url === '/api/networks') return {ok: true, json: async () => ({scanning: false, message: 'Ready', networks: [{ssid: 'Cafe', rssi: -30, open: true}, {ssid: '<Private>', rssi: -60, open: false}]})};
            if (url === '/api/backup') return {ok: true, blob: async () => ({size: 1234})};
            return {ok: !restoreFail, json: async () => ({message: restoreFail ? 'Invalid backup; existing data retained.' : 'Accepted'})};
        }});
    const html = fs.readFileSync(path.join(__dirname, '../main/badge_portal.html'), 'utf8');
    vm.runInContext(html.match(/<script>([\s\S]*?)<\/script>/)[1], context);
    const settle = async () => { for (let i = 0; i < 8; ++i) await new Promise(resolve => setImmediate(resolve)); };
    await settle();
    assert.equal(elements.ssid.value, 'Saved');
    assert.equal(calls.find(call => call.url === '/api/scan').options.headers['X-Setup-Nonce'], nonce);
    assert.equal(elements.networks.children.length, 2);
    assert.equal(elements.networks.children[1].children[0].textContent, '<Private>');

    elements.password.value = 'do-not-reuse';
    await elements.networks.children[0].fire('click');
    assert.equal(elements.ssid.value, 'Cafe');
    assert.equal(elements.password.value, '');
    assert.equal(elements.open_network.checked, true);
    assert.equal(elements.password.disabled, true);
    await elements.networks.children[1].fire('click');
    assert.equal(elements.open_network.checked, false);
    assert.equal(elements.password.disabled, false);
    await elements.networks.children[0].fire('click');
    elements.ssid.value = 'Hidden';
    await elements.ssid.fire('input');
    assert.equal(elements.open_network.checked, false);

    const file = {size: 2048, name: 'backup.bin'};
    elements.restore_file.files = [file];
    await elements.restore_file.fire('change');
    restoreFail = true;
    const before = calls.length;
    await elements.restore.fire('click');
    assert.equal(calls.length, before + 1);
    assert.equal(calls.at(-1).url, '/api/restore');
    assert.equal(calls.at(-1).options.body, file);
    assert.equal(calls.at(-1).options.headers['X-Setup-Nonce'], nonce);
    assert.equal(elements.ssid.value, 'Hidden');
    assert.match(elements.backup_status.textContent, /existing data retained/);
    assert.equal(elements.save.disabled, false);
    assert.equal(calls.some(call => call.url === '/api/config'), false);

    restoreFail = false;
    await elements.export.fire('click');
    assert.equal(calls.at(-1).url, '/api/backup');
    assert.equal(body.children.at(-1).download, 'github-badge-backup.bin');
    assert.equal(body.children.at(-1).clicked, true);
    await elements.restore.fire('click');
    assert.equal(elements.save.disabled, true);
    assert.equal(elements.restore.disabled, true);
    assert(timers.some(timer => timer.ms === 3500));
    assert.equal(calls.some(call => call.url === '/api/config'), false);
    console.log('Badge portal UI scan/backup isolation tests: PASS');
}
main().catch(error => { console.error(error); process.exitCode = 1; });
