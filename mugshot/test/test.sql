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
