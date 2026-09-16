#include <SDL3/SDL.h>

#include "computer.hpp"
#include "a2gspu_liveness_taps.hpp"   // A2GSPU_LIVENESS: last keyboard poll (off => 1 branch)
#include "obs_signal.hpp"   // Observatory: obs_add_memwindow for the ADB uC RAM window
#include "videosystem.hpp"
#include "mmus/mmu_iigs.hpp"

#include "devices/adb/keygloo.hpp"
#include "devices/adb/ADB_Mouse.hpp"
#include "util/DebugHandlerIDs.hpp"
#include "debug.hpp"


// Compute mouse scale from window render dimensions (A2GSPU SHR-cursor path).
// Converts logical-pixel SDL deltas to emulated-pixel deltas (divide by render
// scale); 1.1x empirical correction for GS/OS Toolbox acceleration.
static void update_mouse_scale(keygloo_state_t *kb_state) {
    ADB_Mouse *mouse = kb_state->kg->get_adb_mouse();
    video_system_t *vs = kb_state->computer->video_system;
    if (!mouse || !vs || !vs->window) return;

    float render_sx = 1.0f, render_sy = 1.0f;
    SDL_GetRenderScale(vs->renderer, &render_sx, &render_sy);

    mouse->set_mouse_scale(1.1f / render_sx, 1.1f / render_sy);
}

void keygloo_update_interrupt_status(keygloo_state_t *kb_state, KeyGloo *kg ) {
    // The mouse interrupt IS accounted for: KeyGloo::update_interrupt_status()
    // folds (mouse_interrupt_enabled && mouse_data_full) into the same
    // interrupt_asserted that interrupt_status() returns here. (A "TODO: check
    // if mouse interrupt is enabled, and if so, assert it" sat on this line and
    // described work the code above it already does.)
    if (kg->interrupt_status()) {
        kb_state->irq_control->set_irq(IRQ_ID_KEYGLOO, true);
    } else {
        kb_state->irq_control->set_irq(IRQ_ID_KEYGLOO, false);
    }
    if (kg->data_interrupt_status()) {
        kb_state->irq_control->set_irq(IRQ_ID_ADB_DATAREG, true);
    } else {
        kb_state->irq_control->set_irq(IRQ_ID_ADB_DATAREG, false);
    }
}

// A2GSPU: keyboard soft-switch read counters — answers "which switch does the
// guest poll?" (RTSTRP vs AP1.3) without a full MMU I/O log. Read via the ctrl
// 'iolog' verb; diff two reads across a run to see what a wedged menu polls.
uint64_t g_kg_reads[5] = {0};   // [0]=C000 [1]=C010 [2]=C024 [3]=C025 [4]=C026
void a2gspu_keygloo_read_counts(uint64_t out[5]) { for (int i = 0; i < 5; i++) out[i] = g_kg_reads[i]; }

uint8_t keygloo_read_C000(void *context, uint32_t address) {
    g_kg_reads[0]++;
    if (g_liveness_taps) a2gspu_liveness_note_kbd();
    keygloo_state_t *kb_state = (keygloo_state_t *)context;
    KeyGloo *kg = kb_state->kg;
    return kg->read_key_latch();
}

uint8_t keygloo_read_C010(void *context, uint32_t address) {
    g_kg_reads[1]++;
    if (g_liveness_taps) a2gspu_liveness_note_kbd();
    keygloo_state_t *kb_state = (keygloo_state_t *)context;
    KeyGloo *kg = kb_state->kg;
    return kg->read_key_strobe();
}

void keygloo_write_C010(void *context, uint32_t address, uint8_t value) {
    keygloo_state_t *kb_state = (keygloo_state_t *)context;
    KeyGloo *kg = kb_state->kg;
    kg->write_key_strobe(value);
}

uint8_t keygloo_read_C025(void *context, uint32_t address) {
    g_kg_reads[3]++;
    keygloo_state_t *kb_state = (keygloo_state_t *)context;
    KeyGloo *kg = kb_state->kg;
    return kg->read_mod_latch();
}

// A2GSPU: env-gated GLU mouse trace (W-5 diagnosis). Appends to kg_mouse_trace.txt in CWD.
static void kg_mtrace(const char *tag, int a, int b) {
    static int enabled = -1;
    static int lines = 0;
    if (enabled < 0) enabled = getenv("A2GSPU_MOUSETRACE") ? 1 : 0;
    if (!enabled || lines > 400) return;
    FILE *f = fopen("kg_mouse_trace.txt", "a");
    if (!f) return;
    fprintf(f, "%s %02X %02X\n", tag, a & 0xFF, b & 0xFF);
    fclose(f);
    lines++;
}

uint8_t keygloo_read_C024(void *context, uint32_t address) {
    g_kg_reads[2]++;
    keygloo_state_t *kb_state = (keygloo_state_t *)context;
    KeyGloo *kg = kb_state->kg;
    uint8_t st_before = kg->read_status_register();
    uint8_t data = kg->read_mouse_data();
    kg_mtrace("R24", data, st_before);
    keygloo_update_interrupt_status(kb_state, kg); // could have IRQ after event..
    return data;
}

uint8_t keygloo_read_C026(void *context, uint32_t address) {
    g_kg_reads[4]++;
    keygloo_state_t *kb_state = (keygloo_state_t *)context;
    KeyGloo *kg = kb_state->kg;
    uint8_t data = kg->read_data_register();
    keygloo_update_interrupt_status(kb_state, kg); // could have IRQ after event..
    return data;
}

void keygloo_write_C026(void *context, uint32_t address, uint8_t value) {
    keygloo_state_t *kb_state = (keygloo_state_t *)context;
    KeyGloo *kg = kb_state->kg;
    kg->write_cmd_register(value);
    keygloo_update_interrupt_status(kb_state, kg); // could have IRQ after event..
}

uint8_t keygloo_read_C027(void *context, uint32_t address) {
    keygloo_state_t *kb_state = (keygloo_state_t *)context;
    KeyGloo *kg = kb_state->kg;
    uint8_t st = kg->read_status_register();
    if (st & 0x80) kg_mtrace("R27", st, 0); // log only mouse-data-full polls
    return st;
}

void keygloo_write_C027(void *context, uint32_t address, uint8_t value) {
    keygloo_state_t *kb_state = (keygloo_state_t *)context;
    KeyGloo *kg = kb_state->kg;
    kg->write_status_register(value);
    keygloo_update_interrupt_status(kb_state, kg); // could have IRQ after event..
}

bool keygloo_process_event(keygloo_state_t *kb_state, const SDL_Event &event) {
    KeyGloo *kg = kb_state->kg;
    SDL_Event event_copy = event;
    kg->process_event(event_copy);
    keygloo_update_interrupt_status(kb_state, kg); // could have IRQ after event..
    return true;
}

DebugFormatter *debug_keygloo(keygloo_state_t *kb_state) {
    DebugFormatter *df = new DebugFormatter();
    kb_state->kg->debug_display(df);
    return df;
}

void init_slot_keygloo(computer_t *computer, SlotType_t slot) {

    keygloo_state_t *kb_state = new keygloo_state_t;
    computer->set_module_state(MODULE_KEYGLOO, kb_state);

    kb_state->computer = computer;
    kb_state->irq_control = computer->irq_control;
    kb_state->mmu = computer->mmu;
    kb_state->reset_control = computer->reset_control;

    KeyGloo *kg = new KeyGloo(kb_state->reset_control);
    kb_state->kg = kg;
    // Observatory: register the ADB microcontroller RAM/vars image as a memory window.
    obs_add_memwindow(OBS_SUB_ADB, 0, "adb.uc_ram", kg->obs_uc_window(), kg->obs_uc_len(), OBS_F_INTERNAL_ONLY);

    computer->dispatch->registerHandler(SDL_EVENT_KEY_DOWN, [kb_state](const SDL_Event &event) {
        return keygloo_process_event(kb_state, event);
    });
    computer->dispatch->registerHandler(SDL_EVENT_KEY_UP, [kb_state](const SDL_Event &event) {
        return keygloo_process_event(kb_state, event);
    });
    computer->dispatch->registerHandler(SDL_EVENT_MOUSE_MOTION, [kb_state](const SDL_Event &event) {
        return keygloo_process_event(kb_state, event);
    });
    computer->dispatch->registerHandler(SDL_EVENT_MOUSE_BUTTON_DOWN, [kb_state](const SDL_Event &event) {
        return keygloo_process_event(kb_state, event);
    });
    computer->dispatch->registerHandler(SDL_EVENT_MOUSE_BUTTON_UP, [kb_state](const SDL_Event &event) {
        return keygloo_process_event(kb_state, event);
    });

    // A2GSPU: update mouse scale when window is resized or moved to a different display.
    computer->dispatch->registerHandler(SDL_EVENT_WINDOW_RESIZED, [kb_state](const SDL_Event &event) {
        update_mouse_scale(kb_state);
        return false;  // don't consume — videosystem handles it too
    });
    computer->dispatch->registerHandler(SDL_EVENT_WINDOW_DISPLAY_CHANGED, [kb_state](const SDL_Event &event) {
        update_mouse_scale(kb_state);
        return false;
    });
    update_mouse_scale(kb_state);  // initial scale

    for (int i = 0xC000; i <= 0xC00F; i++) { // should mirror C000 like //e
        computer->mmu->set_C0XX_read_handler(i, { keygloo_read_C000, kb_state });
    }
    //computer->mmu->set_C0XX_read_handler(0xC000, { keygloo_read_C000, kb_state });
    computer->mmu->set_C0XX_read_handler(0xC010, { keygloo_read_C010, kb_state });
    computer->mmu->set_C0XX_write_handler(0xC010, { keygloo_write_C010, kb_state });
    computer->mmu->set_C0XX_read_handler(0xC025, { keygloo_read_C025, kb_state });
    computer->mmu->set_C0XX_read_handler(0xC024, { keygloo_read_C024, kb_state });
    computer->mmu->set_C0XX_read_handler(0xC026, { keygloo_read_C026, kb_state });
    computer->mmu->set_C0XX_write_handler(0xC026, { keygloo_write_C026, kb_state });
    computer->mmu->set_C0XX_read_handler(0xC027, { keygloo_read_C027, kb_state });
    computer->mmu->set_C0XX_write_handler(0xC027, { keygloo_write_C027, kb_state });

    // ── A2GSPU frame handler: SHR detection, mouse grab, Event Manager ──
    // Runs once per VBL. Detects SHR mode for mouse-X scaling, auto-releases
    // mouse grab on leaving SHR, and injects absolute cursor position into
    // GS/OS Toolbox memory when the Event Manager is active. Superset of the
    // upstream handler (still calls kg->frame_handler() in the middle).
    computer->device_frame_dispatcher->registerHandler([kb_state]() {
        ADB_Mouse *mouse = kb_state->kg->get_adb_mouse();
        MMU_IIgs *mmu_gs = nullptr;
        if (mouse && kb_state->computer->cpu && kb_state->computer->cpu->mmu) {
            mmu_gs = dynamic_cast<MMU_IIgs *>(kb_state->computer->cpu->mmu);
            if (mmu_gs) {
                // SHR 320 vs 640: $C029 bit7 (SHR active) + first SCB bit7 (640 mode).
                uint8_t c029 = mmu_gs->new_video_register();
                bool is_shr = (c029 & 0x80) != 0;
                bool is_640 = false;
                if (is_shr) {
                    uint8_t *ram = mmu_gs->get_megaii_memory_base();
                    if (ram) is_640 = (ram[0x19D00] & 0x80) != 0;
                }
                mouse->set_shr_320(is_shr && !is_640);

                // Auto-release mouse capture when leaving SHR (back to text/ProDOS).
                static bool was_shr = false;
                video_system_t *vs = kb_state->computer->video_system;
                if (vs && !is_shr && was_shr && vs->mouse_captured) {
                    vs->display_capture_mouse_message(false);
                }
                was_shr = is_shr;
            }
        }
        kb_state->kg->frame_handler();

        // No Event Manager coordinate writes. This block used to write the host
        // pointer's absolute position into $E1:0190-0193 and the bank-$00 cursor
        // holes every frame, with no environment gate, whenever it judged the Event
        // Manager active -- the emulator writing guest memory, which a real IIgs
        // never does. Measured before removing it: at the GS/OS 6.0.1 desktop (SHR
        // on) its activity test never passed, so it wrote nothing, and the cursor
        // is driven entirely by the ROM-faithful ADB path -- six injected
        // (+30,+20) mouse moves moved the firmware's cursor copy by exactly
        // (+180,+120). Where the test did pass it would have overwritten what
        // GS/OS computed from those same deltas. Absolute host-pointer alignment
        // done faithfully (inject ADB deltas toward the target, reading the
        // guest's position back) is upstream's closed-loop mouse sync, recorded
        // in Docs/UPSTREAM_SYNC.md for the upstream port.

        return true;
    });
    computer->register_debug_display_handler(
        "adb",
        DH_ADB, // unique ID for this, need to have in a header.
        [kb_state]() -> DebugFormatter * {
            return debug_keygloo(kb_state);
        }
    );

    computer->register_reset_handler([kb_state](bool cold_start) {
        if (cold_start) {
            kb_state->kg->zero_0x51();
        }
        kb_state->kg->reset();
        return true;
    });

}