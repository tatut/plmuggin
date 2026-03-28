# Basic test suite for mugshot

This folder contains basic tests for mugshot:
* `test.sql` the schema with templates and other functios to install to PostgreSQL
* `config` configuration file to run mugshot with
* `test.hurl` hurl tests to issue requests and assert responses

This should be run in GitHub Actions.

## Run locally

* Install PL/Muggin `make install` in the repo root folder.
* Load the schema `psql -f mugshot/test/test.sql`
* Compile mugshot: `cd mugshot && make` (in mugshot folder)
* Start mugshot: `./mugshot/mugshot mugshot/test/config &`
* Run hurl test:
