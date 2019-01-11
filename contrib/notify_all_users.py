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

from sys import argv
from os import listdir
from subprocess import Popen, TimeoutExpired

if len(argv) < 2 or argv[1] == "-h" or argv[1] == "--help":
    print("Usage:")
    print("  %s [notify-send options] summary [body text]" % (argv[0]))
    print("Examples:")
    print("  %s mytitle mytext" % (argv[0]))
    print("  %s -i dialog-warning earlyoom \"killing process X\"" %
          (argv[0]))
    exit(1)

wait_time = 10

display_env = 'DISPLAY='
dbus_env = 'DBUS_SESSION_BUS_ADDRESS='
user_env = 'USER='


def rline1(path):
    """read 1st line from path."""
    with open(path) as f:
        for line in f:
            return line


def re_pid_environ(pid):
    """
    read environ of 1 process
    returns tuple with USER, DBUS, DISPLAY like follow:
    ('user', 'DISPLAY=:0',
     'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus')
    returns None if these vars is not in /proc/[pid]/environ
    """
    display_env = 'DISPLAY='
    dbus_env = 'DBUS_SESSION_BUS_ADDRESS='
    user_env = 'USER='
    try:
        env = str(rline1('/proc/' + pid + '/environ'))
        if display_env in env and dbus_env in env and user_env in env:
            env_list = env.split('\x00')

            # iterating over a list of process environment variables
            for i in env_list:
                if i.startswith(user_env):
                    user = i
                    continue

                if i.startswith(display_env):
                    display = i[:10]
                    continue

                if i.startswith(dbus_env):
                    dbus = i
                    continue

                if i.startswith('HOME='):
                    # exclude Display Manager's user
                    if i.startswith('HOME=/var'):
                        return None

            env = user.partition('USER=')[2], display, dbus
            return env

    except FileNotFoundError:
        return None
    except ProcessLookupError:
        return None


def root_notify_env():
    """return set(user, display, dbus)"""
    unsorted_envs_list = []
    # iterates over processes, find processes with suitable env
    for pid in listdir('/proc'):
        if pid[0].isdecimal() is False:
            continue
        one_env = re_pid_environ(pid)
        unsorted_envs_list.append(one_env)
    env = set(unsorted_envs_list)
    env.discard(None)

    # deduplicate dbus
    new_env = []
    end = []
    for i in env:
        key = i[0] + i[1]
        if key not in end:
            end.append(key)
            new_env.append(i)
        else:
            continue

    return new_env


list_with_envs = root_notify_env()


# if somebody logged in with GUI
if len(list_with_envs) > 0:
    # iterating over logged-in users
    for i in list_with_envs:
        username, display_env, dbus_env = i[0], i[1], i[2]
        display_tuple = display_env.partition('=')
        dbus_tuple = dbus_env.partition('=')
        display_key, display_value = display_tuple[0], display_tuple[2]
        dbus_key, dbus_value = dbus_tuple[0], dbus_tuple[2]

        with Popen(['sudo', '-u', username,
                    'notify-send', '--icon=dialog-warning',
                    '{}'.format('earlyoom'), '{}'.format(argv[2])
                    ], env={
            display_key: display_value,
            dbus_key: dbus_value
        }) as proc:
            try:
                proc.wait(timeout=wait_time)
            except TimeoutExpired:
                proc.kill()
                print('TimeoutExpired: notify' + username)

else:
    print('Nobody logged-in with GUI. Nothing to do.')
