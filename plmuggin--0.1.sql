\echo Use "CREATE EXTENSION plmuggin" to load this file. \quit

CREATE FUNCTION plmuggin_call_handler() RETURNS language_handler
  AS 'MODULE_PATHNAME' LANGUAGE C;

CREATE TRUSTED LANGUAGE plmuggin
  HANDLER plmuggin_call_handler;

ALTER LANGUAGE plmuggin OWNER TO @extowner@;

COMMENT ON LANGUAGE plmuggin IS 'PL/Muggin HTML templating language';

CREATE FUNCTION plmuggin_get_metadata(template_name TEXT, meta_key TEXT)
RETURNS TEXT AS $$
  SELECT kv[2] as val
    FROM (SELECT regexp_split_to_array(md, E':\\s+') kv
            FROM (SELECT regexp_split_to_table(substr(prosrc, 0, strpos(prosrc, '---')), E'\\n') md
                    FROM pg_proc
                   WHERE proname=template_name
                ORDER BY oid DESC LIMIT 1))
   WHERE kv[1] = meta_key;
$$ LANGUAGE SQL;

CREATE FUNCTION plmuggin_templates() RETURNS SETOF TEXT AS $$
SELECT proname
  FROM pg_proc
 WHERE prolang = (SELECT oid FROM pg_language WHERE lanname = 'plmuggin')
$$ LANGUAGE SQL;
