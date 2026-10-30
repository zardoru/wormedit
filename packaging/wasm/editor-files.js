'use strict';

function installWormeditBrowserFiles(module, report) {
  const FS = module.FS;
  const picker = document.getElementById('browser-picker');
  const input = document.getElementById('browser-upload');
  const folder = document.getElementById('browser-folder');
  const destination = document.getElementById('browser-destination');
  const entry = document.getElementById('browser-entry');
  const confirm = document.getElementById('browser-confirm');
  const maxFile = 64 * 1024 * 1024, maxTotal = 256 * 1024 * 1024, maxFiles = 4096;
  let pending, sequence = 0, importedBytes = 0;
  let interacted = false;
  module.canvas.addEventListener('pointerdown', () => { interacted = true; });
  module.canvas.addEventListener('keydown', () => { interacted = true; });
  window.addEventListener('beforeunload', event => {
    if (interacted || importedBytes) { event.preventDefault(); event.returnValue = ''; }
  });
  FS.mkdirTree('/imports');
  FS.mkdirTree('/outputs');

  function relativePath(name) {
    const parts = name.replaceAll('\\', '/').split('/');
    if (!name || name.startsWith('/') || parts.some(part => part === '..' || part.includes(':') || part.includes('\0')))
      throw new Error('Unsafe archive or file path. Current project unchanged.');
    return parts.filter(part => part && part !== '.').join('/');
  }

  function complete(paths, error) {
    if (!pending) return;
    const token = pending.token;
    pending = null;
    picker.hidden = true;
    module.ccall('wormedit_browser_dialog_complete', null, ['number', 'string', 'number'],
      [token, error || JSON.stringify(paths), error ? -1 : 1]);
    module.canvas.focus();
    if (error) report(error, 'failed');
  }

  function showDialog(token, kind, location, multiple) {
    if (pending) {
      module.ccall('wormedit_browser_dialog_complete', null, ['number', 'string', 'number'],
        [token, 'Finish the current browser file operation first.', -1]);
      return;
    }
    pending = {token, kind, multiple};
    picker.hidden = false;
    input.value = '';
    input.disabled = false;
    folder.checked = false;
    input.webkitdirectory = false;
    entry.hidden = true;
    entry.replaceChildren();
    destination.value = location.split(/[\\/]/).pop() || (kind === 2 ? 'export' : 'Untitled.wormedit');
    document.getElementById('picker-title').textContent = kind === 0 ? 'Open in editor' :
      kind === 1 ? 'Save / export to browser workspace' : 'Choose a browser output folder';
    document.getElementById('upload-label').hidden = kind !== 0;
    document.getElementById('folder-label').hidden = kind !== 0;
    document.getElementById('destination-label').hidden = kind === 0;
    confirm.hidden = kind === 0;
  }

  function chooseEntry(paths) {
    if (pending.multiple) return complete(paths);
    const native = paths.filter(path => /\.wormedit$/i.test(path));
    const charts = paths.filter(path => /\.(bms|bme|bml|pms|bmson|sm|ssc|osu)$/i.test(path));
    const choices = native.length ? native : charts.length ? charts : paths;
    if (choices.length === 1) return complete(choices);
    entry.replaceChildren(...choices.map(path => new Option(path.replace(/^\/imports\/selection-\d+\//, ''), path)));
    entry.hidden = false;
    confirm.hidden = false;
    report('Choose which project or chart to open. All selected companion files are available.', 'ready');
  }

  async function upload(files) {
    if (!pending || !files.length) return;
    const request = pending;
    input.disabled = true;
    try {
      const extracted = Object.create(null);
      let total = 0;
      for (const file of files) {
        if (file.size > maxFile) throw new Error('A selected file exceeds 64 MiB. Current project unchanged.');
        const bytes = new Uint8Array(await file.arrayBuffer());
        if (pending !== request) return;
        if (/\.zip$/i.test(file.name)) {
          const seen = new Set();
          const archive = fflate.unzipSync(bytes, {filter: item => {
            const name = relativePath(item.name);
            if (seen.has(name) || seen.size >= maxFiles) throw new Error('Duplicate path or too many ZIP entries.');
            seen.add(name);
            total += item.originalSize;
            if (item.originalSize > maxFile || total > maxTotal) throw new Error('ZIP exceeds the browser upload budget.');
            return !item.name.endsWith('/');
          }});
          for (const [name, data] of Object.entries(archive)) {
            const path = relativePath(name);
            if (extracted[path]) throw new Error('Duplicate upload path: ' + path);
            extracted[path] = data;
          }
        } else {
          const path = relativePath(file.webkitRelativePath || file.name);
          if (extracted[path]) throw new Error('Duplicate upload path: ' + path);
          extracted[path] = bytes;
          total += bytes.length;
        }
        if (Object.keys(extracted).length > maxFiles || total > maxTotal || importedBytes + total > maxTotal)
          throw new Error('Browser upload budget exceeded (256 MiB / 4096 files). Download and reload to begin a new session.');
      }
      const root = '/imports/selection-' + (++sequence);
      const paths = [];
      for (const [relative, bytes] of Object.entries(extracted)) {
        const path = root + '/' + relative;
        FS.mkdirTree(path.slice(0, path.lastIndexOf('/')));
        FS.writeFile(path, bytes);
        paths.push(path);
      }
      importedBytes += total;
      chooseEntry(paths);
    } catch (error) { if (pending === request) complete([], String(error)); }
    finally { if (pending === request) input.disabled = false; }
  }

  function downloadTree(roots, name) {
    const archive = Object.create(null);
    let total = 0;
    function collect(directory, prefix) {
      for (const name of FS.readdir(directory)) {
        if (name === '.' || name === '..') continue;
        if (name.includes('.wormedit-tmp-') || name.startsWith('.wormedit-slice-stage-')) continue;
        const path = directory + '/' + name, relative = prefix + name;
        if (FS.isDir(FS.stat(path).mode)) collect(path, relative + '/');
        else {
          const bytes = FS.readFile(path);
          total += bytes.length;
          if (total > maxTotal || Object.keys(archive).length >= maxFiles)
            throw new Error('Download exceeds the browser ZIP budget. Session files remain available.');
          archive[relative] = bytes;
        }
      }
    }
    for (const [root, prefix] of roots) collect(root, prefix);
    const bytes = fflate.zipSync(archive, {level: 1});
    const url = URL.createObjectURL(new Blob([bytes], {type: 'application/zip'}));
    const anchor = document.createElement('a');
    anchor.href = url; anchor.download = name; anchor.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    report('ZIP download requested. Confirm completion in your browser; the workspace is temporary.', 'ready');
  }

  function downloadProject(path) {
    try { downloadTree([[path.slice(0, path.lastIndexOf('/')), '']], path.split('/').pop().replace(/\.wormedit$/i, '') + '.zip'); }
    catch (error) { report('Download failed: ' + String(error), 'failed'); }
  }

  input.addEventListener('change', () => upload([...input.files]));
  folder.addEventListener('change', () => { input.value = ''; input.webkitdirectory = folder.checked; });
  document.getElementById('browser-cancel').addEventListener('click', () => complete([]));
  confirm.addEventListener('click', () => {
    if (!pending) return;
    if (pending.kind === 0) return complete([entry.value]);
    try {
      const name = relativePath(destination.value.trim());
      if (!name || name.includes('/')) throw new Error('Enter a filename or folder name, without path separators.');
      const root = '/outputs/save-' + (++sequence);
      FS.mkdirTree(root);
      const path = root + '/' + name;
      if (pending.kind === 2) FS.mkdirTree(path);
      complete([path]);
    } catch (error) { report(String(error), 'failed'); }
  });
  document.getElementById('download-workspace').addEventListener('click', () => {
    try { downloadTree([['/outputs', 'outputs/'], ['/imports', 'imports/']], 'wormedit-workspace.zip'); }
    catch (error) { report(String(error), 'failed'); }
  });
  module.browserFiles = {showDialog, downloadProject};
}
