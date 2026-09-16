#pragma once

#include <cstdio>
#include <cstdint>
#include <vector>
#include <functional>
#include "gs2.hpp"
#include <SDL3/SDL_audio.h>

// Forward declarations
struct computer_t;

// Oscillator structure
struct Oscillator {
    uint16_t freq;
    uint16_t wtsize;
    uint8_t  control;
    uint8_t  vol;
    uint8_t  data;
    uint32_t wavetblpointer;
    uint8_t  wavetblsize;
    uint8_t  resolution;
    uint32_t accumulator;
    uint8_t  irqpend;
};

// ES5503 Ensoniq "DOC" sound chip emulator
// Based on MAME implementation by R. Belmont
class ES5503 {
public:
    ES5503();
    ~ES5503();

    // Initialize the chip with clock rate and output sample rate
    void init(uint32_t clock_rate, uint32_t sample_rate, int output_channels = 2);
    
    // Reset the chip to initial state
    void reset();
    
    // Register read/write
    uint8_t read(uint8_t offset);
    void write(uint8_t offset, uint8_t data);
    
    // Generate audio samples
    void generate_samples(int16_t *buffer, int num_samples);
    
    // Set callbacks
    void set_irq_callback(std::function<void(bool)> callback) { m_irq_callback = callback; }
    void set_adc_callback(std::function<uint8_t()> callback) { m_adc_callback = callback; }
    
    // Set wave memory pointer
    void set_wave_memory(uint8_t *memory) { m_wave_memory = memory; }
    
    // Set SDL AudioStream pointer for rate updates
    void set_sdl_stream(SDL_AudioStream *stream) { m_sdl_stream = stream; }
    
    // Get current channel strobe (for banking)
    uint8_t get_channel_strobe() const { return m_channel_strobe; }
    
    // Calculate output sample rate based on clock and oscillators
    uint32_t calculate_output_rate() const {
        return (m_clock_rate / 8) / (m_oscsenabled + 2);
    }

    // Oscillator modes
    enum {
        MODE_FREE = 0,
        MODE_ONESHOT = 1,
        MODE_SYNCAM = 2,
        MODE_SWAP = 3
    };

    private:
    
    int m_oscsenabled;          // Number of oscillators enabled
    uint8_t m_channel_strobe;   // Current channel strobe
    int m_output_channels;      // Number of output channels (1=mono, 2=stereo)
    uint32_t m_clock_rate;      // Input clock rate
    uint32_t m_sample_rate;     // Output sample rate
    
    union {
        struct {
            uint8_t rege0_d0 : 1;
            uint8_t rege0_osc : 5;
            uint8_t rege0_d6 : 1;
            uint8_t rege0_ir : 1;
        };
        uint8_t m_rege0;                // Contents of register 0xe0
    };
    uint8_t m_rege1;                // Contents of register 0xe1

    uint8_t *m_wave_memory;     // Pointer to wave memory (64KB DOC RAM)
    SDL_AudioStream *m_sdl_stream;  // SDL AudioStream for rate updates
    
    std::vector<int32_t> m_mix_buffer;
    
    std::function<void(bool)> m_irq_callback;
    std::function<uint8_t()> m_adc_callback;
    
    // Helper methods
    void halt_osc(int onum, int type, uint32_t *accumulator, int resshift, uint8_t newCtrl);
    uint8_t read_wave_byte(uint32_t address);
    void update_sdl_stream_rate();
    int update_irq_status();

    Oscillator m_oscillators[32];

    public:
    Oscillator * get_oscillator(int index) { return &m_oscillators[index]; }
    uint8_t get_rege0() { return m_rege0; }
    uint8_t get_rege1() { return m_rege1; /* m_oscsenabled; */ }

    // ---- A2GSPU snapshot (M5): the registers and the 32 oscillators, field by
    // field (no struct padding in the file). The wave memory is the GLU's and is
    // saved there; the mix buffer and the SDL stream are the host's.
    void A2GSPU_snapshot(FILE *f) {
        int32_t v[2] = { (int32_t)m_oscsenabled, (int32_t)m_output_channels }; fwrite(v, 4, 2, f);
        fwrite(&m_channel_strobe, 1, 1, f); fwrite(&m_clock_rate, 4, 1, f); fwrite(&m_sample_rate, 4, 1, f);
        fwrite(&m_rege0, 1, 1, f); fwrite(&m_rege1, 1, 1, f);
        for (const Oscillator &o : m_oscillators) {
            fwrite(&o.freq, 2, 1, f); fwrite(&o.wtsize, 2, 1, f); fwrite(&o.control, 1, 1, f); fwrite(&o.vol, 1, 1, f);
            fwrite(&o.data, 1, 1, f); fwrite(&o.wavetblpointer, 4, 1, f); fwrite(&o.wavetblsize, 1, 1, f);
            fwrite(&o.resolution, 1, 1, f); fwrite(&o.accumulator, 4, 1, f); fwrite(&o.irqpend, 1, 1, f);
        }
    }
    bool A2GSPU_restore(FILE *f) {
        int32_t v[2]; if (fread(v, 4, 2, f) != 2) return false;
        m_oscsenabled = v[0]; m_output_channels = v[1];
        if (fread(&m_channel_strobe, 1, 1, f) != 1 || fread(&m_clock_rate, 4, 1, f) != 1 || fread(&m_sample_rate, 4, 1, f) != 1) return false;
        if (fread(&m_rege0, 1, 1, f) != 1 || fread(&m_rege1, 1, 1, f) != 1) return false;
        for (Oscillator &o : m_oscillators) {
            if (fread(&o.freq, 2, 1, f) != 1 || fread(&o.wtsize, 2, 1, f) != 1 || fread(&o.control, 1, 1, f) != 1 ||
                fread(&o.vol, 1, 1, f) != 1 || fread(&o.data, 1, 1, f) != 1 || fread(&o.wavetblpointer, 4, 1, f) != 1 ||
                fread(&o.wavetblsize, 1, 1, f) != 1 || fread(&o.resolution, 1, 1, f) != 1 ||
                fread(&o.accumulator, 4, 1, f) != 1 || fread(&o.irqpend, 1, 1, f) != 1) return false;
        }
        if (m_sdl_stream) update_sdl_stream_rate();   // the enabled-oscillator count sets the rate
        update_irq_status();                           // re-drive the IRQ line from the restored irqpend bits
        return true;
    }
    uint8_t get_oscsenabled() { return m_oscsenabled; }
};
