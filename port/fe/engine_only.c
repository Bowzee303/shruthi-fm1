/* Just the engine, built the way a Felucca host builds it: used to prove it contains no floating point. */
#include <stdint.h>
#include <string.h>
#define NVOICE 8
#define NPART 4
#include "fe_tables.h"
#include "felucca_engine.h"
enum { P_ATK, P_REL, P_FTYPE, P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7, P_COUNT };
typedef struct track { int16_t p[P_COUNT]; voice_t v[NVOICE]; } track_t;
static track_t trk[NPART];
#include "dsp.c"
/*ENGINE_BEGIN*/
#include "eng_shruthi.c"
