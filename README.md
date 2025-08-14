# pgyaml
PostgreSQL yaml extension

## installation
```sh
git clone https://github.com/yaml/libyaml.git
cd libyaml
./bootstrap
./configure
make
sudo make install

git clone https://github.com/changtonghf/pgyaml.git
cd pgyaml
--compile & install & unit-test
make PG_CONFIG=/usr/pgsql-14/bin/pg_config
make PG_CONFIG=/usr/pgsql-14/bin/pg_config install
make PG_CONFIG=/usr/pgsql-14/bin/pg_config check
--uninstall & clean
make PG_CONFIG=/usr/pgsql-14/bin/pg_config uninstall
make PG_CONFIG=/usr/pgsql-14/bin/pg_config clean
```
