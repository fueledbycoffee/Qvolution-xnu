#!/usr/bin/env python3
"""Compile the exact owned production spawn body against host adapters only."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
original=a.source.read_text();start=original.index('long sys_posix_spawn(')
body=original[start:]
prefix=Path(__file__).with_name('source-control.c').read_text()
assert prefix.count('/* PRODUCTION_FUNCTION */')==1
a.output.mkdir(parents=True,exist_ok=False)
fixture=a.output/'control.c';fixture.write_text(prefix.replace('/* PRODUCTION_FUNCTION */',body))
child=a.output/'child.c';child.write_text('int main(void) { return 37; }\n')
record={'production_source_sha256':hashlib.sha256(a.source.read_bytes()).hexdigest(),'function_sha256':hashlib.sha256(body.encode()).hexdigest(),'results':[]}
for name,cmd in [('child-compile',['cc',str(child),'-o',str(a.output/'child')]),('control-compile',['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-sign-compare',str(fixture),'-o',str(a.output/'control')]),('control-run',[str(a.output/'control'),str((a.output/'child').resolve())])]:
    result=subprocess.run(cmd,text=True,capture_output=True)
    (a.output/(name+'.log')).write_text(result.stdout+result.stderr)
    record['results'].append({'name':name,'command':cmd,'exit':result.returncode,'output':result.stdout+result.stderr})
    (a.output/'metadata.json').write_text(json.dumps(record,indent=2)+'\n')
    if result.returncode:break
print(json.dumps(record,indent=2))
raise SystemExit(result.returncode)
