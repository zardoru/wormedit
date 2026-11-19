'use strict';

function installWormeditBrowserAssets(module) {
  const panel = document.getElementById('browser-assets');
  const summary = document.getElementById('asset-summary');
  const folder = document.getElementById('asset-folder');
  const apply = document.getElementById('asset-apply');
  const status = document.getElementById('asset-status');
  const rows = document.getElementById('asset-rows');
  const audioExtension = /\.(wav|ogg|flac|mp3)$/i;
  const bmsExtension = /\.(wav|ogg|flac)$/i;
  const choices = new Map();
  let snapshot = {sounds: [], loaded: false, busy: false};
  let files = [];
  let index;
  let submitting = false;
  let notice = '';

  const basename = path => path.slice(path.lastIndexOf('/') + 1);
  const stem = path => path.replace(bmsExtension, '');
  const fold = path => path.toLowerCase();

  function indexFiles(entries) {
    const byPath = new Map(), byFoldedPath = new Map(), byName = new Map();
    const byStemPath = new Map(), byStemName = new Map();
    function add(map, key, entry) {
      if (map.has(key)) map.get(key).push(entry);
      else map.set(key, [entry]);
    }
    for (const entry of entries) {
      const path = entry.relative;
      add(byPath, path, entry);
      add(byFoldedPath, fold(path), entry);
      add(byName, fold(basename(path)), entry);
      if (bmsExtension.test(path)) {
        add(byStemPath, fold(stem(path)), entry);
        add(byStemName, fold(stem(basename(path))), entry);
      }
    }
    return {byPath, byFoldedPath, byName, byStemPath, byStemName};
  }

  function candidates(source) {
    if (!index) return [];
    const path = source.replaceAll('\\', '/').replace(/^\.\//, '');
    const name = basename(path);
    return index.byPath.get(path) || index.byFoldedPath.get(fold(path)) ||
      (bmsExtension.test(path) && index.byStemPath.get(fold(stem(path)))) ||
      index.byName.get(fold(name)) ||
      (bmsExtension.test(path) && index.byStemName.get(fold(stem(name)))) || [];
  }

  function updateControls() {
    folder.disabled = !snapshot.loaded || snapshot.busy || !snapshot.sounds.length || submitting;
    apply.disabled = folder.disabled || ![...choices.values()].some(Boolean);
    status.textContent = notice || (snapshot.loaded
      ? (snapshot.sounds.length ? 'Choose a folder to repair missing audio.' : 'No missing sounds.')
      : 'Open a project to check its sounds.');
  }

  function render() {
    summary.textContent = snapshot.sounds.length ? `Assets · ${snapshot.sounds.length} missing sounds` : 'Assets';
    rows.replaceChildren();
    choices.clear();
    snapshot.sounds.forEach(({id, path}) => {
      const label = document.createElement('label');
      const name = document.createElement('span');
      name.textContent = path;
      const matching = candidates(path);
      label.append(name);
      if (!matching.length) {
        const hint = document.createElement('span');
        hint.textContent = files.length ? 'No matching sound' : 'Choose a folder to find this sound';
        label.append(hint);
      } else {
        const select = document.createElement('select');
        select.append(new Option(matching.length === 1 ? 'Skip this sound' : 'Choose a matching file…', ''));
        for (const file of matching) select.append(new Option(file.relative, file.key));
        select.value = matching.length === 1 ? matching[0].key : '';
        choices.set(id, select.value);
        select.addEventListener('change', () => {
          choices.set(id, select.value);
          updateControls();
        });
        label.append(select);
      }
      rows.append(label);
    });
    updateControls();
  }

  function watch() {
    module.ccall('wormedit_browser_watch_assets', null, ['number'], [Number(panel.open || submitting)]);
  }

  panel.addEventListener('toggle', watch);

  folder.addEventListener('change', () => {
    const selected = [...folder.files].filter(file => audioExtension.test(file.name));
    folder.value = '';
    files = selected.map((file, key) => ({
      file, key: String(key), relative: file.webkitRelativePath.split('/').slice(1).join('/') || file.name
    }));
    index = indexFiles(files);
    notice = files.length ? 'Review the matched sounds before relinking.' :
      'This folder contains no WAV, Ogg, FLAC, or MP3 files.';
    render();
  });

  apply.addEventListener('click', async () => {
    const generation = snapshot.generation;
    const matches = [...choices].filter(([, key]) => key).map(([id, key]) => ({id, file: files[Number(key)]}));
    const unstaged = [...new Set(matches.map(match => match.file))].filter(file => !file.path);
    submitting = true;
    notice = 'Reading selected sounds…';
    updateControls();
    try {
      const paths = await module.browserFiles.stageFiles(unstaged.map(file => file.file), () => true);
      unstaged.forEach((file, index) => { file.path = paths[index]; });
      const payload = JSON.stringify({
        generation, matches: matches.map(({id, file}) => ({id, path: file.path}))
      });
      if (!module.ccall('wormedit_browser_repair_assets', 'number', ['string'], [payload]))
        throw new Error('A sound repair is already pending.');
      notice = 'Relinking sounds…';
    } catch (error) {
      submitting = false;
      watch();
      notice = String(error);
    }
    updateControls();
  });

  module.browserAssets = {
    update(payload) {
      const updated = JSON.parse(payload);
      const changed = snapshot.session !== updated.session;
      if (changed && !submitting) notice = '';
      const differentSounds = snapshot.sounds.length !== updated.sounds.length ||
        snapshot.sounds.some((sound, index) => {
          const next = updated.sounds[index];
          return sound.id !== next.id || sound.path !== next.path;
        });
      snapshot = updated;
      if (changed || differentSounds) render();
      else updateControls();
    },
    complete(message) {
      submitting = false;
      notice = message;
      watch();
      updateControls();
    }
  };
}
