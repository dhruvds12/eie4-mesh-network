#include "LoRaManager.h"


LoRaManager::LoRaManager() : transmitFlag(false), radio(new Module(8, 14, 12, 13))
{
}
