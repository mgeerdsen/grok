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
 */
#include "grk_includes.h"

namespace grk
{
T1DecompressScheduler::T1DecompressScheduler() : success(true), decodeBlocks(nullptr) {}
T1DecompressScheduler::~T1DecompressScheduler()
{
	for(auto& t : t1Implementations)
		delete t;
}
bool T1DecompressScheduler::prepareScheduleDecompress(TileComponent* tilec,
													  TileComponentCodingParams* tccp,
													  DecompressBlocks &blocks,
													  uint8_t prec)
{
	bool wholeTileDecoding = tilec->isWholeTileDecoding();
	for(uint8_t resno = 0; resno <= tilec->highestResolutionDecompressed; ++resno)
	{
		ResDecompressBlocks resBlocks;
		auto res = tilec->tileCompResolution + resno;
		for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
		{
			auto band = res->tileBand + bandIndex;
			auto paddedBandWindow =
				tilec->getBuffer()->getBandWindowPadded(resno, band->orientation);
			for(auto precinct : band->precincts)
			{
				if(!wholeTileDecoding && !paddedBandWindow->nonEmptyIntersection(precinct))
					continue;
				for(uint64_t cblkno = 0; cblkno < precinct->getNumCblks(); ++cblkno)
				{
					auto cblkBounds = precinct->getCodeBlockBounds(cblkno);
					if(wholeTileDecoding || paddedBandWindow->nonEmptyIntersection(&cblkBounds))
					{
						auto cblk = precinct->getDecompressedBlockPtr(cblkno);
						auto block = new DecompressBlockExec();
						block->x = cblk->x0;
						block->y = cblk->y0;
						block->tilec = tilec;
						block->bandIndex = bandIndex;
						block->bandNumbps = band->numbps;
						block->bandOrientation = band->orientation;
						block->cblk = cblk;
						block->cblk_sty = tccp->cblk_sty;
						block->qmfbid = tccp->qmfbid;
						block->resno = resno;
						block->roishift = tccp->roishift;
						block->stepsize = band->stepsize;
						block->k_msbs = (uint8_t)(band->numbps - cblk->numbps);
						block->R_b = prec + gain_b[band->orientation];
						resBlocks.push_back(block);
					}
				}
			}
		}
		if (!resBlocks.empty())
			blocks.push_back(resBlocks);
	}
	return true;
}
bool T1DecompressScheduler::scheduleDecompress(TileCodingParams* tcp, uint16_t blockw,
											   uint16_t blockh,
											   DecompressBlocks &blocks)
{
	// nominal code block dimensions
	uint16_t codeblock_width = (uint16_t)(blockw ? (uint32_t)1 << blockw : 0);
	uint16_t codeblock_height = (uint16_t)(blockh ? (uint32_t)1 << blockh : 0);
	for(auto i = 0U; i < ExecSingleton::get()->num_workers(); ++i)
		t1Implementations.push_back(
			T1Factory::makeT1(false, tcp, codeblock_width, codeblock_height));

	return decompress(blocks);
}
bool T1DecompressScheduler::decompressBlock(T1Interface* impl, DecompressBlockExec* block)
{
	try
	{
		bool rc = block->open(impl);
		delete block;
		return rc;
	}
	catch(std::runtime_error& rerr)
	{
		delete block;
		GRK_ERROR(rerr.what());
		return false;
	}

	return true;
}
bool T1DecompressScheduler::decompress(DecompressBlocks &blocks)
{
	if(!blocks.size())
		return true;
	size_t num_threads = ExecSingleton::get()->num_workers();
	success = true;
	if(num_threads == 1)
	{
		for(auto& rb : blocks){
			for (auto& block : rb) {
				if(!success)
				{
					delete block;
				}
				else
				{
					auto impl = t1Implementations[(size_t)0];
					if(!decompressBlock(impl, block))
						success = false;
				}
			}
		}

		return success;
	}
	size_t maxBlocks = 0;
	for(auto& rb : blocks)
		maxBlocks += rb.size();
	decodeBlocks = new DecompressBlockExec*[maxBlocks];
	size_t ct = 0;
	for(auto& rb : blocks){
		for (auto& block : rb) {
			decodeBlocks[ct++] = block;
		}
	}
	std::atomic<int> blockCount(-1);
	tf::Taskflow taskflow;
	auto numThreads = ExecSingleton::get()->num_workers();
	auto node = new tf::Task[numThreads];
	for(uint64_t i = 0; i < numThreads; i++)
		node[i] = taskflow.placeholder();
	for(uint64_t i = 0; i < numThreads; i++)
	{
		node[i].work([this, maxBlocks, &blockCount] {
			auto threadnum = ExecSingleton::get()->this_worker_id();
			assert(threadnum >= 0);
			while(true)
			{
				uint64_t index = (uint64_t)++blockCount;
				// note: even after failure, we continue to read and delete
				// blocks until index is out of bounds. Otherwise, we leak blocks.
				if(index >= maxBlocks)
					return;
				auto block = decodeBlocks[index];
				if(!success)
				{
					delete block;
					continue;
				}
				auto impl = t1Implementations[(size_t)threadnum];
				if(!decompressBlock(impl, block))
					success = false;
			}
		});
	}
	ExecSingleton::get()->run(taskflow).wait();

	delete[] decodeBlocks;
	delete[] node;

	return success;
}

} // namespace grk
