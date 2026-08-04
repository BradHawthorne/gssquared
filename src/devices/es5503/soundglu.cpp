#include <cstdint>
#include <cassert>
#include <SDL3/SDL.h>
#include "computer.hpp"
#include "devices/es5503/ensoniq.hpp"

#include "soundglu.hpp"
#include "util/DebugFormatter.hpp"
#include "util/DebugHandlerIDs.hpp"
#include "device_irq_id.hpp"

#include "NClock.hpp"
#include "util/audio_probe.hpp"

//==============================================================================
// Mono mirroring
//==============================================================================

/* The stereo path sends odd DOC channels left and even ones right. Almost all
   IIgs software predates stereo cards and parks every voice on one CA0 value,
   so routing it faithfully would put the entire soundtrack in one speaker --
   a regression against the mono behaviour, dressed as a feature.

   So: if every live oscillator shares a CA0, the side that has the energy is
   copied onto both. If any two disagree the software is genuinely addressing
   both sides and the mix is left alone. Deciding this from the oscillators
   rather than from the buffer matters -- a buffer-side test ("is one side
   quiet?") would mirror true stereo whenever one side happened to fall silent,
   which is exactly what a stereo soundtrack does between notes. */
static void ensoniq_mirror_mono_to_stereo(ensoniq_state_t *st, uint32_t n_frames) {
    constexpr int ch = ensoniq_state_t::CHANNELS;
    if (ch != 2 || !st->chip || !st->audio_buffer || n_frames == 0) {
        return;
    }

    const int oscs = st->chip->get_oscsenabled();
    int seen = 0;
    int ca0 = 0;
    for (int o = 0; o < oscs; o++) {
        Oscillator *osc = st->chip->get_oscillator(o);
        if (osc->control & 1) {
            continue;                       // halted
        }
        const int osc_ca0 = (osc->control >> 4) & 1;
        if (seen == 0) {
            ca0 = osc_ca0;
            seen = 1;
        } else if (osc_ca0 != ca0) {
            return;                         // true stereo -- leave L/R alone
        }
    }
    if (seen == 0) {
        return;                             // nothing playing
    }

    // Same flip as ES5503::generate_samples: odd CA0 -> slot 0, even -> slot 1.
    const int live = ca0 ^ 1;
    for (uint32_t i = 0; i < n_frames; i++) {
        const int16_t s = st->audio_buffer[i * ch + live];
        st->audio_buffer[i * ch + 0] = s;
        st->audio_buffer[i * ch + 1] = s;
    }
}

//==============================================================================
// Fast-forward / catch-up
//==============================================================================

/* Advance ES5503 sample generation (and therefore oscillator IRQ delivery) up to
   the supplied 14M-clock time. This is the equivalent of MAME's m_stream->update():
   it is invoked on every register/RAM access and from the periodic cycle handler so
   that oscillator state and interrupts are caught up to "now" rather than only at the
   end of a video frame.

   Timing identity: one ES5503 output sample takes 16 * (oscsenabled + 2) 14M cycles
   (16 14M per ~895 kHz DOC cycle, (oscs+2) DOC cycles per sample). generate_samples()
   raises IRQs inside halt_osc()->update_irq_status()->m_irq_callback as oscillators
   halt, so generating the right number of samples also fires IRQs at the right time. */
static void ensoniq_catch_up(ensoniq_state_t *st, uint64_t now_c14m) {
    if (!st->chip || !st->stream) {
        return;
    }

    // Only generate during normal execution. While paused / single-stepping (or in
    // CLOCK_FREE_RUN, where c_14M is frozen) we just keep the time base pinned to "now"
    // so that resuming does not try to render a huge backlog of samples.
    if (st->computer->execution_mode != EXEC_NORMAL) {
        st->last_catchup_c14m = now_c14m;
        st->c14m_accum = 0;
        return;
    }

    // Guard against the clock going backwards (e.g. after a reset/resync).
    if (now_c14m <= st->last_catchup_c14m) {
        st->last_catchup_c14m = now_c14m;
        return;
    }

    st->c14m_accum += (now_c14m - st->last_catchup_c14m);
    st->last_catchup_c14m = now_c14m;

    const uint32_t c14m_per_sample = 16u * (st->chip->get_oscsenabled() + 2u);
    if (c14m_per_sample == 0) {
        return;
    }

    uint64_t samples_due = st->c14m_accum / c14m_per_sample;
    st->c14m_accum -= samples_due * c14m_per_sample;

    if (samples_due == 0) {
        return;
    }

    // Clamp pathological backlogs to the audio buffer size so we never overrun it.
    const uint64_t MAX_SAMPLES = 16384;
    if (samples_due > MAX_SAMPLES) {
        samples_due = MAX_SAMPLES;
    }

    constexpr int ch = ensoniq_state_t::CHANNELS;
    const uint32_t BATCH = 1024;
    while (samples_due > 0) {
        uint32_t n = (samples_due > BATCH) ? BATCH : (uint32_t)samples_due;
        st->chip->generate_samples(st->audio_buffer, n);
        ensoniq_mirror_mono_to_stereo(st, n);
        // The only point where the generated stream is observable. See
        // audio_probe.hpp: register round-trips prove the chip is ADDRESSED,
        // nothing else proves it SOUNDS. `n` is frames, so the width is passed
        // explicitly -- the probe reports it back rather than inferring it from
        // a sample total, which is what lets a gate catch a dropped channel.
        audio_probe::note(audio_probe::SRC_DOC, st->audio_buffer, n, ch);
        SDL_PutAudioStreamData(st->stream, st->audio_buffer,
                               (int)(n * ch * sizeof(int16_t)));
        samples_due -= n;
    }
}

//==============================================================================
// Apple IIgs Interface (C03C-C03F)
//==============================================================================

void ensoniq_update_transaction(ensoniq_state_t *st) {
    if (st->soundctl & 0x80) { // if waiting for prior transaction to complete
        if (st->clock->get_c14m() >= st->doc_read_complete_time) {
            st->soundctl &= ~0x80; // clear busy bit
    
            if (st->soundctl & 0x40) {     // RAM mode - use full 16-bit address
                uint16_t full_address = (st->soundadrh << 8) | st->soundadrl;
                st->sounddata = st->doc_ram[full_address];
            } else {                       // Register mode - use only low byte
                st->sounddata = st->chip->read(st->soundadrl);
            }        
        }
    }
}

/* updates the soundglu data register from the DOC RAM or the ES5503,
   taking into account that reads from GLU are slower than one apple II cycle */
void ensoniq_doc_data_read(ensoniq_state_t *st) {
    // if never done before, or we have not reached the completion time, don't change data yet.
    if (st->soundctl & 0x80) { // waiting for prior transaction to complete
        ensoniq_update_transaction(st);
    } else {  // trigger new transaction
        st->soundctl |= 0x80; // set busy bit
        st->doc_read_complete_time = st->clock->get_c14m() + 4; // the diff between 1MHz and 895KHz.. it's actually gonna vary around a bunch.        
        return; // don't change data yet.
    }
}

uint8_t ensoniq_read_C0xx(void *context, uint32_t address) {
    ensoniq_state_t *st = (ensoniq_state_t *)context;
    if (!st->chip) return 0;

    // Fast-forward to now so register reads (esp. E0 IRQ status) and deferred
    // DOC/RAM reads observe up-to-date oscillator state. Mirrors MAME read().
    ensoniq_catch_up(st, st->clock->get_c14m());

    switch (address) {
        case 0xC03C:  // Sound Control
            ensoniq_update_transaction(st); // update on every soundglu access
            return st->soundctl | 0xF; // low 4 bits (volume) write-only, read as 1111 on real GLU
            
        case 0xC03D: { // Sound Data
            uint16_t full_address = (st->soundadrh << 8) | st->soundadrl;
            
            // Bit 6 of control: 0 = access DOC registers, 1 = access DOC ram
            /* if (st->soundctl & 0x40) {
                // RAM mode - use full 16-bit address
                st->sounddata = st->doc_ram[full_address];
            } else {
                // Register mode - use only low byte
                st->sounddata = st->chip->read(st->soundadrl);
            } */
            ensoniq_doc_data_read(st);
            
            // Auto-increment if bit 5 is set
            if (st->soundctl & 0x20) {
                full_address++;
                st->soundadrl = full_address & 0xFF;
                st->soundadrh = (full_address >> 8) & 0xFF;
            }
            
            return st->sounddata;
        }
            
        case 0xC03E:  // Sound Address Low
            ensoniq_update_transaction(st); // update on every soundglu access
            return st->soundadrl;
            
        case 0xC03F:  // Sound Address High
            ensoniq_update_transaction(st); // update on every soundglu access
            return st->soundadrh;
            
        default:
            assert(false && "Invalid Ensoniq address");
            return 0;
    }
}

void ensoniq_write_C0xx(void *context, uint32_t address, uint8_t data) {
    ensoniq_state_t *st = (ensoniq_state_t *)context;
    if (!st->chip) return;

    // Fast-forward to now BEFORE applying the write, so prior samples are rendered
    // with the old register/RAM state. Mirrors MAME write(). Covers both DOC
    // register writes and DOC RAM writes.
    ensoniq_catch_up(st, st->clock->get_c14m());

    switch (address) {
        case 0xC03C:  // Sound Control
            // bit 7 (busy bit) is readonly
            st->soundctl = (data & 0x7F) | (st->soundctl & 0x80);
            //st->soundctl = data;
            // Volume IS handled -- the low nibble of the sound control register
            // is the system volume, and it is applied on the next line. (A
            // "TODO: handle volume changes here" sat directly above that call
            // and said the opposite of the code under it.)
            st->audio_system->set_volume(data & 0x0F);
            break;
            
        case 0xC03D: { // Sound Data
            // TODO: what should this do if the busy is already set?
            st->sounddata = data;
            uint16_t full_address = (st->soundadrh << 8) | st->soundadrl;
            
            // Bit 6 of control: 0 = access DOC RAM, 1 = access DOC registers
            if (st->soundctl & 0x40) {
                // RAM mode - use full 16-bit address
                st->doc_ram[full_address] = data;
            } else {
                // Register mode - use only low byte
                st->chip->write(st->soundadrl, data);
                // Writing the oscillator-enable register (0xE1) changes the number of
                // enabled oscillators and thus the output sample rate (chip->write()
                // already updated the SDL stream rate). The pre-write catch_up rendered
                // the backlog at the OLD rate; drop the stale fractional remainder and
                // re-base time here so subsequent samples use the NEW rate cleanly.
                if (st->soundadrl == 0xE1) {
                    st->c14m_accum = 0;
                    st->last_catchup_c14m = st->clock->get_c14m();
                }
            }

            // Auto-increment if bit 5 is set
            if (st->soundctl & 0x20) {
                full_address++;
                st->soundadrl = full_address & 0xFF;
                st->soundadrh = (full_address >> 8) & 0xFF;
            }
            break;
        }
            
        case 0xC03E:  // Sound Address Low
            st->soundadrl = data;
            break;
            
        case 0xC03F:  // Sound Address High
            st->soundadrh = data;
            break;
            
        default:
            assert(false && "Invalid Ensoniq address");
            break;
    }
}

void generate_ensoniq_frame(ensoniq_state_t *st) {
    if (!st->chip || !st->stream) {
        return;
    }
    // Samples are now generated incrementally via ensoniq_catch_up() on every
    // register/RAM access and from the per-video-cycle clock handler. Here we just
    // perform a final catch-up to the current 14M time so the SDL stream stays fed
    // through the end of the frame (it will normally render ~0 samples, since the
    // cycle handler has already advanced to frame end).
    ensoniq_catch_up(st, st->clock->get_c14m());
}

DebugFormatter * debug_ensoniq(ensoniq_state_t *st) {
    DebugFormatter *df = new DebugFormatter();
    df->addLine("Control: %02X Address: %04X", st->soundctl, (st->soundadrh << 8) | st->soundadrl);
    //df->addLine("  Sound Data: %02X", st->sounddata);
    ES5503 *chip = st->chip;
    df->addLine("E0: %02X   E1: %02X   E2: %02X", chip->get_rege0(), chip->get_rege1(), 0  /* , chip->get_adc_callback() */ );

    uint32_t es5503_output_rate = st->chip->calculate_output_rate();
    df->addLine("OutRate: %u Hz  OSCs: %d", es5503_output_rate, chip->get_oscsenabled());

    df->addLine("Osc Freq WtSize Ctrl Vol Data WtPtr WtSize Res Acc       Irq");
    for (int o = 0; o < 32; o++) {
        Oscillator *osc = chip->get_oscillator(o);
        df->addLine(" %2d %04X   %04X  %02X  %02X  %02X   %04X  %02X    %02X  %08X %02X", 
            o, osc->freq, osc->wtsize, osc->control, osc->vol, 
            osc->data, osc->wavetblpointer, osc->wavetblsize, osc->resolution, 
            osc->accumulator, osc->irqpend);
    }
    return df;
}

void init_ensoniq_slot(computer_t *computer, SlotType_t slot) {
    ensoniq_state_t *st = new ensoniq_state_t();
    
    // Allocate 64KB DOC RAM
    st->doc_ram = new uint8_t[0x10000];
    std::memset(st->doc_ram, 0, 0x10000);

    st->audio_system = computer->audio_system;

    // Allocate buffer large enough for the maximum frames per video frame.
    // Max rate ~298kHz at 59.92 fps = ~4972 frames, plus headroom; the catch-up
    // clamp below uses the same 16384 figure, and it is FRAMES, so the buffer
    // has to be that many times CHANNELS int16.
    constexpr int ch = ensoniq_state_t::CHANNELS;
    st->audio_buffer = new int16_t[16384 * ch];

    // Create and initialize ES5503 chip
    st->chip = new ES5503();
    // Observatory: register the DOC oscillator array as a coverage-first memory window.
    obs_add_memwindow(OBS_SUB_DOC, 0, "doc.osc", st->chip->get_oscillator(0),
                      32 * (uint32_t)sizeof(Oscillator), OBS_F_INTERNAL_ONLY);
    st->chip->init(7159090, 48000, ch);  // Apple IIgs clock rate; ch = stereo-card outputs
    st->chip->set_wave_memory(st->doc_ram);
    st->computer = computer;
    st->clock = computer->clock;
    st->irq_control = computer->irq_control;

    // Set up IRQ callback to propagate interrupts to CPU
    st->chip->set_irq_callback([st](bool state) {
        if (st->irq_control && st->computer && st->computer->cpu) {
            st->irq_control->set_irq(IRQ_ID_SOUNDGLU, state);
        }
    });
    
    // Register I/O handlers for C03C-C03F
    for (uint32_t i = 0xC03C; i <= 0xC03F; i++) {
        computer->mmu->set_C0XX_write_handler(i, { ensoniq_write_C0xx, st });
        computer->mmu->set_C0XX_read_handler(i, { ensoniq_read_C0xx, st });
    }

    // Calculate frame rate
    st->frame_rate = (double)computer->clock->get_c14m_per_second() / (double)computer->clock->get_c14m_per_frame();
    
    // Calculate ES5503 output rate and set up SDL stream
    uint32_t es5503_output_rate = st->chip->calculate_output_rate();
    
    // Calculate initial samples per frame
    st->samples_per_frame = (float)es5503_output_rate / st->frame_rate;
    st->samples_accumulated = 0.0f;
    st->stream = st->audio_system->create_stream(es5503_output_rate, ch, SDL_AUDIO_S16LE, true);
    // Set the stream pointer in the chip so it can update the rate when oscillators change
    st->chip->set_sdl_stream(st->stream);

    // Initialize the catch-up time base to "now".
    st->last_catchup_c14m = computer->clock->get_c14m();
    st->c14m_accum = 0;

#if 0
    SDL_AudioSpec spec;
    spec.freq = es5503_output_rate;
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = 1;

    SDL_AudioStream *stream = SDL_CreateAudioStream(&spec, NULL);
    if (!stream) {
        printf("Couldn't create audio stream: %s", SDL_GetError());
    } else if (!SDL_BindAudioStream(dev_id, stream)) {  /* once bound, it'll start playing when there is data available! */
        printf("Failed to bind stream to device: %s", SDL_GetError());
    }
    st->stream = stream;
    // Set the stream pointer in the chip so it can update the rate when oscillators change
    st->chip->set_sdl_stream(stream);
#endif

    // Periodic catch-up: fires once per video cycle (~895 kHz DOC / ~1 MHz video),
    // delivering oscillator IRQs between register accesses. This is the equivalent of
    // MAME's delayed_stream_update timer. It is cheap when fewer than one sample is due
    // (just a delta/divide/compare). Requires the NClockIIgs cycle-handler dispatch.
    computer->clock->set_cycle_handler([st]() {
        ensoniq_catch_up(st, st->clock->get_c14m());
    });

    // register a frame processor for the mockingboard.
    computer->device_frame_dispatcher->registerHandler([st]() {
        generate_ensoniq_frame(st);
        return true;
    });

    computer->register_debug_display_handler(
        "es5503",
        DH_ES5503, // unique ID for this, need to have in a header.
        [st]() -> DebugFormatter * {
            return debug_ensoniq(st);
        }
    );

    computer->register_reset_handler([st](bool cold_start) {
        // this caused the audio to get badly delayed / out of sync. added calculate_output_rate() to reset() to fix.
        st->chip->reset();
        // Re-base the catch-up time so we don't render a backlog after reset.
        st->last_catchup_c14m = st->clock->get_c14m();
        st->c14m_accum = 0;
        return true;
    });
}