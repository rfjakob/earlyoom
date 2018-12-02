#!/usr/bin/env python3
#
# Send a GUI notification to all logged-in users.
# Written by https://github.com/hakavlad .
#
# Example:
#   ./notify_all_users.py earlyoom "Low memory! Killing/Terminating process 2233 tail"
#
# Notification that should pop up:
#   earlyoom
#   Low memory! Killing/Terminating process 2233 tail

import sys
import subprocess

if len(sys.argv) < 2 or sys.argv[1] == "-h" or sys.argv[1] == "--help":
    print("Usage:")
    print("  %s [notify-send options] summary [body text]" % (sys.argv[0]))
    print("Examples:")
    print("  %s mytitle mytext" % (sys.argv[0]))
    print("  %s -i dialog-warning earlyoom \"killing process X\"" %
          (sys.argv[0]))
    exit(1)


def root_notify_env():
    """
    return list of tuples with
    username, DISPLAY and DBUS_SESSION_BUS_ADDRESS
    """
    ps_output_list = subprocess.Popen(['ps', 'ae'], stdout=subprocess.PIPE
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


def send_notify(args):
    b = root_notify_env()

    for i in b:
        username, display_env, dbus_env = i[0], i[1], i[2]
        cmdline = ['sudo', '-u', username, 'env', display_env,
                   dbus_env, 'notify-send']
        cmdline.extend(args)
        print("Running notify-send: %r" % (cmdline))
        subprocess.run(cmdline, check=True, timeout=10)


send_notify(sys.argv[1:])
