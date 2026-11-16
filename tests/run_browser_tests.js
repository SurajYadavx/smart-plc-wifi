/**
 * Test Runner for web/test.html
 * Executes all 156 browser verification tests in Node.js with mock DOM
 */
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const htmlPath = path.join(__dirname, '..', 'web', 'test.html');
const html = fs.readFileSync(htmlPath, 'utf8');
const match = html.match(/<script>([\s\S]*?)<\/script>/i);
if (!match) {
  console.error('Could not find <script> block in web/test.html');
  process.exit(1);
}

const mockEl = {
  appendChild: () => {},
  classList: { add: () => {} },
  style: {},
  set innerHTML(v) {},
  set textContent(v) {}
};

const sandbox = {
  console,
  document: {
    getElementById: () => mockEl,
    createElement: () => ({ ...mockEl })
  },
  window: {}
};

vm.createContext(sandbox);
const code = match[1].replace('let passed = 0, failed = 0, skipped = 0;', 'var passed = 0, failed = 0, skipped = 0;');
vm.runInContext(code, sandbox);

console.log(`[Browser Tests] Result: ${sandbox.passed} PASS, ${sandbox.failed} FAIL, ${sandbox.skipped} SKIP`);
if (sandbox.failed > 0) {
  process.exit(1);
}
