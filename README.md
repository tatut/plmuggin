# PL/Muggin HTML templating for PostgreSQL

PL/Muggin is a template engine inspired by [pug](https://pugjs.org) defined as a PostgreSQL language handler.
Muggin templates are whitespace sensitive and look like this:

```
div.app
  ul.items(m:q="SELECT id,label,complete FROM todos")
    li(data-id={{id}})
      input(type=checkbox, checked={{complete}})
      | {{label}}
```

Muggin can directly do PostgreSQL queries and loop through the results.
The `m:` attribute namespace is reserver for muggin controls.
Other attributes are HTML.

WIP! This is work in progress... not for production use yet.

## Installation

PL/Muggin uses PGXS, so it should be installable with just:
```
# to build and install to postgresql
$ make install
...

# to run regression test
$ make installcheck

# create extension and language
$ psql
postgres=# CREATE EXTENTION plmuggin;
postgres=# CREATE LANGUAGE plmuggin;
```

After installation you can use `plmuggin` as a language in `CREATE FUNCTION`.
See the `sql/plmuggin_regress.sql` file for example.
