#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

#ifndef UNIT_TEST
#include <Arduino.h>
#else
#include <string.h>
#endif

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

#endif
