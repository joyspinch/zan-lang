"""Default assets belong to the user entry, never to package search inputs."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--zanc', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    compiler, root = args.zanc.resolve(), args.root.resolve()
    output, work = args.out.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    scratch = root / '_scratch'
    scratch.mkdir(exist_ok=True)
    results = []
    with tempfile.TemporaryDirectory(prefix='embed-owner-', dir=scratch) as directory:
        fixture = Path(directory)
        sdk = fixture / 'sdk'
        package = sdk / 'packages/OwnerFixture'
        package.mkdir(parents=True)
        (package / 'zan.pkg').write_text('name = "OwnerFixture"\nversion = "1.0.0"\n', encoding='utf-8')
        pkgsrc = package / 'src/OwnerFixture'
        (pkgsrc / 'Assets').mkdir(parents=True)
        (pkgsrc / 'Marker.zan').write_text(
            'namespace OwnerFixture;\npublic class Marker { public static int Value() { return 7; } }\n',
            encoding='utf-8')
        (pkgsrc / 'Assets/leak.txt').write_text('package-source-is-not-application-assets', encoding='utf-8')
        (sdk / 'assets').mkdir()
        (sdk / 'assets/sdk-only.txt').write_text('sdk-assets-must-not-leak', encoding='utf-8')
        project = sdk / 'apps/demo'
        entry = project / 'src/main.zan'
        entry.parent.mkdir(parents=True)
        (project / 'assets').mkdir()
        (project / 'assets/owned.txt').write_text('owned', encoding='utf-8')
        entry.write_text('''using System;
using System.IO;
using OwnerFixture;
class Program {
    static void Main() {
        Console.WriteLine(Marker.Value());
        Console.WriteLine(File.Exists("assets/owned.txt"));
        Console.WriteLine(File.Exists("assets/leak.txt"));
        Console.WriteLine(File.Exists("assets/sdk-only.txt"));
        Console.WriteLine(File.Exists("assets/override.txt"));
        if (File.Exists("assets/owned.txt")) {
            Console.WriteLine(File.ReadAllText("assets/owned.txt"));
        }
    }
}
''', encoding='utf-8')
        override = fixture / 'override'
        override.mkdir()
        (override / 'override.txt').write_text('explicit', encoding='utf-8')
        cases = [
            ('manifest', True, [], b'7\ntrue\nfalse\nfalse\nfalse\nowned\n'),
            ('single-file', False, [], b'7\nfalse\nfalse\nfalse\nfalse\n'),
            ('explicit', False, ['--embed', str(override) + '=assets'],
             b'7\nfalse\nfalse\nfalse\ntrue\n'),
            ('single-file-owned', False, [], b'7\ntrue\nfalse\nfalse\nfalse\nowned\n'),
        ]
        manifest = project / 'zan.proj'
        for name, has_manifest, extra, expected in cases:
            if name == 'single-file-owned':
                (entry.parent / 'assets').mkdir()
                (entry.parent / 'assets/owned.txt').write_text('owned', encoding='utf-8')
            if has_manifest:
                manifest.write_text('name = OwnerFixtureApp\ntype = console\nentry = src/main.zan\n', encoding='utf-8')
            else:
                manifest.unlink(missing_ok=True)
            output.unlink(missing_ok=True)
            command = [str(compiler), str(entry), '--auto-stdlib', '--publish',
                       '--package-project', str(sdk), *extra, '-o', str(output)]
            compiled = subprocess.run(command, cwd=root, capture_output=True, timeout=180)
            (work / (name + '-compile.log')).write_bytes(compiled.stdout + compiled.stderr)
            assert compiled.returncode == 0, (name, compiled.returncode, work)
            assert output.is_file() and output.stat().st_size, name
            ran = subprocess.run([str(output)], cwd=work, capture_output=True, timeout=20)
            (work / (name + '-run.log')).write_bytes(ran.stdout + ran.stderr)
            assert ran.returncode == 0, (name, ran.returncode, ran.stderr)
            assert ran.stdout.replace(b'\r\n', b'\n') == expected, (name, ran.stdout)
            assert not ran.stderr, (name, ran.stderr)
            results.append({'case': name, 'compileExit': compiled.returncode,
                            'runExit': ran.returncode, 'bytes': output.stat().st_size})
    (work / 'result.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    print('user resource ownership, package isolation and explicit embed: PASS')


if __name__ == '__main__':
    main()
