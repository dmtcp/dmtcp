#!/usr/bin/env python3

"""Deterministic tests for gzip readiness and runtime-page fixture offsets."""

import gzip
import os
import pathlib
import struct
import tempfile
import threading
import unittest

from image_layout import inspect, wait_for_image, REFERENCE
from validate import dirty_file_offset


class HelperTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)

    def compressed_image(self):
        header = REFERENCE.ljust(4096, b"\0")
        terminal = bytearray(4096)
        struct.pack_into("<Q", terminal, 16, 2**64 - 1)
        return gzip.compress(header * 2 + terminal)

    def test_waits_for_gzip_trailer(self):
        data = self.compressed_image()
        path = self.root / "partial.dmtcp"
        path.write_bytes(data[:-8])
        with self.assertRaises(EOFError):
            inspect(path)
        released = threading.Event()

        def finish():
            with path.open("ab") as stream:
                stream.write(data[-8:])
            released.set()

        timer = threading.Timer(0.1, finish)
        self.addCleanup(timer.join)
        timer.start()
        result = wait_for_image(path, timeout=3)
        timer.join(timeout=2)
        self.assertTrue(released.is_set())
        self.assertTrue(result["compressed"])
        self.assertEqual(result["signature"], REFERENCE)

    def test_incomplete_image_times_out(self):
        path = self.root / "partial.dmtcp"
        path.write_bytes(self.compressed_image()[:-8])
        with self.assertRaises(TimeoutError):
            wait_for_image(path, timeout=0.03)

    def test_modified_offset_uses_requested_page_size(self):
        for page in (4096, 65536, os.sysconf("SC_PAGE_SIZE")):
            with self.subTest(page=page):
                path = self.root / "fixture.so"
                path.write_bytes(bytes(2 * page) +
                                 bytes([42]) + bytes(3 * page))
                self.assertEqual(dirty_file_offset(path, page), 3 * page)


if __name__ == "__main__":
    unittest.main(verbosity=2)
