# Mugshot - Serve PL/Muggin templates over HTTP

![mugshot logo](https://raw.githubusercontent.com/tatut/plmuggin/main/mugshot/logo.png)

Mugshot is an HTTP server that can serve templates and other PostgreSQL functions over HTTP.
Templates are routed automatically by their name/comment metadata.

## Quick start

1. Install plmuggin extension to your PostgreSQL
1. Run example/snake.sql
1. Compile (`make`)  and start (`mugshot example/snake`)
1. Open http://localhost:3000/
