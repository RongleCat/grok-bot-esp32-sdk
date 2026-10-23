#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace grok {
using Headers = std::vector<std::pair<std::string, std::string>>;
struct Request {
    std::string url, method = "POST", body;
    Headers headers;
    size_t max_response_bytes = 512 * 1024;
    int timeout_ms = 20000;
};
struct Response { int status = 0; std::string body, error; };
// Transport must verify TLS, reject redirects and bound the response body.
class Transport {
public:
    virtual ~Transport() = default;
    virtual Response perform(const Request&) = 0;
};
struct Result {
    std::string code, json;
    int http_status = 0;
    explicit operator bool() const { return code.empty(); }
};
struct Avatar {
    std::string shape, color;
    uint32_t rgb = 0, light_rgb = 0, dark_rgb = 0;
};
struct Bot {
    std::string id, session_id, name, description;
    Avatar avatar;
};
Avatar default_avatar(const std::string& key);
Avatar resolve_avatar(const std::string& key, const std::string& shape, const std::string& color);
const std::vector<std::string>& avatar_shapes();
struct Credentials { std::string access_token, refresh_token, team_id; };
struct LoginFlow { std::string uuid, verifier, login_url; };
struct Cursor {
    std::string bot_id, session_id;
    uint32_t generation = 0;
    uint64_t after_updated_seq = 0;
};
struct Event {
    std::string type, entry_id, text, json;
    uint64_t seq = 0, updated_seq = 0;
    bool replay = false;
};

// Incremental Connect JSON decoder. Accepts arbitrary TCP segmentation.
class ConnectDecoder {
public:
    using Frame = std::function<void(const std::string&)>;
    explicit ConnectDecoder(size_t max_frame = 256 * 1024) : limit_(max_frame) {}
    bool feed(const uint8_t* data, size_t size, const Frame& callback);
    bool finish();
    const std::string& error() const { return error_; }
    bool ended() const { return ended_; }
private:
    size_t limit_;
    std::vector<uint8_t> pending_;
    std::string error_;
    bool ended_ = false;
};
std::string connect_frame(const std::string& json);
// Applies only records for this bot AND session. Never advances on unrelated rows.
Result apply_watch_frame(const std::string&, Cursor&, const std::function<void(const Event&)>&);

class Client {
public:
    explicit Client(Transport& transport, Credentials credentials = {}, std::string version = "0.70.2");
    void set_credentials(Credentials credentials);
    Result get_me();
    Result list_bots(std::vector<Bot>& bots, bool& truncated);
    Result send_message(const Bot&, const std::string& text, const std::string& message_id, uint64_t now_ms);
    Result tail(Cursor&);
    Request watch_request(const Cursor&, size_t inline_body_bytes = 131072) const;
    Result poll_login(const LoginFlow&, Credentials&); // AUTH_PENDING on HTTP 404
    const Credentials& credentials() const { return credentials_; }
private:
    Transport& transport_;
    Credentials credentials_;
    std::string version_;
    Headers headers() const;
    Result rpc(const std::string& method, const std::string& body);
};

#ifdef ESP_PLATFORM
class EspTransport : public Transport {
public:
    Response perform(const Request&) override;
    // Return false from on_bytes to cancel. cancel is checked during idle reads.
    Result stream(const Request&, const std::function<bool(const uint8_t*, size_t)>& on_bytes,
                  const std::function<bool()>& cancel);
};
LoginFlow create_login();
std::string random_uuid();
#endif
} // namespace grok
