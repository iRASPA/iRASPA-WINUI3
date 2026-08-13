#include "ImageExport.h"

#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cwchar>
#include <cwctype>

using Microsoft::WRL::ComPtr;

namespace
{
  bool report(std::wstring *error, const wchar_t *step, HRESULT hr)
  {
    if (error)
    {
      wchar_t buffer[256]{};
      std::swprintf(buffer, std::size(buffer), L"%ls failed (hr=0x%08lX)", step, static_cast<unsigned long>(hr));
      error->assign(buffer);
    }
    return false;
  }

  std::wstring lowerExtension(const std::wstring &filename)
  {
    const size_t dot = filename.find_last_of(L'.');
    if (dot == std::wstring::npos)
      return {};
    std::wstring extension = filename.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return extension;
  }

  GUID containerForExtension(const std::wstring &extension)
  {
    if (extension == L".jpg" || extension == L".jpeg")
      return GUID_ContainerFormatJpeg;
    if (extension == L".tif" || extension == L".tiff")
      return GUID_ContainerFormatTiff;
    if (extension == L".bmp")
      return GUID_ContainerFormatBmp;
    return GUID_ContainerFormatPng;
  }

  bool createFactory(ComPtr<IWICImagingFactory> &factory, std::wstring *error)
  {
    const HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&factory));
    if (FAILED(hr))
      return report(error, L"CoCreateInstance(WICImagingFactory)", hr);
    return true;
  }

  bool bitmapFromRgba(IWICImagingFactory *factory, const uint8_t *pixels, int width, int height,
                      ComPtr<IWICBitmap> &bitmap, std::wstring *error)
  {
    const UINT stride = static_cast<UINT>(width) * 4u;
    const UINT bufferSize = stride * static_cast<UINT>(height);
    const HRESULT hr = factory->CreateBitmapFromMemory(
        static_cast<UINT>(width), static_cast<UINT>(height), GUID_WICPixelFormat32bppRGBA, stride,
        bufferSize, const_cast<BYTE *>(pixels), &bitmap);
    if (FAILED(hr))
      return report(error, L"CreateBitmapFromMemory", hr);
    return true;
  }

  bool encodeBitmapToStream(IWICImagingFactory *factory, IWICBitmapSource *source, IStream *stream,
                            const GUID &container, double dotsPerInch, std::wstring *error)
  {
    ComPtr<IWICBitmapEncoder> encoder;
    HRESULT hr = factory->CreateEncoder(container, nullptr, &encoder);
    if (FAILED(hr))
      return report(error, L"CreateEncoder", hr);
    hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (FAILED(hr))
      return report(error, L"IWICBitmapEncoder::Initialize", hr);

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> frameProperties;
    hr = encoder->CreateNewFrame(&frame, &frameProperties);
    if (FAILED(hr))
      return report(error, L"CreateNewFrame", hr);
    hr = frame->Initialize(frameProperties.Get());
    if (FAILED(hr))
      return report(error, L"IWICBitmapFrameEncode::Initialize", hr);

    UINT width = 0;
    UINT height = 0;
    hr = source->GetSize(&width, &height);
    if (FAILED(hr))
      return report(error, L"GetSize", hr);
    hr = frame->SetSize(width, height);
    if (FAILED(hr))
      return report(error, L"SetSize", hr);

    const double dpi = dotsPerInch > 0.0 ? dotsPerInch : 72.0;
    frame->SetResolution(dpi, dpi);

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppRGBA;
    hr = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(hr))
      return report(error, L"SetPixelFormat", hr);

    if (IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppRGBA))
    {
      hr = frame->WriteSource(source, nullptr);
    }
    else
    {
      ComPtr<IWICFormatConverter> converter;
      hr = factory->CreateFormatConverter(&converter);
      if (FAILED(hr))
        return report(error, L"CreateFormatConverter", hr);
      hr = converter->Initialize(source, pixelFormat, WICBitmapDitherTypeNone, nullptr, 0.0,
                                 WICBitmapPaletteTypeCustom);
      if (FAILED(hr))
        return report(error, L"IWICFormatConverter::Initialize", hr);
      hr = frame->WriteSource(converter.Get(), nullptr);
    }
    if (FAILED(hr))
      return report(error, L"WriteSource", hr);

    hr = frame->Commit();
    if (FAILED(hr))
      return report(error, L"IWICBitmapFrameEncode::Commit", hr);
    hr = encoder->Commit();
    if (FAILED(hr))
      return report(error, L"IWICBitmapEncoder::Commit", hr);
    return true;
  }

  bool decodeFrameToRgba(IWICImagingFactory *factory, IWICBitmapFrameDecode *frame,
                         std::vector<uint8_t> &pixels, int &width, int &height, std::wstring *error)
  {
    UINT w = 0;
    UINT h = 0;
    HRESULT hr = frame->GetSize(&w, &h);
    if (FAILED(hr) || w == 0 || h == 0)
      return report(error, L"IWICBitmapFrameDecode::GetSize", hr);

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr))
      return report(error, L"CreateFormatConverter", hr);
    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr,
                               0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
      return report(error, L"IWICFormatConverter::Initialize", hr);

    const UINT stride = w * 4u;
    const UINT bufferSize = stride * h;
    pixels.assign(bufferSize, 0);
    hr = converter->CopyPixels(nullptr, stride, bufferSize, pixels.data());
    if (FAILED(hr))
      return report(error, L"CopyPixels", hr);

    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
  }
}

namespace ImageExport
{
  bool saveImage(const std::wstring &filename, const uint8_t *pixels, int width, int height,
                 double dotsPerInch, std::wstring *error)
  {
    if (!pixels || width <= 0 || height <= 0)
    {
      if (error)
        error->assign(L"Nothing to save: the rendered frame is empty.");
      return false;
    }

    ComPtr<IWICImagingFactory> factory;
    if (!createFactory(factory, error))
      return false;

    ComPtr<IWICBitmap> bitmap;
    if (!bitmapFromRgba(factory.Get(), pixels, width, height, bitmap, error))
      return false;

    ComPtr<IWICStream> stream;
    HRESULT hr = factory->CreateStream(&stream);
    if (FAILED(hr))
      return report(error, L"CreateStream", hr);
    hr = stream->InitializeFromFilename(filename.c_str(), GENERIC_WRITE);
    if (FAILED(hr))
      return report(error, L"InitializeFromFilename", hr);

    return encodeBitmapToStream(factory.Get(), bitmap.Get(), stream.Get(),
                                containerForExtension(lowerExtension(filename)), dotsPerInch, error);
  }

  bool encodePng(const uint8_t *pixels, int width, int height, std::vector<uint8_t> &out,
                 std::wstring *error)
  {
    out.clear();
    if (!pixels || width <= 0 || height <= 0)
    {
      if (error)
        error->assign(L"Nothing to encode: the image is empty.");
      return false;
    }

    ComPtr<IWICImagingFactory> factory;
    if (!createFactory(factory, error))
      return false;

    ComPtr<IWICBitmap> bitmap;
    if (!bitmapFromRgba(factory.Get(), pixels, width, height, bitmap, error))
      return false;

    ComPtr<IStream> stream;
    HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (FAILED(hr))
      return report(error, L"CreateStreamOnHGlobal", hr);

    if (!encodeBitmapToStream(factory.Get(), bitmap.Get(), stream.Get(), GUID_ContainerFormatPng,
                              72.0, error))
      return false;

    HGLOBAL memory = nullptr;
    hr = GetHGlobalFromStream(stream.Get(), &memory);
    if (FAILED(hr) || !memory)
      return report(error, L"GetHGlobalFromStream", hr);

    const SIZE_T size = GlobalSize(memory);
    void *locked = GlobalLock(memory);
    if (!locked || size == 0)
    {
      if (locked)
        GlobalUnlock(memory);
      if (error)
        error->assign(L"PNG encode produced an empty stream.");
      return false;
    }
    const auto *bytes = static_cast<const uint8_t *>(locked);
    out.assign(bytes, bytes + size);
    GlobalUnlock(memory);
    return true;
  }

  bool decodeImage(const uint8_t *bytes, size_t size, std::vector<uint8_t> &pixels, int &width,
                   int &height, std::wstring *error)
  {
    pixels.clear();
    width = 0;
    height = 0;
    if (!bytes || size == 0)
    {
      if (error)
        error->assign(L"Nothing to decode: the image blob is empty.");
      return false;
    }

    ComPtr<IWICImagingFactory> factory;
    if (!createFactory(factory, error))
      return false;

    ComPtr<IWICStream> stream;
    HRESULT hr = factory->CreateStream(&stream);
    if (FAILED(hr))
      return report(error, L"CreateStream", hr);
    hr = stream->InitializeFromMemory(const_cast<BYTE *>(bytes), static_cast<DWORD>(size));
    if (FAILED(hr))
      return report(error, L"InitializeFromMemory", hr);

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                          &decoder);
    if (FAILED(hr))
      return report(error, L"CreateDecoderFromStream", hr);

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr))
      return report(error, L"GetFrame", hr);

    return decodeFrameToRgba(factory.Get(), frame.Get(), pixels, width, height, error);
  }

  bool loadImageFile(const std::wstring &filename, std::vector<uint8_t> &pixels, int &width,
                     int &height, std::wstring *error)
  {
    pixels.clear();
    width = 0;
    height = 0;

    ComPtr<IWICImagingFactory> factory;
    if (!createFactory(factory, error))
      return false;

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = factory->CreateDecoderFromFilename(filename.c_str(), nullptr, GENERIC_READ,
                                                    WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr))
      return report(error, L"CreateDecoderFromFilename", hr);

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr))
      return report(error, L"GetFrame", hr);

    return decodeFrameToRgba(factory.Get(), frame.Get(), pixels, width, height, error);
  }
}
