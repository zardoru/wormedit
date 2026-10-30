// Run with Playwright installed separately; see docs/building.md. No browser download required.
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const {chromium} = require('playwright');

async function serve(directory, isolated) {
  const server = http.createServer(async (request, response) => {
    try {
      const name = new URL(request.url, 'http://localhost').pathname.slice(1) || 'index.html';
      if (!['index.html', 'wormedit-wasm-smoke.js', 'wormedit-wasm-smoke.wasm',
            'wormedit-wasm-smoke.data', 'wormedit-wasm-smoke.worker.js'].includes(name)) {
        response.writeHead(404).end();
        return;
      }
      if (isolated) {
        response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
        response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
      }
      const type = {'.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm'};
      response.setHeader('Content-Type', type[path.extname(name)] || 'application/octet-stream');
      response.end(await fs.readFile(path.join(directory, name)));
    } catch {
      response.writeHead(500).end();
    }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  return {server, url: `http://127.0.0.1:${server.address().port}`};
}

async function download(page) {
  const pending = page.waitForEvent('download');
  await page.locator('#download').click();
  const result = await pending;
  assert.equal(await result.failure(), null);
  return fs.readFile(await result.path(), 'utf8');
}

async function upload(page, bytes) {
  await page.locator('#open').setInputFiles({
    name: 'project.wormedit', mimeType: 'application/json', buffer: Buffer.from(bytes)
  });
  await page.waitForFunction(() => !document.getElementById('open').disabled);
}

async function main() {
  const directory = path.resolve(process.argv[2] || 'build/wasm-smoke');
  const good = await serve(directory, true);
  const unsupported = await serve(directory, false);
  let browser;
  let temporary;
  try {
    temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'wormedit-wasm-smoke-'));
    browser = await chromium.launch({
      executablePath: process.env.CHROMIUM || '/usr/bin/chromium', headless: true,
      args: ['--no-sandbox'] // Isolated cloud container; never uses a desktop profile.
    });
    const page = await browser.newPage({acceptDownloads: true});
    const errors = [];
    page.on('pageerror', error => errors.push(String(error)));
    await page.goto(good.url);
    await page.waitForFunction(() => document.getElementById('status').dataset.state !== 'loading');
    assert.equal(await page.locator('#status').getAttribute('data-state'), 'ready');
    const initial = await download(page);
    const project = JSON.parse(initial);
    assert.ok(project.document.charts.length > 0, 'sample has an authored chart');
    assert.ok(project.document.charts.some(chart => chart.measures.some(measure => measure.notes.length)),
      'sample exercises note persistence');
    await upload(page, initial);
    assert.equal(await page.locator('#status').getAttribute('data-state'), 'ready');
    assert.equal(await download(page), initial, 'download/re-upload retains canonical bytes');
    project.document.metadata.title = 'WASM 曲 🎵';
    await upload(page, JSON.stringify(project));
    const unicode = await download(page);
    assert.equal(JSON.parse(unicode).document.metadata.title, 'WASM 曲 🎵');
    for (const invalid of ['{', Buffer.from([0xff]), JSON.stringify({...project, format_version: 999}),
                           JSON.stringify({...project, document: {...project.document, id: 0}})]) {
      await upload(page, invalid);
      assert.equal(await page.locator('#status').getAttribute('data-state'), 'rejected');
      assert.equal(await download(page), unicode, 'rejection preserves the complete current document');
    }
    // File.size must be checked before reading or passing oversized bytes to WebAssembly.
    // Playwright limits in-memory uploads to 50 MiB; use a sparse file for this boundary.
    const oversized = path.join(temporary, 'large.wormedit');
    const output = await fs.open(oversized, 'w');
    await output.truncate(64 * 1024 * 1024 + 1);
    await output.close();
    await page.locator('#open').setInputFiles(oversized);
    await page.waitForFunction(() => !document.getElementById('open').disabled);
    assert.equal(await page.locator('#status').getAttribute('data-state'), 'rejected');
    assert.equal(await download(page), unicode);
    await page.reload();
    await page.waitForFunction(() => document.getElementById('status').dataset.state !== 'loading');
    assert.equal(await download(page), initial, 'reload has no implicit persistence');
    if (process.env.WASM_SCREENSHOT) await page.screenshot({path: process.env.WASM_SCREENSHOT});
    assert.deepEqual(errors, []);
    await page.goto(unsupported.url);
    await page.waitForFunction(() => document.getElementById('status').dataset.state === 'failed');
    assert.match(await page.locator('#status').textContent(), /Cross-origin isolation/);
    assert.equal(await page.locator('#download').isDisabled(), true);
    console.log(`PASS: ${browser.version()} startup, real download/upload, Unicode, rejection atomicity, size limit, reload and missing isolation`);
  } finally {
    if (browser) await browser.close();
    await Promise.all([good, unsupported].map(({server}) => new Promise(resolve => server.close(resolve))));
    if (temporary) await fs.rm(temporary, {recursive: true, force: true});
  }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
