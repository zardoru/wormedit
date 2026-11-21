async function enableWormeditIsolation() {
  if (window.crossOriginIsolated) return true;
  if (!navigator.serviceWorker)
    throw new Error('Enable service workers and open the editor directly over HTTPS.');

  const wasControlled = Boolean(navigator.serviceWorker.controller);
  await navigator.serviceWorker.register('./isolation-worker.js');
  if (wasControlled)
    throw new Error('Cross-origin isolation is unavailable. Open the editor in its own tab.');

  // The worker controls the next navigation and sets COOP/COEP on the HTML response.
  await navigator.serviceWorker.ready;
  if (!navigator.serviceWorker.controller) {
    await new Promise(resolve => {
      navigator.serviceWorker.addEventListener('controllerchange', resolve, {once: true});
    });
  }
  window.location.reload();
  return false;
}
