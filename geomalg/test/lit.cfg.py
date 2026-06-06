# -*- Python -*-

import lit.formats
from lit.llvm import llvm_config

config.name = 'geomalg'
config.test_source_root = os.path.dirname(__file__)
config.test_format = lit.formats.ShTest()
config.suffixes = ['.scm', '.cpp', '.mlir']
config.excludes = [
    "Inputs",
    "CMakeLists.txt",
    "README.txt",
    "LICENSE.txt",
]
config.substitutions.append(('%schir_module_path', config.schir_module_path))
config.substitutions.append(('%geomalg_module_path', config.geomalg_module_path))
config.substitutions.append(('%geomalg_lib_dir', config.geomalg_lib_dir))

llvm_config.with_environment('LD_LIBRARY_PATH', config.geomalg_lib_dir, append_path=True)
llvm_config.with_environment('PATH', config.geomalg_tools_dir, append_path=True)
llvm_config.with_environment('PATH', config.schir_tools_dir, append_path=True)
llvm_config.with_environment('PATH', config.llvm_tools_dir, append_path=True)
