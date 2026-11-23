#!/usr/bin/env python3
"""Generate platform-differentiated Zan.Sdk.OceanEngine from contracts & SPI docs."""

from __future__ import annotations

import collections
import json
import pathlib
import re
import shutil
import urllib.parse

CONTRACTS_ROOT = pathlib.Path(r"D:\project\saas\docs\oceanengine\contracts")
API_DOCS_ROOT = pathlib.Path(r"D:\project\saas\docs\oceanengine\api")
SDK_ROOT = pathlib.Path(
    r"D:\project\zan-lang\packages\Zan.Sdk.OceanEngine\src\Sdk\OceanEngine"
)

PLATFORM_MAP = {
    "巨量营销": "Marketing",
    "巨量千川": "Qianchuan",
    "巨量本地推": "LocalPush",
    "巨量星图": "StarMap",
    "群峰服务市场": "ServeMarket",
}

PLATFORM_ORDER = ["Marketing", "Qianchuan", "LocalPush", "StarMap", "ServeMarket"]

# Zan keywords & member names to avoid colliding with on generated types.
ZAN_KEYWORDS = {
    "abstract", "as", "async", "await", "base", "bool", "break", "byte", "case",
    "catch", "char", "checked", "class", "const", "continue", "decimal", "default",
    "delegate", "do", "double", "else", "enum", "event", "explicit", "extern",
    "false", "finally", "fixed", "float", "for", "foreach", "get", "goto", "if",
    "implicit", "in", "int", "interface", "internal", "is", "lock", "long",
    "namespace", "new", "nint", "null", "object", "operator", "out", "override",
    "params", "private", "protected", "public", "readonly", "ref", "return",
    "sbyte", "sealed", "set", "short", "sizeof", "stackalloc", "static",
    "string", "struct", "switch", "this", "throw", "true", "try", "typeof",
    "uint", "ulong", "unchecked", "unsafe", "ushort", "using", "value", "var",
    "virtual", "void", "volatile", "while",
}

RESERVED_PUBLIC_NAMES = {
    "Raw", "RawJson", "DataJson", "StatusCode", "RequestId", "FromResponse",
    "FromJson", "SetField", "QueryObject", "BodyObject", "BuildMultipart",
    "ToString", "Equals", "GetType", "Event", "Object", "Params", "Default",
    "Value", "Type", "Delegate", "Operator", "Base", "This", "String", "New",
    "Out", "Ref", "In", "Is", "As", "Get", "Set", "Checked", "Fixed", "Lock",
}

MAX_LINES_PER_FILE = 750
SPLIT_MODULE_THRESHOLD = 20

# Manual schema supplement for doc_id 1736331299814412 (table-only doc without structured request json).
SERVE_MARKET_ACTIVE_FUNC_FIX = {
    "request_fields": [
        {"name": "app_id", "type": "number", "format": "int64", "required": True, "description": "应用 id"},
        {"name": "use_uid", "type": "number", "format": "int64", "required": True, "description": "使用用户 id"},
        {
            "name": "func_keys",
            "type": "array",
            "required": False,
            "description": "功能Key列表，不传则返回全部",
            "children": [{"name": "", "type": "string"}],
        },
    ],
    "response_fields": [
        {
            "name": "func_list",
            "type": "array",
            "required": False,
            "description": "已购功能key列表",
            "children": [
                {
                    "name": "",
                    "type": "object",
                    "children": [
                        {"name": "key", "type": "string", "description": "功能key"},
                        {"name": "name", "type": "string", "description": "功能名称"},
                        {"name": "desc", "type": "string", "description": "功能描述"},
                    ],
                }
            ],
        }
    ],
    "direct_array": "func_list",
    "credential": "AppAccessToken",
}


def pascal(text: str) -> str:
    parts = re.findall(r"[A-Za-z0-9]+", text or "")
    out = "".join(p[:1].upper() + p[1:] for p in parts)
    if not out:
        return "Item"
    if out[0].isdigit():
        return "N" + out
    return out


def safe_public_name(raw_name: str, used: set[str], suffix: str = "Field") -> str:
    name = pascal(raw_name)
    if name in RESERVED_PUBLIC_NAMES or name.lower() in ZAN_KEYWORDS:
        name = name + suffix
    base = name
    idx = 2
    while name in used:
        name = f"{base}{idx}"
        idx += 1
    used.add(name)
    return name


def safe_param_name(raw_name: str) -> str:
    parts = re.findall(r"[A-Za-z0-9]+", raw_name or "")
    if not parts:
        return "paramVal"
    first = parts[0].lower()
    rest = "".join(p[:1].upper() + p[1:] for p in parts[1:])
    ident = first + rest
    if ident[0].isdigit():
        ident = "p" + ident
    if ident in ZAN_KEYWORDS:
        ident = ident + "Val"
    return ident


def strip_comment(text: str, limit: int = 90) -> str:
    s = re.sub(r"\s+", " ", (text or "")).strip()
    s = s.replace("*/", "* /")
    if len(s) > limit:
        s = s[: limit - 3] + "..."
    return s


def gateway_enum(url: str) -> str:
    host = urllib.parse.urlparse(url or "").netloc.lower()
    if host == "ad.oceanengine.com":
        return "OceanEngineGateway.Ad"
    if host == "open.oceanengine.com":
        return "OceanEngineGateway.Open"
    return "OceanEngineGateway.Api"


def determine_credential(contract: dict, path: str) -> str:
    doc_id = str(contract["doc_id"])
    if doc_id == "1736331299814412":
        return "OceanEngineCredential.AppAccessToken"
    if path.startswith("/open_api/oauth2/"):
        return "OceanEngineCredential.None"
    headers = [h.get("name", "").lower() for h in (contract.get("headers") or [])]
    if "app-access-token" in headers:
        return "OceanEngineCredential.AppAccessToken"
    if "access-token" in headers:
        return "OceanEngineCredential.AccessToken"
    ex = [h.lower() for h in (contract.get("example_auth_headers") or [])]
    if "app-access-token" in ex:
        return "OceanEngineCredential.AppAccessToken"
    if "access-token" in ex:
        return "OceanEngineCredential.AccessToken"
    return "OceanEngineCredential.AccessToken"


def path_segments(url: str, platform: str) -> list[str]:
    path = urllib.parse.urlparse(url or "").path.strip("/")
    segs = [s for s in path.split("/") if s]
    if segs and segs[0] == "open_api":
        segs = segs[1:]
    if segs and re.match(r"^(?:\d+|v\d+(?:\.\d+)?)$", segs[0]):
        segs = segs[1:]
    if platform == "Qianchuan" and len(segs) >= 2 and segs[0] == "qianchuan":
        segs = segs[1:]
    elif platform == "LocalPush" and len(segs) >= 2 and segs[0] == "local":
        segs = segs[1:]
    elif platform == "StarMap" and len(segs) >= 2 and segs[0] == "star":
        segs = segs[1:]
    elif platform == "ServeMarket" and len(segs) >= 2 and segs[0] == "serve_market":
        segs = segs[1:]
    return segs or ["default"]


def scalar_zan_type(field: dict) -> str:
    ftype = (field.get("type") or "").strip().lower()
    fmt = (field.get("format") or "").strip().lower()
    if ftype in ("boolean", "bool"):
        return "bool"
    if ftype in ("int",):
        return "int"
    if ftype == "number":
        if fmt in ("float", "double"):
            return "double"
        if fmt in ("int8", "int16", "int32"):
            return "int"
        return "long"
    if ftype in ("string", "url", "json string", "date", "datetime"):
        return "string"
    return "JsonValue?"


def array_elem_zan_type(field: dict) -> tuple[str, dict | None]:
    """Returns (element_type_in_zan_or_SUBMODEL, object_schema_if_nested_model)."""
    children = field.get("children") or []
    if not children:
        return ("string", None)
    if len(children) == 1 and not (children[0].get("name") or "").strip():
        elem = children[0]
        et = (elem.get("type") or "").strip().lower()
        ef = (elem.get("format") or "").strip().lower()
        ech = elem.get("children") or []
        if et in ("object", "dict", "json") and ech:
            return ("SUBMODEL", elem)
        if et in ("object", "dict", "json", "array"):
            return ("JsonValue", None)
        if et in ("boolean", "bool"):
            return ("bool", None)
        if et == "int":
            return ("int", None)
        if et == "number":
            if ef in ("float", "double"):
                return ("double", None)
            if ef in ("int8", "int16", "int32"):
                return ("int", None)
            return ("long", None)
        return ("string", None)
    # Named children directly under array -> array of object model.
    return ("SUBMODEL", {"children": children})


class ModelCollector:
    """Builds strongly-typed Response + nested submodel classes for a module."""

    def __init__(self, used_types: set[str]):
        self.used_types = used_types
        self.classes: list[list[str]] = []

    def alloc_type(self, candidate: str) -> str:
        name = candidate
        idx = 2
        while name in self.used_types:
            name = f"{candidate}{idx}"
            idx += 1
        self.used_types.add(name)
        return name

    def emit_model(self, class_name: str, fields: list[dict], is_response: bool = False,
                   direct_array: str = "", comment: str = "") -> str:
        lines: list[str] = []
        if comment:
            lines.append(f"// {strip_comment(comment, 100)}")
        lines.append(f"public class {class_name} {{")

        used_members = set()
        if is_response:
            lines.append("    public int StatusCode;")
            lines.append("    public string RequestId;")
            lines.append("    public string Raw;")
            lines.append("    public JsonValue? RawJson;")
            lines.append("    public JsonValue? DataJson;")
            used_members.update({"StatusCode", "RequestId", "Raw", "RawJson", "DataJson"})
        else:
            lines.append("    public JsonValue? RawJson;")
            used_members.add("RawJson")

        parsed_fields = []
        for f in fields:
            raw_name = (f.get("name") or "").strip()
            if not raw_name:
                continue
            if is_response and raw_name in ("code", "message", "request_id"):
                continue
            prop_name = safe_public_name(raw_name, used_members)
            ftype = (f.get("type") or "").strip().lower()
            children = f.get("children") or []

            if ftype in ("object", "json", "dict") and children:
                sub_name = self.alloc_type(class_name + pascal(raw_name))
                self.emit_model(sub_name, children, is_response=False,
                                comment=f.get("description") or "")
                zan_type = f"{sub_name}?"
                kind = ("submodel", sub_name)
            elif ftype == "array":
                elem_type, elem_schema = array_elem_zan_type(f)
                if elem_type == "SUBMODEL" and elem_schema is not None:
                    sub_name = self.alloc_type(class_name + pascal(raw_name) + "Item")
                    self.emit_model(sub_name, elem_schema.get("children") or [],
                                    is_response=False, comment=f.get("description") or "")
                    zan_type = f"List<{sub_name}>"
                    kind = ("submodel_list", sub_name)
                elif elem_type in ("string", "int", "long", "double", "bool", "JsonValue"):
                    zan_type = f"List<{elem_type}>"
                    kind = (f"list_{elem_type}", "")
                else:
                    zan_type = "JsonValue?"
                    kind = ("json", "")
            elif ftype in ("number[]",):
                zan_type = "List<long>"
                kind = ("list_long", "")
            elif ftype in ("object[]",):
                zan_type = "List<JsonValue>"
                kind = ("list_JsonValue", "")
            else:
                zan_type = scalar_zan_type(f)
                kind = ("scalar", zan_type)

            desc = strip_comment(f.get("description") or "", 80)
            if desc:
                lines.append(f"    // {desc}")
            lines.append(f"    public {zan_type} {prop_name};")
            parsed_fields.append((raw_name, prop_name, zan_type, kind))

        lines.append("")
        if is_response:
            lines.append(f"    public static {class_name} FromResponse(OceanEngineResponse resp) {{")
            lines.append(f"        {class_name} model = new {class_name}();")
            lines.append("        model.StatusCode = resp.StatusCode;")
            lines.append("        model.RequestId = resp.RequestId;")
            lines.append("        model.Raw = resp.Raw;")
            lines.append("        model.RawJson = resp.Value;")
            lines.append("        model.DataJson = resp.Data;")
            lines.append("        JsonValue? root = resp.Value;")
            source_var = "root"
        else:
            lines.append(f"    public static {class_name}? FromJson(JsonValue? obj) {{")
            lines.append("        if (obj != null && obj.IsString()) {")
            lines.append("            try { obj = JsonValue.Parse(obj.AsString(\"\")); } catch (Exception e) { }")
            lines.append("        }")
            lines.append("        if (obj == null || !obj.IsObject()) { return null; }")
            lines.append(f"        {class_name} model = new {class_name}();")
            lines.append("        model.RawJson = obj;")
            source_var = "obj"

        for raw_name, prop_name, zan_type, (kind, sub_name) in parsed_fields:
            key_lit = json.dumps(raw_name, ensure_ascii=False)
            src = source_var
            if is_response and raw_name == direct_array and direct_array:
                src = "(resp.Data != null ? resp.Data : root)"
            if kind == "submodel":
                lines.append(f"        model.{prop_name} = {sub_name}.FromJson(OceanEngineJson.GetValue({src}, {key_lit}));")
            elif kind == "submodel_list":
                lines.append(f"        model.{prop_name} = new List<{sub_name}>();")
                lines.append(f"        JsonValue? arr{prop_name} = OceanEngineJson.GetValue({src}, {key_lit});")
                lines.append(f"        if (arr{prop_name} != null && arr{prop_name}.IsArray()) {{")
                lines.append(f"            for (int i = 0; i < arr{prop_name}.Count(); i = i + 1) {{")
                lines.append(f"                {sub_name}? item = {sub_name}.FromJson(arr{prop_name}.At(i));")
                lines.append(f"                if (item != null) {{ model.{prop_name}.Add(({sub_name})item); }}")
                lines.append("            }")
                lines.append("        }")
            elif kind == "list_string":
                lines.append(f"        model.{prop_name} = OceanEngineJson.GetStringList({src}, {key_lit});")
            elif kind == "list_int":
                lines.append(f"        model.{prop_name} = OceanEngineJson.GetIntList({src}, {key_lit});")
            elif kind == "list_long":
                lines.append(f"        model.{prop_name} = OceanEngineJson.GetLongList({src}, {key_lit});")
            elif kind == "list_double":
                lines.append(f"        model.{prop_name} = OceanEngineJson.GetDoubleList({src}, {key_lit});")
            elif kind == "list_bool":
                lines.append(f"        model.{prop_name} = OceanEngineJson.GetBoolList({src}, {key_lit});")
            elif kind == "list_JsonValue":
                lines.append(f"        model.{prop_name} = OceanEngineJson.GetJsonList({src}, {key_lit});")
            elif kind == "scalar":
                if sub_name == "string":
                    lines.append(f"        model.{prop_name} = OceanEngineJson.GetString({src}, {key_lit}, \"\");")
                elif sub_name == "int":
                    lines.append(f"        model.{prop_name} = OceanEngineJson.GetInt({src}, {key_lit}, 0);")
                elif sub_name == "long":
                    lines.append(f"        model.{prop_name} = OceanEngineJson.GetLong({src}, {key_lit}, 0L);")
                elif sub_name == "double":
                    lines.append(f"        model.{prop_name} = OceanEngineJson.GetDouble({src}, {key_lit}, 0.0);")
                elif sub_name == "bool":
                    lines.append(f"        model.{prop_name} = OceanEngineJson.GetBool({src}, {key_lit}, false);")
                else:
                    lines.append(f"        model.{prop_name} = OceanEngineJson.GetValue({src}, {key_lit});")
            else:
                lines.append(f"        model.{prop_name} = OceanEngineJson.GetValue({src}, {key_lit});")

        lines.append("        return model;")
        lines.append("    }")
        lines.append("}")
        self.classes.append(lines)
        return class_name


def build_request_class(req_class: str, fields: list[dict], http_method: str,
                        is_multipart: bool, comment: str) -> list[str]:
    lines: list[str] = []
    if comment:
        lines.append(f"// {strip_comment(comment, 100)}")
    lines.append(f"public class {req_class} {{")
    lines.append("    private OceanEngineTypedRequest req;")
    lines.append("")
    lines.append(f"    public {req_class}() {{")
    lines.append("        this.req = new OceanEngineTypedRequest();")
    lines.append("    }")
    lines.append("")

    sig_field = ""
    for f in fields:
        fn = (f.get("name") or "").strip()
        if fn in ("file_signature", "image_signature", "video_signature", "audio_signature"):
            sig_field = fn
            break

    used_members = {"SetField", "QueryObject", "BodyObject", "BuildMultipart"}
    for f in fields:
        raw_name = (f.get("name") or "").strip()
        if not raw_name:
            continue
        method_name = safe_public_name(raw_name, used_members)
        pname = safe_param_name(raw_name)
        ftype = (f.get("type") or "").strip().lower()
        fmt = (f.get("format") or "").strip().lower()
        desc = strip_comment(f.get("description") or "", 80)
        key_lit = json.dumps(raw_name, ensure_ascii=False)

        if is_multipart and (ftype == "file" or fmt == "binary"):
            if desc:
                lines.append(f"    // {desc}")
            sig_lit = json.dumps(sig_field, ensure_ascii=False)
            lines.append(f"    public {req_class} {method_name}(string fileName, string mediaType, byte[] bytes) {{")
            lines.append(f"        this.req.FormFile({key_lit}, fileName, mediaType, bytes, {sig_lit});")
            lines.append("        return this;")
            lines.append("    }")
            lines.append("")
            continue

        if is_multipart:
            if ftype in ("boolean", "bool"):
                ptype, call = "bool", "FormBool"
            elif ftype == "int" or (ftype == "number" and fmt in ("int8", "int16", "int32")):
                ptype, call = "int", "FormInt"
            elif ftype == "number" and fmt in ("float", "double"):
                ptype, call = "double", "FormField"
            elif ftype == "number":
                ptype, call = "long", "FormLong"
            elif ftype in ("array", "object", "json", "dict"):
                ptype, call = "JsonValue?", "FormJson"
            else:
                ptype, call = "string", "FormField"
            if desc:
                lines.append(f"    // {desc}")
            lines.append(f"    public {req_class} {method_name}({ptype} {pname}) {{")
            if ptype == "double":
                lines.append(f"        this.req.FormField({key_lit}, Convert.ToString({pname}));")
            else:
                lines.append(f"        this.req.{call}({key_lit}, {pname});")
            lines.append("        return this;")
            lines.append("    }")
            lines.append("")
            continue

        prefix = "Query" if http_method == "GET" else "Body"
        if ftype in ("boolean", "bool"):
            ptype, call = "bool", f"{prefix}Bool"
        elif ftype == "int" or (ftype == "number" and fmt in ("int8", "int16", "int32")):
            ptype, call = "int", f"{prefix}Int"
        elif ftype == "number" and fmt in ("float", "double"):
            ptype, call = "double", f"{prefix}Double"
        elif ftype == "number":
            ptype, call = "long", f"{prefix}Long"
        elif ftype == "array":
            elem_type, elem_schema = array_elem_zan_type(f)
            if elem_schema is not None or elem_type == "JsonValue":
                ptype, call = "JsonValue?", f"{prefix}Value"
            elif elem_type == "string":
                ptype, call = "List<string>", f"{prefix}Strings"
            elif elem_type == "int":
                ptype, call = "List<int>", f"{prefix}Ints"
            elif elem_type == "long":
                ptype, call = "List<long>", f"{prefix}Longs"
            elif elem_type == "double" and prefix == "Body":
                ptype, call = "List<double>", "BodyDoubles"
            elif elem_type == "bool" and prefix == "Body":
                ptype, call = "List<bool>", "BodyBools"
            else:
                ptype, call = "JsonValue?", f"{prefix}Value"
        elif ftype == "number[]":
            ptype, call = "List<long>", f"{prefix}Longs"
        elif ftype in ("object", "json", "dict", "object[]"):
            ptype, call = "JsonValue?", f"{prefix}Value"
        else:
            ptype, call = "string", f"{prefix}String"

        if desc:
            lines.append(f"    // {desc}")
        lines.append(f"    public {req_class} {method_name}({ptype} {pname}) {{")
        lines.append(f"        this.req.{call}({key_lit}, {pname});")
        lines.append("        return this;")
        lines.append("    }")
        lines.append("")

    if is_multipart:
        lines.append(f"    public {req_class} SetField(string key, string val) {{")
        lines.append("        this.req.FormField(key, val);")
        lines.append("        return this;")
        lines.append("    }")
        lines.append("")
        lines.append("    public OceanEngineMultipart BuildMultipart() {")
        lines.append("        return this.req.BuildMultipart();")
        lines.append("    }")
    elif http_method == "GET":
        lines.append(f"    public {req_class} SetField(string key, JsonValue? val) {{")
        lines.append("        this.req.QueryValue(key, val);")
        lines.append("        return this;")
        lines.append("    }")
        lines.append("")
        lines.append("    public JsonValue? QueryObject() {")
        lines.append("        return this.req.QueryObject();")
        lines.append("    }")
    else:
        lines.append(f"    public {req_class} SetField(string key, JsonValue? val) {{")
        lines.append("        this.req.BodyValue(key, val);")
        lines.append("        return this;")
        lines.append("    }")
        lines.append("")
        lines.append("    public JsonValue BodyObject() {")
        lines.append("        return this.req.BodyObject();")
        lines.append("    }")

    lines.append("}")
    return lines


def chunk_and_write_models(out_dir: pathlib.Path, ns: str, class_blocks: list[list[str]]) -> list[pathlib.Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    header = [
        "// 由 scripts/generate_oceanengine_sdk.py 自动生成 —— 请勿手工编辑。",
        "",
        "using System;",
        "using System.Json;",
        "using Sdk.OceanEngine;",
        "",
        f"namespace {ns};",
        "",
    ]
    written: list[pathlib.Path] = []
    part = 1
    current = list(header)
    for block in class_blocks:
        if len(current) > len(header) and len(current) + len(block) + 1 > MAX_LINES_PER_FILE:
            p = out_dir / f"ModelsPart{part}.zan"
            p.write_text("\n".join(current) + "\n", encoding="utf-8")
            written.append(p)
            part += 1
            current = list(header)
        current.extend(block)
        current.append("")
    if len(current) > len(header):
        p = out_dir / f"ModelsPart{part}.zan"
        p.write_text("\n".join(current) + "\n", encoding="utf-8")
        written.append(p)
    return written


def parse_spi_markdown_fields(md_text: str) -> tuple[str, list[dict]]:
    """Parse SPI service_label and nested request table fields from markdown."""
    sl_match = re.findall(r"`([a-z0-9_.-]+)`", md_text)
    service_label = ""
    for candidate in sl_match:
        if "." in candidate and not candidate.startswith("http") and not candidate.endswith(".json"):
            service_label = candidate
            break

    lines = md_text.splitlines()
    in_req = False
    root_fields: list[dict] = []
    stack: list[tuple[int, list[dict]]] = [(0, root_fields)]

    for line in lines:
        s = line.strip()
        if s.startswith("## "):
            in_req = s == "## 请求参数"
            continue
        if not in_req:
            continue
        if not s.startswith("|") or s.startswith("| 字段") or s.startswith("| ---"):
            continue
        cols = [c.strip() for c in s.strip("|").split("|")]
        if len(cols) < 3:
            continue
        raw_col, ftype, desc = cols[0], cols[1], cols[2]
        depth = raw_col.count("↳")
        fname = raw_col.replace("↳", "").replace("\\_", "_").strip()
        if not fname or not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", fname):
            continue
        norm_type = ftype.replace("\\[", "[").replace("\\]", "]").strip().lower()
        node: dict = {"name": fname, "description": desc}
        if norm_type.endswith("[]"):
            inner = norm_type[:-2]
            if inner in ("number", "int", "int64"):
                node["type"] = "array"
                node["children"] = [{"name": "", "type": "number", "format": "int64"}]
            elif inner == "string":
                node["type"] = "array"
                node["children"] = [{"name": "", "type": "string"}]
            else:
                node["type"] = "array"
                node["children"] = [{"name": "", "type": "object", "children": []}]
        elif norm_type == "number":
            node["type"] = "number"
            node["format"] = "int64"
        elif norm_type in ("boolean", "bool"):
            node["type"] = "boolean"
        elif norm_type in ("object", "json", "map", "json string", "dict", ""):
            node["type"] = "object"
            node["children"] = []
        else:
            node["type"] = "string"

        while len(stack) > 1 and stack[-1][0] >= depth + 1:
            stack.pop()
        parent_list = stack[-1][1]
        parent_list.append(node)
        if node["type"] == "object":
            stack.append((depth + 1, node["children"]))
        elif node["type"] == "array" and node.get("children") and node["children"][0].get("type") == "object":
            stack.append((depth + 1, node["children"][0]["children"]))

    return service_label, root_fields


def generate_spi(spi_contracts: list[tuple[dict, list[str]]]) -> list[pathlib.Path]:
    spi_dir = SDK_ROOT / "Spi"
    spi_dir.mkdir(parents=True, exist_ok=True)
    written: list[pathlib.Path] = []

    spi_core = [
        "// 由 scripts/generate_oceanengine_sdk.py 自动生成 —— 请勿手工编辑。",
        "",
        "using System;",
        "using System.Json;",
        "using System.Security.Cryptography;",
        "using System.Text;",
        "",
        "namespace Sdk.OceanEngine.Spi;",
        "",
        "// 巨量引擎 SPI 消息回调验签与 Webhook Challenge 解析。",
        "public class OceanEngineSpi {",
        "    // 使用 X-Open-Signature 的小写十六进制值与未经解析的原始 HTTP 请求体字节验签。",
        "    public static bool VerifySignature(string secretKey, byte[] rawBody, string signature) {",
        "        if (secretKey == null || rawBody == null || signature == null) { return false; }",
        "        if (secretKey.Length == 0 || signature.Length != 64) { return false; }",
        "        string expected = Hex.Encode(Hmac.Sha256(Encoding.GetBytes(secretKey), rawBody)).ToLower();",
        "        int diff = 0;",
        "        for (int i = 0; i < 64; i = i + 1) {",
        "            diff = diff | (expected[i] ^ signature[i]);",
        "        }",
        "        return diff == 0;",
        "    }",
        "",
        "    // 解析 SPI 推送 JSON 中的 service_label（若缺失则返回空串）。",
        "    public static string ExtractServiceLabel(string rawBodyJson) {",
        "        if (rawBodyJson == null || rawBodyJson.Length == 0) { return \"\"; }",
        "        try {",
        "            JsonValue? root = JsonValue.Parse(rawBodyJson);",
        "            if (root != null && root.IsObject()) { return root.Str(\"service_label\", \"\"); }",
        "        } catch (Exception e) { }",
        "        return \"\";",
        "    }",
        "",
        "    // 处理接入验证时的 challenge 握手，返回需要原样响应的 JSON（非验证请求返回空串）。",
        "    public static string TryBuildChallengeResponse(string rawBodyJson) {",
        "        if (rawBodyJson == null || rawBodyJson.Length == 0) { return \"\"; }",
        "        try {",
        "            JsonValue? root = JsonValue.Parse(rawBodyJson);",
        "            if (root != null && root.IsObject()) {",
        "                JsonValue? ch = root.Get(\"challenge\");",
        "                if (ch != null && !ch.IsNull()) {",
        "                    JsonValue resp = JsonValue.NewObject();",
        "                    resp.Set(\"challenge\", (JsonValue)ch);",
        "                    return resp.ToJson();",
        "                }",
        "            }",
        "        } catch (Exception e) { }",
        "        return \"\";",
        "    }",
        "}",
        "",
    ]
    p_core = spi_dir / "OceanEngineSpi.zan"
    p_core.write_text("\n".join(spi_core), encoding="utf-8")
    written.append(p_core)

    used_types = {"OceanEngineSpi"}
    collector = ModelCollector(used_types)

    for contract, plats in spi_contracts:
        doc_id = str(contract["doc_id"])
        md_path = API_DOCS_ROOT / f"{doc_id}.md"
        md_text = md_path.read_text(encoding="utf-8") if md_path.exists() else ""
        service_label, fields = parse_spi_markdown_fields(md_text)
        plat_tag = plats[0] if len(plats) == 1 else "Common"
        base_name = f"OceanEngineSpi{plat_tag}Event{doc_id}"
        cls_name = collector.alloc_type(base_name)
        comment = f"{contract['title']} ({'/'.join(plats)}, doc={doc_id}, service_label={service_label or 'unknown'})"
        collector.emit_model(cls_name, fields, is_response=False, comment=comment)

    written.extend(chunk_and_write_models(spi_dir, "Sdk.OceanEngine.Spi", collector.classes))
    return written


def assign_modules(contracts_by_platform: dict[str, list[dict]]) -> dict[str, dict[str, list[tuple[dict, list[str]]]]]:
    """Assign each platform's contracts into balanced modules."""
    result: dict[str, dict[str, list[tuple[dict, list[str]]]]] = {}
    for plat, items in contracts_by_platform.items():
        first_groups: dict[str, list[tuple[dict, list[str]]]] = collections.defaultdict(list)
        for c in items:
            segs = path_segments(c["url"], plat)
            first_groups[segs[0]].append((c, segs))

        mod_groups: dict[str, list[tuple[dict, list[str]]]] = collections.defaultdict(list)
        for gkey, glist in first_groups.items():
            if len(glist) > SPLIT_MODULE_THRESHOLD:
                for c, segs in glist:
                    sub = f"{gkey}_{segs[1]}" if len(segs) >= 2 else gkey
                    mod_groups[sub].append((c, segs))
            else:
                mod_groups[gkey].extend(glist)
        result[plat] = dict(mod_groups)
    return result


def main() -> None:
    idx = json.loads((CONTRACTS_ROOT / "index.json").read_text(encoding="utf-8"))
    labels_meta = idx["labels"]

    # Clean previously generated platform Api/Models and Spi directories (preserve PlatformClient.zan & shared core).
    for plat in PLATFORM_ORDER:
        for sub in ("Api", "Models"):
            target = SDK_ROOT / plat / sub
            if target.exists():
                shutil.rmtree(target)
    spi_dir = SDK_ROOT / "Spi"
    if spi_dir.exists():
        shutil.rmtree(spi_dir)

    contracts_by_platform: dict[str, list[dict]] = {p: [] for p in PLATFORM_ORDER}
    spi_contracts: list[tuple[dict, list[str]]] = []

    for entry in idx["contracts"]:
        doc_id = str(entry["doc_id"])
        data = json.loads((CONTRACTS_ROOT / f"{doc_id}.json").read_text(encoding="utf-8"))
        plats = sorted({PLATFORM_MAP[labels_meta[l]["platform"]] for l in entry.get("labels", []) if l in labels_meta})
        if data["kind"] == "spi_callback":
            spi_contracts.append((data, plats))
        else:
            for p in plats:
                contracts_by_platform[p].append(data)

    modules_by_platform = assign_modules(contracts_by_platform)
    total_files = 0
    total_apis = 0
    max_lines = 0
    max_line_file = ""

    for plat in PLATFORM_ORDER:
        client_cls = f"OceanEngine{plat}Client"
        plat_ns = f"Sdk.OceanEngine.{plat}"
        mod_map = modules_by_platform[plat]

        for mod_raw, entries in sorted(mod_map.items()):
            mod_pascal = pascal(mod_raw)
            api_ns = f"{plat_ns}.Api.{mod_pascal}"
            models_ns = f"{plat_ns}.Models.{mod_pascal}"
            api_cls = f"OceanEngine{plat}{mod_pascal}Api"

            # Prefix segment count stripped for concise method names.
            prefix_len = 2 if "_" in mod_raw else 1

            # Determine unique method names inside this module.
            prelim = []
            for c, segs in entries:
                rem = segs[prefix_len:]
                if not rem:
                    rem = [segs[-1]]
                op_base = "".join(pascal(s) for s in rem)
                prelim.append((c, segs, op_base))

            counts = collections.Counter(op for _, _, op in prelim)
            used_methods: set[str] = set()
            used_types: set[str] = set()
            collector = ModelCollector(used_types)
            req_blocks: list[list[str]] = []
            api_methods: list[str] = []

            for c, segs, op_base in sorted(prelim, key=lambda x: (x[2], str(x[0]["doc_id"]))):
                doc_id = str(c["doc_id"])
                http_method = c["method"]
                url_path = urllib.parse.urlparse(c["url"]).path
                is_multipart = c.get("content_type") == "multipart/form-data"
                gw = gateway_enum(c["url"])
                cred = determine_credential(c, url_path)

                op_name = op_base
                if counts[op_base] > 1:
                    op_name = pascal(http_method.lower()) + op_base
                if op_name in used_methods:
                    op_name = op_name + f"Doc{doc_id}"
                used_methods.add(op_name)

                req_fields = (c.get("request") or {}).get("fields") or []
                resp_fields = c.get("response") or []
                direct_array = ""
                if doc_id == "1736331299814412":
                    req_fields = SERVE_MARKET_ACTIVE_FUNC_FIX["request_fields"]
                    resp_fields = SERVE_MARKET_ACTIVE_FUNC_FIX["response_fields"]
                    direct_array = SERVE_MARKET_ACTIVE_FUNC_FIX["direct_array"]

                req_cls = collector.alloc_type(f"{plat}{mod_pascal}{op_name}Request")
                resp_cls = collector.alloc_type(f"{plat}{mod_pascal}{op_name}Response")

                req_blocks.append(build_request_class(req_cls, req_fields, http_method, is_multipart,
                                                      f"{c['title']} ({url_path}, doc={doc_id})"))
                collector.emit_model(resp_cls, resp_fields, is_response=True,
                                     direct_array=direct_array,
                                     comment=f"{c['title']} ({url_path}, doc={doc_id})")

                path_lit = json.dumps(url_path, ensure_ascii=False)
                da_arg = f", {json.dumps(direct_array)}" if direct_array else ""
                api_methods.append(f"    // {strip_comment(c['title'], 80)} ({http_method} {url_path}, doc={doc_id})")
                api_methods.append(f"    public async {resp_cls} {op_name}Async({req_cls} request, string token = \"\") {{")
                api_methods.append(f"        if (request == null) {{ request = new {req_cls}(); }}")
                if is_multipart:
                    api_methods.append(
                        f"        OceanEngineResponse raw = await this.client.PostMultipartAsync({gw}, {path_lit}, "
                        f"request.BuildMultipart(), {cred}, token{da_arg});"
                    )
                elif http_method == "GET":
                    api_methods.append(
                        f"        OceanEngineResponse raw = await this.client.GetAsync({gw}, {path_lit}, "
                        f"request.QueryObject(), {cred}, token{da_arg});"
                    )
                else:
                    api_methods.append(
                        f"        OceanEngineResponse raw = await this.client.PostJsonAsync({gw}, {path_lit}, "
                        f"request.BodyObject(), {cred}, token{da_arg});"
                    )
                api_methods.append(f"        return {resp_cls}.FromResponse(raw);")
                api_methods.append("    }")
                api_methods.append("")

                if not req_fields:
                    api_methods.append(f"    public async {resp_cls} {op_name}Async(string token = \"\") {{")
                    api_methods.append(f"        return await this.{op_name}Async(new {req_cls}(), token);")
                    api_methods.append("    }")
                    api_methods.append("")

                if is_multipart:
                    api_methods.append(
                        f"    public async OceanEngineResponse {op_name}RawAsync(OceanEngineMultipart form, string token = \"\") {{"
                    )
                    api_methods.append(
                        f"        return await this.client.PostMultipartAsync({gw}, {path_lit}, form, {cred}, token{da_arg});"
                    )
                elif http_method == "GET":
                    api_methods.append(
                        f"    public async OceanEngineResponse {op_name}RawAsync(JsonValue? query = null, string token = \"\") {{"
                    )
                    api_methods.append(
                        f"        return await this.client.GetAsync({gw}, {path_lit}, query, {cred}, token{da_arg});"
                    )
                else:
                    api_methods.append(
                        f"    public async OceanEngineResponse {op_name}RawAsync(JsonValue? body = null, string token = \"\") {{"
                    )
                    api_methods.append(
                        f"        return await this.client.PostJsonAsync({gw}, {path_lit}, body, {cred}, token{da_arg});"
                    )
                api_methods.append("    }")
                api_methods.append("")
                total_apis += 1

            # Write Models (Requests + Responses/Submodels) chunked <= MAX_LINES_PER_FILE.
            models_dir = SDK_ROOT / plat / "Models" / mod_pascal
            model_files = chunk_and_write_models(models_dir, models_ns, req_blocks + collector.classes)
            for mf in model_files:
                lc = len(mf.read_text(encoding="utf-8").splitlines())
                total_files += 1
                if lc > max_lines:
                    max_lines, max_line_file = lc, str(mf)

            # Write Api class(es), splitting if > MAX_LINES_PER_FILE (partial or PartN if ever needed).
            api_dir = SDK_ROOT / plat / "Api" / mod_pascal
            api_dir.mkdir(parents=True, exist_ok=True)
            api_lines = [
                "// 由 scripts/generate_oceanengine_sdk.py 自动生成 —— 请勿手工编辑。",
                "",
                "using System;",
                "using System.Json;",
                "using Sdk.OceanEngine;",
                f"using {plat_ns};",
                f"using {models_ns};",
                "",
                f"namespace {api_ns};",
                "",
                f"public class {api_cls} {{",
                f"    private {client_cls} client;",
                "",
                f"    public {api_cls}({client_cls} client) {{",
                "        if (client == null) { throw new ArgumentException(\"client 不能为空\"); }",
                "        this.client = client;",
                "    }",
                "",
                *api_methods,
                "}",
                "",
            ]
            api_path = api_dir / f"{api_cls}.zan"
            api_path.write_text("\n".join(api_lines), encoding="utf-8")
            lc = len(api_lines)
            total_files += 1
            if lc > max_lines:
                max_lines, max_line_file = lc, str(api_path)

    spi_files = generate_spi(spi_contracts)
    for sf in spi_files:
        lc = len(sf.read_text(encoding="utf-8").splitlines())
        total_files += 1
        if lc > max_lines:
            max_lines, max_line_file = lc, str(sf)

    print(f"Generated {total_apis} platform-scoped API endpoints + {len(spi_contracts)} SPI callbacks across {total_files} files.")
    print(f"Max lines in a single generated file: {max_lines} ({max_line_file})")
    for plat in PLATFORM_ORDER:
        mods = modules_by_platform[plat]
        ep_cnt = sum(len(v) for v in mods.values())
        print(f"  {plat:12s}: {len(mods):3d} modules, {ep_cnt:3d} endpoints")


if __name__ == "__main__":
    main()
