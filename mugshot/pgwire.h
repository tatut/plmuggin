#ifndef PGWIRE_H
#define PGWIRE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../str.h"

/* Define some common PostgreSQL type OIDs */
#define OID_BOOL 16
#define OID_INT8 20
#define OID_INT2 21
#define OID_INT4 23
#define OID_TEXT 25
#define OID_OID  26

#define ERR_SIZE 256

typedef enum PgErrorSeverity {
  ERR_NONE = 0,
  ERR_ERROR,
  ERR_FATAL,
  ERR_PANIC
} PgErrorSeverity;

typedef struct PgError {
  char message[ERR_SIZE];
  PgErrorSeverity severity;
} PgError;

typedef struct PgConn {
  char *buf;
  size_t buf_size;
  size_t buf_pos;

  int sockfd;
  int32_t pid;
  int32_t secret_key;

  // If we are expecting some message, but get an error instead
  // keep it here, error_type = 0 when no error
  PgError error;
  bool close; // indicate that connection should be closed due to error
} PgConn;

typedef struct PgMessage {
  char type;
  uint32_t len;
  size_t data; // pointer in buffer
  size_t read; // pointer for reading fields that increments, initially same as data
} PgMessage;

typedef struct PgResult {
  bool success;
  size_t row_description_start; // row description payload start at buffer position
  short fields; // 0 if no data
  int row_start; // data row start (-1 if not read yet)
} PgResult;

typedef struct PgRow {
  bool success, has_row;
  int row_start; // place in the buffer where this row starts
} PgRow;

typedef struct PgVal {
  bool success, is_null;
  size_t len;
  size_t buf_pos; // the position in the buffer where this data starts
} PgVal;

/* Array header info */
typedef struct PgArray {
  // 20 bytes
  int32_t ndim, has_null, element_type, dim_size, lower_bound;
  char *data; // data pointer after the header
} PgArray;

PgConn *pg_connect(str conn_info);
void pg_close(PgConn *conn);

/* Clear all memory buffers. All results and rows are invalid. */
void pg_clear(PgConn *conn);

/* Put a query to the output buffefr without syncing or reading result.
 * This can be used to send multiple queries before syncing.
 */
bool pg_put_query(PgConn *c, const char* sql, int num_params, int *param_oids,
                  str *param_data);

/* Put sync message and send current output buffer */
bool pg_sync(PgConn *c);

/* Expect ready message */
bool pg_ready(PgConn *c);

/* Read the result of a previously sent query */
PgResult pg_read_result(PgConn *c);

/* Issue a query and return a result object that describes the row.
 * The result object does not contain the rows, use pg_next_row to get
 * the rows.
 * The result object is valid until the next call to pg_query (which reuses
 * the memory buffers) or until pg_clear is called.
 */
PgResult pg_query(PgConn *c, const char* sql, int num_params, int *param_oids,
                  str *param_data);

/* Return OID name for printing purposes.
 * The returned name is not valid after the next call. */
const char *pg_oid_name(int oid);

/* Get oid and name of a field.
 * Name pointer may be invalidated when more data is read.
 * Copy or call this function again.
 */
bool pg_field(PgConn *c, PgResult res, int field_num, int *oid, char **name);

/* Read next row, returns PgRow that contains information about if row
 * was found and success. */
PgRow pg_next_row(PgConn *conn, PgResult *res);

/* Read value of nth field in the given row.
 * Return NULL on error. */
PgVal pg_value(PgConn *c, PgRow *row, int field);

str pg_value_str(PgConn *c, PgVal v);

/* Read the array header */
PgArray pg_value_arr(PgConn *c, PgVal v);

#define MIN_BUFFER_SIZE 4096
#define MIN_BUFFER_INCREASE 8192
#define MAX_BUFFER_INCREASE (5*1024*1024)
#define BUFFER_INCREASE_FACTOR 1.618


/* check that current  buf_pos is at least size away from capacity.
 * if there is no room, realloc the buffer.
 * returns true on success, false if there is no more space and more
 * couldn't be allocated.
 */
bool pg_ensure_buf(PgConn *c, size_t size);

// Macros to write or read from buffer, manipulates the positions
// c is the PgConn*
#define put_ch(c, ch)                                                          \
  {                                                                            \
    if (!pg_ensure_buf(c, 1))                                                  \
      return false;                                                            \
    (c)->buf[(c)->buf_pos++] = ch;                                             \
  }

#define put_i16(c, val)                                                        \
  {                                                                            \
    if (!pg_ensure_buf(c, 2))                                                  \
      return false;                                                            \
    *((int16_t *)&(c)->buf[(c)->buf_pos]) = htons(val);                        \
    (c)->buf_pos += 2;                                                         \
  }

#define put_i32(c, val)                                                        \
  {                                                                            \
    if (!pg_ensure_buf(c, 4))                                                  \
      return false;                                                            \
    *((int32_t *)&(c)->buf[(c)->buf_pos]) = htonl(val);                         \
    (c)->buf_pos += 4;                                                         \
  }

#define put_string(c, string)                                                  \
  {                                                                            \
    size_t _strlen = strlen(string) + 1;                                       \
    if (!pg_ensure_buf(c, _strlen))                                            \
      return false;                                                            \
    memcpy(&(c)->buf[(c)->buf_pos], string, _strlen);                          \
    (c)->buf_pos += _strlen;                                                   \
  }

#define put_str(c, str)                                                        \
  {                                                                            \
    if (!pg_ensure_buf(c, (str).len + 1))                                      \
      return false;                                                            \
    memcpy(&(c)->buf[(c)->buf_pos], (str).data, (str).len);                    \
    (c)->buf_pos += (str).len;                                                 \
    (c)->buf[(c)->buf_pos++] = 0;                                              \
  }

#define put_len_str(c, str)                                                    \
  {                                                                            \
    put_i32(c, (str).len);                                                     \
    if (!pg_ensure_buf(c, (str).len))                                          \
      return false;                                                            \
    memcpy(&(c)->buf[(c)->buf_pos], (str).data, (str).len);                    \
    (c)->buf_pos += (str).len;                                                 \
  }



#define put_len_string(c, string)                                              \
  {                                                                            \
    size_t _strlen = strlen(string);                                           \
    put_i32(c, _strlen);                                                       \
    if (!pg_ensure_buf(c, _strlen))                                            \
      return false;                                                            \
    memcpy(&(c)->buf[(c)->buf_pos], string, _strlen);                          \
    (c)->buf_pos += _strlen;                                                   \
  }

#define mark_len(c) int _len_pos = (c)->buf_pos; (c)->buf_pos += 4;
#define update_len(c)                                                          \
  *((int32_t *)&(c)->buf[_len_pos]) = htonl((c)->buf_pos - _len_pos);


// read from a char pointer, that is incremented after
#define get_i32(c, pos, to)                                                    \
  {                                                                            \
    to = ntohl(*((int32_t *)&(c)->buf[pos]));                                  \
    pos += 4;                                                                  \
  }

#define get_i16(c, pos, to)                                                    \
  {                                                                            \
    to = ntohs(*((int16_t *)&(c)->buf[pos]));                                  \
    pos += 2;                                                                  \
  }

#define get_string(c, pos, to)                                                 \
  {                                                                            \
    to = &(c)->buf[pos];                                                       \
    pos += (strlen(to) + 1);                                                   \
  }




#endif
