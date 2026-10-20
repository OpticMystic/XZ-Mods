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
    fields.update(schema.get('derived_fields',{}))
    return [fields[key] for key in schema.get('wire_order',fields)]

def _legacy_theme(schema):
    """The older single theme slot, derived from the interface and classic theme (24 = modern)."""
    field=schema.get('derived_fields',{}).get('theme')
    return field['derived'] if field else None

def _legacy_rekordbox(schema):
    """The older rekordbox_menu switch, derived from the REKORDBOX button's press choice."""
    field=schema.get('derived_fields',{}).get('rekordbox_menu')
    return field['derived'] if field and 'press' in field.get('derived',{}) else None

def _retire_takeover(values,rule):
    """The REKORDBOX choice of the screen switch button is retired: it loads as its replacement."""
    if rule and values.get(rule['takeover'])==rule['takeover_retired']:
        values[rule['takeover']]=rule['takeover_replacement']

def legacy_values(values,schema):
    """values plus the combined theme and the REKORDBOX switch an older mod version's schema reads."""
    values=dict(values)
    rule=_legacy_theme(schema)
    if rule:
        modern=values.get(rule['interface'])==1
        values['theme']=rule['modern_value'] if modern else values.get(rule['classic'],0)
    button=_legacy_rekordbox(schema)
    if button:values['rekordbox_menu']=1 if values.get(button['press'])==button['mods_value'] else 0
    return values


def defaults(schema):return {field['key']:field['default'] for field in schema['fields']}

def validate(values,schema):
    if isinstance(values,dict):
        values={k:v for k,v in values.items() if k not in schema.get('retired_fields',{})}
        _retire_takeover(values,_legacy_rekordbox(schema))
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
    values=defaults(schema);derived=set(schema.get('derived_fields',{}));read={}
    for line,field in zip(lines,wire_fields(schema)):
        prefix=field['key']+'='
        if not line.startswith(prefix):raise ValueError('Unknown or out-of-order settings field; original file was preserved')
        value=line[len(prefix):]
        if not value.isascii() or not value.isdigit() or len(value)>2 or (len(value)>1 and value[0]=='0'):
            raise ValueError('Invalid native settings integer')
        number=int(value)
        if not field['min']<=number<=field['max']:raise ValueError('Invalid native settings integer')
        read[field['key']]=number
        if field['key'] not in schema.get('retired_fields',{}) and field['key'] not in derived:values[field['key']]=number
    rule=_legacy_theme(schema)
    if rule and 'theme' in read and rule['interface'] not in read:
        # An older record: its theme slot sets the interface, and the classic theme unless it named the modern look.
        modern=read['theme']==rule['modern_value']
        values[rule['interface']]=1 if modern else 0
        if not modern:values[rule['classic']]=read['theme']
    button=_legacy_rekordbox(schema)
    if button and button['press'] not in read:
        # An older record: rekordbox_menu (default on before it existed) and the takeover key decide the press.
        if read.get('rekordbox_menu',1):values[button['press']]=button['menu_on_press']
        elif read.get(button['takeover'])==button['takeover_retired']:values[button['press']]=button['takeover_press']
        else:values[button['press']]=button['menu_off_press']
        values[button['shift']]=button['shift_default']
    return validate(values,schema)

def serialize(values,schema):
    values=validate(values,schema)
    wire=legacy_values(values,schema)
    data=(schema['header']+'\n'+''.join(f"{f['key']}={wire.get(f['key'],f['default'])}\n" for f in wire_fields(schema))).encode('ascii')
    if parse(data,schema)!=values:raise ValueError('Settings round-trip failed')
    return data
