// Owned synthetic GSPS v1 fixtures; no ROMs, media or emulator needed.
#include "a2gspu_snapdiff.hpp"
#include <iostream>
using namespace a2snapdiff;
static int checks=0;
static void check(bool value,const char *label) {
    ++checks;
    if (!value) throw std::runtime_error(label);
}
static void put(Bytes &b,uint64_t v,unsigned n) { while(n--) { b.push_back((uint8_t)v);v>>=8; } }
static void pages(Bytes &b,unsigned n,bool counted=true) {
    if(counted)put(b,n,4);
    for(unsigned i=0;i<n*2;i++) { put(b,255,1);put(b,0,4); }
}
static Bytes fixture(bool gs=false,bool manifest=true) {
    Bytes b;
    put(b,0x47535053,4);put(b,1,4);b.resize(41,0);
    if(gs) {
        put(b,1,4);put(b,2,4);b.resize(b.size()+26,0);
        b.resize(b.size()+65536,0);pages(b,256);
    }
    b.resize(b.size()+4+131072,0);pages(b,256);pages(b,15,false);pages(b,15,false);
    put(b,0x56445347,4);
    for(char c:std::string("testdev\0",8))b.push_back((uint8_t)c);
    put(b,1,4);put(b,2,4);put(b,0x1234,2);
    put(b,0x444E4553,4);
    if(manifest) {
        std::string m=std::string("class=A devices=testdev machine=file/p")+(gs?"5":"3")+" slots=- frame=0";
        put(b,0x4D505347,4);put(b,m.size(),4);b.insert(b.end(),m.begin(),m.end());
    }
    return b;
}
static void selftest() {
    for(bool gs:{false,true}) {
        auto data=fixture(gs);auto a=parse(data);
        auto same=compare(a,parse(data));
        check(same.equal && same.changed_bytes==0,"identical states must have empty diffs");
        auto changed=data;changed[18]=0x42;
        auto cpu=compare(a,parse(changed));
        check(!cpu.equal && cpu.registers.size()==1 && cpu.registers[0].name=="A","CPU register decoded by name");
        changed=data;
        const auto &ram=a.sections.at(gs?"iigs.ram":"iie.ram");
        changed[ram.begin+123]=0x7B;
        auto memory=compare(a,parse(changed));
        check(memory.changed_bytes==1 && memory.changes.size()==1 &&
            memory.changes[0].ranges.size()==1 && memory.changes[0].ranges[0].begin==123 &&
            memory.changes[0].ranges[0].end==124,"one planted RAM byte located exactly");
        changed=data;
        auto dev=a.sections.at("device.testdev");changed[dev.begin]^=1;
        auto device=compare(a,parse(changed));
        check(device.changed_bytes==1 && device.changes[0].name=="device.testdev","device payload difference located");
        // A planted off-by-one expected range is a failing assertion.
        check(memory.changes[0].ranges[0].begin!=124,"wrong expected offset is distinguishable");
        auto legacy=parse(fixture(gs,false));
        check(!legacy.manifest_present,"pre-manifest snapshot declared explicitly");
        check(compare(a,legacy).equal,"manifest presence is metadata, not saved state");
        auto bad=data;bad.resize(40);
        bool refused=false;try { parse(bad); } catch(const std::exception &) {refused=true;}
        check(refused,"truncated CPU must fail");
        bad=data;bad[4]=2;refused=false;
        try { parse(bad); } catch(const std::exception &) {refused=true;}
        check(refused,"unknown snapshot version must fail");
        bad=data;bad.pop_back();refused=false;
        try { parse(bad); } catch(const std::exception &) {refused=true;}
        check(refused,"truncated manifest must fail");
        bad=data;bad.push_back(1);refused=false;
        try { parse(bad); } catch(const std::exception &) {refused=true;}
        check(refused,"trailing garbage must fail");
        bad=data;bad[dev.begin-4]=255;bad[dev.begin-3]=255;bad[dev.begin-2]=255;bad[dev.begin-1]=127;
        refused=false;try { parse(bad); } catch(const std::exception &) {refused=true;}
        check(refused,"oversized device length must fail");
        bad=data;bad[a.sections.at(gs?"iigs.pages":"iie.pages").begin]=9;
        refused=false;try { parse(bad); } catch(const std::exception &) {refused=true;}
        check(refused,"invalid page region must fail");
        // Versions and block presence are explicit changes, even with equal bytes.
        auto version=a;version.sections.at("device.testdev").version=2;
        check(!compare(a,version).equal,"changed device version must not report equality");
        auto absent=a;absent.sections.erase("device.testdev");
        check(!compare(a,absent).equal,"missing device block must not report equality");
    }
    bool refused=false;try { compare(parse(fixture()),parse(fixture(true))); }
    catch(const std::exception &) {refused=true;}
    check(refused,"IIe/IIgs geometry mismatch must fail");
    std::cout<<"snapshot diff: PASS "<<checks<<" checks\n";
}
int main(int argc,char **argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--selftest") {selftest();return 0;}
        if(argc==3 && std::string(argv[1])=="--describe") {std::cout<<describe(read(argv[2]))<<"\n";return 0;}
        if(argc<3 || argc>5) throw std::runtime_error("usage: snapdiff A.snap B.snap [report.json] [--expect-different]");
        bool expect_different=false;std::string out;
        for(int i=3;i<argc;i++) {
            if(std::string(argv[i])=="--expect-different")expect_different=true;
            else if(out.empty())out=argv[i];else throw std::runtime_error("extra argument");
        }
        auto report=compare(read(argv[1]),read(argv[2]));
        if(out.empty())std::cout<<report.json()<<"\n";
        else write_report(argv[1],argv[2],out,report);
        return report.equal==expect_different?1:0;
    } catch(const std::exception &e) {std::cerr<<"snapshot diff: FAIL "<<e.what()<<"\n";return 2;}
}
