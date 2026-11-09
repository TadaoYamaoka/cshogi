"""EOF handling in USI byte streams; no compiled cshogi extension required.

Run: python -m unittest discover -s test -p test_usi_engine_eof.py
"""
import importlib.util
import io
from pathlib import Path
import types
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    'usi_engine_under_test', Path(__file__).resolve().parents[1] / 'cshogi/usi/Engine.py')
engine_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(engine_module)


class GuardedStream(io.BytesIO):
    """Fail quickly if a broken reader loops after EOF instead of raising."""
    def __init__(self, data):
        super().__init__(data)
        self.eof_reads = 0

    def readline(self, *args):
        line = super().readline(*args)
        if not line:
            self.eof_reads += 1
            if self.eof_reads > 1:
                raise AssertionError('Reader retried after EOF')
        return line


class EngineEofTests(unittest.TestCase):
    methods = ('connect', 'usi', 'isready', 'go', 'go_mate')

    def invoke(self, method, response):
        engine = engine_module.Engine('fake_engine', connect=False)
        process = types.SimpleNamespace(stdin=io.BytesIO(), stdout=GuardedStream(response))
        engine.proc = process
        with patch.object(engine_module.subprocess, 'Popen', return_value=process):
            return getattr(engine, method)()

    def test_all_read_loops_raise_at_byte_eof(self):
        for method in self.methods:
            with self.subTest(method=method), self.assertRaises(EOFError):
                self.invoke(method, b'')

    def test_eof_after_blank_line_raises(self):
        for method in self.methods:
            with self.subTest(method=method), self.assertRaises(EOFError):
                self.invoke(method, b'\n')

    def test_blank_line_does_not_prevent_valid_response(self):
        cases = (
            ('connect', b'id name fake_engine\nusiok\n', None),
            ('usi', b'id name fake_engine\nusiok\n', ['', 'id name fake_engine']),
            ('isready', b'readyok\n', None),
            ('go', b'info depth 1\nbestmove 7g7f ponder 3c3d\n', ('7g7f', '3c3d')),
            ('go_mate', b'checkmate nomate\n', 'nomate'),
        )
        for method, response, expected in cases:
            with self.subTest(method=method):
                self.assertEqual(self.invoke(method, b'\n' + response), expected)

    def test_valid_responses_still_work(self):
        self.assertIsNone(self.invoke('connect', b'id name fake_engine\nusiok\n'))
        self.assertEqual(self.invoke('usi', b'id name fake_engine\nusiok\n'), ['id name fake_engine'])
        self.assertIsNone(self.invoke('isready', b'\nreadyok\n'))
        self.assertEqual(self.invoke('go', b'info depth 1\nbestmove 7g7f ponder 3c3d\n'), ('7g7f', '3c3d'))
        self.assertEqual(self.invoke('go_mate', b'checkmate nomate\n'), 'nomate')


if __name__ == '__main__':
    unittest.main()
