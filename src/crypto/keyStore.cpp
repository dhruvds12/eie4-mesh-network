#include "KeyStore.h"
#include <Preferences.h>

static const char* NVS_NS   = "crypto";
static const char* NVS_CTR  = "ctr";
static const uint32_t FLUSH_EVERY = 256;   // write after N packets

/*––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––*/
KeyStore::KeyStore()
{
    Preferences p;
    p.begin(NVS_NS, /*read-only=*/true);
    _txCtr = p.getULong64(NVS_CTR, 0ULL);
    p.end();
}

KeyStore& KeyStore::instance()
{
    static KeyStore inst;
    return inst;
}

void KeyStore::flush()
{
    Preferences p;
    p.begin(NVS_NS, /*rw=*/false);
    p.putULong64(NVS_CTR, _txCtr);
    p.end();
}

uint64_t KeyStore::nextTxCtr()
{
    ++_txCtr;
    if (++_dirtyCnt >= FLUSH_EVERY) {
        _dirtyCnt = 0;
        flush();                       // amortised flash wear
    }
    return _txCtr;
}

uint64_t KeyStore::lastRxCtr(uint32_t node) const
{
    auto it = _rxCtr.find(node);
    return (it == _rxCtr.end()) ? 0ULL : it->second;
}

void KeyStore::updateRxCtr(uint32_t node, uint64_t ctr)
{
    _rxCtr[node] = ctr;
}
