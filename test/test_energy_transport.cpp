// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/energy/Native.h>
#include <WiFiClientSecure.h>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace Hems::Energy;
int main()
{
    JsonDocument doc; NativeInfo info; char const* error;
    auto get=[&] { return providerGet("https://api.energy-charts.info/price",Source::EnergyCharts,doc,info,error); };
    transportBody=R"({"unix_seconds":[1792180800,1792181700],"price":[-10,20],"unit":"EUR / MWh"})";
    assert(get() && caInstalled && redirectPolicy==0 && transportUrl=="https://api.energy-charts.info/price");
    assert(!providerGet("http://api.energy-charts.info/price",Source::EnergyCharts,doc,info,error));
    assert(!providerGet("https://api.energy-charts.info.attacker.test/",Source::EnergyCharts,doc,info,error));
    assert(!providerGet("https://api.forecast.solar/",Source::EnergyCharts,doc,info,error));
    transportCode=-1; assert(!get() && !strcmp(error,"verified_https_failed")); // Includes CA/hostname/transport failures.
    transportCode=302; assert(!get() && redirectPolicy==0); transportCode=500; assert(!get());
    transportCode=429; retryAfter=10000; assert(!get() && info.retrySeconds==10000);
    transportCode=503; retryAfter=0; assert(!get() && info.retrySeconds>=900);
    transportCode=200; transportBody="{broken"; assert(!get());
    transportBody="{} trailing"; assert(!get());
    transportBody="{}"; contentLength=MaxPayload+1; assert(!get());
    contentLength=5; assert(!get()); // Truncated Content-Length must not accept a complete JSON prefix.
    contentLength=MaxPayload; transportBody="{}"+std::string(MaxPayload-2,' '); assert(get());
    contentLength=-1; transportBody="{}"+std::string(MaxPayload,' '); assert(!get());
    transportBody=R"({"unix_seconds":[1792180800,1792181700],"price":[-10,20],"unit":"EUR / MWh"})";
    assert(get()); std::cout<<"Production verified HTTPS transport origin, CA wiring, redirects, failure/recovery, retry and bounded-body tests passed\n";
}
