/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <stdafx.h>

#include "VideoState.h"


HRESULT	VideoState::QueryInterface(REFIID iid, LPVOID* ppv)
{
	if (ppv == nullptr)
		return E_INVALIDARG;

	// Initialise the return result
	*ppv = nullptr;

	// Obtain the IUnknown interface and compare it the provided REFIID
	if (iid == IID_IUnknown)
	{
		*ppv = this;
		AddRef();
		return S_OK;
	}

	return E_NOINTERFACE;
}


ULONG VideoState::AddRef(void)
{
	return ++m_refCount;
}


ULONG VideoState::Release(void)
{
	ULONG newRefValue = --m_refCount;
	if (newRefValue == 0)
		delete this;

	return newRefValue;
}


uint32_t VideoState::BytesPerRow() const
{
	// See docs/bmd_pixel_formats.pdf also in "Blackmagic DeckLink SDK.pdf" from v 12.0
	switch (pixelFormat)
	{
	case PixelFormat::YUV_8BIT:
		return displayMode->FrameWidth() * 16 / 8;

	case PixelFormat::YUV_10BIT:
		return ((displayMode->FrameWidth() + 47) / 48) * 128;

	case PixelFormat::ARGB_8BIT:
	case PixelFormat::BGRA_8BIT:
		return ((displayMode->FrameWidth() + 32) / 8);

	case PixelFormat::R210:
	case PixelFormat::RGB_BE_10BIT:
	case PixelFormat::RGB_LE_10BIT:
		return ((displayMode->FrameWidth() + 63) / 64) * 256;

	case PixelFormat::RGB_BE_12BIT:
	case PixelFormat::RGB_LE_12BIT:
		return ((displayMode->FrameWidth() * 36) / 8);
	}

	throw std::runtime_error("Don't know how to calculate row bytes for given format");
}


uint32_t VideoState::BytesPerFrame() const
{
	return BytesPerRow() * displayMode->FrameHeight();
}
