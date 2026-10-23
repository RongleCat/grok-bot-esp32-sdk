#include "grok_bot.hpp"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <strings.h>

namespace grok {
namespace {
class Handle {
public:
    esp_http_client_handle_t p=nullptr;
    std::string response_type;
    ~Handle(){if(p){esp_http_client_close(p);esp_http_client_cleanup(p);}}
};
bool init(Handle& h,const Request& r) {
    if(r.url.rfind("https://",0)!=0)return false;
    esp_http_client_config_t cfg={};cfg.url=r.url.c_str();cfg.crt_bundle_attach=esp_crt_bundle_attach;
    cfg.timeout_ms=r.timeout_ms;cfg.disable_auto_redirect=true;cfg.buffer_size=4096;cfg.buffer_size_tx=4096;
    cfg.user_data=&h;
    cfg.event_handler=[](esp_http_client_event_t* e)->esp_err_t {
        if(e->event_id==HTTP_EVENT_ON_HEADER&&e->header_key&&strcasecmp(e->header_key,"content-type")==0)
            static_cast<Handle*>(e->user_data)->response_type=e->header_value?e->header_value:"";
        return ESP_OK;
    };
    h.p=esp_http_client_init(&cfg);if(!h.p)return false;
    esp_http_client_set_method(h.p,r.method=="GET"?HTTP_METHOD_GET:HTTP_METHOD_POST);
    for(const auto& pair:r.headers)esp_http_client_set_header(h.p,pair.first.c_str(),pair.second.c_str());
    return true;
}
bool open(Handle& h,const Request& r) {
    if(esp_http_client_open(h.p,r.body.size())!=ESP_OK)return false;
    size_t sent=0;
    while(sent<r.body.size()){int n=esp_http_client_write(h.p,r.body.data()+sent,r.body.size()-sent);if(n<=0)return false;sent+=n;}
    return esp_http_client_fetch_headers(h.p)>=0;
}
std::string urlbase64(const uint8_t* p,size_t n) {
    std::array<unsigned char,128> out{};size_t len=0;
    if(mbedtls_base64_encode(out.data(),out.size(),&len,p,n)!=0)return {};
    std::string s(reinterpret_cast<char*>(out.data()),len);
    for(auto& c:s){if(c=='+')c='-';else if(c=='/')c='_';}while(!s.empty()&&s.back()=='=')s.pop_back();return s;
}
}
Response EspTransport::perform(const Request& r) {
    Handle h;if(!init(h,r))return {0,"","TRANSPORT_INIT_FAILED"};
    if(!open(h,r))return {0,"","NETWORK_OR_TLS_ERROR"};
    Response result;result.status=esp_http_client_get_status_code(h.p);
    if(result.status<200||result.status>=300)return result;
    std::vector<char> buf(4096);
    while(true){int n=esp_http_client_read(h.p,buf.data(),buf.size());if(n<0){result.error="NETWORK_OR_TLS_ERROR";break;}
        if(n==0){if(!esp_http_client_is_complete_data_received(h.p))result.error="RESPONSE_TRUNCATED";break;}
        if(result.body.size()+n>r.max_response_bytes){result.error="RESPONSE_TOO_LARGE";break;}result.body.append(buf.data(),n);}
    if(!result.error.empty()) { result.body.clear(); }
    return result;
}
Result EspTransport::stream(const Request& r,const std::function<bool(const uint8_t*,size_t)>& bytes,const std::function<bool()>& cancel) {
    if(cancel())return {"CANCELLED","",0};
    Handle h;if(!init(h,r))return {"TRANSPORT_INIT_FAILED","",0};if(!open(h,r))return {"STREAM_CONNECTION_FAILED","",0};
    int status=esp_http_client_get_status_code(h.p);if(status!=200)return {"STREAM_CONNECTION_FAILED","",status};
    if(h.response_type.substr(0,h.response_type.find(';'))!="application/connect+json")
        return {"STREAM_INVALID_RESPONSE","",status};
    esp_http_client_set_timeout_ms(h.p,1000);
    std::vector<uint8_t> buf(4096);
    while(!cancel()) {
        int n=esp_http_client_read(h.p,reinterpret_cast<char*>(buf.data()),buf.size());
        if(n<0){if(n==-ESP_ERR_HTTP_EAGAIN)continue;return {"STREAM_CONNECTION_FAILED","",status};}
        if(n==0){if(esp_http_client_is_complete_data_received(h.p))return {};return {"STREAM_UNEXPECTED_EOF","",status};}
        if(!bytes(buf.data(),n))return {"CANCELLED","",status};
    }
    return {"CANCELLED","",status};
}
std::string random_uuid() {
    uint8_t v[16];esp_fill_random(v,sizeof(v));v[6]=(v[6]&15)|64;v[8]=(v[8]&63)|128;
    char s[37];snprintf(s,sizeof(s),"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10],v[11],v[12],v[13],v[14],v[15]);return s;
}
LoginFlow create_login() {
    uint8_t random[32],digest[32];esp_fill_random(random,sizeof(random));LoginFlow flow;flow.uuid=random_uuid();flow.verifier=urlbase64(random,sizeof(random));
    mbedtls_sha256(reinterpret_cast<const uint8_t*>(flow.verifier.data()),flow.verifier.size(),digest,0);
    flow.login_url="https://cursor.com/loginDeepControl?challenge="+urlbase64(digest,sizeof(digest))+"&uuid="+flow.uuid+"&mode=login&redirectTarget=sand&supportsSelectedTeamLogin=true";
    return flow;
}
} // namespace grok
