// Whole-voice comparison: ORIGINAL Part::ProcessBlock/Voice (unmodified source, AVR hardware stubbed)
// vs the C port, over all factory patches. Compares the 8-bit audio block plus cutoff/resonance/VCA CVs.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
extern "C" {
#include "shruthi_core.h"
void sh_random_seed(uint16_t); void ref_seed(uint16_t);
void refv_init(const uint8_t*); void refv_note_on(uint8_t, uint8_t); void refv_note_off(void);
void refv_block(uint8_t*, uint8_t*, uint8_t*, uint8_t*);
}
static int hv(char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }

int main(int argc, char** argv) {
  FILE* f = fopen(argv[1], "r"); if (!f) { puts("no factory file"); return 2; }
  static uint8_t raws[128][92]; static char names[128][16]; int n = 0; char line[1024];
  while (fgets(line, sizeof line, f) && n < 128) {
    if (strncmp(line, "patch\t", 6)) continue;
    char* name = line + 6; char* tab = strchr(name, '\t'); if (!tab) continue; *tab = 0;
    strncpy(names[n], name, 15); char* hex = tab + 1;
    for (int i = 0; i < 92; ++i) raws[n][i] = hv(hex[2 * i]) * 16 + hv(hex[2 * i + 1]);
    ++n;
  }
  fclose(f);
  const int notes[] = {36, 57, 69, 84}; const int BLOCKS = 400, OFF_AT = 250;
  /* The original keeps its state in globals; every (patch, note) run happens in a fresh process so the
   * reference always starts from true power-on state. The child exits 0 when bit-exact. */
  int patches_exact = 0, patches_diverged = 0, runs_exact = 0, runs_total = 0, shown = 0; long total_blocks = 0;
  for (int k = 0; k < n; ++k) {
    sh_patch p0; if (!sh_patch_load(&p0, raws[k])) continue;
    int patch_ok = 1;
    for (unsigned ni = 0; ni < 4; ++ni) {
      fflush(stdout);
      pid_t pid = fork();
      if (pid == 0) {
        sh_patch p = p0; sh_voice v; sh_voice_init(&v, &p);
        refv_init(raws[k]);
        sh_random_seed(0x2468); ref_seed(0x2468);
        /* like the real firmware, blocks run continuously before a key is pressed */
        for (int i = 0; i < 8; ++i) { uint8_t t1[SH_BLOCK], t2[SH_BLOCK], a, b, c; refv_block(t1, &a, &b, &c); sh_voice_process_block(&v, t2); }
        refv_note_on(notes[ni], 100); sh_voice_note_on(&v, notes[ni], 100, 0, 1);
        for (int b = 0; b < BLOCKS; ++b) {
          if (b == OFF_AT) { refv_note_off(); sh_voice_note_off(&v); }
          uint8_t ro[SH_BLOCK], mo[SH_BLOCK], rc, rr, rv;
          refv_block(ro, &rc, &rr, &rv); sh_voice_process_block(&v, mo);
          int audio_diff = memcmp(ro, mo, SH_BLOCK) != 0;
          int cv_diff = rc != v.out_cutoff || rr != v.out_resonance || rv != v.out_vca;
          if (rv == 0 && v.out_vca == 0) audio_diff = 0;   /* VCA closed: audio is irrelevant */
          if (audio_diff || cv_diff) {
            printf("DIVERGE %-9s note %d block %d: %s", names[k], notes[ni], b, cv_diff ? "CV" : "audio");
            if (cv_diff) printf("  ref cut/res/vca=%d/%d/%d mine=%d/%d/%d", rc, rr, rv, v.out_cutoff, v.out_resonance, v.out_vca);
            else for (int i = 0; i < SH_BLOCK; ++i) if (ro[i] != mo[i]) { printf("  sample %d ref=%d mine=%d", i, ro[i], mo[i]); break; }
            printf("\n"); fflush(stdout); _exit(1);
          }
        }
        _exit(0);
      }
      int st = 0; waitpid(pid, &st, 0);
      ++runs_total; total_blocks += BLOCKS;
      if (WIFEXITED(st) && WEXITSTATUS(st) == 0) ++runs_exact; else patch_ok = 0;
    }
    if (patch_ok) ++patches_exact; else ++patches_diverged;
  }
  (void)shown;
  printf("\n%d patches x 4 notes x %d blocks = %d runs (%ld blocks)\n", n, BLOCKS, runs_total, total_blocks);
  printf("bit-exact runs: %d / %d\n", runs_exact, runs_total);
  printf("fully bit-exact patches: %d / %d   diverging patches: %d\n", patches_exact, n, patches_diverged);
  return patches_diverged != 0;
}
