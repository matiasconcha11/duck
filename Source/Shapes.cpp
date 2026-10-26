#include "Shapes.h"

namespace Shapes
{
    juce::StringArray names()
    {
        return { "Classic", "Punchy", "Smooth", "Long", "Breathe", "Hard Cut" };
    }

    std::vector<CurvePoint> points (int shape)
    {
        switch (shape)
        {
            case 1: // Punchy: very fast recovery, just clears the kick's attack
                return { { 0.0f, 0.0f, 0.0f }, { 0.02f, 0.0f, -0.35f }, { 0.2f, 1.0f, 0.0f }, { 0.97f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
            case 2: // Smooth: never fully silent, gentle recovery
                return { { 0.0f, 0.15f, -0.2f }, { 0.6f, 1.0f, 0.0f }, { 0.95f, 1.0f, 0.0f }, { 1.0f, 0.15f, 0.0f } };
            case 3: // Long: slow, even recovery across most of the beat
                return { { 0.0f, 0.0f, 0.0f }, { 0.04f, 0.0f, -0.1f }, { 0.85f, 1.0f, 0.0f }, { 0.97f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
            case 4: // Breathe: slow start, then swells back in
                return { { 0.0f, 0.0f, 0.45f }, { 0.7f, 1.0f, 0.0f }, { 0.95f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
            case 5: // Hard Cut: silent for the first quarter, then straight back
                return { { 0.0f, 0.0f, 0.0f }, { 0.24f, 0.0f, 0.0f }, { 0.25f, 1.0f, 0.0f }, { 0.97f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
            default: // Classic: the standard sidechain pump
                return { { 0.0f, 0.0f, 0.0f }, { 0.03f, 0.0f, -0.3f }, { 0.4f, 1.0f, 0.0f }, { 0.97f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
        }
    }
}
