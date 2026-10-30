# -*- Python -*-

import resource
import subprocess
import sys

import lit.formats
from lit.llvm import llvm_config

# Limit the address space of each test process.
_as_limit = 3 * 1024 * 1024 * 1024
resource.setrlimit(resource.RLIMIT_AS,
                   (_as_limit, resource.getrlimit(resource.RLIMIT_AS)[1]))

config.name = 'nbdl'
config.test_source_root = os.path.dirname(__file__)
config.test_format = lit.formats.ShTest()
config.suffixes = ['.cpp', '.mlir']
config.excludes = [
    "Inputs",
    "CMakeLists.txt",
    "README.txt",
    "LICENSE.txt",
]
config.substitutions.append(('%schir_module_path', config.schir_module_path))
config.substitutions.append(('%nbdl_module_path', config.nbdl_module_path))
config.substitutions.append(('%geomalg_module_path', config.geomalg_module_path))

# TODO Find SDL3 in CMake and pass its paths via lit.site.cfg.py instead
#      of relying on the default paths here and in the tests' -lSDL3.
# Probe for a GPU device that SDL3 can create without a window.
_sdl3_gpu_probe = """
import ctypes, sys
SDL = ctypes.CDLL("libSDL3.so.0")
SDL.SDL_Init.restype = ctypes.c_bool
SDL.SDL_CreateGPUDevice.restype = ctypes.c_void_p
SDL.SDL_DestroyGPUDevice.argtypes = [ctypes.c_void_p]
SDL.SDL_SetHint(b"SDL_VIDEO_DRIVER", b"offscreen")
if not SDL.SDL_Init(ctypes.c_uint32(0x20)):
    sys.exit(1)
Device = SDL.SDL_CreateGPUDevice(ctypes.c_uint32(2), ctypes.c_bool(False), None)
if not Device:
    sys.exit(1)
SDL.SDL_DestroyGPUDevice(Device)
SDL.SDL_Quit()
"""
try:
    if subprocess.run([sys.executable, '-c', _sdl3_gpu_probe],
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                      timeout=30).returncode == 0:
        config.available_features.add('sdl3-gpu')
except subprocess.TimeoutExpired:
    pass

llvm_config.with_environment('LD_LIBRARY_PATH', config.nbdl_lib_dir, append_path=True)
llvm_config.with_environment('PATH', config.schir_tools_dir, append_path=True)
llvm_config.with_environment('PATH', config.llvm_tools_dir, append_path=True)
