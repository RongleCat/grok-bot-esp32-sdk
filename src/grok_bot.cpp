// Protocol port of RongleCat/grok-bot-sdk (MIT).
#include "grok_bot.hpp"
#include "cJSON.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>

namespace grok {
namespace {
using Json=std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
bool valid_utf8(const std::string& s) {
    for(size_t i=0;i<s.size();) {
        uint8_t c=s[i++];if(c<128)continue;
        int n=c>=0xc2&&c<=0xdf?1:c>=0xe0&&c<=0xef?2:c>=0xf0&&c<=0xf4?3:-1;
        if(n<0||i+n>s.size())return false;
        uint32_t cp=c&((1u<<(6-n))-1);for(int k=0;k<n;++k){uint8_t b=s[i++];if((b&0xc0)!=0x80)return false;cp=(cp<<6)|(b&63);}
        if((n==1&&cp<0x80)||(n==2&&cp<0x800)||(n==3&&cp<0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
    }
    return true;
}
Json parse(const std::string& s) {
    if(!valid_utf8(s))return Json(nullptr,cJSON_Delete);
    return Json(cJSON_ParseWithLengthOpts(s.c_str(),s.size()+1,nullptr,true),cJSON_Delete);
}
cJSON* at(const cJSON* j,const char* key) {return cJSON_GetObjectItemCaseSensitive(j,key);}
std::string str(const cJSON* j) {return cJSON_IsString(j)?j->valuestring:"";}
std::string field(const cJSON* j,const char* key) {return str(at(j,key));}
std::string dump(const cJSON* j) {char* p=cJSON_PrintUnformatted(j);std::string s=p?p:"";cJSON_free(p);return s;}
Json object() {return Json(cJSON_CreateObject(),cJSON_Delete);}
void put(cJSON* j,const char* k,const std::string& v) {cJSON_AddStringToObject(j,k,v.c_str());}
void scope(cJSON* j,const Cursor& c) {put(j,"agentId",c.bot_id);put(j,"sessionId",c.session_id);}
bool uint64(const cJSON* j,uint64_t& value) {
    if(cJSON_IsNumber(j)) {double n=j->valuedouble;if(n<0||n>9007199254740991.0||std::floor(n)!=n)return false;value=static_cast<uint64_t>(n);return true;}
    auto s=str(j);if(s.empty()||s.size()>20)return false;
    uint64_t n=0;for(char c:s) {if(c<'0'||c>'9'||n>(UINT64_MAX-(c-'0'))/10)return false;n=n*10+c-'0';}value=n;return true;
}
bool gen(const cJSON* j,uint32_t& value) {if(!j) {value=0;return true;}uint64_t n;if(!uint64(j,n)||n>UINT32_MAX)return false;value=n;return true;}
bool seq(const cJSON* j,uint64_t& v) {auto p=at(j,"updatedSeq");return uint64(p?p:at(j,"seq"),v);}
bool key(const std::string& s) {return !s.empty()&&s.size()<=128&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos;}
bool matches(const cJSON* j,const Cursor& c) {return j&&field(j,"agentId")==c.bot_id&&field(j,"sessionId")==c.session_id;}
std::string base64(const std::string& s) {
    static const std::string table="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if(s.size()%4)return {};
    std::string out;uint32_t acc=0;int bits=0;bool padding=false;
    for(char c:s) {if(c=='='){padding=true;continue;}auto p=table.find(c);if(padding||p==std::string::npos)return {};acc=(acc<<6)|static_cast<uint32_t>(p);bits+=6;if(bits>=8){bits-=8;out+=static_cast<char>((acc>>bits)&255);}}
    return out;
}
Result checked(Response response) {
    if(!response.error.empty())return {response.error,"",response.status};
    if(response.status<200||response.status>=300)return {"HTTP_ERROR","",response.status};
    auto j=parse(response.body);if(!cJSON_IsObject(j.get()))return {"INVALID_JSON","",response.status};
    return {"",std::move(response.body),response.status};
}
constexpr const char* origin="https://api2.cursor.sh";
}

Client::Client(Transport& t,Credentials c,std::string version):transport_(t),credentials_(std::move(c)),version_(std::move(version)) {}
void Client::set_credentials(Credentials c) {credentials_=std::move(c);}
Headers Client::headers() const {
    Headers h={{"content-type","application/json"},{"connect-protocol-version","1"},{"authorization","Bearer "+credentials_.access_token},
        {"x-cursor-client-type","sand"},{"x-cursor-client-version",version_},{"x-cursor-client-source","sand-desktop"},
        {"x-cursor-client-os","darwin"},{"x-sand-box-namespace","prod"},{"x-ghost-mode","true"}};
    if(!credentials_.team_id.empty())h.emplace_back("x-cursor-team-id",credentials_.team_id);
    return h;
}
Result Client::rpc(const std::string& method,const std::string& body) {
    if(credentials_.access_token.empty())return {"MISSING_ACCESS_TOKEN","",0};
    Request r;r.url=std::string(origin)+"/aiserver.v1."+method;r.headers=headers();r.body=body;
    return checked(transport_.perform(r));
}
Result Client::get_me() {
    auto r=rpc("DashboardService/GetMe","{}");if(!r)return r;
    auto j=parse(r.json);if(field(j.get(),"authId").empty()&&field(j.get(),"userId").empty())r.code="ACCOUNT_AUTH_FAILED";return r;
}
Result Client::list_bots(std::vector<Bot>& bots,bool& truncated) {
    auto r=rpc("GrokBotService/ListGrokBotAgents","{\"includeTeamAgents\":true}");if(!r)return r;
    auto j=parse(r.json);auto agents=at(j.get(),"agents");if(!cJSON_IsArray(agents))return {"INVALID_ROSTER_RESPONSE","",r.http_status};
    std::vector<Bot> next;
    cJSON* a=nullptr;cJSON_ArrayForEach(a,agents) {
        auto id=field(a,"agentId");if(!key(id))return {"INVALID_BOT_RECORD","",r.http_status};
        next.push_back({id,field(a,"viewerSessionId"),field(a,"name"),field(a,"description"),resolve_avatar(id,field(a,"avatarShape"),field(a,"avatarColor"))});
    }
    bots=std::move(next);truncated=cJSON_IsTrue(at(j.get(),"truncated"));return r;
}
Result Client::send_message(const Bot& bot,const std::string& text,const std::string& message_id,uint64_t now_ms) {
    if(!key(bot.id)||text.empty()||text.find_first_not_of(" \t\r\n")==std::string::npos||message_id.empty()||bot.session_id.size()>256)return {"MESSAGE_INVALID","",0};
    auto j=object();put(j.get(),"agentId",bot.id);put(j.get(),"sessionId",bot.session_id);put(j.get(),"text",text);put(j.get(),"messageId",message_id);put(j.get(),"sentAtMs",std::to_string(now_ms));
    cJSON_AddArrayToObject(j.get(),"attachmentPaths");cJSON_AddArrayToObject(j.get(),"attachmentNames");cJSON_AddBoolToObject(j.get(),"isFork",false);
    for(auto k:{"source","initiator","sendSource"})cJSON_AddNumberToObject(j.get(),k,1);
    auto r=rpc("GrokBotService/SendGrokBotUserMessage",dump(j.get()));
    if(!r){r.code="SEND_UNCONFIRMED";return r;} // Delivery may have happened. Caller must never auto-retry.
    auto p=parse(r.json);auto d=at(p.get(),"delivery");auto name=str(d);
    bool accepted=cJSON_IsNumber(d)&&(d->valueint>=1&&d->valueint<=3);
    for(auto v:{"GROK_BOT_USER_MESSAGE_DELIVERY_ACCEPTED_BOX","GROK_BOT_USER_MESSAGE_DELIVERY_ACCEPTED_TEMPORAL","GROK_BOT_USER_MESSAGE_DELIVERY_DUPLICATE"})accepted=accepted||name==v;
    if(!accepted) { r.code="SEND_REFUSED"; }
    return r;
}
Result Client::tail(Cursor& c) {
    if(!key(c.bot_id)||c.session_id.size()>256)return {"CURSOR_INVALID","",0};
    auto j=object();scope(j.get(),c);cJSON_AddNumberToObject(j.get(),"limit",1);
    auto r=rpc("GrokBotService/ListGrokBotTranscriptEntries",dump(j.get()));if(!r)return r;
    auto p=parse(r.json);uint32_t generation;auto entries=at(p.get(),"entries");
    if(!gen(at(p.get(),"generation"),generation)||!cJSON_IsArray(entries))return {"CURSOR_INVALID","",r.http_status};
    uint64_t last=0;cJSON* entry=nullptr;cJSON_ArrayForEach(entry,entries){uint64_t n;if(!seq(entry,n))return {"CURSOR_INVALID","",r.http_status};last=std::max(last,n);}
    c.generation=generation;c.after_updated_seq=last;return r;
}
Request Client::watch_request(const Cursor& c,size_t inline_bytes) const {
    Request r;if(!key(c.bot_id)||c.session_id.size()>256||inline_bytes<1||inline_bytes>524288)return r;
    auto j=object();auto cursors=cJSON_AddArrayToObject(j.get(),"cursors");auto cursor=cJSON_CreateObject();cJSON_AddItemToArray(cursors,cursor);scope(cursor,c);
    cJSON_AddNumberToObject(cursor,"generation",c.generation);put(cursor,"afterUpdatedSeq",std::to_string(c.after_updated_seq));
    cJSON_AddBoolToObject(j.get(),"includeUnlistedAgents",false);cJSON_AddNumberToObject(j.get(),"inlineBodyMaxBytes",inline_bytes);
    r.url=std::string(origin)+"/aiserver.v1.GrokBotService/WatchGrokBotTranscripts";r.body=connect_frame(dump(j.get()));r.headers=headers();
    for(auto& h:r.headers)if(h.first=="content-type")h.second="application/connect+json";
    r.headers.emplace_back("accept","application/connect+json");r.headers.emplace_back("connect-accept-encoding","identity");r.timeout_ms=20000;return r;
}
Result Client::poll_login(const LoginFlow& flow,Credentials& credentials) {
    if(!key(flow.uuid)||!key(flow.verifier))return {"LOGIN_FLOW_INVALID","",0};
    Request r;r.url=std::string(origin)+"/auth/poll?uuid="+flow.uuid+"&verifier="+flow.verifier;r.method="GET";
    auto response=transport_.perform(r);if(response.status==404)return {"AUTH_PENDING","",404};
    auto result=checked(std::move(response));if(!result)return result;
    auto j=parse(result.json);Credentials c{field(j.get(),"accessToken"),field(j.get(),"refreshToken"),{}};
    auto team=at(j.get(),"selectedTeamId");if(cJSON_IsString(team))c.team_id=str(team);else if(cJSON_IsNumber(team))c.team_id=std::to_string(static_cast<int64_t>(team->valuedouble));
    if(c.access_token.empty()||c.refresh_token.empty())return {"LOGIN_INVALID_RESPONSE","",result.http_status};
    credentials=std::move(c);result.json.clear();return result; // Never return credentials as diagnostic JSON.
}
std::string connect_frame(const std::string& json) {
    uint32_t n=json.size();std::string out(5,'\0');for(int i=0;i<4;++i)out[1+i]=static_cast<char>(n>>(24-8*i));return out+json;
}
bool ConnectDecoder::feed(const uint8_t* data,size_t size,const Frame& callback) {
    if(!error_.empty())return false;
    if(ended_&&size){error_="STREAM_END_INVALID";return false;}
    // Consume input a byte range at a time; even a huge caller buffer cannot grow pending beyond one frame.
    while(size) {
        size_t target=5;
        if(pending_.size()>=5){uint32_t n=0;for(int i=1;i<5;++i)n=(n<<8)|pending_[i];target+=n;}
        size_t take=std::min(size,target-pending_.size());pending_.insert(pending_.end(),data,data+take);data+=take;size-=take;
        if(pending_.size()<5)continue;
        auto flags=pending_[0];uint32_t n=0;for(int i=1;i<5;++i)n=(n<<8)|pending_[i];
        if(n>limit_){error_="STREAM_FRAME_TOO_LARGE";return false;}
        if(flags!=0&&flags!=2){error_="STREAM_FLAGS_INVALID";return false;}
        if(pending_.size()<5+n)continue;
        std::string json(reinterpret_cast<const char*>(pending_.data()+5),n);auto j=parse(json);
        if(!cJSON_IsObject(j.get())){error_="STREAM_INVALID_JSON";return false;}
        pending_.clear();
        if(flags==2){if(at(j.get(),"error")){error_="STREAM_RPC_ERROR";return false;}ended_=true;if(size){error_="STREAM_END_INVALID";return false;}return true;}
        callback(json);
    }
    return true;
}
bool ConnectDecoder::finish() {if(!ended_&&error_.empty())error_="STREAM_UNEXPECTED_EOF";return error_.empty();}

Result apply_watch_frame(const std::string& json,Cursor& c,const std::function<void(const Event&)>& emit) {
    auto j=parse(json);if(!cJSON_IsObject(j.get()))return {"STREAM_INVALID_JSON","",0};
    auto simple=[&](const char* key,const char* type){auto p=at(j.get(),key);if(p){Event e;e.type=type;e.json=dump(p);emit(e);}};
    simple("connected","connected");simple("heartbeat","heartbeat");
    auto rows=at(j.get(),"rows");if(matches(rows,c)) {
        uint32_t generation;if(!gen(at(rows,"generation"),generation))return {"CURSOR_INVALID","",0};
        struct Op {cJSON* entry;bool deleted;uint64_t updated,seq;};std::vector<Op> operations;
        for(auto k:{"entries","deletes"}) {auto array=at(rows,k);if(array&&!cJSON_IsArray(array))return {"STREAM_INVALID_JSON","",0};cJSON* e=nullptr;cJSON_ArrayForEach(e,array){uint64_t n,s;if(!seq(e,n)||!uint64(at(e,"seq"),s))return {"CURSOR_INVALID","",0};operations.push_back({e,std::string(k)=="deletes",n,s});}}
        if(generation!=c.generation){c.generation=generation;c.after_updated_seq=0;Event e;e.type="reset";emit(e);}
        std::sort(operations.begin(),operations.end(),[](const Op& a,const Op& b){return a.updated<b.updated;});
        for(const auto& op:operations) {
            if(op.updated<=c.after_updated_seq)continue;
            Event e;e.entry_id=field(op.entry,"entryId");e.seq=op.seq;e.updated_seq=op.updated;e.replay=cJSON_IsTrue(at(rows,"replay"));e.json=dump(op.entry);
            if(op.deleted)e.type="message.deleted";
            else {auto body=parse(base64(field(op.entry,"body")));if(!cJSON_IsObject(body.get()))e.type="entry.unavailable";
                else {auto message=at(body.get(),"message");if(field(body.get(),"kind")=="send-message"&&field(message,"type")=="text"&&cJSON_IsString(at(message,"content"))){e.type="message";e.text=field(message,"content");if(e.entry_id.empty())e.entry_id=field(body.get(),"id");}
                    else {e.type="entry";e.json=dump(body.get());}}}
            c.after_updated_seq=op.updated;emit(e);
        }
    }
    auto cleared=at(j.get(),"cleared");if(matches(cleared,c)){uint32_t generation;if(!gen(at(cleared,"newGeneration"),generation))return {"CURSOR_INVALID","",0};c.generation=generation;c.after_updated_seq=0;Event e;e.type="reset";emit(e);}
    if(matches(at(j.get(),"cursorTooOld"),c)){Event e;e.type="resync-required";emit(e);return {"RESYNC_REQUIRED","",0};}
    auto failed=at(j.get(),"turnFailed");if(matches(failed,c)){Event e;e.type="turn.failed";e.json=dump(failed);emit(e);}
    auto state=at(j.get(),"agentStateChanged");if(state&&field(state,"agentId")==c.bot_id){Event e;e.type="bot.state.changed";e.json=dump(state);emit(e);}
    auto updated=at(j.get(),"rosterChanged");if(updated&&field(updated,"agentId")==c.bot_id){Event e;e.type="bot.updated";e.json=dump(updated);emit(e);}
    return {};
}
} // namespace grok
