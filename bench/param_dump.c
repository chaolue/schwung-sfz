/*
 * param_dump: dlopen a built dsp.so, create an instance, load a preset,
 * and print get_param("chain_params"). Verifies what the module actually
 * publishes to the host without needing the Move's UI.
 *
 * Usage: ./param_dump <dsp.so> <module_dir> <preset_index>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include <unistd.h>

/* Must match xsynth_plugin.c's host_api_v1_t exactly. */
typedef struct host_api_v1 {
    uint32_t api_version;
    int sample_rate;
    int frames_per_block;
    uint8_t *mapped_memory;
    int audio_out_offset;
    int audio_in_offset;
    void (*log)(const char *msg);
    int (*midi_send_internal)(const uint8_t *msg, int len);
    int (*midi_send_external)(const uint8_t *msg, int len);
} host_api_v1_t;

typedef struct plugin_api_v2 {
    uint32_t api_version;
    void* (*create_instance)(const char *module_dir, const char *json_defaults);
    void (*destroy_instance)(void *instance);
    void (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    int (*get_error)(void *instance, char *buf, int buf_len);
    void (*render_block)(void *instance, int16_t *out_interleaved_lr, int frames);
} plugin_api_v2_t;

static void host_log(const char *msg) { fprintf(stderr, "[host] %s\n", msg); }

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s <dsp.so> <module_dir> <preset_index>\n", argv[0]); return 2; }
    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    plugin_api_v2_t* (*init)(const host_api_v1_t*) = dlsym(h, "move_plugin_init_v2");
    if (!init) { fprintf(stderr, "dlsym: %s\n", dlerror()); return 1; }

    static uint8_t shared[1 << 20];
    host_api_v1_t host = {0};
    host.api_version = 1;
    host.sample_rate = 44100;
    host.frames_per_block = 128;
    host.mapped_memory = shared;
    host.audio_out_offset = 0;
    host.audio_in_offset = 4096;
    host.log = host_log;
    plugin_api_v2_t *api = init(&host);
    if (!api) { fprintf(stderr, "init returned NULL\n"); return 1; }

    void *inst = api->create_instance(argv[2], "{}");
    if (!inst) { fprintf(stderr, "create_instance failed\n"); return 1; }

    char *list = malloc(600000);
    int ln = api->get_param(inst, "preset_list", list, 600000);
    if (!strcmp(argv[3], "-list")) {
        printf("preset_list (%d bytes):\n%s\n", ln, ln > 0 ? list : "(empty)");
        api->destroy_instance(inst);
        return 0;
    }

    /* Resolve a preset index: a bare number, or the index of the first
     * entry whose JSON blob contains the given substring. */
    int index = -1;
    if (argv[3][0] >= '0' && argv[3][0] <= '9') {
        index = atoi(argv[3]);
    } else if (ln > 0) {
        int depth = 0, idx = 0, start = 0;
        for (int i = 0; i < ln; i++) {
            if (list[i] == '{') { if (depth++ == 0) start = i; }
            else if (list[i] == '}') {
                if (--depth == 0) {
                    char save = list[i + 1]; list[i + 1] = '\0';
                    if (strstr(list + start, argv[3])) { index = idx; }
                    list[i + 1] = save;
                    if (index >= 0) break;
                    idx++;
                }
            }
        }
    }
    if (index < 0) { fprintf(stderr, "preset not found: %s\n", argv[3]); return 1; }
    fprintf(stderr, "loading preset index %d\n", index);

    char idxbuf[16];
    snprintf(idxbuf, sizeof(idxbuf), "%d", index);
    api->set_param(inst, "preset", idxbuf);

    /* Drive render_block in real time — the load runs on a worker thread,
     * so this needs wall-clock, not just iterations. ~120 s ceiling. */
    int16_t out[128 * 2];
    int published = 0;
    char *probe = malloc(600000);
    for (int i = 0; i < 41000 && !published; i++) {
        api->render_block(inst, out, 128);
        usleep(2900);
        if ((i % 340) == 339) {
            int pn = api->get_param(inst, "chain_params", probe, 600000);
            if (pn > 0 && strstr(probe, "\"knob_0\"")) published = 1;
        }
    }
    free(probe); free(list);

    char *buf = malloc(600000);
    int n = api->get_param(inst, "chain_params", buf, 600000);
    printf("chain_params (%d bytes):\n%s\n", n, n > 0 ? buf : "(empty)");
    free(buf);
    api->destroy_instance(inst);
    return 0;
}
