Initial provisioning.
🔵 DEVICE A (Master) — First Boot
On first boot:

No credentials stored
Enter AP mode:
MASTER_SETUP_XXXX
Host a captive portal web page:

Wi‑Fi SSID
Wi‑Fi password


User enters credentials
Save credentials into NVS
Switch to STA mode
Connect to the real Wi‑Fi network

After joining Wi‑Fi:

Start HTTP server for OTA
Start ESP‑NOW for provisioning Device B
Start version control logic

This is the master’s only time in AP mode.

🟢 DEVICE B (Node) — First Boot
Device B contains a tiny provisioning stub:

Boots into ESP‑NOW broadcast mode
Sends:
{ status: "UNPROVISIONED", mac: <mac>, fw_version: 0 }


Master receives this and responds with:

real Wi‑Fi SSID
password
OTA URL for the main firmware


Device B joins the real Wi‑Fi
Device B immediately performs OTA → reboots into full firmware
Device B reports “ready + version”
Master confirms firmware sync
System starts normal operation

No user interaction required.

🔸 MASTER REQUIREMENTS
✔ 1. AP mode + captive portal (first boot only)
Collect router SSID + password.
✔ 2. NVS storage
Store:

router credentials
device pairing keys
firmware version
OTA manifest URL

✔ 3. STA mode connection to real Wi‑Fi
After setup, remain in STA permanently.
✔ 4. Provisioning server
Handles requests from unprovisioned Device B.
✔ 5. ESP‑NOW listener
Receives “I’m unprovisioned” signals from B.
✔ 6. Credential broadcaster
Sends:

router SSID
router password
target OTA URL
encryption key(s)

✔ 7. OTA hosting
Must serve:

firmware_B.bin
firmware_A.bin (if A also supports OTA)
manifest.json

✔ 8. Version synchronisation manager
Ensures:

Device A = version X
Device B = version X
No communication unless both match
No partial system states

✔ 9. OTA orchestrator
Sequence:

Update B
Wait for reboot + version report
Update A
Verify both match