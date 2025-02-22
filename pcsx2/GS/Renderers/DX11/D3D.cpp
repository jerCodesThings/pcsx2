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
#include "GS/Renderers/DX11/D3D.h"
#include "GS/GSExtra.h"

#include "common/Console.h"
#include "common/StringUtil.h"

#include <d3d11.h>

#include "fmt/format.h"

wil::com_ptr_nothrow<IDXGIFactory5> D3D::CreateFactory(bool debug)
{
	UINT flags = 0;
	if (debug)
		flags |= DXGI_CREATE_FACTORY_DEBUG;

	wil::com_ptr_nothrow<IDXGIFactory5> factory;
	const HRESULT hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(factory.put()));
	if (FAILED(hr))
		Console.Error("D3D: Failed to create DXGI factory: %08X", hr);

	return factory;
}

static std::string FixupDuplicateAdapterNames(const std::vector<std::string>& adapter_names, std::string adapter_name)
{
	if (std::any_of(adapter_names.begin(), adapter_names.end(),
			[&adapter_name](const std::string& other) { return (adapter_name == other); }))
	{
		std::string original_adapter_name = std::move(adapter_name);

		u32 current_extra = 2;
		do
		{
			adapter_name = StringUtil::StdStringFromFormat("%s (%u)", original_adapter_name.c_str(), current_extra);
			current_extra++;
		} while (std::any_of(adapter_names.begin(), adapter_names.end(),
			[&adapter_name](const std::string& other) { return (adapter_name == other); }));
	}

	return adapter_name;
}

std::vector<std::string> D3D::GetAdapterNames(IDXGIFactory5* factory)
{
	std::vector<std::string> adapter_names;

	wil::com_ptr_nothrow<IDXGIAdapter1> adapter;
	for (u32 index = 0;; index++)
	{
		const HRESULT hr = factory->EnumAdapters1(index, adapter.put());
		if (hr == DXGI_ERROR_NOT_FOUND)
			break;

		if (FAILED(hr))
		{
			Console.Error(fmt::format("IDXGIFactory2::EnumAdapters() returned %08X", hr));
			continue;
		}

		adapter_names.push_back(FixupDuplicateAdapterNames(adapter_names, GetAdapterName(adapter.get())));
	}

	return adapter_names;
}

wil::com_ptr_nothrow<IDXGIAdapter1> D3D::GetAdapterByName(IDXGIFactory5* factory, const std::string_view& name)
{
	if (name.empty())
		return {};

	// This might seem a bit odd to cache the names.. but there's a method to the madness.
	// We might have two GPUs with the same name... :)
	std::vector<std::string> adapter_names;

	wil::com_ptr_nothrow<IDXGIAdapter1> adapter;
	for (u32 index = 0;; index++)
	{
		const HRESULT hr = factory->EnumAdapters1(index, adapter.put());
		if (hr == DXGI_ERROR_NOT_FOUND)
			break;

		if (FAILED(hr))
		{
			Console.Error(fmt::format("IDXGIFactory2::EnumAdapters() returned %08X", hr));
			continue;
		}

		std::string adapter_name = FixupDuplicateAdapterNames(adapter_names, GetAdapterName(adapter.get()));
		if (adapter_name == name)
		{
			Console.WriteLn(fmt::format("D3D: Found adapter '{}'", adapter_name));
			return adapter;
		}

		adapter_names.push_back(std::move(adapter_name));
	}

	Console.Warning(fmt::format("Adapter '{}' not found.", name));
	return {};
}

wil::com_ptr_nothrow<IDXGIAdapter1> D3D::GetFirstAdapter(IDXGIFactory5* factory)
{
	wil::com_ptr_nothrow<IDXGIAdapter1> adapter;
	HRESULT hr = factory->EnumAdapters1(0, adapter.put());
	if (FAILED(hr))
		Console.Error(fmt::format("IDXGIFactory2::EnumAdapters() for first adapter returned %08X", hr));

	return adapter;
}

wil::com_ptr_nothrow<IDXGIAdapter1> D3D::GetChosenOrFirstAdapter(IDXGIFactory5* factory, const std::string_view& name)
{
	wil::com_ptr_nothrow<IDXGIAdapter1> adapter = GetAdapterByName(factory, name);
	if (!adapter)
		adapter = GetFirstAdapter(factory);

	return adapter;
}

std::string D3D::GetAdapterName(IDXGIAdapter1* adapter)
{
	std::string ret;

	DXGI_ADAPTER_DESC1 desc;
	HRESULT hr = adapter->GetDesc1(&desc);
	if (SUCCEEDED(hr))
	{
		ret = StringUtil::WideStringToUTF8String(desc.Description);
	}
	else
	{
		Console.Error(fmt::format("IDXGIAdapter1::GetDesc() returned {:08X}", hr));
	}

	if (ret.empty())
		ret = "(Unknown)";

	return ret;
}

std::string D3D::GetDriverVersionFromLUID(const LUID& luid)
{
	std::string ret;

	HKEY hKey;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\DirectX", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
	{
		DWORD max_key_len = 0, adapter_count = 0;
		if (RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, &adapter_count, &max_key_len, nullptr, nullptr, nullptr,
				nullptr, nullptr, nullptr) == ERROR_SUCCESS)
		{
			std::vector<TCHAR> current_name(max_key_len + 1);
			for (DWORD i = 0; i < adapter_count; ++i)
			{
				DWORD subKeyLength = static_cast<DWORD>(current_name.size());
				if (RegEnumKeyExW(hKey, i, current_name.data(), &subKeyLength, nullptr, nullptr, nullptr, nullptr) ==
					ERROR_SUCCESS)
				{
					LUID current_luid = {};
					DWORD current_luid_size = sizeof(uint64_t);
					if (RegGetValueW(hKey, current_name.data(), L"AdapterLuid", RRF_RT_QWORD, nullptr, &current_luid,
							&current_luid_size) == ERROR_SUCCESS &&
						current_luid.HighPart == luid.HighPart && current_luid.LowPart == luid.LowPart)
					{
						LARGE_INTEGER driver_version = {};
						DWORD driver_version_size = sizeof(driver_version);
						if (RegGetValueW(hKey, current_name.data(), L"DriverVersion", RRF_RT_QWORD, nullptr,
								&driver_version, &driver_version_size) == ERROR_SUCCESS)
						{
							WORD nProduct = HIWORD(driver_version.HighPart);
							WORD nVersion = LOWORD(driver_version.HighPart);
							WORD nSubVersion = HIWORD(driver_version.LowPart);
							WORD nBuild = LOWORD(driver_version.LowPart);
							ret = fmt::format("{}.{}.{}.{}", nProduct, nVersion, nSubVersion, nBuild);
						}
					}
				}
			}
		}

		RegCloseKey(hKey);
	}

	return ret;
}

D3D::VendorID D3D::GetVendorID(IDXGIAdapter1* adapter)
{
	DXGI_ADAPTER_DESC1 desc;
	const HRESULT hr = adapter->GetDesc1(&desc);
	if (FAILED(hr))
	{
		Console.Error(fmt::format("IDXGIAdapter1::GetDesc() returned {:08X}", hr));
		return VendorID::Unknown;
	}

	switch (desc.VendorId)
	{
		case 0x10DE:
			return VendorID::Nvidia;
		case 0x1002:
		case 0x1022:
			return VendorID::AMD;
		case 0x163C:
		case 0x8086:
		case 0x8087:
			return VendorID::Intel;
		default:
			return VendorID::Unknown;
	}
}

GSRendererType D3D::GetPreferredRenderer()
{
	auto factory = CreateFactory(false);
	auto adapter = GetChosenOrFirstAdapter(factory.get(), GSConfig.Adapter);

	// If we somehow can't get a D3D11 device, it's unlikely any of the renderers are going to work.
	if (!adapter)
		return GSRendererType::DX11;

	D3D_FEATURE_LEVEL feature_level;

	static const D3D_FEATURE_LEVEL check[] = {
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_0,
	};

	const HRESULT hr = D3D11CreateDevice(adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, std::data(check),
		std::size(check), D3D11_SDK_VERSION, nullptr, &feature_level, nullptr);

	if (FAILED(hr))
	{
		// See note above.
		return GSRendererType::DX11;
	}

	switch (GetVendorID(adapter.get()))
	{
		case VendorID::Nvidia:
		{
			if (feature_level == D3D_FEATURE_LEVEL_12_0)
				return GSRendererType::VK;
			else if (feature_level == D3D_FEATURE_LEVEL_11_0)
				return GSRendererType::OGL;
			else
				return GSRendererType::DX11;
		}

		case VendorID::AMD:
		{
			if (feature_level == D3D_FEATURE_LEVEL_12_0)
				return GSRendererType::VK;
			else
				return GSRendererType::DX11;
		}

		case VendorID::Intel:
		{
			// Older Intel GPUs prior to Xe seem to have broken OpenGL drivers which choke
			// on some of our shaders, causing what appears to be GPU timeouts+device removals.
			// Vulkan has broken barriers, also prior to Xe. So just fall back to DX11 everywhere,
			// unless we have Arc, which is easy to identify.
			if (StringUtil::StartsWith(GetAdapterName(adapter.get()), "Intel(R) Arc(TM) "))
				return GSRendererType::VK;
			else
				return GSRendererType::DX11;
		}

		default:
		{
			// Default is D3D11
			return GSRendererType::DX11;
		}
	}
}
