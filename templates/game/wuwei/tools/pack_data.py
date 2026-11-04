"""Encrypt editable CSVs into an authenticated release resource directory.
Requires Python cryptography. The key also ships in the client: this is protection
against casual editing, not a claim that an offline game cannot be reverse-engineered.
"""
import argparse
import base64
import os
from pathlib import Path
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

KEY = b'wuwei-v1:jade-ink:local-save-key!!'[:32]
AAD = b'wuwei-save-v1'
EXCLUDED_TABLES = {'source_tables'}


def pack(source, target):
    target.mkdir(parents=True, exist_ok=True)
    cipher = AESGCM(KEY)
    tables = sorted(path for path in source.glob('*.csv') if path.stem not in EXCLUDED_TABLES)
    if not tables:
        raise ValueError('No runtime CSV tables found')
    for path in tables:
        name = path.stem
        raw = path.read_bytes()
        nonce = os.urandom(12)
        encrypted = cipher.encrypt(nonce, raw, AAD)
        fields = [nonce, encrypted[:-16], encrypted[-16:]]
        envelope = 'WUW1|' + '|'.join(base64.b64encode(value).decode('ascii') for value in fields)
        (target / (name + '.csv')).write_text(envelope, encoding='ascii')
    print(f'Protected {len(tables)} balancing tables: {target}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1] / 'data')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    pack(args.source, args.output)
