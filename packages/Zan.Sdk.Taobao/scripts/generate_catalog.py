#!/usr/bin/env python3
"""Generate modular strongly-typed Zan SDK namespaces from Topsdk + taobao-sdk-net-auto."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path


ZAN_KEYWORDS = {
    "abstract", "as", "async", "await", "base", "bool", "break", "byte", "case",
    "catch", "char", "class", "const", "continue", "decimal", "default", "delegate",
    "do", "double", "else", "enum", "event", "explicit", "extern", "false", "finally",
    "fixed", "float", "for", "foreach", "get", "goto", "if", "implicit", "in", "int",
    "interface", "internal", "is", "lock", "long", "namespace", "new", "nint", "nuint",
    "null", "object", "operator", "out", "override", "params", "partial", "private",
    "protected", "public", "readonly", "ref", "return", "sbyte", "sealed", "set",
    "short", "sizeof", "stackalloc", "static", "string", "struct", "switch", "this",
    "throw", "true", "try", "typeof", "uint", "ulong", "unchecked", "unsafe",
    "ushort", "using", "value", "var", "virtual", "void", "volatile", "weak",
    "while", "yield",
}

REQUEST_RESERVED_METHODS = {
    "BuildParameters", "BuildFiles", "GetApiMethod", "Validate", "SetRaw", "SetRawJson",
}
MODEL_RESERVED_FIELDS = {"Raw", "FromJson", "FromResponse"}
STRUCT_RESERVED_METHODS = {"ToJsonValue", "ToJson", "SetRaw"}

UNIVERSALBP_BUCKETS = {
    "account": "UniversalbpAccount",
    "member": "UniversalbpAccount",
    "diagnose": "UniversalbpAccount",
    "solution": "UniversalbpAccount",
    "check": "UniversalbpAccount",
    "campaign": "UniversalbpCampaign",
    "campaigngroup": "UniversalbpCampaign",
    "adgroup": "UniversalbpAdgroup",
    "adzone": "UniversalbpAdgroup",
    "crowd": "UniversalbpAdgroup",
    "creative": "UniversalbpCreative",
    "material": "UniversalbpCreative",
    "bidword": "UniversalbpKeyword",
    "wordpackage": "UniversalbpKeyword",
    "label": "UniversalbpKeyword",
    "blackword": "UniversalbpKeyword",
    "potential": "UniversalbpKeyword",
    "word": "UniversalbpKeyword",
    "itemcross": "UniversalbpKeyword",
    "template": "UniversalbpKeyword",
    "report": "UniversalbpReport",
    "algo": "UniversalbpReport",
    "suggest": "UniversalbpReport",
    "commonapi": "UniversalbpReport",
    "stdcategory": "UniversalbpReport",
    "shopcategory": "UniversalbpReport",
}


def module_for_api(api_code: str) -> str:
    parts = api_code.split(".")
    if api_code.startswith("taobao.universalbp."):
        sub = parts[3] if len(parts) > 3 and parts[2] == "new" else (parts[2] if len(parts) > 2 else "commonapi")
        return UNIVERSALBP_BUCKETS.get(sub, "UniversalbpReport")
    if api_code.startswith("tmall.channel."):
        return "Channel"
    if api_code.startswith("taobao.fenxiao.dealer."):
        return "FenxiaoDealer"
    if api_code.startswith("taobao.fenxiao."):
        return "Fenxiao"
    if (
        api_code.startswith("taobao.item.")
        or api_code.startswith("taobao.items.")
        or api_code.startswith("taobao.skus.")
        or api_code.startswith("taobao.itemcats.")
        or api_code.startswith("tmall.item.")
        or api_code.startswith("alibaba.gpu.")
    ):
        return "Item"
    if api_code.startswith("taobao.refund.") or api_code.startswith("taobao.refunds.") or api_code.startswith("taobao.rp."):
        return "Refund"
    if (
        api_code.startswith("taobao.inventory.")
        or api_code.startswith("taobao.scitem.")
        or api_code.startswith("taobao.location.")
        or api_code.startswith("tmall.inventory.")
    ):
        return "Inventory"
    if api_code.startswith("taobao.fuwu.") or api_code.startswith("taobao.vas.") or api_code.startswith("taobao.appstore."):
        return "Fuwu"
    if api_code.startswith("taobao.sellercenter.") or api_code.startswith("taobao.subuser.") or api_code.startswith("taobao.subusers."):
        return "Subuser"
    if api_code.startswith("taobao.promotion.") or api_code.startswith("taobao.marketing."):
        return "Promotion"
    if (
        api_code.startswith("taobao.shop.")
        or api_code.startswith("taobao.shopcats.")
        or api_code.startswith("taobao.sellercats.")
        or api_code.startswith("taobao.region.")
    ):
        return "Shop"
    if api_code.startswith("taobao.miniapp.") or api_code.startswith("taobao.openlink."):
        return "Miniapp"
    if api_code.startswith("taobao.tmc."):
        return "Tmc"
    return "Misc"


def to_pascal(name: str) -> str:
    tokens = re.split(r"[^A-Za-z0-9]+", name)
    out = []
    for tok in tokens:
        if not tok:
            continue
        out.append(tok[0].upper() + tok[1:])
    res = "".join(out)
    if not res:
        res = "Field"
    if res[0].isdigit():
        res = "N" + res
    while "__" in res:
        res = res.replace("__", "_")
    if res.lower() in ZAN_KEYWORDS:
        res = res + "Value"
    return res


def sanitize_ident(name: str, is_public: bool = True) -> str:
    res = re.sub(r"[^A-Za-z0-9_]", "", name)
    while "__" in res:
        res = res.replace("__", "_")
    if not res:
        res = "Item"
    if is_public:
        res = res[0].upper() + res[1:]
    if res[0].isdigit():
        res = ("F" if is_public else "f") + res
    if res.lower() in ZAN_KEYWORDS:
        res = res + ("Value" if is_public else "Val")
    return res


def sanitize_comment(text: str) -> str:
    lines = []
    for raw in text.splitlines():
        s = raw.strip().lstrip("/*").rstrip("*/").strip()
        if s.startswith("///"):
            s = s[3:].strip()
        if s.startswith("//"):
            s = s[2:].strip()
        if s in ("<summary>", "</summary>", ""):
            continue
        s = s.replace("<summary>", "").replace("</summary>", "").strip()
        if s:
            lines.append(s)
    return " ".join(lines)


@dataclass
class FieldSpec:
    prop_name: str
    wire_name: str
    cs_type: str
    kind: str  # scalar, scalar_list, struct, struct_list, file
    base_type: str  # string, long, int, bool, double, DateTime, or class name
    item_wire: str = ""
    comment: str = ""
    required: bool = False
    has_struct_helper: bool = False
    struct_helper_type: str = ""
    struct_helper_is_list: bool = False


@dataclass
class ClassSpec:
    name: str
    fields: list[FieldSpec] = field(default_factory=list)
    comment: str = ""


@dataclass
class ApiSpec:
    api_code: str
    stem: str
    module: str
    source: str  # topsdk or net-auto
    comment: str
    req_fields: list[FieldSpec] = field(default_factory=list)
    resp_fields: list[FieldSpec] = field(default_factory=list)
    req_structs: dict[str, ClassSpec] = field(default_factory=dict)
    resp_models: dict[str, ClassSpec] = field(default_factory=dict)
    shared_models: list[str] = field(default_factory=list)


def normalize_cs_type(cs_type: str) -> tuple[str, str, str]:
    t = cs_type.strip()
    if t.endswith("?"):
        t = t[:-1]
    if t.startswith("Nullable<") and t.endswith(">"):
        t = t[9:-1]
    is_list = False
    if t.startswith("IList<") and t.endswith(">"):
        t = t[6:-1]
        is_list = True
    elif t.startswith("List<") and t.endswith(">"):
        t = t[5:-1]
        is_list = True
    t = t.replace("Top.Api.Domain.", "")
    if t in ("Int64", "long"):
        base = "long"
    elif t in ("Int32", "int"):
        base = "int"
    elif t in ("Boolean", "bool"):
        base = "bool"
    elif t in ("Double", "double", "float", "Single", "decimal", "Decimal"):
        base = "double"
    elif t in ("DateTime",):
        base = "DateTime"
    elif t in ("string", "String"):
        base = "string"
    elif t in ("TopFileItem", "FileItem"):
        return ("file", "TaobaoFileItem", cs_type)
    else:
        base = t
    if is_list:
        kind = "scalar_list" if base in ("string", "long", "int", "bool", "double", "DateTime") else "struct_list"
    else:
        kind = "scalar" if base in ("string", "long", "int", "bool", "double", "DateTime") else "struct"
    return (kind, base, cs_type)


def parse_topsdk_class_fields(txt: str, attr_kind: str) -> list[FieldSpec]:
    fields: list[FieldSpec] = []
    # Match optional comment before private field + property with [JsonPropertyName("...")]
    prop_pattern = re.compile(
        r'\[JsonPropertyName\("([^"]+)"\)\]\s*public\s+([^\s{]+)\s+([A-Za-z0-9_]+)\s*\{',
        re.MULTILINE,
    )
    # Extract comments above private fields
    priv_comments: dict[str, str] = {}
    for cm in re.finditer(r'/\*(.*?)\*/\s*private\s+[^\s;]+\s+([A-Za-z0-9_]+);', txt, re.DOTALL):
        priv_comments[cm.group(2).lower()] = sanitize_comment(cm.group(1))

    for m in prop_pattern.finditer(txt):
        wire_name = m.group(1)
        cs_type = m.group(2)
        prop_name = m.group(3)
        kind, base, _ = normalize_cs_type(cs_type)
        cmt = priv_comments.get(prop_name.lower(), "")
        fields.append(FieldSpec(
            prop_name=sanitize_ident(prop_name, True),
            wire_name=wire_name,
            cs_type=cs_type,
            kind=kind,
            base_type=base,
            comment=cmt,
        ))
    return fields


def parse_topsdk(root: Path) -> dict[str, ApiSpec]:
    apis: dict[str, ApiSpec] = {}
    top_dir = root / "Top"
    for ability_dir in sorted(top_dir.iterdir()):
        if not ability_dir.is_dir():
            continue
        req_dir = ability_dir / "Request"
        resp_dir = ability_dir / "Response"
        dom_dir = ability_dir / "Domain"
        if not req_dir.is_dir():
            continue

        # Parse ability summary comments from Ability*.cs
        api_comments: dict[str, str] = {}
        for cs_file in ability_dir.glob("*.cs"):
            ctxt = cs_file.read_text(encoding="utf-8", errors="ignore")
            for cm in re.finditer(r'/\*(.*?)\*/\s*public\s+[A-Za-z0-9_]+\s+[A-Za-z0-9_]+\s*\(', ctxt, re.DOTALL):
                lines = [ln.strip() for ln in cm.group(1).splitlines() if ln.strip()]
                if lines:
                    code_cand = lines[-1]
                    desc = " ".join(lines[:-1]) if len(lines) > 1 else lines[0]
                    api_comments[code_cand] = desc

        # Preload all Domain classes in this ability dir
        domain_cache: dict[str, ClassSpec] = {}
        if dom_dir.is_dir():
            for df in sorted(dom_dir.glob("*.cs")):
                dtxt = df.read_text(encoding="utf-8", errors="ignore")
                d_name = df.stem
                domain_cache[d_name] = ClassSpec(
                    name=sanitize_ident(d_name, True),
                    fields=parse_topsdk_class_fields(dtxt, "json"),
                )

        for rf in sorted(req_dir.glob("*.cs")):
            rtxt = rf.read_text(encoding="utf-8", errors="ignore")
            m_code = re.search(r'GetApiCode\(\)\s*\{\s*return\s+"([^"]+)"', rtxt)
            if not m_code:
                continue
            api_code = m_code.group(1)
            stem = to_pascal(api_code)
            module = module_for_api(api_code)
            req_fields = parse_topsdk_class_fields(rtxt, "json")

            # Check ToRequestParam / ToFileParam overrides in case wire_name differs
            # In Topsdk, JsonPropertyName matches requestParam.Add("...", ...)
            resp_file = resp_dir / rf.name.replace("Request.cs", "Response.cs")
            resp_fields: list[FieldSpec] = []
            if resp_file.exists():
                resp_txt = resp_file.read_text(encoding="utf-8", errors="ignore")
                resp_fields = parse_topsdk_class_fields(resp_txt, "json")

            # Collect transitive domain classes for request and response separately
            def collect_domains(seed_fields: list[FieldSpec]) -> dict[str, ClassSpec]:
                collected: dict[str, ClassSpec] = {}
                queue = [f.base_type for f in seed_fields if f.kind in ("struct", "struct_list")]
                while queue:
                    b = queue.pop(0)
                    if b in collected or b not in domain_cache:
                        continue
                    spec = domain_cache[b]
                    collected[b] = spec
                    for sf in spec.fields:
                        if sf.kind in ("struct", "struct_list") and sf.base_type not in collected:
                            queue.append(sf.base_type)
                return collected

            req_structs = collect_domains(req_fields)
            resp_models = collect_domains(resp_fields)

            apis[api_code] = ApiSpec(
                api_code=api_code,
                stem=stem,
                module=module,
                source="topsdk",
                comment=api_comments.get(api_code, api_code),
                req_fields=req_fields,
                resp_fields=resp_fields,
                req_structs=req_structs,
                resp_models=resp_models,
            )
    return apis


def parse_netauto_nested_classes(txt: str) -> dict[str, ClassSpec]:
    classes: dict[str, ClassSpec] = {}
    # Find nested `public class Xxx : TopObject` blocks
    pattern = re.compile(r'public\s+class\s+([A-Za-z0-9_]+)\s*:\s*TopObject\s*\{')
    matches = list(pattern.finditer(txt))
    for idx, m in enumerate(matches):
        cls_name = m.group(1)
        start = m.end()
        end = matches[idx + 1].start() if idx + 1 < len(matches) else len(txt)
        body = txt[start:end]
        classes[cls_name] = ClassSpec(
            name=cls_name,
            fields=parse_netauto_model_fields(body),
        )
    return classes


def parse_netauto_model_fields(body: str) -> list[FieldSpec]:
    fields: list[FieldSpec] = []
    # Match attributes + property
    lines = body.splitlines()
    comment_buf: list[str] = []
    in_summary = False
    xml_elem = ""
    xml_arr = ""
    xml_item = ""
    for line in lines:
        s = line.strip()
        if s.startswith("///"):
            inner = s[3:].strip()
            if inner == "<summary>":
                in_summary = True
                comment_buf = []
            elif inner == "</summary>":
                in_summary = False
            elif in_summary:
                comment_buf.append(inner)
            continue
        m_elem = re.match(r'\[XmlElement\("([^"]+)"\)\]', s)
        if m_elem:
            xml_elem = m_elem.group(1)
            continue
        m_arr = re.match(r'\[XmlArray\("([^"]+)"\)\]', s)
        if m_arr:
            xml_arr = m_arr.group(1)
            continue
        m_item = re.match(r'\[XmlArrayItem\("([^"]+)"\)\]', s)
        if m_item:
            xml_item = m_item.group(1)
            continue
        m_prop = re.match(r'public\s+([^\s{]+)\s+([A-Za-z0-9_]+)\s*\{\s*get;\s*set;\s*\}', s)
        if m_prop:
            cs_type = m_prop.group(1)
            prop_name = m_prop.group(2)
            wire = xml_arr if xml_arr else xml_elem
            if not wire:
                # Fallback snake_case
                wire = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", prop_name).lower()
            kind, base, _ = normalize_cs_type(cs_type)
            fields.append(FieldSpec(
                prop_name=sanitize_ident(prop_name, True),
                wire_name=wire,
                cs_type=cs_type,
                kind=kind,
                base_type=base,
                item_wire=xml_item,
                comment=" ".join(comment_buf),
            ))
            comment_buf = []
            xml_elem = ""
            xml_arr = ""
            xml_item = ""
        elif s.startswith("public class ") or s.startswith("}"):
            comment_buf = []
            xml_elem = ""
            xml_arr = ""
            xml_item = ""
    return fields


def parse_netauto_unique(root: Path, existing_apis: dict[str, ApiSpec]) -> tuple[dict[str, ApiSpec], dict[str, ClassSpec]]:
    req_dir = root / "Request"
    resp_dir = root / "Response"
    dom_dir = root / "Domain"

    shared_domain_cache: dict[str, ClassSpec] = {}
    for df in sorted(dom_dir.glob("*.cs")):
        dtxt = df.read_text(encoding="utf-8", errors="ignore")
        d_name = df.stem
        shared_domain_cache[d_name] = ClassSpec(
            name=sanitize_ident(d_name, True),
            fields=parse_netauto_model_fields(dtxt),
        )

    added: dict[str, ApiSpec] = {}
    for rf in sorted(req_dir.glob("*.cs")):
        rtxt = rf.read_text(encoding="utf-8", errors="ignore")
        m_code = re.search(r'GetApiName\(\)\s*\{\s*return\s+"([^"]+)"', rtxt)
        if not m_code:
            continue
        api_code = m_code.group(1)
        if api_code in existing_apis:
            req_wires: set[str] = set()
            for vm in re.finditer(r'RequestValidator\.ValidateRequired\("([^"]+)"', rtxt):
                req_wires.add(vm.group(1))
            if req_wires:
                for f in existing_apis[api_code].req_fields:
                    if f.wire_name in req_wires:
                        f.required = True
            continue

        stem = to_pascal(api_code)
        module = module_for_api(api_code)

        # Parse properties before `#region` / `GetApiName`
        head = rtxt.split("GetApiName()")[0]
        raw_props: dict[str, tuple[str, str]] = {}
        helpers: dict[str, str] = {}
        comment_buf: list[str] = []
        in_summary = False
        for line in head.splitlines():
            s = line.strip()
            if s.startswith("///"):
                inner = s[3:].strip()
                if inner == "<summary>":
                    in_summary = True
                    comment_buf = []
                elif inner == "</summary>":
                    in_summary = False
                elif in_summary:
                    comment_buf.append(inner)
                continue
            mp = re.match(r'public\s+([^\s{]+)\s+([A-Za-z0-9_]+)\s*\{\s*get;\s*set;\s*\}', s)
            if mp:
                raw_props[mp.group(2)] = (mp.group(1), " ".join(comment_buf))
                comment_buf = []
                continue
            mh = re.match(r'public\s+([^\s{]+)\s+([A-Za-z0-9_]+_)\s*\{', s)
            if mh:
                helpers[mh.group(2)[:-1]] = mh.group(1)
                comment_buf = []
                continue

        # Wire name mappings from GetParameters() & GetFileParameters()
        wire_map: dict[str, str] = {}
        for wm in re.finditer(r'(?:parameters|fileParams)\.Add\("([^"]+)",\s*this\.([A-Za-z0-9_]+)\)', rtxt):
            wire_map[wm.group(2)] = wm.group(1)

        # Required fields from Validate()
        required_wires: set[str] = set()
        for vm in re.finditer(r'RequestValidator\.ValidateRequired\("([^"]+)"', rtxt):
            required_wires.add(vm.group(1))

        nested_req_classes = parse_netauto_nested_classes(rtxt)

        req_fields: list[FieldSpec] = []
        req_structs: dict[str, ClassSpec] = {}
        shared_used: list[str] = []

        def enqueue_req_struct(type_str: str) -> tuple[str, bool]:
            kind, base, _ = normalize_cs_type(type_str)
            is_list = kind == "struct_list"
            if base in nested_req_classes:
                prefixed = stem + sanitize_ident(base, True)
                if prefixed not in req_structs:
                    orig = nested_req_classes[base]
                    # Rename nested field refs recursively
                    copied = ClassSpec(name=prefixed, fields=[], comment=orig.comment)
                    req_structs[prefixed] = copied
                    for nf in orig.fields:
                        nf_copy = FieldSpec(**nf.__dict__)
                        if nf_copy.kind in ("struct", "struct_list"):
                            sub_name, _ = enqueue_req_struct(nf_copy.cs_type)
                            nf_copy.base_type = sub_name
                        copied.fields.append(nf_copy)
                return (prefixed, is_list)
            if base in shared_domain_cache:
                sname = sanitize_ident(base, True)
                if sname not in shared_used:
                    shared_used.append(sname)
                return (sname, is_list)
            return (sanitize_ident(base, True), is_list)

        for prop_name, (cs_type, cmt) in raw_props.items():
            wire = wire_map.get(prop_name, re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", prop_name).lower())
            kind, base, _ = normalize_cs_type(cs_type)
            fs = FieldSpec(
                prop_name=sanitize_ident(prop_name, True),
                wire_name=wire,
                cs_type=cs_type,
                kind=kind,
                base_type=base,
                comment=cmt,
                required=(wire in required_wires),
            )
            if prop_name in helpers:
                helper_ty = helpers[prop_name]
                h_name, h_list = enqueue_req_struct(helper_ty)
                fs.has_struct_helper = True
                fs.struct_helper_type = h_name
                fs.struct_helper_is_list = h_list
            req_fields.append(fs)

        # Parse Response
        resp_file = resp_dir / rf.name.replace("Request.cs", "Response.cs")
        resp_fields: list[FieldSpec] = []
        resp_models: dict[str, ClassSpec] = {}
        if resp_file.exists():
            resp_txt = resp_file.read_text(encoding="utf-8", errors="ignore")
            nested_resp_classes = parse_netauto_nested_classes(resp_txt)
            # Only top-level properties of Response class (before first nested TopObject class)
            resp_head = resp_txt.split(" : TopObject")[0]
            if "public class " in resp_head:
                # Trim off trailing `public class XxxDomain` header if split landed after it
                idx = resp_head.rfind("/// <summary>\n\t/// ")
                # Safer: split at first `///` before `TopObject` or first `: TopObject` line
                m_nested = re.search(r'public\s+class\s+[A-Za-z0-9_]+\s*:\s*TopObject', resp_txt)
                if m_nested:
                    resp_head = resp_txt[:m_nested.start()]
            resp_fields = parse_netauto_model_fields(resp_head)

            def resolve_resp_type(type_str: str) -> str:
                kind, base, _ = normalize_cs_type(type_str)
                if kind not in ("struct", "struct_list"):
                    return base
                if base in nested_resp_classes:
                    prefixed = stem + sanitize_ident(base, True)
                    if prefixed not in resp_models:
                        orig = nested_resp_classes[base]
                        copied = ClassSpec(name=prefixed, fields=[], comment=orig.comment)
                        resp_models[prefixed] = copied
                        for nf in orig.fields:
                            nf_copy = FieldSpec(**nf.__dict__)
                            if nf_copy.kind in ("struct", "struct_list"):
                                nf_copy.base_type = resolve_resp_type(nf_copy.cs_type)
                            copied.fields.append(nf_copy)
                    return prefixed
                if base in shared_domain_cache:
                    sname = sanitize_ident(base, True)
                    if sname not in shared_used:
                        shared_used.append(sname)
                    return sname
                return sanitize_ident(base, True)

            for rf_item in resp_fields:
                if rf_item.kind in ("struct", "struct_list"):
                    rf_item.base_type = resolve_resp_type(rf_item.cs_type)

        # Expand transitive closure of shared_used domains
        queue = list(shared_used)
        seen_shared: set[str] = set()
        while queue:
            s = queue.pop(0)
            if s in seen_shared or s not in shared_domain_cache:
                continue
            seen_shared.add(s)
            for sf in shared_domain_cache[s].fields:
                if sf.kind in ("struct", "struct_list") and sf.base_type not in seen_shared:
                    queue.append(sf.base_type)

        added[api_code] = ApiSpec(
            api_code=api_code,
            stem=stem,
            module=module,
            source="net-auto",
            comment=api_code,
            req_fields=req_fields,
            resp_fields=resp_fields,
            req_structs=req_structs,
            resp_models=resp_models,
            shared_models=sorted(seen_shared),
        )
    return added, shared_domain_cache


def zan_type_for_model(f: FieldSpec) -> tuple[str, str]:
    """Returns (zan_type, default_init) for a Model field."""
    if f.kind == "scalar":
        if f.base_type == "long":
            return ("long", "0L")
        if f.base_type == "int":
            return ("int", "0")
        if f.base_type == "bool":
            return ("bool", "false")
        if f.base_type == "double":
            return ("double", "0.0")
        return ("string", '""')
    if f.kind == "scalar_list":
        if f.base_type == "long":
            return ("List<long>", "new List<long>()")
        if f.base_type == "int":
            return ("List<int>", "new List<int>()")
        if f.base_type == "bool":
            return ("List<bool>", "new List<bool>()")
        if f.base_type == "double":
            return ("List<double>", "new List<double>()")
        return ("List<string>", "new List<string>()")
    if f.kind == "struct":
        tname = sanitize_ident(f.base_type, True)
        return (tname + "?", "null")
    if f.kind == "struct_list":
        tname = sanitize_ident(f.base_type, True)
        return ("List<" + tname + ">", "new List<" + tname + ">()")
    return ("string", '""')


def req_struct_name(base_name: str, model_names: set[str]) -> str:
    cname = sanitize_ident(base_name, True)
    cand = cname if cname.endswith("Param") else (cname + "Param")
    while cand in model_names:
        cand = cand + "Input"
    return cand


def emit_struct_class(spec: ClassSpec, module_ns: str, model_names: set[str]) -> list[str]:
    cls_name = req_struct_name(spec.name, model_names)
    lines: list[str] = []
    lines.append(f"public class {cls_name} {{")
    lines.append("    private JsonValue body = JsonValue.NewObject();")
    lines.append("")
    used_methods = set(STRUCT_RESERVED_METHODS)
    used_methods.add(cls_name)
    for f in spec.fields:
        mname = f.prop_name
        if mname in used_methods:
            mname = mname + "Param"
        used_methods.add(mname)
        wire = f.wire_name
        if f.kind == "scalar":
            if f.base_type == "long":
                lines.append(f"    public {cls_name} {mname}(long val) {{")
                lines.append(f'        this.body.PutInt("{wire}", val);')
                lines.append("        return this;")
                lines.append("    }")
            elif f.base_type == "int":
                lines.append(f"    public {cls_name} {mname}(int val) {{")
                lines.append(f'        this.body.PutInt("{wire}", (long)val);')
                lines.append("        return this;")
                lines.append("    }")
            elif f.base_type == "bool":
                lines.append(f"    public {cls_name} {mname}(bool val) {{")
                lines.append(f'        this.body.PutBool("{wire}", val);')
                lines.append("        return this;")
                lines.append("    }")
            elif f.base_type == "double":
                lines.append(f"    public {cls_name} {mname}(double val) {{")
                lines.append(f'        this.body.PutDouble("{wire}", val);')
                lines.append("        return this;")
                lines.append("    }")
            elif f.base_type == "DateTime":
                lines.append(f"    public {cls_name} {mname}(string val) {{")
                lines.append(f'        if (val != null) {{ this.body.PutStr("{wire}", val); }}')
                lines.append("        return this;")
                lines.append("    }")
                lines.append(f"    public {cls_name} {mname}(DateTime val) {{")
                lines.append(f'        if (val != null) {{ this.body.PutStr("{wire}", val.ToString()); }}')
                lines.append("        return this;")
                lines.append("    }")
            else:
                lines.append(f"    public {cls_name} {mname}(string val) {{")
                lines.append(f'        if (val != null) {{ this.body.PutStr("{wire}", val); }}')
                lines.append("        return this;")
                lines.append("    }")
        elif f.kind == "scalar_list":
            elem_ty = f.base_type if f.base_type in ("long", "int", "bool", "double") else "string"
            lines.append(f"    public {cls_name} {mname}(List<{elem_ty}> values) {{")
            lines.append("        if (values != null) {")
            lines.append("            JsonValue arr = JsonValue.NewArray();")
            lines.append("            for (int i = 0; i < values.Count; i = i + 1) {")
            if elem_ty == "long":
                lines.append("                arr.AppendInt(values[i]);")
            elif elem_ty == "int":
                lines.append("                arr.AppendInt((long)values[i]);")
            elif elem_ty == "bool":
                lines.append("                arr.AppendBool(values[i]);")
            elif elem_ty == "double":
                lines.append("                arr.AppendDouble(values[i]);")
            else:
                lines.append("                arr.Append(values[i] == null ? JsonValue.NewNull() : JsonValue.NewStr(values[i]));")
            lines.append("            }")
            lines.append(f'            this.body.Set("{wire}", arr);')
            lines.append("        }")
            lines.append("        return this;")
            lines.append("    }")
        elif f.kind == "struct":
            tname = req_struct_name(f.base_type, model_names)
            lines.append(f"    public {cls_name} {mname}({tname} val) {{")
            lines.append(f'        if (val != null) {{ this.body.Set("{wire}", val.ToJsonValue()); }}')
            lines.append("        return this;")
            lines.append("    }")
        elif f.kind == "struct_list":
            tname = req_struct_name(f.base_type, model_names)
            lines.append(f"    public {cls_name} {mname}(List<{tname}> values) {{")
            lines.append("        if (values != null) {")
            lines.append("            JsonValue arr = JsonValue.NewArray();")
            lines.append("            for (int i = 0; i < values.Count; i = i + 1) {")
            lines.append("                if (values[i] != null) { arr.Append(values[i].ToJsonValue()); }")
            lines.append("            }")
            lines.append(f'            this.body.Set("{wire}", arr);')
            lines.append("        }")
            lines.append("        return this;")
            lines.append("    }")
    lines.append("")
    lines.append(f"    public {cls_name} SetRaw(string key, JsonValue val) {{")
    lines.append("        if (key != null && key.Length > 0 && val != null) { this.body.Set(key, val); }")
    lines.append("        return this;")
    lines.append("    }")
    lines.append("    public JsonValue ToJsonValue() { return this.body; }")
    lines.append("    public string ToJson() { return this.body.ToJson(); }")
    lines.append("}")
    return lines


def emit_request_file(api: ApiSpec, model_names: set[str]) -> str:
    req_cls = api.stem + "Request"
    lines: list[str] = [
        "// Generated by scripts/generate_catalog.py.",
        "",
        "using System;",
        "using System.Json;",
        "using Sdk.Taobao;",
        "",
        f"namespace Sdk.Taobao.Api.{api.module};",
        "",
        f"// TOP API: {api.api_code}",
        f"public class {req_cls} {{",
        "    private Dictionary<string, string> fields = new Dictionary<string, string>();",
        "    private Dictionary<string, TaobaoFileItem> files = new Dictionary<string, TaobaoFileItem>();",
        "",
    ]

    used_methods = set(REQUEST_RESERVED_METHODS)
    used_methods.add(req_cls)

    for f in api.req_fields:
        mname = f.prop_name
        if mname in used_methods:
            mname = mname + "Param"
        used_methods.add(mname)
        wire = f.wire_name
        if f.comment:
            lines.append(f"    // {f.comment}")
        if f.kind == "file":
            lines.append(f"    public {req_cls} {mname}(TaobaoFileItem val) {{")
            lines.append(f'        if (val != null) {{ this.SetFile("{wire}", val); }}')
            lines.append("        return this;")
            lines.append("    }")
        elif f.kind == "scalar":
            if f.base_type == "long":
                lines.append(f"    public {req_cls} {mname}(long val) {{")
                lines.append(f'        this.SetField("{wire}", Convert.ToString(val));')
                lines.append("        return this;")
                lines.append("    }")
            elif f.base_type == "int":
                lines.append(f"    public {req_cls} {mname}(int val) {{")
                lines.append(f'        this.SetField("{wire}", Convert.ToString(val));')
                lines.append("        return this;")
                lines.append("    }")
            elif f.base_type == "bool":
                lines.append(f"    public {req_cls} {mname}(bool val) {{")
                lines.append(f'        this.SetField("{wire}", val ? "true" : "false");')
                lines.append("        return this;")
                lines.append("    }")
            elif f.base_type == "double":
                lines.append(f"    public {req_cls} {mname}(double val) {{")
                lines.append(f'        this.SetField("{wire}", Convert.ToString(val));')
                lines.append("        return this;")
                lines.append("    }")
            elif f.base_type == "DateTime":
                lines.append(f"    public {req_cls} {mname}(string val) {{")
                lines.append(f'        if (val != null) {{ this.SetField("{wire}", val); }}')
                lines.append("        return this;")
                lines.append("    }")
                lines.append(f"    public {req_cls} {mname}(DateTime val) {{")
                lines.append(f'        if (val != null) {{ this.SetField("{wire}", val.ToString()); }}')
                lines.append("        return this;")
                lines.append("    }")
            else:
                lines.append(f"    public {req_cls} {mname}(string val) {{")
                lines.append(f'        if (val != null) {{ this.SetField("{wire}", val); }}')
                lines.append("        return this;")
                lines.append("    }")
                if f.has_struct_helper:
                    ht = req_struct_name(f.struct_helper_type, model_names)
                    if f.struct_helper_is_list:
                        lines.append(f"    public {req_cls} {mname}(List<{ht}> values) {{")
                        lines.append("        if (values != null) {")
                        lines.append("            JsonValue arr = JsonValue.NewArray();")
                        lines.append("            for (int i = 0; i < values.Count; i = i + 1) {")
                        lines.append("                if (values[i] != null) { arr.Append(values[i].ToJsonValue()); }")
                        lines.append("            }")
                        lines.append(f'            this.SetField("{wire}", arr.ToJson());')
                        lines.append("        }")
                        lines.append("        return this;")
                        lines.append("    }")
                    else:
                        lines.append(f"    public {req_cls} {mname}({ht} val) {{")
                        lines.append(f'        if (val != null) {{ this.SetField("{wire}", val.ToJson()); }}')
                        lines.append("        return this;")
                        lines.append("    }")
        elif f.kind == "scalar_list":
            if f.base_type == "long":
                lines.append(f"    public {req_cls} {mname}(List<long> values) {{")
                lines.append(f'        if (values != null) {{ this.SetField("{wire}", TaobaoJson.JoinLongList(values)); }}')
                lines.append("        return this;")
                lines.append("    }")
            elif f.base_type == "int":
                lines.append(f"    public {req_cls} {mname}(List<int> values) {{")
                lines.append("        if (values != null) {")
                lines.append("            List<long> tmp = new List<long>();")
                lines.append("            for (int i = 0; i < values.Count; i = i + 1) { tmp.Add((long)values[i]); }")
                lines.append(f'            this.SetField("{wire}", TaobaoJson.JoinLongList(tmp));')
                lines.append("        }")
                lines.append("        return this;")
                lines.append("    }")
            else:
                lines.append(f"    public {req_cls} {mname}(List<string> values) {{")
                lines.append(f'        if (values != null) {{ this.SetField("{wire}", TaobaoJson.JoinStringList(values)); }}')
                lines.append("        return this;")
                lines.append("    }")
        elif f.kind == "struct":
            tname = req_struct_name(f.base_type, model_names)
            lines.append(f"    public {req_cls} {mname}({tname} val) {{")
            lines.append(f'        if (val != null) {{ this.SetField("{wire}", val.ToJson()); }}')
            lines.append("        return this;")
            lines.append("    }")
            lines.append(f"    public {req_cls} {mname}(string rawJson) {{")
            lines.append(f'        if (rawJson != null) {{ this.SetField("{wire}", rawJson); }}')
            lines.append("        return this;")
            lines.append("    }")
        elif f.kind == "struct_list":
            tname = req_struct_name(f.base_type, model_names)
            lines.append(f"    public {req_cls} {mname}(List<{tname}> values) {{")
            lines.append("        if (values != null) {")
            lines.append("            JsonValue arr = JsonValue.NewArray();")
            lines.append("            for (int i = 0; i < values.Count; i = i + 1) {")
            lines.append("                if (values[i] != null) { arr.Append(values[i].ToJsonValue()); }")
            lines.append("            }")
            lines.append(f'            this.SetField("{wire}", arr.ToJson());')
            lines.append("        }")
            lines.append("        return this;")
            lines.append("    }")
            lines.append(f"    public {req_cls} {mname}(string rawJson) {{")
            lines.append(f'        if (rawJson != null) {{ this.SetField("{wire}", rawJson); }}')
            lines.append("        return this;")
            lines.append("    }")

    lines.extend([
        "",
        f"    public {req_cls} SetRaw(string key, string val) {{",
        "        if (key != null && key.Length > 0 && val != null) { this.SetField(key, val); }",
        "        return this;",
        "    }",
        "",
        f"    public {req_cls} SetRawJson(string key, JsonValue val) {{",
        "        if (key != null && key.Length > 0 && val != null) { this.SetField(key, val.ToJson()); }",
        "        return this;",
        "    }",
        "",
        "    public void Validate() {",
    ])
    for f in api.req_fields:
        if f.required:
            if f.kind == "file":
                lines.append(f'        if (!this.files.ContainsKey("{f.wire_name}")) {{')
            else:
                lines.append(f'        if (!this.fields.ContainsKey("{f.wire_name}")) {{')
            lines.append(f'            throw new TaobaoException("client.invalidArguments", "缺少必填参数: {f.wire_name}", 0, "");')
            lines.append("        }")
    lines.extend([
        "    }",
        "",
        f'    public string GetApiMethod() {{ return "{api.api_code}"; }}',
        "    public Dictionary<string, string> BuildParameters() { return this.fields; }",
        "    public Dictionary<string, TaobaoFileItem> BuildFiles() { return this.files; }",
        "",
        "    private void SetField(string key, string val) {",
        "        if (this.fields.ContainsKey(key)) { this.fields[key] = val; } else { this.fields.Add(key, val); }",
        "    }",
        "",
        "    private void SetFile(string key, TaobaoFileItem val) {",
        "        if (this.files.ContainsKey(key)) { this.files[key] = val; } else { this.files.Add(key, val); }",
        "    }",
        "}",
    ])

    return "\n".join(lines) + "\n"


def emit_standalone_struct_file(module: str, spec: ClassSpec, model_names: set[str]) -> str:
    lines: list[str] = [
        "// Generated by scripts/generate_catalog.py.",
        "",
        "using System;",
        "using System.Json;",
        "using Sdk.Taobao;",
        "",
        f"namespace Sdk.Taobao.Api.{module};",
        "",
    ]
    lines.extend(emit_struct_class(spec, module, model_names))
    return "\n".join(lines) + "\n"


def emit_model_class(cls_name: str, fields: list[FieldSpec], is_response: bool) -> list[str]:
    lines: list[str] = [f"public class {cls_name} {{"]
    used_names = set(MODEL_RESERVED_FIELDS)
    used_names.add(cls_name)
    resolved_fields: list[tuple[FieldSpec, str, str]] = []

    for f in fields:
        fname = f.prop_name
        if fname in used_names:
            fname = fname + "Value"
        used_names.add(fname)
        zty, dflt = zan_type_for_model(f)
        if f.comment:
            lines.append(f"    // {f.comment}")
        lines.append(f"    public {zty} {fname} = {dflt};")
        resolved_fields.append((f, fname, zty))

    if is_response:
        lines.append("    public TaobaoResponse? Raw = null;")
        lines.append("")
        lines.append(f"    public static {cls_name} FromResponse(TaobaoResponse raw) {{")
        lines.append(f"        {cls_name} result = (raw != null && raw.Data != null && raw.Data.IsObject())")
        lines.append(f"            ? {cls_name}.FromJson(raw.Data)")
        lines.append(f"            : new {cls_name}();")
        lines.append("        result.Raw = raw;")
        lines.append("        return result;")
        lines.append("    }")

    lines.append("")
    lines.append(f"    public static {cls_name} FromJson(JsonValue? obj) {{")
    lines.append(f"        {cls_name} item = new {cls_name}();")
    lines.append("        if (obj == null || !obj.IsObject()) { return item; }")
    for f, fname, zty in resolved_fields:
        wire = f.wire_name
        item_wire = f.item_wire
        if f.kind == "scalar":
            if f.base_type == "long":
                lines.append(f'        item.{fname} = TaobaoJson.GetLong(obj, "{wire}", 0L);')
            elif f.base_type == "int":
                lines.append(f'        item.{fname} = TaobaoJson.GetInt(obj, "{wire}", 0);')
            elif f.base_type == "bool":
                lines.append(f'        item.{fname} = TaobaoJson.GetBool(obj, "{wire}", false);')
            elif f.base_type == "double":
                lines.append(f'        item.{fname} = TaobaoJson.GetDouble(obj, "{wire}", 0.0);')
            else:
                lines.append(f'        item.{fname} = TaobaoJson.GetString(obj, "{wire}", "");')
        elif f.kind == "scalar_list":
            arg_item = f', "{item_wire}"' if item_wire else ""
            if f.base_type == "long":
                lines.append(f'        item.{fname} = TaobaoJson.GetLongList(obj, "{wire}"{arg_item});')
            elif f.base_type == "int":
                lines.append(f'        item.{fname} = TaobaoJson.GetIntList(obj, "{wire}"{arg_item});')
            elif f.base_type == "bool":
                lines.append(f'        item.{fname} = TaobaoJson.GetBoolList(obj, "{wire}"{arg_item});')
            elif f.base_type == "double":
                lines.append(f'        item.{fname} = TaobaoJson.GetDoubleList(obj, "{wire}"{arg_item});')
            else:
                lines.append(f'        item.{fname} = TaobaoJson.GetStringList(obj, "{wire}"{arg_item});')
        elif f.kind == "struct":
            tname = sanitize_ident(f.base_type, True)
            lines.append(f'        JsonValue? v_{fname} = TaobaoJson.GetValue(obj, "{wire}");')
            lines.append(f"        if (v_{fname} != null && v_{fname}.IsObject()) {{")
            lines.append(f"            item.{fname} = {tname}.FromJson(v_{fname});")
            lines.append("        }")
        elif f.kind == "struct_list":
            tname = sanitize_ident(f.base_type, True)
            arg_item = f', "{item_wire}"' if item_wire else ""
            lines.append(f'        List<JsonValue> list_{fname} = TaobaoJson.GetJsonList(obj, "{wire}"{arg_item});')
            lines.append(f"        for (int i = 0; i < list_{fname}.Count; i = i + 1) {{")
            lines.append(f"            item.{fname}.Add({tname}.FromJson(list_{fname}[i]));")
            lines.append("        }")
    lines.append("        return item;")
    lines.append("    }")
    lines.append("}")
    return lines


def emit_response_file(api: ApiSpec, emitted_models: set[str]) -> str:
    resp_cls = api.stem + "Response"
    lines: list[str] = [
        "// Generated by scripts/generate_catalog.py.",
        "",
        "using System;",
        "using System.Json;",
        "using Sdk.Taobao;",
        "",
        f"namespace Sdk.Taobao.Api.{api.module};",
        "",
    ]
    lines.extend(emit_model_class(resp_cls, api.resp_fields, is_response=True))

    for mname, mspec in api.resp_models.items():
        cname = sanitize_ident(mspec.name, True)
        if cname in emitted_models:
            continue
        emitted_models.add(cname)
        lines.append("")
        lines.extend(emit_model_class(cname, mspec.fields, is_response=False))
    return "\n".join(lines) + "\n"


def emit_shared_domain_file(module: str, spec: ClassSpec, as_struct: bool, as_model: bool) -> str:
    cname = sanitize_ident(spec.name, True)
    lines: list[str] = [
        "// Generated by scripts/generate_catalog.py.",
        "",
        "using System;",
        "using System.Json;",
        "using Sdk.Taobao;",
        "",
        f"namespace Sdk.Taobao.Api.{module};",
        "",
    ]
    if as_model:
        lines.extend(emit_model_class(cname, spec.fields, is_response=False))
    if as_struct:
        # Shared domain used in request helper; if also emitted as model, only emit struct methods?
        # Wait: in net-auto, check if any shared domain is used in BOTH Request and Response!
        if not as_model:
            lines.extend(emit_struct_class(ClassSpec(name=cname, fields=spec.fields), module))
    return "\n".join(lines) + "\n"


def emit_module_api_file(module: str, apis: list[ApiSpec]) -> str:
    api_cls = module + "Api"
    lines: list[str] = [
        "// Generated by scripts/generate_catalog.py.",
        "",
        "using System;",
        "using Sdk.Taobao;",
        "",
        f"namespace Sdk.Taobao.Api.{module};",
        "",
        f"public class {api_cls} {{",
        "    private TaobaoClient client;",
        "",
        f"    public {api_cls}(TaobaoClient client) {{",
        "        if (client == null) {",
        '            throw new TaobaoException("client.invalidArguments", "client 不能为空", 0, "");',
        "        }",
        "        this.client = client;",
        "    }",
    ]
    for api in sorted(apis, key=lambda a: a.api_code):
        req_cls = api.stem + "Request"
        resp_cls = api.stem + "Response"
        method_name = api.stem + "Async"
        raw_method_name = api.stem + "RawAsync"
        lines.append("")
        lines.append(f"    // {api.comment} ({api.api_code})")
        lines.append(f"    public async {resp_cls} {method_name}({req_cls} request) {{")
        lines.append('        if (request == null) { throw new TaobaoException("client.invalidArguments", "request 不能为空", 0, ""); }')
        lines.append("        request.Validate();")
        lines.append("        TaobaoResponse raw = await this.client.CallAsync(")
        lines.append("            request.GetApiMethod(), request.BuildParameters(), request.BuildFiles(), \"\");")
        lines.append(f"        return {resp_cls}.FromResponse(raw);")
        lines.append("    }")
        lines.append("")
        lines.append(f"    public async {resp_cls} {method_name}(string session, {req_cls} request) {{")
        lines.append('        if (request == null) { throw new TaobaoException("client.invalidArguments", "request 不能为空", 0, ""); }')
        lines.append("        request.Validate();")
        lines.append("        TaobaoResponse raw = await this.client.CallAsync(")
        lines.append("            request.GetApiMethod(), request.BuildParameters(), request.BuildFiles(), session);")
        lines.append(f"        return {resp_cls}.FromResponse(raw);")
        lines.append("    }")
        lines.append("")
        lines.append(f"    public async TaobaoResponse {raw_method_name}({req_cls} request, string session = \"\") {{")
        lines.append('        if (request == null) { throw new TaobaoException("client.invalidArguments", "request 不能为空", 0, ""); }')
        lines.append("        request.Validate();")
        lines.append("        return await this.client.CallAsync(")
        lines.append("            request.GetApiMethod(), request.BuildParameters(), request.BuildFiles(), session);")
        lines.append("    }")
    lines.append("}")
    return "\n".join(lines) + "\n"


def generate_files(topsdk_dir: Path, netauto_dir: Path) -> dict[str, str]:
    apis = parse_topsdk(topsdk_dir)
    netauto_added, shared_domains = parse_netauto_unique(netauto_dir, apis)
    apis.update(netauto_added)

    by_module: dict[str, list[ApiSpec]] = defaultdict(list)
    for api in apis.values():
        by_module[api.module].append(api)

    files: dict[str, str] = {}
    for module, mod_apis in sorted(by_module.items()):
        # Check if any module has shared_models used in Request vs Response
        shared_req: set[str] = set()
        shared_resp: set[str] = set()
        for api in mod_apis:
            for f in api.req_fields:
                if f.has_struct_helper and f.struct_helper_type in shared_domains:
                    shared_req.add(f.struct_helper_type)
            for sm in api.shared_models:
                if sm not in shared_req:
                    shared_resp.add(sm)

        # Expand shared_req transitive children
        q = list(shared_req)
        while q:
            cur = q.pop(0)
            if cur in shared_domains:
                for sf in shared_domains[cur].fields:
                    if sf.kind in ("struct", "struct_list"):
                        b = sanitize_ident(sf.base_type, True)
                        if b in shared_domains and b not in shared_req:
                            shared_req.add(b)
                            q.append(b)

        # Collect all Response & Model class names in this module so Request structs never collide
        module_model_names: set[str] = set()
        for api in mod_apis:
            module_model_names.add(api.stem + "Response")
            for mspec in api.resp_models.values():
                module_model_names.add(sanitize_ident(mspec.name, True))
        for sm in shared_resp:
            module_model_names.add(sanitize_ident(sm, True))

        for sm in sorted(shared_req | shared_resp):
            if sm in shared_domains:
                spec = shared_domains[sm]
                if sm in shared_resp:
                    files[f"{module}/Models/{sanitize_ident(sm, True)}.zan"] = emit_shared_domain_file(module, spec, as_struct=False, as_model=True)
                if sm in shared_req:
                    rname = req_struct_name(spec.name, module_model_names)
                    files[f"{module}/Requests/{rname}.zan"] = emit_standalone_struct_file(module, spec, module_model_names)

        emitted_structs: set[str] = {req_struct_name(sm, module_model_names) for sm in shared_req}
        emitted_models: set[str] = set(shared_resp)

        for api in sorted(mod_apis, key=lambda a: a.api_code):
            for sname, spec in api.req_structs.items():
                rname = req_struct_name(spec.name, module_model_names)
                if rname not in emitted_structs:
                    emitted_structs.add(rname)
                    files[f"{module}/Requests/{rname}.zan"] = emit_standalone_struct_file(module, spec, module_model_names)
            files[f"{module}/Requests/{api.stem}Request.zan"] = emit_request_file(api, module_model_names)
            files[f"{module}/Models/{api.stem}Response.zan"] = emit_response_file(api, emitted_models)

        files[f"{module}/{module}Api.zan"] = emit_module_api_file(module, mod_apis)

    return files


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate Zan.Sdk.Taobao API catalog.")
    parser.add_argument("--topsdk", default=r"C:\Users\QQ\Downloads\SDK\Topsdk", help="Path to Topsdk directory")
    parser.add_argument("--netauto", default=r"C:\Users\QQ\Downloads\SDK\taobao-sdk-net-auto", help="Path to taobao-sdk-net-auto directory")
    parser.add_argument("--check", action="store_true", help="Verify generated files match disk without modifying")
    args = parser.parse_args()

    topsdk_dir = Path(args.topsdk)
    netauto_dir = Path(args.netauto)
    if not topsdk_dir.is_dir() or not netauto_dir.is_dir():
        print(f"Source SDK directories not found: {topsdk_dir} / {netauto_dir}", file=sys.stderr)
        return 2

    api_root = Path(__file__).resolve().parent.parent / "src" / "Sdk" / "Taobao" / "Api"
    files = generate_files(topsdk_dir, netauto_dir)

    max_lines = max(content.count("\n") for content in files.values())
    if max_lines > 1200:
        over = [(k, v.count("\n")) for k, v in files.items() if v.count("\n") > 1200]
        print(f"ERROR: Files exceed 1200 lines: {over}", file=sys.stderr)
        return 1

    if args.check:
        mismatches = []
        existing = set()
        if api_root.is_dir():
            for p in api_root.rglob("*.zan"):
                existing.add(p.relative_to(api_root).as_posix())
        for rel, content in files.items():
            p = api_root / rel
            if not p.exists() or p.read_text(encoding="utf-8") != content:
                mismatches.append(f"diff: {rel}")
        for extra in sorted(existing - set(files.keys())):
            mismatches.append(f"extra: {extra}")
        if mismatches:
            print(f"CHECK FAILED ({len(mismatches)} mismatches), first 10: {mismatches[:10]}", file=sys.stderr)
            return 1
        print(f"CHECK OK: {len(files)} files verified, max_lines={max_lines}")
        return 0

    if api_root.is_dir():
        shutil.rmtree(api_root)
    for rel, content in files.items():
        target = api_root / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8", newline="\n")

    print(f"GENERATED: {len(files)} files under {api_root} (max_lines={max_lines})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
