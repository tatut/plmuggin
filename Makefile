MODULE_big = plmuggin
OBJS = plmuggin.o muggin.o
EXTENSION = plmuggin
DATA = plmuggin--0.1.sql
PGFILEDESC = "PL/Muggin - muggin HTML templating language"
REGRESS = plmuggin_regress
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
#PG_CFLAGS=-DTRACE
include $(PGXS)
