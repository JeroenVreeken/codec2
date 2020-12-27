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
#include <string.h>
#include <stdlib.h>
#include "freedv_api.h"
#include "assert.h"

unsigned char header[6] = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x42 };

int bigdata = 0;

int datarx_called = 0;
int datarx_from_ok = 0;
void tfreedv_mode6000_callback_datarx(void *arg, unsigned char *packet, size_t size)
{
    datarx_called++;
    //int i;
    //for (i = 0; i < size; i++) {
    //    printf("0x%02x ", packet[i]);
    //}
    //printf(": %zd\n", size);

    if (!bigdata)
    {
        if (size >= 12 && !memcmp(packet + 6, header, 6)) {
            datarx_from_ok++;
        }
    }
}

int datatx_called = 0;
void tfreedv_mode6000_callback_datatx(void *arg, unsigned char *packet, size_t *size)
{
    datatx_called++;
    if (!bigdata) {
        *size = 0;
    } else {
        memset(packet, 0xa5, 1024);
	*size = 512;
    }
}


static unsigned char *mod_store = NULL;
static size_t mod_store_size = 0;
void tfreedv_mode6000_mod_store(short *mod, size_t nr)
{
    size_t new_size = mod_store_size + nr * sizeof(short);
    mod_store = realloc(mod_store, new_size);
    memcpy(mod_store + mod_store_size, mod, nr * sizeof(short));
    mod_store_size = new_size;
}

static size_t mod_store_pos = 0;
short *tfreedv_mode6000_mod_get(size_t nr)
{
    short *r = (short*)mod_store + mod_store_pos;
    mod_store_pos += nr;

    if (mod_store_pos > (mod_store_size / sizeof(short))) {
        printf("Warning: trying to get non-existing mod data from store\n");
    }

    return r;
}


void voice_gen(unsigned char *voicedata)
{
    int i;
    for (i = 0; i < 384/8; i++) {
        voicedata[i] = i + 'v';
    }
}

int voice_check(unsigned char *voicedata)
{
    int i;
    int ret = 0;
    for (i = 0; i < 384/8; i++) {
        if (voicedata[i] != i + 'v') {
            printf("voice byte %d does not match: 0x%02x != 0x%02x\n", i, voicedata[i], i + 'v');
            ret++;
        }
    }
    return ret;
}

int main(int argc, char **argv)
{
    struct freedv *f;
    int i;
    unsigned char packed_codec_bits[384/8];

    printf("freedv_api tests for mode 6000\n");

    printf("freedv_open(FREEDV_MODE_6000) ");
    f = freedv_open(FREEDV_MODE_6000);
    assert(f != NULL);
    printf("Passed\n");

    printf("freedv_get_mode() ");
    int mode = freedv_get_mode(f);
    assert(mode == FREEDV_MODE_6000);
    printf("Passed\n");

    printf("freedv_get_speech_sample_rate() ");
    int speech_sample_rate = freedv_get_speech_sample_rate(f);
    assert(speech_sample_rate == 8000);
    printf("%d Passed\n", speech_sample_rate);

    printf("freedv_get_modem_sample_rate() ");
    int sample_rate = freedv_get_modem_sample_rate(f);
    assert(sample_rate == 48000);
    printf("%d Passed\n", sample_rate);

    printf("freedv_get_modem_symbol_rate() ");
    int symbol_rate = freedv_get_modem_symbol_rate(f);
    assert(symbol_rate == 6000);
    printf("%d Passed\n", symbol_rate);

    printf("freedv_get_n_max_modem_samples() ");
    int max_samples = freedv_get_n_max_modem_samples(f);
    assert(max_samples == 5768);
    printf("%d Passed\n", max_samples);

    printf("freedv_get_n_nom_modem_samples() ");
    int nom_samples = freedv_get_n_nom_modem_samples(f);
    assert(nom_samples == 5760);
    printf("%d Passed\n", nom_samples);

    printf("freedv_get_n_speech_samples() ");
    int speech_samples = freedv_get_n_speech_samples(f);
    assert(speech_samples == 960);
    printf("%d Passed\n", speech_samples);

    printf("freedv_get_n_codec_bits() ");
    int codec_bits = freedv_get_bits_per_modem_frame(f);
    assert(codec_bits == 384);
    printf("%d Passed\n", codec_bits);

    printf("freedv_get_n_modem_symbols() ");
    int n_modem_symbols = freedv_get_n_modem_symbols(f);
    assert(n_modem_symbols == 720);
    printf("%d Passed\n", n_modem_symbols);

    printf("freedv_get_sync() [Initial state check] ");
    int sync = freedv_get_sync(f);
    if (sync) {
        printf("sync value %d was unexpected\n", sync);
        goto fail;
    }
    printf("Passed\n");

    printf("freedv_get_modem_stats() [Initial state check] ");
    float snr_est;
    freedv_get_modem_stats(f, &sync, &snr_est);
    if (sync) {
        printf("sync value %d was unexpected\n", sync);
        goto fail;
    }
    if (snr_est > 1.0) {
        printf("snr_est value %f was unexpected\n", snr_est);
        goto fail;
    }
    printf("Passed\n");

    printf("freedv_set_callback_data() ");
    freedv_set_callback_data(f, tfreedv_mode6000_callback_datarx, tfreedv_mode6000_callback_datatx, NULL);
    printf("Passed\n");

    printf("freedv_set_data_header() ");
    freedv_set_data_header(f, header);
    printf("Passed\n");

    printf("freedv_datatx() ");
    short mod_out[5760];
    datatx_called = 0;
    freedv_datatx(f, mod_out);
    tfreedv_mode6000_mod_store(mod_out, 5760);
    assert(datatx_called == 1);
    printf("Passed\n"); /* store: D */

    printf("multiple freedv_datatx() ");
    datatx_called = 0;
    for (i = 0; i < 9; i++) {
        freedv_datatx(f, mod_out);
        tfreedv_mode6000_mod_store(mod_out, 5760);
    }
    assert(datatx_called == 9);
    printf("Passed\n");  /* store: D DDDDDDDDD */

    printf("multiple freedv_rawdatatx() ");
    voice_gen(packed_codec_bits);
    for (i = 0; i < 11; i++) {
        freedv_rawdatatx(f, mod_out, packed_codec_bits);
        tfreedv_mode6000_mod_store(mod_out, 5760);
    }
    printf("Passed\n"); /* store: D DDDDDDDDD VVVVVVVVVVV */

    printf("freedv_tx() ");
    short speech_in[960] = { 0 };
    freedv_tx(f, mod_out, speech_in);
    tfreedv_mode6000_mod_store(mod_out, 5760);
    printf("Passed\n"); /* store: D DDDDDDDDD VVVVVVVVVVV V */


    freedv_datatx(f, mod_out);
    tfreedv_mode6000_mod_store(mod_out, 5760);
    freedv_datatx(f, mod_out);
    tfreedv_mode6000_mod_store(mod_out, 5760);
    freedv_datatx(f, mod_out);
    tfreedv_mode6000_mod_store(mod_out, 5760);
    /* store: D DDDDDDDDD VVVVVVVVVVV V DDD */


    printf("freedv_nin() ");
    int nin = freedv_nin(f);
    assert(nin == 5768);
    printf("Passed\n");

    printf("freedv_rawdatarx() ");
    nin = freedv_nin(f);
    short *demod_in = tfreedv_mode6000_mod_get(nin);
    int r = freedv_rawdatarx(f, packed_codec_bits, demod_in);
    if (r) {
        printf("Expected only data, no voice: r = %d\n", r);
        goto fail;
    }
    if (datarx_called) {
        printf("Warning unexpected data received\n");
    }
    printf("Passed\n"); /* store: d DDDDDDDDD VVVVVVVVVVV V DDD */

    printf("multiple freedv_rawdatarx() [data] ");
    datarx_called = 0;
    datarx_from_ok = 0;
    for (i = 0; i < 9; i++) {
        nin = freedv_nin(f);
        demod_in = tfreedv_mode6000_mod_get(nin);
        r = freedv_rawdatarx(f, packed_codec_bits, demod_in);
	assert(r == 0);
        sync = freedv_get_sync(f);
        if (!sync) {
            printf("sync value %d was unexpected\n", sync);
        }
    }
    printf("%d %d\n", datarx_called, datarx_from_ok);
    assert(datarx_called == 9);
    assert(datarx_from_ok == 9);
    printf("Passed\n"); /* store: . ........d VVVVVVVVVVV V DDD */

    printf("freedv_get_sync() [After frame] ");
    sync = freedv_get_sync(f);
    if (!sync) {
        printf("sync value %d was unexpected\n", sync);
    }
    printf("Passed\n");

    printf("freedv_rawdatarx() [last data frame] ");
    nin = freedv_nin(f);
    demod_in = tfreedv_mode6000_mod_get(nin);
    r = freedv_rawdatarx(f, packed_codec_bits, demod_in);
    if (r) {
        printf("Expected only data, no voice: r = %d\n", r);
        goto fail;
    }
    printf("Passed\n"); /* store: . ......... vVVVVVVVVVV V DDD */


    printf("multiple freedv_rawdatarx() [voice] ");
    datarx_called = 0;
    datarx_from_ok = 0;
    for (i = 0; i < 10; i++) {
        nin = freedv_nin(f);
        demod_in = tfreedv_mode6000_mod_get(nin);
        memset(packed_codec_bits, 0, 384/8);
        r = freedv_rawdatarx(f, packed_codec_bits, demod_in);
	assert(r == 384/8);
        assert(voice_check(packed_codec_bits) == 0);
        sync = freedv_get_sync(f);
        if (!sync) {
            printf("sync value %d was unexpected\n", sync);
            goto fail;
        }
    }
    assert(datarx_called == 10);
    assert(datarx_from_ok == 10);
    printf("Passed\n"); /* store: . ......... ..........v V DDD */


    printf("freedv_rx() ");
    short speech_out[960] = { 0 };
    demod_in = tfreedv_mode6000_mod_get(nin);
    datarx_called = 0;
    datarx_from_ok = 0;
    r = freedv_rx(f, speech_out, demod_in);
    assert(r == 960);
    assert(datarx_called == 1);
    assert(datarx_from_ok == 1);
    printf("Passed\n"); /* store: . ......... ........... v DDD */

    printf("freedv_rx() ");
    demod_in = tfreedv_mode6000_mod_get(nin);
    datarx_called = 0;
    datarx_from_ok = 0;
    r = freedv_rx(f, speech_out, demod_in);
    assert(r == 960);
    assert(datarx_called == 1);
    assert(datarx_from_ok == 1);
    printf("Passed\n"); /* store: . ......... ........... . dDD */

    printf("freedv_rx() [data]");
        demod_in = tfreedv_mode6000_mod_get(nin);
    datarx_called = 0;
    datarx_from_ok = 0;
    r = freedv_rx(f, speech_out, demod_in);
    assert(r == 0);
    assert(datarx_called == 1);
    assert(datarx_from_ok == 1);
    printf("Passed\n"); /* store: . ......... ........... . .dD */

    printf("multiple freedv_datatx() bigdata ");
    bigdata = 1;
    datatx_called = 0;
    for (i = 0; i < 20; i++) {
        freedv_datatx(f, mod_out);
        tfreedv_mode6000_mod_store(mod_out, 5760);
    }
    assert(datatx_called == 2);
    printf("Passed\n");

    printf("multiple freedv_rawdatarx() [data] ");
    datarx_called = 0;
    datarx_from_ok = 0;
    for (i = 0; i < 20; i++) {
        nin = freedv_nin(f);
        demod_in = tfreedv_mode6000_mod_get(nin);
        r = freedv_rawdatarx(f, packed_codec_bits, demod_in);
	assert(r == 0);
        sync = freedv_get_sync(f);
        if (!sync) {
            printf("sync value %d was unexpected\n", sync);
        }
    }
    assert(datarx_called == 3);
    assert(datarx_from_ok == 0);
    printf("Passed\n"); 


    printf("freedv_close() ");
    freedv_close(f);
    printf("Passed\n");
    
    // Open new instance for symbol tests

    printf("freedv_open(FREEDV_MODE_6000) ");
    f = freedv_open(FREEDV_MODE_6000);
    assert(f != NULL);
    printf("Passed\n");

    printf("freedv_rawdatasymtx\n");
    signed char frame_sym[720] = {0};
    voice_gen(packed_codec_bits);
    freedv_rawdatasymtx(f, frame_sym, packed_codec_bits);

    printf("freedv_rawdatasymrx\n");
    memset(packed_codec_bits, 0, sizeof(packed_codec_bits));
    r = freedv_rawdatasymrx(f, packed_codec_bits, frame_sym);
    assert(r == 384/8);
    assert(voice_check(packed_codec_bits) == 0);
    printf("Passed\n");

    freedv_rawdatasymtx(f, frame_sym, packed_codec_bits);

    printf("freedv_symrx\n");
    memset(packed_codec_bits, 0, sizeof(packed_codec_bits));
    r = freedv_symrx(f, speech_out, frame_sym);
    assert(r == 960);

    memset(frame_sym, 1, sizeof(frame_sym));
    r = freedv_symrx(f, speech_out, frame_sym);
    assert(r == 0);
    r = freedv_rawdatasymrx(f, packed_codec_bits, frame_sym);
    assert(r == 0);

    printf("freedv_close() ");
    freedv_close(f);
    printf("Passed\n");
    
    printf("Tests passed\n");
    return 0;
fail:
    printf("Test failed\n");
    return 1;
}
