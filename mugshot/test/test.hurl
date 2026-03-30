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

# Check that missing parameter also causes bad request
GET http://localhost:3000/add?x=666
HTTP 400

# Check 404
GET http://localhost:3000/nosuchendpoint
HTTP 404

# Check path parametrers
GET http://localhost:3000/2026/04/08
HTTP 200
[Asserts]
xpath "string(//div)" contains "Date 8.4.2026 is 20551 days after the UNIX epoch"