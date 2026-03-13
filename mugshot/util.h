#ifndef util_h
#define util_h

#define expect(expr)                                                           \
  if (!(expr))                                                                 \
    return false

#define VA_ARGS(...) , ##__VA_ARGS__

#ifdef DEBUG
#define dbg(fmt, ...)                                                          \
  {                                                                            \
    fprintf(stdout, fmt "\n" VA_ARGS(__VA_ARGS__));                            \
  }
#endif

#ifndef DEBUG
#define dbg(...)
#endif

#define err(fmt, ...)                                                          \
  {                                                                            \
    fprintf(stderr, fmt "\n" VA_ARGS(__VA_ARGS__));                            \
  }



#endif
