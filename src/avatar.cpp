// Port of RongleCat/grok-bot-sdk avatar.mjs (MIT).
#include "grok_bot.hpp"
#include <algorithm>
namespace grok {
namespace {
struct Color { const char* name; uint32_t base, light, dark; };
const Color palette[] = {
    {"black",0x000000,0x000000,0xFFFFFF},
    {"brown",0x936439,0xA27952,0x855C36},
    {"red",0xFF263C,0xFF3E51,0xE02135},
    {"orange",0xFF6700,0xFF781C,0xFF6700},
    {"yellow",0xFF9800,0xFFAF38,0xFF9800},
    {"green",0x00C972,0x00C972,0x009957},
    {"cyan",0x00BCA6,0x1CC3B0,0x00A592},
    {"blue",0x1084FE,0x2A92FE,0x0E74E0},
    {"violet",0x9159FE,0xA97EFE,0x804EE0},
    {"magenta",0xFF309B,0xFF5EB1,0xE02A88},
    {"gray",0x777777,0x959595,0x777777},
};
// JavaScript hashes UTF-16 code units, including surrogate pairs.
uint32_t hash_utf16(const std::string& s) {
    uint32_t h=2166136261u;
    for(size_t i=0;i<s.size();) {
        uint32_t cp=static_cast<uint8_t>(s[i++]);
        int n=cp<128?0:cp<224?1:cp<240?2:3;
        if(n) { cp &= (1u<<(6-n))-1; for(int k=0;k<n && i<s.size();++k) cp=(cp<<6)|(static_cast<uint8_t>(s[i++])&63); }
        if(cp>0xffff) { cp-=0x10000; h=(h^(0xd800+(cp>>10)))*16777619u; cp=0xdc00+(cp&1023); }
        h=(h^cp)*16777619u;
    }
    return h;
}
}
const std::vector<std::string>& avatar_shapes() {
    static const std::vector<std::string> names={"blob","pebble","bean","egg","squircle","tablet","capsule","cylinder","hex","gem","crystal","wedge","shield","dome","arch","cloud","teardrop","leaf","square","sparkle","clover","flower","heart","house","star"}; return names;
}
Avatar default_avatar(const std::string& key) {
    static const char* shapes[]={"blob","pebble","squircle","tablet","wedge","hex","cloud","teardrop"};
    uint32_t h=hash_utf16(key), sh=h;
    sh=(sh^(sh>>16))*73244475u; sh=(sh^(sh>>13))*3266489909u; sh^=sh>>16;
    uint32_t seed=h+1831565813u, sample=(seed^(seed>>15))*(1u|seed);
    sample=(sample+(sample^(sample>>7))*(61u|sample))^sample;
    uint32_t r=sample^(sample>>14);
    const auto& c=palette[1+(static_cast<uint64_t>(r)*10>>32)];
    return {shapes[sh%8],c.name,c.base,c.light,c.dark};
}
Avatar resolve_avatar(const std::string& key,const std::string& shape,const std::string& color) {
    auto a=default_avatar(key);
    const auto& shapes=avatar_shapes();
    if(std::find(shapes.begin(),shapes.end(),shape)!=shapes.end()) a.shape=shape;
    for(const auto& c:palette) if(color==c.name) {a.color=color;a.rgb=c.base;a.light_rgb=c.light;a.dark_rgb=c.dark;break;}
    return a;
}
}
