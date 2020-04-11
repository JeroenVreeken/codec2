/*
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
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "mode6000.h"
#include "freedv_data_channel.h"

#include <assert.h>

#define M6000_RATE		48000
#define M6000_SYMBOLRATE	6000
#define M6000_SYMBOLSAMPLES	8
#define M6000_FRAMESIZE		((M6000_RATE * 120)/1000)
#define M6000_FRAMESIZEMAX	(M6000_FRAMESIZE + 8)
#define M6000_FRAMESIZEMIN	(M6000_FRAMESIZE - 8)
#define M6000_FRAMEBUF		(M6000_SYMBOLSAMPLES * 2)
#define M6000_FRAMESYMBOLS	((M6000_FRAMESIZE * M6000_SYMBOLRATE)/M6000_RATE)
#define M6000_INVERT_INTERVAL	10
#define M6000_INVERTSYMBOLS	(M6000_FRAMESYMBOLS / M6000_INVERT_INTERVAL)

#define M6000_INVERTSYMBOLS_MIN	((M6000_INVERTSYMBOLS * 3) / 4)

#define M6000_VOICESIZE		576
#define M6000_VOICEBYTES	72

#define M6000_SYNCSIZE		16
#define M6000_SYNCSIZE_MIN	12

/* Slow data:
   from_bit, bcast_bit, 4 end bits, 64 data bits
 */
#define M6000_SLOWDATABYTES	6
#define M6000_SLOWDATAENDBITS	4
#define M6000_SLOWDATASIZE	(1+1+1+4+64)
#define M6000_RESERVED		(1)

/* full data:
   632 total bits: 
   76 databytes ->608
   8 endbits
   from bit
   bcast bit
   14 spare
 */
#define M6000_FULLDATABYTES	76
#define M6000_FULLDATAENDBITS	8
#define M6000_FULLDATASPARE	14
#define M6000_FULLDATASIZE	(632)

/* Sync words for voice and data frames
   Choosen such that they 'break' false syncs.
 */
 
static bool m6000_sync_voice[M6000_SYNCSIZE] = {
	/*sync*/ 0, 1, 0, 1, 0, 1, 1, 1, 1,
	/*sync*/ 1, 0, 1, 0, 1, 0, 0,
};

static bool m6000_sync_data[M6000_SYNCSIZE] = {
	/*sync*/ 0, 0, 1, 1, 1, 0, 0, 1, 1,
	/*sync*/ 1, 1, 0, 1, 0, 1, 1,
};

static float m6000_bit_shape[M6000_SYMBOLSAMPLES] = {
	0.1464461, 0.5, 0.85355339, 1.0, 0.85355339, 0.5, 0.1464461, 0,
};
#define M6000_BIT_SHAPE_SQ	2.98
#define M6000_THRESHOLD_FAC	(1/(M6000_BIT_SHAPE_SQ * 3))	// derived by experiment....

#define M6000_DEMOD_BIN_DECAY	0.99

enum m6000_sync {
	M6000_SYNC_LOST,
	M6000_SYNC_FRAME,
};

struct m6000 {
	/* modulator */
	
	bool mod_bit;
	int mod_nr;


	/* demodulator */
	
	float demod_samples[M6000_FRAMEBUF];
	int demod_sample_nr;

	float demod_bin[M6000_SYMBOLSAMPLES];
	bool demod_symbols[M6000_FRAMESYMBOLS];
	int demod_bin_selected;
	int demod_symbol_nr;
	float demod_symval;
	float demod_threshold;

	enum m6000_sync demod_sync;
	int demod_sync_nr;
	int demod_nin;
};

struct m6000 *m6000_create(void)
{
	struct m6000 *m = calloc(1, sizeof(struct m6000));

	/* Make sure we don't 'sync' on initial state */
	memset(m->demod_symbols, 1, sizeof(m->demod_symbols));

	m->demod_nin = M6000_FRAMESIZEMAX;

	return m;
}

void m6000_destroy(struct m6000 *m)
{
	free(m);
}

int m6000_get_modem_sample_rate(struct m6000 *m)
{
	return M6000_RATE;
}

int m6000_get_modem_symbol_rate(struct m6000 *m)
{
	return M6000_SYMBOLRATE;
}

int m6000_get_n_nom_modem_samples(struct m6000 *m)
{
	return M6000_FRAMESIZE;
}

int m6000_get_n_max_modem_samples(struct m6000 *m)
{
	return M6000_FRAMESIZEMAX;
}

int m6000_get_codec_bytes(struct m6000 *m)
{
	return M6000_VOICEBYTES;
}

void m6000_get_modem_stats(struct m6000 *m, int *sync, float *snr_est)
{
	if (sync) {
		switch (m->demod_sync) {
		   case M6000_SYNC_LOST:
		   	*sync = 0;
			break;
		   case M6000_SYNC_FRAME:
		   	*sync = 1;
			break;
		}
	}
	if (snr_est) {
		float min = 1000.0;
		float max = 0.0;
		
		int i;
		for (i = 0; i < M6000_SYMBOLSAMPLES; i++) {
			min = fminf(m->demod_bin[i], min);
			max = fmaxf(m->demod_bin[i], max);
		}
		*snr_est = 10.0 * log10f(max/min);
	}
}

int m6000_nin(struct m6000 *m)
{
	return m->demod_nin;
}

static int m6000_mod_symbol(struct m6000 *m, bool bit, short *samples)
{
	samples += m->mod_nr * 8;

	float val = M6000_AMP;
	if (m->mod_bit) {
		val = -M6000_AMP;
	}
	
	if (bit) {
		samples[0] = val;
		samples[1] = val;
		samples[2] = val;
		samples[3] = val;
		samples[4] = val;
		samples[5] = val;
		samples[6] = val;
		samples[7] = val;
	} else {
		samples[0] = val * 0.92388;
		samples[1] = val * 0.70711;
		samples[2] = val * 0.38268;
		samples[3] = 0;
		samples[4] = val * -0.38268;
		samples[5] = val * -0.70711;
		samples[6] = val * -0.92388;
		samples[7] = val * -1.0;
	
		m->mod_bit = !m->mod_bit;
	}
	
	m->mod_nr++;

	return M6000_SYMBOLSAMPLES;
}

static void m6000_mod_bit(struct m6000 *m, bool bit, short *samples)
{
	if ((m->mod_nr % M6000_INVERT_INTERVAL) == 0) {
		m6000_mod_symbol(m, false, samples);
	}
	m6000_mod_symbol(m, bit, samples);
}

int m6000_mod_data(struct m6000 *m, struct freedv_data_channel *fdc, short *samples)
{
	m->mod_nr = 0;
	int i;
	
	for (i = 0; i < sizeof (m6000_sync_data)/sizeof(m6000_sync_data[0]); i++) {
		m6000_mod_bit(m, m6000_sync_data[i], samples);
	}

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
		m6000_mod_bit(m, bit, samples);
		
		byteb++;
		if (byteb > 7) {
			byteb = 0;
			byte++;
		}
	}
	for (byteb = 0; byteb < M6000_FULLDATAENDBITS; byteb++) {
		bool bit = (end_bits >> (7 - byteb)) & 1;
		m6000_mod_bit(m, bit, samples);
	}
	m6000_mod_bit(m, from_bit, samples);
	m6000_mod_bit(m, bcast_bit, samples);
	
	for (i = 0; i < M6000_FULLDATASPARE; i++) {
		m6000_mod_bit(m, 0, samples);
	}

	assert(m->mod_nr == M6000_FRAMESYMBOLS);

	return 0;
}

int m6000_mod_codec(struct m6000 *m, struct freedv_data_channel *fdc, short *samples, unsigned char *voice)
{
	m->mod_nr = 0;
	int i;
	
	for (i = 0; i < sizeof (m6000_sync_voice)/sizeof(m6000_sync_voice[0]); i++) {
		m6000_mod_bit(m, m6000_sync_voice[i], samples);
	}

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
		m6000_mod_bit(m, bit, samples);
			
		byteb++;
		if (byteb > 7) {
			byteb = 0;
			byte++;
		}
	}
	for (byteb = 0; byteb < M6000_SLOWDATAENDBITS; byteb++) {
		bool bit = (end_bits >> (3 - byteb)) & 1;
		m6000_mod_bit(m, bit, samples);
	}
	m6000_mod_bit(m, from_bit, samples);
	m6000_mod_bit(m, bcast_bit, samples);
	m6000_mod_bit(m, crc_bit, samples);
		
	/* reserved */
	m6000_mod_bit(m, 0, samples);

	for (byte = 0, byteb = 0; byte < M6000_VOICEBYTES;) {
		bool bit = (voice[byte] >> (7 - byteb)) & 1;
		m6000_mod_bit(m, bit, samples);
		
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
	if ((*nr % M6000_INVERT_INTERVAL) == 0) {
		*nr = *nr + 1;
	}
	int bit_index = (m->demod_sync_nr + *nr) % M6000_FRAMESYMBOLS;
	bool bit = m->demod_symbols[bit_index];
	*nr = *nr + 1;
	return bit;
}

static int m6000_demod_frame_data(struct m6000 *m, struct freedv_data_channel *fdc)
{
	unsigned char databytes[M6000_FULLDATABYTES] = { 0 };
	int nr = 0;
	int i;
	
	for (i = 0; i < sizeof (m6000_sync_voice)/sizeof(bool); i++) {
		m6000_demod_frame_bit(m, &nr);
	}
	
	int byte;
	int byteb;
	int end_bits = 0;
	int from_bit;
	int bcast_bit;

	for (byte = 0, byteb = 0; byte < M6000_FULLDATABYTES;) {
		bool bit = m6000_demod_frame_bit(m, &nr);
		
		databytes[byte] |= bit << (7 - byteb);
			
		byteb++;
		if (byteb > 7) {
			byteb = 0;
			byte++;
		}
	}
	for (byteb = 0; byteb < M6000_FULLDATAENDBITS; byteb++) {
		bool bit = m6000_demod_frame_bit(m, &nr);

		end_bits |= bit << (7 - byteb);
	}
	from_bit = m6000_demod_frame_bit(m, &nr);
	bcast_bit = m6000_demod_frame_bit(m, &nr);

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
	
	int byte;
	int byteb;
	int end_bits = 0;
	int from_bit;
	int bcast_bit;
	int crc_bit;

	for (byte = 0, byteb = 0; byte < M6000_SLOWDATABYTES;) {
		bool bit = m6000_demod_frame_bit(m, &nr);
		
		databytes[byte] |= bit << (7 - byteb);
			
		byteb++;
		if (byteb > 7) {
			byteb = 0;
			byte++;
		}
	}
	for (byteb = 0; byteb < M6000_SLOWDATAENDBITS; byteb++) {
		bool bit = m6000_demod_frame_bit(m, &nr);

		end_bits |= bit << (3 - byteb);
	}
	from_bit = m6000_demod_frame_bit(m, &nr);
	bcast_bit = m6000_demod_frame_bit(m, &nr);
	crc_bit = m6000_demod_frame_bit(m, &nr);
	
	/* reserved bits */
	m6000_demod_frame_bit(m, &nr);

	freedv_data_channel_rx_frame(fdc, databytes, M6000_SLOWDATABYTES, from_bit, bcast_bit, crc_bit, end_bits);

	memset(voice, 0, M6000_VOICEBYTES);
	for (byte = 0, byteb = 0; byte < M6000_VOICEBYTES;) {
		bool bit = m6000_demod_frame_bit(m, &nr);
		
		voice[byte] |= bit << (7 - byteb);
			
		byteb++;
		if (byteb > 7) {
			byteb = 0;
			byte++;
		}
	}

	return M6000_VOICEBYTES;
}

int m6000_demod(struct m6000 *m, struct freedv_data_channel *fdc, short *samples, unsigned char *voice)
{
	int nin = m->demod_nin;
	int i;
	int offset = 0;
	int ret = 0;
	
	for (i = 0; i < nin; i++) {
		float sample = (float)samples[i] / M6000_AMP;
		int prev_sample_nr = m->demod_sample_nr;
		float prev_sample = m->demod_samples[prev_sample_nr];
		int sample_nr = (prev_sample_nr + 1) % M6000_FRAMEBUF;
		m->demod_sample_nr = sample_nr;
		
		m->demod_samples[sample_nr] = sample;
		
		int bin = sample_nr % M6000_SYMBOLSAMPLES;
		
		m->demod_bin[bin] *= M6000_DEMOD_BIN_DECAY;
		if (signbit(sample) != signbit(prev_sample))
			m->demod_bin[bin] += 1.0;

		int bin_selected = m->demod_bin_selected;
		int bin_sample = (bin_selected + M6000_SYMBOLSAMPLES/2) % M6000_SYMBOLSAMPLES;
		
		if (bin == bin_sample) {
			int bin_prev = (bin_selected + M6000_FRAMEBUF - 1) % M6000_SYMBOLSAMPLES;
			int bin_next = (bin_selected + 1) % M6000_SYMBOLSAMPLES;

			if (offset > -M6000_SYMBOLSAMPLES  && m->demod_bin[bin_prev] > m->demod_bin[bin_selected]) {
				m->demod_bin_selected = bin_prev;
				offset--;
			}
			if (offset < M6000_SYMBOLSAMPLES && m->demod_bin[bin_next] > m->demod_bin[bin_selected]) {
				m->demod_bin_selected = bin_next;
				bin_sample = bin_next;
				offset++;
			}
		}
		if (bin == bin_sample) {
			int sample_off;
			float symval = 0;
			int sym_i;
			for (sym_i = 0; sym_i < M6000_SYMBOLSAMPLES; sym_i++) {
				sample_off = M6000_FRAMEBUF - M6000_SYMBOLSAMPLES - M6000_SYMBOLSAMPLES/2 + sym_i;
				int sample_i = (sample_nr + sample_off) % M6000_FRAMEBUF;
				float sym_sample = m->demod_samples[sample_i];

				symval += (sym_sample - m->demod_threshold) * m6000_bit_shape[sym_i];
			}


			bool symbit = signbit(symval) == signbit(m->demod_symval);
			m->demod_threshold = copysign(symval, symval) * M6000_THRESHOLD_FAC;

			m->demod_symval = symval;
			
			int sym_nr = m->demod_symbol_nr;
			sym_nr++;
			sym_nr = sym_nr % M6000_FRAMESYMBOLS;
			m->demod_symbol_nr = sym_nr;
			m->demod_symbols[sym_nr] = symbit;
			
			if ((m->demod_sync == M6000_SYNC_LOST) ||
			    (m->demod_sync == M6000_SYNC_FRAME && m->demod_sync_nr == sym_nr)) {
				int bit_i;
				int int_ok = 0;
				for (bit_i = 0; bit_i < M6000_FRAMESYMBOLS; bit_i += M6000_INVERT_INTERVAL) {
					int index = (sym_nr + bit_i) % M6000_FRAMESYMBOLS;
					if (!m->demod_symbols[index])
						int_ok++;
				}
				bool interval_ok = false;
				if (m->demod_sync == M6000_SYNC_LOST && int_ok == M6000_INVERTSYMBOLS)
					interval_ok = true;
				if (m->demod_sync == M6000_SYNC_FRAME && int_ok > M6000_INVERTSYMBOLS_MIN)
					interval_ok = true;

				if (interval_ok) {
					int bit_pos = 0;
					int sym_pos = 0;
					int bit_sync_voice = 0;
					int bit_sync_data = 0;
				
					for (; bit_pos < M6000_SYNCSIZE; bit_pos++, sym_pos++) {
						if ((sym_pos % M6000_INVERT_INTERVAL) == 0)
							sym_pos++;
						int bit_index = sym_pos + sym_nr;
						bit_index = bit_index % M6000_FRAMESYMBOLS;
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
			}
		}
	}

	m->demod_nin = M6000_FRAMESIZE + offset;
	
	return ret;
}

