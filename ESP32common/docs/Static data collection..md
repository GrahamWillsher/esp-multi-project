Static data collection.

⭐ The Most Reliable Pattern:
“Full Snapshot + Delta Updates”
This is exactly how professional IoT systems (e.g., Bosch, Sonoff, Philips Hue, Matter devices) do it.
Here’s how it works:

🔵 1. Master fetches a FULL static data snapshot during initialization
When the master boots or pairs with the slave:

Master → sends a “Give me full static data” request
Slave → replies with a complete static dataset (JSON or packed struct)
Master stores it in RAM / NVS
Master displays it immediately

This ensures both devices begin with a known-good baseline.
Think of it like a fresh “state download.”


When a static value changes (e.g., threshold updated, calibration applied)
The master generates a “Change Event” message
The message contains only what changed + new value

in below device 2 is master and device 1 is slave

⭐ 1. Device 2 sends the updated data → Device 1 acknowledges
When Device 2 detects a user change (e.g., threshold, settings, static config), it sends:
UPDATE message:
{
  "version": 12,
  "field": "threshold",
  "value": 123
}

Device 1 processes it and replies:
ACK 12

Where 12 is the version number.
This ensures:

Device 1 definitely received the update
Device 2 knows not to resend
Both devices hold the same version of the static data


⭐ 2. What if ACK is NOT received?
Device 2 MUST retry until acknowledged.
Suggested retry policy:

Retry 3 times quickly (100–200 ms spacing)
If still no ACK: buffer the message and retry after 1 second
After 3–5 failed attempts: request a full resync from Device 1

In my scenario, we need to have subsections for the datatypes e.g. mqtt, battery configuration power settings etc. and then the values that are changed therein, so that we only send data that is relevant, not the whole lot. So if the mqtt server address changes we only send that information on the lines of mqtt configuration/mqtt server, value.