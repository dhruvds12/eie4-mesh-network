#ifndef USERSESSIONMANAGER_H
#define USERSESSIONMANAGER_H

#include <map>
#include <vector>
#include <cstdint>
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "set"

class MQTTManager;

// Special value indicating no active BLE connection
static const uint16_t INVALID_HANDLE = 0xFFFF;

struct UserInfo
{
    uint32_t userID;
    uint16_t bleConnHandle; // INVALID_HANDLE if not connected
    bool isConnected;
    unsigned long lastSeen; // millis() timestamp of last activity
};

/**
 * @brief The UserSessionManager is used to manage sessions for users connected via bluetooth. Read preferring reader-writer mutex
 *
 * IMPORTANT!!!!
 *
 * The USM uses a reader-writer lock (read preferring) built from two binary (non-recursive) mutexes
 * Therefore, any task that holds the _writeMutex then entered any of the USM API functions without releasing
 * the _writeMutex will DEADLOCK.
 *
 * Example:
 * void someFunc{
 *   _usm.addOrRefresh(...);// <- takes write-lock 
 *   _usm.isOnline(...); // <- would try and take the write-lock again as part of a read
 * }
 * 
 * So if you’re still holding _writeMutex from the preceding addOrRefresh()’s writeLock(), then readLock() dead-locks when it tries to take _writeMutex again.
 */
class UserSessionManager
{
public:
    UserSessionManager(MQTTManager *mqttManager);
    ~UserSessionManager();
    // Register or refresh a user's session on BLE connect/auth
    // Could block on mutex!! - need to handle this
    void addOrRefresh(uint32_t userID, uint16_t bleHandle);

    // Remove a user completely (explicit disconnect command)
    // Could block on mutex!! - need to handle this
    void remove(uint32_t userID);

    // Handle unexpected BLE disconnect events
    void handleBleDisconnect(uint16_t bleHandle);

    // Query whether we've ever seen this user
    bool knowsUser(uint32_t userID) const;

    // Query whether the user is currently online
    bool isOnline(uint32_t userID) const;

    // Get the BLE handle for an online user (or INVALID_HANDLE)
    uint16_t getBleHandle(uint32_t userID) const;

    // Retrieve a snapshot of all known users
    std::vector<UserInfo> allUsers() const;

    /// Call this to grab everything that’s been added/removed since
    /// the last time you called getAndClearDiff().
    void getAndClearDiff(std::vector<uint32_t> &added, std::vector<uint32_t> &removed);

    void setMQTTManager(MQTTManager *mqttMgr) { _mqttManager = mqttMgr; }

private:
    // Reader-writer lock implementation
    mutable SemaphoreHandle_t _readCountMutex;
    mutable SemaphoreHandle_t _writeMutex;
    mutable int _readCount;

    std::set<uint32_t> _diffAdded;
    std::set<uint32_t> _diffRemoved;

    MQTTManager *_mqttManager;

    void readLock() const
    {
        // Protect readCount
        xSemaphoreTake(_readCountMutex, portMAX_DELAY);
        if (++_readCount == 1)
        {
            // First reader locks writer
            xSemaphoreTake(_writeMutex, portMAX_DELAY);
        }
        xSemaphoreGive(_readCountMutex);
    }

    void readUnlock() const
    {
        xSemaphoreTake(_readCountMutex, portMAX_DELAY);
        if (--_readCount == 0)
        {
            // Last reader releases writer
            xSemaphoreGive(_writeMutex);
        }
        xSemaphoreGive(_readCountMutex);
    }

    void writeLock() const
    {
        xSemaphoreTake(_writeMutex, portMAX_DELAY);
    }

    void writeUnlock() const
    {
        xSemaphoreGive(_writeMutex);
    }

    std::map<uint32_t, UserInfo> _users;

    std::map<uint16_t, uint32_t> _connHandleUserMap;
};

#endif
