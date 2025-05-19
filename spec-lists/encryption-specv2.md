# FYP Mesh-Chat Security — **Phase 1 implementation spec (revision 2)**

*(`K_net` + user E2EE, 64-bit sender counter, optional 2-byte “nonce suffix”, `K_node` postponed to Phase 2)*

---

## 0 Goals & scope (unchanged)

| # | Requirement (Phase 1)                                                                                          | Status                                          |
| - | -------------------------------------------------------------------------------------------------------------- | ----------------------------------------------- |
| 1 | **Confidentiality + authenticity** of every control packet (RREQ/…/BroadcastInfo/ provisioning/ gateway meta). | ✅  Encrypt with **`K_net`**, 8-byte tag, fixed. |
| 2 | **End-to-end confidentiality + authenticity** of user chat.                                                    | ✅  X25519 → AES-GCM session.                    |
| 3 | **Optional encryption** for node⇄node DATA; UI shows security badge.                                           | ✅  `ENC_NET` flag toggled by user.              |
| 4 | **Broadcast DATA** must be encrypted but readable by any node.                                                 | ✅  `K_net`, always 8-byte tag.                  |
| 5 | Future hop-by-hop secrecy with `K_node`.                                                                       | 🚧  Stub left; no impact on Phase 1 packets.    |

---

## 1 Crypto primitives & libraries (recap)

| Primitive       | Algorithm       | ESP-IDF (C++)                                                         | Android (Kotlin)                                     |
| --------------- | --------------- | --------------------------------------------------------------------- | ---------------------------------------------------- |
| Symmetric AEAD  | **AES-GCM-128** | *mbedTLS* (`mbedtls_gcm_*`) – HW-accelerated on ESP32-S\*, ESP32-C\*. | *Google Tink* (`AesGcmSiv`) or BouncyCastle GCM.     |
| Asymmetric E2EE | **X25519** DH   | *libsodium* (`crypto_kx_*`) — \~30 kB.                                | `KeyPairGenerator("X25519")` (API33+) / Tink Hybrid. |
| RNG             | HW RNG → DRBG   | `esp_random()`                                                        | `SecureRandom()`                                     |

---

## 2 Key hierarchy (recap)

| Key                             | Purpose                               | Stored in                                    | Encrypts                             | Tag length                |
| ------------------------------- | ------------------------------------- | -------------------------------------------- | ------------------------------------ | ------------------------- |
| **`K_net`** (128 bit)           | Mesh-wide control & broadcast channel | NVS on every node; provisioning app; gateway | All control packets & broadcast DATA | **8 bytes** (fixed)       |
| **User pair key** `K_user(A,B)` | User⇄user chat                        | Phones A,B (derived), *not* in nodes/cloud   | `PKT_USER_MSG`                       | Configurable 0 / 8 / 16 B |
| **`K_node[N]`**                 | Hop-by-hop data secrecy (Phase 2)     | Node N + peer cache                          | Unicast DATA from N                  | 8 B                       |

---

## 3 Nonce & tag design

### 3.1 Why nonce = 12 bytes, tag ≥ 8 bytes?

* **GCM** (Galois/Counter Mode) needs a unique 96-bit IV/nonce per `(key,packet)` or confidentiality breaks.
* Tag is the built-in MAC: without it, any bit-flip is silent. 8 bytes gives 1-in-2⁶⁴ forgery probability (Thread / Wi-SUN default).

### 3.2 Zero-overhead nonce: **64-bit sender counter**

Each node stores one 64-bit ever-increasing counter **Ctr** in NVS.

| Nonce field (12 B)        | Source                                                |
| ------------------------- | ----------------------------------------------------- |
| `packetID_lo` (4 B)       | low 32 bits of **Ctr** (already the `packetID` field) |
| `srcNodeID` (4 B)         | existing header field                                 |
| `type` (1 B)              | existing header field                                 |
| `packetID_hi[23:0]` (3 B) | high 24 bits of **Ctr** (never transmitted)           |

#### Sender algorithm

```cpp
Ctr = nvs_read64("ctr") + 1;
nvs_write64("ctr", Ctr)       // batched every N packets
packetID = uint32_t(Ctr);     // low 32 bits on air
nonce = buildNonce(Ctr, srcNodeID, type);
aes_gcm_encrypt(key, nonce, tagLen, ...);
```

#### Receiver algorithm (per `srcNodeID`)

```cpp
uint64_t last = table[src];                 // 0 if first time
uint64_t base = last & 0xFFFFFFFF00000000ULL;
uint64_t cand = base | packetID_lo;
if(cand <= last) cand += 1ULL<<32;          // wrap correction
if(cand - last > (1ULL<<24)) drop();        // anti-replay 16 M
nonce = buildNonce(cand, srcNodeID, type);
if(!aes_gcm_decrypt(...)) drop();
table[src] = cand;                          // accept & update
```

*Works for broadcasts, hop-limited packets, and missed frames because comparison is strictly “greater than”.*

#### Optional explicit nonce (small-header variant)

If flash wear or logic worries you:

* Use the **1-byte `reserved` field + 2 new bytes** (`NonceExt`) appended right after BaseHeader.
* Overhead = 2 bytes only on encrypted packets. Implementation flag: `CONFIG_EXPLICIT_NONCE`.

---

## 4 Packet format (Phase 1)

```
┌──────────────── BaseHeader (14 B, unchanged) ────────────────┐
│ destID | srcID | packetID | type | flags | hop | reserved   │
└──────────────────────────────────────────────────────────────┘
                     ↑ bit7 = FLAG_ENCRYPTED   (no new byte)

[ optional 2-byte NonceExt ]    ← only if CONFIG_EXPLICIT_NONCE

┌────────────── Encrypted region (ExtHdr + Payload) ───────────┐
│ ... variable ...                                             │
└──────────────────────────────────────────────────────────────┘
┌── Tag ──┐   8 / 16 B (or 0 B by chat preference)
└──────────┘
```

---

## 5 Encryption rules by packet type

| Packet type                                                                                        | Key used                         | Tag bytes            | Notes                                                   |
| -------------------------------------------------------------------------------------------------- | -------------------------------- | -------------------- | ------------------------------------------------------- |
| **Control** (`PKT_RREQ`, `PKT_RREP`, `PKT_RERR`, `PKT_BROADCAST_INFO`, provisioning, gateway meta) | `K_net`                          | **8** (fixed)        | Always encrypted.                                       |
| **Broadcast DATA** (`destNodeID=BROADCAST_ADDR`)                                                   | `K_net`                          | **8**                | Needed so every node can read; non-configurable.        |
| **Node⇄Node unicast DATA**                                                                         | Off / `K_net` (user flag)        | 8                    | Default ON; user can disable → plaintext.               |
| **User⇄User `PKT_USER_MSG`**                                                                       | `K_user(A,B)` (X25519 → AES-GCM) | 8 (default) / 16 / 0 | Length preference sent in USER-MSG ext-header (2 bits). |
| *(Phase 2)* Node⇄Node DATA                                                                         | `K_node[src]`                    | 8                    | Only unicast; broadcasts stay on `K_net`.               |

---

## 6 Firmware changes (delta from rev-1)

* **`packet.h`**

  * Reserve `FLAG_ENCRYPTED = 0x80`.
  * Add enum `TagLen : uint8_t { TAG8=0, TAG16=1, TAG0=2 };`
* **CryptoManager**

  * `encrypt(level, tagLen, …)` / `decrypt(...)` accept tag length arg.
  * Nonce builder chooses implicit counter or explicit field at compile-time.
* **Router duplicate detection**

  * For encrypted packets rely on *LastSeenTable* described above.
  * For plaintext keep legacy `(srcID, packetID)` set.
* **Flash writes**

  * Counter cached in RAM, flushed every 256 packets **or** every 15 s. Wear ≈ 1 k writes/day → 10 years on 10 k P/E cell.
* **BLE GATT opcode 0x22 (SET\_ENC\_PREF)**

  * Payload: `{ uint8_t pref; uint8_t tagLenBits }`.
  * Node persists per-user preference in `UserSessionManager`.

---

## 7 Android details (additions)

* USER-MSG ext-header now contains: `uint8 tagLenBits:2 | reserved:6`.
* If user picks “No integrity (0 B)” the app shows 🔴 “Tamper-unsafe mode”.
* Default chat setting: *Secure (8 B tag)*.

---

## 8 Testing / simulation amendments

| New test                            | What to do                                             | Pass criteria                                                               |
| ----------------------------------- | ------------------------------------------------------ | --------------------------------------------------------------------------- |
| **Nonce-wrap**                      | Force sender counter to `0xFFFF_FFFF`, send 3 packets. | Receiver accepts two, rejects replay of first after wrap correction.        |
| **Tag-length interop**              | A->B chat with 0, 8, 16 B tag.                         | Both sides decrypt correctly; sniffed packets differ in length as expected. |
| **No-flash** (explicit nonce build) | Compile with `CONFIG_EXPLICIT_NONCE`.                  | Firmware boots, encrypted traffic has 2 extra bytes, decrypts OK.           |

---

## 9 Glossary (for doc readers)

| Acronym        | Meaning                                                  |
| -------------- | -------------------------------------------------------- |
| **AES**        | Advanced Encryption Standard (block cipher).             |
| **GCM**        | Galois/Counter Mode = AES in CTR + GHASH authentication. |
| **Nonce / IV** | Number-used-once; must be unique per `(key,packet)`.     |
| **Tag**        | Authentication field (MAC) appended to ciphertext.       |
| **NVS**        | ESP-IDF Non-Volatile Storage key/value flash partition.  |

---

## 10 Remaining Phase-1 deliverables (no change)

* CryptoManager & KeyStore
* Router + RadioManager modifications
* BLE & Kotlin integrations
* Firestore rules, Cloud Function
* Test scripts & docs

This revision integrates the 64-bit counter mechanism, wrap-around handling,
optional explicit nonce field, and tag-length configurability while keeping the
header size constant and satisfying all original goals.
