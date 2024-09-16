// rtmpstreamer~.c
//
// A Pure Data external that streams audio via RTMP to a remote server.
//
// This external takes audio input and a URL string, and streams the audio to
// the RTMP server. If no URL is provided, it operates in a non-streaming mode
// without crashing.
//
// Dependencies:
// - FFmpeg libraries (libavformat, libavcodec, libavutil)
//
// Build with CMake and make.
//
// Author: Tony Rewin
// Date: 16.09.2024

#include "m_pd.h"
#include <stdio.h>
#include <string.h>

// Include FFmpeg headers
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>

// Define the class pointer
static t_class *rtmpstreamer_tilde_class;

// Define the object structure
typedef struct _rtmpstreamer_tilde {
  t_object x_obj;            // The object itself
  t_symbol *url;             // RTMP URL
  AVFormatContext *fmt_ctx;  // Format context
  AVStream *audio_st;        // Audio stream
  AVCodecContext *codec_ctx; // Codec context
  AVFrame *frame;            // Audio frame
  int64_t pts;               // Presentation timestamp
  t_sample f;                // Signal inlet placeholder
  int streaming_active;      // Flag to indicate if streaming is active
} t_rtmpstreamer_tilde;

// Function prototypes
void rtmpstreamer_tilde_symbol(t_rtmpstreamer_tilde *x, t_symbol *s);
void rtmpstreamer_tilde_dsp(t_rtmpstreamer_tilde *x, t_signal **sp);
t_int *rtmpstreamer_tilde_perform(t_int *w);
void *rtmpstreamer_tilde_new(t_symbol *s);
void rtmpstreamer_tilde_free(t_rtmpstreamer_tilde *x);
void rtmpstreamer_tilde_setup(void);

// Helper function prototypes
int initialize_streaming(t_rtmpstreamer_tilde *x);
void cleanup_streaming(t_rtmpstreamer_tilde *x);

// DSP method
void rtmpstreamer_tilde_dsp(t_rtmpstreamer_tilde *x, t_signal **sp) {
  // Add perform method to DSP chain
  dsp_add(rtmpstreamer_tilde_perform, 3, x, sp[0]->s_vec, sp[0]->s_n);
}

// Perform function
t_int *rtmpstreamer_tilde_perform(t_int *w) {
  t_rtmpstreamer_tilde *x = (t_rtmpstreamer_tilde *)(w[1]);
  t_sample *in = (t_sample *)(w[2]);
  int n = (int)(w[3]);

  // If streaming is active, process and send frames
  if (x->streaming_active) {
    int ret;

    // Prepare frame
    // For AAC, use floating point planar format
    float *samples = (float *)x->frame->data[0];

    for (int i = 0; i < n; i++) {
      float sample = in[i];
      // Clamp the sample to [-1.0, 1.0]
      if (sample < -1.0f)
        sample = -1.0f;
      if (sample > 1.0f)
        sample = 1.0f;
      samples[i] = sample;
    }

    x->frame->nb_samples = n;
    x->frame->pts = x->pts;
    x->pts += x->frame->nb_samples;

    // Send the frame to the encoder
    ret = avcodec_send_frame(x->codec_ctx, x->frame);
    if (ret < 0) {
      pd_error(x, "[rtmpstreamer~] Error sending frame to codec");
      return (w + 4);
    }

    AVPacket pkt = {0}; // Initialize the packet

    // Receive packets from the encoder
    while (ret >= 0) {
      ret = avcodec_receive_packet(x->codec_ctx, &pkt);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        break;
      else if (ret < 0) {
        pd_error(x, "[rtmpstreamer~] Error encoding audio frame");
        break;
      }

      // Set the stream index
      pkt.stream_index = x->audio_st->index;

      // Write the compressed frame to the media file
      ret = av_interleaved_write_frame(x->fmt_ctx, &pkt);
      av_packet_unref(&pkt);
      if (ret < 0) {
        pd_error(x, "[rtmpstreamer~] Error while writing audio frame");
        break;
      }
    }
  }

  // If streaming is inactive, optionally pass the audio through or do nothing
  // For this example, we'll do nothing to minimize CPU usage

  return (w + 4);
}

int is_valid_rtmp_url(const char* url) {
    // Simple check to verify that the URL starts with "rtmp://"
    return (strncmp(url, "rtmp://", 7) == 0);
}

// Constructor
void *rtmpstreamer_tilde_new(t_symbol *s) {
  t_rtmpstreamer_tilde *x =
      (t_rtmpstreamer_tilde *)pd_new(rtmpstreamer_tilde_class);

  x->url = NULL;
  x->fmt_ctx = NULL;
  x->codec_ctx = NULL;
  x->audio_st = NULL;
  x->frame = NULL;
  x->pts = 0;
  x->streaming_active = 0; // Initialize streaming as inactive

  // Create inlets and outlets
  inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_symbol,
            gensym("symbol"));      // For setting URL
  outlet_new(&x->x_obj, &s_signal); // Signal outlet

  // Do not start streaming at object creation if no valid URL
  if (s && strlen(s->s_name) > 0 && is_valid_rtmp_url(s->s_name)) {
    x->url = s;
    post("[rtmpstreamer~] Valid URL provided at creation: %s", x->url->s_name);
  } else {
    post("[rtmpstreamer~] Invalid or no URL provided at creation. "
         "Non-streaming mode.");
  }

  return (void *)x;
}

// Symbol handling (URL change)
void rtmpstreamer_tilde_symbol(t_rtmpstreamer_tilde *x, t_symbol *s) {
  // If streaming is active, clean up before switching URL
  if (x->streaming_active) {
    cleanup_streaming(x);
    x->streaming_active = 0;
  }

  // Set the new URL and attempt streaming initialization
  x->url = s;

  if (x->url && strlen(x->url->s_name) > 0) {
    post("[rtmpstreamer~] Attempting to stream to %s", x->url->s_name);
    if (initialize_streaming(x) == 0) {
      x->streaming_active = 1;
      post("[rtmpstreamer~] Successfully streaming to %s", x->url->s_name);
    } else {
      pd_error(x, "[rtmpstreamer~] Failed to initialize streaming to '%s'",
               x->url->s_name);
    }
  } else {
    post("[rtmpstreamer~] Invalid or empty URL. Non-streaming mode.");
  }
}

// Destructor
void rtmpstreamer_tilde_free(t_rtmpstreamer_tilde *x) {
  // Clean up streaming if active
  if (x->streaming_active) {
    cleanup_streaming(x);
  }
}

// Setup function
void rtmpstreamer_tilde_setup(void) {
  rtmpstreamer_tilde_class =
      class_new(gensym("rtmpstreamer~"), (t_newmethod)rtmpstreamer_tilde_new,
                (t_method)rtmpstreamer_tilde_free, sizeof(t_rtmpstreamer_tilde),
                CLASS_DEFAULT, A_DEFSYM, 0);

  class_addmethod(rtmpstreamer_tilde_class, (t_method)rtmpstreamer_tilde_dsp,
                  gensym("dsp"), A_CANT, 0);
  CLASS_MAINSIGNALIN(rtmpstreamer_tilde_class, t_rtmpstreamer_tilde, f);
  class_addsymbol(rtmpstreamer_tilde_class, rtmpstreamer_tilde_symbol);
}

// Helper function to initialize streaming
int initialize_streaming(t_rtmpstreamer_tilde *x) {
  // Initialize FFmpeg libraries
  avformat_network_init();

  // Allocate the output media context
  // Change the output format to "flv" which is commonly used with RTMP
  if (avformat_alloc_output_context2(&x->fmt_ctx, NULL, "flv", x->url->s_name) <
      0) {
    pd_error(x, "[rtmpstreamer~] Could not allocate output context");
    return -1;
  }

  // Find the encoder for AAC, which is standard for RTMP audio
  const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
  if (!codec) {
    pd_error(x, "[rtmpstreamer~] AAC codec not found");
    return -1;
  }

  // Create a new audio stream in the output file
  x->audio_st = avformat_new_stream(x->fmt_ctx, NULL);
  if (!x->audio_st) {
    pd_error(x, "[rtmpstreamer~] Could not allocate stream");
    return -1;
  }
  x->audio_st->id = x->fmt_ctx->nb_streams - 1;

  // Allocate and configure the codec context
  x->codec_ctx = avcodec_alloc_context3(codec);
  if (!x->codec_ctx) {
    pd_error(x, "[rtmpstreamer~] Could not allocate codec context");
    return -1;
  }

  // Initialize Channel Layout
  AVChannelLayout layout = {AV_CH_LAYOUT_MONO, 1, {0}, NULL};
  if (av_channel_layout_copy(&x->codec_ctx->ch_layout, &layout) < 0) {
    pd_error(x, "[rtspstreamer~] Could not set channel layout");
    return -1;
  }

  // Set codec parameters
  x->codec_ctx->sample_fmt =
      AV_SAMPLE_FMT_FLTP;          // AAC typically uses floating point planar
  x->codec_ctx->bit_rate = 128000; // Increased bitrate for better audio quality
  x->codec_ctx->sample_rate = sys_getsr(); // Get Pd's sample rate
  // x->codec_ctx->channels = 1;                   // Number of channels

  // Open the codec
  if (avcodec_open2(x->codec_ctx, codec, NULL) < 0) {
    pd_error(x, "[rtmpstreamer~] Could not open codec");
    return -1;
  }

  // Set the codec parameters to the stream
  if (avcodec_parameters_from_context(x->audio_st->codecpar, x->codec_ctx) <
      0) {
    pd_error(x, "[rtmpstreamer~] Could not copy codec parameters");
    return -1;
  }

  // Set stream time base
  x->audio_st->time_base = (AVRational){1, x->codec_ctx->sample_rate};

  // Open the output URL
  if (!(x->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&x->fmt_ctx->pb, x->url->s_name, AVIO_FLAG_WRITE) < 0) {
      pd_error(x, "[rtmpstreamer~] Could not open output URL '%s'",
               x->url->s_name);
      return -1;
    }
  }

  // Write the stream header
  AVDictionary *opts = NULL;
  // Set RTMP-specific options
  av_dict_set(&opts, "rtmp_buffer", "0.5", 0); // Example: set buffer duration
  av_dict_set(&opts, "rtmp_live", "live", 0);  // Set live streaming mode
  if (avformat_write_header(x->fmt_ctx, &opts) < 0) {
    pd_error(x, "[rtmpstreamer~] Error occurred when opening output URL");
    av_dict_free(&opts);
    return -1;
  }
  av_dict_free(&opts);

  // Allocate an audio frame
  x->frame = av_frame_alloc();
  if (!x->frame) {
    pd_error(x, "[rtmpstreamer~] Could not allocate audio frame");
    return -1;
  }

  x->frame->format = x->codec_ctx->sample_fmt;
  x->frame->sample_rate = x->codec_ctx->sample_rate;
  x->frame->nb_samples = x->codec_ctx->frame_size;
  if (x->frame->nb_samples == 0) {
    x->frame->nb_samples = 1024; // Set a default frame size
  }

  // Allocate the data buffers
  if (av_frame_get_buffer(x->frame, 0) < 0) {
    pd_error(x, "[rtmpstreamer~] Could not allocate audio data buffers");
    return -1;
  }

  return 0; // Success
}

// Helper function to clean up streaming
void cleanup_streaming(t_rtmpstreamer_tilde *x) {
  if (x->fmt_ctx) {
    av_write_trailer(x->fmt_ctx);
    if (!(x->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
      avio_closep(&x->fmt_ctx->pb);
    }
    avformat_free_context(x->fmt_ctx);
    x->fmt_ctx = NULL;
  }
  if (x->codec_ctx) {
    avcodec_free_context(&x->codec_ctx);
    x->codec_ctx = NULL;
  }
  if (x->frame) {
    av_frame_free(&x->frame);
    x->frame = NULL;
  }
  avformat_network_deinit();
}