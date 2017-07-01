/*
*    Copyright (C) 2016-2017 Grok Image Compression Inc.
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
*    This source code incorporates work covered by the following copyright and
*    permission notice:
*
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2008, 2011-2012, Centre National d'Etudes Spatiales (CNES), FR
 * Copyright (c) 2012, CS Systemes d'Information, France
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#pragma once

#include "opj_config_private.h"
#include "IBitIO.h"
#include "IGrokStream.h"

namespace grk {

#if defined(GROK_BIG_ENDIAN)
#define grok_write_bytes		grok_write_bytes_BE
#define grok_read_bytes		grok_read_bytes_BE
#define grok_write_double	grok_write_double_BE
#define grok_read_double		grok_read_double_BE
#define grok_write_float		grok_write_float_BE
#define grok_read_float		grok_read_float_BE
#else
#define grok_write_bytes		grok_write_bytes_LE
#define grok_read_bytes		grok_read_bytes_LE
#define grok_write_double	grok_write_double_LE
#define grok_read_double		grok_read_double_LE
#define grok_write_float		grok_write_float_LE
#define grok_read_float		grok_read_float_LE
#endif


#define GROK_STREAM_STATUS_OUTPUT  0x1U
#define GROK_STREAM_STATUS_INPUT   0x2U
#define GROK_STREAM_STATUS_END     0x4U
#define GROK_STREAM_STATUS_ERROR   0x8U

/**
Byte input-output stream.
*/
struct GrokStream : public IGrokStream {

	GrokStream(size_t p_buffer_size, bool l_is_input);
	GrokStream(uint8_t* buffer, size_t p_buffer_size, bool l_is_input);
	~GrokStream();

	/**
	* User data, be it files, ... The actual data depends on the type of the stream.
	*/
	void *					m_user_data;

	/**
	* Pointer to function to free m_user_data (NULL at initialization)
	* when destroying the stream. If pointer is NULL the function is not
	* called and the m_user_data is not freed (even if non-NULL).
	*/
	opj_stream_free_user_data_fn		m_free_user_data_fn;

	/**
	* User data length.
	* Currently set to size of file for file read stream, and size of buffer for buffer read/write stream
	*/
	uint64_t 				m_user_data_length;

	/**
	* Pointer to actual read function (NULL at the initialization of the cio).
	*/
	opj_stream_read_fn		m_read_fn;

	/**
	* Pointer to actual zero copy read function (NULL at the initialization of the cio).
	*/
	opj_stream_zero_copy_read_fn		m_zero_copy_read_fn;


	/**
		* Pointer to actual write function (NULL at the initialization of the cio.
		*/
	opj_stream_write_fn		m_write_fn;

	/**
		* Pointer to actual skip function (NULL at the initialization of the cio.
		* There is no seek function to prevent from back and forth slow procedures.
		*/
	opj_stream_skip_fn		m_skip_fn;

	/**
	* Pointer to actual seek function (if available).
	*/
	opj_stream_seek_fn		m_seek_fn;

	/**
	* Flags to tell the status of the stream.
	* Used with GROK_STREAM_STATUS_* defines.
	*/
	uint32_t m_status;


	/**
	* Reads some bytes from the stream.
	* @param		p_buffer	pointer to the data buffer that will receive the data.
	* @param		p_size		number of bytes to read.
	* @param		p_event_mgr	the user event manager to be notified of special events.
	* @return		the number of bytes read, or -1 if an error occurred or if the stream is at the end.
	*/
	size_t read(uint8_t * p_buffer, size_t p_size, event_mgr_t * p_event_mgr);

	size_t read_data_zero_copy(uint8_t ** p_buffer, size_t p_size, event_mgr_t * p_event_mgr);

	bool write_byte(uint8_t p_value, event_mgr_t * p_event_mgr);

	// write methods that take endian into account
	bool write_short(uint16_t p_value, event_mgr_t * p_event_mgr);
	bool write_24(uint32_t p_value, event_mgr_t * p_event_mgr);
	bool write_int(uint32_t p_value, event_mgr_t * p_event_mgr);

	/**
	* Writes some bytes to the stream (no correction for endian!).
	* @param		p_buffer	pointer to the data buffer holds the data to be written.
	* @param		p_size		number of bytes to write.
	* @param		p_event_mgr	the user event manager to be notified of special events.
	* @return		the number of bytes written, or -1 if an error occurred.
	*/
	size_t write_bytes(const uint8_t * p_buffer,
		size_t p_size,
		event_mgr_t * p_event_mgr);

	/**
	* Writes the content of the stream buffer to the stream.
	* @param		p_event_mgr	the user event manager to be notified of special events.
	* @return		true if the data could be flushed, false else.
	*/
	bool flush(event_mgr_t * p_event_mgr);

	/**
	* Skips a number of bytes from the stream.
	* @param		p_size		the number of bytes to skip.
	* @param		p_event_mgr	the user event manager to be notified of special events.
	* @return		the number of bytes skipped, or -1 if an error occurred.
	*/
	bool skip(int64_t p_size,
		event_mgr_t * p_event_mgr);

	/**
	* Tells the byte offset on the stream (similar to ftell).
	* @return		the current position o fthe stream.
	*/
	int64_t tell(void);


	/**
	* Get the number of bytes left before the end of the stream (similar to cio_numbytesleft).
	* @return		Number of bytes left before the end of the stream.
	*/
	int64_t get_number_byte_left(void);

	/**
	* Seeks a number of bytes from the stream.
	* @param		p_size		the number of bytes to skip.
	* @param		p_event_mgr	the user event manager to be notified of special events.
	* @return		true if the stream is seekable.
	*/
	bool seek(size_t p_size,
		event_mgr_t * p_event_mgr);

	/**
	* Tells if the given stream is seekable.
	*/
	bool has_seek();

private:

	/**
	* Skips a number of bytes from the stream.
	* @param		p_size		the number of bytes to skip.
	* @param		p_event_mgr	the user event manager to be notified of special events.
	* @return		the number of bytes skipped, or -1 if an error occurred.
	*/
	bool write_skip(int64_t p_size,
		event_mgr_t * p_event_mgr);

	/**
	* Skips a number of bytes from the stream.
	* @param		p_size		the number of bytes to skip.
	* @param		p_event_mgr	the user event manager to be notified of special events.
	* @return		the number of bytes skipped, or -1 if an error occurred.
	*/
	bool read_skip(int64_t p_size,
		event_mgr_t * p_event_mgr);

	/**
	* Skips a number of bytes from the stream.
	* @param		p_size		the number of bytes to skip.
	* @param		p_event_mgr	the user event manager to be notified of special events.
	* @return		true if success, or false if an error occurred.
	*/
	bool read_seek(size_t p_size,
		event_mgr_t * p_event_mgr);

	/**
	* Skips a number of bytes from the stream.
	* @param		p_size		the number of bytes to skip.
	* @param		p_event_mgr	the user event manager to be notified of special events.
	* @return		the number of bytes skipped, or -1 if an error occurred.
	*/
	bool write_seek(size_t p_size,
		event_mgr_t * p_event_mgr);

	void write_increment(size_t p_size);
	template<typename TYPE> bool write(uint32_t p_value, uint8_t numBytes, event_mgr_t * p_event_mgr);

	void sanity_check();

	/**
	* Pointer to the current read data.
	*/
	uint8_t *					m_current_data;

	/**
	* The number of bytes read/written from the beginning of the stream
	*/
	int64_t			m_total_bytes;

	/**
	* number of bytes read in from
	*/
	size_t			m_bytes_in_buffer;



	/**
	* Actual data stored into the stream if read from. Data is read by chunk of fixed size.
	* you should never access this data directly.
	*/
	uint8_t *					m_buffer;

	//The size of the buffer.
	size_t			m_buffer_size;


	bool isBufferStream;


};


/** @name Exported functions (see also openjpeg.h) */
/*@{*/
/* ----------------------------------------------------------------------- */
/**
	* Write some bytes to the given data buffer, this function is used in Big Endian cpus.
	* @param p_buffer		pointer the data buffer to write data to.
	* @param p_value		the value to write
	* @param p_nb_bytes	the number of bytes to write
*/
void grok_write_bytes_BE(uint8_t * p_buffer, uint32_t p_value, uint32_t p_nb_bytes);

/**
	* Reads some bytes from the given data buffer, this function is used in Big Endian cpus.
	* @param p_buffer		pointer the data buffer to read data from.
	* @param p_value		pointer to the value that will store the data.
	* @param p_nb_bytes	the nb bytes to read.
	* @return				the number of bytes read or -1 if an error occurred.
	*/
void grok_read_bytes_BE(const uint8_t * p_buffer, uint32_t * p_value, uint32_t p_nb_bytes);

/**
	* Write some bytes to the given data buffer, this function is used in Little Endian cpus.
	* @param p_buffer		pointer the data buffer to write data to.
	* @param p_value		the value to write
	* @param p_nb_bytes	the number of bytes to write
	* @return				the number of bytes written or -1 if an error occurred
*/
void grok_write_bytes_LE(uint8_t * p_buffer, uint32_t p_value, uint32_t p_nb_bytes);

/**
	* Reads some bytes from the given data buffer, this function is used in Little Endian cpus.
	* @param p_buffer		pointer the data buffer to read data from.
	* @param p_value		pointer to the value that will store the data.
	* @param p_nb_bytes	the nb bytes to read.
	* @return				the number of bytes read or -1 if an error occurred.
	*/
void grok_read_bytes_LE(const uint8_t * p_buffer, uint32_t * p_value, uint32_t p_nb_bytes);


/**
	* Write some bytes to the given data buffer, this function is used in Little Endian cpus.
	* @param p_buffer		pointer the data buffer to write data to.
	* @param p_value		the value to write
	*/
void grok_write_double_LE(uint8_t * p_buffer, double p_value);

/***
	* Write some bytes to the given data buffer, this function is used in Big Endian cpus.
	* @param p_buffer		pointer the data buffer to write data to.
	* @param p_value		the value to write
	*/
void grok_write_double_BE(uint8_t * p_buffer, double p_value);

/**
	* Reads some bytes from the given data buffer, this function is used in Little Endian cpus.
	* @param p_buffer		pointer the data buffer to read data from.
	* @param p_value		pointer to the value that will store the data.
	*/
void grok_read_double_LE(const uint8_t * p_buffer, double * p_value);

/**
	* Reads some bytes from the given data buffer, this function is used in Big Endian cpus.
	* @param p_buffer		pointer the data buffer to read data from.
	* @param p_value		pointer to the value that will store the data.
	*/
void grok_read_double_BE(const uint8_t * p_buffer, double * p_value);

/**
	* Reads some bytes from the given data buffer, this function is used in Little Endian cpus.
	* @param p_buffer		pointer the data buffer to read data from.
	* @param p_value		pointer to the value that will store the data.
	*/
void grok_read_float_LE(const uint8_t * p_buffer, float * p_value);

/**
	* Reads some bytes from the given data buffer, this function is used in Big Endian cpus.
	* @param p_buffer		pointer the data buffer to read data from.
	* @param p_value		pointer to the value that will store the data.
	*/
void grok_read_float_BE(const uint8_t * p_buffer, float * p_value);

/**
	* Write some bytes to the given data buffer, this function is used in Little Endian cpus.
	* @param p_buffer		pointer the data buffer to write data to.
	* @param p_value		the value to write
	*/
void grok_write_float_LE(uint8_t * p_buffer, float p_value);

/***
	* Write some bytes to the given data buffer, this function is used in Big Endian cpus.
	* @param p_buffer		pointer the data buffer to write data to.
	* @param p_value		the value to write
	*/
void grok_write_float_BE(uint8_t * p_buffer, float p_value);



}