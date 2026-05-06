#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vorbis/vorbisenc.h>

#define SAMPLE_RATE 44100
#define TWO_PI 6.28318530718

typedef enum {
    WAVE_SINE,
    WAVE_SQUARE,
    WAVE_TRIANGLE,
    WAVE_SAW,
    WAVE_NOISE
} wave_t;

float clamp(float x, float a, float b) {
    if (x < a) return a;
    if (x > b) return b;
    return x;
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

typedef struct {
    float a, d, s, r;
} adsr_t;

float adsr_env(adsr_t e, int i, int total) {
    float t = (float)i / total;

    if (t < e.a) return t / e.a; // attack
    if (t < e.a + e.d) return 1.0f - ((t - e.a) / e.d) * (1.0f - e.s);
    if (t < 1.0f - e.r) return e.s;
    return e.s * (1.0f - (t - (1.0f - e.r)) / e.r);
}

float osc(wave_t w, float phase) {
    switch (w) {
        case WAVE_SINE: return sinf(phase);
        case WAVE_SQUARE: return sinf(phase) > 0 ? 1.0f : -1.0f;
        case WAVE_TRIANGLE: return asinf(sinf(phase)) * (2.0f / M_PI);
        case WAVE_SAW: return (2.0f / M_PI) * (phase - M_PI * floorf(phase / M_PI + 0.5f));
        case WAVE_NOISE: return ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
        default: return 0;
    }
}

int main(int argc, char **argv) {

    char *tone_str = NULL;
    char *outfile = "output.ogg";

    wave_t wave = WAVE_SINE;
    float volume = 0.5f;
    int glide = 0;

    float pan = 0.0f; // -1 left, +1 right
    float dist = 0.0f;

    adsr_t env = {0.01f, 0.1f, 0.7f, 0.2f};

    for (int i = 1; i < argc; i++) {

        if (!strcmp(argv[i], "-tone") && i + 1 < argc) {
            tone_str = argv[++i];

        } else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            outfile = argv[++i];

        } else if (!strcmp(argv[i], "--wave") && i + 1 < argc) {
            char *w = argv[++i];
            if (!strcmp(w, "square")) wave = WAVE_SQUARE;
            else if (!strcmp(w, "triangle")) wave = WAVE_TRIANGLE;
            else if (!strcmp(w, "saw")) wave = WAVE_SAW;
            else if (!strcmp(w, "noise")) wave = WAVE_NOISE;
            else wave = WAVE_SINE;

        } else if (!strcmp(argv[i], "--volume") && i + 1 < argc) {
            volume = atof(argv[++i]);

        } else if (!strcmp(argv[i], "--glide")) {
            glide = 1;

        } else if (!strcmp(argv[i], "--pan") && i + 1 < argc) {
            pan = atof(argv[++i]);

        } else if (!strcmp(argv[i], "--dist") && i + 1 < argc) {
            dist = atof(argv[++i]);

        } else if (!strcmp(argv[i], "--adsr") && i + 4 < argc) {
            env.a = atof(argv[++i]);
            env.d = atof(argv[++i]);
            env.s = atof(argv[++i]);
            env.r = atof(argv[++i]);

        } else if (!strcmp(argv[i], "--version")) {
            printf("mkogg 1.0 by LICGX\n");
            return 0;
        }
    }

    if (!tone_str) {
        printf("no tone\n");
        return 1;
    }

    float tones[128];
    int n = 0;

    char *t = strtok(tone_str, " ");
    while (t && n < 128) {
        tones[n++] = atof(t);
        t = strtok(NULL, " ");
    }

    if (n < 2) return 1;

    FILE *out = fopen(outfile, "wb");

    vorbis_info vi;
    vorbis_info_init(&vi);
    vorbis_encode_init_vbr(&vi, 2, SAMPLE_RATE, 0.4);

    vorbis_comment vc;
    vorbis_comment_init(&vc);

    vorbis_dsp_state vd;
    vorbis_analysis_init(&vd, &vi);

    vorbis_block vb;
    vorbis_block_init(&vd, &vb);

    ogg_stream_state os;
    ogg_stream_init(&os, rand());

    ogg_packet h1, h2, h3;
    vorbis_analysis_headerout(&vd, &vc, &h1, &h2, &h3);

    ogg_stream_packetin(&os, &h1);
    ogg_stream_packetin(&os, &h2);
    ogg_stream_packetin(&os, &h3);

    ogg_page og;
    while (ogg_stream_flush(&os, &og)) {
        fwrite(og.header, 1, og.header_len, out);
        fwrite(og.body, 1, og.body_len, out);
    }

    float phase = 0;

    for (int i = 0; i < n; i += 2) {

        float f1 = tones[i];
        float dur = tones[i + 1];
        float f2 = (i + 2 < n) ? tones[i + 2] : f1;

        int samples = dur * SAMPLE_RATE;

        float **buf = vorbis_analysis_buffer(&vd, samples);

        for (int j = 0; j < samples; j++) {

            float t = (float)j / samples;
            float freq = glide ? lerp(f1, f2, t) : f1;

            float envv = adsr_env(env, j, samples);

            float s = osc(wave, phase);

            s = tanh(s * (1.0f + dist * 5.0f));

            s *= volume * envv;

            float left = s * (1.0f - pan);
            float right = s * (1.0f + pan);

            buf[0][j] = left;
            buf[1][j] = right;

            phase += TWO_PI * freq / SAMPLE_RATE;
        }

        vorbis_analysis_wrote(&vd, samples);

        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, NULL);
            vorbis_bitrate_addblock(&vb);

            ogg_packet op;
            while (vorbis_bitrate_flushpacket(&vd, &op)) {
                ogg_stream_packetin(&os, &op);

                while (ogg_stream_pageout(&os, &og)) {
                    fwrite(og.header, 1, og.header_len, out);
                    fwrite(og.body, 1, og.body_len, out);
                }
            }
        }
    }

    vorbis_analysis_wrote(&vd, 0);

    fclose(out);
    printf("done: %s\n", outfile);
    return 0;
}
