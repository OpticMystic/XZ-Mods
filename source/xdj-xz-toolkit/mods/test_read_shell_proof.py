"""Offline CLI checks. Device commands and network connections are intercepted."""
import base64
import contextlib
import hashlib
import io
import json
from pathlib import Path
import re
import runpy
import socket
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
FIXTURES = HERE / "proof_reader_fixtures"
SYMBOL_OFFSET = 0x1FF0  # Cross a page boundary during the 260-byte read.
BASE = 0x10000000
PID = 7756
REMOTE = "/dev/shm/xz-dev-fixture"


def elf_fixture(size=260):
    """Minimal ELF32 ARM shared object with one exported proof object."""
    shstr = b"\0.shstrtab\0.dynstr\0.dynsym\0"
    dynstr = b"\0xz_mods_shell_proof_v1\0"
    dynsym = bytes(16) + struct.pack("<IIIBBH", 1, SYMBOL_OFFSET, size, 0x11, 0, 1)
    sections = [(0, 0, 0, 0, 0, 0, 0, 0, 0, 0)]
    payload = bytearray(52)
    for name, kind, body, link, info, align, entry in (
        (1, 3, shstr, 0, 0, 1, 0),
        (11, 3, dynstr, 0, 0, 1, 0),
        (19, 11, dynsym, 2, 1, 4, 16),
    ):
        payload.extend(bytes((-len(payload)) % align))
        sections.append((name, kind, 0, 0, len(payload), len(body), link, info, align, entry))
        payload.extend(body)
    payload.extend(bytes((-len(payload)) % 4))
    shoff = len(payload)
    for section in sections:
        payload.extend(struct.pack("<10I", *section))
    payload[:52] = struct.pack(
        "<16sHHIIIIIHHHHHH", b"\x7fELF\x01\x01\x01" + bytes(9),
        3, 40, 1, 0, 0, shoff, 0, 52, 0, 0, 40, len(sections), 1)
    return bytes(payload)


class DeviceFixture:
    def __init__(self, snapshots):
        self.snapshots = iter(snapshots)
        self.commands = []
        self.pid_responses = iter([str(PID)] * 30)
        self.mapping_path = REMOTE + "/mods.so"
        self.short_read = False

    def command(self, host, text):
        self.commands.append(text)
        if text == "pidof rbp":
            return next(self.pid_responses)
        if text == f"cat /proc/{PID}/maps":
            return (f"{BASE:x}-{BASE + 0x4000:x} r--p 00000000 00:00 1 "
                    f"{self.mapping_path}\n")
        if text == (f"dd if=/proc/{PID}/mem bs=4096 skip={(BASE + SYMBOL_OFFSET) // 4096} "
                    "count=2 2>/dev/null | base64"):
            block = bytearray(8192)
            data = next(self.snapshots)
            block[4080:4080 + len(data)] = data
            if self.short_read:
                block = block[:-1]
            return base64.b64encode(block).decode()
        raise AssertionError("Unexpected device command: " + text)


class ProofReaderTests(unittest.TestCase):
    def setUp(self):
        self.fixture = json.loads((FIXTURES / "abi-260.json").read_text())
        self.raw = bytes.fromhex(self.fixture["bytes_hex"])
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.binary = self.folder / "runtime.so"
        self.receipt = self.folder / "deploy.json"
        self.output = self.folder / "proof.json"
        self.write_binary(elf_fixture())

    def write_binary(self, data):
        self.binary.write_bytes(data)
        self.receipt.write_text(json.dumps({
            "runtime_sha256": hashlib.sha256(data).hexdigest(),
            "remote": REMOTE, "after": f"STARTED\n{PID}\nfixture",
        }))

    def changed(self, **values):
        result = bytearray(self.raw)
        for name, value in values.items():
            struct.pack_into("<I", result, self.fixture["offsets"][name], value)
        return bytes(result)

    def run_reader(self, device, samples=1):
        module = types.ModuleType("device_smoke")
        module.command = device.command
        args = [str(HERE / "read_shell_proof.py"), str(self.receipt),
                str(self.binary), str(self.output), "--host", "192.0.2.1",
                "--samples", str(samples)]
        with patch.dict(sys.modules, {"device_smoke": module}), \
             patch.object(sys, "argv", args), patch("time.sleep"), \
             patch.object(socket, "socket", side_effect=AssertionError("Network forbidden")), \
             contextlib.redirect_stdout(io.StringIO()):
            runpy.run_path(args[0], run_name="__main__")
        return json.loads(self.output.read_text())

    def test_current_260_byte_abi_decodes_every_field(self):
        result = self.run_reader(DeviceFixture([self.raw, self.raw]))
        self.assertEqual(result["pid"], PID)
        self.assertEqual(result["samples"][0], self.fixture["expected"])
        self.assertEqual(result["address"], hex(BASE + SYMBOL_OFFSET))

    def test_render_counters_can_advance(self):
        later = self.changed(frames=1001, render_calls=1002, sequence=1003,
                             second_frames=60, model_holds=1004, menu_passes=1005)
        result = self.run_reader(DeviceFixture([self.raw, later]))
        self.assertEqual(result["samples"][0]["frames"], 1001)
        self.assertEqual(result["samples"][0]["commands_run"], 7)
        self.assertIn("commands_run", result["stable_fields"])
        self.assertNotIn("frames", result["stable_fields"])

    def test_command_transition_retries_then_accepts(self):
        changed = self.changed(commands_run=8, last_command=4)
        device = DeviceFixture([self.raw, changed, changed, changed])
        result = self.run_reader(device)
        self.assertEqual(result["samples"][0]["commands_run"], 8)
        self.assertEqual(sum(c.startswith("dd ") for c in device.commands), 4)

    def test_command_transition_does_not_pass_as_stable(self):
        device = DeviceFixture([self.raw, self.changed(commands_run=8)] * 5)
        with self.assertRaisesRegex(ValueError, "Load counters kept changing"):
            self.run_reader(device)
        self.assertFalse(self.output.exists())

    def test_bad_headers_are_rejected_in_either_read(self):
        for corrupt in (self.changed(version=2), self.changed(size=216)):
            for blobs in ([corrupt, self.raw], [self.raw, corrupt]):
                with self.subTest(blobs=blobs), self.assertRaisesRegex(ValueError, "proof header"):
                    self.run_reader(DeviceFixture(blobs))
        self.assertFalse(self.output.exists())

    def test_legacy_or_unknown_symbol_sizes_are_rejected_before_contact(self):
        for size in (216, 256, 264):
            self.write_binary(elf_fixture(size))
            device = DeviceFixture([])
            with self.subTest(size=size), self.assertRaisesRegex(ValueError, "proof ABI"):
                self.run_reader(device)
            self.assertEqual(device.commands, [])

    def test_binary_hash_mismatch_is_rejected_before_contact(self):
        self.binary.write_bytes(self.binary.read_bytes() + b"changed")
        device = DeviceFixture([])
        with self.assertRaisesRegex(ValueError, "Binary differs"):
            self.run_reader(device)
        self.assertEqual(device.commands, [])

    def test_wrong_mapping_is_rejected_before_memory_read(self):
        device = DeviceFixture([])
        device.mapping_path = "/dev/shm/other/mods.so"
        with self.assertRaisesRegex(ValueError, "not mapped"):
            self.run_reader(device)
        self.assertFalse(any(c.startswith("dd ") for c in device.commands))

    def test_mapping_change_during_sample_is_rejected(self):
        device = DeviceFixture([self.raw, self.raw])
        original = device.command
        maps_read = 0

        def remapped(host, text):
            nonlocal maps_read
            reply = original(host, text)
            if text.endswith("/maps"):
                maps_read += 1
                if maps_read == 2:
                    reply = reply.replace(f"{BASE:x}-", f"{BASE + 4096:x}-")
            return reply

        device.command = remapped
        with self.assertRaisesRegex(ValueError, "mapping changed"):
            self.run_reader(device)
        self.assertFalse(self.output.exists())

    def test_wrong_initial_pid_or_restart_during_sample_is_rejected(self):
        for pids in ([str(PID + 1)], [str(PID), str(PID), str(PID + 1)]):
            device = DeviceFixture([self.raw] * 10)
            device.pid_responses = iter(pids)
            with self.subTest(pids=pids), self.assertRaisesRegex(ValueError, "process changed"):
                self.run_reader(device)
            self.assertFalse(self.output.exists())

    def test_receipt_without_deployed_pid_is_rejected_before_contact(self):
        receipt = json.loads(self.receipt.read_text())
        receipt["after"] = "incomplete"
        self.receipt.write_text(json.dumps(receipt))
        device = DeviceFixture([])
        with self.assertRaisesRegex(ValueError, "deployment PID"):
            self.run_reader(device)
        self.assertEqual(device.commands, [])

    def test_truncated_page_read_is_rejected(self):
        device = DeviceFixture([self.raw])
        device.short_read = True
        with self.assertRaisesRegex(ValueError, "Incomplete proof memory read"):
            self.run_reader(device)

    def test_existing_evidence_is_preserved(self):
        self.output.write_text("preserve")
        device = DeviceFixture([])
        with self.assertRaisesRegex(ValueError, "new evidence file"):
            self.run_reader(device)
        self.assertEqual(self.output.read_text(), "preserve")
        self.assertEqual(device.commands, [])

    def test_multiple_samples_keep_their_own_observed_values(self):
        later = self.changed(frames=2000, commands_run=8)
        result = self.run_reader(DeviceFixture([self.raw, self.raw, later, later]), samples=2)
        self.assertEqual([sample["commands_run"] for sample in result["samples"]], [7, 8])
        self.assertFalse(result["atomic_snapshot"])

    def test_zero_samples_is_rejected_before_contact(self):
        device = DeviceFixture([])
        with self.assertRaisesRegex(ValueError, "at least one sample"):
            self.run_reader(device, samples=0)
        self.assertEqual(device.commands, [])

    def test_source_declaration_agrees_with_fixture_offsets(self):
        source = (FIXTURES / "xz_shell_proof.h").read_text()
        body = source.split("struct xz_shell_proof {", 1)[1].split("};", 1)[0]
        body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
        names = []
        for declaration in body.split(";"):
            declaration = declaration.strip()
            if not declaration:
                continue
            self.assertTrue(declaration.startswith("uint32_t "))
            for member in declaration[len("uint32_t "):].split(","):
                name, length = re.fullmatch(r"\s*(\w+)(?:\[(\d+)\])?\s*", member).groups()
                names.extend([name] if length is None else
                             [f"{name}[{i}]" for i in range(int(length))])
        aliases = {f"model_guard_failures[{i}]": f"model_guard_r{i + 3}" for i in range(4)}
        aliases.update({f"library_tracks[{i}]": f"library_tracks_usb{i + 1}" for i in range(2)})
        self.assertEqual({aliases.get(name, name): i * 4 for i, name in enumerate(names)},
                         self.fixture["offsets"])


if __name__ == "__main__":
    unittest.main()
