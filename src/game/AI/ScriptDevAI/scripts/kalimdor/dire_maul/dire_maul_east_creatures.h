/* This file is part of the ScriptDev2 Project, licensed under GPL version2. */
#ifndef DIRE_MAUL_EAST_CREATURES_H
#define DIRE_MAUL_EAST_CREATURES_H

// These are scene checkpoints, not encounter states. The instance persists
// each scene atomically together with its last acknowledged waypoint/timer.
enum DmeIronbarkStage
{
    DME_IRON_LOCKED, DME_IRON_READY, DME_IRON_WAIT_DROP, DME_IRON_WAIT_SAY,
    DME_IRON_WAIT_WALK, DME_IRON_WALKING, DME_IRON_WAIT_EMOTE_1,
    DME_IRON_WAIT_EMOTE_2, DME_IRON_WAIT_OPEN, DME_IRON_WAIT_FAKE,
    DME_IRON_WAIT_DESPAWN, DME_IRON_DONE
};
enum DmePusillinStage
{
    DME_PU_INITIAL, DME_PU_WAIT_1, DME_PU_RUN_9, DME_PU_WAIT_9,
    DME_PU_RUN_19, DME_PU_WAIT_19, DME_PU_RUN_32, DME_PU_WAIT_32,
    DME_PU_RUN_38, DME_PU_WAIT_38, DME_PU_WAIT_ADDS, DME_PU_WAIT_FORM,
    DME_PU_CONFIRM_FINAL, DME_PU_RETRY_READY, DME_PU_COMBAT, DME_PU_DEAD,
    DME_PU_RUN_9_SAY, DME_PU_RUN_19_SAY, DME_PU_RUN_32_SAY,
    DME_PU_RUN_38_SAY, DME_PU_FINAL_SAY
};
void AddSC_dire_maul_east_creatures();
#endif
