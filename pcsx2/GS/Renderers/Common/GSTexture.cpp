/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2021 PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"
#include "GS/Renderers/Common/GSTexture.h"
#include "GS/Renderers/Common/GSDevice.h"
#include "GS/GSPng.h"
#include "common/Align.h"
#include "common/StringUtil.h"
#include <bitset>

GSTexture::GSTexture() = default;

bool GSTexture::Save(const std::string& fn)
{
#ifdef PCSX2_DEVBUILD
	GSPng::Format format = GSPng::RGB_A_PNG;
#else
	GSPng::Format format = GSPng::RGB_PNG;
#endif
	switch (m_format)
	{
		case Format::UNorm8:
			format = GSPng::R8I_PNG;
			break;
		case Format::Color:
			break;
		default:
			Console.Error("Format %d not saved to image", static_cast<int>(m_format));
			return false;
	}

	const GSVector4i rc(0, 0, m_size.x, m_size.y);
	std::unique_ptr<GSDownloadTexture> dl(g_gs_device->CreateDownloadTexture(m_size.x, m_size.y, m_format));
	if (!dl || (dl->CopyFromTexture(rc, this, rc, 0), dl->Flush(), !dl->Map(rc)))
	{
		Console.Error("(GSTexture) DownloadTexture() failed.");
		return false;
	}

	const int compression = GSConfig.PNGCompressionLevel;
	return GSPng::Save(format, fn, dl->GetMapPointer(), m_size.x, m_size.y, dl->GetMapPitch(), compression, g_gs_device->IsRBSwapped());
}

void GSTexture::Swap(GSTexture* tex)
{
	std::swap(m_size, tex->m_size);
	std::swap(m_mipmap_levels, tex->m_mipmap_levels);
	std::swap(m_type, tex->m_type);
	std::swap(m_format, tex->m_format);
	std::swap(m_state, tex->m_state);
	std::swap(m_needs_mipmaps_generated, tex->m_needs_mipmaps_generated);
	std::swap(m_last_frame_used, tex->m_last_frame_used);
}

u32 GSTexture::GetCompressedBytesPerBlock() const
{
	return GetCompressedBytesPerBlock(m_format);
}

u32 GSTexture::GetCompressedBytesPerBlock(Format format)
{
	static constexpr u32 bytes_per_block[] = {
		1, // Invalid
		4, // Color/RGBA8
		8, // HDRColor/RGBA16
		32, // DepthStencil
		1, // UNorm8/R8
		2, // UInt16/R16UI
		4, // UInt32/R32UI
		4, // Int32/R32I
		8, // BC1 - 16 pixels in 64 bits
		16, // BC2 - 16 pixels in 128 bits
		16, // BC3 - 16 pixels in 128 bits
		16, // BC4 - 16 pixels in 128 bits
	};

	return bytes_per_block[static_cast<u32>(format)];
}

u32 GSTexture::GetCompressedBlockSize() const
{
	return GetCompressedBlockSize(m_format);
}

u32 GSTexture::GetCompressedBlockSize(Format format)
{
	if (format >= Format::BC1 && format <= Format::BC7)
		return 4;
	else
		return 1;
}

u32 GSTexture::CalcUploadRowLengthFromPitch(u32 pitch) const
{
	return CalcUploadRowLengthFromPitch(m_format, pitch);
}

u32 GSTexture::CalcUploadRowLengthFromPitch(Format format, u32 pitch)
{
	const u32 block_size = GetCompressedBlockSize(format);
	const u32 bytes_per_block = GetCompressedBytesPerBlock(format);
	return ((pitch + (bytes_per_block - 1)) / bytes_per_block) * block_size;
}

u32 GSTexture::CalcUploadSize(u32 height, u32 pitch) const
{
	return CalcUploadSize(m_format, height, pitch);
}

u32 GSTexture::CalcUploadSize(Format format, u32 height, u32 pitch)
{
	const u32 block_size = GetCompressedBlockSize(format);
	return pitch * ((static_cast<u32>(height) + (block_size - 1)) / block_size);
}

void GSTexture::GenerateMipmapsIfNeeded()
{
	if (!m_needs_mipmaps_generated || m_mipmap_levels <= 1 || IsCompressedFormat())
		return;

	m_needs_mipmaps_generated = false;
	GenerateMipmap();
}

GSDownloadTexture::GSDownloadTexture(u32 width, u32 height, GSTexture::Format format)
	: m_width(width)
	, m_height(height)
	, m_format(format)
{
}

GSDownloadTexture::~GSDownloadTexture() = default;

u32 GSDownloadTexture::GetBufferSize(u32 width, u32 height, GSTexture::Format format, u32 pitch_align /* = 1 */)
{
	const u32 block_size = GSTexture::GetCompressedBlockSize(format);
	const u32 bytes_per_block = GSTexture::GetCompressedBytesPerBlock(format);
	const u32 bw = (width + (block_size - 1)) / block_size;
	const u32 bh = (height + (block_size - 1)) / block_size;

	pxAssert(Common::IsPow2(pitch_align));
	const u32 pitch = Common::AlignUpPow2(bw * bytes_per_block, pitch_align);
	return (pitch * bh);
}

u32 GSDownloadTexture::GetTransferPitch(u32 width, u32 pitch_align) const
{
	const u32 block_size = GSTexture::GetCompressedBlockSize(m_format);
	const u32 bytes_per_block = GSTexture::GetCompressedBytesPerBlock(m_format);
	const u32 bw = (width + (block_size - 1)) / block_size;

	pxAssert(Common::IsPow2(pitch_align));
	return Common::AlignUpPow2(bw * bytes_per_block, pitch_align);
}

void GSDownloadTexture::GetTransferSize(const GSVector4i& rc, u32* copy_offset, u32* copy_size, u32* copy_rows) const
{
	const u32 block_size = GSTexture::GetCompressedBlockSize(m_format);
	const u32 bytes_per_block = GSTexture::GetCompressedBytesPerBlock(m_format);
	const u32 tw = static_cast<u32>(rc.width());
	const u32 tb = ((tw + (block_size - 1)) / block_size);

	*copy_offset = (((static_cast<u32>(rc.y) + (block_size - 1)) / block_size) * m_current_pitch) +
				   ((static_cast<u32>(rc.x) + (block_size - 1)) / block_size) * bytes_per_block;
	*copy_size = tb * bytes_per_block;
	*copy_rows = ((static_cast<u32>(rc.height()) + (block_size - 1)) / block_size);
}

bool GSDownloadTexture::ReadTexels(const GSVector4i& rc, void* out_ptr, u32 out_stride)
{
	if (m_needs_flush)
		Flush();

	if (!Map(rc))
		return false;

	const u32 block_size = GSTexture::GetCompressedBlockSize(m_format);
	const u32 bytes_per_block = GSTexture::GetCompressedBytesPerBlock(m_format);
	const u32 tw = static_cast<u32>(rc.width());
	const u32 tb = ((tw + (block_size - 1)) / block_size);

	const u32 copy_offset = (((static_cast<u32>(rc.y) + (block_size - 1)) / block_size) * m_current_pitch) +
							((static_cast<u32>(rc.x) + (block_size - 1)) / block_size) * bytes_per_block;
	const u32 copy_size = tb * bytes_per_block;
	const u32 copy_rows = ((static_cast<u32>(rc.height()) + (block_size - 1)) / block_size);

	StringUtil::StrideMemCpy(out_ptr, out_stride, m_map_pointer + copy_offset, m_current_pitch, copy_size, copy_rows);
	return true;
}
