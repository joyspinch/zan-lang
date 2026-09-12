"""Reference completeness, repeatable import and invalid-configuration regression tests.
Run after building legend-attributes-test.exe (tools/test.ps1 does both).
Reference data now lives in the server M2.DB (Game* tables, synced from
data/reference CSVs which are generated on demand by import_reference.py).
"""
import csv
import importlib.util
import os
import sqlite3
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import sys
sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT/'templates/game/legend/data'
# The original data.dll snapshot lives with the server template that seeds from it
# (server-legend). Keep this the only place the client tests reach across templates.
DB = ROOT/'templates/server/server-legend/data/M2.DB'
EXE = ROOT/'build/legend-attributes-test.exe'
spec = importlib.util.spec_from_file_location('enrich', ROOT/'templates/game/legend/tools/enrich_attributes.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

GAME_TABLES = {'item': 'GameItem', 'monster': 'GameMonster', 'boss': 'GameBoss'}

def table(name): return module.read(DATA/f'{name}.csv')

def ref_table(name):
    """Read a data.dll source table from the synced M2.DB (verbatim TEXT values)."""
    con = sqlite3.connect(DB)
    try:
        cur = con.execute(f'SELECT * FROM "{GAME_TABLES[name]}"')
        cols = [d[0] for d in cur.description]
        return [dict(zip(cols, row)) for row in cur.fetchall()]
    finally:
        con.close()

def export_reference(dest: Path):
    """Reverse-sync: write reference CSVs from M2.DB so enrich can run standalone."""
    dest.mkdir(parents=True, exist_ok=True)
    for stem in GAME_TABLES:
        rows = ref_table(stem)
        cols = list(rows[0]) if rows else []
        with (dest/f'{stem}.csv').open('w', encoding='utf-8', newline='') as f:
            w = csv.DictWriter(f, fieldnames=cols)
            w.writeheader()
            for r in rows:
                w.writerow({k: ('' if v is None else v) for k, v in r.items()})

class Definitions(unittest.TestCase):
    def test_all_item_attributes_match_reference(self):
        raw = {r['id']:r for r in ref_table('item')}
        runtime = table('items')
        self.assertEqual({r['id'] for r in runtime}, set(raw))
        for row in runtime:
            source = raw[row['id']]
            for key, field in module.ATTRIBUTES.items():
                with self.subTest(item=row['id'], attribute=key):
                    expected = int(source.get(field) or 0) if source['aclass'] == '装备' else 0
                    self.assertEqual(int(row[key]), expected)
            self.assertEqual(int(row['required_job']), int(source.get('job') or 0)-1)
            self.assertEqual(int(row['required_rebirth']), int(source.get('met') or 0))
            self.assertNotIn('base_power', row)

    def test_all_valid_monsters_and_bosses(self):
        for dest, source in [('monsters','monster'),('bosses','boss')]:
            raw = {r['id']:r for r in ref_table(source) if r.get('zhp')}
            runtime = table(dest)
            self.assertEqual({r['id'] for r in runtime}, set(raw))
            for row in runtime:
                for key, field in module.ATTRIBUTES.items():
                    self.assertEqual(int(row[key]), int(raw[row['id']].get('z'+field) or 0), (dest,row['id'],key))

    def test_db_roundtrip_feeds_enrich_identically(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'_scratch',prefix='legend-data-') as tmp:
            path=Path(tmp)/'data';shutil.copytree(DATA,path)
            export_reference(path/'reference')
            norm = lambda b: b.replace(b'\r\n', b'\n')
            before={n:norm((path/f'{n}.csv').read_bytes()) for n in ['items','monsters','bosses']}
            module.enrich(path)
            self.assertEqual(before,{n:norm((path/f'{n}.csv').read_bytes()) for n in before})

    def test_rejected_configurations(self):
        mutations = [
            ('missing_attack', 'items', lambda rows: [r.pop('attack_max') for r in rows]),
            ('bad_job', 'items', lambda rows: rows[15].update(required_job='9')),
            ('negative_stat', 'items', lambda rows: rows[15].update(hp='-1')),
            ('duplicate_id', 'items', lambda rows: rows[1].update(id=rows[0]['id'])),
            ('overflow', 'items', lambda rows: rows[15].update(attack_max='99999999999999')),
            ('missing_attribute_rule', 'attributes', lambda rows: rows.pop()),
            ('unknown_school', 'skills', lambda rows: rows[0].update(school='unsupported')),
            ('inverted_monster_range', 'monsters', lambda rows: rows[0].update(attack_min='99999')),
        ]
        for name, target, mutate in mutations:
            with self.subTest(case=name), tempfile.TemporaryDirectory(dir=ROOT/'_scratch',prefix='legend-invalid-') as tmp:
                path=Path(tmp)/'data';shutil.copytree(DATA,path)
                rows=module.read(path/f'{target}.csv');mutate(rows);module.write(path/f'{target}.csv',rows)
                env=dict(os.environ,LEGEND_DATA_DIR=str(path),LEGEND_VALIDATE_ONLY='1')
                out=subprocess.run([str(EXE)],cwd=tmp,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=20)
                self.assertNotEqual(out.returncode,0,out.stdout+out.stderr)
                self.assertIn('Configuration rejected',out.stdout+out.stderr)

if __name__=='__main__': unittest.main(verbosity=2)
