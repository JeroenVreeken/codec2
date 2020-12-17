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
#include <stdbool.h>

#include "freedv_6000.h"

bool rx_check = true;
bool rx_check_failed = false;
bool const_check_failed = false;

int demod_sync_nr = 0;
int voice_test_nr = 0;

int ber_checked = 0;
int ber_errors = 0;

#define M6000_RATE		48000
#define M6000_SYMBOLRATE	6000
#define M6000_SYMBOLSAMPLES	8
#define M6000_FRAMESIZE		((M6000_RATE * 120)/1000)
#define M6000_FRAMESIZEMAX	(M6000_FRAMESIZE + 8)


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
	
	for (i = 0; i < 48; i++) {
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
	struct freedv *f = freedv_open(FREEDV_MODE_6000);
	
	printf("Mode 6000 modem tests\n");
	
	printf("Check freedv_get_modem_sample_rate() ");
	int m_rate = freedv_get_modem_sample_rate(f);
	if (m_rate != 48000) {
		printf("modemrate is not 48000: %d\n", m_rate);
		const_check_failed = true;
	} else {
		printf("Passed\n");
	}
	
	printf("freedv_get_modem_symbol_rate() ");
	int m_symbolrate = freedv_get_modem_symbol_rate(f);
	if (m_symbolrate != 6000) {
		printf("symbolrate is not 6000: %d\n", m_symbolrate);
		const_check_failed = true;
	} else {
		printf("Passed\n");
	}

	printf("freedv_get_n_nom_modem_samples() ");
	int m_framesize = freedv_get_n_nom_modem_samples(f);
	if (m_framesize != 5760) {
		printf("framesize is not 5760: %d\n", m_framesize);
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
	
	short frame[freedv_get_n_max_modem_samples(f)];
	unsigned char voice[81] = {0};
	if (test_mod) {
		for (i = 0; i < NR_FRAMES; i++) {
			if (i&1) {
				freedv_datatx(f, frame);
			} else {
				m6000_test_voice_gen(voice);
				freedv_rawdatatx(f, frame, voice);
			}
			
			fwrite(frame, freedv_get_n_nom_modem_samples(f), sizeof(short), stderr);
		}
	}
	
	if (test_demod) {
		bool cont = true;
		while (cont) {
			int nin = freedv_nin(f);
			printf("Read %d samples\n", nin);
			int r = fread(frame, sizeof(short), nin, stdin);
			cont = (r == nin);
			if (!cont)
				continue;

			int r_d = freedv_rawdatarx(f, voice, frame);
			
			printf("demod: %d\n", r_d);
			if (r_d)
				m6000_test_voice(voice);

			int sync;
			float snr_est;
			freedv_get_modem_stats(f, &sync, &snr_est);

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

