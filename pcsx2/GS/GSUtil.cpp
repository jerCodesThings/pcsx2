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
#include "GS.h"
#include "GSExtra.h"
#include "GSUtil.h"
#include "MultiISA.h"
#include "common/StringUtil.h"

#ifdef _WIN32
#include "common/RedtapeWindows.h"
#include <d3dcommon.h>
#include <dxgi.h>
#include <VersionHelpers.h>
#include "Renderers/DX11/D3D.h"
#include <wil/com.h>
#endif

static class GSUtilMaps
{
public:
	u8 PrimClassField[8];
	u8 VertexCountField[8];
	u8 ClassVertexCountField[4];
	u32 CompatibleBitsField[64][2];
	u32 SharedBitsField[64][2];
	u32 SwizzleField[64][2];

	// Defer init to avoid AVX2 illegal instructions
	void Init()
	{
		PrimClassField[GS_POINTLIST] = GS_POINT_CLASS;
		PrimClassField[GS_LINELIST] = GS_LINE_CLASS;
		PrimClassField[GS_LINESTRIP] = GS_LINE_CLASS;
		PrimClassField[GS_TRIANGLELIST] = GS_TRIANGLE_CLASS;
		PrimClassField[GS_TRIANGLESTRIP] = GS_TRIANGLE_CLASS;
		PrimClassField[GS_TRIANGLEFAN] = GS_TRIANGLE_CLASS;
		PrimClassField[GS_SPRITE] = GS_SPRITE_CLASS;
		PrimClassField[GS_INVALID] = GS_INVALID_CLASS;

		VertexCountField[GS_POINTLIST] = 1;
		VertexCountField[GS_LINELIST] = 2;
		VertexCountField[GS_LINESTRIP] = 2;
		VertexCountField[GS_TRIANGLELIST] = 3;
		VertexCountField[GS_TRIANGLESTRIP] = 3;
		VertexCountField[GS_TRIANGLEFAN] = 3;
		VertexCountField[GS_SPRITE] = 2;
		VertexCountField[GS_INVALID] = 1;

		ClassVertexCountField[GS_POINT_CLASS] = 1;
		ClassVertexCountField[GS_LINE_CLASS] = 2;
		ClassVertexCountField[GS_TRIANGLE_CLASS] = 3;
		ClassVertexCountField[GS_SPRITE_CLASS] = 2;

		memset(CompatibleBitsField, 0, sizeof(CompatibleBitsField));

		for (int i = 0; i < 64; i++)
		{
			CompatibleBitsField[i][i >> 5] |= 1U << (i & 0x1f);
		}

		CompatibleBitsField[PSM_PSMCT32][PSM_PSMCT24 >> 5] |= 1 << (PSM_PSMCT24 & 0x1f);
		CompatibleBitsField[PSM_PSMCT24][PSM_PSMCT32 >> 5] |= 1 << (PSM_PSMCT32 & 0x1f);
		CompatibleBitsField[PSM_PSMCT16][PSM_PSMCT16S >> 5] |= 1 << (PSM_PSMCT16S & 0x1f);
		CompatibleBitsField[PSM_PSMCT16S][PSM_PSMCT16 >> 5] |= 1 << (PSM_PSMCT16 & 0x1f);
		CompatibleBitsField[PSM_PSMZ32][PSM_PSMZ24 >> 5] |= 1 << (PSM_PSMZ24 & 0x1f);
		CompatibleBitsField[PSM_PSMZ24][PSM_PSMZ32 >> 5] |= 1 << (PSM_PSMZ32 & 0x1f);
		CompatibleBitsField[PSM_PSMZ16][PSM_PSMZ16S >> 5] |= 1 << (PSM_PSMZ16S & 0x1f);
		CompatibleBitsField[PSM_PSMZ16S][PSM_PSMZ16 >> 5] |= 1 << (PSM_PSMZ16 & 0x1f);

		memset(SwizzleField, 0, sizeof(SwizzleField));

		for (int i = 0; i < 64; i++)
		{
			SwizzleField[i][i >> 5] |= 1U << (i & 0x1f);
		}

		SwizzleField[PSM_PSMCT32][PSM_PSMCT24 >> 5] |= 1 << (PSM_PSMCT24 & 0x1f);
		SwizzleField[PSM_PSMCT24][PSM_PSMCT32 >> 5] |= 1 << (PSM_PSMCT32 & 0x1f);
		SwizzleField[PSM_PSMT8H][PSM_PSMCT32 >> 5] |= 1 << (PSM_PSMCT32 & 0x1f);
		SwizzleField[PSM_PSMCT32][PSM_PSMT8H >> 5] |= 1 << (PSM_PSMT8H & 0x1f);
		SwizzleField[PSM_PSMT4HL][PSM_PSMCT32 >> 5] |= 1 << (PSM_PSMCT32 & 0x1f);
		SwizzleField[PSM_PSMCT32][PSM_PSMT4HL >> 5] |= 1 << (PSM_PSMT4HL & 0x1f);
		SwizzleField[PSM_PSMT4HH][PSM_PSMCT32 >> 5] |= 1 << (PSM_PSMCT32 & 0x1f);
		SwizzleField[PSM_PSMCT32][PSM_PSMT4HH >> 5] |= 1 << (PSM_PSMT4HH & 0x1f);
		SwizzleField[PSM_PSMZ32][PSM_PSMZ24 >> 5] |= 1 << (PSM_PSMZ24 & 0x1f);
		SwizzleField[PSM_PSMZ24][PSM_PSMZ32 >> 5] |= 1 << (PSM_PSMZ32 & 0x1f);

		memset(SharedBitsField, 0, sizeof(SharedBitsField));

		SharedBitsField[PSM_PSMCT24][PSM_PSMT8H >> 5] |= 1 << (PSM_PSMT8H & 0x1f);
		SharedBitsField[PSM_PSMCT24][PSM_PSMT4HL >> 5] |= 1 << (PSM_PSMT4HL & 0x1f);
		SharedBitsField[PSM_PSMCT24][PSM_PSMT4HH >> 5] |= 1 << (PSM_PSMT4HH & 0x1f);
		SharedBitsField[PSM_PSMZ24][PSM_PSMT8H >> 5] |= 1 << (PSM_PSMT8H & 0x1f);
		SharedBitsField[PSM_PSMZ24][PSM_PSMT4HL >> 5] |= 1 << (PSM_PSMT4HL & 0x1f);
		SharedBitsField[PSM_PSMZ24][PSM_PSMT4HH >> 5] |= 1 << (PSM_PSMT4HH & 0x1f);
		SharedBitsField[PSM_PSMT8H][PSM_PSMCT24 >> 5] |= 1 << (PSM_PSMCT24 & 0x1f);
		SharedBitsField[PSM_PSMT8H][PSM_PSMZ24 >> 5] |= 1 << (PSM_PSMZ24 & 0x1f);
		SharedBitsField[PSM_PSMT4HL][PSM_PSMCT24 >> 5] |= 1 << (PSM_PSMCT24 & 0x1f);
		SharedBitsField[PSM_PSMT4HL][PSM_PSMZ24 >> 5] |= 1 << (PSM_PSMZ24 & 0x1f);
		SharedBitsField[PSM_PSMT4HL][PSM_PSMT4HH >> 5] |= 1 << (PSM_PSMT4HH & 0x1f);
		SharedBitsField[PSM_PSMT4HH][PSM_PSMCT24 >> 5] |= 1 << (PSM_PSMCT24 & 0x1f);
		SharedBitsField[PSM_PSMT4HH][PSM_PSMZ24 >> 5] |= 1 << (PSM_PSMZ24 & 0x1f);
		SharedBitsField[PSM_PSMT4HH][PSM_PSMT4HL >> 5] |= 1 << (PSM_PSMT4HL & 0x1f);
	}

} s_maps;

void GSUtil::Init()
{
	s_maps.Init();
}

GS_PRIM_CLASS GSUtil::GetPrimClass(u32 prim)
{
	return (GS_PRIM_CLASS)s_maps.PrimClassField[prim];
}

int GSUtil::GetVertexCount(u32 prim)
{
	return s_maps.VertexCountField[prim];
}

int GSUtil::GetClassVertexCount(u32 primclass)
{
	return s_maps.ClassVertexCountField[primclass];
}

const u32* GSUtil::HasSharedBitsPtr(u32 dpsm)
{
	return s_maps.SharedBitsField[dpsm];
}

bool GSUtil::HasSharedBits(u32 spsm, const u32* RESTRICT ptr)
{
	return (ptr[spsm >> 5] & (1 << (spsm & 0x1f))) == 0;
}

// Pixels can NOT coexist in the same 32bits of space.
// Example: Using PSMT8H or PSMT4HL/HH with CT24 would fail this check.
bool GSUtil::HasSharedBits(u32 spsm, u32 dpsm)
{
	return (s_maps.SharedBitsField[dpsm][spsm >> 5] & (1 << (spsm & 0x1f))) == 0;
}

// Pixels can NOT coexist in the same 32bits of space.
// Example: Using PSMT8H or PSMT4HL/HH with CT24 would fail this check.
// SBP and DBO must match.
bool GSUtil::HasSharedBits(u32 sbp, u32 spsm, u32 dbp, u32 dpsm)
{
	return ((sbp ^ dbp) | (s_maps.SharedBitsField[dpsm][spsm >> 5] & (1 << (spsm & 0x1f)))) == 0;
}

// Shares bit depths, only detects 16/24/32 bit formats.
// 24/32bit cross compatible, 16bit compatbile with 16bit.
bool GSUtil::HasCompatibleBits(u32 spsm, u32 dpsm)
{
	return (s_maps.CompatibleBitsField[spsm][dpsm >> 5] & (1 << (dpsm & 0x1f))) != 0;
}

bool GSUtil::HasSameSwizzleBits(u32 spsm, u32 dpsm)
{
	return (s_maps.SwizzleField[spsm][dpsm >> 5] & (1 << (dpsm & 0x1f))) != 0;
}

u32 GSUtil::GetChannelMask(u32 spsm)
{
	switch (spsm)
	{
		case PSM_PSMCT24:
		case PSM_PSMZ24:
			return 0x7;
		case PSM_PSMT8H:
		case PSM_PSMT4HH: // This sucks, I'm sorry, but we don't have a way to do half channels
		case PSM_PSMT4HL: // So uuhh TODO I guess.
			return 0x8;
		default:
			return 0xf;
	}
}

CRCHackLevel GSUtil::GetRecommendedCRCHackLevel(GSRendererType type)
{
	return (type == GSRendererType::DX11 || type == GSRendererType::DX12) ? CRCHackLevel::Full : CRCHackLevel::Partial;
}

GSRendererType GSUtil::GetPreferredRenderer()
{
#if defined(__APPLE__)
	// Mac: Prefer Metal hardware.
	return GSRendererType::Metal;
#elif defined(_WIN32)
	// Use D3D device info to select renderer.
	return D3D::GetPreferredRenderer();
#else
	// Linux: Prefer GL/Vulkan, whatever is available.
#if defined(ENABLE_OPENGL)
	return GSRendererType::OGL;
#elif defined(ENABLE_VULKAN)
	return GSRendererType::VK;
#else
	return GSRendererType::SW;
#endif
#endif
}

const char* psm_str(int psm)
{
	switch (psm)
	{
		// Normal color
		case PSM_PSMCT32:  return "C_32";
		case PSM_PSMCT24:  return "C_24";
		case PSM_PSMCT16:  return "C_16";
		case PSM_PSMCT16S: return "C_16S";

		// Palette color
		case PSM_PSMT8:    return "P_8";
		case PSM_PSMT4:    return "P_4";
		case PSM_PSMT8H:   return "P_8H";
		case PSM_PSMT4HL:  return "P_4HL";
		case PSM_PSMT4HH:  return "P_4HH";

		// Depth
		case PSM_PSMZ32:   return "Z_32";
		case PSM_PSMZ24:   return "Z_24";
		case PSM_PSMZ16:   return "Z_16";
		case PSM_PSMZ16S:  return "Z_16S";

		case PSM_PSGPU24:  return "PS24";

		default:break;
	}
	return "BAD_PSM";
}
