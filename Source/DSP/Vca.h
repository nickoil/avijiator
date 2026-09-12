/*
    This file is part of Avijiator.
    Copyright (C) 2026 Nick Casey

    Avijiator is free software: you can redistribute it and/or modify it
    under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at
    your option) any later version.

    Avijiator is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with Avijiator. If not, see <https://www.gnu.org/licenses/>.
*/

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
