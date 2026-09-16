#!/usr/bin/env python3
"""Mooring's local host for Forge ReloadClient/ReloadServer."""
from pathlib import Path
import json
import os
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Common/Tools/ReloadServer'))
import ReloadServer as forge
from shader_previews import generate, layout_signature

recompile_fsl = forge.recompile_shaders


def recompile(intermediate, platform):
    try:
        configuration = json.loads((intermediate / 'mooring-reload.json').read_text())
        output = Path(configuration['output'])
        if layout_signature(ROOT) != configuration['layout']:
            return 'Shared shader resource layout changed. Rebuild and restart the app before reloading shaders.'
        generate(ROOT, output)
        result = recompile_fsl(intermediate, platform)
        if isinstance(result, str):
            return result
        # Always publish a complete successful generation. A failed compile may
        # already have replaced some binaries on disk; a timestamp delta alone
        # would lose those edits on the next successful request.
        binaries = output / 'shaders/bin'
        paths = [path.relative_to(binaries) for path in sorted((binaries / platform).glob('*.metal'))]
        paths.append(Path('shader-lab.json'))
        return forge.read_shaders_from_disk(paths, binaries)
    except Exception as error:
        return str(error)


if __name__ == '__main__':
    parent = int(os.environ.get('MOORING_RELOAD_PARENT', '0'))
    if parent:
        def watch_parent():
            while os.getppid() == parent:
                time.sleep(1)
            os._exit(0)
        threading.Thread(target=watch_parent, daemon=True).start()
    forge.recompile_shaders = recompile
    # This host serves only the local development app. Forge owns the protocol.
    original_socket = forge.socket.socket

    class LocalSocket(original_socket):
        def bind(self, address):
            return super().bind(('127.0.0.1', address[1]))

    forge.socket.socket = LocalSocket
    forge.main()
