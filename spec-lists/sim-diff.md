# Differences between the Go simulation and ESP32 firmware
## 1. Packet catalogue & numeric IDs
| Packet                               | Go (sim)                   | C++ (firmware)  | Notes                                                                |
| ------------------------------------ | -------------------------- | --------------- | -------------------------------------------------------------------- |
| **PKT\_GATEWAY**                     | *absent*                   | `0x08`          | Firmware adds a dedicated packet for gateway beacons/announcements.  |
| **PKT\_BROADCAST / BROADCAST\_INFO** | same IDs (`0x05` / `0x06`) | identical       | Semantics differ (see §3).                                           |
| **REQ\_ACK flag**                    | `0x04` defined             | **not** defined | Simulation can request explicit ACKs; firmware can’t parse this bit. |
| **Flags 0×01–0×03**                  | same mnemonic names        | identical       | OK on the wire.                                                      |

## 2. Header layouts (on-air byte order is little-endian in both)

| Header                                        | Size Go                                     | Size C++                 | Fields that differ                                                                                                           |
| --------------------------------------------- | ------------------------------------------- | ------------------------ | ---------------------------------------------------------------------------------------------------------------------------- |
| **BaseHeader**                                | 16 B                                        | 16 B                     | identical.                                                                                                                   |
| **DataHeader / `DATA`**                       | 8 B (`FinalDestID` + **`OriginNodeID`**)    | 4 B (`finalDestID` only) | *OriginNodeID* is silently dropped by the firmware; the extra four bytes the sim transmits will shift every following field. |
| **UserMsgHeader**                             | 16 B (`From, To, ToNode, **OriginNodeID**`) | 12 B (Origin omitted)    | Same mis-alignment problem for USER\_MSG.                                                                                    |
| **InfoHeader vs DiffBroadcastInfoHeader**     | *two* variants: full list + diff            | **only diff** variant    | Firmware expects the compact diff header; the sim still has code to build either style.                                      |
| **ACKHeader, RREQ/RREP/RERR, UREQ/UREP/UERR** | identical payload & packing                 | identical                | wire compatible.                                                                                                             |

## 3. Control-plane semantics

| Topic                                  | Simulation (Go)                                                                                             | Firmware (C++)                                                                           |
| -------------------------------------- | ----------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| **Gateway support**                    | none                                                                                                        | first-class (`I_AM_GATEWAY` flag, PKT\_GATEWAY, MQTT‐uplink integration).                |
| **Neighbour / user-presence flooding** | *Either* full user list (`InfoHeader`) or delta (`DiffBroadcastInfoHeader`). Periodic task every **300 s**. | Delta only, FreeRTOS timer every **60 s** by default.  Includes gateway flag piggy-back. |
| **Hop limits**                         | *Per-query* limits (`RreqHopLimit`, `UreqHopLimit` default 10) + broadcast hard cap **5**                   | Only the broadcast hard cap **5** (RREQ/UREQ unlimited).                                 |
| **RREP/UREP reply policy**             | `ReplyThreshold` (default 2) before answering if you have a poorer route                                    | unconditional reply when a route/user is known.                                          |
| **Route time-outs**                    | TODO in code (not yet implemented)                                                                          | none (also TODO).                                                                        |

## 4. Reliability & ACK handling

| Aspect           | Simulation                                                            | Firmware                                                                                                                |
| ---------------- | --------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| **Explicit ACK** | Optional (`REQ_ACK`). `pendingTxs` map, max 3 re-tries, 3 s time-out. | No `REQ_ACK`, but ACK buffer is built for *implicit* acks of DATA & RREP; expiry **10 min** / clean-up every **1 min**. |
| **Implicit ACK** | Overhearing forward ⇒ remove from `pendingTxs`.                       | Same idea, but uses `ackBuffer`.                                                                                        |
| **RERR trigger** | After last retry; immediately on implicit expiry.                     | Only when ACK buffer entry ages out.                                                                                    |

## 5. Medium access (CSMA / duty-cycle)

| Parameter               | Simulation (`txWorker → BroadcastMessageCSMA`)       | Firmware                                           |
| ----------------------- | ---------------------------------------------------- | -------------------------------------------------- |
| **CCA window / sample** | 5 ms / 100 µs (configurable per node).               | RadioManager hides MAC; no CCA/back-off in router. |
| **Back-off**            | Binary exponential *or* BE scheme; tunable per node. | none at router level.                              |

## 6. Concurrency & system scaffolding

| Layer            | Go simulation                                         | ESP32 firmware                                                            |
| ---------------- | ----------------------------------------------------- | ------------------------------------------------------------------------- |
| **Scheduler**    | goroutines + channels (`txQueue`, `pendingTxChecker`) | FreeRTOS tasks + timers + `xTaskNotify`.                                  |
| **Memory model** | GC’d heap; maps guarded by RW-mutex.                  | Manual `pvPortMalloc`/`vPortFree` for queued payloads; recursive mutexes. |
| **Integration**  | `eventBus` for stats; no external I/O.                | MQTT (remote monitor + uplink), BLE client notifier, GatewayManager.      |

## 7. User-lookup (GUT) & message buffering

| Step                       | Simulation                                | Firmware                                                                                                  |
| -------------------------- | ----------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| **GUT entry**              | `UserEntry {NodeID, Seq, LastSeen}`       | `GutEntry {nodeID, seq, ts}` (same info).                                                                 |
| **Buffered user messages** | `userMessageQueue` keyed by *destUserID*. | `_userMsgBuffer` keyed by *destUserID* *plus* `_userRouteBuffer` keyed by *destNodeID* for gateway cases. |
| **UREQ hop-limit**         | 10                                        | unlimited.                                                                                                |
| **UERR forwarding**        | Has loop-avoidance via seen-set + hop ++  | Similar but contains special logic to drop GUT entry when mismatch.                                       |

## 8. Constants & retries
| Constant                | Go                          | C++                                                   |
| ----------------------- | --------------------------- | ----------------------------------------------------- |
| **MAX\_HOPS broadcast** | 5                           | 5 (match).                                            |
| **Pending-tx retries**  | 3, 3 s back-off             | not retried (single send, wait 10 min expiry).        |
| **Payload truncation**  | Automatic truncate to 255 B | Firmware checks and *drops* oversize before queueing. |



## 9. Things only in one side
Simulation-only

event-driven stats (EventMessageSent, etc.).

Adjustable MAC parameters per node (handy for experiments).

Route reply threshold (suppresses chatter).

Firmware-only

Gateway routing & cloud uplink.

BLE / client-notifier hooks.

Bloom-filter neighbour lists (commented, but data structures present).

Ack-buffer clean-up timer / periodic broadcast timer.

Memory-constrained re-transmit buffering (moves data back if route still missing).

## Impact on interoperability
Wire-level breakage – Mixing the two codebases on the same RF channel will fail for DATA and USER_MSG packets because of the OriginNodeID field size mismatch.

Flag mismatch – If the Go side sets REQ_ACK, the ESP32 will treat the high bit as part of the flags field but never answer with an ACK, so the sim will repeat and eventually raise a spurious RERR.

Gateway traffic – Firmware emits PKT_GATEWAY-related frames that the sim simply drops; the sim cannot originate uplink requests (TO_GATEWAY flag) the firmware expects.

Route establishment asymmetry – Because the firmware replies with RREP as soon as it knows any route, while the sim waits for ReplyThreshold, the two can race and create inconsistent hop counts.

Timeout tuning – The sim assumes sub-second retries; the firmware tolerates losses for minutes. When mixed, the sim will declare routes broken long before the node on real hardware does.

## Recommendations
Unify the wire schema

Add OriginNodeID to DATAHeader and UserMsgHeader on the firmware or remove it from the simulation.

Reserve a new flag bit for REQ_ACK in the firmware (don’t reuse 0×04 if already allocated).

Single source of constants (packet IDs, flag masks, MAX_HOPS) in a shared header or code-generation step.

Align reliability strategy – pick explicit ACK or implicit only, and use comparable retry/timeout values.

Decide on gateway support – either stub it out in firmware when running pure mesh tests, or teach the sim to parse/emit PKT_GATEWAY and TO_/FROM_GATEWAY flags.

Route-reply policy – replicate ReplyThreshold in firmware or disable it in the sim for deterministic behaviour.