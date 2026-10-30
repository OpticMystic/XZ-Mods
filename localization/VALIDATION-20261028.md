# Validation and parent handoff, 2026-10-28

This is an incomplete implementation checkpoint, not release acceptance.

Implemented an independent CPU Small100/CT2 provider, pinned Python requirements,
standalone XZ setup command, batched checkpoints, a single-writer OS lock, exact
source/model/tokenizer/vocabulary hashes, machine-unreviewed provenance,
whole-language-set atomic publication and validation before publication. Resume
reuses matching input hashes; source changes and provider errors retain the old
bundle. Imports/builds cannot publish incomplete language sets through the CLI.

Generic Portuguese is `pt`, displayed as Português. The desktop migrates saved
pt-BR preferences and accepts legacy catalog aliases. The legacy seed JSON is now
pt.json. Neither Library settings nor its provider are runtime dependencies.

Extraction includes desktop JS literals/templates and wizard text, plus manual
search fields. Dynamic textContent call sites in app.js and live-stream.js use
XZText around authored literals; runtime values remain outside those calls.
Manual output translates text and descriptive attributes while retaining links.
Search now keeps Unicode letters/numbers and can use translated title/excerpt data.

Verification:

- 22 Python tests passed, including whole-set publication, zero-inference cache
  resume with a fake provider, source-race/provider-failure preservation, parser
  coverage, protected controls, metadata separation and Portuguese base mapping.
- 11 manual-builder checks passed.
- 84 isolated headless Edge checks passed using the retained seed bundle,
  including generic Portuguese preference/catalog migration, all 14 navigation
  languages, RTL direction, import, manual navigation/search and CSP.
- Actual independent provider generated representative desktop/manual outputs for
  all 14 locales. provider-context-20261028.json records those outputs and model
  hashes. These are machine results, not native-speaker approval or installed UI.
- The first corpus round checkpointed 256 segments per language. Further rounds
  sampled manual passages and caught punctuation/marker defects. Old split-input
  evidence is retained as provider-20261028.json to show the quality defect.
  Whole-sentence numeric markers improve the verb context. Lost tokens retry with
  beam search, then use a protected fragment fallback explicitly labeled
  split-fallback-unreviewed. That fallback needs language review.
- The publication gate still fails: the complete new corpus has not been generated.
  ui/xz-l10n-data.js remains the old seed; it was deliberately not rebased to the
  new source revision. The parent must not package this checkpoint as complete.

Runtime and retained work are independent of the Library installation:

- Python: E:/Claude/xz-beta-rc-scratch/xz-translation-runtime/Scripts/python.exe
- Model: E:/Claude/xz-beta-rc-scratch/xz-small100-model
- Checkpoints and failure records: E:/Claude/xz-beta-rc-scratch/xz-translation-progress
- Model proof HTML/JSON: E:/Claude/xz-beta-rc-scratch/xz-provider-proof-context

Resume with that Python and:

```
tools/xz_translate.py --model E:/Claude/xz-beta-rc-scratch/xz-small100-model --state E:/Claude/xz-beta-rc-scratch/xz-translation-progress
```

For bounded supervised work add `--seconds 120`; exit 2 means work remains.
`--round-size 64` samples each locale, stages validated partial catalogs, and
never publishes those partials. Intermediate cache counts are not full string
coverage: protection/input segmentation changed during diagnosis.

Remaining implementation: complete dynamic HTML/template call-site wiring,
desktop provider install/run setup, model acquisition/conversion setup, full
corpus generation and native-language review. NATIVE-TEXT-INVESTIGATION.md records
why a generic native font ABI is unproved. No Unicode atlas/shaping integration
or licensed controller fonts were implemented. Controller hardware, packaging and
installed-webview acceptance are pending. Parent owns integration/local install.
No public push/release/feed or device/internal-storage/USB action occurred.

Final retained round: all 14 staged catalogs passed structural validation.
A real `--cached-only` rerun recorded zero inference calls. Staged counts are 448
Spanish entries, 309 each for French/German/Portuguese/Italian/Japanese, and 281
each for Korean/Simplified Chinese/Traditional Chinese/Dutch/Polish/Turkish/
Ukrainian/Arabic. See evidence/corpus-round-20261028.json. These partial counts
include immutable entries and are not full translated-corpus acceptance. No
translation worker remains running at handoff. The full all-locale corpus still
needs completion before the source freshness/publication gate can pass.
