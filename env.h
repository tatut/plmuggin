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

void log_error(const char *error);
void log_notice(const char *notice);
void log_debug(const char *debug);
