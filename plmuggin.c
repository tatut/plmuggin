#include "postgres.h"
#include "catalog/pg_proc_d.h"
#include "fmgr.h"
#include "postgres.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "funcapi.h"
#include "str.h"
#include "utils/elog.h"
#include "utils/fmgrprotos.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/palloc.h"
#include "utils/syscache.h"
#include "utils/hsearch.h"
#include "executor/spi.h"

#include "muggin.h"
#include "env.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(plmuggin_call_handler);

static bool plmuggin_initialized = false;
static HTAB *plmuggin_templates = NULL; // loaded templates

str load_template(str);

void _PG_init(void) {
  HASHCTL hash_ctl;
  if (plmuggin_initialized)
    return;

  hash_ctl.keysize = sizeof(Oid);
  hash_ctl.entrysize = sizeof(m_Template*);
  plmuggin_templates = hash_create("PL/Muggin templates", 128, &hash_ctl,
                                   HASH_ELEM | HASH_BLOBS);

  plmuggin_initialized = true;

}

static Datum plmuggin_func_handler(PG_FUNCTION_ARGS);

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

str load_template(str name) { // need schema name as well?
  Datum values[1];
  Oid argtypes[1];
  int result;

  const char *sql =
    "SELECT prosrc"
    "  FROM pg_proc"
    " WHERE proname = $0"
    " ORDER BY oid DESC LIMIT 1";

  argtypes[0] = TEXTOID;
  values[0] = CStringGetDatum(str_to_cstr(name));
  result = SPI_execute_with_args(sql, 1, argtypes, values, NULL, 1, 1);
  if (result < 1 || !SPI_tuptable || SPI_tuptable->numvals < 1) {
    LOG_ERROR("Failed to get source for template: \"" STR_FMT "\"",
              STR_ARG(name));
    return (str){0};
  } else {
    HeapTuple tuple;
    char *source;
    tuple = SPI_tuptable->vals[0];
    source = SPI_getvalue(tuple, SPI_tuptable->tupdesc, 1);
    LOG_NOTICE("GOT SOURCE:\n---\n%s\n---\n", source);
    return str_from_cstr(source);
  }
}


/* Execute function */
static Datum plmuggin_func_handler(PG_FUNCTION_ARGS) {
  HeapTuple fn_pg_proc;
  Form_pg_proc pl_struct;
  char *proname, *source;
  Datum ret;
  bool isnull;
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
  strbuf *rendered;
  m_Scope *scope;

  // Create string buffer (before SPI_connect) to be in the right ctx
  rendered = strbuf_new();

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

  arg_out_func = (FmgrInfo*) palloc0(fcinfo->nargs * sizeof(FmgrInfo));
  numargs = get_func_arg_info(fn_pg_proc, &argtypes, &argnames, &argmodes);

  /* Load muggin template from source now */
  template = (str){.data = source, .len = strlen(source)};
  if (SPI_connect() != SPI_OK_CONNECT) {
    elog(ERROR, "Can't connect SPI.");
  }

  // PENDING: cache loaded templates
  tpl = muggin_parse(template, load_template);
  if(!tpl) {
    elog(ERROR, "Template loading failed.");
  }
  pfree(source); // FIXME: do we need to free it?

  scope = muggin_scope_new(tpl);
  for(int i=0;i<numargs;i++) {
    // Bind to template names
    Oid argtype;
    str name;

    argtype = pl_struct->proargtypes.values[i];

    name = (str){.data = argnames[i], .len = strlen(argnames[i])};
    muggin_scope_bind(scope, name, argtype, fcinfo->args[i].value, BF_NEED_ESCAPE);
  }
  prorettype = pl_struct->prorettype;
  ReleaseSysCache(fn_pg_proc);

  /*
   * PENDING: for postgrest we need to
   * # CREATE DOMAIN "text/html" AS TEXT;
   * and return that.
   *
   * should check that return type is *effectively* TEXT.
  if(prorettype != TEXTOID) {
    elog(ERROR, "Muggin template functions must always return text");
    PG_RETURN_NULL();
    }
  */

  ret_type_tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(prorettype));
  if(!HeapTupleIsValid(ret_type_tuple))
    elog(ERROR, "return type cache lookup failed %u", prorettype);
  pg_type_entry = (Form_pg_type) GETSTRUCT(ret_type_tuple);
  result_typioparam = getTypeIOParam(ret_type_tuple);

  fmgr_info(pg_type_entry->typinput, &result_in_func);
  ReleaseSysCache(ret_type_tuple);


  muggin_render(tpl, scope, rendered);
  LOG_TRACE("render done for \"%s\", data %p (len %zu)", proname, rendered->str.data, rendered->str.len);
  SPI_finish();
  ret = InputFunctionCall(&result_in_func, rendered->str.data, result_typioparam, -1);
  LOG_TRACE("InputFunctionCall returned %p", ret);
  PG_RETURN_DATUM(ret);

}

void log_error(const char *error) { elog(ERROR, "%s", error); }
void log_notice(const char *notice) { elog(NOTICE, "%s", notice); }
void log_debug(const char *debug) { elog(DEBUG1, "%s", debug); }

void log_trace(const char *trace) {
  FILE *f = fopen("/tmp/plmuggin-trace.log", "a");
  if(f) {
    fprintf(f, "%s\n", trace);
    fclose(f);
  }
}
