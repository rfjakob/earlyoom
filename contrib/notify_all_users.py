#!/usr/bin/env python3

# This script is part of earlyoom package.

# Send a GUI notification to all logged-in users.

# Written by https://github.com/hakavlad

# Why it was written:
#   https://github.com/rfjakob/earlyoom/issues/72

# Usage:
#   earlyoom -N notify_all_users.py

# Notification that should pop up:
#   earlyoom
#   Low memory! Killing process 2233 tail

from sys import argv
from os import listdir
from subprocess import Popen, TimeoutExpired


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
    try:
        env = str(rline1('/proc/' + pid + '/environ'))
        if display_env in env and dbus_env in env and user_env in env:
            env_list = env.split('\x00')

            # iterating over a list of process environment variables
            for i in env_list:
                if i.startswith(user_env):
                    user = i
                    if user == 'root':
                        return None
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


if len(argv) != 5:
    print('Invalid input.')
    print('Usage:')
    print('    earlyoom -N notify_all_users.py')
    exit(1)


# This script should be executed with euid=0
try:
    rline1('/proc/1/environ')
except PermissionError:
    print('notify_all_users.py: PermissionError')
    exit(1)


wait_time = 10


display_env = 'DISPLAY='
dbus_env = 'DBUS_SESSION_BUS_ADDRESS='
user_env = 'USER='


list_with_envs = root_notify_env()


# if somebody logged in with GUI
if len(list_with_envs) > 0:

    # iterating over logged-in users
    for i in list_with_envs:

        username, display_env, dbus_env = i[0], i[1], i[2]
        display_tuple = display_env.partition('=')
        dbus_tuple = dbus_env.partition('=')
        display_value = display_tuple[2]
        dbus_value = dbus_tuple[2]

        cmd = [
            'sudo', '-u', username,
            'env',
            'DISPLAY=' + display_value,
            'DBUS_SESSION_BUS_ADDRESS=' + dbus_value,
            'notify-send'
        ]

        cmd.extend(argv[1:])

        with Popen(cmd) as proc:
            try:
                proc.wait(timeout=wait_time)
            except TimeoutExpired:
                proc.kill()
                print('TimeoutExpired: notify user:' + username)
else:
    print('Nobody logged-in with GUI. Nothing to do.')
