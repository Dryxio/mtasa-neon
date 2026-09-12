/*****************************************************************************
 * PROJECT: Multi Theft Auto: Neon
 * LICENSE: See LICENSE in the top level directory
 *****************************************************************************/
#pragma once
#include <array>
#include <memory>
#include <vector>
#include <string_view>
#include <string>

// This boundary deliberately contains no GTA or Bullet types. RocketSim owns
// motion; the resource only renders snapshots, without changing the net ABI.
namespace RocketSoccer
{
    struct Controls
    {
        float throttle{}, steer{}, pitch{}, yaw{}, roll{};
        bool  jump{}, boost{}, handbrake{};
    };
    struct Body
    {
        std::array<float, 3> position{}, forward{}, right{}, up{}, velocity{};
    };
    struct Snapshot
    {
        Body               car, ball;
        std::vector<Body>  cars;
        std::vector<float> raw;
        bool               ballOnGround{};
        bool               grounded{}, boosting{};
        int                goalTeam{};
        unsigned long long ticks{};
    };
    struct Triangle
    {
        std::array<float, 9> vertices;
        int                  material{};
    };
    const std::vector<Triangle>& GetArenaMesh();

    class Simulation
    {
    public:
        Simulation();
        ~Simulation();
        Simulation(const Simulation&) = delete;
        Simulation&        operator=(const Simulation&) = delete;
        Snapshot           Step(float seconds, const Controls& controls);
        Snapshot           Predict(std::string_view checkpoint, const std::vector<std::array<float, 8>>& frames, int slot, const std::array<float, 8>& opponent,
                                   float fraction);
        std::string        Rebase(std::string_view checkpoint);
        void               Reset(int kickoff = -1);
        void               Configure(bool flat, bool opponent);
        void               SetControls(int car, const Controls& controls);
        void               SetUnlimitedBoost(bool unlimited);
        bool               ControlBall(int action);
        int                PollGoal();
        std::vector<float> Pads();
        std::array<double, 42> Camera(const std::array<double, 28>& input);
        void                   ResetCamera();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
