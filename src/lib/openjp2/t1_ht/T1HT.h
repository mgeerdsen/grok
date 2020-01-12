/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once
#include "T1Interface.h"

namespace grk {

struct grk_tcp;

namespace t1_ht {


class T1HT: public T1Interface {
public:
	T1HT(bool isEncoder, grk_tcp *tcp, uint16_t maxCblkW, uint16_t maxCblkH);
	virtual ~T1HT();

	void preEncode(encodeBlockInfo *block, grk_tcd_tile *tile, uint32_t &max);
	double encode(encodeBlockInfo *block, grk_tcd_tile *tile, uint32_t max,
			bool doRateControl);

	bool decode(decodeBlockInfo *block);
	void postDecode(decodeBlockInfo *block);

private:

};
}
}
