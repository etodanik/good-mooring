# Copyright (c) 2017-2026 The Forge Interactive Inc.
# 
# This file is part of The-Forge
# (see https://github.com/ConfettiFX/The-Forge).
# 
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
# 
#   http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

import subprocess
import os, sys
from utils import Platforms, OSTargets, ArchTargets, Features, Stages, ShaderBinary, fsl_assert, fsl_platform_assert, WaveopsFlags, clang_format_file
import struct
import platform
import analysis
from typing import List

fsl_basepath = os.path.dirname(__file__)

os_ispc_compiler = ""

if platform.system() == "Windows":
    os_ispc_compiler = "win/ispc.exe"
elif platform.system() == "Darwin":
    os_ispc_compiler = "osx/ispc"
elif platform.system() == "Linux":
    os_ispc_compiler = "linux/ispc"

_config = {
    Platforms.DIRECT3D12:     ('FSL_COMPILER_DXC', 'dxc.exe'),
    Platforms.VULKAN:         ('FSL_COMPILER_VK', 'glslangValidator.exe'),
    Platforms.ANDROID_VULKAN: ('FSL_COMPILER_VK', 'glslangValidator.exe'),
    Platforms.SWITCH:         ('FSL_COMPILER_VK', 'glslangValidator.exe'),
    Platforms.QUEST:          ('FSL_COMPILER_VK', 'glslangValidator.exe'),
    Platforms.MACOS:          ('FSL_COMPILER_MACOS', 'metal.exe'),
    Platforms.IOS:            ('FSL_COMPILER_IOS', 'metal.exe'),
    Platforms.ORBIS:          ('SCE_ORBIS_SDK_DIR', 'host_tools/bin/orbis-wave-psslc.exe'),
    Platforms.PROSPERO:       ('SCE_PROSPERO_SDK_DIR', 'host_tools/bin/prospero-wave-psslc.exe'),
    Platforms.XBOX:           ('GXDKLATEST', 'bin/XboxOne/dxc.exe'),
    Platforms.SCARLETT:       ('GXDKLATEST', 'bin/Scarlett/dxc.exe'),
    Platforms.ISPC:           ('FSL_COMPILER_ISPC', 'ispc'),
    Platforms.HOLOLENS2:      ('FSL_COMPILER_DXC', 'dxc.exe'),
}

def get_available_compilers():
    available = []
    for lang, compiler_path in _config.items():
        if get_compiler_from_env(*compiler_path, _assert=False):
            available += [lang.name]
    available += f'{fsl_basepath}/../ThirdParty/OpenSource/ISPCTextureCompressor/ISPC/{os_ispc_compiler}'
    return available

def get_status(bin, params):
    # For some reason calling subprocess.getstatusoutput on these platforms fails
    if sys.platform == "darwin" or sys.platform == "linux":
        result = subprocess.run([bin] + params, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return result.returncode, result.stderr.decode() + result.stdout.decode()
    else:
        return subprocess.getstatusoutput([bin] + params)

def get_compiler_from_env(varname, subpath = None, _assert=True):
    
    if os.name == 'posix' and 'metal.exe' in subpath:
        return 'xcrun'

    if not varname in os.environ:
        print('WARN: {} not in env vars'.format(varname))
        if _assert: assert False
        return None

    compiler = os.environ[varname]
    if subpath:
        if varname == 'FSL_COMPILER_VK' and sys.platform == "linux":
            compiler = os.environ['FSL_COMPILER_LINUX_VK']
            subpath = subpath.replace('.exe', '')
        compiler = os.path.join(compiler, subpath)
    if not os.path.exists(compiler):
        print('WARN: {} doesn\'t exist'.format(compiler))
        if _assert: assert False
        return None
    return os.path.abspath(compiler)

def util_shadertarget_dx(stage, features):
    level_dx = '_5_0' # dx11
    dx_levels = {
        Features.ATOMICS_64: '_6_6',
        Features.DYNAMIC_RESOURCES: '_6_6',
        Features.RAYTRACING: '_6_5',
        Features.VRS: '_6_4',
        Features.COOP_VECTORS: '_6_9',
        Features.MULTIVIEW: '_6_1',
    }
    if features is not None:
        level_dx = '_5_1' # xbox/dx12 default
        for feature in features:
            if feature in dx_levels:
                level_dx = max(level_dx, dx_levels[feature])
    if stage is Stages.GRAPH:
        level_dx = max(level_dx, '_6_8')
    if stage is Stages.ROOTSIG:
        level_dx = '_1_1'
    stage_dx = {
        Stages.VERT: 'vs',
        Stages.FRAG: 'ps',
        Stages.COMP: 'cs',
        Stages.GEOM: 'gs',
        Stages.GRAPH: 'lib',
        Stages.ROOTSIG: 'rootsig',
    }
    return stage_dx[stage] + level_dx

def util_spirv_target(features, binary: ShaderBinary):
    if Features.RAYTRACING in features:
        return 'spirv1.4'
    if WaveopsFlags.WAVE_OPS_NONE != binary.waveops_flags:
        return 'spirv1.3' # vulkan1.1 default
    return 'spirv1.0' # vulkan1.0 default

def util_shadertarget_metal(platform : Platforms, binary: ShaderBinary):
    if Features.RAYTRACING in binary.features or \
        Features.ATOMICS_64 in binary.features:
        return '2.4'
    if platform == Platforms.IOS:
        if WaveopsFlags.WAVE_OPS_ARITHMETIC_BIT in binary.waveops_flags or Features.PRIM_ID in binary.features:
            return '2.3'
    return '2.3' # default

def compile_binary(platform: Platforms, debug: bool, binary: ShaderBinary, src, dst, intermediate_dir: str, add_defines: List[str], analyze_offline: bool):
    
    # remove any existing binaries
    if os.path.exists(dst): os.remove(dst)

    if sys.platform == 'win32': # todo
        if src.startswith("//"):
            src = src.replace("//", "\\\\", 1)

        if dst.startswith("//"):
            dst = dst.replace("//", "\\\\", 1)
    
    if platform == Platforms.ISPC and not ("FSL_COMPILER_ISPC" in os.environ):
        bin = f'{fsl_basepath}/../ThirdParty/OpenSource/ISPCTextureCompressor/ISPC/{os_ispc_compiler}'
    else:
        bin = get_compiler_from_env(*_config[platform])
        

    class CompiledDerivative:
        def __init__(self):
            self.hash = 0
            self.code = b''

    compiled_derivatives = []
    for derivative_index, derivative in enumerate(binary.derivatives[platform]):
        
        compiled_filepath = f'{intermediate_dir}/{platform.name}/{binary.filename}'
        os.makedirs(os.path.dirname(compiled_filepath), exist_ok=True)
        params = []
        stages = []

        if add_defines:
            params += ['-D' + define for define in add_defines]
        params += ['-D' + define for define in derivative ]
        for ft in binary.features:
            params += [ f'-DFT_{ft.name}']

        if debug:
            params += ['-D_DEBUG']
        else:
            params += ['-DNDEBUG']

        if platform in [Platforms.VULKAN, Platforms.QUEST, Platforms.SWITCH, Platforms.ANDROID_VULKAN]:
            compiled_filepath += f'_{len(compiled_derivatives)}.spv'

            if debug: params += ['-g']
            else: params += ['-g0']
            params += ['-V', src, '-o', compiled_filepath, '-I'+fsl_basepath]
            params += ['-S', binary.stage.name.lower()]
            params += ['--target-env', util_spirv_target(binary.features, binary)]
            
            # spirv_opt = bin.replace('glslangValidator.exe', 'spirv-opt.exe')
            # spirv_opt_filepath = compiled_filepath + '_opt'
            # stages += [ (spirv_opt, ['-O', '-Os', compiled_filepath , '-o', spirv_opt_filepath]) ]
            # compiled_filepath = spirv_opt_filepath

            # spirv_dis = bin.replace('glslangValidator.exe', 'spirv-dis.exe')
            # spirv_filepath = src + f'_{len(compiled_derivatives)}.spv'
            # stages += [ (spirv_dis, [compiled_filepath, '-o ', spirv_filepath]) ]

        elif platform in [Platforms.DIRECT3D12, Platforms.HOLOLENS2]:
            compiled_filepath += f'_{len(compiled_derivatives)}.dxil'

            if debug: params += ['/Zi', '-Qembed_debug']
            else: params += ['-Qstrip_reflect']
            params += ['/T', util_shadertarget_dx(binary.stage, binary.features)]
            params += ['/I', fsl_basepath, '/Fo', compiled_filepath, src]
            params += ['-flegacy-macro-expansion']
            if Features.TYPES_16 in binary.features:
                params += ['-enable-16bit-types']
            if binary.stage is Stages.ROOTSIG:
                if 'compute.rootsig' in compiled_filepath:
                    params += ['/E', 'ComputeRootSignature']
                else:
                    params += ['/E', 'DefaultRootSignature']                
            else:
                params += ['-Qstrip_rootsignature']

        elif platform == Platforms.ORBIS:
            compiled_filepath += f'_{len(compiled_derivatives)}.bsh'

            params += ['-DGNM']
            params += ['-print-register-pressure', 'all']
            if debug: 
                params += ['-Od']
                params += ['-cache', '-cachedir', os.path.dirname(dst)]
            else: 
                params += ['-O4']
            
            shader_profile = {
                Stages.VERT: 'sce_vs_vs_orbis',
                Stages.FRAG: 'sce_ps_orbis',
                Stages.COMP: 'sce_cs_orbis',
                Stages.GEOM: 'sce_gs_orbis',
            }

            profile = shader_profile[binary.stage]

            if binary.stage == Stages.VERT:
                if Features.PRIM_ID in binary.features:
                    profile = 'sce_vs_es_orbis'

            if Features.INVARIANT in binary.features:
                params += ['-nofastmath']
            
            params += ['-profile', profile]
            params += ['-I'+fsl_basepath, '-o', compiled_filepath, src]

        elif platform == Platforms.PROSPERO:
            compiled_filepath += f'_{len(compiled_derivatives)}.bsh'

            params += ['-DGNM']
            params += ['-print-register-pressure', 'all']
            if debug:
                params += ['-Od']
                # This enables shader debugging at source level.
                params += ['-debug-info-path', os.path.dirname(dst)]
            else:
                params += ['-O4']
            shader_profile = {
                Stages.VERT: 'sce_vs_vs_prospero',
                Stages.FRAG: 'sce_ps_prospero',
                Stages.COMP: 'sce_cs_prospero',
                Stages.GEOM: 'sce_gs_prospero',
            }
            params += ['-profile', shader_profile[binary.stage]]
            params += ['-I' + fsl_basepath]
            if Features.RAYTRACING in binary.features:
                sdkDir = os.environ['SCE_PROSPERO_SDK_DIR']
                params += ['-I' + sdkDir + '\\target\\include_common\\']
                params += ['-llibScePsr.wal', '-L' + sdkDir + '\\target\\lib\\']

            if Features.INVARIANT in binary.features:
                params += ['-nofastmath']

            params += ['-o', compiled_filepath, src]

        elif platform in [Platforms.XBOX, Platforms.SCARLETT]:
            compiled_filepath += f'_{len(compiled_derivatives)}.dxil'

            import xbox as xbox_utils
            params += xbox_utils.compiler_args(binary.features)
            params += xbox_utils.include_dirs()
            params += xbox_utils.compiler_args_fsl_analysis(compiled_filepath.replace(".dxil", ".json"))
            if debug: params += ['/Zi', '-Qembed_debug']
            else: params += ['-Qstrip_reflect']
            params += ['/T', util_shadertarget_dx(binary.stage, binary.features)]
            params += ['/I', fsl_basepath]
            params += ['/Fo', compiled_filepath, src]
            
            if binary.stage is Stages.ROOTSIG:
                params += ['/E', 'DefaultRootSignature']
            else:
                params += ['-Qstrip_rootsignature']

        elif platform == Platforms.MACOS:
            compiled_filepath += f'_{len(compiled_derivatives)}.air'

            if os.name == 'nt':
                params += ['-I', fsl_basepath]
                params += ['-dD', '-o', compiled_filepath, src]
            else:
                params = '-sdk macosx metal '.split(' ') + params
                params += ['-I', fsl_basepath]
                params += ['-dD', src, '-o', compiled_filepath]

            if Features.INVARIANT in binary.features:
                params += ['-fpreserve-invariance']

            params += [f"-std=macos-metal{util_shadertarget_metal(platform, binary)}"]
            params += ['-Wno-unused-variable']

            if debug and os.name != 'nt':
                vv = subprocess.run(['xcrun', '--show-sdk-version'],
                    stdout=subprocess.PIPE).stdout.decode().split('.')[0]
                params += ['-gline-tables-only']
                if int(vv) >= 13:
                    params += ['-frecord-sources']

        elif platform == Platforms.IOS:
            compiled_filepath += f'_{len(compiled_derivatives)}.air'

            if os.name == 'nt':
                params += ['-I', fsl_basepath]
                params += ['-dD', '-mios-version-min=8.0', '-o', compiled_filepath, src]
            else:
                params = '-sdk iphoneos metal'.split(' ') + params
                params += ['-mios-version-min=11.0']
                params += ['-I', fsl_basepath]
                params += ['-dD', src, '-o', compiled_filepath]

            if Features.INVARIANT in binary.features:
                params += ['-fpreserve-invariance']

            params += [f"-std=ios-metal{util_shadertarget_metal(platform, binary)}"]
            params += ['-Wno-unused-variable']

            if debug and os.name != 'nt':
                vv = subprocess.run(['xcrun', '--show-sdk-version'],
                    stdout=subprocess.PIPE).stdout.decode().split('.')[0]
                params += ['-gline-tables-only']
                if int(vv) >= 16:
                    params += ['-frecord-sources']
        elif platform == Platforms.ISPC:
            compiled_filepath = f'{dst}.o'
            
            if binary.ispc_os_target == OSTargets.SWITCH:
                target_os = "--target-os=custom_linux"
            elif binary.ispc_os_target == OSTargets.XBOX:
                target_os = None
            else:
                target_os = f"--target-os={binary.ispc_os_target.name.lower()}"
            
            target_simd = ""
            if binary.ispc_arch_target == ArchTargets.ARM64:
                target_simd = f"--target={binary.ispc_target_arm.name.lower()}-i32x4"
            else:
                if "avx512" in binary.ispc_target_x86.name.lower():
                    target_simd = f"--target={binary.ispc_target_x86.name.lower()}-x32"
                elif "avx" in binary.ispc_target_x86.name.lower():
                    target_simd = f"--target={binary.ispc_target_x86.name.lower()}-i32x8"
                else:
                    target_simd = f"--target={binary.ispc_target_x86.name.lower()}-i32x4"

            print("Preprocessing", src)
            # Preprocess result, as we can get some very confusing errors in ISPC from the macros
            # subprocess.run([bin, src, "-E", "-o", f"{src}.i", target_simd] + ([target_os] if target_os else []))
            clang_format_file(f"{src}.i")
            
            params += [src]
            params += ["-o", compiled_filepath]
            params += [target_simd]
            params += ["--wno-perf"]
            # params += ["--nostdlib"]
            
            if target_os:
                params += [target_os]
            if binary.ispc_os_target == OSTargets.LINUX:
                params += ["--pic"]

        stages = [(bin, params)] + stages

        for bin, params in stages:
            print(f"{os.path.basename(binary.fsl_filepath)}: {binary.filename}")
            cp = subprocess.run([bin] + params, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            # We provide the src file in case the error doesn't have any file/line information, this way the user
            # can go to the FSL generated file and check the errors that the compiler might be reporting.
            #
            # error_filename should either be src or binary.filename, never binary.fsl_filepath because that's
            # just the ShaderList file and doesn't give any context to the user about what's the issue.
            error_filename = src
            if cp.returncode != 0:
                print(f"Return code: {cp.returncode} ({binary.filename})")
            cp_output = cp.stdout.decode()
            if (fsl_platform_assert(platform, cp.returncode == 0, error_filename, message=cp_output)):
                return

            if Features.ANALYZE in binary.features:
                analyze_offline = True

            # FSL analysis on supported platforms
            fsl_analysis = analysis.perform_fsl_analysis(binary.filename, bin, intermediate_dir, compiled_filepath, cp_output,
                                                         platform, binary.stage, 
                                                         util_shadertarget_dx(binary.stage, binary.features) if platform == Platforms.DIRECT3D12 else '',
                                                         analyze_offline)
            if fsl_assert(not fsl_analysis.uses_scratch(), error_filename, None, 'Scratch memory used! Indicates spilling.'):
                return
        
        if platform != Platforms.ISPC:
            with open(compiled_filepath, 'rb') as compiled_binary:
                cd = CompiledDerivative()
                cd.hash = derivative_index # hash(''.join(derivative)) #TODO If we use a hash it needs to be the same as in C++
                cd.code = compiled_binary.read()
                compiled_derivatives += [ cd ]

    if platform != Platforms.ISPC:
        with open(dst, 'wb') as combined_binary:

            # Needs to match:
            #
            # struct FSLMetadata
            # {
            #     uint32_t mUseMultiView;
            # };
            #
            # struct FSLHeader
            # {
            #     char mMagic[4];
            #     uint32_t mDerivativeCount;
            #     FSLMetadata mMetadata;
            # };

            num_derivatives = len(compiled_derivatives)
            combined_binary.write(struct.pack('=4sI', b'@FSL', num_derivatives) )
            combined_binary.write(struct.pack('=I', Features.MULTIVIEW in binary.features))
            combined_binary.write(struct.pack('=I', Features.ICB in binary.features))
            combined_binary.write(struct.pack('=4I', binary.num_threads[0], binary.num_threads[1], binary.num_threads[2], 0))
            combined_binary.write(struct.pack('=I', binary.output_types_mask))
            data_start = combined_binary.tell() + 24 * num_derivatives
            for cd in compiled_derivatives:
                combined_binary.write(struct.pack('=QQQ', cd.hash, data_start, len(cd.code)))
                data_start += len(cd.code)
            for cd in compiled_derivatives:
                combined_binary.write(cd.code)
    return 0
