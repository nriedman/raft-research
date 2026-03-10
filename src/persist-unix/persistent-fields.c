#include "../persistent-fields.h"

static int pf_get(int f, void *ctx) {
    return 0;
}

static int pf_set(int f, uint32_t v, void *ctx) {
    return 0;
}

persistent_fields_t persistent_fields_init(void) {
    persistent_fields_t pf = {
        .get = pf_get,
        .set = pf_set,
        .context = 0
    };
    return pf;
}

void persistent_fields_free(persistent_field_t *pf) {
    return;
}
