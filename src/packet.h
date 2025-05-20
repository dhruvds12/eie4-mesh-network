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
    PKT_GATEWAY = 0x08, // Used to inform nodes that I have now got internet connection
    // .......
    PKT_UREQ = 0x0F,
    PKT_UREP = 0x10,
    PKT_UERR = 0x11,
    PKT_USER_MSG = 0x12,
    // TODO new packet type for long range + multihop participation broadcast!!!

};

enum flags : uint8_t
{
    FROM_GATEWAY = 0x01, // message originated/destined from the gateway => this bypasses a lot of functionality ie routing table + GUT
    TO_GATEWAY = 0x02,
    I_AM_GATEWAY = 0x03,
    REQ_ACK = 0x04
};

static constexpr uint8_t FLAG_ENCRYPTED = 0x80;
static const uint32_t BROADCAST_ADDR = 0xFFFFFFFF;

// Base header (16 bytes)
/**
 * @brief The base header (16 bytes).
 *
 * destNodeID      (4 bytes) - The destination of the message (node ID or BROADCAST_ADDR).
 * prevHopID       (4 bytes) - The previous hop of the packet.
 * originNodeID    (4 bytes) -
 *   • RREQ & UREQ:   original sender of the route request
 *   • RREP & UREP:   sender of the route reply
 *   • RERR & UERR:   sender of the error packet
 *   • DATA, BROADCASTINFO, USER_MSG, ACK: original sender of the packet
 * packetID        (4 bytes) - A random packet ID chosen by the sender; constant throughout its journey.
 * packetType      (1 byte)  - Packet type identifier (see PacketType enum).
 * flags           (1 byte)  - Bitmask for optional flags (see flags enum).
 * hopCount        (1 byte)  - Number of hops (incremented +1 per hop).
 * reserved        (1 byte)  - Reserved for future expansion.
 */
struct BaseHeader
{
    uint32_t destNodeID;   // 4 bytes
    uint32_t prevHopID;    // 4 bytes
    uint32_t originNodeID; // 4 bytes
    uint32_t packetID;     // 4 bytes
    uint8_t packetType;    // 1 byte: see PacketType
    uint8_t flags;         // 1 byte: see flags enum
    uint8_t hopCount;      // 1 byte: TTL/hop count
    uint8_t reserved;      // 1 byte: reserved
};

// Extended header for RREQ (8 bytes)
struct RREQHeader
{
    uint32_t RREQDestNodeID; // 4 bytes: Node ID we want to find route to
    // uint8_t currentHops;     // 1 byte:  Current number of hops
    // uint8_t rreqReserved;    // 1 byte:  reserved
};

// Extended header for RREP (11 bytes) -> has to be packed
#pragma pack(push, 1)
struct RREPHeader
{
    // uint32_t originNodeID;   // 4 bytes: original rreq requester
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
    uint32_t originNodeID;       // 4 bytes: The original sender
};

// Extended header for ACk (4 bytes)
struct ACKHeader
{
    uint32_t originalPacketID; // 4 bytes: If ACK required return the original message id as an ack
};

// Extended header for DATA (4 bytes)
struct DATAHeader
{
    uint32_t finalDestID;  // 4 bytes: final intended target
    uint32_t originNodeID; // 4 bytes: oringal sender
};

struct BROADCASTINFOHeader
{
    uint32_t originNodeID;
};

#pragma pack(push, 1)
struct DiffBroadcastInfoHeader
{
    uint32_t originNodeID; // 4 B
    uint16_t numAdded;     // 2 B
    uint16_t numRemoved;   // 2 B
};
#pragma pack(pop)

struct UREQHeader
{
    uint32_t originNodeID;
    uint32_t userID;
};

struct UREPHeader
{
    uint32_t originNodeID;
    uint32_t destNodeID;
    uint32_t userID;
    uint16_t lifetime;
    uint8_t numHops;
};

struct UERRHeader
{
    uint32_t userID;
    uint32_t nodeID;
    uint32_t originNodeID;
    uint32_t originalPacketID;
};

struct UserMsgHeader
{
    uint32_t fromUserID;
    uint32_t toUserID;
    uint32_t toNodeID;
    uint32_t originNodeID;
};

// TODO: ESP32-S3 uses little endian currently rely on this for packing and unpacking.
// Serialisation and deserialisation functions:

// ──────────────────────────────────────────────────────────────────────────────
// Base
// ──────────────────────────────────────────────────────────────────────────────
inline size_t serialiseBaseHeader(const BaseHeader &header, uint8_t *buffer)
{
    size_t offset = 0;
    memcpy(buffer + offset, &header.destNodeID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.prevHopID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.originNodeID, 4);
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
    memcpy(&header.prevHopID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.originNodeID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.packetID, buffer + offset, 4);
    offset += 4;
    header.packetType = buffer[offset++];
    header.flags = buffer[offset++];
    header.hopCount = buffer[offset++];
    header.reserved = buffer[offset++];
    return offset;
}

// ──────────────────────────────────────────────────────────────────────────────
// RREQ (Route Request)
// ──────────────────────────────────────────────────────────────────────────────

inline size_t serialiseRREQHeader(const RREQHeader &header, uint8_t *buffer, size_t offset)
{

    memcpy(buffer + offset, &header.RREQDestNodeID, 4);
    offset += 4;
    // buffer[offset++] = header.currentHops;
    // buffer[offset++] = header.rreqReserved;
    return offset;
}

inline size_t deserialiseRREQHeader(const uint8_t *buffer, RREQHeader &header, size_t offset)
{

    memcpy(&header.RREQDestNodeID, buffer + offset, 4);
    offset += 4;
    // header.currentHops = buffer[offset++];
    // header.rreqReserved = buffer[offset++];
    return offset;
}

// ──────────────────────────────────────────────────────────────────────────────
// RREP (Route Reply)
// ──────────────────────────────────────────────────────────────────────────────
inline size_t serialiseRREPHeader(const RREPHeader &header, uint8_t *buffer, size_t offset)
{

    memcpy(buffer + offset, &header.RREPDestNodeID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.lifetime, 2);
    offset += 2;
    buffer[offset++] = header.numHops;
    return offset;
}

inline size_t deserialiseRREPHeader(const uint8_t *buffer, RREPHeader &header, size_t offset)
{
    memcpy(&header.RREPDestNodeID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.lifetime, buffer + offset, 2);
    offset += 2;
    header.numHops = buffer[offset++];
    return offset;
}

// ──────────────────────────────────────────────────────────────────────────────
// RERR (Route Error)
// ──────────────────────────────────────────────────────────────────────────────

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
    memcpy(buffer + offset, &header.originNodeID, 4);
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
    memcpy(&header.originNodeID, buffer + offset, 4);
    offset += 4;
    return offset;
}

// ──────────────────────────────────────────────────────────────────────────────
// ACK (Acknowledgement)
// ──────────────────────────────────────────────────────────────────────────────

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

// ──────────────────────────────────────────────────────────────────────────────
// Data Header
// ──────────────────────────────────────────────────────────────────────────────

inline size_t serialiseDATAHeader(const DATAHeader &data, uint8_t *buffer, size_t offset)
{
    memcpy(buffer + offset, &data.finalDestID, 4);
    offset += 4;
    memcpy(buffer + offset, &data.originNodeID, 4);
    offset += 4;
    return offset;
}

inline size_t deserialiseDATAHeader(const uint8_t *buffer, DATAHeader &data, size_t offset)
{
    memcpy(&data.finalDestID, buffer + offset, 4);
    offset += 4;
    memcpy(&data.originNodeID, buffer + offset, 4);
    offset += 4;
    return offset;
}

// ──────────────────────────────────────────────────────────────────────────────
// BroadcastInfo
// ──────────────────────────────────────────────────────────────────────────────

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

// ──────────────────────────────────────────────────────────────────────────────
// UREQ (User Route Request)
// ──────────────────────────────────────────────────────────────────────────────
inline size_t serialiseUREQHeader(const UREQHeader &header, uint8_t *buffer, size_t offset)
{
    // originNodeID
    memcpy(buffer + offset, &header.originNodeID, 4);
    offset += 4;
    // userID
    memcpy(buffer + offset, &header.userID, 4);
    offset += 4;
    return offset;
}

inline size_t deserialiseUREQHeader(const uint8_t *buffer, UREQHeader &header, size_t offset)
{
    memcpy(&header.originNodeID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.userID, buffer + offset, 4);
    offset += 4;
    return offset;
}

// ──────────────────────────────────────────────────────────────────────────────
// UREP (User Route Reply)
// ──────────────────────────────────────────────────────────────────────────────
inline size_t serialiseUREPHeader(const UREPHeader &header, uint8_t *buffer, size_t offset)
{
    memcpy(buffer + offset, &header.originNodeID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.destNodeID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.userID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.lifetime, 2);
    offset += 2;
    buffer[offset++] = header.numHops;
    return offset;
}

inline size_t deserialiseUREPHeader(const uint8_t *buffer, UREPHeader &header, size_t offset)
{
    memcpy(&header.originNodeID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.destNodeID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.userID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.lifetime, buffer + offset, 2);
    offset += 2;
    header.numHops = buffer[offset++];
    return offset;
}

// ──────────────────────────────────────────────────────────────────────────────
// UERR (User Error)
// ──────────────────────────────────────────────────────────────────────────────
inline size_t serialiseUERRHeader(const UERRHeader &header, uint8_t *buffer, size_t offset)
{
    memcpy(buffer + offset, &header.userID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.nodeID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.originNodeID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.originalPacketID, 4);
    offset += 4;
    return offset;
}

inline size_t deserialiseUERRHeader(const uint8_t *buffer, UERRHeader &header, size_t offset)
{
    memcpy(&header.userID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.nodeID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.originNodeID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.originalPacketID, buffer + offset, 4);
    offset += 4;
    return offset;
}

// ──────────────────────────────────────────────────────────────────────────────
// USER_MSG (User Message)
// ──────────────────────────────────────────────────────────────────────────────
inline size_t serialiseUserMsgHeader(const UserMsgHeader &header, uint8_t *buffer, size_t offset)
{
    memcpy(buffer + offset, &header.fromUserID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.toUserID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.toNodeID, 4);
    offset += 4;
    memcpy(buffer + offset, &header.originNodeID, 4);
    offset += 4;

    return offset;
}

inline size_t deserialiseUserMsgHeader(const uint8_t *buffer, UserMsgHeader &header, size_t offset)
{
    memcpy(&header.fromUserID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.toUserID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.toNodeID, buffer + offset, 4);
    offset += 4;
    memcpy(&header.originNodeID, buffer + offset, 4);
    offset += 4;
    return offset;
}

#endif
