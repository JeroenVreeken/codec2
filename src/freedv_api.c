/*---------------------------------------------------------------------------*\

  FILE........: freedv_api.c
  AUTHOR......: David Rowe
  DATE CREATED: August 2014

  Library of API functions that implement FreeDV "modes", useful for
  embedding FreeDV in other programs.

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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "fsk.h"
#include "fmfsk.h"
#include "codec2.h"
#include "codec2_fdmdv.h"
#include "fdmdv_internal.h"
#include "varicode.h"
#include "freedv_api.h"
#include "freedv_api_internal.h"
#include "freedv_vhf_framing.h"
#include "comp_prim.h"
#include "mode6000.h"

#include "codec2_ofdm.h"
#include "ofdm_internal.h"
#include "mpdecode_core.h"
#include "gp_interleaver.h"
#include "interldpc.h"

#include "debug_alloc.h"

#define VERSION     14    /* The API version number.  The first version
                           is 10.  Increment if the API changes in a
                           way that would require changes by the API
                           user. */
/*
 * Version 10   Initial version August 2, 2015.
 * Version 11   September 2015
 *              Added: freedv_zero_total_bit_errors(), freedv_get_sync()
 *              Changed all input and output sample rates to 8000 sps.  Rates for FREEDV_MODE_700 and 700B were 7500.
 * Version 12   August 2018
 *              Added OFDM configuration switch structure
 * Version 13   November 2019
 *              Removed 700 and 700B modes
 * Version 14   May 2020
 *              Number of returned speech samples can vary, use freedv_get_n_max_speech_samples() to allocate
 *              buffers.
 */

char *ofdm_statemode[] = {
    "search",
    "trial",
    "synced"
};

/*---------------------------------------------------------------------------* \

  FUNCTION....: freedv_open
  AUTHOR......: David Rowe
  DATE CREATED: 3 August 2014

  Call this first to initialise.  Returns NULL if initialisation
  fails. If a malloc() or calloc() fails in general asserts() will
  fire.

\*---------------------------------------------------------------------------*/

struct freedv *freedv_open(int mode) {
    return freedv_open_advanced(mode, NULL);
}

struct freedv *freedv_open_advanced(int mode, struct freedv_advanced *adv) {
    struct freedv *f;
    int            codec2_mode, nbit = 0, nbyte = 0;
    
    if (false == (FDV_MODE_ACTIVE( FREEDV_MODE_1600,mode) || FDV_MODE_ACTIVE( FREEDV_MODE_2400A,mode) || 
		FDV_MODE_ACTIVE( FREEDV_MODE_2400B,mode) || FDV_MODE_ACTIVE( FREEDV_MODE_800XA,mode) || 
		FDV_MODE_ACTIVE( FREEDV_MODE_700C,mode) || FDV_MODE_ACTIVE( FREEDV_MODE_700D,mode)  ||
		FDV_MODE_ACTIVE( FREEDV_MODE_2020,mode) || FDV_MODE_ACTIVE( FREEDV_MODE_6000,mode)) )
    {
        return NULL;
    }

    /* set everything to zero just in case */
    f = (struct freedv*)CALLOC(1, sizeof(struct freedv));
    if (f == NULL)
        return NULL;

    f->mode = mode;
    f->speech_sample_rate = FREEDV_FS_8000;
    
    /* -----------------------------------------------------------------------------------------------*\
                                       Init Modem and FEC
    \*------------------------------------------------------------------------------------------------*/
    
    if (FDV_MODE_ACTIVE( FREEDV_MODE_1600, f->mode)) {
        codec2_mode = CODEC2_MODE_1300;
        freedv_1600_open(f);
    }

    if (FDV_MODE_ACTIVE( FREEDV_MODE_700C, mode)) {
        nbit = COHPSK_BITS_PER_FRAME;
        freedv_700c_open(f, nbit);
        codec2_mode = CODEC2_MODE_700C;
    }
   
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700D, mode) ) {
        freedv_700d_open(f, adv);
        codec2_mode = CODEC2_MODE_700C;
        nbit = f->ofdm_bitsperframe;
    }
        
#ifdef __LPCNET__
    if (FDV_MODE_ACTIVE( FREEDV_MODE_2020, mode) ) {
        codec2_mode = CODEC_MODE_LPCNET_1733;
        freedv_2020_open(f, adv);
        nbit = f->ofdm_bitsperframe;
     }
#endif
    
    if (FDV_MODE_ACTIVE( FREEDV_MODE_2400A, mode)) {
        codec2_mode = CODEC2_MODE_1300;
        freedv_2400a_open(f);
    }
    
    if (FDV_MODE_ACTIVE( FREEDV_MODE_2400B, mode) ) {
        codec2_mode = CODEC2_MODE_1300;
        freedv_2400b_open(f);
    }
    
    if (FDV_MODE_ACTIVE( FREEDV_MODE_800XA, mode)) {
        codec2_mode = CODEC2_MODE_700C;
        freedv_800xa_open(f);
    }
    if (FDV_MODE_ACTIVE( FREEDV_MODE_6000, mode)) {
        /* Create modem */
	f->m6000 = m6000_create();
	f->fdc = freedv_data_channel_create();

        f->n_nom_modem_samples = m6000_get_n_nom_modem_samples(f->m6000);
        f->n_max_modem_samples = m6000_get_n_max_modem_samples(f->m6000);
        f->n_nat_modem_samples = m6000_get_n_nom_modem_samples(f->m6000);
        f->nin = m6000_nin(f->m6000);
        f->modem_sample_rate = m6000_get_modem_sample_rate(f->m6000);
        f->modem_symbol_rate = m6000_get_modem_symbol_rate(f->m6000);

        /* Malloc something to appease freedv_init and freedv_destroy */
        f->codec_bits = MALLOC(1);

        f->stats.sync = 0;
        
        codec2_mode = CODEC2_MODE_3200;
    }

    /* -----------------------------------------------------------------------------------------------*\
                                            Init Speech Codec
    \*------------------------------------------------------------------------------------------------*/

    if (codec2_mode == CODEC_MODE_LPCNET_1733) {
#ifdef __LPCNET__
        f->lpcnet = lpcnet_freedv_create(1);
#endif
        f->codec2 = NULL;
    }
    else {
        f->codec2 = codec2_create(codec2_mode);
        if (f->codec2 == NULL)
            return NULL;
    }
    
    /* work out how many codec 2 frames per mode frame, and number of
       bytes of storage for packed and unpacked bits.  TODO: do we really
       need to work in packed bits at all?  It's messy, chars would probably
       be OK.... */
    
    if (FDV_MODE_ACTIVE( FREEDV_MODE_1600, mode) || FDV_MODE_ACTIVE( FREEDV_MODE_2400A, mode) || FDV_MODE_ACTIVE( FREEDV_MODE_2400B, mode)) {
        f->n_speech_samples = codec2_samples_per_frame(f->codec2);
        f->n_codec_bits = codec2_bits_per_frame(f->codec2);
        nbit = f->n_codec_bits;
        nbyte = (nbit + 7) / 8;
    } else if (FDV_MODE_ACTIVE( FREEDV_MODE_800XA, mode) ) {
        f->n_speech_samples = 2*codec2_samples_per_frame(f->codec2);
        f->n_codec_bits = codec2_bits_per_frame(f->codec2);
        nbit = f->n_codec_bits;
        nbyte = (nbit + 7) / 8;
        nbyte = nbyte*2;
        nbit = 8*nbyte;
        f->n_codec_bits = nbit;
    } else if FDV_MODE_ACTIVE( FREEDV_MODE_700C, mode) {
        f->n_speech_samples = 2*codec2_samples_per_frame(f->codec2);
        f->n_codec_bits = 2*codec2_bits_per_frame(f->codec2);
        nbit = f->n_codec_bits;
        nbyte = 2*((codec2_bits_per_frame(f->codec2) + 7) / 8);
        } else if (FDV_MODE_ACTIVE(FREEDV_MODE_700D, mode)) /* mode == FREEDV_MODE_700D */ {

        /* should be exactly an integer number of Codec 2 frames in a OFDM modem frame */

        assert((f->ldpc->data_bits_per_frame % codec2_bits_per_frame(f->codec2)) == 0);

        int Ncodec2frames = f->ldpc->data_bits_per_frame/codec2_bits_per_frame(f->codec2);

        f->n_speech_samples = Ncodec2frames*codec2_samples_per_frame(f->codec2);
        f->n_codec_bits = f->interleave_frames*Ncodec2frames*codec2_bits_per_frame(f->codec2);
        nbit = codec2_bits_per_frame(f->codec2);
        nbyte = (nbit + 7) / 8;
        nbyte = nbyte*Ncodec2frames*f->interleave_frames;
        f->nbyte_packed_codec_bits = nbyte;

        //fprintf(stderr, "Ncodec2frames: %d n_speech_samples: %d n_codec_bits: %d nbit: %d  nbyte: %d\n",
        //        Ncodec2frames, f->n_speech_samples, f->n_codec_bits, nbit, nbyte);

        f->packed_codec_bits_tx = (unsigned char*)MALLOC(nbyte*sizeof(char));
        if (f->packed_codec_bits_tx == NULL) return(NULL);
        f->codec_bits = NULL;
    } else if (FDV_MODE_ACTIVE(FREEDV_MODE_2020, mode)) {
#ifdef __LPCNET__
        /* should be exactly an integer number of Codec frames in a OFDM modem frame */

        assert((f->ldpc->data_bits_per_frame % lpcnet_bits_per_frame(f->lpcnet)) == 0);

        int Ncodecframes = f->ldpc->data_bits_per_frame/lpcnet_bits_per_frame(f->lpcnet);

        f->n_speech_samples = Ncodecframes*lpcnet_samples_per_frame(f->lpcnet);
        f->n_codec_bits = f->interleave_frames*Ncodecframes*lpcnet_bits_per_frame(f->lpcnet);
        nbit = f->n_codec_bits;

        // we actually have unpacked data but uses this as it's convenient
        nbyte = nbit;
#endif
    } else if (FDV_MODE_ACTIVE(FREEDV_MODE_6000, mode)) {
        f->n_speech_samples = 6*codec2_samples_per_frame(f->codec2);
        f->n_codec_bits = 6*codec2_bits_per_frame(f->codec2);
        nbyte = m6000_get_codec_bytes(f->m6000);
    }
    
    f->packed_codec_bits = (unsigned char*)CALLOC(nbyte, sizeof(char));
    if (f->packed_codec_bits == NULL) return(NULL);

    if (FDV_MODE_ACTIVE( FREEDV_MODE_1600, mode))
        f->codec_bits = (int*)MALLOC(nbit*sizeof(int));

    if (FDV_MODE_ACTIVE( FREEDV_MODE_700C, mode))
        f->codec_bits = (int*)MALLOC(COHPSK_BITS_PER_FRAME*sizeof(int));
        
    /* Note: VHF Framer/deframer goes directly from packed codec/vc/proto bits to filled frame */

    if (f->packed_codec_bits == NULL) {
        if (f->codec_bits != NULL) {FREE(f->codec_bits); f->codec_bits = NULL; }
        return NULL;
    }

    /* Varicode low bit rate text states */
    
    varicode_decode_init(&f->varicode_dec_states, 1);

    return f;
}

/*---------------------------------------------------------------------------*\

  FUNCTION....: freedv_close
  AUTHOR......: David Rowe
  DATE CREATED: 3 August 2014

  Call to shut down a freedv instance and free memory.

\*---------------------------------------------------------------------------*/

void freedv_close(struct freedv *freedv) {
    assert(freedv != NULL);

    FREE(freedv->packed_codec_bits);
    FREE(freedv->codec_bits);
    FREE(freedv->tx_bits);
    FREE(freedv->rx_bits);
    if (freedv->codec2) codec2_destroy(freedv->codec2);

    if (FDV_MODE_ACTIVE(FREEDV_MODE_1600, freedv->mode)) {
        FREE(freedv->fdmdv_bits);
        fdmdv_destroy(freedv->fdmdv);
    }

    if (FDV_MODE_ACTIVE( FREEDV_MODE_700C, freedv->mode)) {
        cohpsk_destroy(freedv->cohpsk);
        quisk_filt_destroy(freedv->ptFilter8000to7500);
        FREE(freedv->ptFilter8000to7500);
        quisk_filt_destroy(freedv->ptFilter7500to8000);
        FREE(freedv->ptFilter7500to8000);
    }

    if (FDV_MODE_ACTIVE( FREEDV_MODE_700D, freedv->mode)) {
        FREE(freedv->packed_codec_bits_tx);
        if (freedv->interleave_frames > 1)
            FREE(freedv->mod_out);
        FREE(freedv->codeword_symbols);
        FREE(freedv->codeword_amps);
        FREE(freedv->ldpc);
        ofdm_destroy(freedv->ofdm);
    }

    if (FDV_MODE_ACTIVE( FREEDV_MODE_2020, freedv->mode)) {
        if (freedv->interleave_frames > 1)
            FREE(freedv->mod_out);
        FREE(freedv->codeword_symbols);
        FREE(freedv->codeword_amps);
        FREE(freedv->ldpc);
        FREE(freedv->passthrough_2020);
        ofdm_destroy(freedv->ofdm);
#ifdef __LPCNET__
        lpcnet_freedv_destroy(freedv->lpcnet);
#endif        
    }

    if (FDV_MODE_ACTIVE( FREEDV_MODE_2400A, freedv->mode) || FDV_MODE_ACTIVE( FREEDV_MODE_800XA, freedv->mode)){
        fsk_destroy(freedv->fsk);
        fvhff_destroy_deframer(freedv->deframer);
    }
    
    if (FDV_MODE_ACTIVE( FREEDV_MODE_2400B, freedv->mode)){
        fmfsk_destroy(freedv->fmfsk);
		fvhff_destroy_deframer(freedv->deframer);
    }

    if (FDV_MODE_ACTIVE( FREEDV_MODE_6000, freedv->mode)){
	m6000_destroy(freedv->m6000);
	freedv_data_channel_destroy(freedv->fdc);
    }
    
    FREE(freedv);
}

/*---------------------------------------------------------------------------*\

  FUNCTION....: freedv_tx
  AUTHOR......: David Rowe
  DATE CREATED: 3 August 2014

  Takes a frame of input speech samples, encodes and modulates them to
  produce a frame of modem samples that can be sent to the
  transmitter.  See freedv_tx.c for an example.

  speech_in[] is sampled at freedv_get_speech_sample_rate() Hz, and the
  user must supply a block of exactly
  freedv_get_n_speech_samples(). The speech_in[] level should be such
  that the peak speech level is between +/- 16384 and +/- 32767.

  The FDM modem signal mod_out[] is sampled at
  freedv_get_modem_sample_rate() and is always exactly
  freedv_get_n_nom_modem_samples() long.  mod_out[] will be scaled
  such that the peak level is just less than +/-32767.

  The FreeDV 1600/700C/700D/2020 waveforms have a crest factor of
  around 10dB, similar to SSB.  These modes are usually operated at a
  "backoff" of 8dB.  Adjust the power amplifier drive so that the
  average power is 8dB less than the peak power of the PA.  For
  example, on a radio rated at 100W PEP for SSB, the average FreeDV
  power is typically 20W.

  Caution - some PAs cannot handle a high continuous power.  A
  conservative level is 20W average for a 100W PEP rated PA.

  The FreeDV 2400A/800XA modes are constant amplitude, designed for
  Class C PAs.  They have a crest factor of 3dB. If using a SSB PA,
  adjust the drive so you average power is within the limits of your PA
  (e.g. 20W average for a 100W PA).

\*---------------------------------------------------------------------------*/

/* real-valued short output */

void freedv_tx(struct freedv *f, short mod_out[], short speech_in[]) {
    assert(f != NULL);
    COMP tx_fdm[f->n_nom_modem_samples];
    int  i;
    assert((FDV_MODE_ACTIVE( FREEDV_MODE_1600, f->mode))  || (FDV_MODE_ACTIVE( FREEDV_MODE_700C, f->mode))  ||
           (FDV_MODE_ACTIVE( FREEDV_MODE_700D, f->mode))  || 
           (FDV_MODE_ACTIVE( FREEDV_MODE_2400A, f->mode)) || (FDV_MODE_ACTIVE( FREEDV_MODE_2400B, f->mode)) || 
           (FDV_MODE_ACTIVE( FREEDV_MODE_800XA, f->mode)) || (FDV_MODE_ACTIVE( FREEDV_MODE_2020, f->mode)) ||
	   (FDV_MODE_ACTIVE( FREEDV_MODE_6000, f->mode)) );
    
    /* FSK and MEFSK/FMFSK modems work only on real samples. It's simpler to just 
     * stick them in the real sample tx/rx functions than to add a comp->real converter
     * to comptx */
     
    if ((FDV_MODE_ACTIVE( FREEDV_MODE_2400A, f->mode)) || (FDV_MODE_ACTIVE( FREEDV_MODE_2400B, f->mode)) || (FDV_MODE_ACTIVE( FREEDV_MODE_800XA, f->mode))){
        /* 800XA has two codec frames per modem frame */
        if(FDV_MODE_ACTIVE( FREEDV_MODE_800XA, f->mode)){
            codec2_encode(f->codec2, &f->packed_codec_bits[0], &speech_in[  0]);
            codec2_encode(f->codec2, &f->packed_codec_bits[4], &speech_in[320]);
        }else{
            codec2_encode(f->codec2, f->packed_codec_bits, speech_in);
        }
        freedv_tx_fsk_voice(f, mod_out);
    } else if (FDV_MODE_ACTIVE( FREEDV_MODE_6000, f->mode)) {
        int bits_per_codec_frame = codec2_bits_per_frame(f->codec2);
	int speech_per_codec_frame = codec2_samples_per_frame(f->codec2);
	int bytes_per_codec_frame = (bits_per_codec_frame + 7) / 8;
      	int codec_frames = f->n_codec_bits / bits_per_codec_frame;

	for (i=0; i < codec_frames; i++) {
	    codec2_encode(f->codec2, &f->packed_codec_bits[i*bytes_per_codec_frame], &speech_in[i*speech_per_codec_frame]);
	}
        m6000_mod_codec(f->m6000, f->fdc, mod_out, f->packed_codec_bits);
    } else{
        freedv_comptx(f, tx_fdm, speech_in);
        for(i=0; i<f->n_nom_modem_samples; i++)
            mod_out[i] = tx_fdm[i].real;
    }
}

/* complex float output samples */

void freedv_comptx(struct freedv *f, COMP mod_out[], short speech_in[]) {
    assert(f != NULL);

    assert((FDV_MODE_ACTIVE( FREEDV_MODE_1600, f->mode)) || (FDV_MODE_ACTIVE( FREEDV_MODE_700C, f->mode)) || 
           (FDV_MODE_ACTIVE( FREEDV_MODE_2400A, f->mode)) || (FDV_MODE_ACTIVE( FREEDV_MODE_2400B, f->mode)) ||
           (FDV_MODE_ACTIVE( FREEDV_MODE_700D, f->mode))  || (FDV_MODE_ACTIVE( FREEDV_MODE_2020, f->mode)));

    if (FDV_MODE_ACTIVE( FREEDV_MODE_1600, f->mode)) {
        codec2_encode(f->codec2, f->packed_codec_bits, speech_in);
        freedv_comptx_fdmdv_1600(f, mod_out);
    }

    int bits_per_codec_frame=0; int bytes_per_codec_frame=0;
    if (f->codec2) {
        bits_per_codec_frame = codec2_bits_per_frame(f->codec2);
        bytes_per_codec_frame = (bits_per_codec_frame + 7) / 8;
    }
    int i,j;
    
    /* all these modes need to pack a bunch of codec frames into one modem frame */
    
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700C, f->mode)) {
	int codec_frames = f->n_codec_bits / bits_per_codec_frame;

        for (j=0; j<codec_frames; j++) {
            codec2_encode(f->codec2, f->packed_codec_bits + j * bytes_per_codec_frame, speech_in);
            speech_in += codec2_samples_per_frame(f->codec2);
        }
        freedv_comptx_700c(f, mod_out);
    }

    /* special treatment due to interleaver */
    
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700D, f->mode)) {
        int data_bits_per_frame = f->ldpc->data_bits_per_frame;
	int codec_frames = data_bits_per_frame / bits_per_codec_frame;

        //fprintf(stderr, "modem_frame_count_tx: %d dec_frames: %d bytes offset: %d\n",
        //        f->modem_frame_count_tx, codec_frames, (f->modem_frame_count_tx*codec_frames)*bytes_per_codec_frame);
       
        /* buffer up bits until we get enough encoded bits for interleaver */
        
        for (j=0; j<codec_frames; j++) {
            codec2_encode(f->codec2, f->packed_codec_bits_tx + (f->modem_frame_count_tx*codec_frames+j)*bytes_per_codec_frame, speech_in);
            speech_in += codec2_samples_per_frame(f->codec2);
        }

	/* Only use extra local buffer if needed for interleave > 1 */
	if (f->interleave_frames == 1) {
            freedv_comptx_700d(f, mod_out);
	} else {
            /* call modulate function when we have enough frames to run interleaver */
            assert((f->modem_frame_count_tx >= 0) && 
	    		(f->modem_frame_count_tx < f->interleave_frames));
            f->modem_frame_count_tx++;
            if (f->modem_frame_count_tx == f->interleave_frames) {
                freedv_comptx_700d(f, f->mod_out);
                //fprintf(stderr, "  calling freedv_comptx_700d()\n");
                f->modem_frame_count_tx = 0;
            }
            /* output n_nom_modem_samples at a time from modulated buffer */
            for(i=0; i<f->n_nat_modem_samples; i++) {
                mod_out[i] = 
		    f->mod_out[f->modem_frame_count_tx * f->n_nat_modem_samples+i];
            }
	}
    }
    
#ifdef __LPCNET__
    /* special treatment due to interleaver */
    
    if (FDV_MODE_ACTIVE( FREEDV_MODE_2020, f->mode)) {
        int bits_per_codec_frame = lpcnet_bits_per_frame(f->lpcnet);
        int data_bits_per_frame = f->ldpc->data_bits_per_frame;
	int codec_frames = data_bits_per_frame / bits_per_codec_frame;
        
        //fprintf(stderr, "modem_frame_count_tx: %d dec_frames: %d bytes offset: %d\n",
        //        f->modem_frame_count_tx, codec_frames, (f->modem_frame_count_tx*codec_frames)*bytes_per_codec_frame);
       
        /* buffer up bits until we get enough encoded bits for interleaver */
        
        for (j=0; j<codec_frames; j++) {
            lpcnet_enc(f->lpcnet, speech_in, (char*)f->packed_codec_bits + (f->modem_frame_count_tx*codec_frames+j)*bits_per_codec_frame);
            speech_in += lpcnet_samples_per_frame(f->lpcnet);
        }

	/* Only use extra local buffer if needed for interleave > 1 */
	if (f->interleave_frames == 1) {
            freedv_comptx_2020(f, mod_out);
	} else {
            /* call modulate function when we have enough frames to run interleaver */
            assert((f->modem_frame_count_tx >= 0) && 
	    		(f->modem_frame_count_tx < f->interleave_frames));
            f->modem_frame_count_tx++;
            if (f->modem_frame_count_tx == f->interleave_frames) {
                freedv_comptx_2020(f, f->mod_out);
                //fprintf(stderr, "  calling freedv_comptx_700d()\n");
                f->modem_frame_count_tx = 0;
            }
            /* output n_nom_modem_samples at a time from modulated buffer */
            for(i=0; i<f->n_nat_modem_samples; i++) {
                mod_out[i] = 
		    f->mod_out[f->modem_frame_count_tx * f->n_nat_modem_samples+i];
            }
	}
    }
#endif
    
    /* 2400 A and B are handled by the real-mode TX */
    if(FDV_MODE_ACTIVE( FREEDV_MODE_2400A, f->mode) || FDV_MODE_ACTIVE( FREEDV_MODE_2400B, f->mode)){
    	codec2_encode(f->codec2, f->packed_codec_bits, speech_in);
        freedv_comptx_fsk_voice(f,mod_out);
    }
}

void freedv_codectx(struct freedv *f, short mod_out[], unsigned char *packed_codec_bits) {
    assert(f != NULL);
    COMP tx_fdm[f->n_nom_modem_samples];
    int bits_per_codec_frame;
    int bytes_per_codec_frame;
    int codec_frames;
    int  i;
    bits_per_codec_frame = codec2_bits_per_frame(f->codec2);
    bytes_per_codec_frame = (bits_per_codec_frame + 7) / 8;
    codec_frames = f->n_codec_bits / bits_per_codec_frame;

    memcpy(f->packed_codec_bits, packed_codec_bits, bytes_per_codec_frame * codec_frames);
    
    switch(f->mode) {
        case FREEDV_MODE_1600:
            freedv_comptx_fdmdv_1600(f, tx_fdm);
            break;
        case FREEDV_MODE_700C:
            freedv_comptx_700c(f, tx_fdm);
            break;
        case FREEDV_MODE_700D: {
            /* special treatment due to interleaver */
            int data_bits_per_frame = f->ldpc->data_bits_per_frame;
	    int codec_frames = data_bits_per_frame / bits_per_codec_frame;
	    int j;

            /* buffer up bits until we get enough encoded bits for interleaver */
        
            for (j=0; j<codec_frames; j++) {
                memcpy(f->packed_codec_bits_tx + (f->modem_frame_count_tx*codec_frames+j)*bytes_per_codec_frame, packed_codec_bits, bytes_per_codec_frame);
	        packed_codec_bits += bytes_per_codec_frame;
            }

            /* call modulate function when we have enough frames to run interleaver */

            assert((f->modem_frame_count_tx >= 0) && (f->modem_frame_count_tx < f->interleave_frames));
            f->modem_frame_count_tx++;
            if (f->modem_frame_count_tx == f->interleave_frames) {
                freedv_comptx_700d(f, f->mod_out);
                f->modem_frame_count_tx = 0;
            }

            /* output n_nom_modem_samples at a time from modulated buffer */
            for(i=0; i<f->n_nat_modem_samples; i++) {
                mod_out[i] = f->mod_out[f->modem_frame_count_tx*f->n_nat_modem_samples+i].real;
            }

	    return; /* output is already real */
	}
        case FREEDV_MODE_2400A:
        case FREEDV_MODE_2400B:
        case FREEDV_MODE_800XA:
            freedv_tx_fsk_voice(f, mod_out);
            return; /* output is already real */
	case FREEDV_MODE_6000:
            m6000_mod_codec(f->m6000, f->fdc, mod_out, f->packed_codec_bits);
	    return;
    }
    /* convert complex to real */
    for(i=0; i<f->n_nom_modem_samples; i++)
        mod_out[i] = tx_fdm[i].real;
}

void freedv_datatx (struct freedv *f, short mod_out[]) {
    assert(f != NULL);
    if (FDV_MODE_ACTIVE( FREEDV_MODE_2400A, f->mode) || FDV_MODE_ACTIVE( FREEDV_MODE_2400B, f->mode) || FDV_MODE_ACTIVE( FREEDV_MODE_800XA, f->mode)) {
        freedv_tx_fsk_data(f, mod_out);
    }
    if (FDV_MODE_ACTIVE( FREEDV_MODE_6000, f->mode)) {
    	m6000_mod_data(f->m6000, f->fdc, mod_out);
    }}

int  freedv_data_ntxframes (struct freedv *f) {
    assert(f != NULL);
    if (FDV_MODE_ACTIVE( FREEDV_MODE_2400A, f->mode) || FDV_MODE_ACTIVE( FREEDV_MODE_2400B, f->mode)) {
        if (f->deframer->fdc)
            return freedv_data_get_n_tx_frames(f->deframer->fdc, 8);
    } else if (FDV_MODE_ACTIVE( FREEDV_MODE_800XA, f->mode)) {
        if (f->deframer->fdc)
            return freedv_data_get_n_tx_frames(f->deframer->fdc, 6);
    }
    return 0;
}

int freedv_nin(struct freedv *f) {
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700C, f->mode))
        // For mode 700, the input rate is 8000 sps, but the modem rate is 7500 sps
        // For mode 700, we request a larger number of Rx samples that will be decimated to f->nin samples
        return (16 * f->nin + f->ptFilter8000to7500->decim_index) / 15;
    else
        return f->nin;
}

/*---------------------------------------------------------------------------*\

  FUNCTION....: freedv_rx
  AUTHOR......: David Rowe
  DATE CREATED: 3 August 2014

  Takes samples from the radio receiver, demodulates and FEC decodes
  them, producing a frame of decoded speech samples.  See freedv_rx.c
  for an example.

  demod_in[] is a block of received samples sampled at
  freedv_get_modem_sample_rate().  To account for difference in the
  transmit and receive sample clock frequencies, the number of
  demod_in[] samples is time varying. You MUST call freedv_nin()
  BEFORE EACH call to freedv_rx() and pass exactly that many samples
  to this function:

  short demod_in[freedv_get_n_max_modem_samples(f)];
  short speech_out[freedv_get_n_max_speech_samples(f)];

  nin = freedv_nin(f);
  while(fread(demod_in, sizeof(short), nin, fin) == nin) {
      nout = freedv_rx(f, speech_out, demod_in);
      fwrite(speech_out, sizeof(short), nout, fout);
      nin = freedv_nin(f);
  }

  To help set your buffer sizes, The maximum value of freedv_nin() is
  freedv_get_n_max_modem_samples().

  freedv_rx() returns the number of output speech samples available in
  speech_out[], which is sampled at freedv_get_speech_sample_rate(f).
  You should ALWAYS check the return value of freedv_rx(), and read
  EXACTLY that number of speech samples from speech_out[].  

  Not every call to freedv_rx will return speech samples; in some
  modes several modem frames are processed before speech samples are
  returned.  When squelch is active, zero samples may be returned.

  1600 and 700D mode: When out of sync, the number of output speech
  samples returned will be freedv_nin(). When in sync to a valid
  FreeDV 1600 signal, the number of output speech samples will
  alternate between freedv_get_n_speech_samples() and 0.

  The peak level of demod_in[] is not critical, as the demod works
  well over a wide range of amplitude scaling.  However avoid clipping
  (overload, or samples pinned to +/- 32767).  speech_out[] will peak
  at just less than +/-32767.

  When squelch is disabled, this function echoes the demod_in[]
  samples to speech_out[].  This allows the user to listen to the
  channel, which is useful for tuning FreeDV signals or reception of
  non-FreeDV signals.

\*---------------------------------------------------------------------------*/

int freedv_rx(struct freedv *f, short speech_out[], short demod_in[]) {
    assert(f != NULL);
    int i;
    int nin = freedv_nin(f);
    f->nin_prev = nin;

    assert(nin <= f->n_max_modem_samples);
       
    /* FSK RX happens in real floats, so convert to those and call their demod here */
    if( (FDV_MODE_ACTIVE( FREEDV_MODE_2400A, f->mode)) || (FDV_MODE_ACTIVE( FREEDV_MODE_2400B, f->mode)) || (FDV_MODE_ACTIVE( FREEDV_MODE_800XA, f->mode)) ){
        float rx_float[f->n_max_modem_samples];
        for(i=0; i<nin; i++) {
            rx_float[i] = ((float)demod_in[i]);
        }
        return freedv_floatrx(f,speech_out,rx_float);
    }
    
    if ( (FDV_MODE_ACTIVE( FREEDV_MODE_1600, f->mode)) || (FDV_MODE_ACTIVE( FREEDV_MODE_700C, f->mode))
         || (FDV_MODE_ACTIVE( FREEDV_MODE_2020, f->mode))) {

        float gain = 1.0;
        
        assert(nin <= f->n_max_modem_samples);
        COMP rx_fdm[f->n_max_modem_samples];
        for(i=0; i<nin; i++) {
            rx_fdm[i].real = gain*(float)demod_in[i];
            rx_fdm[i].imag = 0.0;
        }
        return freedv_comprx(f, speech_out, rx_fdm);
    }

    /* special low memory version for 700D, to help with stm32 port */
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700D, f->mode)) {
        float gain = 2.0; /* keep levels the same as Octave simulations and C unit tests for real signals */
        return freedv_shortrx(f, speech_out, demod_in, gain);
    }
    

    if (FDV_MODE_ACTIVE( FREEDV_MODE_6000, f->mode)) {
        int ret = m6000_demod(f->m6000, f->fdc, demod_in, f->packed_codec_bits);
	
	if (ret) {
		ret = f->n_speech_samples;
	}
        int bits_per_codec_frame = codec2_bits_per_frame(f->codec2);
	int speech_per_codec_frame = codec2_samples_per_frame(f->codec2);
	int bytes_per_codec_frame = (bits_per_codec_frame + 7) / 8;
      	int codec_frames = f->n_codec_bits / bits_per_codec_frame;

	for (i=0; i < codec_frames; i++) {
	    codec2_decode(f->codec2, &speech_out[i*speech_per_codec_frame], &f->packed_codec_bits[i*bytes_per_codec_frame]);
	}
	
    	f->nin = m6000_nin(f->m6000);
	m6000_get_modem_stats(f->m6000, &f->stats.sync, NULL);

	return ret;
    }

    assert(1); /* should never get here */
    return 0;
}

/* complex sample input version from the radio */

int freedv_comprx(struct freedv *f, short speech_out[], COMP demod_in[]) {
    assert(f != NULL);    
    assert(f->nin <= f->n_max_modem_samples);
    int rx_status = 0;
    f->nin_prev = freedv_nin(f);
   
    if (FDV_MODE_ACTIVE( FREEDV_MODE_1600, f->mode)) {
        rx_status = freedv_comprx_fdmdv_1600(f, demod_in);
    }
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700C, f->mode)) {
        rx_status = freedv_comprx_700c(f, demod_in);
    }

    if( (FDV_MODE_ACTIVE( FREEDV_MODE_2400A, f->mode)) || (FDV_MODE_ACTIVE( FREEDV_MODE_2400B, f->mode)) || (FDV_MODE_ACTIVE( FREEDV_MODE_800XA, f->mode))){
        rx_status = freedv_comprx_fsk(f, demod_in);
    }

    if (FDV_MODE_ACTIVE( FREEDV_MODE_700D, f->mode)) {
        rx_status = freedv_comp_short_rx_700d(f, (void*)demod_in, 0, 1.0);
    }
    
    if (FDV_MODE_ACTIVE( FREEDV_MODE_2020, f->mode)) {
#ifdef __LPCNET__
        rx_status = freedv_comprx_2020(f, demod_in);
#endif
    }

    short demod_in_short[f->nin_prev];
    for(int i=0; i<f->nin_prev; i++)
        demod_in_short[i] = demod_in[i].real;
    return freedv_bits_to_speech(f, speech_out, demod_in_short, rx_status);
}

/* memory efficient real short version - just for 700D on the SM1000 */

int freedv_shortrx(struct freedv *f, short speech_out[], short demod_in[], float gain) {
    assert(f != NULL);
    int rx_status;
    f->nin_prev = f->nin;

    // At this stage short interface only supported for 700D, to help
    // memory requirements on stm32
    assert(f->mode == FREEDV_MODE_700D);
    assert(f->nin <= f->n_max_modem_samples);

    if (FDV_MODE_ACTIVE( FREEDV_MODE_700D, f->mode)) {
        rx_status = freedv_comp_short_rx_700d(f, (void*)demod_in, 1, gain);
    }
    
    return freedv_bits_to_speech(f, speech_out, demod_in, rx_status);
}

/*---------------------------------------------------------------------------*\

  FUNCTION....: freedv_rx_bits_to_speech
  AUTHOR......: David Rowe
  DATE CREATED: May 2020

  The *_rx functions takes off air samples, demodulate and (for some
  modes) FEC decode, giving us a frame of bits.

  This function captures a lot of tricky logic that has been distilled
  through experience:

  There may not be a frame of bits returned on every call freedv_*rx* call.
  When there are valid bits we need to run the speech decoder.
  We may not have demod sync, so various pass through options may happen
  with the input samples.  
  We may squelch based on SNR.
  Need to handle various codecs, and varying number of codec frames per modem frame
  Squelch audio if test frames are being sent
  Determine how many speech samples to return, which will vary if in sync/out of sync
  Work with real and complex inputs (complex wrapper)
  Attenuate audio on pass through
  Deal with 700D first frame burble, and different sync states from OFDM modes like 700D
  Output no samples if squelched, we assume it's OK for the audio sink to run dry
  A FIFO required on output to smooth sample flow to audio sink

\*---------------------------------------------------------------------------*/

int freedv_bits_to_speech(struct freedv *f, short speech_out[], short demod_in[], int rx_status) {
    int nout = 0;
    int decode_speech = 0;
   
    if ((rx_status & RX_SYNC) == 0) {

        if (f->squelch_en == 0) {

            /* attenuate audio 12dB bit as channel noise isn't that pleasant */
            float passthrough_gain = 0.25;
            
            /* pass through received samples so we can hear what's going on, e.g. during tuning */

            if (f->mode == FREEDV_MODE_2020) {           
                /* 8kHz modem sample rate but 16 kHz speech sample
                   rate, so we need to resample */
                nout = 2*f->nin_prev;
                assert(nout <= freedv_get_n_max_speech_samples(f));
                float tmp[nout];
                for(int i=0; i<nout/2; i++)
                    f->passthrough_2020[FDMDV_OS_TAPS_16K+i] = demod_in[i];
                fdmdv_8_to_16(tmp, &f->passthrough_2020[FDMDV_OS_TAPS_16K], nout/2);
                for(int i=0; i<nout; i++)
                    speech_out[i] = passthrough_gain*tmp[i];
            } else {
                nout = f->nin_prev;                    
                for(int i=0; i<nout; i++)
                    speech_out[i] = passthrough_gain*demod_in[i];
           }
        }
    }

    if ((rx_status & RX_SYNC) && (rx_status & RX_BITS)) {
       /* following logic is tricky so spell it out clearly, see table
          in: https://github.com/drowe67/codec2/pull/111 */
        
       if (f->squelch_en == 0) {
           decode_speech = 1;
       }
       else {
           /* squelch is enabled */
           
           /* anti-burble case - don't decode on trial sync unless the
              frame has no bit errors.  This prevents short lived trial
              sync cases generating random bursts of audio */
           if (rx_status & RX_TRIAL_SYNC) {
               if ((rx_status & RX_BIT_ERRORS) == 0)
                   decode_speech = 1;
           }
           else {
               /* sync is solid - decode even through fades as there is still some speech info there */
               if (f->stats.snr_est > f->snr_squelch_thresh)
                   decode_speech = 1;
           }
       }
    }
   
    if (decode_speech) {
        if(FDV_MODE_ACTIVE( FREEDV_MODE_2020, f->mode)) {
#ifdef __LPCNET__
            /* LPCNet decoder */
            int bits_per_codec_frame = lpcnet_bits_per_frame(f->lpcnet);
            int data_bits_per_frame = f->ldpc->data_bits_per_frame;
            int frames = data_bits_per_frame/bits_per_codec_frame;            
        
            if (f->modem_frame_count_rx < f->interleave_frames) {
                nout = f->n_speech_samples;
                for (int i = 0; i < frames; i++) {
                    lpcnet_dec(f->lpcnet, (char*)f->packed_codec_bits + (i + frames*f->modem_frame_count_rx)* bits_per_codec_frame, speech_out);
                    speech_out += lpcnet_samples_per_frame(f->lpcnet);
                }
                f->modem_frame_count_rx++;
            }
 #endif          
        }
        else {
            /* codec 2 decoder */
            int bits_per_codec_frame  = codec2_bits_per_frame(f->codec2);
            int bytes_per_codec_frame = (bits_per_codec_frame + 7) / 8;

            if(FDV_MODE_ACTIVE( FREEDV_MODE_700D, f->mode)) {

                /* 700D a bit special due to interleaving */

                if (f->modem_frame_count_rx < f->interleave_frames) {
                    int data_bits_per_frame = f->ldpc->data_bits_per_frame;
                    int frames = data_bits_per_frame/bits_per_codec_frame;            
                    nout = f->n_speech_samples;
                    for (int i = 0; i < frames; i++) {
                        codec2_decode(f->codec2, speech_out, f->packed_codec_bits + (i + frames*f->modem_frame_count_rx)* bytes_per_codec_frame);
                        speech_out += codec2_samples_per_frame(f->codec2);
                    }
                    f->modem_frame_count_rx++;
                }
            } else { 
                /* non-interleaved Codec 2 modes */
                
                nout = f->n_speech_samples;
                int frames = f->n_codec_bits / bits_per_codec_frame;
                for (int i = 0; i < frames; i++) {
                    codec2_decode(f->codec2, speech_out, f->packed_codec_bits + i * bytes_per_codec_frame);
                    speech_out += codec2_samples_per_frame(f->codec2);
                }
            }
        }
    }

    if (f->verbose == 2) {
        fprintf(stderr, "    sqen: %d nout: %d decsp: %d\n", f->squelch_en, nout, decode_speech);
    }
    
    assert(nout <= freedv_get_n_max_speech_samples(f));    
    return nout;
}


int freedv_codecrx(struct freedv *f, unsigned char *packed_codec_bits, short demod_in[])
{
    assert(f != NULL);
    int i;
    int nin = freedv_nin(f);
    int ret = 0;
    int bits_per_codec_frame = codec2_bits_per_frame(f->codec2);
    int rx_status = 0;
    
    assert(nin <= f->n_max_modem_samples);

    if (FDV_MODE_ACTIVE( FREEDV_MODE_6000, f->mode)) {
        ret = m6000_demod(f->m6000, f->fdc, demod_in, f->packed_codec_bits);
	
	if (ret) {
		ret = f->n_codec_bits / 8;
		memcpy(packed_codec_bits, f->packed_codec_bits, ret);
	}
	
    	f->nin = m6000_nin(f->m6000);
	m6000_get_modem_stats(f->m6000, &f->stats.sync, NULL);

	return ret;
    }

    f->nin_prev = nin;
    
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700D, f->mode) == false) {
        COMP rx_fdm[f->n_max_modem_samples];
    
        for(i=0; i<nin; i++) {
            rx_fdm[i].real = (float)demod_in[i];
            rx_fdm[i].imag = 0.0;
        }

        if (FDV_MODE_ACTIVE( FREEDV_MODE_1600, f->mode)) {
            rx_status = freedv_comprx_fdmdv_1600(f, rx_fdm);
        }

        if (FDV_MODE_ACTIVE( FREEDV_MODE_700C, f->mode)) {
            rx_status = freedv_comprx_700c(f, rx_fdm);
        }

        if( FDV_MODE_ACTIVE( FREEDV_MODE_2400A, f->mode) || FDV_MODE_ACTIVE( FREEDV_MODE_2400B, f->mode) || FDV_MODE_ACTIVE( FREEDV_MODE_800XA, f->mode)){
            rx_status = freedv_comprx_fsk(f, rx_fdm);
        }
    }

    int bytes_per_codec_frame = (bits_per_codec_frame + 7) / 8;

    if (FDV_MODE_ACTIVE( FREEDV_MODE_700D, f->mode)) {
        rx_status = freedv_comp_short_rx_700d(f, (void*)demod_in, 1, 1.0);

        int data_bits_per_frame = f->ldpc->data_bits_per_frame;
        int frames = data_bits_per_frame/bits_per_codec_frame;
            
        if ((rx_status & RX_BITS) && f->modem_frame_count_rx < f->interleave_frames) {
             for (i = 0; i < frames; i++) {
                 memcpy(packed_codec_bits, f->packed_codec_bits + (i + frames*f->modem_frame_count_rx)* bytes_per_codec_frame, bytes_per_codec_frame);
                 packed_codec_bits += bytes_per_codec_frame;
                 ret += bytes_per_codec_frame;
             }
             f->modem_frame_count_rx++;
        }
	return ret;
    }

    if (rx_status & RX_BITS) {
        int codec_frames = f->n_codec_bits / bits_per_codec_frame;

        memcpy(packed_codec_bits, f->packed_codec_bits, bytes_per_codec_frame * codec_frames);
	ret = bytes_per_codec_frame * codec_frames;
    }
    
    return ret;
}

/*---------------------------------------------------------------------------*\

  FUNCTION....: freedv_get_version
  AUTHOR......: Jim Ahlstrom
  DATE CREATED: 28 July 2015

  Return the version of the FreeDV API.  This is meant to help API
  users determine when incompatible changes have occurred.

\*---------------------------------------------------------------------------*/

int freedv_get_version(void)
{
    return VERSION;
}

/*---------------------------------------------------------------------------*\

  FUNCTION....: freedv_set_callback_txt
  AUTHOR......: Jim Ahlstrom
  DATE CREATED: 28 July 2015

  Set the callback functions and the callback state pointer that will be used
  for the aux txt channel.  The freedv_callback_rx is a function pointer that
  will be called to return received characters.  The freedv_callback_tx is a
  function pointer that will be called to send transmitted characters.  The callback
  state is a user-defined void pointer that will be passed to the callback functions.
  Any or all can be NULL, and the default is all NULL.
  The function signatures are:
    void receive_char(void *callback_state, char c);
    char transmit_char(void *callback_state);

\*---------------------------------------------------------------------------*/

void freedv_set_callback_txt(struct freedv *f, freedv_callback_rx rx, freedv_callback_tx tx, void *state)
{
    if (FDV_MODE_ACTIVE( FREEDV_MODE_800XA, f->mode) == false) {
        f->freedv_put_next_rx_char = rx;
        f->freedv_get_next_tx_char = tx;
        f->callback_state = state;
    }
}

/*---------------------------------------------------------------------------*\

  FUNCTION....: freedv_set_callback_protocol
  AUTHOR......: Brady OBrien
  DATE CREATED: 21 February 2016

  Set the callback functions and callback pointer that will be used for the
  protocol data channel. freedv_callback_protorx will be called when a frame
  containing protocol data arrives. freedv_callback_prototx will be called
  when a frame containing protocol information is being generated. Protocol
  information is intended to be used to develop protocols and fancy features
  atop VHF freedv, much like those present in DMR.
   Protocol bits are to be passed in an msb-first char array
   The number of protocol bits are findable with freedv_get_protocol_bits

\*---------------------------------------------------------------------------*/

void freedv_set_callback_protocol(struct freedv *f, freedv_callback_protorx rx, freedv_callback_prototx tx, void *callback_state){
    if (FDV_MODE_ACTIVE( FREEDV_MODE_800XA, f->mode) == false) {
        f->freedv_put_next_proto = rx;
        f->freedv_get_next_proto = tx;
        f->proto_callback_state = callback_state;
    }
}

/*---------------------------------------------------------------------------*\

  FUNCTION....: freedv_set_callback_datarx / freedv_set_callback_datatx
  AUTHOR......: Jeroen Vreeken
  DATE CREATED: 04 March 2016

  Set the callback functions and callback pointer that will be used for the
  data channel. freedv_callback_datarx will be called when a packet has been
  successfully received. freedv_callback_data_tx will be called when 
  transmission of a new packet can begin.
  If the returned size of the datatx callback is zero the data frame is still
  generated, but will contain only a header update.

\*---------------------------------------------------------------------------*/

void freedv_set_callback_data(struct freedv *f, freedv_callback_datarx datarx, freedv_callback_datatx datatx, void *callback_state) {
    if ((FDV_MODE_ACTIVE( FREEDV_MODE_2400A, f->mode)) || (FDV_MODE_ACTIVE( FREEDV_MODE_2400B, f->mode)) || (FDV_MODE_ACTIVE( FREEDV_MODE_800XA, f->mode))){
        if (!f->deframer->fdc)
            f->deframer->fdc = freedv_data_channel_create();
        if (!f->deframer->fdc)
            return;
        
        freedv_data_set_cb_rx(f->deframer->fdc, datarx, callback_state);
        freedv_data_set_cb_tx(f->deframer->fdc, datatx, callback_state);
    }
    if (FDV_MODE_ACTIVE( FREEDV_MODE_6000, f->mode)) {
        freedv_data_set_cb_rx(f->fdc, datarx, callback_state);
        freedv_data_set_cb_tx(f->fdc, datatx, callback_state);
    }
}

/*---------------------------------------------------------------------------*\

  FUNCTION....: freedv_set_data_header
  AUTHOR......: Jeroen Vreeken
  DATE CREATED: 04 March 2016

  Set the data header for the data channel.
  Header compression will be used whenever packets from this header are sent.
  The header will also be used for fill packets when a data frame is requested
  without a packet available.
\*---------------------------------------------------------------------------*/

void freedv_set_data_header(struct freedv *f, unsigned char *header)
{
    if ((FDV_MODE_ACTIVE( FREEDV_MODE_2400A, f->mode)) || (FDV_MODE_ACTIVE( FREEDV_MODE_2400B, f->mode)) || (FDV_MODE_ACTIVE( FREEDV_MODE_800XA, f->mode))){
        if (!f->deframer->fdc)
            f->deframer->fdc = freedv_data_channel_create();
        if (!f->deframer->fdc)
            return;
        
        freedv_data_set_header(f->deframer->fdc, header);
    }
    if (FDV_MODE_ACTIVE( FREEDV_MODE_6000, f->mode)){
        freedv_data_set_header(f->fdc, header);
    }
}

/*---------------------------------------------------------------------------*\

  FUNCTION....: freedv_get_modem_stats
  AUTHOR......: Jim Ahlstrom
  DATE CREATED: 28 July 2015

  Return data from the modem.  The arguments are pointers to the data items.  The
  pointers can be NULL if the data item is not wanted.

\*---------------------------------------------------------------------------*/

void freedv_get_modem_stats(struct freedv *f, int *sync, float *snr_est)
{
    if (FDV_MODE_ACTIVE( FREEDV_MODE_1600, f->mode))
        fdmdv_get_demod_stats(f->fdmdv, &f->stats);
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700C, f->mode))
        cohpsk_get_demod_stats(f->cohpsk, &f->stats);
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700D, f->mode) || FDV_MODE_ACTIVE( FREEDV_MODE_2020, f->mode)) {
        ofdm_get_demod_stats(f->ofdm, &f->stats);
    }
    if (FDV_MODE_ACTIVE( FREEDV_MODE_2400B, f->mode)) {
        fmfsk_get_demod_stats(f->fmfsk, &f->stats);
    }
    if (sync) *sync = f->stats.sync;
    if (snr_est) *snr_est = f->stats.snr_est;
}

/*---------------------------------------------------------------------------*\

  FUNCTIONS...: freedv_set_*
  AUTHOR......: Jim Ahlstrom
  DATE CREATED: 28 July 2015

  Set some parameters used by FreeDV.  It is possible to write a macro
  using ## for this, but I wasn't sure it would be 100% portable.

\*---------------------------------------------------------------------------*/

// Set integers
void freedv_set_test_frames               (struct freedv *f, int val) {f->test_frames = val;}
void freedv_set_test_frames_diversity	  (struct freedv *f, int val) {f->test_frames_diversity = val;}
void freedv_set_squelch_en                (struct freedv *f, int val) {f->squelch_en = val;}
void freedv_set_total_bit_errors          (struct freedv *f, int val) {f->total_bit_errors = val;}
void freedv_set_total_bits                (struct freedv *f, int val) {f->total_bits = val;}
void freedv_set_total_bit_errors_coded    (struct freedv *f, int val) {f->total_bit_errors_coded = val;}
void freedv_set_total_bits_coded          (struct freedv *f, int val) {f->total_bits_coded = val;}
void freedv_set_clip                      (struct freedv *f, int val) {f->clip = val;}
void freedv_set_varicode_code_num         (struct freedv *f, int val) {varicode_set_code_num(&f->varicode_dec_states, val);}
void freedv_set_ext_vco                   (struct freedv *f, int val) {f->ext_vco = val;}


/* Band Pass Filter to cleanup OFDM tx waveform, only supported by FreeDV 700D */

void freedv_set_tx_bpf(struct freedv *f, int val) {
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700D, f->mode)) {
        ofdm_set_tx_bpf(f->ofdm, val);
    }
}

/* DPSK option for OFDM modem, useful for high SNR, fast fading */

void freedv_set_dpsk(struct freedv *f, int val) {
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700D, f->mode) || FDV_MODE_ACTIVE( FREEDV_MODE_2020, f->mode)) {
        ofdm_set_dpsk(f->ofdm, val);
    }
}

void freedv_set_phase_est_bandwidth_mode(struct freedv *f, int val) {
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700D, f->mode) || FDV_MODE_ACTIVE( FREEDV_MODE_2020, f->mode)) {
        ofdm_set_phase_est_bandwidth_mode(f->ofdm, val);
    }
}

void freedv_set_eq(struct freedv *f, int val) {
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700C, f->mode) || FDV_MODE_ACTIVE( FREEDV_MODE_700D, f->mode)) {
        codec2_700c_eq(f->codec2, val);
    }
}

void freedv_set_verbose(struct freedv *f, int verbosity) {
    f->verbose = verbosity;
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700C, f->mode)) {
        cohpsk_set_verbose(f->cohpsk, f->verbose);
    }
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700D, f->mode)) {
        ofdm_set_verbose(f->ofdm, f->verbose);
    }
}

// Set floats
void freedv_set_snr_squelch_thresh        (struct freedv *f, float val) {f->snr_squelch_thresh = val;}

void freedv_set_callback_error_pattern    (struct freedv *f, freedv_calback_error_pattern cb, void *state)
{
    f->freedv_put_error_pattern = cb;
    f->error_pattern_callback_state = state;
}

void freedv_set_carrier_ampl(struct freedv *f, int c, float ampl) {
    assert(FDV_MODE_ACTIVE( FREEDV_MODE_700C, f->mode));
    cohpsk_set_carrier_ampl(f->cohpsk, c, ampl);
}

/*---------------------------------------------------------------------------*\

  FUNCTIONS...: freedv_set_alt_modem_samp_rate
  AUTHOR......: Brady O'Brien
  DATE CREATED: 25 June 2016

  Attempt to set the alternative sample rate on the modem side of the api. Only
   a few alternative sample rates are supported. Please see below.
   
   2400A - 48000, 96000
   2400B - 48000, 96000
  
\*---------------------------------------------------------------------------*/

int freedv_set_alt_modem_samp_rate(struct freedv *f, int samp_rate){
	if (FDV_MODE_ACTIVE( FREEDV_MODE_2400A, f->mode)){ 
		if(samp_rate == 24000 || samp_rate == 48000 || samp_rate == 96000){
			fsk_destroy(f->fsk);
			f->fsk = fsk_create_hbr(samp_rate,1200,10,4,1200,1200);
        
			FREE(f->tx_bits);
			/* Note: fsk expects tx/rx bits as an array of uint8_ts, not ints */
			f->tx_bits = (int*)MALLOC(f->fsk->Nbits*sizeof(uint8_t));
        
			f->n_nom_modem_samples = f->fsk->N;
			f->n_max_modem_samples = f->fsk->N + (f->fsk->Ts);
			f->n_nat_modem_samples = f->fsk->N;
			f->nin = fsk_nin(f->fsk);
			f->modem_sample_rate = samp_rate;
			return 0;
		}else
			return -1;
	}else if(FDV_MODE_ACTIVE( FREEDV_MODE_2400B, f->mode)){
		if(samp_rate == 48000 || samp_rate == 96000){
			return -1;
		}else
			return -1;
	}
	return -1;
}


/*---------------------------------------------------------------------------* \

  FUNCTIONS...: freedv_set_sync
  AUTHOR......: David Rowe
  DATE CREATED: May 2018

  Extended control of sync state machines, especially for FreeDV 700D.
  This mode is required to acquire sync up at very low SNRS.  This is
  difficult to implement, for example we may get a false sync, or the
  state machine may fall out of sync by mistake during a long fade.

  So with this API call we allow some operator assistance.

  Ensure this is called inthe same thread as freedv_rx().

\*---------------------------------------------------------------------------*/

void freedv_set_sync(struct freedv *freedv, int sync_cmd) {
    assert (freedv != NULL);

    if (FDV_MODE_ACTIVE( FREEDV_MODE_700D, freedv->mode) || FDV_MODE_ACTIVE( FREEDV_MODE_2020, freedv->mode)) {
        ofdm_set_sync(freedv->ofdm, sync_cmd);        
    }
}

struct FSK * freedv_get_fsk(struct freedv *f){
	return f->fsk;
}

/*---------------------------------------------------------------------------*\

  FUNCTIONS...: freedv_get_*
  AUTHOR......: Jim Ahlstrom
  DATE CREATED: 28 July 2015

  Get some parameters from FreeDV.

\*---------------------------------------------------------------------------*/

int freedv_get_protocol_bits              (struct freedv *f) {return f->n_protocol_bits;}
int freedv_get_mode                       (struct freedv *f) {return f->mode;}
int freedv_get_test_frames                (struct freedv *f) {return f->test_frames;}
int freedv_get_speech_sample_rate         (struct freedv *f) {return f-> speech_sample_rate;}
int freedv_get_n_speech_samples           (struct freedv *f) {return f->n_speech_samples;}
int freedv_get_modem_sample_rate          (struct freedv *f) {return f->modem_sample_rate;}
int freedv_get_modem_symbol_rate          (struct freedv *f) {return f->modem_symbol_rate;}
int freedv_get_n_max_modem_samples        (struct freedv *f) {return f->n_max_modem_samples;}
int freedv_get_n_nom_modem_samples        (struct freedv *f) {return f->n_nom_modem_samples;}
int freedv_get_total_bits                 (struct freedv *f) {return f->total_bits;}
int freedv_get_total_bit_errors           (struct freedv *f) {return f->total_bit_errors;}
int freedv_get_total_bits_coded           (struct freedv *f) {return f->total_bits_coded;}
int freedv_get_total_bit_errors_coded     (struct freedv *f) {return f->total_bit_errors_coded;}
int freedv_get_sync                       (struct freedv *f) {return f->stats.sync;}
struct CODEC2 *freedv_get_codec2	  (struct freedv *f){return  f->codec2;}
int freedv_get_n_codec_bits               (struct freedv *f){return f->n_codec_bits;}

int freedv_get_n_max_speech_samples(struct freedv *f) {
    /* When "passing through" demod samples to the speech output
       (e.g. no sync and squeclh off) f->nin bounces around with
       timing variations.  So is is possible we may return
       freedv_get_n_max_modem_samples() via the speech_output[]
       array */
    if (freedv_get_n_max_modem_samples(f) > f->n_speech_samples)
        return freedv_get_n_max_modem_samples(f);
    else
        return f->n_speech_samples;
}

int freedv_get_sync_interleaver(struct freedv *f) {
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700D, f->mode) || FDV_MODE_ACTIVE( FREEDV_MODE_2020, f->mode)) {
        return f->ofdm->sync_state_interleaver == synced;
    }

    return 0;
}

int freedv_get_sz_error_pattern(struct freedv *f) 
{
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700C, f->mode)) {
        /* if diversity disabled callback sends error pattern for upper and lower carriers */
        return f->sz_error_pattern * (2 - f->test_frames_diversity);
    }
    else {
        return f->sz_error_pattern;
    }
}

// Get modem status

void freedv_get_modem_extended_stats(struct freedv *f, struct MODEM_STATS *stats)
{
    if (FDV_MODE_ACTIVE( FREEDV_MODE_1600, f->mode))
        fdmdv_get_demod_stats(f->fdmdv, stats);

    if (FDV_MODE_ACTIVE( FREEDV_MODE_2400A, f->mode) || FDV_MODE_ACTIVE( FREEDV_MODE_800XA, f->mode)) {
        fsk_get_demod_stats(f->fsk, stats);
        float EbNodB = stats->snr_est;                       /* fsk demod actually estimates Eb/No     */
        stats->snr_est = EbNodB + 10.0*log10f(800.0/3000.0); /* so convert to SNR Rb=800, noise B=3000 */
    }

    if (FDV_MODE_ACTIVE( FREEDV_MODE_2400B, f->mode)) {
        fmfsk_get_demod_stats(f->fmfsk, stats);
    }
    
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700C, f->mode)) {
        cohpsk_get_demod_stats(f->cohpsk, stats);
    }
    
    if (FDV_MODE_ACTIVE( FREEDV_MODE_700D, f->mode) || FDV_MODE_ACTIVE( FREEDV_MODE_2020, f->mode)) {
        ofdm_get_demod_stats(f->ofdm, stats);
    }
    
}

