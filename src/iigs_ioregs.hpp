#pragma once
// ============================================================================
// iigs_ioregs.hpp -- names for the Apple II / IIgs I/O page ($C000-$C0FF).
//
// WHY: a rail that reports "469,605 stores to device space" has told the
// reader a number and withheld the one fact that would let them read it. The
// MEMVU campaign spent three rails and an hour learning that the number was
// one register, $C03D, the sound GLU data port, written by the ROM. The name
// was known to everyone but the instrument. Every rail that prints an I/O
// address should print its name beside it, and say "unnamed" when it has none
// rather than printing a bare address that looks the same as a named one.
//
// Names follow the Apple IIgs Hardware Reference / Firmware Reference and the
// Apple IIe Technical Reference. Where the same location has a read-side and a
// write-side meaning the write-side (soft-switch SET) name is used, since the
// visibility rails are about stores. Unknown or reserved locations return
// nullptr -- never a guess.
// ============================================================================
#include <cstdint>

inline const char *iigs_io_reg_name(uint16_t addr) {
    switch (addr & 0xFFFF) {
    case 0xC000: return "KBD/80STOREOFF";
    case 0xC001: return "80STOREON";
    case 0xC002: return "RDMAINRAM";
    case 0xC003: return "RDCARDRAM";
    case 0xC004: return "WRMAINRAM";
    case 0xC005: return "WRCARDRAM";
    case 0xC006: return "SETSLOTCXROM";
    case 0xC007: return "SETINTCXROM";
    case 0xC008: return "SETSTDZP";
    case 0xC009: return "SETALTZP";
    case 0xC00A: return "SETINTC3ROM";
    case 0xC00B: return "SETSLOTC3ROM";
    case 0xC00C: return "CLR80VID";
    case 0xC00D: return "SET80VID";
    case 0xC00E: return "CLRALTCHAR";
    case 0xC00F: return "SETALTCHAR";
    case 0xC010: return "KBDSTRB";
    case 0xC011: return "RDLCBNK2";
    case 0xC012: return "RDLCRAM";
    case 0xC013: return "RDRAMRD";
    case 0xC014: return "RDRAMWRT";
    case 0xC015: return "RDCXROM";
    case 0xC016: return "RDALTZP";
    case 0xC017: return "RDC3ROM";
    case 0xC018: return "RD80STORE";
    case 0xC019: return "RDVBL";
    case 0xC01A: return "RDTEXT";
    case 0xC01B: return "RDMIXED";
    case 0xC01C: return "RDPAGE2";
    case 0xC01D: return "RDHIRES";
    case 0xC01E: return "ALTCHARSET";
    case 0xC01F: return "RD80VID";
    case 0xC021: return "MONOCOLOR";
    case 0xC022: return "TBCOLOR";
    case 0xC023: return "VGCINT";
    case 0xC024: return "MOUSEDATA";
    case 0xC025: return "KEYMODREG";
    case 0xC026: return "DATAREG";
    case 0xC027: return "KMSTATUS";
    case 0xC028: return "ROMBANK";
    case 0xC029: return "NEWVIDEO";
    case 0xC02B: return "LANGSEL";
    case 0xC02C: return "CHARROM";
    case 0xC02D: return "SLTROMSEL";
    case 0xC02E: return "VERTCNT";
    case 0xC02F: return "HORIZCNT";
    case 0xC030: return "SPKR";
    case 0xC031: return "DISKREG";
    case 0xC032: return "SCANINT";
    case 0xC033: return "CLOCKDATA";
    case 0xC034: return "CLOCKCTL";
    case 0xC035: return "SHADOW";
    case 0xC036: return "CYAREG";
    case 0xC037: return "DMAREG";
    case 0xC038: return "SCCBREG";
    case 0xC039: return "SCCAREG";
    case 0xC03A: return "SCCBDATA";
    case 0xC03B: return "SCCADATA";
    case 0xC03C: return "SOUNDCTL";
    case 0xC03D: return "SOUNDDATA";
    case 0xC03E: return "SOUNDADRL";
    case 0xC03F: return "SOUNDADRH";
    case 0xC041: return "INTEN";
    case 0xC044: return "MMDELTAX";
    case 0xC045: return "MMDELTAY";
    case 0xC046: return "DIAGTYPE";
    case 0xC047: return "CLRVBLINT";
    case 0xC048: return "CLRXYINT";
    case 0xC050: return "TXTCLR";
    case 0xC051: return "TXTSET";
    case 0xC052: return "MIXCLR";
    case 0xC053: return "MIXSET";
    case 0xC054: return "TXTPAGE1";
    case 0xC055: return "TXTPAGE2";
    case 0xC056: return "LORES";
    case 0xC057: return "HIRES";
    case 0xC058: return "SETAN0";
    case 0xC059: return "CLRAN0";
    case 0xC05A: return "SETAN1";
    case 0xC05B: return "CLRAN1";
    case 0xC05C: return "SETAN2";
    case 0xC05D: return "CLRAN2";
    case 0xC05E: return "SETAN3/DHIRESON";
    case 0xC05F: return "CLRAN3/DHIRESOFF";
    case 0xC060: return "BUTN3/CASSIN";
    case 0xC061: return "BUTN0/OPENAPPLE";
    case 0xC062: return "BUTN1/CLOSEDAPPLE";
    case 0xC063: return "BUTN2";
    case 0xC064: return "PADDL0";
    case 0xC065: return "PADDL1";
    case 0xC066: return "PADDL2";
    case 0xC067: return "PADDL3";
    case 0xC068: return "STATEREG";
    case 0xC070: return "PTRIG";
    case 0xC073: return "BANKSEL";
    case 0xC07E: return "IOUDIS";
    case 0xC07F: return "RDDHIRES";
    default: break;
    }
    if (addr >= 0xC080 && addr <= 0xC08F) return "LANGCARD";
    if (addr >= 0xC0E0 && addr <= 0xC0EF) return "IWM";
    if (addr >= 0xC090 && addr <= 0xC0FF) return "SLOT-IO";
    return nullptr;
}

// "$E1:C03D (SOUNDDATA)" or "$E1:C0F3 (unnamed)" -- never a bare address.
inline const char *iigs_io_reg_name_or(uint16_t addr) {
    const char *n = iigs_io_reg_name(addr);
    return n ? n : "unnamed";
}
