// GSPS v1 saved-state comparison. Parses serialized domains, never restores a
// machine or treats file offsets as CPU-visible addresses. Device payloads are
// opaque, versioned bytes; manifests describe excluded state, not equality proof.
#pragma once
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace a2snapdiff {
using Bytes=std::vector<uint8_t>;
inline constexpr size_t max_file=256u*1024u*1024u, max_ranges=128;
struct Section {size_t begin=0,size=0;uint32_t version=0;std::string model;};
struct Register {std::string name;uint64_t value=0;};
struct Snapshot {
    Bytes bytes;
    std::map<std::string,Section> sections;
    std::vector<Register> registers;
    std::string layout,manifest;
    bool manifest_present=false;
    uint32_t ram_banks=0,rom_banks=0;
};
struct Cursor {
    const Bytes &bytes;size_t pos=0;
    void need(size_t n) const {if(pos>bytes.size() || n>bytes.size()-pos)throw std::runtime_error("truncated snapshot at offset "+std::to_string(pos));}
    uint64_t get(unsigned n) {need(n);uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)bytes[pos++]<<(8*i);return v;}
    void skip(size_t n) {need(n);pos+=n;}
    std::string text(size_t n) {need(n);std::string s(bytes.begin()+pos,bytes.begin()+pos+n);pos+=n;return s;}
};
inline std::string quote(const std::string &value) {
    std::string out="\"";
    for(unsigned char c:value) {
        if(c=='"' || c=='\\') {out+='\\';out+=(char)c;}
        else if(c<32) {char b[7];std::snprintf(b,sizeof b,"\\u%04x",c);out+=b;}
        else out+=(char)c;
    }
    return out+'"';
}
// Legacy snapshot commands accepted the whole unquoted remainder (including
// spaces). Preserve that spelling, and additionally accept one quoted path.
inline bool snapshot_path(const char *args,std::string &path) {
    std::string text=args;auto first=text.find_first_not_of(" \t");if(first==std::string::npos)return false;
    text=text.substr(first);auto last=text.find_last_not_of(" \t");text.resize(last+1);
    if(text.front()=='"') {
        if(text.size()<3 || text.back()!='"' || text.find('"',1)!=text.size()-1)return false;
        text=text.substr(1,text.size()-2);
    } else if(text.find('"')!=std::string::npos)return false;
    if(text.empty())return false;path=text;return true;
}
inline void section(Snapshot &s,Cursor &c,const std::string &name,size_t size,const char *model,uint32_t version=0) {
    if(!s.sections.emplace(name,Section{c.pos,size,version,model}).second)throw std::runtime_error("duplicate section: "+name);
    c.skip(size);
}
inline void pages(Snapshot &s,Cursor &c,const std::string &name,const std::vector<size_t> &sizes,bool counted=true,size_t n=256) {
    if(counted && c.get(4)!=n)throw std::runtime_error("unsupported page geometry: "+name);
    size_t start=c.pos;
    for(size_t i=0;i<n*2;i++) {
        int region=(int8_t)c.get(1);uint32_t off=(uint32_t)c.get(4);
        if(region < -2 || region >= (int)sizes.size() ||
           (region>=0 && off>=sizes[(size_t)region]) || (region<0 && off))
            throw std::runtime_error("invalid page reference: "+name);
    }
    s.sections.emplace(name,Section{start,n*10,0,"serialized-page-map"});
}
inline void iie(Snapshot &s,Cursor &c,const std::string &prefix) {
    section(s,c,prefix+".switches",4,"serialized-mmu-registers");
    section(s,c,prefix+".ram",131072,"physical-ram-offset");
    pages(s,c,prefix+".pages",{131072,16384});
    pages(s,c,prefix+".internal-rom-pages",{131072,16384},false,15);
    pages(s,c,prefix+".slot-rom-pages",{131072,16384},false,15);
}
inline std::string field(const std::string &manifest,const std::string &key) {
    std::istringstream in(manifest);std::string token;
    while(in>>token)if(token.compare(0,key.size()+1,key+"=")==0)return token.substr(key.size()+1);
    return {};
}
inline Snapshot parse_layout(const Bytes &data,bool gs) {
    Snapshot s;s.bytes=data;s.layout=gs?"iigs-v1":"iie-v1";
    Cursor c{data};
    if(c.get(4)!=0x47535053 || c.get(4)!=1)throw std::runtime_error("unsupported GSPS magic/version");
    for(auto entry:std::vector<std::pair<std::string,unsigned>>{
        {"PC",4},{"DB",4},{"SP",2},{"A",2},{"X",2},{"Y",2},{"D",2},
        {"P",1},{"E",1},{"clock_stopped",1},{"halt",1},{"irq_asserted",1},
        {"irq_pipe",1},{"reset_asserted",8},{"rdy",1}})
        s.registers.push_back({entry.first,c.get(entry.second)});
    if(gs) {
        s.ram_banks=(uint32_t)c.get(4);s.rom_banks=(uint32_t)c.get(4);
        if(!s.ram_banks || s.ram_banks>256 || !s.rom_banks || s.rom_banks>4)
            throw std::runtime_error("unsupported IIgs RAM/ROM geometry");
        section(s,c,"iigs.switches",26,"serialized-mmu-registers");
        section(s,c,"iigs.ram",(size_t)s.ram_banks*65536,"physical-ram-offset");
        pages(s,c,"iigs.pages",{(size_t)s.ram_banks*65536,131072,(size_t)s.rom_banks*65536});
        iie(s,c,"megaii");
    } else iie(s,c,"iie");
    size_t devices=0;
    for(;;) {
        uint32_t magic=(uint32_t)c.get(4);
        if(magic==0x444E4553)break;
        if(magic!=0x56445347 || ++devices>64)throw std::runtime_error("invalid device framing/sentinel");
        std::string padded=c.text(8);size_t end=padded.find('\0');
        if(end==std::string::npos || !end)throw std::runtime_error("invalid device block name");
        std::string name=padded.substr(0,end);
        for(size_t i=end;i<padded.size();i++)if(padded[i])throw std::runtime_error("invalid device name padding");
        for(char v:name)if(!((v>='a' && v<='z') || (v>='0' && v<='9') || v=='_'))throw std::runtime_error("invalid device name character");
        uint32_t version=(uint32_t)c.get(4),length=(uint32_t)c.get(4);
        if(!version)throw std::runtime_error("invalid device version");
        section(s,c,"device."+name,length,"opaque-device-payload",version);
    }
    if(c.pos<data.size()) {
        if(c.get(4)!=0x4D505347)throw std::runtime_error("invalid manifest framing");
        size_t size=(size_t)c.get(4);
        if(!size || size>4096)throw std::runtime_error("invalid manifest size");
        s.manifest=c.text(size);s.manifest_present=true;
        for(unsigned char v:s.manifest)if(v<32 || v>126)throw std::runtime_error("non-ASCII manifest");
        auto machine=field(s.manifest,"machine");auto at=machine.rfind("/p");
        if(at!=std::string::npos && ((machine.substr(at+2)=="5")!=gs))
            throw std::runtime_error("manifest platform disagrees with memory layout");
    }
    if(c.pos!=data.size())throw std::runtime_error("trailing bytes after manifest");
    return s;
}
inline Snapshot parse(const Bytes &bytes) {
    if(bytes.size()>max_file)throw std::runtime_error("snapshot exceeds 256 MiB reader limit");
    // No marker scanning through guest RAM. Only a fully parsed, sentinel-ended
    // layout may win; ambiguous layouts are refused, including legacy files.
    std::vector<Snapshot> found;std::string errors;
    for(bool gs:{false,true}) {
        try {found.push_back(parse_layout(bytes,gs));}
        catch(const std::exception &e) {errors+=(gs?" IIgs: ":" IIe: ");errors+=e.what();}
    }
    if(found.size()!=1)throw std::runtime_error(found.empty()?"invalid snapshot;"+errors:"ambiguous snapshot layout");
    return std::move(found[0]);
}
inline Snapshot read(const std::string &path) {
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in)throw std::runtime_error("cannot open snapshot: "+path);
    auto length=in.tellg();
    if(length<0 || (uint64_t)length>max_file)throw std::runtime_error("invalid/oversized snapshot: "+path);
    Bytes bytes((size_t)length);in.seekg(0);
    if(!in.read((char *)bytes.data(),(std::streamsize)bytes.size()))throw std::runtime_error("snapshot read failed: "+path);
    return parse(bytes);
}
inline std::string describe(const Snapshot &s) {
    std::ostringstream out;out<<"{\"schema\":\"a2snapshot-inventory-v1\",\"model\":\"serialized-saved-state\",\"layout\":"<<quote(s.layout)
        <<",\"manifest_present\":"<<(s.manifest_present?"true":"false")<<",\"manifest\":"<<quote(s.manifest)<<",\"complete_replay\":false,\"sections\":[";
    bool comma=false;
    for(const auto &item:s.sections){if(comma)out<<',';comma=true;out<<"{\"name\":"<<quote(item.first)<<",\"version\":"<<item.second.version
        <<",\"bytes\":"<<item.second.size<<",\"model\":"<<quote(item.second.model)<<'}';}
    return out.str()+"]}";
}
struct Range {size_t begin=0,end=0;std::string before,after;};
struct Change {
    std::string name,model;size_t changed_bytes=0,range_count=0,size_before=0,size_after=0;
    uint32_t version_before=0,version_after=0;bool present_before=false,present_after=false;
    std::vector<Range> ranges;
};
struct RegisterChange {std::string name;uint64_t before=0,after=0;};
inline std::string preview(const Snapshot &s,const Section &sec,size_t begin,size_t end) {
    static const char hex[]="0123456789ABCDEF";std::string out;
    for(size_t i=begin;i<end && i<begin+16 && i<sec.size;i++) {
        uint8_t b=s.bytes[sec.begin+i];out+=hex[b>>4];out+=hex[b&15];
    }
    return out;
}
struct Report {
    bool equal=true;size_t changed_bytes=0;std::string layout,manifest_before,manifest_after;
    std::vector<RegisterChange> registers;std::vector<Change> changes;
    std::string json() const {
        std::ostringstream out;
        out<<"{\"schema\":\"a2snapdiff-v1\",\"model\":\"serialized-saved-state\",\"layout\":"<<quote(layout)
           <<",\"equal\":"<<(equal?"true":"false")<<",\"section_changed_bytes\":"<<changed_bytes
           <<",\"limits\":{\"ranges_per_section\":"<<max_ranges<<",\"preview_bytes\":16},\"registers\":[";
        bool first=true;
        for(const auto &r:registers) {if(!first)out<<',';first=false;
            out<<"{\"name\":"<<quote(r.name)<<",\"before\":"<<r.before<<",\"after\":"<<r.after<<'}';}
        out<<"],\"sections\":[";first=true;
        for(const auto &s:changes) {
            if(!first)out<<',';first=false;
            out<<"{\"name\":"<<quote(s.name)<<",\"model\":"<<quote(s.model)
               <<",\"present_before\":"<<(s.present_before?"true":"false")<<",\"present_after\":"<<(s.present_after?"true":"false")
               <<",\"size_before\":"<<s.size_before<<",\"size_after\":"<<s.size_after
               <<",\"version_before\":"<<s.version_before<<",\"version_after\":"<<s.version_after
               <<",\"changed_bytes\":"<<s.changed_bytes<<",\"range_count\":"<<s.range_count
               <<",\"ranges_truncated\":"<<(s.range_count>s.ranges.size()?"true":"false")<<",\"ranges\":[";
            bool first_range=true;
            for(const auto &r:s.ranges) {if(!first_range)out<<',';first_range=false;
                out<<"{\"begin\":"<<r.begin<<",\"end\":"<<r.end<<",\"before_hex\":"<<quote(r.before)<<",\"after_hex\":"<<quote(r.after)<<'}';}
            out<<"]}";
        }
        out<<"],\"manifest_before\":"<<quote(manifest_before)<<",\"manifest_after\":"<<quote(manifest_after)
           <<",\"manifest_changed\":"<<(manifest_before!=manifest_after?"true":"false")
           <<",\"scope\":\"Saved CPU/MMU/RAM/page maps and opaque device payloads only; physical offsets, not CPU-visible addresses. Manifests are metadata. Excluded state is not compared.\"}";
        return out.str();
    }
};
inline Report compare(const Snapshot &a,const Snapshot &b) {
    if(a.layout!=b.layout || a.ram_banks!=b.ram_banks || a.rom_banks!=b.rom_banks)
        throw std::runtime_error("incompatible snapshot memory geometry");
    Report out;out.layout=a.layout;out.manifest_before=a.manifest;out.manifest_after=b.manifest;
    for(size_t i=0;i<a.registers.size();i++)if(a.registers[i].value!=b.registers.at(i).value)
        out.registers.push_back({a.registers[i].name,a.registers[i].value,b.registers[i].value});
    std::set<std::string> names;for(const auto &s:a.sections)names.insert(s.first);for(const auto &s:b.sections)names.insert(s.first);
    for(const auto &name:names) {
        auto ai=a.sections.find(name),bi=b.sections.find(name);
        Section absent{};const auto &as=ai==a.sections.end()?absent:ai->second;
        const auto &bs=bi==b.sections.end()?absent:bi->second;
        Change diff;diff.name=name;diff.model=ai==a.sections.end()?bs.model:as.model;
        diff.present_before=ai!=a.sections.end();diff.present_after=bi!=b.sections.end();
        diff.size_before=as.size;diff.size_after=bs.size;diff.version_before=as.version;diff.version_after=bs.version;
        size_t n=as.size>bs.size?as.size:bs.size;
        for(size_t i=0;i<n;) {
            auto different=[&](size_t j){return j>=as.size || j>=bs.size || a.bytes[as.begin+j]!=b.bytes[bs.begin+j];};
            if(!different(i)) {++i;continue;}
            size_t start=i;while(i<n && different(i))++i;
            diff.changed_bytes+=i-start;++diff.range_count;
            if(diff.ranges.size()<max_ranges)diff.ranges.push_back({start,i,preview(a,as,start,i),preview(b,bs,start,i)});
        }
        if(diff.changed_bytes || diff.present_before!=diff.present_after || diff.version_before!=diff.version_after) {
            out.changed_bytes+=diff.changed_bytes;out.changes.push_back(std::move(diff));
        }
    }
    out.equal=out.registers.empty() && out.changes.empty();return out;
}
inline void write_report(const std::string &a,const std::string &b,const std::string &path,const Report &report) {
    auto dest=std::filesystem::weakly_canonical(std::filesystem::absolute(path));
    for(const auto &input:{a,b}) {
        auto source=std::filesystem::weakly_canonical(std::filesystem::absolute(input));
        if(dest==source || (std::filesystem::exists(dest) && std::filesystem::equivalent(dest,source)))
            throw std::runtime_error("report must not overwrite an input snapshot");
    }
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    out<<report.json()<<'\n';out.close();
    if(!out)throw std::runtime_error("cannot write snapshot diff report: "+path);
}
// Quoted paths preserve Windows backslashes; exactly three paths required.
inline std::vector<std::string> paths(const char *args) {
    std::vector<std::string> out;const char *p=args;
    while(*p) {
        while(*p==' ' || *p=='\t')++p;if(!*p)break;
        bool quoted=*p=='"';if(quoted)++p;std::string word;
        while(*p && (quoted?*p!='"':(*p!=' ' && *p!='\t')))word+=*p++;
        if(quoted) {if(*p!='"')throw std::runtime_error("unclosed path quote");++p;if(*p && *p!=' ' && *p!='\t')throw std::runtime_error("bad path delimiter");}
        if(word.empty())throw std::runtime_error("empty path");out.push_back(word);
    }
    if(out.size()!=3)throw std::runtime_error("usage: diff <snapshotA> <snapshotB> <report.json> (quote paths with spaces)");
    return out;
}
inline void command(const char *args,char *result,size_t size) {
    try {
        auto p=paths(args);auto report=compare(read(p[0]),read(p[1]));write_report(p[0],p[1],p[2],report);
        std::snprintf(result,size,"status=OK diff model=serialized-saved-state format=a2snapdiff-v1 equal=%d registers=%zu sections=%zu section_bytes=%zu -> %s",
            report.equal?1:0,report.registers.size(),report.changes.size(),report.changed_bytes,p[2].c_str());
    } catch(const std::exception &e) {std::snprintf(result,size,"status=FAIL diff %s",e.what());}
}
}
