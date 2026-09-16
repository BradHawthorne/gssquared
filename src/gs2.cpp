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

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>   // main() and the SDL_App* callbacks: this unit only

#include "gs2_app.hpp"

// The record contract's clock and frame, registered at arm so a rail that holds
// only a cpu_state (WATCH, CALLSTREAM) can still stamp every record.
static NClock *s_a2record_clock = nullptr;
void a2record_register(NClock *clk) {
    s_a2record_clock = clk;
    a2record::g_cycles_fn = [] { return (unsigned long long)s_a2record_clock->get_cycles(); };
    a2record::g_frame_fn  = [] { return g_iigs_cur_frame; };
    a2record::arm();   // opens A2GSPU_RECORDS; fatal if it cannot
}

// Strict mode's readback (M8). The warnings counter lives with the diagnostics
// in iigs_diag.hpp; the rail is judged here, at spike end, so it registers here.
static a2rails::Registrar s_reader_strict{"A2GSPU_STRICT", [](char *b, size_t n) {
    snprintf(b, n, "status=OK warnings=%d strict=%d", g_a2gspu_warnings, getenv("A2GSPU_STRICT") ? 1 : 0);
}};

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

// The machine the observer helpers read display state from (a2gspu_observe.hpp).
// Refreshed every frame rather than at the once-per-process arm: a windowed
// session can power off one machine and start another, and a pointer captured
// at the first frame would outlive the computer it points into.
static computer_t *s_a2obs_computer = nullptr;
static int a2obs_video_flags_from_scanner() {
    if (!s_a2obs_computer) return -1;
    display_state_t *d = (display_state_t *)s_a2obs_computer->cached_display_state;
    VideoScannerII *vs = d ? d->video_scanner : nullptr;
    if (!vs) return -1;
    return (vs->is_text() ? 1 : 0) | (vs->is_mixed() ? 2 : 0) | (vs->is_page_2() ? 4 : 0) | (vs->is_hires() ? 8 : 0);
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

    // WIZ5 / A2GSPU_TAP lazy arm — run_one_frame is the SINGLE funnel for BOTH
    // the windowed loop and the headless spike, so one guarded call arms both
    // paths. Both arm helpers self-guard for idempotency (the spike also arms
    // the generic tap earlier); no-op with no A2GSPU_TAP*/WIZ5_* env set, so the
    // stock emulator is unchanged (dual-mode invariant).
    s_a2obs_computer = computer;                 // this frame's machine (see above)
    g_a2obs_video_flags = a2obs_video_flags_from_scanner;
    g_a2gspu_cpu_for_names = computer->cpu;      // likewise: the namer probes the live CPU's MMU
    {
        static bool s_wiz5_armed = false;
        if (!s_wiz5_armed) {
            s_wiz5_armed = true; a2record_register(computer->clock); generic_tap_arm(); wiz5_config_arm(computer); a2gspu_trap_arm();
            a2gspu_rail_arm_env("A2GSPU_WATCH_IPC_ZP");   // every driver path, before the first watched store
            a2gspu_rail_arm_env("A2GSPU_CARD_CMDLOG");    // before the first host write to a card's command register
            // platform from the MMU itself: only a IIgs MMU can see the Mega II image at $E1
            a2gspu_liveness_arm(computer->cpu->mmu->probe_readable(0xE12000));
            g_a2gspu_cpu_for_names = computer->cpu;
            // Names are loaded when a rail that prints them is armed, and otherwise
            // on the first lookup (a2gspu_name_pc seeds on demand). Loading them
            // unconditionally hashed the ROM, read two tables and printed
            // "IIGS SYMS:" on every stock windowed launch -- a IIe included --
            // which is instrumentation work with no A2GSPU_* set.
            if (g_trap_on) iigs_names_ready();
            if (getenv("A2GSPU_BRKMEM")) g_brkmem_on = true;   // BRK reads the flag, not the env, per BRK
        }
    }

    if (cpu->halt == HLT_USER) { // top of frame.
        return false;
    }

    uint64_t c14M_per_frame = clock->get_c14m_per_frame();

    if (computer->execution_mode == EXEC_PAUSED) {
        return true;
    }

    // A2GSPU_TAP_SESSION: periodic memory-capture service (env-gated; no-op unless
    // a session dir was set). Runs once per executed frame in every driver loop
    // (windowed + headless spikes) since they all funnel through run_one_frame.
    if (g_gtap.sess_on) generic_tap_session_frame(cpu, clock->get_cycles());

    // WIZ5_KEYS: per-frame scheduled keyboard injection (env-gated — returns
    // immediately when no key schedule is loaded). Shares this funnel so it
    // drives the game identically in the windowed loop and the headless spike.
    wiz5_keys_tick(computer, g_wiz5_frame++);

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
            while (clock->get_c14m() < clock->get_frame_end_c14M() && !cpu->halt) { // 1/60th second. (halt: see the fast loop below)
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
            // `&& !cpu->halt`: a halt raised INSIDE the frame (a rail breakpoint,
            // STOP_ON_FAULT) stops here, at its instruction, instead of running the
            // rest of the frame first. Stock code only halts between frames (quit
            // events), so without a rail this never changes where a frame ends.
            while (clock->get_c14m() < clock->get_frame_end_c14M() && !cpu->halt) {
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
        // Only for a frame that ran to its end. The cycle-based update consumes a
        // whole frame's scanner data; called on a frame a halt stopped part way
        // (a rail breakpoint), it corrupted the heap within a handful of hits
        // (measured: STATUS_HEAP_CORRUPTION after 7). Upstream's debugger never
        // reaches this with a partial frame -- its breakpoint switches to
        // EXEC_STEP_INTO -- so the guard keeps stock behaviour exactly: stock
        // frames always complete. The halted frame's pixels are emitted when a
        // later run finishes it.
        if (computer->execution_mode != EXEC_STEP_INTO &&
            clock->get_c14m() >= clock->get_frame_end_c14M()) {
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

        if (g_a2gspu_pace_fast) {
            // A2GSPU_PACE=fast (a2gspu_pace.hpp; the headless default): no wait for
            // the wall clock, and the audio this frame generated is dropped rather
            // than left for a paused device to never drain.
            if (computer->audio_system) computer->audio_system->clear_all_streams();
        } else {
            frame_sleep(computer, computer->last_cycle_time, frame_length_ns);
        }
        computer->last_cycle_time = SDL_GetTicksNS(); 

    } else { // Ludicrous Speed!

        // TODO: how to handle VBL timing here. estimate it based on realtime?
        computer->set_frame_start_cycle(); // todo: unsure if this is right..
        uint64_t frame_length_ns = (computer->frame_count & 1) ? clock->get_us_per_frame_odd() : clock->get_us_per_frame_even();
        uint64_t next_frame_time = computer->last_cycle_time + frame_length_ns;

        computer->last_start_frame_c14m = clock->get_frame_start_c14M();
        
        if (computer->debug_window->window_open) {
            while (SDL_GetTicksNS() < next_frame_time && !cpu->halt) { // run emulated frame, but of course we don't sleep in this loop so we'll Go Fast. (halt: see above)
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
            while (SDL_GetTicksNS() < next_frame_time && !cpu->halt) { // run emulated frame, but of course we don't sleep in this loop so we'll Go Fast. (halt: see above)
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

    // A2GSPU_LIVENESS: hash the screen every N frames so a run that stopped
    // progressing is visible in its own output. One untaken branch when unset.
    if (g_liveness_every) a2gspu_liveness_tick(computer);

    return true;
}

/* ========================================================================
   App State and Phase Machine for SDL3 App Callbacks
   ======================================================================== */

gs2_app_t gs2_app_values;

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
    // Kept for the PINS line: the ROM actually loaded is part of a run's identity.
    g_a2gspu_rom_data = rd ? (const uint8_t *)rd->main_rom_data : nullptr;
    g_a2gspu_rom_size = (rd && rd->main_rom_file) ? (size_t)rd->main_rom_file->size() : 0;
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
            // Contiguous FPI RAM is motherboard base (ROM-dependent: 128K on
            // ROM01, 1M on ROM03) PLUS the expansion card -- not a flat 8MB
            // total, which under-allocated and left the banks above $7F
            // floating on a machine that should have them. Matches KEGS.
            // (Ported from upstream e0729c9, with its iigs_memory.hpp.)
            const size_t fast_ram = iigs_memory::fast_ram_bytes(romsize);
            printf("IIgs RAM: %s mobo %zuKB + exp %zuMB = %zu bytes (banks $00-$%02X)\n",
                   iigs_memory::is_rom03(romsize) ? "ROM03" : "ROM01",
                   iigs_memory::mobo_ram_bytes(romsize) / 1024,
                   iigs_memory::kDefaultExpBytes / (1024 * 1024),
                   fast_ram, iigs_memory::last_ram_bank(fast_ram));
            state->mmu_iigs = new MMU_IIgs(256, (int) fast_ram, (uint32_t) romsize, /* (uint8_t *) */rd->main_rom_data, state->mmu_iie);
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
        // A headless run whose disk is not there boots something else under the
        // right name and reports OK. A missing image is fatal when nobody is
        // watching (-n); windowed, it is a warning and the machine boots empty.
        const bool present = std::filesystem::exists(disk_mount.filename);
        const bool mounted = present && computer->mounts->mount_media(disk_mount);
        if (!mounted) {
            char msg[1200];
            snprintf(msg, sizeof msg, "%s '%s' (slot %u drive %u)",
                     present ? "could not mount" : "no such disk image",
                     disk_mount.filename.c_str(), (unsigned)disk_mount.slot, (unsigned)disk_mount.drive);
            if (gs2_app_values.no_input) {
                fprintf(stderr, "A2GSPU MOUNT: FAIL %s -- aborting (exit 2)\n", msg);
                exit(2);
            }
            a2gspu_warn("A2GSPU MOUNT", msg);
        }
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

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    /* STDOUT IS UNBUFFERED FROM HERE. The whole CTRL rail depends on it.
     *
     * session.ps1 launches with -RedirectStandardOutput <dir>/session.log, so
     * stdout is a FILE, and a FILE gets FULL block buffering by default. Nothing
     * in src/ ever called setvbuf, so every diagnostic the process printed after
     * startup sat in a 4 KB buffer until exit. MEASURED: a live IIgs session that
     * had answered `run`, `save` and `restore` had a 4283-byte session.log whose
     * last line was "IIgs init_video_addresses()" -- the last line in the tree
     * that carries a hand-placed fflush(stdout). The CTRL banner, the snapshot
     * manifest (the ONLY place class=/included=/excluded= is ever reported), the
     * device list, the IIGS SYMS tool-pointer-table walks, MEMVU summaries and
     * SPIKE KEYS lines all appeared only after `quit`, at line 128 onward. An
     * agent following Docs/A2GSPU_RAILS.md to "read the manifest in the session
     * log" found no manifest and would conclude the snapshot had none -- Event D
     * of INSTRUMENTATION_SHAPE.md section 1, recreated by a buffer. Worse, a
     * session that is killed rather than quitting loses the buffer outright: the
     * instrument answered and the answer was never written down (P2, P3).
     *
     * _IONBF, not _IOLBF, DELIBERATELY. This is the trap that makes the obvious
     * fix a no-op on the platform the failure was measured on: the Windows CRT
     * documents _IOLBF as EQUIVALENT TO _IOFBF -- it does not line-buffer. A
     * setvbuf(_IOLBF) here would compile, return 0, look right in review and
     * change nothing. Unbuffered costs a write per printf on chatty rails; the
     * high-volume streams (ITRACE, WATCH, TRAP, the record contract) already
     * write to stderr or to their own sinks, so what pays the cost is exactly
     * the low-volume out-of-band text that is worthless if it arrives late.
     *
     * Must be the first statement: setvbuf is only defined before any other
     * operation on the stream. If it fails we say so on stderr rather than
     * carrying on silently -- a silence about silence is the failure twice. Not
     * routed through a2gspu_warn(): that counts into GSDIAG warnings= and would
     * make A2GSPU_STRICT exit 3 over a buffering mode. */
    if (setvbuf(stdout, nullptr, _IONBF, 0) != 0) {
        fprintf(stderr, "A2GSPU WARNING: setvbuf(stdout, _IONBF) failed -- stdout stays "
                        "block-buffered, so out-of-band diagnostics (the snapshot manifest, "
                        "CTRL banners, SYMS walks) will not reach a redirected log until exit, "
                        "and will be LOST if this process is killed.\n");
        fflush(stderr);
    }
    std::cout << "Booting GSSquared!" << std::endl;

    g_a2gspu_argv0 = argv[0];   // so the spike can hash the executable it is running in
    g_a2gspu_build_version = BUILD_VERSION;
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
    g_a2gspu_base_path = gs2_app_values.base_path;   // the symbol tables live beside the ROMs (a2gspu_pins.hpp)
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
        // Interactive control rail also runs headless (stepping protocol).
        if (SDL_getenv("A2GSPU_CTRL")) {
            state->headless = true;
            printf("A2GSPU CTRL: headless interactive mode\n");
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
        // THE WINDOWED FRAME CLOCK. Records stamp g_iigs_cur_frame and ITRACE_FRAME
        // arms from it, but only the spike and CTRL `run` ever advanced it, so every
        // record a windowed session wrote said frame 0. An executed frame counts;
        // a paused one does not. Instrument state only, never the machine's.
        else if (state->computer->execution_mode != EXEC_PAUSED) {
            static int s_windowed_frame = 0;
            iigs_itrace_frame_tick(++s_windowed_frame);
        }
        // state->computer, not the local: transition_to_shutdown() above deletes
        // the computer and nulls the state's pointer, and the poll must see that.
        if (!state->computer) s_a2obs_computer = nullptr;   // shut down: nothing to observe
        a2gspu_copilot_poll(state->computer);
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
