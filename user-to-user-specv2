## 1. Terminology & Constants

| Name                 | Meaning                                                                 |
|----------------------|-------------------------------------------------------------------------|
| `neighTTL`           | Per‑node “local” flood TTL (in hops), recomputed adaptively.            |
| `CACHE_MAX_MSGS`     | Max buffered offline msgs per user (10).                                |
| `CACHE_MAX_AGE`      | Max age of buffered msg before eviction (24 h = 86400 s).               |
| `CLEANUP_INTERVAL`   | Interval for cache‑cleanup task (1 h = 3600 s).                         |
| `T_TTL_UPDATE`       | Interval to recompute `neighTTL` (5 min = 300 s).                       |
| `NODE_ADVERT_INT`    | `NODE_ADVERT` flood interval (15 min = 900 s).                          |
| `USER_SUMMARY_INT`   | `USER_SUMMARY` flood interval (5 min = 300 s).                          |
| `SYNC_PEERS`         | Number of neighbors polled on join (3).                                 |
| `SYNC_CHUNK_SIZE`    | Max bytes per `STATE_SYNC_CHUNK` (≤ 200 B).                             |
| `DEFAULT_MAX_HOPS`   | Absolute max flood TTL (e.g. 12).                                       |
| `BROADCAST_ADDR`     | `0xFFFFFFFF` — “all‑nodes” address.                                     |

---

## 2. Packet Catalogue

| ID     | Name                   | Purpose & TTL                                                                      | Payload (after BaseHeader)                                                                                                                                                      |
|--------|------------------------|------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `0x01` | `PKT_RREQ`             | AODV route‑request<br/>TTL = expanding‑ring → diameter                              | `originNodeID (4 B)`<br/>`RREQDestNodeID (4 B)`                                                                                                                                  |
| `0x02` | `PKT_RREP`             | AODV route‑reply<br/>TTL = route hops back to origin                               | `originNodeID (4 B)`<br/>`RREPDestNodeID (4 B)`<br/>`lifetime (2 B)`<br/>`numHops (1 B)`                                                                                         |
| `0x03` | `PKT_RERR`             | AODV route‑error<br/>TTL = expanding‑ring                                           | `reporterNodeID (4 B)`<br/>`brokenNodeID (4 B)`<br/>`originalDestNodeID (4 B)`<br/>`originalPacketID (4 B)`<br/>`senderNodeID (4 B)`                                           |
| `0x04` | `PKT_DATA`             | Unicast chat/data<br/>TTL = route hops                                              | `finalDestID (4 B)` + `[payload …]`                                                                                                                                              |
| `0x05` | `PKT_BROADCAST`        | Application broadcast (rare)<br/>TTL = `neighTTL`                                  | `[appData …]`                                                                                                                                                                    |
| `0x06` | `PKT_BROADCAST_INFO`   | Flood connected‑userIDs (Bluetooth)<br/>TTL = `neighTTL`                           | `hdr: originNodeID (4 B)`<br/>`list: count (1 B) + [userID (4 B)]×count`                                                                                                         |
| `0x07` | `PKT_ACK`              | Implicit ACK support (unused)                                                      | `originalPacketID (4 B)`                                                                                                                                                         |
| `0x08` | `PKT_MOVE_UPDATE`      | User handoff via BLE<br/>TTL = `neighTTL`                                          | `userID (4 B)`<br/>`oldNodeID (4 B)`<br/>`newNodeID (4 B)`                                                                                                                        |
| `0x09` | `PKT_SYNC_QUERY`       | Bootstrap: ask 1‑hop neighbors<br/>TTL = 1                                         | *(empty)*                                                                                                                                                                        |
| `0x0A` | `PKT_SYNC_INFO`        | Reply to `SYNC_QUERY`<br/>TTL = 1                                                  | `nodeListVer (4 B)`<br/>`userListVer (4 B)`<br/>`lastHeardTs (4 B)`                                                                                                              |
| `0x0B` | `PKT_STATE_SYNC_REQ`   | Pull full state chunks<br/>TTL = 1                                                 | `chunkType (1 B) 0=node,1=user`<br/>`chunkID (1 B)`                                                                                                                               |
| `0x0C` | `PKT_STATE_SYNC_CHUNK` | Chunked state reply<br/>TTL = 1                                                    | `chunkType (1 B)`<br/>`chunkID (1 B)`<br/>`count (1 B)` + `entries…`                                                                                                             |
| `0x0D` | `PKT_LOC_REQ`          | Find user’s nodeID on‑demand<br/>TTL = diameter                                    | `reqID (1 B)`<br/>`userID (4 B)`                                                                                                                                                  |
| `0x0E` | `PKT_LOC_REP`          | Answer to `LOC_REQ` via reverse path<br/>TTL = route hops                          | `reqID (1 B)`<br/>`nodeID (4 B)`<br/>`seq (1 B)`                                                                                                                                   |
|`0x0F` | `PKT_SEND_USER_MESSAGRE` | Send user message (chat)<br/>TTL = route hops                                      | `userID (4 B)`<br/>`msgID (4 B)`<br/>`msgSize (2 B)`<br/>`msgData (msgSize B)`                                                                                                     |

---

## 3. Timers & Bounds

| Symbol               | Typical Value          | Notes                                                                                  |
|----------------------|------------------------|----------------------------------------------------------------------------------------|
| `T_TTL_UPDATE`       | 300 s (5 min)          | Recompute `neighTTL` from recent HELLO_ACK hopCounts.                                 |
| `NODE_ADVERT_INT`    | 900 s (15 min)         | Flood node presence/version via `PKT_BROADCAST_INFO` without user‑list.               |
| `USER_SUMMARY_INT`   | 300 s (5 min)          | Flood connected‑userIDs via `PKT_BROADCAST_INFO`.                                      |
| `CACHE_CLEANUP`      | 3600 s (1 h)           | Purge offline msgs older than `CACHE_MAX_AGE`.                                        |
| `CACHE_MAX_AGE`      | 86400 s (24 h)         | Drop buffered msgs beyond this age.                                                   |
| `CACHE_MAX_MSGS`     | 10 per user            | Evict oldest if exceeded.                                                             |
| `SYNC_TIMEOUT`       | 10 s                   | Timeout waiting for all `SYNC_INFO` replies.                                          |
| `HELLO_INTERVAL`     | 20 s                   | (Implicit) local‑plane HELLO summaries if still used.                                 |
| `LOC_REQ_TTL`        | = network diameter     | e.g. 25 hops.                                                                          |
| `neighTTL`           | dynamic [2–12]         | Adapt to link quality (90 % hop‑count + 1).                                            |

---

## 4. Feature Hooks & State

### 4.1 Adaptive TTL

- Maintain per‑node window of last _N_ = 20 HELLO_ACK or DIFF_REPLY round‑trip hop counts.  
- Every `T_TTL_UPDATE`, compute `neighTTL = ceil(90th percentile) + 1`, clamp to [2, `DEFAULT_MAX_HOPS`].  
- Use `neighTTL` as `BaseHeader.hopCount` for `PKT_BROADCAST_INFO`, `PKT_MOVE_UPDATE`, `PKT_SYNC_QUERY` floods.

---

### 4.2 Proactive BLE Handoff

- On `BLE_DISCONNECT` from Node A + `CONNECT` to B, handset sends a 3‑field BLE‑handoff containing `{userID, oldNodeID, newNodeID}`.  
- Node A immediately broadcasts `PKT_MOVE_UPDATE` (TTL = `neighTTL`).  
- All nodes seeing it update `_gut[userID] = {nodeID=newNodeID, seq++, lastSeen=now}`.  
- Node B upon receiving that also pulls any cached offline msgs for `userID`.

---

### 4.3 Offline‑Cache Quotas & TTL

- Maintain `_offlineBuffer[userID] = deque<entries>`.  
- On `sendData` with no route: enqueue up to `CACHE_MAX_MSGS`; drop oldest if overflow.  
- Periodic cleanup every `CLEANUP_INTERVAL`: drop entries older than `CACHE_MAX_AGE`.  
- On any route discovery or `MOVE_UPDATE` for that `userID`, drain its queue via normal `PKT_DATA` sends.

---

### 4.4 Multi‑Neighbor State Sync

1. On startup (or after long isolation), broadcast `PKT_SYNC_QUERY` (TTL = 1).  
2. Collect up to `SYNC_PEERS` freshest replies `PKT_SYNC_INFO`.  
3. For each chosen neighbor, unicast `PKT_STATE_SYNC_REQ` for `chunkType=0` (nodes), then `=1` (users), incrementing `chunkID` until `count=0`.  
4. Merge chunks into `_routeTable` / discovered nodes and `_gut`.  
5. When all done (or timeout), switch to normal HELLO/USER_SUMMARY operation.

---

With this in place, your mesh will:

- Automatically size floods to real radio conditions  
- Seamlessly handoff users across nodes with minimal delay  
- Bound offline buffering in time and memory  
- Quickly bootstrap from multiple peers to avoid stale state  
- Support on‑demand user lookups (`LOC_REQ`/`LOC_REP`)  
- Broadcast each node’s connected user list efficiently  
