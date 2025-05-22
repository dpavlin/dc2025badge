#!/usr/bin/env python

import os
from subprocess import check_output, check_call, CalledProcessError
from datetime import datetime

ver = check_output(["git", "describe", "--always", "--long", "--tags"]).decode("utf-8").strip()
try:
    check_call(["git", "diff", "--quiet", "HEAD"])
except CalledProcessError:
    ver += "+"

dir = os.path.dirname(os.path.abspath(__file__))

instance_id_file = os.path.join(dir, 'instance_id.txt')

try:
    with open(instance_id_file, 'r') as f:
        cont = f.read().strip().split(' ')
        instance_id = int(cont[0])
    if len(cont)==2 and cont[1]=='inc':
        with open(instance_id_file, 'w') as f:
            f.write('{} inc'.format(instance_id+1))

except FileNotFoundError:
    instance_id = 0

with open(os.path.join(dir, 'Core/Inc/instance_id.h'), 'w') as f:
    f.write('#ifndef __INSTANCE_ID_H\n')
    f.write('#define __INSTANCE_ID_H\n')
    f.write('\n')
    f.write('#define __GIT_VERSION "{}"\n'.format(ver))
    f.write('#define __BUILD_DATE "{}"\n'.format(datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
    f.write('\n')

    f.write('#define __INSTANCE_ID {}\n'.format(instance_id))
    f.write('\n')

    f.write('#endif\n')
