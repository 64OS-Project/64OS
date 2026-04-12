#include <hda.h>
#include <kernel/terminal.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <libk/string.h>
#include <apic.h>
#include <ioapic.h>

static hda_t g_hda;
static volatile u32 g_hda_irq_count = 0;
extern pmm_t pmm;

//==================== AUXILIARY ====================

static inline u32 hda_reg_read32(u32 reg) {
    return *(volatile u32*)((uptr)g_hda.mmio_base + reg);
}

static inline void hda_reg_write32(u32 reg, u32 val) {
    *(volatile u32*)((uptr)g_hda.mmio_base + reg) = val;
}

static inline u16 hda_reg_read16(u32 reg) {
    return *(volatile u16*)((uptr)g_hda.mmio_base + reg);
}

static inline void hda_reg_write16(u32 reg, u16 val) {
    *(volatile u16*)((uptr)g_hda.mmio_base + reg) = val;
}

static inline u8 hda_reg_read8(u32 reg) {
    return *(volatile u8*)((uptr)g_hda.mmio_base + reg);
}

static inline void hda_reg_write8(u32 reg, u8 val) {
    *(volatile u8*)((uptr)g_hda.mmio_base + reg) = val;
}

//==================== VERB SENDING ====================

static int hda_send_verb(u8 codec, u8 nid, u32 verb, u16 payload, u32 *response) {
    if (!g_hda.initialized) return -1;
    
    u32 cmd = (codec << 28) | (nid << 20) | (verb << 8) | payload;
    
    // Wait for CORB space
    u32 timeout = 10000;
    while ((hda_reg_read16(HDA_CORBWP) - g_hda.corb_wp) >= CORB_ENTRIES && timeout--) {
        for (volatile int i = 0; i < 10; i++);
    }
    if (timeout == 0) return -1;
    
    // Write command
    u32 pos = g_hda.corb_wp % CORB_ENTRIES;
    g_hda.corb[pos] = cmd;
    g_hda.corb_wp++;
    hda_reg_write16(HDA_CORBWP, g_hda.corb_wp & 0xFF);
    
    // Wait for response
    timeout = 10000;
    while (g_hda.rirb_rp == hda_reg_read16(HDA_RIRBWP) && timeout--) {
        for (volatile int i = 0; i < 10; i++);
    }
    if (timeout == 0) return -1;
    
    // Read response
    u32 resp_pos = g_hda.rirb_rp % RIRB_ENTRIES;
    *response = g_hda.rirb[resp_pos];
    g_hda.rirb_rp++;
    hda_reg_write16(HDA_RIRBWP, g_hda.rirb_rp & 0xFF);
    
    return 0;
}

//==================== WIDGET PARSING ====================

static const char* widget_type_name(u8 type) {
    switch(type) {
        case HDA_WIDGET_AUDIO_OUTPUT: return "Audio Output";
        case HDA_WIDGET_AUDIO_INPUT: return "Audio Input";
        case HDA_WIDGET_AUDIO_MIXER: return "Audio Mixer";
        case HDA_WIDGET_AUDIO_SELECTOR: return "Selector";
        case HDA_WIDGET_PIN_COMPLEX: return "Pin Complex";
        case HDA_WIDGET_POWER_WIDGET: return "Power Widget";
        case HDA_WIDGET_VOLUME_KNOB: return "Volume Knob";
        case HDA_WIDGET_BEEP_GEN: return "Beep Generator";
        default: return "Unknown";
    }
}

static void hda_parse_widgets(hda_codec_t *codec) {
    u32 response;
    u8 start_nid = 1;
    u8 end_nid = codec->afg_nid;
    
    // Get AFG caps to find number of widgets
    if (hda_send_verb(codec->codec_num, codec->afg_nid, HDA_VERB_GET_PARAMETER, 
                      HDA_PARAM_AFG_CAPS, &response) != 0) {
        terminal_error_printf("[HDA] Failed to get AFG caps\n");
        return;
    }
    
    end_nid = (response >> 16) & 0xFF;
    codec->widget_count = end_nid - start_nid + 1;
    
    terminal_printf("[HDA] Codec %d: %d widgets (NID %d-%d)\n", 
                   codec->codec_num, codec->widget_count, start_nid, end_nid);
    
    codec->widgets = (hda_widget_t*)malloc(codec->widget_count * sizeof(hda_widget_t));
    if (!codec->widgets) return;
    
    for (u8 nid = start_nid; nid <= end_nid; nid++) {
        hda_widget_t *w = &codec->widgets[nid - start_nid];
        memset(w, 0, sizeof(hda_widget_t));
        w->nid = nid;
        
        // Get widget caps
        if (hda_send_verb(codec->codec_num, nid, HDA_VERB_GET_PARAMETER,
                         HDA_PARAM_WIDGET_CAPS, &response) == 0) {
            w->widget_caps = response;
            w->type = (response >> 20) & 0xF;
            w->caps = response & 0xFFFFF;
        }
        
        // Get connection list
        if (w->caps & (1 << 1)) { // Has connection list
            if (hda_send_verb(codec->codec_num, nid, HDA_VERB_GET_PARAMETER,
                             HDA_PARAM_CONN_LIST_LEN, &response) == 0) {
                w->num_connections = response & 0xFF;
                w->connections = (u8*)malloc(w->num_connections);
                
                // Read connections
                for (int i = 0; i < w->num_connections; i++) {
                    // TODO: read connection list entries
                }
            }
        }
        
        // Get pin caps if it's a pin complex
        if (w->type == HDA_WIDGET_PIN_COMPLEX) {
            if (hda_send_verb(codec->codec_num, nid, HDA_VERB_GET_PARAMETER,
                             HDA_PARAM_PIN_CAPS, &response) == 0) {
                w->pin_caps = response;
            }
            
            // Get default configuration
            if (hda_send_verb(codec->codec_num, nid, HDA_VERB_GET_CONVERTER,
                             0, &response) == 0) {
                w->default_config = response;
            }
            
            // Check if it's output pin
            if ((w->pin_caps & HDA_PIN_OUTPUT) && !(w->pin_caps & HDA_PIN_INPUT)) {
                if (!codec->output_nid || codec->output_nid > nid) {
                    codec->output_nid = nid;
                    strcpy(w->name, "Output");
                }
            }
        }
        
        // Get amplifier caps
        if (hda_send_verb(codec->codec_num, nid, HDA_VERB_GET_PARAMETER,
                         HDA_PARAM_AMP_CAPS, &response) == 0) {
            w->amp_caps = response;
        }
        
        // Get sample rates
        if (hda_send_verb(codec->codec_num, nid, HDA_VERB_GET_PARAMETER,
                         HDA_PARAM_SAMPLE_RATES, &response) == 0) {
            w->sample_rates = response;
        }
        
        // Get formats
        if (hda_send_verb(codec->codec_num, nid, HDA_VERB_GET_PARAMETER,
                         HDA_PARAM_FORMATS, &response) == 0) {
            w->formats = response;
        }
        
        terminal_printf("[HDA]   NID %d: %s (caps=0x%x)\n", nid, 
                       widget_type_name(w->type), w->caps);
    }
}

//==================== STREAM SETUP ====================

static int hda_setup_stream(hda_codec_t *codec, hda_stream_t *stream, bool output) {
    static u8 next_tag = 1;
    
    stream->stream_tag = next_tag++;
    stream->output = output;
    stream->running = false;
    
    // Find free stream descriptor
    for (int i = 0; i < 16; i++) {
        volatile u8 *regs = (volatile u8*)((uptr)g_hda.mmio_base + HDA_SD0_BASE + i * 0x20);
        u32 ctl = *(volatile u32*)(regs + HDA_SD_CTL);
        
        if (!(ctl & HDA_SD_CTL_RUN) && !(ctl & HDA_SD_CTL_RESET)) {
            stream->index = i;
            stream->regs = regs;
            break;
        }
    }
    
    if (!stream->regs) return -1;
    
    // Reset stream
    u32 ctl = *(volatile u32*)(stream->regs + HDA_SD_CTL);
    ctl |= HDA_SD_CTL_RESET;
    *(volatile u32*)(stream->regs + HDA_SD_CTL) = ctl;
    for (volatile int i = 0; i < 100; i++);
    ctl &= ~HDA_SD_CTL_RESET;
    *(volatile u32*)(stream->regs + HDA_SD_CTL) = ctl;
    
    // Set stream tag and direction
    ctl |= (stream->stream_tag << HDA_SD_CTL_STREAM_TAG_SHIFT);
    if (output) ctl |= HDA_SD_CTL_DIR_OUT;
    *(volatile u32*)(stream->regs + HDA_SD_CTL) = ctl;
    
    // Set format
    u16 fmt = 0;
    
    // Sample rate (44.1kHz or 48kHz)
    if (codec->sample_rate == 44100) {
        fmt |= HDA_SD_FMT_BASE_44KHZ;
    } else {
        fmt |= HDA_SD_FMT_BASE_48KHZ;
    }
    
    // Bit depth
    switch (codec->bit_depth) {
        case 8:  fmt |= HDA_SD_FMT_BITS_8; break;
        case 16: fmt |= HDA_SD_FMT_BITS_16; break;
        case 20: fmt |= HDA_SD_FMT_BITS_20; break;
        case 24: fmt |= HDA_SD_FMT_BITS_24; break;
        case 32: fmt |= HDA_SD_FMT_BITS_32; break;
        default: fmt |= HDA_SD_FMT_BITS_16; break;
    }
    
    // Channels
    fmt |= (codec->channels - 1) << HDA_SD_FMT_CHAN_SHIFT;
    
    *(volatile u16*)(stream->regs + HDA_SD_FMT) = fmt;
    
    terminal_printf("[HDA] Stream %d setup: tag=%d, format=0x%x\n",
                   stream->index, stream->stream_tag, fmt);
    
    return 0;
}

static int hda_setup_bdl(hda_stream_t *stream, void *buffer, u32 size) {
    u32 page_size = 4096;
    u32 num_pages = (size + page_size - 1) / page_size;
    
    stream->bdl_entries = num_pages;
    stream->bdl = (hda_bdle_t*)pmm_alloc_page(&pmm);
    if (!stream->bdl) return -1;
    memset((void*)stream->bdl, 0, PAGE_SIZE);
    
    u32 offset = 0;
    for (u32 i = 0; i < num_pages; i++) {
        u32 page_offset = offset & ~(page_size - 1);
        u32 phys = (u32)(uptr)buffer + offset;
        
        stream->bdl[i].addr_low = phys;
        stream->bdl[i].addr_high = 0;
        stream->bdl[i].length = (size - offset > page_size) ? page_size : (size - offset);
        stream->bdl[i].flags = (i == num_pages - 1) ? (1 << 0) : 0; // IOC on last
        
        offset += stream->bdl[i].length;
    }
    
    // Set BDL address
    *(volatile u32*)(stream->regs + HDA_SD_BDPL) = (u32)(uptr)stream->bdl;
    *(volatile u32*)(stream->regs + HDA_SD_BDPU) = 0;
    
    // Set cyclic buffer length
    *(volatile u32*)(stream->regs + HDA_SD_CBL) = size;
    
    // Set last valid index
    *(volatile u16*)(stream->regs + HDA_SD_LVI) = num_pages - 1;
    
    stream->buffer = buffer;
    stream->buffer_size = size;
    stream->current_entry = 0;
    stream->buffer_position = 0;
    
    return 0;
}

//==================== SOUND ====================

int hda_playback_start(hda_codec_t *codec, void *buffer, u32 size) {
    if (!codec || !codec->initialized) return -1;
    if (!codec->output_stream) return -1;
    
    hda_stream_t *stream = codec->output_stream;
    
    // Setup BDL
    if (hda_setup_bdl(stream, buffer, size) != 0) return -1;
    
    // Start stream
    u32 ctl = *(volatile u32*)(stream->regs + HDA_SD_CTL);
    ctl |= HDA_SD_CTL_RUN | HDA_SD_CTL_DEIE | HDA_SD_CTL_FEIE;
    *(volatile u32*)(stream->regs + HDA_SD_CTL) = ctl;
    
    // Enable DMA
    ctl |= HDA_SD_CTL_DMA_SELECT;
    *(volatile u32*)(stream->regs + HDA_SD_CTL) = ctl;
    
    stream->running = true;
    
    terminal_printf("[HDA] Playback started: %d bytes\n", size);
    return 0;
}

void hda_playback_stop(hda_codec_t *codec) {
    if (!codec || !codec->output_stream) return;
    
    hda_stream_t *stream = codec->output_stream;
    
    u32 ctl = *(volatile u32*)(stream->regs + HDA_SD_CTL);
    ctl &= ~HDA_SD_CTL_RUN;
    *(volatile u32*)(stream->regs + HDA_SD_CTL) = ctl;
    
    stream->running = false;
    terminal_printf("[HDA] Playback stopped\n");
}

void hda_set_volume(hda_codec_t *codec, u8 volume) {
    if (!codec || !codec->output_nid) return;
    
    // Volume range 0-100, convert to HDA volume (0-127)
    u32 hda_vol = (volume * 127) / 100;
    
    // Set output amplifier
    u32 payload = (hda_vol & 0x7F) | (HDA_AMP_OUTPUT << 8);
    u32 response;
    hda_send_verb(codec->codec_num, codec->output_nid, 
                  HDA_VERB_SET_AMPLIFIER, payload, &response);
    
    terminal_printf("[HDA] Volume set to %d%%\n", volume);
}

void hda_beep(u32 frequency, u32 duration_ms) {
    // Find beep generator widget
    for (int i = 0; i < g_hda.num_codecs; i++) {
        hda_codec_t *codec = &g_hda.codecs[i];
        if (!codec->initialized) continue;
        
        for (u32 w = 0; w < codec->widget_count; w++) {
            hda_widget_t *widget = &codec->widgets[w];
            if (widget->type == HDA_WIDGET_BEEP_GEN) {
                // Set beep frequency
                u32 response;
                hda_send_verb(codec->codec_num, widget->nid, 
                              HDA_VERB_SET_CONVERTER, frequency, &response);
                break;
            }
        }
    }
}

// ==================== IRQ ====================

static void hda_irq_handler(void) {
    g_hda_irq_count++;
    
    // Check RIRB status
    u8 rirb_sts = hda_reg_read8(HDA_RIRBSTS);
    if (rirb_sts & HDA_RIRBSTS_RESPONSE) {
        hda_reg_write8(HDA_RIRBSTS, HDA_RIRBSTS_RESPONSE);
    }
    
    // Check stream statuses
    for (int i = 0; i < 16; i++) {
        volatile u8 *regs = (volatile u8*)((uptr)g_hda.mmio_base + HDA_SD0_BASE + i * 0x20);
        u8 sts = *(volatile u8*)(regs + HDA_SD_STS);
        
        if (sts & HDA_SD_STS_BCIS) {
            *(volatile u8*)(regs + HDA_SD_STS) = HDA_SD_STS_BCIS;
            
            // Stream completed
            if (g_hda.streams[i].running) {
                g_hda.streams[i].buffer_position += g_hda.streams[i].buffer_size;
            }
        }
    }
    
    ioapic_eoi(0);
}

//==================== CODEC INITIALIZATION ====================

static void hda_init_codec(int codec_idx) {
    hda_codec_t *codec = &g_hda.codecs[codec_idx];
    u32 response;
    
    // Get vendor ID
    hda_send_verb(codec->codec_num, 0, HDA_VERB_GET_PARAMETER, HDA_PARAM_VENDOR_ID, &response);
    codec->vendor_id = response >> 16;
    codec->device_id = response & 0xFFFF;
    
    // Get revision ID
    hda_send_verb(codec->codec_num, 0, HDA_VERB_GET_PARAMETER, HDA_PARAM_REVISION_ID, &response);
    codec->revision_id = response & 0xFF;
    
    // Find AFG (Audio Function Group)
    codec->afg_nid = 1;
    for (u8 nid = 1; nid <= 32; nid++) {
        hda_send_verb(codec->codec_num, nid, HDA_VERB_GET_PARAMETER, 
                      HDA_PARAM_WIDGET_CAPS, &response);
        u8 type = (response >> 20) & 0xF;
        if (type == 0x1F) { // AFG type
            codec->afg_nid = nid;
            break;
        }
    }
    
    snprintf(codec->name, sizeof(codec->name), "HD Audio Codec %d", codec_idx);
    codec->sample_rate = 48000;
    codec->bit_depth = 16;
    codec->channels = 2;
    
    // Parse widgets
    hda_parse_widgets(codec);
    
    // Setup output stream
    codec->output_stream = &g_hda.streams[g_hda.num_streams++];
    if (hda_setup_stream(codec, codec->output_stream, true) == 0) {
        terminal_printf("[HDA] Output stream ready\n");
    }
    
    // Unmute and set volume on output
    if (codec->output_nid) {
        // Unmute
        u32 payload = HDA_AMP_OUTPUT << 8;
        hda_send_verb(codec->codec_num, codec->output_nid, 
                      HDA_VERB_SET_AMPLIFIER, payload | 0x80, &response);
        
        // Set volume to 50%
        hda_set_volume(codec, 50);
    }
    
    codec->initialized = true;
    terminal_success_printf("[HDA] Codec %d: %04X:%04X initialized\n",
                           codec_idx, codec->vendor_id, codec->device_id);
}

//==================== BASIC INITIALIZATION ====================

int hda_init(pci_device_t *pci_dev) {
    if (!pci_dev) return -1;
    
    terminal_printf("[HDA] Initializing High Definition Audio...\n");
    
    memset(&g_hda, 0, sizeof(hda_t));
    g_hda.pci_dev = pci_dev;
    
    // Enable bus mastering
    pci_enable_busmaster(pci_dev);
    pci_enable(pci_dev);
    
    // Get MMIO base
    u64 mmio_base = pci_dev->bars[0] & ~0xF;
    g_hda.mmio_base = (volatile void*)(uptr)mmio_base;
    
    terminal_printf("[HDA] MMIO base: 0x%llx\n", mmio_base);
    
    // Reset controller
    hda_reg_write32(HDA_GCTL, hda_reg_read32(HDA_GCTL) | HDA_GCTL_RESET);
    for (volatile int i = 0; i < 10000; i++);
    hda_reg_write32(HDA_GCTL, hda_reg_read32(HDA_GCTL) & ~HDA_GCTL_RESET);
    for (volatile int i = 0; i < 10000; i++);
    
    // Allocate CORB and RIRB
    g_hda.corb = (volatile u32*)pmm_alloc_page(&pmm);
    g_hda.rirb = (volatile u32*)pmm_alloc_page(&pmm);
    
    if (!g_hda.corb || !g_hda.rirb) {
        terminal_error_printf("[HDA] Failed to allocate CORB/RIRB\n");
        return -1;
    }
    
    memset((void*)g_hda.corb, 0, PAGE_SIZE);
    memset((void*)g_hda.rirb, 0, PAGE_SIZE);
    
    // Set base addresses
    hda_reg_write32(HDA_CORBLBASE, (u32)(uptr)g_hda.corb);
    hda_reg_write32(HDA_CORBUBASE, 0);
    hda_reg_write32(HDA_RIRBLBASE, (u32)(uptr)g_hda.rirb);
    hda_reg_write32(HDA_RIRBUBASE, 0);
    
    // Set size (256 entries)
    hda_reg_write8(HDA_CORBSIZE, 0x02);
    hda_reg_write8(HDA_RIRBSIZE, 0x02);
    
    // Reset pointers
    hda_reg_write16(HDA_CORBWP, 0);
    hda_reg_write16(HDA_CORBRP, HDA_CORBRP_RESET);
    hda_reg_write16(HDA_RIRBWP, 0);
    
    // Enable CORB and RIRB
    hda_reg_write8(HDA_CORBCTL, HDA_CORBCTL_ENABLE);
    hda_reg_write8(HDA_RIRBCTL, HDA_RIRBCTL_ENABLE | HDA_RIRBCTL_IRQ_ENABLE);
    
    g_hda.corb_wp = 0;
    g_hda.rirb_rp = 0;
    
    // Detect codecs
    u16 states = hda_reg_read16(HDA_STATESTS);
    terminal_printf("[HDA] Codec states: 0x%x\n", states);
    
    g_hda.num_codecs = 0;
    for (int i = 0; i < 4; i++) {
        if (states & (1 << i)) {
            g_hda.codecs[g_hda.num_codecs].codec_num = i;
            hda_init_codec(g_hda.num_codecs);
            g_hda.num_codecs++;
        }
    }
    
    // Setup IRQ
    u32 gsi, flags;
    if (ioapic_get_override(pci_dev->interrupt_line, &gsi, &flags)) {
        ioapic_redirect_irq(gsi, 48, apic_get_id(), flags);
        ioapic_unmask_irq(gsi);
    }
    
    g_hda.initialized = true;
    terminal_success_printf("[HDA] Controller initialized with %d codec(s)\n", g_hda.num_codecs);
    
    return 0;
}

void hda_print_info(void) {
    if (!g_hda.initialized) {
        terminal_printf("[HDA] Not initialized\n");
        return;
    }
    
    terminal_printf("\n=== HDA Controller Info ===\n");
    terminal_printf("MMIO base: %p\n", g_hda.mmio_base);
    terminal_printf("Codecs: %d\n", g_hda.num_codecs);
    terminal_printf("IRQ count: %d\n", g_hda_irq_count);
    
    for (int i = 0; i < g_hda.num_codecs; i++) {
        hda_codec_t *codec = &g_hda.codecs[i];
        terminal_printf("\n--- Codec %d: %s ---\n", i, codec->name);
        terminal_printf("  Vendor: %04X:%04X\n", codec->vendor_id, codec->device_id);
        terminal_printf("  Revision: %d\n", codec->revision_id);
        terminal_printf("  Sample rate: %d Hz\n", codec->sample_rate);
        terminal_printf("  Bit depth: %d\n", codec->bit_depth);
        terminal_printf("  Channels: %d\n", codec->channels);
        terminal_printf("  Widgets: %d\n", codec->widget_count);
        
        if (codec->output_nid) {
            terminal_printf("  Output NID: %d\n", codec->output_nid);
        }
    }
    terminal_printf("========================\n");
}

//==================== TEST SOUND ====================

void hda_test_beep(void) {
    terminal_printf("[HDA] Generating beep...\n");
    hda_beep(1000, 100);
    for (volatile int i = 0; i < 1000000; i++);
    hda_beep(0, 0);
}
