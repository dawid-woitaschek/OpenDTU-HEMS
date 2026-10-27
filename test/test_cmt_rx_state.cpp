// SPDX-License-Identifier: GPL-2.0-or-later
#include "cmt2300wrapper.h"
#include "cmt2300a.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

namespace {
uint8_t flags = 0;
unsigned reads = 0;
unsigned clears = 0;
}

extern "C" uint8_t CMT2300A_ReadReg(const uint8_t addr)
{
    assert(addr == CMT2300A_CUS_INT_FLAG);
    ++reads;
    return flags;
}

extern "C" void CMT2300A_WriteReg(const uint8_t addr, const uint8_t value)
{
    // Preamble must be consumed so a stale flag cannot pin the RX channel.
    // Sync/CRC/packet flags belong to the receive path and must survive here.
    assert(addr == CMT2300A_CUS_INT_CLR2);
    assert(value == CMT2300A_MASK_PREAM_OK_CLR);
    flags &= ~CMT2300A_MASK_PREAM_OK_FLG;
    ++clears;
}

int main()
{
    CMT2300A radio(0, 0, 0, 0);
    assert(radio.getRxState() == CmtRxState::Idle);
    flags = CMT2300A_MASK_LBD_FLG | CMT2300A_MASK_COL_ERR_FLG | CMT2300A_MASK_PKT_ERR_FLG;
    assert(radio.getRxState() == CmtRxState::Idle);
    assert(clears == 0);

    flags = CMT2300A_MASK_PREAM_OK_FLG;
    assert(radio.available());
    assert(clears == 0); // Existing availability check remains non-destructive.
    const unsigned before = reads;
    assert(radio.getRxState() == CmtRxState::Preamble);
    assert(reads == before + 1);
    assert(clears == 1);
    assert(radio.getRxState() == CmtRxState::Idle);
    assert(!radio.available());

    for (const uint8_t received : { CMT2300A_MASK_SYNC_OK_FLG,
             CMT2300A_MASK_CRC_OK_FLG, CMT2300A_MASK_PKT_OK_FLG }) {
        flags = received | CMT2300A_MASK_PREAM_OK_FLG;
        assert(radio.getRxState() == CmtRxState::Sync);
        assert(flags == (received | CMT2300A_MASK_PREAM_OK_FLG));
        assert(clears == 1); // Stronger RX evidence takes priority over preamble.
        assert(radio.available());
    }
    std::puts("CMT receive-state flag tests passed");
}
