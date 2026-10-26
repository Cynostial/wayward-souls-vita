#!/usr/bin/env python3
"""Build the Wayward Souls Vita launcher. Supply VitaSDK with matching libraries."""
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

ROOT=Path(__file__).resolve().parent

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk',default=os.environ.get('VITASDK'))
    parser.add_argument('--jobs',type=int,default=4)
    parser.add_argument('--check',action='store_true')
    parser.add_argument('--allow-library-mismatch',action='store_true',help='build against alternative library versions; these are unverified')
    args=parser.parse_args()
    if not args.sdk:parser.error('set VITASDK or pass --sdk')
    if args.jobs<1:parser.error('--jobs must be positive')
    sdk=Path(args.sdk).expanduser().resolve()
    settings=json.loads((ROOT/'dependencies.json').read_text())
    def tool(name):
        for filename in (name,name+'.exe'):
            p=sdk/'bin'/filename
            if p.is_file():return p
        raise RuntimeError('Missing SDK tool: '+name)
    names=('arm-vita-eabi-gcc','arm-vita-eabi-ar','vita-elf-create','vita-make-fself','vita-mksfoex','vita-pack-vpk')
    cc,ar,elf_create,make_self,make_sfo,pack_vpk=map(tool,names)
    for relative,digest in settings['required_archives'].items():
        p=sdk/relative
        if not p.is_file():raise RuntimeError('Missing SDK library: '+relative)
        if hashlib.sha256(p.read_bytes()).hexdigest()!=digest:
            if not args.allow_library_mismatch:raise RuntimeError('Library differs from tested version: '+relative)
            print('Using unverified library: '+relative)
    if args.check:
        print('SDK tools and library fingerprints verified.');return
    out=ROOT/'build';out.mkdir(exist_ok=True)
    env=os.environ.copy();env['PATH']=str(sdk/'bin')+os.pathsep+env.get('PATH','')
    def run(command):
        result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        return result.stdout+result.stderr
    arch=['-mcpu=cortex-a9','-mfpu=neon','-mfloat-abi=softfp']
    includes=['-I',ROOT/'src','-I',ROOT/'vendor/so_util','-I',ROOT/'vendor/tlsf']
    flags={
        'native':['-std=c11','-Wall','-Wextra','-Werror','-O2',*arch,*includes],
        'vendor':['-std=gnu11','-O2',*arch,*includes],
        'inet':['-std=gnu11','-O2'],
        'tlsf':['-std=c11','-O2'],
    }
    def compile_unit(unit):
        run([cc,*flags[unit['flavor']],'-c',ROOT/unit['source'],'-o',out/unit['object']])
        return unit['source']
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        pending=[pool.submit(compile_unit,u) for u in settings['units']]
        for future in as_completed(pending):print('Compiled '+future.result(),flush=True)
    archive=out/'libwayward_platform.a'
    archive.unlink(missing_ok=True)
    run([ar,'rcs',archive,*[out/u['object'] for u in settings['units']]])
    elf=out/'wayward.elf';velf=out/'wayward.velf'
    run([cc,*flags['native'],ROOT/'launcher/main.c',archive,'-Wl,--emit-relocs',
         '-Wl,--start-group',*['-l'+n for n in settings['libraries']],'-Wl,--end-group',
         '-Wl,-Map='+str(out/'wayward.map'),'-o',elf])
    run([elf_create,'-s',elf,velf])
    run([make_self,velf,out/'eboot.bin'])
    run([make_sfo,'-s','TITLE_ID='+settings['title_id'],'-s','APP_VER='+settings['application_version'],
         '-d','ATTRIBUTE=32768','-d','ATTRIBUTE2=12',settings['name'],out/'param.sfo'])
    run([pack_vpk,'-s',out/'param.sfo','-b',out/'eboot.bin',
         '-a',str(ROOT/'resources/sce_sys/icon0.png')+'=sce_sys/icon0.png',
         '-a',str(ROOT/'resources/sce_sys/livearea')+'=sce_sys/livearea',
         '-a',str(ROOT/'resources/art')+'=art',
         '-a',str(ROOT/'licenses')+'=licenses',out/'wayward.vpk'])
    print('Built '+str(out/'wayward.vpk'))

if __name__=='__main__':main()
