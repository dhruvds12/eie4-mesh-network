#include "userSessionManager.h"
#include "mqttmanager.h"

UserSessionManager::UserSessionManager(MQTTManager *mqttManager)
    : _readCount(0), _mqttManager(mqttManager)
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
        Serial.printf("Welcome back %u\n", userID);
        it->second.bleConnHandle = bleHandle;
        it->second.isConnected = true;
        it->second.lastSeen = now;
    }
    else
    {
        Serial.printf("Added new user %u\n", userID);
        UserInfo info{userID, bleHandle, true, now};
        _users.emplace(userID, info);
        _diffRemoved.erase(userID);
        _diffAdded.insert(userID);
    }
    writeUnlock();
    if (_mqttManager != nullptr)
    {
        _mqttManager->publishUserAdded(userID);
    }
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
    Serial.println("Disconnecting client registered with session manager");
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

std::vector<uint32_t> UserSessionManager::getConnectedUsers()
{
    readLock();
    std::vector<uint32_t> list;
    list.reserve(_users.size());
    for (auto const &kv : _users)
    {
        list.push_back(kv.second.userID);
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

void UserSessionManager::queueOffline(uint32_t userID,
                                      const OfflineMsg &m)
{
    writeLock();
    auto it = _users.find(userID);
    if (it == _users.end())
    {
        /* user completely unknown – create a stub entry */
        _users.emplace(userID,
                       UserInfo{userID, INVALID_HANDLE, false, millis(), {}});
        it = _users.find(userID);
    }
    auto &box = it->second.inbox;
    if (box.size() == 10) // keep only the 10 newest
        box.pop_front();
    box.push_back(m);
    writeUnlock();
}

bool UserSessionManager::popInbox(uint32_t userID, std::vector<OfflineMsg> &out)
{
    writeLock();
    auto it = _users.find(userID);
    if (it == _users.end() || it->second.inbox.empty())
    {
        writeUnlock();
        return false;
    }
    out.assign(it->second.inbox.begin(), it->second.inbox.end());
    it->second.inbox.clear();
    writeUnlock();
    return true;
}

void UserSessionManager::setOffline(uint32_t userID)
{
    writeLock();
    _users[userID].isConnected = false;
    writeUnlock();
}

void UserSessionManager::setOnline(uint32_t userID)
{
    writeLock();
    _users[userID].isConnected = true;
    writeUnlock();
}