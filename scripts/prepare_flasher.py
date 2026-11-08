Import("env")
from pathlib import Path
import shutil
import json

ROOT = Path(env["PROJECT_DIR"])
FLASH = ROOT / "flasher"
FLASH.mkdir(exist_ok=True)


def project_version() -> str:
    ver_path = ROOT / "VERSION"
    if ver_path.exists():
        ver = ver_path.read_text(encoding="utf-8").strip()
        if ver:
            return ver
    return "0.0.0"


def prepare(source, target, env):
    build = Path(env.subst("$BUILD_DIR"))
    packages = Path(env["PROJECT_PACKAGES_DIR"])
    version = project_version()

    def copy_if(src: Path, name: str):
        if src.exists():
            shutil.copy2(src, FLASH / name)
            print(f"flasher: copied {name} ({src.stat().st_size} bytes)")
            return True
        print(f"flasher: missing {src}")
        return False

    copy_if(build / "firmware.bin", "firmware.bin")
    copy_if(build / "bootloader.bin", "bootloader.bin")
    copy_if(build / "partitions.bin", "partitions.bin")

    boot_app0 = packages / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin"
    if not boot_app0.exists():
        found = list(packages.rglob("boot_app0.bin"))
        boot_app0 = found[0] if found else Path()
    if boot_app0 and boot_app0.exists():
        copy_if(boot_app0, "boot_app0.bin")

    manifest = {
        "name": "T-Print",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": "ESP32-S3",
                "parts": [
                    {"path": "bootloader.bin", "offset": 0},
                    {"path": "partitions.bin", "offset": 32768},
                    {"path": "boot_app0.bin", "offset": 57344},
                    {"path": "firmware.bin", "offset": 65536},
                ],
            }
        ],
    }
    (FLASH / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"flasher ready -> {FLASH}")

    # Mirror into docs/ for GitHub Pages (only / or /docs are valid Pages paths).
    docs = ROOT / "docs"
    docs.mkdir(exist_ok=True)
    for name in ("firmware.bin", "bootloader.bin", "partitions.bin", "boot_app0.bin", "manifest.json", "index.html"):
        src = FLASH / name
        if src.exists():
            shutil.copy2(src, docs / name)
    (docs / ".nojekyll").write_text("", encoding="utf-8")
    print(f"pages mirror -> {docs}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", prepare)
