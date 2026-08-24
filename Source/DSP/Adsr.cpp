#include "Adsr.h"

#include <juce_core/juce_core.h>

void Adsr::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;
    reset();
}

void Adsr::reset() noexcept
{
    stage = Stage::Idle;
    currentLevel = 0.0f;
}

void Adsr::noteOn() noexcept
{
    stage = Stage::Attack;
}

void Adsr::noteOff() noexcept
{
    if (stage != Stage::Idle)
        stage = Stage::Release;
}

float Adsr::processSample() noexcept
{
    switch (stage)
    {
        case Stage::Idle:
            break; // currentLevel stays at whatever it last was - 0, normally

        case Stage::Attack:
        {
            const auto increment = 1.0f / (juce::jmax (minStageSeconds, attackSeconds) * (float) sampleRate);
            currentLevel += increment;

            if (currentLevel >= 1.0f)
            {
                currentLevel = 1.0f;
                stage = Stage::Decay;
            }
            break;
        }

        case Stage::Decay:
        {
            const auto decrement = 1.0f / (juce::jmax (minStageSeconds, decaySeconds) * (float) sampleRate);
            currentLevel -= decrement;

            if (currentLevel <= sustainLevel)
            {
                currentLevel = sustainLevel;
                stage = Stage::Sustain;
            }
            break;
        }

        case Stage::Sustain:
            // Tracks the (already-smoothed) sustain level live, so moving the
            // knob while held is heard immediately, not just on the next trigger.
            currentLevel = sustainLevel;
            break;

        case Stage::Release:
        {
            const auto decrement = 1.0f / (juce::jmax (minStageSeconds, releaseSeconds) * (float) sampleRate);
            currentLevel -= decrement;

            if (currentLevel <= 0.0f)
            {
                currentLevel = 0.0f;
                stage = Stage::Idle;
            }
            break;
        }
    }

    return currentLevel;
}
