// #pragma once
// #include <stdint.h>
// #include <unordered_map>

// /*––––––––––––––––  Mesh-wide AES-128 key burnt into flash  –––––––––––––––*/
// static const uint8_t K_NET[16] = {
//     0x32,0x86,0xA1,0xF1,0x48,0x5B,0x19,0xEF,
//     0xF2,0xCC,0x40,0xA4,0xB5,0x90,0x7C,0x11
// };

// /*------------------------------------------------------------------------
//  *  Singleton that:
//  *      – keeps the monotonic 64-bit sender counter
//  *      – tracks the highest counter seen per peer (anti-replay)
//  *------------------------------------------------------------------------*/
// class KeyStore {
// public:
//     /* returns next counter and persists it lazily                       */
//     uint64_t nextTxCtr();                          // thread-safe from ISR

//     uint64_t lastRxCtr(uint32_t node) const;       // 0 if never seen
//     void     updateRxCtr(uint32_t node, uint64_t ctr);

//     static KeyStore& instance();

// private:
//     KeyStore();
//     void flush();                                  // write _txCtr to NVS

//     uint64_t _txCtr    = 0;
//     uint32_t _dirtyCnt = 0;                         // defer flash writes
//     std::unordered_map<uint32_t,uint64_t> _rxCtr;   // per peer
// };
