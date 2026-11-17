/**
 * Test Runner for web/test.html
 * Executes all browser verification tests in Node.js with mock DOM
 * Loads real production app.js to test actual selectStation() and control safety logic
 */
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const appPath = path.join(__dirname, '..', 'web', 'assets', 'js', 'app.js');
const appCode = fs.readFileSync(appPath, 'utf8');

const htmlPath = path.join(__dirname, '..', 'web', 'test.html');
const html = fs.readFileSync(htmlPath, 'utf8');
const match = html.match(/<script>([\s\S]*?)<\/script>/i);
if (!match) {
  console.error('Could not find <script> block in web/test.html');
  process.exit(1);
}

function createMockElement(tag = 'div') {
  const el = {
    tagName: tag.toUpperCase(),
    value: '',
    disabled: false,
    _innerHTML: '',
    options: [],
    style: {},
    dataset: {},
    classList: {
      _classes: new Set(),
      add(...classes) { classes.forEach(c => this._classes.add(c)); },
      remove(...classes) { classes.forEach(c => this._classes.delete(c)); },
      toggle(c, force) {
        if (force === undefined) force = !this._classes.has(c);
        if (force) this._classes.add(c); else this._classes.delete(c);
        return force;
      },
      contains(c) { return this._classes.has(c); },
      toString() { return Array.from(this._classes).join(' '); }
    },
    get className() { return this.classList.toString(); },
    set className(val) {
      this.classList._classes = new Set((val || '').split(/\s+/).filter(Boolean));
    },
    textContent: '',
    _listeners: {},
    addEventListener(evt, fn) {
      if (!this._listeners[evt]) this._listeners[evt] = [];
      this._listeners[evt].push(fn);
    },
    dispatchEvent(event) {
      const type = event.type || event;
      if (this._listeners[type]) {
        this._listeners[type].forEach(fn => fn(event));
      }
    },
    appendChild(child) { return child; },
    setAttribute(k, v) { this[k] = v; },
    getAttribute(k) { return this[k] !== undefined ? this[k] : null; },
    querySelector() { return null; },
    querySelectorAll() { return []; },
    get innerHTML() { return this._innerHTML; },
    set innerHTML(html) {
      this._innerHTML = html;
      this.options = [];
      const regex = /<option\s+value="([^"]*)"([^>]*)>([\s\S]*?)<\/option>/gi;
      let m;
      while ((m = regex.exec(html)) !== null) {
        const val = m[1];
        const isSel = /selected/i.test(m[2]);
        this.options.push({ value: val, text: m[3], selected: isSel });
        if (isSel) this.value = val;
      }
      if (this.options.length > 0 && !this.value) {
        this.value = this.options[0].value;
      }
    }
  };
  return el;
}

const elementCache = {};
function getOrCreateElement(id) {
  if (!elementCache[id]) {
    elementCache[id] = createMockElement(id.includes('select') ? 'select' : (id.includes('btn') ? 'button' : 'div'));
  }
  return elementCache[id];
}

const sandbox = {
  console,
  document: {
    getElementById: (id) => getOrCreateElement(id),
    createElement: (tag) => createMockElement(tag),
    addEventListener: () => {},
    querySelectorAll: () => [],
    querySelector: () => null,
  },
  window: {
    location: { protocol: 'http:', hostname: '192.168.3.238', origin: 'http://192.168.3.238' }
  },
  localStorage: {
    _data: {},
    getItem(k) { return this._data[k] !== undefined ? this._data[k] : null; },
    setItem(k, v) { this._data[k] = String(v); },
    removeItem(k) { delete this._data[k]; },
    clear() { this._data = {}; }
  },
  performance: { now: () => Date.now() },
  Event: class { constructor(type) { this.type = type; } },
  AbortController: global.AbortController,
  fetch: global.fetch
};

vm.createContext(sandbox);

// 1. Run real production app.js to define production code
vm.runInContext(appCode, sandbox);

// 2. Run test.html test suite
const code = match[1].replace('let passed = 0, failed = 0, skipped = 0;', 'var passed = 0, failed = 0, skipped = 0;');
vm.runInContext(code, sandbox);

console.log(`[Browser Tests] Result: ${sandbox.passed} PASS, ${sandbox.failed} FAIL, ${sandbox.skipped} SKIP`);
if (sandbox.failed > 0) {
  process.exit(1);
}
