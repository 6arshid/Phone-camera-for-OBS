#include "decoder.h"

#include <codecapi.h>
#include <icodecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <objbase.h>

#ifdef SendMessage
#undef SendMessage
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

namespace ocp {
namespace plugin {

namespace {

std::once_flag g_mfOnce;
thread_local bool g_comInited = false;

void ensureCom()
{
	if (!g_comInited) {
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		g_comInited = true;
	}
}

void ensureMf()
{
	std::call_once(g_mfOnce, [] {
		ensureCom();
		MFStartup(MF_VERSION);
	});
}

LONG mediaTypeStride(IMFMediaType *type)
{
	LONG stride = 0;
	UINT32 size = 0;
	if (SUCCEEDED(type->GetBlobSize(MF_MT_DEFAULT_STRIDE, &size)) &&
	    size == sizeof(LONG)) {
		if (SUCCEEDED(type->GetBlob(MF_MT_DEFAULT_STRIDE, (UINT8 *)&stride,
					    sizeof(LONG), nullptr)))
			return stride;
	}
	return 0;
}

class MfDecoder final : public VideoDecoder {
public:
	~MfDecoder() override { release(); }

	bool configure(int width, int height, int fps, int bitrateKbps,
		       std::string *err) override
	{
		ensureCom();
		release();
		if (width <= 0 || height <= 0 || (width & 1) || (height & 1)) {
			if (err)
				*err = "invalid dimensions";
			return false;
		}
		if (fps <= 0)
			fps = 30;
		if (bitrateKbps <= 0)
			bitrateKbps = 4000;

		HRESULT hr;
		MFT_REGISTER_TYPE_INFO inputInfo = {MFMediaType_Video, MFVideoFormat_H264};
		MFT_REGISTER_TYPE_INFO outputInfo = {MFMediaType_Video, MFVideoFormat_NV12};

		IMFActivate **activates = nullptr;
		UINT32 count = 0;
		hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
			       MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER, &inputInfo,
			       &outputInfo, &activates, &count);
		if (FAILED(hr) || count == 0) {
			if (err)
				*err = "no H.264 decoder MFT found";
			return false;
		}

		// Prefer software decoders: hardware MFTs may accept system-memory
		// samples but silently produce no output without a D3D device.
		UINT32 chosen = 0;
		for (UINT32 i = 0; i < count; i++) {
			WCHAR *name = nullptr;
			UINT32 nameLen = 0;
			bool hw = false;
			if (SUCCEEDED(activates[i]->GetAllocatedString(
				    MFT_FRIENDLY_NAME_Attribute, &name, &nameLen)) &&
			    name) {
				hw = wcsstr(name, L"Hardware") != nullptr;
				if (getenv("OCP_DEC_DEBUG"))
					fprintf(stderr, "[dec] mft[%u] %ls\n", i, name);
				CoTaskMemFree(name);
			}
			if (!hw) {
				chosen = i;
				break;
			}
		}

		hr = activates[chosen]->ActivateObject(IID_IMFTransform, (void **)&mft);
		for (UINT32 i = 0; i < count; i++)
			activates[i]->Release();
		CoTaskMemFree(activates);
		if (FAILED(hr)) {
			mft = nullptr;
			if (err)
				*err = "decoder activation failed";
			return false;
		}

		IMFMediaType *inType = nullptr;
		MFCreateMediaType(&inType);
		inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
		inType->SetUINT32(MF_MT_AVG_BITRATE, (UINT32)(bitrateKbps * 1000));
		inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
		MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, (UINT32)width, (UINT32)height);
		MFSetAttributeRatio(inType, MF_MT_FRAME_RATE, (UINT32)fps, 1);
		MFSetAttributeRatio(inType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
		hr = mft->SetInputType(0, inType, 0);
		inType->Release();
		if (FAILED(hr)) {
			release();
			if (err)
				*err = "decoder rejected H.264 input type";
			return false;
		}

		ICodecAPI *codecApi = nullptr;
		if (SUCCEEDED(mft->QueryInterface(IID_ICodecAPI, (void **)&codecApi))) {
			VARIANT v;
			v.vt = VT_BOOL;
			v.boolVal = VARIANT_TRUE;
			HRESULT setHr = codecApi->SetValue(&CODECAPI_AVLowLatencyMode, &v);
			if (getenv("OCP_DEC_DEBUG"))
				fprintf(stderr, "[dec] lowlatency set hr=0x%08lx\n",
					(unsigned long)setHr);
			codecApi->Release();
		}

		IMFMediaType *outType = nullptr;
		MFCreateMediaType(&outType);
		outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
		outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
		MFSetAttributeSize(outType, MF_MT_FRAME_SIZE, (UINT32)width, (UINT32)height);
		MFSetAttributeRatio(outType, MF_MT_FRAME_RATE, (UINT32)fps, 1);
		MFSetAttributeRatio(outType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
		hr = mft->SetOutputType(0, outType, 0);
		if (FAILED(hr)) {
			// The decoder will negotiate the real format via
			// MF_E_TRANSFORM_STREAM_CHANGE once it sees the SPS.
			if (getenv("OCP_DEC_DEBUG"))
				fprintf(stderr, "[dec] preset output type hr=0x%08lx (ok)\n",
					(unsigned long)hr);
		} else {
			LONG attr = mediaTypeStride(outType);
			if (attr != 0)
				strideY_ = (int)abs(attr);
		}
		outType->Release();

		MFT_OUTPUT_STREAM_INFO info = {};
		mft->GetOutputStreamInfo(0, &info);
		outBufSize_ = info.cbSize ? (size_t)info.cbSize
					  : (size_t)width * (size_t)height * 3 / 2 + 4096;

		mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
		mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

		if (getenv("OCP_DEC_DEBUG")) {
			MFT_OUTPUT_STREAM_INFO sinfo = {};
			mft->GetOutputStreamInfo(0, &sinfo);
			fprintf(stderr, "[dec] outStream flags=0x%lx cbSize=%lu\n",
				(unsigned long)sinfo.dwFlags, (unsigned long)sinfo.cbSize);
		}

		width_ = width;
		height_ = height;
		fps_ = fps;
		if (strideY_ <= 0)
			strideY_ = width;
		ptsTicks_ = 0;
		consecutiveErrors_ = 0;
		configured_ = true;
		return true;
	}

	bool feed(const uint8_t *data, size_t size, std::string *err) override
	{
		if (!configured_ || !mft || !data || size == 0) {
			if (err)
				*err = "decoder not configured";
			return false;
		}
		ensureCom();

		IMFMediaBuffer *buf = nullptr;
		HRESULT hr = MFCreateMemoryBuffer((DWORD)size, &buf);
		if (FAILED(hr)) {
			if (err)
				*err = "MFCreateMemoryBuffer failed";
			return false;
		}
		BYTE *ptr = nullptr;
		DWORD maxLen = 0;
		hr = buf->Lock(&ptr, &maxLen, nullptr);
		if (FAILED(hr)) {
			buf->Release();
			return false;
		}
		memcpy(ptr, data, size);
		buf->Unlock();
		buf->SetCurrentLength((DWORD)size);

		IMFSample *sample = nullptr;
		MFCreateSample(&sample);
		sample->AddBuffer(buf);
		LONGLONG hns = (LONGLONG)ptsTicks_ * (10000000LL / fps_);
		sample->SetSampleTime(hns);
		buf->Release();

		hr = mft->ProcessInput(0, sample, 0);
		if (getenv("OCP_DEC_DEBUG") && ptsTicks_ < 2)
			fprintf(stderr, "[dec] ProcessInput[%lld] hr=0x%08lx bytes=%zu\n",
				(long long)ptsTicks_, (unsigned long)hr, size);
		sample->Release();
		if (FAILED(hr)) {
			consecutiveErrors_++;
			if (err) {
				char text[64];
				snprintf(text, sizeof(text), "ProcessInput failed (0x%08lx)",
					 (unsigned long)hr);
				*err = text;
			}
			return false;
		}
		ptsTicks_++;
		drain();
		consecutiveErrors_ = 0;
		return true;
	}

	bool take(DecodedFrame *out) override
	{
		if (!out || queue.empty())
			return false;
		*out = std::move(queue.front());
		queue.pop_front();
		return true;
	}

	int pending() const override { return (int)queue.size(); }

	void flush() override
	{
		if (!mft)
			return;
		ensureCom();
		mft->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
		drain();
	}

	void reset() override
	{
		if (!mft)
			return;
		mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
		drain();
		queue.clear();
		ptsTicks_ = 0;
	}

private:
	void release()
	{
		configured_ = false;
		queue.clear();
		if (mft) {
			mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
			mft->Release();
			mft = nullptr;
		}
	}

	void drain()
	{
		for (;;) {
			MFT_OUTPUT_STREAM_INFO sinfo = {};
			if (SUCCEEDED(mft->GetOutputStreamInfo(0, &sinfo)) && sinfo.cbSize)
				outBufSize_ = (size_t)sinfo.cbSize;

			const bool provides = (sinfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;

			IMFSample *outSample = nullptr;
			IMFMediaBuffer *outBuf = nullptr;
			if (!provides) {
				MFCreateSample(&outSample);
				MFCreateMemoryBuffer((DWORD)outBufSize_, &outBuf);
				outSample->AddBuffer(outBuf);
			}

			MFT_OUTPUT_DATA_BUFFER out = {};
			out.pSample = provides ? nullptr : outSample;
			out.dwStreamID = 0;
			DWORD status = 0;
			HRESULT hr = mft->ProcessOutput(0, 1, &out, &status);
			if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
				if (getenv("OCP_DEC_DEBUG"))
					fprintf(stderr, "[dec] stream change\n");
				if (outSample)
					outSample->Release();
				if (outBuf)
					outBuf->Release();
				continue;
			}
			if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT || FAILED(hr)) {
				if (getenv("OCP_DEC_DEBUG"))
					fprintf(stderr, "[dec] ProcessOutput hr=0x%08lx\n",
						(unsigned long)hr);
				if (outSample)
					outSample->Release();
				if (outBuf)
					outBuf->Release();
				if (hr != MF_E_TRANSFORM_NEED_MORE_INPUT)
					consecutiveErrors_++;
				break;
			}

			IMFSample *result = out.pSample ? out.pSample : outSample;
			IMFMediaBuffer *buf = outBuf;
			if (result && !buf) {
				if (FAILED(result->ConvertToContiguousBuffer(&buf)))
					buf = nullptr;
			}
			BYTE *ptr = nullptr;
			DWORD len = 0;
			if (buf && SUCCEEDED(buf->Lock(&ptr, &len, nullptr))) {
				capture(ptr, len);
				buf->Unlock();
			}
			if (buf)
				buf->Release();
			if (result)
				result->Release();
		}
	}

	void capture(const BYTE *ptr, DWORD len)
	{
		UINT32 w = (UINT32)width_;
		UINT32 h = (UINT32)height_;
		IMFMediaType *type = nullptr;
		if (SUCCEEDED(mft->GetOutputCurrentType(0, &type))) {
			UINT32 ow = 0, oh = 0;
			if (SUCCEEDED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &ow, &oh))) {
				w = ow;
				h = oh;
			}
			LONG attr = mediaTypeStride(type);
			if (attr != 0)
				strideY_ = (int)abs(attr);
			type->Release();
		}
		if (w == 0 || h == 0)
			return;
		int strideY = strideY_ > 0 ? strideY_ : (int)w;
		size_t ySize = (size_t)strideY * h;
		size_t uvSize = (size_t)strideY * (h / 2);
		if (len < ySize + uvSize)
			return;

		DecodedFrame frame;
		frame.width = (int)w;
		frame.height = (int)h;
		frame.strideY = strideY;
		frame.strideUV = strideY;
		frame.index = index_++;
		frame.nv12.assign(ptr, ptr + ySize + uvSize);
		if (queue.size() >= 4)
			queue.pop_front();
		queue.push_back(std::move(frame));
	}

	IMFTransform *mft = nullptr;
	int width_ = 0;
	int height_ = 0;
	int fps_ = 30;
	int strideY_ = 0;
	size_t outBufSize_ = 0;
	LONGLONG ptsTicks_ = 0;
	uint64_t index_ = 0;
	int consecutiveErrors_ = 0;
	bool configured_ = false;
	std::deque<DecodedFrame> queue;
};

}

std::unique_ptr<VideoDecoder> createVideoDecoder()
{
	ensureMf();
	return std::unique_ptr<VideoDecoder>(new MfDecoder());
}

bool decoderSupported()
{
	ensureMf();
	MFT_REGISTER_TYPE_INFO inputInfo = {MFMediaType_Video, MFVideoFormat_H264};
	MFT_REGISTER_TYPE_INFO outputInfo = {MFMediaType_Video, MFVideoFormat_NV12};
	IMFActivate **activates = nullptr;
	UINT32 count = 0;
	HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
			       MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER, &inputInfo,
			       &outputInfo, &activates, &count);
	if (activates) {
		for (UINT32 i = 0; i < count; i++)
			activates[i]->Release();
		CoTaskMemFree(activates);
	}
	return SUCCEEDED(hr) && count > 0;
}

}
}
