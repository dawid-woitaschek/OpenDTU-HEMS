// SPDX-License-Identifier: GPL-2.0-or-later
#include <powermeter/Provider.h>
#include <MqttSettings.h>

namespace PowerMeters {

bool Provider::isDataValid() const
{
    return getLastUpdate() > 0 && ((millis() - getLastUpdate()) < (30 * 1000));
}

std::optional<float> Provider::getPowerTotalOptional() const
{
    auto total = _dataCurrent.get<DataPointLabel::PowerTotal>();
    if (total) { return total; }

    auto phase1 = _dataCurrent.get<DataPointLabel::PowerL1>();
    auto phase2 = _dataCurrent.get<DataPointLabel::PowerL2>();
    auto phase3 = _dataCurrent.get<DataPointLabel::PowerL3>();
    if (!phase1 && !phase2 && !phase3) { return std::nullopt; }

    return phase1.value_or(0.0f) + phase2.value_or(0.0f) + phase3.value_or(0.0f);
}

float Provider::getPowerTotal() const
{
    return getPowerTotalOptional().value_or(0.0f);
}

void Provider::mqttLoop() const
{
    if (!MqttSettings.getConnected()) { return; }

    if (!isDataValid()) { return; }

    auto constexpr halfOfAllMillis = std::numeric_limits<uint32_t>::max() / 2;
    if ((getLastUpdate() - _lastMqttPublish) > halfOfAllMillis) { return; }

    // based on getPowerTotal() as we can not be sure that the PowerTotal value is set for all providers
    MqttSettings.publish("powermeter/powertotal", String(getPowerTotal()));

#define PUB(l, t) \
    { \
        auto oDataPoint = _dataCurrent.get<DataPointLabel::l>(); \
        if (oDataPoint) { MqttSettings.publish("powermeter/" t, String(*oDataPoint)); } \
    }

    PUB(PowerL1, "power1");
    PUB(PowerL2, "power2");
    PUB(PowerL3, "power3");
    PUB(VoltageL1, "voltage1");
    PUB(VoltageL2, "voltage2");
    PUB(VoltageL3, "voltage3");
    PUB(CurrentL1, "current1");
    PUB(CurrentL2, "current2");
    PUB(CurrentL3, "current3");
    PUB(Import, "import");
    PUB(Export, "export");
#undef PUB

    _lastMqttPublish = millis();
}

} // namespace PowerMeters
