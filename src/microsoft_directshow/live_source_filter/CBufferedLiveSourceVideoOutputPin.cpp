/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <stdafx.h>

#include "CBufferedLiveSourceVideoOutputPin.h"


CBufferedLiveSourceVideoOutputPin::CBufferedLiveSourceVideoOutputPin(
	CLiveSource* filter,
	CCritSec* pLock,
	HRESULT* phr):
	ALiveSourceVideoOutputPin(filter, pLock, phr)
{
}


CBufferedLiveSourceVideoOutputPin::~CBufferedLiveSourceVideoOutputPin()
{
	PurgeQueue();
}


HRESULT CBufferedLiveSourceVideoOutputPin::Active()
{
	// TODO: Allow
	if (m_frameQueueMaxSize == 0)
		throw std::runtime_error("Call SetFrameQueueMaxSize() before activating the graph");

	{
		CAutoLock lock(m_pLock);

		if (m_pFilter->IsActive())
			return S_FALSE;	// succeeded, but did not allocate resources (they already exist...)

		assert(IsConnected());
		assert(!m_isActive);

		HRESULT hr = ALiveSourceVideoOutputPin::Active();
		if (FAILED(hr))
			return hr;

		assert(!ThreadExists());

		{
			CAutoLock lock2(&m_filterCritSec);

			m_isActive = true;
		}

		// start the thread
		if (!Create())
			return E_FAIL;

		return S_OK;
	}
}


HRESULT CBufferedLiveSourceVideoOutputPin::Inactive()
{
	{
		CAutoLock lock(m_pLock);

		// do nothing if not connected - its ok not to connect to all pins of a source filter
		if (!IsConnected())
			return NOERROR;

		HRESULT hr = ALiveSourceVideoOutputPin::Inactive();
		if (FAILED(hr))
			return hr;

		{
			CAutoLock lock2(&m_filterCritSec);

			m_isActive = false;

			PurgeQueue();
		}

		if (ThreadExists())
		{
			Close();
		}
	}
}


HRESULT CBufferedLiveSourceVideoOutputPin::OnVideoFrame(VideoFrame& videoFrame)
{
	{
		CAutoLock lock(&m_filterCritSec);

		// Reject frames if not processing
		if (!m_isActive)
			return S_OK;

		// If full throw away oldest to make space
		if (m_videoFrameQueue.size() >= m_frameQueueMaxSize)
		{
			VideoFrame popFrame = m_videoFrameQueue.front();
			popFrame.SourceBufferRelease();

			m_videoFrameQueue.pop_front();
		}

		// Prevent from getting cleaned up and add to queue
		videoFrame.SourceBufferAddRef();
		m_videoFrameQueue.push_back(videoFrame);
	}

	return S_OK;
}


void CBufferedLiveSourceVideoOutputPin::SetFrameQueueMaxSize(size_t frameQueueMaxSize)
{
	if (frameQueueMaxSize == 0)
		throw std::runtime_error("Frame queue size must be > 0");

	{
		CAutoLock lock(&m_filterCritSec);

		if (m_frameQueueMaxSize != 0)
			throw std::runtime_error("Frame queue size can be set only once");

		m_frameQueueMaxSize = frameQueueMaxSize;
	}
}


size_t CBufferedLiveSourceVideoOutputPin::GetFrameQueueSize()
{
	{
		CAutoLock lock(&m_filterCritSec);

		return m_videoFrameQueue.size();
	}
}


DWORD CBufferedLiveSourceVideoOutputPin::ThreadProc()
{
	// ! WARNING: Runs in inner thread

	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin worker thread starting")));

	while (true)
	{
		// TODO: Sleep thread on empty queue and wake if frames arrive
		Sleep(1);

		VideoFrame videoFrame;

		{
			CAutoLock lock(&m_filterCritSec);

			// Stop thread
			if (!m_isActive)
				break;

			// Nothing to do
			if (m_videoFrameQueue.empty())
				continue;

			// Get the front frame (oldest)
			videoFrame = m_videoFrameQueue.front();
			m_videoFrameQueue.pop_front();
		}

		// Get buffer for sample
		// TODO: the 2 null pointers here point to the start and end of the sample, use that in cooperation with the captured time?
		IMediaSample* pSample = NULL;
		HRESULT hr = this->GetDeliveryBuffer(&pSample, NULL, NULL, 0);
		if (FAILED(hr))
		{
			videoFrame.SourceBufferRelease();
			return -1;
		}

		// Convert
		hr = RenderVideoFrameIntoSample(videoFrame, pSample);
		if (FAILED(hr))
		{
			videoFrame.SourceBufferRelease();
			pSample->Release();
			return -2;
		}

		// Deliver frame to renderer
		hr = this->Deliver(pSample);
		if (FAILED(hr))
		{
			DbgLog((LOG_TRACE, 1,
				TEXT("::FillBuffer(#%I64u): Failed to deliver sample"),
				videoFrame.GetCounter()));

			videoFrame.SourceBufferRelease();
			pSample->Release();
			return -3;
		}

		videoFrame.SourceBufferRelease();
		pSample->Release();
	}

	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin worker thread existing")));

	return 0;
}


void CBufferedLiveSourceVideoOutputPin::PurgeQueue()
{
	{
		CAutoLock lock(&m_filterCritSec);

		while (!m_videoFrameQueue.empty())
		{
			VideoFrame popFrame = m_videoFrameQueue.front();
			popFrame.SourceBufferRelease();

			m_videoFrameQueue.pop_front();
		}
	}
}