# OTA Cross-Device Compatibility Checklist

## Purpose
Prevent OTA regressions caused by updating transmitter and receiver OTA flows independently.

## Contract (must stay compatible)

### Receiver -> Transmitter arm step
- Endpoint: `POST /api/ota_arm`
- Body: none (or `{}` accepted for compatibility)
- Response fields required by receiver:
  - `success` (bool)
  - `session_id` (string)
  - `nonce` (string)
  - `expires_at_ms` (uint32 string/int)
  - `signature` (string)

### Receiver -> Transmitter upload step
- Endpoint: `POST /ota_upload`
- Headers required:
  - `X-OTA-Session`
  - `X-OTA-Nonce`
  - `X-OTA-Expires`
  - `X-OTA-Signature`
  - `Content-Type: application/octet-stream`
  - `Content-Length: <firmware size>`
- Transfer-Encoding chunked: not supported

## Stability rules
1. Any transmitter OTA auth/session change must be validated against receiver proxy upload flow.
2. Any receiver OTA forwarding/write-loop change must be validated against transmitter upload parser.
3. Do not rotate an active unconsumed OTA session on duplicate control-plane arm events.
4. Avoid stack-heavy formatting/logging on HTTP server task paths during OTA.

## Required smoke tests after OTA changes

### Build checks
- Build transmitter target
- Build receiver target

### Runtime checks
1. Arm succeeds: receiver logs `Session armed`.
2. Upload stream starts and progresses without `Failed to forward OTA chunk`.
3. Transmitter accepts auth and receives full body.
4. No `Guru Meditation` / stack canary in `httpd` task.
5. Receiver gets final 200/JSON success from transmitter.

## Debugging minimum logs to collect when OTA fails
- Receiver:
  - arm response code/body
  - early transmitter HTTP rejection body (if present)
  - forwarded byte count at failure
- Transmitter:
  - OTA auth validation result
  - upload rejection code/details
  - panic trace (if any)

## Current known fixes (Mar 20, 2026)
- Transmitter: ignore duplicate control-plane arm when existing session is still active and unconsumed.
- Transmitter: reduced `httpd` stack pressure (heap chunk buffer + larger HTTP task stack + lighter auth-success logging).
- Receiver: robust partial-write forwarding loop with early transmitter error capture.
