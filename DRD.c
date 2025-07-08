// Dynamic Range Destroyer (DRD)
// © 2025 David Flater
// GPLv3

// Embed lv2:minorVersion and lv2:microVersion (see DRD.ttl) in the so file.
// Retrieve with:  strings DRD.so | grep -F 'DRD version'
const char VersionString[] = "DRD version 1.0";

#include <math.h>
#include <stdbool.h>
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
  float *buffer[16];    // one input one output up to 8 channels

  // Other state
  bool    init;         // flag for first call to run()
  double  samplerate;   // Hz as supplied by instantiate
  uint8_t nchannels;    // 1, 2, 6, or 8
  double  volume;       // floating volume level 0 to 1
  double  attack;       // gain per sample; see convert_parameter
  double  decay;        // gain per sample; see convert_parameter
} DRD;

// Convert attack and decay parameters from seconds to a gain per sample by
// the same logic used in af_compand.c config_output.
static double convert_parameter(DRD *drd, double s) {
  return (s > 1.0 / drd->samplerate ?
	  1.0 - exp(-1.0 / (drd->samplerate * s)) : 1.0);
}

// Set necessary values prior to run().  samplerate and nchannels are set by
// instantiate.  attack and decay are set by run on the first invocation
// (when init is true).  The defaults for attack and decay that are specified
// in DRD.ttl should be supplied on attackport and decayport if the user does
// not specify values.
static void initialize_DRD(DRD *drd) {
  drd->init = true;
  drd->volume = 0.17782794;  // initial volume -15 dB
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
            const LV2_Feature* const* features)
{
  DRD* drd = (DRD*)calloc(1, sizeof(DRD));
  drd->samplerate = rate;
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

// share/doc/lv2/ns/lv2core.html
// To support lv2:hardRTCapable, the following constraints apply to
// connect_port() and run():
// - There is no use of malloc(), free() or any other heap memory management
//   functions.
// - There is no use of any library functions which do not adhere to these
//   rules.  The plugin may assume that the standard C math library functions
//   are safe.
// - There is no access to files, devices, pipes, sockets, system calls, or
//   any other mechanism that might result in the process or thread blocking.
// - The maximum amount of time for a run() call is bounded by some
//   expression of the form A + B * sample_count, where A and B are platform
//   specific constants.  Note that this bound does not depend on input
//   signals or plugin state.

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

// Transfer function from input volume dB to target volume dB
// dB ranges from -∞ to 0 (but <= -100 is handled in get_gain anyway)
static double xfer(double in_dB) {
  if (in_dB <= -100.0) return in_dB;
  if (in_dB >= -50.0) return -15.0;
  // Cubic Hermite spline
  // The following polynomial has slope 1 at (-100, -100) and slope 0 at
  // (-50, -15).
  const double a = -3.0/3125.0, b = -113.0/500.0, c = -77.0/5.0, d = -340.0;
  const double in2 = in_dB*in_dB;
  const double in3 = in2*in_dB;
  return a*in3 + b*in2 + c*in_dB + d;
}

// Floating volume update function, same as in af_compand.c.
// in_lvl ranges from 0 to 1
static void update_volume(DRD *drd, double in_lvl) {
  const double delta = in_lvl - drd->volume;
  if (delta > 0.0)
    drd->volume += delta * drd->attack;
  else
    drd->volume += delta * drd->decay;
}

// Return the gain that should be applied at a given level.
// in_lvl ranges from 0 to 1
// gain is a multiplier from 0 to ∞
static double get_gain(double in_lvl) {
  // Shortcut <= -100 dB to avoid log10(0) and division by 0
  if (in_lvl <= 1e-5) return 1.0;
  const double in_dB = 20.0 * log10(in_lvl);
  const double out_dB = xfer(in_dB);
  const double out_lvl = pow(10.0, out_dB/20.0);
  return out_lvl/in_lvl;
}

/*
   The `run()` method is the main process function of the plugin.  It
   processes a block of audio in the audio context.
*/
static void run(LV2_Handle instance, uint32_t n_samples) {
  DRD* drd = (DRD*)instance;

  // Since we're not allowed to assume that data are valid yet in either
  // connect_port or activate, we have to do this here on the first
  // invocation (or more if they can change on the fly).
  //
  // The declared ranges of the parameters are not enforced prior to reaching
  // here.  convert_parameter treats negative values the same as zero.
  if (drd->init) {
    drd->init = false;
    drd->attack = convert_parameter(drd, *drd->attackport);
    drd->decay = convert_parameter(drd, *drd->decayport);
  }

  for (uint32_t pos = 0; pos < n_samples; ++pos) {
    // Estimate overall volume level as the maximum level of any channel
    float max_lvl = 0.0;
    for (uint8_t chan=0; chan<drd->nchannels; ++chan) {
      const float chanlvl = fabsf(drd->buffer[chan*2][pos]);
      if (chanlvl > max_lvl) max_lvl = chanlvl;
    }
    // Update floating volume level based on that
    update_volume(drd, max_lvl);
    // Get the gain corresponding to the floating volume level
    const double gain = get_gain(drd->volume);
    // Apply that gain to every channel (might clip)
    for (uint8_t chan=0; chan<drd->nchannels; ++chan)
      drd->buffer[chan*2+1][pos] = drd->buffer[chan*2][pos] * gain;
  }
}

static void deactivate(LV2_Handle instance) {}

// Destroy a plugin instance (counterpart to `instantiate()`).
static void cleanup(LV2_Handle instance) {
  free(instance);
}

// This plugin does not have any extension data.
static const void* extension_data(const char* uri) {
  return NULL;
}

// Apparently you can supply NULLs here instead of defining the functions
// that don't do anything (cf. dpl.lv2).
static const LV2_Descriptor descriptor_mono = {DRD_URI "mono",
instantiate, connect_port, activate, run, deactivate, cleanup, extension_data};
static const LV2_Descriptor descriptor_stereo = {DRD_URI "stereo",
instantiate, connect_port, activate, run, deactivate, cleanup, extension_data};
static const LV2_Descriptor descriptor_6ch = {DRD_URI "6ch",
instantiate, connect_port, activate, run, deactivate, cleanup, extension_data};
static const LV2_Descriptor descriptor_8ch = {DRD_URI "8ch",
instantiate, connect_port, activate, run, deactivate, cleanup, extension_data};

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
