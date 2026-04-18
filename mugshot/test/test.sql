-- Publish some test endpoints
CREATE LANGUAGE plmuggin;
CREATE SCHEMA web;

CREATE DOMAIN "text/html" AS TEXT;
CREATE DOMAIN "application/json" AS TEXT;

CREATE FUNCTION web."GET/hello"(who text) RETURNS "text/html" AS $$
html
  body
    div.hello
      span Hello {{who}}!
$$ LANGUAGE plmuggin;

CREATE FUNCTION web."GET/add"(a INTEGER, b INTEGER) RETURNS "text/html" AS $$
html
  body(m:q="SELECT {{a}} + {{b}} as c")
    div.add {{a}} + {{b}} = {{c}}
$$ LANGUAGE plmuggin;

CREATE FUNCTION web."GET/:year/:month/:date"(year INTEGER, month INTEGER, "date" INTEGER)
RETURNS "text/html" AS $$
html
  body(m:q="SELECT (make_date({{year}},{{month}},{{date}}) - '1970-01-01'::DATE) as days")
    div Date {{date}}.{{month}}.{{year}} is {{days}} days after the UNIX epoch
$$ LANGUAGE plmuggin;

CREATE TABLE contacts (id SERIAL, email TEXT UNIQUE, name TEXT);

CREATE FUNCTION contact_form() RETURNS "text/html" AS $$
form
  label(for="name") Name:
  input(name="name" type="text)
  label(for="email") Email:
  input(name="email" type="text")
$$ LANGUAGE plmuggin;

CREATE OR REPLACE FUNCTION web."POST/form"(email TEXT, name TEXT) RETURNS "application/json" AS $$
DECLARE
  inserted_id INTEGER;
BEGIN
  INSERT INTO contacts(email, name) values (email, name)
    RETURNING id INTO inserted_id;
  RETURN json_build_object('success', true, 'id', inserted_id);
EXCEPTION
  WHEN unique_violation THEN
    SET mugshot.http_status TO '409';
    RETURN '{"success": false}';
END
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION web."GET/numbers"(lo INTEGER, hi INTEGER) RETURNS "text/html" AS $$
html
  head
    title Numbers from {{lo}} to {{hi}}
  body
    div.numbers(m:q="SELECT generate_series({{lo}},{{hi}}) as n")
      span.number {{n}}
$$ LANGUAGE plmuggin;

GRANT USAGE ON SCHEMA web TO test;
GRANT ALL PRIVILEGES ON contacts TO test;
GRANT ALL PRIVILEGES ON contacts_id_seq TO test;
