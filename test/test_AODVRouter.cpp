#include <gtest/gtest.h>
#include "AODVRouter.h"
#include "mocks/MockRadioManager.h"

static const uint8_t PKT_BROADCAST_INFO = 0x00;
static const uint8_t PKT_BROADCAST = 0x01;
static const uint8_t PKT_RREQ = 0x02;
static const uint8_t PKT_RREP = 0x03;
static const uint8_t PKT_RERR = 0x04;
static const uint8_t PKT_ACK = 0x05;
static const uint8_t PKT_DATA = 0x06;

static const uint32_t BROADCAST_ADDR = 0xFFFFFFFF;

TEST(AODVRouterTest, BasicSendDataTest)
{
    MockRadioManager mockRadio;
    uint32_t myID = 100;
    AODVRouter AODVRouter(&mockRadio, myID);

    uint8_t testData[] = {0xDE, 0xAD, 0xBE, 0xEF};
    AODVRouter.sendData(200, testData, sizeof(testData));

    // check that there is an addition to the dataBuffer
    ASSERT_FALSE(AODVRouter._dataBuffer.empty()) << "Expected a new val in dataBuffer";
    std::vector<dataBufferEntry> entry = AODVRouter._dataBuffer[200];

    ASSERT_FALSE(entry.empty()) << "Expected at least one entry for dest 200";
    EXPECT_EQ(entry.size(), 1) << "Expected entry for destNodeId: 200 to be 1";
    EXPECT_EQ(memcmp(entry[0].data, testData, sizeof(testData)), 0) << "Buffer content not equal to expected test data";

    // Need to check that the packet type is a RREQ

    ASSERT_FALSE(mockRadio.txPacketsSent.empty()) << "Expected a packet to be transmitted!";
    // Check the element that has been stored on the txPacketSent vector
    const std::vector<uint8_t> &packetBuffer = mockRadio.txPacketsSent[0].data;

    BaseHeader baseHdr;
    deserialiseBaseHeader(packetBuffer.data(), baseHdr);
    EXPECT_EQ(baseHdr.packetType, PKT_RREQ) << "Packet type should be RREQ";
    EXPECT_EQ(baseHdr.destNodeID, BROADCAST_ADDR) << "Base header destNodeID should be broadcast";
    EXPECT_EQ(baseHdr.srcNodeID, myID) << "Source node should match router's ID";

    size_t baseHeaderSize = sizeof(BaseHeader);
    RREQHeader rreqHdr;
    deserialiseRREQHeader(const_cast<uint8_t *>(packetBuffer.data()), rreqHdr, baseHeaderSize);
    EXPECT_EQ(rreqHdr.originNodeID, myID) << "Origin node ID should match router's ID";
    EXPECT_EQ(rreqHdr.RREQDestNodeID, 200) << "RREQ destination should be 200";
}

TEST(AODVRouterTest, BasicReceiveRREP)
{
    MockRadioManager mockRadio;
    uint32_t myID = 5738;
    AODVRouter AODVRouter(&mockRadio, myID);

    BaseHeader baseHdr;
    baseHdr.destNodeID = myID;
    baseHdr.srcNodeID = 200;
    baseHdr.packetID = 56464645;
    baseHdr.packetType = PKT_RREP;
    baseHdr.flags = 0;
    baseHdr.hopCount = 0;
    baseHdr.reserved = 0;

    RREPHeader rrep;
    rrep.originNodeID = myID;   // node that originally needed the route
    rrep.RREPDestNodeID = 5656; // destination of the route
    rrep.lifetime = 0;
    rrep.numHops = 7;

    uint8_t buffer[255];
    size_t offset = 0;

    offset = serialiseBaseHeader(baseHdr, buffer + offset);
    offset = serialiseRREPHeader(rrep, buffer, offset);

    RadioPacket packet;
    std::copy(buffer, buffer + offset, packet.data);
    packet.len = offset;

    AODVRouter.handlePacket(&packet);

    ASSERT_FALSE(AODVRouter._routeTable.empty()) << "Expected a new route to be added";

    EXPECT_EQ(AODVRouter.hasRoute(5656), true) << "Route to new node should be added to table";

    RouteEntry ExpectedRe;
    ExpectedRe.nextHop = 200;
    ExpectedRe.hopcount = 8;

    RouteEntry actualRe;
    actualRe = AODVRouter.getRoute(5656);

    // Check RouteEntry for requested node
    EXPECT_EQ(actualRe.nextHop, ExpectedRe.nextHop) << "Incorrect next hop";
    EXPECT_EQ(actualRe.hopcount, ExpectedRe.hopcount) << "Incorrect hop count";

    // Check RouteEntry for the node delivering the RREP
    RouteEntry ExpectedReDeliveringNode;
    ExpectedReDeliveringNode.nextHop = 200;
    ExpectedReDeliveringNode.hopcount = 1;

    RouteEntry actualReDeliveringNode;
    actualReDeliveringNode = AODVRouter.getRoute(200);

    EXPECT_EQ(actualReDeliveringNode.nextHop, ExpectedReDeliveringNode.nextHop) << "Incorrect next hop";
    EXPECT_EQ(actualReDeliveringNode.hopcount, ExpectedReDeliveringNode.hopcount) << "Incorrect hop count";
}

// Ensure that the RREP is forwarded + routes added
TEST(AODVRouterTest, ForwardRREP)
{
    // initial setup
    MockRadioManager mockRadio;
    uint32_t myID = 5738;
    AODVRouter AODVRouter(&mockRadio, myID);
    // need to add a route to node 300 so that the packet get routed
    AODVRouter.updateRoute(300, 400, 7);
    ASSERT_FALSE(AODVRouter._routeTable.empty()) << "Expected a new route to be added";

    // create the received radio message
    BaseHeader baseHdr;
    baseHdr.destNodeID = myID;
    baseHdr.srcNodeID = 200;
    baseHdr.packetID = 56464645;
    baseHdr.packetType = PKT_RREP;
    baseHdr.flags = 0;
    baseHdr.hopCount = 0;
    baseHdr.reserved = 0;

    RREPHeader rrep;
    rrep.originNodeID = 300;    // node that originally needed the route
    rrep.RREPDestNodeID = 5656; // destination of the route
    rrep.lifetime = 0;
    rrep.numHops = 7;

    EXPECT_EQ(rrep.originNodeID, 300) << "RREP init origin node should be 300";
    EXPECT_EQ(rrep.RREPDestNodeID, 5656) << "RREP init destination node should be 5656";
    EXPECT_EQ(rrep.lifetime, 0) << "RREP init lifetime should be 0 - infinite";
    EXPECT_EQ(rrep.numHops, 7) << "RREP init num hops should be incremented to 8";

    uint8_t buffer[255];
    size_t offset = 0;

    // Call handle packet
    offset = serialiseBaseHeader(baseHdr, buffer);
    offset = serialiseRREPHeader(rrep, buffer, offset);

    RadioPacket packet;
    std::copy(buffer, buffer + offset, packet.data);
    packet.len = offset;

    AODVRouter.handlePacket(&packet);

    // Check routes and destnode of rrep and srcNode should be added
    // Make sure not empty -> no point continuing
    ASSERT_FALSE(AODVRouter._routeTable.empty()) << "Expected a new route to be added";

    RouteEntry ExpectedReDeliveringNode;
    ExpectedReDeliveringNode.nextHop = 200;
    ExpectedReDeliveringNode.hopcount = 1;

    RouteEntry actualReDeliveringNode;
    actualReDeliveringNode = AODVRouter.getRoute(200);
    EXPECT_EQ(actualReDeliveringNode.nextHop, ExpectedReDeliveringNode.nextHop) << "Incorrect next hop for 200";
    EXPECT_EQ(actualReDeliveringNode.hopcount, ExpectedReDeliveringNode.hopcount) << "Incorrect hop count for 200";

    RouteEntry ExpectedRe;
    ExpectedRe.nextHop = 200;
    ExpectedRe.hopcount = 8;

    RouteEntry actualRe;
    actualRe = AODVRouter.getRoute(5656);

    // Check RouteEntry for requested node
    EXPECT_EQ(actualRe.nextHop, ExpectedRe.nextHop) << "Incorrect next hop";
    EXPECT_EQ(actualRe.hopcount, ExpectedRe.hopcount) << "Incorrect hop count";

    // check that transmitPacket map has a new entry with hopcount 8.
    ASSERT_FALSE(mockRadio.txPacketsSent.empty()) << "Expected packet to be transmitted";
    const std::vector<uint8_t> &packetBuffer = mockRadio.txPacketsSent[0].data;

    size_t offset_txPacket = 0;
    BaseHeader baseHdrTxPacket;
    RREPHeader rrepHdrTxPacket;
    offset_txPacket = deserialiseBaseHeader(packetBuffer.data(), baseHdrTxPacket);
    offset_txPacket = deserialiseRREPHeader(packetBuffer.data(), rrepHdrTxPacket, offset_txPacket);

    EXPECT_EQ(baseHdrTxPacket.packetType, PKT_RREP) << "Packet type should be RREQ";
    EXPECT_EQ(baseHdrTxPacket.destNodeID, 400) << "Base header destNodeID should be 400";
    EXPECT_EQ(baseHdrTxPacket.srcNodeID, myID) << "Source node should match router's ID";
    EXPECT_EQ(baseHdrTxPacket.hopCount, 1) << "Source node should match router's ID";

    // test the rrepHdr
    EXPECT_EQ(rrepHdrTxPacket.originNodeID, 300) << "RREP origin node should be 300";
    EXPECT_EQ(rrepHdrTxPacket.RREPDestNodeID, 5656) << "RREP destination node should be 5656";
    EXPECT_EQ(rrepHdrTxPacket.lifetime, 0) << "RREP lifetime should be 0 - infinite";
    EXPECT_EQ(rrepHdrTxPacket.numHops, 8) << "RREP num hops should be incremented to 8";
    // EXPECT_EQ(true, false) << "Test not implemented";
}

// Ensure that on receiving a RREP that the Data queue is flushed correctly
TEST(AODVRouterTest, ReceiveRREPFlushDataQueue)
{
    MockRadioManager mockRadio;
    uint32_t myID = 100;
    AODVRouter AODVRouter(&mockRadio, myID);

    uint8_t testData[] = {0xDE, 0xAD, 0xBE, 0xEF};
    AODVRouter.sendData(200, testData, sizeof(testData));

    ASSERT_FALSE(mockRadio.txPacketsSent.empty()) << "Expected packet to be transmitted";

    // remove the packet that should have been added as we know this behaviour is correct
    mockRadio.txPacketsSent.erase(mockRadio.txPacketsSent.begin());
    ASSERT_TRUE(mockRadio.txPacketsSent.empty()) << "Expected empty txPacketSent";

    BaseHeader baseHdr;
    baseHdr.destNodeID = myID;
    baseHdr.srcNodeID = 499;
    baseHdr.packetID = 56464645;
    baseHdr.packetType = PKT_RREP;
    baseHdr.flags = 0;
    baseHdr.hopCount = 0;
    baseHdr.reserved = 0;

    RREPHeader rrep;
    rrep.originNodeID = myID;  // node that originally needed the route
    rrep.RREPDestNodeID = 200; // destination of the route
    rrep.lifetime = 0;
    rrep.numHops = 7;

    uint8_t buffer[255];
    size_t offset = 0;

    // Call handle packet
    offset = serialiseBaseHeader(baseHdr, buffer);
    offset = serialiseRREPHeader(rrep, buffer, offset);

    RadioPacket packet;
    std::copy(buffer, buffer + offset, packet.data);
    packet.len = offset;

    AODVRouter.handlePacket(&packet);

    ASSERT_FALSE(mockRadio.txPacketsSent.empty()) << "Expected packet to be transmitted";
    const std::vector<uint8_t> &packetBuffer = mockRadio.txPacketsSent[0].data;

    size_t offset_txPacket = 0;
    BaseHeader baseHdrTxPacket;
    DATAHeader dataTxPacket;
    offset_txPacket = deserialiseBaseHeader(packetBuffer.data(), baseHdrTxPacket);
    offset_txPacket = deserialiseDATAHeader(packetBuffer.data(), dataTxPacket, offset_txPacket);

    EXPECT_EQ(baseHdrTxPacket.packetType, PKT_DATA) << "Packet type should be Data";
    EXPECT_EQ(baseHdrTxPacket.destNodeID, 499) << "Base header destNodeID should be 499";
    EXPECT_EQ(baseHdrTxPacket.srcNodeID, myID) << "Source node should match router's ID";
    EXPECT_EQ(baseHdrTxPacket.hopCount, 0) << "Incorrect number of hops";

    EXPECT_EQ(dataTxPacket.finalDestID, 200) << "Incorrect final destination";

    const size_t headersSize = sizeof(BaseHeader) + sizeof(DATAHeader);
    ASSERT_GE(packetBuffer.size(), headersSize) << "Packet buffer is too short";
    const uint8_t *actualData = packetBuffer.data() + headersSize;
    size_t actualDataLen = packetBuffer.size() - headersSize;

    // Compare the actual payload with testData.
    EXPECT_EQ(actualDataLen, sizeof(testData)) << "Actual data length does not match expected length";
    EXPECT_EQ(memcmp(actualData, testData, sizeof(testData)), 0) << "Transmitted data does not match expected testData";
}

// Ensure that the correct field in the AODVRouter _routeTable is removed
TEST(AODVRouterTest, BasicReceiveRERR)
{
    MockRadioManager mockRadio;
    uint32_t myID = 5738;
    AODVRouter AODVRouter(&mockRadio, myID);
    // need to add a route to node 300 so that the packet get routed
    AODVRouter.updateRoute(300, 400, 7);
    ASSERT_FALSE(AODVRouter._routeTable.empty()) << "Expected a new route to be added";
    EXPECT_EQ(AODVRouter.hasRoute(300), true) << "Route to 300 should have be added";

    BaseHeader baseHdr;
    baseHdr.destNodeID = myID;
    baseHdr.srcNodeID = 200;
    baseHdr.packetID = 56464645;
    baseHdr.packetType = PKT_RERR;
    baseHdr.flags = 0;
    baseHdr.hopCount = 5;
    baseHdr.reserved = 0;

    RERRHeader rerr;
    rerr.reporterNodeID = 400;
    rerr.brokenNodeID = 300;
    rerr.originalDestNodeID = 300;
    rerr.originalPacketID = 555555;
    rerr.senderNodeID = myID;

    uint8_t buffer[255];
    size_t offset = 0;

    // Call handle packet
    offset = serialiseBaseHeader(baseHdr, buffer);
    offset = serialiseRERRHeader(rerr, buffer, offset);

    RadioPacket packet;
    std::copy(buffer, buffer + offset, packet.data);
    packet.len = offset;

    AODVRouter.handlePacket(&packet);
    ASSERT_TRUE(AODVRouter._routeTable.empty()) << "Expected a new route to be added";
    EXPECT_EQ(AODVRouter.hasRoute(300), false) << "Route to 300 should have been removed";
}

// A node on route to destination is broken -> route should still be removed
TEST(AODVRouterTest, ComplicatedReceiveRERR)
{
    MockRadioManager mockRadio;
    uint32_t myID = 5738;
    AODVRouter AODVRouter(&mockRadio, myID);
    // need to add a route to node 300 so that the packet get routed
    AODVRouter.updateRoute(300, 400, 7);
    ASSERT_FALSE(AODVRouter._routeTable.empty()) << "Expected a new route to be added";
    EXPECT_EQ(AODVRouter.hasRoute(300), true) << "Route to 300 should have be added";

    BaseHeader baseHdr;
    baseHdr.destNodeID = myID;
    baseHdr.srcNodeID = 200;
    baseHdr.packetID = 56464645;
    baseHdr.packetType = PKT_RERR;
    baseHdr.flags = 0;
    baseHdr.hopCount = 5;
    baseHdr.reserved = 0;

    RERRHeader rerr;
    rerr.reporterNodeID = 100;
    rerr.brokenNodeID = 400;
    rerr.originalDestNodeID = 300;
    rerr.originalPacketID = 555555;
    rerr.senderNodeID = myID;

    uint8_t buffer[255];
    size_t offset = 0;

    // Call handle packet
    offset = serialiseBaseHeader(baseHdr, buffer);
    offset = serialiseRERRHeader(rerr, buffer, offset);

    RadioPacket packet;
    std::copy(buffer, buffer + offset, packet.data);
    packet.len = offset;

    AODVRouter.handlePacket(&packet);
    ASSERT_TRUE(AODVRouter._routeTable.empty()) << "Expected a new route to be added";
    EXPECT_EQ(AODVRouter.hasRoute(300), false) << "Route to 300 should have been removed";
}

TEST(AODVRouterTest, ForwardRERR)
{
}

// Ensure that the correct response when I am the inteded target of a RREQ
TEST(AODVRouterTest, BasicReceiveRREQ)
{
    MockRadioManager mockRadio;
    uint32_t myID = 5738;
    AODVRouter AODVRouter(&mockRadio, myID);

    BaseHeader baseHdr;
    baseHdr.destNodeID = BROADCAST_ADDR;
    baseHdr.srcNodeID = 200;
    baseHdr.packetID = 56464645;
    baseHdr.packetType = PKT_RREQ;
    baseHdr.flags = 0;
    baseHdr.hopCount = 5;
    baseHdr.reserved = 0;

    RREQHeader rreq;
    rreq.originNodeID = 50;
    rreq.RREQDestNodeID = 5738;

    uint8_t buffer[255];
    size_t offset = 0;

    // Call handle packet
    offset = serialiseBaseHeader(baseHdr, buffer);
    offset = serialiseRREQHeader(rreq, buffer, offset);

    RadioPacket packet;
    std::copy(buffer, buffer + offset, packet.data);
    packet.len = offset;

    AODVRouter.handlePacket(&packet);

    ASSERT_FALSE(mockRadio.txPacketsSent.empty()) << "Expected packet to be transmitted";
    const std::vector<uint8_t> &packetBuffer = mockRadio.txPacketsSent[0].data;

    size_t offset_txPacket = 0;
    BaseHeader baseHdrTxPacket;
    RREPHeader rrepTxPacket;
    offset_txPacket = deserialiseBaseHeader(packetBuffer.data(), baseHdrTxPacket);
    offset_txPacket = deserialiseRREPHeader(packetBuffer.data(), rrepTxPacket, offset_txPacket);

    EXPECT_EQ(baseHdrTxPacket.packetType, PKT_RREP) << "Packet type should be RREP";
    EXPECT_EQ(baseHdrTxPacket.destNodeID, 200) << "Base header destNodeID should be 200";
    EXPECT_EQ(baseHdrTxPacket.srcNodeID, myID) << "Source node should match router's ID";
    EXPECT_EQ(baseHdrTxPacket.hopCount, 0) << "Incorrect number of hops";

    EXPECT_EQ(rrepTxPacket.originNodeID, 50) << "Incorrect origin node";
    EXPECT_EQ(rrepTxPacket.RREPDestNodeID, myID) << "Incorrect rrepDestNodeID";
    EXPECT_EQ(rrepTxPacket.numHops, 0) << "Incorrect initial numHops";
    EXPECT_EQ(rrepTxPacket.lifetime, 0) << "Not using default lifetime";
}

// Ensure that the correct response when I am not the inteded target of a RREQ
TEST(AODVRouterTest, ForwardRREQ)
{
    MockRadioManager mockRadio;
    uint32_t myID = 778;
    AODVRouter AODVRouter(&mockRadio, myID);

    BaseHeader baseHdr;
    baseHdr.destNodeID = BROADCAST_ADDR;
    baseHdr.srcNodeID = 200;
    baseHdr.packetID = 56464645;
    baseHdr.packetType = PKT_RREQ;
    baseHdr.flags = 0;
    baseHdr.hopCount = 5;
    baseHdr.reserved = 0;

    RREQHeader rreq;
    rreq.originNodeID = 50;
    rreq.RREQDestNodeID = 5738;

    uint8_t buffer[255];
    size_t offset = 0;

    // Call handle packet
    offset = serialiseBaseHeader(baseHdr, buffer);
    offset = serialiseRREQHeader(rreq, buffer, offset);

    RadioPacket packet;
    std::copy(buffer, buffer + offset, packet.data);
    packet.len = offset;

    AODVRouter.handlePacket(&packet);

    ASSERT_FALSE(mockRadio.txPacketsSent.empty()) << "Expected packet to be transmitted";
    const std::vector<uint8_t> &packetBuffer = mockRadio.txPacketsSent[0].data;

    size_t offset_txPacket = 0;
    BaseHeader baseHdrTxPacket;
    RREQHeader rreqTxPacket;
    offset_txPacket = deserialiseBaseHeader(packetBuffer.data(), baseHdrTxPacket);
    offset_txPacket = deserialiseRREQHeader(packetBuffer.data(), rreqTxPacket, offset_txPacket);

    EXPECT_EQ(baseHdrTxPacket.packetType, PKT_RREQ) << "Packet type should be RREQ";
    EXPECT_EQ(baseHdrTxPacket.destNodeID, BROADCAST_ADDR) << "Base header destNodeID should be 200";
    EXPECT_EQ(baseHdrTxPacket.srcNodeID, myID) << "Source node should match router's ID";
    EXPECT_EQ(baseHdrTxPacket.hopCount, 6) << "Incorrect number of hops";

    EXPECT_EQ(rreqTxPacket.originNodeID, 50) << "Incorrect origin node";
    EXPECT_EQ(rreqTxPacket.RREQDestNodeID, 5738) << "Incorrect rrepDestNodeID";
}

// Ensure that the correct response when I am not the inteded target of a RREQ but have a route to target
TEST(AODVRouterTest, RespondToRREQ)
{
    MockRadioManager mockRadio;
    uint32_t myID = 60;
    AODVRouter AODVRouter(&mockRadio, myID);
    AODVRouter.updateRoute(5738, 400, 2);
    ASSERT_FALSE(AODVRouter._routeTable.empty()) << "Expected a new route to be added";
    EXPECT_EQ(AODVRouter.hasRoute(5738), true) << "Route to 5738 should have be added";

    BaseHeader baseHdr;
    baseHdr.destNodeID = BROADCAST_ADDR;
    baseHdr.srcNodeID = 200;
    baseHdr.packetID = 56464645;
    baseHdr.packetType = PKT_RREQ;
    baseHdr.flags = 0;
    baseHdr.hopCount = 5;
    baseHdr.reserved = 0;

    RREQHeader rreq;
    rreq.originNodeID = 50;
    rreq.RREQDestNodeID = 5738;

    uint8_t buffer[255];
    size_t offset = 0;

    // Call handle packet
    offset = serialiseBaseHeader(baseHdr, buffer);
    offset = serialiseRREQHeader(rreq, buffer, offset);

    RadioPacket packet;
    std::copy(buffer, buffer + offset, packet.data);
    packet.len = offset;

    AODVRouter.handlePacket(&packet);

    ASSERT_FALSE(mockRadio.txPacketsSent.empty()) << "Expected packet to be transmitted";
    const std::vector<uint8_t> &packetBuffer = mockRadio.txPacketsSent[0].data;

    size_t offset_txPacket = 0;
    BaseHeader baseHdrTxPacket;
    RREPHeader rrepTxPacket;
    offset_txPacket = deserialiseBaseHeader(packetBuffer.data(), baseHdrTxPacket);
    offset_txPacket = deserialiseRREPHeader(packetBuffer.data(), rrepTxPacket, offset_txPacket);

    EXPECT_EQ(baseHdrTxPacket.packetType, PKT_RREP) << "Packet type should be RREP";
    EXPECT_EQ(baseHdrTxPacket.destNodeID, 200) << "Base header destNodeID should be 200";
    EXPECT_EQ(baseHdrTxPacket.srcNodeID, myID) << "Source node should match router's ID";
    EXPECT_EQ(baseHdrTxPacket.hopCount, 0) << "Incorrect number of hops";

    EXPECT_EQ(rrepTxPacket.originNodeID, 50) << "Incorrect origin node";
    EXPECT_EQ(rrepTxPacket.RREPDestNodeID, 5738) << "Incorrect rrepDestNodeID";
    EXPECT_EQ(rrepTxPacket.numHops, 2) << "Incorrect initial numHops";
    EXPECT_EQ(rrepTxPacket.lifetime, 0) << "Not using default lifetime";
}

// Ensure that when I am the receiving node everything works
TEST(AODVRouterTest, handleData)
{
    MockRadioManager mockRadio;
    uint32_t myID = 100;
    AODVRouter AODVRouter(&mockRadio, myID);

    BaseHeader baseHdr;
    baseHdr.destNodeID = BROADCAST_ADDR;
    baseHdr.srcNodeID = 499;
    baseHdr.packetID = 56464645;
    baseHdr.packetType = PKT_DATA;
    baseHdr.flags = 0;
    baseHdr.hopCount = 3;
    baseHdr.reserved = 0;

    DATAHeader dataHdr;
    dataHdr.finalDestID = myID;

    uint8_t buffer[255];
    size_t offset = 0;

    // Call handle packet
    offset = serialiseBaseHeader(baseHdr, buffer);
    offset = serialiseDATAHeader(dataHdr, buffer, offset);

    const char *payloadData = "Test Payload";
    size_t payloadDataLen = strlen(payloadData);
    // Check that the payload will fit in the buffer.
    ASSERT_LE(offset + payloadDataLen, sizeof(buffer)) << "Payload does not fit in buffer";
    memcpy(buffer + offset, payloadData, payloadDataLen);
    offset += payloadDataLen;

    RadioPacket packet;
    std::copy(buffer, buffer + offset, packet.data);
    packet.len = offset;

    AODVRouter.handlePacket(&packet);
}

// Ensure that the Data is forwarded
TEST(AODVRouterTest, forwardData)
{
    MockRadioManager mockRadio;
    uint32_t myID = 100;
    AODVRouter AODVRouter(&mockRadio, myID);
    AODVRouter.updateRoute(5738, 400, 2);
    ASSERT_FALSE(AODVRouter._routeTable.empty()) << "Expected a new route to be added";
    EXPECT_EQ(AODVRouter.hasRoute(5738), true) << "Route to 5738 should have be added";

    BaseHeader baseHdr;
    baseHdr.destNodeID = BROADCAST_ADDR;
    baseHdr.srcNodeID = 499;
    baseHdr.packetID = 56464645;
    baseHdr.packetType = PKT_DATA;
    baseHdr.flags = 0;
    baseHdr.hopCount = 3;
    baseHdr.reserved = 0;

    DATAHeader dataHdr;
    dataHdr.finalDestID = 5738;

    uint8_t buffer[255];
    size_t offset = 0;

    // Call handle packet
    offset = serialiseBaseHeader(baseHdr, buffer);
    offset = serialiseDATAHeader(dataHdr, buffer, offset);

    RadioPacket packet;
    std::copy(buffer, buffer + offset, packet.data);
    packet.len = offset;

    AODVRouter.handlePacket(&packet);

    ASSERT_FALSE(mockRadio.txPacketsSent.empty()) << "Expected packet to be transmitted";
    const std::vector<uint8_t> &packetBuffer = mockRadio.txPacketsSent[0].data;

    size_t offset_txPacket = 0;
    BaseHeader baseHdrTxPacket;
    DATAHeader dataTxPacket;
    offset_txPacket = deserialiseBaseHeader(packetBuffer.data(), baseHdrTxPacket);
    offset_txPacket = deserialiseDATAHeader(packetBuffer.data(), dataTxPacket, offset_txPacket);

    EXPECT_EQ(baseHdrTxPacket.packetType, PKT_DATA) << "Packet type should be DATA";
    EXPECT_EQ(baseHdrTxPacket.destNodeID, 400) << "Base header destNodeID should be 400";
    EXPECT_EQ(baseHdrTxPacket.srcNodeID, baseHdr.srcNodeID ) << "Source node should match original sender";
    EXPECT_EQ(baseHdrTxPacket.hopCount, 4) << "Incorrect number of hops";

    EXPECT_EQ(dataTxPacket.finalDestID, 5738) << "Incorrect final destination";
}

// Ensure that the routeTable is updated when a new route with fewer hops is found
TEST(AODVRouterTest, NewRouteFound)
{
    MockRadioManager mockRadio;
    uint32_t myID = 60;
    AODVRouter AODVRouter(&mockRadio, myID);
    AODVRouter.updateRoute(5738, 400, 4);
    ASSERT_FALSE(AODVRouter._routeTable.empty()) << "Expected a new route to be added";
    EXPECT_EQ(AODVRouter.hasRoute(5738), true) << "Route to 5738 should have be added";

    AODVRouter.updateRoute(5738, 200, 2);
    EXPECT_EQ(AODVRouter.hasRoute(5738), true) << "Route to 5738 should have be added";
    RouteEntry re = AODVRouter.getRoute(5738);

    EXPECT_EQ(re.hopcount, 2) << "Route hopcount should be 2";
    EXPECT_EQ(re.nextHop, 200) << "Route next hop should be 200";
}

TEST(AODVRouterTest, IgnoreNewRoute)
{
    MockRadioManager mockRadio;
    uint32_t myID = 60;
    AODVRouter AODVRouter(&mockRadio, myID);
    AODVRouter.updateRoute(5738, 400, 4);
    ASSERT_FALSE(AODVRouter._routeTable.empty()) << "Expected a new route to be added";
    EXPECT_EQ(AODVRouter.hasRoute(5738), true) << "Route to 5738 should have be added";

    AODVRouter.updateRoute(5738, 200, 8);
    RouteEntry re = AODVRouter.getRoute(5738);

    EXPECT_EQ(re.hopcount, 4) << "Route to 300 should have be added";
    EXPECT_EQ(re.nextHop, 400) << "Route to 300 should have be added";
}

// Ensure node only processes broadcasts and messages intended for me
// Ensure that the RREP is forwarded
TEST(AODVRouterTest, IgnoreMessagesNotAddressedToNode)
{
    MockRadioManager mockRadio;
    uint32_t myID = 100;
    AODVRouter AODVRouter(&mockRadio, myID);

    BaseHeader baseHdr;
    baseHdr.destNodeID = 56;
    baseHdr.srcNodeID = 499;
    baseHdr.packetID = 56464645;
    baseHdr.packetType = PKT_RREP;
    baseHdr.flags = 0;
    baseHdr.hopCount = 0;
    baseHdr.reserved = 0;

    RREPHeader rrep;
    rrep.originNodeID = 56;    // node that originally needed the route
    rrep.RREPDestNodeID = 200; // destination of the route
    rrep.lifetime = 0;
    rrep.numHops = 7;

    uint8_t buffer[255];
    size_t offset = 0;

    // Call handle packet
    offset = serialiseBaseHeader(baseHdr, buffer);
    offset = serialiseRREPHeader(rrep, buffer, offset);

    RadioPacket packet;
    std::copy(buffer, buffer + offset, packet.data);
    packet.len = offset;

    AODVRouter.handlePacket(&packet);
    ASSERT_TRUE(mockRadio.txPacketsSent.empty()) << "Incorrectly handled the packet - should have ignored";
}

TEST(AODVRouterTest, ReadBroadcasts)
{
    MockRadioManager mockRadio;
    uint32_t myID = 100;
    AODVRouter AODVRouter(&mockRadio, myID);

    BaseHeader baseHdr;
    baseHdr.destNodeID = BROADCAST_ADDR;
    baseHdr.srcNodeID = 499;
    baseHdr.packetID = 56464645;
    baseHdr.packetType = PKT_DATA;
    baseHdr.flags = 0;
    baseHdr.hopCount = 0;
    baseHdr.reserved = 0;

    uint8_t buffer[255];
    size_t offset = 0;

    // Call handle packet
    offset = serialiseBaseHeader(baseHdr, buffer);

    RadioPacket packet;
    std::copy(buffer, buffer + offset, packet.data);
    packet.len = offset;

    AODVRouter.handlePacket(&packet);
    ASSERT_TRUE(mockRadio.txPacketsSent.empty()) << "Expected packet to be transmitted";
    // This test behaves as expected but to see internal workings we need to set -vvv
}

// TODO : tests for incorrect packets

//  Receive same packet twice -> should discard based on seen ID
TEST(AODVRouterTest, DiscardSeenPacket)
{
    MockRadioManager mockRadio;
    uint32_t myID = 100;
    AODVRouter AODVRouter(&mockRadio, myID);
    AODVRouter.updateRoute(5738, 400, 2);
    ASSERT_FALSE(AODVRouter._routeTable.empty()) << "Expected a new route to be added";
    EXPECT_EQ(AODVRouter.hasRoute(5738), true) << "Route to 5738 should have be added";

    BaseHeader baseHdr;
    baseHdr.destNodeID = BROADCAST_ADDR;
    baseHdr.srcNodeID = 499;
    baseHdr.packetID = 56464645;
    baseHdr.packetType = PKT_DATA;
    baseHdr.flags = 0;
    baseHdr.hopCount = 3;
    baseHdr.reserved = 0;

    DATAHeader dataHdr;
    dataHdr.finalDestID = 5738;

    uint8_t buffer[255];
    size_t offset = 0;

    // Call handle packet
    offset = serialiseBaseHeader(baseHdr, buffer);
    offset = serialiseDATAHeader(dataHdr, buffer, offset);

    RadioPacket packet;
    std::copy(buffer, buffer + offset, packet.data);
    packet.len = offset;

    AODVRouter.handlePacket(&packet);

    ASSERT_FALSE(mockRadio.txPacketsSent.empty()) << "Expected packet to be transmitted";
    const std::vector<uint8_t> &packetBuffer = mockRadio.txPacketsSent[0].data;

    size_t offset_txPacket = 0;
    BaseHeader baseHdrTxPacket;
    DATAHeader dataTxPacket;
    offset_txPacket = deserialiseBaseHeader(packetBuffer.data(), baseHdrTxPacket);
    offset_txPacket = deserialiseDATAHeader(packetBuffer.data(), dataTxPacket, offset_txPacket);

    EXPECT_EQ(baseHdrTxPacket.packetType, PKT_DATA) << "Packet type should be DATA";
    EXPECT_EQ(baseHdrTxPacket.destNodeID, 400) << "Base header destNodeID should be 400";
    EXPECT_EQ(baseHdrTxPacket.srcNodeID, 499) << "Source node should match router's ID";
    EXPECT_EQ(baseHdrTxPacket.hopCount, 4) << "Incorrect number of hops";

    EXPECT_EQ(dataTxPacket.finalDestID, 5738) << "Incorrect final destination";

    // test seenIDs
    EXPECT_EQ(AODVRouter.seenID(baseHdr.packetID), true) << "ID not added to the set";
    EXPECT_EQ(AODVRouter.seenID((uint32_t)454445354354), false) << "Not seen ID showing true";

    // clear txPacketsSent
    mockRadio.txPacketsSent.erase(mockRadio.txPacketsSent.begin());
    ASSERT_TRUE(mockRadio.txPacketsSent.empty()) << "Expected txPacketsSent to be empty";

    // try re handling the packet
    AODVRouter.handlePacket(&packet);

    ASSERT_TRUE(mockRadio.txPacketsSent.empty()) << "Expected txPacketsSent to be empty";
}

TEST(AODVRouterTest, SendBroadcastInfo)
{
    MockRadioManager mockRadio;
    uint32_t myID = 100;
    AODVRouter AODVRouter(&mockRadio, myID);
    AODVRouter.begin();

    ASSERT_FALSE(mockRadio.txPacketsSent.empty()) << "Expected packet to be transmitted";
    const std::vector<uint8_t> &packetBuffer = mockRadio.txPacketsSent[0].data;

    size_t offset_txPacket = 0;
    BaseHeader baseHdrTxPacket;
    offset_txPacket = deserialiseBaseHeader(packetBuffer.data(), baseHdrTxPacket);


    EXPECT_EQ(baseHdrTxPacket.packetType, PKT_BROADCAST_INFO) << "Packet type should be DATA";
    EXPECT_EQ(baseHdrTxPacket.destNodeID, BROADCAST_ADDR) << "Base header destNodeID should be 400";
    EXPECT_EQ(baseHdrTxPacket.srcNodeID, myID ) << "Source node should match original sender";
    EXPECT_EQ(baseHdrTxPacket.hopCount, 0) << "Incorrect number of hops";
}

TEST(AODVRouterTest, ReceiveBroadcastInfo)
{
    MockRadioManager mockRadio;
    uint32_t myID = 100;
    AODVRouter AODVRouter(&mockRadio, myID);

    //TODO: make sure the packet is also forwarded

}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS())
        ;
    // Always return zero-code and allow PlatformIO to parse results
    return 0;
}