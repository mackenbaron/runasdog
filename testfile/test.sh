#!/bin/bash

fd=$1
put()
{
	str=$@
	declare -i local n=${#str}+1
	printf "$fd:$n:%s\r" "$str"
}

while true
do
	s=$(date)
	put "$s"
	sleep 1
done
