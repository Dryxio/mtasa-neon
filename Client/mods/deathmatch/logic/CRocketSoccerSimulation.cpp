/*****************************************************************************
 * PROJECT: Multi Theft Auto: Neon
 * LICENSE: See LICENSE in the top level directory
 *****************************************************************************/
#include "CRocketSoccerSimulation.h"
#include <rocketweb/Kernel.h>
#include <rocketsim/src/RocketSim.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <zlib.h>

namespace RocketSoccer
{
    namespace
    {
        constexpr double Tick = 1.0 / 120.0;
        void             Check(NeonRocketKernel* kernel, int ok)
        {
            if (!ok)
                throw std::runtime_error(nrk_error(kernel)[0] ? nrk_error(kernel) : "Reference kernel operation failed");
        }
        void ReadState(NeonRocketKernel* kernel, std::array<float, 1024>& scratch, std::vector<float>& state)
        {
            const int count = nrk_state(kernel, scratch.data(), scratch.size());
            Check(kernel, count);
            state.assign(scratch.begin(), scratch.begin() + count);
        }
        Body Interpolate(const float* previous, const float* current, float fraction)
        {
            auto              vec = [](const float* p) { return RocketSim::Vec(p[0], p[1], p[2]); };
            auto              array = [](RocketSim::Vec p) { return std::array<float, 3>{p.x, p.y, p.z}; };
            RocketSim::RotMat a(vec(previous + 3), vec(previous + 6), vec(previous + 9));
            RocketSim::RotMat b(vec(current + 3), vec(current + 6), vec(current + 9));
            btQuaternion      qa, qb;
            static_cast<btMatrix3x3>(a).getRotation(qa);
            static_cast<btMatrix3x3>(b).getRotation(qb);
            RocketSim::RotMat rotation(btMatrix3x3(qa.slerp(qb, fraction).normalized()));
            return {array(vec(previous) + (vec(current) - vec(previous)) * fraction), array(rotation.forward), array(rotation.right), array(rotation.up),
                    array(vec(current + 12))};
        }
    }
    const std::vector<Triangle>& GetArenaMesh()
    {
        static const auto triangles = []
        {
            std::vector<Triangle> result;
            size_t                size;
            const unsigned char*  data = nrk_collision_data(&size);
            size_t                cursor = 0;
            // CMFs store indexed vertices in Bullet metres. The same source feeds
            // the physics kernel and inspection API, avoiding two arena definitions.
            while (cursor < size)
            {
                auto integer = [&](size_t offset)
                {
                    int value;
                    std::memcpy(&value, data + offset, 4);
                    return value;
                };
                const int    count = integer(cursor), vertices = integer(cursor + 4);
                const size_t vertexStart = cursor + 8 + count * 12;
                for (int i = 0; i < count; ++i)
                {
                    Triangle triangle{};
                    for (int corner = 0; corner < 3; ++corner)
                    {
                        int index = integer(cursor + 8 + i * 12 + corner * 4);
                        for (int axis = 0; axis < 3; ++axis)
                        {
                            float value;
                            std::memcpy(&value, data + vertexStart + index * 12 + axis * 4, 4);
                            triangle.vertices[corner * 3 + axis] = value * 50;
                        }
                    }
                    result.push_back(triangle);
                }
                cursor = vertexStart + vertices * 12;
            }
            return result;
        }();
        return triangles;
    }
    struct Simulation::Impl
    {
        std::unique_ptr<NeonRocketKernel, decltype(&nrk_destroy)> kernel{nrk_create(), nrk_destroy};
        std::vector<float>                                        previous, current;
        std::array<float, 1024>                                   stateScratch{};
        double                                                    accumulator{};
        unsigned long long                                        ticks{};
        bool                                                      unlimited{true};
        // Camera calls mutate WASM memory too. Reconciliation uses its own world
        // so authoritative restores cannot reset the player's camera smoothing.
        std::unique_ptr<NeonRocketKernel, decltype(&nrk_destroy)> prediction{nullptr, nrk_destroy};
        std::vector<float>                                        predictedPrevious, predictedCurrent;
    };
    Simulation::Simulation() : m_Impl(std::make_unique<Impl>())
    {
        Check(m_Impl->kernel.get(), m_Impl->kernel != nullptr);
        Configure(false, false);
    }
    Simulation::~Simulation() = default;
    void Simulation::Configure(bool flat, bool opponent)
    {
        auto& scene = *m_Impl;
        Check(scene.kernel.get(), nrk_command(scene.kernel.get(), 0, flat, opponent));
        Reset();
    }
    void Simulation::Reset(int kickoff)
    {
        auto& scene = *m_Impl;
        Check(scene.kernel.get(), nrk_command(scene.kernel.get(), 2, kickoff, 0));
        ReadState(scene.kernel.get(), scene.stateScratch, scene.current);
        scene.previous = scene.current;
        scene.accumulator = 0;
        ResetCamera();
    }
    void Simulation::SetControls(int car, const Controls& controls)
    {
        auto        axis = [](float v) { return std::isfinite(v) ? std::clamp(v, -1.f, 1.f) : 0.f; };
        const float values[8] = {axis(controls.throttle), axis(controls.steer),      axis(controls.pitch),       axis(controls.yaw),
                                 axis(controls.roll),     controls.jump ? 1.f : 0.f, controls.boost ? 1.f : 0.f, controls.handbrake ? 1.f : 0.f};
        Check(m_Impl->kernel.get(), nrk_controls(m_Impl->kernel.get(), car, values));
    }
    Snapshot Simulation::Step(float seconds, const Controls& controls)
    {
        auto& scene = *m_Impl;
        if (std::isfinite(seconds))
            scene.accumulator += std::clamp(static_cast<double>(seconds), 0.0, 0.25);
        const int due = static_cast<int>(std::floor(scene.accumulator / Tick));
        scene.accumulator = std::max(0.0, scene.accumulator - due * Tick);
        if (due > 0)
            SetControls(0, controls);
        const int steps = std::min(due, 12);
        if (steps == 1)
        {
            scene.previous = scene.current;
            Check(scene.kernel.get(), nrk_command(scene.kernel.get(), 1, 1, 0));
            ReadState(scene.kernel.get(), scene.stateScratch, scene.current);
            ++scene.ticks;
        }
        else if (steps > 1)
        {
            // Only the final two tick states are needed for render interpolation.
            // Avoid copying the full published state after every catch-up substep.
            for (int i = 0; i < steps - 1; ++i)
            {
                Check(scene.kernel.get(), nrk_command(scene.kernel.get(), 1, 1, 0));
                ++scene.ticks;
            }
            ReadState(scene.kernel.get(), scene.stateScratch, scene.previous);
            Check(scene.kernel.get(), nrk_command(scene.kernel.get(), 1, 1, 0));
            ReadState(scene.kernel.get(), scene.stateScratch, scene.current);
            ++scene.ticks;
        }
        Snapshot    result{};
        const float fraction = static_cast<float>(scene.accumulator / Tick);
        result.ball = Interpolate(scene.previous.data() + 4, scene.current.data() + 4, fraction);
        for (int i = 0; i < static_cast<int>(scene.current[2]); ++i)
        {
            int          offset = 22 + i * 51;
            const float* old = scene.previous.data() + offset;
            const float* now = scene.current.data() + offset;
            // A respawn must snap rather than interpolate across the whole field.
            result.cars.push_back(Interpolate(old[21] == 1 && now[21] == 0 ? now : old, now, fraction));
        }
        result.car = result.cars.at(0);
        result.grounded = scene.current[22 + 19] == 1;
        result.boosting = scene.current[22 + 23] == 1;
        result.goalTeam = static_cast<int>(scene.current[1]);
        result.ticks = scene.ticks;
        result.raw = scene.current;
        result.ballOnGround = nrk_ball_grounded(scene.kernel.get()) == 1;
        return result;
    }
    Snapshot Simulation::Predict(std::string_view checkpoint, const std::vector<std::array<float, 8>>& frames, int slot, const std::array<float, 8>& opponent,
                                 float fraction)
    {
        auto& scene = *m_Impl;
        if (slot < 0 || slot > 1 || frames.size() > 120 || !std::isfinite(fraction) || fraction < 0 || fraction > 1 || checkpoint.size() > 512 * 1024)
            throw std::runtime_error("Invalid Rocket prediction arguments");
        if (!scene.prediction)
        {
            scene.prediction.reset(nrk_create());
            auto* kernel = scene.prediction.get();
            Check(kernel,
                  kernel && nrk_command(kernel, 0, 0, 1) && nrk_command(kernel, 5, 0, 0) && nrk_command(kernel, 2, 0, 0) && nrk_checkpoint_prepare(kernel));
            ReadState(kernel, scene.stateScratch, scene.predictedCurrent);
            scene.predictedPrevious = scene.predictedCurrent;
        }
        auto* kernel = scene.prediction.get();
        if (!checkpoint.empty())
        {
            std::vector<unsigned char> raw(512 * 1024);
            uLongf                     length = static_cast<uLongf>(raw.size());
            if (uncompress(raw.data(), &length, reinterpret_cast<const Bytef*>(checkpoint.data()), static_cast<uLong>(checkpoint.size())) != Z_OK ||
                !nrk_checkpoint_restore(kernel, raw.data(), length))
                throw std::runtime_error("Incompatible or corrupt Rocket prediction checkpoint");
            ReadState(kernel, scene.stateScratch, scene.predictedCurrent);
            scene.predictedPrevious = scene.predictedCurrent;
        }
        if (frames.size() == 1)
        {
            scene.predictedPrevious = scene.predictedCurrent;
            const auto& controls = frames.front();
            Check(kernel, nrk_controls(kernel, slot, controls.data()) && nrk_controls(kernel, 1 - slot, opponent.data()) && nrk_command(kernel, 1, 1, 0));
            ReadState(kernel, scene.stateScratch, scene.predictedCurrent);
        }
        else if (!frames.empty())
        {
            // Reconciliation only interpolates between the last two predicted
            // ticks. Intermediate state readbacks add work without changing it.
            for (size_t i = 0; i + 1 < frames.size(); ++i)
            {
                const auto& controls = frames[i];
                Check(kernel, nrk_controls(kernel, slot, controls.data()) && nrk_controls(kernel, 1 - slot, opponent.data()) && nrk_command(kernel, 1, 1, 0));
            }
            ReadState(kernel, scene.stateScratch, scene.predictedPrevious);
            const auto& controls = frames.back();
            Check(kernel, nrk_controls(kernel, slot, controls.data()) && nrk_controls(kernel, 1 - slot, opponent.data()) && nrk_command(kernel, 1, 1, 0));
            ReadState(kernel, scene.stateScratch, scene.predictedCurrent);
        }
        Snapshot    result{};
        const auto& now = scene.predictedCurrent;
        const auto& old = scene.predictedPrevious;
        result.ball = Interpolate(old.data() + 4, now.data() + 4, fraction);
        for (int car = 0; car < static_cast<int>(now[2]); ++car)
        {
            int offset = 22 + car * 51;
            result.cars.push_back(
                Interpolate(old[offset + 21] == 1 && now[offset + 21] == 0 ? now.data() + offset : old.data() + offset, now.data() + offset, fraction));
        }
        result.car = result.cars.at(slot);
        result.grounded = now[22 + slot * 51 + 19] == 1;
        result.boosting = now[22 + slot * 51 + 23] == 1;
        result.goalTeam = static_cast<int>(now[1]);
        result.ticks = static_cast<unsigned long long>(now[0]);
        result.raw = now;
        result.ballOnGround = nrk_ball_grounded(kernel) == 1;
        return result;
    }
    std::string Simulation::Rebase(std::string_view checkpoint)
    {
        auto* kernel = m_Impl->prediction.get();
        if (!kernel || checkpoint.empty() || checkpoint.size() > 512 * 1024)
            throw std::runtime_error("Invalid Rocket reference checkpoint");
        std::vector<unsigned char> raw(512 * 1024);
        uLongf                     length = static_cast<uLongf>(raw.size());
        if (uncompress(raw.data(), &length, reinterpret_cast<const Bytef*>(checkpoint.data()), static_cast<uLong>(checkpoint.size())) != Z_OK ||
            !nrk_checkpoint_rebase(kernel, raw.data(), length))
            throw std::runtime_error("Incompatible Rocket reference checkpoint");
        // Rebase leaves the predicted world untouched. The normalized empty
        // checkpoint restores that authoritative state on the next render tick.
        unsigned char compressed[64];
        uLongf        size = sizeof(compressed);
        if (compress2(compressed, &size, raw.data(), 24, Z_BEST_SPEED) != Z_OK)
            throw std::runtime_error("Cannot normalize Rocket reference checkpoint");
        return {reinterpret_cast<const char*>(compressed), size};
    }
    void Simulation::SetUnlimitedBoost(bool value)
    {
        Check(m_Impl->kernel.get(), nrk_command(m_Impl->kernel.get(), 5, value, 0));
        m_Impl->unlimited = value;
    }
    bool Simulation::ControlBall(int action)
    {
        auto& scene = *m_Impl;
        if (action < 0 || action > 3 || !nrk_command(scene.kernel.get(), 4, 0, action))
            return false;
        ReadState(scene.kernel.get(), scene.stateScratch, scene.current);
        std::copy_n(scene.current.begin() + 4, 18, scene.previous.begin() + 4);
        return true;
    }
    int Simulation::PollGoal()
    {
        const int goal = static_cast<int>(m_Impl->current[1]);
        Check(m_Impl->kernel.get(), nrk_command(m_Impl->kernel.get(), 3, 0, 0));
        m_Impl->current[1] = 0;
        return goal;
    }
    std::vector<float> Simulation::Pads()
    {
        std::vector<float> pads(256);
        int                count = nrk_pads(m_Impl->kernel.get(), pads.data(), pads.size());
        Check(m_Impl->kernel.get(), count);
        pads.resize(count);
        return pads;
    }
    std::array<double, 42> Simulation::Camera(const std::array<double, 28>& input)
    {
        std::array<double, 42> output{};
        Check(m_Impl->kernel.get(), nrk_camera(m_Impl->kernel.get(), input.data(), output.data()));
        return output;
    }
    void Simulation::ResetCamera()
    {
        Check(m_Impl->kernel.get(), nrk_command(m_Impl->kernel.get(), 6, 0, 0));
    }
}
