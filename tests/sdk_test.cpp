#include "grok_bot.hpp"
#include "cJSON.h"
#include <iostream>
#include <cstdlib>
#include <limits>
#define CHECK(x) do {if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(0)
using namespace grok;
struct Fake : Transport {Request last;Response next;int calls=0;Response perform(const Request& r) override {++calls;last=r;return next;}};
int main() {
    struct Vector {const char* id;const char* shape;const char* color;};
    const Vector vectors[]={{"a","wedge","blue"},{"abc","hex","cyan"},{"33cf1aae-3a51-452d-b761-34a6ccb62201","cloud","brown"},{"11635495","teardrop","brown"},{"00000000-0000-4000-8000-000000000000","wedge","orange"},{"33CF1AAE-3A51-452D-B761-34A6CCB62201","tablet","red"},{"机器人🤖","teardrop","red"}};
    for(auto v:vectors){auto a=default_avatar(v.id);CHECK(a.shape==v.shape);CHECK(a.color==v.color);}
    CHECK(avatar_shapes().size()==25);auto a=resolve_avatar("a","heart","invalid");CHECK(a.shape=="heart"&&a.color=="blue");a=resolve_avatar("a","invalid","black");CHECK(a.shape=="wedge"&&a.rgb==0);
    Fake t;Client client(t,{"secret","refresh","7"});t.next={200,R"({"agents":[{"agentId":"bot-1","viewerSessionId":"session","name":"小晚","avatarShape":"heart"}],"truncated":true})",""};
    std::vector<Bot> bots;bool truncated=false;CHECK(client.list_bots(bots,truncated));CHECK(bots.size()==1&&truncated&&bots[0].avatar.shape=="heart");CHECK(t.last.url=="https://api2.cursor.sh/aiserver.v1.GrokBotService/ListGrokBotAgents");
    auto bot=bots[0];t.next={200,R"({"delivery":1})",""};CHECK(client.send_message(bot,"帮我看下","uuid",123));auto j=cJSON_Parse(t.last.body.c_str());CHECK(std::string(cJSON_GetObjectItem(j,"text")->valuestring)=="帮我看下");CHECK(std::string(cJSON_GetObjectItem(j,"sessionId")->valuestring)=="session");cJSON_Delete(j);
    auto calls=t.calls;CHECK(!client.send_message(bot," ","uuid",123));CHECK(t.calls==calls);
    t.next={0,"","NETWORK_OR_TLS_ERROR"};CHECK(client.send_message(bot,"hello","uuid",123).code=="SEND_UNCONFIRMED");CHECK(t.calls==calls+1);
    t.next={200,R"({"delivery":9})",""};CHECK(client.send_message(bot,"hello","uuid",123).code=="SEND_REFUSED");
    Credentials creds;t.next={404,"",""};CHECK(client.poll_login({"uuid","verifier",""},creds).code=="AUTH_PENDING");
    t.next={200,R"({"accessToken":"token","refreshToken":"refresh","selectedTeamId":8})",""};auto login=client.poll_login({"uuid","verifier",""},creds);CHECK(login&&login.json.empty()&&creds.team_id=="8");
    Cursor c{"bot-1","session",1,0};t.next={200,R"({"generation":3,"entries":[{"seq":"18446744073709551615"}]})",""};CHECK(client.tail(c));CHECK(c.after_updated_seq==UINT64_MAX&&c.generation==3);
    auto request=client.watch_request(c);CHECK(request.body.size()>5);CHECK(request.body.find("18446744073709551615")!=std::string::npos);
    t.next={200,R"({"generation":3,"entries":[{"seq":"18446744073709551616"}]})",""};CHECK(!client.tail(c));CHECK(c.after_updated_seq==UINT64_MAX);
    for(size_t chunk=1;chunk<40;++chunk){ConnectDecoder d;int count=0;auto frame=connect_frame(R"({"heartbeat":{}})")+connect_frame(R"({"connected":{}})");auto end=connect_frame("{}");end[0]=2;frame+=end;
        for(size_t i=0;i<frame.size();i+=chunk)CHECK(d.feed(reinterpret_cast<const uint8_t*>(frame.data()+i),std::min(chunk,frame.size()-i),[&](const auto&){++count;}));CHECK(count==2&&d.ended()&&d.finish());}
    auto feed=[](ConnectDecoder& d,const std::string& s){return d.feed(reinterpret_cast<const uint8_t*>(s.data()),s.size(),[](const auto&){});};
    ConnectDecoder large(3);CHECK(!feed(large,connect_frame("{\"hello\":1}")));CHECK(large.error()=="STREAM_FRAME_TOO_LARGE");
    ConnectDecoder eof;CHECK(feed(eof,connect_frame("{}")));CHECK(!eof.finish());
    auto bad=connect_frame("{}");bad[0]=1;ConnectDecoder compressed;CHECK(!feed(compressed,bad));
    auto end=connect_frame("{}");end[0]=2;ConnectDecoder trailing;CHECK(!feed(trailing,end+"x"));
    std::vector<Event> events;auto emit=[&](const Event& e){events.push_back(e);};c={"bot-1","session",1,0};
    CHECK(apply_watch_frame(R"({"rows":{"agentId":"other","sessionId":"session","generation":99,"entries":[{"seq":"9"}]}})",c,emit));CHECK(c.after_updated_seq==0&&events.empty());
    CHECK(apply_watch_frame(R"({"rows":{"agentId":"bot-1","sessionId":"other","generation":99,"entries":[{"seq":"9"}]}})",c,emit));CHECK(events.empty());
    auto rows=R"({"rows":{"agentId":"bot-1","sessionId":"session","generation":1,"entries":[{"seq":"2","entryId":"b","body":"eyJraW5kIjoic2VuZC1tZXNzYWdlIiwibWVzc2FnZSI6eyJ0eXBlIjoidGV4dCIsImNvbnRlbnQiOiJoZWxsbyJ9fQ=="},{"seq":"1","entryId":"a","bodyOmitted":true}],"deletes":[{"seq":"3","entryId":"c"}]}})";
    CHECK(apply_watch_frame(rows,c,emit));CHECK(events.size()==3&&events[0].type=="entry.unavailable"&&events[1].text=="hello"&&events[2].type=="message.deleted"&&c.after_updated_seq==3);
    CHECK(apply_watch_frame(rows,c,emit));CHECK(events.size()==3);
    CHECK(apply_watch_frame(R"({"cleared":{"agentId":"bot-1","sessionId":"session","newGeneration":2}})",c,emit));CHECK(c.generation==2&&c.after_updated_seq==0&&events.back().type=="reset");
    CHECK(!apply_watch_frame(R"({"cursorTooOld":{"agentId":"bot-1","sessionId":"session"}})",c,emit));
    std::cout<<"SDK avatar vectors, RPCs, framing, cursor scoping, replay and auth passed\n";
}
