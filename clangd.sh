#!/bin/sh

# Update your .clangd with correct include folder
echo "CompileFlags:\n  Add: [\"-I$(pg_config --includedir-server)\"]" > .clangd
