/*
 * cc_list: load an SFZ and print the labelled ARIA <control> CC controls
 * the shim exposes — i.e. exactly what the plugin turns into knobs.
 *
 * Usage: ./cc_list <sfz>
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>

typedef struct XSynthHandle XSynthHandle;
extern XSynthHandle* xshim_create(uint32_t sample_rate, uint32_t channels);
extern int  xshim_load_sfz(XSynthHandle*, const char *path);
extern int  xshim_cc_control_count(const XSynthHandle*);
extern int  xshim_cc_control_get(const XSynthHandle*, int index,
                                 uint8_t *out_cc, uint8_t *out_initial,
                                 char *out_label, size_t label_len);

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <sfz>\n", argv[0]); return 1; }
    XSynthHandle *h = xshim_create(44100, 2);
    if (!h || xshim_load_sfz(h, argv[1]) != 0) { fprintf(stderr, "load failed\n"); return 1; }
    int n = xshim_cc_control_count(h);
    printf("labelled CC controls: %d\n", n);
    for (int i = 0; i < n; i++) {
        uint8_t cc = 0, init = 0; char lab[32] = {0};
        if (xshim_cc_control_get(h, i, &cc, &init, lab, sizeof(lab)) == 0)
            printf("  knob_%-2d  CC%-3d  init=%-3d (%.2f)  %s%s\n",
                   i, cc, init, init / 127.0, lab,
                   i < 8 ? "" : "   [tab 2+]");
    }
    return 0;
}
