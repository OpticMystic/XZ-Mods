"""Validate the generated JSON contract without a packaged Python dependency."""
import re
from .support_contract import CONTRACT, SOURCES
from .support_sources import encoded

def validate(value, schema):
    if 'anyOf' in schema:
        for branch in schema['anyOf']:
            try:
                validate(value,branch)
                return
            except ValueError:
                pass
        raise ValueError('Invalid nullable support field')
    if 'const' in schema:
        if type(value) is not type(schema['const']) or value!=schema['const']:
            raise ValueError('Invalid support constant')
        return
    kind=schema['type']
    if kind=='object':
        if type(value) is not dict or set(value)-set(schema['properties']) or set(schema['required'])-set(value):
            raise ValueError('Missing or unknown support field')
        for key,child in value.items():validate(child,schema['properties'][key])
    elif kind=='array':
        if type(value) is not list or len(value)>schema['maxItems']:raise ValueError('Support array exceeds limit')
        for child in value:validate(child,schema['items'])
    elif kind=='string':
        if type(value) is not str:raise ValueError('Invalid support text')
        try:raw=value.encode('utf-8')
        except UnicodeError:raise ValueError('Invalid support Unicode') from None
        if len(raw)>schema.get('x-maxUtf8',1000000) or len(value)<schema.get('minLength',0):raise ValueError('Support text exceeds limit')
        if 'pattern' in schema and not re.fullmatch(schema['pattern'],value):raise ValueError('Invalid support text grammar')
        if 'enum' in schema and value not in schema['enum']:raise ValueError('Invalid support enum')
    elif kind=='integer':
        if type(value) is not int or not schema['minimum']<=value<=schema['maximum']:raise ValueError('Invalid support number')
    elif kind=='boolean':
        if type(value) is not bool:raise ValueError('Invalid support boolean')
    elif kind=='null':
        if value is not None:raise ValueError('Expected null')
    else:raise ValueError('Unsupported contract shape')

def validate_report(report):
    validate(report,CONTRACT['report_schema'])
    if not report['description'].strip() and not (report['origin']=='controller' and report.get('comments_pending') is True):raise ValueError('Describe what happened')
    if report['incident']['before_seconds']+report['incident']['after_seconds']>3600:raise ValueError('Incident window exceeds one hour')
    private=report['private'];items=private['items']
    if private['contact'] is not None and not private['contact_consent']:raise ValueError('Contact consent required')
    if (report.get('kind_hint')=='FEATURE' or report.get('reporter_hints',{}).get('topic')=='NEW_IDEA') and items:raise ValueError('Ideas do not include diagnostics')
    if len({i['item_id'] for i in items})!=len(items):raise ValueError('Duplicate item ID')
    limits=CONTRACT['limits']
    if len(encoded(items))>limits['diagnostic_bytes'] or len(encoded(report))>limits['report_bytes']:raise ValueError('Support byte limit exceeded')
    for item in items:
        if len(encoded(item))>limits['item_bytes'] or item['missing_reason'] is not None and item['facts']:raise ValueError('Invalid support item')
        for fact in item['facts']:
            source=next(s for s in SOURCES['sources'] if s['source_id']==item['source_id'])
            if set(fact)-set(source['safe_fields'])-{'relative_seconds','clock_basis','window_match'}:raise ValueError('Fact does not belong to source')
            if 'sync_phase_error_ms' in fact and len(fact['sync_phase_error_ms'])!=4:raise ValueError('Four deck metrics required')
            if fact['relative_seconds'] is None and fact['window_match']!='unknown':raise ValueError('Unknown incident time')
            if fact['clock_basis']=='unknown' and (fact['relative_seconds'] is not None or fact['window_match']!='unknown'):raise ValueError('Unknown clock')
            if ('error_namespace' in fact)!=('error_code' in fact) or ('setting' in fact)!=('value' in fact):raise ValueError('Incomplete typed fact')
            if 'setting' in fact:
                low,high=SOURCES['setting_bounds'][fact['setting']]
                if not low<=fact['value']<=high:raise ValueError('Invalid setting value')
            if 'version' in fact and ('component' not in fact or 'provenance' not in fact):raise ValueError('Missing version provenance')
    return report
