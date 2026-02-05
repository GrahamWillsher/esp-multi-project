#!/usr/bin/env python3
"""
Timezone Detection Test Tool
Tests IP geolocation and worldtimeapi.org timezone detection
"""

import requests
import json
import sys

def print_section(title):
    """Print a formatted section header"""
    print("\n" + "="*70)
    print(f" {title}")
    print("="*70)

def get_my_ip():
    """Get public IP address"""
    print_section("STEP 1: Get Public IP Address")
    try:
        response = requests.get("https://api.ipify.org?format=json", timeout=5)
        response.raise_for_status()
        ip = response.json()['ip']
        print(f"✓ Your public IP: {ip}")
        return ip
    except Exception as e:
        print(f"✗ Error getting IP: {e}")
        return None

def get_ip_geolocation(ip):
    """Get location from IP using ip-api.com"""
    print_section("STEP 2: IP Geolocation (ip-api.com)")
    try:
        url = f"http://ip-api.com/json/{ip}"
        print(f"Request: {url}")
        response = requests.get(url, timeout=5)
        response.raise_for_status()
        data = response.json()
        
        print("\nGeolocation Data:")
        print(f"  Country:   {data.get('country', 'N/A')}")
        print(f"  Region:    {data.get('regionName', 'N/A')}")
        print(f"  City:      {data.get('city', 'N/A')}")
        print(f"  Timezone:  {data.get('timezone', 'N/A')}")
        print(f"  Lat/Lon:   {data.get('lat', 'N/A')}, {data.get('lon', 'N/A')}")
        print(f"  ISP:       {data.get('isp', 'N/A')}")
        
        return data.get('timezone')
    except Exception as e:
        print(f"✗ Error getting geolocation: {e}")
        return None

def test_worldtimeapi_by_ip():
    """Test worldtimeapi.org /api/ip endpoint"""
    print_section("STEP 3: WorldTimeAPI - Auto-detect by IP")
    try:
        url = "http://worldtimeapi.org/api/ip"
        print(f"Request: {url}")
        print("(worldtimeapi will auto-detect timezone from the source IP of this request)")
        response = requests.get(url, timeout=10)
        response.raise_for_status()
        
        print(f"\nHTTP Status: {response.status_code}")
        print(f"Response Length: {len(response.text)} bytes")
        
        # Try to extract what IP worldtimeapi sees from the response
        data = response.json()
        if 'client_ip' in data:
            print(f"WorldTimeAPI sees your IP as: {data['client_ip']}")
        else:
            print("(worldtimeapi doesn't return the IP it sees in this endpoint)")
        
        print("\n--- RAW JSON RESPONSE ---")
        print(response.text)
        print("--- END JSON ---")
        
        data = response.json()
        
        print("\n--- PARSED FIELDS ---")
        print(f"  timezone:       {data.get('timezone', 'N/A')}")
        print(f"  abbreviation:   {data.get('abbreviation', 'N/A')}")
        print(f"  utc_offset:     {data.get('utc_offset', 'N/A')} ({data.get('utc_offset', 0)//3600:+d} hours)")
        print(f"  dst:            {data.get('dst', 'N/A')}")
        print(f"  dst_offset:     {data.get('dst_offset', 'N/A')} ({data.get('dst_offset', 0)//3600:+d} hours)")
        print(f"  dst_from:       {data.get('dst_from', 'N/A')}")
        print(f"  dst_until:      {data.get('dst_until', 'N/A')}")
        print(f"  datetime:       {data.get('datetime', 'N/A')}")
        
        return data
    except Exception as e:
        print(f"✗ Error querying worldtimeapi by IP: {e}")
        return None

def test_worldtimeapi_by_timezone(tz_name):
    """Test worldtimeapi.org with specific timezone"""
    print_section(f"STEP 4: WorldTimeAPI - Specific Timezone ({tz_name})")
    if not tz_name:
        print("✗ No timezone name provided")
        return None
        
    try:
        url = f"http://worldtimeapi.org/api/timezone/{tz_name}"
        print(f"Request: {url}")
        response = requests.get(url, timeout=10)
        response.raise_for_status()
        
        print(f"\nHTTP Status: {response.status_code}")
        
        data = response.json()
        
        print("\n--- PARSED FIELDS ---")
        print(f"  timezone:       {data.get('timezone', 'N/A')}")
        print(f"  abbreviation:   {data.get('abbreviation', 'N/A')}")
        print(f"  utc_offset:     {data.get('utc_offset', 'N/A')} ({data.get('utc_offset', 0)//3600:+d} hours)")
        print(f"  dst:            {data.get('dst', 'N/A')}")
        print(f"  dst_offset:     {data.get('dst_offset', 'N/A')} ({data.get('dst_offset', 0)//3600:+d} hours)")
        print(f"  datetime:       {data.get('datetime', 'N/A')}")
        
        return data
    except Exception as e:
        print(f"✗ Error querying worldtimeapi by timezone: {e}")
        return None

def generate_posix_tz(data):
    """Generate POSIX timezone string from worldtimeapi data"""
    print_section("STEP 5: Generate POSIX Timezone String")
    
    if not data:
        print("✗ No data to process")
        return None
    
    tz_name = data.get('timezone', '')
    abbr = data.get('abbreviation', 'UTC')
    utc_offset = data.get('utc_offset', 0)
    
    print(f"Input: timezone='{tz_name}', abbr='{abbr}', offset={utc_offset}s")
    
    offset_hours = utc_offset // 3600
    offset_mins = abs(utc_offset % 3600) // 60
    
    posix = abbr
    if utc_offset == 0:
        posix += "0"
    else:
        posix += str(-offset_hours)  # POSIX inverts sign
        if offset_mins > 0:
            posix += f":{offset_mins}"
    
    print(f"Base POSIX string: {posix}")
    
    # Check for DST rules
    dst_applied = False
    
    if "London" in tz_name or "Europe/London" in tz_name or abbr in ["GMT", "BST"]:
        posix += "BST-1,M3.5.0/1,M10.5.0/2"
        print(f"✓ Applied UK/GMT→BST DST rules")
        dst_applied = True
    elif "Europe" in tz_name and "London" not in tz_name:
        posix += "CEST-2,M3.5.0/2,M10.5.0/3"
        print(f"✓ Applied EU DST rules")
        dst_applied = True
    
    if not dst_applied:
        print("⚠ No DST rules applied")
    
    print(f"\nFinal POSIX TZ: {posix}")
    return posix

def main():
    """Main test sequence"""
    print("\n" + "="*70)
    print(" TIMEZONE DETECTION TEST TOOL")
    print("="*70)
    
    # Step 1: Get public IP
    ip = get_my_ip()
    if not ip:
        print("\n✗ Failed to get IP address")
        sys.exit(1)
    
    # Step 2: Get geolocation
    geo_timezone = get_ip_geolocation(ip)
    
    # Step 3: Test worldtimeapi by IP auto-detection
    worldtime_data = test_worldtimeapi_by_ip()
    
    # Compare IPs
    print_section("IP ADDRESS COMPARISON")
    print(f"Your public IP (from ipify.org):     {ip}")
    if worldtime_data and 'client_ip' in worldtime_data:
        print(f"IP seen by worldtimeapi:             {worldtime_data['client_ip']}")
        if ip != worldtime_data['client_ip']:
            print("⚠ WARNING: Different IPs detected! You may be behind NAT/proxy")
    else:
        print("IP seen by worldtimeapi:             (not returned in response)")
        print("Note: worldtimeapi uses the source IP of the HTTP request")
    
    # Step 4: Test worldtimeapi by specific timezone (if we got one from geolocation)
    if geo_timezone:
        test_worldtimeapi_by_timezone(geo_timezone)
    
    # Step 5: Generate POSIX string
    if worldtime_data:
        generate_posix_tz(worldtime_data)
    
    print_section("TEST COMPLETE")
    
    if worldtime_data:
        detected_tz = worldtime_data.get('timezone', 'Unknown')
        if detected_tz == "UTC" or detected_tz == "Etc/UTC":
            print("\n⚠ WARNING: worldtimeapi.org returned UTC timezone")
            print("This means IP geolocation failed - your IP may be:")
            print("  - Behind a VPN or proxy")
            print("  - From a datacenter/hosting provider")
            print("  - Not in worldtimeapi's geolocation database")
            print(f"\nSuggestion: Use the explicit timezone from ip-api.com: {geo_timezone}")
        else:
            print(f"\n✓ SUCCESS: Detected timezone: {detected_tz}")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\nInterrupted by user")
        sys.exit(0)
