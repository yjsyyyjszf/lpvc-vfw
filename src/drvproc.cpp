#include "dib_iterator.h"
#include "resource.h"
#include <lpvc/lpvc.h>
#include <windows.h>
#include <commctrl.h>
#include <vfw.h>
#include <cassert>
#include <cwchar>
#include <iterator>
#include <memory>
#include <string>


const DWORD FOURCC_LPVC = mmioFOURCC('L', 'P', 'V', 'C');


static HINSTANCE lpvcDllInstance = 0;


struct EncoderSettings final
{
  lpvc::EncoderSettings settings;
  bool ignoreKeyFrameRequests = false;
  bool forceKeyFrames = true;
  std::size_t keyFrameInterval = 1000;
};

static_assert(std::is_trivially_copyable_v<EncoderSettings>);


static std::string dialogItemText(HWND dialog, int itemId)
{
  auto dialogItem = GetDlgItem(dialog, itemId);
  if(!dialogItem)
    throw std::runtime_error("Failed to get dialog item handle.");

  auto textLength = GetWindowTextLength(dialogItem);
  if(textLength == 0)
    return {};

  auto text = std::string(textLength, '\0');
  auto charsCopied = GetWindowText(dialogItem, text.data(), textLength + 1);

  text.resize(charsCopied);

  return text;
}


// Same as std::stoi, but with custom exception description.
static int stringToInt(const std::string& value)
{
  try
  {
    return std::stoi(value);
  }
  catch(...)
  {
    throw std::runtime_error("Invalid integer value.");
  }
}


static lpvc::BitmapInfo makeBitmapInfo(const BITMAPINFOHEADER& bitmapInfo)
{
    // DIB height can be negative, but LPVC encoder requires bitmap height to be positive.
    return {
      static_cast<std::size_t>(bitmapInfo.biWidth),
      static_cast<std::size_t>(std::abs(bitmapInfo.biHeight))
    };
}


static INT_PTR CALLBACK configDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  try
  {
    static constexpr auto zstdCompressionLevelLabel = "Zstandard compression level: ";

    auto settings = reinterpret_cast<EncoderSettings*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch(message)
    {
      case WM_INITDIALOG:
      {
        // Save encoder settings in dialog's user data.
        SetWindowLongPtr(hwnd, GWLP_USERDATA, lParam);
        settings = reinterpret_cast<EncoderSettings*>(lParam);

        // Initialize controls with current settings.
        auto keyFrameInterval = std::to_string(settings->keyFrameInterval);
        auto zstdWorkerCount = std::to_string(settings->settings.zstdWorkerCount);
        auto zstdCompressionLevel = zstdCompressionLevelLabel + std::to_string(settings->settings.zstdCompressionLevel);

        CheckDlgButton(hwnd, IDC_LPVC_IGNORE_KEY_FRAME_REQUESTS, settings->ignoreKeyFrameRequests ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_LPVC_FORCE_KEY_FRAMES, settings->forceKeyFrames ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_LPVC_USE_PALETTE, settings->settings.usePalette ? BST_CHECKED : BST_UNCHECKED);

        SetDlgItemText(hwnd, IDC_LPVC_FORCE_KEY_FRAMES_INTERVAL, keyFrameInterval.c_str());
        SetDlgItemText(hwnd, IDC_LPVC_ZSTD_WORKER_COUNT, zstdWorkerCount.c_str());
        SetDlgItemText(hwnd, IDC_LPVC_ZSTD_COMPRESSION_LEVEL_TEXT, zstdCompressionLevel.c_str());

        EnableWindow(GetDlgItem(hwnd, IDC_LPVC_FORCE_KEY_FRAMES_INTERVAL), IsDlgButtonChecked(hwnd, IDC_LPVC_FORCE_KEY_FRAMES));

        SendDlgItemMessage(hwnd, IDC_LPVC_ZSTD_COMPRESSION_LEVEL_SLIDER, TBM_SETRANGEMIN, FALSE, 1);
        SendDlgItemMessage(hwnd, IDC_LPVC_ZSTD_COMPRESSION_LEVEL_SLIDER, TBM_SETRANGEMAX, FALSE, ZSTD_maxCLevel());
        SendDlgItemMessage(hwnd, IDC_LPVC_ZSTD_COMPRESSION_LEVEL_SLIDER, TBM_SETPOS, TRUE, settings->settings.zstdCompressionLevel);

        return TRUE;
      }

      case WM_HSCROLL:
      {
        settings->settings.zstdCompressionLevel = static_cast<int>(SendDlgItemMessage(hwnd, IDC_LPVC_ZSTD_COMPRESSION_LEVEL_SLIDER, TBM_GETPOS, 0, 0));

        auto zstdCompressionLevel = zstdCompressionLevelLabel + std::to_string(settings->settings.zstdCompressionLevel);
        SetDlgItemText(hwnd, IDC_LPVC_ZSTD_COMPRESSION_LEVEL_TEXT, zstdCompressionLevel.c_str());

        return 0;
      }

      case WM_COMMAND:
      {
        switch(LOWORD(wParam))
        {
          case IDC_LPVC_FORCE_KEY_FRAMES:
          {
            EnableWindow(GetDlgItem(hwnd, IDC_LPVC_FORCE_KEY_FRAMES_INTERVAL), IsDlgButtonChecked(hwnd, IDC_LPVC_FORCE_KEY_FRAMES));

            return 0;
          }

          case IDOK:
          {
            auto stringToPositiveInt = [](const std::string& string)
            {
              auto result = stringToInt(string);

              if(result < 1)
                throw std::invalid_argument("Positive integer value required.");

              return static_cast<unsigned int>(result);
            };

            settings->ignoreKeyFrameRequests = (IsDlgButtonChecked(hwnd, IDC_LPVC_IGNORE_KEY_FRAME_REQUESTS) == BST_CHECKED);
            settings->forceKeyFrames = (IsDlgButtonChecked(hwnd, IDC_LPVC_FORCE_KEY_FRAMES) == BST_CHECKED);
            settings->settings.usePalette = (IsDlgButtonChecked(hwnd, IDC_LPVC_USE_PALETTE) == BST_CHECKED);
            settings->keyFrameInterval = stringToPositiveInt(dialogItemText(hwnd, IDC_LPVC_FORCE_KEY_FRAMES_INTERVAL));
            settings->settings.zstdWorkerCount = stringToPositiveInt(dialogItemText(hwnd, IDC_LPVC_ZSTD_WORKER_COUNT));

            EndDialog(hwnd, wParam);

            return 0;
          }

          case IDCANCEL:
          {
            EndDialog(hwnd, wParam);

            return 0;
          }
        }
      }
    }
  }
  catch(std::exception& e)
  {
    MessageBox(hwnd, e.what(), "Error", MB_OK | MB_ICONERROR);
  }
  catch(...)
  {
  }

  return 0;
}


class LPVC final
{
public:
  LPVC() = default;

  LRESULT encoderConfig(LPARAM param)
  {
    if(param == -1)
      return ICERR_OK;
  
    auto encoderSettingsCopy = encoderSettings_;

    if(DialogBoxParam(lpvcDllInstance, MAKEINTRESOURCE(IDD_CONFIG_DIALOG), (HWND)param, configDialogProc, (LPARAM)&encoderSettingsCopy) == IDOK)
      encoderSettings_ = encoderSettingsCopy;

    return ICERR_OK;
  }

  LRESULT encoderGetState(LPVOID buffer, DWORD bufferSize)
  {
    if(buffer)
    {
      if(bufferSize >= sizeof(EncoderSettings))
      {
        std::memcpy(buffer, &encoderSettings_, sizeof(encoderSettings_));
        return ICERR_OK;
      }
      else
      {
        return ICERR_BADSIZE;
      }
    }
    else
    {
      return sizeof(EncoderSettings);
    }
  }

  LRESULT encoderSetState(LPVOID buffer, DWORD bufferSize)
  {
    assert(bufferSize == sizeof(encoderSettings_));

    if(buffer)
      std::memcpy(&encoderSettings_, buffer, sizeof(encoderSettings_));
    else
      encoderSettings_ = {};

    return bufferSize;
  }

  LRESULT createEncoder(BITMAPINFO* inputFormat)
  {
    try
    {
      auto bitmapInfo = makeBitmapInfo(inputFormat->bmiHeader);

      encoder_ = std::make_unique<lpvc::Encoder>(bitmapInfo, encoderSettings_.settings);
      frameCountSinceLastKeyFrame_ = 0;
    }
    catch(...)
    {
      return ICERR_ERROR;
    }

    return ICERR_OK;
  }

  LRESULT destroyEncoder()
  {
    encoder_.reset();

    return ICERR_OK;
  }

  LRESULT createDecoder(BITMAPINFO* inputFormat)
  {
    try
    {
      auto bitmapInfo = makeBitmapInfo(inputFormat->bmiHeader);

      decoder_ = std::make_unique<lpvc::Decoder>(bitmapInfo);
    }
    catch(...)
    {
      return ICERR_ERROR;
    }

    return ICERR_OK;
  }

  LRESULT destroyDecoder()
  {
    decoder_.reset();

    return ICERR_OK;
  }

  LRESULT encode(ICCOMPRESS* compressInfo)
  {
    *compressInfo->lpbiOutput = *compressInfo->lpbiInput;
    compressInfo->lpbiOutput->biCompression = FOURCC_LPVC;

    if(compressInfo->lpckid)
      *compressInfo->lpckid = FOURCC_LPVC;

    bool keyFrame = (compressInfo->dwFlags & ICCOMPRESS_KEYFRAME) && !encoderSettings_.ignoreKeyFrameRequests;

    if(encoderSettings_.forceKeyFrames &&
       frameCountSinceLastKeyFrame_ == encoderSettings_.keyFrameInterval - 1)
    {
      frameCountSinceLastKeyFrame_ = 0;
      keyFrame = true;
    }

    auto result = encoder_->encode(
      DIBConstIterator(compressInfo->lpbiInput->biWidth, compressInfo->lpbiInput->biHeight, reinterpret_cast<const std::byte*>(compressInfo->lpInput)),
      reinterpret_cast<std::byte*>(compressInfo->lpOutput),
      keyFrame
    );

    if(result.keyFrame)
      *compressInfo->lpdwFlags = AVIIF_KEYFRAME;
    else
      *compressInfo->lpdwFlags = 0;

    compressInfo->lpbiOutput->biSizeImage = static_cast<DWORD>(result.bytesWritten);

    ++frameCountSinceLastKeyFrame_;

    return ICERR_OK;
  }

  LRESULT decode(ICDECOMPRESS* decompressInfo)
  {
    decoder_->decode(
      reinterpret_cast<const std::byte*>(decompressInfo->lpInput),
      static_cast<std::size_t>(decompressInfo->lpbiInput->biSizeImage),
      DIBIterator(decompressInfo->lpbiOutput->biWidth, decompressInfo->lpbiOutput->biHeight, reinterpret_cast<std::byte*>(decompressInfo->lpOutput))
    );

    return ICERR_OK;
  }

private:
  EncoderSettings encoderSettings_;
  std::size_t frameCountSinceLastKeyFrame_ = 0;
  std::unique_ptr<lpvc::Encoder> encoder_;

  std::unique_ptr<lpvc::Decoder> decoder_;
};


static LRESULT openLPVC(ICOPEN* icOpen)
{
  if(icOpen &&
     icOpen->fccType != ICTYPE_VIDEO)
  {
    return 0;
  }

  auto lpvc = new (std::nothrow) LPVC();

  if(icOpen)
    icOpen->dwError = (lpvc ? ICERR_OK : ICERR_MEMORY);

  return reinterpret_cast<LRESULT>(lpvc);
}


static LRESULT closeLPVC(LPVC* lpvc)
{
  delete lpvc;

  return 1;
}


static INT_PTR CALLBACK aboutDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch(message)
  {
    case WM_COMMAND:
    {
      switch(LOWORD(wParam))
      {
        case IDOK:
        case IDCANCEL:
        {
          EndDialog(hwnd, wParam);
          return 0;
        }
      }
    }
  }

  return 0;
}


static LRESULT icmAbout(LPARAM param)
{
  if(param == -1)
    return ICERR_OK;

  DialogBox(lpvcDllInstance, MAKEINTRESOURCE(IDD_ABOUT_DIALOG), (HWND)param, aboutDialogProc);

  return ICERR_OK;
}


static LRESULT icmGetInfo(ICINFO* icInfo)
{
  icInfo->dwSize = sizeof(ICINFO);
  icInfo->fccType = ICTYPE_VIDEO;
  icInfo->fccHandler = FOURCC_LPVC;
  icInfo->dwFlags = VIDCF_FASTTEMPORALC | VIDCF_FASTTEMPORALD;
  icInfo->dwVersion = lpvc::version();
  icInfo->dwVersionICM = ICVERSION;
  std::wcsncpy(icInfo->szName, L"LPVC", std::size(icInfo->szName) - 1);
  std::wcsncpy(icInfo->szDescription, L"Longplay Video Codec", std::size(icInfo->szDescription) - 1);
  std::wcsncpy(icInfo->szDriver, L"", std::size(icInfo->szDriver) - 1);

  return sizeof(ICINFO);
}


static LRESULT icmCompressQuery(BITMAPINFO* inputFormat, BITMAPINFO* outputFormat)
{
  if(inputFormat->bmiHeader.biBitCount == 24 &&
     inputFormat->bmiHeader.biCompression == BI_RGB)
  {
    if(!outputFormat)
      return ICERR_OK;

    if(outputFormat->bmiHeader.biBitCount == 24 &&
       outputFormat->bmiHeader.biCompression == FOURCC_LPVC)
    {
      return ICERR_OK;
    }
  }

  return ICERR_BADFORMAT;
}


static LRESULT icmCompressGetFormat(BITMAPINFO* inputFormat, BITMAPINFO* outputFormat)
{
  if(!outputFormat)
    return sizeof(BITMAPINFO);

  *outputFormat = *inputFormat;
  outputFormat->bmiHeader.biBitCount = 24;
  outputFormat->bmiHeader.biCompression = FOURCC_LPVC;

  return ICERR_OK;
}


static LRESULT icmDecompressQuery(BITMAPINFO* inputFormat, BITMAPINFO* outputFormat)
{
  if(inputFormat->bmiHeader.biBitCount == 24 &&
     inputFormat->bmiHeader.biCompression == FOURCC_LPVC)
  {
    if(!outputFormat)
      return ICERR_OK;

    if(outputFormat->bmiHeader.biBitCount == 24 &&
       outputFormat->bmiHeader.biCompression == BI_RGB)
    {
      return ICERR_OK;
    }
  }

  return ICERR_BADFORMAT;
}


static LRESULT icmDecompressGetFormat(BITMAPINFO* inputFormat, BITMAPINFO* outputFormat)
{
  if(!outputFormat)
    return sizeof(BITMAPINFO);

  *outputFormat = *inputFormat;
  outputFormat->bmiHeader.biBitCount = 24;
  outputFormat->bmiHeader.biCompression = BI_RGB;

  return ICERR_OK;
}


static LRESULT icmCompressGetSize(BITMAPINFO* inputFormat)
{
  return inputFormat->bmiHeader.biSizeImage;
}


extern "C" __declspec(dllexport) LRESULT CALLBACK DriverProc(DWORD_PTR driverId, HDRVR driverHandle, UINT message, LPARAM param1, LPARAM param2)
{
  auto lpvc = reinterpret_cast<LPVC*>(driverId);

  switch(message)
  {
    // Driver messages
    case DRV_LOAD:
      return 1;

    case DRV_FREE:
      return 1;

    case DRV_OPEN:
      return openLPVC((ICOPEN*)param2);

    case DRV_CLOSE:
      return closeLPVC(lpvc);

    case DRV_QUERYCONFIGURE:
      return 0;

    case DRV_CONFIGURE:
      return DRVCNF_CANCEL;

    case DRV_INSTALL:
      return DRVCNF_OK;

    case DRV_REMOVE:
      return DRVCNF_OK;

    // VFW messages
    case ICM_ABOUT:
      return icmAbout(param1);

    case ICM_CONFIGURE:
      return lpvc->encoderConfig(param1);

    case ICM_GETSTATE:
      return lpvc->encoderGetState((LPVOID)param1, (DWORD)param2);

    case ICM_SETSTATE:
      return lpvc->encoderSetState((LPVOID)param1, (DWORD)param2);

    case ICM_GETINFO:
      return icmGetInfo((ICINFO*)param1);

    case ICM_GETDEFAULTQUALITY:
      return ICERR_UNSUPPORTED;

    // Compression
    case ICM_COMPRESS_QUERY:
      return icmCompressQuery((BITMAPINFO*)param1, (BITMAPINFO*)param2);

    case ICM_COMPRESS_BEGIN:
      return lpvc->createEncoder((BITMAPINFO*)param1);

    case ICM_COMPRESS_GET_FORMAT:
      return icmCompressGetFormat((BITMAPINFO*)param1, (BITMAPINFO*)param2);

    case ICM_COMPRESS_GET_SIZE:
      return icmCompressGetSize((BITMAPINFO*)param1);

    case ICM_COMPRESS:
      return lpvc->encode((ICCOMPRESS*)param1);

    case ICM_COMPRESS_END:
      return lpvc->destroyEncoder();

    // Decompression
    case ICM_DECOMPRESS_QUERY:
      return icmDecompressQuery((BITMAPINFO*)param1, (BITMAPINFO*)param2);

    case ICM_DECOMPRESS_BEGIN:
      return lpvc->createDecoder((BITMAPINFO*)param1);

    case ICM_DECOMPRESS_GET_FORMAT:
      return icmDecompressGetFormat((BITMAPINFO*)param1, (BITMAPINFO*)param2);

    case ICM_DECOMPRESS:
      return lpvc->decode((ICDECOMPRESS*)param1);

    case ICM_DECOMPRESS_END:
      return lpvc->destroyDecoder();
  }

  if(message < DRV_USER)
    return DefDriverProc(driverId, driverHandle, message, param1, param2);
  else
    return ICERR_UNSUPPORTED;
}


BOOL WINAPI DllMain(HINSTANCE dllInstance, DWORD reason, LPVOID reserved)
{
  lpvcDllInstance = dllInstance;

  return TRUE;
}
