#!/usr/bin/env python3
import re
import subprocess
import sys

ADDR2LINE = "../toolchain/local/bin/i686-pc-minima-addr2line"
BINARY = "kernel.bin"

EIP_RE = re.compile(r"(eip:0x([0-9A-Fa-f]+))")

def symbolicate(addr):
    try:
        result = subprocess.run(
            [ADDR2LINE, "-f", "-C", "-e", BINARY, f"0x{addr}"],
            capture_output=True,
            text=True,
        )
        return result.stdout.strip().replace("\n", " @ ")
    except Exception as e:
        return f"<error: {e}>"

def process_line(line):
    matches = EIP_RE.findall(line)

    if not matches:
        return line

    out = line.rstrip("\n")
    for full, addr in matches:
        if addr == "00000000":
            continue
        sym = symbolicate(addr)
        out += f" ==> {sym}"

    return out + "\n"

def main():
    try:
        for line in sys.stdin:
            sys.stdout.write(process_line(line))
            sys.stdout.flush()
    except KeyboardInterrupt:
        sys.exit(0)

if __name__ == "__main__":
    main()
