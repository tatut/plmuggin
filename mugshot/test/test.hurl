# Check URL encoded parameter is decoded
GET http://localhost:3000/hello?who=%E2%98%83%20the%20snowman
HTTP 200
[Asserts]
xpath "string(//span)" contains "Hello ☃ the snowman!"

# Check integer parameters work ok
GET http://localhost:3000/add?a=42000&b=69
HTTP 200
[Asserts]
xpath "string(//div)" contains "42000 + 69 = 42069"

# Check that bad parameters cause bad request response
GET http://localhost:3000/add?a=123&b=sixseven
HTTP 400

# Check that missing parameter causes 404
GET http://localhost:3000/add?x=666
HTTP 404

# Check 404
GET http://localhost:3000/nosuchendpoint
HTTP 404

# Check path parametrers
GET http://localhost:3000/1996/07/08
HTTP 200
[Asserts]
xpath "string(//div)" contains "Date 8.7.1996 is 9685 days after the UNIX epoch"

# Extra stuff after parameters gets 404
GET http://localhost:3000/1997/08/29/judgement
HTTP 404

# Bad parameter value also gets bad request (expected integer, got "October")
GET http://localhost:3000/2015/October/21
HTTP 400