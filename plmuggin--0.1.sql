\echo Use "CREATE EXTENSION plmuggin" to load this file. \quit

CREATE FUNCTION plmuggin_call_handler() RETURNS language_handler
  AS 'MODULE_PATHNAME' LANGUAGE C;

CREATE TRUSTED LANGUAGE plmuggin
  HANDLER plmuggin_call_handler;

ALTER LANGUAGE plmuggin OWNER TO @extowner@;

COMMENT ON LANGUAGE plmuggin IS 'PL/Muggin HTML templating language';
