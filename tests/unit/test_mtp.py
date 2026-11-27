"""MTP importer and desktop locale regression tests without USB hardware."""
import contextlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'importer'))
from rsl import exporter, i18n, library, mtp


class Device:
    def __init__(self, root):
        self.root = root

    def entries(self, path):
        return {p.name: (0, p.is_dir(), p.stat().st_size) for p in (self.root / path).iterdir()}

    def mkdir(self, parent, name):
        (self.root / parent / name).mkdir()

    def get(self, remote, local, tick):
        shutil.copyfile(self.root / remote, local)
        tick(1)

    def put(self, remote, local, tick):
        shutil.copyfile(local, self.root / remote)
        tick(1)

    def rename(self, old, new):
        (self.root / old).rename(self.root / new)

    def remove(self, path):
        target = self.root / path
        if target.is_dir():
            target.rmdir()
        else:
            target.unlink()

    def close(self):
        pass


class MtpTest(unittest.TestCase):
    def test_replacement_preserves_saves_and_cancel_keeps_installed_game(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            remote, source = root / 'remote', root / 'source'
            remote.mkdir()
            source.mkdir()
            script = source / 'script.rpy'
            script.write_bytes(b'first')
            backend = types.SimpleNamespace(Device=lambda key: Device(remote))
            manifest = {'id': 'fixture', 'status': 'experimental', 'performance': {'converted': {}}}
            target = 'mtp://fixture'
            files = [('script.rpy', 5)]
            with mock.patch.object(mtp, '_backend', return_value=backend):
                exporter.export(manifest, str(source), files, target, False)
                save = remote / 'games/fixture/game/saves/a.save'
                save.parent.mkdir()
                save.write_bytes(b'player data')
                script.write_bytes(b'newer')
                exporter.export(manifest, str(source), files, target, True)
                self.assertEqual(save.read_bytes(), b'player data')
                installed = save.parent.parent / 'script.rpy'
                self.assertEqual(installed.read_bytes(), b'newer')

                def cancel(*args):
                    raise InterruptedError('cancelled')

                with self.assertRaises(InterruptedError):
                    exporter.export(manifest, str(source), files, target, True, progress=cancel)
                self.assertEqual(installed.read_bytes(), b'newer')
                self.assertFalse((remote / 'games/.uploading-fixture').exists())
                with contextlib.closing(library.open_library(target)) as lib:
                    with self.assertRaises(mtp.Refused):
                        lib.read('../outside')

    def test_wire_protocol_cleans_interrupted_upload(self):
        program = Path(__file__).resolve().parents[2] / 'build/host/mtp_host'
        if not program.is_file() or sys.platform == 'win32':
            self.skipTest('build the POSIX harness with scripts/test.sh')

        def packet(kind, opcode, transaction, data=b''):
            return struct.pack('<IHHI', len(data) + 12, kind, opcode, transaction) + data

        def command(opcode, transaction, *parameters):
            return packet(1, opcode, transaction, struct.pack('<' + 'I' * len(parameters), *parameters))

        def name(text):
            return bytes([len(text) + 1]) + (text + '\0').encode('utf-16le')

        contents = b'fixture' * 10000

        def upload(object_name, transaction, data):
            metadata = (struct.pack('<IHHI', 0x10001, 0x3000, 0, len(contents)) + bytes(40)
                        + name(object_name) + bytes(3))
            return (command(0x100C, transaction, 0x10001, 0xFFFFFFFF)
                    + packet(2, 0x100C, transaction, metadata)
                    + command(0x100D, transaction + 1) + data)

        complete = packet(2, 0x100D, 3, contents)
        partial = packet(2, 0x100D, 8, contents)[:-1]
        stream = (command(0x1001, 0) + command(0x1002, 1, 1) + upload('complete', 2, complete)
                  + command(0x1009, 4, 1) + command(0x9804, 5, 1, 0xDC07)
                  + packet(2, 0x9804, 5, name('renamed'))
                  + command(0x9804, 6, 1, 0xDC44) + packet(2, 0x9804, 6, name('display-stem'))
                  + upload('partial', 7, partial))
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'original').write_bytes(b'unchanged')
            result = subprocess.run([str(program), tmp], input=stream, capture_output=True, check=True, timeout=10)
            self.assertIn(packet(2, 0x1009, 4, contents), result.stdout)
            self.assertIn(os.fsencode(root / 'complete') , result.stderr)
            self.assertEqual(sorted(p.name for p in root.iterdir()), ['original', 'renamed'])
            self.assertEqual((root / 'renamed').read_bytes(), contents)
            self.assertEqual((root / 'original').read_bytes(), b'unchanged')

    def test_system_locale_mapping_and_catalogs(self):
        for tag, language in [('zh_CN', 'zh-Hans'), ('zh-Hant-HK', 'zh-Hant'), ('ja-JP', 'en')]:
            self.assertEqual(i18n.language_for(tag), language)
        for host, expected in [('zh-CN', '错误：%s'), ('zh-TW', '錯誤：%s'), ('en-US', 'Error: %s')]:
            with mock.patch.dict(os.environ, {'RSL_LANGUAGE': host}):
                self.assertEqual(i18n.tr('错误：%s'), expected)
        for host, expected in [('zh-CN', '已连接：{name}（USB MTP）'),
                               ('zh-TW', '已連接：{name}（USB MTP）'),
                               ('en-US', 'Connected: {name} (USB MTP)')]:
            with mock.patch.dict(os.environ, {'RSL_LANGUAGE': host}):
                self.assertEqual(i18n.tr('已连接：{name}（USB MTP）'), expected)

    def test_games_dir_clean_mtp_label(self):
        self.assertEqual(library.games_dir('mtp://%5C%5C%3F%5Cusb%23vid_057e'), 'rpynx (USB MTP)/games')
