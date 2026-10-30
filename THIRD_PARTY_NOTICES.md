# Third-party notices

JianpuLab is licensed under GPL-3.0-only. MuseScore source files and their original notices remain unmodified in the pinned reference checkout. See LICENSE for the GPL text.

| Component | Source and revision | License information |
| --- | --- | --- |
| MuseScore Studio engraving, editing, MusicXML and MIDI modules | https://github.com/musescore/MuseScore, `4509c9072c583f7ab0c805ded2ab88f29e0ec0e5` | GPL-3.0-only; original file headers retained |
| Muse framework | https://github.com/musescore/muse_framework, `42b02245d36f1f37dd2b04a382e27c28c9ece006` | Original per-file notices; framework and kors license texts included in distribution |
| muse_deps build recipes | https://github.com/musescore/muse_deps, `a9db17d2f0478859711873f2db156eedfc082040` | Fixed recipes resolve dependency sources and preserve their notices |
| Qt 6.10.2 | https://www.qt.io | Redistributed dynamically; Qt license texts copied by packaging script |
| FreeType, HarfBuzz, libpng, zlib | Versions and source hashes in pinned muse_deps recipes | Runtime libraries and supplied licenses installed by dependency manifest |
| pugixml, utfcpp, picojson | Versions and source hashes in pinned muse_deps recipes | Source license files installed into licenses |
| BeatRoot | MuseScore/src/importexport/midi/thirdparty/beatroot | GPL notice in COPYING |
| Embedded music/text fonts | MuseScore/fonts and engraving resource manifests | Original font licenses included under licenses/fonts; font metadata and source remain in corresponding source checkout |

The independent module initializer and format adapter follow upstream production initialization and two-pass import behavior. Compatibility code in cmake/engine is part of JianpuLab and is supplied with its source. No MuseScore branding is used as the application name.

## Editor fonts

The application embeds Noto Serif CJK SC and Noto Sans CJK SC (SIL Open Font License 1.1, from https://github.com/notofonts/noto-cjk) and DejaVu Sans (Bitstream Vera / DejaVu font licenses). Corresponding license texts are distributed in `licenses/NotoCJK` and `licenses/DejaVu`. These fonts are used by the editor and do not change the MuseScore reference checkout.
