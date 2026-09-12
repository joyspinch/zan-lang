"""把 legend 客户端 data/*.csv 与 data/reference/*.csv（data.dll 无损导出）
全量同步进服务端 M2.DB，使数据库成为唯一游戏数据源。

架构口径：服务端拥有数据，客户端只做显示。玩法表按 CSV 文件名建表
（items/monsters/maps/skills/...）；原版原表用 Game* 前缀（GameItem/
GameMap/GameMonster/GameBoss/GameSkill/GameSc/GameTitle/GameCity/
GameAchieve/GameFw/GameJue/GameShuxing/GameTask），列名与取值逐字保留，
空串存 NULL。GameMeta 记录来源与行数，GameSource 记录 data.dll SHA-256。

幂等：每次运行先 DROP 再 CREATE。所有列 TEXT——数值解析留给消费方
（Zan 服务端种子/协议层），本工具只负责无损搬运。

数据链：data.dll --import_reference.py--> data/reference/*.csv
        --本工具--> M2.DB。reference/ 已入库后从客户端模板删除；
        需要重同步时先用 import_reference.py 重新导出。

用法（Python 3，标准库）：
    python sync_csv_to_db.py                       # 默认路径，仓库内
    python sync_csv_to_db.py --db PATH --data-dir DIR --reference-dir DIR
"""

import argparse
import csv
import json
import os
import sqlite3
import sys
from datetime import datetime, timezone

# data.dll 原表 → M2.DB 表名（Game* 前缀避免与经典 M2 表 Monster/StdItems 撞名）
GAME_TABLES = {
    "item": "GameItem", "map": "GameMap", "monster": "GameMonster",
    "boss": "GameBoss", "skill": "GameSkill", "sc": "GameSc",
    "title": "GameTitle", "city": "GameCity", "achieve": "GameAchieve",
    "fw": "GameFw", "jue": "GameJue", "shuxing": "GameShuxing",
    "task": "GameTask",
}


def read_csv(path):
    with open(path, encoding="utf-8-sig", newline="") as f:
        rows = list(csv.reader(f))
    if not rows:
        raise ValueError(f"{path}: empty")
    header = rows[0]
    if len(set(header)) != len(header):
        raise ValueError(f"{path}: duplicate column {header}")
    return header, rows[1:]


def sync_table(con, table, header, rows):
    cols = ", ".join(f'"{c}" TEXT' for c in header)
    pk = ", PRIMARY KEY (\"id\")" if "id" in header else ""
    con.execute(f'DROP TABLE IF EXISTS "{table}"')
    con.execute(f'CREATE TABLE "{table}" ({cols}{pk})')
    ph = ", ".join("?" for _ in header)
    payload = []
    for r in rows:
        if len(r) != len(header):
            raise ValueError(f"{table}: row width {len(r)} != {len(header)}")
        payload.append([v if v != "" else None for v in r])
    con.executemany(f'INSERT INTO "{table}" VALUES ({ph})', payload)
    return len(payload)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", "..", "..", ".."))
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--db", default=os.path.join(repo, "templates/server/server-game/data/M2.DB"))
    ap.add_argument("--data-dir", default=os.path.join(repo, "templates/game/legend/data"))
    ap.add_argument("--reference-dir", default=None)
    args = ap.parse_args()
    ref_dir = args.reference_dir or os.path.join(args.data_dir, "reference")

    con = sqlite3.connect(args.db)
    synced = []
    # 1) 玩法 CSV（按文件名建表）
    for name in sorted(os.listdir(args.data_dir)):
        if not name.endswith(".csv"):
            continue
        table = name[:-4]
        header, rows = read_csv(os.path.join(args.data_dir, name))
        synced.append((table, sync_table(con, table, header, rows)))
    # 2) data.dll 原表（Game* 前缀）
    manifest = {}
    mpath = os.path.join(ref_dir, "manifest.json")
    if os.path.exists(mpath):
        manifest = json.load(open(mpath, encoding="utf-8-sig"))
    for stem, table in GAME_TABLES.items():
        path = os.path.join(ref_dir, stem + ".csv")
        if not os.path.exists(path):
            print(f"skip {table}: {path} missing", file=sys.stderr)
            continue
        header, rows = read_csv(path)
        synced.append((table, sync_table(con, table, header, rows)))
    # 3) 来源清单
    con.execute('DROP TABLE IF EXISTS "GameMeta"')
    con.execute('CREATE TABLE "GameMeta" ('
                '"table_name" TEXT PRIMARY KEY, "rows" INTEGER, "synced_utc" TEXT)')
    now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    con.executemany('INSERT INTO "GameMeta" VALUES (?,?,?)',
                    [(t, n, now) for t, n in synced])
    con.execute('DROP TABLE IF EXISTS "GameSource"')
    con.execute('CREATE TABLE "GameSource" ('
                '"source_file" TEXT, "source_sha256" TEXT, "format_version" INTEGER, '
                '"reference_dir" TEXT, "synced_utc" TEXT)')
    con.execute('INSERT INTO "GameSource" VALUES (?,?,?,?,?)', (
        manifest.get("source_file", "data.dll"),
        manifest.get("source_sha256", ""),
        manifest.get("format_version", 0),
        os.path.relpath(ref_dir, repo), now))
    con.commit()
    con.close()
    total = 0
    for t, n in sorted(synced):
        print(f"  {t}: {n}")
        total += n
    print(f"synced {len(synced)} tables, {total} rows -> {args.db}")


if __name__ == "__main__":
    main()
