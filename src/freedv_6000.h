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
#ifndef _FREEDV_6000_H_
#define _FREEDV_6000_H_

#include "freedv_api.h"

void freedv_6000_open(struct freedv *f);
void freedv_6000_close(struct freedv *f);

int freedv_6000_rawdata_tx(struct freedv *f, short mod_out[]);
int freedv_6000_data_tx(struct freedv *f, short mod_out[]);

int freedv_6000_rawdata_symtx(struct freedv *f, signed char frame[]);
int freedv_6000_data_symtx(struct freedv *f, signed char frame[]);

int freedv_6000_comprx(struct freedv *f, COMP demod_in[]);

int freedv_6000_symrx(struct freedv *f, signed char frame[]);

#endif /* _FREEDV_6000_H_ */
