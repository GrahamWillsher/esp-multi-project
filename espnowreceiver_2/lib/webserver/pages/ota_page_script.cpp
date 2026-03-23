#include "ota_page_script.h"

String get_ota_page_script() {
    return R"rawliteral(
        function formatEnvName(env) {
            if (!env) return '';
            const spaced = String(env).replace(/[-_]+/g, ' ').trim();
            return spaced.replace(/\b\w/g, c => c.toUpperCase());
        }

        function displayDeviceName(meta) {
            const env = formatEnvName(meta.env || '');
            const type = meta.device || '';
            if (env) return env;
            if (type) return type;
            return 'Unknown Device';
        }

        function parseVersion(v) {
            const m = /^(\d+)\.(\d+)\.(\d+)$/.exec(v || '');
            if (!m) return null;
            return { major: Number(m[1]), minor: Number(m[2]), patch: Number(m[3]) };
        }

        function normalizeDeviceType(value) {
            return String(value || '').trim().toUpperCase();
        }

        function validateUploadSelection(device, metadata) {
            if (!metadata || !metadata.valid) {
                // Legacy images without embedded metadata are allowed for backward compatibility.
                return { ok: true, warning: 'Legacy firmware (no metadata). Compatibility checks are limited.' };
            }

            const expectedType = (device === 'Receiver') ? 'RECEIVER' : 'TRANSMITTER';
            const actualType = normalizeDeviceType(metadata.device);
            if (actualType && actualType !== expectedType) {
                return {
                    ok: false,
                    reason: 'Selected firmware targets ' + actualType + ', but this upload path requires ' + expectedType + '.'
                };
            }

            const fileVer = parseVersion(metadata.version);
            if (!fileVer) {
                return { ok: true };
            }

            // Keep receiver/transmitter protocol majors aligned.
            const peerVersion = (device === 'Receiver') ? transmitterVersionForCompat : receiverVersionForCompat;
            const peerVer = parseVersion(peerVersion);
            if (peerVer && fileVer.major !== peerVer.major) {
                return {
                    ok: false,
                    reason: 'Firmware major ' + fileVer.major + ' is incompatible with peer major ' + peerVer.major + '. Update both devices to same major.'
                };
            }

            return { ok: true };
        }

        let receiverVersionForCompat = null;
        let transmitterVersionForCompat = null;

        function updateCompatibilityStatus() {
            const el = document.getElementById('compatibilityStatus');
            const rx = parseVersion(receiverVersionForCompat);
            const tx = parseVersion(transmitterVersionForCompat);

            if (!rx || !tx) {
                el.style.color = '#FFD700';
                el.innerText = '⚠️ Waiting for version info';
                return;
            }

            if (rx.major === tx.major) {
                el.style.color = '#4CAF50';
                el.innerText = '✅ Compatible';
            } else {
                el.style.color = '#ff6b35';
                el.innerText = '⚠️ Version mismatch';
            }
        }

        // Extract firmware metadata from binary file
        async function extractMetadataFromFile(file) {
            return new Promise((resolve, reject) => {
                const reader = new FileReader();
                reader.onload = function(e) {
                    const arrayBuffer = e.target.result;
                    const bytes = new Uint8Array(arrayBuffer);
                    
                    // Search for magic start marker (0x464D5441 = "FMTA" in little-endian)
                    const MAGIC_START = [0x41, 0x54, 0x4D, 0x46];  // Little-endian
                    const MAGIC_END = [0x46, 0x44, 0x4E, 0x45];    // Little-endian
                    
                    let metadataOffset = -1;
                    for (let i = 0; i < bytes.length - 128; i++) {
                        if (bytes[i] === MAGIC_START[0] && bytes[i+1] === MAGIC_START[1] &&
                            bytes[i+2] === MAGIC_START[2] && bytes[i+3] === MAGIC_START[3]) {
                            metadataOffset = i;
                            break;
                        }
                    }
                    
                    if (metadataOffset === -1) {
                        resolve({ valid: false, reason: 'No metadata found' });
                        return;
                    }
                    
                    // Verify end marker
                    if (bytes[metadataOffset + 124] !== MAGIC_END[0] || 
                        bytes[metadataOffset + 125] !== MAGIC_END[1] ||
                        bytes[metadataOffset + 126] !== MAGIC_END[2] || 
                        bytes[metadataOffset + 127] !== MAGIC_END[3]) {
                        resolve({ valid: false, reason: 'Invalid metadata structure' });
                        return;
                    }
                    
                    // Extract fields
                    const decoder = new TextDecoder('utf-8');
                    const env_name = decoder.decode(bytes.slice(metadataOffset + 4, metadataOffset + 36)).replace(/\0/g, '');
                    const device_type = decoder.decode(bytes.slice(metadataOffset + 36, metadataOffset + 52)).replace(/\0/g, '');
                    const version_major = bytes[metadataOffset + 52];
                    const version_minor = bytes[metadataOffset + 53];
                    const version_patch = bytes[metadataOffset + 54];
                    const build_date = decoder.decode(bytes.slice(metadataOffset + 56, metadataOffset + 104)).replace(/\0/g, '');
                    
                    resolve({
                        valid: true,
                        env: env_name,
                        device: device_type,
                        version: `${version_major}.${version_minor}.${version_patch}`,
                        build_date: build_date
                    });
                };
                reader.onerror = reject;
                reader.readAsArrayBuffer(file);
            });
        }

        async function computeFileSha256(file) {
            const buffer = await file.arrayBuffer();

            // Preferred path: native WebCrypto (fast)
            if (window.crypto && window.crypto.subtle) {
                const digest = await window.crypto.subtle.digest('SHA-256', buffer);
                const bytes = new Uint8Array(digest);
                let hex = '';
                for (let i = 0; i < bytes.length; i++) {
                    hex += bytes[i].toString(16).padStart(2, '0');
                }
                return hex;
            }

            // Fallback path for non-secure HTTP contexts where WebCrypto is unavailable.
            // Pure-JS SHA-256 implementation (single-shot over Uint8Array).
            const bytes = new Uint8Array(buffer);
            const K = [
                0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
                0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
                0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
                0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
                0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
                0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
                0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
                0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
            ];

            const rotr = (x, n) => (x >>> n) | (x << (32 - n));
            const ch = (x, y, z) => (x & y) ^ (~x & z);
            const maj = (x, y, z) => (x & y) ^ (x & z) ^ (y & z);
            const bsig0 = x => rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
            const bsig1 = x => rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
            const ssig0 = x => rotr(x, 7) ^ rotr(x, 18) ^ (x >>> 3);
            const ssig1 = x => rotr(x, 17) ^ rotr(x, 19) ^ (x >>> 10);

            const bitLenHi = (bytes.length / 0x20000000) >>> 0;
            const bitLenLo = (bytes.length << 3) >>> 0;

            const padLen = ((bytes.length + 9 + 63) & ~63);
            const msg = new Uint8Array(padLen);
            msg.set(bytes);
            msg[bytes.length] = 0x80;
            msg[padLen - 8] = (bitLenHi >>> 24) & 0xff;
            msg[padLen - 7] = (bitLenHi >>> 16) & 0xff;
            msg[padLen - 6] = (bitLenHi >>> 8) & 0xff;
            msg[padLen - 5] = bitLenHi & 0xff;
            msg[padLen - 4] = (bitLenLo >>> 24) & 0xff;
            msg[padLen - 3] = (bitLenLo >>> 16) & 0xff;
            msg[padLen - 2] = (bitLenLo >>> 8) & 0xff;
            msg[padLen - 1] = bitLenLo & 0xff;

            let h0 = 0x6a09e667, h1 = 0xbb67ae85, h2 = 0x3c6ef372, h3 = 0xa54ff53a;
            let h4 = 0x510e527f, h5 = 0x9b05688c, h6 = 0x1f83d9ab, h7 = 0x5be0cd19;
            const w = new Uint32Array(64);

            for (let i = 0; i < msg.length; i += 64) {
                for (let t = 0; t < 16; t++) {
                    const j = i + (t * 4);
                    w[t] = ((msg[j] << 24) | (msg[j + 1] << 16) | (msg[j + 2] << 8) | msg[j + 3]) >>> 0;
                }
                for (let t = 16; t < 64; t++) {
                    w[t] = (ssig1(w[t - 2]) + w[t - 7] + ssig0(w[t - 15]) + w[t - 16]) >>> 0;
                }

                let a = h0, b = h1, c = h2, d = h3, e = h4, f = h5, g = h6, h = h7;
                for (let t = 0; t < 64; t++) {
                    const t1 = (h + bsig1(e) + ch(e, f, g) + K[t] + w[t]) >>> 0;
                    const t2 = (bsig0(a) + maj(a, b, c)) >>> 0;
                    h = g; g = f; f = e; e = (d + t1) >>> 0;
                    d = c; c = b; b = a; a = (t1 + t2) >>> 0;
                }

                h0 = (h0 + a) >>> 0; h1 = (h1 + b) >>> 0; h2 = (h2 + c) >>> 0; h3 = (h3 + d) >>> 0;
                h4 = (h4 + e) >>> 0; h5 = (h5 + f) >>> 0; h6 = (h6 + g) >>> 0; h7 = (h7 + h) >>> 0;
            }

            const toHex8 = n => n.toString(16).padStart(8, '0');
            let hex = '';
            hex += toHex8(h0) + toHex8(h1) + toHex8(h2) + toHex8(h3) +
                   toHex8(h4) + toHex8(h5) + toHex8(h6) + toHex8(h7);
            return hex;
        }

        function startCommitVerification(statusDiv, uploadBtn, expectedTxnId) {
            let attempts = 0;
            const maxAttempts = 90; // ~3 minutes at 2s interval
            const pollMs = 2000;
            let graceAttempts = 0;        // count of successful (status.success===true) responses
            const txnMismatchGrace = 5;   // tolerate txn=0 for first N successful replies (stale state drain)
            let lastKnownCommitState = '';
            let lastKnownCommitDetail = '';
            let everReachable = false;

            const stateLabels = {
                'boot_pending_validation': 'Boot guard running health checks...',
                'prepared_waiting_reboot': 'Firmware staged — waiting for reboot...',
                'prepare_upload':          'Receiving firmware image...',
                'prepare_writing':         'Writing firmware to flash...',
                'idle':                    'Transmitter online, awaiting OTA state...',
                'unknown':                 'Awaiting state from transmitter...'
            };

            const verificationInterval = setInterval(function() {
                attempts++;

                fetch('/api/transmitter_ota_status')
                    .then(resp => resp.json())
                    .then(status => {
                        if (!status.success) {
                            return;
                        }

                        everReachable = true;
                        graceAttempts++;

                        const currentTxn = Number(status.ota_txn_id || 0);
                        const commitState = status.commit_state || 'unknown';
                        const commitDetail = status.commit_detail || '';
                        lastKnownCommitState = commitState;
                        lastKnownCommitDetail = commitDetail;

                        // After the grace period: if txn is 0 and state is idle/unknown, the
                        // transmitter came back on the old firmware (auto-rollback with no explicit state).
                        if (expectedTxnId && graceAttempts > txnMismatchGrace) {
                            if (currentTxn === 0 && (commitState === 'idle' || commitState === 'unknown')) {
                                clearInterval(verificationInterval);
                                statusDiv.innerHTML = '❌ OTA rollback: Transmitter reverted to previous firmware (OTA transaction not found after reboot)';
                                uploadBtn.disabled = false;
                                uploadBtn.style.cursor = 'pointer';
                                uploadBtn.style.backgroundColor = '#ff6b35';
                                uploadBtn.innerText = 'Retry Upload';
                                return;
                            }
                        }

                        // Within grace window: skip stale/mismatched txn responses.
                        if (expectedTxnId && currentTxn && currentTxn !== expectedTxnId) {
                            return;
                        }

                        if (status.boot_guard_passed || commitState === 'committed_validated') {
                            clearInterval(verificationInterval);
                            const detailSuffix = commitDetail ? ('<br><span style="color:#888;font-size:12px;">' + commitDetail + '</span>') : '';
                            statusDiv.innerHTML = '✅ OTA committed successfully. Transmitter validated new firmware.' + detailSuffix;
                            uploadBtn.disabled = false;
                            uploadBtn.style.cursor = 'pointer';
                            uploadBtn.style.backgroundColor = '#4CAF50';
                            uploadBtn.innerText = 'Upload Complete';
                            return;
                        }

                        // Explicit rollback states + boot guard error all terminate monitoring.
                        if (commitState.indexOf('rollback') >= 0 || commitState === 'boot_guard_error') {
                            clearInterval(verificationInterval);
                            const isBootErr = (commitState === 'boot_guard_error');
                            const reason = status.rollback_reason || commitDetail
                                || (isBootErr ? 'Boot guard health check failed' : 'Unknown rollback reason');
                            statusDiv.innerHTML = '❌ OTA ' + (isBootErr ? 'boot guard error' : 'rollback detected') + ': ' + reason;
                            uploadBtn.disabled = false;
                            uploadBtn.style.cursor = 'pointer';
                            uploadBtn.style.backgroundColor = '#ff6b35';
                            uploadBtn.innerText = 'Retry Upload';
                            return;
                        }

                        const label = stateLabels[commitState] || ('State: ' + commitState);
                        statusDiv.innerHTML = '⏳ Waiting for transmitter validation...<br>'
                            + '<span style="color:#888;font-size:12px;">' + label
                            + (commitDetail ? (' — ' + commitDetail) : '') + '</span>';
                    })
                    .catch(() => {
                        // Expected during the reboot window — transmitter will be briefly offline.
                        if (attempts <= 15) {
                            statusDiv.innerHTML = '⏳ Waiting for transmitter to come back online...'
                                + '<br><span style="color:#888;font-size:12px;">(' + (attempts * 2) + 's elapsed)</span>';
                        }
                    });

                if (attempts >= maxAttempts) {
                    clearInterval(verificationInterval);
                    const totalSecs = maxAttempts * pollMs / 1000;
                    let timeoutMsg;
                    if (!everReachable) {
                        timeoutMsg = '❌ Timeout: Transmitter did not come back after reboot ('
                            + totalSecs + 's). Check device power and network connectivity.';
                    } else if (lastKnownCommitState === 'boot_pending_validation') {
                        timeoutMsg = '❌ Timeout: Transmitter boot guard has not resolved after ' + totalSecs + 's.'
                            + ' Device may be in a boot loop.'
                            + (lastKnownCommitDetail ? ' (' + lastKnownCommitDetail + ')' : '');
                    } else if (lastKnownCommitState) {
                        timeoutMsg = '❌ Timeout: Transmitter stuck in state \'' + lastKnownCommitState + '\' after ' + totalSecs + 's.'
                            + (lastKnownCommitDetail ? ' (' + lastKnownCommitDetail + ')' : '');
                    } else {
                        timeoutMsg = '❌ Timeout waiting for transmitter post-reboot validation (' + totalSecs + 's)';
                    }
                    statusDiv.innerHTML = timeoutMsg;
                    uploadBtn.disabled = false;
                    uploadBtn.style.cursor = 'pointer';
                    uploadBtn.style.backgroundColor = '#ff6b35';
                    uploadBtn.innerText = 'Retry Upload';
                }
            }, pollMs);
        }
        
        function setupDeviceUpload(device) {
            const fileInput = document.getElementById('firmwareFile' + device);
            const uploadBtn = document.getElementById('uploadBtn' + device);
            const statusDiv = document.getElementById('status' + device);
            const progressBarDiv = document.getElementById('progressBar' + device);
            const progressFill = document.getElementById('progressFill' + device);
            const progressBarText = document.getElementById('progressText' + device);
            
            let selectedFile = null;
            let selectedMetadata = null;
            
            // Handle file selection
            fileInput.addEventListener('change', async function(e) {
                if (e.target.files.length > 0) {
                    selectedFile = e.target.files[0];
                    const sizeMB = (selectedFile.size / 1024 / 1024).toFixed(2);
                    
                    // Extract metadata from file
                    try {
                        const metadata = await extractMetadataFromFile(selectedFile);
                        selectedMetadata = metadata;
                        if (metadata.valid) {
                            statusDiv.innerHTML = '📄 ' + selectedFile.name + ' (' + sizeMB + ' MB) → ' +
                                                  '<span style="color: #4CAF50;">● v' + 
                                                  metadata.version + '</span> ' +
                                                  '<span style="color: #888; font-size: 12px;">(Built: ' + 
                                                  metadata.build_date + ')</span>';

                            const preflight = validateUploadSelection(device, metadata);
                            if (!preflight.ok) {
                                statusDiv.innerHTML += '<br><span style="color:#ff6b35;">❌ ' + preflight.reason + '</span>';
                                uploadBtn.disabled = true;
                                uploadBtn.style.backgroundColor = '#666';
                                uploadBtn.innerText = 'Incompatible Firmware';
                                return;
                            }

                            if (preflight.warning) {
                                statusDiv.innerHTML += '<br><span style="color:#FFD700;">⚠️ ' + preflight.warning + '</span>';
                            }
                        } else {
                            statusDiv.innerHTML = '📄 ' + selectedFile.name + ' (' + sizeMB + ' MB) → ' +
                                                  '<span style="color: #FFD700;">* No metadata (legacy firmware)</span>';
                        }
                    } catch (err) {
                        selectedMetadata = null;
                        statusDiv.innerHTML = '📄 ' + selectedFile.name + ' (' + sizeMB + ' MB)';
                    }
                    
                    uploadBtn.disabled = false;
                    uploadBtn.style.backgroundColor = '#ff6b35';
                    uploadBtn.innerText = 'Upload Firmware';
                } else {
                    selectedFile = null;
                    statusDiv.innerHTML = '📁 Select firmware file (.bin)';
                    uploadBtn.disabled = false;
                    uploadBtn.style.backgroundColor = '#666';
                    uploadBtn.innerText = 'Select File First';
                }
            });
            
            // Handle upload button
            uploadBtn.addEventListener('click', async function() {
                if (!selectedFile) {
                    fileInput.click();
                    return;
                }

                const preflight = validateUploadSelection(device, selectedMetadata);
                if (!preflight.ok) {
                    statusDiv.innerHTML = '❌ ' + preflight.reason;
                    uploadBtn.disabled = false;
                    uploadBtn.style.backgroundColor = '#ff6b35';
                    uploadBtn.innerText = 'Retry Upload';
                    return;
                }

                let imageSha256 = '';
                try {
                    statusDiv.innerHTML = '🔐 Computing firmware hash...';
                    imageSha256 = await computeFileSha256(selectedFile);
                } catch (err) {
                    statusDiv.innerHTML = '❌ Unable to compute firmware hash: ' + err.message;
                    uploadBtn.disabled = false;
                    uploadBtn.style.backgroundColor = '#ff6b35';
                    uploadBtn.innerText = 'Retry Upload';
                    return;
                }
                
                console.log('Starting ' + device + ' OTA upload...');
                uploadBtn.style.display = 'none';
                progressBarDiv.style.display = 'block';
                progressFill.style.width = '0%';
                progressBarText.innerText = '0%';
                
                const xhr = new XMLHttpRequest();
                
                // Upload progress
                xhr.upload.addEventListener('progress', function(e) {
                    if (e.lengthComputable) {
                        const percent = Math.round((e.loaded / e.total) * 100);
                        progressFill.style.width = percent + '%';
                        progressBarText.innerText = percent + '%';
                    }
                });
                
                // Upload complete
                xhr.addEventListener('load', function() {
                    if (xhr.status === 200) {
                        try {
                            const response = JSON.parse(xhr.responseText);
                            if (response.success) {
                                if (device === 'Receiver') {
                                    statusDiv.innerHTML = '✅ Firmware uploaded! Device is rebooting...<br><br>Please wait 30 seconds and reload the page.';
                                    progressBarDiv.style.display = 'none';
                                    uploadBtn.style.display = '';
                                    uploadBtn.innerText = 'Upload complete\nRebooting...';
                                    uploadBtn.style.backgroundColor = '#4CAF50';
                                    // Don't redirect - receiver is rebooting
                                } else {
                                    statusDiv.innerHTML = '✅ Firmware uploaded to transmitter.<br><br>Verifying OTA readiness...';
                                    progressBarDiv.style.display = 'none';
                                    uploadBtn.style.display = '';
                                    uploadBtn.innerText = 'Upload complete\nVerifying OTA...';
                                    uploadBtn.style.backgroundColor = '#4CAF50';

                                    let statusPollAttempts = 0;
                                    const maxStatusPollAttempts = 40; // 40s

                                    const statusPollInterval = setInterval(function() {
                                        statusPollAttempts++;

                                        fetch('/api/transmitter_ota_status')
                                            .then(resp => resp.json())
                                            .then(status => {
                                                if (status.success && status.ready_for_reboot) {
                                                    clearInterval(statusPollInterval);
                                                    const expectedTxnId = Number(status.ota_txn_id || 0);

                                                    TransmitterReboot.run({
                                                        countdownSeconds: TransmitterReboot.COUNTDOWN_SECONDS,
                                                        updateCountdown: (seconds) => {
                                                            statusDiv.innerHTML = '✅ OTA verified on transmitter.';
                                                            uploadBtn.disabled = true;
                                                            uploadBtn.style.cursor = 'not-allowed';
                                                            uploadBtn.style.backgroundColor = '#ff9800';
                                                            uploadBtn.innerText = 'Reboot in ' + seconds + 's...';
                                                        },
                                                        onCommandStart: () => {
                                                            statusDiv.innerHTML = '✅ Sending reboot command to transmitter...';
                                                            uploadBtn.disabled = true;
                                                            uploadBtn.style.cursor = 'not-allowed';
                                                            uploadBtn.style.backgroundColor = '#ff9800';
                                                            uploadBtn.innerText = 'Sending reboot command...';
                                                        },
                                                        onSuccess: () => {
                                                            statusDiv.innerHTML = '✅ Waiting for transmitter to come back and validate...';
                                                            uploadBtn.disabled = true;
                                                            uploadBtn.style.cursor = 'not-allowed';
                                                            uploadBtn.style.backgroundColor = '#28a745';
                                                            uploadBtn.innerText = '✓ Reboot command sent';
                                                            startCommitVerification(statusDiv, uploadBtn, expectedTxnId);
                                                        },
                                                        onFailure: (message) => {
                                                            statusDiv.innerHTML = '❌ OTA uploaded, but reboot command failed: ' + (message || 'Unknown error');
                                                            uploadBtn.disabled = false;
                                                            uploadBtn.style.cursor = 'pointer';
                                                            uploadBtn.style.backgroundColor = '#ff6b35';
                                                            uploadBtn.innerText = 'Retry Upload';
                                                        },
                                                        onError: (err) => {
                                                            statusDiv.innerHTML = '❌ OTA uploaded, but reboot request error: ' + err.message;
                                                            uploadBtn.disabled = false;
                                                            uploadBtn.style.cursor = 'pointer';
                                                            uploadBtn.style.backgroundColor = '#ff6b35';
                                                            uploadBtn.innerText = 'Retry Upload';
                                                        }
                                                    });
                                                } else if (status.success && status.in_progress) {
                                                    statusDiv.innerHTML = '✅ Firmware uploaded to transmitter.<br><br>Applying OTA... please wait';
                                                } else if (status.success && !status.in_progress && !status.ready_for_reboot && status.last_success === false) {
                                                    clearInterval(statusPollInterval);
                                                    statusDiv.innerHTML = '❌ Transmitter OTA failed: ' + (status.last_error || 'Unknown error');
                                                    uploadBtn.disabled = false;
                                                    uploadBtn.style.backgroundColor = '#ff6b35';
                                                    uploadBtn.innerText = 'Retry Upload';
                                                } else if (!status.success) {
                                                    clearInterval(statusPollInterval);
                                                    statusDiv.innerHTML = '❌ Unable to verify transmitter OTA: ' + (status.message || status.detail || 'Unknown status error');
                                                    uploadBtn.disabled = false;
                                                    uploadBtn.style.backgroundColor = '#ff6b35';
                                                    uploadBtn.innerText = 'Retry Upload';
                                                }
                                            })
                                            .catch(() => {
                                                // Keep polling until timeout
                                            });

                                        if (statusPollAttempts >= maxStatusPollAttempts) {
                                            clearInterval(statusPollInterval);
                                            statusDiv.innerHTML = '❌ Timed out waiting for transmitter OTA readiness';
                                            uploadBtn.disabled = false;
                                            uploadBtn.style.backgroundColor = '#ff6b35';
                                            uploadBtn.innerText = 'Retry Upload';
                                        }
                                    }, 1000);
                                }
                            } else {
                                statusDiv.innerHTML = '❌ Failed: ' + response.message;
                                progressBarDiv.style.display = 'none';
                                uploadBtn.style.display = '';
                                uploadBtn.disabled = false;
                                uploadBtn.style.backgroundColor = '#ff6b35';
                                uploadBtn.innerText = 'Retry Upload';
                            }
                        } catch (e) {
                            statusDiv.innerHTML = '❌ Error parsing response';
                            progressBarDiv.style.display = 'none';
                            uploadBtn.style.display = '';
                            uploadBtn.disabled = false;
                            uploadBtn.style.backgroundColor = '#ff6b35';
                            uploadBtn.innerText = 'Retry Upload';
                        }
                    } else {
                        statusDiv.innerHTML = '❌ Upload failed: HTTP ' + xhr.status;
                        progressBarDiv.style.display = 'none';
                        uploadBtn.style.display = '';
                        uploadBtn.disabled = false;
                        uploadBtn.style.backgroundColor = '#ff6b35';
                        uploadBtn.innerText = 'Retry Upload';
                    }
                });
                
                // Upload error
                xhr.addEventListener('error', function() {
                    statusDiv.innerHTML = '❌ Network error during upload';
                    progressBarDiv.style.display = 'none';
                    uploadBtn.style.display = '';
                    uploadBtn.disabled = false;
                    uploadBtn.style.backgroundColor = '#ff6b35';
                    uploadBtn.innerText = 'Retry Upload';
                });

                // Choose endpoint based on device
                const endpoint = (device === 'Receiver') ? '/api/ota_upload_receiver' : '/api/ota_upload';
                xhr.open('POST', endpoint);
                // Both receiver self-OTA and transmitter forwarding now use raw binary uploads.
                xhr.setRequestHeader('Content-Type', 'application/octet-stream');
                xhr.setRequestHeader('X-OTA-Image-SHA256', imageSha256);
                xhr.send(selectedFile);
            });
        }
        
        window.onload = function() {
            console.log('OTA page loaded');

            function fallbackReceiverFromVersionApi() {
                return fetch('/api/version')
                    .then(response => response.json())
                    .then(data => {
                        const versionEl = document.getElementById('receiverVersion');
                        const buildEl = document.getElementById('receiverBuild');
                        const v = data.version || 'Unknown';
                        const d = data.build_date || 'Build info unavailable';
                        versionEl.innerHTML = 'Receiver v' + v + ' <span style="color: #FFD700;">*</span>';
                        buildEl.innerText = d;
                        receiverVersionForCompat = v;
                        updateCompatibilityStatus();
                    });
            }

            function fallbackTransmitterFromVersionApi() {
                return fetch('/api/version')
                    .then(response => response.json())
                    .then(data => {
                        const versionEl = document.getElementById('transmitterVersion');
                        const buildEl = document.getElementById('transmitterBuild');
                        const v = data.transmitter_version || 'Unknown';
                        const d = data.transmitter_build_date || 'Build info unavailable';
                        versionEl.innerHTML = 'Transmitter v' + v + ' <span style="color: #FFD700;">*</span>';
                        buildEl.innerText = d;
                        transmitterVersionForCompat = (v && v !== 'Unknown') ? v : null;
                        updateCompatibilityStatus();
                    });
            }
            
            // Load current receiver firmware info
            fetch('/api/firmware_info')
                .then(response => response.json())
                .then(data => {
                    const versionEl = document.getElementById('receiverVersion');
                    const buildEl = document.getElementById('receiverBuild');
                    
                    if (data.valid) {
                        const name = displayDeviceName(data);
                        versionEl.innerHTML = name + ' v' + data.version + ' <span style="color: #4CAF50;">●</span>';
                        buildEl.innerText = data.build_date;
                        receiverVersionForCompat = data.version;
                    } else {
                        versionEl.innerHTML = 'Metadata unavailable <span style="color: #FFD700;">*</span>';
                        buildEl.innerText = data.message || 'No embedded metadata';
                        receiverVersionForCompat = null;
                    }
                    updateCompatibilityStatus();
                })
                .catch(err => {
                    console.error('Failed to fetch firmware info:', err);
                    fallbackReceiverFromVersionApi().catch(() => {
                        document.getElementById('receiverVersion').innerText = 'Unavailable';
                        document.getElementById('receiverBuild').innerText = 'Failed to load';
                        receiverVersionForCompat = null;
                        updateCompatibilityStatus();
                    });
                });
            
            // Setup upload handlers for both devices
            setupDeviceUpload('Receiver');
            setupDeviceUpload('Transmitter');
            
            // Fetch transmitter metadata from ESP-NOW data (simpler approach)
            function fetchTransmitterMetadata() {
                fetch('/api/transmitter_metadata')
                    .then(response => response.json())
                    .then(data => {
                        const versionEl = document.getElementById('transmitterVersion');
                        const buildEl = document.getElementById('transmitterBuild');
                        
                        if (data.status === 'received' && data.version) {
                            // Metadata received from transmitter
                            let indicator;
                            if (data.valid) {
                                // Green circle: Valid metadata from .rodata
                                indicator = '<span style="color: #4CAF50;">●</span>';
                            } else {
                                // Yellow star: Fallback to build flags (metadata not valid)
                                indicator = '<span style="color: #FFD700;">*</span>';
                            }
                            const name = displayDeviceName(data);
                            versionEl.innerHTML = name + ' v' + data.version + ' ' + indicator;
                            const buildDate = data.build_date || data.buildDate || data.build || '';
                            if (buildDate) {
                                buildEl.innerText = buildDate;
                            } else {
                                fallbackTransmitterFromVersionApi().catch(() => {
                                    buildEl.innerText = 'Build info unavailable';
                                });
                            }
                            transmitterVersionForCompat = data.version;
                        } else if (data.status === 'received' && (!data.version || !data.build_date)) {
                            fallbackTransmitterFromVersionApi().catch(() => {
                                versionEl.innerHTML = 'Transmitter metadata incomplete <span style="color: #FFD700;">*</span>';
                                buildEl.innerText = 'Build info unavailable';
                                transmitterVersionForCompat = null;
                                updateCompatibilityStatus();
                            });
                        } else {
                            fallbackTransmitterFromVersionApi().catch(() => {
                                // Gray circle: Waiting for metadata from transmitter
                                versionEl.innerHTML = 'Waiting... <span style="color: #888;">○</span>';
                                buildEl.innerText = 'No metadata received yet';
                                transmitterVersionForCompat = null;
                                updateCompatibilityStatus();
                            });
                            return;
                        }
                        updateCompatibilityStatus();
                    })
                    .catch(err => {
                        console.error('Failed to fetch transmitter metadata:', err);
                        fallbackTransmitterFromVersionApi().catch(() => {
                            document.getElementById('transmitterVersion').innerHTML = 'Unavailable';
                            document.getElementById('transmitterBuild').innerText = 'Failed to load';
                            transmitterVersionForCompat = null;
                            updateCompatibilityStatus();
                        });
                    });
            }
            
            // Initial fetch
            fetchTransmitterMetadata();
            
            // Periodically refresh transmitter metadata
            setInterval(fetchTransmitterMetadata, 5000);
        };
    )rawliteral";
}