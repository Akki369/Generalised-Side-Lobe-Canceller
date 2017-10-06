#pragma diag(push)
#pragma diag(suppress:misra_rule_6_3)
#include <processor_include.h>
#pragma diag(pop)

#pragma diag(push)
#pragma diag(suppress:misra_rule_2_1:"Framework allows inline assembly.")
#pragma diag(suppress:misra_rule_5_6:"Framework allows name re-use.")
#pragma diag(suppress:misra_rule_5_7:"Framework allows name re-use.")
#pragma diag(suppress:misra_rule_6_3:"Framework allows standard integer types.")
#pragma diag(suppress:misra_rule_8_7:"Framework allows non-local objects.")
#pragma diag(suppress:misra_rule_10_1_a:"Framework allows implicit integer conversion.")
#pragma diag(suppress:misra_rule_11_3:"Framework allows pointer to integer conversion.")
#pragma diag(suppress:misra_rule_17_4:"Framework allows pointer arithmetics.")
#pragma diag(suppress:misra_rule_19_7:"Framrwork allows function-style macros for value expansion.")
#pragma diag(suppress:misra_rule_20_8:"Framework allows the signal handling facilities.")

#include <sysreg.h>
#include <signal.h>
#include <sru.h>

#include "framework.h"

#if (DSP_SAMPLE_RATE != 8000)  && \
    (DSP_SAMPLE_RATE != 9600)  && \
    (DSP_SAMPLE_RATE != 12000) && \
    (DSP_SAMPLE_RATE != 16000) && \
    (DSP_SAMPLE_RATE != 19200) && \
    (DSP_SAMPLE_RATE != 24000) && \
    (DSP_SAMPLE_RATE != 32000) && \
    (DSP_SAMPLE_RATE != 48000)
#error Invalid sample rate, supported rates: [8000, 9600, 12000, 16000, 19200, 24000, 32000, 48000]
#endif

#if (DSP_BLOCK_SIZE < 1)
#error Invalid block size, must be positive.
#endif

#if (DSP_INPUT_GAIN < 0) || (DSP_INPUT_GAIN >= 60)
#error Invalid input gain, supported range: 0 to 59 dB
#endif

#if (DSP_OUTPUT_ATTENUATION < 0) || (DSP_OUTPUT_ATTENUATION >= 60)
#error Invalid output attenuation, supported range: 0 to 59 dB
#endif

#define CODEC_STREAM_SIZE   ((DSP_BLOCK_SIZE)*sizeof(sample_t))
#define CODEC_DUAL_DIVISOR  ((1632000/(unsigned int)(DSP_SAMPLE_RATE))-0x22)
#define DMA_CHAIN_NEXT(x)   ((unsigned int)(x) + 3)
#define DMA_CHAIN_BUFFER(x) ((unsigned int)(x) - 0x00080000)
#define DMA_CHAIN_ENTRY(x)  ((unsigned int)(x) - 0x0007FFFD)
#define I2C_FLAG_SCL_U30    (FLG8)
#define I2C_FLAG_SCL_U31    (FLG9)
#define I2C_FLAG_SCL_U32    (FLG10)
#define I2C_FLAG_SCL_U33    (FLG11)
#define I2C_FLAG_SDA        (FLG12)
#define I2C_FLAG_SDAO       (FLG12O)

static void memreg_write(unsigned int volatile *reg, int val);
static void memreg_bit_set(unsigned int volatile *reg, int mask);
static void memreg_bit_clr(unsigned int volatile *reg, int mask);
static void i2c_scl_set(int codec);
static void i2c_scl_clr(int codec);
static void i2c_sda_set(void);
static void i2c_sda_clr(void);
static void i2c_sda_out(void);
static void i2c_sda_in(void);
static void i2c_state_transition(int sig);
static void keyboard_int(int sig);
static int dsp_get_audio_buffer_index(void);

typedef struct {
    unsigned int I;
    unsigned int M;
    unsigned int C;
    unsigned int P;
} dma_table_value_t;

typedef struct {
    unsigned int uxx;
    unsigned int reg;
    unsigned int val;
} codec_table_value_t;

enum i2c_state_e {
    state_init,
    state_new_byte,
    state_new_bit,
    state_set_bit,
    state_clock_bit,
    state_new_ack,
    state_new_ack_1,
    state_new_ack_2,
    state_new_ack_3,
    state_clock_ack,
    state_get_ack,
    state_done_ack,
    state_done_ack_2,
    state_done_ack_3,
    state_done_command,
    state_done,
    state_idle
};

enum i2c_codec_e {
    codec_u30 = 1<<0,
    codec_u31 = 1<<1,
    codec_u32 = 1<<2,
    codec_u33 = 1<<3,
    codec_all = (codec_u30 | codec_u31 | codec_u32 | codec_u33),
   // codec_used = (codec_u32 | codec_u33), // codec_u30 | codec_u31 |  
};

typedef struct {
    unsigned int current;
    unsigned int message;
    unsigned int codec;
    unsigned int size;
    unsigned int byte;
    unsigned int mask;
} i2c_state_t;

static i2c_state_t i2c_state;

static sample_t     audio_buffers[3][DSP_BLOCK_SIZE];

static dma_table_value_t const /*pm*/ audio_dma_table[] = {
    {DMA_CHAIN_NEXT(audio_dma_table+1), CODEC_STREAM_SIZE, 1, DMA_CHAIN_BUFFER(audio_buffers[0])},
    {DMA_CHAIN_NEXT(audio_dma_table+2), CODEC_STREAM_SIZE, 1, DMA_CHAIN_BUFFER(audio_buffers[1])},
    {DMA_CHAIN_NEXT(audio_dma_table+0), CODEC_STREAM_SIZE, 1, DMA_CHAIN_BUFFER(audio_buffers[2])},

};

static codec_table_value_t const /*pm*/ codec_table[] = {
    /*
    Codec U32
    IN.L        -> X110     -> MIC5     -> MIC3L_LINE3L
    IN.R        -> X108     -> MIC4     -> MIC3R_LINE3R
    IN.GND      -> X101     -> GND
    OUT.L      <-  X148    <-  DLSP8   <-  HPLOUT
    OUT.R      <-  X150    <-  DLSP10  <-  HPROUT
    OUT.GND    <-  X151    <-  DLSP11  <-  HPLCOM

    Codec U33
    IN.L        -> X114     -> MIC6     -> MIC3L_LINE3L
    IN.R        -> X112     -> MIC7     -> MIC3R_LINE3R
    IN.GND      -> X101     -> GND
    OUT.L      <-  X152    <-  DLSP12  <-  HPLOUT
    OUT.R      <-  X154    <-  DLSP14  <-  HPROUT
    OUT.GND    <-  X155    <-  DLSP15  <-  HPLCOM
    */

     /* Reset all codecs and set register page to 0. */
    {codec_all, 1, 0x80},
    {codec_all, 0, 0x00},
    
    /* Data transfer to and from codec. */
    {codec_u32,              8, 0xC0},
    {codec_u32,  9, 0x30},
    {codec_u32,  7, 0x0A},
    {codec_u32, 12, 0x50},
    {codec_u32,  2, 0x00 | CODEC_DUAL_DIVISOR},

    /* Input signal path settings. */
    {codec_u32, 15, 0x00 | (DSP_INPUT_GAIN << 2)},
    {codec_u32, 16, 0x00 | (DSP_INPUT_GAIN << 2)},
    {codec_u32, 17, 0x0F},
    {codec_u32, 18, 0xF0},
    {codec_u32, 19, 0x7C},
    {codec_u32, 22, 0x7C},
    {codec_u32, 25, 0xC0},
    
    /* Output signal path settings. */
    {codec_u32, 37, 0xD0},
    {codec_u32, 38, 0x08},
    {codec_u32, 41, 0xA0},
    {codec_u32, 42, 0x6C},
    {codec_u32, 43, 0x00 | (DSP_OUTPUT_ATTENUATION << 2)},
    {codec_u32, 44, 0x00 | (DSP_OUTPUT_ATTENUATION << 2)},
    {codec_u32, 51, 0x0F},
    {codec_u32, 65, 0x0F},
};

static void memreg_write(unsigned int volatile *reg, int val)
{
    *reg = (unsigned int)val;
}

static void memreg_bit_set(unsigned int volatile *reg, int mask)
{
    *reg = (*reg) | (unsigned int)mask;
}

static void memreg_bit_clr(unsigned int volatile *reg, int mask)
{
    *reg = (*reg) & ~(unsigned int)mask;
}

static void i2c_scl_set(int codec)
{
    switch(codec) {
        case codec_u30:
            sysreg_bit_set(sysreg_FLAGS, I2C_FLAG_SCL_U30);
            break;
        case codec_u31:
            sysreg_bit_set(sysreg_FLAGS, I2C_FLAG_SCL_U31);
            break;
        case codec_u32:
            sysreg_bit_set(sysreg_FLAGS, I2C_FLAG_SCL_U32);
            break;
        case codec_u33:
            sysreg_bit_set(sysreg_FLAGS, I2C_FLAG_SCL_U33);
            break;
        default:
            break;
    }
}

static void i2c_scl_clr(int codec)
{
    switch(codec) {
        case codec_u30:
            sysreg_bit_clr(sysreg_FLAGS, I2C_FLAG_SCL_U30);
            break;
        case codec_u31:
            sysreg_bit_clr(sysreg_FLAGS, I2C_FLAG_SCL_U31);
            break;
        case codec_u32:
            sysreg_bit_clr(sysreg_FLAGS, I2C_FLAG_SCL_U32);
            break;
        case codec_u33:
            sysreg_bit_clr(sysreg_FLAGS, I2C_FLAG_SCL_U33);
            break;
        default:
            break;
    }
}

static void i2c_sda_set(void)
{
    sysreg_bit_set(sysreg_FLAGS, I2C_FLAG_SDA);
}

static void i2c_sda_clr(void)
{
    sysreg_bit_clr(sysreg_FLAGS, I2C_FLAG_SDA);
}

static void i2c_sda_out(void)
{
    sysreg_bit_set(sysreg_FLAGS, I2C_FLAG_SDAO);
}

static void i2c_sda_in(void)
{
    sysreg_bit_clr(sysreg_FLAGS, I2C_FLAG_SDAO);
}

static void i2c_state_transition(int sig)
{
    switch(i2c_state.current) {
    case state_init:
        i2c_state.size = 3;
        i2c_sda_clr();
        i2c_state.current = state_new_byte;
        break;

    case state_new_byte:
        i2c_scl_clr(i2c_state.codec);
        i2c_state.byte = i2c_state.message & 0xFFu;
        i2c_state.mask = 0x80;
        i2c_state.current = state_set_bit;
        break;

    case state_new_bit:
        i2c_scl_clr(i2c_state.codec);
        i2c_state.current = state_set_bit;
        break;

    case state_set_bit:
        if((i2c_state.byte & i2c_state.mask) == 0) {
            i2c_sda_clr();
        } else {
            i2c_sda_set();
        }

        i2c_state.current = state_clock_bit;
        break;

    case state_clock_bit:
        i2c_scl_set(i2c_state.codec);

        if(i2c_state.mask != 0x01) {
            i2c_state.mask >>= 1;
            i2c_state.current = state_new_bit;
        } else {
            i2c_state.size -= 1;
            i2c_state.message >>= 8;
            i2c_state.current = state_new_ack;
        }

        break;

    case state_new_ack:
        i2c_scl_clr(i2c_state.codec);
        i2c_state.current = state_new_ack_1;
        break;

    case state_new_ack_1:
        i2c_sda_set();
        i2c_state.current = state_new_ack_2;
        break;

    case state_new_ack_2:
        i2c_sda_in();
        i2c_state.current = state_clock_ack;
        break;

    case state_clock_ack:
        i2c_scl_set(i2c_state.codec);
        i2c_state.current = state_get_ack;
        break;

    case state_get_ack:
        i2c_scl_clr(i2c_state.codec);
        i2c_state.current = state_done_ack;
        break;

    case state_done_ack:
        i2c_sda_out();
        i2c_state.current = state_done_ack_2;
        break;

    case state_done_ack_2:
        i2c_sda_clr();

        if(i2c_state.size == 0) {
            i2c_state.current = state_done_command;
        } else {
            i2c_state.current = state_new_byte;
        }

        break;

    case state_done_command:
        i2c_scl_set(i2c_state.codec);
        i2c_state.current = state_done;
        break;

    case state_done:
        i2c_sda_set();
        i2c_state.current = state_idle;
        break;

    case state_idle:
        break;

    default:
        break;
    }
}

static void dai_interrupt_delegate(int sig)
{
    unsigned int mask = *pDAI_IRPTL_H;

    if((mask & 0xF0000000) != 0) {
        raise(SIG_USR0);
    }
}

void dsp_init()
{
    typedef void (*intfn)(int);

    int i;
    intfn tmz;

    memreg_bit_set(pSYSCTL, IIVT | PPFLGS);
    memreg_bit_set(pPMCTL, CLKOUTEN);

    sysreg_bit_set(sysreg_FLAGS, FLG8O | FLG9O | FLG10O | FLG11O | FLG12O | FLG14O);
    sysreg_bit_set(sysreg_FLAGS, FLG8  | FLG9  | FLG10  | FLG11  | FLG12  | FLG14);

    sysreg_bit_set(sysreg_FLAGS, FLG4O | FLG7O | FLG0O);
    sysreg_bit_set(sysreg_FLAGS, FLG4 | FLG7  | FLG0);

    sysreg_bit_clr(sysreg_FLAGS, FLG14);
    asm("nop; nop; nop; nop;");
    sysreg_bit_set(sysreg_FLAGS, FLG14);

    tmz = interrupt(SIG_TMZ, i2c_state_transition);
    timer_set(1000, 1000);
    timer_on();

    for(i=0; i<sizeof(codec_table)/sizeof(codec_table[0]); ++i) {
        unsigned int codecuxx = codec_table[i].uxx;
        unsigned int codecreg = codec_table[i].reg;
        unsigned int codecval = codec_table[i].val;
        unsigned int mask = 0x01;

        while(mask < 0x10) {
            if((codecuxx & mask) != 0) {
                i2c_state.current = state_init;
                i2c_state.message = (0x18u<<1) | (codecreg<<8) | (codecval<<16);
                i2c_state.codec = mask;

                while(i2c_state.current != state_idle) {
                    idle();
                }
            }
            mask <<= 1;
        }
    }

    timer_off();
    interrupt(SIG_TMZ, tmz);
    
    /*
    SPORT 1A input from codec U32.
    SPORT 0A output to codec U32.
    */

    memreg_write(pSPCTL1, OPMODE | SLEN32 | SDEN_A | SCHEN_A);
    memreg_write(pSPCTL0, OPMODE | SLEN32 | SDEN_A | SCHEN_A | SPTRAN);

    /*
    U32.I2S_BCLK  -> DAI_P6 (clock master)
    U32.I2S_WCLK  -> DAI_P4 (clock master)
    U32.I2S_DIN  <-  DAI_P8
    U32.I2S_DOUT  -> DAI_P7

    KEY6          -> DAI_P20
    KEY7          -> DAI_P19
    KEY8          -> DAI_P18
    KEY9          -> DAI_P17
    */
    
    /*
    DAI_P6        -> SPORT0_CLK
    DAI_P4        -> SPORT0_FS
    DAI_P8       <-  SPORT0_DA
    DAI_P6        -> SPORT1_CLK
    DAI_P4        -> SPORT1_FS
    DAI_P7        -> SPORT1_DA
    DAI_P17       -> DAI_INT_28
    DAI_P18       -> DAI_INT_29
    DAI_P19       -> DAI_INT_30
    DAI_P20       -> DAI_INT_31
    */
   
    /*
    Group A: Clock routing.
    PB6 to SPORT0_CLK
    PB6 to SPORT1_CLK
    */
    memreg_write(pSRU_CLK0, (A_DAI_PB06_O<<0) | (A_DAI_PB06_O<<5));

    /*
    Group B: Serial data routing.
    PB7 to SPORT1A
    */
    memreg_write(pSRU_DAT0, (B_DAI_PB07_O<<12));

    /*
    Group C: Frame sync rounting.
    PB4 to SPORT0_FS
    PB4 to SPORT1_FS
    */
    memreg_write(pSRU_FS0, (C_DAI_PB04_O<<0) | (C_DAI_PB04_O<<5));

    /*
    Group D: Pin signal assignment.
    SPORT0_DA to PB8
    */
    memreg_write(pSRU_PIN1, (D_SPORT0_DA_O<<12));

    /*
    Group E: Miscellaneous registers
    PB17 to DAI_INT_28
    PB18 to DAI_INT_29
    PB19 to DAI_INT_30
    PB20 to DAI_INT_31
    */
    memreg_write(pSRU_EXT_MISCA, (E_DAI_PB17_O<<0) | (E_DAI_PB18_O<<5) | (E_DAI_PB19_O<<10) | (E_DAI_PB20_O<<15));
    
    /*
    Group F: Pin buffer enable.
    Set PBEN8 to output.
    */
    memreg_write(pSRU_PINEN0, 0);
    memreg_write(pSRU_PINEN1, (F_HIGH<<12));
    memreg_write(pSRU_PINEN2, 0);
    memreg_write(pSRU_PINEN3, 0);

    /*
    Pull-up resistors.
    */
    memreg_write(pDAI_PIN_PULLUP, (~(DAI_P04_PULLUP | DAI_P06_PULLUP | DAI_P07_PULLUP | DAI_P08_PULLUP)) & 0x000FFFFF);

    /*
    Enable DAI interrupt on DAI_INT_n on falling edge, for n=28...31.
    */
    memreg_write(pDAI_IRPTL_FE, SRU_EXTMISCA0_INT | SRU_EXTMISCA1_INT | SRU_EXTMISCA2_INT | SRU_EXTMISCA3_INT);
    memreg_write(pDAI_IRPTL_PRI, SRU_EXTMISCA0_INT | SRU_EXTMISCA1_INT | SRU_EXTMISCA2_INT | SRU_EXTMISCA3_INT);

    /*
    Interrupt handler for high-priority DAI pin interrupts. Default keyboard
    handler clears the DAI interrupt latch and raises a USR0 interrupt.
    */
    interrupts(SIG_DAIH, dai_interrupt_delegate);
}

void dsp_start(void)
{
    memreg_write(pCPSP1A, DMA_CHAIN_ENTRY(audio_dma_table+0));
    memreg_write(pCPSP0A, DMA_CHAIN_ENTRY(audio_dma_table+1));
    memreg_bit_set(pSPCTL1, SPEN_A);
    memreg_bit_set(pSPCTL0, SPEN_A);
}

void dsp_stop(void)
{
    memreg_bit_clr(pSPCTL1, SPEN_A);
    memreg_bit_clr(pSPCTL0, SPEN_A);
}

static int dsp_get_audio_buffer_index(void)
{
    unsigned int p = *pCPSP1A;
    int index;

    if(p == DMA_CHAIN_NEXT(audio_dma_table+2)) {
        index = 0;
    } else if(p == DMA_CHAIN_NEXT(audio_dma_table+0)) {
        index = 1;
    } else {
        index = 2;
    }

    return index;
}

sample_t *dsp_get_audio(void)
{
    int index = dsp_get_audio_buffer_index();
    return audio_buffers[index];
}

unsigned int dsp_get_keys(void)
{
    unsigned int dai = *pDAI_PIN_STAT;
    return (~dai>>17) & 0x000Fu;
}

unsigned int dsp_get_cycles(void)
{
    unsigned int cycle;
    asm("%0 = EMUCLK;" : "=d" (cycle));
    return cycle;
}

void dsp_set_leds(unsigned int value)
{
    if(value & LED1) {
        sysreg_bit_set(sysreg_FLAGS, FLG7);
    } else {
        sysreg_bit_clr(sysreg_FLAGS, FLG7);
    }

    if(value & LED2) {
        sysreg_bit_set(sysreg_FLAGS, FLG4);
    } else {
        sysreg_bit_clr(sysreg_FLAGS, FLG4);
    }
}

#pragma diag(pop)
