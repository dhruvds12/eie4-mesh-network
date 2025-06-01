#ifndef USERSESSIONMANAGER_H          //  ← keep original guard
#define USERSESSIONMANAGER_H

#include <vector>
#include <deque>
#include <cstdint>
#include <set>
#include <IClientNotifier.h>
/* Forward declarations to avoid including heavy headers */
class MQTTManager;

/* The BLE side of the code uses this enum through IClientNotifier.
   Declare only the values the router refers to.                    */
// enum class BleType : uint8_t
// {
//     BLE_UnicastUser,
//     BLE_ENC_UnicastUser,
//     BLE_USER_GATEWAY,
//     BLE_Broadcast,
//     BLE_Node,
//     BLE_PUBKEY_RESP
// };

/* ---------------- mirror the data structures AODVRouter needs -------------- */

static const uint16_t INVALID_HANDLE = 0xFFFF;

struct OfflineMsg
{
    BleType                type;
    uint32_t               to;
    uint32_t               from;
    std::vector<uint8_t>   data;
};

struct UserInfo
{
    uint32_t               userID;
    uint16_t               bleConnHandle;          // INVALID_HANDLE if offline
    bool                   isConnected;
    unsigned long          lastSeen;
    std::deque<OfflineMsg> inbox;
};

/* -------------------------- the lightweight stub --------------------------- */
class UserSessionManager
{
public:
    explicit UserSessionManager(MQTTManager* = nullptr) {}

    /* APIs that AODVRouter calls ------------------------------------------- */
    void   addOrRefresh(uint32_t /*userID*/, uint16_t /*bleHandle*/) {}
    void   remove(uint32_t /*userID*/) {}
    void   handleBleDisconnect(uint16_t /*bleHandle*/) {}

    bool   knowsUser(uint32_t /*userID*/)           const { return true; }  // pretend we know everyone
    bool   isOnline  (uint32_t /*userID*/)          const { return true; }  // …and they’re all online
    uint16_t getBleHandle(uint32_t /*userID*/)      const { return INVALID_HANDLE; }

    std::vector<UserInfo>      allUsers()           const { return {}; }
    std::vector<uint32_t>      getConnectedUsers()        { return {}; }

    void getAndClearDiff(std::vector<uint32_t>& added,
                         std::vector<uint32_t>& removed)
    {
        /* nothing happened since last call */
        added.clear();
        removed.clear();
    }

    void setMQTTManager(MQTTManager* /*mqttMgr*/) {}

    void queueOffline(uint32_t /*userID*/, const OfflineMsg& /*m*/) {}

    bool popInbox(uint32_t /*userID*/, std::vector<OfflineMsg>& out)
    {
        out.clear();
        return false;          // nothing to deliver
    }
};

#endif   // USERSESSIONMANAGER_H
