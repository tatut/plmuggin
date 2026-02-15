CREATE LANGUAGE plmuggin;

CREATE OR REPLACE FUNCTION mug1 (foo TEXT) RETURNS TEXT AS
$$div#app
  span(class="{{foo}}-test") contents
$$ LANGUAGE plmuggin;

SELECT mug1('hello');
