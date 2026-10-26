#!/usr/bin/env python3
"""Host regression checks; Python standard library and a C++11 compiler only."""
import importlib.util
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("partial_neutral", ROOT / "scripts/extra/partial_neutral.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


def run(source, binary, *args, expected=0):
    subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
        "-std=c++11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include"),
        str(source), "-o", str(binary)], check=True)
    result = subprocess.run([str(binary), *map(str, args)], capture_output=True, text=True)
    assert result.returncode == expected, result.stdout + result.stderr
    print(result.stdout.strip())


def chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))


def main():
    with tempfile.TemporaryDirectory(prefix="trmnl-local-display-") as tmp:
        tmp = Path(tmp)
        fixture = ROOT / "test/local_display/fastepd-row-transfer.cpp"
        source = fixture.read_text()
        fixed = patch.patch_source(source)
        assert fixed != source and patch.patch_source(fixed) == fixed
        for invalid in ("unexpected source", source + source):
            try:
                patch.patch_source(invalid)
            except ValueError:
                pass
            else:
                raise AssertionError("Unrecognized driver source accepted")
        # Hash guard also applies when an already-patched file is presented.
        invalid_driver = tmp / "unknown.inl"
        invalid_driver.write_text(fixed)
        try:
            patch.apply_patch(invalid_driver)
        except ValueError:
            pass
        else:
            raise AssertionError("Unpinned driver accepted")
        patched = tmp / "patched.cpp"
        patched.write_text(fixed)
        run(fixture, tmp / "original", expected=1)
        run(patched, tmp / "fixed")
        png = tmp / "frame.png"
        png.write_bytes(b"\x89PNG\r\n\x1a\n" +
                        chunk(b"IHDR", struct.pack(">IIBBBBB", 1872, 1404, 1, 0, 0, 0, 0)) +
                        chunk(b"IDAT", zlib.compress((b"\0" + b"\xff" * 234) * 1404)) + chunk(b"IEND", b""))
        run(ROOT / "test/local_display/policy.cpp", tmp / "policy", png)
    print("All local display host checks passed.")


if __name__ == "__main__":
    main()
