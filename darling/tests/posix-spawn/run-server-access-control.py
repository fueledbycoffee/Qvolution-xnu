#!/usr/bin/env python3
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--fork-source',type=Path,required=True);p.add_argument('--helper-source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
fork=a.fork_source.read_text();fork=fork[fork.index('long sys_fork(void)'):]
helper=a.helper_source.read_text();helper=helper[helper.index('void mach_driver_setup_server_access(void)'):helper.index('void mach_driver_init(')]
prefix=Path(__file__).with_name('server-access-control.c').read_text()
source=prefix.replace('/* PRODUCTION_HELPER */',helper).replace('/* PRODUCTION_FORK */',fork)
a.output.mkdir(parents=True,exist_ok=False);fixture=a.output/'control.c';fixture.write_text(source)
record={'fork_function_sha256':hashlib.sha256(fork.encode()).hexdigest(),'helper_function_sha256':hashlib.sha256(helper.encode()).hexdigest(),'results':[]}
for name,command in [('compile',['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',str(fixture),'-o',str(a.output/'control')]),('run',[str(a.output/'control')])]:
    result=subprocess.run(command,text=True,capture_output=True,timeout=10)
    (a.output/(name+'.log')).write_text(result.stdout+result.stderr)
    record['results'].append({'phase':name,'command':command,'exit':result.returncode,'output':result.stdout+result.stderr})
    (a.output/'metadata.json').write_text(json.dumps(record,indent=2)+'\n')
    if result.returncode:break
print(json.dumps(record,indent=2));raise SystemExit(result.returncode)
