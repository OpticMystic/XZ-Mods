from pathlib import Path
import struct,subprocess,argparse
from verify_browser import fixture,page,string
r=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
d=bytearray(fixture());struct.pack_into('<I',d,8,3);struct.pack_into('<4I',d,60,2,0,3,3)
struct.pack_into('<I',d,8192+40+0x38,14225);struct.pack_into('<I',d,8192+40+0x48,123)
artist=bytearray(280);struct.pack_into('<H',artist,0,0x64);struct.pack_into('<I',artist,4,123);struct.pack_into('<H',artist,10,280)
artist+=string('\u65e5\u672c');d+=page(3,2,[artist]);f=a.output/'metadata.pdb';f.write_bytes(d)
binary=a.output/'catalog-metadata';subprocess.run(['gcc','-O1','-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(r/'catalog.c'),str(r/'music.c'),str(r/'test_catalog_metadata.c'),str(r.parent/'audio/vendor/sha256/sha256.c'),'-lm','-o',str(binary)],check=True)
subprocess.run([str(binary),str(f)],check=True)
row=a.output/'browser-metadata';subprocess.run(['gcc','-O1','-std=c11','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(r/'test_browser_metadata.c'),str(r/'catalog.c'),str(r/'music.c'),str(r.parent/'audio/vendor/sha256/sha256.c'),'-pthread','-lm','-o',str(row)],check=True)
subprocess.run([str(row),str(f)],check=True)
