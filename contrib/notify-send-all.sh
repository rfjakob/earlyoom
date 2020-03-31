#!/bin/bash

# Requires:
# * libnotify: https://gitlab.gnome.org/GNOME/libnotify
# * systemd with logind and dbus: https://systemd.io/
# * jq: https://stedolan.github.io/jq/
# * GNU coreutils: https://www.gnu.org/software/coreutils/
# * sudo: https://www.sudo.ws/

loginctl --output=json list-sessions | jq --compact-output '.[]' | while read -r session ; do
	read -r sessionid < <(jq -r .session <<< "${session}")
	read -r userid < <(jq -r .uid <<< "${session}")
	read -r username < <(jq -r .user <<< "${session}")
	# FIXME: Use --output=json once https://github.com/systemd/systemd/issues/15275 is fixed
	read -r displayid < <(loginctl show-session "${sessionid}" | sed -nre 's/^Display=//p')
	if [[ "${displayid}" ]] ; then
		sudo -u "${username}" DISPLAY="${displayid}" DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/"${userid}"/bus notify-send "$@"
	fi
done
