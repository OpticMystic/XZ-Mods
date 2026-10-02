"""Install a paired native bundle locally while retaining every desktop backend."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import uuid
import zipfile

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[3]
sys.path.insert(0, str(ROOT.parent.parent / "vendor"))
from tools.xz_firmware.mods_bundle import load_mods_bundle


def sha(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def child(root: Path, relative: str) -> Path:
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError("Resource path leaves its target: " + relative)
    return path


def replace_bytes(destination: Path, data: bytes) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    incoming = destination.with_name(destination.name + ".wave-rider-" + uuid.uuid4().hex)
    with incoming.open("xb") as stream:
        stream.write(data)
    os.replace(incoming, destination)


def tree_digest(root: Path) -> dict:
    if root.is_file():
        files = {root.name: sha(root)}
    elif root.is_dir():
        files = {path.relative_to(root).as_posix(): sha(path)
                 for path in sorted(root.rglob("*")) if path.is_file()}
    else:
        files = {}
    digest = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()
    return {"path": str(root), "files": len(files), "sha256": digest}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--backup", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    bundle, backup = args.bundle.resolve(), args.backup.resolve()
    manifest = load_mods_bundle(bundle)
    local = Path(os.environ["LOCALAPPDATA"]).resolve()
    installed = local / "XZ Mods"
    library = local / "VJ.Tools Library"
    sibling = REPO.parent / "XZ-Mods"
    resources = {
        "builder": REPO / "apps/xz-mods-builder/resources",
        "installed": installed / "resources",
        "sibling": sibling / "resources",
    }
    canonical = {
        "toolkit": ROOT.parent.parent / "runtime",
        "library": library / "xdj-toolkit/mods",
    }
    for executable in (installed / "xz-mods-builder.exe", library / "library-tauri.exe"):
        if not executable.is_file():
            raise RuntimeError("Expected installed executable: " + str(executable))
    for label, path in [*resources.items(), *canonical.items()]:
        if not path.is_dir():
            raise RuntimeError("Expected existing " + label + " resource directory: " + str(path))
        if backup.is_relative_to(path.resolve()) or path.resolve().is_relative_to(backup):
            raise ValueError("Backup and installed resources must be separate")
    if args.evidence.exists() or backup.exists():
        raise ValueError("Choose fresh backup and evidence paths")

    source_entries = {}
    with zipfile.ZipFile(bundle / "source.zip") as archive:
        for entry in archive.infolist():
            name = Path(entry.filename)
            if name.is_absolute() or ".." in name.parts or (entry.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError("Unsafe source archive entry")
            if not entry.is_dir():
                source_entries[entry.filename] = hashlib.sha256(archive.read(entry)).hexdigest()

    protected = {
        "installed_shell": installed / "xz-mods-builder.exe",
        "library_shell": library / "library-tauri.exe",
        "library_usb_builder": library / "xdj-toolkit/xz-builder.exe",
        "sibling_ui": sibling / "ui",
    }
    for label, directory in resources.items():
        for name in ("backend", "inference", "source/xdj-xz-toolkit/builder"):
            protected[label + "/" + name] = directory / name
    receipt = {
        "bundle": str(bundle), "bundle_manifest_sha256": sha(bundle / "manifest.json"),
        "runtime_sha256": manifest["files"]["libxz-mods.so"],
        "targets": {**{label: str(path / "runtime") for label, path in resources.items()},
                    **{label: str(path) for label, path in canonical.items()}},
        "public_publish": False, "hardware_qualified": False,
        "desktop_backend_rebuilt": False, "desktop_shell_replaced": False,
        "physical_jog_verified": False, "track_phase_verified": False,
    }
    if args.dry_run:
        print(json.dumps(receipt, indent=2))
        return
    protected_before = {label: tree_digest(path) for label, path in protected.items()}
    backup.mkdir(parents=True, exist_ok=False)
    receipt["backup"] = str(backup)
    # The existing installer backs up its three runtime roots. Capture the
    # additional targets and every existing source/bootstrap file it updates.
    for label, directory in resources.items():
        bootstrap = directory / "bootstrap.sh"
        if bootstrap.is_file():
            destination = backup / "desktop" / label / "bootstrap.sh"
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(bootstrap, destination)
        source = directory / "source/xdj-xz-toolkit"
        for relative in source_entries:
            previous = child(source, relative)
            if previous.is_file():
                destination = child(backup / "desktop" / label / "source", relative)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(previous, destination)
    shutil.copytree(resources["sibling"] / "runtime", backup / "sibling-runtime")
    shutil.copytree(canonical["library"], backup / "library-runtime")
    subprocess.run([sys.executable, str(ROOT / "install_local.py"), "--bundle", str(bundle),
                    "--backup", str(backup / "core-runtime"),
                    "--evidence", str(backup / "core-installer.json")], check=True)

    for label, target in (("sibling", resources["sibling"] / "runtime"),
                          ("library", canonical["library"])):
        for relative in manifest["files"]:
            replace_bytes(child(target, relative), (bundle / relative).read_bytes())
        if label == "library":
            replace_bytes(target / "manifest.json", (bundle / "manifest.json").read_bytes())

    metadata = dict(
        firmware=manifest["firmware"], profile=manifest["profile"], hardware_qualified=False,
        runtime_sha256=manifest["files"]["libxz-mods.so"],
        receiver_sha256=manifest["files"]["libxz-receiver.so"],
        doom_sha256=manifest["files"]["xz-doom"],
        ota_updater_sha256=manifest["files"]["xz-updater"],
        ota_runtime_smoke_sha256=manifest["files"]["xz-runtime-smoke"],
        source_zip_sha256=manifest["files"]["source.zip"],
        source_repository=manifest["source_repository"], source_commit=manifest["source_commit"],
        source_directory=manifest.get("source_directory", "packages/xdj-xz-toolkit"),
        bundle_manifest_sha256=receipt["bundle_manifest_sha256"],
        settings_schema_sha256=manifest["files"].get("settings-schema.json"),
        native_wave_rider=True, physical_wave_rider_controls_verified=False, track_phase_verified=False,
    )
    for label, directory in resources.items():
        target = directory / "runtime"
        previous = json.loads((target / "manifest.json").read_text(encoding="utf8"))
        replace_bytes(target / "manifest.json", (json.dumps(dict(previous, **metadata), indent=2) + "\n").encode())
        replace_bytes(directory / "bootstrap.sh", (bundle / "bootstrap.sh").read_bytes())
        source = directory / "source/xdj-xz-toolkit"
        if label == "sibling":
            with zipfile.ZipFile(bundle / "source.zip") as archive:
                for relative in source_entries:
                    replace_bytes(child(source, relative), archive.read(relative))
        readback = {relative: sha(child(source, relative)) for relative in source_entries}
        if readback != source_entries:
            raise RuntimeError("Native source readback differs: " + label)
    receipt["verified"] = {}
    for label, target in [*((label, directory / "runtime") for label, directory in resources.items()),
                           *canonical.items()]:
        readback = {relative: sha(child(target, relative)) for relative in manifest["files"]}
        if readback != manifest["files"]:
            raise RuntimeError("Native runtime readback differs: " + label)
        receipt["verified"][label] = {"path": str(target), "files": readback}
    protected_after = {label: tree_digest(path) for label, path in protected.items()}
    if protected_after != protected_before:
        changed = [label for label in protected_before if protected_before[label] != protected_after[label]]
        raise RuntimeError("A desktop dependency changed during native install: " + ", ".join(changed))
    receipt["protected_unchanged"] = protected_after
    receipt["native_source_files_verified_per_desktop"] = len(source_entries)
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf8")
    print(json.dumps({"installed": True, "runtime_sha256": receipt["runtime_sha256"],
                      "targets_verified": list(receipt["verified"]),
                      "protected_unchanged": True, "evidence": str(args.evidence)}, indent=2))


if __name__ == "__main__":
    main()
