"""Source-aware quality checks. This module never loads a model or user metadata."""
from collections import Counter
import html
import re
from xz_i18n import BRANDS, CONTROLS, STOCK_ALERTS, LOCALES, validate

BAD_MARKERS = ('\u2047', '\ufffd')
EXTRA_BRANDS = ('Small100', 'CTranslate2', 'SentencePiece', 'OpenCC', 'Doom', 'Chex Quest',
                'Beat Arcade', 'Wave Rider', 'Rhythm Lab', 'Static Surfer', 'My House',
                'NVIDIA', 'AMD', 'Intel', 'Microsoft', 'VJ.Tools Library', 'Wingman')
TECHNICAL_NAMES = ('RAM', 'CPU', 'GPU', 'BPM', 'MIDI', 'WAV', 'FLAC', 'MP3', 'PCM', 'UTF-8')
URL_RE = re.compile(r'(?i:https?)://[^\s<>"\']*|(?<![\w@])(?:[A-Za-z0-9-]+\.)+[A-Za-z]{2,24}(?:[/?#][^\s<>"\']*)?(?!\w)', re.ASCII)
EMAIL_RE = re.compile(r'(?<!\w)[\w.+-]+@[\w.-]+\.[A-Za-z]{2,24}(?!\w)', re.ASCII)
EXTRA_RE = re.compile(r'\bUSBs\b|\bE-\d{4}\b|\$\{[^}]+\}|\{[A-Za-z_]\w*\}|%[sdif]|[\u2047\ufffd\u2190-\u21ff\u00b7\u00d7\u00b1\u2264\u2265\u2260]', re.ASCII)

def words_pattern(values, ignore_case=False):
    return re.compile(r'(?<!\w)(?:'+'|'.join(re.escape(v) for v in sorted(set(values),key=len,reverse=True))+r')(?!\w)', re.ASCII | (re.I if ignore_case else 0))

def urls(text):
    return Counter(m.group().rstrip('.,;:!?)]}') for pattern in (URL_RE, EMAIL_RE) for m in pattern.finditer(text))

class LiteralPolicy:
    def __init__(self, source=None):
        controls=set(CONTROLS) | {'AUTO CUE'}
        for document in (source or {}).get('manual_documents',{}).values():
            for value in re.findall(r'<kbd>(.*?)</kbd>',document['html']):
                label=html.unescape(re.sub('<[^>]+>','',value)).strip()
                if label==label.upper() and re.search('[A-Z]',label): controls.add(label)
        self.controls=words_pattern(controls)
        self.brands=words_pattern((*BRANDS,*EXTRA_BRANDS),True)
        self.patterns=(words_pattern(STOCK_ALERTS),self.brands,URL_RE,EMAIL_RE,self.controls,
                       words_pattern(TECHNICAL_NAMES),EXTRA_RE)

    def spans(self,text):
        candidates=[]
        for pattern in self.patterns:
            for match in pattern.finditer(text):
                end=match.end()
                if pattern is URL_RE: end=match.start()+len(match.group().rstrip('.,;:!?)]}'))
                if end>match.start(): candidates.append((match.start(),end))
        spans=[]
        for start,end in sorted(candidates):
            if spans and start<=spans[-1][1]: spans[-1]=(spans[-1][0],max(end,spans[-1][1]))
            else: spans.append((start,end))
        return spans

    def prose(self,text):
        result=[]; start=0
        for left,right in self.spans(text): result.append(text[start:left]); start=right
        result.append(text[start:]); return ''.join(result)

    def errors(self,original,text):
        errors=[]
        for char in BAD_MARKERS:
            if text.count(char)>original.count(char): errors.append(f'introduced U+{ord(char):04X}')
        for label,pattern in (('brand',self.brands),('control',self.controls)):
            expected=Counter(pattern.findall(original)); actual=Counter(pattern.findall(text))
            for token,count in expected.items():
                if actual[token]!=count: errors.append(f'{label} changed: {token}')
        for value,count in urls(original).items():
            if text.count(value)!=original.count(value): errors.append('URL or email changed')
        # A full stop followed by a translated capitalized word is not an invented hostname.
        absolute=re.compile(r'(?i:https?)://[^\s<>\"\']*')
        for pattern in (absolute,EMAIL_RE):
            before=Counter(m.group().rstrip('.,;:!?)]}') for m in pattern.finditer(original))
            after=Counter(m.group().rstrip('.,;:!?)]}') for m in pattern.finditer(text))
            if before!=after: errors.append('URL or email changed')
        for start,end in self.spans(original):
            token=original[start:end]
            if token.startswith(('{','${','%')): continue
            boundary=re.compile(r'(?<!\w)'+re.escape(token)+r'(?!\w)',re.ASCII)
            if len(boundary.findall(text))!=len(boundary.findall(original)):
                errors.append('protected literal changed: '+token)
        return errors

def validate_quality(source,catalog,policy=None,complete=True):
    policy=policy or LiteralPolicy(source)
    errors=validate(source,catalog,complete=complete)
    for key,entry in catalog.get('entries',{}).items():
        original=source['entries'].get(key)
        if original and isinstance(entry.get('text'),str):
            errors.extend(key+': '+error for error in policy.errors(original['source'],entry['text']))
    return errors

def require_quality(data):
    policy=LiteralPolicy(data['source']); receipt={}
    for locale in LOCALES:
        catalog=data['catalogs'].get(locale,{})
        errors=validate_quality(data['source'],catalog,policy)
        if errors: raise ValueError(locale+': '+'; '.join(errors[:12]))
        receipt[locale]={'entries':len(catalog['entries']),'introduced_unknown_markers':0,
                         'protected_literal_errors':0,'review':'machine-unreviewed'}
    return receipt
