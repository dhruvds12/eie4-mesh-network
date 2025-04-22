#ifndef USERSESSIONMANAGER_H
#define USERSESSIONMANAGER_H

#include <map>
#include <vector>
#include <cstdint>
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Special value indicating no active BLE connection
static const uint16_t INVALID_HANDLE = 0xFFFF;

struct UserInfo
{
    uint32_t userID;
    uint16_t bleConnHandle; // INVALID_HANDLE if not connected
    bool isConnected;
    unsigned long lastSeen; // millis() timestamp of last activity
};

class UserSessionManager
{
public:
    UserSessionManager();
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

private:
    // Reader-writer lock implementation
    mutable SemaphoreHandle_t _readCountMutex;
    mutable SemaphoreHandle_t _writeMutex;
    mutable int _readCount;

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
};

#endif
