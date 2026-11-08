#pragma once
#include <stdint.h>
/* Tracks completed game-frame submissions, not command-buffer slots or monitor refreshes.
 * The returned serial is the oldest frame that must finish before producing more work.
 * Zero means the initial queue has not filled. Called by the renderer thread only. */
typedef struct NvnFrameQueue { uint64_t serial[3], count; } NvnFrameQueue;
static uint64_t nvn_frame_queue_push(NvnFrameQueue *queue,uint64_t serial,int limit) {
    if(limit<1 || limit>3)return 0;
    queue->serial[queue->count%3]=serial;
    queue->count++;
    return queue->count>=(uint64_t)limit ? queue->serial[(queue->count-limit)%3] : 0;
}
