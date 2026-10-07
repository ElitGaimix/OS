#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

usage() {
    cat <<'EOF'
Usage: ./build.sh [build|run|uefi|test|uefi-test|clean]

  build  Compile the OS and create build/os.img (default)
  run    Build the OS and launch it in QEMU
  uefi   Build and launch the alternate UEFI boot path
  test   Build the OS and run QEMU integration tests
  uefi-test  Run the QEMU integration tests via UEFI
  clean  Remove generated build files
EOF
}

if (( $# > 1 )); then
    usage >&2
    exit 2
fi

action="${1:-build}"
case "$action" in
    build) target=all ;;
    run) target=run ;;
    uefi) target=uefi ;;
    test) target=test ;;
    uefi-test) target=test-uefi ;;
    clean)
        command -v make >/dev/null 2>&1 || {
            printf 'Missing required tool: make\n' >&2
            exit 1
        }
        exec make clean
        ;;
    -h|--help|help)
        usage
        exit 0
        ;;
    *)
        printf 'Unknown action: %s\n' "$action" >&2
        usage >&2
        exit 2
        ;;
esac

if [[ "$(uname -s)" != Linux ]]; then
    printf 'This build script is intended for Linux.\n' >&2
    exit 1
fi

required_tools=(make clang ld.lld nasm objcopy qemu-img python3)
if [[ "$action" == run ]]; then
    required_tools+=(qemu-system-x86_64)
fi
if [[ "$action" == uefi || "$action" == test || "$action" == uefi-test ]]; then
    required_tools+=(qemu-system-x86_64)
fi
if [[ "$action" == uefi || "$action" == uefi-test ]]; then
    required_tools+=(lld-link)
fi

missing_tools=()
for tool in "${required_tools[@]}"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        missing_tools+=("$tool")
    fi
done

if (( ${#missing_tools[@]} > 0 )); then
    printf 'Missing required tools: %s\n' "${missing_tools[*]}" >&2
    printf 'On Ubuntu/Debian, install them with:\n' >&2
    printf '  sudo apt update && sudo apt install build-essential clang lld nasm qemu-system-x86 qemu-utils\n' >&2
    exit 1
fi

exec make "$target"
