"""Frozen backend entrypoint. No VJ.Tools imports."""
import os
from pathlib import Path
import sys
if sys.platform=='darwin':
    os.environ['SSL_CERT_FILE']=str(Path(os.environ['XZ_BUILDER_RESOURCES'])/'cacert.pem')
from builder.service import main
main()
