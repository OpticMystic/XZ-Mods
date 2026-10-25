"""Build the report-only TLS helper from Mbed TLS 3.6.7 source.

Supply the extracted official release with --mbedtls. Its archive SHA-256 is
a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6.
The --test build is for local fixtures only and must never be packaged.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

SOURCES = "aes asn1parse asn1write base64 bignum bignum_core bignum_mod bignum_mod_raw block_cipher chacha20 chachapoly cipher cipher_wrap constant_time ctr_drbg ecdh ecdsa ecp ecp_curves ecp_curves_new entropy gcm md mps_reader mps_trace oid pem pk pk_ecc pk_wrap pkparse platform platform_util poly1305 rsa rsa_alt_helpers sha1 sha256 sha512 ssl_ciphersuites ssl_client ssl_msg ssl_tls ssl_tls12_client x509 x509_crt".split()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--zig',required=True)
    p.add_argument('--mbedtls',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--test',action='store_true')
    a=p.parse_args();root=Path(__file__).resolve().parent
    a.output.mkdir(parents=True,exist_ok=False)
    common=[a.zig,'cc','-target','arm-linux-gnueabi.2.13','-mcpu=cortex_a9','-O2','-std=c11',f'-DMBEDTLS_CONFIG_FILE="{(root/"mbedtls_xz_config.h").as_posix()}"',f'-I{(a.mbedtls/"include").as_posix()}']
    def compile_one(name):
        obj=a.output/(name+'.o')
        subprocess.run([*common,f'-I{(a.mbedtls/"library").as_posix()}','-c',str(a.mbedtls/'library'/(name+'.c')),'-o',str(obj)],check=True)
        return obj
    with ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile_one,SOURCES))
    binary=a.output/('xz-support-test' if a.test else 'xz-support')
    subprocess.run([*common,'-Wall','-Wextra','-Werror','-s',*(['-DXZ_SUPPORT_TEST'] if a.test else []),str(root/'support_tool.c'),*[str(x) for x in objects],'-o',str(binary)],check=True)
    (a.output/'build.json').write_text(json.dumps({'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'test_only':a.test,'mbedtls':'3.6.7'},indent=2)+'\n')

if __name__=='__main__':main()
