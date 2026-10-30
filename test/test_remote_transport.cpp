// SPDX-License-Identifier: GPL-2.0-or-later
#include <remote/Service.h>
#include <RuntimeDiagnostics.h>
#include <WiFiClientSecure.h>
#include <cassert>
#include <iostream>
namespace Diagnostics { void workerPhase(Phase, int8_t) {} void heapCheckpoint(HeapPoint, HeapProvider, int8_t) {} }
int main() {
    Remote::Settings settings; strcpy(settings.endpoint, "https://collector.example.org:8443/api/v1/telemetry"); strcpy(settings.token, "hems_secret");
    Remote::Transmission flight; flight.stage(42, 0);
    int code; char const* error;
    auto run = [&](std::string body, bool length = true, int status = 200) {
        responseBytes = "HTTP/1.0 " + std::to_string(status) + " Result\r\n";
        if (length) responseBytes += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        responseBytes += "\r\n" + body;
        bool ok = Remote::post(settings, "{\"schema_version\":1}", 20, flight, code, error);
        assert(activeClients == 0); return ok;
    };
    std::string ack = "{\"accepted\":true,\"accepted_checkpoint_sequence\":42}";
    assert(run(ack) && code == 200 && !error);
    assert(verifiedRoots && verifiedHost == "collector.example.org");
    assert(requestBytes.find("POST /api/v1/telemetry HTTP/1.0\r\n") == 0);
    assert(requestBytes.find("Host: collector.example.org:8443\r\n") != std::string::npos);
    assert(requestBytes.find("Authorization: Bearer hems_secret\r\n") != std::string::npos);
    assert(run("{\"accepted\":true,\"duplicate\":true,\"accepted_checkpoint_sequence\":42}"));
    assert(run(ack, false));
    for (auto invalid : {"{}", "{\"accepted\":true}", "{\"accepted\":true,\"accepted_checkpoint_sequence\":43}", "{\"accepted\":true,\"accepted_checkpoint_sequence\":41}", "{\"accepted\":true,\"accepted_checkpoint_sequence\":\"42\"}", "bad", "[]"}) assert(!run(invalid));
    assert(!run(ack + "{}") && !run(ack + "x"));
    assert(!run(std::string(1025, ' ')));
    assert(!run(ack, true, 302) && code == 302);
    assert(!run(ack, true, 503) && code == 503);
    dnsWorks = false; assert(!run(ack) && std::string(error) == "dns_failed"); dnsWorks = true;
    tlsResult = -100; assert(!run(ack) && std::string(error) == "tls_failed"); tlsResult = 0;
    writeWorks = false; assert(!run(ack)); writeWorks = true;
    stallBody = true; assert(!run("", false)); stallBody = false;
    failClientAllocation = true; assert(!run(ack) && std::string(error) == "insufficient_memory"); failClientAllocation = false;
    responseBytes = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n";
    assert(!Remote::post(settings, "{}", 2, flight, code, error));
    responseBytes = "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n{}";
    assert(!Remote::post(settings, "{}", 2, flight, code, error));
    strcpy(settings.endpoint, "http://collector.example.org/a"); assert(!run(ack));
    std::cout << "remote verified HTTPS/HTTP bounds/acknowledgements passed\n";
}
