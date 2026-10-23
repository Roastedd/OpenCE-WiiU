#include "wiiu_stream_queue.h"

void stream_queue_init(struct stream_queue *queue, unsigned slots)
{
	unsigned i;

	queue->slots = slots < 2 ? 2 : slots > STREAM_QUEUE_SLOTS_MAX ? STREAM_QUEUE_SLOTS_MAX : slots;
	queue->posted = queue->retired = queue->taken = 0;
	for (i = 0; i < STREAM_QUEUE_SLOTS_MAX; i++)
		queue->used[i] = 0;
}

uint32_t stream_queue_submission(const struct stream_queue *queue)
{
	return __atomic_load_n(&queue->posted, __ATOMIC_RELAXED) + 1;
}

unsigned stream_queue_slot(const struct stream_queue *queue)
{
	return __atomic_load_n(&queue->posted, __ATOMIC_RELAXED) % queue->slots;
}

int stream_queue_slot_free(const struct stream_queue *queue)
{
	/* (the stream being written is posted + 1 and uses slot posted % slots, which stream posted + 1 - slots used last: it must be retired, so at most slots - 1 are in flight) */
	return __atomic_load_n(&queue->posted, __ATOMIC_RELAXED) - __atomic_load_n(&queue->retired, __ATOMIC_ACQUIRE) < queue->slots;
}

void stream_queue_post(struct stream_queue *queue, uint32_t used)
{
	const uint32_t posted = __atomic_load_n(&queue->posted, __ATOMIC_RELAXED);

	queue->used[posted % queue->slots] = used;
	__atomic_store_n(&queue->posted, posted + 1, __ATOMIC_RELEASE);
}

uint32_t stream_queue_retired(const struct stream_queue *queue)
{
	return __atomic_load_n(&queue->retired, __ATOMIC_ACQUIRE);
}

int stream_queue_take(struct stream_queue *queue, unsigned *slot, uint32_t *used, uint32_t *submission)
{
	if (__atomic_load_n(&queue->posted, __ATOMIC_ACQUIRE) == queue->taken)
		return 0;
	*slot = queue->taken % queue->slots;
	*used = queue->used[*slot];
	*submission = queue->taken + 1;
	queue->taken++;
	return 1;
}

void stream_queue_retire(struct stream_queue *queue)
{
	__atomic_store_n(&queue->retired, __atomic_load_n(&queue->retired, __ATOMIC_RELAXED) + 1, __ATOMIC_RELEASE);
}
