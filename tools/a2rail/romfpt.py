#!/usr/bin/env python3
"""romfpt.py -- toolbox function entry points, read out of an Apple IIgs ROM.

WHY THIS EXISTS

BLAME located the Dealer Demo's sound writer as granule $FF/4600, correctly,
and the name-by-default seed could only call it "(unnamed)": documented entry
points cover the Monitor, not the toolbox. The ROM itself knows better. The
Tool Locator dispatches every call through the system Tool Pointer Table --
the address it reads from $E1/03C0 at run time; $FE/012F in ROM 01 -- a count
followed by one long per tool set, each the address of that set's Function
Pointer Table: a count, then one long per function, function N at FPT+4N.
Each entry is the function's address MINUS ONE: the dispatcher pushes it and
executes RTL, which lands one byte later (the first version of this took the
entry as the address, and every name was one byte early -- caught when a trap
set on an entry never fired). Everything below $FE0000 is a RAM tool set and
is skipped. So the ROM yields "tool set T, function F, begins at $BB/AAAA",
and the Toolbox Reference names the functions.

Verified 2026-09-02 against dispatcher traps: the Dealer Demo's DOC-RAM
writer at $FF/46ED resolves to SoundTools.FFStartSound, and the call word the
trap saw was $0E08. (A PC between two entries is named after the PRECEDING
entry -- right for a function body, wrong for a shared helper; the boot's DOC
clear at $FF/4149 is such a helper, called from SoundBootInit. A dispatcher
trap is the authority for helpers.)

USAGE
    python romfpt.py <main.rom> [--tpt HHHHHH] [--e1 <bank-E1 dump>] [--sym out.sym]

    --tpt   the TPT address (default $FE012F, ROM 01)
    --e1    a 64 KB dump of bank $E1 from a booted machine; the TPT address is
            the long at $03C0 (works for any ROM version)
    --sym   write "HHHHHH name" lines, the format A2GSPU_ROM_SYMBOLS and the
            names-by-default loader read
    --names write the tool-set and function names as "T F name" lines (F=0 is
            the set's name): the emulator reads this to name a RAM-resident
            tool set's functions from the LIVE tool pointer table, since those
            load wherever the Memory Manager puts them on each boot

Names are given for the sets whose call lists are transcribed here (Tool
Locator, Memory Manager, Misc Tools, Event Manager, Sound); every other
function is Tool<NN>.fn<MM>, which is still a true statement about where a
toolbox function begins.
"""
import struct, sys, os

TOOLS = {1: 'ToolLocator', 2: 'MemoryMgr', 3: 'MiscTools', 4: 'QuickDrawII', 5: 'DeskMgr', 6: 'EventMgr',
         7: 'Scheduler', 8: 'SoundTools', 9: 'ADBTools', 10: 'SANE', 11: 'IntegerMath', 12: 'TextTools',
         13: 'RAMDisk', 14: 'WindowMgr', 15: 'MenuMgr', 16: 'ControlMgr', 17: 'SystemLoader', 18: 'QDAux',
         19: 'PrintMgr', 20: 'LineEdit', 21: 'DialogMgr', 22: 'ScrapMgr', 23: 'StandardFile', 24: 'DiskII',
         25: 'NoteSynth', 26: 'NoteSeq', 27: 'FontMgr', 28: 'ListMgr', 29: 'ACE', 30: 'ResourceMgr',
         32: 'MIDI', 33: 'VideoOverlay', 34: 'TextEdit', 35: 'MIDISynth', 37: 'AnimationTools'}

HOUSEKEEPING = {1: 'BootInit', 2: 'StartUp', 3: 'ShutDown', 4: 'Version', 5: 'Reset', 6: 'Status'}

NAMES = {
    1: {9: 'GetTSPtr', 10: 'SetTSPtr', 11: 'GetFuncPtr', 12: 'GetWAP', 13: 'SetWAP', 14: 'LoadTools',
        15: 'LoadOneTool', 16: 'UnloadOneTool', 17: 'TLMountVolume', 18: 'TLTextMountVolume',
        19: 'SaveTextState', 20: 'RestoreTextState', 21: 'MessageCenter', 22: 'SetDefaultTPT',
        23: 'MessageByName', 24: 'StartUpTools', 25: 'ShutDownTools', 26: 'GetMsgHandle',
        27: 'AcceptRequests', 28: 'SendRequest'},
    2: {9: 'NewHandle', 10: 'ReAllocHandle', 11: 'RestoreHandle', 12: 'AddToOOMQueue', 13: 'RemoveFromOOMQueue',
        16: 'DisposeHandle', 17: 'DisposeAll', 18: 'PurgeHandle', 19: 'PurgeAll', 24: 'GetHandleSize',
        25: 'SetHandleSize', 26: 'FindHandle', 27: 'FreeMem', 28: 'MaxBlock', 29: 'TotalMem', 30: 'CheckHandle',
        31: 'CompactMem', 32: 'HLock', 33: 'HLockAll', 34: 'HUnlock', 35: 'HUnlockAll', 36: 'SetPurge',
        37: 'SetPurgeAll', 40: 'PtrToHand', 41: 'HandToPtr', 42: 'HandToHand', 43: 'BlockMove',
        47: 'RealFreeMem', 48: 'SetHandleID'},
    3: {9: 'WriteBRam', 10: 'ReadBRam', 11: 'WriteBParam', 12: 'ReadBParam', 13: 'ReadTimeHex', 14: 'WriteTimeHex',
        15: 'ReadAsciiTime', 16: 'SetVector', 17: 'GetVector', 18: 'SetHeartBeat', 19: 'DelHeartBeat',
        20: 'ClrHeartBeat', 21: 'SysFailMgr', 22: 'GetAddr', 23: 'ReadMouse', 24: 'InitMouse', 25: 'SetMouse',
        26: 'HomeMouse', 27: 'ClearMouse', 28: 'ClampMouse', 29: 'GetMouseClamp', 30: 'PosMouse', 31: 'ServeMouse',
        32: 'GetNewID', 33: 'DeleteID', 34: 'StatusID', 35: 'IntSource', 36: 'FWEntry', 37: 'GetTick',
        38: 'PackBytes', 39: 'UnPackBytes', 40: 'Munger', 41: 'GetIRQEnable', 42: 'SetAbsClamp', 43: 'GetAbsClamp',
        44: 'SysBeep', 46: 'AddToQueue', 47: 'DeleteFromQueue', 48: 'SetInterruptState', 49: 'GetInterruptState',
        50: 'GetIntStateRecSize', 51: 'ReadMouse2', 52: 'GetCodeResConverter', 53: 'GetROMResource',
        54: 'ReleaseROMResource', 55: 'ConvSeconds', 57: 'SysBeep2', 58: 'VersionString', 59: 'WaitUntil',
        60: 'StringToText', 61: 'ShowBootInfo', 62: 'ScanDevices', 63: 'AlertMessage', 64: 'DoSysPrefs'},
    6: {9: 'DoWindows', 10: 'GetNextEvent', 11: 'EventAvail', 12: 'GetMouse', 13: 'Button', 14: 'StillDown',
        15: 'WaitMouseUp', 16: 'TickCount', 17: 'GetDblTime', 18: 'GetCaretTime', 19: 'SetSwitch', 20: 'PostEvent',
        21: 'FlushEvents', 22: 'GetOSEvent', 23: 'OSEventAvail', 24: 'SetEventMask', 25: 'FakeMouse',
        26: 'SetAutoKeyLimit', 27: 'GetKeyTranslation', 28: 'SetKeyTranslation'},
    8: {9: 'WriteRamBlock', 10: 'ReadRamBlock', 11: 'GetTableAddress', 12: 'GetSoundVolume', 13: 'SetSoundVolume',
        14: 'FFStartSound', 15: 'FFStopSound', 16: 'FFSoundStatus', 17: 'FFGeneratorStatus', 18: 'SetSoundMIRQV',
        19: 'SetUserSoundIRQV', 20: 'FFSoundDoneStatus', 21: 'FFSetUpSound', 22: 'FFStartPlaying', 23: 'SetDOCReg',
        24: 'ReadDOCReg'},
}
PREFIX = {1: 'TL', 2: 'MM', 3: 'MT', 4: 'QD', 5: 'Desk', 6: 'EM', 7: 'Sch', 8: 'Sound', 9: 'ADB', 10: 'SANE',
          11: 'IM', 12: 'TT', 13: 'RD'}


def fname(tool, f):
    if f in HOUSEKEEPING:
        return PREFIX.get(tool, 'T%d' % tool) + HOUSEKEEPING[f]
    return NAMES.get(tool, {}).get(f, 'fn%02X' % f)


def write_names(out):
    with open(out, 'w', encoding='ascii') as fh:
        fh.write('# Apple IIgs toolbox names: "T F name" (T tool set, F function; F=0 names the set), written by\n')
        fh.write('# tools/a2rail/romfpt.py --names. Read by the emulator to name RAM-resident tool sets from the\n')
        fh.write('# live tool pointer table. A function not listed is named fnXX; a set not listed, ToolNN.\n')
        n = 0
        for t in sorted(TOOLS):
            fh.write('%d 0 %s\n' % (t, TOOLS[t])); n += 1
        for t in sorted(set(list(NAMES) + list(PREFIX))):
            for f in sorted(HOUSEKEEPING):
                fh.write('%d %d %s\n' % (t, f, fname(t, f))); n += 1
            for f in sorted(NAMES.get(t, {})):
                fh.write('%d %d %s\n' % (t, f, NAMES[t][f])); n += 1
    print('wrote %d names to %s' % (n, out))


def main(argv):
    if len(argv) < 2 or argv[1] in ('-h', '--help'):
        print(__doc__); return 2
    if argv[1] == '--names':
        write_names(argv[2]); return 0
    rom = open(argv[1], 'rb').read()
    n = len(rom); base = 0x1000000 - n
    def L(a):
        o = a - base
        return struct.unpack('<I', rom[o:o + 4])[0] & 0xFFFFFF if 0 <= o <= n - 4 else None
    tpt = 0xFE012F
    if '--tpt' in argv: tpt = int(argv[argv.index('--tpt') + 1], 16)
    if '--e1' in argv:
        e1 = open(argv[argv.index('--e1') + 1], 'rb').read()
        tpt = struct.unpack('<I', e1[0x3C0:0x3C4])[0] & 0xFFFFFF
    count = L(tpt)
    if count is None or not (1 <= count <= 64):
        print('no tool pointer table at $%06X (count=%s); is this a IIgs ROM, and is --tpt right?' % (tpt, count)); return 1
    print('system TPT at $%06X: %d tool sets' % (tpt, count))
    syms = []
    for tool in range(1, count + 1):
        fpt = L(tpt + 4 * tool)
        name = TOOLS.get(tool, 'Tool%02d' % tool)
        if fpt is None or not (base <= fpt < 0x1000000):
            print('  %2d %-13s not in ROM' % (tool, name)); continue
        fc = L(fpt)
        if fc is None or fc > 300 or fc == 0:
            print('  %2d %-13s FPT $%06X: no ROM functions' % (tool, name, fpt)); continue
        got = 0
        for f in range(1, fc + 1):
            e = L(fpt + 4 * f)
            if e and base <= e < 0x1000000:
                syms.append((e + 1, '%s.%s' % (name, fname(tool, f)))); got += 1   # entry = address - 1 (RTL lands at +1)
        print('  %2d %-13s FPT $%06X: %d functions, %d in ROM' % (tool, name, fpt, fc, got))
    syms = sorted(set(syms))
    if '--sym' in argv:
        out = argv[argv.index('--sym') + 1]
        with open(out, 'w', encoding='ascii') as fh:
            fh.write('# Apple IIgs ROM toolbox function entry points, read from the ROM image by tools/a2rail/romfpt.py\n')
            fh.write('# source TPT $%06X, %d tool sets, %d entries. A PC between two entries is named after the\n' % (tpt, count, len(syms)))
            fh.write('# preceding entry: right for a function body, wrong for a shared helper (use a dispatcher trap).\n')
            fh.write('# Each address is the table entry plus one: the entry is the function minus one, for the RTL.\n')
            for e, nm in syms:
                fh.write('%06X %s\n' % (e, nm))
        print('wrote %d symbols to %s' % (len(syms), out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
