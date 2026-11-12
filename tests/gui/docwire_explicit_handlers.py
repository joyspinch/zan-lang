from pathlib import Path
import sys,os,subprocess,tempfile,json,hashlib
root=Path(__file__).resolve().parents[2]
if '--gate-held' not in sys.argv:
    command=f'& "{sys.executable}" -I "{Path(__file__)}" --gate-held'
    raise SystemExit(subprocess.run(['pwsh','-NoProfile','-Command',f'& "{root}/scripts/build_gate.ps1" -Command $env:ZAN_DOCWIRE_COMMAND; exit $LASTEXITCODE'],cwd=root,env=dict(os.environ,ZAN_DOCWIRE_COMMAND=command)).returncode)
work=Path(tempfile.mkdtemp(prefix='docwire-explicit-',dir=root/'build/test-out'));print(work,flush=True)
paths=[root/p for p in ['tests/gui/docwire_explicit_handlers_test.zan','packages/Zan.Gui/src/Gui/Core/ModelBinder.zan','packages/Zan.Gui/src/Gui/Core/DocWire.zan','build/zanc.exe']]
identities={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
result={'status':'started','sourceHashes':identities,'sourceCopied':False,'cases':[]}
try:
    run=subprocess.run([str(root/'build/zanc.exe'),str(paths[0]),'--auto-stdlib','--package-project',str(root),'-o',str(work/'probe.exe')],cwd=root,capture_output=True,timeout=180)
    (work/'compile.log').write_bytes(run.stdout+run.stderr)
    if run.returncode: raise RuntimeError('compile failed')
    for mode in ['explicit','missing']:
        run=subprocess.run([str(work/'probe.exe'),mode],cwd=root,capture_output=True,timeout=20)
        (work/(mode+'.log')).write_bytes(run.stdout+run.stderr)
        expected=0 if mode=='explicit' else 1
        if run.returncode!=expected or (mode=='missing' and b'MissingHandler' not in run.stdout): raise RuntimeError(mode+' failed')
        result['cases'].append({'name':mode,'exitCode':run.returncode})
    result['identitiesUnchanged']=all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==h for p,h in identities.items())
    if not result['identitiesUnchanged']: raise RuntimeError('source identity changed')
    result['status']='passed'
except BaseException as error:
    result['status']='failed';result['error']=str(error);raise
finally:
    (work/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
