// SPDX-License-Identifier: GPL-2.0-or-later
#include "FirmwareUpdateValidation.h"
#include <cassert>
#include <iostream>

int main()
{
    using FirmwareUpdateValidation::isFactoryImageFilename;

    assert(isFactoryImageFilename("OpenDTU.factory.bin"));
    assert(isFactoryImageFilename("OpenDTU.factory.bin.gz"));
    assert(isFactoryImageFilename("OPENDTU.FACTORY.BIN"));
    assert(isFactoryImageFilename("OpenDTU.FaCtOrY.BiN.Gz"));

    assert(!isFactoryImageFilename("OpenDTU.bin"));
    assert(!isFactoryImageFilename("OpenDTU.bin.gz"));
    assert(!isFactoryImageFilename("OpenDTU.factory.bin.backup"));
    assert(!isFactoryImageFilename("factory.bin"));
    assert(!isFactoryImageFilename("firmware"));
    assert(!isFactoryImageFilename(""));
    assert(!isFactoryImageFilename(nullptr));

    std::cout << "Firmware update filename validation tests passed\n";
}
