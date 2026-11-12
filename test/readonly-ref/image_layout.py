#!/usr/bin/env python3

"""Inspect test images; never changes or converts a checkpoint."""

import gzip
import io
import pathlib
import struct
import time
import zlib

EMBEDDED = b"DMTCP_CHECKPOINT_IMAGE_v4.0\n"
REFERENCE = b"DMTCP_REF_IMAGE_DRAFT_v1\n"
EXTERNAL_RO = 0x1000


def inspect(path):
    path = pathlib.Path(path)
    payload = path.read_bytes()
    compressed = payload.startswith(b"\x1f\x8b")
    if compressed:
        payload = gzip.decompress(payload)
    stream = io.BytesIO(payload)
    signature = stream.read(4096).split(b"\0", 1)[0]
    assert signature in (EMBEDDED, REFERENCE), signature
    assert stream.read(4096).startswith(signature)
    rows = []
    while True:
        header = stream.read(4096)
        assert len(header) == 4096, "truncated area header"
        values = struct.unpack_from("<11Q", header)
        virtual, offset, prot, flags = values[2:6]
        properties = values[10]
        if virtual == 2**64 - 1:
            break
        mapped = struct.unpack_from("<q", header, 72)[0]
        stored = (
            0
            if properties & (3 | EXTERNAL_RO)
            else (mapped if not flags & 32 and mapped > 0 else virtual)
        )
        assert stream.tell() + stored <= len(payload), "truncated area payload"
        name = header[88:1112].split(
            b"\0", 1)[0].decode(errors="surrogateescape")
        rows.append(
            dict(
                name=name,
                offset=offset,
                virtual=virtual,
                prot=prot,
                flags=flags,
                properties=properties,
                stored=stored,
            )
        )
        stream.seek(stored, 1)
    return dict(
        signature=signature,
        compressed=compressed,
        rows=rows,
        disk_bytes=path.stat().st_size,
        logical_bytes=len(payload),
    )


def wait_for_image(path, timeout=20):
    """Wait for a complete image, including the gzip trailer."""
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        try:
            return inspect(path)
        except (OSError, EOFError, zlib.error, AssertionError) as error:
            last_error = error
            time.sleep(0.01)
    raise TimeoutError(
        "checkpoint image did not become complete: " + str(path)
    ) from last_error
