#pragma once
// Socket tool framing is independent of command parsing and never truncates.
#include <string>
namespace a2agent {
inline bool valid_command(const std::string &text) {
    if(text.empty() || text.size()>2047)return false;
    for(size_t i=0;i<text.size();) {
        unsigned char c=(unsigned char)text[i++];
        if(c<32 || c==127)return false;
        if(c<128)continue;
        unsigned count=0,min=0,point=0;
        if(c>=0xc2 && c<=0xdf){count=1;min=0x80;point=c&31;}
        else if(c>=0xe0 && c<=0xef){count=2;min=0x800;point=c&15;}
        else if(c>=0xf0 && c<=0xf4){count=3;min=0x10000;point=c&7;}
        else return false;
        if(i+count>text.size())return false;
        while(count--) {unsigned char d=(unsigned char)text[i++];if((d&0xc0)!=0x80)return false;point=(point<<6)|(d&63);}
        if(point<min || point>0x10ffff || (point>=0xd800 && point<=0xdfff))return false;
    }
    return true;
}
inline std::string word(const std::string &text){return text.substr(0,text.find_first_of(" \t"));}
inline bool live_context(const std::string &text) {
    if(word(text)!="context")return false;
    auto first=text.find_first_not_of(' ',7);
    const auto query=first==std::string::npos?"overview":word(text.substr(first));
    for(auto name:{"overview","topology","device","address","irq","schedule","history","changes","stop","pixel","snapshot"})if(query==name)return true;
    return false;
}
}
