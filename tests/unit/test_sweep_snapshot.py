"""Exercise production snapshot synchronization with deterministic interleavings."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
text = (ROOT / 'src/rf/sweep.c').read_text()


def function(signature, source=text):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t systime_t;
#define MS2ST(x) (x)
static uint32_t now, sleeps;
static bool locked;
static void (*on_sleep)(void);
static systime_t chVTGetSystemTimeX(void) { return now; }
static void osalSysLock(void) { assert(!locked); locked = true; }
static void osalSysUnlock(void) { assert(locked); locked = false; }
static void chThdSleepMilliseconds(unsigned ms) {
  assert(!locked); now += ms; ++sleeps;
  if (on_sleep) on_sleep();
}
static bool sweep_in_progress, sweep_copy_in_progress, sweep_cancel_request;
static bool sweep_snapshot_valid, sweep_snapshot_pending;
static uint16_t sweep_snapshot_points, sweep_points = 2;
static uint32_t sweep_generation;
static float measured[2][2][2];
typedef struct {
  const float (*data)[2]; uint16_t points; uint32_t generation;
} sweep_service_snapshot_t;
''' + '\n'.join(function(signature) for signature in (
    'void sweep_service_wait_for_copy_release(',
    'void sweep_service_begin_measurement(',
    'void sweep_service_end_measurement(',
    'uint32_t sweep_service_increment_generation(',
    'bool sweep_service_snapshot_acquire(',
    'bool sweep_service_snapshot_release(',
)) + r'''
typedef int FIL;
typedef int FILINFO;
#define FILE_LOAD_CALLBACK(name) const char* name(FIL* f, FILINFO* fno, uint8_t format)
static const char* import_error;
static FILE_LOAD_CALLBACK(load_snp_data) {
  (void)f; (void)fno; (void)format;
  assert(sweep_in_progress && !sweep_snapshot_valid && !sweep_copy_in_progress);
  return import_error;
}
''' + function('static FILE_LOAD_CALLBACK(load_snp)',
               (ROOT / 'src/ui/menus/menu_storage.c').read_text()) + r'''
static void release_copy(void) {
  sweep_copy_in_progress = false;
  on_sleep = NULL;
}
static void complete_sweep(void) {
  measured[0][1][0] = 200;
  sweep_service_increment_generation();
  on_sleep = NULL;
}
static sweep_service_snapshot_t held;
static void acquire_pending(void) {
  assert(sweep_service_snapshot_acquire(0, &held));
  on_sleep = release_copy;
}
int main(void) {
  sweep_service_snapshot_t snap;
  assert(!sweep_service_snapshot_acquire(2, &snap));
  assert(!sweep_service_snapshot_acquire(0, NULL));
  // Between slices: generation 7 is old, only the first point has been replaced.
  sweep_generation = 7;
  measured[0][0][0] = 200; measured[0][1][0] = 100;
  now = UINT32_MAX - 10;
  assert(!sweep_service_snapshot_acquire(0, &snap));
  assert(!sweep_snapshot_pending && !sweep_copy_in_progress);
  // A waiting reader sees both new points after a completed publication.
  on_sleep = complete_sweep;
  assert(sweep_service_snapshot_acquire(0, &snap));
  assert(snap.generation == 8 && snap.points == 2);
  assert(snap.data[0][0] == 200 && snap.data[1][0] == 200);
  assert(sweep_service_snapshot_release(&snap));
  // Publish-time point count is stable even if a later setting changes it.
  sweep_points = 1;
  assert(sweep_service_snapshot_acquire(0, &snap));
  assert(snap.points == 2);
  // Sleeping, unlike yielding, allows a lower-priority shell to release.
  unsigned before = sleeps;
  on_sleep = release_copy;
  sweep_service_wait_for_copy_release();
  assert(sleeps > before);
  // A waiting reader gets the completed sweep before the next write begins.
  sweep_snapshot_pending = true;
  on_sleep = acquire_pending;
  before = sleeps;
  sweep_service_begin_measurement();
  assert(sleeps == before + 2);
  assert(sweep_in_progress && !sweep_snapshot_valid);
  sweep_service_end_measurement();
  assert(!sweep_service_snapshot_acquire(0, &snap));
  assert(load_snp(NULL, NULL, 0) == NULL);
  assert(sweep_service_snapshot_acquire(0, &snap));
  assert(snap.generation == 9);
  assert(sweep_service_snapshot_release(&snap));
  import_error = "Format err";
  assert(load_snp(NULL, NULL, 0) == import_error);
  assert(!sweep_service_snapshot_acquire(0, &snap));
  puts("[PASS] completed snapshots, rollover timeout, reader priority and writer exclusion");
}
'''
with tempfile.TemporaryDirectory(prefix='nanovna-snapshot-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(source)
    subprocess.run(shlex.split(os.environ.get('HOST_CC', 'gcc')) + [
        '-std=c11', '-Wall', '-Wextra', '-Werror',
        str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
