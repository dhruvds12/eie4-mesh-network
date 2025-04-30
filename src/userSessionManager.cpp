#include "userSessionManager.h"

UserSessionManager::UserSessionManager(MQTTManager *MQTTManager)
    : _readCount(0), _mqttManager(MQTTManager)
{
    _readCountMutex = xSemaphoreCreateMutex();
    _writeMutex = xSemaphoreCreateMutex();
}

UserSessionManager::~UserSessionManager()
{
    if (_readCountMutex)
        vSemaphoreDelete(_readCountMutex);
    if (_writeMutex)
        vSemaphoreDelete(_writeMutex);
}

void UserSessionManager::addOrRefresh(uint32_t userID, uint16_t bleHandle)
{
    writeLock();
    unsigned long now = millis();
    auto it = _users.find(userID);
    if (it != _users.end())
    {
        it->second.bleConnHandle = bleHandle;
        it->second.isConnected = true;
        it->second.lastSeen = now;
    }
    else
    {
        UserInfo info{userID, bleHandle, true, now};
        _users.emplace(userID, info);
        _diffRemoved.erase(userID);
        _diffAdded.insert(userID);
    }
    writeUnlock();
    if(_mqttManager != nullptr) {
        _mqttManager->publishUserAdded(userID);
    }
    Serial.printf("Added new user %u\n", userID);
}

void UserSessionManager::remove(uint32_t userID)
{
    writeLock();
    _users.erase(userID);
    _diffAdded.erase(userID);
    _diffRemoved.insert(userID);

    writeUnlock();
}

void UserSessionManager::handleBleDisconnect(uint16_t bleHandle)
{
    writeLock();
    unsigned long now = millis();
    for (auto &kv : _users)
    {
        if (kv.second.bleConnHandle == bleHandle)
        {
            kv.second.isConnected = false;
            kv.second.bleConnHandle = INVALID_HANDLE;
            kv.second.lastSeen = now;
            break;
        }
    }
    writeUnlock();
}

bool UserSessionManager::knowsUser(uint32_t userID) const
{
    readLock();
    bool found = _users.find(userID) != _users.end();
    readUnlock();
    return found;
}

bool UserSessionManager::isOnline(uint32_t userID) const
{
    readLock();
    auto it = _users.find(userID);
    bool online = (it != _users.end()) && it->second.isConnected;
    readUnlock();
    return online;
}

uint16_t UserSessionManager::getBleHandle(uint32_t userID) const
{
    readLock();
    auto it = _users.find(userID);
    uint16_t handle = (it != _users.end()) ? it->second.bleConnHandle : INVALID_HANDLE;
    readUnlock();
    return handle;
}

std::vector<UserInfo> UserSessionManager::allUsers() const
{
    readLock();
    std::vector<UserInfo> list;
    list.reserve(_users.size());
    for (auto const &kv : _users)
    {
        list.push_back(kv.second);
    }
    readUnlock();
    return list;
}

void UserSessionManager::getAndClearDiff(std::vector<uint32_t> &added,
                                         std::vector<uint32_t> &removed)
{
    writeLock();
    // move out current diffs
    added.assign(_diffAdded.begin(), _diffAdded.end());
    removed.assign(_diffRemoved.begin(), _diffRemoved.end());

    // clear for next round
    _diffAdded.clear();
    _diffRemoved.clear();
    writeUnlock();
}