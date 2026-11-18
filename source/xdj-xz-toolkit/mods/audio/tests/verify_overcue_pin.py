"""Exercise the real OverCue decoder after every source stem file is removed."""
from pathlib import Path
import subprocess,tempfile
from verify_overcue import fixture
from flac_fixture import repack
HERE=Path(__file__).resolve().parent;ROOT=HERE.parent
with tempfile.TemporaryDirectory(prefix='xz-oc-pin-') as folder:
 root=Path(folder);binary=root/'pin'
 sources=[ROOT/'overcue_file.c',ROOT/'stem_decode.c',ROOT/'vendor/miniz/miniz_tinfl.c',ROOT/'vendor/sha256/sha256.c',ROOT.parent/'ram/pin.c',HERE/'test_overcue_pin.c']
 subprocess.run(['cc','-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all',*map(str,sources),'-lm','-o',str(binary)],check=True)
 for codec in ('zlib','flac'):
  source,bundle,_,_=fixture(root/codec)
  if codec=='flac':repack(source,bundle)
  subprocess.run([str(binary),str(source)],check=True)
