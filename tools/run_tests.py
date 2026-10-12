"""Build and exercise the shipped DLL in workspace-only synthetic hosts.

The GPU oracle reads the installed mod directly; no game/shader source is
packaged. Nothing is installed into Steam, Workshop, or the user's Documents.
"""
from pathlib import Path
import hashlib
import json
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parents[1]
BUILD = ROOT / 'build'
RELEASE = WORKSPACE / 'full_releases' / 'Globe Map Fix'
EXE = Path(r'D:\SteamLibrary\steamapps\common\Hearts of Iron IV\hoi4.exe')
MOD = Path(r'D:\SteamLibrary\steamapps\workshop\content\394360\3816496697')
GXX = shutil.which('g++')
RESULTS = []


def run(name, args, cwd=ROOT, timeout=90):
    p = subprocess.run([str(a) for a in args], cwd=cwd, capture_output=True,
                       text=True, errors='replace', timeout=timeout)
    result = dict(name=name, returncode=p.returncode, stdout=p.stdout, stderr=p.stderr)
    RESULTS.append(result)
    print(f'{name}: {"PASS" if p.returncode == 0 else "FAIL"}')
    if p.stdout.strip(): print(p.stdout.strip())
    if p.stderr.strip(): print(p.stderr.strip())
    return p.returncode == 0


def compile_test(name, files, options=()):
    return run('build '+name, [GXX, '-std=c++17', '-O2', '-ffp-contract=off',
        '-fno-math-errno', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-static',
        *files, '-o', BUILD/name, *options])


def bmp_header(width=5632, height=2048):
    # A synthetic header is sufficient to test dimension resolution, without
    # copying any game asset into the source or release directories.
    b = bytearray(54)
    struct.pack_into('<2sIHHI', b, 0, b'BM', 54, 0, 0, 54)
    struct.pack_into('<IiiHHIIiiII', b, 14, 40, width, height, 1, 24, 0, 0, 0, 0, 0, 0)
    return b


def setup(name, mode, status, overrides=None):
    case = BUILD / 'cases' / name
    case.mkdir(parents=True, exist_ok=True)
    host = case/'hoi4.exe'
    shutil.copy2(BUILD/'hoi4.exe', host)
    dll_dir = case/'injected_mods'/'Globe Map Fix'
    dll_dir.mkdir(parents=True, exist_ok=True)
    dll = dll_dir/'Globe Map Fix.dll'
    shutil.copy2(RELEASE/'Globe Map Fix.dll', dll)
    user = case/'用户数据'
    (user/'mod').mkdir(parents=True, exist_ok=True)
    (case/'map').mkdir(exist_ok=True)
    (case/'map'/'provinces.bmp').write_bytes(bmp_header())
    (case/'gfx'/'FX').mkdir(parents=True, exist_ok=True)
    (case/'gfx'/'FX'/'constants.fxh').write_text('Code [[ static const float WATER_HEIGHT = 9.5f; ]]', encoding='utf-8')
    enabled = ['mod/globe.mod']
    (user/'mod'/'globe.mod').write_text('name="Globe Mod"\npath="'+MOD.as_posix()+'"\n', encoding='utf-8')
    if mode == 'inactive': enabled=[]
    if mode in ('wrong_shader', 'mask_globe', 'override_terrain'):
        override = case/'override'
        (override/'gfx'/'FX').mkdir(parents=True, exist_ok=True)
        if mode == 'wrong_shader': (override/'gfx'/'FX'/'globe.fxh').write_text('Code [[ float3 GlobeWarp(float3 p) { return p; } ]]')
        if mode == 'override_terrain': (override/'gfx'/'FX'/'pdxmap.shader').write_text('Code [[ float3 Flat(float3 p) { return p; } ]]')
        descriptor = 'path="'+override.as_posix()+'"\n'
        if mode == 'mask_globe': descriptor += 'replace_path="gfx/FX"\n'
        (user/'mod'/'override.mod').write_text(descriptor, encoding='utf-8')
        enabled.append('mod/override.mod')
    state = json.dumps({'enabled_mods':enabled,'disabled_dlcs':[]}, ensure_ascii=False)
    if mode == 'malformed': state='{"enabled_mods":["mod/globe.mod",]}'
    (user/'dlc_load.json').write_text(state, encoding='utf-8')
    custom='' if mode=='command_userdir' else str(user)
    ini='[fix]\nenabled='+('0' if mode=='disabled' else '1')+'\nprobe='+('1' if mode=='probe' else '0')+'\nuser_data_dir='+custom+'\n'
    # Win32 INI APIs use UTF-16 for non-ASCII custom paths.
    (dll_dir/'Globe Map Fix.ini').write_text(ini, encoding='utf-16')
    if mode == 'injector_probe':
        proxy = WORKSPACE/'full_releases'/'hoi4_mod_injector'/'dxgi.dll'
        shutil.copy2(proxy, case/'dxgi.dll')
        (case/'injected_mods'/'hoi4_mod_injector.ini').write_text('[injector]\ndelay_ms=700\nprobe=1\n')
    elif mode == 'injector':
        shutil.copy2(WORKSPACE/'full_releases'/'hoi4_mod_injector'/'dxgi.dll', case/'dxgi.dll')
        (case/'injected_mods'/'hoi4_mod_injector.ini').write_text('[injector]\ndelay_ms=700\nprobe=0\n')
    if overrides: overrides(case, user, dll_dir)
    argument='injector' if mode.startswith('injector') else str(dll)
    extra=[]
    if mode=='command_userdir': extra=['-userdir='+str(user)]
    if mode=='duplicate':
        second=case/'duplicate';second.mkdir(exist_ok=True)
        shutil.copy2(dll,second/dll.name);shutil.copy2(dll_dir/'Globe Map Fix.ini',second/'Globe Map Fix.ini')
        extra=['duplicate',second/dll.name]
    ok = run('integration '+name, [host, argument, str(status), *extra], cwd=case, timeout=30)
    log=dll_dir/'Globe Map Fix.log'
    if log.exists(): RESULTS[-1]['dll_log']=log.read_text(errors='replace')
    loader=case/'injected_mods'/'hoi4_mod_injector.log'
    if loader.exists(): RESULTS[-1]['injector_log']=loader.read_text(errors='replace')
    return ok


def main():
    BUILD.mkdir(exist_ok=True)
    passed = compile_test('unit_tests.exe', [ROOT/'tools'/'unit_tests.cpp'])
    passed &= compile_test('gpu_oracle.exe', [ROOT/'tools'/'gpu_oracle.cpp'], ['-municode','-ld3d11','-ld3dcompiler'])
    passed &= compile_test('hoi4.exe', [ROOT/'tools'/'integration_host.cpp', ROOT/'tools'/'fixture.S'], ['-municode','-ldxgi','-luuid','-luser32'])
    if not passed: return 1
    passed &= run('unit + installed exe signatures', [BUILD/'unit_tests.exe', EXE])
    passed &= run('actual HLSL GPU oracle', [BUILD/'gpu_oracle.exe', MOD/'gfx'/'FX'/'globe.fxh'])
    for name,mode,status in [('active','active',4),('command_userdir','command_userdir',4),('duplicate','duplicate',4),('inactive','inactive',1),
        ('wrong_shader','wrong_shader',2),('replace_path_masks_shader','mask_globe',1),
        ('terrain_override','override_terrain',2),('malformed_json','malformed',2),
        ('disabled','disabled',5),('probe','probe',3),
        ('real_injector','injector',4),('real_injector_probe','injector_probe',3)]:
        passed &= setup(name,mode,status)
    summary=dict(schema='globe_map_fix.test_report.v1',passed=bool(passed),
        dll_sha256=hashlib.sha256((RELEASE/'Globe Map Fix.dll').read_bytes()).hexdigest(),
        real_game_tested=False,results=RESULTS)
    (BUILD/'test_report.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    return 0 if passed else 1


if __name__=='__main__':
    sys.exit(main())
