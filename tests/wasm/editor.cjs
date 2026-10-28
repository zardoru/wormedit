const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const http = require('node:http');
const path = require('node:path');
const {chromium} = require('playwright');

async function serve(directory) {
  const server = http.createServer(async (request, response) => {
    const name = new URL(request.url, 'http://localhost').pathname.slice(1) || 'index.html';
    if (!/^[\w.-]+$/.test(name)) { response.writeHead(404).end(); return; }
    try {
      if (!request.url.includes('no-isolation')) {
        response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
        response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
      }
      response.setHeader('Content-Type', {'.html': 'text/html', '.js': 'text/javascript',
        '.wasm': 'application/wasm'}[path.extname(name)] || 'application/octet-stream');
      response.end(await fs.readFile(path.join(directory, name)));
    } catch { response.writeHead(404).end(); }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  return {server, url: `http://127.0.0.1:${server.address().port}`};
}

async function main() {
  const {server, url} = await serve(path.resolve(process.argv[2] || 'build/wasm'));
  let browser, page;
  try {
    browser = await chromium.launch({executablePath: process.env.CHROMIUM || '/usr/bin/chromium',
      headless: true, args: ['--no-sandbox', '--use-angle=swiftshader', '--enable-unsafe-swiftshader']});
    page = await browser.newPage({viewport: {width: 1320, height: 900}, acceptDownloads: true});
    await page.addInitScript(() => {
      window.__wormeditAudio = {callbacks: 0, nonzero: 0, peak: 0, contexts: []};
      const NativeContext = window.AudioContext || window.webkitAudioContext;
      const createProcessor = NativeContext.prototype.createScriptProcessor;
      class ObservedContext extends NativeContext {
        constructor(...args) { super(...args); window.__wormeditAudio.contexts.push(this); }
        createScriptProcessor(...args) {
          const node = createProcessor.apply(this, args);
          let current, listener;
          Object.defineProperty(node, 'onaudioprocess', {
            get: () => current,
            set: handler => {
              if (listener) node.removeEventListener('audioprocess', listener);
              current = handler;
              listener = handler && (event => {
                handler.call(node, event);
                const audio = window.__wormeditAudio;
                ++audio.callbacks;
                let peak = 0;
                for (let channel = 0; channel < event.outputBuffer.numberOfChannels; ++channel)
                  for (const value of event.outputBuffer.getChannelData(channel)) peak = Math.max(peak, Math.abs(value));
                audio.peak = Math.max(audio.peak, peak);
                if (peak > 0.0001) ++audio.nonzero;
              });
              if (listener) node.addEventListener('audioprocess', listener);
            }
          });
          return node;
        }
      }
      window.AudioContext = ObservedContext;
      if (window.webkitAudioContext) window.webkitAudioContext = ObservedContext;
    });
    const errors = [];
    page.on('pageerror', error => { errors.push(String(error)); console.error('PAGE ERROR:', error); });
    page.on('console', message => console.log('BROWSER:', message.type(), message.text()));
    page.on('crash', () => console.error('PAGE CRASH'));
    await page.goto(url);
    await page.waitForFunction(() => window.wormedit?.editorStarted ||
      document.getElementById('browser-status').dataset.state === 'failed', null, {timeout: 90000});
    assert.equal(await page.locator('#browser-status').getAttribute('data-state'), 'ready');
    await page.waitForTimeout(2000);
    console.log("CANVAS:", await page.evaluate(() => ({frames: window.wormedit.editorFrames, rect: document.querySelector("canvas").getBoundingClientRect().toJSON(), size: [document.querySelector("canvas").width, document.querySelector("canvas").height], context: window.wormedit.ctx ? [window.wormedit.ctx.drawingBufferWidth, window.wormedit.ctx.drawingBufferHeight] : null})));
    await page.screenshot({path: process.env.WASM_EDITOR_SCREENSHOT || '/tmp/wormedit-browser-editor.png'});
    console.log('EDITOR STARTED:', browser.version());
    const canvas = page.locator('#canvas');
    async function click(x, y) {
      const bounds = await canvas.boundingBox();
      await page.mouse.click(bounds.x + x, bounds.y + y, {delay: 70});
      await page.waitForTimeout(200);
    }
    await click(640, 200);
    await page.screenshot({path: '/tmp/wormedit-browser-wizard.png'});
    await click(943, 512);
    await click(533, 55);
    await page.screenshot({path: '/tmp/wormedit-browser-compose.png'});
    console.log('CREATED PROJECT AND OPENED COMPOSE');
    const {unzipSync} = require(path.resolve(process.argv[2] || 'build/wasm', 'fflate.js'));
    async function save(first) {
      await canvas.focus();
      const received = page.waitForEvent('download', {timeout: 30000});
      await page.keyboard.press('Control+s');
      if (first) {
        await page.locator('#browser-destination').fill('browser-edit.wormedit');
        await page.locator('#browser-confirm').click();
      }
      const download = await received;
      assert.equal(await download.failure(), null);
      const bytes = await fs.readFile(await download.path());
      const files = unzipSync(bytes);
      const name = Object.keys(files).find(name => name.endsWith('.wormedit'));
      assert.ok(name, 'actual ZIP contains the native project');
      const document = JSON.parse(Buffer.from(files[name]).toString()).document;
      return {document, bytes, files};
    }
    const countNotes = document => document.charts.flatMap(chart => chart.measures).flatMap(measure => measure.notes).length;
    await page.keyboard.press('Alt+2');
    await click(490, 581);
    await click(538, 534);
    const drawn = await save(true);
    console.log('NOTES AFTER DRAW:', countNotes(drawn.document));
    assert.equal(countNotes(drawn.document), 2);
    await page.keyboard.press('Control+z');
    await page.waitForTimeout(200);
    const undone = await save(false);
    assert.equal(countNotes(undone.document), 1);
    await page.keyboard.press('Control+Shift+z');
    await page.waitForTimeout(200);
    const redone = await save(false);
    assert.equal(countNotes(redone.document), 2);
    await fs.writeFile('/tmp/wormedit-browser-project.zip', redone.bytes);
    console.log('PASS: creation, drawing, native ZIP save, undo, redo');
    await click(320, 55);
    await page.screenshot({path: '/tmp/wormedit-browser-metadata.png'});
    await click(500, 168);
    await page.keyboard.press('Control+a');
    await page.keyboard.type('Browser edited', {delay: 25});
    await click(1180, 256);
    const titled = await save(false);
    assert.equal(titled.document.metadata.title, 'Browser edited');
    console.log('PASS: real metadata text input and Apply');
    async function open(files) {
      await canvas.focus();
      await page.keyboard.press('Control+o');
      await page.locator('#browser-upload').waitFor({state: 'visible'});
      await page.locator('#browser-upload').setInputFiles(files);
      await page.locator('#browser-picker').waitFor({state: 'hidden'});
      await page.waitForTimeout(1000);
    }
    await open({name: 'roundtrip.zip', mimeType: 'application/zip', buffer: titled.bytes});
    const reopened = await save(false);
    assert.deepEqual(reopened.document, titled.document);
    console.log('PASS: project ZIP reopened through the actual file dialog and async importer');
    const {zipSync} = require(path.resolve(process.argv[2] || 'build/wasm', 'fflate.js'));
    await open({name: 'unsafe.zip', mimeType: 'application/zip',
      buffer: Buffer.from(zipSync({'../escape.wormedit': Buffer.from('{}')}))});
    await click(1245, 574);
    const rejected = await save(false);
    assert.deepEqual(rejected.document, titled.document);
    console.log('PASS: unsafe ZIP rejected without replacing current project');
    const source = (await fs.readFile('tests/fixtures/osu/versioned-stable-v14.osu', 'utf8'))
      .replace('[General]', '[General]\nAudioFilename: sliced-seek-source.ogg');
    await open([
      {name: 'audio.osu', mimeType: 'text/plain', buffer: Buffer.from(source)},
      {name: 'sliced-seek-source.ogg', mimeType: 'audio/ogg', buffer: await fs.readFile('tests/fixtures/audio/sliced-seek-source.ogg')}
    ]);
    await click(533, 55);
    await page.waitForTimeout(1000);
    await page.screenshot({path: '/tmp/wormedit-browser-audio.png'});
    const withAudio = await save(true);
    await fs.writeFile('/tmp/wormedit-browser-audio-project.zip', withAudio.bytes);
    assert.ok(withAudio.document.audio_assets.length > 0);
    console.log('OUTPUT GROUPS:', JSON.stringify(withAudio.document.output_groups));
    for (const asset of withAudio.document.audio_assets)
      assert.ok(withAudio.files[asset.source_path], 'saved project includes its referenced audio bytes');
    const before = await page.evaluate(() => window.__wormeditAudio.nonzero);
    await canvas.focus();
    await page.keyboard.press('Space');
    await page.waitForFunction(previous => window.__wormeditAudio.nonzero > previous, before, {timeout: 15000});
    await page.waitForTimeout(1200);
    const later = await page.evaluate(() => window.__wormeditAudio.nonzero);
    await page.waitForFunction(previous => window.__wormeditAudio.nonzero > previous, later, {timeout: 5000});
    const audio = await page.evaluate(() => ({...window.__wormeditAudio,
      contexts: window.__wormeditAudio.contexts.map(context => context.state)}));
    console.log('AUDIO:', audio);
    assert.ok(audio.contexts.includes('running'));
    await page.keyboard.press('Space');
    console.log('PASS: chart/audio import, media copied into project ZIP, actual WebAudio produces nonzero PCM beyond the initial note');
    await click(107, 55);
    await page.screenshot({path: '/tmp/wormedit-browser-export.png'});
    await click(230, 403);
    await page.screenshot({path: '/tmp/wormedit-browser-export-dialog.png'});
    await click(857, 637);
    await click(1137, 380);
    await page.waitForFunction(() => {
      const FS = window.wormedit.FS;
      function containsExport(directory) {
        return FS.readdir(directory).some(name => {
          if (name === '.' || name === '..') return false;
          const full = directory + '/' + name;
          return FS.isDir(FS.stat(full).mode) ? containsExport(full) : /\/exports\/.*\.osu$/i.test(full);
        });
      }
      return containsExport('/outputs');
    }, null, {timeout: 30000});
    await page.waitForTimeout(500);
    const exported = await save(false);
    const exportedChart = Object.keys(exported.files).find(name => /exports\/.*\.osu$/i.test(name));
    assert.ok(exportedChart);
    const chartText = Buffer.from(exported.files[exportedChart]).toString('utf8');
    assert.match(chartText, /osu file format v14/);
    assert.match(chartText, /\[HitObjects\]/);
    const exportedAudio = /AudioFilename\s*:\s*([^\r\n]+)/.exec(chartText)[1].trim();
    assert.ok(exported.files[path.posix.join(path.posix.dirname(exportedChart), exportedAudio)]);
    console.log('PASS: native export target creation, actual osu!mania chart export with media');
    const workspaceDownload = page.waitForEvent('download');
    await page.locator('#download-workspace').click();
    const workspace = await workspaceDownload;
    assert.equal(await workspace.failure(), null);
    const workspaceFiles = unzipSync(await fs.readFile(await workspace.path()));
    assert.ok(Object.keys(workspaceFiles).some(name => /outputs\/.*\/exports\/.*\.osu$/.test(name)));
    console.log('PASS: workspace ZIP retrieves staged export files');
    await canvas.focus();
    await page.keyboard.press('Control+o');
    await page.locator('#browser-upload').waitFor({state: 'visible'});
    await page.locator('#browser-cancel').click();
    await page.waitForTimeout(500);
    const cancelled = await save(false);
    assert.deepEqual(cancelled.document, exported.document);
    console.log('PASS: browser picker cancellation preserves current document');
    const acceptedUnload = page.waitForEvent('dialog');
    const navigation = page.reload();
    const warning = await acceptedUnload;
    assert.equal(warning.type(), 'beforeunload');
    await warning.accept();
    await navigation;
    await page.waitForFunction(() => window.wormedit?.editorStarted, null, {timeout: 90000});
    assert.equal(await page.evaluate(() => {
      const FS = window.wormedit.FS;
      return ['/imports', '/outputs'].every(directory => FS.readdir(directory).length === 2);
    }), true);
    console.log('PASS: reload warns and starts with an empty volatile workspace');
    await page.goto(url + '/storage.html');
    await page.waitForFunction(() => window.storageModule?.storageCheckPassed || window.storageModule?.storageCheckFailed,
      null, {timeout: 30000});
    assert.equal(await page.evaluate(() => window.storageModule.storageCheckPassed), true);
    console.log('PASS: concurrent publication and no-overwrite browser storage contract');
    await page.goto(url + '/index.html?no-isolation');
    await page.waitForFunction(() => document.getElementById('browser-status').dataset.state === 'failed');
    assert.match(await page.locator('#browser-status').textContent(), /cross-origin isolation/);
    console.log('PASS: missing isolation headers fail explicitly before startup');
    assert.deepEqual(errors, []);
  } catch (error) {
    if (page) await page.screenshot({path: '/tmp/wormedit-browser-failure.png'}).catch(() => {});
    throw error;
  } finally {
    if (browser) await browser.close();
    await new Promise(resolve => server.close(resolve));
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
