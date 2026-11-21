// A/B render: two versions of a sound, rendered in NRT with the same events.
// tools/ab/ab.sh runs this file; see tools/ab/README.md for the config.
//
// Environment:
//   AB_CONFIG        the config file (it returns an Event)
//   AB_OUT           the output folder: a.wav and b.wav go there
//   AB_PLUGINS       the plugin folder of this tree (from scripts/build-sc-plugin.py)
//   AB_CORE_PLUGINS  optional: the folder of the core UGens of scsynth
(
var config = thisProcess.interpreter.executeFile("AB_CONFIG".getenv);
var out = "AB_OUT".getenv;
var core = "AB_CORE_PLUGINS".getenv ?? { Platform.resourceDir +/+ "../../lib/SuperCollider/plugins" };
var duration = config[\duration] ? 10;
var tail = config[\tail] ? 2;
var seed = config[\seed] ? 1234;
var render, sides;

sides = [\a, \b].collect { |key|
	var side = config[key];
	if(side.isNil or: { side[\def].isNil }) {
		"ab.scd: the config needs %: (def: SynthDef(...))".format(key).error;
		1.exit;
	};
	// The events need the SynthDesc to send the arguments of the SynthDef.
	SynthDescLib.global.add(side[\def].asSynthDesc);
	side.put(\key, key)
};

render = { |side, next|
	var opts = ServerOptions.new
		.numOutputBusChannels_(config[\channels] ? 2)
		.sampleRate_(config[\sampleRate] ? 48000)
		.blockSize_(config[\blockSize] ? 64)
		.memSize_(config[\memSize] ? 65536)
		.maxNodes_(4096)
		.ugenPluginsPath_([core, side[\plugins] ? "AB_PLUGINS".getenv]);
	var def = side[\def];
	var score, events;
	// The same random seed for the events of both sides.
	thisThread.randSeed = seed;
	events = if(config[\events].notNil) {
		config[\events].value(def.name).asScore(duration).score
	} {
		[[0.0, Synth.basicNew(def.name, Server.default, 1000).newMsg(nil, config[\args] ? [])]]
	};
	score = Score(
		(config[\setup] ? []).collect { |msg| [0.0, msg] }
		++ [[0.0, ['/d_recv', def.asBytes]]]
		++ events.reject { |e| e[1][0] == \c_set }
		++ [[duration + tail, [\c_set, 0, 0]]]
	);
	score.sort;
	score.recordNRT(nil, out +/+ side[\key] ++ ".wav",
		sampleRate: opts.sampleRate, headerFormat: "WAV", sampleFormat: "float",
		options: opts, duration: duration + tail,
		action: {
			"ab.scd: %.wav: %".format(side[\key], side[\name] ? def.name).postln;
			next.value
		});
};

render.(sides[0], { render.(sides[1], { 0.exit }) });
)
