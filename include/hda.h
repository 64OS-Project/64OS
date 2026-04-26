#ifndef HDA_H
#define HDA_H

#include <kernel/types.h>
#include <pci.h>

// ==================== PCI ====================
#define HDA_PCI_VENDOR_INTEL     0x8086
#define HDA_PCI_DEVICE           0x2668
#define HDA_PCI_CLASS            0x04
#define HDA_PCI_SUBCLASS         0x03

//==================== GLOBAL REGISTERS ====================
#define HDA_GCTL                 0x08
#define HDA_GCTL_RESET           (1 << 0)
#define HDA_GCTL_FCSS            (1 << 1)
#define HDA_GCTL_UNSOL           (1 << 8)

#define HDA_STATESTS             0x0E
#define HDA_WAKEEN               0x0C

// ==================== CORB (Command Output Ring Buffer) ====================
#define HDA_CORBLBASE            0x40
#define HDA_CORBUBASE            0x44
#define HDA_CORBWP               0x48
#define HDA_CORBRP               0x4A
#define HDA_CORBCTL              0x4C
#define HDA_CORBSTS              0x4D
#define HDA_CORBSIZE             0x4E

// CORB Control bits
#define HDA_CORBCTL_ENABLE       (1 << 0)
#define HDA_CORBCTL_DMA_ENABLE   (1 << 1)
#define HDA_CORBRP_RESET         (1 << 15)

// ==================== RIRB (Response Input Ring Buffer) ====================
#define HDA_RIRBLBASE            0x50
#define HDA_RIRBUBASE            0x54
#define HDA_RIRBWP               0x58
#define HDA_RIRBCTL              0x5C
#define HDA_RIRBSTS              0x5D
#define HDA_RIRBSIZE             0x5E

// RIRB Control bits
#define HDA_RIRBCTL_ENABLE       (1 << 0)
#define HDA_RIRBCTL_DMA_ENABLE   (1 << 1)
#define HDA_RIRBCTL_IRQ_ENABLE   (1 << 2)

// RIRB Status bits
#define HDA_RIRBSTS_RESPONSE     (1 << 0)
#define HDA_RIRBSTS_OVERRUN      (1 << 2)

// ==================== DMA Position Buffer ====================
#define HDA_DPIBLBASE            0x60
#define HDA_DPIBUBASE            0x64

// ==================== STREAM DESCRIPTORS ====================
#define HDA_SD0_BASE             0x80
#define HDA_SD1_BASE             0xA0
#define HDA_SD2_BASE             0xC0
#define HDA_SD3_BASE             0xE0
#define HDA_SD4_BASE             0x100
#define HDA_SD5_BASE             0x120
#define HDA_SD6_BASE             0x140
#define HDA_SD7_BASE             0x160
#define HDA_SD8_BASE             0x180
#define HDA_SD9_BASE             0x1A0

// Stream registers offset
#define HDA_SD_CTL               0x00
#define HDA_SD_STS               0x03
#define HDA_SD_LPIB              0x04
#define HDA_SD_CBL               0x08
#define HDA_SD_LVI               0x0C
#define HDA_SD_FIFOD             0x10
#define HDA_SD_FMT               0x12
#define HDA_SD_BDPL              0x18
#define HDA_SD_BDPU              0x1C

// Stream Control bits
#define HDA_SD_CTL_RUN           (1 << 0)
#define HDA_SD_CTL_RESET         (1 << 1)
#define HDA_SD_CTL_DEIE          (1 << 2)
#define HDA_SD_CTL_FEIE          (1 << 3)
#define HDA_SD_CTL_DMA_SELECT    (1 << 4)
#define HDA_SD_CTL_STRIPE_MASK   (3 << 5)
#define HDA_SD_CTL_TP            (1 << 7)
#define HDA_SD_CTL_DIR_OUT       (1 << 8)
#define HDA_SD_CTL_DIR_IN        (0 << 8)
#define HDA_SD_CTL_STREAM_TAG_SHIFT 20

// Stream Status bits
#define HDA_SD_STS_BCIS          (1 << 2)
#define HDA_SD_STS_FIFOE         (1 << 3)
#define HDA_SD_STS_DESE          (1 << 4)
#define HDA_SD_STS_FIFORDY       (1 << 5)

// Stream Format bits
#define HDA_SD_FMT_BASE_SHIFT    14
#define HDA_SD_FMT_BASE_44KHZ    (0 << 14)
#define HDA_SD_FMT_BASE_48KHZ    (2 << 14)
#define HDA_SD_FMT_MULTI_SHIFT   11
#define HDA_SD_FMT_DIV_SHIFT     8
#define HDA_SD_FMT_BITS_SHIFT    4
#define HDA_SD_FMT_BITS_8        (0 << 4)
#define HDA_SD_FMT_BITS_16       (1 << 4)
#define HDA_SD_FMT_BITS_20       (2 << 4)
#define HDA_SD_FMT_BITS_24       (3 << 4)
#define HDA_SD_FMT_BITS_32       (4 << 4)
#define HDA_SD_FMT_CHAN_SHIFT    0

// ==================== VERBS ====================
#define HDA_VERB_GET_PARAMETER   0xF00
#define HDA_VERB_GET_CONVERTER   0x100
#define HDA_VERB_SET_CONVERTER   0x200
#define HDA_VERB_SET_POWER_STATE 0x700
#define HDA_VERB_SET_AMPLIFIER   0x300
#define HDA_VERB_GET_AMPLIFIER   0x400
#define HDA_VERB_SET_GPIO_MASK   0x500
#define HDA_VERB_SET_GPIO_DIR    0x600

// Parameters
#define HDA_PARAM_VENDOR_ID      0x00
#define HDA_PARAM_REVISION_ID    0x02
#define HDA_PARAM_SUBSYSTEM_ID   0x04
#define HDA_PARAM_AFG_CAPS       0x08
#define HDA_PARAM_WIDGET_CAPS    0x09
#define HDA_PARAM_SAMPLE_RATES   0x0A
#define HDA_PARAM_FORMATS        0x0B
#define HDA_PARAM_PIN_CAPS       0x0C
#define HDA_PARAM_AMP_CAPS       0x0D
#define HDA_PARAM_CONN_LIST_LEN  0x0E
#define HDA_PARAM_POWER_STATE    0x0F
#define HDA_PARAM_GPIO_COUNT     0x10

// ==================== WIDGET TYPES ====================
#define HDA_WIDGET_AUDIO_OUTPUT  0x00
#define HDA_WIDGET_AUDIO_INPUT   0x01
#define HDA_WIDGET_AUDIO_MIXER   0x02
#define HDA_WIDGET_AUDIO_SELECTOR 0x03
#define HDA_WIDGET_PIN_COMPLEX   0x04
#define HDA_WIDGET_POWER_WIDGET  0x05
#define HDA_WIDGET_VOLUME_KNOB   0x06
#define HDA_WIDGET_BEEP_GEN      0x07

// ==================== PIN COMPLEX ====================
#define HDA_PIN_JACK             0x00
#define HDA_PIN_NONE             0x01
#define HDA_PIN_FIXED            0x02
#define HDA_PIN_BOTH             0x03
#define HDA_PIN_OUTPUT           (1 << 4)
#define HDA_PIN_INPUT            (1 << 5)
#define HDA_PIN_HEADPHONE        (1 << 6)
#define HDA_PIN_HDMI             (1 << 7)

// ==================== AMPLIFIER ====================
#define HDA_AMP_GET_INPUT        0x00
#define HDA_AMP_GET_OUTPUT       0x80
#define HDA_AMP_LEFT             0x00
#define HDA_AMP_RIGHT            0x01
#define HDA_AMP_MUTE             (1 << 7)

// ==================== CORB/RIRB SIZES ====================
#define CORB_ENTRIES             256
#define RIRB_ENTRIES             256

// ==================== AMPLIFIER ====================
#define HDA_AMP_OUTPUT          0x80
#define HDA_AMP_INPUT           0x00
#define HDA_AMP_LEFT            0x00
#define HDA_AMP_RIGHT           0x01
#define HDA_AMP_MUTE            (1 << 7)

// ==================== PIN COMPLEX ====================
#define HDA_PIN_JACK            0x00
#define HDA_PIN_NONE            0x01
#define HDA_PIN_FIXED           0x02
#define HDA_PIN_BOTH            0x03
#define HDA_PIN_OUTPUT          (1 << 4)
#define HDA_PIN_INPUT           (1 << 5)
#define HDA_PIN_HEADPHONE       (1 << 6)
#define HDA_PIN_HDMI            (1 << 7)

// Buffer Descriptor List Entry
typedef struct {
    u32 addr_low;
    u32 addr_high;
    u32 length;
    u32 flags;
} __attribute__((packed)) hda_bdle_t;

// Stream descriptor
typedef struct {
    u8 index;
    u8 stream_tag;
    volatile u8 *regs;
    bool running;
    bool output;
    hda_bdle_t *bdl;
    u32 bdl_entries;
    u32 current_entry;
    void *buffer;
    u32 buffer_size;
    u32 buffer_position;
} hda_stream_t;

// Widget node
typedef struct {
    u8 nid;
    u8 type;
    u32 caps;
    u32 widget_caps;
    u32 sample_rates;
    u32 formats;
    u32 amp_caps;
    u32 pin_caps;
    u32 default_config;
    u8 *connections;
    u8 num_connections;
    u8 selected_connection;
    u32 volume;
    bool muted;
    char name[32];
} hda_widget_t;

// Codec structure
typedef struct {
    u8 codec_num;
    u16 vendor_id;
    u16 device_id;
    u8 revision_id;
    u8 afg_nid;
    u32 widget_count;
    hda_widget_t *widgets;
    hda_stream_t *output_stream;
    hda_stream_t *input_stream;
    u32 sample_rate;
    u32 bit_depth;
    u32 channels;
    u8 output_nid;
    u8 input_nid;
    bool initialized;
    char name[64];
} hda_codec_t;

// HDA Controller
typedef struct {
    pci_device_t *pci_dev;
    volatile void *mmio_base;
    
    // CORB
    volatile u32 *corb;
    u32 corb_wp;
    u32 corb_rp;
    
    // RIRB
    volatile u32 *rirb;
    u32 rirb_rp;
    
    // Streams
    hda_stream_t streams[16];
    u32 num_streams;
    
    // Codecs
    hda_codec_t codecs[4];
    u32 num_codecs;
    
    bool initialized;
    bool irq_enabled;
} hda_t;

//==================== FUNCTIONS ====================

int hda_init(pci_device_t *pci_dev);
void hda_print_info(void);
int hda_playback_start(hda_codec_t *codec, void *buffer, u32 size);
void hda_playback_stop(hda_codec_t *codec);
void hda_set_volume(hda_codec_t *codec, u8 volume);
void hda_beep(u32 frequency, u32 duration_ms);

#endif
