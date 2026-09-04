#include "PIDReportHandler.h"

#ifdef FFB_SERIAL_TRACE
void ftV(long v)              { Serial.print(v); Serial.write(','); }
void ftTag(char tag, long v)  { Serial.write(tag); ftV(v); }
void ftEnd(long v)            { Serial.println(v); }
#endif

// Constructor: Initializes the PIDReportHandler with default values
PIDReportHandler::PIDReportHandler()
{
    nextEID = 1;  // Next available Effect ID starts at 1
    deviceState = MDEVICESTATE_SPRING;  // Set device state to SPRING mode
    deviceGain.gain = 255;  // B1: full device gain until the host sends Set Device Gain
    pidBlockLoad.ramPoolAvailable = MEMORY_SIZE;  // C1: real starting pool (was left at 0 -> underflow)
}

// Destructor: Frees all effects when the handler is destroyed
PIDReportHandler::~PIDReportHandler() 
{
    FreeAllEffects();  // Free all allocated effects
}

// Gets the next available effect ID and marks it as allocated
uint8_t PIDReportHandler::GetNextFreeEffect(void)
{
    // C2: was `nextEID == MAX_EFFECTS` which made effect ID MAX_EFFECTS unreachable
    // (only MAX_EFFECTS-1 usable). IDs are 1..MAX_EFFECTS, array is [MAX_EFFECTS + 1].
    if (nextEID > MAX_EFFECTS)  // If all effects are allocated, return 0
        return 0;

    uint8_t id = nextEID++;  // Allocate the next effect ID

    // Search for the next free spot in the effect states
    while (nextEID <= MAX_EFFECTS && g_EffectStates[nextEID].state != 0)
        nextEID++;

    // Mark the effect as allocated and update the PID state
    g_EffectStates[id].state = MEFFECTSTATE_ALLOCATED;
    pidState.effectBlockIndex = id;

    return id;  // Return the allocated effect ID
}

// Stops all currently active effects
void PIDReportHandler::StopAllEffects(void)
{
    for (uint8_t id = 1; id <= MAX_EFFECTS; id++)  // effect IDs are 1-based
        StopEffect(id);  // Stop each effect
}

// Starts a specific effect by its ID
void PIDReportHandler::StartEffect(uint8_t id)
{
    if (id == 0 || id > MAX_EFFECTS)  // D1: reject out-of-range indices
        return;

    // Mark the effect as playing, reset elapsed time, and record the start time.
    // Keep the ALLOCATED bit so a subsequent Stop leaves the block allocated (not "free").
    g_EffectStates[id].state = MEFFECTSTATE_ALLOCATED | MEFFECTSTATE_PLAYING;
    g_EffectStates[id].elapsedTime = 0;
    g_EffectStates[id].startTime = millis();
}

// Stops a specific effect by its ID (does NOT free its pool block - see FreeEffect)
void PIDReportHandler::StopEffect(uint8_t id)
{
    if (id == 0 || id > MAX_EFFECTS)  // D1
        return;

    // C1: stopping an effect does not free memory - removed the bogus
    //     `pidBlockLoad.ramPoolAvailable += SIZE_EFFECT;`
    g_EffectStates[id].state &= ~MEFFECTSTATE_PLAYING;
}

// Frees a specific effect by its ID
void PIDReportHandler::FreeEffect(uint8_t id)
{
    if (id == 0 || id > MAX_EFFECTS)  // D1
        return;

    // C1: refund the pool only if this block was actually allocated
    if (g_EffectStates[id].state != MEFFECTSTATE_FREE)
        pidBlockLoad.ramPoolAvailable += SIZE_EFFECT;

    g_EffectStates[id].state = 0;  // Mark the effect as free
    if (id < nextEID)  // Update nextEID if needed
        nextEID = id;
}

// Frees all effects and resets the handler
void PIDReportHandler::FreeAllEffects(void)
{
    nextEID = 1;  // Reset next available effect ID
    memset((void*)&g_EffectStates, 0, sizeof(g_EffectStates));  // Clear all effect states
    pidBlockLoad.ramPoolAvailable = MEMORY_SIZE;  // Reset available memory
}

// Handles effect operation based on the received data (start, stop, loop)
void PIDReportHandler::EffectOperation(USB_FFBReport_EffectOperation_Output_Data_t* data)
{
    if (data->effectBlockIndex == 0 || data->effectBlockIndex > MAX_EFFECTS)  // D1
        return;
#ifdef FFB_SERIAL_TRACE
    ftTag('O', data->operation); ftV(data->effectBlockIndex); ftEnd(data->loopCount);
#endif

    g_EffectStates[data->effectBlockIndex].loopCount = data->loopCount;
    if (data->operation == 1)  // Start effect
    {
        if (data->loopCount == 0xFF)
        {
            g_EffectStates[data->effectBlockIndex].totalDuration = USB_DURATION_INFINITE;
        }
        else if (data->loopCount > 0)
        {
            // Calculate total duration based on loop count
            g_EffectStates[data->effectBlockIndex].totalDuration = (
                g_EffectStates[data->effectBlockIndex].startDelay
                + g_EffectStates[data->effectBlockIndex].duration
            ) * data->loopCount;
        }
        StartEffect(data->effectBlockIndex);  // Start the effect
    }
    else if (data->operation == 2)  // StartSolo: stop all and start a specific effect
    {
        StopAllEffects();
        StartEffect(data->effectBlockIndex);
    }
    else if (data->operation == 3)  // Stop effect
    {
        StopEffect(data->effectBlockIndex);
    }
}

// Handles the BlockFree operation to free one or all effects
void PIDReportHandler::BlockFree(USB_FFBReport_BlockFree_Output_Data_t* data)
{
    uint8_t eid = data->effectBlockIndex;
    if (eid == 0xFF)  // Free all effects
    {
        FreeAllEffects();
    }
    else  // Free a specific effect
    {
        FreeEffect(eid);
    }
}

// Handles device control commands (enable/disable actuators, reset, pause, etc.)
void PIDReportHandler::DeviceControl(USB_FFBReport_DeviceControl_Output_Data_t* data)
{
    uint8_t control = data->control;

    switch (control)
    {
        case 0x01:  // Enable Actuators
            pidState.status |= 2;
            break;
        case 0x02:  // Disable Actuators
            pidState.status &= ~(0x02);
            break;
        case 0x03:  // Stop All Effects
            StopAllEffects();
            deviceState &= ~(MDEVICESTATE_SPRING);
            break;
        case 0x04:  // Reset Device - PID 1.0 s5.13: clear pause, enable actuators, clear effects
            FreeAllEffects();
            deviceState = MDEVICESTATE_SPRING;   // B2: also clears MDEVICESTATE_PAUSED
            pidState.status |= 0x02;             // B2: actuators enabled
            break;
        case 0x05:  // Pause Device
            deviceState |= MDEVICESTATE_PAUSED;
            break;
        case 0x06:  // Continue Device
            deviceState &= ~(MDEVICESTATE_PAUSED);
            break;
        default:  // Handle unknown commands
            break;
    }
#ifdef FFB_SERIAL_TRACE_FULL
    ftTag('D', control); ftEnd(deviceState);
#endif
}

// Sets the device gain based on received data
void PIDReportHandler::DeviceGain(USB_FFBReport_DeviceGain_Output_Data_t* data)
{
    deviceGain.gain = data->gain;  // Update the device gain
}

// Placeholder for setting custom force (not yet implemented)
void PIDReportHandler::SetCustomForce(USB_FFBReport_SetCustomForce_Output_Data_t* data)
{
}

// Placeholder for setting custom force data (not yet implemented)
void PIDReportHandler::SetCustomForceData(USB_FFBReport_SetCustomForceData_Output_Data_t* data)
{
}

// Placeholder for setting download force sample (not yet implemented)
void PIDReportHandler::SetDownloadForceSample(USB_FFBReport_SetDownloadForceSample_Output_Data_t* data)
{
}

// Sets an effect with the provided data (effect type, duration, direction, etc.)
void PIDReportHandler::SetEffect(USB_FFBReport_SetEffect_Output_Data_t* data)
{
    volatile TEffectState* effect = &g_EffectStates[data->effectBlockIndex];

    effect->duration = data->duration;  // Set effect duration
    for (int i=0; i<FFB_AXIS_COUNT; ++i)
    {
        effect->direction[i] = data->direction[i];  // Set effect direction
    }
    effect->effectType = data->effectType;
    effect->gain = data->gain;
    effect->enableAxis = data->enableAxis;
    effect->startDelay = data->startDelay;
    effect->triggerButton = data->triggerButton;  // B3: 0 = no trigger, 1..8 = gated on that button

    // Recalculate effect duration if looping
    if (effect->loopCount != 0xFF)
    {
        uint8_t loopCount = effect->loopCount > 0 ? effect->loopCount : 1;
        effect->totalDuration = (data->duration + data->startDelay) * loopCount;
    }
}

// Sets the envelope for an effect (attack and fade levels and times)
void PIDReportHandler::SetEnvelope(USB_FFBReport_SetEnvelope_Output_Data_t* data, volatile TEffectState* effect)
{
    effect->attackLevel = data->attackLevel;
    effect->fadeLevel = data->fadeLevel;
    effect->attackTime = data->attackTime;
    effect->fadeTime = data->fadeTime;
}

// Sets the condition (axis-specific parameters) for an effect
void PIDReportHandler::SetCondition(USB_FFBReport_SetCondition_Output_Data_t* data, volatile TEffectState* effect)
{
    // D2: the low nibble is the Parameter Block Offset; the upper bits are the two
    // 2-bit Type-Specific-Block-Offset ordinals. Masking prevents an out-of-bounds
    // write into conditions[FFB_AXIS_COUNT].
    uint8_t axis = data->parameterBlockOffset & 0x0F;
    if (axis >= FFB_AXIS_COUNT)
        return;
    if (axis >= effect->conditionReportsCount)
    {
        effect->conditionReportsCount = axis + 1;  // Update condition report count
    }

    // Set condition parameters for the axis
    effect->conditions[axis].cpOffset = data->cpOffset;
    effect->conditions[axis].positiveCoefficient = data->positiveCoefficient;
    effect->conditions[axis].negativeCoefficient = data->negativeCoefficient;
    effect->conditions[axis].positiveSaturation = data->positiveSaturation;
    effect->conditions[axis].negativeSaturation = data->negativeSaturation;
    effect->conditions[axis].deadBand = data->deadBand;
}

// Sets periodic parameters for an effect (magnitude, offset, phase, period)
void PIDReportHandler::SetPeriodic(USB_FFBReport_SetPeriodic_Output_Data_t* data, volatile TEffectState* effect)
{
    // Clamp to the spec range so the int32 products in the periodic calculators (E6) stay
    // in bounds even if a non-conformant host exceeds the declared logical maximums.
    effect->magnitude = data->magnitude > 10000 ? 10000 : data->magnitude;
    int16_t off = data->offset;
    effect->offset = off > 10000 ? 10000 : (off < -10000 ? -10000 : off);
    effect->phase = data->phase % 36000;
    effect->period = data->period;
}

void PIDReportHandler::SetConstantForce(USB_FFBReport_SetConstantForce_Output_Data_t* data, volatile TEffectState* effect)
{
	int16_t m = data->magnitude;
	effect->magnitude = m > 10000 ? 10000 : (m < -10000 ? -10000 : m);
}

void PIDReportHandler::SetRampForce(USB_FFBReport_SetRampForce_Output_Data_t* data, volatile TEffectState* effect)
{
	effect->startMagnitude = data->startMagnitude;
	effect->endMagnitude = data->endMagnitude;
}

void PIDReportHandler::CreateNewEffect(USB_FFBReport_CreateNewEffect_Feature_Data_t* inData)
{
	pidBlockLoad.reportId = 6;
	pidBlockLoad.effectBlockIndex = GetNextFreeEffect();

	if (pidBlockLoad.effectBlockIndex == 0)
	{
		pidBlockLoad.loadStatus = 2;    // 1=Success,2=Full,3=Error
	}
	else
	{
		pidBlockLoad.loadStatus = 1;    // 1=Success,2=Full,3=Error

		volatile TEffectState* effect = &g_EffectStates[pidBlockLoad.effectBlockIndex];

		memset((void*)effect, 0, sizeof(TEffectState));
		effect->state = MEFFECTSTATE_ALLOCATED;
		pidBlockLoad.ramPoolAvailable -= SIZE_EFFECT;
	}
#ifdef FFB_SERIAL_TRACE_FULL
	ftTag('C', inData->effectType); ftV(pidBlockLoad.effectBlockIndex);
	ftEnd(pidBlockLoad.loadStatus);
#endif
}

// Unpack USB data based on the incoming report ID
void PIDReportHandler::UppackUsbData(uint8_t* data, uint16_t len)
{
    (void)len;  // D4: length is not used - the switch is on data[0]
    // Extract the effect ID from the incoming data. Valid only for the parameter-block
    // reports (1..6) - reports 12/13 carry a control/gain byte here instead.
    uint8_t effectId = data[1];  // The effectBlockIndex is the second byte.
    bool validId = (effectId >= 1 && effectId <= MAX_EFFECTS);  // D1

    // Handle different report IDs
    switch (data[0])  // reportID
    {
    case 1:
        if (validId) SetEffect((USB_FFBReport_SetEffect_Output_Data_t*)data);  // Set effect
        break;
    case 2:
        if (validId) SetEnvelope((USB_FFBReport_SetEnvelope_Output_Data_t*)data, &g_EffectStates[effectId]);  // Set envelope
        break;
    case 3:
        if (validId) SetCondition((USB_FFBReport_SetCondition_Output_Data_t*)data, &g_EffectStates[effectId]);  // Set condition
        break;
    case 4:
        if (validId) SetPeriodic((USB_FFBReport_SetPeriodic_Output_Data_t*)data, &g_EffectStates[effectId]);  // Set periodic effect
        break;
    case 5:
        if (validId) SetConstantForce((USB_FFBReport_SetConstantForce_Output_Data_t*)data, &g_EffectStates[effectId]);  // Set constant force
        break;
    case 6:
        if (validId) SetRampForce((USB_FFBReport_SetRampForce_Output_Data_t*)data, &g_EffectStates[effectId]);  // Set ramp force
        break;
    case 7:
        SetCustomForceData((USB_FFBReport_SetCustomForceData_Output_Data_t*)data);  // Set custom force data
        break;
    case 8:
        SetDownloadForceSample((USB_FFBReport_SetDownloadForceSample_Output_Data_t*)data);  // Set download force sample
        break;
    case 9:
        // No operation for this report ID
        break;
    case 10:
        EffectOperation((USB_FFBReport_EffectOperation_Output_Data_t*)data);  // Perform effect operation
        break;
    case 11:
        BlockFree((USB_FFBReport_BlockFree_Output_Data_t*)data);  // Free effect block
        break;
    case 12:
        DeviceControl((USB_FFBReport_DeviceControl_Output_Data_t*)data);  // Control device
        break;
    case 13:
        DeviceGain((USB_FFBReport_DeviceGain_Output_Data_t*)data);  // Set device gain
        break;
    case 14:
        SetCustomForce((USB_FFBReport_SetCustomForce_Output_Data_t*)data);  // Set custom force
        break;
    default:
        break;  // No action for unknown report IDs
    }
}

// Get the PID pool report data
uint8_t* PIDReportHandler::getPIDPool()
{
    // (Note: not currently wired up - DynamicHID::GetReport builds this inline.)
    pidPoolReport.reportId = 7;  // Set the report ID for the PID pool
    pidPoolReport.ramPoolSize = MEMORY_SIZE;  // Set the size of the RAM pool
    pidPoolReport.maxSimultaneousEffects = MAX_EFFECTS;  // Set the max simultaneous effects allowed
    pidPoolReport.memoryManagement = 1;  // F3: bit0 = Device Managed Pool. NOT Shared Parameter Blocks.
    return (uint8_t*)& pidPoolReport;  // Return the address of the PID pool report
}

// Get the PID block load report data
uint8_t* PIDReportHandler::getPIDBlockLoad()
{
    return (uint8_t*)& pidBlockLoad;  // Return the address of the PID block load report
}

uint8_t PIDReportHandler::playingCount()
{
    uint8_t n = 0;
    for (uint8_t i = 1; i <= MAX_EFFECTS; i++)
        if (g_EffectStates[i].state & MEFFECTSTATE_PLAYING) n++;
    return n;
}

// Get the PID status report data
uint8_t* PIDReportHandler::getPIDStatus()
{
    // F1: byte 1 is [DevicePaused, ActuatorsEnabled, SafetySwitch, ActuatorOverride,
    //     ActuatorPower, EffectPlaying, pad, pad] - recompute the EffectPlaying bit (bit 5).
    uint8_t playing = playingCount() ? 1 : 0;
    pidState.status = (pidState.status & ~0x20) | (playing << 5);
    return (uint8_t*)& pidState;  // Return the address of the PID status report
}
