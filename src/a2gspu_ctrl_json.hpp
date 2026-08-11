#pragma once
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace a2ctrljson {
inline void append_escaped(char *out,size_t cap,size_t &o,const char *s){
    for(;s&&*s&&o+8<cap;++s){unsigned char c=(unsigned char)*s;
        if(c=='\\'||c=='\"'){out[o++]='\\';out[o++]=(char)c;}
        else if(c=='\n'){out[o++]='\\';out[o++]='n';}
        else if(c=='\r'){out[o++]='\\';out[o++]='r';}
        else if(c>=0x20)out[o++]=(char)c;
    }
}
inline void format(char *out,size_t cap,const char *command,const char *text){
    size_t o=(size_t)snprintf(out,cap,"{\"schema\":\"a2ctrl-reply-v2\",\"ok\":%s,\"command\":\"",
        text&&!strncmp(text,"status=OK",9)?"true":"false");
    append_escaped(out,cap,o,command); o+=(size_t)snprintf(out+o,cap-o,"\",\"text\":\"");
    append_escaped(out,cap,o,text); snprintf(out+o,cap-o,"\"}");
}
}
