-- Snake in PostgreSQL

CREATE EXTENSION plmuggin;
CREATE SCHEMA web;
CREATE SCHEMA game;

CREATE DOMAIN "text/html" AS TEXT;
CREATE DOMAIN "text/css" AS TEXT;

CREATE TYPE direction AS ENUM ('UP', 'RIGHT', 'DOWN', 'LEFT');

CREATE TYPE blocktype AS ENUM ('FOOD', 'SNAKE');

CREATE TABLE snake (
  dir direction,  -- direction snake is headed
  score integer,  -- current score
  frame integer,  -- current frame
  length integer, -- length of snake
  food_x integer, -- position of food x
  food_y integer,  -- position of food y
  game_over boolean
);

CREATE TABLE snake_pos (id serial, x integer, y integer);

-- The main page
CREATE OR REPLACE FUNCTION web."GET/"() RETURNS "text/html" AS $$
html
  head
    script(src="https://cdn.jsdelivr.net/npm/htmx.org@2.0.8/dist/htmx.min.js")
    script(src="https://cdn.jsdelivr.net/npm/hyperscript.org@0.9.91/dist/_hyperscript.min.js")
    link(rel=stylesheet href=/styles)
    meta(name=viewport content="width=device-width, initial-scale=1.0")
    title Snake in PostgreSQL
    style #game { border: solid 15px black; margin: 20px; }
  body(_="on keypress fetch `/kbd/${event.key}` with method:'POST'")
    div.game
      header.game-header
        div.score-panel
          span.score-label Score
          span#score.score-value 0
        div.spacer
        button.new-game-btn(hx-post="/start" hx-target="#game" hx-swap="outerHTML") Play!
      div#game.grid
$$ LANGUAGE plmuggin;

-- Update food
CREATE OR REPLACE FUNCTION game.new_food() RETURNS VOID AS $$
DECLARE
 fx integer;
 fy integer;
 existing_id integer;
BEGIN
 LOOP
   fx := RANDOM(1,39);
   fy := RANDOM(1,39);
   SELECT id INTO existing_id FROM snake_pos WHERE x = fx AND y = fy;
   IF existing_id IS NULL THEN
     UPDATE snake SET food_x = fx, food_y = fy;
     EXIT;
   END IF;
 END LOOP;
END
$$ LANGUAGE plpgsql;

-- Function to get all the game items for render
CREATE OR REPLACE FUNCTION game.entities() RETURNS SETOF RECORD AS $$
DECLARE
  snake_tail integer; snake_head integer;
  cls text;
  r record;
  g_over boolean;
BEGIN
  SELECT min(id), max(id) INTO snake_tail, snake_head FROM snake_pos;
  -- Return all snake positions
  FOR r IN SELECT x, y,
                  LAG(x) OVER w AS px, LAG(y) OVER w AS py,
                  LEAD(x) OVER w as nx, LEAD(y) OVER w as ny
               FROM snake_pos
             WINDOW w AS (ORDER BY id ASC)
             ORDER BY id ASC
  LOOP
    IF r.px IS NULL THEN
      cls := 'snake-tail';
      -- Add border radius based on direction
      IF r.nx > r.x THEN
        cls := cls || ' tl bl'; -- right
      ELSIF r.nx < r.x THEN
        cls := cls || ' tr br'; -- left
      ELSIF r.ny > r.y THEN
        cls := cls || ' tl tr'; -- down
      ELSIF r.ny < r.y THEN
        cls := cls || ' bl br'; -- up
      END IF;
    ELSIF r.nx IS NULL THEN
      cls := 'snake-head';
    ELSE
      cls := 'snake';
    END IF;
    -- add border radius classes when turning
    IF (r.px = r.x AND r.py > r.y) THEN    -- going up
      IF    r.nx < r.x THEN  -- turn left
        cls := cls || ' tr';
      ELSIF r.nx > r.x THEN  -- turn right
        cls := cls || ' tl';
      END IF;
    ELSIF (r.px = r.x AND r.py < r.y) THEN -- going down
      IF r.nx < r.x THEN     -- turn left
        cls := cls || ' br';
      ELSIF r.nx > r.x THEN  -- turn right
        cls := cls || ' bl';
      END IF;
    ELSIF (r.py = r.y AND r.px < r.x) THEN -- going right
      IF r.ny < r.y THEN     -- turn up
        cls := cls || ' br';
      ELSIF r.ny > r.y THEN  -- turn down
        cls := cls || ' tr';
      END IF;
    ELSIF (r.py = r.y AND r.px > r.x) THEN -- going left
      IF r.ny < r.y THEN     -- turn up
        cls := cls || ' bl';
      ELSIF r.ny > r.y THEN  -- turn down
        cls := cls || ' tl';
      END IF;
    END IF;

    RETURN NEXT (r.x * 16, r.y * 16, cls, ''::TEXT);
  END LOOP;

  -- Return the food 
  FOR r IN SELECT food_x * 16, food_y * 16, 'apple'::TEXT, ''::TEXT FROM snake
  LOOP
    RETURN NEXT r;
  END LOOP;

  -- Return score element
  RETURN NEXT (0, 0, 'score'::TEXT,
               ('init put ' || (select (count(id) - 3) * 17 from snake_pos)::text || ' into #score')::TEXT);

  -- If game over, add the overlay
  SELECT game_over INTO g_over FROM snake;
  IF g_over THEN
    RETURN NEXT (160, 160, 'game-over'::TEXT, ''::TEXT);
  END IF;
  
END
$$ LANGUAGE plpgsql;

-- Template to render game grid
CREATE OR REPLACE FUNCTION game.render(hx_trigger TEXT) RETURNS "text/html" AS $$
div#game.grid(hx-post="/tick" hx-trigger="{{hx_trigger}}" hx-swap="outerHTML"
    m:q="SELECT * FROM game.entities() as e (x integer, y integer, type text, hs text)")
  div(_="{{hs}}" class="cell {{type}}" style="top: {{y}}px;left: {{x}}px;")
$$ LANGUAGE plmuggin;

-- Calculate the next frame, returns updated level
CREATE OR REPLACE FUNCTION web."POST/tick"() RETURNS "text/html" AS $$
DECLARE
  d direction;
  at_x integer;
  at_y integer;
  new_x integer;
  new_y integer;
  fd_x integer;
  fd_y integer;
  current_frame integer;
  current_length integer;
  wanted_length integer;
  g_over boolean;
  existing_id integer;
BEGIN
  SELECT dir, frame, length, food_x, food_y, game_over
    FROM snake
    INTO d, current_frame, wanted_length, fd_x, fd_y, g_over;
  IF g_over = true THEN
    RETURN game.render('game-over');
  ELSE
    SELECT length FROM snake INTO wanted_length;
    SELECT COUNT(*) INTO current_length
      FROM snake_pos;
    RAISE NOTICE 'Frame %, length: %', current_frame, current_length;
    -- Unless we need to grow, delete the lowest numbered
    IF wanted_length = current_length THEN
      DELETE FROM snake_pos WHERE id = (SELECT id FROM snake_pos ORDER BY id ASC LIMIT 1);
    END IF;
    -- Create new piece
    SELECT x, y INTO at_x, at_y FROM snake_pos ORDER BY id DESC LIMIT 1;
    IF    d = 'UP'    THEN
      new_x := at_x; new_y = at_y - 1;
    ELSIF d = 'RIGHT' THEN
      new_x := at_x + 1; new_y = at_y;
    ELSIF d = 'DOWN'  THEN
      new_x := at_x; new_y = at_y + 1;
    ELSIF d = 'LEFT'  THEN
      new_x := at_x - 1; new_y = at_y;
    END IF;
    SELECT id INTO existing_id FROM snake_pos WHERE x = new_x AND y = new_y;
    IF (new_x < 0 OR new_x > 39 OR new_y < 0 OR new_y > 39 OR
        existing_id IS NOT NULL) THEN
      UPDATE snake SET game_over = true;
    ELSE
      INSERT INTO snake_pos (x, y) values (new_x, new_y);
      -- Check if we hit the food, if so grow!
      if at_x = fd_x AND at_y = fd_y THEN
        UPDATE snake SET length = length + 1;
        PERFORM game.new_food();
      END IF;
    END IF;
    RETURN game.render('every 0.1s');
  END IF;
END
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION web."POST/start"() RETURNS "text/html" AS $$
BEGIN 
  DELETE FROM snake;
  DELETE FROM snake_pos;
  INSERT INTO snake (dir, score, frame, length, game_over)
  VALUES ('UP', 0, 0, 15, false); -- 3 => 15
  PERFORM game.new_food();
  INSERT INTO snake_pos (x,y)
  VALUES (20, 21), (20, 20);
  RETURN web."POST/tick"();
END  
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION web."POST/kbd/:key"(key TEXT) RETURNS "text/html" AS $$
DECLARE
  new_dir direction;
BEGIN
  <<direction>>
  BEGIN
    IF key = 'w' OR key = 'W' THEN
      new_dir := 'UP';
    ELSIF key = 'd' OR key = 'D' THEN
      new_dir := 'RIGHT';
    ELSIF key = 's' OR key = 'S' THEN
      new_Dir := 'DOWN';
    ELSIF key = 'a' OR key = 'A' THEN
      new_dir := 'LEFT';
    ELSE
      EXIT direction;
    END IF;
    UPDATE snake SET dir = new_dir WHERE game_over = false;
  END;
  SET mugshot.http_status TO '204';
  RETURN 'ok';
END
$$ LANGUAGE plpgsql;


CREATE OR REPLACE FUNCTION web."GET/styles"() RETURNS "text/css" AS $$
BEGIN
RETURN '/* ===== Snake Game Stylesheet ===== */

*, *::before, *::after {
  box-sizing: border-box;
  margin: 0;
  padding: 0;
}

body {
  background-color: #0f1117;
  color: #e0e0e0;
  font-family: ''Segoe UI'', system-ui, sans-serif;
  display: flex;
  justify-content: center;
  align-items: flex-start;
  min-height: 100vh;
  padding: 2rem 1rem;
}

/* ===== Game Container ===== */

.game {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 1rem;
}

/* ===== Header Bar ===== */

.game-header {
  display: flex;
  align-items: center;
  gap: 2rem;
  background: #1a1d27;
  border: 1px solid #2e3247;
  border-radius: 10px;
  padding: 0.75rem 1.5rem;
  width: 100%;
}

/* Score */

.score-panel,
.level-panel {
  display: flex;
  flex-direction: column;
  align-items: center;
  min-width: 64px;
}

.score-label,
.level-label {
  font-size: 0.65rem;
  font-weight: 600;
  letter-spacing: 0.12em;
  text-transform: uppercase;
  color: #6c7a9c;
}

.score-value {
  font-size: 2rem;
  font-weight: 700;
  line-height: 1;
  color: #f0c040;
  font-variant-numeric: tabular-nums;
}

.level-value {
  font-size: 2rem;
  font-weight: 700;
  line-height: 1;
  color: #7ec8e3;
  font-variant-numeric: tabular-nums;
}

/* Spacer between panels and button */

.game-header .spacer {
  flex: 1;
}

/* New Game Button */

.new-game-btn {
  padding: 0.55rem 1.4rem;
  font-size: 0.9rem;
  font-weight: 700;
  letter-spacing: 0.05em;
  text-transform: uppercase;
  border: none;
  border-radius: 6px;
  cursor: pointer;
  background: #3ddc84;
  color: #0f1117;
  transition: background 0.15s ease, transform 0.1s ease, box-shadow 0.15s ease;
  box-shadow: 0 2px 8px rgba(61, 220, 132, 0.35);
}

.new-game-btn:hover {
  background: #57e89b;
  box-shadow: 0 4px 14px rgba(61, 220, 132, 0.5);
}

.new-game-btn:active {
  background: #2bbf6e;
  transform: translateY(1px);
  box-shadow: 0 1px 4px rgba(61, 220, 132, 0.3);
}

/* ===== Game Grid ===== */

.grid {
  /*
   * 40 columns/rows of 16px cells, no gap (stride = 16px).
   * Total = 40 * 16 = 640px.
   * Cells: left = col * 16px, top = row * 16px.
   */
  position: relative;
  width: 640px;
  height: 640px;
  background: #12151e;
  border: 2px solid #2e3247;
  border-radius: 6px;
}

/* ===== Grid Cells (absolutely positioned; only non-empty cells are rendered) ===== */

.cell {
  position: absolute;
  width: 16px;
  height: 16px;
}

/* Apple */

.cell.apple {
  background: #e84040;
  border-radius: 50%;
  box-shadow: 0 0 6px rgba(232, 64, 64, 0.7);
  position: relative;
}

/* subtle apple highlight */
.cell.apple::after {
  content: '''';
  position: absolute;
  top: 2px;
  left: 4px;
  width: 4px;
  height: 3px;
  background: rgba(255, 255, 255, 0.4);
  border-radius: 50%;
}

/* Snake Body */

.cell.snake {
  background: #2da84e;
}

.cell.snake-head {
  background: #4eff8a;
  border-radius: 4px;
  box-shadow: 0 0 8px rgba(78, 255, 138, 0.6);
}

.cell.snake-tail {
  background: #11361b;
}

/* direction based border radiuses for nice corners and ends */
.tl { border-top-left-radius:     66%; }
.tr { border-top-right-radius:    66%; }
.bl { border-bottom-left-radius:  66%; }
.br { border-bottom-right-radius: 66%; }

/* ===== Game Over Overlay ===== */

/* Overlay sits inside .grid (position:relative), covers it fully */
.game-over {
  display: flex;
  position: absolute;
  inset: 0;
  z-index: 10;
  background: rgba(10, 12, 18, 0.55);
  border-radius: 4px;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 1.5rem;
  width: 320px;
  height: 320px;
}

.game-over::after {
  content: ''Game Over!'';
  font-size: 4rem;
  text-align: center;
}

.game.game-over .game-over-overlay {
  display: flex;
}

.game-over-title {
  font-size: 3rem;
  font-weight: 900;
  letter-spacing: 0.1em;
  text-transform: uppercase;
  color: #ff4f4f;
  text-shadow: 0 0 24px rgba(255, 79, 79, 0.7), 0 2px 4px rgba(0,0,0,0.8);
}

.game-over-score {
  font-size: 1rem;
  font-weight: 600;
  color: #9aa3bf;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}
';
END
$$ LANGUAGE plpgsql;

GRANT USAGE ON SCHEMA web TO test;
GRANT USAGE ON SCHEMA game TO test;

GRANT ALL ON TABLE snake TO test;
GRANT ALL ON TABLE snake_pos TO test;
GRANT ALL ON TABLE snake_pos_id_seq TO test;
