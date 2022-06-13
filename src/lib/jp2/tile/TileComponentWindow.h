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

#include "ResWindow.h"

namespace grk
{

template<class T>
constexpr T getFilterPad(bool lossless)
{
	return lossless ? 1 : 2;
}

template<typename T>
struct TileComponentWindow
{
	typedef grk_buf2d<T, AllocatorAligned> Buf2dAligned;
	TileComponentWindow(bool isCompressor, bool lossless, bool wholeTileDecompress,
						grk_rect32 unreducedTileComp, grk_rect32 reducedTileComp,
						grk_rect32 unreducedImageCompWindow, Resolution* resolutions_,
						uint8_t numresolutions, uint8_t reducedNumResolutions)
		: unreducedBounds_(unreducedTileComp), bounds_(reducedTileComp), compress_(isCompressor),
		  wholeTileDecompress_(wholeTileDecompress)
	{
		assert(reducedNumResolutions > 0);
		if(!compress_)
		{
			unreducedBounds_ = unreducedImageCompWindow.intersection(unreducedTileComp);
			assert(unreducedBounds_.valid());

			bounds_ = unreducedImageCompWindow.scaleDownCeilPow2(
				(uint32_t)(numresolutions - reducedNumResolutions));
			bounds_ = bounds_.intersection(reducedTileComp);
			assert(bounds_.valid());
		}
		// fill resolutions vector
		for(uint32_t resno = 0; resno < reducedNumResolutions; ++resno)
			resolution_.push_back(resolutions_ + resno);

		auto tileCompAtRes = resolutions_ + reducedNumResolutions - 1;
		auto tileCompAtLowerRes =
			reducedNumResolutions > 1 ? resolutions_ + reducedNumResolutions - 2 : nullptr;
		// create resolution buffers
		auto highestResWindow = new ResWindow<T>(
			numresolutions, (uint8_t)(reducedNumResolutions - 1U), nullptr, tileCompAtRes,
			tileCompAtLowerRes, bounds_, unreducedBounds_, unreducedTileComp,
			wholeTileDecompress ? 0 : getFilterPad<uint32_t>(lossless));
		// setting top level prevents allocation of tileCompBandWindows buffers
		if(!useBandWindows())
			highestResWindow->disableBandWindowAllocation();

		// create windows for all resolutions except highest resolution
		for(uint8_t resno = 0; resno < reducedNumResolutions - 1; ++resno)
		{
			// resolution window ==  LL band window of next highest resolution
			auto resWindow = ResWindow<T>::getBandWindow((uint8_t)(numresolutions - 1 - resno), 0,
														 unreducedBounds_);
			resWindows.push_back(new ResWindow<T>(
				numresolutions, resno,
				useBandWindows() ? nullptr : highestResWindow->getResWindowBufferREL(),
				resolutions_ + resno, resno > 0 ? resolutions_ + resno - 1 : nullptr, resWindow,
				unreducedBounds_, unreducedTileComp,
				wholeTileDecompress ? 0 : getFilterPad<uint32_t>(lossless)));
		}
		resWindows.push_back(highestResWindow);
	}
	~TileComponentWindow()
	{
		for(auto& b : resWindows)
			delete b;
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
	static grk_rect32 getBandWindow(uint8_t numDecomps, uint8_t orientation,
									grk_rect32 tileCompWindowUnreduced)
	{
		return ResWindow<T>::getBandWindow(numDecomps, orientation, tileCompWindowUnreduced);
	}

	/**
	 * Transform code block offsets from canvas coordinates
	 * to either band coordinates (relative to sub band origin),
	 * in the case of whole tile decompression,
	 *
	 * or buffer coordinates (relative to associated resolution origin),
	 * in the case of compression or region decompression
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {LL,HL,LH,HH}
	 * @param offsetx x offset of code block in canvas coordinates
	 * @param offsety y offset of code block in canvas coordinates
	 *
	 */
	void toRelativeCoordinates(uint8_t resno, eBandOrientation orientation, uint32_t& offsetx,
							   uint32_t& offsety) const
	{
		assert(resno < resolution_.size());

		auto res = resolution_[resno];
		auto band = res->tileBand + getBandIndex(resno, orientation);

		// get offset relative to band
		offsetx -= band->x0;
		offsety -= band->y0;

		if(useBufferCoordinatesForCodeblock() && resno > 0)
		{
			auto resLower = resolution_[resno - 1U];

			if(orientation & 1)
				offsetx += resLower->width();
			if(orientation & 2)
				offsety += resLower->height();
		}
	}
	template<typename F>
	void postProcess(Buf2dAligned& src, uint8_t resno, eBandOrientation bandOrientation,
					 DecompressBlockExec* block)
	{
		grk_buf2d<int32_t, AllocatorAligned> dst;
		dst = getCodeBlockDestWindowREL(resno, bandOrientation);
		dst.copy<F>(src, F(block));
	}

	/**
	 * Get padded band window buffer
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {0,1,2,3} for {LL,HL,LH,HH} band windows
	 *
	 * If resno is > 0, return LL,HL,LH or HH band window, otherwise return LL resolution window
	 *
	 */
	const Buf2dAligned* getBandWindowBufferPaddedREL(uint8_t resno,
													 eBandOrientation orientation) const
	{
		assert(resno < resolution_.size());
		assert(resno > 0 || orientation == BAND_ORIENT_LL);

		if(resno == 0 && (compress_ || wholeTileDecompress_))
			return resWindows[0]->getResWindowBufferREL();

		return resWindows[resno]->getBandWindowBufferPaddedREL(orientation);
	}
	/**
	 * Get padded band window buffer
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {0,1,2,3} for {LL,HL,LH,HH} band windows
	 *
	 * If resno is > 0, return LL,HL,LH or HH band window, otherwise return LL resolution window
	 *
	 */
	const grk_buf2d_simple<int32_t>
		getBandWindowBufferPaddedSimple(uint8_t resno, eBandOrientation orientation) const
	{
		assert(resno < resolution_.size());
		assert(resno > 0 || orientation == BAND_ORIENT_LL);

		if(resno == 0 && (compress_ || wholeTileDecompress_))
			return resWindows[0]->getResWindowBufferSimple();

		return resWindows[resno]->getBandWindowBufferPaddedSimple(orientation);
	}
	/**
	 * Get padded band window buffer
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {0,1,2,3} for {LL,HL,LH,HH} band windows
	 *
	 * If resno is > 0, return LL,HL,LH or HH band window, otherwise return LL resolution window
	 *
	 */
	const grk_buf2d_simple<float>
		getBandWindowBufferPaddedSimpleF(uint8_t resno, eBandOrientation orientation) const
	{
		assert(resno < resolution_.size());
		assert(resno > 0 || orientation == BAND_ORIENT_LL);

		if(resno == 0 && (compress_ || wholeTileDecompress_))
			return resWindows[0]->getResWindowBufferSimpleF();

		return resWindows[resno]->getBandWindowBufferPaddedSimpleF(orientation);
	}

	/**
	 * Get padded band window
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {0,1,2,3} for {LL,HL,LH,HH} band windows
	 *
	 */
	const grk_rect32* getBandWindowPadded(uint8_t resno, eBandOrientation orientation) const
	{
		return resWindows[resno]->getBandWindowPadded(orientation);
	}

	const grk_rect32* getResWindowPadded(uint8_t resno) const
	{
		return resWindows[resno]->getResWindowBoundsPadded();
	}
	/*
	 * Get intermediate split window
	 *
	 * @param orientation 0 for upper split window, and 1 for lower split window
	 */
	const Buf2dAligned* getResWindowBufferSplitREL(uint8_t resno,
												   eSplitOrientation orientation) const
	{
		assert(resno > 0 && resno < resolution_.size());

		return resWindows[resno]->getResWindowBufferSplitREL(orientation);
	}
	/*
	 * Get intermediate split window simple buffer
	 *
	 * @param orientation 0 for upper split window, and 1 for lower split window
	 */
	const grk_buf2d_simple<int32_t>
		getResWindowBufferSplitSimple(uint8_t resno, eSplitOrientation orientation) const
	{
		return getResWindowBufferSplitREL(resno, orientation)->simple();
	}

	/*
	 * Get intermediate split window simpleF buffer
	 *
	 * @param orientation 0 for upper split window, and 1 for lower split window
	 */
	const grk_buf2d_simple<float>
		getResWindowBufferSplitSimpleF(uint8_t resno, eSplitOrientation orientation) const
	{
		return getResWindowBufferSplitREL(resno, orientation)->simpleF();
	}

	/**
	 * Get resolution window
	 *
	 * @param resno resolution number
	 *
	 */
	const Buf2dAligned* getResWindowBufferREL(uint32_t resno) const
	{
		return resWindows[resno]->getResWindowBufferREL();
	}
	/**
	 * Get resolution window
	 *
	 * @param resno resolution number
	 *
	 */
	const grk_buf2d_simple<int32_t> getResWindowBufferSimple(uint32_t resno) const
	{
		return getResWindowBufferREL(resno)->simple();
	}
	/**
	 * Get resolution window
	 *
	 * @param resno resolution number
	 *
	 */
	const grk_buf2d_simple<float> getResWindowBufferSimpleF(uint32_t resno) const
	{
		return getResWindowBufferREL(resno)->simpleF();
	}

	/**
	 * Get highest resolution window
	 *
	 *
	 */
	uint32_t getResWindowBufferHighestStride(void) const
	{
		return getResWindowBufferHighestREL()->stride;
	}

	/**
	 * Get highest resolution window
	 *
	 *
	 */
	grk_buf2d_simple<int32_t> getResWindowBufferHighestSimple(void) const
	{
		return getResWindowBufferHighestREL()->simple();
	}
	/**
	 * Get highest resolution window
	 *
	 *
	 */
	grk_buf2d_simple<float> getResWindowBufferHighestSimpleF(void) const
	{
		return getResWindowBufferHighestREL()->simpleF();
	}
	bool alloc()
	{
		for(auto& b : resWindows)
		{
			if(!b->alloc(!compress_))
				return false;
		}

		return true;
	}
	/**
	 * Get bounds of tile component (canvas coordinates)
	 * decompress: reduced canvas coordinates of window
	 * compress: unreduced canvas coordinates of entire tile
	 */
	grk_rect32 bounds() const
	{
		return bounds_;
	}
	grk_rect32 unreducedBounds() const
	{
		return unreducedBounds_;
	}
	uint64_t stridedArea(void) const
	{
		auto win = getResWindowBufferHighestREL();
		return win->stride * win->height();
	}

	// set data to buf without owning it
	void attach(T* buffer, uint32_t stride)
	{
		getResWindowBufferHighestREL()->attach(buffer, stride);
	}
	// transfer data to buf, and cease owning it
	void transfer(T** buffer, uint32_t* stride)
	{
		getResWindowBufferHighestREL()->transfer(buffer, stride);
	}

  private:
	/**
	 * Get code block destination window
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {LL,HL,LH,HH}
	 *
	 */
	const Buf2dAligned* getCodeBlockDestWindowREL(uint8_t resno, eBandOrientation orientation) const
	{
		return (useBufferCoordinatesForCodeblock())
				   ? getResWindowBufferHighestREL()
				   : getBandWindowBufferPaddedREL(resno, orientation);
	}
	/**
	 * Get highest resolution window
	 *
	 *
	 */
	Buf2dAligned* getResWindowBufferHighestREL(void) const
	{
		return resWindows.back()->getResWindowBufferREL();
	}

	bool useBandWindows() const
	{
		return !wholeTileDecompress_;
	}
	bool useBufferCoordinatesForCodeblock() const
	{
		return compress_ || !wholeTileDecompress_;
	}
	uint8_t getBandIndex(uint8_t resno, eBandOrientation orientation) const
	{
		uint8_t index = 0;
		if(resno > 0)
			index = (uint8_t)orientation-1;

		return index;
	}
	/******************************************************/
	// decompress: unreduced/reduced image component window
	// compress:  unreduced/reduced tile component
	grk_rect32 unreducedBounds_;
	grk_rect32 bounds_;
	/******************************************************/

	std::vector<Resolution*> resolution_;
	// windowed bounds for windowed decompress, otherwise full bounds
	std::vector<ResWindow<T>*> resWindows;

	bool compress_;
	bool wholeTileDecompress_;
};

} // namespace grk
