#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

#ifndef UNIT_TEST
#include <Arduino.h>
#else
#include <string.h>
#endif

enum PacketType : uint8_t
{
    PKT_RREQ = 0x01,
    PKT_RREP = 0x02,
    PKT_RERR = 0x03,
    PKT_DATA = 0x04,
    PKT_BROADCAST = 0x05,
    PKT_BROADCAST_INFO = 0x06,
    PKT_ACK = 0x07,

    PKT_HELLO_SUMMARY = 0x10,
    PKT_GET_DIFF = 0x11,
    PKT_DIFF_REPLY = 0x12,
    PKT_MOVE_UPDATE = 0x13,
    PKT_LOC_REQ = 0x14,
    PKT_LOC_REP = 0x15,
    PKT_STATE_SYNC_REQ = 0x16,
    PKT_STATE_SYNC_CHUNK = 0x17,
    PKT_NODE_ADVERT = 0x18,
    PKT_USER_SUMMARY = 0x19,
    PKT_USER_MOVED = 0x1A // Not sure if this will make the cut
};

static const uint32_t BROADCAST_ADDR = 0xFFFFFFFF;

// Base Header (16 bytes)
struct BaseHeader
{
    uint32_t destNodeID; // 4 bytes
    uint32_t srcNodeID;  // 4 bytes
    uint32_t packetID;   // 4 bytes
    uint8_t packetType;  // 1 byte: 0x01=broadcast, 0x02=RREQ, 0x03=RREP, 0x04 = RERR, 0x05 = ACK, 0x06 = data
    uint8_t flags;       // 1 byte: bitmask for options (e.g., WantAck)
    uint8_t hopCount;    // 1 byte: TTL/hop count
    uint8_t reserved;    // 1 byte: reserved
};

// Extended header for RREQ (8 bytes)
struct RREQHeader
{
    uint32_t originNodeID;   // 4 bytes: origin of the RREQ
    uint32_t RREQDestNodeID; // 4 bytes: Node ID we want to find route to
    // uint8_t currentHops;     // 1 byte:  Current number of hops
    // uint8_t rreqReserved;    // 1 byte:  reserved
};

// Extended header for RREP (11 bytes) -> has to be packed
#pragma pack(push, 1)
struct RREPHeader
{
    uint32_t originNodeID;   // 4 bytes: original rreq requester
    uint32_t RREPDestNodeID; // 4 bytes: destination of route
    uint16_t lifetime;       // 2 bytes: route lifetime
    uint8_t numHops;         // 1 byte:  Number of hops using this route
};
#pragma pack(pop)

// Extended header for RERR (20 bytes)
struct RERRHeader
{
    uint32_t reporterNodeID;     // 4 bytes: Node that reported the issue
    uint32_t brokenNodeID;       // 4 bytes: Broken nodeID
    uint32_t originalDestNodeID; // 4 bytes: Original intended destination
    uint32_t originalPacketID;   // 4 bytes: Original packetID that will have been overwritten
    uint32_t senderNodeID;       // 4 bytes: The original sender
};

// Extended header for ACk (4 bytes)
struct ACKHeader
{
    uint32_t originalPacketID; // 4 bytes: If ACK required return the original message id as an ack
};

// Extended header for DATA (4 bytes)
struct DATAHeader
{
    uint32_t finalDestID; // 4 bytes: final intended target
};

struct BROADCASTINFOHeader
{
    uint32_t originNodeID;
};

/*  --- local‑plane --- */
typedef struct __attribute__((packed)) {
    uint32_t nodeID;
    uint8_t  listVer;
    uint8_t  bloom64[8];
    uint8_t  flags;              /* bit0: BLE‑slot‑free .. etc */
} HELLO_SUMMARY_t;               /* 14 B */

typedef struct __attribute__((packed)) {
    uint32_t srcNodeID;
    uint8_t  wantVer;
} GET_DIFF_t;                    /* 5 B */

typedef struct __attribute__((packed)) {
    uint32_t dstNodeID;
    uint8_t  baseVer;
    uint8_t  n;
    /* trailing n × UserDiffElem */
} DIFF_REPLY_t;

/* one diff element (9 B) */
typedef struct __attribute__((packed)) {
    uint32_t userID;
    uint32_t nodeID;   /* 0 if “drop” */
    uint8_t  seq;
} UserDiffElem_t;

/*  --- mobility advert --- */
typedef struct __attribute__((packed)) {
    uint32_t userID;
    uint32_t newNodeID;
    uint8_t  seq;
    uint32_t prevNodeID;
    /* +16 B signature optional */
} MOVE_UPDATE_t;

/*  --- on‑demand look‑up --- */
typedef struct __attribute__((packed)) {
    uint8_t  reqID;
    uint32_t srcNodeID;
    uint32_t userID;
} LOC_REQ_t;

typedef struct __attribute__((packed)) {
    uint8_t  reqID;
    uint32_t dstNodeID;
    uint32_t userID;
    uint32_t currNodeID;
    uint8_t  seq;
} LOC_REP_t;

/*  --- bootstrap chunk pulls --- */
typedef struct __attribute__((packed)) {
    uint8_t chunkID;
    uint8_t _rsv;
} STATE_SYNC_REQ_t;

typedef struct __attribute__((packed)) {
    uint8_t chunkID;
    uint8_t n;
    /* trailing n × UserDiffElem_t  (same 9 B element) */
} STATE_SYNC_CHUNK_t;

/*  --- directory layer (slow gossip) --- */
typedef struct __attribute__((packed)) {
    uint32_t nodeID;
    uint8_t  capFlags;
    uint8_t  battery;
    int16_t  lat_enc;   // could be removed
    int16_t  lon_enc;   // could be removed
    uint8_t  ver;
} NODE_ADVERT_t;        /* 14 B */

typedef struct __attribute__((packed)) {
    uint8_t  chunkID;
    uint8_t  n;
    /* trailing n × UserDiffElem_t */
} USER_SUMMARY_t;

/*  --- optional early route‑error --- */
typedef struct __attribute__((packed)) {
    uint32_t userID;
    uint32_t newNodeID;
    uint8_t  seq;
} USER_MOVED_t;

// TODO: ESP32-S3 uses little endian currently rely on this for packing and unpacking.
// Serialisation and deserialisation functions:
inline size_t serialiseBaseHeader(const BaseHeader &header, uint8_t *buffer)
{
    size_t offset = 0;
    memcpy(buffer + offset, &header.destNodeID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.srcNodeID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.packetID, 4);
    offset += 4;
    buffer[offset++] = header.packetType;
    buffer[offset++] = header.flags;
    buffer[offset++] = header.hopCount;
    buffer[offset++] = header.reserved;
    return offset;
}

inline size_t deserialiseBaseHeader(const uint8_t *buffer, BaseHeader &header)
{
    size_t offset = 0;
    memcpy(&header.destNodeID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.srcNodeID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.packetID, buffer + offset, 4);
    offset += 4;
    header.packetType = buffer[offset++];
    header.flags = buffer[offset++];
    header.hopCount = buffer[offset++];
    header.reserved = buffer[offset++];
    return offset;
}

inline size_t serialiseRREQHeader(const RREQHeader &header, uint8_t *buffer, size_t offset)
{
    memcpy(buffer + offset, &header.originNodeID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.RREQDestNodeID, 4);
    offset += 4;
    // buffer[offset++] = header.currentHops;
    // buffer[offset++] = header.rreqReserved;
    return offset;
}

inline size_t deserialiseRREQHeader(const uint8_t *buffer, RREQHeader &header, size_t offset)
{
    memcpy(&header.originNodeID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.RREQDestNodeID, buffer + offset, 4);
    offset += 4;
    // header.currentHops = buffer[offset++];
    // header.rreqReserved = buffer[offset++];
    return offset;
}

inline size_t serialiseRREPHeader(const RREPHeader &header, uint8_t *buffer, size_t offset)
{
    memcpy(buffer + offset, &header.originNodeID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.RREPDestNodeID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.lifetime, 2);
    offset += 2;
    buffer[offset++] = header.numHops;
    return offset;
}

inline size_t deserialiseRREPHeader(const uint8_t *buffer, RREPHeader &header, size_t offset)
{
    memcpy(&header.originNodeID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.RREPDestNodeID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.lifetime, buffer + offset, 2);
    offset += 2;
    header.numHops = buffer[offset++];
    return offset;
}

inline size_t serialiseRERRHeader(const RERRHeader &header, uint8_t *buffer, size_t offset)
{
    memcpy(buffer + offset, &header.reporterNodeID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.brokenNodeID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.originalDestNodeID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.originalPacketID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.senderNodeID, 4);
    offset += 4;
    return offset;
}

inline size_t deserialiseRERRHeader(const uint8_t *buffer, RERRHeader &header, size_t offset)
{
    memcpy(&header.reporterNodeID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.brokenNodeID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.originalDestNodeID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.originalPacketID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.senderNodeID, buffer + offset, 4);
    offset += 4;
    return offset;
}

inline size_t serialiseACKHeader(const ACKHeader &ack, uint8_t *buffer, size_t offset)
{
    memcpy(buffer + offset, &ack.originalPacketID, 4);
    offset += 4;
    return offset;
}

inline size_t deserialiseACKHeader(const uint8_t *buffer, ACKHeader &ack, size_t offset)
{
    memcpy(&ack.originalPacketID, buffer + offset, 4);
    offset += 4;
    return offset;
}

inline size_t serialiseDATAHeader(const DATAHeader &data, uint8_t *buffer, size_t offset)
{
    memcpy(buffer + offset, &data.finalDestID, 4);
    offset += 4;
    return offset;
}

inline size_t deserialiseDATAHeader(const uint8_t *buffer, DATAHeader &data, size_t offset)
{
    memcpy(&data.finalDestID, buffer + offset, 4);
    offset += 4;
    return offset;
}

inline size_t serialiseBroadcastInfoHeader(const BROADCASTINFOHeader &data, uint8_t *buffer, size_t offset)
{
    memcpy(buffer + offset, &data.originNodeID, 4);
    offset += 4;
    return offset;
}

inline size_t deserialiseBroadcastInfoHeader(const uint8_t *buffer, BROADCASTINFOHeader &data, size_t offset)
{
    memcpy(&data.originNodeID, buffer + offset, 4);
    offset += 4;
    return offset;
}

#endif
