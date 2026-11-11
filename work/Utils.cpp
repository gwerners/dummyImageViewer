#include "Utils.h"
#include <avif/avif.h>
#include <jxl/decode.h>
#include <jxl/encode.h>  // JxlColorEncodingSetToSRGB
#include <stdio.h>
#include <stdlib.h>
#include <webp/decode.h>
#include <vector>
#include "Platform.h"

// Função que carrega uma imagem AVIF e retorna uma textura Raylib
Image LoadAVIF(const char* filename) {
  Image img = {0};

  std::vector<unsigned char> bytes = ReadFileBytes(filename);
  if (bytes.empty()) {
    TraceLog(LOG_ERROR, "Erro ao abrir %s", filename);
    return img;
  }
  const uint8_t* avifData = bytes.data();
  size_t fileSize = bytes.size();

  avifDecoder* decoder = avifDecoderCreate();
  decoder->maxThreads = 4;

  avifResult res = avifDecoderSetIOMemory(decoder, avifData, fileSize);
  if (res != AVIF_RESULT_OK) {
    TraceLog(LOG_ERROR, "Erro no avifDecoderSetIOMemory: %s",
             avifResultToString(res));
    goto cleanup;
  }

  res = avifDecoderParse(decoder);
  if (res != AVIF_RESULT_OK) {
    TraceLog(LOG_ERROR, "Erro ao fazer parse AVIF: %s",
             avifResultToString(res));
    goto cleanup;
  }

  res = avifDecoderNextImage(decoder);
  if (res != AVIF_RESULT_OK) {
    TraceLog(LOG_ERROR, "Erro ao decodificar imagem AVIF: %s",
             avifResultToString(res));
    goto cleanup;
  }

  avifRGBImage rgb;
  avifRGBImageSetDefaults(&rgb, decoder->image);
  rgb.format = AVIF_RGB_FORMAT_RGBA;
  rgb.depth = 8;
  rgb.rowBytes = rgb.width * 4;
  rgb.pixels = (uint8_t*)malloc((size_t)rgb.rowBytes * rgb.height);

  res = avifImageYUVToRGB(decoder->image, &rgb);
  if (res != AVIF_RESULT_OK) {
    TraceLog(LOG_ERROR, "Erro ao converter para RGB: %s",
             avifResultToString(res));
    free(rgb.pixels);
    goto cleanup;
  }

  img.data = rgb.pixels;
  img.width = rgb.width;
  img.height = rgb.height;
  img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  img.mipmaps = 1;

cleanup:
  avifDecoderDestroy(decoder);
  return img;
}
Image LoadWEBP(const char* filename) {
  Image image = {0};

  std::vector<unsigned char> fileData = ReadFileBytes(filename);
  if (fileData.empty())
    return image;
  long size = (long)fileData.size();

  int width, height;
  uint8_t* rgba = WebPDecodeRGBA(fileData.data(), size, &width, &height);

  if (!rgba)
    return image;

  image.data = rgba;
  image.width = width;
  image.height = height;
  image.mipmaps = 1;
  image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;

  return image;
}

// JPEG XL: decodifica o primeiro frame para RGBA8 (sRGB).
Image LoadJXL(const char* filename) {
  Image image = {0};
  std::vector<unsigned char> bytes = ReadFileBytes(filename);
  if (bytes.empty()) {
    TraceLog(LOG_ERROR, "Erro ao abrir %s", filename);
    return image;
  }

  JxlDecoder* dec = JxlDecoderCreate(nullptr);
  if (!dec)
    return image;
  if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) !=
      JXL_DEC_SUCCESS) {
    JxlDecoderDestroy(dec);
    return image;
  }
  JxlDecoderSetInput(dec, bytes.data(), bytes.size());
  JxlDecoderCloseInput(dec);

  JxlPixelFormat format = {4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
  uint32_t w = 0, h = 0;
  uint8_t* pixels = nullptr;
  bool ok = false;

  for (;;) {
    JxlDecoderStatus st = JxlDecoderProcessInput(dec);
    if (st == JXL_DEC_BASIC_INFO) {
      JxlBasicInfo info;
      if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS)
        break;
      w = info.xsize;
      h = info.ysize;
    } else if (st == JXL_DEC_COLOR_ENCODING) {
      // pede saida em sRGB (senao vem no perfil original da imagem)
      JxlColorEncoding enc;
      JxlColorEncodingSetToSRGB(&enc, /*is_gray=*/JXL_FALSE);
      JxlDecoderSetOutputColorProfile(dec, &enc, nullptr, 0);
    } else if (st == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
      size_t size = 0;
      if (JxlDecoderImageOutBufferSize(dec, &format, &size) != JXL_DEC_SUCCESS)
        break;
      pixels = (uint8_t*)malloc(size);
      if (!pixels || JxlDecoderSetImageOutBuffer(dec, &format, pixels, size) !=
                         JXL_DEC_SUCCESS)
        break;
    } else if (st == JXL_DEC_FULL_IMAGE) {
      ok = true;  // primeiro frame completo
      break;
    } else if (st == JXL_DEC_SUCCESS) {
      ok = pixels != nullptr;
      break;
    } else {  // erro / precisa de mais dados
      TraceLog(LOG_ERROR, "JXL: falha ao decodificar %s (status %d)", filename,
               (int)st);
      break;
    }
  }
  JxlDecoderDestroy(dec);

  if (!ok || !pixels || w == 0 || h == 0) {
    free(pixels);
    return image;
  }
  image.data = pixels;
  image.width = (int)w;
  image.height = (int)h;
  image.mipmaps = 1;
  image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  return image;
}
