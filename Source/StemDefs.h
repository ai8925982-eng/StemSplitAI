#pragma once

namespace stemsplit
{
constexpr int kNumStems = 30;

// Static description of every stem. The numeric columns are the "priors" used
// by the built-in heuristic DSP engine only (centre frequency, width in octaves,
// percussive / harmonic affinity, centre / side stereo affinity).
// The AI (ONNX) backend ignores them: it must output the stems in THIS ORDER.
struct StemInfo
{
    const char* id;     // ASCII: parameter IDs + export file names
    const char* name;   // UTF-8 display name
    const char* group;  // for UI colour
    float fc, sigmaOct, perc, harm, centre, side;
};

inline const StemInfo& stemInfo (int i)
{
    static const StemInfo table[kNumStems] =
    {
        // id          name                              group         fc      sig   perc  harm  cen   side
        { "kick",       "Kick",                           "Drums",      70.f,  0.9f, 1.0f, 0.05f, 1.0f, 0.2f },
        { "snare",      "Snare",                          "Drums",    1500.f,  1.8f, 1.0f, 0.05f, 1.0f, 0.3f },
        { "hihat",      "Hi-hat",                         "Drums",    9000.f,  0.7f, 1.0f, 0.10f, 0.6f, 1.0f },
        { "tom",        "Tom",                            "Drums",     180.f,  0.8f, 1.0f, 0.05f, 0.5f, 0.7f },
        { "cymbal",     "Crash / Ride / Cymbal",          "Drums",    6000.f,  1.0f, 0.6f, 0.40f, 0.5f, 1.0f },
        { "overhead",   "Overhead / Room",                "Drums",    2500.f,  2.5f, 0.5f, 0.30f, 0.3f, 1.0f },
        { "bass",       "Bass Guitar",                    "Bass",       80.f,  0.8f, 0.1f, 1.00f, 1.0f, 0.05f },
        { "synthbass",  "Synth Bass",                     "Bass",       55.f,  0.7f, 0.05f,1.00f, 1.0f, 0.05f },
        { "acoustic",   "Acoustic Guitar",                "Guitar",    500.f,  1.3f, 0.1f, 1.00f, 0.5f, 0.8f },
        { "electric",   "Electric Guitar",                "Guitar",    900.f,  1.3f, 0.15f,1.00f, 0.4f, 1.0f },
        { "gtrsolo",    "Guitar Solo",                    "Guitar",   1500.f,  1.0f, 0.05f,1.00f, 1.0f, 0.2f },
        { "powerchord", "Power Chord",                    "Guitar",    220.f,  1.0f, 0.2f, 1.00f, 0.5f, 1.0f },
        { "rhythm",     "Rhythm Guitar",                  "Guitar",    700.f,  1.2f, 0.15f,1.00f, 0.3f, 1.0f },
        { "piano",      "Piano",                          "Keys",      500.f,  1.8f, 0.2f, 1.00f, 0.6f, 0.6f },
        { "epiano",     "Electric Piano",                 "Keys",      400.f,  1.3f, 0.1f, 1.00f, 0.6f, 0.6f },
        { "synth",      "Synthesizer",                    "Keys",     1200.f,  1.8f, 0.05f,1.00f, 0.5f, 0.7f },
        { "organ",      "Organ",                          "Keys",      350.f,  1.3f, 0.0f, 1.00f, 0.6f, 0.4f },
        { "pad",        "Pad",                            "Keys",      700.f,  1.8f, 0.0f, 1.00f, 0.4f, 1.0f },
        { "phin",       "พิณ (Phin)",                     "Isan",      600.f,  1.0f, 0.2f, 0.90f, 0.8f, 0.3f },
        { "khaen",      "แคน (Khaen)",                    "Isan",      900.f,  1.0f, 0.0f, 1.00f, 0.8f, 0.3f },
        { "wot",        "โหวด (Wot)",                     "Isan",     1400.f,  0.9f, 0.0f, 1.00f, 0.8f, 0.3f },
        { "ponglang",   "โปงลาง (Pong Lang)",             "Isan",     1100.f,  1.1f, 0.8f, 0.60f, 0.7f, 0.4f },
        { "isanwind",   "เครื่องเป่าอีสาน (Isan Winds)",  "Isan",     1800.f,  1.0f, 0.0f, 1.00f, 0.7f, 0.4f },
        { "violin",     "Violin",                         "Orchestral",1000.f, 1.0f, 0.0f, 1.00f, 0.7f, 0.4f },
        { "strings",    "Strings",                        "Orchestral", 600.f, 1.8f, 0.0f, 1.00f, 0.4f, 1.0f },
        { "brass",      "Brass",                          "Orchestral", 500.f, 1.2f, 0.05f,1.00f, 0.7f, 0.4f },
        { "fx",         "FX / Sound Effects",             "FX",       5000.f,  2.5f, 0.4f, 0.30f, 0.2f, 1.0f },
        { "perc",       "Percussion",                     "Drums",    3000.f,  2.0f, 0.7f, 0.10f, 0.4f, 0.8f },
        { "vocals",     "Lead Vocals",                    "Vocal",    1200.f,  1.5f, 0.10f,1.00f, 1.0f, 0.03f },
        { "chorus",     "Chorus / Backing Vocals",        "Chorus",   1500.f,  1.6f, 0.10f,1.00f, 0.25f,1.0f },
    };
    return table[i];
}
} // namespace stemsplit
