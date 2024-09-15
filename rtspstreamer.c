// rtspstreamer~.c
//
// A Pure Data external that streams audio via RTSP to a remote server.
//
// This external takes audio input and a URL string, and streams the audio to the RTSP server.
//
// Dependencies:
// - FFmpeg libraries (libavformat, libavcodec, libavutil)
//
// Build with CMake and make.
//
// Author: Tony Rewin
// Date: 15.09.2024

#include "m_pd.h"
#include <stdio.h>
#include <string.h>

// Include FFmpeg headers
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/avassert.h>


// Define the class pointer
static t_class *rtspstreamer_tilde_class;

// Define the object structure
typedef struct _rtspstreamer_tilde {
    t_object x_obj;           // The object itself
    t_symbol *url;            // RTSP URL
    AVFormatContext *fmt_ctx; // Format context
    AVStream *audio_st;       // Audio stream
    AVCodecContext *codec_ctx;// Codec context
    AVFrame *frame;           // Audio frame
    int64_t pts;              // Presentation timestamp
    t_sample f;               // Signal inlet placeholder
} t_rtspstreamer_tilde;

// Function prototypes
void rtspstreamer_tilde_symbol(t_rtspstreamer_tilde *x, t_symbol *s);
void rtspstreamer_tilde_dsp(t_rtspstreamer_tilde *x, t_signal **sp);
t_int *rtspstreamer_tilde_perform(t_int *w);
void *rtspstreamer_tilde_new(t_symbol *s);
void rtspstreamer_tilde_free(t_rtspstreamer_tilde *x);
void rtspstreamer_tilde_setup(void);

// Method called when a symbol is sent to the inlet (URL)
void rtspstreamer_tilde_symbol(t_rtspstreamer_tilde *x, t_symbol *s) {
    x->url = s;
    // Currently, changing the URL at runtime is not implemented
    pd_error(x, "Changing URL at runtime is not supported");
}

// DSP method
void rtspstreamer_tilde_dsp(t_rtspstreamer_tilde *x, t_signal **sp) {
    // Add perform method to DSP chain
    dsp_add(rtspstreamer_tilde_perform, 3, x, sp[0]->s_vec, sp[0]->s_n);
}

// Perform function
t_int *rtspstreamer_tilde_perform(t_int *w) {
    t_rtspstreamer_tilde *x = (t_rtspstreamer_tilde *)(w[1]);
    t_sample *in = (t_sample *)(w[2]);
    int n = (int)(w[3]);

    int ret;

    // Prepare frame
    int16_t *samples = (int16_t *)x->frame->data[0];

    for (int i = 0; i < n; i++) {
        float sample = in[i];
        if (sample < -1.0f) sample = -1.0f;
        if (sample > 1.0f) sample = 1.0f;
        samples[i] = (int16_t)(sample * 32767.0f);
    }

    x->frame->nb_samples = n;
    x->frame->pts = x->pts;
    x->pts += x->frame->nb_samples;

    // Send the frame to the encoder
    ret = avcodec_send_frame(x->codec_ctx, x->frame);
    if (ret < 0) {
        pd_error(x, "Error sending frame to codec");
        return w + 4;
    }

    AVPacket pkt = {0}; // Initialize the packet
    // No need to call av_init_packet

    // Receive packets from the encoder
    while (ret >= 0) {
        ret = avcodec_receive_packet(x->codec_ctx, &pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0) {
            pd_error(x, "Error encoding audio frame");
            break;
        }

        // Set the stream index
        pkt.stream_index = x->audio_st->index;

        // Write the compressed frame to the media file
        ret = av_interleaved_write_frame(x->fmt_ctx, &pkt);
        av_packet_unref(&pkt);
        if (ret < 0) {
            pd_error(x, "Error while writing audio frame");
            break;
        }
    }

    return (w + 4);
}

// Constructor
void *rtspstreamer_tilde_new(t_symbol *s) {
    t_rtspstreamer_tilde *x = (t_rtspstreamer_tilde *)pd_new(rtspstreamer_tilde_class);

    x->url = s;
    x->fmt_ctx = NULL;
    x->codec_ctx = NULL;
    x->audio_st = NULL;
    x->frame = NULL;
    x->pts = 0;

    // Initialize FFmpeg libraries
    avformat_network_init();

    // Allocate the output media context
    if (avformat_alloc_output_context2(&x->fmt_ctx, NULL, "rtsp", x->url->s_name) < 0) {
        pd_error(x, "Could not allocate output context");
        return NULL;
    }

    // Find the encoder for PCM S16LE
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    if (!codec) {
        pd_error(x, "Codec not found");
        return NULL;
    }

    // Create a new audio stream in the output file
    x->audio_st = avformat_new_stream(x->fmt_ctx, NULL);
    if (!x->audio_st) {
        pd_error(x, "Could not allocate stream");
        return NULL;
    }
    x->audio_st->id = x->fmt_ctx->nb_streams - 1;

    // Allocate and configure the codec context
    x->codec_ctx = avcodec_alloc_context3(codec);
    if (!x->codec_ctx) {
        pd_error(x, "Could not allocate codec context");
        return NULL;
    }

    // Set codec parameters
    x->codec_ctx->sample_fmt = AV_SAMPLE_FMT_S16;
    x->codec_ctx->bit_rate = 64000;
    x->codec_ctx->sample_rate = sys_getsr(); // Get Pd's sample rate

    // Set channel layout
    if (av_channel_layout_copy(&x->codec_ctx->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_MONO) < 0) {
        pd_error(x, "Could not set channel layout");
        return NULL;
    }

    // Set the codec parameters to the stream
    if (avcodec_parameters_from_context(x->audio_st->codecpar, x->codec_ctx) < 0) {
        pd_error(x, "Could not copy codec parameters");
        return NULL;
    }

    // Open the output URL
    if (!(x->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&x->fmt_ctx->pb, x->url->s_name, AVIO_FLAG_WRITE) < 0) {
            pd_error(x, "Could not open output URL '%s'", x->url->s_name);
            return NULL;
        }
    }

    // Write the stream header
    AVDictionary *opts = NULL;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0); // Use TCP for RTSP
    if (avformat_write_header(x->fmt_ctx, &opts) < 0) {
        pd_error(x, "Error occurred when opening output URL");
        av_dict_free(&opts);
        return NULL;
    }
    av_dict_free(&opts);

    // Allocate an audio frame
    x->frame = av_frame_alloc();
    if (!x->frame) {
        pd_error(x, "Could not allocate audio frame");
        return NULL;
    }

    x->frame->format = x->codec_ctx->sample_fmt;
    x->frame->sample_rate = x->codec_ctx->sample_rate;
    x->frame->nb_samples = x->codec_ctx->frame_size;
    if (x->frame->nb_samples == 0) {
        x->frame->nb_samples = 1024; // Set a default frame size
    }

    // Set channel layout in frame
    if (av_channel_layout_copy(&x->frame->ch_layout, &x->codec_ctx->ch_layout) < 0) {
        pd_error(x, "Could not copy channel layout to frame");
        return NULL;
    }

    // Allocate the data buffers
    if (av_frame_get_buffer(x->frame, 0) < 0) {
        pd_error(x, "Could not allocate audio data buffers");
        return NULL;
    }

    // Create inlets and outlets
    inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_symbol, gensym("symbol"));
    inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, gensym("signal"));
    outlet_new(&x->x_obj, &s_signal);

    return (void *)x;
}

// Destructor
void rtspstreamer_tilde_free(t_rtspstreamer_tilde *x) {
    if (x->fmt_ctx) {
        av_write_trailer(x->fmt_ctx);
        if (!(x->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&x->fmt_ctx->pb);
        }
        avformat_free_context(x->fmt_ctx);
    }
    if (x->codec_ctx) {
        av_channel_layout_uninit(&x->codec_ctx->ch_layout);
        avcodec_free_context(&x->codec_ctx);
    }
    if (x->frame) {
        av_channel_layout_uninit(&x->frame->ch_layout);
        av_frame_free(&x->frame);
    }
    avformat_network_deinit();
}

// Setup function
void rtspstreamer_tilde_setup(void) {
    rtspstreamer_tilde_class = class_new(gensym("rtspstreamer~"),
                                         (t_newmethod)rtspstreamer_tilde_new,
                                         (t_method)rtspstreamer_tilde_free,
                                         sizeof(t_rtspstreamer_tilde),
                                         CLASS_DEFAULT,
                                         A_DEFSYM, 0);

    class_addmethod(rtspstreamer_tilde_class, (t_method)rtspstreamer_tilde_dsp, gensym("dsp"), A_CANT, 0);
    CLASS_MAINSIGNALIN(rtspstreamer_tilde_class, t_rtspstreamer_tilde, f);
    class_addsymbol(rtspstreamer_tilde_class, rtspstreamer_tilde_symbol);
}
