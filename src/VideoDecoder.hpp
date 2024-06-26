#pragma once
extern "C"
{
#include <libavutil/pixfmt.h>
}
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

// TODO way to build Coollab without FFMPEG, and add it to COOLLAB_REQUIRE_ALL_FEATURES
// TODO test that the linux and mac exe work even on a machine that has no ffmpeg installed
// TODO and check that they have all the non-lgpl algorithms
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVStream;
struct AVPacket;
struct SwsContext;

namespace ffmpeg {

struct Frame {
    uint8_t* data{};   /// Pointer to all the pixels, in the color space that you requested when constructing the VideoDecoder. If there is some alpha it will always be straight alpha, never premultiplied.
    int      width{};  /// In pixels
    int      height{}; /// In pixels
    bool     is_different_from_previous_frame{};
    bool     is_last_frame{}; /// If this is the last frame in the file, we will keep returning it, but you can might want to do something else (like displaying nothing, or seeking back to the beginning of the file).
};

enum class SeekMode {
    Exact, /// Returns the exact requested frame.
    Fast,  /// Returns the keyframe just before the requested frame, and then other calls to get_frame_at() will read a few frames quickly, so that we eventually reach the requested frame. Guarantees that get_frame_at() will never take too long to return.
};

class VideoDecoder {
public:
    /// Throws a `std::runtime_error` if the creation fails (file not found / invalid video file / format not supported, etc.)
    /// `pixel_format` is the format of the frames you will receive. For example you can set it to `AV_PIX_FMT_RGBA` to get an RGBA image with 8 bits per channel. If there is some alpha it will always be straight alpha, never premultiplied.
    explicit VideoDecoder(std::filesystem::path const& path, AVPixelFormat pixel_format);
    ~VideoDecoder();
    VideoDecoder(VideoDecoder const&)                        = delete; ///
    auto operator=(VideoDecoder const&) -> VideoDecoder&     = delete; /// Not allowed to copy nor move the class (because we spawn a thread with a reference to this object)
    VideoDecoder(VideoDecoder&&) noexcept                    = delete; /// If you need to move it then heap-allocate it, typically in a std::unique_ptr
    auto operator=(VideoDecoder&&) noexcept -> VideoDecoder& = delete; ///

    /// The returned frame will be valid until the next call to get_frame_at() (or until the VideoDecoder is destroyed)
    /// Might return nullopt if we cannot read any frame from the file (which shouldn't happen, unless your video file is corrupted)
    auto get_frame_at(double time_in_seconds, SeekMode) -> std::optional<Frame>;

    /// Total duration of the video.
    [[nodiscard]] auto duration_in_seconds() const -> double;

    /// Detailed info about the video, its encoding, etc.
    [[nodiscard]] auto detailed_info() const -> std::string const& { return _detailed_info; }

private:
    void convert_frame_to_desired_color_space(AVFrame const&) const;

    [[nodiscard]] auto video_stream() const -> AVStream const&;

    /// Throws on error
    /// Returns true iff decoding actually completed and filled up the `frame`.
    [[nodiscard]] auto decode_next_frame_into(AVFrame* frame) -> bool;

    [[nodiscard]] auto get_frame_at_impl(double time_in_seconds, SeekMode) -> AVFrame const*;

    static void video_decoding_thread_job(VideoDecoder& This);
    void        process_packets_until(double time_in_seconds);

    [[nodiscard]] auto present_time(AVFrame const&) const -> double;
    [[nodiscard]] auto present_time(AVPacket const&) const -> double;

    [[nodiscard]] auto seeking_would_move_us_forward(double time_in_seconds) -> bool;

    [[nodiscard]] auto retrieve_detailed_info() const -> std::string;

    void               log_frame_decoding_error(std::string const&);
    void               log_frame_decoding_error(std::string const&, int err);
    [[nodiscard]] auto too_many_errors() const -> bool { return _error_count.load() >= 5; }

private:
    /// Always contains the last requested frame, + the frames that will come after that one
    class FramesQueue {
    public:
        FramesQueue();
        ~FramesQueue();
        FramesQueue(FramesQueue const&)                        = delete;
        auto operator=(FramesQueue const&) -> FramesQueue&     = delete;
        FramesQueue(FramesQueue&&) noexcept                    = delete;
        auto operator=(FramesQueue&&) noexcept -> FramesQueue& = delete;

        [[nodiscard]] auto size() -> size_t;
        [[nodiscard]] auto size_no_lock() -> size_t;
        [[nodiscard]] auto is_full() -> bool;
        [[nodiscard]] auto is_empty() -> bool;

        [[nodiscard]] auto first() -> AVFrame const&;
        [[nodiscard]] auto second() -> AVFrame const&;
        [[nodiscard]] auto get_frame_to_fill() -> AVFrame*;

        void push(AVFrame*);
        void pop();
        void clear();

        auto waiting_for_queue_to_fill_up() -> std::condition_variable& { return _waiting_for_push; }
        auto waiting_for_queue_to_empty_out() -> std::condition_variable& { return _waiting_for_pop; }

        auto mutex() -> std::mutex& { return _mutex; }

    private:
        std::vector<AVFrame*> _alive_frames{};
        std::vector<AVFrame*> _dead_frames{};
        std::mutex            _mutex{};

        std::condition_variable _waiting_for_push{};
        std::condition_variable _waiting_for_pop{};
    };

private:
    // Contexts
    AVFormatContext* _format_ctx{};
    AVFormatContext* _format_ctx_to_test_seeking{}; // Dummy context that we use to seek and check that a seek would actually bring us closer to the frame we want to reach (which is not the case when the closest keyframe to the frame we seek is before the frame we are currently decoding)
    AVCodecContext*  _decoder_ctx{};
    SwsContext*      _sws_ctx{};

    // Data
    AVFrame*    _desired_color_space_frame{};
    uint8_t*    _desired_color_space_buffer{};
    AVPacket*   _packet{};
    AVPacket*   _packet_to_test_seeking{}; // Dummy packet that we use to seek and check that a seek would actually bring us closer to the frame we want to reach (which is not the case when the closest keyframe to the frame we seek is before the frame we are currently decoding)
    FramesQueue _frames_queue{};

    // Thread
    std::thread       _video_decoding_thread{};
    std::atomic<bool> _wants_to_stop_video_decoding_thread{false};
    std::atomic<bool> _wants_to_pause_decoding_thread_asap{false};
    std::mutex        _decoding_context_mutex{};

    // Info
    int                   _video_stream_idx{};
    std::string           _detailed_info{};
    int64_t               _previous_pts{-99999};
    std::atomic<bool>     _has_reached_end_of_file{false};
    std::atomic<uint32_t> _error_count{0};
    std::optional<double> _seek_target{};
};

} // namespace ffmpeg