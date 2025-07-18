// Dynamic Range Destroyer (DRD)
// © 2025 David Flater
// GPLv3

// Embed lv2:minorVersion and lv2:microVersion (see DRD.ttl) in the so file.
// Retrieve with:  strings DRD.so | grep -F 'DRD version'
const char VersionString[] = "DRD version 2.6";

// The L in LV2 means LADSPA.
// The L in LADSPA means Linux.
// I ought to be able to use GNU extensions without any screaming.
// exp10f instead of powf(10.0f,
#define _GNU_SOURCE

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <lv2.h>      // lv2.h and lv2/core/lv2.h are duplicates.

#define DRD_URI "http://flaterco.com/lv2/DRD#"

typedef enum {
  DRD_ATTACK,
  DRD_DECAY,
  DRD_INPUT0,
  DRD_OUTPUT0,
  DRD_INPUT1,
  DRD_OUTPUT1,
  DRD_INPUT2,
  DRD_OUTPUT2,
  DRD_INPUT3,
  DRD_OUTPUT3,
  DRD_INPUT4,
  DRD_OUTPUT4,
  DRD_INPUT5,
  DRD_OUTPUT5,
  DRD_INPUT6,
  DRD_OUTPUT6,
  DRD_INPUT7,
  DRD_OUTPUT7,
  DRD_LAST
} PortIndex;

// Plugin private data structure
typedef struct {
  // Connected ports
  float *attackport, *decayport;
  float *buffer[16];  // one input one output up to 8 channels

  // Other state
  float   samplerate; // Hz as supplied by instantiate (reduced to float)
  uint8_t nchannels;  // 1, 2, 6, or 8
  float   volume;     // floating volume level, nominally 0 to 1, potentially
                      // 0 to ∞ if the input is already out of range
} DRD;

// Convert attack and decay parameters from seconds to a gain per sample.
// The logic here is equivalent to what happens in config_output in
// af_compand.c of FFmpeg 7.1.1.
static float convert_parameter(float s, float samplerate) {
  return (s > 1.0f / samplerate ?
	  1.0f - expf(-1.0f / (samplerate * s)) : 1.0f);
}

// Constantly used constants:  -15 dB, -50 dB, and -100 dB as levels
static const float n15dB = exp10f(-0.75f),
                   n50dB = exp10f(-2.5f),
                  n100dB = 1e-5f;

// Gain function
// in_lvl nominal range 0 to 1, permissible range 0 to ∞
// Returns gain, nominal range 0 to ∞, actual range 0 to 82.333
static float gainfn(float in_lvl) {
  // Fast paths for straight lines ≤ -100 dB and ≥ -50 dB
  if (in_lvl <= n100dB) return 1.0f;
  if (in_lvl >= n50dB) return n15dB/in_lvl;
  // Slow path for cubic Hermite spline connecting those two lines.  A factor
  // of 20 for conversion to and from dB has been cancelled out so that it's
  // just a function of the log10 of the level.
  const float in1 = log10f(in_lvl);
  const float in2 = in1*in1;
  const float in3 = in2*in1;
  return exp10f(-0.384f*in3 -4.52f*in2 -16.4f*in1 -17.0f);
}

// Initialize state variables prior to run().  samplerate and nchannels are
// set by instantiate.
static void initialize_DRD(DRD *drd) {
  drd->volume = n15dB;  // initial volume -15 dB
}

/*
   The `instantiate()` function is called by the host to create a new plugin
   instance.  The host passes the plugin descriptor, sample rate, and bundle
   path for plugins that need to load additional resources (e.g. waveforms).
   The features parameter contains host-provided features defined in LV2
   extensions, but this plugin does not use any.
*/
static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features) {
  DRD* drd = (DRD*)calloc(1, sizeof(DRD));
  drd->samplerate = rate; // double to float conversion
  if (!strcmp (descriptor->URI, DRD_URI "mono"))
    drd->nchannels = 1;
  else if (!strcmp (descriptor->URI, DRD_URI "stereo"))
    drd->nchannels = 2;
  else if (!strcmp (descriptor->URI, DRD_URI "6ch"))
    drd->nchannels = 6;
  else if (!strcmp (descriptor->URI, DRD_URI "8ch"))
    drd->nchannels = 8;
  else {
    free (drd);
    return NULL;
  }
  initialize_DRD(drd);  // Avert failure when activate() is never called
  return (LV2_Handle)drd;
}

/*
  share/doc/lv2/ns/lv2core.html
  To support lv2:hardRTCapable, the following constraints apply to
  connect_port() and run():
  - There is no use of malloc(), free() or any other heap memory management
    functions.
  - There is no use of any library functions which do not adhere to these
    rules.  The plugin may assume that the standard C math library functions
    are safe.
  - There is no access to files, devices, pipes, sockets, system calls, or
    any other mechanism that might result in the process or thread blocking.
  - The maximum amount of time for a run() call is bounded by some
    expression of the form A + B * sample_count, where A and B are platform
    specific constants.  Note that this bound does not depend on input
    signals or plugin state.
*/

/*
   The `connect_port()` method is called by the host to connect a particular
   port to a buffer.  The plugin must store the data location, but data may not
   be accessed except in run().
*/
// "Data present at the time of the connect_port() call MUST NOT be
// considered meaningful."
// "Note that connect_port() may be called before or after activate()."
// — lv2.h
static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
  DRD* drd = (DRD*)instance;
  if (port == DRD_ATTACK)
    drd->attackport = (float*)data;
  else if (port == DRD_DECAY)
    drd->decayport = (float*)data;
  else if (port < DRD_LAST)
    drd->buffer[port - DRD_INPUT0] = (float*)data;
}

/*
   The `activate()` method is called by the host to initialise and prepare
   the plugin instance for running.  The plugin must reset all internal state
   except for buffer locations set by `connect_port()`.
*/
// "When present, hosts MUST call this function once before run() is called
// for the first time." — lv2.h
// ffmpeg-7.1.1 + lilv-v0.24.26 tested:  activate is *never called*.
// The bug belongs to FFmpeg libavfilter/af_lv2.c
// Reported https://trac.ffmpeg.org/ticket/11661
static void activate(LV2_Handle instance) {
  DRD* drd = (DRD*)instance;
  initialize_DRD(drd);
}

/*
   The `run()` method is the main process function of the plugin.  It
   processes a block of audio in the audio context.
*/
static void run(LV2_Handle instance, uint32_t n_samples) {
  DRD* drd = (DRD*)instance;

  // Poll the control knobs to respond to changes.  Some hosts can change
  // these on the fly and some can't.  The defaults for attack and decay that
  // are specified in DRD.ttl should be supplied on attackport and decayport
  // if the user does not specify values.
  const float attack = convert_parameter(*drd->attackport, drd->samplerate);
  const float decay  = convert_parameter(*drd->decayport,  drd->samplerate);

  for (uint32_t pos = 0; pos < n_samples; ++pos) {
    // Estimate overall volume level as the maximum level of any channel
    float max_lvl = 0.0f;
    for (uint8_t chan=0; chan<drd->nchannels; ++chan) {
      const float chanlvl = fabsf(drd->buffer[chan*2][pos]);
      if (chanlvl > max_lvl) max_lvl = chanlvl;
    }
    // Update floating volume level based on max_lvl.  The logic here is
    // equivalent to update_volume in af_compand.c of FFmpeg 7.1.1.
    const float delta = max_lvl - drd->volume;
    drd->volume += delta * (delta > 0.0f ? attack : decay);
    // Get the gain corresponding to the floating volume level
    const float gain = gainfn(drd->volume);
    // Apply that gain to every channel (might clip)
    for (uint8_t chan=0; chan<drd->nchannels; ++chan)
      drd->buffer[chan*2+1][pos] = drd->buffer[chan*2][pos] * gain;
  }
}

// Destroy a plugin instance (counterpart to `instantiate()`).
static void cleanup(LV2_Handle instance) {
  free(instance);
}

// The omitted functions are deactivate and extension_data.  There's nothing
// to do in deactivate and this plugin has no extension data.
// lv2.h says they can be NULL if not needed.
static const LV2_Descriptor descriptor_mono = {DRD_URI "mono",
instantiate, connect_port, activate, run, NULL, cleanup, NULL};
static const LV2_Descriptor descriptor_stereo = {DRD_URI "stereo",
instantiate, connect_port, activate, run, NULL, cleanup, NULL};
static const LV2_Descriptor descriptor_6ch = {DRD_URI "6ch",
instantiate, connect_port, activate, run, NULL, cleanup, NULL};
static const LV2_Descriptor descriptor_8ch = {DRD_URI "8ch",
instantiate, connect_port, activate, run, NULL, cleanup, NULL};

/*
   The `lv2_descriptor()` function is the entry point to the plugin library.
   The host will load the library and call this function repeatedly with
   increasing indices to find all the plugins defined in the library.  The
   index is not an identifier:  the URI of the returned descriptor is used to
   determine the identify of the plugin.
*/
LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
  switch (index) {
  case 0:
    return &descriptor_mono;
  case 1:
    return &descriptor_stereo;
  case 2:
    return &descriptor_6ch;
  case 3:
    return &descriptor_8ch;
  default:
    return NULL;
  }
}
