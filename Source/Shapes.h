#pragma once

#include "Curve.h"

// The ducking curves Duck offers. Each starts ducked, recovers to full volume and holds there,
// with a short fade back down at the end so looping into the next hit doesn't click.
namespace Shapes
{
    juce::StringArray names();
    std::vector<CurvePoint> points (int shape);
}
