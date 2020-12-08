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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once
namespace grk {

/**
 @file PacketIter.h
 @brief Implementation of a packet iterator (PI)

 The functions in PI.C have for goal to realize a packet iterator that permits to get the next
 packet following the progression order and change of it. The functions in PI.C are used
 by some function in T2.C.
 */

/** @defgroup PI PI - Implementation of a packet iterator */
/*@{*/

/**
 T2 compressing mode
 */
enum J2K_T2_MODE {
	THRESH_CALC = 0, /** Function called in Rate allocation process*/
	FINAL_PASS = 1 /** Function called in Tier 2 process*/
};


/***
 * Packet iterator resolution
 */
struct grk_pi_resolution {
	uint32_t pdx, pdy;
	uint32_t pw, ph;
};

/**
 * Packet iterator component
 */
struct grk_pi_comp {
	uint32_t dx, dy;
	/** number of resolution levels */
	uint32_t numresolutions;
	grk_pi_resolution *resolutions;
};

/**
 Packet iterator
 */
struct PacketIter {
	PacketIter();
	~PacketIter();

	uint8_t* get_include(uint16_t layerIndex);
	bool update_include(void);
	void destroy_include(void);

	/** Enabling Tile part generation*/
	bool  tp_on;

	std::vector<uint8_t*> *include;

	/** layer step used to localize the packet in the include vector */
	uint64_t step_l;
	/** resolution step used to localize the packet in the include vector */
	uint64_t step_r;
	/** component step used to localize the packet in the include vector */
	uint64_t step_c;
	/** precinct step used to localize the packet in the include vector */
	uint32_t step_p;
	/** component that identify the packet */
	uint16_t compno;
	/** resolution that identify the packet */
	uint8_t resno;
	/** precinct that identify the packet */
	uint64_t precinctIndex;
	/** layer that identify the packet */
	uint16_t layno;
	/** true if the first packet */
	bool first;
	/** progression order change information */
	 grk_poc  poc;
	/** number of components in the image */
	uint16_t numcomps;
	/** Components*/
	grk_pi_comp *comps;
	/** tile coordinates*/
	uint32_t tx0, ty0, tx1, ty1;
	/** packet coordinates */
	uint32_t x, y;
	/** packet sub-sampling factors */
	uint32_t dx, dy;
};

/** @name Exported functions */
/*@{*/
/* ----------------------------------------------------------------------- */
/**
 * Creates a packet iterator for compressing.
 *
 * @param	image		the image being encoded.
 * @param	cp		the coding parameters.
 * @param	tileno	index of the tile being encoded.
 * @param	t2_mode	the type of pass for generating the packet iterator
 * @param 	include	vector of include buffers, one per layer
 *
 * @return	a list of packet iterator that points to the first packet of the tile (not true).
 */
PacketIter* pi_create_compress(const grk_image *image, CodingParams *cp,
		uint16_t tileno, J2K_T2_MODE t2_mode, std::vector<uint8_t*> *include);

/**
 * Updates the compressing parameters of the codec.
 *
 * @param	p_image		the image being encoded.
 * @param	p_cp		the coding parameters.
 * @param	tile_no	index of the tile being encoded.
 */
void pi_update_encoding_parameters(const grk_image *p_image, CodingParams *p_cp,
		uint16_t tile_no);

/**
 Modify the packet iterator for enabling tile part generation
 @param pi 		Handle to the packet iterator generated in pi_initialise_encode
 @param cp 		Coding parameters
 @param tileno 	Number that identifies the tile for which to list the packets
 @param pino   	packet iterator number
 @param first_poc_tile_part true for first POC tile part
 @param tppos 	The position of the tile part flag in the progression order
 @param t2_mode T2 mode
 */
void pi_enable_tile_part_generation(PacketIter *pi, CodingParams *cp, uint16_t tileno, uint32_t pino,
		bool first_poc_tile_part, uint32_t tppos, J2K_T2_MODE t2_mode);

/**
 Create a packet iterator for Decoder
 @param image Raw image for which the packets will be listed
 @param cp Coding parameters
 @param tileno Number that identifies the tile for which to list the packets
 @param include	vector of include buffers, one per layer
 @return a packet iterator that points to the first packet of the tile
 @see pi_destroy
 */
PacketIter* pi_create_decompress(grk_image *image, CodingParams *cp, uint16_t tileno, std::vector<uint8_t*> *include);
/**
 * Destroys a packet iterator array.
 *
 * @param	p_pi			the packet iterator array to destroy.
 */
void pi_destroy(PacketIter *p_pi);

/**
 Modify the packet iterator to point to the next packet
 @param pi Packet iterator to modify
 @return false if pi pointed to the last packet or else returns true
 */
bool pi_next(PacketIter *pi);
/* ----------------------------------------------------------------------- */
/*@}*/

/*@}*/

}
