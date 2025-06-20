#ifndef IROUTER_H
#define IROUTER_H

#include <cstdint>
#include <cstdlib>
#include <vector>
#include <array>

// Basic interface that will allow swapping between routers if needed
// TODO: Expand the interface as required
class IRouter
{
public:
    virtual ~IRouter() {}

    virtual bool begin() = 0;
    virtual void sendData(uint32_t destNodeID, const uint8_t *data, size_t len, uint32_t packetId, uint8_t flags = 0) = 0;
    virtual void sendUserMessage(uint32_t fromUserID, uint32_t toUserID, const uint8_t *data, size_t len, uint32_t packetId, uint8_t flags = 0) = 0;
    virtual void sendPubKeyReq(uint32_t targetUserID, uint32_t senderUserID) = 0;
    virtual std::vector<uint32_t> getKnownNodeIDs() const = 0;
    virtual std::vector<uint32_t> getKnownUserIDs() const = 0;
    virtual bool haveGateway() const = 0;
    virtual void addPubKey(uint32_t userID, std::array<uint8_t, 32>) = 0;
    virtual void sendMoveUserReq(uint32_t userID, uint32_t oldNodeID) = 0;
};

#endif