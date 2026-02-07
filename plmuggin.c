#include "postgres.h"
#include "catalog/pg_proc_d.h"
#include "fmgr.h"
#include "postgres.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "funcapi.h"
#include "utils/fmgrprotos.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/palloc.h"
#include "utils/syscache.h"
#include "muggin.h"
#include "env.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(plmuggin_call_handler);

static Datum plmuggin_func_handler(PG_FUNCTION_ARGS);

/* Define allocator that uses PostgreSQL palloc family */
void *pg_alloc_alloc(Alloc *a, size_t sz);
void pg_alloc_free(Alloc *a, void *mem);
void *pg_alloc_realloc(Alloc *a, void *mem, size_t new_size);

void *pg_alloc_alloc(Alloc *a, size_t sz) { return palloc0(sz); }
void pg_alloc_free(Alloc *a, void *mem) { pfree(mem); }
void *pg_alloc_realloc(Alloc *a, void *mem, size_t new_size) {
  return repalloc(mem, new_size);
}

static Alloc pg_alloc = (Alloc) {
  .alloc = pg_alloc_alloc,
  .free = pg_alloc_free,
  .realloc = pg_alloc_realloc
};


/* plmuggin language handler entrypoint */
Datum plmuggin_call_handler(PG_FUNCTION_ARGS) {
  Datum retval = (Datum) 0;
  PG_TRY();
  {
    retval = plmuggin_func_handler(fcinfo);
  }

  PG_FINALLY();
  {
    // cleanup here
  }

  PG_END_TRY();
  return retval;
}

/* Execute function */
static Datum plmuggin_func_handler(PG_FUNCTION_ARGS) {
  HeapTuple fn_pg_proc;
  Form_pg_proc pl_struct;
  char *proname, *source;
  Datum ret;
  bool isnull;
  volatile MemoryContext proc_ctx = NULL;
  FmgrInfo *arg_out_func;
  int numargs;
  char **argnames, *argmodes;
  Oid *argtypes;
  Form_pg_type type_struct;
  Oid prorettype;
  HeapTuple ret_type_tuple;
  Form_pg_type pg_type_entry;
  Oid result_typioparam;
  FmgrInfo result_in_func;
  m_Template *tpl;
  str template;
  str rendered;

  fn_pg_proc = SearchSysCache1(PROCOID, ObjectIdGetDatum(fcinfo->flinfo->fn_oid));
  if(!HeapTupleIsValid(fn_pg_proc)) {
    elog(ERROR, "function cache lookup failed: %u", fcinfo->flinfo->fn_oid);
  }

  /* Extract the source, this needs to be cached! */
  pl_struct = (Form_pg_proc) GETSTRUCT(fn_pg_proc);
  proname = pstrdup(NameStr(pl_struct->proname));
  ret = SysCacheGetAttr(PROCOID, fn_pg_proc, Anum_pg_proc_prosrc, &isnull);
  if(isnull) {
    elog(ERROR, "Unable to load source for function: %s", proname);
  }

  source = DatumGetCString(DirectFunctionCall1(textout, ret));
  ereport(NOTICE, (errmsg("fn %s source: %s", proname, source)));

  proc_ctx = AllocSetContextCreate(TopMemoryContext, "PL/Muggin function",
                                   ALLOCSET_START_SMALL_SIZES);
  arg_out_func = (FmgrInfo*) palloc0(fcinfo->nargs * sizeof(FmgrInfo));
  numargs = get_func_arg_info(fn_pg_proc, &argtypes, &argnames, &argmodes);

  /* Load muggin template from source now */
  template = (str) {.data = source, .len = strlen(source) };

  tpl = muggin_parse(&pg_alloc, template);
  if(!tpl) {
    elog(ERROR, "Template loading failed.");
  }

  for(int i=0;i<numargs;i++) {
    // Bind to template names
    Oid argtype;
    char *value;
    HeapTuple type_tuple;
    argtype = pl_struct->proargtypes.values[i];
    type_tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(argtype));
    if(!HeapTupleIsValid(type_tuple))
      elog(ERROR, "cache lookup failed for type %u", argtype);

    type_struct = (Form_pg_type) GETSTRUCT(type_tuple);
    fmgr_info_cxt(type_struct->typoutput, &(arg_out_func[i]), proc_ctx);
    ReleaseSysCache(type_tuple);

    value = OutputFunctionCall(&arg_out_func[i], fcinfo->args[i].value);
    ereport(NOTICE, (errmsg("argument: %d; name: %s; value: %s",
                            i, argnames[i], value)));

  }
  prorettype = pl_struct->prorettype;
  ReleaseSysCache(fn_pg_proc);

  if(prorettype != TEXTOID) {
    elog(ERROR, "Muggin template functions must always return text");
    PG_RETURN_NULL();
  }

  ret_type_tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(prorettype));
  if(!HeapTupleIsValid(ret_type_tuple))
    elog(ERROR, "return type cache lookup failed %u", prorettype);
  pg_type_entry = (Form_pg_type) GETSTRUCT(ret_type_tuple);
  result_typioparam = getTypeIOParam(ret_type_tuple);

  fmgr_info_cxt(pg_type_entry->typinput, &result_in_func, proc_ctx);
  ReleaseSysCache(ret_type_tuple);

  muggin_render(tpl, &pg_alloc, &rendered);
  ereport(NOTICE, "rendered template!");
  ret = InputFunctionCall(&result_in_func, rendered.data, result_typioparam, -1);
  PG_RETURN_DATUM(ret);

}

void log_error(const char *error) { ereport(ERROR, error); }
void log_notice(const char *notice) { ereport(NOTICE, notice); }
