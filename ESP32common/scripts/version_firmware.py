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
    
    # Guard against multiple invocations during build process
    if env.get('__FIRMWARE_METADATA_GENERATED__'):
        return
    
    # Generate build timestamp (canonical format: DD MM YYYY HH:MM:SS)
    build_date = time.strftime('%d %m %Y %H:%M:%S')
    
    # Touch firmware_metadata.cpp to force SCons to recompile it every build.
    # Without this, SCons sees the source file unchanged and uses the cached .o,
    # so the BUILD_DATE define added dynamically here never makes it into the binary.
    # Note: __file__ is not available in PlatformIO's exec() context, so we resolve
    # the path via LIBSOURCE_DIRS (which maps to lib_extra_dirs in platformio.ini).
    metadata_src = None

    # 1) Try library source dirs advertised by SCons/PlatformIO
    for lib_dir in env.get('LIBSOURCE_DIRS', []):
        candidate = os.path.normpath(os.path.join(str(lib_dir), 'firmware_metadata', 'firmware_metadata.cpp'))
        if os.path.exists(candidate):
            metadata_src = candidate
            break

    # 2) Fallback: discover esp32common as a sibling/ancestor relative to PROJECT_DIR
    #    Handles layouts like:
    #      <root>/espnowreceiver_LCD + <root>/esp32common
    #      <root>/ESPnowtransmitter2/espnowtransmitter2 + <root>/esp32common
    if not metadata_src:
        project_dir = os.path.normpath(str(env.get('PROJECT_DIR', '')))
        if project_dir:
            probe = project_dir
            for _ in range(6):
                candidate = os.path.join(probe, 'esp32common', 'firmware_metadata', 'firmware_metadata.cpp')
                candidate = os.path.normpath(candidate)
                if os.path.exists(candidate):
                    metadata_src = candidate
                    break

                candidate_alt = os.path.join(probe, 'ESP32 Common', 'firmware_metadata', 'firmware_metadata.cpp')
                candidate_alt = os.path.normpath(candidate_alt)
                if os.path.exists(candidate_alt):
                    metadata_src = candidate_alt
                    break

                parent = os.path.dirname(probe)
                if parent == probe:
                    break
                probe = parent
    if metadata_src:
        os.utime(metadata_src, None)  # update mtime to now
        print(f"  Touched firmware_metadata.cpp to force recompile: {metadata_src}")
    else:
        print("  Warning: Could not find firmware_metadata.cpp to touch - BUILD_DATE may be stale")
    
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
    
    # Mark as generated to prevent multiple invocations
    env['__FIRMWARE_METADATA_GENERATED__'] = True
    
    print(f"  Dynamic metadata: DEVICE_HARDWARE={board}, ENV={env_name}")

# Generate build metadata before compilation
generate_build_metadata(env)

# Register the post-build action
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", rename_firmware)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", rename_firmware)

print("=" * 60)
print("Firmware Versioning Script Loaded")
print("=" * 60)
