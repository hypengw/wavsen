export module wavsen.ffi.ffmpeg;

export import :avutil;
export import :avcodec;
export import :avformat;
export import :swscale;
#if LITO_FEAT_AUDIO
export import :swresample;
#endif
