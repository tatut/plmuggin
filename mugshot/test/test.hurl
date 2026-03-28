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
