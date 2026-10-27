"""Identifier implementations emitted from support-contract.json by the generator."""

def implementations(contract):
    ids = contract['identifiers']
    alphabet = ids['report_reference']['alphabet']
    maximum = 2 ** ids['report_reference']['bits'] - 1
    minimum = ids['item_number']['minimum']
    item_max = ids['item_number']['maximum']
    ts = f'''const REFERENCE_ALPHABET = '{alphabet}';
function compactIdentifier(value: unknown): string | null {{
  return typeof value === 'string' && /^[a-zA-Z0-9 -]+$/.test(value) ? value.replace(/[ -]/g, '').toUpperCase() : null;
}}
export function formatReportReference(value: number): string | null {{
  if (!Number.isSafeInteger(value) || value < 0 || value > {maximum}) return null;
  let symbols = '';
  for (let i = 0; i < 8; i++) {{ symbols = REFERENCE_ALPHABET[value % 32] + symbols; value = Math.floor(value / 32); }}
  return 'R-' + symbols.slice(0, 4) + '-' + symbols.slice(4);
}}
export function parseReportReference(value: unknown): number | null {{
  const compact = compactIdentifier(value);
  if (!compact || compact.length !== 9 || compact[0] !== 'R') return null;
  const symbols = compact.slice(1).replace(/[IL]/g, '1').replace(/O/g, '0');
  let result = 0;
  for (const symbol of symbols) {{ const digit = REFERENCE_ALPHABET.indexOf(symbol); if (digit < 0) return null; result = result * 32 + digit; }}
  return result;
}}
export function formatItemNumber(value: number): string | null {{
  return Number.isSafeInteger(value) && value >= {minimum} && value <= {item_max} ? 'XZ-' + value : null;
}}
export function parseItemNumber(value: unknown): number | null {{
  const compact = compactIdentifier(value);
  if (!compact || !/^XZ[0-9]+$/.test(compact)) return null;
  const number = Number(compact.slice(2));
  return formatItemNumber(number) === null ? null : number;
}}
'''
    js = ts.replace(': unknown', '').replace(': number)', ')').replace('): number | null', ')').replace('): string | null', ')')
    py = f'''
_REFERENCE_ALPHABET = {alphabet!r}
def _compact_identifier(value):
    if not isinstance(value, str) or not value or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -' for c in value):
        return None
    return value.replace(' ', '').replace('-', '').upper()

def format_report_reference(value):
    if type(value) is not int or not 0 <= value <= {maximum}:
        return None
    symbols = ''
    for _ in range(8):
        symbols = _REFERENCE_ALPHABET[value % 32] + symbols
        value //= 32
    return 'R-' + symbols[:4] + '-' + symbols[4:]

def parse_report_reference(value):
    compact = _compact_identifier(value)
    if not compact or len(compact) != 9 or compact[0] != 'R':
        return None
    result = 0
    for symbol in compact[1:].replace('I', '1').replace('L', '1').replace('O', '0'):
        if symbol not in _REFERENCE_ALPHABET:
            return None
        result = result * 32 + _REFERENCE_ALPHABET.index(symbol)
    return result

def format_item_number(value):
    return 'XZ-' + str(value) if type(value) is int and {minimum} <= value <= {item_max} else None

def parse_item_number(value):
    compact = _compact_identifier(value)
    if not compact or not compact.startswith('XZ') or not compact[2:].isdigit():
        return None
    digits = compact[2:].lstrip('0')
    if not digits or len(digits) > {len(str(item_max))}:
        return None
    value = int(digits)
    return value if format_item_number(value) else None
'''
    rust = f'''
const REFERENCE_ALPHABET: &[u8] = b"{alphabet}";
fn compact_identifier(value: &str) -> Option<String> {{
    if value.is_empty() || !value.bytes().all(|b| b.is_ascii_alphanumeric() || b == b' ' || b == b'-') {{ return None; }}
    Some(value.bytes().filter(|b| *b != b' ' && *b != b'-').map(|b| b.to_ascii_uppercase() as char).collect())
}}
pub fn format_report_reference(mut value: u64) -> Option<String> {{
    if value > {maximum} {{ return None; }}
    let mut symbols = [b'0'; 8];
    for symbol in symbols.iter_mut().rev() {{ *symbol = REFERENCE_ALPHABET[(value % 32) as usize]; value /= 32; }}
    let text = std::str::from_utf8(&symbols).ok()?;
    Some(format!("R-{{}}-{{}}", &text[..4], &text[4..]))
}}
pub fn parse_report_reference(value: &str) -> Option<u64> {{
    let compact = compact_identifier(value)?;
    if compact.len() != 9 || !compact.starts_with('R') {{ return None; }}
    compact.bytes().skip(1).try_fold(0, |value, symbol| {{
        let symbol = match symbol {{ b'I' | b'L' => b'1', b'O' => b'0', other => other }};
        Some(value * 32 + REFERENCE_ALPHABET.iter().position(|b| *b == symbol)? as u64)
    }})
}}
pub fn format_item_number(value: u64) -> Option<String> {{
    ({minimum}..={item_max}).contains(&value).then(|| format!("XZ-{{value}}"))
}}
pub fn parse_item_number(value: &str) -> Option<u64> {{
    let compact = compact_identifier(value)?;
    let digits = compact.strip_prefix("XZ")?;
    if digits.is_empty() || !digits.bytes().all(|b| b.is_ascii_digit()) {{ return None; }}
    let value = digits.parse().ok()?;
    format_item_number(value).map(|_| value)
}}
#[cfg(test)]
mod identifier_tests {{
    use super::*;
    #[test]
    fn shared_identifier_vectors() {{
        let contract: serde_json::Value = serde_json::from_str(CONTRACT_JSON).unwrap();
        for (name, parse, format) in [
            ("report_reference", parse_report_reference as fn(&str)->Option<u64>, format_report_reference as fn(u64)->Option<String>),
            ("item_number", parse_item_number, format_item_number)
        ] {{
            for v in contract["identifiers"][name]["vectors"].as_array().unwrap() {{
                assert_eq!(parse(v["input"].as_str().unwrap()), v["value"].as_u64(), "{{name}}: {{v}}");
                if let Some(value) = v["value"].as_u64() {{ assert_eq!(format(value).as_deref(), v["canonical"].as_str()); }}
            }}
        }}
        assert!(format_report_reference({maximum + 1}).is_none());
        assert!(format_item_number({minimum - 1}).is_none());
        assert!(format_item_number({item_max + 1}).is_none());
    }}
}}
'''
    return ts, js, py, rust
