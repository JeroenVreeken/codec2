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

int freedv_6000_datatx(struct freedv *f, short *samples);
int freedv_6000_codectx(struct freedv *f, short *samples);
int freedv_6000_codecrx(struct freedv *f, short *samples);

int freedv_6000_get_codec_bytes(struct freedv *);

#endif /* _FREEDV_6000_H_ */
