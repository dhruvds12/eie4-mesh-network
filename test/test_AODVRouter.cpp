#include <gtest/gtest.h>
#include "AODVRouter.h"
#include "MockRadioManager.h"

TEST(AODVRouterTest, BasicTest)
{
    MockRadioManager mockRadio;
    uint32_t myID = 100;
    AODVRouter AODVRouter(&mockRadio, myID);

    uint8_t testData[] = {0xDE, 0xAD, 0xBE, 0xEF};
    AODVRouter.sendData(200, testData, sizeof(testData));

    ASSERT_FALSE(mockRadio.txPacketsSent.empty()) << "Expected a packet to be transmitted!";
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS())
        ;
    // Always return zero-code and allow PlatformIO to parse results
    return 0;
}