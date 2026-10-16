#pragma once
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

namespace a2ctrljson {
inline std::string native_payload;
inline std::string native_status;
inline const char *surface="unknown";
inline std::string quote(const std::string &s) {
    std::string out="\"";
    for(unsigned char c:s) {
        if(c=='\\' || c=='\"') {out+='\\';out+=(char)c;}
        else if(c<32) {char b[7];std::snprintf(b,sizeof b,"\\u%04x",c);out+=b;}
        else out+=(char)c;
    }
    return out+'\"';
}
inline void append_escaped(char *out,size_t cap,size_t &o,const char *s){
    for(;s&&*s&&o+8<cap;++s){unsigned char c=(unsigned char)*s;
        if(c=='\\'||c=='\"'){out[o++]='\\';out[o++]=(char)c;}
        else if(c=='\n'){out[o++]='\\';out[o++]='n';}
        else if(c=='\r'){out[o++]='\\';out[o++]='r';}
        else if(c>=0x20)out[o++]=(char)c;
    }
}
// FORK: "status" is the reply's own word (OK, PASS, HALT, BUDGET, HANG, FAIL, ...), and
// "ok" is true when the command did what it was asked: OK, PASS (an assertion held) and
// HALT (a run stopped where a breakpoint asked it to). It used to be true for "status=OK"
// alone, so a passing assert and a breakpoint stop both reported ok:false. BUDGET (the
// target was not reached), HANG and FAIL are false.
inline void status_word(const char *text,char *w,size_t cap){
    size_t n=0; const char *p=text && !std::strncmp(text,"status=",7)?text:nullptr;
    if(p){p+=7; while(*p&&((*p>='A'&&*p<='Z')||*p=='_')&&n+1<cap)w[n++]=*p++;}
    w[n]=0;
}
inline void format(char *out,size_t cap,const char *command,const char *text){
    char w[24]; status_word(text,w,sizeof w);
    if(!w[0] && !native_status.empty())std::snprintf(w,sizeof w,"%s",native_status.c_str());
    const bool ok=!strcmp(w,"OK")||!strcmp(w,"PASS")||!strcmp(w,"HALT");
    std::string json="{\"schema\":\"a2ctrl-reply-v2\",\"ok\":"+std::string(ok?"true":"false")+
        ",\"status\":"+quote(w)+",\"command\":"+quote(command?command:"")+",\"text\":"+quote(text?text:"")+
        ",\"data\":"+(native_payload.empty()?"null":native_payload)+"}";
    if(json.size()>=cap) {
        std::string failure="{\"schema\":\"a2ctrl-reply-v2\",\"ok\":false,\"status\":\"FAIL\",\"command\":"+quote(command?command:"")+",\"error\":\"reply-too-large\",\"data\":null}";
        if(failure.size()<cap)std::memcpy(out,failure.c_str(),failure.size()+1);
        else std::snprintf(out,cap,"{\"ok\":false,\"error\":\"reply-too-large\"}");
    } else std::memcpy(out,json.c_str(),json.size()+1);
}
}
