/**
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
 */
#pragma once

#include "grk_includes.h"
#include <stdexcept>
#include <algorithm>

/*
 Various coordinate systems are used to describe regions in the tile component buffer.

 1) Canvas coordinates:  JPEG 2000 global image coordinates.

 2) Tile component coordinates: canvas coordinates with sub-sampling applied

 3) Band coordinates: coordinates relative to a specified sub-band's origin

 4) Buffer coordinates: coordinate system where all resolutions are translated
	to common origin (0,0). If each code block is translated relative to the origin of the
 resolution that **it belongs to**, the blocks are then all in buffer coordinate system

 Note: the name of any method or variable returning non canvas coordinates is appended
 with "REL", to signify relative coordinates.

 */

namespace grk
{

enum eSplitOrientation
{
	SPLIT_L,
	SPLIT_H,
	SPLIT_NUM_ORIENTATIONS
};

template<class T>
constexpr T getFilterPad(bool lossless)
{
	return lossless ? 1 : 2;
}

/**
 * ResWindow
 *
 * Manage all buffers for a single DWT resolution. This class
 * stores a buffer for the resolution (in REL coordinates),
 * and also buffers for the 4 sub-bands generated by the DWT transform
 * (in Canvas coordinates).
 *
 * Note: if top level window is present, then only this window allocates
 * a memory buffer, and all other ResWindows attach themselves
 * to the top level memory buffer
 *
 */
template<typename T>
struct ResWindow
{
	ResWindow(uint8_t numresolutions,
					uint8_t resno,
					grk_buf2d<T, AllocatorAligned>* resWindowTopLevelREL,
					Resolution* tileCompAtRes,
					Resolution* tileCompAtLowerRes,
					grk_rect32 resWindow,
					grk_rect32 tileCompWindowUnreduced,
					grk_rect32 tileCompUnreduced,
					uint32_t FILTER_WIDTH)
		: allocated_(false),
		  filterWidth_(FILTER_WIDTH),
		  tileCompAtRes_(tileCompAtRes),
		  tileCompAtLowerRes_(tileCompAtLowerRes),
		  resWindowBufferTopLevelREL_(resWindowTopLevelREL),
		  resWindowBufferREL_(
			  new grk_buf2d<T, AllocatorAligned>(resWindow.width(), resWindow.height()))


	{
		for(uint32_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i)
			resWindowBufferSplitREL_[i] = nullptr;

		resWindowBoundsPadded_ = resWindow.growIPL(2 * FILTER_WIDTH).intersection(tileCompAtRes);
		// windowed decompression
		if(FILTER_WIDTH)
		{
			uint32_t numDecomps =
				(resno == 0) ? (uint32_t)(numresolutions - 1U) : (uint32_t)(numresolutions - resno);

			/*
			bandWindowsBoundsPadded_ is used for determining which precincts and code blocks overlap
			the window of interest, in each respective resolution
			*/
			for(uint8_t orient = 0; orient < ((resno) > 0 ? BAND_NUM_ORIENTATIONS : 1); orient++)
			{
				auto padded = getPaddedBandWindow(numDecomps, orient, tileCompWindowUnreduced,
											tileCompUnreduced, 2 * FILTER_WIDTH);
				bandWindowsBoundsPadded_.push_back(padded);
			}
			if(tileCompAtLowerRes_)
			{
				assert(resno > 0);
				for(uint8_t orient = 0; orient < BAND_NUM_ORIENTATIONS; orient++)
				{
					// todo: should only need padding equal to FILTER_WIDTH, not 2*FILTER_WIDTH
					auto bandWindow = getPaddedBandWindow(numDecomps, orient, tileCompWindowUnreduced,
													tileCompUnreduced, 2 * FILTER_WIDTH);
					auto bandFull = orient == BAND_ORIENT_LL ? *((grk_rect32*)tileCompAtLowerRes_)
															 : tileCompAtRes_->tileBand[orient - 1];
					auto bandWindowREL =
						bandWindow.pan(-(int64_t)bandFull.x0, -(int64_t)bandFull.y0);
					bandWindowsBuffersPaddedREL_.push_back(
						new grk_buf2d<T, AllocatorAligned>(&bandWindowREL));
				}
				auto winLow = bandWindowsBuffersPaddedREL_[BAND_ORIENT_LL];
				auto winHigh = bandWindowsBuffersPaddedREL_[BAND_ORIENT_HL];
				resWindowBufferREL_->x0 = (std::min<uint32_t>)(2 * winLow->x0, 2 * winHigh->x0 + 1);
				resWindowBufferREL_->x1 = (std::max<uint32_t>)(2 * winLow->x1, 2 * winHigh->x1 + 1);
				winLow = bandWindowsBuffersPaddedREL_[BAND_ORIENT_LL];
				winHigh = bandWindowsBuffersPaddedREL_[BAND_ORIENT_LH];
				resWindowBufferREL_->y0 = (std::min<uint32_t>)(2 * winLow->y0, 2 * winHigh->y0 + 1);
				resWindowBufferREL_->y1 = (std::max<uint32_t>)(2 * winLow->y1, 2 * winHigh->y1 + 1);

				// todo: shouldn't need to clip
				auto resBounds = grk_rect32(0, 0, tileCompAtRes_->width(), tileCompAtRes_->height());
				resWindowBufferREL_->clipIPL(&resBounds);

				// two windows formed by horizontal pass and used as input for vertical pass
				grk_rect32 splitResWindowREL[SPLIT_NUM_ORIENTATIONS];

				splitResWindowREL[SPLIT_L] = grk_rect32(
					resWindowBufferREL_->x0, bandWindowsBuffersPaddedREL_[BAND_ORIENT_LL]->y0,
					resWindowBufferREL_->x1, bandWindowsBuffersPaddedREL_[BAND_ORIENT_LL]->y1);

				resWindowBufferSplitREL_[SPLIT_L] =
					new grk_buf2d<T, AllocatorAligned>(&splitResWindowREL[SPLIT_L]);

				splitResWindowREL[SPLIT_H] = grk_rect32(
					resWindowBufferREL_->x0,
					bandWindowsBuffersPaddedREL_[BAND_ORIENT_LH]->y0 + tileCompAtLowerRes_->height(),
					resWindowBufferREL_->x1,
					bandWindowsBuffersPaddedREL_[BAND_ORIENT_LH]->y1 + tileCompAtLowerRes_->height());

				resWindowBufferSplitREL_[SPLIT_H] =
					new grk_buf2d<T, AllocatorAligned>(&splitResWindowREL[SPLIT_H]);
			}
			// compression or full tile decompression
		}
		else
		{
			// dummy LL band window
			bandWindowsBuffersPaddedREL_.push_back(new grk_buf2d<T, AllocatorAligned>(0, 0));
			assert(tileCompAtRes->numTileBandWindows == 3 || !tileCompAtLowerRes);
			if(tileCompAtLowerRes_)
			{
				for(uint32_t i = 0; i < tileCompAtRes->numTileBandWindows; ++i)
				{
					auto b = tileCompAtRes->tileBand + i;
					bandWindowsBuffersPaddedREL_.push_back(
						new grk_buf2d<T, AllocatorAligned>(b->width(), b->height()));
				}
				// note: only dimensions of split resolution window buffer matter, not actual
				// coordinates
				for(uint32_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i)
					resWindowBufferSplitREL_[i] = new grk_buf2d<T, AllocatorAligned>(
						resWindow.width(), resWindow.height() / 2);
			}
		}
	}
	~ResWindow()
	{
		delete resWindowBufferREL_;
		for(auto& b : bandWindowsBuffersPaddedREL_)
			delete b;
		for(uint32_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i)
			delete resWindowBufferSplitREL_[i];
	}
	bool alloc(bool clear)
	{
		if(allocated_)
			return true;

		// if top level window is present, then all buffers attach to this window
		if(resWindowBufferTopLevelREL_)
		{
			// ensure that top level window is allocated
			if(!resWindowBufferTopLevelREL_->alloc2d(clear))
				return false;

			// don't allocate bandWindows for windowed decompression
			if(filterWidth_)
				return true;

			// attach to top level window
			if(resWindowBufferREL_ != resWindowBufferTopLevelREL_)
				resWindowBufferREL_->attach(resWindowBufferTopLevelREL_->getBuffer(),
											resWindowBufferTopLevelREL_->stride);

			// tileCompResLower_ is null for lowest resolution
			if(tileCompAtLowerRes_)
			{
				for(uint8_t orientation = 0; orientation < bandWindowsBuffersPaddedREL_.size();
					++orientation)
				{
					switch(orientation)
					{
						case BAND_ORIENT_HL:
							bandWindowsBuffersPaddedREL_[orientation]->attach(
								resWindowBufferTopLevelREL_->getBuffer() +
									tileCompAtLowerRes_->width(),
								resWindowBufferTopLevelREL_->stride);
							break;
						case BAND_ORIENT_LH:
							bandWindowsBuffersPaddedREL_[orientation]->attach(
								resWindowBufferTopLevelREL_->getBuffer() +
									tileCompAtLowerRes_->height() *
										resWindowBufferTopLevelREL_->stride,
								resWindowBufferTopLevelREL_->stride);
							break;
						case BAND_ORIENT_HH:
							bandWindowsBuffersPaddedREL_[orientation]->attach(
								resWindowBufferTopLevelREL_->getBuffer() +
									tileCompAtLowerRes_->width() +
									tileCompAtLowerRes_->height() *
										resWindowBufferTopLevelREL_->stride,
								resWindowBufferTopLevelREL_->stride);
							break;
						default:
							break;
					}
				}
				resWindowBufferSplitREL_[SPLIT_L]->attach(resWindowBufferTopLevelREL_->getBuffer(),
														  resWindowBufferTopLevelREL_->stride);
				resWindowBufferSplitREL_[SPLIT_H]->attach(
					resWindowBufferTopLevelREL_->getBuffer() +
						tileCompAtLowerRes_->height() * resWindowBufferTopLevelREL_->stride,
					resWindowBufferTopLevelREL_->stride);
			}
		}
		else
		{
			// resolution window is always allocated
			if(!resWindowBufferREL_->alloc2d(clear))
				return false;

			// band windows are allocated if present
			for(auto& b : bandWindowsBuffersPaddedREL_)
			{
				if(!b->alloc2d(clear))
					return false;
			}
			if(tileCompAtLowerRes_)
			{
				resWindowBufferSplitREL_[SPLIT_L]->attach(resWindowBufferREL_->getBuffer(),
														  resWindowBufferREL_->stride);
				resWindowBufferSplitREL_[SPLIT_H]->attach(resWindowBufferREL_->getBuffer() +
															  tileCompAtLowerRes_->height() *
																  resWindowBufferREL_->stride,
														  resWindowBufferREL_->stride);
			}
		}
		allocated_ = true;

		return true;
	}

	/**
	 * Get band window (in tile component coordinates) for specified number
	 * of decompositions
	 *
	 * Note: if numDecomps is zero, then the band window (and there is only one)
	 * is equal to the unreduced tile component window
	 *
	 * See table F-1 in JPEG 2000 standard
	 *
	 */
	static grk_rect32 getBandWindow(uint32_t numDecomps, uint8_t orientation,
									grk_rect32 tileCompWindowUnreduced)
	{
		assert(orientation < BAND_NUM_ORIENTATIONS);
		if(numDecomps == 0)
			return tileCompWindowUnreduced;

		uint32_t tcx0 = tileCompWindowUnreduced.x0;
		uint32_t tcy0 = tileCompWindowUnreduced.y0;
		uint32_t tcx1 = tileCompWindowUnreduced.x1;
		uint32_t tcy1 = tileCompWindowUnreduced.y1;

		/* project window onto sub-band generated by `numDecomps` decompositions */
		/* See equation B-15 of the standard. */
		uint32_t bx0 = orientation & 1;
		uint32_t by0 = (uint32_t)(orientation >> 1U);

		uint32_t bx0Shift = (1U << (numDecomps - 1)) * bx0;
		uint32_t by0Shift = (1U << (numDecomps - 1)) * by0;

		return grk_rect32(
			(tcx0 <= bx0Shift) ? 0 : ceildivpow2<uint32_t>(tcx0 - bx0Shift, numDecomps),
			(tcy0 <= by0Shift) ? 0 : ceildivpow2<uint32_t>(tcy0 - by0Shift, numDecomps),
			(tcx1 <= bx0Shift) ? 0 : ceildivpow2<uint32_t>(tcx1 - bx0Shift, numDecomps),
			(tcy1 <= by0Shift) ? 0 : ceildivpow2<uint32_t>(tcy1 - by0Shift, numDecomps));
	}
	/**
	 * Get band window (in tile component coordinates) for specified number
	 * of decompositions (with padding)
	 *
	 * Note: if numDecomps is zero, then the band window (and there is only one)
	 * is equal to the unreduced tile component window (with padding)
	 */
	static grk_rect32 getPaddedBandWindow(uint32_t numDecomps,
									uint8_t orientation,
									grk_rect32 unreducedTileCompWindow,
									grk_rect32 unreducedTileComp,
									uint32_t padding)
	{
		assert(orientation < BAND_NUM_ORIENTATIONS);
		if(numDecomps == 0)
		{
			assert(orientation == 0);
			return unreducedTileCompWindow.growIPL(padding).intersection(&unreducedTileComp);
		}
		auto oneLessDecompWindow = unreducedTileCompWindow;
		auto oneLessDecompTile = unreducedTileComp;
		if(numDecomps > 1)
		{
			oneLessDecompWindow = getBandWindow(numDecomps - 1, 0, unreducedTileCompWindow);
			oneLessDecompTile = getBandWindow(numDecomps - 1, 0, unreducedTileComp);
		}

		return getBandWindow(
			1, orientation,
			oneLessDecompWindow.growIPL(2 * padding).intersection(&oneLessDecompTile));
	}
	grk_buf2d<T, AllocatorAligned>* getResWindowBufferREL(void){
		return resWindowBufferREL_;
	}
	grk_rect32* getResWindowBoundsPadded(void){
		return &resWindowBoundsPadded_;
	}
	void disableBandWindowAllocation(void){
		resWindowBufferTopLevelREL_ = resWindowBufferREL_;
	}
private:
	bool allocated_;
	uint32_t filterWidth_;

	Resolution* tileCompAtRes_; // non-null will trigger creation of band window buffers
	Resolution* tileCompAtLowerRes_; // null for lowest resolution

	grk_rect32 resWindowBoundsPadded_;
	grk_buf2d<T, AllocatorAligned>* resWindowBufferTopLevelREL_;
public:
	grk_buf2d<T, AllocatorAligned>* resWindowBufferREL_;
	grk_buf2d<T, AllocatorAligned>* resWindowBufferSplitREL_[SPLIT_NUM_ORIENTATIONS];

	std::vector<grk_rect32> bandWindowsBoundsPadded_;
	std::vector<grk_buf2d<T, AllocatorAligned>*> bandWindowsBuffersPaddedREL_;
};

}
