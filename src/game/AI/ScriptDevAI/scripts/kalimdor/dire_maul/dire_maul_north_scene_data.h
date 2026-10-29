/* This file is part of ScriptDevAI, licensed under GPL version 2. */
#ifndef DIRE_MAUL_NORTH_SCENE_DATA_H
#define DIRE_MAUL_NORTH_SCENE_DATA_H
#include <cstdint>
namespace DmnScene
{
struct Point { float x, y, z, o; std::uint32_t waitTime; };
// Frozen from the actual Classic DB. Native waypoint rows still own travel,
// orientation and waits; these coordinates only restore a confirmed checkpoint.
static Point const MizzleRoute[8] =
{
    {683.296997070f, 484.384002686f, 29.544500351f, 0.017000001f, 0},
    {728.942993164f, 483.296997070f, 28.181800842f, 0.000000000f, 0},
    {757.398986816f, 482.507995605f, 28.177900314f, 0.000000000f, 0},
    {768.049987793f, 482.501007080f, 29.541599274f, 0.000000000f, 0},
    {778.070983887f, 482.738006592f, 34.931999207f, 0.000000000f, 0},
    {784.580993652f, 481.700988770f, 37.198001862f, 0.000000000f, 0},
    {816.530029297f, 482.302001953f, 37.318199158f, 3.158999920f, 1000},
    {816.530029297f, 482.302001953f, 37.318199158f, 3.158999920f, 10800000},
};
static Point const KromcrushRoute[26] =
{
    {537.613708496f, 481.983886719f, 29.463390350f, 100.000000000f, 0},
    {492.961486816f, 482.695495605f, 29.462610245f, 100.000000000f, 0},
    {491.612487793f, 531.299926758f, 27.914310455f, 100.000000000f, 0},
    {540.208679199f, 535.683410645f, 27.918609619f, 100.000000000f, 0},
    {585.912902832f, 535.829223633f, 6.770165920f, 100.000000000f, 0},
    {586.854675293f, 563.096130371f, -4.755411148f, 100.000000000f, 0},
    {587.004882812f, 593.865112305f, -4.755402088f, 100.000000000f, 0},
    {538.365478516f, 591.854614258f, -4.754774094f, 100.000000000f, 0},
    {491.177398682f, 591.805786133f, -21.824129105f, 100.000000000f, 0},
    {478.263397217f, 591.426574707f, -25.406110764f, 100.000000000f, 0},
    {478.058410645f, 579.173217773f, -25.402069092f, 100.000000000f, 0},
    {527.757324219f, 579.964416504f, -25.402559280f, 100.000000000f, 0},
    {558.312316895f, 582.135986328f, -25.402730942f, 100.000000000f, 0},
    {558.166809082f, 541.215087891f, -25.399990082f, 100.000000000f, 0},
    {512.609497070f, 542.440185547f, -25.395690918f, 100.000000000f, 0},
    {467.379486084f, 542.868896484f, -24.540449142f, 100.000000000f, 0},
    {431.992309570f, 543.879882812f, -18.330619812f, 100.000000000f, 0},
    {409.537200928f, 536.034484863f, -17.201030731f, 100.000000000f, 0},
    {390.534088135f, 511.588287354f, -12.770500183f, 100.000000000f, 0},
    {385.664215088f, 462.625701904f, -7.219532013f, 100.000000000f, 0},
    {385.274993896f, 413.141601562f, -1.672127008f, 100.000000000f, 0},
    {384.235107422f, 365.147094727f, 0.719256580f, 100.000000000f, 0},
    {385.736602783f, 335.804290771f, 2.871598005f, 100.000000000f, 0},
    {385.666503906f, 297.633209229f, 11.203720093f, 100.000000000f, 0},
    {384.429809570f, 255.936096191f, 11.439550400f, 100.000000000f, 10},
    {384.429809570f, 255.936096191f, 11.439550400f, 100.000000000f, 0},
};
constexpr std::uint32_t RouteTimeout = 60000;
constexpr std::uint32_t RetryDelay = 1000;
constexpr std::uint32_t MaxRouteRetries = 5;
// Consume a timer using the same budget through every adjacent scene phase.
// This avoids restarting each delay at a long update, and never underflows.
inline bool ConsumeTimer(std::uint32_t& timer, std::uint32_t& budget)
{
    if (timer > budget) { timer -= budget; budget = 0; return false; }
    budget -= timer; timer = 0; return true;
}
inline bool CanUseMenu(bool currentActor, bool playerAlive, bool actorAlive,
    bool sameMap, bool closeEnough, bool inCombat, bool sameReceiptStage)
{
    return currentActor && playerAlive && actorAlive && sameMap && closeEnough &&
        !inCombat && sameReceiptStage;
}
}
#endif
