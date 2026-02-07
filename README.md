# PL/Muggin HTML templating for PostgreSQL

PL/Muggin is a template engine inspired by [pug](https://pugjs.org) defined as a PostgreSQL language handler.
Muggin templates are whitespace sensitive and look like this:

```
div.app
  ul.items(m:for="SELECT * FROM todos", m:as="row")
    li(data-id={{row.id}})
      input(type=checkbox, checked=row.done)
      | row.label
```

Muggin can directly do PostgreSQL queries and loop through the results.
The `m:` attribute namespace is reserver for muggin controls.
Other attributes are HTML.

WIP! This is work in progress... not for production use yet.
