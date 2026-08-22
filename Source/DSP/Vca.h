#pragma once

//==============================================================================
/*
    Output amplifier stage. Stateless - the caller owns the smoothed gain
    value (see SynthVoice), this just multiplies. Takes the amplitude
    modulation summing point as an explicit argument so item 3's shared-ADSR
    routing and item 7's accent are a multiply here, not a restructure.
*/
struct Vca
{
    static float processSample (float input, float gain, float amplitudeModulation) noexcept
    {
        return input * gain * amplitudeModulation;
    }
};
