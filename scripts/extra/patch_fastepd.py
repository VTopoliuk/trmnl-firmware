"""Run after PlatformIO resolves libraries, before any compile actions execute."""
Import("env")
import sys
from pathlib import Path

project = Path(env.subst("$PROJECT_DIR"))
sys.path.insert(0, str(project / "scripts" / "extra"))
from partial_neutral import apply_patch

driver = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "FastEPD" / "src" / "FastEPD.inl"
apply_patch(driver)
print("Local live: FastEPD neutral-row DMA fix verified.")
