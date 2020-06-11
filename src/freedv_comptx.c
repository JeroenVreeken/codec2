/*---------------------------------------------------------------------------*\

  FILE........: freedv_tx.c
  AUTHOR......: David Rowe
  DATE CREATED: August 2014

  Demo transmit program for FreeDV API functions.

\*---------------------------------------------------------------------------*/

/*
  Copyright (C) 2014 David Rowe

  All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License version 2.1, as
  published by the Free Software Foundation.  This program is
  distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
  License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "freedv_api.h"

int main(int argc, char *argv[]) {
    FILE                     *fin, *fout;
    struct freedv            *freedv;
    int                       mode;
    int                       use_testframes, interleave_frames, use_clip, use_txbpf, use_dpsk;
    int                       i;

    if (argc < 4) {
        char f2020[80] = {0};
        #ifdef __LPCNET__
        sprintf(f2020,"|2020");
        #endif     
        printf("usage: %s 1600|700C|700D|2400A|2400B|800XA%s InputRawSpeechFile OutputModemRawFile\n"
               " [--testframes] [--interleave depth] [--clip 0|1] [--txbpf 0|1] [--dpsk]\n", argv[0], f2020);
        printf("e.g    %s 1600 hts1a.raw hts1a_fdmdv.raw\n", argv[0]);
        exit(1);
    }

    mode = -1;
    if (!strcmp(argv[1],"1600")) mode = FREEDV_MODE_1600;
    if (!strcmp(argv[1],"700C")) mode = FREEDV_MODE_700C;
    if (!strcmp(argv[1],"700D")) mode = FREEDV_MODE_700D;
    if (!strcmp(argv[1],"2400A")) mode = FREEDV_MODE_2400A;
    if (!strcmp(argv[1],"2400B")) mode = FREEDV_MODE_2400B;
    if (!strcmp(argv[1],"800XA")) mode = FREEDV_MODE_800XA;
    #ifdef __LPCNET__
    if (!strcmp(argv[1],"2020"))  mode = FREEDV_MODE_2020;
    #endif
    if (mode == -1) {
        fprintf(stderr, "Error in mode: %s\n", argv[1]);
        exit(1);
    }

    if (strcmp(argv[2], "-")  == 0) fin = stdin;
    else if ( (fin = fopen(argv[2],"rb")) == NULL ) {
        fprintf(stderr, "Error opening input raw speech sample file: %s: %s.\n", argv[2], strerror(errno));
        exit(1);
    }

    if (strcmp(argv[3], "-") == 0) fout = stdout;
    else if ( (fout = fopen(argv[3],"wb")) == NULL ) {
        fprintf(stderr, "Error opening output modem sample file: %s: %s.\n", argv[3], strerror(errno));
        exit(1);
    }

    use_testframes = 0; interleave_frames = 1; use_clip = 0; use_txbpf = 1; use_dpsk = 0;
    
    if (argc > 4) {
        for (i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--testframes") == 0) use_testframes = 1;
            else if (strcmp(argv[i], "--interleave") == 0) { interleave_frames = atoi(argv[i+1]); i++; }
            else if (strcmp(argv[i], "--clip") == 0) { use_clip = atoi(argv[i+1]); i++; }
            else if (strcmp(argv[i], "--txbpf") == 0) { use_txbpf = atoi(argv[i+1]); i++; }
            else if (strcmp(argv[i], "--dpsk") == 0) use_dpsk = 1;
            else {
                fprintf(stderr, "unkown option: %s\n", argv[i]);
                exit(1);
            }
        }
    }

    /* freedv_open_advanced() for non-standard start up */ 
    if ((mode == FREEDV_MODE_700D) || (mode == FREEDV_MODE_2020)) {
        struct freedv_advanced adv;
        adv.interleave_frames = interleave_frames;
        freedv = freedv_open_advanced(mode, &adv);
    }
    else {
        /* Just use this normally */
        freedv = freedv_open(mode);
    }
    assert(freedv != NULL);

    /* these are all optional ------------------ */
    freedv_set_test_frames(freedv, use_testframes);
    freedv_set_clip(freedv, use_clip);
    freedv_set_tx_bpf(freedv, use_txbpf);
    freedv_set_dpsk(freedv, use_dpsk);
    freedv_set_verbose(freedv, 1);

    /* handy functions to set buffer sizes, note tx/modulator always
       returns freedv_get_n_nom_modem_samples() (unlike rx side) */
    int n_speech_samples = freedv_get_n_speech_samples(freedv);
    short speech_in[n_speech_samples];
    int n_nom_modem_samples = freedv_get_n_nom_modem_samples(freedv);
    short mod_out[n_nom_modem_samples];
    COMP mod_out_comp[n_nom_modem_samples];

    /* OK main loop  --------------------------------------- */

    while(fread(speech_in, sizeof(short), n_speech_samples, fin) == n_speech_samples) {
        freedv_comptx(freedv, mod_out_comp, speech_in);
	for (i = 0; i < n_nom_modem_samples; i++) {
	   mod_out[i] = mod_out_comp[i].real;
	}
        fwrite(mod_out, sizeof(short), n_nom_modem_samples, fout);
    
        /* if using pipes we don't want the usual buffering to occur */
        if (fout == stdout) fflush(stdout);
        if (fin == stdin) fflush(stdin);
    }
    
    freedv_close(freedv);
    fclose(fin);
    fclose(fout);
    
    return 0;
}

