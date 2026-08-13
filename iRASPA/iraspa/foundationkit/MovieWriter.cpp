#include "MovieWriter.h"

#include <codecapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <algorithm>
#include <cstdio>
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace
{
  // MFShutdown is process-global and unconditional: the availableFormats() probe and any
  // number of writers encoding at the same time would otherwise pull Media Foundation out
  // from under each other. Startup and shutdown are therefore reference counted here, and
  // nothing calls MFShutdown directly.
  class MediaFoundationRuntime
  {
  public:
    static HRESULT acquire()
    {
      const std::lock_guard<std::mutex> lock(mutex());
      if (count() == 0)
      {
        const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
        if (FAILED(hr))
          return hr;
      }
      ++count();
      return S_OK;
    }

    static void release()
    {
      const std::lock_guard<std::mutex> lock(mutex());
      if (count() == 0)
        return;
      if (--count() == 0)
        MFShutdown();
    }

  private:
    static std::mutex &mutex()
    {
      static std::mutex m;
      return m;
    }

    static int &count()
    {
      static int c = 0;
      return c;
    }
  };

  struct MediaFoundationScope
  {
    MediaFoundationScope() : hr(MediaFoundationRuntime::acquire()) {}
    ~MediaFoundationScope()
    {
      if (SUCCEEDED(hr))
        MediaFoundationRuntime::release();
    }
    MediaFoundationScope(const MediaFoundationScope &) = delete;
    MediaFoundationScope &operator=(const MediaFoundationScope &) = delete;

    const HRESULT hr;
  };

  GUID subtypeForFormat(MovieWriter::Format format)
  {
    return format == MovieWriter::Format::hevc ? MFVideoFormat_HEVC : MFVideoFormat_H264;
  }

  unsigned int nearestEvenInt(unsigned int value)
  {
    return (value % 2 == 0) ? value : value + 1;
  }

  // The ffmpeg build asked for a flat 5 Mbit/s, which is about right for 1280x720 at
  // 25 fps and wrong by an order of magnitude at either end of the range iRASPA offers.
  // That point is kept as the anchor and the rate scales with the pixel rate:
  //
  //   bitRate = width * height * fps * 5e6 / (1280 * 720 * 25)
  //
  // HEVC reaches comparable quality at roughly 60% of the H.264 rate. The result is
  // clamped so that thumbnails do not get a starved encoder and 4k movies do not produce
  // files nobody can stream.
  UINT32 bitRateFor(unsigned int width, unsigned int height, int fps, MovieWriter::Format format)
  {
    constexpr double bitsPerPixelPerFrame = 5.0e6 / (1280.0 * 720.0 * 25.0);
    const double pixelRate = static_cast<double>(width) * static_cast<double>(height) *
                             static_cast<double>((std::max)(fps, 1));
    const double efficiency = (format == MovieWriter::Format::hevc) ? 0.6 : 1.0;
    const double rate = pixelRate * bitsPerPixelPerFrame * efficiency;
    return static_cast<UINT32>((std::clamp)(rate, 1.0e6, 8.0e7));
  }
}

std::vector<MovieWriter::Format> MovieWriter::availableFormats()
{
  // Enumerating every installed transform costs tens of milliseconds and the answer
  // cannot change while the process lives, so the menus get a cached copy.
  static const std::vector<Format> formats = []()
  {
    std::vector<Format> result;

    const MediaFoundationScope session;
    if (FAILED(session.hr))
      return result;

    for (const Format format : {Format::h264, Format::hevc})
    {
      MFT_REGISTER_TYPE_INFO outputInfo = {MFMediaType_Video, subtypeForFormat(format)};
      IMFActivate **activates = nullptr;
      UINT32 count = 0;
      // Hardware transforms are excluded to match what initialize() will actually let the
      // sink writer use; counting them would offer a format that then fails to encode on
      // a machine whose only encoder for it is the GPU's.
      const UINT32 flags = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT |
                           MFT_ENUM_FLAG_SORTANDFILTER;
      if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, flags, nullptr, &outputInfo,
                           &activates, &count)))
      {
        continue;
      }

      for (UINT32 i = 0; i < count; ++i)
      {
        if (activates[i])
          activates[i]->Release();
      }
      CoTaskMemFree(activates);

      if (count > 0)
        result.push_back(format);
    }

    return result;
  }();

  return formats;
}

const wchar_t *MovieWriter::displayName(Format format)
{
  switch (format)
  {
  case Format::hevc:
    return L"HEVC (H.265)";
  case Format::h264:
  default:
    return L"H.264";
  }
}

MovieWriter::MovieWriter(unsigned int width, unsigned int height, int fps, Format format) :
    m_width(nearestEvenInt(width)), m_height(nearestEvenInt(height)),
    m_fps((std::max)(fps, 1)), m_format(format)
{
}

MovieWriter::~MovieWriter()
{
  // Deliberately not finalizing here: an MP4 whose moov box was never written is not a
  // file the user should be handed, and an abandoned encode is nearly always the result
  // of an error path that already reported itself.
  releaseWriter();

  if (m_mediaFoundationStarted)
    MediaFoundationRuntime::release();
}

bool MovieWriter::fail(const wchar_t *step, HRESULT hr)
{
  wchar_t message[256] = {};
  swprintf_s(message, L"MovieWriter: %ls failed (hr=0x%08X)", step,
             static_cast<unsigned int>(hr));
  m_lastError = message;
  return false;
}

void MovieWriter::releaseWriter()
{
  m_sinkWriter.Reset();
  m_writing = false;
  m_swizzled.clear();
  m_swizzled.shrink_to_fit();
}

bool MovieWriter::initialize(const std::wstring &filename)
{
  if (m_writing)
    return true;

  m_lastError.clear();

  if (!m_mediaFoundationStarted)
  {
    const HRESULT hr = MediaFoundationRuntime::acquire();
    if (FAILED(hr))
      return fail(L"MFStartup", hr);
    m_mediaFoundationStarted = true;
  }

  ComPtr<IMFAttributes> attributes;
  HRESULT hr = MFCreateAttributes(&attributes, 3);
  if (FAILED(hr))
    return fail(L"MFCreateAttributes", hr);

  attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
  // Software encoders only. MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS is what would let the
  // sink writer pick a GPU encoder instead (the older MF_SINK_WRITER_ENABLE_HARDWARE_
  // TRANSFORMS name it is sometimes quoted under no longer exists in the Windows SDK).
  //
  // Vendor encoder MFTs cannot be trusted for offline export, and their failures are
  // silent. The AMD HEVC encoder on the development machine is wrong in two independent
  // ways: given no profile on the output type it dropped 110 of 120 frames, and given one
  // it reports every frame encoded (120 received, 120 encoded) while producing a
  // structurally valid MP4 that Microsoft's own decoder cannot get a single frame out of.
  // Nothing distinguishes a driver like that from a sound one at runtime, and a corrupt
  // movie discovered later is worse than a slower one. It costs little in any case:
  // export is render-bound, and encoding both ways took the same wall time at 1600x1600.
  attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);

  hr = MFCreateSinkWriterFromURL(filename.c_str(), nullptr, attributes.Get(), &m_sinkWriter);
  if (FAILED(hr))
    return fail(L"MFCreateSinkWriterFromURL", hr);

  ComPtr<IMFMediaType> outputType;
  hr = MFCreateMediaType(&outputType);
  if (FAILED(hr))
  {
    releaseWriter();
    return fail(L"MFCreateMediaType (output)", hr);
  }

  outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  outputType->SetGUID(MF_MT_SUBTYPE, subtypeForFormat(m_format));
  outputType->SetUINT32(MF_MT_AVG_BITRATE, bitRateFor(m_width, m_height, m_fps, m_format));
  outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE, m_width, m_height);
  MFSetAttributeRatio(outputType.Get(), MF_MT_FRAME_RATE, static_cast<UINT32>(m_fps), 1);
  MFSetAttributeRatio(outputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  // Always name the profile. An encoder left to guess may do so badly: fed HEVC output
  // types with no profile, the AMD hardware encoder dropped most of the frames it was
  // given (see the hardware-transforms note above).
  if (m_format == Format::h264)
    outputType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
  else
    outputType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH265VProfile_Main_420_8);

  hr = m_sinkWriter->AddStream(outputType.Get(), &m_streamIndex);
  if (FAILED(hr))
  {
    releaseWriter();
    return fail(L"IMFSinkWriter::AddStream", hr);
  }

  ComPtr<IMFMediaType> inputType;
  hr = MFCreateMediaType(&inputType);
  if (FAILED(hr))
  {
    releaseWriter();
    return fail(L"MFCreateMediaType (input)", hr);
  }

  inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  inputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
  MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE, m_width, m_height);
  MFSetAttributeRatio(inputType.Get(), MF_MT_FRAME_RATE, static_cast<UINT32>(m_fps), 1);
  MFSetAttributeRatio(inputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

  // RGB32 in Media Foundation defaults to the bottom-up DIB convention, so a plain
  // top-down copy would come out of the encoder upside down. The two cures are handing
  // the colour converter a negative stride pointing at the last row, or declaring the
  // buffer top-down with a positive MF_MT_DEFAULT_STRIDE. The declaration is chosen:
  // pointer arithmetic that walks backwards through a buffer is exactly the kind of
  // detail that gets "tidied up" later, whereas an attribute on the media type states the
  // intent where anyone reading the type will see it.
  inputType->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(m_width * 4u));

  hr = m_sinkWriter->SetInputMediaType(m_streamIndex, inputType.Get(), nullptr);
  if (FAILED(hr))
  {
    releaseWriter();
    return fail(L"IMFSinkWriter::SetInputMediaType", hr);
  }

  hr = m_sinkWriter->BeginWriting();
  if (FAILED(hr))
  {
    releaseWriter();
    return fail(L"IMFSinkWriter::BeginWriting", hr);
  }

  m_swizzled.resize(static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 4u);
  m_writing = true;
  m_finalized = false;
  return true;
}

bool MovieWriter::addFrame(const uint8_t *pixels, size_t frameIndex)
{
  if (!m_writing)
  {
    m_lastError = L"MovieWriter: addFrame called before a successful initialize";
    return false;
  }

  if (!pixels)
  {
    m_lastError = L"MovieWriter: addFrame called with a null frame buffer";
    return false;
  }

  const size_t stride = static_cast<size_t>(m_width) * 4u;
  for (size_t row = 0; row < m_height; ++row)
  {
    const uint8_t *source = pixels + row * stride;
    uint8_t *destination = m_swizzled.data() + row * stride;
    for (size_t x = 0; x < m_width; ++x)
    {
      destination[x * 4 + 0] = source[x * 4 + 2];
      destination[x * 4 + 1] = source[x * 4 + 1];
      destination[x * 4 + 2] = source[x * 4 + 0];
      destination[x * 4 + 3] = source[x * 4 + 3];
    }
  }

  const DWORD frameBytes = static_cast<DWORD>(stride * m_height);

  // A fresh buffer per frame: the encoder may still hold the previous sample, and for
  // hardware transforms that reference can outlive WriteSample by several frames.
  ComPtr<IMFMediaBuffer> buffer;
  HRESULT hr = MFCreateMemoryBuffer(frameBytes, &buffer);
  if (FAILED(hr))
    return fail(L"MFCreateMemoryBuffer", hr);

  BYTE *destination = nullptr;
  hr = buffer->Lock(&destination, nullptr, nullptr);
  if (FAILED(hr))
    return fail(L"IMFMediaBuffer::Lock", hr);

  hr = MFCopyImage(destination, static_cast<LONG>(stride), m_swizzled.data(),
                   static_cast<LONG>(stride), static_cast<DWORD>(stride), m_height);
  buffer->Unlock();
  if (FAILED(hr))
    return fail(L"MFCopyImage", hr);

  hr = buffer->SetCurrentLength(frameBytes);
  if (FAILED(hr))
    return fail(L"IMFMediaBuffer::SetCurrentLength", hr);

  ComPtr<IMFSample> sample;
  hr = MFCreateSample(&sample);
  if (FAILED(hr))
    return fail(L"MFCreateSample", hr);

  hr = sample->AddBuffer(buffer.Get());
  if (FAILED(hr))
    return fail(L"IMFSample::AddBuffer", hr);

  const LONGLONG duration = 10'000'000LL / m_fps;
  const LONGLONG timestamp = static_cast<LONGLONG>(frameIndex) * 10'000'000LL / m_fps;

  hr = sample->SetSampleTime(timestamp);
  if (FAILED(hr))
    return fail(L"IMFSample::SetSampleTime", hr);

  hr = sample->SetSampleDuration(duration);
  if (FAILED(hr))
    return fail(L"IMFSample::SetSampleDuration", hr);

  hr = m_sinkWriter->WriteSample(m_streamIndex, sample.Get());
  if (FAILED(hr))
    return fail(L"IMFSinkWriter::WriteSample", hr);

  return true;
}

bool MovieWriter::finalize()
{
  // A repeat call reports the original outcome rather than a fresh success: the caller
  // that only checks the second one should still see that the file went wrong.
  if (m_finalized)
    return m_finalizeResult;

  if (!m_writing)
  {
    m_lastError = L"MovieWriter: finalize called before a successful initialize";
    return false;
  }

  m_finalized = true;

  const HRESULT hr = m_sinkWriter->Finalize();
  releaseWriter();
  m_finalizeResult = SUCCEEDED(hr);

  if (FAILED(hr))
    return fail(L"IMFSinkWriter::Finalize", hr);

  return true;
}
