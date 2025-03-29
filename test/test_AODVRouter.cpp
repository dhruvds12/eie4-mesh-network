#include <gtest/gtest.h>
#include "AODVRouter.h"
#include "MockRadioManager.h"

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

    ASSERT_FALSE(mockRadio.txPacketsSent.empty()) << "Expected a packet to be transmitted!";

    // Need to check that the packet type is a RREQ
    
    // Check the element that has been stored on the txPacketSent vector
    const std::vector<uint8_t>& packetBuffer = mockRadio.txPacketsSent[0].data;

    BaseHeader baseHdr;
    deserialiseBaseHeader(packetBuffer.data(), baseHdr);
    EXPECT_EQ(baseHdr.packetType, PKT_RREQ) << "Packet type should be RREQ";
    EXPECT_EQ(baseHdr.destNodeID, BROADCAST_ADDR) << "Base header destNodeID should be broadcast";
    EXPECT_EQ(baseHdr.srcNodeID, myID) << "Source node should match router's ID";

    size_t baseHeaderSize = sizeof(BaseHeader);
    RREQHeader rreqHdr;
    deserialiseRREQHeader(const_cast<uint8_t*>(packetBuffer.data()), rreqHdr, baseHeaderSize);
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
    rrep.originNodeID = myID; // node that originally needed the route
    rrep.RREPDestNodeID = 5656; // destination of the route
    rrep.lifetime = 0;
    rrep.numHops = 7;

    uint8_t buffer[255];
    size_t offset = 0;

    offset += serialiseBaseHeader(baseHdr, buffer + offset);
    offset += serialiseRREPHeader(rrep, buffer, offset);

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

    //Check RouteEntry for the node delivering the RREP
    RouteEntry ExpectedReDeliveringNode;
    ExpectedReDeliveringNode.nextHop = 200;
    ExpectedReDeliveringNode.hopcount = 1;

    RouteEntry actualReDeliveringNode;
    actualReDeliveringNode = AODVRouter.getRoute(200);

    EXPECT_EQ(actualReDeliveringNode.nextHop, ExpectedReDeliveringNode.nextHop) << "Incorrect next hop";
    EXPECT_EQ(actualReDeliveringNode.hopcount, ExpectedReDeliveringNode.hopcount) << "Incorrect hop count";
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS())
        ;
    // Always return zero-code and allow PlatformIO to parse results
    return 0;
}