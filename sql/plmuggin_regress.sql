CREATE LANGUAGE plmuggin;

CREATE OR REPLACE FUNCTION mug1 (foo TEXT) RETURNS TEXT AS
$$div#app
  span(class="{{foo}}-test") contents
$$ LANGUAGE plmuggin;

SELECT mug1('hello');

-- create table to use in query
CREATE TABLE todos (id SERIAL, label TEXT, complete boolean);
INSERT INTO todos (label,complete) VALUES
  ('regression test', true),
  ('not done', false),
  ('done', true);

CREATE OR REPLACE FUNCTION render_todos() RETURNS TEXT AS
$$div.todos
  ul(m:q="SELECT id,label,complete FROM todos")
    li(data-id="{{id}}" class="todo todo-state-{{complete}}") {{label}}
$$ LANGUAGE plmuggin;

select render_todos();
