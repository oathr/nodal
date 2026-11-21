#pragma once

#include <utility>

#include "Params.h"

// Factory presets. Values are written in real units through the inverse maps of Params.h
// (menus by index, switches 0/1); anything not named keeps its default. The first preset is
// a new instance's. Strings are UTF-8: always juce::String::fromUTF8.
namespace nodal::presets {

struct Preset {
    const char* category;
    const char* name;
    const char* note;
    std::vector<std::pair<const char*, double>> values;
};

// Table indices (FactoryTables::names()).
enum Table { Basic, Pulse, Sweep, Vowel, Hollow, FM, Sync, Fold, Digital, Organ, Shards, Choir };

inline const std::vector<Preset>& all() {
    using namespace nodal::params;
    static const std::vector<Preset> list = {
        {"BASE", "Miroir", "Le point de départ : la même note, le même souffle, en synthé.",
         {{"table", Basic}, {"position", 0.62f}, {"formant", 0.5f}, {"voices", 2}, {"detune", tDetune(10.0f)}}},

        {"VOIX", "Chœur de verre", "Une voix devient un chœur de verre : six voix, formants gardés.",
         {{"table", Choir}, {"position", 0.4f}, {"formant", 0.8f}, {"voices", 5}, {"detune", tDetune(18.0f)}, {"spread", 0.3f},
          {"width", 0.9f}, {"breath", 0.25f}, {"glide", tGlide(0.04f)}, {"release", tRelease(0.2f)}}},
        {"VOIX", "Robot accordé", "Hauteur ramenée au demi-ton, sans glisse : le robot qui chante juste.",
         {{"table", Digital}, {"position", 0.45f}, {"quantize", 1}, {"glide", 0.0f}, {"voices", 0}, {"formant", 0.9f}, {"breath", 0.1f},
          {"voiced", 0.8f}}},
        {"VOIX", "Vocodeur pastel", "Les voyelles de la voix posées sur une table de voyelles, en do majeur.",
         {{"table", Vowel}, {"position", 0.3f}, {"track", tBipolar(0.4f)}, {"formant", 1.0f}, {"quantize", 2}, {"voices", 3},
          {"detune", tDetune(9.0f)}, {"breath", 0.35f}, {"glide", tGlide(0.03f)}}},

        {"SOUFFLE", "Flûte en FM", "Le souffle reste, la note passe dans une FM qui s'ouvre quand on joue fort.",
         {{"table", FM}, {"position", 0.25f}, {"track", tBipolar(0.5f)}, {"formant", 0.3f}, {"voices", 1}, {"breath", 0.45f},
          {"detune", tDetune(6.0f)}}},
        {"SOUFFLE", "Orgue d'air", "Registres d'orgue, sous-octave, longue queue.",
         {{"table", Organ}, {"position", 0.5f}, {"sub", tSub(-12.0f)}, {"voices", 2}, {"formant", 0.2f}, {"release", tRelease(0.35f)},
          {"breath", 0.2f}}},

        {"CORDES", "Violon synthétique", "Scie large et brillante qui suit l'archet.",
         {{"table", Basic}, {"position", 0.64f}, {"track", tBipolar(0.3f)}, {"voices", 4}, {"detune", tDetune(15.0f)}, {"width", 0.8f},
          {"formant", 0.6f}, {"tone", tTone(7000.0f)}, {"breath", 0.1f}}},
        {"CORDES", "Violoncelle sous-octave", "Une octave plus bas, creux, avec une sous-basse.",
         {{"table", Hollow}, {"position", 0.6f}, {"transpose", tSemitones(-12.0f)}, {"sub", tSub(-8.0f)}, {"voices", 2},
          {"formant", 0.5f}, {"release", tRelease(0.18f)}}},

        {"GUITARE", "Lead synchro", "Sync qui s'ouvre avec l'attaque, saturé : un lead de guitare-synthé.",
         {{"table", Sync}, {"position", 0.35f}, {"track", tBipolar(0.7f)}, {"drive", 0.35f}, {"voices", 1}, {"glide", tGlide(0.01f)},
          {"release", tRelease(0.2f)}, {"formant", 0.3f}, {"voiced", 0.8f}}},
        {"GUITARE", "Pluck numérique", "Impulsion étroite, relâchement court, filtré.",
         {{"table", Pulse}, {"position", 0.3f}, {"release", tRelease(0.06f)}, {"tone", tTone(4500.0f)}, {"voices", 2},
          {"formant", 0.2f}, {"breath", 0.0f}}},

        {"TEXTURE", "Nuage de grains", "Huit voix piochent dans les deux dernières secondes : la phrase en nuage.",
         {{"table", Shards}, {"position", 0.5f}, {"spray", tSpray(1.2f)}, {"grain", tGrain(0.18f)}, {"voices", 7}, {"spread", 0.6f},
          {"width", 1.0f}, {"mix", 0.85f}, {"formant", 0.4f}}},
        {"TEXTURE", "Brume", "Chœur lointain, grains longs, souffle, filtre fermé.",
         {{"table", Choir}, {"position", 0.7f}, {"spray", tSpray(0.6f)}, {"grain", tGrain(0.4f)}, {"voices", 5}, {"formant", 0.7f},
          {"breath", 0.5f}, {"tone", tTone(3000.0f)}, {"glide", tGlide(0.2f)}, {"release", tRelease(0.5f)}}},
        {"TEXTURE", "Constellation", "Une octave au-dessus, en pentatonique, des grains courts qui scintillent.",
         {{"table", Shards}, {"position", 0.8f}, {"quantize", 4}, {"spray", tSpray(2.0f)}, {"grain", tGrain(0.08f)}, {"voices", 7},
          {"transpose", tSemitones(12.0f)}, {"width", 1.0f}, {"glide", 0.0f}}},
        {"TEXTURE", "Bourdon fixe", "La note ne bouge plus (do 2) : c'est le timbre de l'entrée qui fait vivre la table.",
         {{"table", Sweep}, {"position", 0.3f}, {"pitch_mode", 1}, {"fixed_note", tNote(36.0f)}, {"track", tBipolar(0.8f)},
          {"formant", 1.0f}, {"voices", 4}, {"detune", tDetune(20.0f)}, {"release", tRelease(0.4f)}}},

        {"FX", "Plaque de Chladni", "Chaque voix lit une autre région de la table : la plaque change de mode.",
         {{"table", Sweep}, {"position", 0.5f}, {"voices", 3}, {"spread", 0.8f}, {"spray", tSpray(0.3f)}, {"grain", tGrain(0.05f)},
          {"formant", 0.4f}}},
        {"FX", "Octave dessous", "L'original garde sa place, le synthé double une octave plus bas.",
         {{"table", Basic}, {"position", 0.33f}, {"transpose", tSemitones(-12.0f)}, {"voices", 0}, {"formant", 0.4f},
          {"sub", tSub(-6.0f)}, {"mix", 0.55f}}},
        {"FX", "Fold sauvage", "Plus on joue fort et brillant, plus la table se replie.",
         {{"table", Fold}, {"position", 0.1f}, {"track", tBipolar(1.0f)}, {"drive", 0.6f}, {"voices", 2}, {"formant", 0.3f}}},
    };
    return list;
}

// Plain values (APVTS units) of every parameter for a preset.
inline std::vector<float> valuesOf(const Preset& p) {
    const auto& specs = params::all();
    std::vector<float> values;
    for (const auto& s : specs) values.push_back(s.defaultValue);
    for (const auto& [id, value] : p.values) {
        const int i = params::indexOf(id);
        if (i >= 0) values[size_t(i)] = float(value);
    }
    return values;
}

}  // namespace nodal::presets
