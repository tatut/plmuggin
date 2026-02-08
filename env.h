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


void log_error(const char *error);
void log_notice(const char *notice);
