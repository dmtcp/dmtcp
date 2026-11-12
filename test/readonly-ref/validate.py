#!/usr/bin/env python3
"""CPU-only reference-image round trips, run by the integration suite."""

import argparse
import json
import os
import platform
import pathlib
import resource
import signal
import shutil
import subprocess
import tempfile
import time
import unittest

from image_layout import wait_for_image, EMBEDDED, REFERENCE, EXTERNAL_RO


def dirty_file_offset(path, page_size):
    marker = bytes([42]) + bytes(page_size - 1)
    return page_size + path.read_bytes().index(marker)


def wait(predicate, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.02)
    raise TimeoutError("checkpoint/restore deadline exceeded")


class ReferenceTests(unittest.TestCase):
    prefix = None

    @classmethod
    def setUpClass(cls):
        # The test-only image inspector currently describes this host layout.
        if (platform.machine() != "x86_64" or
                os.sysconf("SC_PAGE_SIZE") != 4096):
            raise unittest.SkipTest(
                "image inspector requires x86_64 with 4 KiB pages")

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="dmtcp-ro-ref-")
        self.addCleanup(self.temporary.cleanup)
        self.work = pathlib.Path(self.temporary.name)
        self.images = self.work / "images"
        self.images.mkdir()
        self.groups = []
        self.streams = []
        self.addCleanup(self.cleanup_processes)
        self.environment = {
            k: os.environ[k]
            for k in ("PATH", "HOME", "USER", "LANG")
            if k in os.environ
        }
        self.environment.update(
            DMTCP_GZIP="0", DMTCP_QUIET="2",
            DMTCP_TMPDIR=str(self.work / "tmp")
        )
        (self.work / "tmp").mkdir()
        self.asset = self.work / "immutable.bin"
        with self.asset.open("wb") as stream:
            stream.truncate(8 * 1024 * 1024)
            stream.write(bytes([42]))
            stream.seek(8 * 1024 * 1024 - 1)
            stream.write(bytes([17]))
        self.page_size = os.sysconf("SC_PAGE_SIZE")
        self.library = self.work / "libfixture.so"
        fixture = pathlib.Path(__file__).with_name("libfixture.so")
        self.assertTrue(fixture.is_file(), "run make tests first")
        shutil.copyfile(fixture, self.library)
        self.probe = pathlib.Path(__file__).with_name("probe").resolve()
        self.assertTrue(self.probe.is_file(), "run make tests first")

    def stop(self, process):
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        if process.poll() is None:
            process.wait(timeout=5)

    def cleanup_processes(self):
        for process in reversed(self.groups):
            self.stop(process)
        for stream in self.streams:
            stream.close()

    def spawn(self, command, name):
        stream = (self.work / (name + ".log")).open("w")
        self.streams.append(stream)
        process = subprocess.Popen(
            list(map(str, command)),
            cwd=self.work,
            env=self.environment,
            stdout=stream,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        self.groups.append(process)
        return process

    def coordinator(self, name):
        port_file = self.work / (name + ".port")
        process = self.spawn(
            [
                self.prefix / "bin/dmtcp_coordinator",
                "--port",
                "0",
                "--port-file",
                port_file,
                "--exit-on-last",
            ],
            name,
        )
        wait(lambda: port_file.exists()
             and port_file.read_text().strip().isdigit())
        return process, port_file.read_text().strip()

    def roundtrip(
        self,
        mode="clean",
        dependency=None,
        externalize=True,
        include=(),
        exclude=(),
        should_reference=True,
        compressed=False,
        checkpoint_again=False,
        expect_any=True,
        simulate=False,
    ):
        mapped_file = self.asset if mode.startswith("asset") else self.library
        coordinator, port = self.coordinator("source-coordinator")
        options = ["--gzip" if compressed else "--no-gzip"]
        if externalize:
            options += ["--externalize-readonly"]
            for path in include:
                options += ["--readonly-include", str(path)]
            for path in exclude:
                options += ["--readonly-exclude", str(path)]
        source = self.spawn(
            [
                self.prefix / "bin/dmtcp_launch",
                "--join-coordinator",
                "--coord-port",
                port,
                "--ckptdir",
                self.images,
            ]
            + options
            + [self.probe, mode, mapped_file],
            "source",
        )
        source_log = self.work / "source.log"
        wait(lambda: source_log.read_text().count("progress") >= 3)
        subprocess.run(
            [
                str(self.prefix / "bin/dmtcp_command"),
                "--coord-port",
                port,
                "--bcheckpoint",
            ],
            env=self.environment,
            check=True,
            capture_output=True,
            timeout=20,
        )
        wait(lambda: len(list(self.images.glob("*.dmtcp"))) == 1)
        image = next(self.images.glob("*.dmtcp"))
        reference = image
        layout = wait_for_image(image)
        self.assertEqual(layout["signature"],
                         REFERENCE if externalize else EMBEDDED)
        self.assertEqual(layout["compressed"], compressed)
        referenced = [row for row in layout["rows"]
                      if row["properties"] & EXTERNAL_RO]
        removed = sum(row["virtual"] for row in referenced)
        report = dict(
            source_bytes=layout["logical_bytes"] + removed,
            optimized_bytes=layout["logical_bytes"],
            removed_payload_bytes=removed,
            external_regions=len(referenced),
            rows=layout["rows"],
        )
        if externalize:
            self.assertEqual(bool(removed), expect_any)
            self.assertEqual(
                any(row["name"] == str(mapped_file) for row in referenced),
                should_reference,
            )
            if "dirty" in mode or "writable" in mode:
                dirty_offset = self.page_size
                if not mode.startswith("asset"):
                    dirty_offset = dirty_file_offset(
                        mapped_file, self.page_size)
                for row in layout["rows"]:
                    if (
                        row["name"] == str(mapped_file)
                        and row["offset"]
                        <= dirty_offset
                        < row["offset"] + row["virtual"]
                    ):
                        self.assertFalse(row["properties"] & EXTERNAL_RO)
        else:
            self.assertEqual(referenced, [])
        if simulate:
            inspection = subprocess.run(
                [str(self.prefix / "bin/mtcp_restart"), "--simulate", image],
                env=self.environment,
                capture_output=True,
                text=True,
                timeout=20,
                check=True,
            )
            output = inspection.stdout + inspection.stderr
            self.assertIn("DMTCP_REF_IMAGE_DRAFT_v1", output)
            self.assertIn(str(mapped_file), output)
        self.stop(source)
        self.assertIsNotNone(source.poll())
        self.stop(coordinator)
        if dependency == "missing":
            self.library.rename(self.library.with_suffix(".hidden"))
        elif dependency == "changed":
            content = self.library.read_bytes()
            offset = content.index(bytes([42]) + bytes(4095))
            with self.library.open("r+b") as stream:
                stream.seek(offset)
                stream.write(bytes([43]))
        _, port = self.coordinator("restore-coordinator")
        restored = self.spawn(
            [
                self.prefix / "bin/dmtcp_restart",
                "--join-coordinator",
                "--coord-port",
                port,
                reference,
            ],
            "restored",
        )
        self.assertNotEqual(source.pid, restored.pid)
        restored_log = self.work / "restored.log"
        wait(
            lambda: restored_log.read_text().count("restarts=1") >= 8
            or restored.poll() is not None
        )
        text = restored_log.read_text()
        if dependency == "missing":
            self.assertIn("Assertion failed", text)
            self.assertNotIn("restarts=1", text)
        elif dependency == "changed":
            self.assertIn("state mismatch", text)
            self.assertNotIn("restarts=1", text)
        else:
            self.assertGreaterEqual(text.count("restarts=1"), 8, text)
        if checkpoint_again:
            subprocess.run(
                [
                    str(self.prefix / "bin/dmtcp_command"),
                    "--coord-port",
                    port,
                    "--bcheckpoint",
                ],
                env=self.environment,
                check=True,
                capture_output=True,
                timeout=20,
            )
            self.assertEqual(wait_for_image(image)["signature"], REFERENCE)
            self.stop(restored)
            self.stop(self.groups[-2])  # the second coordinator
            _, port = self.coordinator("third-coordinator")
            restarted = self.spawn(
                [
                    self.prefix / "bin/dmtcp_restart",
                    "--join-coordinator",
                    "--coord-port",
                    port,
                    image,
                ],
                "twice-restored",
            )
            log = self.work / "twice-restored.log"
            wait(
                lambda: log.read_text().count("restarts=2") >= 8
                or restarted.poll() is not None
            )
            self.assertGreaterEqual(
                log.read_text().count("restarts=2"), 8, log.read_text()
            )
        print(
            json.dumps(
                {
                    k: report[k]
                    for k in (
                        "source_bytes",
                        "optimized_bytes",
                        "removed_payload_bytes",
                        "external_regions",
                    )
                }
            ),
            flush=True,
        )
        return report

    def test_non_library_not_selected_by_default(self):
        self.roundtrip("asset", should_reference=False)

    def test_include_non_library_file(self):
        report = self.roundtrip("asset", include=(self.asset,))
        self.assertTrue(
            any(
                row["name"] == str(
                    self.asset) and row["properties"] & EXTERNAL_RO
                for row in report["rows"]
            )
        )

    def test_exclude_overrides_include(self):
        self.roundtrip(
            "asset",
            include=(self.work, str(self.work / "*.bin")),
            exclude=(self.asset, self.library),
            should_reference=False,
        )

    def test_exclude_default_library(self):
        self.roundtrip(exclude=(self.library,), should_reference=False)

    def test_included_file_modified_page(self):
        self.roundtrip("asset-dirty", include=(str(self.work / "*.bin"),))

    def test_included_file_writable_page(self):
        self.roundtrip("asset-writable", include=(self.asset,))

    def test_include_shared_mapping_remains_embedded(self):
        self.roundtrip("asset-shared", include=(self.asset,),
                       should_reference=False)

    def test_include_anonymous_mapping_remains_embedded(self):
        self.roundtrip("asset-anon", include=("/",), should_reference=False)

    def test_include_deleted_mapping_remains_embedded(self):
        self.roundtrip("asset-deleted", include=(self.work,),
                       should_reference=False)

    def test_include_incomplete_file_span_remains_embedded(self):
        self.roundtrip("asset-tail", include=(self.asset,),
                       should_reference=False)

    def test_exclude_all_embeds_every_region(self):
        self.roundtrip(exclude=("/",), should_reference=False,
                       expect_any=False)

    def test_reference_image_inspection(self):
        self.roundtrip(simulate=True)

    def test_compressed_native_reference(self):
        self.roundtrip(compressed=True)

    def test_recheckpoint_after_restore(self):
        self.roundtrip(checkpoint_again=True)

    def test_embedded_image_remains_default(self):
        self.roundtrip(externalize=False)

    def test_unmodified_library(self):
        self.roundtrip()

    def test_private_modified_readonly_page(self):
        self.roundtrip("dirty")

    def test_writable_page(self):
        self.roundtrip("writable")

    def test_missing_dependency_rejected(self):
        self.roundtrip(dependency="missing")

    def test_changed_dependency_fails_application_invariant(self):
        # Explicitly NOT a test of loader-side integrity verification.
        self.roundtrip(dependency="changed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[2])
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if not (args.prefix / "bin/dmtcp_launch").is_file():
        parser.error("--prefix must contain built DMTCP runtime tools")
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    ReferenceTests.prefix = args.prefix.resolve()
    suite = unittest.TestSuite()
    for _ in range(args.repeats):
        suite.addTests(
            unittest.defaultTestLoader.loadTestsFromTestCase(ReferenceTests))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(0 if result.wasSuccessful() else 1)
