"""Losslessly export supplied MiniLegend JSON resources to editable UTF-8 CSV.
Does not execute binaries, decrypt assets, infer skill effects or reinterpret drop odds.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path


def export(source: Path, output: Path):
    raw = source.read_bytes()
    data = json.loads(raw.decode('utf-8-sig'))
    if not isinstance(data, dict):
        raise ValueError('Expected an object containing named table arrays')
    output.mkdir(parents=True, exist_ok=True)
    counts = {}
    for name, rows in data.items():
        if not name.isidentifier() or not isinstance(rows, list):
            raise ValueError(f'Invalid table: {name}')
        if any(not isinstance(row, dict) for row in rows):
            raise ValueError(f'Invalid row: {name}')
        columns = list(dict.fromkeys(key for row in rows for key in row))
        ids = [row.get('id') for row in rows]
        if rows and (None in ids or len(ids) != len(set(ids))):
            raise ValueError(f'Missing or duplicate id: {name}')
        # Reject nested structures instead of losing information in str(dict).
        if any(not isinstance(value, (str, int, float, bool, type(None)))
               for row in rows for value in row.values()):
            raise ValueError(f'Nested cell: {name}')
        with (output / f'{name}.csv').open('w', encoding='utf-8', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=columns or ['id'], lineterminator='\n')
            writer.writeheader()
            writer.writerows(rows)
        counts[name] = len(rows)
    manifest = {'format_version': 1, 'source_file': source.name,
                'source_sha256': hashlib.sha256(raw).hexdigest(), 'tables': counts,
                'notes': 'Original IDs and columns; blank means source field absent. No probability/effect inference.'}
    (output / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    return counts


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(export(args.source, args.output), ensure_ascii=False))
