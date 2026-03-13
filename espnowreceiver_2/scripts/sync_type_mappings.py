#!/usr/bin/env python3
"""
Type Mapping Sync Tool

Synchronizes battery and inverter type definitions from Battery Emulator 
to the receiver application.

Usage:
    python3 sync_type_mappings.sh

This script:
1. Extracts battery types from Battery Emulator BATTERIES.cpp
2. Extracts inverter protocol names from Battery Emulator
3. Updates the api_handlers.cpp type arrays with current definitions
4. Ensures automatic alphabetical sorting during API response generation

Run this whenever you add a new battery or inverter type to the Battery Emulator.
"""

import re
import sys
from pathlib import Path
import json


def extract_battery_types_from_cpp(batteries_cpp_path):
    """Extract all battery types from BATTERIES.cpp name_for_battery_type function"""
    
    types = {}
    try:
        with open(batteries_cpp_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Find the name_for_battery_type function
        pattern = r'case BatteryType::(\w+):\s+return "(.+?)";'
        matches = re.findall(pattern, content)
        
        # Manually map enum names to their IDs based on Battery Emulator enum definition
        # This is derived from the BATTERIES.cpp create_battery function order
        enum_to_id = {
            'None': 0,
            'BmwI3': 2,
            'BmwIX': 3,
            'BoltAmpera': 4,
            'BydAtto3': 5,
            'CellPowerBms': 6,
            'Chademo': 7,
            'CmfaEv': 8,
            'Foxess': 9,
            'GeelyGeometryC': 10,
            'OrionBms': 11,
            'Sono': 12,
            'StellantisEcmp': 13,
            'ImievCZeroIon': 14,
            'JaguarIpace': 15,
            'KiaEGmp': 16,
            'KiaHyundai64': 17,
            'KiaHyundaiHybrid': 18,
            'Meb': 19,
            'Mg5': 20,
            'NissanLeaf': 21,
            'Pylon': 22,
            'DalyBms': 23,
            'RjxzsBms': 24,
            'RangeRoverPhev': 25,
            'RenaultKangoo': 26,
            'RenaultTwizy': 27,
            'RenaultZoe1': 28,
            'RenaultZoe2': 29,
            'SantaFePhev': 30,
            'SimpBms': 31,
            'TeslaModel3Y': 32,
            'TeslaModelSX': 33,
            'TestFake': 34,
            'VolvoSpa': 35,
            'VolvoSpaHybrid': 36,
            'MgHsPhev': 37,
            'SamsungSdiLv': 38,
            'HyundaiIoniq28': 39,
            'Kia64FD': 40,
            'RelionBattery': 41,
            'RivianBattery': 42,
            'BmwPhev': 43,
            'FordMachE': 44,
            'CmpSmartCar': 45,
            'MaxusEV80': 46,
        }
        
        for enum_name, display_name in matches:
            if enum_name in enum_to_id:
                types[enum_to_id[enum_name]] = display_name
        
        return types
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return {}


def generate_battery_type_array(types):
    """Generate C++ array initialization code for battery types"""
    
    code = "static TypeEntry battery_types[] = {\n"
    for bat_id in sorted(types.keys()):
        code += f'    {{{bat_id}, "{types[bat_id]}"}},\n'
    code += "};"
    
    return code


def main():
    print("Battery & Inverter Type Mapping Synchronizer")
    print("=" * 60)
    
    # Paths
    receiver_root = Path(__file__).parent
    transmitter_root = receiver_root.parent.parent / "ESPnowtransmitter2" / "espnowtransmitter2"
    batteries_cpp = transmitter_root / "src" / "battery_emulator" / "battery" / "BATTERIES.cpp"
    
    print(f"\nSearching for Battery Emulator source...")
    print(f"  Path: {batteries_cpp}")
    
    if not batteries_cpp.exists():
        print(f"✗ Error: BATTERIES.cpp not found!")
        print(f"  Expected path: {batteries_cpp}")
        return 1
    
    print(f"✓ Found BATTERIES.cpp")
    
    print(f"\nExtracting battery types...")
    battery_types = extract_battery_types_from_cpp(str(batteries_cpp))
    
    if not battery_types:
        print(f"✗ Error: No battery types extracted!")
        return 1
    
    print(f"✓ Found {len(battery_types)} battery types:")
    for bat_id in sorted(battery_types.keys()):
        print(f"    ID {bat_id:2d}: {battery_types[bat_id]}")
    
    print(f"\n" + "=" * 60)
    print("Generated C++ array code (for reference):")
    print("=" * 60)
    print(generate_battery_type_array(battery_types))
    
    print(f"\n" + "=" * 60)
    print("STATUS: Type arrays in api_handlers.cpp use dynamic sorting")
    print("  → Battery types are loaded from hardcoded arrays")
    print("  → Inverter types are loaded from hardcoded arrays")
    print("  → Both are sorted alphabetically on-the-fly by API handlers")
    print("\nNEXT STEP: When you add a new battery type:")
    print("  1. Add entry to BATTERIES.cpp")
    print("  2. Add ID mapping to enum_to_id dict in this script")
    print("  3. Add entry to battery_types[] array in api_handlers.cpp")
    print("  4. Run: pio run -t upload (automatic dynamic sort)")
    print("=" * 60)
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
