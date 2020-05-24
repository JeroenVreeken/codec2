/*
  freedv_6000.c     A 6000 baud FSK mode
  
  Copyright (C) 2020 Jeroen Vreeken <jeroen@vreeken.net>

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.


  You should have received a copy of the GNU Lesser General Public License
  along with this program; if not, see <http://www.gnu.org/licenses/>.
*/
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freedv_6000.h"
#include "freedv_api_internal.h"
#include "codec2.h"
#include "debug_alloc.h"

#include <assert.h>

#define M6000_AMP (16383)


#define M6000_RATE		48000
#define M6000_SYMBOLRATE	6000
#define M6000_SYMBOLSAMPLES	8
#define M6000_FRAMESIZE		((M6000_RATE * 120)/1000)
#define M6000_FRAMESIZEMAX	(M6000_FRAMESIZE + 8)
#define M6000_FRAMESIZEMIN	(M6000_FRAMESIZE - 8)
#define M6000_FRAMEBUF		(M6000_SYMBOLSAMPLES * 2)
#define M6000_FRAMESYMBOLS	((M6000_FRAMESIZE * M6000_SYMBOLRATE)/M6000_RATE)

#define M6000_VOICESIZE		648
#define M6000_VOICEBYTES	81

//4800: 576 128 -8: 120 15
//5200: 624 80 -8: 72 9
//5400: 648 56 -8: 48 6

#define M6000_SYNCSIZE		16
#define M6000_SYNCSIZE_MIN	12

/* Slow data:
   from_bit, bcast_bit, 4 end bits, 64 data bits
 */
#define M6000_SLOWDATABYTES	6
#define M6000_SLOWDATAENDBITS	4
#define M6000_SLOWDATASIZE	(1+1+1+4+48)
#define M6000_RESERVED		(1)

/* full data:
   704 total bits: 
   86 databytes ->688
   8 endbits
   from bit
   bcast bit
   6 spare
 */
#define M6000_FULLDATABYTES	86
#define M6000_FULLDATAENDBITS	8
#define M6000_FULLDATASPARE	6
#define M6000_FULLDATASIZE	(704)

/* Sync words for voice and data frames
   Choosen such that they 'break' false syncs.
 */
 
static bool m6000_sync_voice[M6000_SYNCSIZE] = {
	/*sync*/ 0, 0, 0, 0, 1, 0, 1, 0,  1, 1, 0, 1, 1, 0, 1, 0,
};

static bool m6000_sync_data[M6000_SYNCSIZE] = {
	/*sync*/ 0, 0, 0, 0, 1, 1, 0, 1,  1, 0, 1, 0, 1, 1, 0, 1,
};

static float m6000_sym_demod_shape[M6000_SYMBOLSAMPLES] = {
	0.1464461, 0.5, 0.85355339, 1.0, 0.85355339, 0.5, 0.1464461, 0,
};

/* raised cosine bit */
/*
#define M6000_SYMBOLSHAPELEN	4
#define M6000_SYMBOLSHAPESAMPLES (M6000_SYMBOLSAMPLES * M6000_SYMBOLSHAPELEN)
static float m6000_sym_shape[M6000_SYMBOLSHAPESAMPLES] = {
	0,		0,		0, 		0, 	0, 		0,	 	0, 		0,
	0.038060234, 	0.1464461,	0.30865828,	0.5,	0.69134172,	0.85355339,	0.96193977,	1.0,
	0.96193977,	0.85355339, 	0.69134172,	0.5,	0.30865828,	0.1464461,	0.038060234,	0,
	0, 		0, 		0, 		0,	0,		0, 		0, 		0,
};
*/

/* root raised cosine B 0.5*/
/*#define M6000_SYMBOLSHAPELEN	8
#define M6000_SYMBOLSHAPESAMPLES (M6000_SYMBOLSAMPLES * M6000_SYMBOLSHAPELEN)
static float m6000_sym_shape[M6000_SYMBOLSHAPESAMPLES] = {
	-0.009855,	-0.006644,	-0.000407,	0.007169,	0.013682,	0.016747,	0.014825,	0.007894,
	-0.002296,	-0.012526,	-0.018970,	-0.018421,	-0.009543,	0.006242,	0.024592,	0.038983,
	0.042317,	0.029067,	-0.002552,	-0.048980,	-0.100834,	-0.143862,	-0.161319,	-0.137371,
	-0.060857,	0.071447,	0.252888,	0.466877,	0.688905,	0.890356,	1.043398,	1.125998,
	1.125998,	1.043398,	0.890356,	0.688905,	0.466877,	0.252888,	0.071447,	-0.060857,
	-0.137371,	-0.161319,	-0.143862,	-0.100834,	-0.048980,	-0.002552,	0.029067,	0.042317,
	0.038983,	0.024592,	0.006242,	-0.009543,	-0.018421,	-0.018970,	-0.012526,	-0.002296,
	0.007894,	0.014825,	0.016747,	0.013682,	0.007169,	-0.000407,	-0.006644	-0.009855
};
*/

/* raised cosine B 0.5*/
#define M6000_SYMBOLSHAPELEN	8
#define M6000_SYMBOLSHAPESAMPLES (M6000_SYMBOLSAMPLES * M6000_SYMBOLSHAPELEN)
static float m6000_sym_shape[M6000_SYMBOLSHAPESAMPLES] = {
0.001082,	0.003279,	0.005025,	0.005794,	0.005327,	0.003777,	0.001758,	0.000237,	
0.000272,	0.002641,	0.007460,	0.013885,	0.020036,	0.023216,	0.020438,	0.009209,	
-0.011582,	-0.040858,	-0.074862,	-0.107153,	-0.129197,	-0.131538,	-0.105397,	-0.044441,	
0.053616,	0.185913,	0.344125,	0.515065,	0.682183,	0.827760,	0.935439,	0.992680,	
0.992680,	0.935439,	0.827760,	0.682183,	0.515065,	0.344125,	0.185913,	0.053616,	
-0.044441,	-0.105397,	-0.131538,	-0.129197,	-0.107153,	-0.074862,	-0.040858,	-0.011582,	
0.009209,	0.020438,	0.023216,	0.020036,	0.013885,	0.007460,	0.002641,	0.000272,	
0.000237,	0.001758,	0.003777,	0.005327,	0.005794,	0.005025,	0.003279,	0.001082,	
};


#define M6000_DEMOD_BIN_DECAY	0.99

#define M6000_SCRAMBLER_SEED	0x4a80

enum m6000_sync {
    M6000_SYNC_LOST,
    M6000_SYNC_FRAME,
};

struct m6000 {
    /* modulator */

    bool mod_bit[M6000_SYMBOLSHAPELEN];
    int mod_bit_nr;
    int mod_nr;


    /* demodulator */
 
    float demod_samples[M6000_FRAMEBUF];
    int demod_sample_nr;

    float demod_bin[M6000_SYMBOLSAMPLES];
    bool demod_symbols[M6000_FRAMESYMBOLS];
    int demod_bin_selected;
    int demod_symbol_nr;
    float demod_symval;

    enum m6000_sync demod_sync;
    int demod_sync_nr;
};

void freedv_6000_open(struct freedv *f) 
{
    f->codec2 = codec2_create(CODEC2_MODE_3200);
    assert(f->codec2 != NULL);
    
    /* Create modem */
    f->m6000 = calloc(1, sizeof(struct m6000));

    /* Make sure we don't 'sync' on initial state */
    memset(f->m6000->demod_symbols, 1, sizeof(f->m6000->demod_symbols));

    f->nin = M6000_FRAMESIZEMAX;
    f->fdc = freedv_data_channel_create();

    f->n_nom_modem_samples = M6000_FRAMESIZE;
    f->n_max_modem_samples = M6000_FRAMESIZEMAX;
    f->n_nat_modem_samples = M6000_FRAMESIZE;
    f->modem_sample_rate = M6000_RATE;
    f->modem_symbol_rate = M6000_SYMBOLRATE;
    f->n_speech_samples = 6*codec2_samples_per_frame(f->codec2);
    f->n_codec_frames = 6;
    f->bits_per_codec_frame = codec2_bits_per_frame(f->codec2);
    f->bits_per_modem_frame = f->n_codec_frames * f->bits_per_codec_frame;

    int n_packed_bytes = M6000_VOICEBYTES;
    f->tx_payload_bits = CALLOC(1, n_packed_bytes); assert(f->tx_payload_bits != NULL);
    f->rx_payload_bits = CALLOC(1, n_packed_bytes); assert(f->rx_payload_bits != NULL);

    f->stats.sync = 0;
}

void freedv_6000_close(struct freedv *f)
{
    freedv_data_channel_destroy(f->fdc);
    free(f->m6000);
}

static int m6000_mod_symbol(struct m6000 *m, bool bit, short *samples)
{
    int sym;
    int i;
    bool new_bit = !bit ^ m->mod_bit[m->mod_bit_nr];
    m->mod_bit_nr++;
    m->mod_bit_nr %= M6000_SYMBOLSHAPELEN;
    m->mod_bit[m->mod_bit_nr] = new_bit;
    
    float val[M6000_SYMBOLSHAPESAMPLES] = {0};
    
    for (sym = 0; sym < M6000_SYMBOLSHAPELEN; sym++) {
        int bit_i = (m->mod_bit_nr + M6000_SYMBOLSHAPELEN - sym) % M6000_SYMBOLSHAPELEN;
        
        float amp = m->mod_bit[bit_i] ? M6000_AMP : - M6000_AMP;
        
        for (i = 0; i < M6000_SYMBOLSAMPLES; i++) {
            val[i] += amp * m6000_sym_shape[sym * M6000_SYMBOLSAMPLES + i];
        }
    }

    
    samples += m->mod_nr * 8;

    for (i = 0; i < M6000_SYMBOLSAMPLES; i++) {
        samples[i] = val[i];
    }

    m->mod_nr++;

    return M6000_SYMBOLSAMPLES;
}

static void m6000_mod_bit(struct m6000 *m, bool bit, short *samples)
{
    m6000_mod_symbol(m, bit, samples);
}

static void m6000_mod_bit_scrambled(struct m6000 *m, bool bit, unsigned short *scrambler, short *samples)
{
    bool scrambler_bit = ((*scrambler & 0x2) >> 1) ^ (*scrambler & 0x1);
    *scrambler >>= 1;
    *scrambler |= scrambler_bit << 14;
    
    m6000_mod_bit(m, bit ^ scrambler_bit, samples);
}

int freedv_6000_datatx(struct freedv *f, short *samples)
{
    struct freedv_data_channel *fdc = f->fdc;
    struct m6000 *m = f->m6000;
    m->mod_nr = 0;
    int i;
    
    for (i = 0; i < sizeof (m6000_sync_data)/sizeof(m6000_sync_data[0]); i++) {
        m6000_mod_bit(m, m6000_sync_data[i], samples);
    }
    unsigned short scrambler = M6000_SCRAMBLER_SEED;

    unsigned char databytes[M6000_FULLDATABYTES] = { 0 };
    int end_bits;
    int from_bit;
    int bcast_bit;
    int crc_bit;
    int byte;
    int byteb;

    freedv_data_channel_tx_frame(fdc, databytes, M6000_FULLDATABYTES, &from_bit, &bcast_bit, &crc_bit, &end_bits);


    for (byte = 0, byteb = 0; byte < M6000_FULLDATABYTES;) {
        bool bit = (databytes[byte] >> (7 - byteb)) & 1;
        m6000_mod_bit_scrambled(m, bit, &scrambler, samples);
        
        byteb++;
        if (byteb > 7) {
            byteb = 0;
            byte++;
        }
    }
    for (byteb = 0; byteb < M6000_FULLDATAENDBITS; byteb++) {
        bool bit = (end_bits >> (7 - byteb)) & 1;
        m6000_mod_bit_scrambled(m, bit, &scrambler, samples);
    }
    m6000_mod_bit_scrambled(m, from_bit, &scrambler, samples);
    m6000_mod_bit_scrambled(m, bcast_bit, &scrambler, samples);
    
    for (i = 0; i < M6000_FULLDATASPARE; i++) {
        m6000_mod_bit_scrambled(m, 0, &scrambler, samples);
    }

    assert(m->mod_nr == M6000_FRAMESYMBOLS);

    return 0;
}

int freedv_6000_rawdatatx(struct freedv *f, short *samples)
{
    struct m6000 *m = f->m6000;
    struct freedv_data_channel *fdc = f->fdc;
    unsigned char *voice = f->tx_payload_bits;
    m->mod_nr = 0;
    int i;
    
    for (i = 0; i < sizeof (m6000_sync_voice)/sizeof(m6000_sync_voice[0]); i++) {
        m6000_mod_bit(m, m6000_sync_voice[i], samples);
    }
    unsigned short scrambler = M6000_SCRAMBLER_SEED;

    unsigned char databytes[M6000_SLOWDATABYTES] = { 0 };
    int end_bits;
    int from_bit;
    int bcast_bit;
    int crc_bit;
    int byte;
    int byteb;

    freedv_data_channel_tx_frame(fdc, databytes, M6000_SLOWDATABYTES, &from_bit, &bcast_bit, &crc_bit, &end_bits);

    for (byte = 0, byteb = 0; byte < M6000_SLOWDATABYTES;) {
        bool bit = (databytes[byte] >> (7 - byteb)) & 1;
        m6000_mod_bit_scrambled(m, bit, &scrambler, samples);
            
        byteb++;
        if (byteb > 7) {
            byteb = 0;
            byte++;
        }
    }
    for (byteb = 0; byteb < M6000_SLOWDATAENDBITS; byteb++) {
        bool bit = (end_bits >> (3 - byteb)) & 1;
        m6000_mod_bit_scrambled(m, bit, &scrambler, samples);
    }
    m6000_mod_bit_scrambled(m, from_bit, &scrambler, samples);
    m6000_mod_bit_scrambled(m, bcast_bit, &scrambler, samples);
    m6000_mod_bit_scrambled(m, crc_bit, &scrambler, samples);
        
    /* reserved */
    m6000_mod_bit_scrambled(m, 0, &scrambler, samples);

    for (byte = 0, byteb = 0; byte < M6000_VOICEBYTES;) {
        bool bit = (voice[byte] >> (7 - byteb)) & 1;
        m6000_mod_bit_scrambled(m, bit, &scrambler, samples);
        
        byteb++;
        if (byteb > 7) {
            byteb = 0;
            byte++;
        }
    }

    assert(m->mod_nr == M6000_FRAMESYMBOLS);

    return 0;
}

static bool m6000_demod_frame_bit(struct m6000 *m, int *nr)
{
    int bit_index = (m->demod_sync_nr + *nr) % M6000_FRAMESYMBOLS;
    bool bit = m->demod_symbols[bit_index];
    *nr = *nr + 1;
    return bit;
}

static bool m6000_demod_frame_bit_scrambled(struct m6000 *m, int *nr, unsigned short *scrambler)
{
    bool scrambler_bit = ((*scrambler & 0x2) >> 1) ^ (*scrambler & 0x1);
    *scrambler >>= 1;
    *scrambler |= scrambler_bit << 14;
    
    return m6000_demod_frame_bit(m, nr) ^ scrambler_bit;
}

static int m6000_demod_frame_data(struct m6000 *m, struct freedv_data_channel *fdc)
{
    unsigned char databytes[M6000_FULLDATABYTES] = { 0 };
    int nr = 0;
    int i;
    
    for (i = 0; i < sizeof (m6000_sync_voice)/sizeof(bool); i++) {
        m6000_demod_frame_bit(m, &nr);
    }
    unsigned short scrambler = M6000_SCRAMBLER_SEED;
    
    int byte;
    int byteb;
    int end_bits = 0;
    int from_bit;
    int bcast_bit;

    for (byte = 0, byteb = 0; byte < M6000_FULLDATABYTES;) {
        bool bit = m6000_demod_frame_bit_scrambled(m, &nr, &scrambler);
        
        databytes[byte] |= bit << (7 - byteb);
            
        byteb++;
        if (byteb > 7) {
            byteb = 0;
            byte++;
        }
    }
    for (byteb = 0; byteb < M6000_FULLDATAENDBITS; byteb++) {
        bool bit = m6000_demod_frame_bit_scrambled(m, &nr, &scrambler);

        end_bits |= bit << (7 - byteb);
    }
    from_bit = m6000_demod_frame_bit_scrambled(m, &nr, &scrambler);
    bcast_bit = m6000_demod_frame_bit_scrambled(m, &nr, &scrambler);

    freedv_data_channel_rx_frame(fdc, databytes, M6000_FULLDATABYTES, from_bit, bcast_bit, 0, end_bits);

    return 0;
}

static int m6000_demod_frame_voice(struct m6000 *m, struct freedv_data_channel *fdc, unsigned char *voice)
{
    unsigned char databytes[M6000_SLOWDATABYTES] = { 0 };
    int nr = 0;
    int i;
    
    for (i = 0; i < sizeof (m6000_sync_voice)/sizeof(bool); i++) {
        m6000_demod_frame_bit(m, &nr);
    }
    unsigned short scrambler = M6000_SCRAMBLER_SEED;
    
    int byte;
    int byteb;
    int end_bits = 0;
    int from_bit;
    int bcast_bit;
    int crc_bit;

    for (byte = 0, byteb = 0; byte < M6000_SLOWDATABYTES;) {
        bool bit = m6000_demod_frame_bit_scrambled(m, &nr, &scrambler);
        
        databytes[byte] |= bit << (7 - byteb);
            
        byteb++;
        if (byteb > 7) {
            byteb = 0;
            byte++;
        }
    }
    for (byteb = 0; byteb < M6000_SLOWDATAENDBITS; byteb++) {
        bool bit = m6000_demod_frame_bit_scrambled(m, &nr, &scrambler);

        end_bits |= bit << (3 - byteb);
    }
    from_bit = m6000_demod_frame_bit_scrambled(m, &nr, &scrambler);
    bcast_bit = m6000_demod_frame_bit_scrambled(m, &nr, &scrambler);
    crc_bit = m6000_demod_frame_bit_scrambled(m, &nr, &scrambler);
    
    /* reserved bits */
    m6000_demod_frame_bit_scrambled(m, &nr, &scrambler);

    freedv_data_channel_rx_frame(fdc, databytes, M6000_SLOWDATABYTES, from_bit, bcast_bit, crc_bit, end_bits);

    memset(voice, 0, M6000_VOICEBYTES);
    for (byte = 0, byteb = 0; byte < M6000_VOICEBYTES;) {
        bool bit = m6000_demod_frame_bit_scrambled(m, &nr, &scrambler);
        
        voice[byte] |= bit << (7 - byteb);
            
        byteb++;
        if (byteb > 7) {
            byteb = 0;
            byte++;
        }
    }

    return M6000_VOICEBYTES;
}

int freedv_comprx_6000(struct freedv *f, COMP demod_in[])
{
    struct m6000 *m = f->m6000;
    struct freedv_data_channel *fdc = f->fdc;
    unsigned char *voice = f->rx_payload_bits;
    int nin = f->nin;
    int i;
    int offset = 0;
    int ret = 0;
    
    for (i = 0; i < nin; i++) {
        float sample = demod_in[i].real / M6000_AMP;
        int prev_sample_nr = m->demod_sample_nr;
        float prev_sample = m->demod_samples[prev_sample_nr];
        int sample_nr = (prev_sample_nr + 1) % M6000_FRAMEBUF;
        m->demod_sample_nr = sample_nr;
        
        m->demod_samples[sample_nr] = sample;
        
        int bin = sample_nr % M6000_SYMBOLSAMPLES;
        
        m->demod_bin[bin] *= M6000_DEMOD_BIN_DECAY;
        if (signbit(sample) != signbit(prev_sample)) {
            m->demod_bin[bin] += 1.0;
        }

        int bin_selected = m->demod_bin_selected;
        int bin_zerosync = (bin_selected + M6000_SYMBOLSAMPLES/2 - 1) % M6000_SYMBOLSAMPLES;
        
        if (bin == bin_zerosync) {
            float max = 0;
            int off_max = 0;
            int j;
            for (j = -M6000_SYMBOLSAMPLES/2; j < M6000_SYMBOLSAMPLES; j++) {
                int bin_i = (bin_selected + M6000_SYMBOLSAMPLES + j) % M6000_SYMBOLSAMPLES;
                if (m->demod_bin[bin_i] > max) {
                    max = m->demod_bin[bin_i];
                    off_max = j;
                }
            }
            if (offset > -M6000_SYMBOLSAMPLES  && off_max < 0) {
                m->demod_bin_selected = (m->demod_bin_selected -1 + M6000_SYMBOLSAMPLES) % M6000_SYMBOLSAMPLES;
                offset--;
            }
            if (offset < M6000_SYMBOLSAMPLES && off_max > 0) {
                m->demod_bin_selected = (m->demod_bin_selected + 1) % M6000_SYMBOLSAMPLES;
                offset++;
            }
        }
        int bin_sample = (m->demod_bin_selected + M6000_SYMBOLSAMPLES/2) % M6000_SYMBOLSAMPLES;

        if (bin == bin_sample) {
            int sample_off;
            float symval = 0;
            int sym_i;

            for (sym_i = 0; sym_i < M6000_SYMBOLSAMPLES; sym_i++) {
                sample_off = M6000_FRAMEBUF - M6000_SYMBOLSAMPLES - M6000_SYMBOLSAMPLES/2 + sym_i;
                int sample_i = (sample_nr + sample_off) % M6000_FRAMEBUF;
                float sym_sample = m->demod_samples[sample_i];

                symval += sym_sample * m6000_sym_demod_shape[sym_i];
            }

            bool symbit = signbit(symval) == signbit(m->demod_symval);

            m->demod_symval = symval;
            
            int sym_nr = m->demod_symbol_nr;
            sym_nr++;
            sym_nr = sym_nr % M6000_FRAMESYMBOLS;
            m->demod_symbol_nr = sym_nr;
            m->demod_symbols[sym_nr] = symbit;
            
            if ((m->demod_sync == M6000_SYNC_LOST) ||
                (m->demod_sync == M6000_SYNC_FRAME && m->demod_sync_nr == sym_nr)) {
                int bit_pos;
                int bit_sync_voice = 0;
                int bit_sync_data = 0;
                
                for (bit_pos = 0; bit_pos < M6000_SYNCSIZE; bit_pos++) {
                    int bit_index = (sym_nr + bit_pos) % M6000_FRAMESYMBOLS;
                    bool bitval = m->demod_symbols[bit_index];
                    
                    if (m6000_sync_voice[bit_pos] == bitval)
                        bit_sync_voice++;
                    if (m6000_sync_data[bit_pos] == bitval)
                        bit_sync_data++;
                }
                    
                int bit_sync_min;
                if (m->demod_sync == M6000_SYNC_LOST)
                    bit_sync_min = M6000_SYNCSIZE;
                else
                    bit_sync_min = M6000_SYNCSIZE_MIN;

                bool is_sync_voice = false;
                bool is_sync_data = false;

                if (bit_sync_voice >= bit_sync_min && bit_sync_voice > bit_sync_data)
                        is_sync_voice = true;
                if (bit_sync_data >= bit_sync_min && bit_sync_data > bit_sync_voice)
                        is_sync_data = true;

                if (is_sync_voice || is_sync_data) {
                    m->demod_sync = M6000_SYNC_FRAME;
                    m->demod_sync_nr = sym_nr;
                } else {
                    m->demod_sync = M6000_SYNC_LOST;
                }
                if (is_sync_data)
                    m6000_demod_frame_data(m, fdc);
                if (is_sync_voice) {
                    ret = m6000_demod_frame_voice(m, fdc, voice);
                }
            }
        };
    }

    f->nin = M6000_FRAMESIZE + offset;

    switch (m->demod_sync) {
       case M6000_SYNC_LOST:
           f->stats.sync = 0;
        break;
       case M6000_SYNC_FRAME:
           f->stats.sync = 1;
        break;
    }

    float min = 1000.0;
    float max = 0.0;
        
    for (i = 0; i < M6000_SYMBOLSAMPLES; i++) {
        min = fminf(m->demod_bin[i], min);
        max = fmaxf(m->demod_bin[i], max);
    }
    f->stats.snr_est = 10.0 * log10f(max/min);

    int rx_status = 0;
    if (m->demod_sync == M6000_SYNC_FRAME)
        rx_status |= RX_SYNC;
    if (ret)
        rx_status |= RX_BITS;
    return rx_status;
}

