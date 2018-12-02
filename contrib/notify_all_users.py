#!/usr/bin/env python3

# notify all users

# notify_all_users.py --pid 2233 --name tail --signal 15

# output:
# earlyoom
# Low memory! Killing/Terminating process 2233 tail

from argparse import ArgumentParser
from subprocess import Popen, PIPE

parser = ArgumentParser()

parser.add_argument(
    '--signal',
    help="""signal (15 or 9)""",
    default=None,
    type=str
)

parser.add_argument(
    '--pid',
    help="""pid""",
    default=None,
    type=str
)

parser.add_argument(
    '--name',
    help="""process name""",
    default=None,
    type=str
)

args = parser.parse_args()

pid = args.pid
signal = args.signal
name = args.name

if name == None or pid == None or signal == None:
    print('Run script with correct --pid, --name and --signal options.' \
        '\nAlso run script with --help option.')
    exit()

notify_sig_dict = {'9': 'Killing', '15': 'Terminating'}

title = 'earlyoom'

body = 'Low memory! {} process {} {}'.format(
    notify_sig_dict[signal], pid, name.replace('&', '*'))


# return list of tuples with
# username, DISPLAY and DBUS_SESSION_BUS_ADDRESS
def root_notify_env():

    ps_output_list = Popen(['ps', 'ae'], stdout=PIPE
        ).communicate()[0].decode().split('\n')

    lines_with_displays = []
    for line in ps_output_list:
        if ' DISPLAY=' in line and ' DBUS_SESSION_BUS_ADDRES' \
            'S=' in line and ' USER=' in line:
            lines_with_displays.append(line)

    # list of tuples with needments
    deus = []
    for i in lines_with_displays:
        for i in i.split(' '):
            if i.startswith('USER='):
                user = i.strip('\n').split('=')[1]
                continue
            if i.startswith('DISPLAY='):
                disp_value = i.strip('\n').split('=')[1][0:2]
                disp = 'DISPLAY=' + disp_value
                continue
            if i.startswith('DBUS_SESSION_BUS_ADDRESS='):
                dbus = i.strip('\n')
        deus.append(tuple([user, disp, dbus]))

    # unique list of tuples
    vult = []
    for user_env_tuple in set(deus):
        vult.append(user_env_tuple)

    return vult


def send_notify(signal, name, pid):

    b = root_notify_env()

    if len(b) > 0:
        for i in b:
            username, display_env, dbus_env = i[0], i[1], i[2]
            Popen(['sudo', '-u', username, 'env', display_env,
                    dbus_env, 'notify-send', '--icon=dialog-warning',
                    '{}'.format(title), '{}'.format(body)])


send_notify(signal, name, pid)
