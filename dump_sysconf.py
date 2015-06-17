#!/usr/bin/python

import sys
import os

if len(sys.argv) != 2:
    print 'Usage: %s <destination-dir>' % sys.argv[0]
    print 'sysconf values are written to <destination-dir>/sysconf/<id>'
    sys.exit(1)
dest = sys.argv[1]

for conf_id in os.sysconf_names.values():
    try:
        value = os.sysconf(conf_id)
    except OSError:
        continue
    open(os.path.join(dest, 'sysconf', str(conf_id)), 'w').write(str(value))
