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
#ifndef _MODE6000_H_
#define _MODE6000_H_

#include <stdbool.h>
#include "freedv_data_channel.h"

struct m6000;

struct m6000 *m6000_create(void);
void m6000_destroy(struct m6000 *m);

int m6000_mod_data(struct m6000 *m, struct freedv_data_channel *fdc, short *samples);
int m6000_mod_codec(struct m6000 *m, struct freedv_data_channel *fdc, short *samples, unsigned char *voice);
int m6000_demod(struct m6000 *m, struct freedv_data_channel *fdc, short *samples, unsigned char *voice);

int m6000_get_modem_sample_rate(struct m6000 *m);
int m6000_get_modem_symbol_rate(struct m6000 *m);
int m6000_get_n_nom_modem_samples(struct m6000 *m);
int m6000_get_n_max_modem_samples(struct m6000 *m);
int m6000_get_codec_bytes(struct m6000 *m);
int m6000_nin(struct m6000 *m);
void m6000_get_modem_stats(struct m6000 *m, int *sync, float *snr_est);

#endif /* _MODE6000_H_ */
