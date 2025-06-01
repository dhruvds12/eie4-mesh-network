#ifndef MOCK_CLIENT_NOTIFIER_H
#define MOCK_CLIENT_NOTIFIER_H

#include "IClientNotifier.h"
#include <vector>
#include <cstdint>
#include <cstring>

/*  A very small fake that simply remembers every call so
    the unit-tests can inspect what the router tried to send.      */
class MockClientNotifier : public IClientNotifier
{
public:
    struct Rec {
        Outgoing msg;
        /* make a deep-copy of the payload so it stays valid */
        std::vector<uint8_t> copy;
        explicit Rec(const Outgoing& o)
            : msg(o) {
            if (o.data && o.length) {
                copy.assign(o.data, o.data + o.length);
                msg.data   = copy.data();
                msg.length = copy.size();
            }
        }
    };

    std::vector<Rec> log;       /* every notify() call ends up here */
    bool gwOnline = false;      /* last state passed to setGatewayState */

    /* ---- IClientNotifier ----------------------------------- */
    bool notify(const Outgoing& o) override
    {
        log.emplace_back(o);
        return true;            /* always “succeeds” */
    }

    bool setGatewayState(bool online) override
    {
        gwOnline = online;
        return true;
    }
};

#endif /* MOCK_CLIENT_NOTIFIER_H */
