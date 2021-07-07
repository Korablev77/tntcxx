[![Crowdin](https://badges.crowdin.net/tarantool-cpp/localized.svg)](https://crowdin.com)

# Tarantool Tarantool C++ Connector documentation
Part of Tarantool documentation, published to 
https://www.tarantool.io/en/doc/latest/getting_started/getting_started_cxx/

### Create pot files from rst
```bash
python -m sphinx . doc/locale/en -c doc -b gettext
```

### Create/update po from pot files
```bash
sphinx-intl update -p doc/locale/en -d doc/locale -l ru
```

### Build documentation to doc/output
```bash
python -m sphinx . doc/output -c doc
```
