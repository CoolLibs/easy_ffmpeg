#pragma once
#include <filesystem>
extern "C"
{
#include <libavutil/frame.h> // AVFrame
}

struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVPacket;
enum AVPixelFormat;
enum AVMediaType;

namespace ffmpeg {

class VideoDecoder {
public:
    explicit VideoDecoder(std::filesystem::path const& path);
    ~VideoDecoder();
    VideoDecoder(VideoDecoder const&)                    = delete;
    auto operator=(VideoDecoder const&) -> VideoDecoder& = delete;
    VideoDecoder(VideoDecoder&&) noexcept;                    // TODO
    auto operator=(VideoDecoder&&) noexcept -> VideoDecoder&; // TODO

    void move_to_next_frame();
    auto current_frame() const -> AVFrame const&; // TODO take a desired format as param // TODO return our own Frame type, that only contains info we now are valid (like width and height that we copy from the other frame)
private:
    int      decode_packet();
    int      open_codec_context(int* stream_idx, AVCodecContext** dec_ctx, AVMediaType type);
    AVFrame* convertFrameToRGBA(AVFrame* frame, AVFrame* rgbaFrame) const;

private:
    // TODO use pimpl to store all of these ? A unique_ptr will make sure we never have a bug where we forgot to ass one member to the move constructor. And we will never crretae 1000s of videos, so the cost is negligeable
    // and also allows to easily cleanup during constructor

    // Contexts
    AVFormatContext* _format_ctx{};
    AVCodecContext*  _decoder_ctx{};

    // Data
    AVFrame*         frame      = NULL;
    mutable AVFrame* rgba_frame = NULL;
    mutable uint8_t* rgbaBuffer{};
    AVPacket*        _packet{};

    // Info
    int           width{};
    int           height{};
    AVPixelFormat pix_fmt;
    AVStream*     video_stream{};
    int           video_stream_idx{};
};

} // namespace ffmpeg