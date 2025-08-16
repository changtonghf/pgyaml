# pgyaml
PostgreSQL yaml extension

## installation
```sh
# source code compilation
git clone https://github.com/yaml/libyaml.git
cd libyaml
./bootstrap
./configure
make
sudo make install
# via the official dnf mirror repository
sudo dnf config-manager --set-enabled powertools
sudo dnf install libyaml-devel

git clone https://github.com/changtonghf/pgyaml.git
cd pgyaml
# compile & install & unit-test
make PG_CONFIG=/usr/pgsql-14/bin/pg_config
make PG_CONFIG=/usr/pgsql-14/bin/pg_config install
make PG_CONFIG=/usr/pgsql-14/bin/pg_config check
# uninstall & clean
make PG_CONFIG=/usr/pgsql-14/bin/pg_config uninstall
make PG_CONFIG=/usr/pgsql-14/bin/pg_config clean
```
