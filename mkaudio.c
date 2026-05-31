#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <time.h>

/* ── format detection*/

typedef enum {
    FMT_OGG,
    FMT_WAV,
    FMT_AIFF,
    FMT_FLAC,
    FMT_MP3,
    FMT_AAC,
    FMT_ALAC,
} audio_fmt_t;

static audio_fmt_t parse_fmt(const char *s) {
    if (!s) return FMT_OGG;
    if (!strcasecmp(s, "ogg"))  return FMT_OGG;
    if (!strcasecmp(s, "wav"))  return FMT_WAV;
    if (!strcasecmp(s, "aiff") || !strcasecmp(s, "aif")) return FMT_AIFF;
    if (!strcasecmp(s, "flac")) return FMT_FLAC;
    if (!strcasecmp(s, "mp3"))  return FMT_MP3;
    if (!strcasecmp(s, "aac") || !strcasecmp(s, "m4a")) return FMT_AAC;
    if (!strcasecmp(s, "alac")) return FMT_ALAC;
    fprintf(stderr, "Unknown format '%s', defaulting to ogg\n", s);
    return FMT_OGG;
}



#define SAMPLE_RATE 44100
#define TWO_PI 6.28318530718f
#define MAX_TONES 128

typedef enum { WAVE_SINE, WAVE_SQUARE, WAVE_TRIANGLE, WAVE_SAW, WAVE_NOISE } wave_t;

typedef struct { float a, d, s, r; } adsr_t;

static float lerp(float a, float b, float t)  { return a + (b-a)*t; }

static float adsr_env(adsr_t e, int i, int total) {
    float t = (float)i / total;
    if (t < e.a)            return t / e.a;
    if (t < e.a + e.d)      return 1.0f - ((t - e.a) / e.d) * (1.0f - e.s);
    if (t < 1.0f - e.r)     return e.s;
    return e.s * (1.0f - (t - (1.0f - e.r)) / e.r);
}

static float osc(wave_t w, float phase) {
    switch (w) {
        case WAVE_SINE:     return sinf(phase);
        case WAVE_SQUARE:   return sinf(phase) > 0 ? 1.0f : -1.0f;
        case WAVE_TRIANGLE: return asinf(sinf(phase)) * (2.0f / (float)M_PI);
        case WAVE_SAW:      return (2.0f / (float)M_PI) * (phase - (float)M_PI * floorf(phase / (float)M_PI + 0.5f));
        case WAVE_NOISE:    return ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
        default:            return 0;
    }
}

static int render(float *tones, int n,
                  wave_t wave, float volume, int glide,
                  float pan, float dist, adsr_t env,
                  float **out_buf)
{
    int total_frames = 0;
    for (int i = 0; i < n; i += 2)
        total_frames += (int)(tones[i+1] * SAMPLE_RATE);

    float *buf = malloc(total_frames * 2 * sizeof(float)); /* stereo interleaved */
    if (!buf) { perror("malloc"); return -1; }

    float phase = 0;
    int frame = 0;

    for (int i = 0; i < n; i += 2) {
        float f1   = tones[i];
        float dur  = tones[i+1];
        float f2   = (i+2 < n) ? tones[i+2] : f1;
        int   samp = (int)(dur * SAMPLE_RATE);

        for (int j = 0; j < samp; j++) {
            float t    = (float)j / samp;
            float freq = glide ? lerp(f1, f2, t) : f1;
            float envv = adsr_env(env, j, samp);
            float s    = osc(wave, phase);
            s = tanhf(s * (1.0f + dist * 5.0f));
            s *= volume * envv;

            buf[(frame+j)*2 + 0] = s * (1.0f - pan);   /* L */
            buf[(frame+j)*2 + 1] = s * (1.0f + pan);   /* R */

            phase += TWO_PI * freq / SAMPLE_RATE;
        }
        frame += samp;
    }

    *out_buf = buf;
    return total_frames;
}


static int16_t f2s16(float x) {
    if (x >  1.0f) x =  1.0f;
    if (x < -1.0f) x = -1.0f;
    return (int16_t)(x * 32767.0f);
}


static void write_u16be(FILE *f, uint16_t v) {
    fputc((v>>8)&0xff, f); fputc(v&0xff, f);
}
static void write_u32be(FILE *f, uint32_t v) {
    fputc((v>>24)&0xff,f); fputc((v>>16)&0xff,f);
    fputc((v>>8)&0xff,f);  fputc(v&0xff,f);
}
/* 80-bit IEEE 754 extended (AIFF sample-rate field) */
static void write_f80(FILE *f, double rate) {
    uint16_t exp = 0x3fff + 31;
    uint32_t hi  = (uint32_t)rate;
    uint32_t lo  = 0;
    write_u16be(f, exp);
    write_u32be(f, hi);
    write_u32be(f, lo);
}


static void write_u16le(FILE *f, uint16_t v) {
    fputc(v&0xff,f); fputc((v>>8)&0xff,f);
}
static void write_u32le(FILE *f, uint32_t v) {
    fputc(v&0xff,f); fputc((v>>8)&0xff,f);
    fputc((v>>16)&0xff,f); fputc((v>>24)&0xff,f);
}

static int write_wav(const char *path, float *buf, int frames) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }

    int channels   = 2;
    int bps        = 16;
    int byte_rate  = SAMPLE_RATE * channels * bps/8;
    int data_bytes = frames * channels * bps/8;

    /* RIFF header */
    fwrite("RIFF", 1, 4, f);
    write_u32le(f, 36 + data_bytes);
    fwrite("WAVE", 1, 4, f);

    /* fmt  chunk */
    fwrite("fmt ", 1, 4, f);
    write_u32le(f, 16);           /* chunk size */
    write_u16le(f, 1);            /* PCM */
    write_u16le(f, channels);
    write_u32le(f, SAMPLE_RATE);
    write_u32le(f, byte_rate);
    write_u16le(f, channels * bps/8); /* block align */
    write_u16le(f, bps);

    /* data chunk */
    fwrite("data", 1, 4, f);
    write_u32le(f, data_bytes);

    for (int i = 0; i < frames * 2; i++) {
        int16_t s = f2s16(buf[i]);
        fputc(s & 0xff, f);
        fputc((s >> 8) & 0xff, f);
    }

    fclose(f);
    return 0;
}

static int write_aiff(const char *path, float *buf, int frames) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }

    int channels   = 2;
    int bps        = 16;
    int ssnd_bytes = frames * channels * (bps/8);
    int comm_size  = 18;
    int ssnd_chunk = ssnd_bytes + 8;                /* +8 for offset/blockSize */
    int form_size  = 4 + (8+comm_size) + (8+ssnd_chunk);

    fwrite("FORM", 1, 4, f);
    write_u32be(f, form_size);
    fwrite("AIFF", 1, 4, f);

    /* COMM chunk */
    fwrite("COMM", 1, 4, f);
    write_u32be(f, comm_size);
    write_u16be(f, channels);
    write_u32be(f, frames);
    write_u16be(f, bps);
    write_f80(f, SAMPLE_RATE);

    /* SSND chunk */
    fwrite("SSND", 1, 4, f);
    write_u32be(f, ssnd_chunk);
    write_u32be(f, 0);   /* offset */
    write_u32be(f, 0);   /* blockSize */

    for (int i = 0; i < frames * 2; i++) {
        int16_t s = f2s16(buf[i]);
        fputc((s >> 8) & 0xff, f);
        fputc(s & 0xff, f);
    }

    fclose(f);
    return 0;
}


#ifdef HAVE_VORBIS
#include <vorbis/vorbisenc.h>

static int write_ogg(const char *path, float *buf, int frames) {
    FILE *out = fopen(path, "wb");
    if (!out) { perror(path); return 1; }

    vorbis_info      vi;  vorbis_info_init(&vi);
    vorbis_encode_init_vbr(&vi, 2, SAMPLE_RATE, 0.4f);

    vorbis_comment   vc;  vorbis_comment_init(&vc);
    vorbis_dsp_state vd;  vorbis_analysis_init(&vd, &vi);
    vorbis_block     vb;  vorbis_block_init(&vd, &vb);

    ogg_stream_state os;  ogg_stream_init(&os, rand());

    ogg_packet h1,h2,h3;
    vorbis_analysis_headerout(&vd, &vc, &h1, &h2, &h3);
    ogg_stream_packetin(&os, &h1);
    ogg_stream_packetin(&os, &h2);
    ogg_stream_packetin(&os, &h3);

    ogg_page og;
    while (ogg_stream_flush(&os, &og)) {
        fwrite(og.header, 1, og.header_len, out);
        fwrite(og.body,   1, og.body_len,   out);
    }

    int pos = 0, chunk = 4096;
    while (pos < frames) {
        int todo = (pos+chunk > frames) ? frames-pos : chunk;
        float **vbuf = vorbis_analysis_buffer(&vd, todo);
        for (int j = 0; j < todo; j++) {
            vbuf[0][j] = buf[(pos+j)*2+0];
            vbuf[1][j] = buf[(pos+j)*2+1];
        }
        vorbis_analysis_wrote(&vd, todo);
        pos += todo;

        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, NULL);
            vorbis_bitrate_addblock(&vb);
            ogg_packet op;
            while (vorbis_bitrate_flushpacket(&vd, &op)) {
                ogg_stream_packetin(&os, &op);
                while (ogg_stream_pageout(&os, &og)) {
                    fwrite(og.header, 1, og.header_len, out);
                    fwrite(og.body,   1, og.body_len,   out);
                }
            }
        }
    }
    vorbis_analysis_wrote(&vd, 0);
    while (vorbis_analysis_blockout(&vd, &vb) == 1) {
        vorbis_analysis(&vb, NULL);
        vorbis_bitrate_addblock(&vb);
        ogg_packet op;
        while (vorbis_bitrate_flushpacket(&vd, &op)) {
            ogg_stream_packetin(&os, &op);
            ogg_page og2;
            while (ogg_stream_pageout(&os, &og2)) {
                fwrite(og2.header, 1, og2.header_len, out);
                fwrite(og2.body,   1, og2.body_len,   out);
            }
        }
    }

    ogg_stream_clear(&os);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    fclose(out);
    return 0;
}
#endif /* HAVE_VORBIS */


#ifdef HAVE_FLAC
#include <FLAC/stream_encoder.h>

static int write_flac(const char *path, float *buf, int frames) {
    FLAC__StreamEncoder *enc = FLAC__stream_encoder_new();
    if (!enc) { fprintf(stderr, "FLAC encoder alloc failed\n"); return 1; }

    FLAC__stream_encoder_set_channels(enc, 2);
    FLAC__stream_encoder_set_bits_per_sample(enc, 16);
    FLAC__stream_encoder_set_sample_rate(enc, SAMPLE_RATE);
    FLAC__stream_encoder_set_total_samples_estimate(enc, frames);
    FLAC__stream_encoder_set_compression_level(enc, 5);

    FLAC__StreamEncoderInitStatus status =
        FLAC__stream_encoder_init_file(enc, path, NULL, NULL);
    if (status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
        fprintf(stderr, "FLAC init: %s\n", FLAC__StreamEncoderInitStatusString[status]);
        FLAC__stream_encoder_delete(enc);
        return 1;
    }

    int batch = 4096;
    FLAC__int32 *ibuf = malloc(batch * 2 * sizeof(FLAC__int32));
    int pos = 0;
    while (pos < frames) {
        int todo = (pos+batch > frames) ? frames-pos : batch;
        for (int j = 0; j < todo; j++) {
            ibuf[j*2+0] = (FLAC__int32)f2s16(buf[(pos+j)*2+0]);
            ibuf[j*2+1] = (FLAC__int32)f2s16(buf[(pos+j)*2+1]);
        }
        FLAC__stream_encoder_process_interleaved(enc, ibuf, todo);
        pos += todo;
    }
    free(ibuf);
    FLAC__stream_encoder_finish(enc);
    FLAC__stream_encoder_delete(enc);
    return 0;
}
#endif /* HAVE_FLAC */

#ifdef HAVE_LAME
#include <lame/lame.h>

static int write_mp3(const char *path, float *buf, int frames) {
    lame_t lame = lame_init();
    lame_set_in_samplerate(lame, SAMPLE_RATE);
    lame_set_num_channels(lame, 2);
    lame_set_VBR(lame, vbr_default);
    lame_set_VBR_quality(lame, 4);
    lame_init_params(lame);

    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); lame_close(lame); return 1; }

    int batch = 4096;
    int mp3buf_size = batch * 5/4 + 7200;
    short *left  = malloc(batch * sizeof(short));
    short *right = malloc(batch * sizeof(short));
    unsigned char *mp3buf = malloc(mp3buf_size);

    int pos = 0;
    while (pos < frames) {
        int todo = (pos+batch > frames) ? frames-pos : batch;
        for (int j = 0; j < todo; j++) {
            left[j]  = f2s16(buf[(pos+j)*2+0]);
            right[j] = f2s16(buf[(pos+j)*2+1]);
        }
        int bytes = lame_encode_buffer(lame, left, right, todo, mp3buf, mp3buf_size);
        if (bytes > 0) fwrite(mp3buf, 1, bytes, f);
        pos += todo;
    }
    int bytes = lame_encode_flush(lame, mp3buf, mp3buf_size);
    if (bytes > 0) fwrite(mp3buf, 1, bytes, f);

    free(left); free(right); free(mp3buf);
    lame_close(lame);
    fclose(f);
    return 0;
}
#endif /* HAVE_LAME */

#ifdef HAVE_FDK_AAC
#include <fdk-aac/aacenc_lib.h>

static int write_aac(const char *path, float *buf, int frames) {
    HANDLE_AACENCODER enc;
    if (aacEncOpen(&enc, 0, 2) != AACENC_OK) {
        fprintf(stderr, "AAC encoder open failed\n"); return 1;
    }
    aacEncoder_SetParam(enc, AACENC_AOT,        2);          /* LC */
    aacEncoder_SetParam(enc, AACENC_SAMPLERATE,  SAMPLE_RATE);
    aacEncoder_SetParam(enc, AACENC_CHANNELMODE, MODE_2);
    aacEncoder_SetParam(enc, AACENC_BITRATE,     128000);
    aacEncoder_SetParam(enc, AACENC_TRANSMUX,    2);          /* ADTS */
    if (aacEncEncode(enc, NULL, NULL, NULL, NULL) != AACENC_OK) {
        fprintf(stderr, "AAC init failed\n"); aacEncClose(&enc); return 1;
    }

    AACENC_InfoStruct info;
    aacEncInfo(enc, &info);
    int frame_size = info.frameLength;  /* typically 1024 */

    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); aacEncClose(&enc); return 1; }

    int out_size = 6144 * 2 / 8 + 1;
    short   *pcm = malloc(frame_size * 2 * sizeof(short));
    uint8_t *out = malloc(out_size);

    int pos = 0;
    while (pos < frames) {
        int todo = (pos+frame_size > frames) ? frames-pos : frame_size;
        /* zero-pad last frame */
        memset(pcm, 0, frame_size * 2 * sizeof(short));
        for (int j = 0; j < todo; j++) {
            pcm[j*2+0] = f2s16(buf[(pos+j)*2+0]);
            pcm[j*2+1] = f2s16(buf[(pos+j)*2+1]);
        }

        AACENC_BufDesc in_bd  = {0}, out_bd = {0};
        AACENC_InArgs  in_a   = {0};
        AACENC_OutArgs out_a  = {0};

        void *in_ptr   = pcm;
        void *out_ptr  = out;
        int   in_id    = IN_AUDIO_DATA;
        int   out_id   = OUT_BITSTREAM_DATA;
        int   in_sz    = frame_size * 2 * sizeof(short);
        int   in_el    = sizeof(short);
        int   out_sz   = out_size;
        int   out_el   = sizeof(uint8_t);

        in_bd.numBufs  = 1; in_bd.bufs  = &in_ptr;
        in_bd.bufferIdentifiers = &in_id;
        in_bd.bufSizes = &in_sz; in_bd.bufElSizes = &in_el;

        out_bd.numBufs = 1; out_bd.bufs = &out_ptr;
        out_bd.bufferIdentifiers = &out_id;
        out_bd.bufSizes = &out_sz; out_bd.bufElSizes = &out_el;

        in_a.numInSamples = frame_size * 2;

        if (aacEncEncode(enc, &in_bd, &out_bd, &in_a, &out_a) == AACENC_OK)
            if (out_a.numOutBytes > 0)
                fwrite(out, 1, out_a.numOutBytes, f);
        pos += todo;
    }

    /* flush */
    AACENC_BufDesc out_bd = {0};
    AACENC_InArgs  in_a   = { .numInSamples = -1 };
    AACENC_OutArgs out_a  = {0};
    void *out_ptr = out; int out_id = OUT_BITSTREAM_DATA;
    int   out_sz  = out_size; int out_el = sizeof(uint8_t);
    out_bd.numBufs = 1; out_bd.bufs = &out_ptr;
    out_bd.bufferIdentifiers = &out_id;
    out_bd.bufSizes = &out_sz; out_bd.bufElSizes = &out_el;
    if (aacEncEncode(enc, NULL, &out_bd, &in_a, &out_a) == AACENC_OK)
        if (out_a.numOutBytes > 0)
            fwrite(out, 1, out_a.numOutBytes, f);

    free(pcm); free(out);
    aacEncClose(&enc);
    fclose(f);
    return 0;
}
#endif /* HAVE_FDK_AAC */

/* Alac writer, BUT it's still under maintenance and fallbacks to wav */
static int write_alac(const char *path, float *buf, int frames) {
#ifdef HAVE_ALAC
    fprintf(stderr, "ALAC: Falling back to WAV\n");
#else
    fprintf(stderr, "ALAC: libalac not found at compile time. Writing WAV instead.\n");
#endif

    return write_wav(path, buf, frames);
}

int main(int argc, char **argv) {

    srand((unsigned)time(NULL));

    char      *tone_str  = NULL;
    char      *outfile   = NULL;
    char      *fmt_str   = NULL;

    wave_t     wave   = WAVE_SINE;
    float      volume = 0.5f;
    int        glide  = 0;
    float      pan    = 0.0f;
    float      dist   = 0.0f;
    adsr_t     env    = {0.01f, 0.1f, 0.7f, 0.2f};

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "-tone")           && i+1<argc) tone_str = argv[++i];
        else if (!strcmp(argv[i], "-o")              && i+1<argc) outfile  = argv[++i];
        else if (!strcmp(argv[i], "--audio-format")  && i+1<argc) fmt_str  = argv[++i];
        else if (!strcmp(argv[i], "--wave")          && i+1<argc) {
            char *w = argv[++i];
            if      (!strcmp(w,"square"))   wave = WAVE_SQUARE;
            else if (!strcmp(w,"triangle")) wave = WAVE_TRIANGLE;
            else if (!strcmp(w,"saw"))      wave = WAVE_SAW;
            else if (!strcmp(w,"noise"))    wave = WAVE_NOISE;
            else                            wave = WAVE_SINE;
        }
        else if (!strcmp(argv[i], "--volume") && i+1<argc) volume = atof(argv[++i]);
        else if (!strcmp(argv[i], "--glide"))              glide  = 1;
        else if (!strcmp(argv[i], "--pan")    && i+1<argc) pan    = atof(argv[++i]);
        else if (!strcmp(argv[i], "--dist")   && i+1<argc) dist   = atof(argv[++i]);
        else if (!strcmp(argv[i], "--adsr")   && i+4<argc) {
            env.a = atof(argv[++i]);
            env.d = atof(argv[++i]);
            env.s = atof(argv[++i]);
            env.r = atof(argv[++i]);
        }
        else if (!strcmp(argv[i], "--version")) {
            printf("mkaudio 2.0 by LICGX\n");
            return 0;
        }
    }

    if (!tone_str) { fprintf(stderr, "no -tone specified\n"); return 1; }


    audio_fmt_t fmt = parse_fmt(fmt_str);
    if (!outfile) {
        const char *ext[] = {"output.ogg","output.wav","output.aiff",
                             "output.flac","output.mp3","output.aac","output.m4a"};
        outfile = (char*)ext[fmt];
    }

    /* parse tones */
    float tones[MAX_TONES];
    int   n = 0;
    char *t = strtok(tone_str, " ");
    while (t && n < MAX_TONES) { tones[n++] = atof(t); t = strtok(NULL, " "); }
    if (n < 2) { fprintf(stderr, "need at least one freq+duration pair\n"); return 1; }

    /* render */
    float *buf   = NULL;
    int    frames = render(tones, n, wave, volume, glide, pan, dist, env, &buf);
    if (frames < 0) return 1;

    /* encode */
    int rc = 0;
    switch (fmt) {
        case FMT_WAV:  rc = write_wav(outfile, buf, frames);  break;
        case FMT_AIFF: rc = write_aiff(outfile, buf, frames); break;
        case FMT_ALAC: rc = write_alac(outfile, buf, frames); break;
#ifdef HAVE_VORBIS
        case FMT_OGG:  rc = write_ogg(outfile, buf, frames);  break;
#endif
#ifdef HAVE_FLAC
        case FMT_FLAC: rc = write_flac(outfile, buf, frames); break;
#endif
#ifdef HAVE_LAME
        case FMT_MP3:  rc = write_mp3(outfile, buf, frames);  break;
#endif
#ifdef HAVE_FDK_AAC
        case FMT_AAC:  rc = write_aac(outfile, buf, frames);  break;
#endif
        default:
            fprintf(stderr,
                "Format not compiled in. Rebuild with the appropriate -DHAVE_* flag.\n"
                "  OGG:  -DHAVE_VORBIS  -lvorbisenc -lvorbis -logg\n"
                "  FLAC: -DHAVE_FLAC    -lFLAC\n"
                "  MP3:  -DHAVE_LAME    -lmp3lame\n"
                "  AAC:  -DHAVE_FDK_AAC -lfdk-aac\n");
            rc = 1;
    }

    free(buf);
    if (!rc) printf("done: %s\n", outfile);
    return rc;
}
