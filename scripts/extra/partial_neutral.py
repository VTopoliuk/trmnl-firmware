"""Small, pinned FastEPD fix: neutralize both alternating row DMA buffers."""
from hashlib import sha256
from pathlib import Path

ORIGINAL_SHA256 = "c45ad681fa897cccb5168b9a80ef4a826fd258266b440d9b9840aff9ccaf1aba"
OLD = """if (iSkipped == 0) { // new skipped section
                    memset((void *)d, 0, pState->native_width/4);
                }"""
NEW = """if (iSkipped < 2) { // local live fix: neutralize BOTH ping-pong DMA buffers
                    memset((void *)d, 0, pState->native_width/4);
                }"""


def patch_source(source):
    if source.count(NEW) == 1 and OLD not in source:
        return source
    if source.count(OLD) != 1 or NEW in source:
        raise ValueError("Unexpected FastEPD partial-row implementation; review the driver patch.")
    return source.replace(OLD, NEW)


def apply_patch(path):
    path = Path(path)
    source = path.read_text()
    original = source.replace(NEW, OLD)
    if sha256(original.encode()).hexdigest() != ORIGINAL_SHA256:
        raise ValueError("FastEPD differs from pinned commit 9a84c35; refusing an unreviewed driver patch.")
    patched = patch_source(source)
    if patched != source:
        path.write_text(patched)
    return patched != source
