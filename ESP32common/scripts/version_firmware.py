"""
PlatformIO Build Script
- Pre-build: Generates dynamic build flags (DEVICE_HARDWARE, BUILD_DATE, PIO_ENV_NAME)
- Post-build: Renames firmware binary to include version information

Usage:
    Add to platformio.ini:
    extra_scripts = post:../../esp32common/scripts/version_firmware.py
"""

Import('env')
import os
import shutil
import re
import time

def get_version_from_flags(env):
    """Extract version numbers from build flags"""
    build_flags = env.Flatten(env.get("BUILD_FLAGS", []))
    
    version = {
        'major': None,
        'minor': None,
        'patch': None
    }
    
    for flag in build_flags:
        flag_str = str(flag)
        
        # Match -D FW_VERSION_MAJOR=X
        if 'FW_VERSION_MAJOR=' in flag_str:
            match = re.search(r'FW_VERSION_MAJOR=(\d+)', flag_str)
            if match:
                version['major'] = match.group(1)
        
        # Match -D FW_VERSION_MINOR=X
        elif 'FW_VERSION_MINOR=' in flag_str:
            match = re.search(r'FW_VERSION_MINOR=(\d+)', flag_str)
            if match:
                version['minor'] = match.group(1)
        
        # Match -D FW_VERSION_PATCH=X
        elif 'FW_VERSION_PATCH=' in flag_str:
            match = re.search(r'FW_VERSION_PATCH=(\d+)', flag_str)
            if match:
                version['patch'] = match.group(1)
    
    return version

def rename_firmware(source, target, env):
    """Rename firmware binary with version suffix"""
    
    # Get version numbers from build flags
    version = get_version_from_flags(env)
    
    if not all([version['major'], version['minor'], version['patch']]):
        print("Warning: Could not extract all version numbers from build flags")
        print(f"  Found: MAJOR={version['major']}, MINOR={version['minor']}, PATCH={version['patch']}")
        return
    
    # Get the firmware binary path
    firmware_source = str(target[0])
    
    if not os.path.exists(firmware_source):
        print(f"Warning: Firmware file not found: {firmware_source}")
        return
    
    # Get environment name
    env_name = env.get('PIOENV', 'unknown')
    
    # Create new filename with version
    firmware_dir = os.path.dirname(firmware_source)
    original_name = os.path.basename(firmware_source)
    name_parts = os.path.splitext(original_name)
    
    # Format: env_fw_MAJOR_MINOR_PATCH.bin
    versioned_name = f"{env_name}_fw_{version['major']}_{version['minor']}_{version['patch']}{name_parts[1]}"
    versioned_path = os.path.join(firmware_dir, versioned_name)
    
    try:
        # Copy the firmware with versioned name
        shutil.copy2(firmware_source, versioned_path)
        print(f"✓ Created versioned firmware: {versioned_name}")
        print(f"  Location: {versioned_path}")
    except Exception as e:
        print(f"Error creating versioned firmware: {e}")

def generate_build_metadata(env):
    """Generate dynamic build-time metadata flags"""
    
    # Generate build timestamp
    build_date = time.strftime('%d-%m-%Y %H:%M:%S')
    
    # Extract device hardware from board setting (e.g., "esp32-poe2" -> "ESP32-POE2")
    board = env.get('BOARD', 'unknown').upper().replace('-', '_')
    
    # Get environment name
    env_name = env.get('PIOENV', 'unknown')
    
    # Append dynamic definitions to build flags
    env.Append(CPPDEFINES=[
        ('DEVICE_HARDWARE', f'\\"{board}\\"'),
        ('PIO_ENV_NAME', f'\\"{env_name}\\"'),
        ('BUILD_DATE', f'\\"{build_date}\\"'),
    ])
    
    print(f"  Dynamic metadata: DEVICE_HARDWARE={board}, ENV={env_name}")

# Generate build metadata before compilation
generate_build_metadata(env)

# Register the post-build action
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", rename_firmware)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", rename_firmware)

print("=" * 60)
print("Firmware Versioning Script Loaded")
print("=" * 60)
