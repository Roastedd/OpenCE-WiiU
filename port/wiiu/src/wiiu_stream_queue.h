/* A queue of streams of commands from one producer (the game thread) to one consumer (the GX2 thread): `slots` buffers, of which at most slots - 1 are waiting or being run while the producer
fills the other. Submission numbers count the streams posted, from 1; `retired` is how many the consumer has finished. Words shared by the two threads are read and written with acquire/release
ordering, so that what the producer wrote into a stream is seen by the consumer that takes it, and what the consumer did is seen by whoever sees the stream retired. */
#ifndef WIIU_STREAM_QUEUE_H
#define WIIU_STREAM_QUEUE_H

#include <stdint.h>

#define STREAM_QUEUE_SLOTS_MAX 8

struct stream_queue
{
	uint32_t slots;
	uint32_t posted, retired;
	uint32_t taken; /* the consumer's: streams taken (run or being run) */
	uint32_t used[STREAM_QUEUE_SLOTS_MAX];
};

void stream_queue_init(struct stream_queue *queue, unsigned slots);
/* the producer: the number the stream being written will have, and its slot */
uint32_t stream_queue_submission(const struct stream_queue *queue);
unsigned stream_queue_slot(const struct stream_queue *queue);
/* whether the slot for the stream being written is free (the stream of slots - 1 submissions ago has been retired) */
int stream_queue_slot_free(const struct stream_queue *queue);
/* hands over the stream being written, `used` bytes of it */
void stream_queue_post(struct stream_queue *queue, uint32_t used);
/* the last stream finished */
uint32_t stream_queue_retired(const struct stream_queue *queue);
/* the consumer: the next stream posted, if any (returns 0 if none): its slot, bytes and submission number */
int stream_queue_take(struct stream_queue *queue, unsigned *slot, uint32_t *used, uint32_t *submission);
/* the stream taken last is finished */
void stream_queue_retire(struct stream_queue *queue);

#endif
