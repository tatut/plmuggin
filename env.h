#define LOG_ERROR(...)                                                         \
  do {                                                                         \
    char __error[256];                                                         \
    snprintf(__error, 256, __VA_ARGS__);                                       \
    log_error(__error);                                                        \
  } while (false)


#define LOG_NOTICE(...)                                                        \
  do {                                                                         \
    char __notice[256];                                                        \
    snprintf(__notice, 256, __VA_ARGS__);                                      \
    log_notice(__notice);                                                      \
  } while (false)

#ifdef DEBUG
#define LOG_DEBUG(...)                                                         \
  do {                                                                         \
    char __debug[256];                                                         \
    snprintf(__debug, 256, __VA_ARGS__);                                       \
    log_debug(__debug);                                                        \
  } while (false)
#endif

#ifndef DEBUG
#define LOG_DEBUG(...) // nop
#endif

#ifdef TRACE
#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STRING STRINGIZE(__LINE__)

#define LOG_TRACE(...)                                                         \
  do {                                                                         \
    char __trace[512];                                                         \
    snprintf(__trace, 512,                                                     \
             "["__FILE__                                                       \
             ":" LINE_STRING "] "__VA_ARGS__);                                 \
    log_trace(__trace);                                                        \
  } while (false)
#endif

#ifndef TRACE
#define LOG_TRACE(...) // nop
#endif


void log_error(const char *error);
void log_notice(const char *notice);
void log_debug(const char *debug);
void log_trace(const char *trace);
