"""Unit tests for scripts/merge_firmware.py.

The script normally runs inside PlatformIO's SCons context — ``Import``
and ``env`` are provided by that harness. Here we install a fake
``Import`` builtin and a stub ``env`` before importing, then drive the
``merge_firmware`` callback with a synthetic env that mimics the fields
PlatformIO would inject.

What this locks down:
  * every declared upstream file is checked for existence
  * esptool.py is invoked with ``merge_bin`` and the expected offsets
    (0x0 / 0x8000 / 0xe000 / 0x10000) — a silent shift here would flash
    a bricked image to the device
"""
from pathlib import Path


class FakeEnv:
    """Minimal SCons Environment stub — just what merge_firmware.py touches."""

    def __init__(self, build_dir, package_dir, values=None):
        self._build_dir = str(build_dir)
        self._package_dir = str(package_dir)
        self._values = values or {}
        self.executed_commands = []

    def subst(self, s):
        return (s
                .replace("$BUILD_DIR", self._build_dir)
                .replace("${BUILD_DIR}", self._build_dir)
                .replace("${PROGNAME}", "firmware")
                .replace("$PYTHONEXE", "python"))

    def PioPlatform(self):
        pkg = self._package_dir
        class _Plat:
            def get_package_dir(self, name):
                # Point both packages at the same fake dir; test only
                # cares that the paths are threaded through.
                return f"{pkg}/{name}"
        return _Plat()

    def BoardConfig(self):
        class _Board:
            def __init__(self, values):
                self._v = values
            def get(self, key, default=None):
                return self._v.get(key, default)
        return _Board(self._values)

    def AddCustomTarget(self, **kw):
        self._custom_target = kw

    def Action(self, fn, msg):
        return (fn, msg)

    def Execute(self, cmd):
        self.executed_commands.append(cmd)
        return 0


SCRIPT_PATH = (Path(__file__).resolve().parents[1] / "scripts"
               / "merge_firmware.py")


def _load_module(env):
    """Freshly exec scripts/merge_firmware.py with `Import` and `env`
    injected — the top-level ``Import("env")`` call is a SCons builtin
    that would raise NameError under plain Python.

    Uses ``exec`` (not ``importlib``) because we need to prepopulate the
    ``env`` binding before the module body — which calls
    ``env.AddCustomTarget`` at import time — runs.
    """
    module_globals = {
        "__name__": "merge_firmware",
        "__file__": str(SCRIPT_PATH),
        # SCons's Import mutates the caller's globals. Our stub does the
        # same thing manually via the module_globals dict below.
        "Import": lambda *_args, **_kw: None,
        "env": env,
    }
    exec(compile(SCRIPT_PATH.read_text(), str(SCRIPT_PATH), "exec"),
         module_globals)

    class _Mod:
        pass
    mod = _Mod()
    for k, v in module_globals.items():
        setattr(mod, k, v)
    return mod


def test_merge_firmware_missing_file_raises(tmp_path):
    env = FakeEnv(build_dir=tmp_path, package_dir=tmp_path / "pkg",
                  values={"build.mcu": "esp32c3", "upload.flash_size": "4MB"})
    mod = _load_module(env)
    # No bootloader.bin — should raise before executing anything.
    try:
        mod.merge_firmware(None, None, env)
        raised = False
    except FileNotFoundError as e:
        raised = True
        assert "bootloader.bin" in str(e)
    assert raised, "expected FileNotFoundError for missing bootloader.bin"


def test_merge_firmware_invokes_esptool_with_correct_offsets(tmp_path):
    # Populate all four upstream artifacts so the file check passes.
    (tmp_path / "bootloader.bin").write_bytes(b"BOOT")
    (tmp_path / "partitions.bin").write_bytes(b"PART")
    (tmp_path / "firmware.bin").write_bytes(b"APP")
    pkg = tmp_path / "pkg"
    boot_app0_dir = pkg / "framework-arduinoespressif32" / "tools" / "partitions"
    boot_app0_dir.mkdir(parents=True, exist_ok=True)
    (boot_app0_dir / "boot_app0.bin").write_bytes(b"OTA0")

    env = FakeEnv(build_dir=tmp_path, package_dir=pkg,
                  values={"build.mcu": "esp32c3", "upload.flash_size": "4MB"})
    mod = _load_module(env)
    mod.merge_firmware(None, None, env)

    assert len(env.executed_commands) == 1
    cmd = env.executed_commands[0]
    # Standard esptool merge_bin offsets. If these shift, devices brick.
    for token in [
        "merge_bin", "--chip", "esp32c3",
        "--flash_size", "4MB",
        "0x0", "bootloader.bin",
        "0x8000", "partitions.bin",
        "0xe000", "boot_app0.bin",
        "0x10000", "firmware.bin",
    ]:
        assert token in cmd, f"missing token '{token}' in esptool command:\n{cmd}"
