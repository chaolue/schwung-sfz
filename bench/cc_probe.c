/*
 * cc_probe: load an SFZ, sweep one MIDI CC across a set of values, and
 * report the rendered peak level for each. Used to verify live `_oncc`
 * modulation (volume_oncc / amplitude_oncc) actually reaches the audio.
 *
 * Usage: ./cc_probe <sfz> <note> <cc> <val>[,<val>...] [<cc>:<val>,...]
 *
 * The optional 5th argument pre-sets CCs before every probe — needed for
 * ARIA libraries whose `<control>` block declares `set_hdcc<N>` defaults
 * that xsynth does not (yet) push into the runtime CC state.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define FRAMES_PER_BLOCK 128
#define SAMPLE_RATE 44100

typedef struct XSynthHandle XSynthHandle;
extern XSynthHandle* xshim_create(uint32_t sample_rate, uint32_t channels);
extern void xshim_destroy(XSynthHandle*);
extern int  xshim_load_sfz(XSynthHandle*, const char *path);
extern void xshim_note_on(XSynthHandle*, uint8_t ch, uint8_t key, uint8_t vel);
extern void xshim_note_off(XSynthHandle*, uint8_t ch, uint8_t key);
extern void xshim_cc(XSynthHandle*, uint8_t ch, uint8_t cc, uint8_t val);
extern void xshim_all_notes_off(XSynthHandle*);
extern void xshim_render(XSynthHandle*, float *out, size_t num_samples);

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <sfz> <note> <cc> <val>[,<val>...] [<cc>:<val>,...]\n", argv[0]);
        return 1;
    }
    const char *sfz = argv[1];
    int note = atoi(argv[2]);
    int cc   = atoi(argv[3]);

    XSynthHandle *h = xshim_create(SAMPLE_RATE, 2);
    if (!h) { fprintf(stderr, "create failed\n"); return 1; }
    if (xshim_load_sfz(h, sfz) != 0) { fprintf(stderr, "load failed\n"); return 1; }
    fprintf(stderr, "loaded %s\n", sfz);

    /* 1 second of stereo render per probe. */
    const int blocks = SAMPLE_RATE / FRAMES_PER_BLOCK;
    float *buf = malloc(sizeof(float) * FRAMES_PER_BLOCK * 2);

    /* strtok_r throughout: the pre-set loop below is nested inside this
     * one, and plain strtok shares one static cursor — the inner call
     * would silently terminate the outer sweep after one iteration. */
    char *list = strdup(argv[4]);
    char *outer_save = NULL;
    for (char *tok = strtok_r(list, ",", &outer_save); tok;
         tok = strtok_r(NULL, ",", &outer_save)) {
        int val = atoi(tok);

        xshim_all_notes_off(h);
        /* Let any previous tail die out before measuring. */
        for (int b = 0; b < blocks; b++)
            xshim_render(h, buf, FRAMES_PER_BLOCK * 2);

        /* Pre-set CCs (e.g. the library's set_hdcc defaults). */
        if (argc >= 6) {
            char *pre = strdup(argv[5]);
            char *inner_save = NULL;
            for (char *p2 = strtok_r(pre, ",", &inner_save); p2;
                 p2 = strtok_r(NULL, ",", &inner_save)) {
                int pcc, pval;
                if (sscanf(p2, "%d:%d", &pcc, &pval) == 2)
                    xshim_cc(h, 0, (uint8_t)pcc, (uint8_t)pval);
            }
            free(pre);
        }
        xshim_cc(h, 0, (uint8_t)cc, (uint8_t)val);
        xshim_note_on(h, 0, (uint8_t)note, 100);

        double peak = 0.0, sumsq = 0.0;
        double peakL = 0.0, peakR = 0.0;
        long n = 0;
        for (int b = 0; b < blocks; b++) {
            xshim_render(h, buf, FRAMES_PER_BLOCK * 2);
            for (int i = 0; i < FRAMES_PER_BLOCK * 2; i++) {
                double v = fabs((double)buf[i]);
                if (v > peak) peak = v;
                /* Interleaved L,R,L,R... */
                if ((i & 1) == 0) { if (v > peakL) peakL = v; }
                else              { if (v > peakR) peakR = v; }
                sumsq += v * v;
                n++;
            }
        }
        xshim_note_off(h, 0, (uint8_t)note);
        double rms = sqrt(sumsq / (double)n);
        /* Balance: -1 = hard left, 0 = centred, +1 = hard right. */
        double bal = (peakL + peakR) > 0.0
                   ? (peakR - peakL) / (peakL + peakR) : 0.0;
        printf("CC%-3d = %-3d   peak=%.5f   rms=%.5f   L=%.5f R=%.5f  balance=%+.3f %s\n",
               cc, val, peak, rms, peakL, peakR, bal,
               bal < -0.9 ? "HARD LEFT" : bal > 0.9 ? "HARD RIGHT"
               : (bal > -0.1 && bal < 0.1) ? "centred" : "");
        fflush(stdout);
    }
    free(list); free(buf);
    xshim_destroy(h);
    return 0;
}
