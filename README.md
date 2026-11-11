# PRODAP Sample to Chord

Sample drop karo -> key, BPM, chords detect -> chord MIDI / bass MIDI FL Studio mein drag karo.

## VST3 banane ka sabse chhota tareeqa (PC pe kuch install nahi karna)

1. GitHub pe naya repo banao (Add file > Upload files) aur is folder ka SAB kuch upload karo (`.github` folder bhi). Commit karo.
2. Repo ke **Actions** tab mein "Build PRODAP Sample to Chord" > **Run workflow**. 5-10 min wait.
3. Run khulke neeche **PRODAP-Sample-to-Chord-Windows** download karo, zip kholo.
4. `PRODAP Sample to Chord.vst3` folder ko `C:\Program Files\Common Files\VST3\` mein copy karo.
5. FL Studio: Options > Manage plugins > Find installed plugins. Phir channel rack mein + > PRODAP Sample to Chord.

Preview mein awaaz na aaye to plugin ke gear icon mein "Smart disable" band karo.

## Local build (optional)

Visual Studio 2022 (Desktop development with C++), CMake, Git chahiye:

    cmake -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release --target PRODAP_SampleToChord_VST3

## Instruments (preview)

Piano, Keys, Rhodes, Synth, Pad, Organ, Strings, Guitar. Ye asli sampled sounds hain
(MuseScore General soundfont se bane, plugin ke andar embedded, alag file ki zaroorat nahi).
Bass preview ke liye electric bass. Instrument sirf preview ki awaaz badalta hai, MIDI same rehti hai.

## Accuracy

- Key-lock: chords hamesha ek hi scale ke andar (key aur chords saath mein decide hote hain), C minor sample pe D minor chords nahi aate
- Tuning estimate, drums/noise ka asar kam, 7th chords ko triad mein convert, reverb tail ignore
- Sample ka bar-start offset khud dhoondta hai, BPM fine-tune + whole-bars snap, double-tempo ki galti auto theek
- 4 bar sample -> 4 chords, 8 ya zyada bar -> pehle 8 bars ke 8 chords
- 100% guarantee audio se possible nahi. Galat chord pe click karke sahi chord chuno, preview aur MIDI turant update.

## License

JUCE AGPLv3 / commercial: free plugin ke saath source public rakhna hoga.
Instrument samples: MuseScore_General (MIT, parts public domain/CC0), details `THIRD_PARTY_NOTICES.txt`.
