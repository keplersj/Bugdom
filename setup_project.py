import urllib.request
import os.path
import platform
import subprocess
import shutil
import tempfile
import sys
import os
from zipfile import ZipFile
from pathlib import Path
from dataclasses import dataclass

#----------------------------------------------------------------

libs_dir = os.path.abspath('extern')
cache_dir = os.path.abspath('cache')

sdl_ver = "2.0.14"

#----------------------------------------------------------------

@dataclass
class Project:
    dir_name: str
    gen_args: list
    build_configs: list

#----------------------------------------------------------------

def get_package(url):
    name = url[url.rfind('/')+1:]

    path = os.path.normpath(F"{cache_dir}/{name}")
    if os.path.exists(path):
        print("Not redownloading:", path)
    else:
        print(F"Downloading: {url}")
        os.makedirs(cache_dir, exist_ok=True)
        urllib.request.urlretrieve(url, path)
    return path

def call(cmd, **kwargs):
    print(">", ' '.join(cmd))
    #print(kwargs)
    try:
        result = subprocess.run(cmd, **kwargs)
        return result
    except subprocess.CalledProcessError as e:
        print("\x1b[1;31mSubprocess failed!\x1b[0m")
        raise e

def nuke_if_exists(path):
    if os.path.exists(path):
        print("==== Nuking", path)
        shutil.rmtree(path)

#----------------------------------------------------------------

# Make sure we're running from the correct directory...
if not os.path.exists('src/Enemies/Enemy_WorkerBee.c'):  # some file that's likely to be from the Bugdom source tree
    print("\x1b[1;31mSTOP - Please run this script from the root of the Bugdom source repo\x1b[0m")
    sys.exit(1)

system = platform.system()

projects = []
extra_build_args = []

if system == 'Windows':
    nuke_if_exists(F'{libs_dir}/SDL2-{sdl_ver}')

    print("==== Fetching SDL")
    sdl_zip_path = get_package(F"http://libsdl.org/release/SDL2-devel-{sdl_ver}-VC.zip")
    with ZipFile(sdl_zip_path, 'r') as zip:
        zip.extractall(path=libs_dir)

    projects.append(Project('build-msvc', ['-G', 'Visual Studio 16 2019', '-A', 'x64'], ['Release', 'Debug']))

elif system == 'Darwin':
    sdl2_framework = "SDL2.framework"
    sdl2_framework_target_path = F'{libs_dir}/{sdl2_framework}'

    nuke_if_exists(sdl2_framework_target_path)

    print("==== Fetching SDL")
    sdl_dmg_path = get_package(F"http://libsdl.org/release/SDL2-{sdl_ver}.dmg")

    # Mount the DMG and copy SDL2.framework to extern/
    with tempfile.TemporaryDirectory() as mount_point:
        call(['hdiutil', 'attach', sdl_dmg_path, '-mountpoint', mount_point, '-quiet'])
        shutil.copytree(F'{mount_point}/{sdl2_framework}', sdl2_framework_target_path, symlinks=True)
        call(['hdiutil', 'detach', mount_point, '-quiet'])

    projects.append(Project('build-xcode', ['-G', 'Xcode'], ['Release', 'Debug']))
    extra_build_args = ['--', '-quiet']

else:
    projects.append(Project('build-release', ['-DCMAKE_BUILD_TYPE=Release'], []))
    projects.append(Project('build-debug', ['-DCMAKE_BUILD_TYPE=Debug'], []))
    extra_build_args = ['--', '-j', str(len(os.sched_getaffinity(0)))]

#----------------------------------------------------------------
# Prepare config dirs

for proj in projects:
    nuke_if_exists(proj.dir_name)

    print(F"==== Preparing {proj.dir_name}")
    call(['cmake', '-S', '.', '-B', proj.dir_name] + proj.gen_args)

#----------------------------------------------------------------
# Build the game

print("==== Ready to build.")

proj = projects[0]
if input(F"Build project '{proj.dir_name}' now? (Y/N) ").upper() == 'Y':
    if proj.build_configs:
        config = proj.build_configs[0]
        print(F"==== Building the game:", proj.dir_name, config)
        call(['cmake', '--build', proj.dir_name, '--config', config] + extra_build_args)
    else:
        print(F"==== Building the game:", proj.dir_name)
        call(['cmake', '--build', proj.dir_name] + extra_build_args)