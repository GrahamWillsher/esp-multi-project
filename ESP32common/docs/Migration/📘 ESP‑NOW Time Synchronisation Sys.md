📘 ESP‑NOW Time Synchronisation System 

Transmitter → Receiver Clock Update & Periodic Uptime Broadcast 

This document describes a simple, robust design for synchronising the system clock of an ESP‑NOW receiver using a time‑enabled transmitter. After the initial time update, the transmitter periodically sends its system uptime every 5 seconds. 
The receiver displays: 

Transmitter system time 

Transmitter uptime (ticks) 

“Last updated X seconds ago” 

This provides a predictable, industry‑standard handling of non‑real‑time communication. 

 

## 1. Overview of Synchronisation Design 

✔️ Transmitter duties 

Maintain accurate system time (e.g., via NTP). 

Respond to time‑requests from the receiver with a TimeSync packet. 

Every 5 seconds, broadcast a Uptime packet containing: 

uptime ticks 

a sequence number 

a timestamp 

✔️ Receiver duties 

Request the time once at startup. 

Update its system clock when receiving a TimeSync packet. 

When receiving Uptime packets: 

Update uptime on dashboard 

Record the arrival time 

Display “Last updated X seconds ago” based on the last update timestamp. 

Mark time as stale if no updates arrive for too long. 

Summary of System Behavior 

✔️ Receiver requests the time once at startup 

✔️ Transmitter responds with absolute time and uptime 

✔️ Every 5 seconds the transmitter broadcasts uptime packets 

✔️ Receiver displays updated: 

system time 

uptime 

“last updated X seconds ago” 

✔️ Uses sequence numbers to ignore: 

delayed packets 

duplicated packets 

out‑of‑order packets 

✔️ Receiver marks data as stale when no updates arrive 