#!/usr/bin/env python3
"""Shared strong-typing helpers for generated WeChat Zan SDK facades."""
from dataclasses import dataclass
from pathlib import Path
import hashlib
import re

RESERVED = {
    'abstract','as','async','await','base','bool','break','case','catch','char','class','const',
    'continue','default','delegate','do','double','else','enum','event','explicit','extern','false',
    'finally','fixed','float','for','foreach','get','goto','if','implicit','in','int','interface',
    'internal','is','lock','long','namespace','new','nint','null','object','operator','out',
    'override','params','private','protected','public','readonly','ref','return','set','short',
    'sizeof','stackalloc','static','string','struct','switch','this','throw','true','try','typeof',
    'uint','ulong','unchecked','unsafe','ushort','using','value','var','virtual','void','volatile','while'
}

SCALARS = {
    'string':'string','char':'string','int':'int','short':'int','byte':'int','sbyte':'int',
    'uint':'long','long':'long','ulong':'long','float':'double','double':'double','decimal':'double',
    'bool':'bool','DateTime':'string','DateTimeOffset':'string','Guid':'string','object':'JsonValue',
    'dynamic':'JsonValue','JObject':'JsonValue','JToken':'JsonValue','JsonValue':'JsonValue',
}
INFRA_NAMES = {
    'timeout','timeOut','cancellationToken','accessTokenOrAppKey','accessTokenOrAppId',
    'accessTokenOrAppid','accessTokenOrAppID'
}
TOKEN_BY_CREDENTIAL = {
    'access_token': {'accessToken','authorizerAccessToken','oauthAccessToken'},
    'component_access_token': {'componentAccessToken'},
    'provider_access_token': {'providerAccessToken'},
    'suite_access_token': {'suiteAccessToken'},
}


def safe(name):
    if not name: return 'value'
    name = re.sub(r'[^A-Za-z0-9_]', '_', name)
    if name[0].isdigit(): name = '_' + name
    if name in RESERVED: name += '_'
    return name


def pascal(name):
    parts = re.findall(r'[A-Za-z0-9]+', name)
    return ''.join(x[:1].upper()+x[1:] for x in parts) or 'Value'


def split_top(text, sep=','):
    out=[]; start=0; angle=paren=bracket=brace=0; quote=''; esc=False
    for i,ch in enumerate(text):
        if quote:
            if esc: esc=False
            elif ch=='\\': esc=True
            elif ch==quote: quote=''
            continue
        if ch in ('"',"'"): quote=ch; continue
        if ch=='<': angle+=1
        elif ch=='>': angle=max(0,angle-1)
        elif ch=='(': paren+=1
        elif ch==')': paren=max(0,paren-1)
        elif ch=='[': bracket+=1
        elif ch==']': bracket=max(0,bracket-1)
        elif ch=='{': brace+=1
        elif ch=='}': brace=max(0,brace-1)
        elif ch==sep and angle==paren==bracket==brace==0:
            out.append(text[start:i].strip()); start=i+1
    tail=text[start:].strip()
    if tail: out.append(tail)
    return out


def parse_params(text):
    result=[]
    for raw in split_top(text):
        raw=re.sub(r'\[[^\]]+\]\s*','',raw).strip()
        raw=raw.split('=',1)[0].strip()
        raw=re.sub(r'\b(?:ref|out|in|params|this)\s+','',raw)
        m=re.match(r'(.+?)\s+([A-Za-z_]\w*)$',raw)
        if m: result.append((m.group(1).strip(),m.group(2)))
    return result


def clean_type(cs):
    cs=' '.join(cs.replace('global::','').split())
    cs=re.sub(r'\?$', '', cs)
    if cs.startswith('Nullable<') and cs.endswith('>'): cs=cs[9:-1].strip()
    return cs


def list_element(cs):
    cs=clean_type(cs)
    if cs.endswith('[]'): return cs[:-2].strip()
    for prefix in ('List<','IList<','IEnumerable<','ICollection<','HashSet<','Collection<'):
        if cs.startswith(prefix) and cs.endswith('>'): return cs[len(prefix):-1].strip()
    return None


def simple_type(cs):
    cs=clean_type(cs)
    if '<' not in cs and '.' in cs: cs=cs.rsplit('.',1)[-1]
    return cs


def map_type(cs, enum_names=None):
    cs=clean_type(cs)
    elem=list_element(cs)
    if elem is not None:
        inner=map_type(elem,enum_names)
        if inner in ('string','int','long','double','bool'): return f'List<{inner}>'
        return 'JsonValue'
    if cs.startswith('Dictionary<') or cs.startswith('IDictionary<'): return 'JsonValue'
    s=simple_type(cs)
    if s in SCALARS: return SCALARS[s]
    if enum_names and s in enum_names: return 'int'
    return 'JsonValue'


def find_matching(text, start, op='{', cl='}'):
    depth=0; state='code'; esc=False; i=start
    while i<len(text):
        c=text[i]; n=text[i+1] if i+1<len(text) else ''
        if state=='line':
            if c in '\r\n': state='code'
        elif state=='block':
            if c=='*' and n=='/': state='code'; i+=1
        elif state=='string':
            if esc: esc=False
            elif c=='\\': esc=True
            elif c=='"': state='code'
        elif state=='char':
            if esc: esc=False
            elif c=='\\': esc=True
            elif c=="'": state='code'
        else:
            if c=='/' and n=='/': state='line'; i+=1
            elif c=='/' and n=='*': state='block'; i+=1
            elif c=='"': state='string'
            elif c=="'": state='char'
            elif c==op: depth+=1
            elif c==cl:
                depth-=1
                if depth==0:return i
        i+=1
    return -1

@dataclass
class Field:
    key:str
    cs_type:str
    name:str
    source_path:object=None

@dataclass
class Model:
    name:str
    path:Path
    namespace:str
    owner:str
    fields:list
    base:str

class ModelIndex:
    def __init__(self, root, strip_comments=lambda x:x):
        self.root=Path(root)
        self.by_name={}
        self.enums=set()
        self.strip_comments=strip_comments
        self._scan()

    def _scan(self):
        partial={}
        class_pattern=re.compile(
            r'\b(?:public\s+|internal\s+|private\s+|protected\s+|partial\s+|sealed\s+|abstract\s+)*'
            r'class\s+(\w+)(?:\s*:\s*([^\{]+))?\s*\{')
        for path in self.root.rglob('*.cs'):
            raw=path.read_text(encoding='utf-8-sig',errors='ignore')
            text=self.strip_comments(raw)
            ns=''
            nm=re.search(r'\bnamespace\s+([A-Za-z0-9_.]+)',text)
            if nm: ns=nm.group(1)
            for em in re.finditer(r'\benum\s+(\w+)',text): self.enums.add(em.group(1))
            declarations=[]
            for cm in class_pattern.finditer(text):
                end=find_matching(text,cm.end()-1)
                if end<0: continue
                declarations.append((cm,end))
            for cm,end in declarations:
                parents=[]
                for pm,pend in declarations:
                    if pm.start()<cm.start()<end and end<pend:
                        parents.append((pm.start(),pm.group(1)))
                parents.sort()
                owner='.'.join(name for _,name in parents)
                body=text[cm.end():end]
                fields=self._fields(body)
                for field in fields:
                    field.source_path = path
                base=(cm.group(2) or '').split(',')[0].strip()
                key=(ns,owner,cm.group(1),path.resolve())
                partial.setdefault(key,[]).extend(fields)
                model=Model(cm.group(1),path,ns,owner,partial[key],base)
                self.by_name.setdefault(model.name,[]).append(model)
        # Collapse exact duplicate scans/partial identities and merge only the
        # same namespace + containing-class chain + type name. Nested C# DTOs
        # such as TransactionsRequestData.Amount must remain distinct from
        # CombineTransactionsRequestData.Amount even when namespaces match.
        merged={}
        for name,models in self.by_name.items():
            groups={}
            for m in models:
                key=(m.namespace,m.owner,m.name)
                if key not in groups:
                    groups[key]=Model(m.name,m.path,m.namespace,m.owner,[],m.base)
                seen={(f.key,f.name) for f in groups[key].fields}
                for f in m.fields:
                    if (f.key,f.name) not in seen:
                        groups[key].fields.append(f); seen.add((f.key,f.name))
                if not groups[key].base and m.base: groups[key].base=m.base
            merged[name]=list(groups.values())
        self.by_name=merged

    def _fields(self, body):
        fields=[]
        pattern=re.compile(r'((?:\s*\[[^\]]+\]\s*)*)\bpublic\s+(?!class\b|enum\b|interface\b|static\b)([A-Za-z0-9_\.<>\[\],?\s]+?)\s+(\w+)\s*\{\s*get\s*;?\s*(?:private\s+|protected\s+|internal\s+)?set\s*;?\s*\}',re.S)
        for m in pattern.finditer(body):
            depth=0
            for ch in body[:m.start()]:
                if ch=='{':depth+=1
                elif ch=='}':depth-=1
            if depth!=0: continue
            attrs=m.group(1); key=m.group(3)
            km=re.search(r'(?:JsonProperty|DataMember|XmlElement|JsonPropertyName)\s*\(\s*(?:PropertyName\s*=\s*|Name\s*=\s*)?"([^"]+)"',attrs)
            if km:key=km.group(1)
            fields.append(Field(key,m.group(2).strip(),m.group(3)))
        # Public fields are also used by a few DTOs.
        for m in re.finditer(r'\bpublic\s+(?!const\b|static\b)([A-Za-z0-9_\.<>\[\],?]+)\s+(\w+)\s*;',body):
            depth=0
            for ch in body[:m.start()]:
                if ch=='{':depth+=1
                elif ch=='}':depth-=1
            if depth==0: fields.append(Field(m.group(2),m.group(1),m.group(2)))
        return fields

    def resolve(self, cs_type, source_path=None):
        name=simple_type(cs_type)
        candidates=self.by_name.get(name,[])
        if not candidates:return None
        if len(candidates)==1:return candidates[0]
        if source_path:
            source=Path(source_path).resolve()
            def score(m):
                a=source.parts; b=m.path.resolve().parts; n=0
                for x,y in zip(a,b):
                    if x.lower()!=y.lower():break
                    n+=1
                return n
            candidates=sorted(candidates,key=score,reverse=True)
        return candidates[0]

    def fields_for(self, cs_type, source_path=None, seen=None):
        model=self.resolve(cs_type,source_path)
        if not model:return []
        result=list(model.fields)
        if model.base:
            if seen is None:seen=set()
            if model.name not in seen:
                seen.add(model.name)
                for f in self.fields_for(model.base,model.path,seen):
                    if not any(x.key==f.key for x in result):result.append(f)
        return result



class SharedModelRegistry:
    """Collect C# DTO identities once and emit reusable Zan models per product."""
    def __init__(self, index, prefix):
        self.index = index
        self.prefix = prefix
        self.names = {}
        self.models = {}
        self.typed_fields = {}
        self.collecting = set()

    def _identity(self, model):
        return (model.namespace, model.owner, model.name)

    def _name(self, model):
        key = self._identity(model)
        if key in self.names:
            return self.names[key]
        candidates = {
            (m.namespace, m.owner, m.name)
            for m in self.index.by_name.get(model.name, [])
        }
        ignored = {
            'Senparc', 'Weixin', 'TenPay', 'TenPayV3', 'MP', 'Work',
            'WxOpen', 'Open', 'Apis', 'API', 'AdvancedAPIs', 'AdvancedApi',
            'Entities', 'Entity', 'Models', 'Model', 'NotifyJson',
            'RequestData', 'ResponseData', 'Request', 'Requests',
            'Response', 'Responses', 'Result', 'Results', 'Data'
        }
        name = self.prefix + pascal(model.name)
        if len(candidates) > 1:
            try:
                rel = model.path.resolve().relative_to(self.index.root.resolve())
                raw_parts = list(rel.with_suffix('').parts)
            except ValueError:
                raw_parts = model.namespace.split('.') + [model.path.stem]
            parts = []
            for raw in raw_parts:
                for item in re.findall(r'[A-Za-z0-9]+', raw):
                    if item in ignored or item == model.name:
                        continue
                    if not parts or parts[-1] != item:
                        parts.append(item)
            qualifier = ''.join(pascal(x) for x in parts[-3:]) or 'Shared'
            name = self.prefix + qualifier + pascal(model.name)
        if name in self.names.values():
            try:
                rel = model.path.resolve().relative_to(self.index.root.resolve())
                raw_parts = list(rel.with_suffix('').parts)
            except ValueError:
                raw_parts = model.namespace.split('.') + [model.path.stem]
            raw_parts += model.owner.split('.') if model.owner else []
            parts = []
            for raw in raw_parts:
                for item in re.findall(r'[A-Za-z0-9]+', raw):
                    if item in ignored or item == model.name:
                        continue
                    if not parts or parts[-1] != item:
                        parts.append(item)
            qualifier = ''.join(pascal(x) for x in parts[-4:]) or 'Shared'
            name = self.prefix + qualifier + pascal(model.name)
        if name in self.names.values():
            digest = hashlib.sha1((model.namespace + '.' + model.owner + '.' + model.name).encode('utf-8')).hexdigest()[:6]
            name += digest
        self.names[key] = name
        return name

    def type(self, cs_type, source_path=None):
        cs = clean_type(cs_type)
        elem = list_element(cs)
        if elem is not None:
            inner = self.type(elem, source_path)
            if inner == 'JsonValue':
                return 'JsonValue'
            return f'List<{inner}>'
        if cs.startswith('Dictionary<') or cs.startswith('IDictionary<'):
            return 'JsonValue'
        simple = simple_type(cs)
        if simple in SCALARS:
            return SCALARS[simple]
        if simple in self.index.enums:
            return 'int'
        model = self.index.resolve(cs, source_path)
        if model is None:
            return 'JsonValue'
        name = self._name(model)
        self._collect(model)
        return name

    def model_type(self, cs_type, source_path=None):
        model = self.index.resolve(cs_type, source_path)
        if model is None:
            return ''
        return self.type(cs_type, source_path)

    def _collect(self, model):
        key = self._identity(model)
        if key in self.models or key in self.collecting:
            return
        self.collecting.add(key)
        self.models[key] = model
        fields = []
        seen = set()
        for field in self.index.fields_for(model.name, model.path):
            fname = safe(field.key)
            if fname in seen:
                continue
            seen.add(fname)
            fields.append((self.type(field.cs_type, field.source_path or model.path), fname))
        self.typed_fields[key] = fields
        self.collecting.remove(key)

    def render(self, namespace, marker, generator):
        lines = [
            'using System;',
            'using System.Json;',
            '',
            f'namespace {namespace};',
            '',
            '/// <summary>',
            '/// Shared DTO entities generated from the vendored C# SDK.',
            '/// One C# namespace/type identity maps to exactly one Zan model.',
            f'/// {marker}; regenerate with {generator}.',
            '/// </summary>',
        ]
        ordered = sorted(self.models, key=lambda key: self.names[key])
        for key in ordered:
            lines.append(f'class {self.names[key]} {{')
            fields = self.typed_fields.get(key, [])
            if fields:
                for ztype, fname in fields:
                    lines.append(f'    public {ztype} {fname};')
            else:
                lines.append('    public JsonValue Value;')
            lines += ['}', '']
        return '\n'.join(lines).rstrip() + '\n'


# Kept as an alias for generator extensions that imported the old name.
TypeEmitter = SharedModelRegistry

def unwrap_task(ret):
    ret=clean_type(ret)
    m=re.search(r'(?:Task|ValueTask)\s*<\s*(.+)\s*>',ret)
    return m.group(1).strip() if m else ret


def credential_enum(name, product):
    if not name:return 'WechatCredentialKind.None'
    if name=='provider_access_token':return 'WechatCredentialKind.ProviderAccessToken'
    if name=='suite_access_token':return 'WechatCredentialKind.SuiteAccessToken'
    if name=='component_access_token':return 'WechatCredentialKind.ComponentAccessToken'
    if product=='Open':return 'WechatCredentialKind.AuthorizerAccessToken'
    return 'WechatCredentialKind.AccessToken'


def filtered_params(method_params, credential):
    out=[]
    token_names=TOKEN_BY_CREDENTIAL.get(credential,set())
    for cs,name in parse_params(method_params):
        if name in INFRA_NAMES or name in token_names:continue
        if name.lower() in ('timeout','cancellationtoken','destination','filestream'):continue
        if 'CancellationToken' in cs or re.search(r'(^|[.])Stream$', clean_type(cs)):continue
        out.append((cs,name))
    return out


def query_keys(endpoint, credential):
    if '?' not in endpoint:return []
    q=endpoint.split('?',1)[1]
    keys=[]
    for part in q.split('&'):
        key=part.split('=',1)[0].strip()
        if key and key!=credential and re.fullmatch(r'[A-Za-z_][A-Za-z0-9_\-.]*',key):keys.append(key)
    return keys


def anonymous_fields(body, params):
    pmap={n:(t,n) for t,n in params}; normalized={re.sub('_','',n).lower():n for _,n in params}
    best=[]
    for m in re.finditer(r'\bnew\s*\{',body):
        end=find_matching(body,m.end()-1)
        if end<0:continue
        entries=[]
        for item in split_top(body[m.end():end]):
            if not item:continue
            if '=' in item:
                key,expr=item.split('=',1); key=key.strip(); expr=expr.strip()
                refs=re.findall(r'\b[A-Za-z_]\w*\b',expr)
                src=next((r for r in refs if r in pmap),None)
                if src: entries.append(Field(key,pmap[src][0],src))
            else:
                name=item.strip()
                if name in pmap: entries.append(Field(name,pmap[name][0],name))
        if len(entries)>len(best):best=entries
    return best


def build_request_fields(method, endpoint, verb, credential, index, source_path):
    params=filtered_params(method['params'],credential)
    qkeys=query_keys(endpoint,credential)
    fields=[]
    model_params=[]; scalar_params=[]
    for cs,name in params:
        mf=index.fields_for(cs,source_path)
        if mf:model_params.append((cs,name,mf))
        else:scalar_params.append((cs,name))
    if model_params:
        for cs,name,mf in model_params:
            for f in mf:
                if not any(x.key==f.key for x in fields):fields.append(f)
        for cs,name in scalar_params:
            fields.append(Field(name,cs,name,source_path))
    elif verb not in ('GET','DELETE'):
        anon=anonymous_fields(method.get('body',''),params)
        fields=anon if anon else [Field(n,t,n,source_path) for t,n in params]
    else:
        fields=[Field(n,t,n,source_path) for t,n in params]
    # Query strings often use snake_case while C# scalar parameters use camelCase.
    # Preserve the protocol key and the source parameter name independently.
    qnorm={re.sub('_','',key).lower():key for key in qkeys}
    remapped=[]
    for f in fields:
        protocol=qnorm.get(re.sub('_','',f.key).lower(),f.key)
        remapped.append(Field(protocol,f.cs_type,f.name,f.source_path))
    fields=remapped
    placement={f.key:('query' if verb in ('GET','DELETE') else 'body') for f in fields}
    norm={re.sub('_','',f.key).lower():f.key for f in fields}
    unused=[f.key for f in fields]
    for key in qkeys:
        target=norm.get(re.sub('_','',key).lower())
        if target is None and unused:
            target=unused[0]
        if target:
            placement[target]='query'
            if target in unused:unused.remove(target)
    return fields,placement


def response_fields(method,index,source_path):
    typ=unwrap_task(method['ret'])
    return typ,index.fields_for(typ,source_path)


def setter_call(ztype, place):
    prefix='Query' if place=='query' else 'Body'
    return {
        'string':prefix+'String','int':prefix+'Int','long':prefix+'Long',
        'double':prefix+'Double','bool':prefix+'Bool','JsonValue':prefix+'Value'
    }.get(ztype,prefix+'Json')


def emit_typed_classes(api_class, method, endpoint, verb, credential, index,
                       source_path, models):
    base=api_class + re.sub(r'Async$','',method['name'])
    req=base+'Request'; resp=base+'Response'
    fields,placement=build_request_fields(method,endpoint,verb,credential,index,source_path)
    response_cs,rfields=response_fields(method,index,source_path)
    lines=[]
    if fields:
        lines += [f'class {req} {{','    WechatTypedRequest request;','',f'    public {req}() {{ this.request = new WechatTypedRequest(); }}','']
        seen=set()
        for f in fields:
            mname=pascal(f.name)
            if mname in seen:continue
            seen.add(mname)
            zt=models.type(f.cs_type, f.source_path or source_path)
            place=placement.get(f.key,'body')
            lines += [f'    {req} {mname}({zt} fieldValue) {{']
            if zt.startswith('List<') and zt[5:-1] in ('string','int','long','double','bool'):
                inner=zt[5:-1]
                suffix={'string':'Strings','int':'Ints','long':'Longs','double':'Doubles','bool':'Bools'}[inner]
                lines += [f'        this.request.{("Query" if place=="query" else "Body")}{suffix}("{f.key}", fieldValue);']
            elif zt in ('string','int','long','double','bool','JsonValue'):
                call=setter_call(zt,place)
                lines += [f'        this.request.{call}("{f.key}", fieldValue);']
            else:
                prefix='Query' if place=='query' else 'Body'
                lines += [f'        this.request.{prefix}Json("{f.key}", Json.Serialize<{zt}>(fieldValue));']
            lines += ['        return this;','    }','']
        lines += ['    string QueryText() { return this.request.QueryText(); }','    string JsonBody() { return this.request.JsonBody(); }','}','']
    shared_base=models.model_type(response_cs, source_path)
    if shared_base:
        lines += [f'class {resp} : {shared_base} {{','    public string Raw;','}','']
    else:
        lines += [f'class {resp} {{','    public string Raw;']
        seen={'Raw'}
        for f in rfields:
            name=safe(f.key)
            if name in seen:continue
            seen.add(name)
            lines.append(f'    public {models.type(f.cs_type, f.source_path or source_path)} {name};')
        if not rfields:
            lines.append('    public JsonValue Value;')
        lines += ['}','']
    return req if fields else '',resp,lines
