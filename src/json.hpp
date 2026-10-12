#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gmf {
// A bounded JSON parser for dlc_load.json. Unknown fields are parsed/skipped;
// malformed input, duplicate enabled_mods and invalid Unicode are rejected.
class Json {
    std::string_view s; std::size_t p = 0;
    void space() { while (p<s.size() && (s[p]==' ' || s[p]=='\r' || s[p]=='\n' || s[p]=='\t')) ++p; }
    bool take(char c) { space(); if (p==s.size() || s[p]!=c) return false; ++p; return true; }
    bool hex(std::uint32_t& n) {
        n=0;
        for(int i=0;i<4;++i) {
            if(p==s.size()) return false;
            char c=s[p++]; int v;
            if(c>='0'&&c<='9') v=c-'0'; else if(c>='a'&&c<='f') v=c-'a'+10;
            else if(c>='A'&&c<='F') v=c-'A'+10; else return false;
            n=(n<<4)|std::uint32_t(v);
        }
        return true;
    }
    static void utf8(std::string& o,std::uint32_t u) {
        if(u<0x80) o+=char(u);
        else if(u<0x800) {o+=char(0xc0|(u>>6));o+=char(0x80|(u&63));}
        else if(u<0x10000) {o+=char(0xe0|(u>>12));o+=char(0x80|((u>>6)&63));o+=char(0x80|(u&63));}
        else {o+=char(0xf0|(u>>18));o+=char(0x80|((u>>12)&63));o+=char(0x80|((u>>6)&63));o+=char(0x80|(u&63));}
    }
    bool string(std::string& o) {
        if(!take('"')) return false;
        o.clear();
        while(p<s.size()) {
            unsigned char c=s[p++];
            if(c=='"') return true;
            if(c<32) return false;
            if(c!='\\') {
                if(c<0x80) {o+=char(c);continue;}
                unsigned more;std::uint32_t u,min;
                if(c>=0xc2&&c<=0xdf) {more=1;u=c&31;min=0x80;}
                else if(c>=0xe0&&c<=0xef) {more=2;u=c&15;min=0x800;}
                else if(c>=0xf0&&c<=0xf4) {more=3;u=c&7;min=0x10000;}
                else return false;
                o+=char(c);
                for(unsigned i=0;i<more;++i) {
                    if(p==s.size())return false;
                    unsigned char next=s[p++];if((next&0xc0)!=0x80)return false;
                    u=(u<<6)|(next&63);o+=char(next);
                }
                if(u<min||u>0x10ffff||(u>=0xd800&&u<=0xdfff))return false;
                continue;
            }
            if(p==s.size()) return false;
            char e=s[p++];
            switch(e) {
            case '"': case '\\': case '/':o+=e;break;
            case 'b':o+='\b';break;case 'f':o+='\f';break;case 'n':o+='\n';break;
            case 'r':o+='\r';break;case 't':o+='\t';break;
            case 'u': {
                std::uint32_t u,v; if(!hex(u)) return false;
                if(u>=0xd800&&u<=0xdbff) {
                    if(p+2>s.size()||s[p]!='\\'||s[p+1]!='u') return false;
                    p+=2; if(!hex(v)||v<0xdc00||v>0xdfff) return false;
                    u=0x10000+((u-0xd800)<<10)+(v-0xdc00);
                } else if(u>=0xdc00&&u<=0xdfff) return false;
                if(u==0) return false;
                utf8(o,u); break;
            }
            default:return false;
            }
        }
        return false;
    }
    bool value(int depth) {
        if(depth>32) return false;
        space(); if(p==s.size()) return false;
        if(s[p]=='"') {std::string ignored;return string(ignored);}
        if(s[p]=='{'||s[p]=='[') {
            bool object=s[p++]=='{'; char end=object?'}':']';
            if(take(end)) return true;
            do {
                if(object) {std::string key;if(!string(key)||!take(':')) return false;}
                if(!value(depth+1)) return false;
                if(take(end)) return true;
            } while(take(','));
            return false;
        }
        for(auto literal:{"true","false","null"}) {
            std::string_view l=literal;
            if(s.substr(p,l.size())==l) {p+=l.size();return true;}
        }
        if(s[p]=='-') ++p;
        if(p==s.size()) return false;
        if(s[p]=='0') ++p;
        else {if(s[p]<'1'||s[p]>'9') return false;do{++p;}while(p<s.size()&&s[p]>='0'&&s[p]<='9');}
        if(p<s.size()&&s[p]=='.') {++p;std::size_t start=p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;if(start==p)return false;}
        if(p<s.size()&&(s[p]=='e'||s[p]=='E')) {++p;if(p<s.size()&&(s[p]=='+'||s[p]=='-'))++p;std::size_t start=p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;if(start==p)return false;}
        return true;
    }
public:
    explicit Json(std::string_view text):s(text) {if(s.substr(0,3)=="\xef\xbb\xbf")p=3;}
    bool enabled(std::vector<std::string>& mods) {
        mods.clear(); bool found=false;
        if(!take('{')||take('}'))return false;
        do {
            std::string key;if(!string(key)||!take(':'))return false;
            if(key=="enabled_mods") {
                if(found||!take('['))return false;
                found=true;
                if(!take(']')) {
                    do{std::string path;if(!string(path)||path.empty()||path.find('\0')!=std::string::npos)return false;mods.push_back(path);if(mods.size()>4096)return false;if(take(']'))break;if(!take(','))return false;}while(true);
                }
            } else if(!value(0))return false;
            if(take('}'))break;
            if(!take(','))return false;
        }while(true);
        space();return found&&p==s.size();
    }
};
}
