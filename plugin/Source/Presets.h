#pragma once

#include <utility>

#include "Params.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4305)  // preset values written as double literals into float maps
#endif

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
// QUANTIZE indices (scaleName()).
enum Scale {
    Off, Chromatic, Major, Minor, Dorian, Mixolydian, Lydian, Phrygian, Locrian, WholeTone, HalfWholeDim, WholeHalfDim,
    MinorBlues, MinorPenta, MajorPenta, HarmonicMinor, HarmonicMajor, Dorian4, PhrygianDominant, MelodicMinor,
    LydianAugmented, LydianDominant, SuperLocrian, Spanish8, Bhairav, HungarianMinor, Hirajoshi, InSen, Iwato, Kumoi,
    PelogSelisir, PelogTembung, Messiaen3, Messiaen4, Messiaen5, Messiaen6, Messiaen7
};
enum Breath { Flute, Air, Whisper, Input };
constexpr double voices(int n) { return n - 1; }

inline const std::vector<Preset>& all() {
    using namespace nodal::params;
    static const std::vector<Preset> list = {
        // ------------------------------------------------------------------ BASE
        {"BASE", "Miroir", "Le point de départ : la même note, le même souffle, en synthé.",
         {{"table", Basic}, {"position", 0.62}, {"formant", 0.5}, {"voices", voices(2)}, {"detune", tDetune(10)}, {"breath_mode", Air}}},

        // ------------------------------------------------------------------ VOIX
        {"VOIX", "Chœur de verre", "Une voix devient un chœur de verre : six voix, formants gardés.",
         {{"breath", 0.4}, {"table", Choir}, {"position", 0.4}, {"formant", 0.8}, {"voices", voices(6)}, {"detune", tDetune(18)}, {"spread", 0.3},
          {"width", 0.9}, {"breath_mode", Whisper}, {"glide", tGlide(0.04)}, {"release", tRelease(3.0 * 0.2)}}},
        {"VOIX", "Robot accordé", "Hauteur ramenée au demi-ton, sans glisse : le robot qui chante juste.",
         {{"table", Digital}, {"position", 0.45}, {"quantize", Chromatic}, {"glide", 0.0}, {"voices", voices(1)}, {"formant", 0.9},
          {"breath_mode", Whisper}, {"voiced", 0.8}}},
        {"VOIX", "Vocodeur pastel", "Les voyelles de la voix posées sur une table de voyelles, en majeur.",
         {{"breath", 0.5}, {"table", Vowel}, {"position", 0.3}, {"track", tBipolar(0.4)}, {"formant", 1.0}, {"quantize", Major}, {"voices", voices(4)},
          {"detune", tDetune(9)}, {"breath_mode", Whisper}, {"glide", tGlide(0.03)}}},
        {"VOIX", "Chorale mineure", "Huit voix larges, ramenées en mineur, longue queue.",
         {{"table", Choir}, {"position", 0.6}, {"quantize", Minor}, {"voices", voices(8)}, {"detune", tDetune(22)}, {"spread", 0.4},
          {"width", 1.0}, {"formant", 0.9}, {"breath_mode", Whisper}, {"release", tRelease(3.0 * 0.4)}}},
        {"VOIX", "Voix d'orgue", "Registres d'orgue qui prononcent les voyelles.",
         {{"table", Organ}, {"position", 0.35}, {"formant", 0.7}, {"voices", voices(2)}, {"sub", tSub(-14)},
          {"breath_mode", Input}, {"release", tRelease(3.0 * 0.25)}}},
        {"VOIX", "Ange à l'octave", "Une octave au-dessus, voyelles un peu plus graves pour rester humaines.",
         {{"table", Choir}, {"position", 0.2}, {"transpose", tSemitones(12)}, {"formant", 0.6}, {"formant_shift", 0.5 - 3.0 / 24.0},
          {"voices", voices(4)}, {"detune", tDetune(12)}, {"width", 0.8}, {"breath_mode", Air}, {"tone", tTone(8000)}}},
        {"VOIX", "Basse chantée", "Une octave plus bas, formants remontés : on comprend encore les mots.",
         {{"table", Hollow}, {"position", 0.3}, {"transpose", tSemitones(-12)}, {"sub", tSub(-6)}, {"voices", voices(1)}, {"formant", 0.7},
          {"formant_shift", 0.5 + 4.0 / 24.0}, {"breath_mode", Input}, {"drive", 0.2}}},
        {"VOIX", "Voix FM", "FM qui s'ouvre avec la brillance de la voix, en dorien.",
         {{"table", FM}, {"position", 0.4}, {"track", tBipolar(0.5)}, {"formant", 0.8}, {"voices", voices(2)}, {"detune", tDetune(6)},
          {"breath_mode", Air}, {"quantize", Dorian}}},
        {"VOIX", "Talkbox", "Sync saturée, formants à fond : la voix qui parle dans un tuyau.",
         {{"table", Sync}, {"position", 0.5}, {"formant", 1.0}, {"voices", voices(1)}, {"drive", 0.3}, {"tone", tTone(5000)},
          {"breath_mode", Whisper}, {"glide", tGlide(0.02)}, {"track", tBipolar(0.3)}}},
        {"VOIX", "Murmure", "Surtout du souffle qui garde les voyelles, un peu de note dessous.",
         {{"breath", 0.9}, {"table", Vowel}, {"position", 0.7}, {"formant", 1.0}, {"breath_mode", Whisper}, {"voices", voices(3)},
          {"voiced", 0.2}, {"tone", tTone(6000)}, {"mix", 0.9}}},

        // ------------------------------------------------------------------ SOUFFLE
        {"SOUFFLE", "Flûte en FM", "Le souffle de flûte reste, la note passe dans une FM qui s'ouvre quand on joue fort.",
         {{"breath", 0.6}, {"table", FM}, {"position", 0.25}, {"track", tBipolar(0.5)}, {"formant", 0.3}, {"voices", voices(1)},
          {"breath_mode", Flute}, {"detune", tDetune(6)}}},
        {"SOUFFLE", "Flûte de verre", "Presque une sinusoïde, beaucoup de souffle sur les harmoniques.",
         {{"breath", 0.6}, {"table", Basic}, {"position", 0.05}, {"track", tBipolar(0.2)}, {"formant", 0.2}, {"voices", voices(2)}, {"detune", tDetune(4)},
          {"breath_mode", Flute}, {"tone", tTone(10000)}}},
        {"SOUFFLE", "Shakuhachi", "Creux, très soufflé, ramené sur la gamme In-Sen.",
         {{"breath", 0.65}, {"table", Hollow}, {"position", 0.2}, {"quantize", InSen}, {"breath_mode", Flute}, {"formant", 0.2},
          {"voices", voices(1)}, {"glide", tGlide(0.06)}, {"release", tRelease(3.0 * 0.25)}}},
        {"SOUFFLE", "Orgue d'air", "Registres d'orgue, sous-octave, longue queue, air brillant.",
         {{"breath", 0.4}, {"table", Organ}, {"position", 0.5}, {"sub", tSub(-12)}, {"voices", voices(3)}, {"formant", 0.2}, {"release", tRelease(3.0 * 0.35)},
          {"breath_mode", Air}}},
        {"SOUFFLE", "Pan au vent", "Flûte de Pan éparpillée sur une pentatonique majeure.",
         {{"breath", 0.6}, {"table", Basic}, {"position", 0.15}, {"breath_mode", Flute}, {"voices", voices(3)}, {"detune", tDetune(8)},
          {"spread", 0.2}, {"quantize", MajorPenta}}},
        {"SOUFFLE", "Sax numérique", "Impulsion qui s'ouvre avec le souffle, un peu de grain.",
         {{"breath", 0.4}, {"table", Pulse}, {"position", 0.25}, {"track", tBipolar(0.6)}, {"formant", 0.6}, {"drive", 0.25}, {"voices", voices(2)},
          {"breath_mode", Input}, {"tone", tTone(7000)}}},
        {"SOUFFLE", "Clarinette creuse", "Harmoniques impaires, souffle de bois.",
         {{"breath", 0.5}, {"table", Hollow}, {"position", 0.45}, {"track", tBipolar(0.3)}, {"formant", 0.3}, {"voices", voices(1)},
          {"breath_mode", Flute}, {"glide", tGlide(0.02)}}},
        {"SOUFFLE", "Ocarina", "Une octave au-dessus, ronde, en majeur.",
         {{"breath", 0.55}, {"table", Basic}, {"position", 0.12}, {"transpose", tSemitones(12)}, {"formant", 0.1}, {"voices", voices(1)},
          {"breath_mode", Flute}, {"quantize", Major}, {"glide", tGlide(0.03)}}},

        // ------------------------------------------------------------------ CORDES
        {"CORDES", "Violon synthétique", "Scie large et brillante qui suit l'archet.",
         {{"table", Basic}, {"position", 0.64}, {"track", tBipolar(0.3)}, {"voices", voices(5)}, {"detune", tDetune(15)}, {"width", 0.8},
          {"formant", 0.6}, {"tone", tTone(7000)}, {"breath_mode", Air}}},
        {"CORDES", "Violoncelle sous-octave", "Une octave plus bas, creux, avec une sous-basse.",
         {{"table", Hollow}, {"position", 0.6}, {"transpose", tSemitones(-12)}, {"sub", tSub(-8)}, {"voices", voices(3)}, {"formant", 0.5},
          {"release", tRelease(3.0 * 0.18)}, {"breath_mode", Input}}},
        {"CORDES", "Quatuor de verre", "Quatre voix éclatées, mineur harmonique.",
         {{"table", Shards}, {"position", 0.3}, {"voices", voices(4)}, {"detune", tDetune(10)}, {"spread", 0.5}, {"width", 1.0},
          {"formant", 0.5}, {"release", tRelease(3.0 * 0.3)}, {"quantize", HarmonicMinor}}},
        {"CORDES", "Archet granuleux", "L'archet en grains courts piochés dans le dernier quart de seconde.",
         {{"table", Basic}, {"position", 0.66}, {"voices", voices(6)}, {"detune", tDetune(14)},
          {"formant", 0.5}, {"width", 0.9}}},
        {"CORDES", "Ensemble 80", "Impulsion large, huit voix, filtre : un ensemble de cordes à l'ancienne.",
         {{"table", Pulse}, {"position", 0.1}, {"voices", voices(8)}, {"detune", tDetune(25)}, {"width", 1.0}, {"tone", tTone(6000)},
          {"release", tRelease(3.0 * 0.4)}, {"formant", 0.3}}},
        {"CORDES", "Alto sombre", "Pic résonant bas, filtre fermé.",
         {{"table", Sweep}, {"position", 0.15}, {"formant", 0.7}, {"voices", voices(3)}, {"tone", tTone(3000)}, {"release", tRelease(3.0 * 0.3)},
          {"breath_mode", Input}}},
        {"CORDES", "Violon fantôme", "Une octave au-dessus, en grains, avec de l'air.",
         {{"table", Choir}, {"position", 0.9}, {"transpose", tSemitones(12)}, {"voices", voices(4)}, 
          {"breath_mode", Air}, {"tone", tTone(9000)}, {"mix", 0.8}}},
        {"CORDES", "Contrebasse", "Une octave plus bas, sous-basse, filtre bas.",
         {{"table", Basic}, {"position", 0.45}, {"transpose", tSemitones(-12)}, {"sub", tSub(-3)}, {"voices", voices(1)}, {"tone", tTone(1500)},
          {"drive", 0.15}, {"formant", 0.3}}},

        // ------------------------------------------------------------------ GUITARE
        {"GUITARE", "Lead synchro", "Sync qui s'ouvre avec l'attaque, saturé : un lead de guitare-synthé.",
         {{"table", Sync}, {"position", 0.35}, {"track", tBipolar(0.7)}, {"drive", 0.35}, {"voices", voices(1)}, {"glide", tGlide(0.01)},
          {"release", tRelease(3.0 * 0.2)}, {"formant", 0.3}, {"voiced", 0.8}}},
        {"GUITARE", "Pluck numérique", "Impulsion étroite, relâchement court, filtré.",
         {{"table", Pulse}, {"position", 0.3}, {"release", tRelease(3.0 * 0.06)}, {"tone", tTone(4500)}, {"voices", voices(2)}, {"formant", 0.2},
          {"breath", 0.0}}},
        {"GUITARE", "Guitare-orgue", "Registres d'orgue sous les doigts, sous-octave légère.",
         {{"table", Organ}, {"position", 0.6}, {"voices", voices(2)}, {"sub", tSub(-10)}, {"release", tRelease(3.0 * 0.15)}, {"formant", 0.1},
          {"tone", tTone(6000)}}},
        {"GUITARE", "Fuzz pliée", "Plus on attaque fort et brillant, plus la table se replie.",
         {{"table", Fold}, {"position", 0.4}, {"track", tBipolar(0.8)}, {"drive", 0.7}, {"voices", voices(2)}, {"tone", tTone(5000)},
          {"formant", 0.2}}},
        {"GUITARE", "Arpège de verre", "Éclats courts, pentatonique majeure, un peu éparpillés.",
         {{"table", Shards}, {"position", 0.5}, {"release", tRelease(3.0 * 0.09)}, {"quantize", MajorPenta}, {"voices", voices(3)}, {"width", 0.7},
          }},
        {"GUITARE", "Bourdon fixe", "La note ne bouge plus (do 2) : la dynamique et la brillance du jeu font vivre la table. Marche avec des accords.",
         {{"table", Sweep}, {"position", 0.3}, {"pitch_mode", 1}, {"fixed_note", tNote(36)}, {"track", tBipolar(0.8)}, {"formant", 1.0},
          {"voices", voices(5)}, {"detune", tDetune(20)}, {"release", tRelease(3.0 * 0.4)}}},
        {"GUITARE", "Basse synthé", "Une octave plus bas, impulsion, sous-basse, saturée.",
         {{"table", Pulse}, {"position", 0.2}, {"transpose", tSemitones(-12)}, {"sub", tSub(-6)}, {"voices", voices(1)},
          {"release", tRelease(3.0 * 0.08)}, {"tone", tTone(1800)}, {"drive", 0.3}}},
        {"GUITARE", "Sitar numérique", "Sync aigre, gamme Bhairav, glisse courte.",
         {{"table", Sync}, {"position", 0.8}, {"quantize", Bhairav}, {"voices", voices(2)}, {"detune", tDetune(5)}, {"formant", 0.4},
          {"glide", tGlide(0.015)}, {"breath_mode", Input}}},

        // ------------------------------------------------------------------ LEAD
        {"LEAD", "Lead 8 bits", "Marches numériques, demi-ton strict, aucune glisse.",
         {{"table", Digital}, {"position", 0.9}, {"quantize", Chromatic}, {"glide", 0.0}, {"voices", voices(1)}}},
        {"LEAD", "Scie géante", "Huit scies désaccordées, très larges.",
         {{"table", Basic}, {"position", 0.66}, {"voices", voices(8)}, {"detune", tDetune(30)}, {"width", 1.0}, {"glide", tGlide(0.04)},
          {"tone", tTone(9000)}}},
        {"LEAD", "Lead d'Orient", "Creux, saturé, phrygien dominant.",
         {{"table", Hollow}, {"position", 0.5}, {"quantize", PhrygianDominant}, {"glide", tGlide(0.05)}, {"voices", voices(2)},
          {"formant", 0.3}, {"drive", 0.2}}},
        {"LEAD", "Lead voyelle", "La brillance du jeu fait parcourir les voyelles, en dorien.",
         {{"table", Vowel}, {"position", 0.5}, {"track", tBipolar(1.0)}, {"formant", 0.5}, {"voices", voices(2)}, {"drive", 0.2},
          {"quantize", Dorian}}},
        {"LEAD", "Lead Messiaen", "FM sur le troisième mode de Messiaen.",
         {{"table", FM}, {"position", 0.6}, {"quantize", Messiaen3}, {"voices", voices(3)}, {"spread", 0.3}, {"glide", tGlide(0.03)}}},
        {"LEAD", "Lead Hirajoshi", "Éclats et souffle de flûte, gamme Hirajoshi.",
         {{"breath", 0.5}, {"table", Shards}, {"position", 0.2}, {"quantize", Hirajoshi}, {"voices", voices(2)}, {"breath_mode", Flute},
          {"glide", tGlide(0.06)}}},

        // ------------------------------------------------------------------ PAD
        {"PAD", "Nappe lydienne", "Chœur large en lydien, glisse lente, très longue queue.",
         {{"table", Choir}, {"position", 0.5}, {"quantize", Lydian}, {"voices", voices(8)}, {"detune", tDetune(20)}, {"spread", 0.5},
          {"width", 1.0}, {"release", tRelease(3.0 * 0.9)}, {"glide", tGlide(0.3)}, {"tone", tTone(6000)}}},
        {"PAD", "Brume", "Chœur lointain, grains longs, chuchotement, filtre fermé.",
         {{"breath", 0.6}, {"table", Choir}, {"position", 0.7}, {"voices", voices(6)}, {"formant", 0.7},
          {"breath_mode", Whisper}, {"tone", tTone(3000)}, {"glide", tGlide(0.2)}, {"release", tRelease(3.0 * 0.5)}}},
        {"PAD", "Givre", "Une octave au-dessus, éclats éparpillés sur une seconde.",
         {{"table", Shards}, {"position", 0.9}, {"voices", voices(8)},
          {"transpose", tSemitones(12)}, {"width", 1.0}, {"release", tRelease(3.0 * 0.8)}, {"tone", tTone(10000)}}},
        {"PAD", "Nappe sous-octave", "Orgue doux une octave plus bas, six voix.",
         {{"table", Organ}, {"position", 0.3}, {"transpose", tSemitones(-12)}, {"sub", tSub(-10)}, {"voices", voices(6)},
          {"detune", tDetune(15)}, {"release", tRelease(3.0 * 0.8)}, {"glide", tGlide(0.2)}}},
        {"PAD", "Aube", "Chaque voix lit une autre région de la table, en majeur.",
         {{"table", Sweep}, {"position", 0.6}, {"track", tBipolar(0.5)}, {"voices", voices(7)}, {"detune", tDetune(18)}, {"spread", 0.7},
          {"release", tRelease(3.0 * 1.0)}, {"quantize", Major}, {"width", 1.0}}},
        {"PAD", "Velours", "Creux, filtré bas, souffle sombre.",
         {{"table", Hollow}, {"position", 0.7}, {"voices", voices(6)}, {"detune", tDetune(12)}, {"tone", tTone(2500)}, {"release", tRelease(3.0 * 0.7)},
          {"breath_mode", Input}}},
        {"PAD", "Cathédrale", "Plein jeu d'orgue, huit voix, dorien.",
         {{"table", Organ}, {"position", 0.8}, {"voices", voices(8)}, {"detune", tDetune(10)}, {"spread", 0.3}, {"release", tRelease(3.0 * 1.0)},
          {"quantize", Dorian}, {"sub", tSub(-18)}}},
        {"PAD", "Halo", "Une quinte au-dessus, voyelles en grains, majeur.",
         {{"table", Vowel}, {"position", 0.8}, {"voices", voices(7)}, 
          {"transpose", tSemitones(7)}, {"quantize", Major}, {"formant", 0.5}, {"width", 1.0}, {"release", tRelease(3.0 * 0.6)}}},

        // ------------------------------------------------------------------ TEXTURE
        {"TEXTURE", "Nuage de grains", "Huit voix piochent dans les deux dernières secondes : la phrase en nuage.",
         {{"table", Shards}, {"position", 0.5}, {"voices", voices(8)}, {"spread", 0.6},
          {"width", 1.0}, {"mix", 0.85}, {"formant", 0.4}}},
        {"TEXTURE", "Constellation", "Une octave au-dessus, en pentatonique, des grains courts qui scintillent.",
         {{"table", Shards}, {"position", 0.8}, {"quantize", MajorPenta}, {"voices", voices(8)},
          {"transpose", tSemitones(12)}, {"width", 1.0}, {"glide", 0.0}}},
        {"TEXTURE", "Poussière", "Grains de 30 ms, marches numériques, air.",
         {{"table", Digital}, {"position", 0.6}, {"voices", voices(8)}, {"width", 1.0},
          {"tone", tTone(7000)}, {"breath_mode", Air}}},
        {"TEXTURE", "Écho de soi", "Les deux dernières secondes rejouées lentement par-dessus.",
         {{"table", Choir}, {"position", 0.4}, {"voices", voices(6)}, {"glide", tGlide(0.3)},
          {"mix", 0.7}, {"release", tRelease(3.0 * 0.6)}}},
        {"TEXTURE", "Gamelan", "Éclats métalliques sur le pelog selisir, éparpillés.",
         {{"table", Shards}, {"position", 0.35}, {"quantize", PelogSelisir}, 
          {"voices", voices(6)}, {"release", tRelease(3.0 * 0.15)}}},
        {"TEXTURE", "Jardin de pierre", "Creux et soufflé, gamme Kumoi, en grains.",
         {{"breath", 0.5}, {"table", Hollow}, {"position", 0.3}, {"quantize", Kumoi}, {"voices", voices(4)},
          {"breath_mode", Flute}}},
        {"TEXTURE", "Kaléidoscope", "Toutes les régions de la table à la fois, par tons entiers.",
         {{"table", Sweep}, {"position", 0.5}, {"spread", 1.0}, {"voices", voices(8)}, 
          {"quantize", WholeTone}, {"width", 1.0}}},

        // ------------------------------------------------------------------ FX
        {"FX", "Plaque de Chladni", "Chaque voix lit une autre région de la table : la plaque change de mode.",
         {{"table", Sweep}, {"position", 0.5}, {"voices", voices(4)}, {"spread", 0.8}, 
          {"formant", 0.4}}},
        {"FX", "Octave dessous", "L'original garde sa place, le synthé double une octave plus bas.",
         {{"table", Basic}, {"position", 0.33}, {"transpose", tSemitones(-12)}, {"voices", voices(1)}, {"formant", 0.4}, {"sub", tSub(-6)},
          {"mix", 0.55}}},
        {"FX", "Fold sauvage", "Plus on joue fort et brillant, plus la table se replie.",
         {{"table", Fold}, {"position", 0.1}, {"track", tBipolar(1.0)}, {"drive", 0.6}, {"voices", voices(2)}, {"formant", 0.3}}},
        {"FX", "Robot dissonant", "Super locrien, désaccord fort, saturé.",
         {{"table", Digital}, {"position", 0.2}, {"quantize", SuperLocrian}, {"voices", voices(3)}, {"detune", tDetune(40)}, {"drive", 0.4}}},
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

#ifdef _MSC_VER
#pragma warning(pop)
#endif
