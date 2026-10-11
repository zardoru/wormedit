# Editor fonts

`AdwaitaMono-Regular.ttf` remains the Latin editor font. It is part of
Adwaita Fonts, distributed under SIL Open Font License 1.1 in `LICENSE`;
the bundled copy was taken from the system Adwaita Fonts package.

`NotoSansCJKjp-Regular.otf` is the unmodified Noto Sans CJK Japanese regular
OpenType release. It includes Japanese kana, Simplified/Traditional Chinese
ideographs, Korean Hangul and CJK punctuation. Shared ideographs use Japanese
shapes; language-specific font switching and UI translation are separate work.

- Upstream: https://github.com/notofonts/noto-cjk
- Download: https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/OTF/Japanese/NotoSansCJKjp-Regular.otf
- Retrieved: 2026-10-11
- SHA-256: `68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5`
- Copyright: Adobe (font metadata); licensed under SIL Open Font License 1.1,
  reproduced in `NotoSansCJK-LICENSE`.

The font file is bundled in builds and installed packages. Only glyphs observed
in rendered UI text are baked, up to 4,096 additional characters per frontend
session. Unsupported characters or a full cache use a visible box and one log
warning; restarting the frontend resets the cache. No system font is required.
