#!/usr/bin/env python3
import argparse
import pathlib
import struct

SECTOR_SIZE = 512
PARTITION_START = 4096
PARTITION_SECTORS = 16384
ROOT_ENTRY_COUNT = 512
ROOT_DIRECTORY_SECTORS = ROOT_ENTRY_COUNT * 32 // SECTOR_SIZE


def short_entry(name, extension, attributes, first_cluster, size=0):
    entry = bytearray(32)
    entry[0:8] = name.ljust(8, b" ")
    entry[8:11] = extension.ljust(3, b" ")
    entry[11] = attributes
    struct.pack_into("<H", entry, 26, first_cluster)
    struct.pack_into("<I", entry, 28, size)
    return entry


def set_fat_entry(fat, cluster, value):
    struct.pack_into("<H", fat, cluster * 2, value)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output")
    parser.add_argument("efi_application")
    parser.add_argument("kernel")
    parser.add_argument("base_disk")
    args = parser.parse_args()

    efi_application = pathlib.Path(args.efi_application).read_bytes()
    kernel = pathlib.Path(args.kernel).read_bytes()
    base_disk = pathlib.Path(args.base_disk).read_bytes()
    startup_script = b"EFI\\BOOT\\BOOTX64.EFI\r\n"
    total_sectors = PARTITION_START + PARTITION_SECTORS
    disk = bytearray(total_sectors * SECTOR_SIZE)
    disk[:min(len(base_disk), len(disk))] = base_disk[:len(disk)]

    mbr = bytearray(SECTOR_SIZE)
    partition = 446
    mbr[partition] = 0x80
    mbr[partition + 1:partition + 4] = b"\xFF\xFF\xFF"
    mbr[partition + 4] = 0xEF
    mbr[partition + 5:partition + 8] = b"\xFF\xFF\xFF"
    struct.pack_into("<II", mbr, partition + 8, PARTITION_START, PARTITION_SECTORS)
    mbr[510:512] = b"\x55\xAA"
    disk[:SECTOR_SIZE] = mbr

    fat_sectors = 1
    while True:
        clusters = (
            PARTITION_SECTORS - 1 - 2 * fat_sectors - ROOT_DIRECTORY_SECTORS
        )
        required = ((clusters + 2) * 2 + SECTOR_SIZE - 1) // SECTOR_SIZE
        if required == fat_sectors:
            break
        fat_sectors = required
    if not 4085 <= clusters < 65525:
        parser.error("generated ESP is not a valid FAT16 volume")

    boot = bytearray(SECTOR_SIZE)
    boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"COPILOT "
    struct.pack_into("<HBHBHHBHHHII", boot, 11,
                     SECTOR_SIZE, 1, 1, 2, ROOT_ENTRY_COUNT, 0, 0xF8,
                     fat_sectors, 63, 255, PARTITION_START, PARTITION_SECTORS)
    boot[36] = 0x80
    boot[38] = 0x29
    struct.pack_into("<I", boot, 39, 0x4F534F53)
    boot[43:54] = b"COPILOT EFI"
    boot[54:62] = b"FAT16   "
    boot[510:512] = b"\x55\xAA"

    volume_offset = PARTITION_START * SECTOR_SIZE
    disk[volume_offset:volume_offset + SECTOR_SIZE] = boot
    fat = bytearray(fat_sectors * SECTOR_SIZE)
    set_fat_entry(fat, 0, 0xFFF8)
    set_fat_entry(fat, 1, 0xFFFF)
    set_fat_entry(fat, 2, 0xFFFF)
    set_fat_entry(fat, 3, 0xFFFF)
    next_cluster = 4
    file_chains = {}
    for name, contents in (
        ("BOOTX64", efi_application),
        ("KERNEL", kernel),
        ("STARTUP", startup_script),
    ):
        cluster_count = (len(contents) + SECTOR_SIZE - 1) // SECTOR_SIZE
        clusters_for_file = list(range(next_cluster, next_cluster + cluster_count))
        if clusters_for_file:
            for index, cluster in enumerate(clusters_for_file):
                next_value = (
                    clusters_for_file[index + 1]
                    if index + 1 < len(clusters_for_file)
                    else 0xFFFF
                )
                set_fat_entry(fat, cluster, next_value)
        file_chains[name] = (clusters_for_file, contents)
        next_cluster += cluster_count
    if next_cluster >= clusters + 2:
        parser.error("EFI files do not fit in the generated FAT16 volume")

    fat_start = volume_offset + SECTOR_SIZE
    disk[fat_start:fat_start + len(fat)] = fat
    second_fat = fat_start + fat_sectors * SECTOR_SIZE
    disk[second_fat:second_fat + len(fat)] = fat
    root_start = second_fat + fat_sectors * SECTOR_SIZE
    data_start = root_start + ROOT_DIRECTORY_SECTORS * SECTOR_SIZE

    root = bytearray(ROOT_DIRECTORY_SECTORS * SECTOR_SIZE)
    root[0:32] = short_entry(b"EFI", b"", 0x10, 2)
    root[32:64] = short_entry(
        b"STARTUP", b"NSH", 0x20,
        file_chains["STARTUP"][0][0], len(startup_script)
    )
    disk[root_start:root_start + len(root)] = root

    def cluster_offset(cluster):
        return data_start + (cluster - 2) * SECTOR_SIZE

    efi_directory = bytearray(SECTOR_SIZE)
    efi_directory[0:32] = short_entry(b".", b"", 0x10, 2)
    efi_directory[32:64] = short_entry(b"..", b"", 0x10, 0)
    efi_directory[64:96] = short_entry(b"BOOT", b"", 0x10, 3)
    disk[cluster_offset(2):cluster_offset(2) + SECTOR_SIZE] = efi_directory

    boot_directory = bytearray(SECTOR_SIZE)
    boot_directory[0:32] = short_entry(b".", b"", 0x10, 3)
    boot_directory[32:64] = short_entry(b"..", b"", 0x10, 2)
    boot_directory[64:96] = short_entry(b"BOOTX64", b"EFI", 0x20,
                                         file_chains["BOOTX64"][0][0],
                                         len(efi_application))
    boot_directory[96:128] = short_entry(b"KERNEL", b"BIN", 0x20,
                                          file_chains["KERNEL"][0][0],
                                          len(kernel))
    disk[cluster_offset(3):cluster_offset(3) + SECTOR_SIZE] = boot_directory

    for clusters_for_file, contents in file_chains.values():
        for index, cluster in enumerate(clusters_for_file):
            source_start = index * SECTOR_SIZE
            data = contents[source_start:source_start + SECTOR_SIZE]
            destination = cluster_offset(cluster)
            disk[destination:destination + len(data)] = data

    pathlib.Path(args.output).write_bytes(disk)


if __name__ == "__main__":
    main()
