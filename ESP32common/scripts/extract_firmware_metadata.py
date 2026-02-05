#!/usr/bin/env python3
"""
Extract and display firmware metadata from .bin files

Usage: python extract_firmware_metadata.py <firmware.bin>
"""

import sys
import struct

MAGIC_START = 0x464D5441  # "FMTA"
MAGIC_END = 0x454E4446    # "ENDF"

def read_cstring(data, offset, max_len):
    """Read null-terminated string from binary data"""
    end = offset
    while end < offset + max_len and data[end] != 0:
        end += 1
    return data[offset:end].decode('utf-8', errors='ignore')

def extract_metadata(filename):
    """Extract metadata from firmware binary"""
    with open(filename, 'rb') as f:
        data = f.read()
    
    print(f"Searching for metadata in {filename} ({len(data)} bytes)...")
    
    # Search for magic start marker
    for i in range(len(data) - 128):
        magic = struct.unpack('<I', data[i:i+4])[0]
        if magic == MAGIC_START:
            # Verify end marker
            magic_end = struct.unpack('<I', data[i+124:i+128])[0]
            if magic_end == MAGIC_END:
                print(f"\n✓ Found metadata at offset: 0x{i:08X}\n")
                
                # Extract fields
                env_name = read_cstring(data, i+4, 32)
                device_type = read_cstring(data, i+36, 16)
                version_major = data[i+52]
                version_minor = data[i+53]
                version_patch = data[i+54]
                build_date = read_cstring(data, i+56, 48)
                
                # Display
                print(f"Environment:  {env_name}")
                print(f"Device Type:  {device_type}")
                print(f"Version:      {version_major}.{version_minor}.{version_patch}")
                print(f"Build Date:   {build_date}")
                print(f"\nMetadata is VALID ●")
                return True
    
    print("\n✗ No metadata found")
    return False

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print("Usage: python extract_firmware_metadata.py <firmware.bin>")
        sys.exit(1)
    
    extract_metadata(sys.argv[1])
