#!/usr/bin/env python3
"""Tests for preview discovery, source mapping, and reload publication."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from shader_previews import generate, layout_signature
import reload_shaders


class ShaderPreviewsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.sources = self.root / 'Mooring/Shaders'
        self.sources.mkdir(parents=True)
        self.output = self.root / 'generated'
        (self.sources / 'Water.srt.h').write_text('// Shared resource layout\n')
        (self.sources / 'Ocean.list').write_text('#frag water.frag\n#include "Water.frag.fsl"\n#end\n')
        (self.sources / 'Water.frag.fsl').write_text('float4 PS_MAIN() {\n SHADER_PREVIEW("Foam", pattern);\n}\n')
        self.preview = self.sources / 'Water.preview.fsl'
        self.preview.write_text('SHADER_PREVIEW_2D(previewHash, "Hash", PREVIEW_INTEGER_GRID);\n')

    def manifest(self):
        generate(self.root, self.output)
        return json.loads((self.output / 'shaders/bin/shader-lab.json').read_text())

    def test_registration_and_stable_probe_ids(self):
        first = self.manifest()
        self.assertEqual(first['previews'][0]['kind'], '2D')
        self.assertTrue(first['previews'][0]['integer'])
        probe = first['probes'][0]
        self.assertGreaterEqual(probe['id'], 1000)
        self.assertLess(probe['id'], 2 ** 24)
        original = self.sources / 'Water.frag.fsl'
        original.write_text('\n\n' + original.read_text())
        self.assertEqual(probe['id'], self.manifest()['probes'][0]['id'])
        mirror = (self.output / 'shader-lab/sources/Water.frag.fsl').read_text()
        self.assertIn(f'SHADER_PREVIEW_VALUE({probe["id"]},', mirror)
        self.assertIn(f'#line 1 "{original}"', mirror)
        self.assertIn('SHADER_PREVIEW("Foam",', original.read_text())

    def test_comment_markers_are_ignored(self):
        self.preview.write_text(self.preview.read_text() + '\n// SHADER_PREVIEW_1D(ignored, "Ignored", PREVIEW_CONTINUOUS);\n')
        self.assertEqual(len(self.manifest()['previews']), 1)

    def test_invalid_and_duplicate_registration(self):
        self.preview.write_text('SHADER_PREVIEW_2D(previewHash, "Hash", misspelled);')
        with self.assertRaisesRegex(ValueError, 'use SHADER_PREVIEW'):
            self.manifest()
        self.preview.write_text('SHADER_PREVIEW_2D(previewHash, "Hash", PREVIEW_CONTINUOUS);\n' * 2)
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            self.manifest()

    def test_helper_probe_rejected(self):
        (self.sources / 'Water.frag.fsl').write_text('float helper() { SHADER_PREVIEW("Hash", value); }\nfloat4 PS_MAIN() {}')
        with self.assertRaisesRegex(ValueError, 'inside PS_MAIN'):
            self.manifest()

    def test_new_deleted_previews_and_source_cleanup(self):
        added = self.sources / 'Extra.preview.fsl'
        added.write_text('SHADER_PREVIEW_3D_SLICE(volume, "Volume", PREVIEW_CONTINUOUS);\n')
        self.assertEqual(len(self.manifest()['previews']), 2)
        wrapper = (self.output / 'shader-lab/shader_preview_volume.frag.fsl').read_text()
        self.assertLess(wrapper.index('Water.srt.h'), wrapper.index('ShaderPreview.h.fsl'))
        self.assertIn('ShaderPreview3D preview', wrapper)
        added.unlink()
        self.assertEqual(len(self.manifest()['previews']), 1)
        self.assertFalse((self.output / 'shader-lab/sources/Extra.preview.fsl').exists())
        self.assertFalse((self.output / 'shader-lab/shader_preview_volume.frag.fsl').exists())

    def test_reload_failure_then_complete_success(self):
        self.manifest()
        intermediate = self.output / 'shaders/intermediate'
        intermediate.mkdir(parents=True)
        (intermediate / 'mooring-reload.json').write_text(json.dumps({'output': str(self.output), 'layout': layout_signature(self.root)}))
        binary = self.output / 'shaders/bin/MACOS/water.frag.metal'
        binary.parent.mkdir(parents=True)
        binary.write_bytes(b'previous partially updated binary')
        with patch.object(reload_shaders, 'ROOT', self.root), patch.object(reload_shaders, 'recompile_fsl', side_effect=['compile error', []]):
            self.assertEqual(reload_shaders.recompile(intermediate, 'MACOS'), 'compile error')
            published = reload_shaders.recompile(intermediate, 'MACOS')
        self.assertEqual({entry.relative_path for entry in published}, {'MACOS/water.frag.metal', 'shader-lab.json'})
        (self.sources / 'Water.srt.h').write_text('changed resource layout')
        with patch.object(reload_shaders, 'ROOT', self.root):
            self.assertIn('Rebuild and restart', reload_shaders.recompile(intermediate, 'MACOS'))


if __name__ == '__main__':
    unittest.main()
