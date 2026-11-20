"""report.py - parse the m24sr-nfc report block (layout of src/report.h)."""
import struct

MAGIC = 0x4E33424D
VERSION = 1
HEADER = struct.Struct("<16I")
FIELDS = ["magic", "version", "status", "step", "error", "last_sw", "i2c_errors",
          "i2c_nacks", "recover_pulses", "busy_polls", "wtx_count", "fault_pc",
          "fault_cfsr", "cc_len", "ndef_len", "log_len"]
CC_SIZE, NDEF_MAX, LOG_SIZE = 16, 512, 1536
STATUS = {1: "RUNNING", 2: "DONE", 3: "FAILED", 4: "FAULT"}
SIZE = HEADER.size + CC_SIZE + NDEF_MAX + LOG_SIZE


def parse(blob):
    if len(blob) < SIZE:
        raise ValueError("report block too short")
    h = dict(zip(FIELDS, HEADER.unpack_from(blob, 0)))
    if h["magic"] != MAGIC:
        raise ValueError(f"bad magic 0x{h['magic']:08X}: is the firmware running?")
    if h["version"] != VERSION:
        raise ValueError(f"report version {h['version']}, expected {VERSION}")
    h["error"] = struct.unpack("<i", struct.pack("<I", h["error"]))[0]
    off = HEADER.size
    h["cc"] = blob[off:off + min(h["cc_len"], CC_SIZE)]
    off += CC_SIZE
    h["ndef"] = blob[off:off + min(h["ndef_len"], NDEF_MAX)]
    off += NDEF_MAX
    h["log"] = blob[off:off + min(h["log_len"], LOG_SIZE)].decode("ascii", "replace")
    return h


def to_text(h):
    lines = [
        f"status {STATUS.get(h['status'], h['status'])}, step {h['step']}, error {h['error']},"
        f" SW {h['last_sw']:04X}",
        f"I2C errors {h['i2c_errors']}, NACKs {h['i2c_nacks']}, busy polls {h['busy_polls']},"
        f" WTX {h['wtx_count']}, bus recovery pulses {h['recover_pulses']}",
    ]
    if h["status"] == 4:
        lines.append(f"FAULT at pc 0x{h['fault_pc']:08X}, CFSR 0x{h['fault_cfsr']:08X}")
    if h["ndef"]:
        lines.append("NDEF: " + h["ndef"].hex(" "))
    lines.append("--- log ---")
    lines.append(h["log"].rstrip())
    return "\n".join(lines) + "\n"
