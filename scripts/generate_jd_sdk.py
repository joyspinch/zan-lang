#!/usr/bin/env python3
"""Generate the Zan JD (JOS) API surface from the vendored C# SDK.

Reads stdlib/Sdk/jos-net-open-api-sdk-2.0/{Domain,Request,Response}/*.cs and
emits Zan classes under stdlib/Sdk/Jd/:

  Domain/           212 shared business models (Sdk.Jd.Domain)
  Api/<module>/     211 request + response contracts plus 44 module API clients,
                    split by API module so that `using Sdk.Jd.Api.Ware;` does
                    not drag in all 211 interfaces (auto-stdlib globs a
                    namespace directory whole).

Requests are strongly typed: each carries its API name and typed setters that
feed the untyped JdRequest underneath. Module API methods execute the request,
deserialize the response contract automatically, and keep the full envelope in
Response.Raw. Reusable business entities remain centralized in Domain/.

Run:  python scripts/generate_jd_sdk.py
"""

import os
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
SRC = os.path.join(ROOT, "stdlib", "Sdk", "jos-net-open-api-sdk-2.0")
OUT = os.path.join(ROOT, "stdlib", "Sdk", "Jd")

# C# type -> Zan type. DateTime is not bindable by Json.Deserialize and the
# gateway sends dates as strings anyway; Dictionary/object become JsonValue so
# the caller can walk whatever arrived.
SCALARS = {
    "string": "string",
    "int": "int",
    "long": "long",
    "short": "int",
    "bool": "bool",
    "double": "double",
    "float": "double",
    "decimal": "double",
    "DateTime": "string",
    "System.DateTime": "string",
    "object": "JsonValue",
    "Object": "JsonValue",
    "System.Object": "JsonValue",
}

# Zan keywords that cannot be used as field or parameter names.
RESERVED = {
    "abstract", "as", "async", "await", "base", "bool", "break", "case",
    "catch", "char", "class", "const", "continue", "default", "delegate",
    "do", "double", "else", "enum", "event", "explicit", "extern", "false",
    "finally", "fixed", "float", "for", "foreach", "get", "goto", "if",
    "implicit", "in", "int", "interface", "internal", "is", "lock", "long",
    "namespace", "new", "nint", "null", "object", "operator", "out",
    "override", "params", "private", "protected", "public", "readonly", "ref",
    "return", "sbyte", "sealed", "set", "short", "sizeof", "stackalloc",
    "static", "string", "struct", "switch", "this", "throw", "true", "try",
    "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort", "using",
    "value", "var", "virtual", "void", "volatile", "while",
}


def flat(path):
    with open(path, encoding="utf-8-sig", errors="replace") as fh:
        return re.sub(r"\s+", " ", fh.read())


def safe(name):
    return name + "_" if name in RESERVED else name


def map_type(cs, known_domains):
    """C# declared type -> Zan type; fail when an entity was not classified."""
    cs = cs.strip()
    if cs.startswith("Nullable<") and cs.endswith(">"):
        return map_type(cs[len("Nullable<"):-1], known_domains)
    if cs.startswith("System.Nullable<") and cs.endswith(">"):
        return map_type(cs[len("System.Nullable<"):-1], known_domains)
    if cs.startswith("Dictionary<") or cs.startswith("IDictionary<"):
        return "JsonValue"
    if cs.endswith("[]"):
        inner = map_type(cs[:-2], known_domains)
        if inner == "JsonValue":
            return "JsonValue"
        return "List<" + inner + ">"
    for prefix in ("List<", "IList<", "ICollection<", "IEnumerable<"):
        if cs.startswith(prefix) and cs.endswith(">"):
            inner = map_type(cs[len(prefix):-1], known_domains)
            if inner == "JsonValue":
                return "JsonValue"
            return "List<" + inner + ">"
    if cs in SCALARS:
        return SCALARS[cs]
    if cs in known_domains:
        return cs
    raise ValueError("unmapped C# entity type: %s" % cs)


PROP_RE = re.compile(
    r'\[JsonProperty\("([^"]+)"\)\]\s*public\s+'
    r"([A-Za-z0-9_<>\[\]\.,\s]+?)\s+(\w+)\s*\{\s*get;\s*set;\s*\}"
)


def parse_typed(path, base_re):
    """Parses a Domain/Response class into (class name, [(json key, C# type, field)])."""
    text = flat(path)
    m = base_re.search(text)
    if not m:
        return None
    fields = [(j.group(1), j.group(2).strip(), j.group(3))
              for j in PROP_RE.finditer(text)]
    return m.group(1), fields


def parse_request(path):
    """Parses a Request class into (class, response class, api name, params)."""
    text = flat(path)
    m = re.search(r"class\s+(\w+)\s*:\s*JdRequestBase<(\w+)>", text)
    a = re.search(r'ApiName\s*\{\s*get\s*\{\s*return\s+"([^"]+)"', text)
    if not m or not a:
        return None
    props = dict(
        (p.group(2), p.group(1).strip())
        for p in re.finditer(
            r"public\s+([A-Za-z0-9_<>\[\]\.,\s]+?)\s+(\w+)\s*\{\s*get;\s*set;\s*\}",
            text)
    )
    # Preserve the order the C# PrepareParam adds them in.
    params = [(add.group(1), props.get(add.group(2), "string"), add.group(2))
              for add in re.finditer(
                  r'parameters\.Add\("([^"]+)",\s*this\.\s*(\w+)\s*\)', text)]
    return m.group(1), m.group(2), a.group(1), params


def doc_header(origin):
    return ("// Generated from %s by scripts/generate_jd_sdk.py -- do not edit.\n"
            % origin)


def emit_fields(fields, known_domains, indent="    "):
    lines = []
    seen = set()
    for key, cs_type, name in fields:
        zt = map_type(cs_type, known_domains)
        fname = safe(name)
        if fname in seen:
            continue
        seen.add(fname)
        # The JSON key drives binding, so the field must be named after it.
        # When the key is not a valid identifier the field is unreachable by
        # the binder; skip it rather than emit something that never binds.
        if key != name:
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key):
                fname = safe(key)
                if fname in seen:
                    continue
                seen.add(fname)
            else:
                continue
        lines.append("%spublic %s %s;" % (indent, zt, fname))
    return lines


def setter_for(zan_type):
    if zan_type == "string":
        return "Set", None
    if zan_type == "int":
        return "SetInt", None
    if zan_type == "long":
        return "SetLong", None
    if zan_type == "bool":
        return "SetBool", None
    if zan_type == "double":
        return "Set", "str"
    return "SetValue", None


def api_method_name(api):
    parts = api.split(".")[2:]
    words = []
    for part in parts:
        clean = re.sub(r"[^A-Za-z0-9]", "", part)
        if clean:
            words.append(clean[:1].upper() + clean[1:])
    return "".join(words) + "Async"


def main():
    if not os.path.isdir(SRC):
        sys.exit("vendored C# SDK not found at %s" % SRC)

    domain_files = sorted(f for f in os.listdir(os.path.join(SRC, "Domain"))
                          if f.endswith(".cs"))
    dom_re = re.compile(r"class\s+(\w+)\s*:\s*JdObject")
    resp_re = re.compile(r"class\s+(\w+)\s*:\s*JdResponse")

    domains = []
    for f in domain_files:
        parsed = parse_typed(os.path.join(SRC, "Domain", f), dom_re)
        if parsed:
            domains.append((f, parsed[0], parsed[1]))
    known = set(d[1] for d in domains)

    # ---- Domain ----------------------------------------------------------
    dom_dir = os.path.join(OUT, "Domain")
    os.makedirs(dom_dir, exist_ok=True)
    for origin, cls, fields in domains:
        body = emit_fields(fields, known)
        if not body:
            body = ["    // (no bindable fields in the source class)"]
        text = (doc_header("Domain/" + origin)
                + "using System;\nusing System.Json;\n\n"
                + "namespace Sdk.Jd.Domain;\n\n"
                + "class %s {\n" % cls
                + "\n".join(body) + "\n}\n")
        with open(os.path.join(dom_dir, cls + ".zan"), "w",
                  encoding="utf-8") as fh:
            fh.write(text)

    # ---- Requests + Responses, grouped by API module ---------------------
    requests = []
    for f in sorted(os.listdir(os.path.join(SRC, "Request"))):
        if not f.endswith(".cs"):
            continue
        parsed = parse_request(os.path.join(SRC, "Request", f))
        if parsed:
            requests.append((f,) + parsed)

    responses = {}
    for f in sorted(os.listdir(os.path.join(SRC, "Response"))):
        if not f.endswith(".cs"):
            continue
        parsed = parse_typed(os.path.join(SRC, "Response", f), resp_re)
        if parsed:
            responses[parsed[0]] = (f, parsed[1])

    missing_responses = sorted(entry[2] for entry in requests
                               if entry[2] not in responses)
    if missing_responses:
        raise ValueError("requests without response contracts: %s"
                         % ", ".join(missing_responses))

    by_module = defaultdict(list)
    for entry in requests:
        api = entry[3]
        parts = api.split(".")
        mod = parts[1] if len(parts) > 1 else "misc"
        by_module[mod].append(entry)

    def modname(mod):
        # jingdong.<module> segments are lowercase / camelCase; make a
        # PascalCase namespace segment out of them.
        return re.sub(r"[^A-Za-z0-9]", "", mod[:1].upper() + mod[1:])

    written = 0
    for mod, entries in sorted(by_module.items()):
        ns = modname(mod)
        mdir = os.path.join(OUT, "Api", ns)
        os.makedirs(mdir, exist_ok=True)
        for origin, cls, resp_cls, api, params in entries:
            resp = responses.get(resp_cls)
            rfields = emit_fields(resp[1], known) if resp else []
            if not rfields:
                rfields = ["    // (gateway returns no typed fields)"]

            lines = [doc_header("Request/" + origin),
                     "using System;",
                     "using System.Json;",
                     "using Sdk.Jd;",
                     "using Sdk.Jd.Domain;",
                     "",
                     "namespace Sdk.Jd.Api.%s;" % ns,
                     "",
                     "/// <summary>Response of <c>%s</c>.</summary>" % api,
                     "class %s {" % resp_cls]
            lines.extend(rfields)
            lines.append("    /// <summary>Full JOS response envelope for diagnostics.</summary>")
            lines.append("    public string Raw;")
            lines.append("}")
            lines.append("")
            lines.append("/// <summary>Request for <c>%s</c>.</summary>" % api)
            lines.append("class %s {" % cls)
            lines.append("    JdRequest req;")
            lines.append("")
            lines.append("    public %s() {" % cls)
            lines.append('        this.req = new JdRequest("%s");' % api)
            lines.append("    }")
            lines.append("")
            for key, cs_type, name in params:
                zt = map_type(cs_type, known)
                setter, conv = setter_for(zt)
                arg = safe(name)
                lines.append("    /// <summary>Sets the <c>%s</c> parameter.</summary>"
                             % key)
                lines.append("    %s %s(%s %s) {"
                             % (cls, name[:1].upper() + name[1:], zt, arg))
                if conv == "str":
                    lines.append('        this.req.Set("%s", Convert.ToString(%s));'
                                 % (key, arg))
                else:
                    lines.append('        this.req.%s("%s", %s);'
                                 % (setter, key, arg))
                lines.append("        return this;")
                lines.append("    }")
                lines.append("")
            lines.append("    /// <summary>The underlying protocol request.</summary>")
            lines.append("    JdRequest Raw() { return this.req; }")
            lines.append("}")

            with open(os.path.join(mdir, cls + ".zan"), "w",
                      encoding="utf-8") as fh:
                fh.write("\n".join(lines) + "\n")
            written += 1

        api_class = "Jd" + ns + "Api"
        api_lines = [doc_header("Request/%s module" % mod),
                     "using System;",
                     "using System.Json;",
                     "using Sdk.Jd;",
                     "",
                     "namespace Sdk.Jd.Api.%s;" % ns,
                     "",
                     "/// <summary>Strongly typed client for jingdong.%s.*.</summary>" % mod,
                     "class %s {" % api_class,
                     "    JdClient client;",
                     "",
                     "    public %s(JdClient client) { this.client = client; }" % api_class,
                     ""]
        seen_methods = set()
        for origin, cls, resp_cls, api, params in entries:
            method = api_method_name(api)
            if method in seen_methods:
                raise ValueError("duplicate generated method %s.%s" % (api_class, method))
            seen_methods.add(method)
            api_lines.append("    /// <summary>Executes <c>%s</c>.</summary>" % api)
            api_lines.append("    async %s %s(%s request) {" % (resp_cls, method, cls))
            api_lines.append("        JdResponse raw = await this.client.ExecuteAsync(request.Raw());")
            api_lines.append("        %s result = Json.Deserialize<%s>(raw.Data);" %
                             (resp_cls, resp_cls))
            api_lines.append("        result.Raw = raw.Raw;")
            api_lines.append("        return result;")
            api_lines.append("    }")
            api_lines.append("")
        api_lines.append("}")
        with open(os.path.join(mdir, api_class + ".zan"), "w",
                  encoding="utf-8") as fh:
            fh.write("\n".join(api_lines) + "\n")

    parsed_fields = sum(len(fields) for _, _, fields in domains)
    parsed_fields += sum(len(fields) for _, fields in responses.values())
    print("domains: %d" % len(domains))
    print("api contracts: %d across %d modules" % (written, len(by_module)))
    print("typed module methods: %d" % sum(len(v) for v in by_module.values()))
    print("entity/response fields: %d" % parsed_fields)


if __name__ == "__main__":
    main()
