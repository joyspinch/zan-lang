#!/usr/bin/env python3
"""Generate strongly typed Zan facades for the Senparc WeChat Pay APIs.

Normal methods expose generated request/response classes. RawAsync methods remain
as an explicit escape hatch for future protocol additions. Merchant credentials are
resolved only by WechatPay*Client configuration and never appear in method params.
"""
from pathlib import Path
import json
import re

from generate_wechat_product_apis import (
    ROOT, strip_comments, parse_methods, expanded_body, endpoint_literals,
    endpoint_constants, base_path,
)
from wechat_typed_codegen import (
    ModelIndex, Field, SharedModelRegistry, build_request_fields, response_fields, map_type,
    setter_call, parse_params, filtered_params, safe, simple_type,
)

VENDORED = ROOT / 'stdlib/Sdk/WeiXinMPSDK-master/src/Senparc.Weixin.TenPay'
OUTPUT = ROOT / 'stdlib/Sdk/Wechat/TenPay'
MARKER = 'Generated from Senparc WeChat Pay APIs'

SOURCE_GROUPS = [
    ('LegacyV2', VENDORED / 'Senparc.Weixin.TenPay/V2/TenPay', 'legacy_mp'),
    ('LegacyRights', VENDORED / 'Senparc.Weixin.TenPay/V2/TenPayRights', 'legacy_mp'),
    ('LegacyUniversal', VENDORED / 'Senparc.Weixin.TenPay/V3/Universal', 'v2'),
    ('V3', VENDORED / 'Senparc.Weixin.TenPayV3/Apis', 'v3'),
]

LEGACY_PATHS = {
    'UnifiedorderAsync': '/pay/unifiedorder',
    'Html5OrderAsync': '/pay/unifiedorder',
    'OrderQueryAsync': '/pay/orderquery',
    'CloseOrderAsync': '/pay/closeorder',
    'RefundAsync': '/secapi/pay/refund',
    'RefundQueryAsync': '/pay/refundquery',
    'DownloadBillAsync': '/pay/downloadbill',
    'MicroPayAsync': '/pay/micropay',
    'ReverseAsync': '/secapi/pay/reverse',
    'ShortUrlAsync': '/tools/shorturl',
    'TransfersAsync': '/mmpaymkttransfers/promotion/transfers',
    'GetTransferInfoAsync': '/mmpaymkttransfers/gettransferinfo',
    'PayBankAsync': '/mmpaysptrans/pay_bank',
    'QueryBankAsync': '/mmpaysptrans/query_bank',
    'GetPublicKeyAsync': '/risk/getpublickey',
}


def is_public_task(method):
    return method['access'] == 'public' and (
        'Task' in method['ret'] or 'ValueTask' in method['ret'] or
        'await ' in method['body']
    )


def pascal(text):
    words = re.findall(r'[A-Za-z0-9]+', text)
    return ''.join(w[:1].upper() + w[1:] for w in words if w)


def class_name(group, root, path):
    rel = path.relative_to(root).with_suffix('')
    parts = list(rel.parts)
    stem_parts = parts[-1].split('.')
    parts = parts[:-1] + stem_parts
    cleaned = []
    for part in parts:
        value = pascal(part)
        if value.lower() in ('apis', 'api', 'current', 'async'):
            if value.lower() == 'current': cleaned.append('Current')
            continue
        cleaned.append(value)
    prefix = 'WechatPayV3' if group == 'V3' else 'WechatPayLegacy'
    return prefix + ''.join(cleaned) + 'Api'


def normalize_path(path, mode='v3'):
    p = path.strip().replace('{{', '{').replace('}}', '}')
    p = re.sub(r'^https?://[^/]+', '', p, flags=re.I)
    p = p.replace('/{0}v3/', '/v3/').replace('/{0}v3', '/v3')
    if p.startswith('v3/'): p = '/' + p
    if mode == 'v3' and p.startswith('/') and not p.startswith('/v3/'):
        # endpoint_literals can begin after the {0}v3 marker; restore the protocol prefix.
        p = '/v3' + p
    p = re.sub(r'//+', '/', p)
    p = re.sub(r'\{[^{}]*(?:Escape|UrlQueryHelper|ToParams)[^{}]*$', '{value}', p)
    return p


def tenpay_endpoint_literals(text, mode):
    values = list(endpoint_literals(text))
    for literal in re.findall(r'"([^"\r\n]+)"', text):
        value = literal.strip().replace('{{', '{').replace('}}', '}')
        starts = [value.find('/{0}v3/'), value.find('/v3/'), value.find('v3/')]
        starts = [x for x in starts if x >= 0]
        if starts:
            value = value[min(starts):]
            if value.startswith('v3/'): value = '/' + value
            values.append(value)
    normalized = []
    for value in values:
        path = normalize_path(value, mode)
        if path.startswith('/') and path not in normalized:
            normalized.append(path)
    return normalized


def infer_verb(text):
    checks = [
        ('DELETE', r'ApiRequestMethod\.DELETE|HttpMethod\.Delete|CommonJsonSendType\.DELETE|\bDELETE\b'),
        ('PATCH', r'ApiRequestMethod\.PATCH|HttpMethod\.Patch|CommonJsonSendType\.PATCH|\bPATCH\b'),
        ('PUT', r'ApiRequestMethod\.PUT|HttpMethod\.Put|CommonJsonSendType\.PUT|\bPUT\b'),
        ('GET', r'ApiRequestMethod\.GET|HttpMethod\.Get|CommonJsonSendType\.GET|HttpGet|\.GetAsync\s*\('),
    ]
    for verb, pattern in checks:
        if re.search(pattern, text): return verb
    return 'POST'


def transport_kind(method, text):
    params = method['params']
    if re.search(r'Multipart|PostFile|StreamContent|ByteArrayContent|FormData', text, re.I):
        return 'multipart'
    if re.search(r'(?:Stream\s+(?:destination|fileStream)|Download(?:File)?Async)', params + '\n' + text, re.I):
        return 'download'
    return 'json'


def certificate_required(path, method, text):
    joined = (path + '\n' + method['params'] + '\n' + text).lower()
    return any(token in joined for token in (
        '/secapi/', 'mmpaymkttransfers', 'mmpaysptrans', 'sendredpack',
        'sendgroupredpack', 'sendworkwxredpack', 'pay_bank', 'certpath',
        'certpassword', 'x509certificate', 'clientcertificate'))


def support_files(path):
    prefix = path.stem.split('.')[0]
    return sorted(set([path] + list(path.parent.glob(prefix + '*.cs'))))


def parse_family(path):
    methods = []
    constants = {}
    next_id = 0
    for file in support_files(path):
        clean = strip_comments(file.read_text(encoding='utf-8-sig', errors='ignore'))
        parsed, next_id = parse_methods(clean, file.as_posix(), next_id)
        methods.extend(parsed)
        for name, values in endpoint_constants(clean).items():
            constants.setdefault(name, []).extend(values)
    by_name = {}
    for method in methods:
        by_name.setdefault(method['name'], []).append(method)
    return methods, by_name, constants


SPECIAL_PATHS = {
    'CertificatesAsync': '/v3/certificates',
    'GetPublicKeysAsync': '/v3/pub_key',
}


def choose_path(mode, method, expanded, constants):
    if mode == 'v2' and method['name'] in LEGACY_PATHS:
        return LEGACY_PATHS[method['name']], False, [LEGACY_PATHS[method['name']]]
    endpoints = tenpay_endpoint_literals(expanded, mode)
    if not endpoints:
        for name, values in constants.items():
            if re.search(r'\b' + re.escape(name) + r'\b', expanded):
                endpoints.extend(normalize_path(v, mode) for v in values)
    if method['name'] in SPECIAL_PATHS:
        special = SPECIAL_PATHS[method['name']]
        return special, ('{' in special or '}' in special), [special]
    normalized = []
    for endpoint in endpoints:
        if endpoint.startswith('/') and endpoint not in normalized:
            normalized.append(endpoint)
    if mode == 'v3':
        preferred = [p for p in normalized if p.startswith('/v3/')]
        if preferred: normalized = preferred
    if len(normalized) == 1:
        p = normalized[0]
        dynamic = '{' in p or '}' in p or ' ' in p
        return p, dynamic, normalized
    if normalized:
        # Expanded helper bodies often contribute a base prefix followed by the
        # actual concrete URL. The concrete URL is the longest candidate.
        p = sorted(normalized, key=lambda value: (len(value.rstrip('/')), len(value)), reverse=True)[0]
        return p, ('{' in p or '}' in p or ' ' in p), normalized
    return '', True, normalized


def normalized_name(text):
    return re.sub(r'[^a-z0-9]', '', text.lower())


def placeholder_field(placeholder, fields, method):
    raw = placeholder.strip()
    if raw.isdigit(): return None
    raw = raw.replace('data.', '').replace('request.', '')
    for prefix in ('escaped', 'escape'):
        if raw.lower().startswith(prefix) and len(raw) > len(prefix):
            raw = raw[len(prefix):]
            raw = raw[:1].lower() + raw[1:]
    candidates = [raw]
    if raw == 'value':
        candidates = [name for _, name in filtered_params(method['params'], '')]
    for candidate in candidates:
        want = normalized_name(candidate)
        for field in fields:
            if normalized_name(field.key) == want or normalized_name(field.name) == want:
                return field
    return fields[0] if raw == 'value' and fields else None


def typed_request_parts(api_class, entry, index, mode, models):
    method = entry['_method']
    endpoint = entry['path'] or ''
    path_only = base_path(endpoint) if endpoint else ''
    credential = 'access_token' if mode == 'legacy_mp' else ''
    fields, placement = build_request_fields(method, endpoint, entry['verb'], credential, index, method['source'])
    placeholders = re.findall(r'\{([^{}]+)\}', path_only)
    path_fields = {}
    for placeholder in placeholders:
        field = placeholder_field(placeholder, fields, method)
        if field:
            placement[field.key] = 'path'
            path_fields[placeholder] = field.key
    base = api_class + re.sub(r'Async$', '', entry['name'])
    request_name = base + 'Request'
    response_name = base + 'Response'
    lines = [f'class {request_name} {{']
    if mode == 'v2':
        lines += ['    WechatPayV2Request request;', '']
        lines += [f'    public {request_name}() {{ this.request = new WechatPayV2Request(); }}', '']
    else:
        lines += ['    WechatTypedRequest request;', '    string overridePath;', '']
        lines += [f'    public {request_name}() {{', '        this.request = new WechatTypedRequest();', '        this.overridePath = "";', '    }', '']
    seen = set()
    for field in fields:
        method_name = pascal(field.name)
        if method_name in seen: continue
        seen.add(method_name)
        ztype = models.type(field.cs_type, field.source_path or method['source'])
        place = placement.get(field.key, 'body')
        lines += [f'    {request_name} {method_name}({ztype} fieldValue) {{']
        if mode == 'v2':
            if ztype == 'string': value = 'fieldValue'
            elif ztype in ('int', 'long', 'double', 'bool'): value = 'Convert.ToString(fieldValue)'
            elif ztype == 'JsonValue': value = 'fieldValue.ToJson()'
            else: value = f'Json.Serialize<{ztype}>(fieldValue)'
            lines += [f'        this.request.Set("{field.key}", {value});']
        elif ztype.startswith('List<') and ztype[5:-1] in ('string','int','long','double','bool'):
            inner = ztype[5:-1]
            suffix = {'string':'Strings','int':'Ints','long':'Longs','double':'Doubles','bool':'Bools'}[inner]
            prefix = {'query':'Query', 'path':'Path'}.get(place, 'Body')
            if place == 'path':
                lines += [f'        this.request.PathValue("{field.key}", JsonValue.Parse(Json.Serialize<List<{inner}>>(fieldValue)));']
            else:
                lines += [f'        this.request.{prefix}{suffix}("{field.key}", fieldValue);']
        elif ztype in ('string','int','long','double','bool','JsonValue'):
            prefix = {'query':'Query', 'path':'Path'}.get(place, 'Body')
            call = {'string':'String','int':'Int','long':'Long','double':'Double','bool':'Bool','JsonValue':'Value'}[ztype]
            lines += [f'        this.request.{prefix}{call}("{field.key}", fieldValue);']
        else:
            if place == 'path':
                lines += [f'        this.request.PathValue("{field.key}", JsonValue.Parse(Json.Serialize<{ztype}>(fieldValue)));']
            else:
                prefix = 'Query' if place == 'query' else 'Body'
                lines += [f'        this.request.{prefix}Json("{field.key}", Json.Serialize<{ztype}>(fieldValue));']
        lines += ['        return this;', '    }', '']
    if mode != 'v2':
        lines += [f'    {request_name} RequestPath(string fieldValue) {{', '        this.overridePath = fieldValue;', '        return this;', '    }', '']
        lines += ['    string Path() {', '        if (this.overridePath.Length > 0) { return this.overridePath; }', f'        string result = "{path_only}";']
        if '{1}' in path_only:
            lines += ['        string partner = "";', '        if (this.request.HasValue("sp_mchid")) { partner = "partner/"; }', '        result = result.Replace("{1}", partner);']
        if '{0}' in path_only:
            lines += ['        result = result.Replace("{0}", "");']
        for placeholder, key in path_fields.items():
            lines += [f'        result = result.Replace("{{{placeholder}}}", Encoding.UrlEncode(this.request.ValueText("{key}")));']
        lines += ['        return result;', '    }', '    string QueryText() { return this.request.QueryText(); }', '    string JsonBody() { return this.request.JsonBody(); }', '']
    else:
        lines += ['    WechatPayV2Request Inner() { return this.request; }', '']
    lines += ['}', '']
    response_cs, rfields = response_fields(method, index, method['source'])
    shared_base = '' if mode == 'v2' else models.model_type(response_cs, method['source'])
    if shared_base:
        lines += [f'class {response_name} : {shared_base} {{', '    public string Raw;', '}', '']
    else:
        lines += [f'class {response_name} {{', '    public string Raw;']
        used = {'Raw'}
        for field in rfields:
            name = safe(field.key)
            if name in used: continue
            used.add(name)
            ztype = models.type(field.cs_type, field.source_path or method['source'])
            # V2 XML values stay strings so identifiers keep leading zeroes.
            if mode == 'v2' and ztype != 'JsonValue': ztype = 'string'
            lines.append(f'    public {ztype} {name};')
        if not rfields: lines.append('    public JsonValue Value;')
        lines += ['}', '']
    return request_name, response_name, lines, rfields


def raw_method_lines(entry, mode):
    name = re.sub(r'Async$', 'RawAsync', entry['name'])
    path = base_path(entry['path']) if entry['path'] else ''
    if mode == 'v2':
        if entry['dynamic']:
            return [f'    async WechatRawResponse {name}(string path, WechatPayV2Request request) {{', f'        return await this.client.RequestAsync(path, request, {str(entry["certificate"]).lower()});', '    }', '']
        return [f'    async WechatRawResponse {name}(WechatPayV2Request request) {{', f'        return await this.client.RequestAsync("{path}", request, {str(entry["certificate"]).lower()});', '    }', '']
    if entry['kind'] == 'multipart':
        return [f'    async WechatRawResponse {name}(string path, string query, WechatMultipart multipart) {{', f'        return await this.client.RequestMultipartAsync("{entry["verb"]}", path, query, multipart);', '    }', '']
    if entry['verb'] in ('GET', 'DELETE'):
        return [f'    async WechatRawResponse {name}(string path, string query) {{', f'        return await this.client.RequestJsonAsync("{entry["verb"]}", path, query, "");', '    }', '']
    return [f'    async WechatRawResponse {name}(string path, string query, string jsonBody) {{', f'        return await this.client.RequestJsonAsync("{entry["verb"]}", path, query, jsonBody);', '    }', '']


def render_module(group, root, path, mode, index, models):
    methods, by_name, constants = parse_family(path)
    own = [m for m in methods if Path(m['source']).resolve() == path.resolve() and is_public_task(m)]
    if not own: return None
    seen = set(); entries = []
    for method in own:
        if method['name'] in seen: continue
        seen.add(method['name'])
        expanded = expanded_body(method, by_name)
        endpoint, dynamic, candidates = choose_path(mode, method, expanded, constants)
        verb = infer_verb(expanded)
        kind = transport_kind(method, expanded)
        entries.append({
            'name': method['name'], 'verb': verb, 'path': endpoint,
            'dynamic': dynamic, 'candidates': candidates, 'kind': kind,
            'params': ' '.join(method['params'].split()),
            'certificate': certificate_required(endpoint, method, expanded) if mode == 'v2' else False,
            '_method': method,
        })
    if not entries: return None
    cls = class_name(group, root, path)
    rel = path.relative_to(root).as_posix()
    client = 'WechatPayV3Client' if mode == 'v3' else ('WechatPayLegacyMpClient' if mode == 'legacy_mp' else 'WechatPayV2Client')
    lines = [
        'using System;', 'using System.Json;', 'using System.Text;', 'using Sdk.Wechat;',
        'using Sdk.Wechat.Models.TenPay;', '',
        'namespace Sdk.Wechat.TenPay;', '',
        '/// <summary>', f'/// {rel} 的 Zan 微信支付强类型接口。',
        '/// 商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。',
        f'/// {MARKER}; regenerate with scripts/generate_wechat_tenpay_apis.py.',
        '/// </summary>',
    ]
    declarations = []; typed = []
    for entry in entries:
        req, resp, model_lines, rfields = typed_request_parts(cls, entry, index, mode, models)
        declarations += model_lines
        typed.append((entry, req, resp, rfields))
    lines += declarations
    lines += [f'class {cls} {{', f'    {client} client;', '', f'    public {cls}({client} client) {{ this.client = client; }}', '']
    for entry, req, resp, rfields in typed:
        shown_path = entry['path'] if entry['path'] else '(request.RequestPath 指定)'
        lines.append(f'    /// <summary>{entry["verb"]} {shown_path}; C# 参数：{entry["params"]}</summary>')
        if mode == 'v2':
            raw = 'WechatRawResponse raw = await this.client.RequestAsync(' + (f'"{base_path(entry["path"])}"' if entry['path'] else '""') + f', request.Inner(), {str(entry["certificate"]).lower()});'
            lines += [f'    async {resp} {entry["name"]}({req} request) {{', f'        {raw}', f'        {resp} result = new {resp}();', '        result.Raw = raw.Body;', '        WechatPayV2Request xml = WechatPayV2Request.ParseXml(raw.Body);']
            for field in rfields:
                name = safe(field.key)
                ztype = map_type(field.cs_type, index.enums)
                if ztype != 'JsonValue': lines.append(f'        result.{name} = xml.Get("{field.key}", "");')
            lines += ['        return result;', '    }', '']
        elif entry['kind'] == 'download':
            lines += [f'    async WechatRawResponse {entry["name"]}({req} request) {{', '        return await this.client.DownloadAsync(request.Path());', '    }', '']
        elif entry['kind'] == 'multipart':
            lines += [f'    async {resp} {entry["name"]}({req} request, WechatMultipart multipart) {{', f'        WechatRawResponse raw = await this.client.RequestMultipartAsync("{entry["verb"]}", request.Path(), request.QueryText(), multipart);', f'        {resp} result = Json.Deserialize<{resp}>(raw.Body);', '        result.Raw = raw.Body;', '        return result;', '    }', '']
        else:
            body = '""' if entry['verb'] in ('GET', 'DELETE') else 'request.JsonBody()'
            lines += [f'    async {resp} {entry["name"]}({req} request) {{', f'        WechatRawResponse raw = await this.client.RequestJsonAsync("{entry["verb"]}", request.Path(), request.QueryText(), {body});', f'        {resp} result = Json.Deserialize<{resp}>(raw.Body);', '        result.Raw = raw.Body;', '        return result;', '    }', '']
        lines += raw_method_lines(entry, mode)
    lines.append('}')
    return cls, entries, '\n'.join(lines) + '\n'

def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for old in OUTPUT.glob('*.zan'):
        if MARKER in old.read_text(encoding='utf-8', errors='ignore'):
            old.unlink()
    modules = []
    index = ModelIndex(VENDORED, strip_comments)
    models = SharedModelRegistry(index, 'WechatPay')
    for group, root, mode in SOURCE_GROUPS:
        for path in sorted(root.rglob('*.cs')):
            rendered = render_module(group, root, path, mode, index, models)
            if rendered is None: continue
            cls, entries, text = rendered
            filename = cls + '.zan'
            # A class name can collide for partial files with generic suffixes;
            # preserve every source surface by adding a stable numeric suffix.
            target = OUTPUT / filename
            suffix = 2
            while target.exists() and MARKER in target.read_text(encoding='utf-8', errors='ignore'):
                filename = cls + str(suffix) + '.zan'
                target = OUTPUT / filename
                suffix += 1
            target.write_text(text.replace(f'class {cls} {{', f'class {target.stem} {{')
                                   .replace(f'public {cls}(', f'public {target.stem}('),
                              encoding='utf-8', newline='\n')
            modules.append({'group': group, 'source': path.relative_to(root).as_posix(),
                            'file': filename, 'class': target.stem, 'mode': mode,
                            'methods': [{k: v for k, v in entry.items() if not k.startswith('_')} for entry in entries]})
    models_dir = ROOT / 'stdlib/Sdk/Wechat/Models/TenPay'
    models_dir.mkdir(parents=True, exist_ok=True)
    for old in models_dir.glob('*.zan'):
        if MARKER in old.read_text(encoding='utf-8', errors='ignore'):
            old.unlink()
    (models_dir / 'WechatPayModels.zan').write_text(
        models.render('Sdk.Wechat.Models.TenPay', MARKER,
                      'scripts/generate_wechat_tenpay_apis.py'),
        encoding='utf-8', newline='\n')

    manifest = {
        'modules': modules,
        'module_count': len(modules),
        'method_count': sum(len(m['methods']) for m in modules),
        'dynamic_count': sum(e['dynamic'] for m in modules for e in m['methods']),
        'multipart_count': sum(e['kind'] == 'multipart' for m in modules for e in m['methods']),
        'shared_model_count': len(models.models),
    }
    path = ROOT / '_scratch/wechat_tenpay_generated_manifest.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    print('TenPay modules=' + str(manifest['module_count']),
          'methods=' + str(manifest['method_count']),
          'dynamic=' + str(manifest['dynamic_count']),
          'multipart=' + str(manifest['multipart_count']))


if __name__ == '__main__':
    main()
