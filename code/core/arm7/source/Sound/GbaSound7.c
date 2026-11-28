#include "common.h"
#include <libtwl/ipc/ipcSync.h>
#include <libtwl/rtos/rtosIrq.h>
#include "GbaTimer.h"
#include "GbSound.h"
#include "GbaSound.h"
#include "GbaSoundDefinitions.h"
#include "GbaSound7.h"

gbas_shared_t* gSoundSharedData;
static gbat_t sTimers[2];
static gbas_direct_channel7_t sDirectChannels[2];
static u16 sSoundCntH;
static bool sPaused;
static s16 sLastOutLeft;
static s16 sLastOutRight;
static u16 sDeclickSamples;
static bool sLastMasterEnable;

enum
{
    SOUND_EVENT_RESET_A = 1 << 0,
    SOUND_EVENT_RESET_B = 1 << 1,
    SOUND_EVENT_ROUTING_CHANGED = 1 << 2,
    DECLICK_SAMPLES = 256,
};

// The IPC interrupt posts events; only the mixer changes its playback state.
static volatile u8 sPendingSoundEvents;

static void clearDirectChannel(gbas_direct_channel7_t* channel)
{
    channel->curPlaySamples = 0;
    channel->curPlaySampleCount = 0;
}

static inline s16 slewSample(s16 cur, s16 target)
{
    int diff = (int)target - (int)cur;
    if (diff > 0)
    {
        int step = diff >> 5;
        return (s16)(cur + (step ? step : 1));
    }
    if (diff < 0)
    {
        int step = (-diff) >> 5;
        return (s16)(cur - (step ? step : 1));
    }
    return target;
}

static s8 updateDirectChannel(gbas_direct_channel_t* channel, gbas_direct_channel7_t* channel7, u32 timerOverflows)
{
    u32 samples = channel7->curPlaySamples;
    // no overflows, or too many overflows
    if (timerOverflows == 0 || timerOverflows > 10)
    {
        return (s8)samples;
    }

    for (u32 i = timerOverflows; i > 0; --i)
    {
        u32 readOffset = channel->readOffset;
        u32 fifoCount = (channel->writeOffset - readOffset);
        if (!(fifoCount & 4) && !channel->dmaRequest)
        {
            channel->dmaRequest = true;
            ipc_triggerArm7Irq();
        }
        if (channel7->curPlaySampleCount != 0)
        {
            samples >>= 8;
            channel7->curPlaySampleCount--;
        }
        else if (__builtin_expect((fifoCount & 7) != 0, true))
        {
            samples = channel->fifo[readOffset];
            channel7->curPlaySampleCount = 3;
            channel->readOffset = (readOffset + 1) & 7;
        }
    }
    channel7->curPlaySamples = samples;

    return (s8)samples;
}

static inline s16 clampSample(int inSample)
{
    int outSample = inSample << 23;
    if (inSample != (outSample >> 23))
        outSample = 0x7FFFFFFF ^ (inSample >> 31);
    return outSample >> 16;
}

void gbas_init(gbas_shared_t* sharedData)
{
    gSoundSharedData = sharedData;
    gbat_initTimer(&sTimers[0]);
    gbat_initTimer(&sTimers[1]);
    gbs_init();
    sPaused = false; //true;
    sLastOutLeft = 0;
    sLastOutRight = 0;
    sDeclickSamples = 0;
    sLastMasterEnable = false;
    sPendingSoundEvents = 0;
    clearDirectChannel(&sDirectChannels[0]);
    clearDirectChannel(&sDirectChannels[1]);
}

void gbas_setTimerReload(u32 timer, u16 reload)
{
    sTimers[timer].reload = reload;
}

void gbas_setTimerControl(u32 timer, u16 control)
{
    sTimers[timer].control = control;
    if (!(control & GBAT_CONTROL_ENABLED))
    {
        sTimers[timer].isStarted = false;
    }
}

void gbas_setSoundCntHLo(u8 value)
{
    ((u8*)&sSoundCntH)[0] = value;
}

void gbas_setSoundCntHHi(u8 value)
{
    u8 oldHi = ((u8*)&sSoundCntH)[1];
    u8 newHi = value & 0x77;
    ((u8*)&sSoundCntH)[1] = newHi;

    u8 events = 0;
    if (value & (GBA_SOUNDCNT_H_DIRECT_A_RESET >> 8))
        events |= SOUND_EVENT_RESET_A;
    if (value & (GBA_SOUNDCNT_H_DIRECT_B_RESET >> 8))
        events |= SOUND_EVENT_RESET_B;
    if ((oldHi ^ newHi) & 0x33)
        events |= SOUND_EVENT_ROUTING_CHANGED;
    if (events)
        sPendingSoundEvents |= events;
}

void gbas_updateMixer(s16* outLeft, s16* outRight)
{
    if (__builtin_expect(sPendingSoundEvents != 0, 0))
    {
        u32 irqs = rtos_disableIrqs();
        u8 events = sPendingSoundEvents;
        sPendingSoundEvents = 0;
        rtos_restoreIrqs(irqs);

        if (events & SOUND_EVENT_RESET_A)
            clearDirectChannel(&sDirectChannels[0]);
        if (events & SOUND_EVENT_RESET_B)
            clearDirectChannel(&sDirectChannels[1]);
        sDeclickSamples = DECLICK_SAMPLES;
    }

    bool masterEnable = gSoundSharedData->masterEnable;
    if (masterEnable != sLastMasterEnable)
    {
        sLastMasterEnable = masterEnable;
        if (!masterEnable)
        {
            clearDirectChannel(&sDirectChannels[0]);
            clearDirectChannel(&sDirectChannels[1]);
        }
        sDeclickSamples = DECLICK_SAMPLES;
    }

    s16 finalLeft = 0;
    s16 finalRight = 0;

    if (!sPaused)
    {
        u32 timer0Overflows = gbat_updateTimer(&sTimers[0]);
        u32 timer1Overflows = gbat_updateTimer(&sTimers[1]);
        
        //master enable
        if (masterEnable)
        {
            u32 soundCntH = sSoundCntH;
            int sampA = updateDirectChannel(&gSoundSharedData->directChannels[0],
                &sDirectChannels[0],
                (soundCntH & GBA_SOUNDCNT_H_DIRECT_A_TIMER_1) ? timer1Overflows : timer0Overflows);
            int sampB = updateDirectChannel(&gSoundSharedData->directChannels[1],
                &sDirectChannels[1],
                (soundCntH & GBA_SOUNDCNT_H_DIRECT_B_TIMER_1) ? timer1Overflows : timer0Overflows);

            if (soundCntH & GBA_SOUNDCNT_H_DIRECT_A_VOLUME_FULL)
                sampA <<= 1;

            int left = 0;
            int right = 0;
            if (soundCntH & GBA_SOUNDCNT_H_DIRECT_A_ENABLE_LEFT)
                left += sampA;
            if (soundCntH & GBA_SOUNDCNT_H_DIRECT_A_ENABLE_RIGHT)
                right += sampA;

            if (soundCntH & GBA_SOUNDCNT_H_DIRECT_B_VOLUME_FULL)
                sampB <<= 1;

            if (soundCntH & GBA_SOUNDCNT_H_DIRECT_B_ENABLE_LEFT)
                left += sampB;
            if (soundCntH & GBA_SOUNDCNT_H_DIRECT_B_ENABLE_RIGHT)
                right += sampB;

            finalLeft = clampSample(left);
            finalRight = clampSample(right);
        }
    }

    u16 declick = sDeclickSamples;
    if (__builtin_expect(declick != 0, 0))
    {
        finalLeft = slewSample(sLastOutLeft, finalLeft);
        finalRight = slewSample(sLastOutRight, finalRight);
        sDeclickSamples = declick - 1;
    }
    sLastOutLeft = finalLeft;
    sLastOutRight = finalRight;
    *outLeft = finalLeft;
    *outRight = finalRight;
}
