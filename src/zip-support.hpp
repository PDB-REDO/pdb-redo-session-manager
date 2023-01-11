/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * 
 * Copyright (c) 2023 NKI/AVL, Netherlands Cancer Institute
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <memory>
#include <sstream>
#include <filesystem>

#include <gxrio.hpp>

// libarchive
#include <archive.h>
#include <archive_entry.h>

class ZipWriter
{
  public:
	ZipWriter()
		: m_s(new std::stringstream)
	{
		m_a = archive_write_new();
		archive_write_set_format_zip(m_a);
		archive_write_open(m_a, this, &open_cb, &write_cb, &close_cb);
	}

	~ZipWriter()
	{
		archive_write_free(m_a);
	}

	void add(std::filesystem::path file, std::filesystem::path name)
	{
		gxrio::ifstream in(file);

		bool compressed = file.extension() == ".gz";
		assert(compressed == (name.extension() == ".gz"));

		std::vector<char> data;

		if (compressed)
			name.replace_extension();

		for (;;)
		{
			char buffer[10240];
			auto n = in.rdbuf()->sgetn(buffer, sizeof(buffer));

			if (n == 0)
				break;

			data.insert(data.end(), buffer, buffer + n);
		}

		auto entry = archive_entry_new();
		archive_entry_set_pathname(entry, name.c_str());
		archive_entry_set_filetype(entry, AE_IFREG);
		archive_entry_set_perm(entry, 0644);
		archive_entry_set_size(entry, data.size());
		archive_write_header(m_a, entry);

		archive_write_data(m_a, data.data(), data.size());

		archive_entry_free(entry);
	}

	std::istream *finish()
	{
		archive_write_close(m_a);

		return m_s.release();
	}

  private:
	static int open_cb(struct archive *a, void *self)
	{
		return ARCHIVE_OK;
	}

	static la_ssize_t write_cb(struct archive *a, void *self, const void *buffer, size_t length)
	{
		return static_cast<ZipWriter *>(self)->write(static_cast<const char *>(buffer), length);
	}

	static int close_cb(struct archive *a, void *self)
	{
		return ARCHIVE_OK;
	}

	la_ssize_t write(const char *buffer, size_t length)
	{
		m_s->write(buffer, length);
		return length;
	}

	struct archive *m_a;
	std::unique_ptr<std::stringstream> m_s;
};

// --------------------------------------------------------------------
