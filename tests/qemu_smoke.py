#!/usr/bin/env python3
import argparse
import pathlib
import socket
import subprocess
import sys
import tempfile
import time
import secrets


def wait_for_text(log_path, expected, process, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if log_path.exists():
            output = log_path.read_text(errors="replace")
            if expected in output:
                return output
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited with status {process.returncode}")
        time.sleep(0.05)
    output = log_path.read_text(errors="replace") if log_path.exists() else ""
    raise TimeoutError(f"Timed out waiting for {expected!r}\n{output}")


def wait_for_count(log_path, expected, count, process, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        output = log_path.read_text(errors="replace") if log_path.exists() else ""
        if output.count(expected) >= count:
            return output
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited with status {process.returncode}")
        time.sleep(0.05)
    output = log_path.read_text(errors="replace") if log_path.exists() else ""
    raise TimeoutError(
        f"Timed out waiting for {count} occurrences of {expected!r}\n{output}"
    )


def monitor_connect(port, process, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited with status {process.returncode}")
        try:
            connection = socket.create_connection(("127.0.0.1", port), timeout=1)
            connection.settimeout(2)
            connection.recv(4096)
            return connection
        except OSError:
            time.sleep(0.05)
    raise TimeoutError("Could not connect to the QEMU monitor")


def send_key(monitor, key):
    monitor.sendall(f"sendkey {key}\n".encode("ascii"))
    time.sleep(0.08)


def run_command(monitor, command):
    qemu_keys = {
        "a": "q",
        "z": "w",
        "q": "a",
        "w": "z",
        "m": "semicolon",
    }
    for character in command:
        if character == " ":
            key = "spc"
        elif character.isdigit():
            key = f"shift-{character}"
        else:
            key = qemu_keys.get(character, character)
        send_key(monitor, key)
    send_key(monitor, "ret")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", default="build/os.img")
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--memory-mib", type=int, default=128)
    parser.add_argument("--uefi-firmware")
    args = parser.parse_args()
    if args.memory_mib < 128:
        parser.error("QEMU memory must be at least 128 MiB")

    image = pathlib.Path(args.image).resolve()
    if not image.is_file():
        parser.error(f"OS image not found: {image}")

    with tempfile.TemporaryDirectory(prefix="os-qemu-test-") as temporary:
        serial_log = pathlib.Path(temporary) / "serial.log"
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            udp_port = reservation.getsockname()[1]
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            tcp_port = reservation.getsockname()[1]
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]

        command = [args.qemu]
        if args.uefi_firmware:
            command.extend([
                "-machine",
                "pc",
                "-m",
                f"{args.memory_mib}M",
                "-drive",
                f"if=pflash,format=raw,unit=0,file={args.uefi_firmware},readonly=on",
                "-drive",
                f"format=raw,if=ide,index=0,file={image}",
            ])
        else:
            command.extend([
                "-m",
                f"{args.memory_mib}M",
                "-drive",
                f"format=qcow2,file={image}",
            ])
        command.extend([
            "-netdev",
            f"user,id=net0,hostfwd=udp:127.0.0.1:{udp_port}-:5555,"
            f"hostfwd=tcp:127.0.0.1:{tcp_port}-:5556",
            "-device",
            "rtl8139,netdev=net0",
            "-display",
            "none",
            "-serial",
            f"file:{serial_log}",
            "-monitor",
            f"tcp:127.0.0.1:{port},server,nowait",
            "-no-reboot",
        ])
        process = subprocess.Popen(
            command,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        monitor = None
        try:
            wait_for_text(serial_log, "kernel: boot complete", process)
            output = wait_for_text(serial_log, "memory: free_pages=", process)
            wait_for_text(
                serial_log,
                "net: IPv4 10.0.2.15 UDP echo port 5555 TCP echo port 5556",
                process,
            )
            initial_pages = int(output.split("memory: free_pages=", 1)[1].split()[0])
            if initial_pages == 0:
                raise AssertionError("The physical page allocator reported no free RAM")
            if args.memory_mib > 1024 and initial_pages <= 262144:
                raise AssertionError(
                    "The physical allocator did not expose memory above 1 GiB"
                )

            monitor = monitor_connect(port, process)
            payload = b"copilot-kernel-" + secrets.token_bytes(8)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                udp.settimeout(1)
                echoed = None
                for _ in range(5):
                    udp.sendto(payload, ("127.0.0.1", udp_port))
                    try:
                        echoed, _ = udp.recvfrom(2048)
                        break
                    except socket.timeout:
                        continue
                if echoed != payload:
                    raise AssertionError(
                        f"RTL8139 UDP echo mismatch: {echoed!r}"
                    )

            tcp_payload = b"tcp-kernel-" + secrets.token_bytes(8)
            with socket.create_connection(("127.0.0.1", tcp_port), timeout=3) as tcp:
                tcp.settimeout(3)
                tcp.sendall(tcp_payload)
                echoed = bytearray()
                while len(echoed) < len(tcp_payload):
                    chunk = tcp.recv(len(tcp_payload) - len(echoed))
                    if not chunk:
                        break
                    echoed.extend(chunk)
                if bytes(echoed) != tcp_payload:
                    raise AssertionError(
                        f"RTL8139 TCP echo mismatch: {bytes(echoed)!r}"
                    )

            run_command(monitor, "test")
            wait_for_text(serial_log, "heap: kernel allocator passed", process)
            run_command(monitor, "pci")
            wait_for_text(serial_log, "pci: devices=", process)
            wait_for_text(serial_log, "vendor=10EC device=8139", process)
            cases = (
                ("badptr", "process: exit pid=1 code=0", 1),
                ("write", "process: exit pid=2 code=14", 2),
                ("kaccess", "process: exit pid=3 code=14", 3),
                ("heaptest", "process: exit pid=4 code=0", 4),
                ("waittest", "process: exit pid=5 code=0", 5),
                ("nx", "process: exit pid=7 code=14", 7),
            )
            for program, expected, pid in cases:
                run_command(monitor, program)
                wait_for_text(serial_log, expected, process)
                run_command(monitor, f"wait {pid}")
                wait_for_text(serial_log, f"shell: reap pid={pid} result=0", process)

            run_command(monitor, "put note hello world")
            wait_for_text(serial_log, "tinyfs: file stored", process)
            run_command(monitor, "cat note")
            wait_for_text(serial_log, "shell: cat succeeded", process)
            run_command(monitor, "rm note")
            wait_for_text(serial_log, "tinyfs: file removed", process)

            output = wait_for_count(
                serial_log,
                f"memory: free_pages={initial_pages}\n",
                7,
                process,
            )
            final_pages = int(
                output.rsplit("memory: free_pages=", 1)[1].split()[0]
            )
            if final_pages != initial_pages:
                raise AssertionError(
                    f"Page leak: started with {initial_pages}, ended with {final_pages}"
                )

            output = serial_log.read_text(errors="replace")
            if output.count("exception: user vector=14") != 3:
                raise AssertionError("Expected three isolated user page faults")
            print("QEMU smoke tests passed: boot, UDP/TCP echo, PCI, heaps,")
            print("TinyFS writes, ELF, processes, isolation and memory reclamation.")
        finally:
            if monitor:
                monitor.close()
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            if process.stderr:
                errors = process.stderr.read().decode(errors="replace")
                if errors.strip():
                    print(errors, end="", file=sys.stderr)


if __name__ == "__main__":
    main()
