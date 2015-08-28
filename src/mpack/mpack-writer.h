/*
 * Copyright (c) 2015 Nicholas Fraser
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * @file
 *
 * Declares the MPack Writer.
 */

#ifndef MPACK_WRITER_H
#define MPACK_WRITER_H 1

#include "mpack-common.h"

#ifdef MPACK_WRITER

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MPACK_WRITE_TRACKING
struct mpack_track_t;
#endif

/**
 * @defgroup writer Write API
 *
 * The MPack Write API encodes structured data of a fixed (hardcoded) schema to MessagePack.
 *
 * @{
 */

/**
 * A buffered MessagePack encoder.
 *
 * The encoder wraps an existing buffer and, optionally, a flush function.
 * This allows efficiently encoding to an in-memory buffer or to a stream.
 *
 * All write operations are synchronous; they will block until the
 * data is fully written, or an error occurs.
 */
typedef struct mpack_writer_t mpack_writer_t;

/**
 * The mpack writer's flush function to flush the buffer to the output stream.
 * It should flag an appropriate error on the writer if flushing fails.
 * Keep in mind that flagging an error may longjmp.
 *
 * The specified context for callbacks is at writer->context.
 */
typedef void (*mpack_flush_t)(mpack_writer_t* writer, const char* buffer, size_t count);

/**
 * A teardown function to be called when the writer is destroyed.
 */
typedef void (*mpack_writer_teardown_t)(mpack_writer_t* writer);

struct mpack_writer_t {
    mpack_flush_t flush;              /* Function to write bytes to the output stream */
    mpack_writer_teardown_t teardown; /* Function to teardown the context on destroy */
    void* context;                    /* Context for writer callbacks */

    char* buffer;         /* Byte buffer */
    size_t size;          /* Size of the buffer */
    size_t used;          /* How many bytes have been written into the buffer */
    mpack_error_t error;  /* Error state */

    #ifdef MPACK_SETJMP
    /* Optional jump target in case of error (pointer because it's
     * very large and may be unused) */
    jmp_buf* jump_env;
    #endif

    #ifdef MPACK_WRITE_TRACKING
    mpack_track_t track; /* Stack of map/array/str/bin/ext writes */
    #endif
};

/**
 * @name Core Writer Functions
 * @{
 */

/**
 * Initializes an mpack writer with the given buffer. The writer
 * does not assume ownership of the buffer.
 *
 * Trying to write past the end of the buffer will result in mpack_error_io unless
 * a flush function is set with mpack_writer_set_flush(). To use the data without
 * flushing, call mpack_writer_buffer_used() to determine the number of bytes
 * written.
 *
 * @param writer The MPack writer.
 * @param buffer The buffer into which to write mpack data.
 * @param size The size of the buffer.
 */
void mpack_writer_init(mpack_writer_t* writer, char* buffer, size_t size);

#ifdef MPACK_MALLOC
/**
 * Initializes an mpack writer using a growable buffer.
 *
 * The data is placed in the given data pointer if and when the writer
 * is destroyed without error. The data should be freed with MPACK_FREE().
 * The data pointer is NULL during writing, and will remain NULL
 * if an error occurs.
 *
 * mpack_error_memory is raised if the buffer fails to grow.
 *
 * @param writer The MPack writer.
 * @param data Where to place the allocated data.
 * @param size Where to write the size of the data.
 */
void mpack_writer_init_growable(mpack_writer_t* writer, char** data, size_t* size);
#endif

/**
 * Initializes an mpack writer directly into an error state. Use this if you
 * are writing a wrapper to mpack_writer_init() which can fail its setup.
 */
void mpack_writer_init_error(mpack_writer_t* writer, mpack_error_t error);

#ifdef MPACK_STDIO
/**
 * Initializes an mpack writer that writes to a file.
 */
void mpack_writer_init_file(mpack_writer_t* writer, const char* filename);
#endif

/**
 * @def mpack_writer_init_stack(writer, flush, context)
 * @hideinitializer
 *
 * Initializes an mpack writer using stack space.
 */

#define mpack_writer_init_stack_line_ex(line, writer) \
    char mpack_buf_##line[MPACK_STACK_SIZE]; \
    mpack_writer_init(writer, mpack_buf_##line, sizeof(mpack_buf_##line))

#define mpack_writer_init_stack_line(line, writer) \
    mpack_writer_init_stack_line_ex(line, writer)

#define mpack_writer_init_stack(writer) \
    mpack_writer_init_stack_line(__LINE__, (writer))

#ifdef MPACK_SETJMP

/**
 * @hideinitializer
 *
 * Registers a jump target in case of error.
 *
 * If the writer is in an error state, 1 is returned when this is called. Otherwise
 * 0 is returned when this is called, and when the first error occurs, control flow
 * will jump to the point where this was called, resuming as though it returned 1.
 * This ensures an error handling block runs exactly once in case of error.
 *
 * A writer that jumps still needs to be destroyed. You must call
 * mpack_writer_destroy() in your jump handler after getting the final error state.
 *
 * The argument may be evaluated multiple times.
 *
 * @returns 0 if the writer is not in an error state; 1 if and when an error occurs.
 * @see mpack_writer_destroy()
 */
#define MPACK_WRITER_SETJMP(writer)                                        \
    (mpack_assert((writer)->jump_env == NULL, "already have a jump set!"), \
	((writer)->error != mpack_ok) ? 1 :                                    \
		!((writer)->jump_env = (jmp_buf*)MPACK_MALLOC(sizeof(jmp_buf))) ?  \
			((writer)->error = mpack_error_memory, 1) :                    \
			(setjmp(*(writer)->jump_env)))

/**
 * Clears a jump target. Subsequent write errors will not cause the writer to
 * jump.
 */
static inline void mpack_writer_clearjmp(mpack_writer_t* writer) {
    if (writer->jump_env)
        MPACK_FREE(writer->jump_env);
    writer->jump_env = NULL;
}
#endif

/**
 * Cleans up the mpack writer, flushing any buffered bytes to the
 * underlying stream, if any. Returns the final error state of the
 * writer in case an error occurred flushing. Causes an assert if
 * there are any unclosed compound types in tracking mode.
 *
 * Note that if a jump handler is set, a writer may jump during destroy if it
 * fails to flush any remaining data. In this case the writer will not be fully
 * destroyed; you can still get the error state, and you must call destroy as
 * usual in the jump handler.
 */
mpack_error_t mpack_writer_destroy(mpack_writer_t* writer);

/**
 * Cleans up the mpack writer, discarding any open writes and unflushed data.
 *
 * Use this to cancel writing in the middle of writing a document (for example
 * in case an error occurred.) This should be used instead of mpack_writer_destroy()
 * because the former will assert in tracking mode if there are any unclosed
 * compound types.
 */
void mpack_writer_destroy_cancel(mpack_writer_t* writer);

/**
 * Sets the custom pointer to pass to the writer callbacks, such as flush
 * or teardown.
 *
 * @param writer The MPack writer.
 * @param context User data to pass to the writer callbacks.
 */
static inline void mpack_writer_set_context(mpack_writer_t* writer, void* context) {
    writer->context = context;
}

/**
 * Sets the flush function to write out the data when the buffer is full.
 *
 * If no flush function is used, trying to write past the end of the
 * buffer will result in mpack_error_io.
 *
 * This should normally be used with mpack_writer_set_context() to register
 * a custom pointer to pass to the flush function.
 *
 * @param writer The MPack writer.
 * @param flush The function to write out data from the buffer.
 */
static inline void mpack_writer_set_flush(mpack_writer_t* writer, mpack_flush_t flush) {
    mpack_assert(writer->size != 0, "cannot use flush function without a writeable buffer!");
    writer->flush = flush;
}

/**
 * Sets the teardown function to call when the writer is destroyed.
 *
 * This should normally be used with mpack_writer_set_context() to register
 * a custom pointer to pass to the teardown function.
 *
 * @param writer The MPack writer.
 * @param teardown The function to call when the writer is destroyed.
 */
static inline void mpack_writer_set_teardown(mpack_writer_t* writer, mpack_writer_teardown_t teardown) {
    writer->teardown = teardown;
}

/**
 * Returns the number of bytes currently stored in the buffer. This
 * may be less than the total number of bytes written if bytes have
 * been flushed to an underlying stream.
 */
static inline size_t mpack_writer_buffer_used(mpack_writer_t* writer) {
    return writer->used;
}

/**
 * Places the writer in the given error state, jumping if a jump target is set.
 *
 * This allows you to externally flag errors, for example if you are validating
 * data as you read it.
 *
 * If the writer is already in an error state, this call is ignored and no jump
 * is performed.
 */
void mpack_writer_flag_error(mpack_writer_t* writer, mpack_error_t error);


/**
 * Queries the error state of the mpack writer.
 *
 * If a writer is in an error state, you should discard all data since the
 * last time the error flag was checked. The error flag cannot be cleared.
 */
static inline mpack_error_t mpack_writer_error(mpack_writer_t* writer) {
    return writer->error;
}

/**
 * Writes a MessagePack object header (an MPack Tag.)
 *
 * If the value is a map, array, string, binary or extension type, the
 * containing elements or bytes must be written separately and the
 * appropriate finish function must be called (as though one of the
 * mpack_start_*() functions was called.)
 */
void mpack_write_tag(mpack_writer_t* writer, mpack_tag_t tag);

/**
 * @}
 */

/**
 * @name Typed Write Functions
 * @{
 */

/*! Writes an 8-bit integer in the most efficient packing available. */
void mpack_write_i8(mpack_writer_t* writer, int8_t value);

/*! Writes a 16-bit integer in the most efficient packing available. */
void mpack_write_i16(mpack_writer_t* writer, int16_t value);

/*! Writes a 32-bit integer in the most efficient packing available. */
void mpack_write_i32(mpack_writer_t* writer, int32_t value);

/*! Writes a 64-bit integer in the most efficient packing available. */
void mpack_write_i64(mpack_writer_t* writer, int64_t value);

/*! Writes an integer in the most efficient packing available. */
static inline void mpack_write_int(mpack_writer_t* writer, int64_t value) {
    mpack_write_i64(writer, value);
}

/*! Writes an 8-bit unsigned integer in the most efficient packing available. */
void mpack_write_u8(mpack_writer_t* writer, uint8_t value);

/*! Writes an 16-bit unsigned integer in the most efficient packing available. */
void mpack_write_u16(mpack_writer_t* writer, uint16_t value);

/*! Writes an 32-bit unsigned integer in the most efficient packing available. */
void mpack_write_u32(mpack_writer_t* writer, uint32_t value);

/*! Writes an 64-bit unsigned integer in the most efficient packing available. */
void mpack_write_u64(mpack_writer_t* writer, uint64_t value);

/*! Writes an unsigned integer in the most efficient packing available. */
static inline void mpack_write_uint(mpack_writer_t* writer, uint64_t value) {
    mpack_write_u64(writer, value);
}

/*! Writes a float. */
void mpack_write_float(mpack_writer_t* writer, float value);

/*! Writes a double. */
void mpack_write_double(mpack_writer_t* writer, double value);

/*! Writes a boolean. */
void mpack_write_bool(mpack_writer_t* writer, bool value);

/*! Writes a boolean with value true. */
void mpack_write_true(mpack_writer_t* writer);

/*! Writes a boolean with value false. */
void mpack_write_false(mpack_writer_t* writer);

/*! Writes a nil. */
void mpack_write_nil(mpack_writer_t* writer);

/**
 * Writes a string.
 *
 * To stream a string in chunks, use mpack_start_str() instead.
 *
 * MPack does not care about the underlying encoding, but UTF-8 is highly
 * recommended, especially for compatibility with JSON.
 */
void mpack_write_str(mpack_writer_t* writer, const char* str, uint32_t length);

/**
 * Writes a binary blob.
 *
 * To stream a binary blob in chunks, use mpack_start_bin() instead.
 */
void mpack_write_bin(mpack_writer_t* writer, const char* data, uint32_t count);

/**
 * Writes an extension type.
 *
 * To stream an extension blob in chunks, use mpack_start_ext() instead.
 *
 * Extension types [0, 127] are available for application-specific types. Extension
 * types [-128, -1] are reserved for future extensions of MessagePack.
 */
void mpack_write_ext(mpack_writer_t* writer, int8_t exttype, const char* data, uint32_t count);

/**
 * Opens an array. count elements should follow, and mpack_finish_array()
 * should be called when done.
 */
void mpack_start_array(mpack_writer_t* writer, uint32_t count);

/**
 * Opens a map. count*2 elements should follow, and mpack_finish_map()
 * should be called when done.
 *
 * Remember that while map elements in MessagePack are implicitly ordered,
 * they are not ordered in JSON. If you need elements to be read back
 * in the order they are written, consider use an array instead.
 */
void mpack_start_map(mpack_writer_t* writer, uint32_t count);

/**
 * Opens a string. count bytes should be written with calls to 
 * mpack_write_bytes(), and mpack_finish_str() should be called
 * when done.
 *
 * To write an entire string at once, use mpack_write_str() or
 * mpack_write_cstr() instead.
 *
 * MPack does not care about the underlying encoding, but UTF-8 is highly
 * recommended, especially for compatibility with JSON.
 */
void mpack_start_str(mpack_writer_t* writer, uint32_t count);

/**
 * Opens a binary blob. count bytes should be written with calls to 
 * mpack_write_bytes(), and mpack_finish_bin() should be called
 * when done.
 */
void mpack_start_bin(mpack_writer_t* writer, uint32_t count);

/**
 * Opens an extension type. count bytes should be written with calls
 * to mpack_write_bytes(), and mpack_finish_ext() should be called
 * when done.
 *
 * Extension types [0, 127] are available for application-specific types. Extension
 * types [-128, -1] are reserved for future extensions of MessagePack.
 */
void mpack_start_ext(mpack_writer_t* writer, int8_t exttype, uint32_t count);

/**
 * Writes a portion of bytes for a string, binary blob or extension type which
 * was opened by one of the mpack_start_*() functions. The corresponding
 * mpack_finish_*() function should be called when done.
 *
 * To write an entire string, binary blob or extension type at
 * once, use one of the mpack_write_*() functions instead.
 *
 * @see mpack_start_str()
 * @see mpack_start_bin()
 * @see mpack_start_ext()
 * @see mpack_finish_str()
 * @see mpack_finish_bin()
 * @see mpack_finish_ext()
 * @see mpack_write_str()
 * @see mpack_write_bin()
 * @see mpack_write_ext()
 */
void mpack_write_bytes(mpack_writer_t* writer, const char* data, size_t count);

#ifdef MPACK_WRITE_TRACKING
/**
 * Finishes writing an array.
 *
 * This will track writes to ensure that the correct number of elements are written.
 */
void mpack_finish_array(mpack_writer_t* writer);

/**
 * Finishes writing a map.
 *
 * This will track writes to ensure that the correct number of elements are written.
 */
void mpack_finish_map(mpack_writer_t* writer);

/**
 * Finishes writing a string.
 *
 * This will track writes to ensure that the correct number of bytes are written.
 */
void mpack_finish_str(mpack_writer_t* writer);

/**
 * Finishes writing a binary blob.
 *
 * This will track writes to ensure that the correct number of bytes are written.
 */
void mpack_finish_bin(mpack_writer_t* writer);

/**
 * Finishes writing an extended type binary data blob.
 *
 * This will track writes to ensure that the correct number of bytes are written.
 */
void mpack_finish_ext(mpack_writer_t* writer);

/**
 * Finishes writing the given compound type.
 *
 * This will track writes to ensure that the correct number of elements
 * or bytes are written.
 */
void mpack_finish_type(mpack_writer_t* writer, mpack_type_t type);
#else
static inline void mpack_finish_array(mpack_writer_t* writer) {MPACK_UNUSED(writer);}
static inline void mpack_finish_map(mpack_writer_t* writer) {MPACK_UNUSED(writer);}
static inline void mpack_finish_str(mpack_writer_t* writer) {MPACK_UNUSED(writer);}
static inline void mpack_finish_bin(mpack_writer_t* writer) {MPACK_UNUSED(writer);}
static inline void mpack_finish_ext(mpack_writer_t* writer) {MPACK_UNUSED(writer);}
static inline void mpack_finish_type(mpack_writer_t* writer, mpack_type_t type) {MPACK_UNUSED(writer); MPACK_UNUSED(type);}
#endif

/**
 * Writes a null-terminated string. (The null-terminator is not written.)
 *
 * MPack does not care about the underlying encoding, but UTF-8 is highly
 * recommended, especially for compatibility with JSON.
 */
void mpack_write_cstr(mpack_writer_t* writer, const char* str);

/**
 * @}
 */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif
#endif

