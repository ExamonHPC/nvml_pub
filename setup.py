"""
Setup script for building the NVML GPM Python extension module
"""

from setuptools import setup, Extension
import os
import sys

# Find CUDA/NVML include and library paths
cuda_include_paths = [
    '/usr/local/cuda/include',
    '/usr/local/cuda/targets/x86_64-linux/include',
    '/opt/cuda/include',
    '/usr/include',
]

cuda_library_paths = [
    '/usr/lib64',
    '/usr/local/cuda/lib64',
    '/usr/local/cuda/targets/x86_64-linux/lib',
    '/opt/cuda/lib64',
    '/usr/lib/x86_64-linux-gnu',
]

# Filter to existing paths
include_dirs = [path for path in cuda_include_paths if os.path.exists(path)]
library_dirs = [path for path in cuda_library_paths if os.path.exists(path)]

if not include_dirs:
    print("Warning: CUDA include directory not found. Common paths:")
    for path in cuda_include_paths:
        print(f"  {path}")
    print("Extension may fail to build.")

if not library_dirs:
    print("Warning: CUDA library directory not found. Common paths:")
    for path in cuda_library_paths:
        print(f"  {path}")
    print("Extension may fail to build.")

# Define the extension module
nvml_gpm_extension = Extension(
    'nvml_gpm_extension',
    sources=['nvml_gpm_extension.c'],
    include_dirs=include_dirs,
    library_dirs=library_dirs,
    libraries=['nvidia-ml'],
    extra_compile_args=['-Wall'],
)

setup(
    name='nvml_gpm_extension',
    version='1.0.0',
    description='Python extension for NVML GPM metrics',
    author='EXAMON Team',
    ext_modules=[nvml_gpm_extension],
    python_requires='>=3.6',
)
