/* Owned shader-key harvesting, serviced only by the main shell-log worker. */
#include "wiiu_missing_keys.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ACCEPTING 1u
#define ACTIVE 2u
#define QUIESCED 4u
#define GAMEPLAY 8u
/* Files from earlier runs are only counted (the folder never emptied itself: 128 of them stopped the capture at its first write on
   2026-11-06, errno 28, and 41 keys were lost). RUN_CAP bounds what one run writes; OLD_CAP is the folder's size at which the capture
   stays off, so that the scan stays short (SCAN_CAP) and the card does not fill with batches no one collects. */
#define RUN_CAP 64u
#define OLD_CAP 1000u
#define SCAN_CAP 4096u
#define WRITE_BYTES 4096u
#define PREFIX "shader-misses-"
enum { IDLE, SCAN_OPEN, SCAN_READ, SCAN_CLOSE, FORM, CHECK, OPEN, WRITE,
       SYNC, CLOSE, CHECK_FINAL, RENAME, COMMIT, FAIL_CLOSE, FAILED };

struct missing_record
{
    uint32_t stage, hash_high, hash_low, program_high, program_low, packed_mask, count, reserved;
    union { unsigned char wire[WIIU_MISSING_WIRE]; uint32_t words[WIIU_MISSING_WORDS]; } data;
    unsigned char padding[32];
};
typedef char missing_record_size[sizeof(struct missing_record) == 2240 ? 1 : -1];

static struct
{
    wiiu_missing_keys_config config;
    char directory[160], part[256], final[256];
    struct missing_record records[WIIU_MISSING_SLOTS];
    char text[WIIU_MISSING_TEXT];
    uint32_t initialized, gate, io_gate, observing, producer, consumer, write_sequence, read_sequence;
    uint32_t accepted, written, full, unavailable, lost_io, io_errors, close_errors;
    uint32_t rejected_producer, rejected_consumer, phase, batch_records, files, failed;
    uint32_t scanned, owned_files, old_files, sequence, always;
    const char *why;
    size_t text_size, text_written;
    DIR *scan;
    int descriptor, last_error;
} capture;

#ifndef WIIU_MISSING_PUBLISH_HOOK
#define WIIU_MISSING_PUBLISH_HOOK() ((void)0)
#endif
#ifndef WIIU_MISSING_OBSERVE_HOOK
#define WIIU_MISSING_OBSERVE_HOOK() ((void)0)
#endif
#ifndef WIIU_MISSING_CLAIM_HOOK
#define WIIU_MISSING_CLAIM_HOOK() ((void)0)
#endif

static uint32_t load(const uint32_t *word) { return __atomic_load_n(word, __ATOMIC_ACQUIRE); }
static void store(uint32_t *word, uint32_t value) { __atomic_store_n(word, value, __ATOMIC_RELEASE); }
static void add(uint32_t *word, uint32_t value) { __atomic_add_fetch(word, value, __ATOMIC_RELAXED); }

static int owner(uint32_t *slot)
{
    uint32_t who = capture.config.thread_id ? capture.config.thread_id(capture.config.context) : 1u;
    uint32_t expected = load(slot);

    if (!who)
        return 0;
    if (expected)
        return expected == who;
    if (__atomic_compare_exchange_n(slot, &expected, who, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return 1;
    return expected == who;
}

int wiiu_missing_keys_start(const wiiu_missing_keys_config *config)
{
    if (load(&capture.initialized))
        return EBUSY;
    if (!config || !config->directory || !*config->directory || strlen(config->directory) >= sizeof(capture.directory))
        return EINVAL;
    capture.config = *config;
    snprintf(capture.directory, sizeof(capture.directory), "%s", config->directory);
    capture.descriptor = -1;
    store(&capture.gate, ACCEPTING);
    store(&capture.io_gate, ACCEPTING | GAMEPLAY); /* blocked until authoritative game initialization */
    store(&capture.initialized, 1);
    return 0;
}

int wiiu_missing_keys_producer(void)
{
    if (!load(&capture.initialized) || !(load(&capture.gate) & ACCEPTING))
        return 0;
    if (!owner(&capture.producer))
    {
        add(&capture.rejected_producer, 1);
        return 0;
    }
    return 1;
}

int wiiu_missing_keys_observe(void)
{
    uint32_t expected = ACCEPTING;
    if (!wiiu_missing_keys_producer()) return 0;
    if (!__atomic_compare_exchange_n(&capture.gate, &expected, ACCEPTING | ACTIVE, 0,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return 0;
    store(&capture.observing, 1);
    WIIU_MISSING_OBSERVE_HOOK();
    return 1;
}

void wiiu_missing_keys_observed(void)
{
    store(&capture.observing, 0);
    __atomic_fetch_and(&capture.gate, ~ACTIVE, __ATOMIC_RELEASE);
}

static struct missing_record *begin_record(uint32_t *sequence)
{
    uint32_t expected = ACCEPTING;

    if (load(&capture.observing))
    {
        if (!owner(&capture.producer)) { add(&capture.rejected_producer, 1); return NULL; }
        /* An observation admitted before stop owns ACTIVE until its full seen/copy scope ends. */
    }
    else
    {
        if (!wiiu_missing_keys_producer())
        {
            if (load(&capture.initialized) && !(load(&capture.gate) & ACCEPTING)) add(&capture.unavailable, 1);
            return NULL;
        }
        if (!__atomic_compare_exchange_n(&capture.gate, &expected, ACCEPTING | ACTIVE, 0,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        {
            add(&capture.unavailable, 1);
            return NULL;
        }
    }
    *sequence = load(&capture.write_sequence);
    if (*sequence - load(&capture.read_sequence) >= WIIU_MISSING_SLOTS)
    {
        add(&capture.full, 1);
        if (!load(&capture.observing)) __atomic_fetch_and(&capture.gate, ~ACTIVE, __ATOMIC_RELEASE);
        return NULL;
    }
    return &capture.records[*sequence % WIIU_MISSING_SLOTS];
}

static int publish(uint32_t sequence)
{
    WIIU_MISSING_PUBLISH_HOOK();
    add(&capture.accepted, 1);
    store(&capture.write_sequence, sequence + 1u);
    if (!load(&capture.observing)) __atomic_fetch_and(&capture.gate, ~ACTIVE, __ATOMIC_RELEASE);
    return 1;
}

int wiiu_missing_keys_pixel(uint32_t high, uint32_t low, const unsigned char *wire, uint32_t bytes)
{
    uint32_t sequence;
    struct missing_record *record;

    if (!wire || bytes != WIIU_MISSING_WIRE)
    {
        wiiu_missing_keys_unavailable();
        return 0;
    }
    record = begin_record(&sequence);
    if (!record)
        return 0;
    record->stage = 1; record->hash_high = high; record->hash_low = low; record->count = bytes;
    memcpy(record->data.wire, wire, bytes);
    return publish(sequence);
}

int wiiu_missing_keys_vertex(uint32_t high, uint32_t low, uint32_t program_high,
    uint32_t program_low, uint32_t packed_mask, uint32_t instructions, const uint32_t *words)
{
    uint32_t sequence;
    struct missing_record *record;

    if (!words || !instructions || instructions > WIIU_MISSING_WORDS / 4u)
    {
        wiiu_missing_keys_unavailable();
        return 0;
    }
    record = begin_record(&sequence);
    if (!record)
        return 0;
    record->stage = 0; record->hash_high = high; record->hash_low = low;
    record->program_high = program_high; record->program_low = program_low;
    record->packed_mask = packed_mask; record->count = instructions;
    memcpy(record->data.words, words, instructions * 4u * sizeof(uint32_t));
    return publish(sequence);
}

void wiiu_missing_keys_unavailable(void) { add(&capture.unavailable, 1); }
void wiiu_missing_keys_stop(void) { __atomic_fetch_and(&capture.gate, ~ACCEPTING, __ATOMIC_ACQ_REL); }
void wiiu_missing_keys_suspend(void) { __atomic_fetch_and(&capture.io_gate, ~ACCEPTING, __ATOMIC_ACQ_REL); }
void wiiu_missing_keys_resume(void)
{
    uint32_t previous = __atomic_fetch_or(&capture.io_gate, ACCEPTING, __ATOMIC_ACQ_REL);
    /* QUIESCED remains in the same atomic word, so a racing resume cannot admit a final-close turn. */
    if (previous & QUIESCED) wiiu_missing_keys_suspend();
}
void wiiu_missing_keys_quiesce(void)
{
    __atomic_fetch_or(&capture.io_gate, QUIESCED, __ATOMIC_ACQ_REL);
    wiiu_missing_keys_suspend();
}

void wiiu_missing_keys_gameplay(int active)
{
    /* The same claim word closes the check/next-filesystem-step race. An already claimed syscall retains ownership. */
    if (active && !load(&capture.always)) __atomic_fetch_or(&capture.io_gate, GAMEPLAY, __ATOMIC_ACQ_REL);
    else __atomic_fetch_and(&capture.io_gate, ~GAMEPLAY, __ATOMIC_ACQ_REL);
}

void wiiu_missing_keys_always(int on)
{
    store(&capture.always, on ? 1u : 0u);
    if (on) __atomic_fetch_and(&capture.io_gate, ~GAMEPLAY, __ATOMIC_ACQ_REL);
}

void wiiu_missing_keys_counts_get(wiiu_missing_keys_counts *out)
{
    uint32_t phase, gate, io_gate;

    memset(out, 0, sizeof(*out));
    if (!load(&capture.initialized))
        return;
    io_gate = load(&capture.io_gate);
    gate = load(&capture.gate); /* acquire any in-flight publication before queue sampling */
#define COPY(field) out->field = load(&capture.field)
    COPY(accepted); COPY(written); COPY(full); COPY(unavailable); COPY(lost_io); COPY(io_errors); COPY(close_errors);
    COPY(rejected_producer); COPY(rejected_consumer); COPY(batch_records); COPY(files); COPY(old_files); COPY(failed);
#undef COPY
    phase = load(&capture.phase);
    out->queue_pending = load(&capture.write_sequence) - load(&capture.read_sequence);
    out->gate = load(&capture.gate);
    out->io_gate = load(&capture.io_gate);
    out->gameplay = (out->io_gate & GAMEPLAY) != 0;
    out->phase = load(&capture.phase);
    out->last_error = __atomic_load_n(&capture.last_error, __ATOMIC_ACQUIRE);
    out->drained = !(io_gate & ACTIVE) && !(out->io_gate & ACTIVE) && gate == 0 && out->gate == 0 && !out->queue_pending && phase == IDLE && out->phase == IDLE && !out->failed;
    out->complete = out->drained && out->accepted == out->written && !out->full && !out->unavailable &&
        !out->lost_io && !out->io_errors && !out->rejected_producer && !out->rejected_consumer;
}

void wiiu_missing_keys_report(const char *reason)
{
    char text[320];
    wiiu_missing_keys_counts counts;

    if (!load(&capture.initialized) || !capture.config.report)
        return;
    wiiu_missing_keys_counts_get(&counts);
    snprintf(text, sizeof(text), "Shader capture %s: accepted %u written %u pending %u+%u files %u (+%u earlier) state %u gates %u/%u play %u drained %u complete %u",
        reason, counts.accepted, counts.written, counts.queue_pending, counts.batch_records, counts.files, counts.old_files,
        counts.phase, counts.gate, counts.io_gate, counts.gameplay, counts.drained, counts.complete);
    capture.config.report(capture.config.context, text);
    if (counts.full || counts.unavailable || counts.lost_io || counts.io_errors || counts.close_errors ||
        counts.rejected_producer || counts.rejected_consumer)
    {
        snprintf(text, sizeof(text), "Shader capture loss: full %u unavailable %u lostIO %u IO %u close %u owners %u/%u errno %d",
            counts.full, counts.unavailable, counts.lost_io, counts.io_errors, counts.close_errors,
            counts.rejected_producer, counts.rejected_consumer, counts.last_error);
        capture.config.report(capture.config.context, text);
    }
}

static const char *const phase_names[] = { "idle", "opendir", "readdir", "closedir", "form", "stat", "open", "write",
    "fsync", "close", "stat", "rename", "commit", "close", "failed" };

static void say(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *format, ...)
{
    char text[200];
    va_list arguments;

    if (!capture.config.report)
        return;
    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    capture.config.report(capture.config.context, text);
}

static void fail(int error, int close_unknown)
{
    uint32_t phase = load(&capture.phase);

    /* (said while the game runs, not behind the close: after QUIESCED the log is already ending) */
    if (!(load(&capture.io_gate) & QUIESCED))
    {
        if (capture.why)
            say("Shader capture gives up: %s (errno %d), %u records not written", capture.why, error ? error : EIO,
                (unsigned)(load(&capture.write_sequence) - load(&capture.read_sequence) + load(&capture.batch_records)));
        else
            say("Shader capture gives up: %s failed (errno %d), %u records not written", phase < sizeof(phase_names) / sizeof(*phase_names) ? phase_names[phase] : "?",
                error ? error : EIO, (unsigned)(load(&capture.write_sequence) - load(&capture.read_sequence) + load(&capture.batch_records)));
    }
    __atomic_store_n(&capture.last_error, error ? error : EIO, __ATOMIC_RELEASE);
    add(&capture.io_errors, 1);
    store(&capture.failed, 1);
    wiiu_missing_keys_stop();
    if (close_unknown)
    {
        add(&capture.close_errors, 1);
        capture.descriptor = -1; /* never retry an uncertain numeric descriptor */
    }
    store(&capture.phase, capture.descriptor >= 0 ? FAIL_CLOSE : FAILED);
}

static size_t record_text(const struct missing_record *record, char *out, size_t room)
{
    size_t used = 0;
    unsigned i;
    int size;

    if (record->stage == 1 && record->count == WIIU_MISSING_WIRE)
    {
        if (room < 516u) return 0;
        out[used++] = 'P'; out[used++] = ' ';
        for (i = 0; i < WIIU_MISSING_WIRE; i++)
        {
            static const char hex[] = "0123456789abcdef";
            out[used++] = hex[record->data.wire[i] >> 4]; out[used++] = hex[record->data.wire[i] & 15];
        }
        out[used++] = '\n';
        return used;
    }
    if (record->stage || !record->count || record->count > WIIU_MISSING_WORDS / 4u || room < 4931u)
        return 0;
    size = snprintf(out, room, "V %08x%08x %u %u", record->program_high, record->program_low, record->packed_mask, record->count);
    if (size < 0 || (size_t)size >= room) return 0;
    used = (size_t)size;
    for (i = 0; i < record->count * 4u; i++)
    {
        size = snprintf(out + used, room - used, " %08x", record->data.words[i]);
        if (size != 9 || used + (size_t)size >= room) return 0;
        used += (size_t)size;
    }
    out[used++] = '\n';
    return used;
}

static int service_turn(void)
{
    uint32_t phase;
    int error;

    phase = load(&capture.phase);
    if (phase == FAILED)
    {
        uint32_t read = load(&capture.read_sequence), available = load(&capture.write_sequence) - read;
        uint32_t drop = available > WIIU_MISSING_BATCH ? WIIU_MISSING_BATCH : available;
        if (capture.text_size || load(&capture.batch_records))
        {
            add(&capture.lost_io, load(&capture.batch_records)); store(&capture.batch_records, 0); capture.text_size = 0;
        }
        if (drop) { add(&capture.lost_io, drop); store(&capture.read_sequence, read + drop); return 1; }
        return 0;
    }
    if (capture.config.io_allowed && !capture.config.io_allowed(capture.config.context)) return 0;
    switch (phase)
    {
    case IDLE:
        if (load(&capture.read_sequence) == load(&capture.write_sequence))
        {
            /* The folder is counted once, in the menu, before anything is queued, so that the run's log says at its start whether the capture
               can write and a full folder is known before gameplay (which admits no file work) has queued a single key. */
            if (capture.sequence || !(load(&capture.gate) & ACCEPTING)) return 0;
            store(&capture.phase, SCAN_OPEN);
            return 1;
        }
        store(&capture.phase, capture.sequence ? FORM : SCAN_OPEN);
        return 1;
    case SCAN_OPEN:
        capture.scan = opendir(capture.directory);
        if (!capture.scan) fail(errno, 0); else store(&capture.phase, SCAN_READ);
        return 1;
    case SCAN_READ:
    {
        struct dirent *entry;
        errno = 0; entry = readdir(capture.scan); error = errno;
        if (!entry) { if (error) __atomic_store_n(&capture.last_error,error,__ATOMIC_RELEASE); store(&capture.phase, SCAN_CLOSE); return 1; }
        if (++capture.scanned > SCAN_CAP) { __atomic_store_n(&capture.last_error,EFBIG,__ATOMIC_RELEASE); store(&capture.phase,SCAN_CLOSE); return 1; }
        if (!strncmp(entry->d_name,PREFIX,sizeof(PREFIX)-1)) capture.owned_files++;
        return 1;
    }
    case SCAN_CLOSE:
        error = closedir(capture.scan); capture.scan = NULL;
        if (error || __atomic_load_n(&capture.last_error,__ATOMIC_ACQUIRE))
        {
            capture.why = "the folder could not be listed";
            fail(error ? errno : __atomic_load_n(&capture.last_error,__ATOMIC_ACQUIRE), 0);
        }
        else if (capture.owned_files >= OLD_CAP)
        {
            capture.why = "the folder has too many shader-misses files: collect and delete the old ones";
            say("Shader capture: %u files in halo/ (limit %u)", (unsigned)capture.owned_files, OLD_CAP);
            fail(ENOSPC, 0);
        }
        else
        {
            capture.old_files = capture.owned_files;
            say("Shader capture: %u files in halo/, writing to " PREFIX "%08x%08x-NNNN.txt (at most %u files this run, %u in all)",
                (unsigned)capture.old_files, (unsigned)capture.config.run_high, (unsigned)capture.config.run_low, RUN_CAP, OLD_CAP);
            capture.sequence = 1;
            store(&capture.phase, IDLE);
        }
        return 1;
    case FORM:
    {
        uint32_t read = load(&capture.read_sequence), write = load(&capture.write_sequence), taken = 0;
        capture.text_size = capture.text_written = 0;
        WIIU_MISSING_CLAIM_HOOK();
        while (read != write && taken < WIIU_MISSING_BATCH)
        {
            size_t size = record_text(&capture.records[read % WIIU_MISSING_SLOTS],capture.text+capture.text_size,sizeof(capture.text)-capture.text_size);
            if (!size) break;
            capture.text_size += size; read++; taken++;
        }
        if (!taken) { if (read != write) fail(EINVAL,0); else store(&capture.phase,IDLE); return 1; }
        store(&capture.batch_records,taken);
        store(&capture.read_sequence,read); /* phase FORM was published before slots become reusable */
        if (capture.sequence > RUN_CAP) { capture.why = "this run has written its limit of files"; fail(ENOSPC,0); return 1; }
        if (capture.owned_files >= OLD_CAP) { capture.why = "the folder is full"; fail(ENOSPC,0); return 1; }
        snprintf(capture.final,sizeof(capture.final),"%s/" PREFIX "%08x%08x-%04u.txt",capture.directory,capture.config.run_high,capture.config.run_low,capture.sequence);
        snprintf(capture.part,sizeof(capture.part),"%s.part",capture.final);
        store(&capture.phase,CHECK); return 1;
    }
    case CHECK:
    case CHECK_FINAL:
    {
        struct stat info;
        if (stat(capture.final,&info)==0) { fail(EEXIST,0); return 1; }
        if (errno != ENOENT) { fail(errno,0); return 1; }
        store(&capture.phase,phase==CHECK ? OPEN : RENAME); return 1;
    }
    case OPEN:
        capture.descriptor = open(capture.part,O_WRONLY|O_CREAT|O_EXCL,0644);
        if (capture.descriptor < 0) fail(errno,0); else { capture.owned_files++; store(&capture.phase,WRITE); }
        return 1;
    case WRITE:
    {
        size_t wanted = capture.text_size - capture.text_written;
        ssize_t done;
        if (wanted > WRITE_BYTES) wanted = WRITE_BYTES;
        done = write(capture.descriptor,capture.text+capture.text_written,wanted);
        if (done < 0 && errno == EINTR) return 1;
        if (done <= 0 || (size_t)done > wanted) { fail(done < 0 ? errno : EIO,0); return 1; }
        capture.text_written += (size_t)done;
        if (capture.text_written == capture.text_size) store(&capture.phase,SYNC);
        return 1;
    }
    case SYNC:
        if (fsync(capture.descriptor)!=0) fail(errno,0); else store(&capture.phase,CLOSE);
        return 1;
    case CLOSE:
        if (close(capture.descriptor)!=0) fail(errno,1); else { capture.descriptor=-1; store(&capture.phase,CHECK_FINAL); }
        return 1;
    case RENAME:
        if (rename(capture.part,capture.final)!=0) fail(errno,0); else store(&capture.phase,COMMIT);
        return 1;
    case COMMIT:
        add(&capture.written,load(&capture.batch_records)); add(&capture.files,1); store(&capture.batch_records,0);
        capture.text_size=0; capture.sequence++; store(&capture.phase,IDLE); return 1;
    case FAIL_CLOSE:
        error=close(capture.descriptor); capture.descriptor=-1;
        if (error) add(&capture.close_errors,1);
        store(&capture.phase,FAILED); return 1;
    default:
        fail(EINVAL,0); return 1;
    }
}

int wiiu_missing_keys_service(void *unused)
{
    uint32_t expected = ACCEPTING;
    int work;

    (void)unused;
    if (!load(&capture.initialized)) return 0;
    if (!owner(&capture.consumer)) { add(&capture.rejected_consumer, 1); return 0; }
    if (capture.config.io_allowed && !capture.config.io_allowed(capture.config.context)) return 0;
    if (!__atomic_compare_exchange_n(&capture.io_gate, &expected, ACCEPTING | ACTIVE, 0,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return 0;
    work = service_turn();
    __atomic_fetch_and(&capture.io_gate, ~ACTIVE, __ATOMIC_RELEASE);
    return work;
}
