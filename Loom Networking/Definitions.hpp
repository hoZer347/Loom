#pragma once

#define LOOM_NULL_REQUEST 0
#define LOOM_TEXT_MESSAGE 1
#define LOOM_INFO_MESSAGE 2

#define LOOM_REQUEST_LAST 64

#ifndef MessageType
#define MessageType size_t
#endif

#ifndef MessageSize
#define MessageSize size_t
#endif

#ifndef MAX_CHUNK_SIZE
#define MAX_CHUNK_SIZE 1024
#endif
