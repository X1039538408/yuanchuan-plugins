# Offline search data

The installed plug-in uses embedded C++ tables. It requires no Python, online
translation service, or network connection for searching.

`pinyin_data.h` is generated from the **pypinyin 0.55.0** wheel distributed on PyPI:

- Source: https://github.com/mozillazg/python-pinyin
- Wheel: `pypinyin-0.55.0-py2.py3-none-any.whl`
- SHA-256: `d53b1e8ad2cdb815fb2cb604ed3123372f5a28c6f447571244aca36fc62a286f`
- Character data: https://github.com/mozillazg/pinyin-data
- Phrase data: https://github.com/mozillazg/phrase-pinyin-data

The snapshot contains 41,923 character readings and 47,111 phrases. Conversion
uses the longest matching phrase, then the first recorded character reading.
This handles many polyphonic words (for example, 旅行 and 重复), but does not
infer the intended pronunciation of every custom name. Users can supply an
explicit pinyin field or an alternate Chinese alias. An ideograph absent from
the snapshot keeps its original Chinese search name instead of receiving an
invented reading.

Pinyin uses `v` for `ü`. Search also accepts `ü`, `u:`, common tone marks, case
differences, spaces and separators. Mixed names retain searchable brand letters
and receive a Chinese-only variant: `CC扫光` supports `ccsaoguang`, `ccsg`,
`saoguang` and `sg`.

All three upstream projects use the MIT license. Their full notices are in this
directory, and the combined distributable notice is
`installer/XFXNativeConsole.THIRD-PARTY-NOTICES.txt`. Distribute that file with the
plug-in and its installer.

## Effect translations

`effect_translations.h` is generated from
`installer/XFXNativeConsole.aliases.tsv`, the editable source of truth. Match
names and English product names remain unchanged; a custom Chinese translation
takes priority over the bundled catalog. Matching uses known exact identifiers
or English names. Unknown effects retain their original names.

The catalog includes common Adobe/Cycore effects and descriptive Chinese search
labels for selected Sapphire, BCC, Trapcode, RE:Vision and Video Copilot effects.
Third-party search labels describe the effect; they are not vendor-approved UI
translations, and they do not modify effect parameter panels. Brand names are
retained. The catalog does not claim complete coverage of third-party products.

Name/terminology references:

- Adobe Chinese effect list: https://helpx.adobe.com/cn/after-effects/desktop/apply-effects-and-animation-presets/effects-and-animation-presets/effect-list.html
- Adobe distort effects: https://helpx.adobe.com/cn/after-effects/desktop/apply-effects-and-animation-presets/list-of-effects/distort-effects.html
- Adobe color correction: https://helpx.adobe.com/cn/after-effects/desktop/adjust-colors/work-with-color-correction-effects/color-correction-effects.html
- Adobe stylize effects: https://helpx.adobe.com/cn/after-effects/desktop/apply-effects-and-animation-presets/list-of-effects/stylize-effects.html
- Boris FX Continuum effect list: https://borisfx.com/documentation/continuum/bcc-effects-list/
- Boris FX Sapphire effect index: https://borisfx.com/documentation/sapphire/ae/name-index/
- Maxon Particular: https://www.maxon.net/en/product-detail/red-giant/particles-and-3d/trapcode-particular
- Maxon Starglow: https://help.maxon.net/rg/en-us/Content/html/01-Starglow-trapcode-starglow.html
- RE:Vision effects: https://revisionfx.com/
- Video Copilot Saber: https://www.videocopilot.net/index.php/tutorials/saber_plug-in

## Regeneration

Obtain the pinned wheel as a development dependency and run from the repository:

```powershell
python native/src/data/generate_search_data.py path/to/pypinyin-0.55.0-py2.py3-none-any.whl
```

The generator checks the wheel hash and writes both C++ headers. Normal builds
use those checked-in headers and need no additional source file, library, build
flag or code-generation step. The packed string blocks avoid MSVC's COFF section
limit without `/bigobj`.
