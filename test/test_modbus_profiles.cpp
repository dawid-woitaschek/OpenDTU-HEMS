// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/modbus/Protocol.h>
#include <cassert>
#include <cstring>
#include <initializer_list>
using namespace Hems::Modbus;
int main() {
    uint8_t frame[MaxFrame] {}, payload[32] {};
    assert(makeReadRequest(frame, 0x1234, 1, ReadFunction::HoldingRegisters, 0xfffe, 2));
    uint8_t expected[] { 0x12, 0x34, 0, 0, 0, 6, 1, 3, 0xff, 0xfe, 0, 2 };
    assert(!memcmp(frame, expected, sizeof(expected)));
    assert(!makeReadRequest(frame, 1, 1, ReadFunction::HoldingRegisters, 65535, 2));
    assert(!makeReadRequest(frame, 1, 1, ReadFunction::HoldingRegisters, 0, 17));
    assert(!makeReadRequest(frame, 1, 1, ReadFunction::HoldingRegisters, 0, 0));
    for (uint8_t code : { 1, 5, 6, 15, 16, 255 }) {
        assert(!makeReadRequest(frame, 1, 1, static_cast<ReadFunction>(code), 0, 1));
    }
    uint8_t good[] { 0x12, 0x34, 0, 0, 0, 7, 1, 3, 4, 0, 1, 0, 2 };
    assert(parseReadResponse(good, sizeof(good), 0x1234, 1, ReadFunction::HoldingRegisters, 2, payload));
    assert(payload[1] == 1 && payload[3] == 2);
    assert(!parseReadResponse(good, sizeof(good) - 1, 0x1234, 1, ReadFunction::HoldingRegisters, 2, payload));
    for (size_t i : { 0, 2, 4, 5, 6, 7, 8 }) {
        ++good[i]; assert(!parseReadResponse(good, sizeof(good), 0x1234, 1, ReadFunction::HoldingRegisters, 2, payload)); --good[i];
    }
    uint8_t exception[] { 0x12, 0x34, 0, 0, 0, 3, 1, 0x83, 2 };
    auto r = parseReadResponse(exception, sizeof(exception), 0x1234, 1, ReadFunction::HoldingRegisters, 2, payload);
    assert(r.error == Error::Exception && r.exception == 2);
    exception[8] = 0; assert(!parseReadResponse(exception, sizeof(exception), 0x1234, 1, ReadFunction::HoldingRegisters, 2, payload));

    size_t writeSize = 0;
    uint16_t one[] { 0xabcd };
    assert(makeWriteHoldingRequest(frame, writeSize, 0x1234, 7, 0x0102, one, 1));
    assert(writeSize == 12);
    uint8_t expectedSingle[] { 0x12, 0x34, 0, 0, 0, 6, 7, 6, 1, 2, 0xab, 0xcd };
    assert(!memcmp(frame, expectedSingle, sizeof(expectedSingle)));
    assert(parseWriteHoldingResponse(expectedSingle, sizeof(expectedSingle), 0x1234, 7, 0x0102, one, 1));
    expectedSingle[11] ^= 1;
    assert(!parseWriteHoldingResponse(expectedSingle, sizeof(expectedSingle), 0x1234, 7, 0x0102, one, 1));
    expectedSingle[11] ^= 1;

    uint16_t two[] { 0x1122, 0x3344 };
    assert(makeWriteHoldingRequest(frame, writeSize, 0x4567, 1, 1002, two, 2));
    assert(writeSize == 17);
    uint8_t expectedMultiple[] {
        0x45, 0x67, 0, 0, 0, 11, 1, 16, 0x03, 0xea, 0, 2, 4, 0x11, 0x22, 0x33, 0x44
    };
    assert(!memcmp(frame, expectedMultiple, sizeof(expectedMultiple)));
    uint8_t multipleResponse[] { 0x45, 0x67, 0, 0, 0, 6, 1, 16, 0x03, 0xea, 0, 2 };
    assert(parseWriteHoldingResponse(multipleResponse, sizeof(multipleResponse), 0x4567, 1, 1002, two, 2));

    uint8_t writeException[] { 0x45, 0x67, 0, 0, 0, 3, 1, 0x90, 3 };
    r = parseWriteHoldingResponse(writeException, sizeof(writeException), 0x4567, 1, 1002, two, 2);
    assert(r.error == Error::Exception && r.exception == 3 && r.responded);

    assert(!makeWriteHoldingRequest(frame, writeSize, 1, 1, 0, two, 0));
    assert(!makeWriteHoldingRequest(frame, writeSize, 1, 1, 0, two, 3));
    assert(!makeWriteHoldingRequest(frame, writeSize, 1, 1, 65535, two, 2));
    assert(!makeWriteHoldingRequest(frame, writeSize, 1, 1, 0, nullptr, 1));
}
