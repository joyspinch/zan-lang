"""Offline parser fixtures only: cmake -P, no build or native application."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

HERE = Path(__file__).resolve().parent
REAL = HERE / 'cmake/fixtures/frame_log_missing_surface.txt'
FIRST = 'firstframe whole_ms=143.25 tree_ms=23.75 blend=9776kpx style=3196/268miss clock=raw-ms surface=1920x1230\n'
INLINE = ('frames=30 full=2 render_ms=8.5 worst_ms=20 partial=28 part_render_ms=3.1 '
          'tree_ms=7.0 slow=2 fill=3/17kpx blend=4/21kpx style=60/1miss clock=raw-ms surface=1920x1230\n')
LEGACY = ('frames=30 full=2 render_ms=8.5 worst_ms=20 partial=28 part_render_ms=3.1 '
          'tree_ms=7.0 slow16=2 clock=raw-ms surface=1920x1230\n'
          '  raster/frame fill=3/17kpx blend=4/21kpx style=60/1miss\n')
SECOND_INLINE = ('frames=30 full=7 render_ms=12.375 worst_ms=47.625 partial=23 part_render_ms=4.875 '
                 'tree_ms=9.125 slow=5 fill=6/31kpx blend=8/43kpx restore=2/5kpx clock=raw-ms '
                 'round=4/7kpx grad=1/11kpx glyph=9/13kpx snap=3/17kpx '
                 'radial=1/19kpx blur=2/23kpx blurhit=3/29kpx img=4/37kpx '
                 'bsimd=0/41kpx bscalar=0/47kpx style=90/4miss surface=1600x1000\n')
SECOND_LEGACY = ('frames=30 full=7 render_ms=12.375 worst_ms=47.625 partial=23 '
                 'part_render_ms=4.875 tree_ms=9.125 slow16=5 clock=raw-ms surface=1600x1000\n'
                 '  raster/frame fill=' + SECOND_INLINE.split('fill=', 1)[1].split(' surface=', 1)[0] + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    fixtures = args.output / 'fixtures'
    fixtures.mkdir(parents=True, exist_ok=True)
    output = args.output / 'results'
    output.mkdir(parents=True, exist_ok=True)
    cases = {
        'inline': (FIRST + INLINE, True, 'inline'),
        'legacy': (FIRST + LEGACY, True, 'legacy'),
        'two-inline': (FIRST + INLINE + SECOND_INLINE, True, 'inline;inline'),
        'two-legacy': (FIRST + LEGACY + SECOND_LEGACY, True, 'legacy;legacy'),
        'absent-zero-primitives': (FIRST + INLINE.replace('fill=3/17kpx ', ''), True, 'inline'),
        'missing-batch-surface': (FIRST + INLINE.replace(' surface=1920x1230', ''), False, ''),
        'missing-first-surface': (FIRST.replace(' surface=1920x1230', '') + INLINE, False, ''),
        'missing-first-clock': (FIRST.replace('clock=raw-ms ', '') + INLINE, False, ''),
        'missing-slow': (FIRST + INLINE.replace('slow=2 ', ''), False, ''),
        'missing-batch-clock': (FIRST + INLINE.replace('clock=raw-ms ', ''), False, ''),
        'missing-timing': (FIRST + INLINE.replace('tree_ms=7.0 ', ''), False, ''),
        'negative-raster': (FIRST + INLINE.replace('blend=4/21kpx', 'blend=4/-21kpx'), False, ''),
        'duplicate-surface': (FIRST + INLINE.replace('surface=1920x1230', 'surface=1920x1230 surface=1920x1230'), False, ''),
        'mixed-slow': (FIRST + INLINE.replace('slow=2', 'slow=2 slow16=2'), False, ''),
        'batch-before-first': (INLINE + FIRST, False, ''),
        'duplicate-first': (FIRST + FIRST + INLINE, False, ''),
        'orphan-raster': (FIRST + LEGACY.splitlines()[1] + '\n' + INLINE, False, ''),
        'legacy-interleaved': (FIRST + LEGACY.replace('\n  raster', '\nscroll_damage=1,2+3x4\n  raster'), False, ''),
        'legacy-truncated': (FIRST + LEGACY.splitlines()[0] + '\n', False, ''),
        'invalid-batch-count': (FIRST + INLINE.replace('frames=30', 'frames=29'), False, ''),
        'invalid-partial-total': (FIRST + INLINE.replace('full=2', 'full=3'), False, ''),
        'slow-over-frames': (FIRST + INLINE.replace('slow=2', 'slow=31'), False, ''),
        'zero-surface': (FIRST + INLINE.replace('surface=1920x1230', 'surface=0x1230'), False, ''),
        'missing-style': (FIRST + INLINE.replace('style=60/1miss ', ''), False, ''),
        'missing-first-whole': (FIRST.replace('whole_ms=143.25 ', '') + INLINE, False, ''),
        'duplicate-render': (FIRST + INLINE.replace('render_ms=8.5 ', 'render_ms=8.5 render_ms=8.6 ', 1), False, ''),
        'duplicate-first-tree': (FIRST.replace('tree_ms=23.75', 'tree_ms=23.75 tree_ms=24.25') + INLINE, False, ''),
        'illegal-decimal': (FIRST + INLINE.replace('render_ms=8.5 ', 'render_ms=8.5.1 ', 1), False, ''),
        'negative-decimal': (FIRST + INLINE.replace('tree_ms=7.0 ', 'tree_ms=-7.25 '), False, ''),
        'trailing-decimal': (FIRST + INLINE.replace('part_render_ms=3.1 ', 'part_render_ms=3. '), False, ''),
    }
    rows = []
    for name, (text, expected, format_name) in cases.items():
        path = fixtures / (name + '.log')
        path.write_text(text, encoding='utf-8')
        rows.append(run_case(name, path, expected, format_name, output))
    # Read the immutable real failed receipt log in place: do not patch/add fields.
    rows.append(run_case('actual-missing-surface', REAL, False, '', output))
    report = {'pass': all(row['pass'] for row in rows),
              'scope': 'Offline parser fixture checks only, synthetic positive text is not IDE or budget acceptance',
              'slowSemantics': 'count(wholeMs > 16), wholeMs=endMs-perfBeginMs; frozen tick validity is separate',
              'surfaceSemantics': 'complete canvas in device pixels including chrome, not content-only PNG dimensions',
              'firstBlendSemantics': 'kind-2 cumulative peek before first frame end; no delta conversion',
              'realInput': str(REAL), 'realInputSha256': digest(REAL),
              'parserSha256': digest(HERE / 'cmake/FrameLogParser.cmake'), 'cases': rows}
    (output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 0 if report['pass'] else 1


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def expectations(name, format_name):
    # Independent expected values, deliberately not parsed from fixture text.
    result = {'BATCH_COUNT': str(len(format_name.split(';'))), 'FORMATS': format_name,
              'FIRST_whole_ms': '143.25', 'FIRST_tree_ms': '23.75',
              'FIRST_BLEND_KPX': '9776', 'FIRST_STYLE_MISS': '268', 'FIRST_CLOCK': 'raw-ms', 'FIRST_SURFACE': '1920x1230'}
    raster_keys = ('fill', 'blend', 'restore', 'round', 'grad', 'glyph', 'snap',
                   'radial', 'blur', 'blurhit', 'img', 'bsimd', 'bscalar')
    batches = [({'render_ms': '8.5', 'worst_ms': '20', 'part_render_ms': '3.1', 'tree_ms': '7.0',
                 'SLOW_GT16': '2', 'STYLE_RESOLVE': '60', 'STYLE_MISS': '1', 'PARTIAL': '28',
                 'SURFACE': '1920x1230', 'CLOCK': 'raw-ms', 'TOTAL_KPX': '21' if name == 'absent-zero-primitives' else '38'},
                [0 if name == 'absent-zero-primitives' else 17, 21, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])]
    if ';' in format_name:
        batches.append(({'render_ms': '12.375', 'worst_ms': '47.625', 'part_render_ms': '4.875',
                         'tree_ms': '9.125', 'SLOW_GT16': '5', 'STYLE_RESOLVE': '90', 'STYLE_MISS': '4',
                         'PARTIAL': '23', 'SURFACE': '1600x1000', 'CLOCK': 'raw-ms', 'TOTAL_KPX': '127'},
                        [31, 43, 5, 7, 11, 13, 17, 19, 23, 29, 37, 41, 47]))
    for index, (values, raster) in enumerate(batches):
        values.update({key + '_KPX': str(value) for key, value in zip(raster_keys, raster)})
        result.update({'BATCH_' + str(index) + '_' + key: value for key, value in values.items()})
    return result


def run_case(name, path, expected, format_name, output):
    command = ['cmake', '-DLOG=' + str(path)]
    asserted_fields = {}
    if expected:
        asserted_fields = expectations(name, format_name)
        expected_path = output / (name + '-expected.cmake')
        expected_path.write_text('set(EXPECT_FIELDS "' + ';'.join(asserted_fields) + '")\n'
                                 + ''.join('set(EXPECT_' + key + ' "' + value + '")\n'
                                           for key, value in asserted_fields.items()), encoding='utf-8')
        command.append('-DEXPECTATIONS=' + str(expected_path))
    command += ['-P', str(HERE / 'cmake/frame_log_parse_case.cmake')]
    result = subprocess.run(command, cwd=HERE, capture_output=True, text=True, timeout=15)
    text = result.stdout + result.stderr
    (output / (name + '.log')).write_text(text, encoding='utf-8')
    actual = result.returncode == 0 and 'FRAME_LOG_PARSE_OK' in text
    return {'name': name, 'expectedAccepted': expected, 'exit': result.returncode,
            'pass': actual == expected and (expected or 'FRAME_LOG_INVALID' in text),
            'assertedFields': asserted_fields,
            'log': str(output / (name + '.log'))}


if __name__ == '__main__':
    raise SystemExit(main())
