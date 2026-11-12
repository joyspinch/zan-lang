# Website brand assets

The website build copies official artwork directly from the repository's `assets/brand/` directory. Do not maintain separate logo drawings or mascot images here.

| Published file | Source under `assets/brand/` |
| --- | --- |
| `mark.svg` | `logo/zan-mark.svg` |
| `wordmark.svg` | `logo/zan-wordmark-white.svg` |
| `zanzi-hero.png` | `mascot/zanzi-hero.png` |
| `social.png` | `banner/zan-banner.png` |

Run `npm --prefix zan-site run build` or `npm --prefix zan-site run dev` to assemble the site. Shared website colors live in `src/theme.css` and follow `assets/brand/VI.md`.
