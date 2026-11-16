"""Exercise immutable reflection strings through real publish and sharding."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess


def run(command, work, stem, env):
    result = subprocess.run(command, cwd=work, env=env, capture_output=True,
                            timeout=120)
    (work / (stem + '.stdout')).write_bytes(result.stdout)
    (work / (stem + '.stderr')).write_bytes(result.stderr)
    if result.returncode:
        raise AssertionError(f'{stem}: exit {result.returncode}; see {work}')
    return result


def ir_counts(text):
    rows = [line for line in text.splitlines()
            if line.startswith('@__zan.refl.str.')]
    assert rows, 'reflection constants were not exercised'
    counts = Counter(line.split(' constant ', 1)[1].split(', align', 1)[0]
                     for line in rows)
    return {'definitions': len(rows), 'unique': len(counts),
            'max_repeat': max(counts.values()),
            'unnamed': sum('private unnamed_addr constant' in row for row in rows)}


def image_counts(image):
    data = image.read_bytes()
    if data[:2] != b'MZ':
        return {'bytes': len(data), 'format': 'non-PE'}
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    sections = struct.unpack_from('<H', data, pe + 6)[0]
    start = pe + 24 + struct.unpack_from('<H', data, pe + 20)[0]
    payloads = []
    for section in range(sections):
        off = start + 40 * section
        name = data[off:off + 8].rstrip(b'\0')
        if name not in (b'.rdata', b'.data'):
            continue
        size, raw = struct.unpack_from('<II', data, off + 16)
        chunk = data[raw:raw + size]
        for match in re.finditer(b'\xff' * 8, chunk):
            pos = match.start()
            if pos + 16 > len(chunk):
                continue
            header = struct.unpack_from('<Q', chunk, pos + 8)[0]
            length = header & 0xffffffff
            if header >> 32 != 0x5a414e53 or pos + 16 + length >= len(chunk):
                continue
            if chunk[pos + 16 + length] == 0:
                payloads.append(chunk[pos + 16:pos + 16 + length])
    counts = Counter(payloads)
    assert counts, 'managed string image accounting was not exercised'
    return {'bytes': len(data), 'instances': len(payloads), 'unique': len(counts),
            'max_repeat': max(counts.values()),
            'fixture_field_instances': counts[b'ReflectionPoolFieldUniqueB7C91']}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--zanc', type=Path, required=True)
    parser.add_argument('--src', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    compiler, source, work = args.zanc.resolve(), args.src.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    sdk_before = hashlib.sha256(compiler.read_bytes()).hexdigest()
    env = os.environ.copy()
    env.pop('ZAN_OBF', None)
    env['ZAN_NO_OBF'] = '1'
    env['ZAN_NO_SHARD'] = '0'
    env['ZAN_SHARD_MAX_FN'] = '5'
    env['ZAN_SHARD_MAX_INSN'] = '1000000'
    env['ZAN_SHARD'] = '0'
    common = [str(compiler), str(source), '--auto-stdlib', '--publish',
              '--no-check-leaks', '--no-arc-guard']
    ir = run(common + ['--emit-ir'], work, 'original-ir', env)
    counts = ir_counts(ir.stdout.decode('utf-8'))
    assert counts['definitions'] == counts['unique'], counts
    assert counts['unnamed'] == counts['definitions'], counts
    summary = {'original_ir': counts, 'images': {}}
    expected = (b'Same\nSame\nint\nReflectionPoolFieldUniqueB7C91\n'
                b'15\nPoolB\nliteral-positive-refl-pool\n')
    for mode in ('single', 'shard'):
        env['ZAN_SHARD'] = '0' if mode == 'single' else '1'
        dump = work / (mode + '.shards.ll')
        dump.unlink(missing_ok=True)
        env['ZAN_SHARD_DUMP'] = str(dump)
        image = work / (mode + ('.exe' if os.name == 'nt' else '.bin'))
        compiled = run(common + ['-o', str(image)], work, mode + '-compile', env)
        result = run([str(image)], work, mode + '-run', env)
        assert result.stdout.replace(b'\r\n', b'\n') == expected, result.stdout
        assert not result.stderr, result.stderr
        data = image.read_bytes()
        assert b'SOURCE_ONLY_REFL_POOL_8B1C7F6A' not in data
        assert b'literal-positive-refl-pool' in data
        image_stats = image_counts(image)
        summary['images'][mode] = image_stats
        if mode == 'single' and 'fixture_field_instances' in image_stats:
            assert image_stats['fixture_field_instances'] == 1, image_stats
        if mode == 'shard':
            diagnostics = compiled.stdout + compiled.stderr
            objects = re.search(rb'shard: ([1-9][0-9]*) objects emitted', diagnostics)
            assert objects
            assert b'falling back to single module' not in diagnostics
            # S4/S4b（design.md §8.2/§8.3）：碎片不再携带任何 pool 常量定义
            # ——托管串由 const blob 打包为单一 external 定义（碎片仅 blob
            # GEP 引用），其余内部常量（含晋升后的空池项）由 const promote
            # 升 external 唯一定义、碎片 extern 引用。防复制语义改由镜像
            # 账本（fixture_field_instances ≤ objects+1）承担；此处钉住
            # 两机制必须实际参与，且碎片零 pool 定义（extern 声明合法）。
            assert b'shard: const blob:' in diagnostics, diagnostics
            assert b'shard: const promote:' in diagnostics, diagnostics
            if 'fixture_field_instances' in image_stats:
                # One canonical pool entry per emitted object plus coordinator.
                assert 1 <= image_stats['fixture_field_instances'] <= int(objects[1]) + 1, image_stats
            dump_text = dump.read_text(encoding='utf-8')
            leaked = [line for line in dump_text.splitlines()
                      if line.startswith('@__zan.refl.str.')
                      and ('= private ' in line or '= internal ' in line)]
            assert not leaked, (f'{len(leaked)} pool constant definitions '
                                f'leaked into shards', leaked[:3])
            summary['shard_pool_rows'] = sum(
                1 for line in dump_text.splitlines()
                if line.startswith('@__zan.refl.str.'))
    sdk_after = hashlib.sha256(compiler.read_bytes()).hexdigest()
    assert sdk_before == sdk_after, 'compiler changed during regression'
    summary['sdk_sha256'] = sdk_after
    (work / 'result.json').write_text(json.dumps(summary, indent=2) + '\n',
                                     encoding='utf-8')
    print('reflection constant pool publish/shard/runtime: PASS')


if __name__ == '__main__':
    main()
