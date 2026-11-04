"""Enrich runtime CSV definitions from lossless reference CSVs.
Only replaces source-owned definition columns; preserves template rewards/progression.
Source fields with uncertain meanings remain in reference/*.csv, never guessed.
"""
import argparse
import csv
from pathlib import Path

ATTRIBUTES = dict(hp='hp', mp='mp', attack_min='a1', attack_max='a2',
                  magic_min='b1', magic_max='b2', tao_min='c1', tao_max='c2',
                  defense_min='d1', defense_max='d2', magic_defense_min='e1', magic_defense_max='e2')

def read(path):
    with path.open(encoding='utf-8-sig', newline='') as f:
        rows = list(csv.DictReader(f))
    ids = [r['id'] for r in rows]
    if len(ids) != len(set(ids)):
        raise ValueError(f'{path}: duplicate ID')
    return rows

def write(path, rows):
    with path.open('w', encoding='utf-8', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]), lineterminator='\n')
        w.writeheader(); w.writerows(rows)

def number(row, key, default=0):
    value = row.get(key) or str(default)
    n = int(value)
    if n < 0 or n > 100000000:
        raise ValueError(f'{row["id"]}.{key}: out of range {value}')
    return n

def enrich(directory):
    counts = {}
    for target, source in [('items', 'item'), ('monsters', 'monster'), ('bosses', 'boss')]:
        raw = {r['id']: r for r in read(directory/'reference'/f'{source}.csv')}
        rows = read(directory/f'{target}.csv')
        for row in rows:
            original = raw[row['id']]
            for obsolete in ['base_power', 'attack', 'defense']:
                row.pop(obsolete, None)
            equipment = source != 'item' or original.get('aclass') == '装备'
            for attr, field in ATTRIBUTES.items():
                key = field if source == 'item' else 'z'+field
                row[attr] = number(original, key) if equipment else 0
            if source == 'item':
                row['required_job'] = number(original, 'job') - 1
                row['required_rebirth'] = number(original, 'met')
                row['weight'] = number(original, 'weight')
                row['sell_price'] = number(original, 'pirce')
                row['stack_limit'] = number(original, 'overlap', 1)
                row['description'] = original.get('present') or '-'
            else:
                row['hp'] = number(original, 'zhp')
                row['attack_school'] = {1:'attack', 2:'magic', 3:'tao'}[number(original, 'job')]
        write(directory/f'{target}.csv', rows)
        counts[target] = len(rows)
    return counts

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, default=Path(__file__).resolve().parents[1]/'data')
    print(enrich(parser.parse_args().data))
