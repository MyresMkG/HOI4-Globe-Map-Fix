"""Package the verified workspace DLL and matching evidence without installing it."""
from pathlib import Path
import ctypes
import hashlib
import json
import shutil

ROOT=Path(__file__).resolve().parents[1]
RELEASE=ROOT.parents[1]/'full_releases'/'Globe Map Fix'

def binary_version(path):
    version=ctypes.WinDLL('version',use_last_error=True)
    size_fn=version.GetFileVersionInfoSizeW
    size_fn.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_ulong)]
    size_fn.restype=ctypes.c_ulong
    info_fn=version.GetFileVersionInfoW
    info_fn.argtypes=[ctypes.c_wchar_p,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_void_p]
    query=version.VerQueryValueW
    query.argtypes=[ctypes.c_void_p,ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(ctypes.c_uint)]
    ignored=ctypes.c_ulong()
    size=size_fn(str(path),ctypes.byref(ignored))
    if not size: raise RuntimeError('DLL VERSIONINFO is missing')
    buf=ctypes.create_string_buffer(size)
    if not info_fn(str(path),0,size,buf): raise RuntimeError('DLL VERSIONINFO read failed')
    address=ctypes.c_void_p();length=ctypes.c_uint()
    if not query(buf,'\\',ctypes.byref(address),ctypes.byref(length)) or length.value<52:
        raise RuntimeError('DLL fixed version information is invalid')
    words=ctypes.cast(address,ctypes.POINTER(ctypes.c_uint32))
    if words[0]!=0xFEEF04BD: raise RuntimeError('DLL version signature is invalid')
    def dotted(ms,ls): return '.'.join(map(str,[ms>>16,ms&65535,ls>>16,ls&65535]))
    return dotted(words[2],words[3]),dotted(words[4],words[5])

def main():
    dll=RELEASE/'Globe Map Fix.dll'
    report=json.loads((ROOT/'build'/'test_report.json').read_text(encoding='utf-8'))
    sha=hashlib.sha256(dll.read_bytes()).hexdigest()
    if not report['passed'] or report['version']!='1.1.1' or report['dll_sha256']!=sha:
        raise RuntimeError('Release DLL does not match a passing v1.1.1 test report')
    for path,wanted in report['source_sha256'].items():
        if hashlib.sha256((ROOT/path).read_bytes()).hexdigest()!=wanted:
            raise RuntimeError('Source changed after validation: '+path)
    file_version,product_version=binary_version(dll)
    if file_version!='1.1.1.0' or product_version!='1.1.1.0':
        raise RuntimeError('Unexpected DLL file/product version')
    report.update(file_version=file_version,product_version=product_version)
    for target in ['README.md','使用说明.md']:
        shutil.copy2(ROOT/'README.md',RELEASE/target)
    shutil.copy2(ROOT/'CHANGELOG.md',RELEASE/'更新说明_v1.1.1.md')
    (RELEASE/'验证报告.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    items=['Globe Map Fix.dll','Globe Map Fix.ini','README.md','使用说明.md','更新说明_v1.1.1.md','验证报告.json']
    (RELEASE/'SHA256.txt').write_text(''.join(
        hashlib.sha256((RELEASE/name).read_bytes()).hexdigest()+'  '+name+'\n' for name in items),encoding='utf-8')
    print('Packaged v1.1.1:',RELEASE)
    print('DLL SHA256:',sha)

if __name__=='__main__': main()
