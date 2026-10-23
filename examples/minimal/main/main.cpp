#include "grok_bot.hpp"
extern "C" void app_main() {
    // Compile/link probe: no credentials, Wi-Fi setup or cloud calls.
    grok::EspTransport transport;
    grok::Client client(transport);
    auto flow = grok::create_login();
    (void)client; (void)flow;
}
