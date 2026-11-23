#!/usr/bin/env python3
"""Generate modular strongly-typed Zan SDK namespaces from a Jushuitan documentation snapshot."""

import argparse
import json
import re
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from urllib.parse import urlsplit


CATEGORIES = {
    "基础": "Basic",
    "商品": "Product",
    "库存": "Inventory",
    "订单": "Order",
    "物流": "Logistics",
    "采购": "Purchase",
    "入库": "Inbound",
    "出库": "Outbound",
    "售后": "AfterSales",
    "其它出入库": "OtherStockMovement",
    "调拨": "Transfer",
    "跨境": "CrossBorder",
    "聚财快": "Jucaikuai",
    "聚工单开放": "WorkOrder",
    "财务": "Finance",
    "拣货批次": "PickingWave",
    "奇门": None,  # The public directory has no entries in this category.
    "票聚": "Invoice",
    "运营云仓": "CloudWarehouse",
    "胜算": "Shengsuan",
    "WMS": "Wms",
}

# Public method stems are keyed by document ID so unrelated documentation edits
# cannot rename an existing SDK entry point. Grouped in official category order.
METHODS = {
    # 基础
    1: "QueryShops",
    2: "QueryLogisticsCompanies",
    3: "QueryWarehouses",
    611: "GetCompanyUsers",
    862: "QueryMySuppliers",
    955: "QueryDistributors",
    # 商品
    486: "UpsertCategories",
    1609: "QueryHistoricalCostPrices",
    1117: "BindSkuLinks",
    1074: "BatchUploadSkus",
    1032: "UpdateProductStyles",
    836: "QuerySkuOperationHistory",
    683: "SetSkuBins",
    600: "UploadHistoricalCostPrices",
    495: "SaveBom",
    4: "UploadSkusLegacy",
    483: "SaveSkuSuppliers",
    449: "QuerySkuSuppliers",
    358: "QuerySkuBom",
    65: "BatchUploadCombinedSkus",
    14: "QuerySkus",
    13: "QueryProductStyles",
    12: "QueryCategories",
    11: "QueryCombinedSkus",
    10: "QueryShopSkus",
    5: "UploadShopSkus",
    # 库存
    15: "QueryInventory",
    16: "QueryInventoryCounts",
    85: "UpdateInventoryCounts",
    631: "BatchUpdateVirtualInventory",
    1125: "QueryPackAndBinInventory",
    1645: "QuerySkuSerialInventory",
    # 订单
    1497: "UndoOrderCancellation",
    1435: "UpdateOrderAddress",
    1396: "UpdateOrderInvoice",
    1367: "ChangeOrderLogisticsCompany",
    944: "UpdateSellerRemarkByOnlineOrderId",
    362: "SetFulfillmentWarehouse",
    18: "UploadOrders",
    263: "MarkOrdersAsExceptions",
    233: "SplitOrders",
    214: "UpdateOrderRemarkFlag",
    196: "UpdateSellerRemarkByInternalOrderId",
    118: "SetOfflineOrderNote",
    72: "UpdateOrderLabels",
    62: "ShipOrders",
    22: "QueryOrders",
    19: "CancelOrderByInternalOrderId",
    # 物流
    23: "BatchRegisterExpressShipments",
    24: "WeighAndShipOrders",
    25: "QueryShipments",
    1395: "QueryExpressRegistrations",
    # 采购
    26: "QueryPurchaseOrders",
    27: "UploadPurchaseOrders",
    28: "UploadSuppliers",
    29: "QuerySuppliers",
    52: "QueryManufacturingOrders",
    55: "UploadManufacturingOrders",
    63: "UpdatePurchaseAndManufacturingLabels",
    92: "UploadInboundAppointments",
    156: "QueryInboundAppointments",
    193: "ChangePurchaseOrderStatus",
    452: "VoidPurchaseManufacturingOrAppointmentOrders",
    698: "EditPurchaseOrders",
    # 入库
    270: "BatchCreatePurchaseReceipts",
    317: "CancelPurchaseReceipts",
    670: "BatchAddSkuSerialNumbers",
    671: "BatchAddPackSerialNumbers",
    901: "QueryPurchaseReceipts",
    1137: "CreateProductionBatches",
    # 出库
    34: "QuerySalesShipments",
    35: "ShipOutboundOrders",
    36: "QueryPurchaseReturns",
    93: "CancelPurchaseReturns",
    269: "BatchCreatePurchaseReturns",
    # 售后
    1443: "SetAfterSalesLabels",
    743: "BatchConfirmReceiptBySkuSerialNumber",
    582: "VoidAfterSalesOrders",
    581: "ConfirmAfterSalesOrders",
    38: "QueryAfterSalesOrders",
    579: "UndoAfterSalesConfirmation",
    272: "ConfirmReturnedGoodsReceipt",
    124: "UploadUnidentifiedReturns",
    41: "UploadAfterSalesOrders",
    40: "QuerySalesReturnReceipts",
    # 其它出入库
    42: "QueryStockMovements",
    43: "CreateStockMovements",
    223: "ChangeStockMovementStatus",
    # 调拨
    47: "QueryTransferOrders",
    48: "UploadInterWarehouseTransfers",
    49: "UploadIntraWarehouseTransfers",
    64: "ConfirmTransferOrders",
    266: "CancelTransferOrders",
    357: "CreateTransferDiscrepancyOrders",
    # 跨境
    1144: "QueryEnabledLogisticsChannels",
    1207: "QueryProductCustomsDeclarations",
    1280: "QueryTransferFirstLegFees",
    1110: "QueryAfterSalesOrders",
    1426: "ReturnPackagesToWarehouse",
    489: "SaveLastMileShippingFees",
    502: "PushSkuMappings1688",
    71: "UpdateOrderLogistics",
    891: "CreateFirstLegOrders",
    1109: "QueryOrders",
    # 聚财快
    1639: "BatchSaveAccountingArchiveData",
    1640: "BatchSaveAccountingDocumentData",
    # 聚工单开放
    1011: "UpdateWorkOrders",
    1127: "AddWorkOrders",
    1130: "QueryWorkOrderTypes",
    # 财务
    1246: "QueryPayments",
    1247: "QueryPayables",
    1320: "QueryLedgerEntries",
    1414: "QueryOperatingFees",
    1437: "QueryExpenseProjects",
    # 拣货批次
    730: "CheckSortingCartAvailability",
    729: "BindSortingBinAndCart",
    467: "UnbindWaveCart",
    456: "ReportSortingWithoutOutbound",
    111: "BindSortingCart",
    232: "UnbindOutboundOrderByCart",
    194: "ReportSortingWithOutbound",
    188: "GetSortingWaves",
    180: "UpdateWavePickingStatus",
    172: "AssignRobotWave",
    160: "RecalculateWaveReservations",
    159: "QueryRobotWaves",
    112: "QueryPickingWaves",
    # 票聚
    1197: "UpdateProductInvoiceInfo",
    1261: "SubmitInvoiceCallback",
    1430: "QueryInvoiceResults",
    1623: "ImportNonOrderInvoiceApplications",
    # 运营云仓
    340: "QueryVirtualWarehouseTransfers",
    341: "QueryVirtualWarehouseAllocationsAndReturns",
    342: "CreateVirtualWarehouseTransfers",
    343: "CreateVirtualWarehouseAllocationsAndReturns",
    490: "QueryVirtualWarehouses",
    625: "QueryVirtualWarehouseInventory",
    799: "UpsertVirtualWarehousePurchaseRequests",
    802: "QueryPurchaseRequests",
    1648: "QueryStockMovementLedger",
    # 胜算
    1377: "UploadMultiCostData",
    1378: "UploadExternalPurchaseOrderCosts",
    1379: "CreateUploadBatchNumber",
    1380: "GetUploadBatchStatus",
    1381: "CancelUploadBatch",
    1382: "QueryUploadBatchErrors",
    1493: "UploadOrderFees",
    1576: "QueryProfitStatementItems",
    1577: "QueryPlatformBillSummaries",
    1583: "QueryStockMovementDetails",
    1584: "QueryPlatformBills",
    1596: "QueryAllocatedPlatformBillItems",
    972: "QueryShopPromotionFees",
    532: "QueryStockMovementDetailsByLevel",
    533: "UpdateStockMovementDetailPushStatus",
    634: "QueryStockMovementSummaries",
    635: "UpdateStockMovementSummaryPushStatus",
    970: "QueryDailyBookkeepingEntries",
    971: "QueryListingPromotionFees",
    973: "QueryRawPlatformBills",
    # WMS
    818: "BatchRemoveSkusFromShelves",
    819: "BatchPutSkusOnShelves",
    986: "AddShippingPack",
    996: "QuerySkuSerialMovements",
    1030: "SavePackingVideoRecord",
    593: "ForceApproveOutOfStockSingleItemOrdersForShipping",
    1319: "QueryFastDeliveryOrderCountV2",
    1503: "QueryRecommendedSkuBins",
    322: "CreateQuickSaleLabels",
    73: "ForceApproveAndReceiveOutOfStockSingleItemOrders",
    76: "QueryWorkLogs",
    154: "AppendWorkload",
    173: "GetQuickSaleSkuSerialInfo",
    252: "QuerySkuSerialTracking",
    54: "QueryWarehouseMovements",
    436: "ReceiveQuickSaleGoods",
    450: "QueryRecommendedSkuBinsByPackNumber",
}

QIMEN_ID = 1047
LIST_ONLY_ID = 800
DEFAULT_GATEWAY = "https://openapi.jushuitan.com"
HANGZHOU_GATEWAY = "https://openapi-hz.jushuitan.com"
BANNER = "// Generated by scripts/generate_catalog.py."

PKG_SRC_ROOT = Path(__file__).resolve().parents[1] / "src" / "Sdk" / "Jushuitan"
API_OUT = PKG_SRC_ROOT / "Api"

CATEGORY_ROW = re.compile(
    r"^\| \[([^\]]+)\]\(#api-group-(\d+)\) \| (\d+) \| "
    r"\[打开分类\]\(api/([^/]+)/README\.md\) \|$", re.MULTILINE
)
ENTRY_ROW = re.compile(
    r"^\| `([^`]+)` \| \[([^\]]+)\]\((?:api/([^/]+)/)?(\d+)\.md\) \| "
    r"\[官方\]\(https://openweb\.jushuitan\.com/dev-doc\?docType=(\d+)&docId=(\d+)\) \|$",
    re.MULTILINE,
)
PRODUCTION_ROW = re.compile(r"^\| 正式环境 \| `([^`]+)` \|$", re.MULTILINE)

ZAN_KEYWORDS = {
    "abstract", "as", "async", "await", "base", "bool", "break", "byte", "case", "catch",
    "char", "class", "const", "continue", "decimal", "default", "delegate", "do", "double",
    "else", "enum", "event", "explicit", "extern", "false", "finally", "fixed", "float",
    "for", "foreach", "goto", "if", "implicit", "in", "int", "interface", "internal", "is",
    "lock", "long", "namespace", "new", "null", "object", "operator", "out", "override",
    "params", "private", "protected", "public", "readonly", "ref", "return", "sbyte",
    "sealed", "short", "sizeof", "stackalloc", "static", "string", "struct", "switch",
    "this", "throw", "true", "try", "typeof", "uint", "ulong", "unchecked", "unsafe",
    "ushort", "using", "var", "virtual", "void", "volatile", "while", "nint", "nuint",
}


@dataclass(frozen=True)
class Entry:
    document_id: int
    doc_type: int
    title: str
    path: str


@dataclass
class SchemaNode:
    json_key: str
    kind: str  # object, array, string, integer, number, boolean, null
    required: bool
    children: dict = field(default_factory=dict)
    item_kind: str = ""
    item_children: dict = field(default_factory=dict)
    is_dynamic_dict: bool = False


def read_text(path):
    return path.read_text(encoding="utf-8")


def read_entries(text, source):
    for path, title, category, local_id, doc_type, official_id in ENTRY_ROW.findall(text):
        if local_id != official_id:
            raise ValueError(f"{source}: local ID {local_id} differs from official ID {official_id}")
        yield category or None, Entry(int(local_id), int(doc_type), title, path)


def read_catalog(docs):
    index_path = docs / "README.md"
    _, start, remaining = read_text(index_path).partition("\n## 业务 API\n")
    section, end, _ = remaining.partition("\n## 消息模板\n")
    if not (start and end):
        raise ValueError(f"{index_path}: business API section is missing")

    categories = CATEGORY_ROW.findall(section)
    if len(categories) != len(CATEGORIES) or {c[3] for c in categories} != set(CATEGORIES):
        raise ValueError(f"{index_path}: category mapping differs from CATEGORIES")

    by_category = defaultdict(list)
    ids = set()
    paths = set()
    for category, entry in read_entries(section, index_path):
        if category not in CATEGORIES or entry.document_id in ids or entry.path in paths:
            raise ValueError(f"{index_path}: unknown category or duplicate entry {entry}")
        by_category[category].append(entry)
        ids.add(entry.document_id)
        paths.add(entry.path)

    expected_ids = set(METHODS) | {QIMEN_ID}
    if ids != expected_ids or LIST_ONLY_ID in ids:
        raise ValueError(
            f"{index_path}: missing IDs {sorted(expected_ids - ids)}, "
            f"unexpected IDs {sorted(ids - expected_ids)}"
        )

    for title, doc_type, count, category in categories:
        category_path = docs / "api" / category / "README.md"
        category_index = read_text(category_path)
        entries = by_category[category]
        if len(entries) != int(count) or not category_index.startswith(f"# {title}\n"):
            raise ValueError(f"{category_path}: heading or entry count differs from the main index")
        if not CATEGORIES[category] and entries:
            raise ValueError(f"{category_path}: the category now needs a generated API class")
        if list(read_entries(category_index, category_path)) != [(None, entry) for entry in entries]:
            raise ValueError(f"{category_path}: entries differ from the main index")
        if any(entry.doc_type != int(doc_type) for entry in entries):
            raise ValueError(f"{category_path}: docType differs from the category index")
        yield category, title, entries


def read_detail(file, category_title, entry):
    text = read_text(file)
    expected = f"`{entry.path}` · {category_title} · 文档 ID `{entry.document_id}`"
    if expected not in text.splitlines():
        raise ValueError(f"{file}: path, category, or document ID differs from the index")
    if entry.document_id != QIMEN_ID and not text.startswith(f"# {entry.title}\n"):
        raise ValueError(f"{file}: title differs from the index")
    return text


def production_url(text, source):
    matches = PRODUCTION_ROW.findall(text)
    if len(matches) != 1:
        raise ValueError(f"{source}: expected one official production URL")
    return matches[0]


def regular_gateway(file, detail, entry):
    url = production_url(detail, file)
    parts = urlsplit(url)
    origin = f"{parts.scheme}://{parts.netloc}"
    expected_origin = HANGZHOU_GATEWAY if entry.document_id == 955 else DEFAULT_GATEWAY
    if url != origin + entry.path or not entry.path.startswith("/open/") or origin != expected_origin:
        raise ValueError(f"{file}: unexpected official production URL {url}")
    return origin


def zan_string(value):
    return json.dumps(value.replace(r"\_", "_"), ensure_ascii=False)


def normalize_type(raw_type):
    t = raw_type.lower().strip()
    if "boolean" in t:
        return "boolean"
    if "integer" in t:
        return "integer"
    if "number" in t:
        return "number"
    if "array" in t:
        return "array"
    if "object" in t:
        return "object"
    if "string" in t:
        return "string"
    return "null"


def clean_segment(seg):
    # Remove literal \t and surrounding whitespace from dirty official doc tables.
    s = seg.replace("\\t", "").replace("\t", "").strip()
    # Doc typo in 1261.md: table lists "xml)url", official JSON example uses "xml_url".
    if s == "xml)url":
        s = "xml_url"
    return s


def is_valid_ident(name):
    return bool(re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", name))


def parse_path_tokens(raw_path):
    # Convert $["foo"] into .foo and parse segment tokens.
    p = raw_path.strip()
    p = re.sub(r'\["([^"]*)"\]', lambda m: "." + clean_segment(m.group(1)), p)
    if p == "$":
        return [("$", False)]
    if p.startswith("$."):
        p = p[2:]
    elif p.startswith("$[]"):
        p = "$[]" + p[3:]
    tokens = []
    for part in p.split("."):
        if not part:
            continue
        is_arr = part.endswith("[]")
        name = part[:-2] if is_arr else part
        name = clean_segment(name) if name != "$" else "$"
        tokens.append((name, is_arr))
    return tokens


def parse_schema_table(detail_text, heading):
    m = re.search(rf"## {re.escape(heading)}\n\n(.*?)(?=\n## |\Z)", detail_text, re.S)
    if not m:
        return []
    rows = []
    for line in m.group(1).splitlines():
        if not line.startswith("|"):
            continue
        parts = [c.strip() for c in line.strip("|").split("|")]
        if len(parts) < 3 or parts[0] in ("字段路径", "---"):
            continue
        raw_path = parts[0].strip("`")
        raw_type = parts[1].strip("`")
        req_flag = parts[2].strip() == "是"
        rows.append((raw_path, normalize_type(raw_type), req_flag))
    return rows


def build_schema_tree(rows):
    root = SchemaNode(json_key="$", kind="object", required=True)
    for raw_path, kind, req in rows:
        tokens = parse_path_tokens(raw_path)
        if not tokens:
            continue
        if tokens == [("$", False)]:
            root.kind = kind
            continue
        if tokens[0] == ("$", True):
            root.kind = "array"
            if len(tokens) == 1:
                root.item_kind = kind
                continue
            cur = root
            cur_is_item = True
            rest = tokens[1:]
        else:
            cur = root
            cur_is_item = False
            rest = tokens

        for idx, (seg_name, is_arr) in enumerate(rest):
            is_last = idx == len(rest) - 1
            if not is_valid_ident(seg_name):
                # Dynamic example dictionary key (e.g. "111", "年份") -> mark parent dynamic JsonValue.
                cur.is_dynamic_dict = True
                break

            target_map = cur.item_children if cur_is_item else cur.children
            node = target_map.get(seg_name)
            if node is None:
                node_kind = kind if (is_last and not is_arr) else ("array" if is_arr else "object")
                node = SchemaNode(json_key=seg_name, kind=node_kind, required=req if is_last else False)
                target_map[seg_name] = node
            if is_last:
                if is_arr:
                    node.item_kind = kind
                else:
                    node.kind = kind
                    node.required = node.required or req
            else:
                if is_arr:
                    node.kind = "array"
                    if not node.item_kind:
                        node.item_kind = "object"
                elif node.kind not in ("object", "array"):
                    node.kind = "object"
            cur = node
            cur_is_item = is_arr
    return root


def promote_array_nodes_from_example(resp_tree, detail_text):
    m = re.search(r"## 响应示例\n\n```json\n(.*?)\n```", detail_text, re.S)
    if not m:
        return
    try:
        ex = json.loads(m.group(1))
    except Exception:
        return
    if not isinstance(ex, dict):
        return
    ex_root = ex
    if "data" in ex and isinstance(ex["data"], dict) and "data" not in resp_tree.children:
        ex_root = ex["data"]

    def walk(node, val):
        if node.kind == "object" and isinstance(val, list) and len(node.children) > 0:
            node.kind = "array"
            node.item_kind = "object"
            node.item_children = node.children
            node.children = {}
            if val and isinstance(val[0], dict):
                for k, child in node.item_children.items():
                    if k in val[0]:
                        walk(child, val[0][k])
        elif node.kind == "object" and isinstance(val, dict):
            for k, child in node.children.items():
                if k in val:
                    walk(child, val[k])
        elif node.kind == "array" and isinstance(val, list) and val and isinstance(val[0], dict):
            for k, child in node.item_children.items():
                if k in val[0]:
                    walk(child, val[0][k])

    for k, child in resp_tree.children.items():
        if k in ex_root:
            walk(child, ex_root[k])


def to_pascal(name):
    parts = [p for p in re.split(r"[_\-]+", name) if p]
    if not parts:
        return "Value"
    out = []
    for p in parts:
        out.append(p[0].upper() + p[1:])
    res = "".join(out)
    if res[0].isdigit():
        res = "Item" + res
    return res


def safe_dto_field(json_key, used_names, reserved=()):
    candidate = json_key
    if not is_valid_ident(candidate) or candidate in ZAN_KEYWORDS or candidate in reserved:
        candidate = "field_" + re.sub(r"[^A-Za-z0-9_]", "_", candidate)
    base = candidate
    suffix = 2
    while candidate in used_names:
        candidate = f"{base}_{suffix}"
        suffix += 1
    used_names.add(candidate)
    return candidate


def safe_setter_name(json_key, used_names, reserved=()):
    candidate = to_pascal(json_key)
    if candidate in reserved or candidate in ZAN_KEYWORDS:
        candidate = "Param" + candidate
    base = candidate
    suffix = 2
    while candidate in used_names:
        candidate = f"{base}{suffix}"
        suffix += 1
    used_names.add(candidate)
    return candidate


def scalar_zan_type(kind):
    if kind == "boolean":
        return "bool"
    if kind == "integer":
        return "long"
    if kind == "number":
        return "double"
    if kind == "string":
        return "string"
    return "JsonValue"


def has_object_fields(node):
    return node.kind == "object" and not node.is_dynamic_dict and len(node.children) > 0


def has_array_item_fields(node):
    return node.kind == "array" and not node.is_dynamic_dict and node.item_kind == "object" and len(node.item_children) > 0


def alloc_class_name(prefix, suffix, used_classes):
    base = prefix + suffix
    name = base
    idx = 2
    while name in used_classes:
        name = f"{base}{idx}"
        idx += 1
    used_classes.add(name)
    return name


def render_request_file(module_ns, stem, req_tree):
    lines = [
        BANNER,
        "",
        "using System;",
        "using System.Json;",
        "using Sdk.Jushuitan;",
        "",
        f"namespace {module_ns};",
        "",
    ]

    used_classes = {f"{stem}Request"}
    helper_blocks = []

    def emit_param_class(class_name, children_map):
        cls_lines = [f"public class {class_name} {{", "    private JsonValue obj = JsonValue.NewObject();", ""]
        used_methods = {"ToJsonValue", "SetRaw", "Validate"}
        required_keys = []
        for key, child in children_map.items():
            setter = safe_setter_name(key, used_methods, reserved=("ToJsonValue", "SetRaw", "Validate"))
            if child.required:
                required_keys.append(key)
            if child.kind == "string":
                cls_lines += [
                    f"    public {class_name} {setter}(string value) {{",
                    f"        if (value != null) {{ this.obj.Set({zan_string(key)}, JsonValue.NewStr(value)); }}",
                    "        return this;",
                    "    }",
                    "",
                ]
            elif child.kind == "integer":
                cls_lines += [
                    f"    public {class_name} {setter}(long value) {{",
                    f"        this.obj.Set({zan_string(key)}, JsonValue.NewNum(Convert.ToString(value)));",
                    "        return this;",
                    "    }",
                    "",
                ]
            elif child.kind == "number":
                cls_lines += [
                    f"    public {class_name} {setter}(double value) {{",
                    f"        this.obj.Set({zan_string(key)}, JsonValue.NewNum(Convert.ToString(value)));",
                    "        return this;",
                    "    }",
                    "",
                ]
            elif child.kind == "boolean":
                cls_lines += [
                    f"    public {class_name} {setter}(bool value) {{",
                    f"        this.obj.Set({zan_string(key)}, JsonValue.NewBool(value));",
                    "        return this;",
                    "    }",
                    "",
                ]
            elif has_object_fields(child):
                sub_cls = alloc_class_name(class_name, to_pascal(key) + "Param", used_classes)
                emit_param_class(sub_cls, child.children)
                cls_lines += [
                    f"    public {class_name} {setter}({sub_cls} value) {{",
                    f"        if (value != null) {{ value.Validate(); this.obj.Set({zan_string(key)}, value.ToJsonValue()); }}",
                    "        return this;",
                    "    }",
                    "",
                ]
            elif has_array_item_fields(child):
                item_cls = alloc_class_name(class_name, to_pascal(key) + "ItemParam", used_classes)
                emit_param_class(item_cls, child.item_children)
                cls_lines += [
                    f"    public {class_name} {setter}(List<{item_cls}> items) {{",
                    "        if (items != null) {",
                    "            JsonValue arr = JsonValue.NewArray();",
                    "            for (int i = 0; i < items.Count; i = i + 1) {",
                    "                if (items[i] != null) { items[i].Validate(); arr.Append(items[i].ToJsonValue()); }",
                    "            }",
                    f"            this.obj.Set({zan_string(key)}, arr);",
                    "        }",
                    "        return this;",
                    "    }",
                    "",
                ]
            elif child.kind == "array":
                elem_kind = child.item_kind
                if elem_kind == "string":
                    cls_lines += [
                        f"    public {class_name} {setter}(List<string> values) {{",
                        "        if (values != null) {",
                        "            JsonValue arr = JsonValue.NewArray();",
                        "            for (int i = 0; i < values.Count; i = i + 1) {",
                        "                arr.Append(values[i] == null ? JsonValue.NewNull() : JsonValue.NewStr(values[i]));",
                        "            }",
                        f"            this.obj.Set({zan_string(key)}, arr);",
                        "        }",
                        "        return this;",
                        "    }",
                        "",
                    ]
                elif elem_kind in ("integer", "number"):
                    z_t = "long" if elem_kind == "integer" else "double"
                    cls_lines += [
                        f"    public {class_name} {setter}(List<{z_t}> values) {{",
                        "        if (values != null) {",
                        "            JsonValue arr = JsonValue.NewArray();",
                        "            for (int i = 0; i < values.Count; i = i + 1) {",
                        "                arr.Append(JsonValue.NewNum(Convert.ToString(values[i])));",
                        "            }",
                        f"            this.obj.Set({zan_string(key)}, arr);",
                        "        }",
                        "        return this;",
                        "    }",
                        "",
                    ]
                else:
                    cls_lines += [
                        f"    public {class_name} {setter}(JsonValue values) {{",
                        f"        if (values != null) {{ this.obj.Set({zan_string(key)}, values); }}",
                        "        return this;",
                        "    }",
                        "",
                    ]
            else:
                cls_lines += [
                    f"    public {class_name} {setter}(JsonValue value) {{",
                    f"        if (value != null) {{ this.obj.Set({zan_string(key)}, value); }}",
                    "        return this;",
                    "    }",
                    "",
                ]

        cls_lines += [
            f"    public {class_name} SetRaw(string key, JsonValue value) {{",
            "        if (key != null && key.Length > 0 && value != null) { this.obj.Set(key, value); }",
            "        return this;",
            "    }",
            "",
            "    public void Validate() {",
        ]
        for rk in required_keys:
            cls_lines += [
                f"        if (!this.obj.Has({zan_string(rk)})) {{",
                f"            throw new JushuitanException(\"client.missingRequiredParam\", \"缺少必填参数: {rk}\", 0, \"\");",
                "        }",
            ]
        cls_lines += [
            "    }",
            "",
            "    public JsonValue ToJsonValue() { return this.obj; }",
            "}",
            "",
        ]
        helper_blocks.extend(cls_lines)

    req_cls = f"{stem}Request"
    req_lines = [f"public class {req_cls} {{"]

    if req_tree.kind == "array":
        req_lines += [
            "    private JsonValue body = JsonValue.NewArray();",
            "",
        ]
        if req_tree.item_kind == "object" and len(req_tree.item_children) > 0:
            item_cls = alloc_class_name(stem, "ItemParam", used_classes)
            emit_param_class(item_cls, req_tree.item_children)
            req_lines += [
                f"    public {req_cls} AddItem({item_cls} item) {{",
                "        if (item != null) { item.Validate(); this.body.Append(item.ToJsonValue()); }",
                "        return this;",
                "    }",
                "",
                f"    public {req_cls} Items(List<{item_cls}> items) {{",
                "        if (items != null) {",
                "            for (int i = 0; i < items.Count; i = i + 1) { this.AddItem(items[i]); }",
                "        }",
                "        return this;",
                "    }",
                "",
            ]
        elif req_tree.item_kind == "string":
            req_lines += [
                f"    public {req_cls} AddItem(string item) {{",
                "        this.body.Append(item == null ? JsonValue.NewNull() : JsonValue.NewStr(item));",
                "        return this;",
                "    }",
                "",
            ]
        elif req_tree.item_kind == "integer":
            req_lines += [
                f"    public {req_cls} AddItem(long item) {{",
                "        this.body.Append(JsonValue.NewNum(Convert.ToString(item)));",
                "        return this;",
                "    }",
                "",
            ]
        req_lines += [
            f"    public {req_cls} AddRawItem(JsonValue item) {{",
            "        if (item != null) { this.body.Append(item); }",
            "        return this;",
            "    }",
            "",
            "    public void Validate() {}",
            "    public JsonValue ToJsonValue() { return this.body; }",
            "    public string ToJson() { return this.body.ToJson(); }",
            "}",
            "",
        ]
    else:
        req_lines += [
            "    private JsonValue body = JsonValue.NewObject();",
            "",
        ]
        used_methods = {"ToJsonValue", "ToJson", "Validate", "SetRaw"}
        required_keys = []
        for key, child in req_tree.children.items():
            setter = safe_setter_name(key, used_methods, reserved=("ToJsonValue", "ToJson", "Validate", "SetRaw"))
            if child.required:
                required_keys.append(key)
            if child.kind == "string":
                req_lines += [
                    f"    public {req_cls} {setter}(string value) {{",
                    f"        if (value != null) {{ this.body.Set({zan_string(key)}, JsonValue.NewStr(value)); }}",
                    "        return this;",
                    "    }",
                    "",
                ]
            elif child.kind == "integer":
                req_lines += [
                    f"    public {req_cls} {setter}(long value) {{",
                    f"        this.body.Set({zan_string(key)}, JsonValue.NewNum(Convert.ToString(value)));",
                    "        return this;",
                    "    }",
                    "",
                ]
            elif child.kind == "number":
                req_lines += [
                    f"    public {req_cls} {setter}(double value) {{",
                    f"        this.body.Set({zan_string(key)}, JsonValue.NewNum(Convert.ToString(value)));",
                    "        return this;",
                    "    }",
                    "",
                ]
            elif child.kind == "boolean":
                req_lines += [
                    f"    public {req_cls} {setter}(bool value) {{",
                    f"        this.body.Set({zan_string(key)}, JsonValue.NewBool(value));",
                    "        return this;",
                    "    }",
                    "",
                ]
            elif has_object_fields(child):
                sub_cls = alloc_class_name(stem, to_pascal(key) + "Param", used_classes)
                emit_param_class(sub_cls, child.children)
                req_lines += [
                    f"    public {req_cls} {setter}({sub_cls} value) {{",
                    f"        if (value != null) {{ value.Validate(); this.body.Set({zan_string(key)}, value.ToJsonValue()); }}",
                    "        return this;",
                    "    }",
                    "",
                ]
            elif has_array_item_fields(child):
                item_cls = alloc_class_name(stem, to_pascal(key) + "ItemParam", used_classes)
                emit_param_class(item_cls, child.item_children)
                req_lines += [
                    f"    public {req_cls} {setter}(List<{item_cls}> items) {{",
                    "        if (items != null) {",
                    "            JsonValue arr = JsonValue.NewArray();",
                    "            for (int i = 0; i < items.Count; i = i + 1) {",
                    "                if (items[i] != null) { items[i].Validate(); arr.Append(items[i].ToJsonValue()); }",
                    "            }",
                    f"            this.body.Set({zan_string(key)}, arr);",
                    "        }",
                    "        return this;",
                    "    }",
                    "",
                ]
            elif child.kind == "array":
                elem_kind = child.item_kind
                if elem_kind == "string":
                    req_lines += [
                        f"    public {req_cls} {setter}(List<string> values) {{",
                        "        if (values != null) {",
                        "            JsonValue arr = JsonValue.NewArray();",
                        "            for (int i = 0; i < values.Count; i = i + 1) {",
                        "                arr.Append(values[i] == null ? JsonValue.NewNull() : JsonValue.NewStr(values[i]));",
                        "            }",
                        f"            this.body.Set({zan_string(key)}, arr);",
                        "        }",
                        "        return this;",
                        "    }",
                        "",
                    ]
                elif elem_kind in ("integer", "number"):
                    z_t = "long" if elem_kind == "integer" else "double"
                    req_lines += [
                        f"    public {req_cls} {setter}(List<{z_t}> values) {{",
                        "        if (values != null) {",
                        "            JsonValue arr = JsonValue.NewArray();",
                        "            for (int i = 0; i < values.Count; i = i + 1) {",
                        "                arr.Append(JsonValue.NewNum(Convert.ToString(values[i])));",
                        "            }",
                        f"            this.body.Set({zan_string(key)}, arr);",
                        "        }",
                        "        return this;",
                        "    }",
                        "",
                    ]
                else:
                    req_lines += [
                        f"    public {req_cls} {setter}(JsonValue values) {{",
                        f"        if (values != null) {{ this.body.Set({zan_string(key)}, values); }}",
                        "        return this;",
                        "    }",
                        "",
                    ]
            else:
                req_lines += [
                    f"    public {req_cls} {setter}(JsonValue value) {{",
                    f"        if (value != null) {{ this.body.Set({zan_string(key)}, value); }}",
                    "        return this;",
                    "    }",
                    "",
                ]

        req_lines += [
            f"    public {req_cls} SetRaw(string key, JsonValue value) {{",
            "        if (key != null && key.Length > 0 && value != null) { this.body.Set(key, value); }",
            "        return this;",
            "    }",
            "",
            "    public void Validate() {",
        ]
        for rk in required_keys:
            req_lines += [
                f"        if (!this.body.Has({zan_string(rk)})) {{",
                f"            throw new JushuitanException(\"client.missingRequiredParam\", \"缺少必填参数: {rk}\", 0, \"\");",
                "        }",
            ]
        req_lines += [
            "    }",
            "",
            "    public JsonValue ToJsonValue() { return this.body; }",
            "    public string ToJson() { return this.body.ToJson(); }",
            "}",
            "",
        ]

    return "\n".join(lines + req_lines + helper_blocks)


def determine_response_mode(resp_tree):
    top = resp_tree.children
    if "code" not in top or "data" not in top:
        return "RootEnvelope"
    return "DataEnvelope"


def render_response_file(module_ns, stem, resp_tree):
    mode = determine_response_mode(resp_tree)
    resp_cls = f"{stem}Response"
    used_classes = {resp_cls}
    nested_blocks = []

    def emit_dto_fields(cls_prefix, children_map, reserved=()):
        field_lines = []
        used_fields = set()
        for key, child in children_map.items():
            fname = safe_dto_field(key, used_fields, reserved=reserved)
            if fname != key:
                field_lines.append(f"    [JsonProperty({zan_string(key)})]")
            if child.kind in ("boolean", "integer", "number", "string"):
                z_type = scalar_zan_type(child.kind)
            elif has_object_fields(child):
                sub_cls = alloc_class_name(cls_prefix, to_pascal(key), used_classes)
                emit_dto_class(sub_cls, child.children)
                z_type = sub_cls
            elif has_array_item_fields(child):
                item_cls = alloc_class_name(cls_prefix, to_pascal(key) + "Item", used_classes)
                emit_dto_class(item_cls, child.item_children)
                z_type = f"List<{item_cls}>"
            elif child.kind == "array":
                if child.item_kind in ("boolean", "integer", "number", "string"):
                    z_type = f"List<{scalar_zan_type(child.item_kind)}>"
                else:
                    z_type = "List<JsonValue>"
            else:
                z_type = "JsonValue"
            field_lines.append(f"    public {z_type} {fname};")
        return field_lines

    def emit_dto_class(cls_name, children_map):
        body = emit_dto_fields(cls_name, children_map)
        nested_blocks.append(f"public class {cls_name} {{")
        nested_blocks.extend(body)
        nested_blocks.append("}")
        nested_blocks.append("")

    body_lines = []
    bind_lines = []

    if mode == "RootEnvelope":
        body_lines = emit_dto_fields(stem, resp_tree.children, reserved=("Raw", "Bind"))
        bind_lines = [
            f"    public static {resp_cls} Bind(JushuitanResponse raw) {{",
            f"        {resp_cls} result = (raw != null && raw.Data != null && raw.Data.IsObject())",
            f"            ? Json.Deserialize<{resp_cls}>(raw.DataJson)",
            f"            : new {resp_cls}();",
            "        result.Raw = raw;",
            "        return result;",
            "    }",
        ]
    else:
        data_node = resp_tree.children["data"]
        if has_object_fields(data_node):
            body_lines = emit_dto_fields(stem, data_node.children, reserved=("Raw", "Bind"))
            bind_lines = [
                f"    public static {resp_cls} Bind(JushuitanResponse raw) {{",
                f"        {resp_cls} result = (raw != null && raw.Data != null && raw.Data.IsObject())",
                f"            ? Json.Deserialize<{resp_cls}>(raw.DataJson)",
                f"            : new {resp_cls}();",
                "        result.Raw = raw;",
                "        return result;",
                "    }",
            ]
        elif has_array_item_fields(data_node):
            item_cls = alloc_class_name(stem, "DataItem", used_classes)
            emit_dto_class(item_cls, data_node.item_children)
            body_lines = [f"    public List<{item_cls}> Items;"]
            bind_lines = [
                f"    public static {resp_cls} Bind(JushuitanResponse raw) {{",
                f"        {resp_cls} result = new {resp_cls}();",
                "        result.Items = (raw != null && raw.Data != null && raw.Data.IsArray())",
                f"            ? Json.Deserialize<List<{item_cls}>>(raw.DataJson)",
                f"            : new List<{item_cls}>();",
                "        result.Raw = raw;",
                "        return result;",
                "    }",
            ]
        elif data_node.kind == "array":
            elem_t = scalar_zan_type(data_node.item_kind) if data_node.item_kind in ("boolean", "integer", "number", "string") else "JsonValue"
            body_lines = [f"    public List<{elem_t}> Items;"]
            bind_lines = [
                f"    public static {resp_cls} Bind(JushuitanResponse raw) {{",
                f"        {resp_cls} result = new {resp_cls}();",
                "        result.Items = (raw != null && raw.Data != null && raw.Data.IsArray())",
                f"            ? Json.Deserialize<List<{elem_t}>>(raw.DataJson)",
                f"            : new List<{elem_t}>();",
                "        result.Raw = raw;",
                "        return result;",
                "    }",
            ]
        elif data_node.kind == "boolean":
            body_lines = ["    public bool Result;"]
            bind_lines = [
                f"    public static {resp_cls} Bind(JushuitanResponse raw) {{",
                f"        {resp_cls} result = new {resp_cls}();",
                "        result.Result = raw != null && raw.Data != null ? raw.Data.AsBool(false) : false;",
                "        result.Raw = raw;",
                "        return result;",
                "    }",
            ]
        elif data_node.kind in ("string", "integer", "number"):
            z_t = scalar_zan_type(data_node.kind)
            if data_node.kind == "string":
                expr = 'raw != null && raw.Data != null && !raw.Data.IsNull() ? raw.Data.AsString("") : ""'
            elif data_node.kind == "integer":
                expr = "raw != null && raw.Data != null ? raw.Data.AsLong(0L) : 0L"
            else:
                expr = "raw != null && raw.Data != null ? raw.Data.AsDouble(0.0) : 0.0"
            body_lines = [f"    public {z_t} Value;"]
            bind_lines = [
                f"    public static {resp_cls} Bind(JushuitanResponse raw) {{",
                f"        {resp_cls} result = new {resp_cls}();",
                f"        result.Value = {expr};",
                "        result.Raw = raw;",
                "        return result;",
                "    }",
            ]
        else:
            body_lines = ["    public JsonValue Data;"]
            bind_lines = [
                f"    public static {resp_cls} Bind(JushuitanResponse raw) {{",
                f"        {resp_cls} result = new {resp_cls}();",
                "        result.Data = raw != null ? raw.Data : JsonValue.NewNull();",
                "        result.Raw = raw;",
                "        return result;",
                "    }",
            ]

    lines = [
        BANNER,
        "",
        "using System;",
        "using System.Json;",
        "using Sdk.Jushuitan;",
        "",
        f"namespace {module_ns};",
        "",
        f"public class {resp_cls} {{",
        *body_lines,
        "    public JushuitanResponse Raw;",
        "",
        *bind_lines,
        "}",
        "",
        *nested_blocks,
    ]
    return "\n".join(lines)


def render_module_api(module_name, title, endpoints_info):
    class_name = f"{module_name}Api"
    module_ns = f"Sdk.Jushuitan.Api.{module_name}"
    lines = [
        BANNER,
        "",
        "using System;",
        "using Sdk.Jushuitan;",
        "",
        f"namespace {module_ns};",
        "",
        f"// {title}",
        f"public class {class_name} {{",
        "    private JushuitanClient client;",
        "",
        f"    public {class_name}(JushuitanClient client) {{",
        "        if (client == null) {",
        "            throw new JushuitanException(\"client.invalidArguments\", \"client 不能为空\", 0, \"\");",
        "        }",
        "        this.client = client;",
        "    }",
        "",
    ]

    for entry, gateway, resp_mode in endpoints_info:
        stem = METHODS[entry.document_id]
        req_cls = f"{stem}Request"
        resp_cls = f"{stem}Response"
        is_default = gateway == DEFAULT_GATEWAY and resp_mode == "DataEnvelope"
        lines.append(f"    // {entry.title}（文档 {entry.document_id}）")
        if is_default:
            lines += [
                f"    public async {resp_cls} {stem}Async({req_cls} request) {{",
                "        if (request == null) { throw new JushuitanException(\"client.invalidArguments\", \"request 不能为空\", 0, \"\"); }",
                "        request.Validate();",
                f"        JushuitanResponse raw = await this.client.CallAsync({zan_string(entry.path)}, request.ToJsonValue());",
                f"        return {resp_cls}.Bind(raw);",
                "    }",
                "",
                f"    public async {resp_cls} {stem}Async(string accessToken, {req_cls} request) {{",
                "        if (request == null) { throw new JushuitanException(\"client.invalidArguments\", \"request 不能为空\", 0, \"\"); }",
                "        request.Validate();",
                f"        JushuitanResponse raw = await this.client.CallAsync({zan_string(entry.path)}, request.ToJsonValue(), accessToken);",
                f"        return {resp_cls}.Bind(raw);",
                "    }",
                "",
            ]
        else:
            lines += [
                f"    public async {resp_cls} {stem}Async({req_cls} request) {{",
                "        if (request == null) { throw new JushuitanException(\"client.invalidArguments\", \"request 不能为空\", 0, \"\"); }",
                "        request.Validate();",
                f"        JushuitanResponse raw = await this.client.CallAsync(",
                f"            {zan_string(gateway)}, {zan_string(entry.path)}, request.ToJsonValue(),",
                f"            this.client.AccessToken, JushuitanResponseMode.{resp_mode});",
                f"        return {resp_cls}.Bind(raw);",
                "    }",
                "",
                f"    public async {resp_cls} {stem}Async(string accessToken, {req_cls} request) {{",
                "        if (request == null) { throw new JushuitanException(\"client.invalidArguments\", \"request 不能为空\", 0, \"\"); }",
                "        request.Validate();",
                f"        JushuitanResponse raw = await this.client.CallAsync(",
                f"            {zan_string(gateway)}, {zan_string(entry.path)}, request.ToJsonValue(),",
                f"            accessToken, JushuitanResponseMode.{resp_mode});",
                f"        return {resp_cls}.Bind(raw);",
                "    }",
                "",
            ]

    lines += ["}", ""]
    return "\n".join(lines)


def generate_files(docs):
    files = {}
    for category, title, entries in read_catalog(docs):
        module_name = CATEGORIES[category]
        if not module_name:
            continue
        module_ns = f"Sdk.Jushuitan.Api.{module_name}"
        endpoints_info = []
        for entry in entries:
            if entry.document_id == QIMEN_ID:
                continue
            detail_path = docs / "api" / category / f"{entry.document_id}.md"
            detail = read_detail(detail_path, title, entry)
            gateway = regular_gateway(detail_path, detail, entry)
            req_rows = parse_schema_table(detail, "业务请求参数")
            resp_rows = parse_schema_table(detail, "返回参数")
            req_tree = build_schema_tree(req_rows)
            resp_tree = build_schema_tree(resp_rows)
            promote_array_nodes_from_example(resp_tree, detail)
            resp_mode = determine_response_mode(resp_tree)
            stem = METHODS[entry.document_id]
            endpoints_info.append((entry, gateway, resp_mode))

            req_file = API_OUT / module_name / "Requests" / f"{stem}Request.zan"
            resp_file = API_OUT / module_name / "Models" / f"{stem}Response.zan"
            files[req_file] = render_request_file(module_ns, stem, req_tree)
            files[resp_file] = render_response_file(module_ns, stem, resp_tree)

        api_file = API_OUT / module_name / f"{module_name}Api.zan"
        files[api_file] = render_module_api(module_name, title, endpoints_info)

    return files


def sync_files(expected, check_only):
    existing = set(API_OUT.rglob("*.zan")) if API_OUT.exists() else set()

    expected_paths = set(expected)
    if check_only:
        if existing != expected_paths:
            extra = sorted(str(p) for p in existing - expected_paths)
            missing = sorted(str(p) for p in expected_paths - existing)
            raise SystemExit(f"Generated file set mismatch: extra={extra}, missing={missing}")
        for path, content in expected.items():
            if read_text(path) != content:
                raise SystemExit(f"Out of date generated file: {path}")
        return

    for stale in existing - expected_paths:
        stale.unlink()
    for path, content in expected.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("docs", type=Path, help="Path to docs/jushuitan")
    parser.add_argument("--check", action="store_true", help="Verify generated files without modifying them")
    args = parser.parse_args()
    expected = generate_files(args.docs)
    sync_files(expected, args.check)
    print(f"OK: {len(expected)} generated files across {len([c for c in CATEGORIES.values() if c])} modules")


if __name__ == "__main__":
    main()
