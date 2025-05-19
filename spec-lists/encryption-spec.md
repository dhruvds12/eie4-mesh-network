# FYP Mesh-Chat Security — Phase-1 Design Specification

*(`K_net` + user end-to-end; `K_node` reserved for Phase-2)*

---

## 0 . Goals & Scope

| # | Requirement                                                                                                 | In scope for **Phase-1**            | Notes / Phase-2 hook             |
| - | ----------------------------------------------------------------------------------------------------------- | ----------------------------------- | -------------------------------- |
| 1 | **Confidentiality** of all control traffic (RREQ/RREP/RERR, BroadcastInfo, BLE provisioning, gateway meta). | YES — encrypted with `K_net`.       |                                  |
| 2 | **Confidentiality & authenticity** of user chat payloads.                                                   | YES — X25519/AES-GCM per user pair. |                                  |
| 3 | **Optional encryption** for node⇄node data-packets; user UI must show “⚠ Unencrypted” when disabled.        | YES — flag per message.             |                                  |
| 4 | **Broadcast DATA** (group chat / alerts) must be encrypted, but still readable by every node.               | YES — encrypted with `K_net`.       |                                  |
| 5 | Future **per-node hop secrecy** via `K_node` without breaking the above.                                    | RESERVED (design stub).             | Only unicast DATA will adopt it. |

---

## 1 . Cryptographic primitives

| Primitive                   | Algorithm                                      | Library (µ-Controller)                                                  | Library (Android)                                                             |
| --------------------------- | ---------------------------------------------- | ----------------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| Symmetric encryption & AEAD | **AES-GCM-128** (12-byte nonce, 16-byte tag)   | *mbedTLS* (`mbedtls_gcm_*`) — already in ESP-IDF; HW-accel on ESP32-Sx. | *Google Tink* Aead (`AesGcmSiv`) or *BouncyCastle* `GCMBlockCipher`.          |
| Asymmetric DH               | **X25519**                                     | *libsodium* (`crypto_kx_*`) – statically linked (\~30 kB).              | `java.security.KeyPairGenerator("X25519")` (API 33+) or Tink *Hybrid* module. |
| RNG                         | DRBG seeded from ESP32 HWRNG (`esp_random()`). | `SecureRandom`.                                                         |                                                                               |

*All algorithms are NIST-approved and available in Kotlin/JVM and embedded C.*

---

## 2 . Key hierarchy

| Symbol                              | Type / length | Created where             | Stored                                                                   | Used to encrypt                               | Rotation                                                                                           |
| ----------------------------------- | ------------- | ------------------------- | ------------------------------------------------------------------------ | --------------------------------------------- | -------------------------------------------------------------------------------------------------- |
| **`K_net`**                         | 128-bit AES   | Factory tool              | Flash (NVS) of **all** nodes; also in Android app & provisioning backend | • All control packets<br>• Broadcast DATA     | Rare (requires reflashing whole fleet or OTA “network re-key” broadcast signed by maintainer key). |
| **User key-pair** `(Priv_u, Pub_u)` | X25519        | Phone **once** on sign-up | `Priv_u` in Android Keystore; `Pub_u` in Firestore `users/{uid}/pubKey`  | USER\_MSG payloads (pairwise AES-GCM session) | User can regenerate; triggers Firestore update & push-msg to nodes.                                |
| **`K_node[N]`** (***Phase-2***)     | 128-bit AES   | Node *N* (random)         | Node *N* flash + replicated table on every peer                          | Unicast DATA `srcNodeID=N`                    | On demand via `UPDATE_NODE_KEY` msg (encrypted under old `K_node[N]`).                             |

> **Security levels shown to user:**
>
> * 🔒 **E2EE** (User → User)
> * 🖧 **Mesh-encrypted** (via `K_net`)
> * ⚠ **Plaintext** (when encryption toggled off by user)

---

## 3 . Packet format additions (LoRa, MQTT, BLE-bridge)

```
+---------------- BaseHeader (existing, clear) ---------------------------+
| destNodeID | srcNodeID | packetID | type | flags | hopCount | reserved |
+------------------------------------------------------------------------+
| ENC_FLAG (1 B) | NONCE (12 B) | CIPHERTEXT (ExtHdr + Payload) | TAG (16 B) |
+------------------------------------------------------------------------+
```

* `flags |= ENC_NET` when encrypted by `K_net`.
* `flags |= ENC_USER` when encrypted by user pair key.
* `flags |= ENC_NODE` (future) for `K_node`.

`NONCE` composition per scheme:

| Scheme   | Nonce layout (12 B)                                      |
| -------- | -------------------------------------------------------- |
| `K_net`  | `packetID[4] ∥ 0x4E6574("Net")[3] ∥ type[1] ∥ random[4]` |
| `K_user` | `packetID[4] ∥ srcUserID[4] ∥ dstUserID[4]`              |
| `K_node` | `packetID[4] ∥ srcNodeID[4] ∥ 0x4E64("Nd")[2] ∥ 0`       |

---

## 4 . Firmware changes (C++ / ESP-IDF)

### 4.1 New modules

```text
src/crypto/CryptoManager.h/.cpp
    bool encrypt(NetLevel lvl, const BaseHeader&, uint8_t* buf, size_t len,
                 uint8_t* out, size_t& outLen);
    bool decrypt(NetLevel lvl, const BaseHeader&, uint8_t* buf, size_t len,
                 uint8_t* out, size_t& outLen);
```

*Pulls key from `KeyStore` (NVS-backed singleton).*

### 4.2 Router hot-path adjustments

* **`transmitPacket()`**

  ```cpp
  if(flags & (ENC_NET|ENC_USER|ENC_NODE)) {
      CryptoManager::encrypt(level, hdr, payloadBuf, payloadLen,
                             txBuf+hdrLen, encLen);
  }
  ```
* **`handlePacket()`** – after reading `BaseHeader`

  ```cpp
  if(bh.flags & ENC_MASK) {
      if(!CryptoManager::decrypt(level, bh, extPayBuf, len, decBuf, decLen)){
          return;          // drop-auth-fail
      }
      extPayBuf = decBuf;
      len = decLen;
  }
  ```

### 4.3 Control path (broadcast)

* All `sendRREQ / sendBroadcastInfo / …` paths set `flags |= ENC_NET`.
* Their transmit helper calls `encrypt(ENC_NET, …)` automatically.

### 4.4 BLE interface updates

* New GATT opcodes

  * `0x20 SET_NODE_KEY{16 B}` (encrypted with `K_net`)
  * `0x21 SET_USER_PUBKEY{32 B}` (plain in BLE-secure channel)
  * `0x22 SET_ENC_PREF {0=plain,1=net,2=user}`

### 4.5 Compile-time switch

`CONFIG_MESH_CRYPTO_OPT` enum: `OFF / OPTIONAL / FORCE`.
*At **OPTIONAL** the router auto-downgrades if next-hop broadcast advert lacks `CRYPTO_CAP` flag.*

---

## 5 . Android ( Kotlin / Jetpack )

### 5.1 Dependencies

```kotlin
implementation("com.google.crypto.tink:tink-android:1.12.0")
implementation("org.bouncycastle:bcprov-jdk18on:1.78") // fallback
```

### 5.2 Key generation & storage

```kotlin
val kp = KeyPairGenerator.getInstance("X25519")
        .apply { initialize(255) }
        .generateKeyPair()
val priv = kp.private.encoded  // 32 B
val pub  = kp.public.encoded   // 32 B

// store priv securely
val ksEntry = KeyStore.SecretKeyEntry(
        SecretKeySpec(priv, "RAW"))
androidKeyStore.setEntry("mesh_priv", ksEntry, null)

// publish pub to Firestore
Firebase.firestore.collection("users")
   .document(auth.uid!!)
   .set(mapOf("pubKey" to Base64.encodeToString(pub, NO_WRAP)))
```

### 5.3 Message send flow

1. Compose → user selects “Encrypted chat” toggle.
2. App fetches `pubKey` of peer from **Firestore (online)** or from **Node (offline LIST\_USERS\_RESP)**.
3. Build `K_user` with `X25519`.
4. Encrypt payload via Tink `Aead` (`AES128_GCM_SIV`).
5. Fill `UserMsgHeader`, set `ENC_USER`, wrap into BLE TX frame.
6. Display padlock colour:

   * 🟢 E2EE,
   * 🟡 Mesh-encrypted,
   * 🔴 Plain.

### 5.4 Receive flow

* Parse ENC flags.
* Try decrypting in order `K_user → K_net`.
* If both fail, mark as unknown/unsafe.

---

## 6 . Firebase design (public-key directory)

| Collection               | Doc-ID                | Fields                                 |
| ------------------------ | --------------------- | -------------------------------------- |
| **`users`**              | `uid` (Firebase Auth) | `pubKey` = Base64(X25519)              |
| **`nodes`** *(optional)* | `nodeId` (UInt32)     | `ownerUid`, `lastSeen`, `firmwareVers` |

**Security rules**

```js
match /users/{uid} {
  allow read: if request.auth != null;
  allow write: if request.auth != null && request.auth.uid == uid;
}
```

*Cloud Function* `onCreate` to fan-out new `pubKey` to gateway topic for caching.

---

## 7 . Security level handshake & UI surface

* Nodes include `CAP_CRYPTO (bit0)` in `BroadcastInfo.flags`.
* App displays **chat header badge** determined by:

  * Has `ENC_USER` tag — show 🔒“E2EE”;
  * else if peer-node advertises `CAP_CRYPTO` **and** user opts-in — show 🖧 mesh-encrypted;
  * else red banner: “Messages visible on radio channel”.

---

## 8 . Testing matrix

| Test                   | Setup                         | Expected                                                                                              |
| ---------------------- | ----------------------------- | ----------------------------------------------------------------------------------------------------- |
| Unit — CryptoManager   | Host GTest                    | enc→dec round-trip all three levels                                                                   |
| Interop — Phone ⇄ Node | BLE, no Wi-Fi                 | E2EE chat works offline                                                                               |
| Broadcast storm        | 50 nodes, 1 pps BroadcastInfo | 0 auth failures, 0 drops                                                                              |
| Downgrade attack       | Evil node omits `CAP_CRYPTO`  | Neighbours fall back to plaintext **only** if `CONFIG_MESH_CRYPTO_OPT==OPTIONAL`; phone shows warning |
| Size check             | DATA 200 B E2EE               | fits ≤ 255 B frame                                                                                    |

---

## 9 . Phase-2 (`K_node`) stub (no code now)

* Extend `BroadcastInfo` with optional TLV `NODE_KEY{ nodeId, K_node }` encrypted under `K_net`.
* Add `ENC_NODE` flag defined above; used **only** for unicast DATA.
* Same CryptoManager API — new enum value maps to the table lookup.
* Requires \~ (N×16 B) RAM for key-table; compile-time enable.

---

### 10 . Deliverables checklist

* [ ] `CryptoManager` & `KeyStore` (C++)
* [ ] Packet encoder/decoder mods in AODVRouter & RadioManager
* [ ] BLE opcodes & characteristic handler update
* [ ] Kotlin helper class `MeshAead.kt` + UI badges
* [ ] Firestore rules & onboarding Cloud Function
* [ ] Integration test script (Python + ESP-Tool)

*Releasing with these deliverables meets Phase-1 requirements and leaves a clean hook for per-node hop secrecy in Phase-2.*
