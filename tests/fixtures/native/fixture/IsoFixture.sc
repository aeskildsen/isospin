// The class of the IsoFixture test UGen (isospin build checks only).
IsoFixture : UGen {
    *ar { |in, gain(1)|
        ^this.multiNew('audio', in, gain)
    }

    checkInputs { ^this.checkSameRateAsFirstInput }
}
