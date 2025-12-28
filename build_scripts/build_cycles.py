import os
import sys
from pathlib import Path
from sys import platform
import argparse
import shutil
import subprocess
import config

parser = argparse.ArgumentParser(description='cycles build script', allow_abbrev=False, formatter_class=argparse.ArgumentDefaultsHelpFormatter, epilog="")
parser.add_argument('--cycles-arch', help='Kernels will be built for the specified architecture(s). Possible options are sm_30 sm_35 sm_37 sm_50 sm_52 sm_60 sm_61 sm_70 sm_75 sm_86 sm_89 sm_120 compute_75 and "all". If set to "all", all possible kernels will be built.', default='all')
args,unknown = parser.parse_known_args()
args = vars(args)

cycles_arch = args["cycles_arch"]

# Required so we can import relative packages
sys.path.insert(0, os.path.dirname(__file__))

from third_party import cycles
cycles.main(cycles_arch)

additional_build_targets.append("UniRender_cycles")
