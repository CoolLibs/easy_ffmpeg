#include "glad/glad.h"
//
#include <glfw/include/GLFW/glfw3.h>
#include <imgui.h>
#include <cstdint>
#include <exception>
#include <fstream>
#include <quick_imgui/quick_imgui.hpp>
#include "easy_ffmpeg/easy_ffmpeg.hpp"
#include "exe_path/exe_path.h"
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

void check_equal(ffmpeg::Frame const& frame, std::filesystem::path const& path_to_expected_values)
{
    static constexpr size_t expected_width  = 256;
    static constexpr size_t expected_height = 144;
    CHECK(frame.width == expected_width);   // NOLINT(*avoid-do-while)
    CHECK(frame.height == expected_height); // NOLINT(*avoid-do-while)

    std::vector<uint8_t> expected_values;
    {
        auto file = std::ifstream{path_to_expected_values};
        auto line = std::string{};
        while (std::getline(file, line))
            expected_values.push_back(static_cast<uint8_t>(std::stoi(line)));
        REQUIRE(expected_values.size() == 4 * expected_width * expected_height); // NOLINT(*avoid-do-while)
    }

    for (size_t i = 0; i < 4 * static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height); ++i)
        REQUIRE(frame.data[i] == expected_values[i]); // NOLINT(*avoid-do-while, *pointer-arithmetic)
}

TEST_CASE("VideoDecoder")
{
    auto decoder = ffmpeg::VideoDecoder{exe_path::dir() / "test.gif", AV_PIX_FMT_RGBA};
    check_equal(*decoder.get_frame_at(0., ffmpeg::SeekMode::Exact), exe_path::dir() / "expected_frame_0.txt");
    check_equal(*decoder.get_frame_at(0.13, ffmpeg::SeekMode::Exact), exe_path::dir() / "expected_frame_3.txt");
    std::cout << decoder.detailed_info();
}

auto make_texture() -> GLuint
{
    GLuint textureID; // NOLINT(*init-variables)
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    return textureID;
}

auto main(int argc, char* argv[]) -> int // NOLINT(*cognitive-complexity)
{
    // av_log_set_level(AV_LOG_VERBOSE);
    {
        int const exit_code = doctest::Context{}.run(); // Run all unit tests

        bool const should_run_imgui_tests = argc < 2 || strcmp(argv[1], "-nogpu") != 0; // NOLINT(*-pointer-arithmetic)
        if (
            should_run_imgui_tests
            && exit_code == 0 // Only open the window if the tests passed; this makes it easier to notice when some tests fail
        )
        {
            ffmpeg::set_frame_decoding_error_callback([](std::string const& error_message) {
                std::cerr << error_message << "\n\n";
            });
            try
            {
                // A VideoDecoder is not allowed to be copied nor moved, so if you need those operations you need to heap-allocate the VideoDecoder. You should typically use a std::unique_ptr for that.
                auto decoder = std::make_unique<ffmpeg::VideoDecoder>(exe_path::dir() / "test.gif", AV_PIX_FMT_RGBA);

                GLuint                       texture_id; // NOLINT(*init-variables)
                quick_imgui::AverageTime     timer{};
                std::optional<double>        time_when_paused{};
                std::optional<ffmpeg::Frame> frame{};
                double                       time_offset{0.};
                quick_imgui::loop("easy_ffmpeg tests", [&]() {
                    if (!time_when_paused.has_value())
                    {
                        static bool first{true};
                        if (first)
                        {
                            first      = false;
                            texture_id = make_texture(); // Must be created after the glfw context is created
                            std::cout << decoder->detailed_info();
                            // glfwSwapInterval(0);
                        }
                        timer.start();
                        frame = decoder->get_frame_at(glfwGetTime() + time_offset, ffmpeg::SeekMode::Fast);
                        timer.stop();
                        if (!frame.has_value())
                        {
                            ImGui::Text("CANNOT READ ANY FRAMES FROM THE VIDEO");
                            return;
                        }
                        if (frame->is_last_frame)
                        {
                            glfwSetTime(0.); // Next frame we will start over at the beginning of the file
                            time_offset = 0.;
                        }

                        if (frame->is_different_from_previous_frame) // Optimisation: don't recreate the texture unless the frame has actually changed
                        {
                            glBindTexture(GL_TEXTURE_2D, texture_id);
                            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame->width, frame->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame->data);
                        }
                    }

                    ImGui::Begin("easy_ffmpeg tests");
                    ImGui::Text("Time: %.2f", glfwGetTime() + time_offset);
                    if (ImGui::Button("-10s"))
                        time_offset -= 10.;
                    ImGui::SameLine();
                    if (ImGui::Button("+10s"))
                        time_offset += 10.;
                    timer.imgui_plot();
                    ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<void*>(static_cast<uint64_t>(texture_id))), ImVec2{850.f * static_cast<float>(frame->width) / static_cast<float>(frame->height), 850.f}); // NOLINT(performance-no-int-to-ptr, *reinterpret-cast)
                    ImGui::End();

                    if (time_when_paused.has_value())
                        glfwSetTime(*time_when_paused);
                    if (ImGui::IsKeyPressed(ImGuiKey_Space))
                    {
                        if (time_when_paused.has_value())
                            time_when_paused.reset();
                        else
                            time_when_paused = glfwGetTime();
                    }
                });
            }
            catch (std::exception const& e)
            {
                std::cout << e.what() << '\n';
                throw;
            }
        }
        return exit_code;
    }
}
