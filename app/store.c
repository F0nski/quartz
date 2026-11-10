/* The project in flash: the q_project as is, one store object. Part of the unity build (app.c). */
#include "audio.h"
_Static_assert(sizeof(q_project) <= PLAT_STORE_MAX, "q_project must fit one store object");

int store_load(q_project *p)
{
    if (plat_store_size(OBJ_PROJECT) == (int)sizeof *p) {
        q_project t;
        if (plat_store_load(OBJ_PROJECT, &t, sizeof t) == (int)sizeof t && t.version == 1) {
            *p = t;
            return 0;
        }
    }
    project_defaults(p);
    return -1;
}

/* a flash write stalls the render: not while playing */
int store_save(const q_project *p)
{
    if (audio_playing()) return -1;
    return plat_store_save(OBJ_PROJECT, p, sizeof *p);
}
