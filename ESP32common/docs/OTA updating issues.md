I have two ESP32 devices, each capable of OTA updates.
I want to ensure they are never out of sync, especially when underlying shared code changes.
This is a real problem in distributed embedded systems, and there are safe patterns to manage it.

1. Use a Shared Firmware Version Number
2. Make Your Devices REFUSE to run incompatible logic
3. Use a Central OTA Server (not manual OTA)

Instead of clicking “update A, then update B,” use:
✔ A single OTA manifest file
This manifest tells all devices:

current official version
download URL
required minimum version
date/time published

Both devices check the same manifest.
✔ Update order controlled by the server logic
Your OTA server publishes:
version 1.0.3 → all devices must update to 1.0.3
Each device:

checks version
downloads firmware
installs
reboots
reports successful update

Only then does it rejoin the system.
4. Use a Staged Rollout for Safety (Optional but Smart)
This is how pros do it:
Stage 1 → Update Device A only

Run health checks

If success:
Stage 2 → Update Device B

Ensure system returns to normal function
If not, rollback both

This prevents “double failure.”
But YOU do not manually push code.
Your OTA system does (GitHub Actions, CI/CD, OTA backend).

5. For Backward‑Incompatible Changes: Use “Dual‑Protocol Mode”
When your protocol or communication structure changes, do:
Firmware version X
→ supports old protocol only
Firmware version X+1
→ supports both old and new protocols
→ can auto‑detect partner version
→ can switch modes
Firmware version X+2
→ drops the old protocol
→ requires both devices updated