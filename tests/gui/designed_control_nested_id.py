from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
if '--gate-held' not in sys.argv:
    command = f'& "{sys.executable}" -I "{Path(__file__)}" --gate-held; exit $LASTEXITCODE'
    env = dict(os.environ, ZAN_NESTED_ID_COMMAND=command)
    raise SystemExit(subprocess.run(
        ['pwsh', '-NoProfile', '-Command',
         '& "' + str(ROOT / 'scripts/build_gate.ps1') + '" -Command $env:ZAN_NESTED_ID_COMMAND'],
        cwd=ROOT, env=env).returncode)

work = Path(tempfile.mkdtemp(prefix='designed-control-nested-id-', dir=ROOT / 'build/test-out'))
sources = [ROOT / 'tests/gui/designed_control_nested_id_test.zan',
           ROOT / 'src/ide_zan/src/DesignedControl.zan',
           ROOT / 'build/zanc.exe']
identities = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
result = {'status': 'started', 'sourceHashes': identities, 'sourceCopied': False}
try:
    exe = work / 'probe.exe'
    args = [str(ROOT / 'build/zanc.exe'), str(sources[0]), str(sources[1]),
            '--auto-stdlib', '--package-project', str(ROOT),
            '--embed', str(ROOT / 'tests/gui/nested-id-test.html') + '=nested-id-test.html',
            '-o', str(exe)]
    compiled = subprocess.run(args, cwd=ROOT, capture_output=True, timeout=180)
    (work / 'compile.log').write_bytes(compiled.stdout + compiled.stderr)
    result['compileExit'] = compiled.returncode
    if compiled.returncode:
        raise RuntimeError('compile failed')
    ran = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, timeout=30)
    (work / 'run.log').write_bytes(ran.stdout + ran.stderr)
    result['exitCode'] = ran.returncode
    if ran.returncode != 0 or b'DESIGNED_CONTROL_NESTED_ID_PASS' not in ran.stdout:
        raise RuntimeError('nested id regression failed')
    result['identitiesUnchanged'] = all(
        hashlib.sha256(Path(p).read_bytes()).hexdigest() == h
        for p, h in identities.items())
    if not result['identitiesUnchanged']:
        raise RuntimeError('source identity changed')
    result['status'] = 'passed'
except BaseException as error:
    result['status'] = 'failed'
    result['error'] = str(error)
    raise
finally:
    (work / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(work, flush=True)
