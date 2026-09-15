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

import sys, os, argparse, json, subprocess, tempfile, sqlite3, hashlib, time, random
from utils import Platforms, Stages
from enum import Enum
import pickle

fsl_root = os.path.sep.join(os.path.abspath(__file__).split(os.path.sep)[:-1])
forge_root = os.path.dirname(os.path.dirname(os.path.dirname(fsl_root)))
rga_executable = os.path.sep.join((forge_root, 'Tools', 'rga', 'rga.exe'))
malioc_executable = os.path.sep.join((forge_root, 'Tools', 'mali_offline_compiler', 'malioc.exe'))

platforms_immediate = [Platforms.ORBIS, Platforms.PROSPERO, Platforms.XBOX, Platforms.SCARLETT]
platforms_offline_compiler = [Platforms.DIRECT3D12, Platforms.ANDROID_VULKAN, Platforms.SWITCH, Platforms.QUEST, Platforms.HOLOLENS2]
platforms_use_cache = [Platforms.DIRECT3D12, Platforms.ANDROID_VULKAN, Platforms.SWITCH]

class CacheContext:
    def __init__(self, connection: sqlite3.Connection, cursor: sqlite3.Cursor):
        self.connection = connection
        self.cursor = cursor

class AnalysisResult:
    class AnalysisData:
        def __init__(self):
            self.vgpr_live = -1
            self.vgpr_requested = -1
            self.vgpr_available = -1

            self.sgpr_live = -1
            self.sgpr_requested = -1
            self.sgpr_available = -1

            self.lds_used = -1
            self.scratch_used = -1
            
            self.occupancy = -1
            self.occupancy_max = -1

            self.raw = ''

        def to_obj_json(self):
            return {
                'occupancy': self.occupancy,
                'vgpr': self.vgpr_requested if self.vgpr_requested != -1 else self.vgpr_live,
                'sgpr': self.sgpr_requested if self.sgpr_requested != -1 else self.sgpr_live,
                'lds': self.lds_used,
                'scratch': self.scratch_used
            }

    def __init__(self):
        # Result data per separate 'target'
        # Target may depend on GPU/architecture/variant
        self.target_data = {}

    def uses_scratch(self):
        for data in self.target_data.values():
            if data.scratch_used > 0:
                return True
        return False

    def to_obj_json(self, fsl_filename: str, compiler_path: str, compiled_filename: str, 
                        intermediate_path: str, platform: Platforms, stage: Stages,
                        shadertarget_dx: str, allow_offline: bool = False):
        result = {
            'fsl_filename': fsl_filename,
            'compiler_path': compiler_path,
            'compiled_filename': compiled_filename,
            'intermediate_path': intermediate_path,
            'platform': platform.name,
            'stage': stage.name,
            'shadertarget_dx': shadertarget_dx,
            'allow_offline': allow_offline
        }
        results_obj = {}
        for target, data in zip(self.target_data.keys(), self.target_data.values()):
            results_obj[target] = data.to_obj_json()
        result['results'] = results_obj
        return result

    def __str__(self):
        out_str = ''

        def str_if_value(value, str):
            return str if value != -1 else ''

        for target, data in zip(self.target_data.keys(), self.target_data.values()):
            out_str += (f"\t({target})")
            if data.occupancy != -1:
                out_str += f" Occupancy: {data.occupancy}"
                out_str += str_if_value(data.occupancy_max, f"/{data.occupancy_max}")
            if data.vgpr_live != -1 or data.vgpr_requested != -1:
                out_str += f" VGPR: "
                out_str += str_if_value(data.vgpr_requested, f"{data.vgpr_requested}")
                out_str += str_if_value(data.vgpr_live, f"({data.vgpr_live})")
                out_str += str_if_value(data.vgpr_available, f"/{data.vgpr_available}")
            if data.sgpr_live != -1 or data.sgpr_requested != -1:
                out_str += f" SGPR: "
                out_str += str_if_value(data.sgpr_requested, f"{data.sgpr_requested}")
                out_str += str_if_value(data.sgpr_live, f"({data.sgpr_live})")
                out_str += str_if_value(data.sgpr_available, f"/{data.sgpr_available}")
            out_str += str_if_value(data.lds_used, f" LDS: {data.lds_used}")
            out_str += str_if_value(data.scratch_used, f" Scratch: {data.scratch_used}")
            out_str += "\n"
        return out_str

class LiveRegType(Enum):
        ALL = 0,
        VGPR = 1,
        SGPR = 2

# This is for an ASCII live register visualization
def parse_livereg(livereg: str, livereg_type: LiveRegType = LiveRegType.ALL) -> AnalysisResult.AnalysisData:
    result = AnalysisResult.AnalysisData()
    result.raw = livereg

    for line in livereg.splitlines():
        if '|' in line:
            # Remove disassembly output after table
            table = line.split('*/')[0]

            # Since this is a fixed format, we assume the first numeric entry is VGPR, second is SGPR
            first_numeric = True
            for field in table.split('|'):
                field_trimmed = field.replace(' ', '')
                if len(field_trimmed) > 0 and field_trimmed.isnumeric():
                    if first_numeric:
                        if livereg_type == LiveRegType.ALL:
                            result.vgpr_live = max(result.vgpr_live, int(field_trimmed))
                        first_numeric = False
                    else:
                        if livereg_type in [LiveRegType.ALL, LiveRegType.SGPR]:
                            result.sgpr_live = max(result.sgpr_live, int(field_trimmed))
                        else:
                            result.vgpr_live = max(result.vgpr_live, int(field_trimmed))
     
    return result

# This is for shader statistics output
def parse_stats(stats: str) -> AnalysisResult.AnalysisData:
    result = AnalysisResult.AnalysisData()
    result.raw = stats

    for line in stats.splitlines():
        split = line.split('=')
        left = split[0]
        value = -1
        if len(split) > 1 and split[1].replace(" ", "").isnumeric():
            value = int(split[1].replace(" ", ""))

        if 'resourceUsage.numUsedVgprs' in left:
            result.vgpr_requested = value
        elif 'resourceUsage.numUsedSgprs' in left:
            result.sgpr_requested = value
        elif 'resourceUsage.ldsUsageSizeInBytes' in left:
            result.lds_used = value
        elif 'resourceUsage.scratchMemUsageInBytes' in left:
            result.scratch_used = value
        elif 'numAvailableVgprs' in left:
            result.vgpr_available = value
        elif 'numAvailableSgprs' in left:
            result.sgpr_available = value

    return result

def analyze_rga(compiler_path: str, compiled_filename: str, stage: Stages, target_dx: str) -> str:
    result = AnalysisResult()

    targets = []
    # Full ASICs list: rga -s dx12 -l
    targets = ['gfx1010', 'gfx1030', 'gfx1100', 'gfx1150', 'gfx1201']

    # TODO: Occupancy?

    target_params = []
    for target in targets:
        target_params += ['-c', target]

    blob_params = []
    model_params = []
    file_modifier = []
    if stage == Stages.VERT:
        blob_params += ['--vs-blob', compiled_filename]
        model_params += ['--vs-model', target_dx]
        file_modifier.append('vert')
        file_modifier.append('vertex')
    elif stage == Stages.GEOM:
        blob_params += ['--gs-blob', compiled_filename]
        model_params += ['--gs-model', target_dx]
        file_modifier.append('geom')
        file_modifier.append('geometry')
    elif stage == Stages.FRAG:
        blob_params += ['--ps-blob', compiled_filename]
        model_params += ['--ps-model', target_dx]
        file_modifier.append('pixel')
        file_modifier.append('pixel')
    elif stage == Stages.COMP:
        blob_params += ['--cs-blob', compiled_filename]
        model_params += ['--cs-model', target_dx]
        file_modifier.append('comp')
        file_modifier.append('compute')
    
    if len(blob_params) == 0:
        print(f"Unsupported shader stage: {stage}")
        return result

    with tempfile.TemporaryDirectory() as tmpdir:
        os.chdir(tmpdir)

        proc = subprocess.run([rga_executable, '-s', 'dx12', 
                        '--dxc', os.path.abspath(compiler_path).replace(os.path.basename(compiler_path), ""), 
                        '--offline', '-a', 'out', '--isa', 'out', '--livereg', 'out', '--livereg-sgpr', 'out'] + 
                        target_params + blob_params + model_params, stdout=subprocess.PIPE)

        # Read resulting files for each target and parse them
        for target in targets:
            filename = f'{target}_out_{file_modifier[0]}.stats'
            if os.path.isfile(filename):
                with open(filename, 'r') as stats:
                    result.target_data[target] = parse_stats(stats.read())
            else:
                print(f"Running RGA failed for {compiled_filename} (target {target})")
                print(proc.stdout.decode('utf-8'))
            
            filename = f'{target}_out_{file_modifier[1]}.livereg'
            if os.path.isfile(filename):
                with open(filename, 'r') as livereg:
                    livereg_result = parse_livereg(livereg.read(), LiveRegType.VGPR)
                    result.target_data[target].vgpr_live = livereg_result.vgpr_live
                    result.target_data[target].raw += livereg_result.raw

            filename = f'{target}_out_{file_modifier[1]}.livereg_sgpr'
            if os.path.isfile(filename):
                with open(filename, 'r') as livereg:
                    livereg_result = parse_livereg(livereg.read(), LiveRegType.SGPR)
                    result.target_data[target].sgpr_live = livereg_result.sgpr_live
                    result.target_data[target].raw += livereg_result.raw
        
        os.chdir(fsl_root)
    
    return result

def analyze_mali(compiled_filename: str) -> AnalysisResult:
    result = AnalysisResult()
   
    # malioc --list
    targets = ['Mali-G31', 'Mali-G57', 'Mali-G620']

    for core in targets:
        data = AnalysisResult.AnalysisData()
        data.occupancy_max = 100 # Percentage
        
        proc = subprocess.run([malioc_executable, '-c', core, '--format', 'json', '--spirv', '-d', compiled_filename], stdout=subprocess.PIPE)

        report = json.loads(proc.stdout)
        shader = report['shaders'][0]
        if 'errors' not in shader:
            properties = shader['variants'][0]['properties']
            for prop in properties:
                if prop['name'] == 'work_registers_used': # TODO: x% used at y% occupancy (format text)
                    data.vgpr_requested = prop['value']
                elif prop['name'] == 'thread_occupancy':
                    data.occupancy = prop['value']
                elif prop['name'] == 'stack_spill_bytes':
                    data.scratch_used = prop['value']
        
            data.raw = json.dumps(report, indent=2)
            result.target_data[core] = data

    return result

def analyze_adreno(stage: Stages, compiled_filename: str) -> AnalysisResult:
    result = AnalysisResult()

    aoc_path_options = [os.path.join(os.environ['ADRENO_OFFLINE_COMPILER_INSTALLDIR'] if 'ADRENO_OFFLINE_COMPILER_INSTALLDIR' \
                                     in os.environ else 'C:\\Program Files\\Qualcomm\\Adreno Offline Compiler', 'aoc.exe'),
                        os.path.join(forge_root, "Jenkins", "aoc.exe")]

    aoc_exe = None
    for option in aoc_path_options:
        if os.path.exists(option):
            aoc_exe = option
            break

    if aoc_exe is None:
        print("WARNING: Adreno Offline Compiler not found!")
        return result

    stage_params = {
        Stages.VERT: '-vs',
        Stages.FRAG: '-fs',
        Stages.GEOM: '-gs',
        Stages.COMP: '-cs'
    }

    if stage not in stage_params:
        print(f"Unsupported shader stage: {stage}")
        return result

    targets = ['a650', 'a730', 'a830']
    for target in targets:
        data = AnalysisResult.AnalysisData()
        
        proc = subprocess.run([aoc_exe, '-api=vulkan', '-dump=all', f'-arch={target}', stage_params[stage], compiled_filename], stdout=subprocess.PIPE)
        aoc_out = proc.stdout.decode('utf-8')
        data.raw = aoc_out

        for line in aoc_out.splitlines():
            if 'Overall register footprint per shader instance' in line:
                value = int(line.split(':')[1].replace(' ', ''))
                data.vgpr_requested = max(value, data.vgpr_requested)
            if 'Scratch memory usage per shader instanc' in line:
                value = int(line.split(':')[1].replace(' ', ''))
                data.scratch_used = max(value, data.scratch_used)

        result.target_data[target] = data

    return result

def analyze_quest(fsl_filename: str, intermediate_path: str, project_name: str):
    result = AnalysisResult()

    stats_filename = f'{os.path.basename(fsl_filename)}.stats'
    stats_path = os.path.join(os.path.dirname(intermediate_path), 'libs', 'files', 'Screenshots', stats_filename)

    if not os.path.exists(stats_path):
        # May be a shared shader file (from OS) that was copied, search for the stats file
        search_path = os.path.dirname(os.path.dirname(os.path.dirname(intermediate_path)))
        for cur, _, files in os.walk(search_path):
            if project_name in cur and stats_filename in files:
                stats_path = os.path.join(cur, stats_filename)
                break

    if os.path.exists(stats_path):
        with open(stats_path, 'r') as file:
            data = AnalysisResult.AnalysisData()
            content = file.read()
            for line in content.splitlines():
                if 'Overall register footprint per shader instance' in line:
                    data.vgpr_requested = max(data.vgpr_requested, int(line.split(';')[1]))
                if 'Scratch memory usage per shader instance' in line:
                    data.scratch_used = max(data.scratch_used, int(line.split(';')[1]))

            data.raw = content
            result.target_data['default'] = data
    else:
        print(f"Warning: No .stats file for {os.path.basename(fsl_filename)}")
    
    return result

def init_cache() -> CacheContext:
    # Find or create sqlite db in global dir
    cache_path = ''
    if sys.platform.startswith('win'):
        cache_path = os.getenv('LOCALAPPDATA')
    elif sys.platform.startswith('darwin'):
        cache_path = '~/Library/Application Support'
    else:
        cache_path = os.getenv('XDG_DATA_HOME', '~/.local/share')
    cache_path = os.path.join(cache_path, 'FSLAnalysis')
    os.makedirs(cache_path, exist_ok=True)

    connection = sqlite3.connect(os.path.join(cache_path, 'cache.sqlite'), isolation_level=None)
    cursor = connection.cursor()
    context = CacheContext(connection, cursor)

    try:
        cursor.execute('pragma journal_mode=wal;')
    except:
        pass

    create_table_sql = \
        'CREATE TABLE AnalysisResult(' \
            'FileHash BLOB, ' \
            'Platform INT, ' \
            'AnalysisHash BLOB, ' \
            'LastUsed DATETIME, ' \
            'Result BLOB,' \
            'PRIMARY KEY (FileHash, Platform, AnalysisHash)' \
        ')'

    query = context.cursor.execute("SELECT sql FROM sqlite_master WHERE name='AnalysisResult'")
    result = query.fetchone()
    if result is None or result[0] != create_table_sql:
        context.cursor.execute('DROP TABLE IF EXISTS AnalysisResult')
        try:
            context.cursor.execute(create_table_sql)
        except:
            pass
    else:
        try:
            # Drop old entries
            context.cursor.execute("DELETE FROM AnalysisResult WHERE LastUsed < DATETIME('now', '-7 days')")
        except:
            print("Warning: Unable to delete old cache entries")
    context.connection.commit()

    return context

def create_hash(file_path: str) -> bytes:
    with open(file_path, 'rb') as file:
        # From 3.11's file_digest
        digestobj = hashlib.md5()
        buf = bytearray(2**18)
        view = memoryview(buf)
        while True:
            size = file.readinto(buf)
            if size == 0:
                break
            digestobj.update(view[:size])

        return digestobj.digest()

def execute_commit_handle_locked(context: CacheContext, sql, parameters, max_retries):
    for _ in range(max_retries):
        retry = False
        try:
            context.cursor.execute(sql, parameters)
            context.connection.commit()
        except sqlite3.OperationalError as e:
            # May be multiple simultaneous writers
            retry = True
        except:
            pass
        
        if retry:
            # Stagger retries 0-1 seconds
            time.sleep(random.random())
        else:
            return


def query_cache(context: CacheContext, file_hash: bytes, platform: Platforms, analysis_hash: bytes):
    result = None
    try:
        query = context.cursor.execute('SELECT Result FROM AnalysisResult WHERE FileHash=? AND Platform=? AND AnalysisHash=?', 
                                       (file_hash, platform.value, analysis_hash))
        result = query.fetchone()
    except:
        pass

    if result is not None:
        # Update LastUsed
        execute_commit_handle_locked(context, 
                                     "UPDATE AnalysisResult SET LastUsed=DATETIME('now') WHERE FileHash=? AND Platform=? AND AnalysisHash=?", 
                                     (file_hash, platform.value, analysis_hash), 10)

        return result[0]

    return None

def add_cache(context: CacheContext, file_hash: bytes, platform: Platforms, analysis_hash: bytes, result_serialized: bytes):
    execute_commit_handle_locked(context, 
                                 "INSERT OR IGNORE INTO AnalysisResult VALUES(?, ?, ?, DATETIME('now'), ?)", 
                                 (file_hash, platform.value, analysis_hash, result_serialized), 10)

def exit_cache(context: CacheContext):
    if context is not None:
        context.connection.close()

def write_output(analysis_result: AnalysisResult, fsl_filename: str, compiler_path: str, intermediate_path: str,
                 compiled_filename: str, compilation_stdout: str, platform: Platforms, stage: Stages,
                 shadertarget_dx: str, allow_offline: bool):
    compiled_name = os.path.basename(compiled_filename)
    json_path = os.path.join(intermediate_path, 'FSLAnalysis', f'{compiled_name}.json')
    os.makedirs(os.path.dirname(json_path), exist_ok=True)

    with open(json_path, 'w') as out_file:
        json.dump(analysis_result.to_obj_json(fsl_filename, compiler_path, compiled_filename, 
                compilation_stdout, platform, stage, shadertarget_dx, allow_offline), out_file)
    # Write raw output from tools for developers to take a closer look at
    for target, data in zip(analysis_result.target_data.keys(), analysis_result.target_data.values()):
        if len(data.raw) > 0:
            with open(f'{os.path.dirname(json_path)}/{compiled_name}_{target}.txt', 'w', newline='') as out_file:
                out_file.write(data.raw)

def perform_fsl_analysis(fsl_filename: str, compiler_path: str, intermediate_path: str, compiled_filename: str, 
                         compilation_stdout: str, platform: Platforms, stage: Stages,
                         shadertarget_dx: str, allow_offline: bool = False, project_name: str = '') -> AnalysisResult:
    analysis_result = AnalysisResult()

    if platform not in platforms_immediate and platform not in platforms_offline_compiler:
        return analysis_result

    # Don't even write a file for these
    if ".rootsig" in compiled_filename or ".graph" in compiled_filename:
        return analysis_result

    if allow_offline or platform not in platforms_offline_compiler:
        cache = None
        analysis_hash = None
        compiled_hash = None
        cache_entry = None
        if platform in platforms_use_cache:
            cache = init_cache()
            analysis_hash = create_hash(__file__)
            compiled_hash = create_hash(compiled_filename)
            cache_entry = query_cache(cache, compiled_hash, platform, analysis_hash)
            if cache_entry is not None:
                print(f"Using cached analysis result for {fsl_filename}")
                analysis_result = pickle.loads(cache_entry)

        if cache_entry is None:
            if platform == Platforms.ORBIS:
                import orbis as orbis_utils
                analysis_result = orbis_utils.analyze(compiler_path, compiled_filename, compilation_stdout)
            elif platform == Platforms.PROSPERO:
                import prospero as prospero_utils
                analysis_result = prospero_utils.analyze(compiler_path, compiled_filename, compilation_stdout)
            elif platform in [Platforms.XBOX, Platforms.SCARLETT]:
                import xbox as xbox_utils
                analysis_result = xbox_utils.analyze(compiled_filename, platform)
            elif platform == Platforms.ANDROID_VULKAN:
                analysis_result = analyze_mali(compiled_filename)

                result_adreno = analyze_adreno(stage, compiled_filename)
                for target, data in result_adreno.target_data.items():
                    analysis_result.target_data[target] = data
            elif platform == Platforms.DIRECT3D12:
                analysis_result = analyze_rga(compiler_path, compiled_filename, stage, shadertarget_dx)
            elif platform == Platforms.SWITCH:
                import switch as switch_utils
                analysis_result = switch_utils.analyze(stage, compiled_filename)
            elif platform == Platforms.QUEST:
                analysis_result = analyze_quest(fsl_filename, intermediate_path, project_name)

            if platform in platforms_use_cache:
                add_cache(cache, compiled_hash, platform, analysis_hash, pickle.dumps(analysis_result))
        
        exit_cache(cache)

    # Report result
    if len(analysis_result.target_data.keys()) > 0:
        print(f"Analysis for {fsl_filename}:\n{analysis_result}")

    write_output(analysis_result, fsl_filename, compiler_path, intermediate_path, compiled_filename, 
                intermediate_path, platform, stage, shadertarget_dx, allow_offline)

    return analysis_result

# The script can be invoked to explicitly perform analysis 
# only for the platforms where it is done using an offline compiler
def perform_offline_analysis():
    parser = argparse.ArgumentParser()
    parser.add_argument('json_path', help='Path to json file created during shader compilation')
    parser.add_argument('project_name', help='Project name')
    args = parser.parse_args()
    
    with open(args.json_path, 'r') as file:
        prior_out = json.load(file)

        platform = Platforms[prior_out['platform']]
        stage = Stages[prior_out['stage']]
        intermediate_path = os.path.dirname(os.path.dirname(args.json_path))
        
        # Done if offline analysis was allowed during build. Quest is a special case, an 
        # earlier test run may have failed to produce the required .stats files.
        if prior_out['allow_offline'] and platform != Platforms.QUEST:
            return

        if platform in platforms_offline_compiler:
            perform_fsl_analysis(prior_out['fsl_filename'], prior_out['compiler_path'], intermediate_path,
                                    prior_out['compiled_filename'], '', 
                                    platform, stage, prior_out['shadertarget_dx'], True, args.project_name)

if __name__ == '__main__':
    from fsl import fsl_switch_root
    sys.path.append(fsl_switch_root)

    perform_offline_analysis()