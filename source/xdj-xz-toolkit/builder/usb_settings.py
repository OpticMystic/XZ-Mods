"""Native USB preferences, defined by the schema paired with the shipped runtime."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path

MAX_RECORD_BYTES = 1023

def contract(resources):
    root=Path(resources)/'runtime'
    manifest=json.loads((root/'manifest.json').read_text())
    data=(root/'settings-schema.json').read_bytes()
    if hashlib.sha256(data).hexdigest()!=manifest.get('settings_schema_sha256'):
        raise ValueError('The USB settings schema does not match this app runtime. Reinstall the complete XZ Mods package.')
    schema=json.loads(data)
    if schema.get('format')!='xz-mods-settings-schema/1':raise ValueError('Unsupported bundled settings schema')
    return schema

def wire_fields(schema):
    fields={f['key']:f for f in schema['fields']}
    fields.update(schema.get('retired_fields',{}))
    return [fields[key] for key in schema.get('wire_order',fields)]


def defaults(schema):return {field['key']:field['default'] for field in schema['fields']}

def validate(values,schema):
    if isinstance(values,dict):
        values={k:v for k,v in values.items() if k not in schema.get('retired_fields',{})}
    if not isinstance(values,dict) or set(values)!={f['key'] for f in schema['fields']}:
        raise ValueError('Supply the complete settings form; missing or unknown fields are not allowed')
    for field in schema['fields']:
        value=values[field['key']]
        if type(value) is not int or not field['min']<=value<=field['max']:
            raise ValueError('Invalid value for '+field['label'])
    columns=[values[key] for key in ('browse_1','browse_2','browse_3') if key in values]
    if len(set(columns))!=len(columns):raise ValueError('Browse columns must differ')
    return dict(values)

def parse(data,schema):
    if len(data)>MAX_RECORD_BYTES:raise ValueError('Settings file is too large for this runtime')
    try:text=data.decode('ascii')
    except UnicodeError:raise ValueError('Settings file is not native ASCII') from None
    if not text.startswith(schema['header']+'\n') or not text.endswith('\n') or '\r' in text or '\0' in text:
        raise ValueError('Unsupported settings header or line endings; original file was preserved')
    lines=text.split('\n')[1:-1]
    if len(lines) not in schema['accepted_field_counts']:raise ValueError('Incomplete or newer settings record; original file was preserved')
    values=defaults(schema)
    for line,field in zip(lines,wire_fields(schema)):
        prefix=field['key']+'='
        if not line.startswith(prefix):raise ValueError('Unknown or out-of-order settings field; original file was preserved')
        value=line[len(prefix):]
        if not value.isascii() or not value.isdigit() or len(value)>2 or (len(value)>1 and value[0]=='0'):
            raise ValueError('Invalid native settings integer')
        number=int(value)
        if not field['min']<=number<=field['max']:raise ValueError('Invalid native settings integer')
        if field['key'] not in schema.get('retired_fields',{}):values[field['key']]=number
    return validate(values,schema)

def serialize(values,schema):
    values=validate(values,schema)
    data=(schema['header']+'\n'+''.join(f"{f['key']}={values.get(f['key'],f['default'])}\n" for f in wire_fields(schema))).encode('ascii')
    if parse(data,schema)!=values:raise ValueError('Settings round-trip failed')
    return data
