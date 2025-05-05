# Mesh-to-Internet Messaging MVP – Full Technical Specification  
*(LoRa + AODV mesh, Oracle-hosted backend, Firestore storage, ESP32 gateways, zero-cost/cloud-free tier)*

---

## 0 Primary design goals

| Goal                         | Rationale                                                                 |
|------------------------------|---------------------------------------------------------------------------|
| Runs free-tier only          | Oracle “Always-Free” VM for the API; Firestore Spark plan for data.       |
| Tiny footprint on gateways   | ESP32 dual-core is shared with the mesh stack; comms must be sleep-friendly. |
| No paid SMS / phone verification | Users self-declare the last 9 digits of a phone number as userID.       |
| Disaster-recovery tolerant   | Works with intermittent power, bandwidth, and Internet.                   |
| Future-proof for E2E crypto  | Header room and database fields reserved.                                 |

---
## 0.1 Component Overview
```mermaid
 ┌─────────────┐                       ┌────────────────────────┐
 │  Android /  │  HTTPS (JSON)         │  Go API Service        │
 │  Web Client │◀─────────────────────▶│  (Oracle Free VM)      │
 └─────────────┘                       │  - net/http + chi      │
                                       │  - Firestore client    │
            AODV + LoRa                │  - JWT verify (adm‑go) │
 ┌─────────────┐                       │  - SQLite WAL Buffer   │
 │  Gateway(s) │────HTTP POST /sync────┤                       │
 │  ESP32       │                      └──────────┬────────────┘
 └────┬────────┘                                  │
      ▼                                           │
 ┌─────────────┐  LoRa mesh packets               ▼
 │   Nodes     │                           Cloud Firestore
 └─────────────┘
```
*All persistent data lives in Firestore; the Go service only needs disk for a write‑ahead‑log (WAL) that shields you from transient outages.*
*JWT verification done through Firebase Admin SDK --> dropped from API due to overhead to verify each user and gateway during testing.*

## 1 System components

| Tag    | Component                    | Technology                                   | Free-tier fit                         |
|--------|------------------------------|----------------------------------------------|---------------------------------------|
| APP    | Android (and optional Web) client | Kotlin / Flutter; Firebase Auth Anonymous     | Unlimited sign-ins                    |
| NODE   | LoRa mesh node               | existing C / Arduino code                    | —                                     |
| GW     | Gateway firmware             | ESP32 + ArduinoWiFi + mbedTLS                | fits 4 MB flash                       |
| API    | Backend REST service         | GO             | 1 vCPU, 1 GB Oracle                   |
| DB     | Database                     | Cloud Firestore (Spark)                      | 50 k reads / 20 k writes per day      |
| QUEUE  | Memory buffer in API         | ________                  | absorbs outages                       |

---

## 2 Identity & minimal authentication

### 2.1 User identity

```text
userID  = <last 9 digits>     # e.g. 771234567
profile = { userID, phoneDigits, displayName?, anonUid, publicKey? }
```
*phoneDigits* is stored only so other users can search; no verification is attempted.

## 2.2 Auth flow (free)

1. **APP** calls `FirebaseAuth.signInAnonymously()` → receives ID token (JWT).  
2. **GW**: at first boot an admin script mints one custom token and flashes it; gateway refreshes as required.  
3. **API** verifies every incoming request with Firebase Admin SDK (no cost).  
4. Firestore security rules allow R/W only if `request.auth.token.uid` matches the supplied `anonUid`.  

## 3 Data model (Firestore)

```csharp
users/{userID}
  anonUid            string
  phoneDigits        string
  displayName        string
  publicKey          bytes   # reserved
  createdAt          timestamp

gateways/{gwId}
  anonUid            string
  hw,fw              string
  lastPingAt         timestamp
  lastIp             string

presences/{userID}
  gwId               string
  lastSeenAt         timestamp   # updated every GW sync

messages/{msgId}
  srcID              string
  dstID              string
  body               bytes       # plaintext MVP
  tsCreated          timestamp
  deliveredVia       string      # gwId or "direct"
  tsDelivered        timestamp   # set by GW / APP ACK
  tsRead             timestamp   # set by destination APP
```
*Enable TTL on messages (e.g. 30 days) so old docs auto-purge.*
## 4 Mesh packet format

| Byte(s) | Field       | Notes                                          |
|---------|-------------|------------------------------------------------|
| 0       | `VER TYPE`  |                                                |
| 1–2     | `SEQ`       | incrementing per `srcID`                       |
| 3–6     | `SRC`       | 32-bit userID (28 bits fit) or nodeID          |
| 7–10    | `DST`       | 32-bit                                         |
| 11      | `FRAG`      | high-bit LAST, low-7 index                     |
| 12…n    | `PAYLOAD`   |                                                |

*Max LoRa payload at SF 12 is 51 B → fragment as needed.*

## 5 Gateway ⇄ API protocol  
*(energy-friendly, HTTP/1.1 only)*

### 5.1 Single “sync” endpoint

```bash
POST /v1/sync
Headers:  Authorization: Bearer <ID token>
Body (CBOR or JSON, ≤ 4 KB):
{
  "gwId"  : "GW_A1B2",
  "since" : 1714740000,                     # unix s last downlink received
  "seen"  : ["771234567","781112223"],      # userIDs sniffed in last 90 s
  "uplink": [
      { "msgId":"abc123", "src":"771234567",
        "dst":"780009999", "ts":1714740012,
        "body":"<b64>" }
  ]
}
```

### 5.2 Response

```bash
{
  "ack":   ["abc123"],                        // msgs accepted this call
  "down":  [
      { "msgId":"def456", "src":"780009999",
        "dst":"771234567", "ts":1714740020,
        "body":"<b64>" }
  ],
  "next":  15                                 // client sleeps (seconds) before next sync
}
```
The API always answers within 1 RTT, so the ESP32 can shut Wi-Fi down immediately.


## 5.3 Gateway timing

| Event                                          | Action                                                                 |
|------------------------------------------------|------------------------------------------------------------------------|
| Have ≥ N LoRa packets OR timer (Tu) expires    | Wake Wi-Fi, call `/sync`, sleep next                                   |
| Idle but Vcc high                              | Still call `/sync` every T<sub>idle</sub> (e.g. 90 s) to refresh presence |

## 6 APP ⇄ API endpoints

| Route                  | Method | Purpose                                                             |
|------------------------|--------|---------------------------------------------------------------------|
| `/v1/register`         | POST   | `{userID, phoneDigits, displayName?, anonUid}` – create if absent.  |
| `/v1/message`          | POST   | App-to-Internet user chat. Same JSON as gateway uplink.            |
| `/v1/ack`              | POST   | `{msgIds:[…]}` – marks `tsRead`.                                    |
| `/v1/directory?q=`     | GET    | Prefix search on `phoneDigits` or `userID`.                         |

_All traffic is HTTPS (Let’s Encrypt cert on Oracle VM)._

## 7 Message routing logic (API)

1. Write message doc to `messages/{msgId}` (`status="new"`).

2. Decide path:
   - If `presences/{dstID}.lastSeenAt < 120 s` → enqueue to that gateway in the `down` array of its next `/sync`.
   - Else, if destination user online via Internet client → push directly back in HTTP response.
   - Else → store with `status="pending"`; a Cloud Function trigger (free) watches presences updates and delivers when the user re-appears.

> Firestore callbacks stream changes to the backend process; the Oracle VM never polls.

## 8 Queues & fault tolerance

| Layer    | What happens offline?                                                                                  |
|----------|--------------------------------------------------------------------------------------------------------|
| Gateway  | UART ring buffer (8 KB) persists LoRa packets; flushed each `/sync`.                                  |
| API VM   | `asyncio.Queue` buffers incoming HTTP requests while Firestore is transiently down; on-disk WAL (SQLite) flushes later. |
| Firestore| Native durable writes; if write quota exceeded the API returns 503 so gateways retry later.            |

## 9 Security roadmap

| Phase | LoRa link               | Internet link | ID / auth               |
|-------|-------------------------|---------------|-------------------------|
| MVP   | cleartext               | TLS 1.2       | Firebase Anonymous      |
| 1     | XSalsa20 + 4B MAC       | TLS 1.3       | same                    |
| 2     | Double-Ratchet (Signal) | TLS 1.3       | optional verified phone |

> All headers stay identical; only body becomes ciphertext.

## 10 Free-tier cost table

| Service                | Free quota        | Forecast in pilot (≤ 100 users) |
|------------------------|-------------------|---------------------------------|
| Firestore writes       | 20 000 /day       | < 2 000                         |
| Firestore reads        | 50 000 /day       | < 5 000                         |
| Firestore storage      | 1 GiB             | ~ few MB                        |
| Oracle VM egress       | 10 TB /mo         | negligible                      |
| Firebase Auth anon     | unlimited         | = users                         |
| Let’s Encrypt cert     | free              | —                               |

## 11 Implementation checklist

| Step | Owner   | Notes                                                                 |
|------|---------|-----------------------------------------------------------------------|
| 1    | Ops     | Provision Oracle VM, install Docker (1 CPU, 1 GB, Ubuntu 22)          |
| 2    | Ops     | `docker run Postgres` (optional) for WAL queue; falls back to SQLite if absent |
| 3    | BE      | Deploy FastAPI container; set `FIREBASE_PROJECT` env                  |
| 4    | BE      | Create Firebase project → enable Anonymous Auth and Firestore         |
| 5    | BE      | Write Firestore rules (`request.auth.token.uid == anonUid`)           |
| 6    | FW      | Gateway firmware: Wi-Fi + HTTPS + `/v1/sync`; test with dummy data    |
| 7    | FW      | Mesh firmware: add MSG packet type & fragmentation                    |
| 8    | APP     | Android app: anon sign-in → prompt 9-digit phone → register           |
| 9    | APP     | Implement send/recv UI; talk LoRaService over BLE                     |
| 10   | QA      | Field test: two phones, two nodes, one gateway, Oracle VM over LTE    |
| 11   | DevOps  | Instrument Firestore usage; set TTL on messages                       |
| 12   | Arch    | Plan Phase 1 encryption & group chat extension                        |

## Summary

This specification delivers one-to-one text messaging across a LoRa-based AODV mesh and the public Internet with:

- Zero recurring cost (all services inside free tiers)  
- Lightweight ESP32 gateway traffic (single short HTTPS POST per duty cycle)  
- Simple, Firebase-backed identity without paid SMS  
- An upgrade path to full end-to-end encryption and, later, group chat  
