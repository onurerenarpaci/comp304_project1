#!/bin/bash

IP_MSG="$(/snap/bin/curl --no-progress-meter https://icanhazdadjoke.com 2>&1)"
STATUS=$?

ICON="face-smile"

if [ $STATUS -ne 0 ]; then
    MESSAGE="Error Occurred! [ $IP_MSG ]" 
    ICON="dialog-error"
else
    MESSAGE="$IP_MSG" 
fi
/usr/bin/notify-send -t 4000 -i "$ICON" "Ha ha ha" "$MESSAGE"
echo "$MESSAGE" >> /home/parallels/jokes.txt
