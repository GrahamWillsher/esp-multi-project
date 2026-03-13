"""
PlatformIO Build Hook - Generates type mappings before compilation

This hook runs as part of the PlatformIO build process to ensure battery and 
inverter type arrays stay synchronized with the Battery Emulator source.

Place this in: <project_root>/platformio_pre_build.py
Configure in platformio.ini with: extra_scripts = platformio_pre_build.py
"""

import os
import sys
from pathlib import Path
import subprocess

def run_pre_build_hook(env, config):
    """Run before PlatformIO compilation starts"""
    
    # Define paths
    root = Path(env.subst("$PROJECT_DIR"))
    script_path = root / ".." / ".." / "esp32common" / "scripts" / "generate_type_mappings.py"
    battery_emulator_path = root / ".." / ".." / "ESPnowtransmitter2" / "espnowtransmitter2" / "src" / "battery_emulator" / "battery" / "BATTERIES.cpp"
    output_path = root / "lib" / "webserver" / "api" / "type_mappings_generated.h"
    
    print(f"\n[TypeMapping] Checking paths...")
    print(f"  Script: {script_path}")
    print(f"  Source: {battery_emulator_path}")
    print(f"  Output: {output_path}")
    
    # Check if source exists
    if not battery_emulator_path.exists():
        print(f"[TypeMapping] Warning: Battery Emulator source not found: {battery_emulator_path}")
        print(f"[TypeMapping] Skipping type mapping generation")
        return
    
    # Check if script exists
    if not script_path.exists():
        print(f"[TypeMapping] Error: Generator script not found: {script_path}")
        return
    
    print(f"\n[TypeMapping] Generating type mappings...")
    
    try:
        # Run the generator script
        result = subprocess.run(
            [sys.executable, str(script_path), str(battery_emulator_path), str(output_path)],
            capture_output=True,
            text=True,
            timeout=30
        )
        
        if result.returncode == 0:
            print(f"[TypeMapping] ✓ Successfully generated type mappings")
            print(result.stdout)
        else:
            print(f"[TypeMapping] ✗ Generator script failed:")
            print(result.stderr)
            
    except subprocess.TimeoutExpired:
        print(f"[TypeMapping] ✗ Generator script timed out")
    except Exception as e:
        print(f"[TypeMapping] ✗ Error running generator: {e}")


# PlatformIO hook entry point
if __name__ == "__main__":
    # This runs during `pio run` before compilation
    import sys
    sys.path.insert(0, "${PROJECT_PLATFORMS_DIR}")
    
    from platformio.project.config import ProjectConfig
    config = ProjectConfig.get_default_path()
    env = {}
    env['subst'] = lambda x: str(x).replace("$PROJECT_DIR", os.getcwd())
    
    run_pre_build_hook(env, config)
