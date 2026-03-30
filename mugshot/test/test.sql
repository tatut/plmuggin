-- Publish some test endpoints
CREATE LANGUAGE plmuggin;
CREATE SCHEMA web;

CREATE DOMAIN "text/html" AS TEXT;

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
