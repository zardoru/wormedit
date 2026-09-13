# Phosphor icon subset

This directory contains the two regular-weight SVGs selected for wormedit's
hitsound/keysound layering UI:

- `waveform.svg`
- `stack.svg`

Their SVG path data is copied unchanged (with only an EOF newline normalized)
from
[`phosphor-icons/core`](https://github.com/phosphor-icons/core) commit
`2b75f3ad12b420c9504ef05df8d2564a28f8500e`:

```text
assets/regular/waveform.svg
assets/regular/stack.svg
```

The artwork is distributed under the MIT License in [`LICENSE`](LICENSE).
Keep the upstream revision, original glyph names, and license with redistributed
copies. The application may compose a small `stack` badge over `waveform`, but
should retain the sound-first silhouette and verify it at 16, 20, and 24 pixels.
