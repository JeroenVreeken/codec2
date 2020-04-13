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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mode6000.h"

bool rx_check = true;
bool rx_check_failed = false;
bool const_check_failed = false;

int demod_sync_nr = 0;
int data_channel_rx_6 = 0;
int data_channel_rx_86 = 0;
int data_channel_rx_ok_6 = 0;
int data_channel_rx_ok_86 = 0;
int voice_test_nr = 0;

int ber_checked = 0;
int ber_errors = 0;

#define M6000_RATE		48000
#define M6000_SYMBOLRATE	6000
#define M6000_SYMBOLSAMPLES	8
#define M6000_FRAMESIZE		((M6000_RATE * 120)/1000)
#define M6000_FRAMESIZEMAX	(M6000_FRAMESIZE + 8)

void freedv_data_channel_rx_frame(struct freedv_data_channel *fdc, unsigned char *data, size_t size, int from_bit, int bcast_bit, int crc_bit, int end_bits)
{
	printf("freedv_data_channel_rx_frame(%p, %p, %zd)\n", fdc, data, size);
	
	int i;
	unsigned char testval = 0;
	int ok = 1;
	
	for (i = 0; i < size; i++) {
		unsigned char value = data[i];
		printf("%02x", value);
		
		if (rx_check) {
			unsigned char expected = testval;
			int bit;
		
			for (bit = 0; bit < 8; bit++) {
				ber_checked++;
				unsigned char mask = 1 << bit;
				if ((expected & mask) != (value & mask)) {
					ber_errors++;
				}
			}

			if (value != testval) {
				printf(" data byte %d: 0x%02x does not match calculated 0x%02x\n", i, value, testval);
				rx_check_failed = true;
				ok = 0;
			}
		}
		
		testval += size;
	}
	
	if (rx_check) {
		if (end_bits != size) {
			printf("end_bits %d invalid\n", end_bits);
			rx_check_failed = true;
		}
		if (from_bit != 0) {
			printf("from_bit %d invalid\n", from_bit);
			rx_check_failed = true;
		}
		if (bcast_bit != 1) {
			printf("bcast_bit %d invalid\n", bcast_bit);
			rx_check_failed = true;
		}
		if (crc_bit != (size == 6)) {
			printf("crc_bit %d invalid\n", crc_bit);
			rx_check_failed = true;
		}
	}
	
	if (size == 6) {
		data_channel_rx_6++;
		data_channel_rx_ok_6 += ok;
	}
	if (size == 86) {
		data_channel_rx_86++;
		data_channel_rx_ok_86 += ok;
	}
	
	printf(" %d %d %d\n", from_bit, bcast_bit, end_bits);
}

void freedv_data_channel_tx_frame(struct freedv_data_channel *fdc, unsigned char *data, size_t size, int *from_bit, int *bcast_bit, int *crc_bit, int *end_bits)
{
	printf("freedv_data_channel_tx_frame(%p, %p, %zd\n", fdc, data, size);
	
	unsigned char testval = 0;
	int i;
	
	for (i = 0; i < size; i++) {
		data[i] = testval;
		testval += size;
	}
	*end_bits = size;
	*from_bit = 0;
	*bcast_bit = 1;
	*crc_bit = (size == 6);
}

void m6000_test_voice_gen(unsigned char *voice)
{
	int i;
	
	for (i = 0; i < 81; i++) {
		voice[i] = i + 'v';
	}
}

void m6000_test_voice(unsigned char *voice)
{
	int i;
	int r = 1;
	
	for (i = 0; i < 81; i++) {
		unsigned char expected = i + 'v';
		unsigned char value = voice[i];
		int bit;
		bool ok = true;
		
		for (bit = 0; bit < 8; bit++) {
			ber_checked++;
			unsigned char mask = 1 << bit;
			if ((expected & mask) != (value & mask)) {
				ber_errors++;
				ok = false;
			}
		}
		
		if (!ok) {
			printf("Voice byte %d value 0x%02x does not match generated pattern 0x%02x\n", i, value, expected);
			rx_check_failed = true;
			r = 0;
		}
	}
	voice_test_nr += r;
}

#define NR_FRAMES 200

int main(int argc, char **argv)
{
	struct m6000 *m6000 = m6000_create();

	printf("Mode 6000 modem tests\n");
	
	printf("Check m6000_get_modem_sample_rate() ");
	int m_rate = m6000_get_modem_sample_rate(m6000);
	if (m_rate != 48000) {
		printf("modemrate is not 48000\n");
		const_check_failed = true;
	} else {
		printf("Passed\n");
	}
	
	printf("m6000_get_modem_symbol_rate() ");
	int m_symbolrate = m6000_get_modem_symbol_rate(m6000);
	if (m_symbolrate != 6000) {
		printf("symbolrate is not 6000\n");
		const_check_failed = true;
	} else {
		printf("Passed\n");
	}

	printf("m6000_get_n_nom_modem_samples() ");
	int m_framesize = m6000_get_n_nom_modem_samples(m6000);
	if (m_framesize != 5760) {
		printf("framesize is not 5760: %d\n", m_framesize);
		const_check_failed = true;
	} else {
		printf("Passed\n");
	}

	printf("m6000_get_codec_bytes() ");
	int codec_bytes = m6000_get_codec_bytes(m6000);
	if (codec_bytes != 81) {
		printf("codec bytes is not 81: %d\n", codec_bytes);
		const_check_failed = true;
	} else {
		printf("Passed\n");
	}
	
	int i;
	
	bool test_mod = false;
	bool test_demod = false;
	
	if (argc >= 2) {
		if (!strcmp(argv[1], "demod")) {
			test_demod = true;
			test_mod = false;
		}
		if (!strcmp(argv[1], "mod")) {
			test_demod = false;
			test_mod = true;
		}
	}
	if (test_demod) {
		printf("Executing demod test: provide input on stdin\n");
	} else {
		printf("Skipping demod test, to execute run: 'tmode6000 demod <testdata.raw'\n");
	}
	if (test_demod) {
		printf("Executing mod test: Test data generated to stderr\n");
	} else {
		printf("Skipping mod test, to execute run: 'tmode6000 mod 2>testdata.raw'\n");
	}
	
	short frame[m6000_get_n_max_modem_samples(m6000)];
	unsigned char voice[81] = {0};
	if (test_mod) {
		for (i = 0; i < NR_FRAMES; i++) {
			if (i&1) {
				m6000_mod_data(m6000, NULL, frame);
			} else {
				m6000_test_voice_gen(voice);
				m6000_mod_codec(m6000, NULL, frame, voice);
			}
			
			fwrite(frame, m6000_get_n_nom_modem_samples(m6000), sizeof(short), stderr);
		}
	}
	
	if (test_demod) {
		bool cont = true;
		while (cont) {
			int nin = m6000_nin(m6000);
			printf("Read %d samples\n", nin);
			int r = fread(frame, sizeof(short), nin, stdin);
			cont = (r == nin);
			if (!cont)
				continue;

			int r_d = m6000_demod(m6000, NULL, frame, voice);
			
			printf("demod: %d\n", r_d);
			if (r_d)
				m6000_test_voice(voice);

			int sync;
			float snr_est;
			m6000_get_modem_stats(m6000, &sync, &snr_est);

			if (sync)
				demod_sync_nr++;
		
			printf("sync: %d snr_est: %f\n", sync, snr_est);
		}
	}
	
	if (test_mod) {
		printf("Generated %d frames\n", NR_FRAMES);
	}
	if (test_demod) {
		printf("Demod was synced for %d frames\n", demod_sync_nr);
		printf("Demod received %d (out of %d) frames with 6 data bytes\n", 
		    data_channel_rx_ok_6, data_channel_rx_6);
		if (data_channel_rx_ok_6 < (NR_FRAMES/2)-1) {
			printf("RX failed due to to little 6 byte frames\n");
			rx_check_failed = true;
		}
		printf("Demod received %d (out of %d) frames with 86 data bytes\n", 
		    data_channel_rx_ok_86, data_channel_rx_86);
		if (data_channel_rx_ok_86 < (NR_FRAMES/2)-1) {
			printf("RX failed due to to little 86 byte frames\n");
			rx_check_failed = true;
		}
		printf("Demod received %d correct voice frames\n", voice_test_nr);
		printf("BER %f (%d errors in %d bits)\n", (float)ber_errors/(float)ber_checked, ber_errors, ber_checked);
	}

	if (rx_check_failed || const_check_failed) {
		printf("Test failed\n");
		return 1;
	}
	printf("Test passed\n");

	return 0;
}

