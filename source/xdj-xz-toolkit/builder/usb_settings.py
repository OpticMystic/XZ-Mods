"""Native USB preferences, defined by the schema paired with the shipped runtime."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path

def contract(resources):
    root=Path(resources)/'runtime'
    manifest=json.loads((root/'manifest.json').read_text())
    data=(root/'settings-schema.json').read_bytes()
    if hashlib.sha256(data).hexdigest()!=manifest.get('settings_schema_sha256'):
        raise ValueError('The USB settings schema does not match this app runtime. Reinstall the complete XZ Mods package.')
    schema=json.loads(data)
    if schema.get('format')!='xz-mods-settings-schema/1':raise ValueError('Unsupported bundled settings schema')
    return schema

def defaults(schema):return {field['key']:field['default'] for field in schema['fields']}

def validate(values,schema):
    if not isinstance(values,dict) or set(values)!={f['key'] for f in schema['fields']}:
        raise ValueError('Supply the complete settings form; missing or unknown fields are not allowed')
    for field in schema['fields']:
        value=values[field['key']]
        if type(value) is not int or not field['min']<=value<=field['max']:
            raise ValueError('Invalid value for '+field['label'])
    return dict(values)

def parse(data,schema):
    if len(data)>511:raise ValueError('Settings file is too large for this runtime')
    try:text=data.decode('ascii')
    except UnicodeError:raise ValueError('Settings file is not native ASCII') from None
    if not text.startswith(schema['header']+'\n') or not text.endswith('\n') or '\r' in text or '\0' in text:
        raise ValueError('Unsupported settings header or line endings; original file was preserved')
    lines=text.split('\n')[1:-1]
    if len(lines) not in schema['accepted_field_counts']:raise ValueError('Incomplete or newer settings record; original file was preserved')
    values=defaults(schema)
    for line,field in zip(lines,schema['fields']):
        prefix=field['key']+'='
        if not line.startswith(prefix):raise ValueError('Unknown or out-of-order settings field; original file was preserved')
        value=line[len(prefix):]
        if not value.isascii() or not value.isdigit() or len(value)>2 or (len(value)>1 and value[0]=='0'):
            raise ValueError('Invalid native settings integer')
        values[field['key']]=int(value)
    return validate(values,schema)

def serialize(values,schema):
    values=validate(values,schema)
    data=(schema['header']+'\n'+''.join(f"{f['key']}={values[f['key']]}\n" for f in schema['fields'])).encode('ascii')
    if parse(data,schema)!=values:raise ValueError('Settings round-trip failed')
    return data
