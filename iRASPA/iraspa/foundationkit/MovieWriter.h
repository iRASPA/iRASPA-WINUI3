#pragma once

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// H.264/HEVC MP4 encoder on top of the Media Foundation sink writer; the Windows
/// replacement for the ffmpeg-based MovieWriter of the Qt build.
///
/// Frame buffers: addFrame() takes tightly packed RGBA8888, top row first, of exactly
/// width() * height() * 4 bytes. Those are the rounded dimensions -- H.264 and HEVC only
/// accept even extents, so the constructor rounds the requested width and height up to
/// even and the caller is expected to render at that size. No padding of undersized
/// buffers is attempted: silently encoding a differently sized image is worse than the
/// caller asking its renderer for width()/height() in the first place (which is what the
/// Qt build does by rounding with nearestEvenInt() before it constructs a MovieWriter).
///
/// Threading: an instance belongs to one thread for its whole lifetime. That thread must
/// have COM initialised before initialize() is called; MovieWriter never changes the
/// apartment state, as the apartment belongs to whoever owns the thread.
class MovieWriter
{
public:
  enum class Format
  {
    h264 = 0, hevc = 1
  };

  /// Formats an encoder is actually installed for on this machine, cached after the first
  /// call. Empty means no video can be written at all.
  static std::vector<Format> availableFormats();
  static const wchar_t *displayName(Format format);

  MovieWriter(unsigned int width, unsigned int height, int fps, Format format);
  ~MovieWriter();

  MovieWriter(const MovieWriter &) = delete;
  MovieWriter &operator=(const MovieWriter &) = delete;

  /// Creates filename and starts writing. On failure lastError() says which step failed
  /// and the object is left safe to destroy.
  bool initialize(const std::wstring &filename);
  bool addFrame(const uint8_t *pixels, size_t frameIndex);
  /// Flushes the encoder and closes the container. Harmless to call more than once.
  bool finalize();

  /// Requested extents rounded up to even; the size addFrame() expects.
  unsigned int width() const { return m_width; }
  unsigned int height() const { return m_height; }
  int framesPerSecond() const { return m_fps; }
  Format format() const { return m_format; }

  const std::wstring &lastError() const { return m_lastError; }

private:
  bool fail(const wchar_t *step, HRESULT hr);
  void releaseWriter();

  const unsigned int m_width;
  const unsigned int m_height;
  const int m_fps;
  const Format m_format;

  bool m_mediaFoundationStarted = false;
  bool m_writing = false;
  bool m_finalized = false;
  bool m_finalizeResult = false;
  DWORD m_streamIndex = 0;
  std::wstring m_lastError;

  /// Scratch for the RGBA-to-BGRA swizzle, reused across frames.
  std::vector<uint8_t> m_swizzled;

  Microsoft::WRL::ComPtr<IMFSinkWriter> m_sinkWriter;
};
