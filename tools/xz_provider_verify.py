"""Real, offline XZ provider test, invoked with argument arrays by the host."""
import json
import sys
from pathlib import Path
from xz_translate import Provider, LANG
from xz_translation_quality import LiteralPolicy

provider = Provider(Path(sys.argv[1]), 4)
results = {locale: provider.translate(['Choose a language for the manual.'], locale)[0] for locale in LANG}
if any(value == 'Choose a language for the manual.' for value in results.values()):
    raise ValueError('Provider returned untranslated test prose')
for locale, value in results.items():
    errors = LiteralPolicy().errors('Choose a language for the manual.', value)
    if errors: raise ValueError(locale + ': ' + '; '.join(errors))
print(json.dumps({'verified': True, 'review': 'machine-unreviewed', 'results': results, 'provider': provider.identity, 'runtime': {'python': sys.executable, 'prefix': sys.prefix, 'modules': {name: sys.modules[name].__file__ for name in ('ctranslate2','sentencepiece','opencc')}}}, ensure_ascii=True))
