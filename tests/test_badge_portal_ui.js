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
    removeAttribute(name) { delete this.attributes[name]; if (name === 'href') delete this.href; }
    async fire(name) { return this.handlers[name]?.({target: this, preventDefault() {}}); }
}

async function main() {
    const elements = Object.fromEntries(['setup', 'status', 'error', 'save', 'scan', 'scan_status', 'networks', 'ssid', 'password', 'open_network', 'username', 'interval', 'nonce', 'utc_offset', 'export', 'backup_download', 'restore', 'restore_file', 'backup_status'].map(id => [id, new Element()]));
    const calls = [], timers = [], body = new Element(), urls = new Map(), revoked = [], windowHandlers = {};
    let urlSerial = 0, backupMode = 'ok';
    const backupBytes = Buffer.concat([Buffer.from('GHBK0001'), Buffer.alloc(1226)]);
    const window = {addEventListener(name, fn) { windowHandlers[name] = fn; }};
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
    const context = vm.createContext({document, window, FormData, URLSearchParams, Date,
        URL: {createObjectURL: blob => { const url = `blob:local-backup-${++urlSerial}`; urls.set(url, blob); return url; },
            revokeObjectURL: url => { revoked.push(url); urls.delete(url); }},
        setTimeout: (fn, ms) => { timers.push({fn, ms}); },
        fetch: async (url, options = {}) => {
            calls.push({url, options});
            if (url === '/api/state') return {ok: true, json: async () => ({ssid: 'Saved', username: 'octocat', interval: 6, nonce, open: false, pending: false, saved: false, message: 'Ready'})};
            if (url === '/api/networks') return {ok: true, json: async () => ({scanning: false, message: 'Ready', networks: [{ssid: 'Cafe', rssi: -30, open: true}, {ssid: '<Private>', rssi: -60, open: false}]})};
            if (url === '/api/backup') {
                if (backupMode === 'network') throw new Error('Network disconnected');
                if (backupMode === 'server') return {ok: false, json: async () => ({message: 'CACHE INCOMPLETE OR CORRUPT'})};
                if (backupMode === 'interrupted') return {ok: true, blob: async () => { throw new Error('Incomplete response'); }};
                const bytes = backupMode === 'empty' ? Buffer.alloc(0) : backupMode === 'oversized' ? Buffer.alloc(128 * 1024 + 1) :
                    backupMode === 'html' ? Buffer.from('<html>Reconnect to setup Wi-Fi</html>') : backupBytes;
                return {ok: true, blob: async () => new Blob([bytes], {type: 'application/octet-stream'})};
            }
            return {ok: !restoreFail, json: async () => ({message: restoreFail ? 'Invalid backup; existing data retained.' : 'Accepted'})};
        }});
    const html = fs.readFileSync(path.join(__dirname, '../main/badge_portal.html'), 'utf8');
    const downloadTag = html.match(/<a id="backup_download"[^>]*>/)?.[0];
    assert(downloadTag, 'The prepared backup needs a persistent, user-clickable save link');
    elements.backup_download.download = downloadTag.match(/download="([^"]+)"/)[1];
    elements.backup_download.hidden = /\bhidden\b/.test(downloadTag);
    assert.equal(elements.backup_download.hidden, true);
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
    assert.equal(calls.at(-1).options.headers['X-Setup-Nonce'], nonce);
    assert.equal(calls.at(-1).options.cache, 'no-store');
    const download = elements.backup_download;
    // Simulate a browser ignoring script-triggered click(): no saved-file event
    // exists. The real user must still have a live link, without another fetch.
    assert.equal(download.hidden, false);
    assert.equal(download.download, 'github-badge-backup.bin');
    assert.equal(download.clicked, true);
    assert.equal(download.removed, undefined);
    assert.equal(urls.size, 1);
    assert.deepEqual(Buffer.from(await urls.get(download.href).arrayBuffer()), backupBytes);
    assert.match(elements.backup_status.textContent, /SAVE BACKUP/);
    assert.doesNotMatch(elements.backup_status.textContent, /Backup downloaded/i);
    const beforeSave = calls.length, firstUrl = download.href;
    await download.fire('click');
    assert.equal(calls.length, beforeSave);
    assert.equal(download.href, firstUrl);
    assert.equal(urls.has(firstUrl), true);
    assert.equal(timers.some(timer => timer.ms === 60000), false); // Do not expire a visible save link.
    await elements.export.fire('click');
    assert(revoked.includes(firstUrl));
    assert.equal(urls.size, 1); // Re-export releases the earlier password-bearing Blob.
    for (const mode of ['server', 'network', 'interrupted', 'empty', 'oversized', 'html']) {
        backupMode = mode;
        await elements.export.fire('click');
        assert.equal(download.hidden, true, mode);
        assert.equal(urls.size, 0, mode);
        assert.equal(download.href, undefined, mode);
        assert.equal(elements.save.disabled, false, mode);
        assert.equal(elements.export.disabled, false, mode);
        assert.doesNotMatch(elements.backup_status.textContent, /Backup downloaded/i);
    }
    backupMode = 'ok';
    await elements.export.fire('click');
    const lastUrl = download.href;
    windowHandlers.pagehide();
    assert(revoked.includes(lastUrl));
    assert.equal(download.hidden, true);
    assert.equal(urls.size, 0);
    await elements.export.fire('click');
    await elements.restore.fire('click');
    assert.equal(download.hidden, true);
    assert.equal(urls.size, 0);
    assert.equal(elements.save.disabled, true);
    assert.equal(elements.restore.disabled, true);
    assert(timers.some(timer => timer.ms === 3500));
    assert.equal(calls.some(call => call.url === '/api/config'), false);
    console.log('Badge portal UI scan/backup isolation tests: PASS');
}

async function setupAcknowledgement(initiallySaved = false) {
    const ids = ['setup', 'status', 'error', 'save', 'scan', 'scan_status', 'networks', 'ssid', 'password', 'open_network', 'username', 'interval', 'nonce', 'utc_offset', 'export', 'backup_download', 'restore', 'restore_file', 'backup_status'];
    const elements = Object.fromEntries(ids.map(id => [id, new Element()]));
    const nonce = '0123456789abcdef0123456789abcdef';
    const state = {ssid: 'Saved', username: 'octocat', interval: 6, nonce, open: false, pending: false, saved: initiallySaved, saved_result_id: 1, message: 'Ready'};
    const calls = [];
    let ackFails = !initiallySaved, offline = false, replaceResultDuringAck = false;
    const context = vm.createContext({
        document: {querySelector: selector => elements[selector.slice(1)], getElementById: id => elements[id], createElement: () => new Element()},
        window: {addEventListener() {}}, Date, URLSearchParams, URL: {}, setTimeout() {},
        FormData: class { [Symbol.iterator]() { return Object.entries({ssid: 'New network', password: '', username: 'octocat', interval: '6', nonce})[Symbol.iterator](); } },
        fetch: async (url, options = {}) => {
            calls.push({url, options});
            if (offline) throw new Error('Hotspot closed');
            if (url === '/api/state') return {ok: true, json: async () => ({...state})};
            if (url === '/api/networks') return {ok: true, json: async () => ({scanning: false, message: 'Ready', networks: []})};
            if (url === '/api/ack') {
                assert.equal(options.method, 'POST');
                assert.equal(options.headers['X-Setup-Nonce'], nonce);
                assert.equal(options.headers['X-Setup-Result'], String(state.saved_result_id));
                assert.equal(options.body, undefined);
                assert.match(elements.status.textContent, /^Saved\. Wi-Fi connected\./, 'Show the received success before acknowledging shutdown');
                assert.equal(elements.save.disabled, true);
                if (replaceResultDuringAck) {
                    // Another tab completes a later save before this delayed
                    // ACK is checked; the server rejects the earlier result ID.
                    replaceResultDuringAck = false;
                    ++state.saved_result_id;
                    return {ok: false, status: 409};
                }
                if (ackFails) throw new Error('Lost acknowledgement response');
            }
            return {ok: true, json: async () => ({message: 'Testing Wi-Fi'})};
        }
    });
    const html = fs.readFileSync(path.join(__dirname, '../main/badge_portal.html'), 'utf8');
    vm.runInContext(html.match(/<script>([\s\S]*?)<\/script>/)[1], context);
    const settle = async () => { for (let i = 0; i < 8; ++i) await new Promise(resolve => setImmediate(resolve)); };
    await settle();
    if (!initiallySaved) {
        assert.equal(calls.some(call => call.url === '/api/ack'), false);
        await elements.setup.fire('submit');
        state.pending = true;
        await vm.runInContext('refresh()', context);
        assert.equal(calls.some(call => call.url === '/api/ack'), false, 'HTTP 202 and a pending save do not acknowledge success');
        state.pending = false;
        state.message = 'Settings NOT saved. Previous settings retained; retry';
        await vm.runInContext('refresh()', context);
        assert.equal(elements.save.disabled, false);
        assert.equal(calls.some(call => call.url === '/api/ack'), false, 'Failed persistence keeps setup open');
        state.saved = true;
        await vm.runInContext('refresh()', context);
        assert.equal(calls.filter(call => call.url === '/api/ack').length, 1);
        assert.match(elements.status.textContent, /^Saved\./, 'A lost ACK response must not turn a saved result into a failure');
        ackFails = false;
        await vm.runInContext('refresh()', context);
        assert.equal(calls.filter(call => call.url === '/api/ack').length, 2, 'Retry a lost acknowledgement');
    } else {
        assert.equal(calls.filter(call => call.url === '/api/ack').length, 1, 'A reloaded page can acknowledge an already-saved result');
        assert.equal(calls.some(call => call.url === '/api/scan'), false);
    }
    const ackCount = calls.filter(call => call.url === '/api/ack').length;
    await vm.runInContext('refresh()', context);
    assert.equal(calls.filter(call => call.url === '/api/ack').length, ackCount);
    const before = calls.length;
    await elements.setup.fire('submit');
    await elements.export.fire('click');
    assert.equal(calls.length, before, 'Do not start another operation during successful shutdown');
    state.saved = false;
    state.saved_result_id = 0;
    state.pending = true;
    state.message = 'Testing a newer submission';
    await vm.runInContext('refresh()', context);
    assert.equal(calls.filter(call => call.url === '/api/ack').length, ackCount, 'A newer pending submission invalidates the earlier success');
    assert.equal(elements.status.textContent, state.message);
    assert.equal(elements.save.disabled, true);
    state.pending = false;
    state.message = 'The newer settings were not saved';
    await vm.runInContext('refresh()', context);
    assert.equal(elements.save.disabled, false, 'The earlier success must not permanently disable this tab');
    state.saved = true;
    state.saved_result_id = 2;
    await vm.runInContext('refresh()', context);
    assert.equal(calls.filter(call => call.url === '/api/ack').length, ackCount + 1, 'A later successful submission needs its own acknowledgement');
    state.saved_result_id = 3;
    replaceResultDuringAck = true;
    await vm.runInContext('refresh()', context);
    assert.equal(calls.filter(call => call.url === '/api/ack').at(-1).options.headers['X-Setup-Result'], '3');
    await vm.runInContext('refresh()', context);
    assert.equal(calls.filter(call => call.url === '/api/ack').at(-1).options.headers['X-Setup-Result'], '4', 'Do not reuse an old ACK across saves within the same setup session');
    const validAckCount = calls.filter(call => call.url === '/api/ack').length;
    for (const invalid of [undefined, 0, -1, 1.5, 4294967296, '4']) {
        state.saved_result_id = invalid;
        await vm.runInContext('refresh()', context);
    }
    assert.equal(calls.filter(call => call.url === '/api/ack').length, validAckCount, 'Do not send acknowledgements without a valid result ID');
    offline = true;
    for (let i = 0; i < 3; ++i) await vm.runInContext('refresh()', context);
    assert.match(elements.status.textContent, /hotspot closed.*badge is ready/i);
}

(async () => {
    await main();
    await setupAcknowledgement();
    await setupAcknowledgement(true);
    console.log('Badge portal saved-result acknowledgement tests: PASS');
})().catch(error => { console.error(error); process.exitCode = 1; });
