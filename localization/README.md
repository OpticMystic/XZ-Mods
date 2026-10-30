# XZ local automatic translation

`tools/xz_translate.py` runs a CPU Small100 CTranslate2 provider with independent
model, Python runtime and checkpoint paths. It does not import Library code,
read Library settings, call its worker, or require Library to be installed.

## Setup and run

Choose a Small100 CT2 folder containing model.bin, shared_vocabulary.json and
sentencepiece.bpe.model. From this checkout:

```powershell
./tools/setup-xz-translation.ps1 -Model E:/Models/xz-small100
```

The setup command creates an XZ-only venv under LOCALAPPDATA/XZ Mods and installs
`localization/requirements.txt`. An existing runtime can be supplied with
`-Runtime`. No paid service or credentials are used. Model acquisition/conversion
is a separate prerequisite. The desktop language dialog now detects saved paths,
connects and live-tests the local provider, creates an XZ-only venv with pinned
dependencies, and starts or cancels full generation through native commands.
The host launches fixed scripts without a shell. Logs and checkpoints live in
the XZ application data directory. Generation has a 48-hour deadline; setup
and verification have a 30-minute deadline. Closing the app cancels its worker.
The packaged provider consumes localization/source.json; canonical source builds
continue to extract directly from the checkout.

```powershell
<own-python> tools/xz_translate.py --model <model-folder> --state <checkpoint-folder> --round-size 256
<own-python> tools/xz_translate.py --model <model-folder> --state <checkpoint-folder>
```

The first command is a bounded diagnostic only. It performs one resumable round across all 14 locales. The second
continues through the whole corpus. `--seconds 120` provides a time budget checked
after each batch. Exit 2 means checkpointed work remains. Errors return nonzero.
`--cached-only` validates retained translations with zero inference and does not
publish. The process is foreground and reports JSON progress after every saved batch.
`progress.json` is the latest receipt; locale cache files retain completed work.
An OS-held lock rejects concurrent writers to the same checkpoint directory.

Cache keys bind model, vocabulary, tokenizer, decoding recipe and exact segment
hash. Source changes reuse identical segments and translate new ones. Every output
entry carries the exact full source hash and machine-unreviewed provenance.
No model output is marked human-reviewed. Protected tokens are masked inside whole sentences and restored only when each
marker survives exactly once. This retains grammatical context. Language review
is still needed. Lost markers trigger one beam-search retry, then a protected
fragment fallback labeled split-fallback-unreviewed per entry. The fallback loses
sentence context. Structural validation still rejects altered controls.

Every locale must pass complete structural validation before publication. A final
source revision check rejects mid-run source edits. Publication atomically replaces
one `ui/xz-l10n-data.js` containing the complete source and all 14 catalogs. Failures
leave the previous bundle intact. Consumers prefer this canonical bundle over the
legacy seed JSON files. The CLI `import` command requires a complete catalog and a complete current
all-language bundle, then atomically replaces that bundle.

## Model contract

The official [Small100 model](https://huggingface.co/alirezamsh/small100) and
[tokenizer](https://huggingface.co/alirezamsh/small100/blob/main/tokenization_small100.py)
use a target language token on the encoder input. The checked converted vocabulary
contains es, fr, de, pt, it, ja, ko, zh, nl, pl, tr, uk and ar tokens. Model revision
8ab680e26a596d2e3d2d2d17ae0f68df1037328c is MIT licensed. The implementation checks
actual vocabulary before inference. Portuguese uses the pt model. Legacy pt-BR preferences and imported catalogs
migrate to pt. zh-Hans and zh-Hant use zh followed by pinned OpenCC t2s/s2t;
script conversion does not establish regional terminology quality.

This run copied only model bytes from an existing optional local installation to
E:/Claude/xz-beta-rc-scratch/xz-small100-model, then installed a separate venv at
E:/Claude/xz-beta-rc-scratch/xz-translation-runtime. It has no dependency on the
original installation afterward. Checkpoints are at
E:/Claude/xz-beta-rc-scratch/xz-translation-progress.

## Coverage and remaining work

Extraction includes static HTML, JavaScript literal/template candidates, the
language wizard and generated manual/search text. Candidates are conservative:
some code strings can enter the catalog and extraction counts do not establish
runtime coverage. Authored textContent literals in app.js and live-stream.js use
XZText, preserving runtime interpolations. Other dynamically assembled HTML and
templates still need explicit call-site wiring. The wizard is captured for
translation. Manual search retains Unicode letters/numbers and reads translated
titles and excerpts; CJK substring matching is supported.

Partial staged catalogs are structurally checked after every locale round.
They remain in the checkpoint directory and are never the application bundle.

The previously published seed remains in place until a whole corpus run finishes.
The source freshness gate deliberately rejects that old generation against the
changed source. Do not release or claim complete localization from the seed or
from a partial checkpoint. A full machine generation and language review remain
separate acceptance steps.

See NATIVE-TEXT-INVESTIGATION.md for the controller boundary. Native Unicode
rendering, a desktop provider install/run wizard, complete dynamic-template
wiring, installed desktop acceptance and hardware acceptance remain unfinished.
Parent owns integration and installation. No public publication is authorized.

Over-context manual-search passages split at sentence boundaries and carry
`sentence-split-unreviewed` provenance. Normal inputs keep their existing cache
recipe. Batches group similar lengths to reduce padding. Protection repairs
checkpoint every 32 entries and at locale completion, rather than rewriting the
whole cache after every entry. A cancellation can redo at most that repair batch.
Final receipts separate changed output from unchanged text. Unchanged English
is not counted as translated prose.
