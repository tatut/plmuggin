CREATE TABLE todos (id SERIAL, label TEXT, complete boolean);
INSERT INTO todos (label,complete) VALUES
         ('regression test', true),
         ('not done', false),
         ('done', true);

-- Return todos info for rendering
-- (Not strictly necessary, but keeps our HTML template tidier)
CREATE OR REPLACE FUNCTION fetch_todos(filter TEXT)
  RETURNS TABLE(id INT, label TEXT, complete BOOLEAN, class TEXT) AS $$
SELECT id, label, complete,
       (CASE WHEN complete
             THEN 'complete'
             ELSE '' END) "class"
  FROM todos
 WHERE (CASE
  WHEN filter = 'all'      THEN TRUE
  WHEN filter = 'complete' THEN complete = TRUE
  WHEN filter = 'active'   THEN complete = FALSE END)
$$ LANGUAGE SQL STABLE;

-- Return data for footer
CREATE OR REPLACE FUNCTION fetch_todos_footer()
  RETURNS TABLE(left INTEGER, label TEXT) AS $$
SELECT COUNT(id),
       (CASE WHEN COUNT(id) = 1
             THEN 'item'
             ELSE 'items' END) label
  FROM todos
 WHERE complete = false;
$$ LANGUAGE SQL STABLE;

-- Create schema whose functions are published via HTTP
CREATE SCHEMA web;

CREATE FUNCTION todomvc_footer() RETURNS TEXT AS $$
footer#footer.footer(m:q="SELECT fetch_todos_footer()")
  span.todo-count
    strong {{left}}
    | {{label}} left
$$ LANGUAGE plmuggin;

-- Create route function
CREATE FUNCTION web."GET/:filter"(filter TEXT) RETURNS TEXT AS $$
html
  head
    meta(charset="UTF-8")
    script(src="https://unpkg.com/@digicreon/mujs@1.4.1/dist/mu.min.js")
    script mu.init();
    link(rel=stylesheet href=todomvc.css)
    title Mugshot TodoMVC
  body
    section.todoapp
      header.header
        h1 Things to do!
        form(action=todo method=post)
          input.new-todo(placeholder="What needs to be done?" autofocus)
    section#app.main
      ul.todo-list(m:q="SELECT * FROM fetch_todos({{filter}})")
        li(class="{{class}}" id="todo{{id}})
          input.toggle(type=checkbox checked="{{complete}}")
          label {{label}}
          button.destroy(mu-url="/todo/{{id}}" mu-mode=remove mu-method=delete
                         mu-target="#todo{{id}}")
    &todomvc_footer()
$$ LANGUAGE plmuggin;

-- Create functions to add/delete/patch
CREATE FUNCTION web."DELETE/todo/:id"(id INTEGER, filter TEXT) RETURNS TEXT AS $$
  -- delete the todo
  DELETE FROM todos WHERE id = id;
  -- return updated footer (mujs removes the row by itself)
  RETURN SELECT todomvc_footer();
$$ LANGUAGE plpgsql;
