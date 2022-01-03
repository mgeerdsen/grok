/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"

namespace grk
{
static void j2k_read_int16_to_float(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
	j2k_write<int16_t, float>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_read_int32_to_float(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
	j2k_write<int32_t, float>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_read_float32_to_float(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
	j2k_write<float, float>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_read_float64_to_float(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
	j2k_write<double, float>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_read_int16_to_int32(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
	j2k_write<int16_t, int32_t>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_read_int32_to_int32(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
	j2k_write<int32_t, int32_t>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_read_float32_to_int32(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
	j2k_write<float, int32_t>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_read_float64_to_int32(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
	j2k_write<double, int32_t>(p_src_data, p_dest_data, nb_elem);
}

static const j2k_mct_function j2k_mct_read_functions_to_float[] = {
	j2k_read_int16_to_float, j2k_read_int32_to_float, j2k_read_float32_to_float,
	j2k_read_float64_to_float};
static const j2k_mct_function j2k_mct_read_functions_to_int32[] = {
	j2k_read_int16_to_int32, j2k_read_int32_to_int32, j2k_read_float32_to_int32,
	j2k_read_float64_to_int32};


bool CodeStreamDecompress::parseTileHeaderMarkers(bool* canDecompress)
{
	if(m_decompressorState.getState() == DECOMPRESS_STATE_EOC)
	{
		m_curr_marker = J2K_MS_EOC;
		return true;
	}
	/* We need to encounter a SOT marker (a new tile-part header) */
	if(m_decompressorState.getState() != DECOMPRESS_STATE_TPH_SOT)
	{
		GRK_ERROR("parse_markers: no SOT marker found");
		return false;
	}
	/* Seek in code stream for SOT marker specifying desired tile index.
	 * If we don't find it, we stop when we read the EOC or run out of data */
	while(!m_decompressorState.lastTilePartWasRead && (m_curr_marker != J2K_MS_EOC))
	{
		/* read markers until SOD is detected */
		while(m_curr_marker != J2K_MS_SOD)
		{
			// end of stream with no EOC
			if(m_stream->numBytesLeft() == 0)
			{
				m_decompressorState.setState(DECOMPRESS_STATE_NO_EOC);
				break;
			}
			uint16_t marker_size;
			if(!read_short(&marker_size))
				return false;
			else if(marker_size < 2)
			{
				GRK_ERROR("Marker size %d for marker 0x%x is less than 2", marker_size,
						  m_curr_marker);
				return false;
			}
			else if(marker_size == 2)
			{
				GRK_ERROR("Zero-size marker in header.");
				return false;
			}
			// subtract tile part header and header marker size
			if(m_decompressorState.getState() & DECOMPRESS_STATE_TPH)
				m_currentTileProcessor->tilePartDataLength -= (uint32_t)(marker_size + 2);

			marker_size =
				(uint16_t)(marker_size - 2); /* Subtract the size of the marker ID already read */
			auto marker_handler = get_marker_handler(m_curr_marker);
			if(!marker_handler)
			{
				GRK_ERROR("Unknown marker encountered while seeking SOT marker");
				return false;
			}
			if(!(m_decompressorState.getState() & marker_handler->states))
			{
				GRK_ERROR("Marker 0x%x is not compliant with its expected position", m_curr_marker);
				return false;
			}
			if(!process_marker(marker_handler, marker_size))
				return false;
			/* Add the marker to the code stream index*/
			if(codeStreamInfo)
			{
				if(!TileLengthMarkers::addTileMarkerInfo(
					   m_currentTileProcessor->m_tileIndex, codeStreamInfo, marker_handler->id,
					   (uint32_t)m_stream->tell() - marker_size - grk_marker_length,
					   marker_size + grk_marker_length))
				{
					GRK_ERROR("Not enough memory to add tl marker");
					return false;
				}
			}
			if(marker_handler->id == J2K_MS_SOT)
			{
				uint64_t sot_pos = m_stream->tell() - marker_size - grk_marker_length;
				if(sot_pos > m_decompressorState.lastSotReadPosition)
					m_decompressorState.lastSotReadPosition = sot_pos;
				if(m_decompressorState.skipTileData)
				{
					if(!m_stream->skip(m_currentTileProcessor->tilePartDataLength))
					{
						GRK_ERROR("Stream too short");
						return false;
					}
					break;
				}
			}
			if(!readMarker())
				return false;
		}
		// no bytes left and no EOC marker : we're done!
		if(!m_stream->numBytesLeft() && m_decompressorState.getState() == DECOMPRESS_STATE_NO_EOC)
			break;
		/* If we didn't skip data before, we need to read the SOD marker*/
		if(!m_decompressorState.skipTileData)
		{
			if(!m_currentTileProcessor->prepareSodDecompress(this))
				return false;
			if(!m_decompressorState.lastTilePartWasRead)
			{
				if(!readMarker())
				{
					m_decompressorState.setState(DECOMPRESS_STATE_NO_EOC);
					break;
				}
			}
		}
		else
		{
			if(!readMarker())
			{
				m_decompressorState.setState(DECOMPRESS_STATE_NO_EOC);
				break;
			}
			/* Indicate we will try to read a new tile-part header*/
			m_decompressorState.skipTileData = false;
			m_decompressorState.lastTilePartWasRead = false;
			m_decompressorState.setState(DECOMPRESS_STATE_TPH_SOT);
		}
	}
	if(!m_currentTileProcessor)
	{
		GRK_ERROR("Missing SOT marker");
		return false;
	}
	// ensure lossy wavelet has quantization set
	auto tcp = get_current_decode_tcp();
	auto numComps = m_headerImage->numcomps;
	for(uint32_t k = 0; k < numComps; ++k)
	{
		auto tccp = tcp->tccps + k;
		if(tccp->qmfbid == 0 && tccp->qntsty == J2K_CCP_QNTSTY_NOQNT)
		{
			GRK_ERROR(
				"Tile-components compressed using the irreversible processing path\n"
				"must have quantization parameters specified in the QCD/QCC marker segments,\n"
				"either explicitly, or through implicit derivation from the quantization\n"
				"parameters for the LL subband, as explained in the JPEG2000 standard, ISO/IEC\n"
				"15444-1.  The present set of code-stream parameters is not legal.");
			return false;
		}
	}
	// do QCD marker quantization step size sanity check
	// see page 553 of Taubman and Marcellin for more details on this check
	if(tcp->main_qcd_qntsty != J2K_CCP_QNTSTY_SIQNT)
	{
		// 1. Check main QCD
		uint8_t maxDecompositions = 0;
		for(uint32_t k = 0; k < numComps; ++k)
		{
			auto tccp = tcp->tccps + k;
			if(tccp->numresolutions == 0)
				continue;
			// only consider number of resolutions from a component
			// whose scope is covered by main QCD;
			// ignore components that are out of scope
			// i.e. under main QCC scope, or tile QCD/QCC scope
			if(tccp->fromQCC || tccp->fromTileHeader)
				continue;
			auto decomps = (uint8_t)(tccp->numresolutions - 1);
			if(maxDecompositions < decomps)
				maxDecompositions = decomps;
		}
		if((tcp->main_qcd_numStepSizes < 3 * (uint32_t)maxDecompositions + 1))
		{
			GRK_ERROR("From Main QCD marker, "
					  "number of step sizes (%u) is less than "
					  "3* (maximum decompositions) + 1, "
					  "where maximum decompositions = %u ",
					  tcp->main_qcd_numStepSizes, maxDecompositions);
			return false;
		}
		// 2. Check Tile QCD
		TileComponentCodingParams* qcd_comp = nullptr;
		for(uint32_t k = 0; k < numComps; ++k)
		{
			auto tccp = tcp->tccps + k;
			if(tccp->fromTileHeader && !tccp->fromQCC)
			{
				qcd_comp = tccp;
				break;
			}
		}
		if(qcd_comp && (qcd_comp->qntsty != J2K_CCP_QNTSTY_SIQNT))
		{
			uint32_t maxTileDecompositions = 0;
			for(uint32_t k = 0; k < numComps; ++k)
			{
				auto tccp = tcp->tccps + k;
				if(tccp->numresolutions == 0)
					continue;
				// only consider number of resolutions from a component
				// whose scope is covered by Tile QCD;
				// ignore components that are out of scope
				// i.e. under Tile QCC scope
				if(tccp->fromQCC && tccp->fromTileHeader)
					continue;
				auto decomps = (uint8_t)(tccp->numresolutions - 1);
				if(maxTileDecompositions < decomps)
					maxTileDecompositions = decomps;
			}
			if((qcd_comp->numStepSizes < 3 * maxTileDecompositions + 1))
			{
				GRK_ERROR("From Tile QCD marker, "
						  "number of step sizes (%u) is less than"
						  " 3* (maximum tile decompositions) + 1, "
						  "where maximum tile decompositions = %u ",
						  qcd_comp->numStepSizes, maxTileDecompositions);

				return false;
			}
		}
	}
	/* Current marker is the EOC marker ?*/
	if(m_curr_marker == J2K_MS_EOC && m_decompressorState.getState() != DECOMPRESS_STATE_EOC)
		m_decompressorState.setState(DECOMPRESS_STATE_EOC);
	// if we are not ready to decompress tile part data,
	// then skip tiles with no tile data i.e. no SOD marker
	if(!m_decompressorState.lastTilePartWasRead)
	{
		tcp = m_cp.tcps + m_currentTileProcessor->m_tileIndex;
		if(!tcp->m_compressedTileData)
		{
			*canDecompress = false;
			return true;
		}
	}
	if(!merge_ppt(m_cp.tcps + m_currentTileProcessor->m_tileIndex))
	{
		GRK_ERROR("Failed to merge PPT data");
		return false;
	}
	if(!m_currentTileProcessor->init())
	{
		GRK_ERROR("Cannot decompress tile %u", m_currentTileProcessor->m_tileIndex);
		return false;
	}
	*canDecompress = true;
	m_decompressorState.orState(DECOMPRESS_STATE_DATA);

	return true;
}


/**
 * Reads a POC marker (Progression Order Change)
 *
 * @param       headerData   header data
 * @param       header_size     size of header data
 */
bool CodeStreamDecompress::read_poc(uint8_t* headerData, uint16_t header_size)
{
	assert(headerData != nullptr);
	auto image = getHeaderImage();
	uint16_t maxNumResLevels = 0;
	auto tcp = get_current_decode_tcp();
	for(uint16_t i = 0; i < image->numcomps; ++i)
	{
		if(tcp->tccps[i].numresolutions > maxNumResLevels)
			maxNumResLevels = tcp->tccps[i].numresolutions;
	}

	uint16_t numComps = image->numcomps;
	uint32_t componentRoom = (numComps <= 256) ? 1 : 2;
	uint32_t chunkSize = 5 + 2 * componentRoom;
	uint32_t currentNumProgressions = header_size / chunkSize;
	uint32_t currentRemainingProgressions = header_size % chunkSize;

	if((currentNumProgressions == 0) || (currentRemainingProgressions != 0))
	{
		GRK_ERROR("Error reading POC marker");
		return false;
	}
	uint32_t oldNumProgressions = tcp->getNumProgressions();
	currentNumProgressions += oldNumProgressions;
	if(currentNumProgressions > GRK_J2K_MAXRLVLS)
	{
		GRK_ERROR("read_poc: number of progressions %u exceeds Grok maximum number %d",
				  currentNumProgressions, GRK_J2K_MAXRLVLS);
		return false;
	}

	for(uint32_t i = oldNumProgressions; i < currentNumProgressions; ++i)
	{
		auto current_prog = tcp->progressionOrderChange + i;
		/* RSpoc_i */
		grk_read<uint8_t>(headerData, &current_prog->resS);
		++headerData;
		if(current_prog->resS >= maxNumResLevels)
		{
			GRK_ERROR("read_poc: invalid POC start resolution number %d", current_prog->resS);
			return false;
		}
		/* CSpoc_i */
		grk_read<uint16_t>(headerData, &(current_prog->compS), componentRoom);
		headerData += componentRoom;
		if(current_prog->compS > image->numcomps)
		{
			GRK_ERROR("read_poc: invalid POC start component %d", current_prog->compS);
			return false;
		}
		/* LYEpoc_i */
		grk_read<uint16_t>(headerData, &(current_prog->layE));
		/* make sure layer end is in acceptable bounds */
		current_prog->layE = std::min<uint16_t>(current_prog->layE, tcp->numlayers);
		headerData += 2;
		/* REpoc_i */
		grk_read<uint8_t>(headerData, &current_prog->resE);
		++headerData;
		if(current_prog->resE <= current_prog->resS)
		{
			GRK_ERROR("read_poc: invalid POC end resolution %d", current_prog->compS);
			return false;
		}
		/* CEpoc_i */
		grk_read<uint16_t>(headerData, &(current_prog->compE), componentRoom);
		headerData += componentRoom;
		current_prog->compE = std::min<uint16_t>(current_prog->compE, numComps);
		if(current_prog->compE <= current_prog->compS)
		{
			GRK_ERROR("read_poc: invalid POC end component %d", current_prog->compS);
			return false;
		}
		/* Ppoc_i */
		uint8_t tmp;
		grk_read<uint8_t>(headerData++, &tmp);
		if(tmp >= GRK_NUM_PROGRESSION_ORDERS)
		{
			GRK_ERROR("read_poc: unknown POC progression order %d", tmp);
			return false;
		}
		current_prog->progression = (GRK_PROG_ORDER)tmp;
	}
	tcp->numpocs = currentNumProgressions - 1;
	return true;
}


/**
 * Reads a CRG marker (Component registration)
 *
 * @param       headerData   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_crg(uint8_t* headerData, uint16_t header_size)
{
	assert(headerData != nullptr);
	uint32_t numComps = getHeaderImage()->numcomps;
	if(header_size != numComps * 4)
	{
		GRK_ERROR("Error reading CRG marker");
		return false;
	}
	for(uint32_t i = 0; i < numComps; ++i)
	{
		auto comp = getHeaderImage()->comps + i;
		// Xcrg_i
		grk_read<uint16_t>(headerData, &comp->Xcrg);
		headerData += 2;
		// Xcrg_i
		grk_read<uint16_t>(headerData, &comp->Ycrg);
		headerData += 2;
	}
	return true;
}
/**
 * Reads a PLM marker (Packet length, main header marker)
 *
 * @param       headerData   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_plm(uint8_t* headerData, uint16_t header_size)
{
	assert(headerData != nullptr);
	if(!m_cp.plm_markers)
		m_cp.plm_markers = new PacketLengthMarkers();

	return m_cp.plm_markers->readPLM(headerData, header_size);
}
/**
 * Reads a PLT marker (Packet length, tile-part header)
 *
 * @param       headerData   the data contained in the PLT box.
 * @param       header_size   the size of the data contained in the PLT marker.

 */
bool CodeStreamDecompress::read_plt(uint8_t* headerData, uint16_t header_size)
{
	assert(headerData != nullptr);
	auto tileProcessor = currentProcessor();

	return tileProcessor->packetLengthCache.createMarkers(nullptr)->readPLT(headerData,
																			header_size);
}
/**
 * Reads a PPM marker (Packed packet headers, main header)
 *
 * @param       headerData   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_ppm(uint8_t* headerData, uint16_t header_size)
{
	if(!m_cp.ppm_marker)
	{
		m_cp.ppm_marker = new PPMMarker();
	}
	return m_cp.ppm_marker->read(headerData, header_size);
}
/**
 * Merges all PPM markers read (Packed headers, main header)
 *
 * @param       p_cp      main coding parameters.

 */
bool CodeStreamDecompress::merge_ppm(CodingParams* p_cp)
{
	return p_cp->ppm_marker ? p_cp->ppm_marker->merge() : true;
}
/**
 * Reads a PPT marker (Packed packet headers, tile-part header)
 *
 * @param       headerData   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_ppt(uint8_t* headerData, uint16_t header_size)
{
	assert(headerData != nullptr);
	uint32_t Z_ppt;
	auto tileProcessor = currentProcessor();

	/* We need to have the Z_ppt element + 1 byte of Ippt at minimum */
	if(header_size < 2)
	{
		GRK_ERROR("Error reading PPT marker");
		return false;
	}

	auto cp = &(m_cp);
	if(cp->ppm_marker)
	{
		GRK_ERROR("Error reading PPT marker: packet header have been previously found in the main "
				  "header (PPM marker).");
		return false;
	}

	auto tcp = &(cp->tcps[tileProcessor->m_tileIndex]);
	tcp->ppt = true;

	/* Z_ppt */
	grk_read<uint32_t>(headerData++, &Z_ppt, 1);
	--header_size;

	/* check allocation needed */
	if(tcp->ppt_markers == nullptr)
	{ /* first PPT marker */
		uint32_t newCount = Z_ppt + 1U; /* can't overflow, Z_ppt is UINT8 */
		assert(tcp->ppt_markers_count == 0U);

		tcp->ppt_markers = (grk_ppx*)grkCalloc(newCount, sizeof(grk_ppx));
		if(tcp->ppt_markers == nullptr)
		{
			GRK_ERROR("Not enough memory to read PPT marker");
			return false;
		}
		tcp->ppt_markers_count = newCount;
	}
	else if(tcp->ppt_markers_count <= Z_ppt)
	{
		uint32_t newCount = Z_ppt + 1U; /* can't overflow, Z_ppt is UINT8 */
		auto new_ppt_markers = (grk_ppx*)grkRealloc(tcp->ppt_markers, newCount * sizeof(grk_ppx));

		if(new_ppt_markers == nullptr)
		{
			/* clean up to be done on tcp destruction */
			GRK_ERROR("Not enough memory to read PPT marker");
			return false;
		}
		tcp->ppt_markers = new_ppt_markers;
		memset(tcp->ppt_markers + tcp->ppt_markers_count, 0,
			   (newCount - tcp->ppt_markers_count) * sizeof(grk_ppx));
		tcp->ppt_markers_count = newCount;
	}

	if(tcp->ppt_markers[Z_ppt].m_data != nullptr)
	{
		/* clean up to be done on tcp destruction */
		GRK_ERROR("Zppt %u already read", Z_ppt);
		return false;
	}

	tcp->ppt_markers[Z_ppt].m_data = (uint8_t*)grkMalloc(header_size);
	if(tcp->ppt_markers[Z_ppt].m_data == nullptr)
	{
		/* clean up to be done on tcp destruction */
		GRK_ERROR("Not enough memory to read PPT marker");
		return false;
	}
	tcp->ppt_markers[Z_ppt].m_data_size = header_size;
	memcpy(tcp->ppt_markers[Z_ppt].m_data, headerData, header_size);
	return true;
}
/**
 * Merges all PPT markers read (Packed packet headers, tile-part header)
 *
 * @param       p_tcp   the tile.

 */
bool CodeStreamDecompress::merge_ppt(TileCodingParams* p_tcp)
{
	assert(p_tcp != nullptr);
	assert(p_tcp->ppt_buffer == nullptr);
	if(!p_tcp->ppt)
		return true;

	if(p_tcp->ppt_buffer != nullptr)
	{
		GRK_ERROR("multiple calls to CodeStreamDecompress::merge_ppt()");
		return false;
	}

	uint32_t ppt_data_size = 0U;
	for(uint32_t i = 0U; i < p_tcp->ppt_markers_count; ++i)
	{
		ppt_data_size += p_tcp->ppt_markers[i]
							 .m_data_size; /* can't overflow, max 256 markers of max 65536 bytes */
	}

	p_tcp->ppt_buffer = new uint8_t[ppt_data_size];
	p_tcp->ppt_len = ppt_data_size;
	ppt_data_size = 0U;
	for(uint32_t i = 0U; i < p_tcp->ppt_markers_count; ++i)
	{
		if(p_tcp->ppt_markers[i].m_data != nullptr)
		{ /* standard doesn't seem to require contiguous Zppt */
			memcpy(p_tcp->ppt_buffer + ppt_data_size, p_tcp->ppt_markers[i].m_data,
				   p_tcp->ppt_markers[i].m_data_size);
			ppt_data_size +=
				p_tcp->ppt_markers[i]
					.m_data_size; /* can't overflow, max 256 markers of max 65536 bytes */

			grkFree(p_tcp->ppt_markers[i].m_data);
			p_tcp->ppt_markers[i].m_data = nullptr;
			p_tcp->ppt_markers[i].m_data_size = 0U;
		}
	}

	p_tcp->ppt_markers_count = 0U;
	grkFree(p_tcp->ppt_markers);
	p_tcp->ppt_markers = nullptr;

	p_tcp->ppt_data = p_tcp->ppt_buffer;
	p_tcp->ppt_data_size = p_tcp->ppt_len;

	return true;
}

/**
 * Read SOT (Start of tile part) marker
 *
 * @param       headerData   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_sot(uint8_t* headerData, uint16_t header_size)
{
	SOTMarker sot;

	return sot.read(this, headerData, header_size);
}

/**
 * Reads a RGN marker (Region Of Interest)
 *
 * @param       headerData   header data
 * @param       header_size     size of header data
 */
bool CodeStreamDecompress::read_rgn(uint8_t* headerData, uint16_t header_size)
{
	uint32_t comp_no, roi_sty;
	assert(headerData != nullptr);
	auto image = getHeaderImage();
	uint32_t numComps = image->numcomps;
	uint32_t comp_room = (numComps <= 256) ? 1 : 2;

	if(header_size != 2 + comp_room)
	{
		GRK_ERROR("Error reading RGN marker");
		return false;
	}

	auto tcp = get_current_decode_tcp();

	/* Crgn */
	grk_read<uint32_t>(headerData, &comp_no, comp_room);
	headerData += comp_room;
	/* Srgn */
	grk_read<uint32_t>(headerData++, &roi_sty, 1);
	if(roi_sty != 0)
	{
		GRK_ERROR("RGN marker RS value of %u is not supported by JPEG 2000 Part 1", roi_sty);
		return false;
	}

	/* testcase 3635.pdf.asan.77.2930 */
	if(comp_no >= numComps)
	{
		GRK_ERROR("bad component number in RGN (%u is >= number of components %u)", comp_no,
				  numComps);
		return false;
	}

	/* SPrgn */
	grk_read<uint8_t>(headerData++, &(tcp->tccps[comp_no].roishift));
	if(tcp->tccps[comp_no].roishift >= 32)
	{
		GRK_ERROR("Unsupported ROI shift : %u", tcp->tccps[comp_no].roishift);
		return false;
	}

	return true;
}
/**
 * Reads a MCO marker (Multiple Component Transform Ordering)
 *
 * @param       headerData   header data.
 * @param       header_size     size of header data

 */
bool CodeStreamDecompress::read_mco(uint8_t* headerData, uint16_t header_size)
{
	uint32_t tmp, i;
	uint32_t nb_stages;
	assert(headerData != nullptr);
	auto image = getHeaderImage();
	auto tcp = get_current_decode_tcp();

	if(header_size < 1)
	{
		GRK_ERROR("Error reading MCO marker");
		return false;
	}
	/* Nmco : only one transform stage*/
	grk_read<uint32_t>(headerData, &nb_stages, 1);
	++headerData;

	if(nb_stages > 1)
	{
		GRK_WARN("Cannot take in charge multiple transformation stages.");
		return true;
	}

	if(header_size != nb_stages + 1)
	{
		GRK_WARN("Error reading MCO marker");
		return false;
	}
	for(i = 0; i < image->numcomps; ++i)
	{
		auto tccp = tcp->tccps + i;
		tccp->m_dc_level_shift = 0;
	}
	grkFree(tcp->m_mct_decoding_matrix);
	tcp->m_mct_decoding_matrix = nullptr;

	for(i = 0; i < nb_stages; ++i)
	{
		grk_read<uint32_t>(headerData, &tmp, 1);
		++headerData;

		if(!CodeStreamDecompress::add_mct(tcp, getHeaderImage(), tmp))
			return false;
	}

	return true;
}
bool CodeStreamDecompress::add_mct(TileCodingParams* p_tcp, GrkImage* p_image, uint32_t index)
{
	uint32_t i;
	assert(p_tcp != nullptr);
	auto mcc_record = p_tcp->m_mcc_records;

	for(i = 0; i < p_tcp->m_nb_mcc_records; ++i)
	{
		if(mcc_record->m_index == index)
			break;
	}

	if(i == p_tcp->m_nb_mcc_records)
	{
		/** element discarded **/
		return true;
	}

	if(mcc_record->m_nb_comps != p_image->numcomps)
	{
		/** do not support number of comps != image */
		return true;
	}
	auto deco_array = mcc_record->m_decorrelation_array;
	if(deco_array)
	{
		uint32_t data_size =
			MCT_ELEMENT_SIZE[deco_array->m_element_type] * p_image->numcomps * p_image->numcomps;
		if(deco_array->m_data_size != data_size)
			return false;

		uint32_t nb_elem = (uint32_t)p_image->numcomps * p_image->numcomps;
		uint32_t mct_size = nb_elem * (uint32_t)sizeof(float);
		p_tcp->m_mct_decoding_matrix = (float*)grkMalloc(mct_size);

		if(!p_tcp->m_mct_decoding_matrix)
			return false;

		j2k_mct_read_functions_to_float[deco_array->m_element_type](
			deco_array->m_data, p_tcp->m_mct_decoding_matrix, nb_elem);
	}

	auto offset_array = mcc_record->m_offset_array;

	if(offset_array)
	{
		uint32_t data_size = MCT_ELEMENT_SIZE[offset_array->m_element_type] * p_image->numcomps;
		if(offset_array->m_data_size != data_size)
			return false;

		uint32_t nb_elem = p_image->numcomps;
		uint32_t offset_size = nb_elem * (uint32_t)sizeof(uint32_t);
		auto offset_data = (uint32_t*)grkMalloc(offset_size);

		if(!offset_data)
			return false;

		j2k_mct_read_functions_to_int32[offset_array->m_element_type](offset_array->m_data,
																	  offset_data, nb_elem);

		auto current_offset_data = offset_data;

		for(i = 0; i < p_image->numcomps; ++i)
		{
			auto tccp = p_tcp->tccps + i;
			tccp->m_dc_level_shift = (int32_t) * (current_offset_data++);
		}
		grkFree(offset_data);
	}

	return true;
}
/**
 * Reads a CBD marker (Component bit depth definition)
 *
 * @param       headerData   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_cbd(uint8_t* headerData, uint16_t header_size)
{
	assert(headerData != nullptr);
	if(header_size < 2 || (header_size - 2) != getHeaderImage()->numcomps)
	{
		GRK_ERROR("Error reading CBD marker");
		return false;
	}
	/* Ncbd */
	uint16_t numComps;
	grk_read<uint16_t>(headerData, &numComps);
	headerData += 2;

	if(numComps != getHeaderImage()->numcomps)
	{
		GRK_ERROR("Crror reading CBD marker");
		return false;
	}

	for(uint16_t i = 0; i < getHeaderImage()->numcomps; ++i)
	{
		/* Component bit depth */
		uint8_t comp_def;
		grk_read<uint8_t>(headerData++, &comp_def);
		auto comp = getHeaderImage()->comps + i;
		comp->sgnd = ((uint32_t)(comp_def >> 7U) & 1U);
		comp->prec = (uint8_t)((comp_def & 0x7f) + 1U);
	}

	return true;
}


/**
 * Reads a TLM marker (Tile Length Marker)
 *
 * @param       headerData   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_tlm(uint8_t* headerData, uint16_t header_size)
{
	if(!m_cp.tlm_markers)
		m_cp.tlm_markers = new TileLengthMarkers();

	return m_cp.tlm_markers->read(headerData, header_size);
}
bool CodeStreamDecompress::read_SQcd_SQcc(bool fromQCC, uint32_t comp_no, uint8_t* headerData,
										  uint16_t* header_size)
{
	assert(headerData != nullptr);
	assert(comp_no < getHeaderImage()->numcomps);
	auto tcp = get_current_decode_tcp();
	auto tccp = tcp->tccps + comp_no;

	if(*header_size < 1)
	{
		GRK_ERROR("Error reading SQcd or SQcc element");
		return false;
	}
	/* Sqcx */
	uint32_t tmp = 0;
	auto current_ptr = headerData;
	grk_read<uint32_t>(current_ptr++, &tmp, 1);
	uint8_t qntsty = tmp & 0x1f;
	*header_size = (uint16_t)(*header_size - 1);
	if(qntsty > J2K_CCP_QNTSTY_SEQNT)
	{
		GRK_ERROR("Undefined quantization style %d", qntsty);
		return false;
	}

	// scoping rules
	bool ignore = false;
	bool fromTileHeader = isDecodingTilePartHeader();
	bool mainQCD = !fromQCC && !fromTileHeader;

	if(tccp->quantizationMarkerSet)
	{
		bool tileHeaderQCC = fromQCC && fromTileHeader;
		bool setMainQCD = !tccp->fromQCC && !tccp->fromTileHeader;
		bool setMainQCC = tccp->fromQCC && !tccp->fromTileHeader;
		bool setTileHeaderQCD = !tccp->fromQCC && tccp->fromTileHeader;
		bool setTileHeaderQCC = tccp->fromQCC && tccp->fromTileHeader;

		if(!fromTileHeader)
		{
			if(setMainQCC || (mainQCD && setMainQCD))
				ignore = true;
		}
		else
		{
			if(setTileHeaderQCC)
				ignore = true;
			else if(setTileHeaderQCD && !tileHeaderQCC)
				ignore = true;
		}
	}

	if(!ignore)
	{
		tccp->quantizationMarkerSet = true;
		tccp->fromQCC = fromQCC;
		tccp->fromTileHeader = fromTileHeader;
		tccp->qntsty = qntsty;
		if(mainQCD)
			tcp->main_qcd_qntsty = tccp->qntsty;
		tccp->numgbits = (uint8_t)(tmp >> 5);
		if(tccp->qntsty == J2K_CCP_QNTSTY_SIQNT)
		{
			tccp->numStepSizes = 1;
		}
		else
		{
			tccp->numStepSizes = (tccp->qntsty == J2K_CCP_QNTSTY_NOQNT)
									 ? (uint8_t)(*header_size)
									 : (uint8_t)((*header_size) / 2);
			if(tccp->numStepSizes > GRK_J2K_MAXBANDS)
			{
				GRK_WARN("While reading QCD or QCC marker segment, "
						 "number of step sizes (%u) is greater"
						 " than GRK_J2K_MAXBANDS (%u).\n"
						 "So, number of elements stored is limited to "
						 "GRK_J2K_MAXBANDS (%u) and the rest are skipped.",
						 tccp->numStepSizes, GRK_J2K_MAXBANDS, GRK_J2K_MAXBANDS);
			}
		}
		if(mainQCD)
			tcp->main_qcd_numStepSizes = tccp->numStepSizes;
	}
	if(qntsty == J2K_CCP_QNTSTY_NOQNT)
	{
		if(*header_size < tccp->numStepSizes)
		{
			GRK_ERROR("Error reading SQcd_SQcc marker");
			return false;
		}
		for(uint32_t band_no = 0; band_no < tccp->numStepSizes; band_no++)
		{
			/* SPqcx_i */
			grk_read<uint32_t>(current_ptr++, &tmp, 1);
			if(!ignore)
			{
				if(band_no < GRK_J2K_MAXBANDS)
				{
					// top 5 bits for exponent
					tccp->stepsizes[band_no].expn = (uint8_t)(tmp >> 3);
					// mantissa = 0
					tccp->stepsizes[band_no].mant = 0;
				}
			}
		}
		*header_size = (uint16_t)(*header_size - tccp->numStepSizes);
	}
	else
	{
		if(*header_size < 2 * tccp->numStepSizes)
		{
			GRK_ERROR("Error reading SQcd_SQcc marker");
			return false;
		}
		for(uint32_t band_no = 0; band_no < tccp->numStepSizes; band_no++)
		{
			/* SPqcx_i */
			grk_read<uint32_t>(current_ptr, &tmp, 2);
			current_ptr += 2;
			if(!ignore)
			{
				if(band_no < GRK_J2K_MAXBANDS)
				{
					// top 5 bits for exponent
					tccp->stepsizes[band_no].expn = (uint8_t)(tmp >> 11);
					// bottom 11 bits for mantissa
					tccp->stepsizes[band_no].mant = (uint16_t)(tmp & 0x7ff);
				}
			}
		}
		*header_size = (uint16_t)(*header_size - 2 * tccp->numStepSizes);
	}
	if(!ignore)
	{
		/* if scalar derived, then compute other stepsizes */
		if(tccp->qntsty == J2K_CCP_QNTSTY_SIQNT)
		{
			for(uint32_t band_no = 1; band_no < GRK_J2K_MAXBANDS; band_no++)
			{
				uint8_t bandDividedBy3 = (uint8_t)((band_no - 1) / 3);
				tccp->stepsizes[band_no].expn = 0;
				if(tccp->stepsizes[0].expn > bandDividedBy3)
					tccp->stepsizes[band_no].expn =
						(uint8_t)(tccp->stepsizes[0].expn - bandDividedBy3);
				tccp->stepsizes[band_no].mant = tccp->stepsizes[0].mant;
			}
		}
	}
	return true;
}
bool CodeStreamDecompress::read_SPCod_SPCoc(uint32_t compno, uint8_t* headerData,
											uint16_t* header_size)
{
	uint32_t i;
	assert(headerData != nullptr);
	assert(compno < getHeaderImage()->numcomps);

	if(compno >= getHeaderImage()->numcomps)
		return false;

	auto cp = &(m_cp);
	auto tcp = get_current_decode_tcp();
	auto tccp = tcp->tccps + compno;
	auto current_ptr = headerData;

	/* make sure room is sufficient */
	if(*header_size < SPCod_SPCoc_len)
	{
		GRK_ERROR("Error reading SPCod SPCoc element");
		return false;
	}
	/* SPcox (D) */
	// note: we actually read the number of decompositions
	grk_read<uint8_t>(current_ptr++, &tccp->numresolutions);
	if(tccp->numresolutions > GRK_J2K_MAX_DECOMP_LVLS)
	{
		GRK_ERROR("Invalid number of decomposition levels : %u. The JPEG 2000 standard\n"
				  "allows a maximum number of %u decomposition levels.",
				  tccp->numresolutions, GRK_J2K_MAX_DECOMP_LVLS);
		return false;
	}
	++tccp->numresolutions;
	if(m_cp.pcap && !tcp->isHT())
		tcp->setIsHT(true, tccp->qmfbid == 1, tccp->numgbits);

	/* If user wants to remove more resolutions than the code stream contains, return error */
	if(cp->m_coding_params.m_dec.m_reduce >= tccp->numresolutions)
	{
		GRK_ERROR("Error decoding component %u.\nThe number of resolutions "
				  " to remove (%d) must be strictly less than the number "
				  "of resolutions (%d) of this component.\n"
				  "Please decrease the reduce parameter.",
				  compno, cp->m_coding_params.m_dec.m_reduce, tccp->numresolutions);
		return false;
	}
	/* SPcoc (E) */
	grk_read<uint8_t>(current_ptr++, &tccp->cblkw);
	/* SPcoc (F) */
	grk_read<uint8_t>(current_ptr++, &tccp->cblkh);

	if(tccp->cblkw > 8 || tccp->cblkh > 8 || (tccp->cblkw + tccp->cblkh) > 8)
	{
		GRK_ERROR("Illegal code-block width/height (2^%d, 2^%d) found in COD/COC marker segment.\n"
				  "Code-block dimensions must be powers of 2, must be in the range 4-1024, and "
				  "their product must "
				  "lie in the range 16-4096.",
				  (uint32_t)tccp->cblkw + 2, (uint32_t)tccp->cblkh + 2);
		return false;
	}

	tccp->cblkw = (uint8_t)(tccp->cblkw + 2U);
	tccp->cblkh = (uint8_t)(tccp->cblkh + 2U);

	/* SPcoc (G) */
	tccp->cblk_sty = *current_ptr++;
	if((tccp->cblk_sty & GRK_CBLKSTY_HT) && tccp->cblk_sty != GRK_CBLKSTY_HT)
	{
		GRK_ERROR(
			"Unrecognized code-block style byte 0x%x found in COD/COC marker segment.\nWith bit-6 "
			"set (HT block coder), the other mode flags from the original J2K block coder must be "
			"0.",
			tccp->cblk_sty);
		return false;
	}
	uint8_t high_bits = (uint8_t)(tccp->cblk_sty >> 6U);
	if(high_bits == 2)
	{
		GRK_ERROR("Unrecognized code-block style byte 0x%x found in COD/COC marker segment. "
				  "Most significant 2 bits can be 00, 01 or 11, but not 10",
				  tccp->cblk_sty);
		return false;
	}

	/* SPcoc (H) */
	tccp->qmfbid = *current_ptr++;
	if(tccp->qmfbid > 1)
	{
		GRK_ERROR("Invalid qmfbid : %u. "
				  "Should be either 0 or 1",
				  tccp->qmfbid);
		return false;
	}
	*header_size = (uint16_t)(*header_size - SPCod_SPCoc_len);

	/* use custom precinct size ? */
	if(tccp->csty & J2K_CCP_CSTY_PRT)
	{
		if(*header_size < tccp->numresolutions)
		{
			GRK_ERROR("Error reading SPCod SPCoc element");
			return false;
		}

		for(i = 0; i < tccp->numresolutions; ++i)
		{
			uint8_t tmp;
			/* SPcoc (I_i) */
			grk_read<uint8_t>(current_ptr, &tmp);
			++current_ptr;
			/* Precinct exponent 0 is only allowed for lowest resolution level (Table A.21) */
			if((i != 0) && (((tmp & 0xf) == 0) || ((tmp >> 4) == 0)))
			{
				GRK_ERROR("Invalid precinct size");
				return false;
			}
			tccp->precinctWidthExp[i] = tmp & 0xf;
			tccp->precinctHeightExp[i] = (uint32_t)(tmp >> 4U);
		}

		*header_size = (uint16_t)(*header_size - tccp->numresolutions);
	}
	else
	{
		/* set default size for the precinct width and height */
		for(i = 0; i < tccp->numresolutions; ++i)
		{
			tccp->precinctWidthExp[i] = 15;
			tccp->precinctHeightExp[i] = 15;
		}
	}

	return true;
}
/**
 * Reads a MCC marker (Multiple Component Collection)
 *
 * @param       headerData   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_mcc(uint8_t* headerData, uint16_t header_size)
{
	uint32_t i, j;
	uint32_t tmp;
	uint32_t index;
	uint32_t nb_collections;
	uint32_t nb_comps;

	assert(headerData != nullptr);

	auto tcp = get_current_decode_tcp();

	if(header_size < 2)
	{
		GRK_ERROR("Error reading MCC marker");
		return false;
	}

	/* first marker */
	/* Zmcc */
	grk_read<uint32_t>(headerData, &tmp, 2);
	headerData += 2;
	if(tmp != 0)
	{
		GRK_WARN("Multiple data spanning not supported");
		return true;
	}
	if(header_size < 7)
	{
		GRK_ERROR("Error reading MCC marker");
		return false;
	}

	grk_read<uint32_t>(headerData, &index,
					   1); /* Imcc -> no need for other values, take the first */
	++headerData;

	auto mcc_record = tcp->m_mcc_records;

	for(i = 0; i < tcp->m_nb_mcc_records; ++i)
	{
		if(mcc_record->m_index == index)
			break;
		++mcc_record;
	}

	/** NOT FOUND */
	bool newmcc = false;
	if(i == tcp->m_nb_mcc_records)
	{
		// resize tcp->m_nb_mcc_records if necessary
		if(tcp->m_nb_mcc_records == tcp->m_nb_max_mcc_records)
		{
			grk_simple_mcc_decorrelation_data* new_mcc_records;
			tcp->m_nb_max_mcc_records += default_number_mcc_records;

			new_mcc_records = (grk_simple_mcc_decorrelation_data*)grkRealloc(
				tcp->m_mcc_records,
				tcp->m_nb_max_mcc_records * sizeof(grk_simple_mcc_decorrelation_data));
			if(!new_mcc_records)
			{
				grkFree(tcp->m_mcc_records);
				tcp->m_mcc_records = nullptr;
				tcp->m_nb_max_mcc_records = 0;
				tcp->m_nb_mcc_records = 0;
				GRK_ERROR("Not enough memory to read MCC marker");
				return false;
			}
			tcp->m_mcc_records = new_mcc_records;
			mcc_record = tcp->m_mcc_records + tcp->m_nb_mcc_records;
			memset(mcc_record, 0,
				   (tcp->m_nb_max_mcc_records - tcp->m_nb_mcc_records) *
					   sizeof(grk_simple_mcc_decorrelation_data));
		}
		// set pointer to prospective new mcc record
		mcc_record = tcp->m_mcc_records + tcp->m_nb_mcc_records;
		newmcc = true;
	}
	mcc_record->m_index = index;

	/* only one marker atm */
	/* Ymcc */
	grk_read<uint32_t>(headerData, &tmp, 2);
	headerData += 2;
	if(tmp != 0)
	{
		GRK_WARN("Multiple data spanning not supported");
		return true;
	}

	/* Qmcc -> number of collections -> 1 */
	grk_read<uint32_t>(headerData, &nb_collections, 2);
	headerData += 2;

	if(nb_collections > 1)
	{
		GRK_WARN("Multiple collections not supported");
		return true;
	}
	header_size = (uint16_t)(header_size - 7);

	for(i = 0; i < nb_collections; ++i)
	{
		if(header_size < 3)
		{
			GRK_ERROR("Error reading MCC marker");
			return false;
		}
		grk_read<uint32_t>(
			headerData++, &tmp,
			1); /* Xmcci type of component transformation -> array based decorrelation */

		if(tmp != 1)
		{
			GRK_WARN("Collections other than array decorrelations not supported");
			return true;
		}
		grk_read<uint32_t>(headerData, &nb_comps, 2);

		headerData += 2;
		header_size = (uint16_t)(header_size - 3);

		uint32_t nb_bytes_by_comp = 1 + (nb_comps >> 15);
		mcc_record->m_nb_comps = nb_comps & 0x7fff;

		if(header_size < (nb_bytes_by_comp * mcc_record->m_nb_comps + 2))
		{
			GRK_ERROR("Error reading MCC marker");
			return false;
		}

		header_size = (uint16_t)(header_size - (nb_bytes_by_comp * mcc_record->m_nb_comps + 2));

		for(j = 0; j < mcc_record->m_nb_comps; ++j)
		{
			/* Cmccij Component offset*/
			grk_read<uint32_t>(headerData, &tmp, nb_bytes_by_comp);
			headerData += nb_bytes_by_comp;

			if(tmp != j)
			{
				GRK_WARN("Collections with index shuffle are not supported");
				return true;
			}
		}

		grk_read<uint32_t>(headerData, &nb_comps, 2);
		headerData += 2;

		nb_bytes_by_comp = 1 + (nb_comps >> 15);
		nb_comps &= 0x7fff;

		if(nb_comps != mcc_record->m_nb_comps)
		{
			GRK_WARN("Collections with differing number of indices are not supported");
			return true;
		}

		if(header_size < (nb_bytes_by_comp * mcc_record->m_nb_comps + 3))
		{
			GRK_ERROR("Error reading MCC marker");
			return false;
		}

		header_size = (uint16_t)(header_size - (nb_bytes_by_comp * mcc_record->m_nb_comps + 3));

		for(j = 0; j < mcc_record->m_nb_comps; ++j)
		{
			/* Wmccij Component offset*/
			grk_read<uint32_t>(headerData, &tmp, nb_bytes_by_comp);
			headerData += nb_bytes_by_comp;

			if(tmp != j)
			{
				GRK_WARN("Collections with index shuffle not supported");
				return true;
			}
		}
		/* Wmccij Component offset*/
		grk_read<uint32_t>(headerData, &tmp, 3);
		headerData += 3;

		mcc_record->m_is_irreversible = !((tmp >> 16) & 1);
		mcc_record->m_decorrelation_array = nullptr;
		mcc_record->m_offset_array = nullptr;

		index = tmp & 0xff;
		if(index != 0)
		{
			for(j = 0; j < tcp->m_nb_mct_records; ++j)
			{
				auto mct_data = tcp->m_mct_records + j;
				if(mct_data->m_index == index)
				{
					mcc_record->m_decorrelation_array = mct_data;
					break;
				}
			}

			if(mcc_record->m_decorrelation_array == nullptr)
			{
				GRK_ERROR("Error reading MCC marker");
				return false;
			}
		}

		index = (tmp >> 8) & 0xff;
		if(index != 0)
		{
			for(j = 0; j < tcp->m_nb_mct_records; ++j)
			{
				auto mct_data = tcp->m_mct_records + j;
				if(mct_data->m_index == index)
				{
					mcc_record->m_offset_array = mct_data;
					break;
				}
			}

			if(mcc_record->m_offset_array == nullptr)
			{
				GRK_ERROR("Error reading MCC marker");
				return false;
			}
		}
	}

	if(header_size != 0)
	{
		GRK_ERROR("Error reading MCC marker");
		return false;
	}

	// only increment mcc record count if we are working on a new mcc
	// and everything succeeded
	if(newmcc)
		++tcp->m_nb_mcc_records;

	return true;
}
/**
 * Reads a MCT marker (Multiple Component Transform)
 *
 * @param       headerData   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_mct(uint8_t* headerData, uint16_t header_size)
{
	uint32_t i;
	uint32_t tmp;
	uint32_t indix;
	assert(headerData != nullptr);
	auto tcp = get_current_decode_tcp();

	if(header_size < 2)
	{
		GRK_ERROR("Error reading MCT marker");
		return false;
	}
	/* first marker */
	/* Zmct */
	grk_read<uint32_t>(headerData, &tmp, 2);
	headerData += 2;
	if(tmp != 0)
	{
		GRK_WARN("Cannot take in charge mct data within multiple MCT records");
		return true;
	}

	/* Imct -> no need for other values, take the first,
	 * type is double with decorrelation x0000 1101 0000 0000*/
	grk_read<uint32_t>(headerData, &tmp, 2); /* Imct */
	headerData += 2;

	indix = tmp & 0xff;
	auto mct_data = tcp->m_mct_records;

	for(i = 0; i < tcp->m_nb_mct_records; ++i)
	{
		if(mct_data->m_index == indix)
			break;
		++mct_data;
	}

	bool newmct = false;
	// NOT FOUND
	if(i == tcp->m_nb_mct_records)
	{
		if(tcp->m_nb_mct_records == tcp->m_nb_max_mct_records)
		{
			grk_mct_data* new_mct_records;
			tcp->m_nb_max_mct_records += default_number_mct_records;

			new_mct_records = (grk_mct_data*)grkRealloc(
				tcp->m_mct_records, tcp->m_nb_max_mct_records * sizeof(grk_mct_data));
			if(!new_mct_records)
			{
				grkFree(tcp->m_mct_records);
				tcp->m_mct_records = nullptr;
				tcp->m_nb_max_mct_records = 0;
				tcp->m_nb_mct_records = 0;
				GRK_ERROR("Not enough memory to read MCT marker");
				return false;
			}

			/* Update m_mcc_records[].m_offset_array and m_decorrelation_array
			 * to point to the new addresses */
			if(new_mct_records != tcp->m_mct_records)
			{
				for(i = 0; i < tcp->m_nb_mcc_records; ++i)
				{
					grk_simple_mcc_decorrelation_data* mcc_record = &(tcp->m_mcc_records[i]);
					if(mcc_record->m_decorrelation_array)
					{
						mcc_record->m_decorrelation_array =
							new_mct_records +
							(mcc_record->m_decorrelation_array - tcp->m_mct_records);
					}
					if(mcc_record->m_offset_array)
					{
						mcc_record->m_offset_array =
							new_mct_records + (mcc_record->m_offset_array - tcp->m_mct_records);
					}
				}
			}

			tcp->m_mct_records = new_mct_records;
			mct_data = tcp->m_mct_records + tcp->m_nb_mct_records;
			memset(mct_data, 0,
				   (tcp->m_nb_max_mct_records - tcp->m_nb_mct_records) * sizeof(grk_mct_data));
		}

		mct_data = tcp->m_mct_records + tcp->m_nb_mct_records;
		newmct = true;
	}
	if(mct_data->m_data)
	{
		grkFree(mct_data->m_data);
		mct_data->m_data = nullptr;
		mct_data->m_data_size = 0;
	}
	mct_data->m_index = indix;
	mct_data->m_array_type = (J2K_MCT_ARRAY_TYPE)((tmp >> 8) & 3);
	mct_data->m_element_type = (J2K_MCT_ELEMENT_TYPE)((tmp >> 10) & 3);
	/* Ymct */
	grk_read<uint32_t>(headerData, &tmp, 2);
	headerData += 2;
	if(tmp != 0)
	{
		GRK_WARN("Cannot take in charge multiple MCT markers");
		return true;
	}
	if(header_size <= 6)
	{
		GRK_ERROR("Error reading MCT markers");
		return false;
	}
	header_size = (uint16_t)(header_size - 6);

	mct_data->m_data = (uint8_t*)grkMalloc(header_size);
	if(!mct_data->m_data)
	{
		GRK_ERROR("Error reading MCT marker");
		return false;
	}
	memcpy(mct_data->m_data, headerData, header_size);
	mct_data->m_data_size = header_size;
	if(newmct)
		++tcp->m_nb_mct_records;

	return true;
}
bool CodeStreamDecompress::read_unk(void)
{
	uint32_t size_unk = 2;
	uint16_t unknownMarker = m_curr_marker;
	while(true)
	{
		// keep reading potential markers until we either find the next one, or
		// we reach the end of the stream
		try
		{
			if(!readMarker(true))
			{
				GRK_ERROR("Unable to read unknown marker 0x%02x.", unknownMarker);
				return false;
			}
		}
		catch(InvalidMarkerException&)
		{
			size_unk += 2;
			continue;
		}
		addMarker(unknownMarker, m_stream->tell() - 2 - size_unk, size_unk);
		auto marker_handler = get_marker_handler(m_curr_marker);
		// check if we need to process another unknown marker
		if(!marker_handler)
		{
			size_unk = 2;
			unknownMarker = m_curr_marker;
			continue;
		}
		// the next marker is known and located correctly
		break;
	}

	return true;
}

/**
 * Reads a COD marker (Coding Style defaults)
 *
 * @param       headerData   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_cod(uint8_t* headerData, uint16_t header_size)
{
	uint32_t i;
	assert(headerData != nullptr);
	auto image = getHeaderImage();
	auto cp = &(m_cp);

	/* If we are in the first tile-part header of the current tile */
	auto tcp = get_current_decode_tcp();

	/* Only one COD per tile */
	if(tcp->cod)
	{
		GRK_WARN("Multiple COD markers detected for tile part %u."
				 " The JPEG 2000 standard does not allow more than one COD marker per tile.",
				 tcp->m_tilePartIndexCounter);
	}
	tcp->cod = true;

	/* Make sure room is sufficient */
	if(header_size < cod_coc_len)
	{
		GRK_ERROR("Error reading COD marker");
		return false;
	}
	grk_read<uint8_t>(headerData++, &tcp->csty); /* Scod */
	/* Make sure we know how to decompress this */
	if((tcp->csty & ~(uint32_t)(J2K_CP_CSTY_PRT | J2K_CP_CSTY_SOP | J2K_CP_CSTY_EPH)) != 0U)
	{
		GRK_ERROR("Unknown Scod value in COD marker");
		return false;
	}
	uint8_t tmp;
	grk_read<uint8_t>(headerData++, &tmp); /* SGcod (A) */
	/* Make sure progression order is valid */
	if(tmp >= GRK_NUM_PROGRESSION_ORDERS)
	{
		GRK_ERROR("Unknown progression order %d in COD marker", tmp);
		return false;
	}
	tcp->prg = (GRK_PROG_ORDER)tmp;
	grk_read<uint16_t>(headerData, &tcp->numlayers); /* SGcod (B) */
	headerData += 2;

	if(tcp->numlayers == 0)
	{
		GRK_ERROR("Number of layers must be positive");
		return false;
	}

	/* If user didn't set a number layer to decompress take the max specify in the code stream. */
	if(cp->m_coding_params.m_dec.m_layer)
	{
		tcp->numLayersToDecompress = cp->m_coding_params.m_dec.m_layer;
	}
	else
	{
		tcp->numLayersToDecompress = tcp->numlayers;
	}

	grk_read<uint8_t>(headerData++, &tcp->mct); /* SGcod (C) */
	if(tcp->mct > 1)
	{
		GRK_ERROR("Invalid MCT value : %u. Should be either 0 or 1", tcp->mct);
		return false;
	}
	header_size = (uint16_t)(header_size - cod_coc_len);
	for(i = 0; i < image->numcomps; ++i)
	{
		tcp->tccps[i].csty = tcp->csty & J2K_CCP_CSTY_PRT;
	}

	if(!read_SPCod_SPCoc(0, headerData, &header_size))
	{
		return false;
	}

	if(header_size != 0)
	{
		GRK_ERROR("Error reading COD marker");
		return false;
	}
	/* Apply the coding style to other components of the current tile or the m_default_tcp*/
	/* loop */
	uint32_t prc_size;
	auto ref_tccp = &tcp->tccps[0];
	prc_size = ref_tccp->numresolutions * (uint32_t)sizeof(uint32_t);

	for(i = 1; i < getHeaderImage()->numcomps; ++i)
	{
		auto copied_tccp = ref_tccp + i;

		copied_tccp->numresolutions = ref_tccp->numresolutions;
		copied_tccp->cblkw = ref_tccp->cblkw;
		copied_tccp->cblkh = ref_tccp->cblkh;
		copied_tccp->cblk_sty = ref_tccp->cblk_sty;
		copied_tccp->qmfbid = ref_tccp->qmfbid;
		memcpy(copied_tccp->precinctWidthExp, ref_tccp->precinctWidthExp, prc_size);
		memcpy(copied_tccp->precinctHeightExp, ref_tccp->precinctHeightExp, prc_size);
	}

	return true;
}
/**
 * Reads a COC marker (Coding Style Component)
 *
 * @param       headerData   header data
 * @param       header_size     size of header data

 */
bool CodeStreamDecompress::read_coc(uint8_t* headerData, uint16_t header_size)
{
	uint32_t comp_room;
	uint32_t comp_no;
	assert(headerData != nullptr);
	auto tcp = get_current_decode_tcp();
	auto image = getHeaderImage();

	comp_room = image->numcomps <= 256 ? 1 : 2;

	/* make sure room is sufficient*/
	if(header_size < comp_room + 1)
	{
		GRK_ERROR("Error reading COC marker");
		return false;
	}
	header_size = (uint16_t)(header_size - (comp_room + 1));

	grk_read<uint32_t>(headerData, &comp_no, comp_room); /* Ccoc */
	headerData += comp_room;
	if(comp_no >= image->numcomps)
	{
		GRK_ERROR("Error reading COC marker : invalid component number %d", comp_no);
		return false;
	}

	tcp->tccps[comp_no].csty = *headerData++; /* Scoc */

	if(!read_SPCod_SPCoc(comp_no, headerData, &header_size))
	{
		return false;
	}

	if(header_size != 0)
	{
		GRK_ERROR("Error reading COC marker");
		return false;
	}
	return true;
}
/**
 * Reads a QCD marker (Quantization defaults)
 *
 * @param       headerData   header data
 * @param       header_size     size of header data
 */
bool CodeStreamDecompress::read_qcd(uint8_t* headerData, uint16_t header_size)
{
	assert(headerData != nullptr);
	if(!read_SQcd_SQcc(false, 0, headerData, &header_size))
		return false;
	if(header_size != 0)
	{
		GRK_ERROR("Error reading QCD marker");
		return false;
	}

	// Apply the quantization parameters to the other components
	// of the current tile or m_default_tcp
	auto tcp = get_current_decode_tcp();
	auto src = tcp->tccps;
	assert(src);
	for(uint32_t i = 1; i < getHeaderImage()->numcomps; ++i)
	{
		auto dest = src + i;
		// respect the QCD/QCC scoping rules
		bool ignore = false;
		if(dest->fromQCC)
		{
			if(!src->fromTileHeader || dest->fromTileHeader)
				ignore = true;
		}
		if(!ignore)
		{
			dest->qntsty = src->qntsty;
			dest->numgbits = src->numgbits;
			auto size = GRK_J2K_MAXBANDS * sizeof(grk_stepsize);
			memcpy(dest->stepsizes, src->stepsizes, size);
		}
	}
	return true;
}
/**
 * Reads a QCC marker (Quantization component)
 *
 * @param       headerData   header data
 * @param       header_size     size of header data
 */
bool CodeStreamDecompress::read_qcc(uint8_t* headerData, uint16_t header_size)
{
	assert(headerData != nullptr);
	uint32_t comp_no;
	uint16_t num_comp = getHeaderImage()->numcomps;
	if(num_comp <= 256)
	{
		if(header_size < 1)
		{
			GRK_ERROR("Error reading QCC marker");
			return false;
		}
		grk_read<uint32_t>(headerData++, &comp_no, 1);
		--header_size;
	}
	else
	{
		if(header_size < 2)
		{
			GRK_ERROR("Error reading QCC marker");
			return false;
		}
		grk_read<uint32_t>(headerData, &comp_no, 2);
		headerData += 2;
		header_size = (uint16_t)(header_size - 2);
	}

	if(comp_no >= getHeaderImage()->numcomps)
	{
		GRK_ERROR("QCC component: component number: %u must be less than"
				  " total number of components: %u",
				  comp_no, getHeaderImage()->numcomps);
		return false;
	}

	if(!read_SQcd_SQcc(true, comp_no, headerData, &header_size))
	{
		return false;
	}

	if(header_size != 0)
	{
		GRK_ERROR("Error reading QCC marker");
		return false;
	}

	return true;
}
/**
 * Reads a SOC marker (Start of Codestream)
 */
bool CodeStreamDecompress::read_soc()
{
	uint8_t data[2];
	uint16_t marker;
	if(m_stream->read(data, 2) != 2)
		return false;

	grk_read<uint16_t>(data, &marker);
	if(marker != J2K_MS_SOC)
		return false;

	/* Next marker should be a SIZ marker in the main header */
	m_decompressorState.setState(DECOMPRESS_STATE_MH_SIZ);

	if(codeStreamInfo)
	{
		codeStreamInfo->setMainHeaderStart(m_stream->tell() - 2);
		addMarker(J2K_MS_SOC, codeStreamInfo->getMainHeaderStart(), 2);
	}
	return true;
}
/**
 * Reads a CAP marker
 *
 * @param       headerData   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_cap(uint8_t* headerData, uint16_t header_size)
{
	CodingParams* cp = &(m_cp);
	if(header_size < sizeof(cp->pcap))
	{
		GRK_ERROR("Error with SIZ marker size");
		return false;
	}

	uint32_t tmp;
	grk_read<uint32_t>(headerData, &tmp); /* Pcap */
	if(tmp & 0xFFFDFFFF)
	{
		GRK_ERROR("Pcap in CAP marker has unsupported options.");
		return false;
	}
	if((tmp & 0x00020000) == 0)
	{
		GRK_ERROR("Pcap in CAP marker should have its 15th MSB set. ");
		return false;
	}
	headerData += sizeof(uint32_t);
	cp->pcap = tmp;
	uint32_t count = grk_population_count(cp->pcap);
	uint32_t expected_size = (uint32_t)sizeof(cp->pcap) + 2U * count;
	if(header_size != expected_size)
	{
		GRK_ERROR("CAP marker size %d != expected size %d", header_size, expected_size);
		return false;
	}
	for(uint32_t i = 0; i < count; ++i)
	{
		grk_read<uint16_t>(headerData, cp->ccap + i);
	}

	return true;
}
/**
 * Reads a SIZ marker (image and tile size)
 *
 * @param       headerData   header data
 * @param       header_size     size of header data
 */
bool CodeStreamDecompress::read_siz(uint8_t* headerData, uint16_t header_size)
{
	SIZMarker siz;

	bool rc =  siz.read(this, headerData, header_size);
	if (rc) {
		uint16_t numTilesToDecompress = (uint16_t)(m_cp.t_grid_height * m_cp.t_grid_width);
		m_multiTile = numTilesToDecompress > 1;
	}

	return rc;
}
/**
 * Reads a COM marker (comments)
 *
 * @param       headerData   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_com(uint8_t* headerData, uint16_t header_size)
{
	assert(headerData != nullptr);
	assert(header_size != 0);
	if(header_size < 2)
	{
		GRK_ERROR("CodeStreamDecompress::read_com: Corrupt COM segment ");
		return false;
	}
	else if(header_size == 2)
	{
		GRK_WARN("CodeStreamDecompress::read_com: Empty COM segment. Ignoring ");
		return true;
	}
	if(m_cp.num_comments == GRK_NUM_COMMENTS_SUPPORTED)
	{
		GRK_WARN("CodeStreamDecompress::read_com: Only %u comments are supported. Ignoring",
				 GRK_NUM_COMMENTS_SUPPORTED);
		return true;
	}

	uint16_t commentType;
	grk_read<uint16_t>(headerData, &commentType);
	auto numComments = m_cp.num_comments;
	m_cp.isBinaryComment[numComments] = (commentType == 0);
	if(commentType > 1)
	{
		GRK_WARN("CodeStreamDecompress::read_com: Unrecognized comment type 0x%x. Assuming IS "
				 "8859-15:1999 (Latin) values",
				 commentType);
	}

	headerData += 2;
	uint16_t commentSize = (uint16_t)(header_size - 2);
	size_t commentSizeToAlloc = commentSize;
	if(!m_cp.isBinaryComment[numComments])
		commentSizeToAlloc++;
	m_cp.comment[numComments] = (char*)new uint8_t[commentSizeToAlloc];
	if(!m_cp.comment[numComments])
	{
		GRK_ERROR(
			"CodeStreamDecompress::read_com: Out of memory when allocating memory for comment ");
		return false;
	}
	memcpy(m_cp.comment[numComments], headerData, commentSize);
	m_cp.comment_len[numComments] = commentSize;

	// make null-terminated string
	if(!m_cp.isBinaryComment[numComments])
		m_cp.comment[numComments][commentSize] = 0;
	m_cp.num_comments++;
	return true;
}


} // namespace grk
