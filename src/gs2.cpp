/*
 *   Copyright (c) 2025-2026 Jawaid Bazyar

 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.

 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.

 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <cstdio>
#include <unistd.h>
#include <time.h>
#include <getopt.h>
#include <regex>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include "gs2.hpp"
#include "platform-specific/menu.h"
#include "Module_ID.hpp"
#include "paths.hpp"
#include "cpu.hpp"
#include "display/display.hpp"
#include "devices/speaker/speaker.hpp"
#include "platforms.hpp"
#include "util/dialog.hpp"
#include "util/mount.hpp"
#include "ui/OSD.hpp"
#include "systemconfig.hpp"
#include "slots.hpp"
#include "videosystem.hpp"
#include "debugger/debugwindow.hpp"
#include "computer.hpp"
#include "mmus/mmu_ii.hpp"
#include "mmus/mmu_iie.hpp"
#include "mmus/mmu_iigs.hpp"
#include "house_fnv.hpp"
#include "bus_trace.hpp"
#include "mmu_state_trace.hpp"
#include "iigs_video_summary.hpp"
#include "iigs_toolbox.hpp"
#include "iigs_diag.hpp"
#include "devices/slot_bus/slot_bus.hpp"
#include "util/EventTimer.hpp"
#include "ui/SelectSystem.hpp"
#include "ui/MainAtlas.hpp"
#include "cpus/cpu_implementations.hpp"
#include "NClock.hpp"
#include "opcodes.hpp"
#include "version.h"
#include "util/Metrics.hpp"
#include "util/DebugHandlerIDs.hpp"
#include "util/printf_helper.hpp"

/**
 * References: 
 * Apple Machine Language: Don Inman, Kurt Inman
 * https://www.righto.com/2012/12/the-6502-overflow-flag-explained.html?m=1
 * https://www.masswerk.at/6502/6502_instruction_set.html#USBC
 * 
 */

/**
 * gssquared
 * 
 * Apple II/+/e/c/GS Emulator Extraordinaire
 * 
 * Main component Goals:
 *  - 6502 CPU (nmos) emulation
 *  - 65c02 CPU (cmos) emulation
 *  - 65sc816 CPU (8 and 16-bit) emulation
 *  - Display Emulation
 *     - Text - 40 / 80 col
 *     - Lores - 40 x 40, 40 x 48, 80 x 40, 80 x 48
 *     - Hires - 280x192 etc.
 *  - Disk I/O
 *   - 5.25 Emulation
 *   - 3.5 Emulation (SmartPort)
 *   - SCSI Card Emulation
 *  - Memory management emulate - a proposed MMU to allow multiple virtual CPUs to each have their own 16M address space
 * User should be able to select the Apple variant: 
 *  - Apple II
 *  - Apple II+
 *  - Apple IIe
 *  - Apple IIe Enhanced
 *  - Apple IIc
 *  - Apple IIc+
 *  - Apple IIGS
 * and edition of ROM.
 */

/** Globals we haven't dealt properly with yet. */
OSD *osd = nullptr;

// Defined in OSD.cpp — used here where osd is accessible for menu-triggered disk toggle
void handle_disk_toggle(computer_t *computer, OSD *osd, storage_key_t key);

void handle_single_event(computer_t *computer, cpu_state *cpu, SDL_Event &event) {
    // Handle disk toggle from menu directly here where osd is accessible
    if (event.type == gs2_app_values.menu_event_type && event.user.code == MENU_DISK_TOGGLE) {
        storage_key_t key((uint64_t)(uintptr_t)event.user.data1);
        handle_disk_toggle(computer, osd, key);
        return;
    }
    // check for system "pre" events
    if (computer->sys_event->dispatch(event)) {
        return;
    }
    if (computer->debug_window->handle_event(event)) { // ignores event if not for debug window
        return;
    }
    if (!osd->event(event)) { // if osd doesn't handle it..
        computer->dispatch->dispatch(event); // they say call "once per frame"
    }
}

/*
 * In the SDL3 App Callbacks model, SDL delivers events via SDL_AppEvent()
 * which calls handle_single_event() directly. frame_event() is still called
 * from run_one_frame() to maintain the MEASURE timing, but no longer polls
 * events itself. osd->update() is called from SDL_AppIterate().
 */
void frame_event(computer_t *computer, cpu_state *cpu) {
    // Events are now dispatched by SDL_AppEvent; nothing to poll here.
    // osd->update() is called from SDL_AppIterate before run_one_frame().
}

void frame_appevent(computer_t *computer, cpu_state *cpu) {
    Event *event = computer->event_queue->getNextEvent();
    if (event) {
        switch (event->getEventType()) {
            case EVENT_PLAY_SOUNDEFFECT:
                computer->sound_effect->play(event->getEventData());
                break;
            case EVENT_REFOCUS:
                computer->video_system->raise();
                break;
            case EVENT_QUIT:
                computer->cpu->halt = HLT_USER;
                break;
            /* case EVENT_MODAL_SHOW:
                osd->show_diskii_modal(event->getEventKey(), event->getEventData());
                break; */
            /* case EVENT_MODAL_CLICK:
                {
                    storage_key_t key;
                    key.key = event->getEventKey();

                    uint64_t data = event->getEventData();
                    printf("EVENT_MODAL_CLICK: %llu %llu\n", u64_t(key), u64_t(data));
                    if (data == 1) {
                        // save and unmount.
                        computer->mounts->unmount_media(key, SAVE_AND_UNMOUNT);
                    } else if (data == 2) {
                        // save as - need to open file dialog, get new filename, change media filename, then unmount.
                    } else if (data == 3) {
                        // discard
                        computer->mounts->unmount_media(key, DISCARD);
                    } else if (data == 4) {
                        // cancel
                        // Do nothing!
                    }
                    osd->close_diskii_modal(key, data);
                }
                break; */
            case EVENT_SHOW_MESSAGE:
                osd->set_heads_up_message((const char *)event->getEventData(), 512);
                break;
         
        }
        delete event; // processed, we can now delete it.
    }
}

/*
 * Update window
 */
void frame_video_update(computer_t *computer, bool force_full_frame = false) {

    computer->video_system->update_display(force_full_frame);    
    osd->render();
    computer->debug_window->render();
    computer->video_system->present();
}

void frame_sleep(computer_t *computer, uint64_t last_cycle_time, uint64_t ns_per_frame)
    /* uint64_t frame_count) */ {
    if (gs2_app_values.modal_tracking) return;

    uint64_t wakeup_time = last_cycle_time + ns_per_frame; /*  + (frame_count & 1); */ // even frames have 16688154, odd frames have 16688154 + 1

    // sleep out the rest of this frame.
    uint64_t sleep_loops = 0;
    uint64_t current_time = SDL_GetTicksNS();
    if (current_time > wakeup_time) {
        computer->clock_slip++;
        // TODO: log clock slip for later display.
        //printf("Clock slip: event_time: %10llu, audio_time: %10llu, display_time: %10llu, app_event_time: %10llu, total: %10llu\n", event_time, audio_time, display_time, app_event_time, event_time + audio_time + display_time + app_event_time);
    } else {
        if (gs2_app_values.sleep_mode) { // sleep most of it, but more aggressively sneak up on target than SDL_DelayPrecise does itself
            SDL_DelayPrecise((wakeup_time - SDL_GetTicksNS())*0.95);
        }
        // busy wait sync cycle time
        do {
            sleep_loops++;
        } while (SDL_GetTicksNS() < wakeup_time);

    }
}

#if 0
DebugFormatter *debug_clock(computer_t *computer) {
    DebugFormatter *f = new DebugFormatter();
    f->addLine("Clock Mode: %s", computer->clock->get_clock_mode_name(computer->clock->get_clock_mode()));
    f->addLine("CPU Slow Mode: %d", computer->clock->get_slow_mode());
    f->addLine("CPU Expected Rate: %d", computer->clock->get_hz_rate());
    f->addLine("CPU eMHZ: %12.8f, FPS: %12.8f", computer->e_mhz, computer->fps);
    f->addLine("CPU Cycle: %12llu", computer->clock->get_cycles());
    f->addLine("Vid Cycle: %12llu", computer->clock->get_vid_cycles());
    f->addLine("14M Cycle: %12llu", computer->clock->get_c14m());

    return f;
}
#endif

void register_clock_debug(computer_t *computer) {

    computer->register_debug_display_handler(
        "clock",
        DH_CLOCK, // unique ID for this, need to have in a header.
        [computer]() -> DebugFormatter * {
            return computer->clock->debug();
        }
    );

}


DebugFormatter *debug_mmu_iigs(MMU_IIgs *mmu_iigs) {
    DebugFormatter *f = new DebugFormatter();
    mmu_iigs->debug_dump(f);
    return f;
}

/*
Initialize emulation state before the first frame.
Called from transition_to_emulation() when a system is selected.
*/
void run_cpus_init(computer_t *computer) {
    computer->last_cycle_time = SDL_GetTicksNS();
    computer->last_start_frame_c14m = 0;
    computer->cached_speaker_state = computer->get_module_state(MODULE_SPEAKER);
    computer->cached_display_state = computer->get_module_state(MODULE_DISPLAY);
}

/*
Execute one frame of emulation. Returns true if emulation should continue,
false if the user requested a halt.
*/
bool run_one_frame(computer_t *computer) {
    cpu_state *cpu = computer->cpu;
    NClock *clock = computer->clock;
    speaker_state_t *speaker_state = (speaker_state_t *)computer->cached_speaker_state;
    display_state_t *ds = (display_state_t *)computer->cached_display_state;

    if (cpu->halt == HLT_USER) { // top of frame.
        return false;
    }

    uint64_t c14M_per_frame = clock->get_c14m_per_frame();

    if (computer->execution_mode == EXEC_PAUSED) {
        return true;
    }

    if (computer->speed_shift) {
        computer->speed_shift = false;

        if (clock->get_clock_mode() == CLOCK_FREE_RUN) {
            speaker_state->sp->reset(clock->get_frame_start_c14M());
            int x = ds->video_scanner->get_frame_scan()->get_count();
            if (x > 100) {
                printf("Video scanner has %d samples @ speed shift [%d,%d]\n", x, ds->video_scanner->get_hcount(), ds->video_scanner->get_vcount());
            }
        } else {
            int x = ds->video_scanner->get_frame_scan()->get_count();
            if (x > 100) {
                printf("Video scanner has %d samples @ speed shift [%d,%d]\n", x, ds->video_scanner->get_hcount(), ds->video_scanner->get_vcount());
            }
        }

        clock->set_clock_mode(computer->speed_new);

        if (computer->speed_new == CLOCK_FREE_RUN) {
            assert(true);
        }
        display_update_video_scanner(ds);
    }

    if (computer->execution_mode == EXEC_STEP_INTO) {

        /* This will run about 60fps, primarily waiting on user input in the debugger window. */
        while (computer->instructions_left) {
            if (computer->event_timer->isEventPassed(clock->get_c14m())) {
                computer->event_timer->processEvents(clock->get_c14m());
            }
            if (computer->vid_event_timer->isEventPassed(clock->get_vid_cycles())) {
                computer->vid_event_timer->processEvents(clock->get_vid_cycles());
            }
            if (computer->cpu_event_timer->isEventPassed(clock->get_cycles())) {
                computer->cpu_event_timer->processEvents(clock->get_cycles());
            }
            (cpu->cpun->execute_next)(cpu);
            computer->instructions_left--;
        }

        MEASURE(computer->event_times, frame_event(computer, cpu));

        /* Emit Audio Frame */
        // disable audio in step mode.
        
        /* Process Internal Event Queue */
        MEASURE(computer->app_event_times, frame_appevent(computer, cpu));

        /* Execute Device Frames - 60 fps */
        MEASURE(computer->device_times, computer->device_frame_dispatcher->dispatch());

        /* Emit Video Frame */
        // set flag to force full frame draw instead of cycle based draw.
        MEASURE(computer->display_times, frame_video_update(computer, true));

        // if we're in stepwise mode, we should increment these only if we got to end of frame.
        if (clock->get_c14m() >= clock->get_frame_end_c14M()) {
            if (clock->get_video_scanner() != nullptr) {
                computer->video_system->update_display(false); // set flag to false to draw with cycle based, and, gobble up frame data.
            }

            // update frame counters.
            clock->next_frame();
            // set next frame cycle time (used for mouse) is at top of frame.
            computer->set_frame_start_cycle();
        }

        // sleep for 1/60th second ish, without updating frame counts etc.
        uint64_t wakeup_time = computer->last_cycle_time + 16667000;
        SDL_DelayPrecise(wakeup_time - SDL_GetTicksNS());
        
    } else if ((computer->execution_mode == EXEC_NORMAL) && (clock->get_clock_mode() != CLOCK_FREE_RUN)) {

        computer->set_frame_start_cycle();

        if (computer->debug_window->window_open) {
            while (clock->get_c14m() < clock->get_frame_end_c14M()) { // 1/60th second.
                if (computer->event_timer->isEventPassed(clock->get_c14m())) {
                    computer->event_timer->processEvents(clock->get_c14m());
                }
                if (computer->vid_event_timer->isEventPassed(clock->get_vid_cycles())) {
                    computer->vid_event_timer->processEvents(clock->get_vid_cycles());
                }
                if (computer->cpu_event_timer->isEventPassed(clock->get_cycles())) {
                    computer->cpu_event_timer->processEvents(clock->get_cycles());
                }
                // do the pre check.
                if (computer->debug_window->check_pre_breakpoint(cpu)) {
                    computer->execution_mode = EXEC_STEP_INTO;
                    computer->instructions_left = 0;
                    break;
                }

                (cpu->cpun->execute_next)(cpu);
                
                // Do the post check.
                if (computer->debug_window->check_post_breakpoint(&cpu->trace_entry)) {
                    computer->execution_mode = EXEC_STEP_INTO;
                    computer->instructions_left = 0;
                    break;
                }
                if (cpu->trace_entry.opcode == 0x00) { // catch a BRK and stop execution.
                    computer->execution_mode = EXEC_STEP_INTO;
                    computer->instructions_left = 0;
                    break;
                }
            
            }
        } else { // skip all debug checks if debug window is not open - this may seem repetitious but it saves all kinds of cycles where every cycle counts 
            while (clock->get_c14m() < clock->get_frame_end_c14M()) {
                if (computer->event_timer->isEventPassed(clock->get_c14m())) {
                    computer->event_timer->processEvents(clock->get_c14m());
                }
                if (computer->vid_event_timer->isEventPassed(clock->get_vid_cycles())) {
                    computer->vid_event_timer->processEvents(clock->get_vid_cycles());
                }
                if (computer->cpu_event_timer->isEventPassed(clock->get_cycles())) {
                    computer->cpu_event_timer->processEvents(clock->get_cycles());
                }
                (cpu->cpun->execute_next)(cpu);
            }
        }

        /* Process Events */
        MEASURE(computer->event_times, frame_event(computer, cpu));

        /* Process Internal Event Queue */
        MEASURE(computer->app_event_times, frame_appevent(computer, cpu));

        /* Execute Device Frames - 60 fps */
        MEASURE(computer->device_times, computer->device_frame_dispatcher->dispatch());

        /* Emit Video Frame */
        if (computer->execution_mode != EXEC_STEP_INTO) {
            MEASURE(computer->display_times, frame_video_update(computer));
        }
        
        // calculate what sleep-until time should be.
        uint64_t frame_length_ns = (computer->frame_count & 1) ? clock->get_us_per_frame_odd() : clock->get_us_per_frame_even();
        
        // update frame status; calculate stats; move these variables into computer;
        computer->frame_status_update();

        // if we completed a full frame, update the frame counters. otherwise we were interrupted by breakpoint etc 
        if (clock->get_c14m() >= clock->get_frame_end_c14M()) {
            clock->next_frame();

            computer->last_start_frame_c14m = clock->get_frame_start_c14M();
        }

        uint64_t time_to_sleep = frame_length_ns - (SDL_GetTicksNS() - computer->last_cycle_time);
        computer->set_idle_percent(((float)time_to_sleep / (float)frame_length_ns) * 100.0f);

        frame_sleep(computer, computer->last_cycle_time, frame_length_ns);
        computer->last_cycle_time = SDL_GetTicksNS(); 

    } else { // Ludicrous Speed!

        // TODO: how to handle VBL timing here. estimate it based on realtime?
        computer->set_frame_start_cycle(); // todo: unsure if this is right..
        uint64_t frame_length_ns = (computer->frame_count & 1) ? clock->get_us_per_frame_odd() : clock->get_us_per_frame_even();
        uint64_t next_frame_time = computer->last_cycle_time + frame_length_ns;

        computer->last_start_frame_c14m = clock->get_frame_start_c14M();
        
        if (computer->debug_window->window_open) {
            while (SDL_GetTicksNS() < next_frame_time) { // run emulated frame, but of course we don't sleep in this loop so we'll Go Fast.
                if (computer->event_timer->isEventPassed(clock->get_c14m())) {
                    computer->event_timer->processEvents(clock->get_c14m());
                }
                if (computer->vid_event_timer->isEventPassed(clock->get_vid_cycles())) {
                    computer->vid_event_timer->processEvents(clock->get_vid_cycles());
                }
                if (computer->cpu_event_timer->isEventPassed(clock->get_cycles())) {
                    computer->cpu_event_timer->processEvents(clock->get_cycles());
                }
                if (computer->debug_window->check_pre_breakpoint(cpu)) {
                    computer->execution_mode = EXEC_STEP_INTO;
                    computer->instructions_left = 0;
                    break;
                }

                (cpu->cpun->execute_next)(cpu);
                
                if (computer->debug_window->check_post_breakpoint(&cpu->trace_entry)) {
                    computer->execution_mode = EXEC_STEP_INTO;
                    computer->instructions_left = 0;
                    break;
                }
                if (cpu->trace_entry.opcode == 0x00) { // catch a BRK and stop execution.
                    computer->execution_mode = EXEC_STEP_INTO;
                    computer->instructions_left = 0;
                    break;
                }
            
            }
        } else { // skip all debug checks if debug window is not open - this may seem repetitious but it saves all kinds of cycles where every cycle counts (GO FAST MODE)
            while (SDL_GetTicksNS() < next_frame_time) { // run emulated frame, but of course we don't sleep in this loop so we'll Go Fast.
                if (computer->event_timer->isEventPassed(clock->get_c14m())) {
                    computer->event_timer->processEvents(clock->get_c14m());
                }
                if (computer->vid_event_timer->isEventPassed(clock->get_vid_cycles())) {
                    computer->vid_event_timer->processEvents(clock->get_vid_cycles());
                }
                if (computer->cpu_event_timer->isEventPassed(clock->get_cycles())) {
                    computer->cpu_event_timer->processEvents(clock->get_cycles());
                }
                (cpu->cpun->execute_next)(cpu);
            }
        }

        // this was roughly one video frame so let's pretend we went that many.
        clock->adjust_c14m(c14M_per_frame);

            /* Process Events */
            MEASURE(computer->event_times, frame_event(computer, cpu));
    
            /* Emit Audio Frame */
            // TODO: reevaluate disable audio output in ludicrous speed.

            /* Process Internal Event Queue */
            MEASURE(computer->app_event_times, frame_appevent(computer, cpu));
    
            /* Execute Device Frames - 60 fps */
            MEASURE(computer->device_times, computer->device_frame_dispatcher->dispatch());
    
            /* Emit Video Frame */
    
            MEASURE(computer->display_times, frame_video_update(computer, true));
    

        // update frame window counters.
        // this gets wildly out of sync because we're not actually executing this many cycles in the loop,
        // because we are basing loop on time. So, maybe loop should be based on cycles per below after all,
        // while just periodically doing the frame update stuff here.
        computer->last_cycle_time = SDL_GetTicksNS(); 
        
        // update frame status; calculate stats; move these variables into computer;
        computer->frame_status_update();

        clock->next_frame(); // TODO: now redundant to above.
        computer->last_start_frame_c14m = clock->get_frame_start_c14M();
    }

    return true;
}

/* ========================================================================
   App State and Phase Machine for SDL3 App Callbacks
   ======================================================================== */

gs2_app_t gs2_app_values;

enum AppPhase {
    PHASE_SYSTEM_SELECT,
    PHASE_EMULATION,
    PHASE_SHUTTING_DOWN,
};

/* This is "application state" as passed by SDL into the various AppCallbacks routines */
struct GS2AppState {
    AppPhase phase = PHASE_SYSTEM_SELECT;

    // Parsed from command line (persistent across system-select cycles)
    int platform_id = PLATFORM_APPLE_II_PLUS;
    std::vector<disk_mount_t> disks_to_mount;

    // True when the user gave -p PLATFORM and we skipped the system
    // selector at startup. In that mode, closing the emulator window
    // quits the app (rather than bouncing back to the selector) — the
    // user expressly asked for one specific machine and there's no
    // "back" to return to.
    bool auto_launched = false;

    // System selection
    SelectSystem *select_system = nullptr;
    AssetAtlas_t *aa = nullptr;

    // Emulation state
    computer_t *computer = nullptr;

    // MMU pointers tracked for cleanup
    MMU_II *mmu_ii = nullptr;
    MMU_IIe *mmu_iie = nullptr;
    MMU_IIgs *mmu_iigs = nullptr;

    // a2gspu headless-boot spike (env-gated; no behaviour change unless
    // A2GSPU_SPIKE_FRAMES is set). Runs N deterministic frames, dumps a
    // renderer-free $E1 oracle + a backbuffer screenshot, then exits.
    bool headless = false;
    int  spike_frames = 0;
};

/*
 * Configure the selected system and transition from system-select to emulation.
 * This is the code that was between select_system->select() and run_cpus() in old main().
 */
void transition_to_emulation(GS2AppState *state, int system_id) {
    computer_t *computer = state->computer;
    video_system_t *vs = computer->video_system;

    // Emulation manages its own timing, so turn off vsync.
    SDL_SetRenderVSync(vs->renderer, 0);

    SystemConfig_t *system_config = get_system_config(system_id);
    state->platform_id = system_config->platform_id;

    platform_info* platform = get_platform(state->platform_id);
    print_platform_info(platform);

    // TODO: This is a little disjointed. the clock abstraction should probably program all the things that need the clock.
    // the initial setting here is 1MHz, except for platform which has the right starting clock?
    //select_system_clock(system_config->clock_set);
    //computer->set_clock(&system_clock_mode_info[computer->speed_new]);
    //set_clock_mode(computer->cpu, platform->default_clock_mode);

    computer->cpu->set_processor(platform->cpu_type);
    // important to do this before setting up the rest of the computer.
    NClockII *nclock = NClockFactory::create_clock(platform->id, system_config->clock_set);
    computer->set_clock(nclock);
    getMenuInterface()->setComputer(computer);

    //computer->set_cpu(new cpu_state(platform->cpu_type));

    computer->set_platform(platform);
    computer->set_video_scanner(system_config->scanner_type);
    computer->set_system_id(system_id);
    
    // TODO: load platform roms - this info should get stored in the 'computer'
    rom_data *rd = load_platform_roms(platform, system_config->rom_dir);
    if (!rd) {
        system_failure("Failed to load platform roms, exiting.");
        return;
    }

    // we will ALWAYS have a 256 page map. because it's a 6502 and all is addressible in a II.
    // II can have 4k, 8k, 12k; or 16k, 32k, 48k.
    // II Plus can have 16k, 32K, or 48k RAM. 16K more BUT IN THE LANGUAGE CARD MODULE.
    // always 12k rom, but not necessarily always the same ROM.
    state->mmu_ii = nullptr;
    state->mmu_iie = nullptr;
    state->mmu_iigs = nullptr;

    switch (platform->mmu_type) {
        case MMU_MMU_II:
            state->mmu_ii = new MMU_II(256, 48*1024, (uint8_t *) rd->main_rom_data);
            computer->cpu->set_mmu(state->mmu_ii);
            computer->set_mmu(state->mmu_ii);
            computer->debug_window->set_mmu(state->mmu_ii);
            break;
        case MMU_MMU_IIE:
            state->mmu_iie = new MMU_IIe(256, 128*1024, (uint8_t *) rd->main_rom_data);
            computer->cpu->set_mmu(state->mmu_iie);
            computer->set_mmu(state->mmu_iie);
            computer->debug_window->set_mmu(state->mmu_iie);
            break;
        case MMU_MMU_IIGS: {
            // ROM size is data-driven, not hardcoded: 0x20000 (128KB ROM01) or
            // 0x40000 (256KB ROM03/ROM04). The //e-compat ROM window ($C000-$FFFF)
            // is always the last 16KB of the image, i.e. base = (romsize - 0x4000).
            // For a 128KB image this computes the original 0x1C000 (zero ROM01 change).
            size_t romsize = (size_t) rd->main_rom_file->size();
            state->mmu_iie = new MMU_IIe(256, 128*1024, /* (uint8_t *) */rd->main_rom_data + (romsize - 0x4000));
            state->mmu_iigs = new MMU_IIgs(256, 8*1024*1024, (uint32_t) romsize, /* (uint8_t *) */rd->main_rom_data, state->mmu_iie);
            state->mmu_iigs->init_map();
            computer->cpu->set_mmu(state->mmu_iigs); // cpu gets FPI
            computer->set_mmu(state->mmu_iie); // everything else gets the Mega II
            computer->debug_window->set_mmu(state->mmu_iigs);
            state->mmu_iigs->set_clock((NClockII *)nclock);

            break;
        }
        default:
            printf("Unknown MMU type: %d\n", platform->mmu_type);
            break;
    }
    // need to tell the MMU about our ROM somehow.
    // need a function in MMU to "reset page to default".
    computer->cpu->cpun = createCPU(platform->cpu_type, (NClock *)nclock);

    computer->cpu->core = computer->cpu->cpun.get(); // set the core. Probably need a better set cpu for cpu_state.
    //computer->cpu->core->set_clock(nclock);

    // Initialize the slot manager.
    //SlotManager_t *slot_manager = new SlotManager_t();


    //init_display_font(rd);


    // Iterate through Platform Devices and create/register/initialize the devices.
    for (int i = 0; platform->mb_devices[i] != DEVICE_ID_END; i++) {
        Device_t *device = get_device(platform->mb_devices[i]);
        if (device->power_on == nullptr) {
            printf("Device has no poweron, not found: %d", platform->mb_devices[i]);
            continue;
        }
        device->power_on(computer, SLOT_NONE);
    }

    // Iterate through SystemConfig Slot Devices and create/register/initialize the devices.
    for (int i = 0; i < NUM_SLOTS; i++) {
        device_id id = system_config->slot_devices[i];
        if (id == DEVICE_ID_NONE) continue;

        Device_t *device = get_device(id);
        if (device->power_on == nullptr) {
            printf("Slot Device has no poweron handler: %d", id);
            continue;
        } 
        device->power_on(computer, (SlotType_t)i);

        computer->slot_manager->register_slot(device, (SlotType_t)i);
    }

    register_clock_debug(computer);

    computer->cpu->reset();

    // mount disks - AFTER device init.
    for (const auto& disk_mount : state->disks_to_mount) {
        computer->mounts->mount_media(disk_mount);
    }

    osd = new OSD(computer, vs->renderer, vs->window, computer->slot_manager, 1120, 768, state->aa);

    // TODO: this should be handled differently. have osd save/restore?
    int error = SDL_SetRenderTarget(vs->renderer, nullptr);
    /* if (!error) {
        fprintf(stderr, "Error setting render target: %s\n", SDL_GetError());
        return(1);
    } */
    computer->video_system->set_window_title(system_config->name);
    
    computer->video_system->update_display(); // check for events 60 times per second.

    if (platform->mmu_type == MMU_MMU_IIGS) {
        //mmu_iigs->set_cpu(computer->cpu); // not needed any more, clock handles it.
        
        //computer->debug_window->set_open();
        //computer->cpu->execution_mode = EXEC_STEP_INTO;
        
        computer->register_debug_display_handler(
            "mmugs",
            DH_MMUGS, // unique ID for this, need to have in a header.
            [state]() -> DebugFormatter * {
                return debug_mmu_iigs(state->mmu_iigs);
            }
        );


        computer->cpu->trace_buffer->set_cpu_type(PROCESSOR_65816);
        computer->video_system->set_display_engine(DM_ENGINE_RGB);

        computer->register_reset_handler([state](bool cold_start) {
            state->mmu_iigs->reset();
            return true;
        });
    }

    run_cpus_init(computer);
    state->phase = PHASE_EMULATION;
}

/*
 * Clean up emulation state and transition to system select or exit.
 */
void transition_to_shutdown(GS2AppState *state) {
    computer_t *computer = state->computer;

    // save cpu trace buffer, then exit.
    // TODO: move this to the trace buffer destructor.
    std::string tracepath;
    Paths::calc_docs(tracepath, "trace.bin");
    computer->cpu->trace_buffer->save_to_file(tracepath);

    // deallocate stuff.
    delete osd;
    osd = nullptr;

    platform_info *platform = computer->platform;
    getMenuInterface()->setComputer(nullptr);
    delete computer;
    state->computer = nullptr;

    switch (platform->mmu_type) {
        case MMU_MMU_II:
            delete state->mmu_ii;
            break;
        case MMU_MMU_IIE:
            delete state->mmu_iie;
            break;
        case MMU_MMU_IIGS:
            delete state->mmu_iigs;
            delete state->mmu_iie;
            break;
    }
    state->mmu_ii = nullptr;
    state->mmu_iie = nullptr;
    state->mmu_iigs = nullptr;

    delete state->select_system;
    state->select_system = nullptr;

    // AssetAtlas holds textures tied to the old renderer — must delete before creating new computer
    delete state->aa;
    state->aa = nullptr;

    // Create fresh computer and select system for next cycle
    state->computer = new computer_t(nullptr);
    video_system_t *vs = state->computer->video_system;

    initMenu(vs->window);

    // Recreate AssetAtlas with the new renderer
    state->aa = new AssetAtlas_t(vs->renderer, "img/atlas.png");
    state->aa->set_elements(MainAtlas_count, asset_rects);

    state->select_system = new SelectSystem(vs, state->aa);

    // Let vsync throttle the selection UI instead of spinning.
    SDL_SetRenderVSync(vs->renderer, 1);
    state->phase = PHASE_SYSTEM_SELECT;
}

/* ========================================================================
   a2gspu headless-boot spike (local instrumentation harness)

   Boots the IIgs via -p, runs `spike_frames` deterministic frames with no GUI,
   then: (1) GREEN FLOOR - reads the Mega II bank-$E1 image directly
   (get_megaii_memory_base()+0x10000), dumps it, and computes a non-blank metric
   + FNV-1a hash over the SHR window ($2000-$9FFF); two runs at the same N must
   match (determinism). (2) DATUM - renders the current frame to the backbuffer
   and SDL_RenderReadPixels it to a BMP (save_screenshot), recording whether
   headless pixel readback works at all. Throwaway-grade; GPL-local.
   ======================================================================== */

// ---- A2GSPU warm-boot snapshot: file magic + cpu_state value-field (de)serialize ----
// Magic + version guard the on-disk layout: a stale snap.bin from an older binary is
// rejected loudly instead of mis-read. cpu serialize touches ONLY value fields. It NEVER
// touches the owned/plumbing pointers (mmu/cpun/core/trace_buffer) — a blanket copy would
// double-free the unique_ptr cpun. cpu_type is construction-fixed (set by the same -p 5
// platform across both runs) and intentionally excluded.
static const uint32_t A2GSPU_SNAP_MAGIC = 0x47535053;  // 'GSPS'
static const uint32_t A2GSPU_SNAP_VER   = 1;
static void a2gspu_cpu_save(FILE *f, cpu_state *cpu) {
    uint32_t magic = A2GSPU_SNAP_MAGIC, ver = A2GSPU_SNAP_VER;
    fwrite(&magic, 4, 1, f); fwrite(&ver, 4, 1, f);
    fwrite(&cpu->full_pc, 4, 1, f);     // union: pc+pb (the field the inject path sets)
    fwrite(&cpu->full_db, 4, 1, f);     // union: data bank in byte 2
    fwrite(&cpu->sp, 2, 1, f);
    fwrite(&cpu->a, 2, 1, f);
    fwrite(&cpu->x, 2, 1, f);
    fwrite(&cpu->y, 2, 1, f);
    fwrite(&cpu->d, 2, 1, f);
    fwrite(&cpu->p, 1, 1, f);           // union byte: all flag bitfields alias this
    uint8_t e = cpu->E;       fwrite(&e, 1, 1, f);       // 1-bit bitfield -> temp
    uint8_t cs = cpu->clock_stopped ? 1 : 0; fwrite(&cs, 1, 1, f);
    fwrite(&cpu->halt, 1, 1, f);
    uint8_t ia = cpu->irq_asserted ? 1 : 0;  fwrite(&ia, 1, 1, f);
    fwrite(&cpu->irq_pipe, 1, 1, f);
    fwrite(&cpu->reset_asserted, 8, 1, f);   // uint64_t bitmask (NOT a bool)
    uint8_t rd = cpu->rdy ? 1 : 0;     fwrite(&rd, 1, 1, f);
}
// Checked fread: read exactly `cnt` elements of `sz` bytes; on a short/failed read,
// `return false` out of the enclosing function. A truncated snapshot (interrupted
// save / partial copy) thus fails loud instead of restoring a half-initialized
// machine and reporting success.
#define A2GSPU_FREAD(ptr, sz, cnt, f) do { \
        if (fread((ptr), (sz), (cnt), (f)) != (size_t)(cnt)) return false; \
    } while (0)

// Returns false (without consuming the cpu block) on a bad magic/version, or on any
// truncated read of the cpu block.
static bool a2gspu_cpu_load(FILE *f, cpu_state *cpu) {
    uint32_t magic = 0, ver = 0;
    if (fread(&magic, 4, 1, f) != 1 || fread(&ver, 4, 1, f) != 1) return false;
    if (magic != A2GSPU_SNAP_MAGIC || ver != A2GSPU_SNAP_VER) return false;
    A2GSPU_FREAD(&cpu->full_pc, 4, 1, f);
    A2GSPU_FREAD(&cpu->full_db, 4, 1, f);
    A2GSPU_FREAD(&cpu->sp, 2, 1, f);
    A2GSPU_FREAD(&cpu->a, 2, 1, f);
    A2GSPU_FREAD(&cpu->x, 2, 1, f);
    A2GSPU_FREAD(&cpu->y, 2, 1, f);
    A2GSPU_FREAD(&cpu->d, 2, 1, f);
    A2GSPU_FREAD(&cpu->p, 1, 1, f);
    uint8_t e = 0;  A2GSPU_FREAD(&e, 1, 1, f);  cpu->E = e & 1;
    uint8_t cs = 0; A2GSPU_FREAD(&cs, 1, 1, f); cpu->clock_stopped = (cs != 0);
    A2GSPU_FREAD(&cpu->halt, 1, 1, f);
    uint8_t ia = 0; A2GSPU_FREAD(&ia, 1, 1, f); cpu->irq_asserted = (ia != 0);
    A2GSPU_FREAD(&cpu->irq_pipe, 1, 1, f);
    A2GSPU_FREAD(&cpu->reset_asserted, 8, 1, f);
    uint8_t rd = 0; A2GSPU_FREAD(&rd, 1, 1, f); cpu->rdy = (rd != 0);
    return true;
}
#undef A2GSPU_FREAD

// Trailing snapshot sentinel: a magic word written LAST (after the cpu + mmu blocks)
// on save, and required to read back exactly on load. The per-field checked-fread above
// catches a truncation that lands inside a known field; the sentinel additionally catches
// a snapshot truncated at/after the final field (e.g. the mmu block cut short, or a save
// interrupted right before its last bytes) — the load would otherwise hit EOF only after
// every field happened to read, and restore a silently-incomplete machine as success.
static const uint32_t A2GSPU_SNAP_END = 0x444E4553;  // 'SEND'
static void a2gspu_snap_write_sentinel(FILE *f) {
    uint32_t end = A2GSPU_SNAP_END;
    fwrite(&end, 4, 1, f);
}
static bool a2gspu_snap_check_sentinel(FILE *f) {
    uint32_t end = 0;
    if (fread(&end, 4, 1, f) != 1) return false;
    return end == A2GSPU_SNAP_END;
}

// ===========================================================================
// Env-gated headless CPU micro-test suite (A2GSPU_CPUTEST=<name>).
//
// Each case runs the REAL instruction stepper (cpu->cpun->execute_next, the
// 65816 mode-dispatcher) against a hand-built scenario in bank-0 RAM, then
// exits with a pass/fail code. No GUI, no test ROM, no device dependency — the
// same headless exit-code contract the boot/assert gate uses, applied to a
// single CPU corner case. Scenarios are driven with REAL opcodes injected in
// RAM (XCE/REP/SEP/etc.) so the dispatcher selects the correct width core just
// as it does in normal execution; this is what gives the tests their teeth.
//
// This suite is the regression net for the CPU-accuracy contracts the project's
// behavioral oracle (and every golden derived from it) silently depends on. A
// future edit that breaks one of these corners flips the named test to FAIL
// instead of corrupting goldens undetected. Each case below is teeth-proven:
// it PASSES with the accuracy fix in place and FAILS if the fix is reverted.
//
// Cases (each a separate A2GSPU_CPUTEST value):
//   wai_wake        WAI wakes on a MASKED pending IRQ (I gates servicing,
//                   not the wake); the masked IRQ is not serviced.
//   plp_native_x    PLP in native mode with the pulled X(index-8) bit set
//                   forces the index high bytes to $00.
//   rti_native_x    same contract via RTI's pull path.
//   dp_wrap_dl0     emulation-mode (d) indirect with DL==0 wraps the pointer
//                   high-byte fetch inside the zero page ($00FF -> $0000).
//   dec_z_816       65816 decimal-mode ADC sets Z from the BCD result (A==0).
//   dec_z_nmos      NMOS 6502 decimal-mode ADC sets Z from the BINARY sum
//                   A+M+C (the M4 +C fix); contrasts the 65816 contract.
//   branch_cyc      native-mode taken branch across a page = flat 3 cycles
//                   (no NMOS page-cross +1 penalty).
//   jmp_ind_816     65816 JMP ($xxFF) reads the vector high byte from the NEXT
//                   page (no page-boundary bug).
//   jmp_ind_nmos    NMOS 6502 JMP ($xxFF) DOES wrap inside the page (the bug).
// ===========================================================================

// Drive the dispatched 65816 from cold emulation state into native mode with
// 16-bit index registers (E=0, X=0). Returns with the machine parked so the
// caller can inject and run its own opcodes from PROG.
//
// Program at PROG (emulation cold state):  18 FB        CLC ; XCE  -> native
//                                          C2 10        REP #$10  -> X=0 (16b idx)
static void microtest_go_native_x16(cpu_state *cpu, MMU *mmu, uint32_t PROG) {
    mmu->write(PROG + 0, 0x18);   // CLC
    mmu->write(PROG + 1, 0xFB);   // XCE  -> E=C=0 : native mode
    mmu->write(PROG + 2, 0xC2);   // REP #imm
    mmu->write(PROG + 3, 0x10);   //  #$10 -> clear X flag (16-bit index)
    // Full cold-state reset so a prior case cannot contaminate this one when the
    // suite is run back-to-back via A2GSPU_CPUTEST=all (each case is independent).
    cpu->p = 0x00; cpu->d = 0x0000; cpu->a = 0x0000; cpu->sp = 0x01FF;
    cpu->E = 1; cpu->_M = 1; cpu->_X = 1;     // cold emulation defaults (after p=0)
    cpu->pb = 0x00; cpu->db = 0x00; cpu->full_db = 0;
    cpu->pc = (uint16_t)PROG;
    cpu->I = 1; cpu->irq_asserted = false; cpu->irq_pipe = 0;
    cpu->clock_stopped = false; cpu->halt = 0; cpu->reset_asserted = 0; cpu->rdy = false;
    // Exactly three instructions: CLC, XCE, REP #$10. (Running a 4th step here
    // would prematurely execute the caller's first real opcode at PROG+4.)
    for (int i = 0; i < 3; i++) (cpu->cpun->execute_next)(cpu);  // CLC, XCE, REP
}

// --- PLP / RTI native-mode index-high clear (contract 1) -------------------
// In native mode with 16-bit index (X=0) and non-zero index high bytes, pulling
// a P with the X(index-8) bit SET must force x_hi/y_hi to $00 (the real 65816
// mirrors the SEP/XCE width-narrowing). Reverting the fix leaves x_hi/y_hi at
// their pre-set values -> FAIL. via_rti selects RTI's pull path vs PLP's.
static int microtest_native_x_pull(cpu_state *cpu, MMU *mmu, bool via_rti) {
    const uint32_t PROG = 0x001000;
    microtest_go_native_x16(cpu, mmu, PROG);    // native, M=8bit (A_lo), X=16bit
    bool native16 = (cpu->E == 0 && cpu->_X == 0);

    // The bytes to be pulled are pushed with REAL stack instructions (PHA) so the
    // push and the later pull use the identical SP-relative mapping regardless of
    // the alt-ZP / language-card soft-switch state a prior case may have left set.
    // P value to be pulled: the X index-width bit (bit 4, $10) set => narrow the
    // index registers to 8-bit. (In native P, bit 4 is the X flag.)
    const uint8_t pulled_p = 0x10;       // X=1, everything else clear

    const char *opname;
    uint32_t at = PROG + 4;
    if (!via_rti) {
        // LDA #pulled_p ; PHA ; (dirty x/y hi) ; PLP
        mmu->write(at++, OP_LDA_IMM); mmu->write(at++, pulled_p);
        mmu->write(at++, OP_PHA_IMP);
        mmu->write(at++, OP_PLP_IMP);
        cpu->pc = (uint16_t)(PROG + 4);
        (cpu->cpun->execute_next)(cpu);  // LDA
        (cpu->cpun->execute_next)(cpu);  // PHA  (push P onto the real stack)
        cpu->x_hi = 0xAA; cpu->y_hi = 0xBB;   // dirty AFTER the push, before the pull
        (cpu->cpun->execute_next)(cpu);  // PLP  (pulls the byte we just pushed)
        opname = "plp_native_x";
    } else {
        // Push the RTI frame with PHA in reverse pull order: PB, PCH, PCL, P.
        // RTI pulls P (top), then PCL, PCH, then PB.
        const uint8_t frame[4] = { 0x00, 0x20, 0x00, pulled_p }; // PB, PCH, PCL, P
        for (int i = 0; i < 4; i++) {
            mmu->write(at++, OP_LDA_IMM); mmu->write(at++, frame[i]);
            mmu->write(at++, OP_PHA_IMP);
        }
        uint32_t rti_at = at;
        mmu->write(at++, OP_RTI_IMP);
        cpu->pc = (uint16_t)(PROG + 4);
        for (int i = 0; i < 8; i++) (cpu->cpun->execute_next)(cpu);  // 4x (LDA;PHA)
        cpu->x_hi = 0xAA; cpu->y_hi = 0xBB;   // dirty before the RTI pull
        cpu->pc = (uint16_t)rti_at;
        (cpu->cpun->execute_next)(cpu);  // RTI
        opname = "rti_native_x";
    }

    bool x_narrowed = (cpu->_X == 1);     // the pulled P actually set X
    int ok = native16 && x_narrowed && (cpu->x_hi == 0x00) && (cpu->y_hi == 0x00);
    printf("CPUTEST %s: native16=%d x_set=%d x_hi=%02X y_hi=%02X -- %s\n",
           opname, native16, x_narrowed, cpu->x_hi, cpu->y_hi, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// --- emulation-mode (d) indirect DL=0 page wrap (contract 2) ----------------
// In emulation mode with the Direct-page low byte == 0, the (d) indirect
// pointer's high-byte fetch wraps INSIDE the zero page ($00FF -> $0000), not
// across into $0100. We build a pointer whose low byte is at $00FF: wrap-correct
// reads the high byte from $0000, the (reverted) bug reads it from $0100. The
// two candidate pointers address two different bytes; A distinguishes them.
static int microtest_dp_wrap_dl0(cpu_state *cpu, MMU *mmu) {
    const uint32_t PROG = 0x001000;
    // pointer low at $00FF; wrap-correct high at $0000 -> ptr=$1234
    //                       buggy        high at $0100 -> ptr=$9934
    mmu->write(0x0000FF, 0x34);          // pointer low byte
    mmu->write(0x000000, 0x12);          // wrap-correct high byte
    mmu->write(0x000100, 0x99);          // buggy (cross-page) high byte
    mmu->write(0x001234, 0x42);          // value at the wrap-correct target
    mmu->write(0x009934, 0x77);          // value at the buggy target
    // LDA (d) with d-operand $FF, in emulation mode, D(irect)=$0000.
    mmu->write(PROG + 0, OP_LDA_IND);    // B2  LDA (d)
    mmu->write(PROG + 1, 0xFF);          //  (d)=$FF
    mmu->write(PROG + 2, OP_STP_IMP);    // DB  STP

    cpu->E = 1; cpu->_M = 1; cpu->_X = 1;   // emulation mode (cold)
    cpu->d = 0x0000;                        // DL == 0 (the quirk condition)
    cpu->pb = 0x00; cpu->db = 0x00; cpu->full_db = 0;
    cpu->a = 0x0000; cpu->pc = (uint16_t)PROG;
    cpu->I = 1; cpu->irq_asserted = false; cpu->irq_pipe = 0;
    cpu->clock_stopped = false; cpu->halt = 0; cpu->reset_asserted = 0; cpu->rdy = false;

    (cpu->cpun->execute_next)(cpu);      // LDA (d)
    uint8_t a = cpu->a_lo;
    int ok = (a == 0x42);                // wrap-correct value loaded
    printf("CPUTEST dp_wrap_dl0: a=%02X (want 42 wrap / 77 buggy) -- %s\n",
           a, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// --- decimal-mode Z flag (contract 3) ---------------------------------------
// The 65816 sets the decimal-mode Z from the BCD result (A==0). The NMOS 6502
// sets it from the raw BINARY sum A+M+C (carry included — the M4 fix added the
// +C). We pick operands where the two disagree: A=$99, M=$01, C=0, decimal.
//   BCD:    $99 + $01 = $00 with carry  -> 65816 Z=1 (result A is $00)
//   binary: $99 + $01 + 0 = $9A != 0    -> NMOS   Z=0
// Without the M4 +C the NMOS binary sum can still differ; we additionally drive
// a carry-in case below to pin the +C specifically.
//
// 65816 path runs on the live dispatched core. NMOS path runs on a transient
// 6502 core (the project's oracle never uses NMOS, but the fix's +C lives on
// that path, so the teeth require it). The transient core shares this CPU's
// mmu/clock and is destroyed on return; it does not perturb the machine state
// that boot/golden depend on (this test is env-gated off the boot path).
static int microtest_dec_z_816(cpu_state *cpu, MMU *mmu) {
    const uint32_t PROG = 0x001000;
    microtest_go_native_x16(cpu, mmu, PROG);    // native; M still 8-bit (REP #$10 only)
    // SED ; LDA #$99 ; ADC #$01   (8-bit A, decimal)
    mmu->write(PROG + 4, OP_SED_IMP);   // F8 SED
    mmu->write(PROG + 5, OP_LDA_IMM);   // A9
    mmu->write(PROG + 6, 0x99);
    mmu->write(PROG + 7, OP_ADC_IMM);   // 69
    mmu->write(PROG + 8, 0x01);
    mmu->write(PROG + 9, OP_STP_IMP);   // DB
    cpu->C = 0; cpu->pc = (uint16_t)(PROG + 4);
    for (int i = 0; i < 4 && !cpu->clock_stopped; i++) (cpu->cpun->execute_next)(cpu);
    // BCD: $99 + $01 = $00, carry out. 65816: A==$00 -> Z=1.
    int ok = (cpu->a_lo == 0x00) && (cpu->Z == 1) && (cpu->C == 1);
    printf("CPUTEST dec_z_816: a=%02X Z=%d C=%d (want a=00 Z=1 C=1) -- %s\n",
           cpu->a_lo, cpu->Z, cpu->C, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

static int microtest_dec_z_nmos(cpu_state *cpu, MMU *mmu, NClock *clk) {
    const uint32_t PROG = 0x001000;
    // Build a transient NMOS 6502 core that shares this machine's clock.
    std::unique_ptr<BaseCPU> nmos = createCPU(PROCESSOR_6502, clk);
    if (!nmos) { printf("CPUTEST dec_z_nmos: no NMOS core -- FAIL\n"); return 1; }
    BaseCPU *saved = cpu->cpun.release();   // park the live dispatcher
    cpu->cpun.reset(nmos.release());
    BaseCPU *saved_core = cpu->core;
    cpu->core = cpu->cpun.get();

    // SED ; SEC ; LDA #$98 ; ADC #$01   (binary A+M+C = $98+$01+1 = $9A != 0)
    //   The M4 fix counts the carry-in: without +C the binary sum is $99 ->
    //   still != 0 here, so we also assert the carry-included branch via a
    //   second operand set that is zero ONLY with the +C: A=$98 M=$01 C=0 ...
    // We instead use the cleanest discriminator of the +C term:
    //   A=$99 M=$00 C=1, decimal:
    //     binary A+M+C = $99+$00+1 = $9A  -> Z=0  (correct, carry counted)
    //     WITHOUT +C   = $99+$00   = $99  -> Z=0  (same here)
    // That does not discriminate. Use A=$FF M=$00 C=1:
    //     with +C: $FF+$00+1 = $100 -> low byte $00 -> Z=1
    //     no  +C : $FF+$00   = $FF  -> Z=0
    // BCD result of $FF+$00+1 in decimal is implementation noise; we assert
    // ONLY Z, which the fix derives from the binary low byte being $00.
    mmu->write(PROG + 0, OP_SED_IMP);   // F8 SED
    mmu->write(PROG + 1, OP_SEC_IMP);   // 38 SEC (C=1, the carry-in)
    mmu->write(PROG + 2, OP_LDA_IMM);   // A9
    mmu->write(PROG + 3, 0xFF);
    mmu->write(PROG + 4, OP_ADC_IMM);   // 69
    mmu->write(PROG + 5, 0x00);
    mmu->write(PROG + 6, OP_NOP_IMP);   // EA (NMOS has no STP; park on NOP)

    cpu->E = 1; cpu->_M = 1; cpu->_X = 1;
    cpu->pb = 0; cpu->db = 0; cpu->full_db = 0;
    cpu->p = 0; cpu->a = 0; cpu->pc = (uint16_t)PROG;
    cpu->I = 1; cpu->irq_asserted = false; cpu->irq_pipe = 0;
    cpu->clock_stopped = false; cpu->halt = 0; cpu->reset_asserted = 0; cpu->rdy = false;
    for (int i = 0; i < 5; i++) (cpu->cpun->execute_next)(cpu);  // SED SEC LDA ADC NOP

    // binary A+M+C = $FF+$00+1 = $100 -> low byte $00 -> NMOS decimal Z = 1.
    int ok = (cpu->Z == 1);
    printf("CPUTEST dec_z_nmos: Z=%d (want Z=1 from binary A+M+C carry) -- %s\n",
           cpu->Z, ok ? "PASS" : "FAIL");

    // restore the live dispatched core; drop the transient.
    cpu->cpun.reset(saved);
    cpu->core = saved_core;
    return ok ? 0 : 1;
}

// --- native-mode taken-branch cycle count (contract 4) ----------------------
// On the 65816 in native mode (E=0) a taken branch is a flat 3 cycles with NO
// page-cross penalty (the NMOS/emulation +1 for crossing a page does not apply).
// We place a BNE that is taken and crosses a page boundary, then measure the
// clock delta across exactly that one instruction. Native = 3; the reverted
// (penalty-applied) path would charge 4.
static int microtest_branch_cyc(cpu_state *cpu, MMU *mmu, NClock *clk) {
    const uint32_t PROG = 0x001000;
    microtest_go_native_x16(cpu, mmu, PROG);
    // Put the BNE so its NEXT instruction byte sits near a page end and the
    // target is on a different page. BNE at $10FE: PC after operand = $1100;
    // rel = +$10 (forward) -> target $1110, crossing from page $11 ... we need
    // the cross to be from the post-fetch PC's page. Place BNE at $10FC:
    //   opcode $10FC, operand $10FD, PC after fetch = $10FE; target = $10FE +
    //   (int8)$7F = $117D -> page $10 -> $11 cross. Z must be 0 (taken).
    const uint32_t BR = 0x0010FC;
    mmu->write(BR + 0, 0xD0);            // BNE
    mmu->write(BR + 1, 0x7F);            //  rel +$7F (forward, page-crossing)
    cpu->_Z = 0;                         // not-equal => BNE taken
    cpu->pc = (uint16_t)BR;
    uint64_t c0 = clk->get_cycles();
    (cpu->cpun->execute_next)(cpu);      // the BNE
    uint64_t c1 = clk->get_cycles();
    uint64_t cyc = c1 - c0;
    uint16_t pc = cpu->pc;
    bool crossed = ((BR + 2) & 0xFF00) != (pc & 0xFF00);
    int ok = (cyc == 3) && crossed && (pc == 0x117D);
    printf("CPUTEST branch_cyc: cycles=%llu pc=%04X crossed=%d (want 3, no +1) -- %s\n",
           (unsigned long long)cyc, pc, crossed, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// --- JMP (indirect) page-boundary behavior (contract 5) ---------------------
// 65816: JMP ($xxFF) fetches the vector high byte from the NEXT page (correct).
// NMOS 6502: it WRAPS inside the page (the classic bug). We lay a vector whose
// low byte is at $20FF; correct high comes from $2100, the bug reads $2000.
static int microtest_jmp_ind(cpu_state *cpu, MMU *mmu, bool nmos, NClock *clk) {
    const uint32_t PROG = 0x001000;
    const uint32_t VECLO = 0x0020FF;     // pointer low byte
    mmu->write(VECLO, 0xCD);             // vector low = $CD
    mmu->write(0x002100, 0xAB);          // NEXT-page high  -> $ABCD (816 correct)
    mmu->write(0x002000, 0x12);          // SAME-page high  -> $12CD (NMOS bug)
    mmu->write(PROG + 0, OP_JMP_IND);    // 6C  JMP ($20FF)
    mmu->write(PROG + 1, 0xFF);
    mmu->write(PROG + 2, 0x20);

    BaseCPU *saved = nullptr, *saved_core = nullptr;
    std::unique_ptr<BaseCPU> nmoscore;
    if (nmos) {
        nmoscore = createCPU(PROCESSOR_6502, clk);
        if (!nmoscore) { printf("CPUTEST jmp_ind_nmos: no NMOS core -- FAIL\n"); return 1; }
        saved = cpu->cpun.release();
        cpu->cpun.reset(nmoscore.release());
        saved_core = cpu->core; cpu->core = cpu->cpun.get();
    }
    cpu->E = 1; cpu->_M = 1; cpu->_X = 1;
    cpu->pb = 0; cpu->db = 0; cpu->full_db = 0;
    cpu->pc = (uint16_t)PROG;
    cpu->I = 1; cpu->irq_asserted = false; cpu->irq_pipe = 0;
    cpu->clock_stopped = false; cpu->halt = 0; cpu->reset_asserted = 0; cpu->rdy = false;

    (cpu->cpun->execute_next)(cpu);      // the JMP (Indirect)
    uint16_t pc = cpu->pc;
    int ok;
    if (nmos) {
        ok = (pc == 0x12CD);             // page-wrapped (the bug present)
        printf("CPUTEST jmp_ind_nmos: pc=%04X (want 12CD wrapped/bug) -- %s\n",
               pc, ok ? "PASS" : "FAIL");
        cpu->cpun.reset(saved);          // restore live dispatcher
        cpu->core = saved_core;
    } else {
        ok = (pc == 0xABCD);             // next-page high (no bug)
        printf("CPUTEST jmp_ind_816: pc=%04X (want ABCD next-page/no-bug) -- %s\n",
               pc, ok ? "PASS" : "FAIL");
    }
    return ok ? 0 : 1;
}

// Program injected at bank-0 $1000 (8-bit emulation mode, the cold-boot state):
//     1000  CB        WAI
//     1001  EE 10 10  INC $1010      ; "resumed past WAI" marker -> $1010 = 1
//     1004  DB        STP            ; halt the part cleanly so the loop ends
//   $1010 is pre-zeroed; the IRQ vector $FFFE/$FFFF is pointed at a TRAP page so
//   that, if the masked IRQ were wrongly serviced, the PC would land in $FF00..
//   and the marker would never be written (caught as a FAIL).
static int microtest_wai_wake(cpu_state *cpu, MMU *mmu) {

    // --- build the scenario in RAM (write_raw bypasses IO/shadow side effects) ---
    // $1000 is bank-0 main RAM outside every IIgs shadow window ($0400-$07FF text,
    // $2000-$5FFF hires) so the injected bytes are not mirrored into $E1.
    const uint32_t PROG = 0x001000;
    const uint32_t MARK = 0x001010;
    // Use the same bus write()/read() path the stepper's fetch uses, so the
    // injected bytes are guaranteed visible at the program counter (write_raw and
    // the fetch can resolve to different page buffers depending on bank state).
    mmu->write(PROG + 0, 0xCB);              // WAI
    mmu->write(PROG + 1, 0xEE);              // INC abs
    mmu->write(PROG + 2, 0x10);
    mmu->write(PROG + 3, 0x10);              // -> $1010 (the marker)
    mmu->write(PROG + 4, 0xDB);              // STP
    mmu->write(MARK, 0x00);                  // marker starts at 0
    // Sanity: confirm the injected WAI opcode is visible to the CPU at PROG.
    uint8_t fetched = mmu->read(PROG);
    // Point the emulation-mode IRQ vector ($FFFE/$FFFF) at $FF00 and lay a STP
    // there: if the masked IRQ were (wrongly) serviced, the PC diverts into the
    // $FFxx trap instead of running INC $2010 -> the marker stays 0 -> FAIL.
    mmu->write(0x00FFFE, 0x00);
    mmu->write(0x00FFFF, 0xFF);
    mmu->write(0x00FF00, 0xDB);              // STP at the trap

    // --- set the CPU into the exact corner state ---
    cpu->halt = 0;
    cpu->clock_stopped = false;
    cpu->reset_asserted = 0;
    cpu->rdy = false;
    cpu->E = 1;                 // emulation mode (the cold-boot default)
    cpu->pb = 0x00;
    cpu->db = 0x00;
    cpu->pc = (uint16_t)PROG;
    cpu->I = 1;                 // interrupt SERVICING masked
    cpu->irq_asserted = true;   // ... but an interrupt IS pending (the wake source)
    cpu->irq_pipe = 0;          // with I=1 the pipe never latches -> no servicing

    bool saw_rdy = false;       // WAI must set RDY at least once
    bool resumed = false;       // ... then RDY must clear and execution continue
    const int STEP_CAP = 64;    // bounded: a never-waking part can't hang the test
    int steps = 0;
    for (; steps < STEP_CAP; steps++) {
        (cpu->cpun->execute_next)(cpu);
        if (cpu->rdy) saw_rdy = true;
        else if (saw_rdy) resumed = true;   // first clear after a set = the wake
        if (cpu->clock_stopped) break;      // STP reached -> program finished
    }

    uint8_t  marker = mmu->read(MARK);
    uint32_t pc24   = ((uint32_t)cpu->pb << 16) | cpu->pc;
    bool took_vector = (cpu->pc >= 0xFF00 && cpu->pc <= 0xFFFF && cpu->pb == 0);

    // PASS criteria (all must hold):
    //  - WAI raised RDY (the part actually entered the wait)         saw_rdy
    //  - RDY then cleared (it WOKE on the pending, masked interrupt)  resumed
    //  - the instruction after WAI ran (INC $2010)                    marker==1
    //  - the masked IRQ was NOT serviced (no divert to the vector)    !took_vector
    //  - the part halted within the step cap (no RDY spin)            clock_stopped
    int ok = saw_rdy && resumed && (marker == 0x01) && !took_vector && cpu->clock_stopped;

    printf("CPUTEST wai_wake: fetched=%02X saw_rdy=%d resumed=%d marker=%02X "
           "took_vector=%d stopped=%d steps=%d pc=%06X -- %s\n",
           fetched, saw_rdy, resumed, marker, took_vector, cpu->clock_stopped,
           steps, (unsigned)pc24, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// Dispatcher: A2GSPU_CPUTEST=<name> selects one case. "all" runs every case and
// fails if any one fails (the CI entry point). Unknown name -> FAIL.
static int run_cpu_microtest(GS2AppState *state, const char *which) {
    computer_t *computer = state->computer;
    cpu_state  *cpu = computer->cpu;
    if (!cpu || !cpu->mmu || !cpu->cpun || !computer->clock) {
        printf("CPUTEST: machine not initialized -- FAIL\n");
        return 1;
    }
    MMU    *mmu = cpu->mmu;
    NClock *clk = (NClock *)computer->clock;   // NClockII is-a NClock

    if (strcmp(which, "wai_wake")     == 0) return microtest_wai_wake(cpu, mmu);
    if (strcmp(which, "dp_wrap_dl0")  == 0) return microtest_dp_wrap_dl0(cpu, mmu);
    if (strcmp(which, "dec_z_816")    == 0) return microtest_dec_z_816(cpu, mmu);
    if (strcmp(which, "dec_z_nmos")   == 0) return microtest_dec_z_nmos(cpu, mmu, clk);
    if (strcmp(which, "branch_cyc")   == 0) return microtest_branch_cyc(cpu, mmu, clk);
    if (strcmp(which, "plp_native_x") == 0) return microtest_native_x_pull(cpu, mmu, false);
    if (strcmp(which, "rti_native_x") == 0) return microtest_native_x_pull(cpu, mmu, true);
    if (strcmp(which, "jmp_ind_816")  == 0) return microtest_jmp_ind(cpu, mmu, false, clk);
    if (strcmp(which, "jmp_ind_nmos") == 0) return microtest_jmp_ind(cpu, mmu, true,  clk);

    if (strcmp(which, "all") == 0) {
        int fails = 0;
        fails += (microtest_wai_wake(cpu, mmu) != 0);
        fails += (microtest_native_x_pull(cpu, mmu, false) != 0);
        fails += (microtest_native_x_pull(cpu, mmu, true)  != 0);
        fails += (microtest_dp_wrap_dl0(cpu, mmu) != 0);
        fails += (microtest_dec_z_816(cpu, mmu) != 0);
        fails += (microtest_dec_z_nmos(cpu, mmu, clk) != 0);
        fails += (microtest_branch_cyc(cpu, mmu, clk) != 0);
        fails += (microtest_jmp_ind(cpu, mmu, false, clk) != 0);
        fails += (microtest_jmp_ind(cpu, mmu, true,  clk) != 0);
        printf("CPUTEST all: %d failure(s)\n", fails);
        return fails == 0 ? 0 : 1;
    }

    printf("CPUTEST: unknown case '%s' -- FAIL\n", which);
    return 1;
}

// ===========================================================================
// Env-gated headless MMU + VIDEO micro-test suite (A2GSPU_MMUTEST=<name>).
//
// Mirrors the CPU micro-test rail (A2GSPU_CPUTEST) for the two subsystems that
// directly PRODUCE the SHR boot golden every render golden trusts: the IIgs FPI
// memory controller (main/aux steering, the SHADOW register, the $E1 bank latch,
// the language-card bank/read switches) and the Super Hi-Res pixel decode (per-
// scanline SCB 320/640 mode, the 640 dot->palette-offset mapping, and the $0RGB
// palette expansion). A silent regression in either subsystem would corrupt the
// golden undetected; these cases flip the named test to FAIL instead. Each is
// TEETH-PROVEN: it PASSES with the real mapping/decode and FAILS if the relevant
// logic is reverted. Cases are env-gated off the boot path, so the boot/CPU/
// GS-OS golden is unchanged.
//
// Cases (each a separate A2GSPU_MMUTEST value):
//   mmu_aux_steer  main/aux read+write steering: RAMRD/RAMWRT are independent;
//                  80STORE+PAGE2 steers the text page to aux regardless of RAMRD;
//                  ALTZP steers the zero page to aux. (calc_aux_read/write.)
//   mmu_shadow     the $C035 SHADOW register decode: each inhibit bit gates the
//                  matching window (text1/hgr1/SHR) for shadow-to-$E1.
//   mmu_e1_latch   the $E1 bank latch: with the latch set a bank-$E1 write lands
//                  in the Mega-II aux image (the SHR window) and reads back; the
//                  golden's SHR bytes flow through exactly this path.
//   mmu_lc_bank    language-card $C08x bank-select + read-enable: the classic
//                  double-read write-enable, bank1/bank2 select, ROM-read default.
//   vid_scb_mode   per-scanline SCB mode decode: bit7 picks 640 vs 320, changing
//                  how many palette-index pixels a line contributes to the hist.
//   vid_640_offset the 640-mode dot->palette-offset map {dot0:+8,dot1:+12,
//                  dot2:+0,dot3:+4} (the exact renderer contract).
//   all            runs every case; nonzero exit if any one fails (the CI entry).
// (The $0RGB->RGB888 palette expansion is intentionally NOT a case here — see the
//  honesty note above run_mmu_microtest: it has no teeth as a standalone test.)
// ===========================================================================

// --- main/aux read+write steering (calc_aux_read / calc_aux_write) ----------
// Drives the REAL $C00x soft-switches through the MMU's bus write path (the
// faithful 74LS259 decode) and asserts the resolved aux offset for representative
// pages. The contract has teeth on three independent axes: RAMRD vs RAMWRT must
// steer reads and writes SEPARATELY; 80STORE+PAGE2 must override RAMRD for the
// text page; ALTZP must steer the zero page. A revert that collapses any axis
// (e.g. read/write sharing one flag, or dropping the 80STORE override) flips this.
static int microtest_mmu_aux_steer(MMU_IIgs *m) {
    const uint32_t AUX = 0x1'0000, MAIN = 0x0'0000;
    auto sw = [&](uint16_t a){ m->write(a, 0x00); };   // touch a soft-switch ($C0xx)
    // Cold baseline: all main. (RAMRD/RAMWRT/80STORE/PAGE2/ALTZP/HIRES off.)
    sw(0xC000); sw(0xC002); sw(0xC004); sw(0xC008); sw(0xC054); sw(0xC056);
    int ok = 1;
    // Axis 1: RAMRD on, RAMWRT off -> a $40-page READ steers to aux, WRITE stays main.
    sw(0xC003);                                   // RAMRD on
    int a1r = (m->calc_aux_read (0x004000) == AUX);
    int a1w = (m->calc_aux_write(0x004000) == MAIN);
    sw(0xC005);                                   // RAMWRT on -> now WRITE steers too
    int a1w2 = (m->calc_aux_write(0x004000) == AUX);
    sw(0xC002); sw(0xC004);                        // both back off
    ok &= a1r && a1w && a1w2;
    // Axis 2: 80STORE + PAGE2 steers the TEXT page ($04xx) to aux even with RAMRD off.
    sw(0xC001);                                   // 80STORE on
    sw(0xC055);                                   // PAGE2 on
    int a2on = (m->calc_aux_read(0x000400) == AUX) && (m->calc_aux_write(0x000400) == AUX);
    sw(0xC054);                                   // PAGE2 off -> text page back to main
    int a2off = (m->calc_aux_read(0x000400) == MAIN) && (m->calc_aux_write(0x000400) == MAIN);
    sw(0xC000);                                   // 80STORE off
    ok &= a2on && a2off;
    // Axis 3: ALTZP steers the zero page ($00xx) both read and write.
    sw(0xC009);                                   // ALTZP on
    int a3on = (m->calc_aux_read(0x000000) == AUX) && (m->calc_aux_write(0x000000) == AUX);
    sw(0xC008);                                   // ALTZP off
    int a3off = (m->calc_aux_read(0x000000) == MAIN);
    ok &= a3on && a3off;
    printf("MMUTEST mmu_aux_steer: rd=%d wr=%d wr2=%d 80s2=%d 80soff=%d zp=%d zpoff=%d -- %s\n",
           a1r, a1w, a1w2, a2on, a2off, a3on, a3off, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// --- $C035 SHADOW register window decode (shadow_is_enabled) -----------------
// Each SHADOW inhibit bit gates shadow-to-$E1 for one display window. We set the
// register via the real $C035 write and assert the per-window enable for the
// text1 ($0400), hgr1 ($2000) and SHR (bank-1 $12000) windows. Two register
// values pin the polarity: $00 (nothing inhibited) shadows ALL three; $0B
// (TEXT1+HGR1+SHR inhibit bits set) shadows NONE of them. A revert that inverts a
// bit, drops a window, or mis-ranges the SHR aux window flips this.
static int microtest_mmu_shadow(MMU_IIgs *m) {
    int ok = 1;
    // SHR-only witness: $16000 is inside the SHR window ($12000-$19FFF) but ABOVE the
    // overlapping AUXHGR window ($12000-$15FFF), so only the SHR inhibit bit gates it
    // (a bank-1 $2000 address sits in both windows and would need both bits to inhibit).
    const uint32_t SHR_ONLY = 0x016000;
    m->write(0xC035, 0x00);                       // nothing inhibited
    int all_on = m->shadow_is_enabled(0x000400)   // text1
              && m->shadow_is_enabled(0x002000)   // hgr1
              && m->shadow_is_enabled(SHR_ONLY);  // SHR
    // bits: TEXT1=$01, HGR1=$02, SHR=$08  -> $0B inhibits all three windows.
    m->write(0xC035, 0x01 | 0x02 | 0x08);
    int all_off = !m->shadow_is_enabled(0x000400)
               && !m->shadow_is_enabled(0x002000)
               && !m->shadow_is_enabled(SHR_ONLY);
    // selective: inhibit ONLY SHR ($08) -> text1 still shadowed, SHR not.
    m->write(0xC035, 0x08);
    int sel = m->shadow_is_enabled(0x000400) && !m->shadow_is_enabled(SHR_ONLY);
    m->write(0xC035, 0x08);                        // restore the reset default
    ok &= all_on && all_off && sel;
    printf("MMUTEST mmu_shadow: all_on=%d all_off=%d sel=%d -- %s\n",
           all_on, all_off, sel, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// --- $E1 bank-latch SHR visibility (bank_e1_write / bank_e1_read) ------------
// The SHR pixels the golden hashes live in the Mega-II AUX image (bank $E1). With
// the bank latch SET (reg_new_video bit0, the reset default), a write to a bank-
// $E1 address lands at megaii_base[addr & 0x1FFFF] -- i.e. $E1:$2000 -> the SHR
// window at Mega-II linear $12000 -- and reads back through the same latch. With
// the latch CLEAR, the same $E1 access falls back to the Mega-II's own read/write
// (bank-0 of the image), so the byte does NOT appear at the aux-image $12000.
// This is the exact aux/main fork the golden's SHR bytes traverse.
static int microtest_mmu_e1_latch(MMU_IIgs *m) {
    uint8_t *m2 = m->get_megaii_memory_base();
    if (!m2) { printf("MMUTEST mmu_e1_latch: no Mega-II image -- FAIL\n"); return 1; }
    const uint32_t E1_PIX = 0xE12000;                 // $E1:$2000 (top of the SHR window)
    const uint32_t AUXIDX = 0x12000;                  // its Mega-II aux linear index
    // Latch SET (reset default = reg_new_video $01). Write+read through bank $E1.
    m->write(0xC029, 0x01);                            // bank_latch = 1
    m->write(E1_PIX, 0x5A);
    int set_land = (m2[AUXIDX] == 0x5A);               // landed in the aux image
    int set_read = (m->read(E1_PIX) == 0x5A);          // reads back through the latch
    // Latch CLEAR: the SAME $E1 write must NOT update the aux-image $12000 slot.
    m2[AUXIDX] = 0x00;                                 // clear the witness
    m->write(0xC029, 0x00);                            // bank_latch = 0
    m->write(E1_PIX, 0xA5);
    int clr_miss = (m2[AUXIDX] != 0xA5);               // did NOT take the aux path
    m->write(0xC029, 0x01);                            // restore the reset default
    int ok = set_land && set_read && clr_miss;
    printf("MMUTEST mmu_e1_latch: set_land=%d set_read=%d clr_miss=%d -- %s\n",
           set_land, set_read, clr_miss, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// --- language-card $C08x bank-select + read/write-enable ---------------------
// Drives the real LC soft-switch decode (the same LanguageCardLogic the boot ROM
// hits) and asserts the bank-select, read-enable and the classic double-read
// write-enable. Contract: a $C08x access with A3 set selects bank1; with A3 clear,
// bank2. Read-enable follows the A0/A1 pattern (00/11 -> RAM read, 01/10 -> ROM).
// Write-enable requires TWO consecutive odd reads (PRE_WRITE then WRITE); an even
// read in between clears it. The write-enable is the load-bearing teeth surface:
// we first CLEAR it with an even read, confirm a SINGLE odd read does NOT yet
// enable (only arms PRE_WRITE), then confirm the SECOND odd read enables. A revert
// that drops the double-read latch (enables on one read, or never) flips this.
static int microtest_mmu_lc_bank(MMU_IIgs *m) {
    int ok = 1;
    // --- bank select + read-enable polarity ---
    // $C08B = ...1011: A3=1 (bank1), A1A0=11 (read-enable RAM).
    m->read(0xC08B);
    int b1 = m->is_lc_bank1();                    // A3 set -> bank1
    int re = m->is_lc_read_enable();              // 11 -> read-enable (RAM)
    // $C083 = ...0011: A3=0 (bank2), A1A0=11 (read-enable).
    m->read(0xC083);
    int b2 = !m->is_lc_bank1();                   // A3 clear -> bank2
    // $C089 = ...1001: A1A0=01 -> read-enable CLEARED (ROM read).
    m->read(0xC089);
    int ro = !m->is_lc_read_enable();             // 01 -> ROM read (read-enable off)
    // --- the double-read write-enable latch (the teeth) ---
    m->read(0xC088);                              // EVEN read -> write-enable CLEARED
    int we_off = !m->is_lc_write_enable();        // now NOT write-enabled
    m->read(0xC08B);                              // 1st ODD read -> arms PRE_WRITE only
    int we_mid = !m->is_lc_write_enable();        // still NOT enabled after one odd read
    m->read(0xC08B);                              // 2nd ODD read -> write-enable SET
    int we_on = m->is_lc_write_enable();          // NOW write-enabled (the latch fired)
    ok &= b1 && re && b2 && ro && we_off && we_mid && we_on;
    // Leave the LC back in the reset-ish read-enabled bank2 state the boot path expects.
    m->set_state_register(0x0C); m->bsr_map_memory();
    printf("MMUTEST mmu_lc_bank: b1=%d re=%d b2=%d ro=%d we_off=%d we_mid=%d we_on=%d -- %s\n",
           b1, re, b2, ro, we_off, we_mid, we_on, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// --- video: SCB per-scanline 320/640 mode decode (iigs_shr::histogram) -------
// The shared SHR decode (the one the video summary AND any SHR render path use)
// reads the per-line SCB to pick 320 vs 640. A 320-mode line decodes each byte as
// two 4-bit indices (160 bytes -> 320 px); a 640-mode line decodes each byte as
// four 2-bit dots (160 bytes -> 640 px). We build a buffer whose lines are split
// half 320 / half 640 and assert the TOTAL pixel count the histogram accounts for
// matches the mode-correct sum. A revert that ignores SCB bit7 (always-320 or
// always-640) changes that total and flips this.
static int microtest_vid_scb_mode(MMU_IIgs *m) {
    uint8_t *m2 = m->get_megaii_memory_base();
    if (!m2) { printf("MMUTEST vid_scb_mode: no Mega-II image -- FAIL\n"); return 1; }
    uint8_t *e1 = m2 + iigs_shr::MEGAII_E1;          // bank $E1 base
    // Zero the SCB + pixel window, then lay a deterministic pattern.
    for (int i = 0; i < iigs_shr::LINES; i++) e1[iigs_shr::SCB + i] = 0;
    for (int i = 0; i < iigs_shr::LINES * iigs_shr::BYTES_PER_LINE; i++)
        e1[iigs_shr::PIX + i] = 0x11;                 // every nibble/dot non-zero & uniform
    const int N640 = 100;                            // first 100 lines 640, rest 320
    for (int vc = 0; vc < iigs_shr::LINES; vc++)
        e1[iigs_shr::SCB + vc] = (vc < N640) ? 0x80 : 0x00;
    long hist[16];
    iigs_shr::histogram(e1, hist);
    long total = 0; for (int i = 0; i < 16; i++) total += hist[i];
    // 640-mode line = 160 bytes * 4 dots = 640 px; 320-mode line = 160*2 = 320 px.
    long want = (long)N640 * 640 + (long)(iigs_shr::LINES - N640) * 320;
    int ok = (total == want);
    // teeth witness: an always-320 decode would total LINES*320 (= 64000), distinct.
    printf("MMUTEST vid_scb_mode: total=%ld want=%ld (always320=%d) -- %s\n",
           total, want, iigs_shr::LINES * 320, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// --- video: 640-mode dot->palette-offset map {8,12,0,4} ----------------------
// In 640 mode each byte is 4 two-bit dots; the EFFECTIVE palette index is the
// 2-bit value plus the per-dot color sub-bank offset dot0:+8 dot1:+12 dot2:+0
// dot3:+4 (the exact renderer/HW contract). EVERY line is the same 640 byte $1B =
// 00 01 10 11 -> dot0=0 dot1=1 dot2=2 dot3=3, so each dot carries a DISTINCT 2-bit
// value and the per-dot OFFSET PAIRING is load-bearing (not just the offset SET):
//   dot0:0+8=8  dot1:1+12=13  dot2:2+0=2  dot3:3+4=7  -> bins {2,7,8,13}.
// Each = 160 bytes * 200 lines = 32000; all other bins 0. A revert that permutes
// the offsets (e.g. {0,4,8,12}) lands a DIFFERENT bin set ({0,5,10,15}) and flips
// this; the distinct dot values defeat the all-equal aliasing.
static int microtest_vid_640_offset(MMU_IIgs *m) {
    uint8_t *m2 = m->get_megaii_memory_base();
    if (!m2) { printf("MMUTEST vid_640_offset: no Mega-II image -- FAIL\n"); return 1; }
    uint8_t *e1 = m2 + iigs_shr::MEGAII_E1;
    for (int i = 0; i < iigs_shr::LINES; i++) e1[iigs_shr::SCB + i] = 0x80;  // ALL 640 mode
    // $1B = 00 01 10 11 -> dot0=0 dot1=1 dot2=2 dot3=3, on every pixel byte.
    for (int i = 0; i < iigs_shr::LINES * iigs_shr::BYTES_PER_LINE; i++)
        e1[iigs_shr::PIX + i] = 0x1B;
    long hist[16];
    iigs_shr::histogram(e1, hist);
    // Expected occupied bins from the {8,12,0,4} pairing: {2,7,8,13}.
    const long PER = (long)iigs_shr::BYTES_PER_LINE * iigs_shr::LINES;   // 160*200 = 32000
    int idx_ok = (hist[8]==PER && hist[13]==PER && hist[2]==PER && hist[7]==PER);
    long other = 0;
    for (int i = 0; i < 16; i++) if (i!=2 && i!=7 && i!=8 && i!=13) other += hist[i];
    int ok = idx_ok && (other == 0);
    printf("MMUTEST vid_640_offset: h8=%ld h13=%ld h2=%ld h7=%ld other=%ld (per=%ld) -- %s\n",
           hist[8], hist[13], hist[2], hist[7], other, PER, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// NOTE on a deliberately-OMITTED video case (honesty): the $0RGB-palette-word ->
// RGB888 (*0x11 nibble-replication) expansion is golden-relevant, but it lives
// INLINE inside the printf-only iigs_video_summary() (no returnable function), so a
// standalone test would have to re-derive the formula rather than call production
// code — giving it NO teeth (a revert of the real *0x11 would not flip it). Rather
// than ship a brittle self-referential check or refactor the summary just to test
// it, that case is skipped here. The two video cases that ARE present (scb_mode,
// 640_offset) drive the SHARED production decode iigs_shr::histogram(), so they DO
// have teeth on the mode/offset contracts the golden actually depends on.

// Dispatcher: A2GSPU_MMUTEST=<name>. "all" runs every case (the CI entry point).
static int run_mmu_microtest(GS2AppState *state, const char *which) {
    MMU_IIgs *m = state->mmu_iigs;
    if (!m) { printf("MMUTEST: IIgs MMU not initialized (need -p 5) -- FAIL\n"); return 1; }

    if (strcmp(which, "mmu_aux_steer")  == 0) return microtest_mmu_aux_steer(m);
    if (strcmp(which, "mmu_shadow")     == 0) return microtest_mmu_shadow(m);
    if (strcmp(which, "mmu_e1_latch")   == 0) return microtest_mmu_e1_latch(m);
    if (strcmp(which, "mmu_lc_bank")    == 0) return microtest_mmu_lc_bank(m);
    if (strcmp(which, "vid_scb_mode")   == 0) return microtest_vid_scb_mode(m);
    if (strcmp(which, "vid_640_offset") == 0) return microtest_vid_640_offset(m);

    if (strcmp(which, "all") == 0) {
        int fails = 0;
        fails += (microtest_mmu_aux_steer(m)  != 0);
        fails += (microtest_mmu_shadow(m)     != 0);
        fails += (microtest_mmu_e1_latch(m)   != 0);
        fails += (microtest_mmu_lc_bank(m)    != 0);
        fails += (microtest_vid_scb_mode(m)   != 0);
        fails += (microtest_vid_640_offset(m) != 0);
        printf("MMUTEST all: %d failure(s)\n", fails);
        return fails == 0 ? 0 : 1;
    }

    printf("MMUTEST: unknown case '%s' -- FAIL\n", which);
    return 1;
}

static void run_headless_spike(GS2AppState *state) {
    computer_t *computer = state->computer;

    // Env-gated CPU micro-test short-circuit: when A2GSPU_CPUTEST names a case,
    // run only that case and exit with its verdict (the normal frame spike, the
    // GS/OS round-trip, and the boot golden are all bypassed). Keeps the CPU
    // corner-case proof on the same headless exit-code rail as the boot gate.
    if (const char *ct = SDL_getenv("A2GSPU_CPUTEST")) {
        int rc = run_cpu_microtest(state, ct);
        printf("=== CPUTEST COMPLETE (%s) ===\n", rc == 0 ? "PASS" : "FAIL");
        exit(rc);
    }

    // Env-gated MMU + VIDEO micro-test short-circuit (same rail as A2GSPU_CPUTEST):
    // exercises the FPI mapping + SHR decode contracts the render golden trusts,
    // then exits with the verdict. Bypasses the frame spike / boot golden entirely.
    if (const char *mt = SDL_getenv("A2GSPU_MMUTEST")) {
        int rc = run_mmu_microtest(state, mt);
        printf("=== MMUTEST COMPLETE (%s) ===\n", rc == 0 ? "PASS" : "FAIL");
        exit(rc);
    }

    printf("\n=== A2GSPU HEADLESS SPIKE: running %d frames ===\n", state->spike_frames);

    computer->execution_mode = EXEC_NORMAL;

    bus_trace_reset();            // arm the bus-trace oracle
    g_bus_trace_enabled = true;
    slot_bus_reset();             // arm the faithful slot-bus model (the virtual slot)
    g_slot_bus_enabled = true;
    mmu_state_trace_reset();      // arm the ground-truth MMU-state stream (cycle-aligned with the slot bus)
    g_mmu_state_trace_enabled = true;

    // Arm the headless GS/OS app-bringup diagnostics (env-gated, stdout-only).
    g_iigs_tbtrace_enabled = (SDL_getenv("A2GSPU_TBTRACE") != nullptr);
    g_iigs_errhook_enabled = (SDL_getenv("A2GSPU_ERRHOOK") != nullptr);
    g_iigs_brkdump_enabled = (SDL_getenv("A2GSPU_BRKDUMP") != nullptr);
    g_iigs_stop_on_fault   = (SDL_getenv("A2GSPU_STOP_ON_FAULT") != nullptr);
    g_brkmem_on = (SDL_getenv("A2GSPU_BRKMEM") != nullptr);
    if (const char *bp = SDL_getenv("A2GSPU_BREAK")) {
        g_iigs_break_enabled = true;
        g_iigs_break_addr = (uint32_t)strtoul(bp, nullptr, 16) & 0xFFFFFF;
    }
    if (const char *bk = SDL_getenv("A2GSPU_TBTRACE_BANK"))
        g_iigs_tbtrace_bank = (int)strtoul(bk, nullptr, 16);
    g_iigs_trace_from = 0; g_iigs_trace_armed = false;
    if (const char *tf = SDL_getenv("A2GSPU_TRACE_FROM"))
        g_iigs_trace_from = (uint32_t)strtoul(tf, nullptr, 16) & 0xFFFFFF;

    // ---- A2GSPU_ITRACE: additive, env-gated, per-instruction execution trace ----
    // Two arm modes (OR'd): A2GSPU_ITRACE_FROM=<hexPC> arms when full_pc first
    // hits that 24-bit PC; A2GSPU_ITRACE_FRAME=<N> arms at the start of headless
    // frame N. A2GSPU_ITRACE_N caps logged instructions (default 256). The master
    // gate g_iigs_itrace_enabled is set iff at least one arm mode is supplied, so
    // a normal run/golden (no ITRACE_* set) pays only one untaken branch.
    g_iigs_itrace_enabled = false; g_iigs_itrace_armed = false;
    g_iigs_itrace_logged = 0; g_iigs_cur_frame = 0;
    g_iigs_itrace_use_pc = false; g_iigs_itrace_from = 0; g_iigs_itrace_frame = -1;
    g_iigs_itrace_n = 256;
    if (const char *itf = SDL_getenv("A2GSPU_ITRACE_FROM")) {
        g_iigs_itrace_from = (uint32_t)strtoul(itf, nullptr, 16) & 0xFFFFFF;
        g_iigs_itrace_use_pc = true; g_iigs_itrace_enabled = true;
    }
    if (const char *itfr = SDL_getenv("A2GSPU_ITRACE_FRAME")) {
        g_iigs_itrace_frame = (int)strtol(itfr, nullptr, 10);
        g_iigs_itrace_enabled = true;
    }
    if (const char *itn = SDL_getenv("A2GSPU_ITRACE_N")) {
        int v = (int)strtol(itn, nullptr, 10);
        if (v > 0) g_iigs_itrace_n = v;
    }
    if (g_iigs_itrace_enabled)
        fprintf(stderr, "IIGS ITRACE: enabled (from=%s$%06X frame=%d n=%d)\n",
                g_iigs_itrace_use_pc ? "" : "(none)",
                (unsigned)g_iigs_itrace_from, g_iigs_itrace_frame, g_iigs_itrace_n);
    g_iigs_sym_base = 0; g_iigs_sym_base_locked = false;
    if (const char *sb = SDL_getenv("A2GSPU_SYM_BASE")) {
        g_iigs_sym_base = (uint32_t)strtoul(sb, nullptr, 16) & 0xFFFFFF;
        g_iigs_sym_base_locked = true;   // pinned -> no auto-inference
    }
    if (const char *sp = SDL_getenv("A2GSPU_SYMBOLS")) iigs_symbols_load(sp);
    g_iigs_pending.clear();
    g_iigs_last_result.clear();
    g_iigs_last_gsos_err = 0;

    // Snapshot the $E1 SHR window at trace-arm time. Replaying the trace
    // (init + every captured write) must byte-match the final $E1 below -> proves
    // the two taps capture 100% of SHR-affecting writes (snoop-completeness test).
    {
        uint8_t *m2i = state->mmu_iigs ? state->mmu_iigs->get_megaii_memory_base() : nullptr;
        if (m2i) {
            FILE *fi = fopen("spike_e1_init.bin", "wb");
            if (fi) { fwrite(m2i + 0x12000, 1, 0x8000, fi); fclose(fi); } // $E1:$2000-$9FFF
        }
    }

    // Optional: inject + launch a flat Rosetta-built binary after some boot frames.
    //   A2GSPU_RUN_BIN=path[@HEXADDR]   (default load/exec addr $7E0000)
    //   A2GSPU_RUN_BOOT=N               (boot frames before injecting; default = spike_frames/2)
    // The binary's bus effects are isolated by resetting the traces at injection time.
    const char *snap_save = SDL_getenv("A2GSPU_SNAP_SAVE");
    const char *snap_load = SDL_getenv("A2GSPU_SNAP_LOAD");
    const char *runbin = getenv("A2GSPU_RUN_BIN");

    // SNAP_LOAD: short-circuit the ~1200-frame boot by restoring a prior desktop snapshot.
    // Runs BEFORE the boot loops below; the existing post-restore frame loops then self-heal
    // the (intentionally excluded) scanner/clock/device state before $E1 is read.
    if (snap_load) {
        FILE *sf = fopen(snap_load, "rb");
        if (!sf) { printf("A2GSPU SNAP: could not open '%s' for load\n", snap_load); }
        else {
            bool cpu_ok = a2gspu_cpu_load(sf, computer->cpu);
            bool mmu_ok = (cpu_ok && state->mmu_iigs) ? state->mmu_iigs->A2GSPU_restore(sf) : false;
            bool end_ok = (cpu_ok && mmu_ok) ? a2gspu_snap_check_sentinel(sf) : false;
            fclose(sf);
            if (!cpu_ok || !mmu_ok || !end_ok) {
                printf("A2GSPU SNAP: restore FAILED from '%s' (cpu=%s mmu=%s end=%s) -- bad/stale magic, "
                       "geometry mismatch, or truncated snapshot; aborting.\n", snap_load,
                       cpu_ok ? "ok" : "BAD-MAGIC", mmu_ok ? "ok" : "BAD",
                       end_ok ? "ok" : "TRUNC");
                exit(2);
            }
            computer->cpu->halt = 0;          // force-run (HLT would no-op run_one_frame)
            // re-arm the trace window so it covers ONLY the post-restore run (mirror of
            // the inject re-arm on the runbin path)
            bus_trace_reset(); slot_bus_reset(); mmu_state_trace_reset();
            printf("A2GSPU SNAP: restored from '%s'; skipping boot.\n", snap_load);
        }
    }

    if (runbin) {
        char binpath[1024];
        strncpy(binpath, runbin, sizeof(binpath) - 1);
        binpath[sizeof(binpath) - 1] = 0;
        uint32_t loadaddr = 0x7E0000;
        char *at = strchr(binpath, '@');
        if (at) { *at = 0; loadaddr = (uint32_t)strtoul(at + 1, nullptr, 16); }
        int bootframes = state->spike_frames / 2;
        const char *bf = getenv("A2GSPU_RUN_BOOT");
        if (bf) bootframes = atoi(bf);
        if (bootframes < 0) bootframes = 0;
        if (bootframes > state->spike_frames) bootframes = state->spike_frames;

        int i = 0;
        if (!snap_load) {   // SNAP_LOAD already provided a booted desktop; skip the long boot
            for (; i < bootframes; i++) {
                iigs_itrace_frame_tick(i);
                if (!run_one_frame(computer)) { printf("SPIKE: halted during boot at frame %d\n", i); break; }
            }
        }
        FILE *rb = (state->mmu_iigs) ? fopen(binpath, "rb") : nullptr;
        if (rb) {
            int n = 0, c;
            while ((c = fgetc(rb)) != EOF) { state->mmu_iigs->write(loadaddr + n, (uint8_t)c); n++; }
            fclose(rb);
            bus_trace_reset(); slot_bus_reset(); mmu_state_trace_reset();   // isolate the injected program
            computer->cpu->full_pc = loadaddr;                              // jump to it (pb = addr>>16)
            printf("A2GSPU RUN: injected %d bytes at $%06X after %d boot frames; PC set.\n",
                   n, loadaddr, i);
        } else if (!state->mmu_iigs) {
            printf("A2GSPU RUN: no IIgs MMU on this platform -- skipped (use -p 5)\n");
        } else {
            printf("A2GSPU RUN: could not open binary '%s'\n", binpath);
        }
        for (; i < state->spike_frames; i++) {
            iigs_itrace_frame_tick(i);
            if (!run_one_frame(computer)) { printf("SPIKE: emulation halted early at frame %d\n", i); break; }
        }
        printf("A2GSPU RUN: final CPU full_pc=$%06X (if ~= the inject addr, the injected code ran)\n",
               (unsigned)computer->cpu->full_pc);
    } else {
        for (int i = 0; i < state->spike_frames; i++) {
            iigs_itrace_frame_tick(i);
            if (!run_one_frame(computer)) {
                printf("SPIKE: emulation halted early at frame %d\n", i);
                break;
            }
        }
    }
    // SNAP_SAVE: after BOTH boot paths, post run_one_frame mutation, pre trace-disarm.
    // The assert/golden gate further below still runs, so a SNAP_SAVE run can be golden-blessed.
    if (snap_save) {
        FILE *sf = fopen(snap_save, "wb");
        if (!sf) { printf("A2GSPU SNAP: could not open '%s' for save\n", snap_save); }
        else if (!state->mmu_iigs) { printf("A2GSPU SNAP: no IIgs MMU (use -p 5)\n"); fclose(sf); }
        else {
            a2gspu_cpu_save(sf, computer->cpu);
            state->mmu_iigs->A2GSPU_snapshot(sf);
            a2gspu_snap_write_sentinel(sf);   // trailing magic -> truncation is detectable on load
            fclose(sf);
            printf("A2GSPU SNAP: saved machine state to '%s'\n", snap_save);
        }
    }

    g_bus_trace_enabled = false;  // disarm before any teardown writes
    g_slot_bus_enabled = false;
    g_mmu_state_trace_enabled = false;

    // ---- (1) renderer-free $E1 oracle ----
    uint8_t *m2 = state->mmu_iigs ? state->mmu_iigs->get_megaii_memory_base() : nullptr;
    if (m2) {
        const uint8_t *e1 = m2 + 0x10000;          // Mega II bank $E1 (64 KB)
        FILE *f = fopen("spike_e1.bin", "wb");
        if (f) { fwrite(e1, 1, 0x10000, f); fclose(f); }
        uint64_t hash = HOUSE_FNV_BASIS;    // FNV-1a 64
        int nonzero = 0; uint8_t seen[256] = {0}; int ndist = 0;
        for (int a = 0x2000; a <= 0x9FFF; a++) {
            uint8_t b = e1[a];
            hash = (hash ^ b) * HOUSE_FNV_PRIME;
            if (b) nonzero++;
            if (!seen[b]) { seen[b] = 1; ndist++; }
        }
        printf("SPIKE E1: wrote spike_e1.bin (64KB).\n");
        printf("SPIKE E1: SHR-window $2000-$9FFF nonzero=%d/32768 distinct=%d hash=%016llX\n",
               nonzero, ndist, (unsigned long long)hash);
        printf("SPIKE E1: SCB[0..7]@$9D00 = %02X %02X %02X %02X %02X %02X %02X %02X\n",
               e1[0x9D00], e1[0x9D01], e1[0x9D02], e1[0x9D03],
               e1[0x9D04], e1[0x9D05], e1[0x9D06], e1[0x9D07]);
        // Headless SHR/video-state summary (mode/palette/pixel histogram).
        if (SDL_getenv("A2GSPU_VIDEOSUM")) iigs_video_summary(e1);
        // Downsampled ASCII map of the screen (SEE a rectangle/layout headless).
        if (SDL_getenv("A2GSPU_VIDEOMAP")) iigs_video_map(e1);
    } else {
        printf("SPIKE E1: mmu_iigs/megaii base is NULL -- FAILED\n");
    }

    // ---- (1.5) bus-trace oracle: ordered SHR-write golden ----
    {
        uint64_t n = 0;
        uint64_t th = bus_trace_dump("spike_trace.bin", &n);
        uint64_t c0 = g_bus_trace.empty() ? 0 : g_bus_trace.front().cycle;
        uint64_t c1 = g_bus_trace.empty() ? 0 : g_bus_trace.back().cycle;
        printf("SPIKE TRACE: wrote spike_trace.bin (%llu SHR writes) content-hash=%016llX\n",
               (unsigned long long)n, (unsigned long long)th);
        printf("SPIKE TRACE: cycle span [%llu .. %llu]\n",
               (unsigned long long)c0, (unsigned long long)c1);
        // Bracket the shadow-mirror slot-visibility outcome (provenance-tagged, before any hardware).
        uint64_t ha = 0, hd = 0, nd = 0, ns = 0;
        bus_trace_brackets(&ha, &hd, &nd, &ns);
        printf("SPIKE BRACKET: shadow-VISIBLE   (naked M2B0 sees ALL)      hash=%016llX  writes=%llu  miss=0\n",
               (unsigned long long)ha, (unsigned long long)(nd + ns));
        printf("SPIKE BRACKET: shadow-INVISIBLE (naked M2B0, direct-$E1 only) hash=%016llX  writes=%llu  MISS=%llu shadowed\n",
               (unsigned long long)hd, (unsigned long long)nd, (unsigned long long)ns);
    }

    // ---- (1.6) faithful slot-bus stream (the virtual slot; superset of the SHR oracle) ----
    {
        uint64_t n = 0;
        uint64_t sh = slot_bus_dump("spike_slot.bin", &n);
        printf("SPIKE SLOT: wrote spike_slot.bin (%llu Mega-II writes) content-hash=%016llX\n",
               (unsigned long long)n, (unsigned long long)sh);
    }

    // ---- (1.7) ground-truth MMU-state stream (the bus-snoop comparator's authoritative reference) ----
    {
        uint64_t n = 0;
        uint64_t mh = mmu_state_trace_dump("spike_mmu_truth.bin", &n);
        printf("SPIKE MMU: wrote spike_mmu_truth.bin (%llu mapping-state changes) content-hash=%016llX\n",
               (unsigned long long)n, (unsigned long long)mh);
    }

    // ---- (2) backbuffer pixel-readback datum ----
    video_system_t *vs = computer->video_system;
    vs->update_display(true);
    SDL_ClearError();
    vs->save_screenshot("spike_frame.bmp");
    const char *err = SDL_GetError();
    printf("SPIKE FRAMEBUF: save_screenshot('spike_frame.bmp') SDL_GetError='%s'\n",
           (err && *err) ? err : "(none)");

    // ---- (3) optional headless memory-range hexdump + final CPU state ----
    if (const char *dg = SDL_getenv("A2GSPU_DUMP")) {
        uint8_t *mb = state->mmu_iigs ? state->mmu_iigs->get_megaii_memory_base() : nullptr;
        iigs_mem_range_dump(computer->cpu, mb, dg);
    }
    if (g_iigs_brkdump_enabled) iigs_cpu_state_dump_regs(computer->cpu, "SPIKE-END");

    // ---- (4) golden-diff (#9) + assertion gate (#2) -> exit code (CI loop) ----
    {
        uint8_t *m2a = state->mmu_iigs ? state->mmu_iigs->get_megaii_memory_base() : nullptr;
        if (m2a) {
            const uint8_t *e1a = m2a + 0x10000;
            int nz = 0; uint8_t sn[256] = {0}; int nd = 0;
            uint64_t h = HOUSE_FNV_BASIS;   // house FNV basis
            for (int a = 0x2000; a <= 0x9FFF; a++) {
                uint8_t b = e1a[a]; h = (h ^ b) * HOUSE_FNV_PRIME;
                if (b) nz++; if (!sn[b]) { sn[b] = 1; nd++; }
            }
            int gate_rc = 0; bool any_gate = false;
            if (const char *gf = SDL_getenv("A2GSPU_GOLDEN")) {
                any_gate = true;
                FILE *gp = fopen(gf, "r");
                if (gp) {
                    unsigned long long g = 0;
                    if (fscanf(gp, "%llx", &g) == 1) {
                        int match = (g == h);
                        printf("IIGS GOLDEN: %s (cur=%016llX want=%016llX)\n",
                               match ? "MATCH" : "DIFF", (unsigned long long)h, g);
                        if (!match) gate_rc = 1;   // the SHR golden now GATES the exit
                    } else {
                        // The golden file exists/opens but its first token is not hex
                        // (empty/blank/corrupt). Treat that as a HARD gate failure, not a
                        // silent PASS — a corrupt golden must never disable the gate. This
                        // is distinct from "file absent -> bless" (the fopen("w") branch).
                        printf("IIGS GOLDEN: ERROR — golden file '%s' exists but contains no "
                               "parseable hash (cur=%016llX); failing gate.\n",
                               gf, (unsigned long long)h);
                        gate_rc = 1;
                    }
                    fclose(gp);
                } else if ((gp = fopen(gf, "w"))) {
                    fprintf(gp, "%016llX\n", (unsigned long long)h); fclose(gp);
                    printf("IIGS GOLDEN: blessed %s = %016llX\n", gf, (unsigned long long)h);
                }
            }
            if (const char *as = SDL_getenv("A2GSPU_ASSERT")) {
                any_gate = true;
                gate_rc |= iigs_eval_asserts(as, computer->cpu, e1a, nz, nd);
            }
            // machine-readable status line for the corpus harness (exit code stays
            // gate-driven for harness compat; the status NAME is the richer category)
            long scb = (e1a[0x9D00] & 0x80) ? 640 : 320;
            const char *st = (any_gate && gate_rc) ? "GATE_FAIL"
                           : g_iigs_brk_count       ? "CRASH_BRK"
                           : g_iigs_last_gsos_err    ? "GSOS_ERROR" : "OK";
            iigs_emit_status(st, any_gate ? gate_rc : 0,
                             any_gate ? (gate_rc ? "FAIL" : "PASS") : "none",
                             g_iigs_last_gsos_err, g_iigs_brk_count, scb, h);
            if (any_gate) {
                printf("=== SPIKE COMPLETE (gate %s) ===\n", gate_rc == 0 ? "PASS" : "FAIL");
                exit(gate_rc);
            }
        }
    }

    printf("=== SPIKE COMPLETE ===\n");
}

/* ========================================================================
   SDL3 App Callback Entry Points
   ======================================================================== */

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    std::cout << "Booting GSSquared!" << std::endl;

    SDL_SetAppMetadata("GSSquared", VERSION_STRING, "Copyright 2025-2026 by Jawaid Bazyar");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_COPYRIGHT_STRING, "Copyright 2025-2026 by Jawaid Bazyar");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_CREATOR_STRING, "Jawaid Bazyar");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_URL_STRING, "https://github.com/jawaidbazyar/gssquared");
    // for the scrollback in the debugger.
    SDL_SetHint(SDL_HINT_MAC_SCROLL_MOMENTUM, "1");

    GS2AppState *state = new GS2AppState();
    
    int platform_id = PLATFORM_APPLE_II_PLUS;  // default to Apple II Plus
    bool platform_explicit = false;            // true when -p was given on CLI
    int config_index = -1;                     // -c N: select builtin system config by index
    int opt;
    int slot, drive;

    if (isatty(fileno(stdin))) {
        gs2_app_values.console_mode = true;
    }
    Paths::initialize(gs2_app_values.console_mode);

    gs2_app_values.base_path = get_base_path(gs2_app_values.console_mode);
    printf("base_path: %s\n", gs2_app_values.base_path.c_str());
    gs2_app_values.pref_path = get_pref_path();
    printf("pref_path: %s\n", gs2_app_values.pref_path.c_str());

    { // always parse CLI args so headless/automation launches honor -p/-n (no TTY needed)
        // parse command line options
        while ((opt = getopt(argc, argv, "snxp:d:c:")) != -1) {
            switch (opt) {
                case 'p':
                    platform_id = std::stoi(optarg);
                    platform_explicit = true;
                    break;
                case 'd':
                    {
                        std::string filename;
                        std::string arg_str(optarg);
                        // Using regex for better parsing
                        std::regex disk_pattern("s([0-9]+)d([0-9]+)=(.+)");
                        std::smatch matches;
                    
                        if (std::regex_match(arg_str, matches, disk_pattern) && matches.size() == 4) {
                            slot = std::stoi(matches[1]);
                            drive = std::stoi(matches[2]) - 1;
                            filename = matches[3];
                            //std::cout << std::format("Mounting disk {} in slot {} drive {}\n", filename, slot, drive) << std::endl;
                            std::cout << "Mounting disk " << filename << " in slot " << slot << " drive " << drive << std::endl;
                            state->disks_to_mount.push_back({ (uint16_t)slot, (uint16_t)drive, filename});
                        }
                    }
                    break;
                /* case 'x':
                    gs2_app_values.disk_accelerator = true;
                    break; */
                case 's':
                    gs2_app_values.sleep_mode = true;
                    break;
                case 'n':
                    gs2_app_values.no_input = true;
                    break;
                case 'c':
                    config_index = std::stoi(optarg);
                    platform_explicit = true;  // reuse the auto-launch path
                    break;
                default:
                    std::cerr << "Usage: " << argv[0] << " [-p platform] [-dsXdY=filename] [-s]\n";
                    std::cerr << "  -p N: skip the system-selector UI and auto-launch the\n";
                    std::cerr << "        first builtin system that matches the given platform.\n";
                    std::cerr << "        Closing the emulator window then quits the app rather\n";
                    std::cerr << "        than returning to the selector. Valid N:\n";
                    std::cerr << "          0 = Apple II         3 = Apple IIe Enhanced\n";
                    std::cerr << "          1 = Apple II Plus    4 = Apple IIe 65816\n";
                    std::cerr << "          2 = Apple IIe        5 = Apple IIgs\n";
                    std::cerr << "  -dsXdY=filename: mount disk image `filename` in slot X drive Y.\n";
                    std::cerr << "        Drives are 1-indexed; e.g. -ds6d1=foo.dsk for the\n";
                    std::cerr << "        first drive of the controller in slot 6.\n";
                    std::cerr << "  -s: sleep mode (don't busy-wait, sleep)\n";
                    return SDL_APP_FAILURE;
            }
        }
    }

    gs2_app_values.menu_event_type = SDL_RegisterEvents(1);

    state->platform_id = platform_id;

    // a2gspu headless-boot spike: env-gated, leaves normal launch untouched.
    {
        const char *sf = SDL_getenv("A2GSPU_SPIKE_FRAMES");
        if (sf) {
            int n = SDL_atoi(sf);
            if (n > 0) {
                state->headless = true;
                state->spike_frames = n;
                printf("A2GSPU SPIKE: headless mode enabled, %d frames\n", n);
            }
        }
    }

    // Debug print mounted media
    std::cout << "Mounted Media (" << state->disks_to_mount.size() << " disks):" << std::endl;
    for (const auto& disk_mount : state->disks_to_mount) {
        std::cout << " Slot " << disk_mount.slot << " Drive " << disk_mount.drive << " - " << disk_mount.filename << std::endl;
    }

    state->computer = new computer_t(nullptr); // We'll set the clock later.

    video_system_t *vs = state->computer->video_system;

    initMenu(vs->window);

    state->aa = new AssetAtlas_t(vs->renderer, "img/atlas.png");
    state->aa->set_elements(MainAtlas_count, asset_rects);

    state->select_system = new SelectSystem(vs, state->aa);

    // Let vsync throttle the selection UI instead of spinning.
    SDL_SetRenderVSync(vs->renderer, 1);
    state->phase = PHASE_SYSTEM_SELECT;

    // If the caller passed `-p PLATFORM`, skip the system-selector UI
    // and jump straight into emulation using the first builtin system
    // whose platform_id matches.
    if (platform_explicit) {
        const int system_id = (config_index >= 0)
                                  ? config_index
                                  : find_first_system_for_platform(platform_id);
        if (system_id >= 0 && system_id < NUM_SYSTEM_CONFIGS) {
            std::cout << "Auto-launching system_id=" << system_id
                      << " (" << get_system_config(system_id)->name << ")" << std::endl;
            transition_to_emulation(state, system_id);
            state->auto_launched = true;
        } else {
            std::cerr << "No system config for index/platform "
                      << system_id << ", staying in selector\n";
        }
    }

    *appstate = state;

    // Register callback so emulation continues during macOS menu tracking and window resize
    setMenuTrackingCallback(SDL_AppIterate, state);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    GS2AppState *state = (GS2AppState *)appstate;

    // Let the platform menu consume the event first (Linux hamburger/right-click)
    if (handleMenuEvent(event)) return SDL_APP_CONTINUE;

    if (state->phase == PHASE_SYSTEM_SELECT) {
        state->select_system->event(*event);
        if (event->type == SDL_EVENT_QUIT) {
            return SDL_APP_SUCCESS; // clean exit
        }
        return SDL_APP_CONTINUE;
    }

    if (state->phase == PHASE_EMULATION) {
        computer_t *computer = state->computer;
        cpu_state *cpu = computer->cpu;

        // In no-input mode (automation/headless), ignore keyboard/mouse; only
        // handle quit + window events.
        if (gs2_app_values.no_input) {
            if (event->type == SDL_EVENT_QUIT) {
                cpu->halt = HLT_USER;
            } else if (event->type >= SDL_EVENT_WINDOW_FIRST && event->type <= SDL_EVENT_WINDOW_LAST) {
                handle_single_event(computer, cpu, *event);
            }
        } else {
            handle_single_event(computer, cpu, *event);
        }

        // handled in computer now
        /* if (event->type == SDL_EVENT_QUIT) {
            cpu->halt = HLT_USER;
        } */
        return SDL_APP_CONTINUE;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    GS2AppState *state = (GS2AppState *)appstate;

    // a2gspu headless-boot spike: run once then exit, bypassing the GUI loop.
    if (state->headless) {
        if (state->phase == PHASE_EMULATION) {
            run_headless_spike(state);
            return SDL_APP_SUCCESS;
        }
        printf("SPIKE: phase=%d not PHASE_EMULATION -- auto-launch (-p) failed\n",
               (int)state->phase);
        return SDL_APP_FAILURE;
    }

    // Pump any pending GTK/GDK events (Linux menu). Called here rather than
    // in SDL_AppEvent to avoid blocking SDL's X11 connection (deadlock risk).
    pumpMenuEvents();

    if (state->phase == PHASE_SYSTEM_SELECT) {
        /* Render the selection UI (one frame). Events already dispatched by SDL_AppEvent. */
        video_system_t *vs = state->computer->video_system;

        if (state->select_system->update()) {
            SDL_SetRenderDrawColor(vs->renderer, 0, 0, 0, 255);
            vs->clear();
            state->select_system->render();
            vs->present();
        }

        int system_id = state->select_system->get_selected_system();
        if (system_id == SELECT_QUIT) {
            return SDL_APP_SUCCESS; // user closed window during selection
        }
        if (system_id >= 0) {
            transition_to_emulation(state, system_id);
        }
        SDL_Delay(16);
        return SDL_APP_CONTINUE;
    }

    if (state->phase == PHASE_EMULATION) {
        computer_t *computer = state->computer;

        osd->update();

        if (!run_one_frame(computer)) {
            // User requested halt. Always run transition_to_shutdown
            // so the trace buffer is saved and the computer/MMUs are
            // properly destroyed. Then either go back to the selector
            // (interactive flow) or exit the app (we auto-launched
            // via -p PLATFORM and have no selector to return to).
            transition_to_shutdown(state);
            if (state->auto_launched) {
                return SDL_APP_SUCCESS;
            }
        }
        return SDL_APP_CONTINUE;
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    //(void)result;
    GS2AppState *state = (GS2AppState *)appstate;
    if (!state) return;

    if (osd) {
        delete osd;
        osd = nullptr;
    }
    if (state->computer) {
        delete state->computer;
        state->computer = nullptr;
    }

    // Clean up MMUs if they exist (e.g., quit during emulation)
    delete state->mmu_ii;
    delete state->mmu_iie;
    delete state->mmu_iigs;

    delete state->select_system;
    delete state->aa;
    delete state;
    // SDL_Quit() is called automatically by SDL after this returns.
}
