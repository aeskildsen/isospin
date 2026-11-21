// Pulsar: pulsar synthesis with three trains of sine grains (the fundamental
// and two formants), in two channels (left, right). Each train starts a
// sine grain with a Hann window at each period of freq. energy (0 to 1,
// usually an envelope) sets the levels, the grain durations and the rate of
// the LFOs; velocity makes the note louder and brighter. See Pulsar.md.
Pulsar : MultiOutUGen {
    *ar { |freq = 440, energy = 1, velocity = 0.5, formant1 = 2, formant2 = 6,
            overlap0 = 1, overlap1 = 2, overlap2 = 2,
            wobble = 1, detune = 3, jitter = 1, law = 0, width = 1|
        ^this.multiNew('audio', freq, energy, velocity, formant1, formant2,
            overlap0, overlap1, overlap2, wobble, detune, jitter, law, width)
    }

    init { |... theInputs|
        inputs = theInputs;
        ^this.initOutputs(2, rate)
    }
}
