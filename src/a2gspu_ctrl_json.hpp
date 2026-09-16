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
// FORK: "status" is the reply's own word (OK, PASS, HALT, BUDGET, HANG, FAIL, ...), and
// "ok" is true when the command did what it was asked: OK, PASS (an assertion held) and
// HALT (a run stopped where a breakpoint asked it to). It used to be true for "status=OK"
// alone, so a passing assert and a breakpoint stop both reported ok:false. BUDGET (the
// target was not reached), HANG and FAIL are false.
inline void status_word(const char *text,char *w,size_t cap){
    size_t n=0; const char *p=text?strstr(text,"status="):nullptr;
    if(p){p+=7; while(*p&&((*p>='A'&&*p<='Z')||*p=='_')&&n+1<cap)w[n++]=*p++;}
    w[n]=0;
}
inline void format(char *out,size_t cap,const char *command,const char *text){
    char w[24]; status_word(text,w,sizeof w);
    const bool ok=!strcmp(w,"OK")||!strcmp(w,"PASS")||!strcmp(w,"HALT");
    size_t o=(size_t)snprintf(out,cap,"{\"schema\":\"a2ctrl-reply-v2\",\"ok\":%s,\"status\":\"%s\",\"command\":\"",
        ok?"true":"false",w);
    append_escaped(out,cap,o,command); o+=(size_t)snprintf(out+o,cap-o,"\",\"text\":\"");
    append_escaped(out,cap,o,text); snprintf(out+o,cap-o,"\"}");
}
}
